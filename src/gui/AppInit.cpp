// =============================================================================
//  AppInit.cpp — tout ce qui se passe AVANT la première trame.
//
//  Ligne de commande, chemins de données, fenêtre GLFW, construction de la
//  Machine, montages (disquettes, cartouche, disques durs), périphériques hôtes
//  (audio, MIDI, réseau), puis Dear ImGui. Rien ici ne se répète : c'est la
//  différence de nature avec AppLoop.cpp, et la raison du découpage.
//
//  Renvoie < 0 pour « la fenêtre est ouverte, on peut boucler », sinon le code de
//  sortie du processus (--help, --version, option inconnue, échec d'ouverture).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Pacing.hpp"
#include "BuildInfo.hpp"       // commit + horodatage de build (--version, hello)
#include "gui/GlHeaders.hpp"   // GLFW + GL, l'inclusion au même endroit pour tous
#include "gui/CrtEffectStack.h"   // passe d'effets CRT (opt-in, façade moniteur)
#include "core/Symbols.hpp"       // table de symboles du débogueur (noms ↔ adresses)
#include <cfloat>                 // FLT_MAX (contrainte de ratio fenêtre écran)
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include <string>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>       // _NSGetExecutablePath (résolution du chemin exécutable)
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX               // sinon windows.h définit min()/max() en MACROS et
#endif                         // casse tous les std::min / std::max du fichier
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN    // pas de winsock/ole : rien d'utile ici, des conflits sûrs
#endif
#include <windows.h>           // GetModuleFileNameW (résolution du chemin exécutable)
#endif

#include "core/Machine.hpp"
#include "net/NetBackend.hpp"
#include "net/SlirpBackend.hpp"
#ifdef NEOST_WITH_NET
#include "net/HayesModem.hpp"
#endif
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "io/JoystickInput.hpp"
#include "io/MediaScan.hpp"
#include "io/DongleTable.hpp"
#include "core/Framing.hpp"
#include "util/HostPath.hpp"   // chemins hôte : UNE définition d'« absolu »
#include "gui/App.hpp"      // état du frontend (ex-globaux g_*)
#include "gui/ConfigWindow.hpp"   // fenêtre Configuration + fenêtre Disquettes
#include "gui/CrtUi.hpp"          // presets et réglages des effets CRT
#include "gui/DebugWindows.hpp"   // hexa, CPU, joystick, débogueur
#include "gui/DockLayout.hpp"     // ancrage + taille de la fenêtre hôte
#include "gui/InputCallbacks.hpp" // callbacks GLFW clavier/souris
#include "gui/JoyMap.hpp"         // manettes hôte → ports ST, par GUID
#include "gui/KioskMenu.hpp"      // menu plein écran de la borne
#include "gui/StScreenView.hpp"   // texture de l'écran ST + cadrages borne/bureau
#include "gui/AppConfig.hpp"
#include "gui/StKeys.hpp"   // neost.cfg : structure, analyse, écriture, profils

namespace fs = std::filesystem;
// Configuration : extraite dans gui/AppConfig (logique pure, donc testable).
// Importée sans qualifier pour que les sites d'appel restent inchangés.
using namespace neost::appconfig;

#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#include "imgui_internal.h"   // gestionnaire de réglages personnalisé (ImGuiSettingsHandler)
#include "imgui_impl_glfw.h"
#include "gui/KeyboardWindow.hpp"
#include "audio/MidiDeviceProfiles.hpp"
#include "audio/MidiEndpoint.hpp"
#include "audio/MidiInHost.hpp"
#include "audio/MidiOutHost.hpp"
#include "audio/Mt32Synth.hpp"
#include "audio/GmSynth.hpp"
#include "imgui_impl_opengl2.h"
#include "gui/UiCommon.hpp"    // pictogrammes Font Awesome + IconButton
#include "gui/MediaPages.hpp"  // pages Disquettes / Cartouche / Disque dur / Réseau
#endif
















// Analyse argv. Renvoie < 0 pour « continuer », sinon le code de sortie : 0 pour
// --help / --version (sortie NORMALE, avant toute fenêtre — c'est ce qui les rend
// testables sans écran, chantier A8), 2 pour une option inconnue. Les arguments
// positionnels restants (ROM, disque) ressortent dans `pos`.
//
// ⚠ Une option inconnue est REFUSÉE, pas ignorée : jusqu'au 2026-08-26 une faute de
// frappe (`--kisok`, `--crt-presets`) partait sans le moindre message et l'utilisateur
// croyait l'avoir activée.
static int parseCommandLine(App& A, int argc, char** argv, std::vector<std::string>& pos) {
    Config& cfg = A.cfg;
    int audioLatencyCli = 0;   // 0 = pas d'override CLI (on garde la valeur du neost.cfg)
    //   --kiosk            : borne en vrai plein écran EXCLUSIF (reste au-dessus de
    //                        tout, ne peut pas être recouvert par une autre fenêtre).
    //   --kiosk-monitor N  : moniteur cible (0 = principal ; défaut 0).
    // Cibles du parseur (--kiosk-monitor, --run-frames, --shot, --scancode-at,
    // --key-hold, --joy-at, --mouse-at) : elles vivent dans App, avec le pourquoi
    // de chacune — cf. gui/App.hpp § « Harnais d'injection ».
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        if (a == "--version") {           // identité de build (release-readiness)
#ifdef NEOST_VERSION
            std::printf("NeoST %s (commit %s, built %s)\n", NEOST_VERSION,
                        neost::build::kCommit, neost::build::kDate);
#else
            std::printf("NeoST (unknown version) (commit %s, built %s)\n",
                        neost::build::kCommit, neost::build::kDate);
#endif
            return 0;
        }
        // --help : sortie AVANT toute création de fenêtre (comme --version), ce qui la
        // rend testable sans écran — c'est la seule surface du GUI que la CI peut
        // exercer aujourd'hui (chantier A8).
        if (a == "--help" || a == "-h") {
            std::printf(
                "NeoST - Atari ST emulator\n"
                "Usage: neost [options] [<rom.img> [<disk.st>]]\n"
                "\n"
                "  --help, -h            this help\n"
                "  --version             build identity\n"
                "  --kiosk               kiosk (arcade cabinet) mode\n"
                "  --kiosk-monitor N     monitor index for kiosk mode\n"
                "  --audio-latency MS    audio cushion in ms (default 85, clamped 20-250;\n"
                "                        raise to 120-150 on a tight machine - an underrun\n"
                "                        costs an audible gap, a little more latency does not)\n"
                "  --crt                 enable CRT effects\n"
                "  --crt-preset NAME     off|light|arcade|phosphor (implies --crt)\n"
                "  --run-frames N        quit after N emulated frames (harness use)\n"
                "  --shot PATH           dump the ST framebuffer as PPM before that exit\n"
                "  --shot-window P N C   dump the RENDERED WINDOW as PPM (P00000.ppm...),\n"
                "                        C frames from emulated frame N (display debugging)\n"
                "  --scancode-at N H     raw ST scancodes (hex, comma-separated) at frame N\n"
                "                        (repeatable; space=39, numeric pad 0-9 =\n"
                "                         70,6d,6e,6f,6a,6b,6c,67,68,69)\n"
                "  --key-hold N          frames a key stays down (default 2)\n"
                "  --joy-at N VAL        HOLD joystick port 1 state VAL from frame N on\n"
                "                        (bits: up$01 down$02 left$04 right$08 fire$80)\n"
                "  --mouse-at N S        mouse script from frame N: L/R/U/D = +/-8 px,\n"
                "                        1/2 = left/right click, . = idle (1 frame each)\n"
                "\n"
                "Without a positional argument, the last ROM from neost.cfg is reloaded\n"
                "(or EmuTOS US). Headless runs: see neost-headless --help.\n");
            return 0;
        }
        if      (a == "--kiosk")           A.kiosk = A.kioskLaunched = true;
        //   --run-frames N : quitte proprement après N trames ÉMULÉES (pas affichées).
        //   --shot PATH    : dump du framebuffer ST en PPM juste avant cette sortie.
        //   Chantier A8 : c'est la brique qui rend le GUI observable par un harnais —
        //   un job CI sous xvfb peut désormais bâtir la cible `neost`, la faire tourner
        //   N trames et comparer la capture, là où le GUI n'était testable que sur ses
        //   arguments. En interactif, permet aussi de capturer l'état exact d'un rapport
        //   utilisateur (« l'écran est cassé à tel moment ») sans outil externe.
        else if (a == "--run-frames" && i + 1 < argc) { A.runFrames = std::atol(argv[++i]); A.harnessRun = true; }
        else if (a == "--shot" && i + 1 < argc)       A.shotPath = argv[++i];
        else if (a == "--shot-window" && i + 3 < argc) {   // PREFIX FROM COUNT
            A.shotWinPrefix = argv[++i];
            A.shotWinFrom   = std::atol(argv[++i]);
            A.shotWinMax    = std::atoi(argv[++i]);
        }
        else if (a == "--key-hold" && i + 1 < argc)   A.keyHold = std::atoi(argv[++i]);
        else if (a == "--scancode-at" && i + 2 < argc) {
            const long f = std::atol(argv[++i]);
            std::vector<uint8_t> sc;
            for (const char* t = argv[++i]; *t; ) {
                sc.push_back((uint8_t)std::strtoul(t, nullptr, 16));
                while (*t && *t != ',') ++t;
                if (*t == ',') ++t;
            }
            A.scanAt.emplace_back(f, std::move(sc));
        }
        else if (a == "--joy-at" && i + 2 < argc) {
            const long f = std::atol(argv[++i]);
            A.joyAt.emplace_back(f, (uint8_t)std::strtoul(argv[++i], nullptr, 0));
        }
        else if (a == "--mouse-at" && i + 2 < argc) {
            const long f = std::atol(argv[++i]);
            A.mouseAt.emplace_back(f, argv[++i]);
        }
        else if (a == "--kiosk-monitor" && i + 1 < argc) A.kioskMonitor = std::atoi(argv[++i]);
        //   --audio-latency MS : coussin audio visé (défaut 85, borné [20,250] par Audio).
        //   Monter à 120-150 sur une machine juste (borne Raspberry Pi) : un underrun coûte
        //   un trou audible le temps de ré-amorcer, une latence un peu plus haute non.
        else if (a == "--audio-latency" && i + 1 < argc) audioLatencyCli = std::atoi(argv[++i]);
        //   --crt              : active les effets CRT (façade moniteur).
        //   --crt-preset NAME  : preset (off|leger|arcade|phosphor) ; implique --crt.
        else if (a == "--crt")             A.crtOn = true;
        else if (a == "--crt-preset" && i + 1 < argc) {
            const std::string name = argv[++i];
            if (!applyCrtPreset(name, A.crtParams, A.crtOn))
                std::fprintf(stderr, "[main] unknown CRT preset: '%s' "
                             "(off|light|arcade|phosphor)\n", name.c_str());
        }
        else if (!a.empty() && a[0] == '-') {
            // ⚠ AVALÉ EN SILENCE jusqu'au 2026-08-26 (chantier A8) : une faute de frappe
            // (`--kisok`, `--crt-presets`) partait sans le moindre message et l'option
            // était simplement ignorée — l'utilisateur croyait l'avoir activée. On refuse
            // maintenant, AVANT d'ouvrir la moindre fenêtre, et on renvoie 2 pour qu'un
            // script le voie.
            std::fprintf(stderr, "[main] unknown option: '%s'\n"
                                 "Try 'neost --help' for the list of options.\n", a.c_str());
            return 2;
        }
        else if (!a.empty()) pos.push_back(a);
    }
    // Les overrides CLI priment sur le cfg (et resteront cohérents si le panneau
    // déclenche un save ultérieur en mode fenêtré).
    cfg.crt = A.crtOn; cfg.crtParams = A.crtParams;
    if (audioLatencyCli > 0) cfg.audioLatencyMs = audioLatencyCli;
    return -1;
}

int appInit(App& A, int argc, char** argv) {
    // Répertoire de l'exécutable (pour retrouver roms/ et disk/ depuis build/).
    // Lancé via le PATH, argv[0] est NU (« neost ») : l'ancien repli « . » faisait
    // écrire neost.cfg/neost.state dans ../ du cwd courant (config « split-brain »).
    // → on résout le vrai chemin : /proc/self/exe (Linux), _NSGetExecutablePath (macOS).
    A.exeDir = [&] {
        std::error_code ec;
#if defined(__linux__)
        const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec && self.has_parent_path()) return self.parent_path().string();
#elif defined(__APPLE__)
        char buf[4096]; uint32_t sz = sizeof buf;
        if (_NSGetExecutablePath(buf, &sz) == 0) {
            const auto self = std::filesystem::canonical(buf, ec);
            if (!ec && self.has_parent_path()) return self.parent_path().string();
        }
#elif defined(_WIN32)
        // GetModuleFileNameW et non argv[0] : lancé depuis le menu Démarrer ou par
        // association de fichier, argv[0] peut être nu, et le cwd est alors celui de
        // l'explorateur — roms/ et neost.cfg seraient cherchés n'importe où.
        // La version W (et non A) : un chemin contenant des accents — « C:\\Users\\Frédéric »
        // — ressort en mojibake avec la version ANSI et aucun fichier n'est trouvé.
        {
            std::wstring buf(MAX_PATH, L'\0');
            for (;;) {
                const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
                if (n == 0) break;                       // échec : on tombera sur argv[0]
                if (n < buf.size()) { buf.resize(n); break; }
                if (buf.size() >= 32768) break;          // borne NT : on abandonne
                buf.resize(buf.size() * 2);              // tronqué : on réessaie plus grand
            }
            if (!buf.empty()) {
                const auto self = std::filesystem::path(buf).lexically_normal();
                if (self.has_parent_path()) return self.parent_path().string();
            }
        }
#endif
        const std::string a0 = argv[0] ? argv[0] : "";
        const auto i = a0.find_last_of('/');
        return (i == std::string::npos) ? std::string(".") : a0.substr(0, i);
    }();
    const std::string& exeDir = A.exeDir;
    // Préférences mémorisées (dernier ROM + type de moniteur).
    A.cfg = loadConfig(exeDir);
    Config& cfg = A.cfg;
    A.cfgPristine = cfg;      // référence figée pour le mode borne (cf. saveConfig)
    A.showHex = cfg.showHex; A.showCpu = cfg.showCpu;
    A.showJoy = cfg.showJoy; A.showCfg = cfg.showCfg; A.showFloppy = cfg.showFloppy;
    A.showKbd = cfg.showKbd;
    // Disposition ancrée : un imgui.ini écrit par une version antérieure garde des
    // nœuds pour des fenêtres qui n'existent plus (Disk/Cart Library) et ne connaît
    // pas la fenêtre Configuration — qui flotterait alors au-dessus de l'écran ST.
    // On resème la disposition UNE fois, puis on note la version dans neost.cfg.
    static constexpr int kUiVersion = 4;
    const bool uiLayoutOutdated = (cfg.uiVersion < kUiVersion);
    cfg.uiVersion = kUiVersion;
    A.dockOn   = cfg.dock;     // mode ancré mémorisé (cf. renderDockSpace)
    A.autoZoom = cfg.autoZoom; // zoom adaptatif de l'écran ST (bureau ET kiosk)
    A.mouseSpeed = cfg.mouseSpeed;   // sensibilité de la souris émulée (page Input)
    A.crtOn    = cfg.crt;      A.crtParams = cfg.crtParams;   // effets CRT (figés en kiosk)
    A.kioskRomDirs = cfg.romDirs;   // dossiers ROM additionnels du menu kiosk (persistés)
    const std::string defRom = cfg.rom.empty() ? std::string("roms/etos192us.img") : cfg.rom;
    // Ligne de commande : arguments POSITIONNELS (ROM, disque) + DRAPEAUX. Le
    // parseur vit à part — c'est la seule surface du frontend qu'un test peut
    // exercer sans écran (--help / --version sortent AVANT la moindre fenêtre).
    std::vector<std::string> pos;
    if (const int rc = parseCommandLine(A, argc, argv, pos); rc >= 0) return rc;
    // Sans argument positionnel, ./neost recharge le dernier ROM (ou EmuTOS US).
    const std::string romLogical = !pos.empty() ? pos[0] : defRom;
    const std::string tosPath  = resolveData(romLogical, exeDir);
    const std::string defDisk  = cfg.disk.empty() ? std::string("disks/diskA.st") : cfg.disk;
    const std::string diskPath = resolveData(pos.size() > 1 ? pos[1] : defDisk, exeDir);
    const std::string cartPath = cfg.cart.empty() ? std::string() : resolveData(cfg.cart, exeDir);
    A.disksDir = resolveData("disks", exeDir);   // dossier pour la Disk Library
    A.loadDongleTable();   // crée disks/dongles.txt (titres connus) dès le premier lancement
    A.cartsDir  = resolveData("carts", exeDir);   // dossier pour la Cart Library
    A.hdDir     = resolveData("hd", exeDir);      // dossier pour la fenêtre Hard Disks
    A.gemdosDir = resolveData("gemdos", exeDir);  // lecteur GEMDOS livré avec le dépôt
    A.romsDir   = resolveData("roms", exeDir);    // dossier pour le sélecteur de ROM

    A.dbgMouse = std::getenv("NEOST_DEBUG_MOUSE") != nullptr;
    A.dbgJoy   = std::getenv("NEOST_DEBUG_JOY")   != nullptr;

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;

    // Pas de hint de profil → contexte legacy compatible (GL 2.1, immediate mode).
    // Fenêtre hôte large : elle héberge la fenêtre ImGui "Atari ST Screen" + le debug.
    GLFWwindow* window = nullptr;
    if (A.kiosk) {
        int nmon = 0; GLFWmonitor** mons = glfwGetMonitors(&nmon);
        // Index borné DES DEUX côtés : --kiosk-monitor -1 passerait la borne haute
        // seule et lirait mons[-1] (pointeur poubelle → crash dans glfwGetVideoMode).
        GLFWmonitor* mon = (mons && A.kioskMonitor >= 0 && A.kioskMonitor < nmon)
                               ? mons[A.kioskMonitor] : glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = glfwGetVideoMode(mon);
        // Vrai plein écran EXCLUSIF : la fenêtre appartient au moniteur → reste
        // au-dessus de TOUT (panneaux/dock inclus), impossible à recouvrir. Hints
        // calés sur le mode courant → aucun changement de résolution.
        if (vm) {
            glfwWindowHint(GLFW_RED_BITS,     vm->redBits);
            glfwWindowHint(GLFW_GREEN_BITS,   vm->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS,    vm->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, vm->refreshRate);
        }
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        window = glfwCreateWindow(vm ? vm->width : 1280, vm ? vm->height : 860,
                                  "NeoST", mon, nullptr);
    } else {
#ifdef NEOST_VERSION
        window = glfwCreateWindow(1280, 860, "NeoST " NEOST_VERSION " — Atari ST", nullptr, nullptr);
#else
        window = glfwCreateWindow(1280, 860, "NeoST — Atari ST", nullptr, nullptr);
#endif
    }
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (A.kiosk) {
        // Curseur masqué + souris capturée d'emblée : les jeux GEM (souris) comme les
        // jeux joystick sont jouables à la borne, sans curseur hôte visible.
        A.mouseCaptured = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    // VSync : évite le tearing hôte (une coupure horizontale rare à une hauteur
    // variable, très visible sur les rasters de Super Hang-On). L'ancien bridage
    // dormait 20 ms APRÈS le swap bloquant et tombait donc à 30-37 fps sur un écran
    // 60 Hz ; ce n'est plus le cas : le sommeil ci-dessous vise une échéance ABSOLUE
    // (`emuNext`) et soustrait implicitement le temps passé dans swapBuffers. Si un
    // écran lent fait malgré tout manquer une échéance, la boucle de rattrapage
    // exécute plusieurs trames avant la présentation suivante, donc le temps émulé
    // et la production audio restent à 50/60/71 Hz.
    glfwSwapInterval(1);

    // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
    const MachineType machType0 = Machine::adjustMachineForTos(parseMachine(cfg.machine), tosPath);
    A.machine = std::make_unique<Machine>(parseRamBytes(cfg.mem), Cpu68k::parseCore(cfg.cpu),
                                          machType0);   // RAM + cœur + modèle (cfg, ajusté au TOS)
    Machine& machine = *A.machine;
    std::fprintf(stderr, "[main] CPU core: %s | machine: %s | RAM: %s\n",
                 Cpu68k::coreName(machine.cpu.core()),
                 machineName(machType0), cfg.mem.c_str());
    if (!machine.loadTos(tosPath))
        std::fprintf(stderr, "[main] Starting without a TOS (the CPU will run on nothing).\n");
    neost::stkeys::setCountryFromTos(machine.bus.rom);    // pays du TOS → surcharges keymap (FR/DE/UK…)
    if (!machine.loadDisk(diskPath))
        std::fprintf(stderr, "[main] No floppy mounted (%s).\n", diskPath.c_str());
    // Lecteur B mémorisé (diskb=) : remonté au démarrage comme le A. Silencieux si
    // l'image a disparu — un second lecteur vide est un état parfaitement normal.
    if (!cfg.diskb.empty() && !machine.fdc.loadImage(resolveData(cfg.diskb, exeDir), 1)) {
        std::fprintf(stderr, "[main] Drive B: image not found (%s).\n", cfg.diskb.c_str());
        cfg.diskb.clear();
    }
    if (!cartPath.empty() && !machine.loadCart(cartPath))
        std::fprintf(stderr, "[main] No cartridge mounted (%s).\n", cartPath.c_str());
    // Disque dur GEMDOS : mappe un dossier hôte sur C: en redirigeant les appels
    // GEMDOS (façon Hatari). Installe une cartouche système à $FA0000 → exclusif
    // avec une cartouche externe. Mémorisé dans neost.cfg (gemdos=) et configurable
    // via le menu Machine → Disque dur ; NEOST_GEMDOS_DIR prime. Cf. io/GemdosHd.hpp
    // et l'option --gemdos du headless.
    if (const char* gd = std::getenv("NEOST_GEMDOS_DIR")) cfg.gemdos = gd;
    if (const char* hd = std::getenv("NEOST_ACSI_IMG"))   cfg.acsi  = hd;
    if (!cfg.gemdos.empty()) {
        if (!cartPath.empty())
            std::fprintf(stderr, "[main] cartridge ignored: incompatible with GEMDOS HD\n");
        if (!machine.gemdos.setDirectory(A.resolvePath(cfg.gemdos)))
            cfg.gemdos.clear();                // dossier invalide → on ne le mémorise pas
    }
    // Disque dur ACSI (image brute, cible 0) : le TOS lit la table de partitions et
    // monte C:/D:… Mémorisé (acsi=) et configurable via le menu ; NEOST_ACSI_IMG
    // prime. Cf. io/Acsi.hpp.
    if (!cfg.acsi.empty()) {
        if (machine.fdc.mountAcsi(A.resolvePath(cfg.acsi)))
            std::fprintf(stderr, "[main] ACSI : %d partition(s)\n", machine.fdc.acsiPartitionCount());
        else cfg.acsi.clear();                 // image invalide → idem
    }
    A.usatanApply();
#ifdef NEOST_WITH_NET
    // Modem Hayes (modem= dans neost.cfg) : commandes AT sur l'USART → pont TCP.
    if (cfg.modem) A.modemApply(true);
#endif
    // EtherNEC (ethernec= dans neost.cfg) : NE2000 sur le port cartouche. Backend
    // par défaut = boucle locale (NetBackendNull une fois un pont SLIRP/pcap
    // absent) : la carte est DÉTECTÉE par le pilote, sans trafic hôte v1.
    A.etherNull = std::make_unique<NetBackendNull>();
    // SLIRP (slirp= dans neost.cfg) : la sortie Internet RÉELLE de la NE2000 (NAT
    // mode utilisateur, cf. --slirp du headless). Un seul point de vérité pour le
    // backend : SLIRP ouvert → SLIRP, sinon boucle locale. La bascule est à chaud —
    // la carte vue par le pilote ST ne change pas, seul son « câble » change.
    A.slirpNet = std::make_unique<SlirpBackend>();
    if (cfg.slirp)    A.slirpApply(true);           // AVANT : les applicateurs lisent A.neBackend()
    if (cfg.netusbee) A.netUsbeeApply(true);
    if (cfg.ethernec) A.etherApply(true);
    machine.midi.setLoopback(cfg.midiLoopback);   // fiche MIDI OUT→IN (OFF par défaut)
    // Clé Steinberg (dongle= dans neost.cfg) : /ROM3, cohabite avec le HD GEMDOS.
    if (!cfg.dongle.empty()) {
        const CartridgeKey::Model dm = cfg.dongle == "cubase2" ? CartridgeKey::Model::Cubase2
                                     : cfg.dongle == "cubase3" ? CartridgeKey::Model::Cubase3
                                     : cfg.dongle == "auto"    ? CartridgeKey::Model::Auto
                                     : cfg.dongle == "notator" ? CartridgeKey::Model::Notator
                                     : CartridgeKey::Model::None;
        if (!machine.setDongle(dm)) {
            A.stateMsg = "Steinberg key needs the cartridge port free (EtherNEC/NetUSBee)";
            A.stateMsgFrames = 150;
            cfg.dongle.clear();
        }
    }
    // Périphériques des ports (joy0= … cartbutton= dans neost.cfg).
    {
        std::string* slots[] = { &cfg.joy0, &cfg.joy1, &cfg.rs232, &cfg.printer, &cfg.cartbutton };
        for (int pi = 0; pi < int(PortDevices::Port::Count); ++pi) {
            if (slots[pi]->empty()) continue;
            const PortDevices::Device d = PortDevices::fromId(slots[pi]->c_str());
            if (d == PortDevices::Device::None || !machine.plugPort(PortDevices::Port(pi), d))
                slots[pi]->clear();   // identifiant inconnu ou connecteur inadapté : on oublie
        }
        machine.psg.setPortBDacGain(cfg.mixDac);
    }
    // Un périphérique que dongles.txt EXPLIQUE pour la disquette montée au démarrage
    // est réputé image-dérivé : on le marque comme auto-branché (sans rien brancher ni
    // débrancher ici) pour qu'un montage à chaud ultérieur le retire. Sans ça, la clé
    // qu'un auto-plug d'une session PRÉCÉDENTE a persistée dans neost.cfg est
    // indiscernable d'un choix de l'utilisateur, et resterait collée au jeu suivant.
    if (cfg.autoDongle && !diskPath.empty()) {
        for (const auto& r : neost::matchDongleRules(A.loadDongleTable(), diskPath)) {
            if (r.cart) { if (machine.dongle.model() == r.key) A.autoCartKey = r.key; }
            else if (machine.ports.at(r.port) == r.dev) A.autoPortDev[int(r.port)] = r.dev;
        }
    }
    // Sortie MIDI hôte : synthé GM intégré (DLSMusicDevice macOS) et/ou port virtuel
    // (CoreMIDI/ALSA) et/ou appareils matériels. Dès qu'une sortie est ouverte,
    // l'ACIA y envoie MIDI OUT (au lieu du bouclage).
    A.midiOut = std::make_unique<MidiOutHost>();
    MidiOutHost& midiOut = *A.midiOut;
    // Entrée MIDI hôte : un appareil branché (clavier maître, groovebox) entre dans
    // le MIDI IN du ST. Drainé une fois par trame, cf. la boucle principale.
    A.midiIn = std::make_unique<MidiInHost>();
    // Roland MT-32/CM-32L (Munt) : rendu DANS la sortie audio, daté au cycle (pas de gigue).
    A.mt32 = std::make_unique<Mt32Synth>();
    Mt32Synth& mt32 = *A.mt32;
    // Synthé GM intégré (TinySoundFont) : même chemin de rendu que le MT-32 — c'est lui
    // qui sert la case « Built-in General MIDI synth » là où macOS a le DLSMusicDevice.
    A.gm = std::make_unique<GmSynth>();
    GmSynth& gm = *A.gm;
    // Fréquence de sortie visée. Ce n'est PAS forcément celle qu'on obtiendra : le
    // périphérique en négocie une (cf. Audio::rate()), et cette lambda s'exécute AVANT
    // que l'objet Audio n'existe. On la corrige juste après audio.start().
    uint32_t& audioRate = A.audioRate;   // 48 kHz par défaut (cf. App.hpp)
    midiOut.setLeadMs(cfg.midiLeadMs);
    A.midiOutApply();
    A.midiInApply();
    if (A.midiLearnUids()) saveConfig(A, exeDir, cfg, &machine);
    machine.mfp.setColorMonitor(!cfg.mono);   // moniteur mémorisé (avant le reset)
    machine.fdc.setFastFdc(cfg.fastfdc);      // FDC rapide mémorisé (accès disque ÷10)
    // Socket MC68881 (Mega STE uniquement, cf. Fpu.hpp) : sonde + trapping.
    machine.bus.setFpuPresent(cfg.fpu && machType0 == MachineType::MegaSte);
    machine.reset();
    loadRtcFromConfig(machine, cfg);                 // horloge Mega : reprise neost.cfg + pont hôte
    cfg.rom = romLogical; saveConfig(A, exeDir, cfg, &machine);

    // Son : un seul périphérique (Audio) mixe le YM2149 ET les bruits mécaniques
    // du lecteur. Le cœur émet des FdcSound, DriveSound joue les WAV de
    // roms/drivesound/ (jeu « epson_smd480l » = vrai lecteur) et Audio les
    // additionne au flux PSG (cf. Audio::render).
    A.drive = std::make_unique<DriveSound>();
    DriveSound& drive = *A.drive;
    // DISPONIBILITÉ (échantillons chargés) et ACTIVATION (réglage drivesound=) sont deux
    // choses : c'est la disponibilité qui décide du câblage — brancher DriveSound sur
    // l'Audio et armer le sink FdcSound — car ce câblage se fait UNE fois, ici. Le
    // réglage, lui, se rebascule à chaud (case de la page Son, chargement d'un profil) ;
    // s'il avait décidé du câblage, démarrer son coupé aurait rendu la case sans effet
    // pour toute la session.
    A.driveSoundAvail = drive.init(resolveData("roms/drivesound/epson_smd480l", exeDir), 48000);
    A.driveSoundOn    = A.driveSoundAvail && cfg.driveSound;
    const bool driveSoundAvail = A.driveSoundAvail;
    bool& driveSoundOn = A.driveSoundOn;
    drive.setEnabled(driveSoundOn);
    A.audio = std::make_unique<Audio>(machine.psg, driveSoundAvail ? &drive : nullptr, &machine.dmasnd);
    Audio& audio = *A.audio;
    audio.setMt32(&mt32);                // Roland MT-32/CM-32L (Munt) mixé dans la sortie
    audio.setGm(&gm);                    // synthé GM intégré (TSF) mixé dans la sortie
    audio.setMixGains(cfg.mixYm, cfg.mixDma, cfg.mixDrive, cfg.mixMt32, cfg.mixGm);   // mixeur (page Sound)
    audio.setLatencyMs(uint32_t(cfg.audioLatencyMs < 0 ? 0 : cfg.audioLatencyMs));  // AVANT start (borné dans Audio)
    audio.start();   // échec silencieux possible (CI / pas de carte son)
    // Le périphérique peut rendre une AUTRE fréquence que les 48 kHz demandés (44,1 kHz
    // sur un matériel qui n'accepte que ça). Le reste de la chaîne suit déjà rate_, mais
    // le MT-32 et les bruits de lecteur avaient été initialisés sur l'hypothèse 48 kHz :
    // eux seuls sortiraient alors désaccordés (~8,8 % à 44,1 kHz) et dériveraient du
    // mixage. On les réaligne — chemin rare, sans effet quand 48 kHz est accordé.
    if (audio.ok() && audio.rate() != audioRate) {
        std::fprintf(stderr, "[Audio] device negotiated %u Hz (not %u) — realigning MT-32 "
                             "and drive sounds\n", audio.rate(), audioRate);
        audioRate = audio.rate();
        if (driveSoundAvail) drive.init(resolveData("roms/drivesound/epson_smd480l", exeDir), audioRate);
        if (mt32.isOpen() || gm.isOpen()) { mt32.close(); gm.close(); A.midiOutApply(); }
    }
    // Sink FdcSound armé SEULEMENT si la sortie audio existe : sans elle, produceFrame ne
    // draine jamais DriveSound et chaque Step/Seek/Index allouait un son miniaudio jamais
    // recyclé (croissance mémoire non bornée sur une longue session).
    if (driveSoundAvail && audio.ok())
        machine.fdc.setSoundSink([&drive](FdcSound e) { drive.onEvent(e); });
    audio.setMasterVolume(cfg.volume);   // volume maître mémorisé (menu Son, neost.cfg)
    // La chaîne DMA/LMC1992 ne s'applique QUE si la machine courante a le son DMA :
    // sur ST/Mega ST le gain de rattrapage LMC (×2, compensation du ½-YM STE)
    // doublait le YM (clipping) et l'état microwire d'une session STE colorait le ST.
    // Prédicat DYNAMIQUE : suit les reconfigure à chaud (applyConfig).
    audio.setDmaGate([&machine] { return machineHasDmaSound(machine.machineType()); });
    // Modèle « push » (Phase C) : on ARME l'horodatage des écritures PSG (cycle CPU dans
    // la trame). Dès lors, write8 enregistre les écritures et la synthèse les rejoue au bon
    // instant (digidrums/sync-buzzer). produceFrame (après runFrame) génère et empile la trame.
    machine.psg.setCycleClock([&machine] { return machine.frameRelCycle(); });
    // Idem pour le son DMA STE : on horodate les transitions PLAY/STOP de la trame, pour
    // les rejouer au cycle exact (bruitages one-shot courts, queues de samples — cf.
    // DmaSound::mixStereo). Posé UNIQUEMENT côté frontend (jamais en headless → pas de fuite).
    machine.dmasnd.setCycleClock([&machine] { return machine.frameRelCycle(); });

    A.screen = std::make_unique<GlScreen>();
    GlScreen& screen = *A.screen;
    screen.init();


    // Callbacks installés AVANT ImGui : ImGui chaîne les nôtres derrière les siens.
    A.ikbd = &machine.ikbd;
    // Émulation joystick clavier : off en mode normal (elle avale les flèches),
    // mais ON d'emblée en KIOSK — une borne se joue au joystick (flèches + Ctrl
    // droit = feu), sans menu pour l'activer. F11 la rebascule si besoin (jeu clavier).
    A.kbdJoy     = A.kiosk;
    A.kbdJoyPort = cfg.joyport;
    A.port0Auto  = (cfg.port0 == "auto");
    A.joyDeadzone = cfg.joydeadzone;    // zone morte des sticks (mémorisée)
    joymapParse(A, cfg.joymap);            // affectation manettes→ports (par GUID)
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    // Glisser-déposer : le seul chemin vers un support qui ne vit PAS dans les dossiers
    // du dépôt, faute de sélecteur de fichiers natif (aucune dépendance à ajouter pour
    // ça). Posé AVANT ImGui_ImplGlfw_InitForOpenGL(.., true) : le backend ImGui chaîne
    // vers le callback déjà installé, alors qu'installer après l'écraserait.
#if defined(NEOST_WITH_IMGUI)
    // Callback GLFW : signature imposée, donc SANS capture (elle doit se convertir
    // en pointeur de fonction) — d'où app() plutôt que le paramètre `A`.
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        for (int i = 0; i < count; ++i)
            if (paths[i]) app().dropped.emplace_back(paths[i]);
    });
#endif

#if defined(NEOST_WITH_IMGUI)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Harnais (--run-frames) : ni lire ni écrire imgui.ini — le run ne dépend pas de
    // la disposition de fenêtres du poste et n'y laisse rien (même règle que le gel
    // de saveConfig). IniFilename=nullptr coupe le chargement ET l'auto-save d'ImGui.
    if (A.harnessRun) ImGui::GetIO().IniFilename = nullptr;
#ifdef IMGUI_HAS_DOCK
    // Ancrage (branche `docking` de Dear ImGui) : les fenêtres de debug deviennent
    // des onglets d'une disposition persistante. Le MULTI-VIEWPORT (l'autre apport
    // de la branche) reste volontairement COUPÉ : il sort les fenêtres du contexte
    // GL de la fenêtre hôte, ce que le backend OpenGL 2 immediate-mode et la passe
    // CRT (FBO liés à ce contexte) ne suivent pas.
    if (A.dockOn) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    // Enregistre la taille de la fenêtre PRINCIPALE dans imgui.ini : gestionnaire de
    // réglages personnalisé, posé AVANT le 1er NewFrame (qui charge imgui.ini et applique
    // la taille relue). Un resize marque les réglages « sales » → ImGui resauvegarde.
    A.window = window;
    registerWindowSettings(A);
    glfwSetWindowSizeCallback(window, [](GLFWwindow*, int, int) {
        if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
    });
    // Police de l'interface : DejaVu Sans (dossier fonts/), nettement plus lisible que
    // la police bitmap intégrée d'ImGui. Doit être chargée AVANT le 1er rendu (l'atlas
    // est construit à la 1re trame). Repli silencieux sur la police par défaut si absente.
    {
        ImGuiIO& io = ImGui::GetIO();
        const std::string fontPath = resolveData("fonts/DejaVuSans.ttf", exeDir);
        if (fileExists(fontPath)) {
            // ⚠ DejaVu Sans occupe une PARTIE de la zone à usage privé (U+F000-F003 =
            // ses ligatures ff/fi/fl/ffi héritées, plus U+F400+) — la MÊME zone où vit
            // Font Awesome. Or ImFontBaked_BuildLoadGlyph parcourt les sources DANS
            // L'ORDRE et retient la PREMIÈRE qui sait fournir le codepoint
            // (imgui_draw.cpp:4590-4610) : la police de base, chargée en premier,
            // gagnait donc contre l'icône fusionnée. Une seule collision en pratique,
            // mais bien visible — **U+F001 = ICON_FA_MUSIC**, la note de la page MIDI,
            // rendue en ligature « fi » illisible à 15 px, donc « l'icône n'apparaît
            // pas ». On EXCLUT la plage FA de la police de texte : le champ existe pour
            // exactement ce cas (« designed to exclude ranges from a font source, when
            // merging fonts with overlapping glyphs », imgui.h:3728). Statique : ImGui
            // garde le POINTEUR jusqu'à la construction de l'atlas.
            static const ImWchar kNoIconRange[] = { 0xF000, 0xF8FF, 0 };
            ImFontConfig baseCfg;
            baseCfg.GlyphExcludeRanges = kNoIconRange;
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f, &baseCfg);
        } else {
            io.Fonts->AddFontDefault();   // base toujours présente (requis avant la fusion FA)
            std::fprintf(stderr, "[main] font %s not found — falling back to the default ImGui font.\n",
                         fontPath.c_str());
        }
        // Fusionne les pictogrammes Font Awesome dans la police courante (menus/boutons).
        const std::string faPath = resolveData("fonts/fa-solid-900.ttf", exeDir);
        if (fileExists(faPath)) {
            static const ImWchar fa_ranges[] = { 0xf000, 0xf8ff, 0 };
            ImFontConfig fcfg;
            fcfg.MergeMode = true;            // ajoute les glyphes FA à la police de base
            fcfg.PixelSnapH = true;
            fcfg.GlyphMinAdvanceX = 14.0f;    // chasse fixe des icônes (alignement)
            fcfg.GlyphOffset.y = 1.0f;        // léger recentrage vertical sur la ligne de texte
            io.Fonts->AddFontFromFileTTF(faPath.c_str(), 13.0f, &fcfg, fa_ranges);
        } else {
            std::fprintf(stderr, "[main] icon font %s not found — no pictograms.\n",
                         faPath.c_str());
        }
    }
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    // Disposition d'une version antérieure → on la resème une fois (cf. kUiVersion).
    if (uiLayoutOutdated) A.dockReset = true;
    ImGui_ImplOpenGL2_Init();
#endif

    std::printf("[main] Middle mouse button or Ctrl+Alt+G: capture/release mouse | "
                "Reset button in the CPU window | close the window: quit\n");
    std::printf("[main] Joystick: USB pad auto-detected (port 1) | F11 = keyboard "
                "emulation (arrows + right Ctrl) | \"Joystick\" menu\n");

    // Bridage à la durée ÉMULÉE de chaque trame : indispensable pour que le temps
    // émulé colle au temps réel — sinon le compteur 200 Hz d'EmuTOS s'emballe et les
    // écarts de double-clic (mesurés en tics) deviennent trop grands. Le vsync ne
    // suffit pas (60 Hz, voire non limitant). ⚠ La période N'EST PAS fixe : la
    // géométrie vidéo (50/60/71 Hz, cf. Shifter::Geometry) change la durée d'une
    // trame — 313×512 cyc ≈ 19,98 ms (50 Hz), 263×508 ≈ 16,66 ms (60 Hz, le défaut
    // d'EmuTOS US et de beaucoup de jeux NTSC), 501×224 ≈ 13,99 ms (mono 71 Hz).
    // L'ancien bridage FIXE à 20 ms ralentissait un écran 60 Hz de ~17 % : temps
    // émulé < temps réel → musique RALENTIE et anneau audio affamé (son HACHÉ, y
    // compris les bruits du lecteur, mixés dans le même anneau) — produceFrame
    // pousse `frameCycles × rate / 8 MHz` échantillons par trame, le thread audio
    // en draine `rate` par seconde : la cadence trame doit suivre la géométrie.
    using clock = std::chrono::steady_clock;
    auto& emuNext = A.emuNext;
    emuNext = clock::now();   // échéance réelle de la prochaine trame émulée

    double& lastMx = A.lastMx; double& lastMy = A.lastMy;
    if (A.kiosk) glfwGetCursorPos(window, &lastMx, &lastMy);   // évite le saut au 1er delta
    // Géométrie fenêtrée de départ (à retrouver en quittant le kiosk). Lancé en
    // --kiosk, on n'a JAMAIS été fenêtré : A.winX/A.winY resteraient à (0,0) et la
    // première sortie de borne collerait la fenêtre à l'origine de l'écran VIRTUEL
    // (donc sur le mauvais moniteur en bi-écran, barre de titre hors champ sous X11).
    // D'où le drapeau : tant qu'il est faux, la sortie de borne centre la fenêtre.
    if (!A.kiosk) {
        glfwGetWindowPos(window, &A.winX, &A.winY);
        glfwGetWindowSize(window, &A.winW, &A.winH);
        A.winGeomValid = true;
    }


    return -1;                    // rien à signaler : la boucle peut commencer
}
