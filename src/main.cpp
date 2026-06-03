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
#include <algorithm>
#include <vector>
#include <sys/stat.h>

#include "core/Machine.hpp"
#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "io/JoystickInput.hpp"

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
                float joydeadzone = 0.30f; bool fastfdc = false;
                bool showDisk = true, showCart = true, showHex = true, showCpu = true;
                bool showJoy = false; };
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
        else if (line.rfind("fastfdc=", 0) == 0) c.fastfdc = (line.substr(8) == "1");
        else if (line.rfind("showDisk=", 0) == 0) c.showDisk = (line.substr(9) == "1");
        else if (line.rfind("showCart=", 0) == 0) c.showCart = (line.substr(9) == "1");
        else if (line.rfind("showHex=", 0) == 0) c.showHex = (line.substr(8) == "1");
        else if (line.rfind("showCpu=", 0) == 0) c.showCpu = (line.substr(8) == "1");
        else if (line.rfind("showJoy=", 0) == 0) c.showJoy = (line.substr(8) == "1");
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
             << "\njoydeadzone=" << c.joydeadzone << "\nfastfdc=" << (c.fastfdc ? 1 : 0)
             << "\nshowDisk=" << (c.showDisk ? 1 : 0)
             << "\nshowCart=" << (c.showCart ? 1 : 0)
             << "\nshowHex=" << (c.showHex ? 1 : 0)
             << "\nshowCpu=" << (c.showCpu ? 1 : 0)
             << "\nshowJoy=" << (c.showJoy ? 1 : 0) << "\n";
}

#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#include "imgui_internal.h"   // gestionnaire de réglages personnalisé (ImGuiSettingsHandler)
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
// --- Pictogrammes Font Awesome 5 Free Solid (fonts/fa-solid-900.ttf, fusionnés dans
// la police ImGui — cf. chargement dans main()). Chaînes UTF-8 des codepoints FA de la
// zone à usage privé. À préfixer à un libellé : ICON_FA_REDO " Reset".
#define ICON_FA_POWER_OFF     "\xef\x80\x91"
#define ICON_FA_REDO          "\xef\x80\x9e"
#define ICON_FA_ADJUST        "\xef\x81\x82"
#define ICON_FA_EJECT         "\xef\x81\x92"
#define ICON_FA_SAVE          "\xef\x83\x87"
#define ICON_FA_BOLT          "\xef\x83\xa7"
#define ICON_FA_DESKTOP       "\xef\x84\x88"
#define ICON_FA_GAMEPAD       "\xef\x84\x9b"
#define ICON_FA_KEYBOARD      "\xef\x84\x9c"
#define ICON_FA_SERVER        "\xef\x88\xb3"
#define ICON_FA_CLONE         "\xef\x89\x8d"
#define ICON_FA_MICROCHIP     "\xef\x8b\x9b"
#define ICON_FA_SIGN_OUT_ALT  "\xef\x8b\xb5"
#define ICON_FA_COMPACT_DISC  "\xef\x94\x9f"
#define ICON_FA_MEMORY        "\xef\x94\xb8"
#define ICON_FA_PALETTE       "\xef\x94\xbf"

// Bouton à ICÔNE SEULE (le texte est superflu quand le pictogramme est explicite) :
// l'infobulle au survol rappelle l'action. Renvoie true au clic.
static bool IconButton(const char* icon, const char* tooltip) {
    const bool clicked = ImGui::Button(icon);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

// --- Persistance de la taille de la fenêtre PRINCIPALE (fenêtre GLFW) dans imgui.ini ---
// La fenêtre hôte n'est pas une fenêtre ImGui ; on enregistre donc sa taille via un
// gestionnaire de réglages ImGui personnalisé, qui écrit/relit une section
// « [NeoST][Window] Size=L,H » dans imgui.ini (à côté des positions des sous-fenêtres).
static GLFWwindow* g_window = nullptr;        // fenêtre hôte (pour interroger/poser sa taille)
static int  g_iniWinW = 0, g_iniWinH = 0;     // taille relue depuis imgui.ini
static bool g_iniWinValid = false;

static void* WinSettings_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char* /*name*/) {
    return (void*)1;                           // une seule entrée → on accepte toujours
}
static void WinSettings_ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    int w = 0, h = 0;
    if (std::sscanf(line, "Size=%d,%d", &w, &h) == 2 && w > 0 && h > 0) {
        g_iniWinW = w; g_iniWinH = h; g_iniWinValid = true;
    }
}
static void WinSettings_ApplyAll(ImGuiContext*, ImGuiSettingsHandler*) {
    if (g_iniWinValid && g_window) glfwSetWindowSize(g_window, g_iniWinW, g_iniWinH);
}
static void WinSettings_WriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
    if (!g_window) return;
    int w = 0, h = 0;
    glfwGetWindowSize(g_window, &w, &h);
    buf->appendf("[%s][Window]\n", handler->TypeName);
    buf->appendf("Size=%d,%d\n\n", w, h);
}
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
bool  g_dbgJoy = false;                // NEOST_DEBUG_JOY=1 → trace l'état brut des manettes
bool  g_kbdJoy = false;                // émulation joystick au clavier (flèches + Ctrl droit)
int   g_kbdJoyPort = 1;                // port ST visé par l'émulation clavier (0/1)
float g_joyDeadzone = 0.30f;           // zone morte centrale des sticks analogiques [0,0.95]
uint8_t g_lastJoy0 = 0, g_lastJoy1 = 0; // dernier octet composé posé sur l'IKBD (fenêtre Joystick)
bool  g_showDisk = true, g_showCart = true, g_showHex = true, g_showCpu = true;  // fenêtres masquables
bool  g_showJoy = false;               // fenêtre joystick (visualisation live)
bool  g_joyCfgDirty = false;           // un réglage joystick a changé → resauver neost.cfg

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

// Callback clavier GLFW → IKBD. La touche Suppr (DEL) est réservée à l'hôte (elle
// libère la souris capturée), donc jamais transmise au ST. Échap, lui, est bien
// envoyé au ST (beaucoup de jeux/applications s'en servent).
void onKey(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
    if (!g_ikbd || action == GLFW_REPEAT) return;   // l'IKBD gère sa propre répétition
    if (key == GLFW_KEY_DELETE) return;             // touche hôte (libération souris)
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
    if (IconButton(ICON_FA_POWER_OFF, "Reset (RESET physique)")) reqReset = true;
    ImGui::Separator();
    ImGui::Text("PC = %08X    SR = %04X", cpu.pc(), cpu.sr());
    ImGui::Separator();
    for (int i = 0; i < 8; ++i)
        ImGui::Text("D%d=%08X   A%d=%08X", i, cpu.reg(i), i, cpu.reg(i + 8));
    ImGui::End();
}

// Fenêtre Joystick : visualisation LIVE de ce que voit l'hôte et de ce qui est
// réellement envoyé au ST. Affiche, pour chaque manette présente, le nom, si elle
// est reconnue « gamepad » (mapping SDL), ses axes (bruts + gamepad) sous forme de
// barres avec la zone morte, ses boutons et son hat ; puis l'octet ST composé pour
// chaque port avec les 5 bits décodés. Inclut les réglages (émulation clavier,
// port, zone morte) modifiables ici. lastJoy0/1 = ce qui a été posé sur l'IKBD.
void drawJoystickAxisBar(const char* label, float v, float dz) {
    // v ∈ [-1,1] → barre [0,1] ; coloration si |v| dépasse la zone morte.
    const float frac = (v + 1.0f) * 0.5f;
    const bool active = (v < -dz) || (v > dz);
    if (active) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.80f, 0.30f, 1.0f));
    char buf[32]; std::snprintf(buf, sizeof buf, "%+.2f", v);
    ImGui::ProgressBar(frac, ImVec2(140.0f, 0.0f), buf);
    if (active) ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextUnformatted(label);
}

void drawJoyDirLed(const char* label, bool on) {
    const ImVec4 col = on ? ImVec4(0.20f, 0.85f, 0.30f, 1.0f) : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    ImGui::TextColored(col, "%s", label);
    ImGui::SameLine();
}

void drawJoystickWindow(GLFWwindow* win, uint8_t lastJoy0, uint8_t lastJoy1) {
    ImGui::Begin("Joystick", &g_showJoy);

    // --- Réglages (modifient les globals ; resauve via g_joyCfgDirty) -----------
    if (ImGui::Checkbox("Émulation clavier (flèches + Ctrl droit)", &g_kbdJoy)) g_joyCfgDirty = true;
    ImGui::SameLine(); ImGui::TextDisabled("(F11)");
    ImGui::Text("Port émulé :"); ImGui::SameLine();
    if (ImGui::RadioButton("1 (jeux)", g_kbdJoyPort == 1)) { g_kbdJoyPort = 1; g_joyCfgDirty = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("0 (souris)", g_kbdJoyPort == 0)) { g_kbdJoyPort = 0; g_joyCfgDirty = true; }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Zone morte", &g_joyDeadzone, 0.0f, 0.95f, "%.2f")) {
        if (g_joyDeadzone < 0.0f) g_joyDeadzone = 0.0f;
        if (g_joyDeadzone > 0.95f) g_joyDeadzone = 0.95f;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) g_joyCfgDirty = true;

    ImGui::Separator();

    // --- Sortie réellement envoyée au ST (le plus important) --------------------
    auto decodeRow = [](const char* who, uint8_t v) {
        ImGui::Text("%s  $%02X :", who, v); ImGui::SameLine();
        drawJoyDirLed("HAUT",   v & stjoy::UP);
        drawJoyDirLed("BAS",    v & stjoy::DOWN);
        drawJoyDirLed("GAUCHE", v & stjoy::LEFT);
        drawJoyDirLed("DROITE", v & stjoy::RIGHT);
        drawJoyDirLed("FEU",    v & stjoy::FIRE);
        ImGui::NewLine();
    };
    ImGui::TextDisabled("→ Envoyé à l'IKBD (ST) :");
    decodeRow("Port 0", lastJoy0);
    decodeRow("Port 1", lastJoy1);

    ImGui::Separator();

    // --- État brut de chaque manette présente -----------------------------------
    int nPresent = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        ++nPresent;
        const char* nm = glfwGetJoystickName(jid);
        const int stPort = (nPresent == 1) ? 1 : (nPresent == 2 ? 0 : -1);
        ImGui::Text("Manette %d : %s", jid, nm ? nm : "?");
        if (stPort >= 0) { ImGui::SameLine(); ImGui::TextDisabled("→ port ST %d", stPort); }

        GLFWgamepadstate gs;
        if (glfwGetGamepadState(jid, &gs)) {
            ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f), "  reconnue gamepad (mapping SDL)");
            ImGui::Indent(8.0f);
            drawJoystickAxisBar("LX", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  g_joyDeadzone);
            drawJoystickAxisBar("LY", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y],  g_joyDeadzone);
            drawJoystickAxisBar("RX", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], g_joyDeadzone);
            drawJoystickAxisBar("RY", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], g_joyDeadzone);
            ImGui::Unindent(8.0f);
        } else {
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f), "  NON reconnue gamepad → lecture brute");
        }

        // Axes bruts (toujours affichés : révèlent un axe non centré au repos).
        int axN = 0, btN = 0, hatN = 0;
        const float*         ax  = glfwGetJoystickAxes(jid, &axN);
        const unsigned char* bt  = glfwGetJoystickButtons(jid, &btN);
        const unsigned char* hat = glfwGetJoystickHats(jid, &hatN);
        ImGui::Text("  Axes bruts (%d) :", axN);
        for (int i = 0; i < axN && ax; ++i) {
            char lbl[16]; std::snprintf(lbl, sizeof lbl, "a%d%s", i,
                                        (i == 0 ? " (X?)" : i == 1 ? " (Y?)" : ""));
            ImGui::Indent(8.0f); drawJoystickAxisBar(lbl, ax[i], g_joyDeadzone); ImGui::Unindent(8.0f);
        }
        ImGui::Text("  Boutons (%d) :", btN); ImGui::SameLine();
        for (int i = 0; i < btN && bt; ++i)
            if (bt[i]) { ImGui::SameLine(); ImGui::Text("%d", i); }
        if (hat && hatN >= 1)
            ImGui::Text("  Hat0 : %s%s%s%s", (hat[0]&GLFW_HAT_UP)?"H":"", (hat[0]&GLFW_HAT_DOWN)?"B":"",
                        (hat[0]&GLFW_HAT_LEFT)?"G":"", (hat[0]&GLFW_HAT_RIGHT)?"D":"");
        // Décomposition analogique / numérique + effet du filtre anti-bloqué.
        const float thr = (g_joyDeadzone < 0.0f) ? 0.0f : (g_joyDeadzone > 0.95f ? 0.95f : g_joyDeadzone);
        uint8_t an = 0, dg = 0; stjoy::readStickRaw(jid, thr, an, dg);
        const uint8_t fin = stjoy::readStick(jid, g_joyDeadzone);
        ImGui::Text("  analogique $%02X | numérique brut $%02X", an, dg);
        if ((dg & ~fin) & ~an)
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f),
                               "  filtre anti-bloqué : bits numériques collés ignorés ($%02X)",
                               uint8_t((dg & ~fin) & ~an));
        ImGui::Text("  → octet ST envoyé : $%02X", fin);
        ImGui::Separator();
    }
    if (nPresent == 0) ImGui::TextDisabled("Aucune manette détectée. (Clavier : active l'émulation ci-dessus.)");
    (void)win;
    ImGui::End();
}

// Fenêtre de l'écran ST : fenêtre de BASE (toujours là, jamais au premier plan).
// Placée sous les barres au 1er lancement, puis DÉPLAÇABLE par glissé de sa barre de
// titre (ImGui mémorise sa position). L'image fait toujours 640×400 (taille du mode
// monochrome) ; les 3 résolutions y sont normalisées. Clic dans l'image = capture souris.
void drawStScreen(const GlScreen& s, bool captured, bool& reqCapture, float topOffset) {
    // FirstUseEver (et non Always) : on ne fixe la position qu'au tout 1er affichage,
    // sinon la fenêtre serait re-ancrée à chaque trame et impossible à déplacer.
    ImGui::SetNextWindowPos(ImVec2(0.0f, topOffset), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoBringToFrontOnFocus;
    // Souris capturée → tout le mouvement va au ST (curseur verrouillé) : on FIGE la
    // fenêtre (pas de glissé). Une fois libérée (DEL), elle redevient déplaçable.
    if (captured) flags |= ImGuiWindowFlags_NoMove;
    ImGui::Begin("Atari ST Screen", nullptr, flags);
    ImGui::TextDisabled(captured ? "Souris capturée — Suppr (DEL) pour la libérer"
                                 : "Clic dans l'écran pour capturer la souris (curseur GEM)");
    const ImTextureID id = (ImTextureID)(intptr_t)s.tex;
    ImGui::Image(id, ImVec2(640.0f, 400.0f));           // taille mode monochrome (rés. normalisées)
    if (!captured && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        reqCapture = true;
    ImGui::End();
}

// Bibliothèque de disquettes : liste les images montables du dossier disks/,
// monte/éjecte sur le lecteur A. reqMount = chemin à monter (sinon vide),
// reqEject = éjection.
void drawDiskLibrary(const std::string& disksDir, const std::string& mounted,
                     std::string& reqMount, bool& reqEject) {
    ImGui::Begin("Disk Library");
    const std::string curName = mounted.empty() ? "(vide)"
                                                 : fs::path(mounted).filename().string();
    // Bouton Éjecter COMPLÈTEMENT À GAUCHE, puis le nom du disque monté à sa droite.
    if (!mounted.empty()) {
        if (IconButton(ICON_FA_EJECT, "Éjecter")) reqEject = true;
        ImGui::SameLine();
    }
    ImGui::Text("Lecteur A : %s", curName.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Images dans %s/", disksDir.c_str());

    std::error_code ec;
    if (fs::is_directory(disksDir, ec)) {
        const fs::path base(disksDir);
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        // Récolte RÉCURSIVE des images .st/.msa/.dim/.stx, triées par ordre alphabétique de
        // DOSSIER puis de FICHIER (insensible à la casse) sur le chemin relatif à disks/.
        std::vector<fs::path> images;
        for (const auto& e : fs::recursive_directory_iterator(base, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
            if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx")
                images.push_back(e.path());
        }
        auto sortKey = [&](const fs::path& p) {
            std::string rel = fs::relative(p, base, ec).generic_string();   // "sous-dossier/fichier"
            for (auto& ch : rel) ch = (char)std::tolower((unsigned char)ch);
            return rel;
        };
        std::sort(images.begin(), images.end(),
                  [&](const fs::path& a, const fs::path& b) { return sortKey(a) < sortKey(b); });

        for (const auto& p : images) {
            const std::string rel = fs::relative(p, base, ec).generic_string();  // affiché (montre le dossier)
            ImGui::PushID(p.string().c_str());
            if (!mountedName.empty() && p.filename().string() == mountedName) {
                ImGui::TextDisabled("●");                  // montée
            } else if (ImGui::SmallButton("Monter")) {
                reqMount = p.string();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(rel.c_str());
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
        if (IconButton(ICON_FA_EJECT, "Éjecter")) reqEject = true;
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
    // Répertoire de l'exécutable (pour retrouver roms/ et disk/ depuis build/).
    const std::string exeDir = [&] {
        const std::string a0 = argv[0] ? argv[0] : "";
        const auto i = a0.find_last_of('/');
        return (i == std::string::npos) ? std::string(".") : a0.substr(0, i);
    }();
    // Préférences mémorisées (dernier ROM + type de moniteur).
    Config cfg = loadConfig(exeDir);
    g_showDisk = cfg.showDisk; g_showCart = cfg.showCart; g_showHex = cfg.showHex;
    g_showCpu  = cfg.showCpu;  g_showJoy  = cfg.showJoy;
    const std::string defRom = cfg.rom.empty() ? std::string("roms/etos192us.img") : cfg.rom;
    // Sans argument, ./neost recharge le dernier ROM (ou EmuTOS US par défaut).
    const std::string romLogical = (argc > 1) ? std::string(argv[1]) : defRom;
    const std::string tosPath  = resolveData(romLogical, exeDir);
    const std::string defDisk  = cfg.disk.empty() ? std::string("disks/diskA.st") : cfg.disk;
    const std::string diskPath = resolveData((argc > 2) ? argv[2] : defDisk, exeDir);
    const std::string cartPath = cfg.cart.empty() ? std::string() : resolveData(cfg.cart, exeDir);
    const std::string disksDir = resolveData("disks", exeDir);   // dossier pour la Disk Library
    const std::string cartsDir = resolveData("carts", exeDir);   // dossier pour la Cart Library
    const std::string romsDir  = resolveData("roms", exeDir);     // dossier pour le sélecteur de ROM

    g_dbgMouse = std::getenv("NEOST_DEBUG_MOUSE") != nullptr;
    g_dbgJoy   = std::getenv("NEOST_DEBUG_JOY")   != nullptr;

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
    machine.fdc.setFastFdc(cfg.fastfdc);      // FDC rapide mémorisé (accès disque ÷10)
    machine.reset();
    cfg.rom = romLogical; saveConfig(exeDir, cfg);   // mémorise dès le lancement

    // Son : un seul périphérique (Audio) mixe le YM2149 ET les bruits mécaniques
    // du lecteur. Le cœur émet des FdcSound, DriveSound joue les WAV de
    // roms/drivesound/ (jeu « epson_smd480l » = vrai lecteur) et Audio les
    // additionne au flux PSG (cf. Audio::render).
    DriveSound drive;
    bool driveSoundOn = drive.init(resolveData("roms/drivesound/epson_smd480l", exeDir), 48000);
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
        machine.fdc.setFastFdc(cfg.fastfdc);   // ré-applique le FDC rapide après reconfig
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
    // Enregistre la taille de la fenêtre PRINCIPALE dans imgui.ini : gestionnaire de
    // réglages personnalisé, posé AVANT le 1er NewFrame (qui charge imgui.ini et applique
    // la taille relue). Un resize marque les réglages « sales » → ImGui resauvegarde.
    g_window = window;
    {
        ImGuiSettingsHandler h;
        h.TypeName   = "NeoST";
        h.TypeHash   = ImHashStr("NeoST");
        h.ReadOpenFn = WinSettings_ReadOpen;
        h.ReadLineFn = WinSettings_ReadLine;
        h.ApplyAllFn = WinSettings_ApplyAll;
        h.WriteAllFn = WinSettings_WriteAll;
        ImGui::AddSettingsHandler(&h);
    }
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
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f);
        } else {
            io.Fonts->AddFontDefault();   // base toujours présente (requis avant la fusion FA)
            std::fprintf(stderr, "[main] police %s introuvable — police ImGui par défaut.\n",
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
            std::fprintf(stderr, "[main] police d'icônes %s introuvable — pas de pictogrammes.\n",
                         faPath.c_str());
        }
    }
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
#endif

    std::printf("[main] Clic dans l'écran : capture souris | Suppr (DEL) : libère | "
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

        // Suppr (DEL) libère la souris si elle est capturée (le curseur GEM est piloté
        // tant que la capture est active). Échap, lui, reste disponible pour le ST.
        if (g_mouseCaptured && glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) {
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
            g_lastJoy0 = joy0; g_lastJoy1 = joy1;   // pour la fenêtre Joystick
            // Diagnostic manette (NEOST_DEBUG_JOY=1) : ~3×/s, état brut des axes.
            if (g_dbgJoy) { static int t = 0; if (++t % 16 == 0) stjoy::debug(window, kbd, g_kbdJoyPort, g_joyDeadzone); }
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
            if (ImGui::BeginMenu(ICON_FA_MICROCHIP " Machine")) {
                if (ImGui::MenuItem(ICON_FA_REDO " Reset"))      reqReset = true;
                if (ImGui::MenuItem(ICON_FA_POWER_OFF " Hard Reset")) reqHardReset = true;
                // Modèle / RAM / cœur / ROM : appliqués À CHAUD (hard reset avec les
                // nouveaux paramètres) — aucun redémarrage de l'appli. Mémorisés dans
                // neost.cfg. `reqRebuild` déclenche la reconfiguration en fin de boucle.
                if (ImGui::BeginMenu(ICON_FA_SERVER " Modèle")) {
                    const char* const ids[]   = { "st", "megast", "ste", "megaste" };
                    const char* const labels[] = { "ST", "Mega ST", "STE", "Mega STE" };
                    for (int i = 0; i < 4; ++i)
                        if (ImGui::MenuItem(labels[i], nullptr, cfg.machine == ids[i])) {
                            cfg.machine = ids[i]; saveConfig(exeDir, cfg); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ICON_FA_MEMORY " Mémoire")) {
                    const char* const mids[]   = { "256k", "512k", "1m", "2m", "4m" };
                    const char* const mlabels[] = { "256 Ko", "512 Ko", "1 Mo", "2 Mo", "4 Mo" };
                    for (int i = 0; i < 5; ++i)
                        if (ImGui::MenuItem(mlabels[i], nullptr, cfg.mem == mids[i])) {
                            cfg.mem = mids[i]; saveConfig(exeDir, cfg); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ICON_FA_MICROCHIP " Cœur CPU")) {
                    const char* const cids[]    = { "moira", "musashi" };
                    const char* const clabels[] = { "Moira (cycle-exact)", "Musashi" };
                    for (int i = 0; i < 2; ++i)
                        if (ImGui::MenuItem(clabels[i], nullptr, cfg.cpu == cids[i])) {
                            cfg.cpu = cids[i]; saveConfig(exeDir, cfg); reqRebuild = true;
                        }
                    ImGui::EndMenu();
                }
                // Image TOS/EmuTOS (.img/.rom du dossier roms/), chargée à chaud.
                if (ImGui::BeginMenu(ICON_FA_SAVE " ROM")) {
                    std::error_code ec;
                    const std::string curRom = fs::path(cfg.rom).filename().string();
                    if (fs::is_directory(romsDir, ec)) {
                        std::vector<fs::path> roms;
                        for (const auto& e : fs::directory_iterator(romsDir, ec)) {
                            if (!e.is_regular_file()) continue;
                            std::string ext = e.path().extension().string();
                            for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                            if (ext != ".img" && ext != ".rom") continue;
                            roms.push_back(e.path());
                        }
                        auto romSortKey = [](const fs::path& p) {
                            std::string name = p.filename().string();
                            for (auto& ch : name) ch = (char)std::tolower((unsigned char)ch);
                            return name;
                        };
                        std::sort(roms.begin(), roms.end(),
                                  [&](const fs::path& a, const fs::path& b) {
                                      return romSortKey(a) < romSortKey(b);
                                  });
                        for (const auto& p : roms) {
                            const std::string name = p.filename().string();
                            if (ImGui::MenuItem(name.c_str(), nullptr, name == curRom)) {
                                cfg.rom = p.string(); saveConfig(exeDir, cfg); reqRebuild = true;
                            }
                        }
                    } else {
                        ImGui::TextDisabled("(dossier roms/ introuvable)");
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(ICON_FA_COMPACT_DISC " Cartouche")) {
                    if (machine.bus.mountedCartPath().empty()) {
                        ImGui::TextDisabled("(aucune)");
                    } else if (ImGui::MenuItem(ICON_FA_EJECT " Éjecter")) {
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
                // FDC rapide (équivalent hatari --fastfdc) : accès disque ÷10. Prend effet
                // immédiatement (pas de reset), mémorisé dans neost.cfg.
                if (ImGui::MenuItem(ICON_FA_BOLT " FDC rapide (accès disque ÷10)", nullptr, cfg.fastfdc)) {
                    cfg.fastfdc = !cfg.fastfdc;
                    machine.fdc.setFastFdc(cfg.fastfdc);
                    saveConfig(exeDir, cfg);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_SIGN_OUT_ALT " Quitter")) glfwSetWindowShouldClose(window, 1);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_DESKTOP " Résolution")) {
                if (ImGui::MenuItem(ICON_FA_PALETTE " Couleur (basse rés)", nullptr,  color)) reqMonitor = 1;
                if (ImGui::MenuItem(ICON_FA_ADJUST " Mono (haute rés)",    nullptr, !color)) reqMonitor = 0;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(ICON_FA_GAMEPAD " Joystick")) {
                // Émulation au clavier (flèches + Ctrl droit). F11 bascule aussi.
                if (ImGui::MenuItem(ICON_FA_KEYBOARD " Émulation clavier (flèches + Ctrl droit)", "F11", &g_kbdJoy)) {
                    cfg.kbdjoy = g_kbdJoy; saveConfig(exeDir, cfg);
                }
                if (ImGui::BeginMenu(ICON_FA_GAMEPAD " Port émulé au clavier")) {
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
            if (ImGui::BeginMenu(ICON_FA_CLONE " Fenêtres")) {
                ImGui::MenuItem(ICON_FA_SAVE " Disk Library",  nullptr, &g_showDisk);
                ImGui::MenuItem(ICON_FA_COMPACT_DISC " Cart Library",  nullptr, &g_showCart);
                ImGui::MenuItem(ICON_FA_MEMORY " Mémoire (hex)", nullptr, &g_showHex);
                ImGui::MenuItem(ICON_FA_MICROCHIP " CPU 68000",     nullptr, &g_showCpu);
                ImGui::MenuItem(ICON_FA_GAMEPAD " Joystick",      nullptr, &g_showJoy);
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
        ImGui::Checkbox("Disk", &g_showDisk);  ImGui::SameLine();
        ImGui::Checkbox("Cart", &g_showCart);  ImGui::SameLine();
        ImGui::Checkbox("Hex",  &g_showHex);   ImGui::SameLine();
        ImGui::Checkbox("CPU",  &g_showCpu);   ImGui::SameLine();
        ImGui::Checkbox("Joy",  &g_showJoy);
        if (drive.ok()) {
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
            if (ImGui::Checkbox("Son lecteur", &driveSoundOn)) drive.setEnabled(driveSoundOn);
        }
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        if (IconButton(color ? ICON_FA_ADJUST : ICON_FA_PALETTE, color ? "Passer en Mono" : "Passer en Couleur"))
            reqMonitor = color ? 0 : 1;
        ImGui::SameLine();
        if (IconButton(ICON_FA_REDO, "Reset")) reqReset = true;
        ImGui::SameLine();
        // Reset à froid : efface la ST-RAM → EmuTOS/TOS refait un boot complet.
        if (IconButton(ICON_FA_POWER_OFF, "Hard Reset")) reqHardReset = true;
        const float toolH = ImGui::GetWindowSize().y;
        ImGui::End();

        // --- Fenêtre écran (base) + fenêtres masquables ----------------------
        drawStScreen(screen, g_mouseCaptured, reqCapture, menuH + toolH);
        if (g_showDisk) drawDiskLibrary(disksDir, machine.fdc.mountedPath(), reqMount, reqEject);
        if (g_showCart) drawCartLibrary(cartsDir, machine.bus.mountedCartPath(), reqMountCart, reqEjectCart);
        if (g_showHex)  drawHexViewer(machine.bus);
        if (g_showCpu)  drawCpuState(machine.cpu, reqReset);
        if (g_showJoy)  drawJoystickWindow(window, g_lastJoy0, g_lastJoy1);
        // Un réglage joystick a changé dans la fenêtre → resauve neost.cfg.
        if (g_joyCfgDirty) {
            cfg.kbdjoy = g_kbdJoy; cfg.joyport = g_kbdJoyPort; cfg.joydeadzone = g_joyDeadzone;
            saveConfig(exeDir, cfg); g_joyCfgDirty = false;
        }
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
            cfg.mono = (reqMonitor == 0);   // mémorise le mode
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
    cfg.disk = machine.fdc.mountedPath();
    cfg.cart = machine.bus.mountedCartPath();
    cfg.mono = !machine.mfp.colorMonitor();
    cfg.showDisk = g_showDisk; cfg.showCart = g_showCart; cfg.showHex = g_showHex;
    cfg.showCpu  = g_showCpu;  cfg.showJoy  = g_showJoy;
    saveConfig(exeDir, cfg);

#if defined(NEOST_WITH_IMGUI)
    // Écrit imgui.ini avant l'arrêt → garantit la sauvegarde de la taille de fenêtre
    // (et des positions de sous-fenêtres) même si rien d'autre n'a marqué les réglages.
    if (ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
