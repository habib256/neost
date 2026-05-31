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

#include "core/Machine.hpp"

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

// Fenêtre dédiée à l'écran de l'Atari ST (framebuffer décodé, 1:1). Un clic dans
// l'image demande la capture souris (reqCapture passe à true).
void drawStScreen(const GlScreen& s, bool captured, bool& reqCapture) {
    ImGui::Begin("Atari ST Screen");
    ImGui::TextDisabled(captured
        ? "Souris capturée — Échap pour la libérer"
        : "Clic dans l'écran pour capturer la souris (curseur GEM)");
    const ImTextureID id = (ImTextureID)(intptr_t)s.tex;
    ImGui::Image(id, ImVec2((float)s.w, (float)s.h));   // ligne 0 du ST en haut (uv par défaut)
    if (!captured && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        reqCapture = true;
    ImGui::End();
}
#endif // NEOST_WITH_IMGUI
} // namespace

int main(int argc, char** argv) {
    // ROM par défaut : EmuTOS FR (libre) dans rom/ ; argument = autre image.
    const char* tosPath = (argc > 1) ? argv[1] : "rom/etos192fr.img";

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;

    // Pas de hint de profil → contexte legacy compatible (GL 2.1, immediate mode).
    // Fenêtre hôte large : elle héberge la fenêtre ImGui "Atari ST Screen" + le debug.
    GLFWwindow* window = glfwCreateWindow(1024, 720, "NeoST — Atari ST", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);                    // VSync : cadence la boucle

    Machine machine;                        // toute la carte mère
    if (!machine.loadTos(tosPath))
        std::fprintf(stderr, "[main] Démarrage sans TOS (le CPU tournera à vide).\n");
    machine.reset();

    GlScreen screen;
    screen.init();

    // Callbacks installés AVANT ImGui : ImGui chaîne les nôtres derrière les siens.
    g_ikbd = &machine.ikbd;
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
            lastMx = mx; lastMy = my;
            if (dx || dy) {
                const bool l = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
                const bool r = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                machine.ikbd.mouseEvent(dx * MOUSE_X_SIGN, dy * MOUSE_Y_SIGN, l, r);
            }
        }

        machine.cpu.updateIpl();               // entrées reçues → réévalue l'IPL

        machine.runFrame();                    // une trame complète (timing + décodage)
        screen.update(machine.shifter.pixels(), machine.shifter.width(), machine.shifter.height());

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.10f, 0.10f, 0.12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        bool reqReset = false, reqCapture = false;
#if defined(NEOST_WITH_IMGUI)
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawStScreen(screen, g_mouseCaptured, reqCapture);  // écran ST dans sa fenêtre
        drawHexViewer(machine.bus);
        drawCpuState(machine.cpu, reqReset);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
#else
        screen.drawFullscreen();               // repli sans ImGui
#endif
        if (reqReset) machine.reset();
        if (reqCapture) {                      // clic dans l'écran → on capture la souris
            g_mouseCaptured = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwGetCursorPos(window, &lastMx, &lastMy);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

        glfwSwapBuffers(window);
    }

#if defined(NEOST_WITH_IMGUI)
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
