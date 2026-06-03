// =============================================================================
//  main.cpp — Frontend fenêtré de NeoST (GLFW3 + OpenGL + Dear ImGui).
//
//  Le matériel et la boucle d'horloge vivent dans Machine (cœur sans GUI) ; ce
//  fichier ne fait que : créer la fenêtre, téléverser le framebuffer décodé du
//  Shifter dans une texture, router le clavier vers l'IKBD, et afficher l'UI de
//  debug. Le même cœur tourne en headless (neost-headless) pour les traces.
//
//  Modèle temporel (cf. Machine) : 68000 ~8 MHz, 512 cycles/ligne, 313 lignes
//  PAL ≈ 50 Hz. Le Timer C du MFP (200 Hz) débloque l'accueil EmuTOS.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <GLFW/glfw3.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <thread>
#include <string>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>

#include "core/Machine.hpp"
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "JoystickInput.hpp"

namespace fs = std::filesystem;

// Résout un chemin de données indépendamment du répertoire courant : tel quel,
// puis relatif au répertoire de l'exécutable (utile quand on lance depuis build/).
static bool fileExists(const std::string& p) { struct stat s; return ::stat(p.c_str(), &s) == 0; }
static std::string resolveData(const std::string& given, const std::string& exeDir) {
    const std::string cands[] = { given, exeDir + "/" + given, exeDir + "/../" + given, "../" + given };
    for (const auto& c : cands) if (fileExists(c)) return c;
    return given;
}

// --- Persistance des préférences (dernier ROM, type de moniteur) -------------
// Fichier neost.cfg à la racine du projet (à côté de build/).
// cpu = cœur 68000 choisi AU DÉMARRAGE ("moira" cycle-exact par défaut, ou "musashi").
// Défaut machine : 512 Ko ST + cœur Moira.
// kbdjoy = émulation joystick au clavier active ; joyport = port ST visé par
// l'émulation clavier (0 ou 1, défaut 1 = port « jeux »).
struct Config { std::string rom; std::string disk; std::string cart; bool mono = false;
                std::string cpu = "moira"; std::string machine = "st";
                std::string mem = "512k"; bool kbdjoy = false; int joyport = 1;
                float joydeadzone = 0.30f; };
static std::string cfgPath(const std::string& exeDir) { return exeDir + "/../neost.cfg"; }
static Config loadConfig(const std::string& exeDir) {
    Config c;
    std::ifstream f(cfgPath(exeDir));
    if (!f) f.open("neost.cfg");
    std::string line;
    while (std::getline(f, line)) {
        if      (line.rfind("rom=", 0)  == 0) c.rom  = line.substr(4);
        else if (line.rfind("disk=", 0) == 0) c.disk = line.substr(5);
        else if (line.rfind("cart=", 0) == 0) c.cart = line.substr(5);
        else if (line.rfind("mono=", 0) == 0) c.mono = (line.substr(5) == "1");
        else if (line.rfind("cpu=", 0)  == 0) c.cpu  = line.substr(4);
        else if (line.rfind("machine=", 0) == 0) c.machine = line.substr(8);
        else if (line.rfind("mem=", 0)  == 0) c.mem  = line.substr(4);
        else if (line.rfind("kbdjoy=", 0) == 0) c.kbdjoy = (line.substr(7) == "1");
        else if (line.rfind("joyport=", 0) == 0) c.joyport = (line.substr(8) == "0") ? 0 : 1;
        else if (line.rfind("joydeadzone=", 0) == 0) c.joydeadzone = std::strtof(line.substr(12).c_str(), nullptr);
    }
    return c;
}
static void saveConfig(const std::string& exeDir, const Config& c) {
    std::ofstream f(cfgPath(exeDir));
    if (!f) f.open("neost.cfg");
    if (f) f << "rom=" << c.rom << "\ndisk=" << c.disk << "\ncart=" << c.cart
             << "\nmono=" << (c.mono ? 1 : 0)
             << "\ncpu=" << c.cpu << "\nmachine=" << c.machine << "\nmem=" << c.mem
             << "\nkbdjoy=" << (c.kbdjoy ? 1 : 0) << "\njoyport=" << c.joyport
             << "\njoydeadzone=" << c.joydeadzone << "\n";
}

#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#endif

namespace {
// Signe des deltas souris → IKBD. Vérifié en headless (injection contrôlée) :
// paquet +dx = curseur à droite, paquet +dy = curseur vers le bas. GLFW donne
// les mêmes signes (origine en haut-gauche), donc identité sur les deux axes.
constexpr int MOUSE_X_SIGN = +1;
constexpr int MOUSE_Y_SIGN = +1;

Ikbd* g_ikbd = nullptr;                // cible des callbacks clavier/souris GLFW
bool  g_mouseCaptured = false;         // souris capturée → entrées dirigées vers le ST
bool  g_dbgMouse = false;              // NEOST_DEBUG_MOUSE=1 → trace les paquets souris
bool  g_kbdJoy = false;                // émulation joystick au clavier (flèches + Ctrl droit)
int   g_kbdJoyPort = 1;                // port ST visé par l'émulation clavier (0/1)
float g_joyDeadzone = 0.30f;           // zone morte centrale des sticks analogiques [0,0.95]
bool  g_showDisk = true, g_showCart = true, g_showHex = true, g_showCpu = true;  // fenêtres masquables

void onGlfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW erreur %d : %s\n", code, desc);
}

// Callback bouton souris : ÉVÉNEMENTIEL (capte chaque transition, même un
// double-clic rapide qu'une scrutation par trame manquerait). Envoie un paquet
// IKBD sans mouvement portant l'état courant des boutons.
void onMouseButton(GLFWwindow* w, int /*button*/, int /*action*/, int /*mods*/) {
    if (!g_ikbd || !g_mouseCaptured) return;
    const bool l = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    const bool r = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (g_dbgMouse) std::fprintf(stderr, "[souris] bouton  L=%d R=%d\n", l, r);
    g_ikbd->mouseEvent(0, 0, l, r);
}

// --- Écran ST : téléverse le framebuffer ARGB du Shifter dans une texture GL.
//  L'affichage se fait ensuite dans une fenêtre ImGui "Atari ST Screen" (via
//  ImGui::Image) ; en l'absence d'ImGui, on retombe sur un quad plein cadre.
struct GlScreen {
    GLuint tex = 0;
    int w = 0, h = 0;

    void init() {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    void update(const uint32_t* px, int pw, int ph) {
        glBindTexture(GL_TEXTURE_2D, tex);
        if (pw != w || ph != h) {           // la résolution ST a changé → réalloue
            w = pw; h = ph;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                         GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, px);
        }
    }
    void drawFullscreen() {                 // repli sans ImGui (V inversé : ligne 0 en haut)
        glBindTexture(GL_TEXTURE_2D, tex);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
            glTexCoord2f(0.f, 1.f); glVertex2f(-1.f, -1.f);
            glTexCoord2f(1.f, 1.f); glVertex2f( 1.f, -1.f);
            glTexCoord2f(1.f, 0.f); glVertex2f( 1.f,  1.f);
            glTexCoord2f(0.f, 0.f); glVertex2f(-1.f,  1.f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
};

// Traduit une touche GLFW en scancode "make" du clavier Atari ST (0 = ignorée).
// Les scancodes ST suivent la matrice de l'IKBD, pas l'ASCII (cf. doc Atari).
uint8_t glfwToStScancode(int key) {
    switch (key) {
        case GLFW_KEY_ESCAPE: return 0x01;
        case GLFW_KEY_1: return 0x02; case GLFW_KEY_2: return 0x03;
        case GLFW_KEY_3: return 0x04; case GLFW_KEY_4: return 0x05;
        case GLFW_KEY_5: return 0x06; case GLFW_KEY_6: return 0x07;
        case GLFW_KEY_7: return 0x08; case GLFW_KEY_8: return 0x09;
        case GLFW_KEY_9: return 0x0A; case GLFW_KEY_0: return 0x0B;
        case GLFW_KEY_MINUS: return 0x0C; case GLFW_KEY_EQUAL: return 0x0D;
        case GLFW_KEY_BACKSPACE: return 0x0E; case GLFW_KEY_TAB: return 0x0F;
        case GLFW_KEY_Q: return 0x10; case GLFW_KEY_W: return 0x11;
        case GLFW_KEY_E: return 0x12; case GLFW_KEY_R: return 0x13;
        case GLFW_KEY_T: return 0x14; case GLFW_KEY_Y: return 0x15;
        case GLFW_KEY_U: return 0x16; case GLFW_KEY_I: return 0x17;
        case GLFW_KEY_O: return 0x18; case GLFW_KEY_P: return 0x19;
        case GLFW_KEY_LEFT_BRACKET: return 0x1A; case GLFW_KEY_RIGHT_BRACKET: return 0x1B;
        case GLFW_KEY_ENTER: return 0x1C; case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return 0x1D;
        case GLFW_KEY_A: return 0x1E; case GLFW_KEY_S: return 0x1F;
        case GLFW_KEY_D: return 0x20; case GLFW_KEY_F: return 0x21;
        case GLFW_KEY_G: return 0x22; case GLFW_KEY_H: return 0x23;
        case GLFW_KEY_J: return 0x24; case GLFW_KEY_K: return 0x25;
        case GLFW_KEY_L: return 0x26; case GLFW_KEY_SEMICOLON: return 0x27;
        case GLFW_KEY_APOSTROPHE: return 0x28; case GLFW_KEY_GRAVE_ACCENT: return 0x29;
        case GLFW_KEY_LEFT_SHIFT: return 0x2A; case GLFW_KEY_BACKSLASH: return 0x2B;
        case GLFW_KEY_Z: return 0x2C; case GLFW_KEY_X: return 0x2D;
        case GLFW_KEY_C: return 0x2E; case GLFW_KEY_V: return 0x2F;
        case GLFW_KEY_B: return 0x30; case GLFW_KEY_N: return 0x31;
        case GLFW_KEY_M: return 0x32; case GLFW_KEY_COMMA: return 0x33;
        case GLFW_KEY_PERIOD: return 0x34; case GLFW_KEY_SLASH: return 0x35;
        case GLFW_KEY_RIGHT_SHIFT: return 0x36; case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT: return 0x38; case GLFW_KEY_SPACE: return 0x39;
        case GLFW_KEY_CAPS_LOCK: return 0x3A;
        case GLFW_KEY_F1: return 0x3B; case GLFW_KEY_F2: return 0x3C;
        case GLFW_KEY_F3: return 0x3D; case GLFW_KEY_F4: return 0x3E;
        case GLFW_KEY_F5: return 0x3F; case GLFW_KEY_F6: return 0x40;
        case GLFW_KEY_F7: return 0x41; case GLFW_KEY_F8: return 0x42;
        case GLFW_KEY_F9: return 0x43; case GLFW_KEY_F10: return 0x44;
        case GLFW_KEY_HOME: return 0x47;
        case GLFW_KEY_UP: return 0x48; case GLFW_KEY_LEFT: return 0x4B;
        case GLFW_KEY_RIGHT: return 0x4D; case GLFW_KEY_DOWN: return 0x50;
        case GLFW_KEY_INSERT: return 0x52; case GLFW_KEY_DELETE: return 0x53;
        default: return 0x00;
    }
}

// Callback clavier GLFW → IKBD. Échap est réservé à l'hôte (libère la souris),
// donc jamais transmis au ST.
void onKey(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
    if (!g_ikbd || action == GLFW_REPEAT) return;   // l'IKBD gère sa propre répétition
    if (key == GLFW_KEY_ESCAPE) return;             // touche hôte (libération souris)
#if defined(NEOST_WITH_IMGUI)
    if (ImGui::GetIO().WantCaptureKeyboard) return;  // une saisie ImGui a le focus
#endif
    // Émulation joystick clavier active : les touches du joystick (flèches + Ctrl
    // droit) pilotent la manette et NE sont PAS transmises au clavier ST (sinon
    // double effet) ; elles sont scrutées par trame dans la boucle (cf. stjoy::compose).
    if (g_kbdJoy && stjoy::kbdBit(key)) return;
    const uint8_t sc = glfwToStScancode(key);
    if (sc) g_ikbd->keyEvent(sc, action == GLFW_PRESS);
}

#if defined(NEOST_WITH_IMGUI)
void drawHexViewer(Bus& bus) {
    static int base = 0;
    ImGui::Begin("Mémoire (hex)");
    ImGui::InputInt("Adresse base", &base, 16, 256, ImGuiInputTextFlags_CharsHexadecimal);
    if (base < 0) base = 0;
    const auto& mem = bus.ram;
    for (int row = 0; row < 16; ++row) {
        const int addr = base + row * 16;
        char line[128];
        int n = std::snprintf(line, sizeof line, "%06X:", addr);
        for (int col = 0; col < 16 && (addr + col) < (int)mem.size(); ++col)
            n += std::snprintf(line + n, sizeof line - n, " %02X", mem[addr + col]);
        ImGui::TextUnformatted(line);
    }
    ImGui::End();
}

// reqReset passe à true si le bouton RESET est cliqué.
void drawCpuState(Cpu68k& cpu, bool& reqReset) {
    ImGui::Begin("CPU 68000");
    if (ImGui::Button("Reset (RESET physique)")) reqReset = true;
    ImGui::Separator();
    ImGui::Text("PC = %08X    SR = %04X", cpu.pc(), cpu.sr());
    ImGui::Separator();
    for (int i = 0; i < 8; ++i)
        ImGui::Text("D%d=%08X   A%d=%08X", i, cpu.reg(i), i, cpu.reg(i + 8));
    ImGui::End();
}

// Fenêtre de l'écran ST : c'est la fenêtre de BASE (toujours là, ancrée sous les
// barres, jamais au premier plan). L'image fait toujours 640×400 (taille du mode
// monochrome) ; les 3 résolutions y sont normalisées. Clic = capture souris.
void drawStScreen(const GlScreen& s, bool captured, bool& reqCapture, float topOffset) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, topOffset), ImGuiCond_Always);
    ImGui::Begin("Atari ST Screen", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextDisabled(captured ? "Souris capturée — Échap pour la libérer"
                                 : "Clic dans l'écran pour capturer la souris (curseur GEM)");
    const ImTextureID id = (ImTextureID)(intptr_t)s.tex;
    ImGui::Image(id, ImVec2(640.0f, 400.0f));           // taille mode monochrome (rés. normalisées)
    if (!captured && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        reqCapture = true;
    ImGui::End();
}

// Bibliothèque de disquettes : liste les .st du dossier disks/, monte/éjecte sur
// le lecteur A. reqMount = chemin à monter (sinon vide), reqEject = éjection.
void drawDiskLibrary(const std::string& disksDir, const std::string& mounted,
                     std::string& reqMount, bool& reqEject) {
    ImGui::Begin("Disk Library");
    const std::string curName = mounted.empty() ? "(vide)"
                                                 : fs::path(mounted).filename().string();
    ImGui::Text("Lecteur A : %s", curName.c_str());
    if (!mounted.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Éjecter")) reqEject = true;
    }
    ImGui::Separator();
    ImGui::TextDisabled("Images dans %s/", disksDir.c_str());

    std::error_code ec;
    if (fs::is_directory(disksDir, ec)) {
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        for (const auto& e : fs::directory_iterator(disksDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (ext != ".st" && ext != ".msa") continue;
            const std::string name = e.path().filename().string();
            ImGui::PushID(name.c_str());
            if (name == mountedName) {
                ImGui::TextDisabled("●");                  // montée
            } else if (ImGui::SmallButton("Monter")) {
                reqMount = e.path().string();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("(dossier disks/ introuvable)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Monter puis Reset pour démarrer une disquette amorçable.");
    ImGui::End();
}

// Bibliothèque de cartouches : liste les images du dossier carts/ et branche le
// port $FA0000. Un reset reste nécessaire pour que le TOS relise le magic de boot.
void drawCartLibrary(const std::string& cartsDir, const std::string& mounted,
                     std::string& reqMount, bool& reqEject) {
    ImGui::Begin("Cart Library");
    const std::string curName = mounted.empty() ? "(vide)"
                                                 : fs::path(mounted).filename().string();
    ImGui::Text("Port cartouche : %s", curName.c_str());
    if (!mounted.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Éjecter")) reqEject = true;
    }
    ImGui::Separator();
    ImGui::TextDisabled("Images dans %s/", cartsDir.c_str());

    std::error_code ec;
    if (fs::is_directory(cartsDir, ec)) {
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        for (const auto& e : fs::directory_iterator(cartsDir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (ext != ".bin" && ext != ".img" && ext != ".rom") continue;
            const std::string name = e.path().filename().string();
            ImGui::PushID(name.c_str());
            if (name == mountedName) {
                ImGui::TextDisabled("●");                  // branchée
            } else if (ImGui::SmallButton("Brancher")) {
                reqMount = e.path().string();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("(dossier carts/ introuvable)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Brancher/éjecter relance la machine pour re-détecter la cartouche.");
    ImGui::End();
}
#endif // NEOST_WITH_IMGUI
} // namespace

int main(int argc, char** argv) {
    // Répertoire de l'exécutable (pour retrouver rom/ et disk/ depuis build/).
    const std::string exeDir = [&] {
        const std::string a0 = argv[0] ? argv[0] : "";
        const auto i = a0.find_last_of('/');
        return (i == std::string::npos) ? std::string(".") : a0.substr(0, i);
    }();
    // Préférences mémorisées (dernier ROM + type de moniteur).
    Config cfg = loadConfig(exeDir);
    const std::string defRom = cfg.rom.empty() ? std::string("rom/etos192us.img") : cfg.rom;
    // Sans argument, ./neost recharge le dernier ROM (ou EmuTOS US par défaut).
    const std::string romLogical = (argc > 1) ? std::string(argv[1]) : defRom;
    const std::string tosPath  = resolveData(romLogical, exeDir);
    const std::string defDisk  = cfg.disk.empty() ? std::string("disks/diskA.st") : cfg.disk;
    const std::string diskPath = resolveData((argc > 2) ? argv[2] : defDisk, exeDir);
    const std::string cartPath = cfg.cart.empty() ? std::string() : resolveData(cfg.cart, exeDir);
    const std::string disksDir = resolveData("disks", exeDir);   // dossier pour la Disk Library
    const std::string cartsDir = resolveData("carts", exeDir);   // dossier pour la Cart Library
    const std::string romsDir  = resolveData("rom", exeDir);     // dossier pour le sélecteur de ROM

    g_dbgMouse = std::getenv("NEOST_DEBUG_MOUSE") != nullptr;

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;

    // Pas de hint de profil → contexte legacy compatible (GL 2.1, immediate mode).
    // Fenêtre hôte large : elle héberge la fenêtre ImGui "Atari ST Screen" + le debug.
    GLFWwindow* window = glfwCreateWindow(1280, 860, "NeoST — Atari ST", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);                    // VSync : cadence la boucle

    // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
    const MachineType machType0 = Machine::adjustMachineForTos(parseMachine(cfg.machine), tosPath);
    Machine machine(parseRamBytes(cfg.mem), Cpu68k::parseCore(cfg.cpu),
                    machType0);                   // RAM + cœur + modèle (cfg, ajusté au TOS)
    std::fprintf(stderr, "[main] cœur CPU : %s | machine : %s | RAM : %s\n",
                 Cpu68k::coreName(machine.cpu.core()),
                 machineName(machType0), cfg.mem.c_str());
    if (!machine.loadTos(tosPath))
        std::fprintf(stderr, "[main] Démarrage sans TOS (le CPU tournera à vide).\n");
    if (!machine.loadDisk(diskPath))
        std::fprintf(stderr, "[main] Aucune disquette montée (%s).\n", diskPath.c_str());
    if (!cartPath.empty() && !machine.loadCart(cartPath))
        std::fprintf(stderr, "[main] Aucune cartouche montée (%s).\n", cartPath.c_str());
    machine.mfp.setColorMonitor(!cfg.mono);   // moniteur mémorisé (avant le reset)
    machine.reset();
    cfg.rom = romLogical; saveConfig(exeDir, cfg);   // mémorise dès le lancement

    // Son : un seul périphérique (Audio) mixe le YM2149 ET les bruits mécaniques
    // du lecteur. Le cœur émet des FdcSound, DriveSound joue les WAV de
    // rom/drivesound/ (jeu « epson_smd480l » = vrai lecteur) et Audio les
    // additionne au flux PSG (cf. Audio::render).
    DriveSound drive;
    bool driveSoundOn = drive.init(resolveData("rom/drivesound/epson_smd480l", exeDir), 48000);
    if (driveSoundOn)
        machine.fdc.setSoundSink([&drive](FdcSound e) { drive.onEvent(e); });
    Audio audio(machine.psg, driveSoundOn ? &drive : nullptr, &machine.dmasnd);
    audio.start();   // échec silencieux possible (CI / pas de carte son)

    GlScreen screen;
    screen.init();

    // Applique la config courante (modèle / RAM / cœur / ROM) À CHAUD : reconfigure
    // la Machine en place (son adresse ne change pas → les références d'Audio vers
    // psg/dmasnd restent valides), recharge la ROM, repose le moniteur, puis reset.
    // C'est un hard reset avec les nouveaux paramètres — aucun redémarrage de l'appli.
    // Le disque monté est conservé.
    auto applyConfig = [&] {
        const std::string romP = resolveData(cfg.rom, exeDir);
        // Abaisse la machine si le TOS ne la supporte pas (TOS <= 1.04 → ST), comme Hatari.
        const MachineType machTypeR = Machine::adjustMachineForTos(parseMachine(cfg.machine), romP);
        machine.reconfigure(parseRamBytes(cfg.mem), Cpu68k::parseCore(cfg.cpu), machTypeR);
        machine.loadTos(romP);
        if (cfg.cart.empty()) machine.ejectCart();
        else                  machine.loadCart(resolveData(cfg.cart, exeDir));
        machine.mfp.setColorMonitor(!cfg.mono);
        machine.reset();
        std::fprintf(stderr, "[main] reconfig à chaud : cœur %s | machine %s | RAM %s\n",
                     Cpu68k::coreName(machine.cpu.core()),
                     machineName(machTypeR), cfg.mem.c_str());
    };

    // Callbacks installés AVANT ImGui : ImGui chaîne les nôtres derrière les siens.
    g_ikbd = &machine.ikbd;
    g_kbdJoy     = cfg.kbdjoy;          // émulation joystick clavier (mémorisée)
    g_kbdJoyPort = cfg.joyport;
    g_joyDeadzone = cfg.joydeadzone;    // zone morte des sticks (mémorisée)
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);

#if defined(NEOST_WITH_IMGUI)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
#endif

    std::printf("[main] Clic dans l'écran : capture souris | Échap : libère | "
                "bouton Reset dans la fenêtre CPU | fermer la fenêtre : quitter\n");
    std::printf("[main] Joystick : manette USB auto (port 1) | F11 = émulation "
                "clavier (flèches + Ctrl droit) | menu « Joystick »\n");

    // Bridage à 50 fps (PAL) : indispensable pour que le temps émulé colle au
    // temps réel — sinon le compteur 200 Hz d'EmuTOS s'emballe et les écarts de
    // double-clic (mesurés en tics) deviennent trop grands. Le vsync ne suffit
    // pas (60 Hz, voire non limitant).
    using clock = std::chrono::steady_clock;
    constexpr auto kFramePeriod = std::chrono::microseconds(20000);   // 1/50 s
    auto nextFrame = clock::now();

    double lastMx = 0, lastMy = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();                      // les transitions de boutons → onMouseButton

        // Échap libère la souris si elle est capturée (le curseur GEM est piloté
        // tant que la capture est active).
        if (g_mouseCaptured && glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            g_mouseCaptured = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        if (g_mouseCaptured) {                  // mouvement relatif → paquet IKBD (boutons inclus)
            double mx, my; glfwGetCursorPos(window, &mx, &my);
            const int dx = int(mx - lastMx), dy = int(my - lastMy);
            if (dx || dy) {
                lastMx += dx; lastMy += dy;     // on ne consomme QUE l'entier → le reste
                                                // fractionnaire s'accumule (drags lents)
                const bool l = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
                const bool r = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                if (g_dbgMouse) std::fprintf(stderr, "[souris] mvt dx=%d dy=%d L=%d R=%d\n", dx, dy, l, r);
                machine.ikbd.mouseEvent(dx * MOUSE_X_SIGN, dy * MOUSE_Y_SIGN, l, r);
            }
        }

        // F11 (front montant) : bascule l'émulation joystick au clavier. Pratique
        // surtout sans ImGui (pas de menu) ; mémorisé en config en fin de boucle.
        {
            static bool f11Prev = false;
            const bool f11 = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
            if (f11 && !f11Prev) {
                g_kbdJoy = !g_kbdJoy;
                cfg.kbdjoy = g_kbdJoy; saveConfig(exeDir, cfg);
                std::fprintf(stderr, "[joystick] émulation clavier %s (port %d)\n",
                             g_kbdJoy ? "ON" : "OFF", g_kbdJoyPort);
            }
            f11Prev = f11;
        }

        // Joystick hôte → IKBD (manettes USB + émulation clavier). Scruté chaque
        // trame : l'IKBD répond avec cet état aux interrogations $16 / au report
        // auto $14. L'émulation clavier est inhibée si une saisie ImGui a le focus.
        {
            bool kbd = g_kbdJoy;
#if defined(NEOST_WITH_IMGUI)
            if (ImGui::GetIO().WantCaptureKeyboard) kbd = false;
#endif
            uint8_t joy0 = 0, joy1 = 0;
            stjoy::compose(window, kbd, g_kbdJoyPort, g_joyDeadzone, joy0, joy1);
            machine.ikbd.setJoystick(joy0, joy1);
        }

        machine.cpu.updateIpl();               // entrées reçues → réévalue l'IPL

        machine.runFrame();                    // une trame complète (timing + décodage)
        screen.update(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        bool reqReset = false, reqHardReset = false, reqRebuild = false, reqCapture = false;
        int  reqMonitor = -1;
#if defined(NEOST_WITH_IMGUI)
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        std::string reqMount; bool reqEject = false;
        std::string reqMountCart; bool reqEjectCart = false;
        const bool color = machine.mfp.colorMonitor();

        // --- Menu (haut) -----------------------------------------------------
        float menuH = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            menuH = ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu("Machine")) {
                if (ImGui::MenuItem("Reset"))      reqReset = true;
                if (ImGui::MenuItem("Hard Reset")) reqHardReset = true;
                // Modèle / RAM / cœur / ROM : appliqués À CHAUD (hard reset avec les
                // nouveaux paramètres) — aucun redémarrage de l'appli. Mémorisés dans
                // neost.cfg. `reqRebuild` déclenche la reconfiguration en fin de boucle.
                if (ImGui::BeginMenu("Modèle")) {
                    const char* const ids[]   = { "st", "megast", "ste", "megaste" };
                    const char* const labels[] = { "ST", "Mega ST", "STE", "Mega STE" };
                    for (int i = 0; i < 4; ++i)
                        if (ImGui::MenuItem(labels[i], nullptr, cfg.machine == ids[i])) {
                            cfg.machine = ids[i]; saveConfig(exeDir, cfg); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Mémoire")) {
                    const char* const mids[]   = { "256k", "512k", "1m", "2m", "4m" };
                    const char* const mlabels[] = { "256 Ko", "512 Ko", "1 Mo", "2 Mo", "4 Mo" };
                    for (int i = 0; i < 5; ++i)
                        if (ImGui::MenuItem(mlabels[i], nullptr, cfg.mem == mids[i])) {
                            cfg.mem = mids[i]; saveConfig(exeDir, cfg); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Cœur CPU")) {
                    const char* const cids[]    = { "moira", "musashi" };
                    const char* const clabels[] = { "Moira (cycle-exact)", "Musashi" };
                    for (int i = 0; i < 2; ++i)
                        if (ImGui::MenuItem(clabels[i], nullptr, cfg.cpu == cids[i])) {
                            cfg.cpu = cids[i]; saveConfig(exeDir, cfg); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                // Image TOS/EmuTOS (.img/.rom du dossier rom/), chargée à chaud.
                if (ImGui::BeginMenu("ROM")) {
                    std::error_code ec;
                    const std::string curRom = fs::path(cfg.rom).filename().string();
                    if (fs::is_directory(romsDir, ec)) {
                        for (const auto& e : fs::directory_iterator(romsDir, ec)) {
                            if (!e.is_regular_file()) continue;
                            std::string ext = e.path().extension().string();
                            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                            if (ext != ".img" && ext != ".rom") continue;
                            const std::string name = e.path().filename().string();
                            if (ImGui::MenuItem(name.c_str(), nullptr, name == curRom)) {
                                cfg.rom = e.path().string(); saveConfig(exeDir, cfg); reqRebuild = true;
                            }
                        }
                    } else {
                        ImGui::TextDisabled("(dossier rom/ introuvable)");
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Cartouche")) {
                    if (machine.bus.mountedCartPath().empty()) {
                        ImGui::TextDisabled("(aucune)");
                    } else if (ImGui::MenuItem("Éjecter")) {
                        reqEjectCart = true;
                    }
                    std::error_code ec;
                    const std::string curCart = fs::path(machine.bus.mountedCartPath()).filename().string();
                    if (fs::is_directory(cartsDir, ec)) {
                        for (const auto& e : fs::directory_iterator(cartsDir, ec)) {
                            if (!e.is_regular_file()) continue;
                            std::string ext = e.path().extension().string();
                            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                            if (ext != ".bin" && ext != ".img" && ext != ".rom") continue;
                            const std::string name = e.path().filename().string();
                            if (ImGui::MenuItem(name.c_str(), nullptr, name == curCart))
                                reqMountCart = e.path().string();
                        }
                    } else {
                        ImGui::TextDisabled("(dossier carts/ introuvable)");
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quitter")) glfwSetWindowShouldClose(window, 1);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Résolution")) {
                if (ImGui::MenuItem("Couleur (basse rés)", nullptr,  color)) reqMonitor = 1;
                if (ImGui::MenuItem("Mono (haute rés)",    nullptr, !color)) reqMonitor = 0;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Joystick")) {
                // Émulation au clavier (flèches + Ctrl droit). F11 bascule aussi.
                if (ImGui::MenuItem("Émulation clavier (flèches + Ctrl droit)", "F11", &g_kbdJoy)) {
                    cfg.kbdjoy = g_kbdJoy; saveConfig(exeDir, cfg);
                }
                if (ImGui::BeginMenu("Port émulé au clavier")) {
                    if (ImGui::MenuItem("Port 1 (jeux)", nullptr, g_kbdJoyPort == 1)) {
                        g_kbdJoyPort = cfg.joyport = 1; saveConfig(exeDir, cfg);
                    }
                    if (ImGui::MenuItem("Port 0 (souris)", nullptr, g_kbdJoyPort == 0)) {
                        g_kbdJoyPort = cfg.joyport = 0; saveConfig(exeDir, cfg);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                // Zone morte centrale des sticks analogiques (anti-drift). Le D-pad
                // numérique n'est pas concerné. Mémorisée à la validation du slider.
                ImGui::TextDisabled("Zone morte (sticks)");
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::SliderFloat("##deadzone", &g_joyDeadzone, 0.0f, 0.95f, "%.2f")) {
                    if (g_joyDeadzone < 0.0f) g_joyDeadzone = 0.0f;
                    if (g_joyDeadzone > 0.95f) g_joyDeadzone = 0.95f;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    cfg.joydeadzone = g_joyDeadzone; saveConfig(exeDir, cfg);
                }
                ImGui::Separator();
                // Manettes USB détectées (la 1re → port 1, la 2e → port 0).
                ImGui::TextDisabled("Manettes USB détectées :");
                int nPad = 0;
                for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                    if (!glfwJoystickPresent(jid)) continue;
                    const char* nm = glfwGetGamepadName(jid);
                    if (!nm) nm = glfwGetJoystickName(jid);
                    ImGui::BulletText("Port %d : %s", (nPad == 0) ? 1 : 0, nm ? nm : "?");
                    ++nPad;
                }
                if (nPad == 0) ImGui::BulletText("(aucune)");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Fenêtres")) {
                ImGui::MenuItem("Disk Library",  nullptr, &g_showDisk);
                ImGui::MenuItem("Cart Library",  nullptr, &g_showCart);
                ImGui::MenuItem("Mémoire (hex)", nullptr, &g_showHex);
                ImGui::MenuItem("CPU 68000",     nullptr, &g_showCpu);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- Barre de boutons (sous le menu) ---------------------------------
        ImGui::SetNextWindowPos(ImVec2(0.0f, menuH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 0.0f), ImGuiCond_Always);
        ImGui::Begin("##toolbar", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (ImGui::Button("Reset")) reqReset = true;
        ImGui::SameLine();
        // Reset à froid : efface la ST-RAM → EmuTOS/TOS refait un boot complet.
        if (ImGui::Button("Hard Reset")) reqHardReset = true;
        ImGui::SameLine();
        if (ImGui::Button(color ? "Passer en Mono" : "Passer en Couleur"))
            reqMonitor = color ? 0 : 1;
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Checkbox("Disk", &g_showDisk);  ImGui::SameLine();
        ImGui::Checkbox("Cart", &g_showCart);  ImGui::SameLine();
        ImGui::Checkbox("Hex",  &g_showHex);   ImGui::SameLine();
        ImGui::Checkbox("CPU",  &g_showCpu);
        if (drive.ok()) {
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
            if (ImGui::Checkbox("Son lecteur", &driveSoundOn)) drive.setEnabled(driveSoundOn);
        }
        const float toolH = ImGui::GetWindowSize().y;
        ImGui::End();

        // --- Fenêtre écran (base) + fenêtres masquables ----------------------
        drawStScreen(screen, g_mouseCaptured, reqCapture, menuH + toolH);
        if (g_showDisk) drawDiskLibrary(disksDir, machine.fdc.mountedPath(), reqMount, reqEject);
        if (g_showCart) drawCartLibrary(cartsDir, machine.bus.mountedCartPath(), reqMountCart, reqEjectCart);
        if (g_showHex)  drawHexViewer(machine.bus);
        if (g_showCpu)  drawCpuState(machine.cpu, reqReset);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        // Disk Library : montage / éjection à chaud du lecteur A.
        if (!reqMount.empty()) {
            machine.fdc.loadImage(reqMount);
            cfg.disk = reqMount; saveConfig(exeDir, cfg);
        }
        if (reqEject) {
            machine.fdc.eject();
            cfg.disk.clear(); saveConfig(exeDir, cfg);
        }
        // Cart Library : branchement / éjection à chaud du port cartouche.
        if (!reqMountCart.empty()) {
            if (machine.loadCart(reqMountCart)) {
                cfg.cart = reqMountCart; saveConfig(exeDir, cfg);
                reqHardReset = true;       // le TOS sonde le port cartouche au boot
            }
        }
        if (reqEjectCart) {
            machine.ejectCart();
            cfg.cart.clear(); saveConfig(exeDir, cfg);
            reqHardReset = true;           // relance sans la ROM $FA0000
        }
#else
        screen.drawFullscreen();               // repli sans ImGui
#endif
        // Changement de moniteur (couleur/mono) → hard reset pour que TOS
        // re-détecte la résolution au boot.
        if (reqMonitor >= 0 && (reqMonitor == 1) != machine.mfp.colorMonitor()) {
            machine.mfp.setColorMonitor(reqMonitor == 1);
            machine.reset();
            cfg.rom = romLogical; cfg.mono = (reqMonitor == 0);   // mémorise le mode
            saveConfig(exeDir, cfg);
        }
        // Application des requêtes (en fin de boucle, hors rendu ImGui) :
        if (reqRebuild)   applyConfig();       // modèle/RAM/cœur/ROM → reconfig à chaud
        if (reqHardReset) machine.hardReset(); // power-cycle (RAM effacée, boot à froid)
        if (reqReset)     machine.reset();     // reset « doux » (RAM conservée)
        if (reqCapture) {                      // clic dans l'écran → on capture la souris
            g_mouseCaptured = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(window, &lastMx, &lastMy);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

        glfwSwapBuffers(window);

        // Cadence à 50 Hz réels (resync si on a pris du retard, sans accumuler).
        nextFrame += kFramePeriod;
        const auto now = clock::now();
        if (now < nextFrame) std::this_thread::sleep_until(nextFrame);
        else                 nextFrame = now;
    }

    // Mémorise le dernier ROM, la disquette/cartouche montée et le moniteur.
    cfg.rom = romLogical;
    cfg.disk = machine.fdc.mountedPath();
    cfg.cart = machine.bus.mountedCartPath();
    cfg.mono = !machine.mfp.colorMonitor();
    saveConfig(exeDir, cfg);

#if defined(NEOST_WITH_IMGUI)
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
