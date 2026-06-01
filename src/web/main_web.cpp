// =============================================================================
//  main_web.cpp — Frontend WebAssembly de NeoST (Emscripten + GLFW3 + WebGL).
//
//  Même principe que main.cpp (GUI natif) mais adapté au navigateur :
//    - le cœur (neost_core / Machine) est STRICTEMENT identique ;
//    - la sortie vidéo passe par un shader WebGL (GLES2) au lieu de l'OpenGL
//      immédiat (glBegin/glEnd) et du format BGRA, non supportés par WebGL ;
//    - la boucle est pilotée par emscripten_set_main_loop (requestAnimationFrame)
//      au lieu d'un sleep, car le navigateur cadence lui-même les trames.
//
//  Le ROM (EmuTOS) et la disquette par défaut sont embarqués dans le système de
//  fichiers virtuel via --preload-file (cf. CMakeLists.txt). Des fonctions C
//  exportées (neost_reset, neost_set_mono, neost_mount_disk) permettent à la
//  page HTML (shell) de piloter la machine.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <GLFW/glfw3.h>
#include <GLES2/gl2.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "core/Machine.hpp"

namespace {

Machine* g_machine = nullptr;            // carte mère (allouée dans main)
GLFWwindow* g_window = nullptr;

// --- État vidéo WebGL --------------------------------------------------------
GLuint g_tex = 0, g_prog = 0, g_vbo = 0;
GLint  g_locPos = -1, g_locUV = -1, g_locTex = -1;
int    g_texW = 0, g_texH = 0;

// --- État souris -------------------------------------------------------------
bool   g_mouseCaptured = false;
double g_lastMx = 0, g_lastMy = 0;

// Signe des deltas souris → IKBD (identique au frontend natif, cf. main.cpp).
constexpr int MOUSE_X_SIGN = +1;
constexpr int MOUSE_Y_SIGN = +1;

// Le framebuffer du Shifter est ARGB8888 (0xAARRGGBB). Téléversé en WebGL comme
// GL_RGBA/UNSIGNED_BYTE, les octets little-endian se lisent B,G,R,A : on rétablit
// l'ordre des canaux dans le fragment shader (.bgr) plutôt que de recopier.
const char* kVert =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  vUV = aUV;\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

const char* kFrag =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "void main() {\n"
    "  vec4 c = texture2D(uTex, vUV);\n"
    "  gl_FragColor = vec4(c.b, c.g, c.r, 1.0);\n"   // BGRA mémoire → RGB écran
    "}\n";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "[web] shader: %s\n", log);
    }
    return s;
}

void initGl() {
    // Quad plein écran. Coordonnées de texture : ligne 0 du Shifter (haut) en
    // haut de l'écran → v=0 sur les sommets supérieurs.
    const float quad[] = {
        //  x,    y,    u,   v
        -1.f,  1.f,  0.f, 0.f,   // haut-gauche
        -1.f, -1.f,  0.f, 1.f,   // bas-gauche
         1.f,  1.f,  1.f, 0.f,   // haut-droite
         1.f, -1.f,  1.f, 1.f,   // bas-droite
    };
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFrag);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glBindAttribLocation(g_prog, 0, "aPos");
    glBindAttribLocation(g_prog, 1, "aUV");
    glLinkProgram(g_prog);
    g_locPos = glGetAttribLocation(g_prog, "aPos");
    g_locUV  = glGetAttribLocation(g_prog, "aUV");
    g_locTex = glGetUniformLocation(g_prog, "uTex");

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void uploadFrame(const uint32_t* px, int w, int h) {
    glBindTexture(GL_TEXTURE_2D, g_tex);
    if (w != g_texW || h != g_texH) {     // la résolution ST a changé → réalloue
        g_texW = w; g_texH = h;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
}

void drawScreen() {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(g_window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_prog);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(g_locPos);
    glVertexAttribPointer(g_locPos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(g_locUV);
    glVertexAttribPointer(g_locUV, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(g_locTex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// --- Clavier : touche GLFW → scancode "make" Atari ST (cf. main.cpp natif) ---
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

void onKey(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
    if (!g_machine || action == GLFW_REPEAT) return;   // l'IKBD gère sa répétition
    if (key == GLFW_KEY_ESCAPE) {                       // Échap = touche hôte (libère la souris)
        if (g_mouseCaptured && action == GLFW_PRESS) {
            g_mouseCaptured = false;
            glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        return;
    }
    const uint8_t sc = glfwToStScancode(key);
    if (sc) g_machine->ikbd.keyEvent(sc, action == GLFW_PRESS);
}

void onMouseButton(GLFWwindow* w, int /*button*/, int /*action*/, int /*mods*/) {
    if (!g_machine) return;
    if (!g_mouseCaptured) {                 // premier clic : on capture la souris
        g_mouseCaptured = true;
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(w, &g_lastMx, &g_lastMy);
        return;
    }
    const bool l = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    const bool r = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    g_machine->ikbd.mouseEvent(0, 0, l, r);
}

// Boucle principale appelée par requestAnimationFrame. Une trame émulée par
// rappel (≈ 60 Hz ici, contre 50 Hz réels : la machine tourne légèrement vite,
// acceptable pour un test navigateur).
void mainLoop() {
    // À la toute première trame, on signale à la page que le runtime tourne
    // (le statut "Prêt" s'affiche alors de façon fiable, cf. shell.html).
    static bool announced = false;
    if (!announced) {
        announced = true;
        EM_ASM({ if (window.neostOnReady) window.neostOnReady(); });
    }

    glfwPollEvents();

    if (g_mouseCaptured) {                  // mouvement relatif → paquet IKBD
        double mx, my;
        glfwGetCursorPos(g_window, &mx, &my);
        const int dx = int(mx - g_lastMx), dy = int(my - g_lastMy);
        if (dx || dy) {
            g_lastMx += dx; g_lastMy += dy;
            const bool l = glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
            const bool r = glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            g_machine->ikbd.mouseEvent(dx * MOUSE_X_SIGN, dy * MOUSE_Y_SIGN, l, r);
        }
    }

    g_machine->cpu.updateIpl();
    g_machine->runFrame();
    uploadFrame(g_machine->shifter.pixels(),
                g_machine->shifter.width(), g_machine->shifter.height());
    drawScreen();
    glfwSwapBuffers(g_window);
}

} // namespace

// =============================================================================
//  API exportée vers le JavaScript de la page (shell HTML).
// =============================================================================
extern "C" {

EMSCRIPTEN_KEEPALIVE void neost_reset() {
    if (g_machine) g_machine->reset();
}

// Charge une autre ROM TOS (déjà présente dans le FS virtuel) et reset, comme un
// changement de cartouche : permet de tester EmuTOS US/FR et TOS 1.02 à distance.
EMSCRIPTEN_KEEPALIVE void neost_load_tos(const char* path) {
    if (!g_machine || !path) return;
    g_machine->loadTos(path);
    g_machine->reset();
}

// mono != 0 → moniteur monochrome (haute résolution) ; sinon couleur (basse rés).
// Comme sur le matériel, le type de moniteur est lu au reset → on reset.
EMSCRIPTEN_KEEPALIVE void neost_set_mono(int mono) {
    if (!g_machine) return;
    g_machine->mfp.setColorMonitor(mono == 0);
    g_machine->reset();
}

// Monte une image .st déjà écrite dans le FS virtuel (cf. shell : upload fichier
// → FS.writeFile → neost_mount_disk), puis reset pour booter dessus.
EMSCRIPTEN_KEEPALIVE void neost_mount_disk(const char* path) {
    if (!g_machine || !path) return;
    g_machine->fdc.loadImage(path, 0);
    g_machine->reset();
}

// Monte une image dans le lecteur B (secondaire) — pas de reset (B ne boote pas).
EMSCRIPTEN_KEEPALIVE void neost_mount_disk_b(const char* path) {
    if (!g_machine || !path) return;
    g_machine->fdc.loadImage(path, 1);
}

} // extern "C"

int main(int argc, char** argv) {
    const std::string romPath  = (argc > 1) ? argv[1] : "/rom/etos192us.img";
    const std::string diskPath = (argc > 2) ? argv[2] : "/disks/diskA.st";

    // Cœur CPU choisi AVANT le démarrage via le paramètre d'URL ?cpu=musashi|moira
    // (le sélecteur de la page recharge avec ce paramètre — cf. shell.html).
    char cpuBuf[16] = "musashi";
    EM_ASM({
        var c = new URLSearchParams(location.search).get('cpu') || 'musashi';
        stringToUTF8(c, $0, 16);
    }, cpuBuf);
    const CpuCore cpuCore = Cpu68k::parseCore(cpuBuf);

    if (!glfwInit()) { std::fprintf(stderr, "[web] glfwInit a échoué\n"); return 1; }

    // L'écran ST le plus grand est 640×400 (mono) ; le canvas est dimensionné par
    // la page. Pas de hint de profil : le port GLFW d'Emscripten crée un contexte
    // WebGL (GLES2) compatible avec nos shaders.
    g_window = glfwCreateWindow(640, 400, "NeoST — Atari ST (WASM)", nullptr, nullptr);
    if (!g_window) { std::fprintf(stderr, "[web] création fenêtre échouée\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(g_window);

    static Machine machine(512u * 1024u, cpuCore);   // cœur choisi (statique : survit à main())
    g_machine = &machine;
    std::fprintf(stderr, "[web] cœur CPU : %s\n", Cpu68k::coreName(machine.cpu.core()));
    if (!machine.loadTos(romPath))
        std::fprintf(stderr, "[web] TOS introuvable (%s) — CPU à vide.\n", romPath.c_str());
    if (!machine.loadDisk(diskPath))
        std::fprintf(stderr, "[web] disquette introuvable (%s).\n", diskPath.c_str());
    machine.mfp.setColorMonitor(true);  // couleur (basse rés) par défaut

    // Bruits mécaniques du lecteur : le cœur émet des FdcSound, la page les joue
    // via Web Audio (cf. shell.html, window.neostDriveSound). Codes : 0 = moteur,
    // 1 = pas (clic), 2 = seek — dans l'ordre de l'énum FdcSound.
    machine.fdc.setSoundSink([](FdcSound e) {
        EM_ASM({ if (window.neostDriveSound) window.neostDriveSound($0); }, static_cast<int>(e));
    });

    machine.reset();

    initGl();

    glfwSetKeyCallback(g_window, onKey);
    glfwSetMouseButtonCallback(g_window, onMouseButton);

    std::printf("[web] NeoST démarré. Clic = capture souris, Échap = libère.\n");

    // fps=0 → requestAnimationFrame ; simulate_infinite_loop=1 → main() ne rend
    // pas la main (le cœur reste vivant via la Machine statique).
    emscripten_set_main_loop(mainLoop, 0, 1);
    return 0;
}
