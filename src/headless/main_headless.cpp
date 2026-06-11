// =============================================================================
//  main_headless.cpp — NeoST sans interface : exécution déterministe + traces.
//
//  But : produire des journaux d'exécution très précis (trace d'instructions
//  façon MAME, registres, interruptions) pour diff avec une trace MAME, et
//  pouvoir tourner en CI / sans serveur graphique. Aucune dépendance GL/GLFW.
//
//  Exemples :
//    neost-headless --frames 50 --trace trace.txt
//    neost-headless --frames 50 --trace trace.txt --regs --irq
//    neost-headless --until-pc FC0030 --trace -        (trace vers stdout)
//    neost-headless --frames 50 --screenshot screen.ppm
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/Machine.hpp"
#include "core/Tracer.hpp"
#include "m68k.h"

namespace {
void usage() {
    std::printf(
        "Usage: neost-headless [options] [rom]\n"
        "  --frames N        nombre de trames à exécuter (défaut 200, ~4 s ST)\n"
        "  --trace FILE      écrit la trace d'instructions ('-' = stdout)\n"
        "  --regs            ajoute l'état des registres à chaque instruction\n"
        "  --irq             trace aussi les interruptions prises\n"
        "  --until-pc HEX    arrête dès que PC atteint cette adresse (hex)\n"
        "  --cpu CORE        cœur 68000 : musashi (défaut) ou moira\n"
        "  --machine TYPE    profil : st, megast, ste (défaut), megaste\n"
        "  --mem SIZE        ST-RAM : 256k, 512k (défaut), 1m, 2m, 4m\n"
        "  --walk-mouse      après le boot, injecte un mouvement souris + clic (diag)\n"
        "  --keys STR        après le boot, tape STR au clavier (ex. menus de diag)\n"
        "  --joy P1[,P0]     maintient un état joystick (bits haut$01 bas$02 g$04 d$08 feu$80)\n"
        "  --disk FILE       monte une image dans le lecteur A (défaut disks/diskA.st)\n"
        "  --diskb FILE      monte une image dans le lecteur B (2e lecteur)\n"
        "  --fastfdc         FDC rapide (délais ÷10) — accélère les accès disque\n"
        "  --loopback        « branche » le connecteur de bouclage RS232 (test S série)\n"
        "  --cart FILE       monte une cartouche ($FA0000) : Test Kit diagnostic, etc.\n"
        "  --glue-selftest   auto-test de la machine Glue (bordures) puis quitte\n"
        "  --screenshot PPM  dump du framebuffer final au format PPM\n"
        "  rom               image TOS (défaut roms/etos192fr.img)\n");
}

// Dump du framebuffer décodé en PPM binaire (P6) — comparable visuellement.
bool writePpm(const char* path, const uint32_t* px, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        const uint32_t c = px[i];                 // ARGB8888
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((c >> 16) & 0xFF),
            static_cast<unsigned char>((c >> 8)  & 0xFF),
            static_cast<unsigned char>( c        & 0xFF) };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

uint8_t stScancode(char c) {
    switch (c) {
        case '1': return 0x02; case '2': return 0x03;
        case '3': return 0x04; case '4': return 0x05;
        case '5': return 0x06; case '6': return 0x07;
        case '7': return 0x08; case '8': return 0x09;
        case '9': return 0x0A; case '0': return 0x0B;
        case '\n': case '\r': return 0x1C;
        case 'a': case 'A': return 0x1E;
        case 's': case 'S': return 0x1F;
        case 'd': case 'D': return 0x20;
        case 'f': case 'F': return 0x21;
        case 'g': case 'G': return 0x22;
        case 'h': case 'H': return 0x23;
        case 'j': case 'J': return 0x24;
        case 'k': case 'K': return 0x25;
        case 'l': case 'L': return 0x26;
        case 'z': case 'Z': return 0x2C;
        case 'x': case 'X': return 0x2D;
        case 'c': case 'C': return 0x2E;
        case 'v': case 'V': return 0x2F;
        case 'b': case 'B': return 0x30;
        case 'n': case 'N': return 0x31;
        case 'm': case 'M': return 0x32;
        case ' ': return 0x39;
        // Touches spéciales pour piloter des menus (scancodes ST) : flèches, Esc,
        // F1-F3, Tab, Backspace, Delete. Conventions ASCII libres choisies ici.
        case '<': return 0x48;   // flèche HAUT
        case '>': return 0x50;   // flèche BAS
        case '[': return 0x4B;   // flèche GAUCHE
        case ']': return 0x4D;   // flèche DROITE
        case '=': return 0x01;   // Esc
        case '!': return 0x3B;   // F1
        case '@': return 0x3C;   // F2
        case '#': return 0x3D;   // F3
        case '$': return 0x3E;   // F4
        case '%': return 0x3F;   // F5
        case '\t': return 0x0F;  // Tab
        case '^': return 0x0E;   // Backspace
        case '~': return 0x53;   // Delete
        case 'q': case 'Q': return 0x10;
        case 'w': case 'W': return 0x11;
        case 'e': case 'E': return 0x12;
        case 'r': case 'R': return 0x13;
        case 't': case 'T': return 0x14;
        case 'y': case 'Y': return 0x15;
        case 'u': case 'U': return 0x16;
        case 'i': case 'I': return 0x17;
        case 'o': case 'O': return 0x18;
        case 'p': case 'P': return 0x19;
        default: return 0x00;
    }
}
} // namespace

int main(int argc, char** argv) {
    int         frames     = 200;
    std::string tracePath;
    std::string shotPath;
    std::string diskPath   = "disks/diskA.st";
    std::string diskBPath;                       // lecteur B (optionnel, --diskb)
    bool        fastFdc    = false;   // FDC rapide (--fastfdc) : délais commande/transfert ÷10
    std::string romPath    = "roms/etos192us.img";
    std::string cartPath;
    bool        regs       = false;
    bool        irq        = false;
    bool        haveUntil  = false;
    uint32_t    untilPc    = 0;
    bool        walkMouse  = false;
    std::string keys;                 // touches à injecter après le boot (ex. "Z\n")
    bool        haveJoy    = false;   // --joy : maintient un état joystick pendant le run
    uint8_t     joy0Hold   = 0, joy1Hold = 0;  // bits ST (haut$01 bas$02 gauche$04 droite$08 feu$80)
    bool        loopback   = false;   // « branche » le connecteur de bouclage RS232 (test S)
    bool        machineMono = false;
    bool        glueSelfTest = false; // auto-test déterministe de la machine Glue (bordures)
    int         shotEvery   = 0;      // --shot-every N : dump une capture toutes les N trames
    std::string shotPrefix;           // --shot-every PREFIX : préfixe des captures périodiques
    int         shotFrom    = 0;      // --shot-from N : ne capture qu'à partir de la trame N
    // Injections DATÉES dans la boucle principale (≠ --keys/--joy qui agissent après/avant) :
    // indispensables pour piloter un menu de démo (intro → menu → déplacement) tout en
    // gardant --shot-every actif (calibration d'étalons, diagnostic scrolling).
    int         keysAtFrame = -1;     // --keys-at N STR : tape STR à partir de la trame N
    std::string keysAt;
    int         joyAtFrame  = -1;     // --joy-at N P1 : pose l'état joystick port 1 à la trame N
    uint8_t     joyAt1      = 0;
    // --mouse-at N "SCRIPT" : pilote la souris (mode REL) à partir de la trame N pour
    // naviguer un menu souris (ex. Vroom). Un token = une trame ; L/R/U/D = déplacement
    // (±8 px), '1' = clic gauche, '2' = clic droit (appui+relâche sur 2 trames), '.' = idle.
    int         mouseAtFrame = -1;
    std::string mouseAt;
    // --joy-script N "SCRIPT" : pose l'état joystick port 1 trame par trame à partir de N.
    // Tokens : U/D/L/R = direction, F = feu, '.' = neutre. Permet de PULSER (presser puis
    // relâcher) le feu et de bouger une sélection dans un menu joystick (ex. Vroom).
    int         joyScrFrame = -1;
    std::string joyScr;
    CpuCore     cpuCore    = CpuCore::Musashi;
    MachineType machType   = MachineType::Ste;
    std::size_t ramBytes   = 512u * 1024u;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s attend un argument\n", opt); std::exit(2); }
            return argv[++i];
        };
        if      (!std::strcmp(a, "--frames"))     frames    = std::atoi(next(a));
        else if (!std::strcmp(a, "--trace"))      tracePath = next(a);
        else if (!std::strcmp(a, "--regs"))       regs      = true;
        else if (!std::strcmp(a, "--irq"))        irq       = true;
        else if (!std::strcmp(a, "--screenshot")) shotPath  = next(a);
        else if (!std::strcmp(a, "--disk"))       diskPath  = next(a);
        else if (!std::strcmp(a, "--diskb"))      diskBPath = next(a);
        else if (!std::strcmp(a, "--fastfdc"))    fastFdc   = true;
        else if (!std::strcmp(a, "--cart"))       cartPath  = next(a);
        else if (!std::strcmp(a, "--walk-mouse")) walkMouse = true;
        else if (!std::strcmp(a, "--keys"))       keys      = next(a);
        else if (!std::strcmp(a, "--joy")) {      // état joystick maintenu : "P1" ou "P1,P0"
            const char* s = next(a);
            joy1Hold = (uint8_t)std::strtoul(s, nullptr, 0);   // port 1 (jeux) en premier
            const char* comma = std::strchr(s, ',');
            joy0Hold = comma ? (uint8_t)std::strtoul(comma + 1, nullptr, 0) : 0;  // port 0 optionnel
            haveJoy = true;
        }
        else if (!std::strcmp(a, "--loopback"))   loopback  = true;
        else if (!std::strcmp(a, "--mono"))       machineMono = true;
        else if (!std::strcmp(a, "--glue-selftest")) glueSelfTest = true;
        else if (!std::strcmp(a, "--shot-every"))  { shotEvery = std::atoi(next(a)); shotPrefix = next(a); }
        else if (!std::strcmp(a, "--shot-from"))   shotFrom = std::atoi(next(a));
        else if (!std::strcmp(a, "--keys-at"))     { keysAtFrame = std::atoi(next(a)); keysAt = next(a); }
        else if (!std::strcmp(a, "--joy-at"))      { joyAtFrame = std::atoi(next(a)); joyAt1 = (uint8_t)std::strtoul(next(a), nullptr, 0); }
        else if (!std::strcmp(a, "--mouse-at"))    { mouseAtFrame = std::atoi(next(a)); mouseAt = next(a); }
        else if (!std::strcmp(a, "--joy-script"))  { joyScrFrame = std::atoi(next(a)); joyScr = next(a); }
        else if (!std::strcmp(a, "--cpu"))        cpuCore   = Cpu68k::parseCore(next(a));
        else if (!std::strcmp(a, "--machine"))    machType  = parseMachine(next(a));
        else if (!std::strcmp(a, "--mem"))        ramBytes  = parseRamBytes(next(a));
        else if (!std::strcmp(a, "--until-pc"))   { untilPc = (uint32_t)std::strtoul(next(a), nullptr, 16); haveUntil = true; }
        else if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-')                     { std::fprintf(stderr, "option inconnue: %s\n", a); usage(); return 2; }
        else                                      romPath   = a;
    }

    // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
    machType = Machine::adjustMachineForTos(machType, romPath);
    Machine machine(ramBytes, cpuCore, machType);
    // Auto-test de la machine Glue (bordures) : pas besoin de ROM/boot, on teste
    // directement la logique du Shifter contre les valeurs documentées d'Hatari.
    if (glueSelfTest) return machine.shifter.glueSelfTest() ? 0 : 1;
    std::fprintf(stderr, "[headless] cœur CPU : %s | machine : %s | RAM : %s\n",
                 Cpu68k::coreName(machine.cpu.core()), machineName(machType), ramLabel(ramBytes));
    if (!machine.loadTos(romPath)) {
        std::fprintf(stderr, "[headless] impossible de charger %s\n", romPath.c_str());
        return 1;
    }
    machine.loadDisk(diskPath);   // lecteur A (optionnel)
    if (!diskBPath.empty()) machine.loadDiskB(diskBPath);   // lecteur B (optionnel)
    machine.fdc.setFastFdc(fastFdc);   // FDC rapide (--fastfdc) : accès disque ÷10
    if (!cartPath.empty()) machine.loadCart(cartPath);   // cartouche $FA0000 (optionnelle)
    machine.mfp.setColorMonitor(!machineMono);   // --mono → moniteur mono (haute rés)

    // Capture du port série (RS-232) : les ROMs de diagnostic y impriment leur
    // rapport. On l'affiche sur stderr en fin d'exécution.
    std::string serialOut;
    machine.mfp.setSerialSink([&serialOut](uint8_t b) { serialOut.push_back(char(b)); });

    Tracer tracer;
    if (!tracePath.empty()) {
        if (!tracer.open(tracePath)) {
            std::fprintf(stderr, "[headless] impossible d'ouvrir la trace %s\n", tracePath.c_str());
            return 1;
        }
        tracer.setLogRegs(regs);
        tracer.setLogInterrupts(irq);
        machine.cpu.setTracer(&tracer);    // active le hook d'instruction
    }

    // Mode déterministe absolu : l'horloge RTC Mega ST(E) doit être constante.
    // Sinon EmuTOS STE affiche l'heure système réelle sur le bureau, ce qui casse
    // la comparaison pixel au pixel (test `etos_ste_boot`).
    machine.rtc.setDateTime(Rtc::DateTime{0, 0, 12, 1, 1, 1, 26}); // 1er jan 2026, 12:00:00
    // Même chose pour l'horloge IKBD (commande $1C) : EmuTOS STE/ST affiche la
    // date/heure du bureau depuis l'IKBD, pas la RTC — figée pour le déterminisme.
    machine.ikbd.setClock(26, 1, 1, 12, 0, 0);

    machine.reset();

    // Joystick maintenu (--joy) : pose l'état hôte sur l'IKBD (lu aux interrogations
    // $16 et au report auto $14). Constant pour tout le run — utile pour piloter un
    // jeu (« tient le feu/une direction ») ou valider le chemin de report joystick.
    if (haveJoy) {
        machine.ikbd.setJoystick(joy0Hold, joy1Hold);
        machine.bus.stePads.setJoystick(joy0Hold, joy1Hold);   // joypads STE ($FF9200/02)
        std::fprintf(stderr, "[headless] joystick maintenu : port1=$%02X port0=$%02X\n",
                     joy1Hold, joy0Hold);
    }

    // Exécution déterministe : nombre fixe de trames (pas de Date/random/sleep).
    // Note : --until-pc s'évalue par trame (granularité d'une trame), suffisant
    // pour borner une capture autour d'un point d'intérêt.
    for (int frame = 0; frame < frames; ++frame) {
        // Injections datées (--keys-at / --joy-at) : pilotage d'un menu de démo en
        // PLEINE boucle (l'intro Cuddly attend espace ; le robot du menu, le stick),
        // sans perdre --shot-every. Une touche = make à +0, break à +2, 4 trames/char.
        if (keysAtFrame >= 0 && frame >= keysAtFrame) {
            const int rel = frame - keysAtFrame;
            const int idx = rel / 4;
            if (idx < (int)keysAt.size()) {
                const uint8_t sc = stScancode(keysAt[idx]);
                if (sc) {
                    if      (rel % 4 == 0) { machine.ikbd.keyEvent(sc, true);  machine.cpu.updateIpl(); }
                    else if (rel % 4 == 2) { machine.ikbd.keyEvent(sc, false); machine.cpu.updateIpl(); }
                }
            }
        }
        if (joyAtFrame >= 0 && frame == joyAtFrame) {
            machine.ikbd.setJoystick(0, joyAt1);
            machine.bus.stePads.setJoystick(0, joyAt1);   // joypads STE ($FF9200/02)
            std::fprintf(stderr, "[headless] joystick posé à la trame %d : port1=$%02X\n", frame, joyAt1);
        }
        // Script souris daté (--mouse-at) : 1 token = 1 trame. Pilote un menu souris.
        if (mouseAtFrame >= 0 && frame >= mouseAtFrame) {
            const int idx = frame - mouseAtFrame;
            if (idx < (int)mouseAt.size()) {
                static bool mClickL = false, mClickR = false;
                const char t = mouseAt[idx];
                int dx = 0, dy = 0; bool l = false, r = false;
                switch (t) {
                    case 'L': dx = -8; break;
                    case 'R': dx =  8; break;
                    case 'U': dy = -8; break;
                    case 'D': dy =  8; break;
                    case '1': l = true; mClickL = true; break;   // clic gauche : appui
                    case '2': r = true; mClickR = true; break;   // clic droit : appui
                    default: break;                              // '.' = idle
                }
                // Maintien d'un clic : si la trame précédente était un appui et celle-ci
                // ne l'est pas, on relâche (paquet bouton=0) pour finir le clic.
                if (t != '1' && mClickL) { l = false; mClickL = false; }
                if (t != '2' && mClickR) { r = false; mClickR = false; }
                machine.ikbd.mouseEvent(dx, dy, l, r);
                machine.cpu.updateIpl();
            }
        }
        // Script joystick daté (--joy-script) : 1 token = 1 trame. Pulse feu / déplace
        // une sélection dans un menu joystick (ex. menu Vroom atteint au feu).
        if (joyScrFrame >= 0 && frame >= joyScrFrame) {
            const int idx = frame - joyScrFrame;
            if (idx < (int)joyScr.size()) {
                uint8_t st = 0;
                switch (joyScr[idx]) {
                    case 'U': st = 0x01; break;
                    case 'D': st = 0x02; break;
                    case 'L': st = 0x04; break;
                    case 'R': st = 0x08; break;
                    case 'F': st = 0x80; break;
                    default:  st = 0x00; break;    // '.' = neutre
                }
                machine.ikbd.setJoystick(0, st);
                machine.bus.stePads.setJoystick(0, st);   // joypads STE ($FF9200/02)
                machine.cpu.updateIpl();
            }
        }
        machine.runFrame();
        if (shotEvery > 0 && frame >= shotFrom && (frame % shotEvery) == 0) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s%05d.ppm", shotPrefix.c_str(), frame);
            writePpm(path, machine.shifter.pixels(),
                     machine.shifter.width(), machine.shifter.height());
        }
        if (haveUntil && machine.cpu.pc() == untilPc) {
            std::fprintf(stderr, "[headless] PC=$%06X atteint à la trame %d\n", untilPc, frame);
            break;
        }
    }

    // Diagnostic souris : après boot, on déplace le pointeur en diagonale et on
    // clique au milieu du parcours, pour voir si le curseur GEM apparaît/bouge.
    if (walkMouse) {
        auto idle   = [&](int frames) { for (int i = 0; i < frames; ++i) machine.runFrame(); };
        auto packet = [&](int dx, int dy, bool l) {
            machine.ikbd.mouseEvent(dx, dy, l, false);
            machine.cpu.updateIpl();
            machine.runFrame();
        };
        // CLIC-GLISSÉ : prendre l'icône Disque A (haut-gauche) et la traîner au centre.
        for (int i = 0; i < 58; ++i) packet(-5, -3, false);  // 1) aller sur Disque A
        idle(5);
        packet(0, 0, true);                                  // 2) appui (bouton bas)
        idle(3);
        for (int i = 0; i < 45; ++i) packet(4, 4, true);     // 3) glisser bouton TENU
        idle(3);
        packet(0, 0, false);                                 // 4) relâcher
        idle(40);
        std::fprintf(stderr, "[headless] séquence : clic-glissé de Disque A vers le centre\n");
    }

    // Injection de touches (pilotage des menus de diagnostic). Table de scancodes
    // ST (jeu « PC/AT » du clavier ST) pour A-Z, 0-9 et Entrée ; on envoie make
    // puis break, avec quelques trames de battement, puis on laisse tourner.
    if (!keys.empty()) {
        auto idle = [&](int n) { for (int i = 0; i < n; ++i) machine.runFrame(); };
        for (char c : keys) {
            const uint8_t sc = stScancode(c);
            if (!sc) continue;
            machine.ikbd.keyEvent(sc, true);  machine.cpu.updateIpl(); idle(2);
            machine.ikbd.keyEvent(sc, false); machine.cpu.updateIpl(); idle(2);
        }
        // « Branche » le connecteur de bouclage RS232 APRÈS la navigation clavier :
        // s'il était branché plus tôt, l'écho du rapport série imprimé en console au
        // boot reviendrait en réception et serait lu comme entrée terminal → le test
        // clavier échouerait. Le technicien le branche juste avant de lancer le test S.
        if (loopback) machine.mfp.setLoopback(true);
        idle(frames);   // laisse les tests déclenchés s'exécuter
        std::fprintf(stderr, "[headless] touches injectées : \"%s\"\n", keys.c_str());
    }

    std::fprintf(stderr, "[headless] %llu instructions tracées\n",
                 (unsigned long long)tracer.instructionCount());
    // Métrique précision cycle : pire retard d'IRQ timer MFP + préemptions du
    // timeslice CPU (cf. Scheduler). Retard faible = quantum « sous la ligne ».
    std::fprintf(stderr, "[headless] timer IRQ retard max = %lld cyc | préemptions = %ld\n",
                 (long long)machine.sched.timerMaxLate, machine.sched.preemptions);
    std::fprintf(stderr, "[headless] vidéo : %dx%d @ %d Hz\n",
                 machine.shifter.width(), machine.shifter.height(), machine.shifter.refreshHz());

    if (!shotPath.empty()) {
        if (writePpm(shotPath.c_str(), machine.shifter.pixels(),
                     machine.shifter.width(), machine.shifter.height()))
            std::fprintf(stderr, "[headless] capture écran → %s (%dx%d)\n",
                         shotPath.c_str(), machine.shifter.width(), machine.shifter.height());
    }

    // --disasm ADDR,LEN : désassemble LEN octets à partir de ADDR (hexa) via Musashi.
    if (const char* da = std::getenv("NEOST_DISASM")) {
        uint32_t addr = 0, len = 0;
        std::sscanf(da, "%x,%x", &addr, &len);
        char buf[256];
        uint32_t pc = addr;
        while (pc < addr + len) {
            unsigned int n = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
            std::fprintf(stderr, "%06X: %s\n", pc, buf);
            pc += n ? n : 2;
        }
    }

    if (!serialOut.empty())
        std::fprintf(stderr, "[headless] port série RS-232 (%zu octets) :\n%s\n",
                     serialOut.size(), serialOut.c_str());

    tracer.close();
    return 0;
}
