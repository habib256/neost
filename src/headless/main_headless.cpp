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
        "  --diskb FILE      monte une image dans le lecteur B (2e lecteur)\n"
        "  --fastfdc         FDC rapide (délais ÷10) — accélère les accès disque\n"
        "  --loopback        « branche » le connecteur de bouclage RS232 (test S série)\n"
        "  --cart FILE       monte une cartouche ($FA0000) : Test Kit diagnostic, etc.\n"
        "  --screenshot PPM  dump du framebuffer final au format PPM\n"
        "  rom               image TOS (défaut rom/etos192fr.img)\n");
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
} // namespace

int main(int argc, char** argv) {
    int         frames     = 200;
    std::string tracePath;
    std::string shotPath;
    std::string diskPath   = "disks/diskA.st";
    std::string diskBPath;                       // lecteur B (optionnel, --diskb)
    bool        fastFdc    = false;   // FDC rapide (--fastfdc) : délais commande/transfert ÷10
    std::string romPath    = "rom/etos192us.img";
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

    machine.reset();

    // Joystick maintenu (--joy) : pose l'état hôte sur l'IKBD (lu aux interrogations
    // $16 et au report auto $14). Constant pour tout le run — utile pour piloter un
    // jeu (« tient le feu/une direction ») ou valider le chemin de report joystick.
    if (haveJoy) {
        machine.ikbd.setJoystick(joy0Hold, joy1Hold);
        std::fprintf(stderr, "[headless] joystick maintenu : port1=$%02X port0=$%02X\n",
                     joy1Hold, joy0Hold);
    }

    // Exécution déterministe : nombre fixe de trames (pas de Date/random/sleep).
    // Note : --until-pc s'évalue par trame (granularité d'une trame), suffisant
    // pour borner une capture autour d'un point d'intérêt.
    for (int frame = 0; frame < frames; ++frame) {
        machine.runFrame();
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
        auto scancode = [](char c) -> uint8_t {
            static const char* row = "qwertyuiop";   // $10-$19
            static const char* row2 = "asdfghjkl";    // $1E-$26
            static const char* row3 = "zxcvbnm";       // $2C-$32
            c = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
            if (c == '\n' || c == '\r') return 0x1C;   // Entrée
            if (c == ' ') return 0x39;
            for (int i = 0; row[i];  ++i) if (row[i]  == c) return uint8_t(0x10 + i);
            for (int i = 0; row2[i]; ++i) if (row2[i] == c) return uint8_t(0x1E + i);
            for (int i = 0; row3[i]; ++i) if (row3[i] == c) return uint8_t(0x2C + i);
            if (c >= '1' && c <= '9') return uint8_t(0x02 + (c - '1'));
            if (c == '0') return 0x0B;
            return 0;
        };
        auto idle = [&](int n) { for (int i = 0; i < n; ++i) machine.runFrame(); };
        for (char c : keys) {
            const uint8_t sc = scancode(c);
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

    if (!serialOut.empty())
        std::fprintf(stderr, "[headless] port série RS-232 (%zu octets) :\n%s\n",
                     serialOut.size(), serialOut.c_str());

    tracer.close();
    return 0;
}
