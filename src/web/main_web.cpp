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
#include "core/Pacing.hpp"
#include <GLFW/glfw3.h>

#include "gui/StKeys.hpp"
#include <GLES2/gl2.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/AudioMix.hpp"
#include "core/Framing.hpp"
#include "core/Machine.hpp"
#include "io/JoystickInput.hpp"

namespace {

Machine* g_machine = nullptr;            // carte mère (allouée dans main)
MachineType g_reqMachine = MachineType::St;  // machine DEMANDÉE (?machine=…) — cf. neost_load_tos
GLFWwindow* g_window = nullptr;
bool   g_kbdJoy = false;                 // émulation joystick clavier (flèches + Ctrl droit)
int    g_kbdJoyPort = 1;                 // port ST visé par l'émulation clavier
float  g_joyDeadzone = 0.30f;            // zone morte centrale des sticks analogiques

// --- État vidéo WebGL --------------------------------------------------------
GLuint g_tex = 0, g_prog = 0, g_vbo = 0;
GLint  g_locPos = -1, g_locUV = -1, g_locTex = -1;
int    g_texW = 0, g_texH = 0;

// --- Sortie audio : modèle « push », comme le frontend natif ------------------
// AVANT : la page tirait des échantillons quand son ScriptProcessorNode le
// demandait (~toutes les 43 ms) et le cœur synthétisait alors en lisant les
// registres du YM EN DIRECT. Tout ce qui module le son SOUS la trame — digidrums
// (volume écrit à plusieurs kHz), sync-buzzer, bruitages DMA courts — était donc
// échantillonné une seule fois par bloc : les samples devenaient inaudibles.
//
// MAINTENANT : le son est produit PAR TRAME ÉMULÉE, juste après runFrame, par la
// chaîne partagée du cœur (core/AudioMix.cpp) qui REJOUE les écritures horodatées
// à leur cycle. La page ne fait plus que mettre en file et sortir — elle nous
// renvoie la profondeur de sa file, dont on asservit le débit.
neost::FrameMixBuffers g_mixBuf;
uint32_t g_audioRate     = 0;      // 0 = sortie fermée (rien n'est produit)
neost::pacing::AudioPacer g_pacer;  // A28 : report fractionnaire + servo + rampe anti-clic
int      g_queuedFrames  = 0;      // profondeur de la file de la page (frames)
int      g_cushionFrames = 0;      // coussin visé (frames) — cf. neost_audio_open
float    g_masterVol     = 1.0f;   // volume maître utilisateur (0..1)
uint32_t g_audioUnderruns = 0;     // signalés par la page (diagnostic)

// Plein écran (posé par le shell sur fullscreenchange) : l'image passe alors en
// ZOOM ADAPTATIF — cadrée sur la région de contenu (règle du kiosk, calcul
// partagé core/Framing), buffer entier dès qu'une démo ouvre les bordures.
bool   g_fullscreen = false;

// Zoom adaptatif : cadrer sur la ZONE ACTIVE plutôt que sur le buffer entier.
// ACTIF PAR DÉFAUT, y compris en fenêtré — sans lui la page s'ouvre sur un cadre
// bordé de noir, alors que la région de contenu est calculée à chaque trame de
// toute façon (cf. stContentRegion plus bas) et qu'une démo qui ouvre les
// bordures élargit le cadre toute seule. C'était auparavant lié au plein écran :
// une seule variable portait DEUX rôles — le cadrage, et « qui possède la taille
// du canvas » (en plein écran c'est Emscripten). Les deux sont maintenant séparés.
bool   g_autoZoom = true;

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
        glDeleteShader(s);
        return 0;                 // 0, PAS le shader cassé : cf. initGl
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

    // Statuts VÉRIFIÉS, contrairement à la version d'origine : un shader qui ne
    // compilait pas était quand même attaché, le link échouait, et les
    // glGetAttribLocation rendaient -1 — que drawScreen passait ensuite chaque trame
    // à glEnableVertexAttribArray/glVertexAttribPointer (donc 0xFFFFFFFF) : erreurs GL
    // en boucle, canvas noir, et pour seul indice un log noyé dans la console. Le
    // frontend desktop (gui/OpenGLShader.cpp) faisait déjà tout cela correctement.
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVert);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) {
        std::fprintf(stderr, "[web] shaders did not compile — rendering disabled\n");
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        g_prog = 0; g_locPos = g_locUV = -1;
        return;
    }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glBindAttribLocation(g_prog, 0, "aPos");
    glBindAttribLocation(g_prog, 1, "aUV");
    glLinkProgram(g_prog);
    GLint linked = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(g_prog, sizeof log, nullptr, log);
        std::fprintf(stderr, "[web] program link: %s\n", log);
        glDeleteProgram(g_prog); g_prog = 0;
    }
    // Les shaders sont référencés par le programme : plus besoin des objets.
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!g_prog) { g_locPos = g_locUV = -1; return; }
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

// Taille d'AFFICHAGE d'un buffer ST, aspect pixel compris — même règle que le
// frontend bureau (cf. drawStScreen/drawStKiosk dans main.cpp) : les pixels de
// basse résolution sont deux fois plus larges ET deux fois plus hauts que ceux
// de la mono, si bien que 320×200, 640×200 et 640×400 couvrent la MÊME surface
// à l'écran. On classe donc par dimension : ≤ 480 px de large = classe basse
// rés (×2), ≤ 300 lignes = classe 200 lignes (×2).
void displaySize(int w, int h, int& dw, int& dh) {
    dw = w * ((w <= 480) ? 2 : 1);
    dh = h * ((h <= 300) ? 2 : 1);
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

// Taille INTRINSÈQUE du canvas : c'est elle qui fixe le RATIO — la page met
// `width:100%; height:auto` en fenêtré et `object-fit:contain` en plein écran.
// Elle suit donc ce qu'on DESSINE (la VUE), pas la résolution du Shifter : le
// buffer entier en fenêtré, la seule région de contenu en plein écran adaptatif.
// Sans ça, une trame overscan 416×276 (affichage correct 832×552, ratio 1.507)
// était étirée de 6 % dans le cadre 1.6 initial — et le recadrage plein écran
// serait pareillement déformé si le canvas gardait le ratio du cadre complet.
void syncCanvasSize(int viewW, int viewH) {
    int dw = 0, dh = 0;
    displaySize(viewW, viewH, dw, dh);
    // Comparer à la taille RÉELLE du canvas, pas à la dernière posée : à la
    // sortie du plein écran, le port GLFW d'Emscripten laisse le canvas à la
    // taille de l'ÉCRAN — un cache « dernière valeur » croirait n'avoir rien à
    // faire et la page garderait un ratio faux.
    int cw = 0, ch = 0;
    glfwGetWindowSize(g_window, &cw, &ch);
    if (cw == dw && ch == dh) return;
    glfwSetWindowSize(g_window, dw, dh);   // port GLFW Emscripten → taille du canvas
    std::fprintf(stderr, "[web] view %dx%d → canvas %dx%d\n", viewW, viewH, dw, dh);
}

// [u0,v0]–[u1,v1] = portion du buffer ST affichée ; viewAspect = ratio
// d'AFFICHAGE de cette portion (aspect pixel ST compris).
//
// LETTERBOX AU VIEWPORT, et non « le canvas fait déjà le bon ratio » : en plein
// écran, le port GLFW d'Emscripten redimensionne LUI-MÊME le canvas à la taille
// de l'écran (mesuré : 640×400 demandés, 800×600 imposés) — dessiner plein cadre
// y étirerait l'image au ratio de l'écran. On centre donc un viewport au ratio
// de la vue ; en fenêtré, le canvas suit la vue (syncCanvasSize) et le letterbox
// est neutre. Même recette que drawStKiosk et que le frontend Android.
void drawScreen(float u0, float v0, float u1, float v1, float viewAspect) {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(g_window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);            // effacement : tout le canvas
    glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    // Programme absent (compilation/link en échec, cf. initGl) : on s'arrête après
    // l'effacement plutôt que d'émettre des appels GL avec des locations à -1.
    if (!g_prog || g_locPos < 0 || g_locUV < 0) return;

    int vpW = fbw, vpH = fbh;
    const float fbAspect = (fbh > 0) ? float(fbw) / float(fbh) : viewAspect;
    if (fbAspect > viewAspect) vpW = int(float(fbh) * viewAspect + 0.5f);   // pillarbox
    else                       vpH = int(float(fbw) / viewAspect + 0.5f);   // letterbox
    glViewport((fbw - vpW) / 2, (fbh - vpH) / 2, std::max(1, vpW), std::max(1, vpH));

    glUseProgram(g_prog);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    // Le quad ne change que si le CADRAGE change (entrée/sortie de plein écran,
    // latch overscan) : on ne réécrit pas 16 floats par trame pour rien.
    static float lu0 = 0.f, lv0 = 0.f, lu1 = 1.f, lv1 = 1.f;
    if (u0 != lu0 || v0 != lv0 || u1 != lu1 || v1 != lv1) {
        const float quad[] = {
            //  x,    y,    u,  v
            -1.f,  1.f,  u0, v0,   // haut-gauche
            -1.f, -1.f,  u0, v1,   // bas-gauche
             1.f,  1.f,  u1, v0,   // haut-droite
             1.f, -1.f,  u1, v1,   // bas-droite
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_DYNAMIC_DRAW);
        lu0 = u0; lv0 = v0; lu1 = u1; lv1 = v1;
    }
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

// Clavier : touche GLFW → scancode ST — gui/StKeys, module PARTAGÉ avec le GUI
// (A21, 2026-08-27). La copie locale s'arrêtait avant le pavé numérique, Undo et
// Help, et n'avait pas le keymap international : AZERTY sous TOS FR tapait en
// QWERTY dans le navigateur.

void setMouseCaptured(GLFWwindow* w, bool captured) {
    g_mouseCaptured = captured;
    glfwSetInputMode(w, GLFW_CURSOR,
                     g_mouseCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (g_mouseCaptured) glfwGetCursorPos(w, &g_lastMx, &g_lastMy);
}

void onKey(GLFWwindow* w, int key, int /*scancode*/, int action, int mods) {
    if (!g_machine || action == GLFW_REPEAT) return;   // l'IKBD gère sa répétition
    // Repli indispensable aux trackpads / souris sans bouton central. Le keydown est
    // aussi un geste utilisateur valide pour demander le pointer lock du navigateur.
    // Seul le CHORD est réservé : un G sans Ctrl+Alt continue vers l'IKBD.
    static bool mouseCaptureChordHeld = false;
    if (key == GLFW_KEY_G) {
        if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
            mouseCaptureChordHeld = true;
            setMouseCaptured(w, !g_mouseCaptured);
            return;
        }
        if (action == GLFW_RELEASE && mouseCaptureChordHeld) {
            mouseCaptureChordHeld = false;
            return;
        }
    }
    // Émulation joystick clavier active : les touches du joystick (flèches + Ctrl
    // droit) pilotent la manette et ne sont PAS transmises au clavier ST.
    if (g_kbdJoy && stjoy::kbdBit(key)) return;
    const uint8_t sc = neost::stkeys::scancodeFor(key, 0);
    if (sc) g_machine->ikbd.keyEvent(sc, action == GLFW_PRESS);
}

void onMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    if (!g_machine) return;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) setMouseCaptured(w, !g_mouseCaptured);
        return;
    }
    if (!g_mouseCaptured) return;
    const bool l = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    const bool r = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    g_machine->ikbd.mouseEvent(0, 0, l, r);
}

// Produit le son d'UNE trame émulée et le remet à la page. À appeler APRÈS
// Machine::runFrame, et à CHAQUE trame : les écritures horodatées du YM et les
// événements DMA appartiennent à la trame qui vient de s'exécuter, et doivent être
// consommés là — sinon ils s'accumulent sans fin (fuite mémoire) et le son se
// désaligne. Sortie fermée (avant le geste utilisateur, onglet muet) : on appelle
// quand même la chaîne avec 0 échantillon, ce qui ne fait que DRAINER.
void produceAudioFrame() {
    Machine& m = *g_machine;
    const int64_t fc = m.frameCycles();
    if (g_audioRate == 0) { neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, fc, g_mixBuf); return; }

    // Débit : durée émulée × fréquence de sortie, avec report fractionnaire (la
    // moyenne colle EXACTEMENT au temps émulé). Puis asservissement proportionnel
    // vers le coussin, comme le natif : ±8 échantillons sur ~960, soit ≤ 0,8 % de
    // hauteur — inaudible, mais suffisant pour absorber la dérive entre l'horloge
    // de l'AudioContext et celle de la machine (elles ne sont PAS les mêmes).
    // A28 : le calcul est PARTAGÉ (core/Pacing.hpp) — il était recopié à
    // l'identique ici, dans le GUI natif et dans l'Android.
    const int n = g_pacer.samplesForFrame(fc, g_audioRate, g_cushionFrames, g_queuedFrames);
    if (n <= 0) { neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, fc, g_mixBuf); return; }

    // Chaîne PARTAGÉE avec le GUI et le headless (core/AudioMix.cpp) : YM horodaté,
    // DMA STE horodaté, HPF, gains et tonalité LMC1992. La branche DMA est gatée par
    // le modèle courant — sur ST/Mega ST il n'y a pas de LMC, et son gain de
    // rattrapage ×2 doublerait un YM déjà à pleine échelle.
    float* st = neost::mixEmulatedFrame(m.psg, &m.dmasnd, machineHasDmaSound(m.bus.machine),
                                        uint32_t(n), g_audioRate, fc, g_mixBuf);
    if (!st) return;

    // Volume maître en RAMPE sur le bloc (un saut poserait une marche par bloc :
    // clic audible au mute et « zipper » en glissant le curseur), puis clamp.
    g_pacer.applyMasterVolume(st, n, g_masterVol);
    neost::pacing::AudioPacer::clampStereo(st, n);

    // Estimation locale entre deux rapports de la page : sans elle, l'asservissement
    // verrait une file figée pendant plusieurs trames et sur-corrigerait.
    // Bornée à 1 s : contexte audio suspendu (page jamais cliquée), la page jette les
    // blocs sans rappeler neost_audio_set_queued — le cumul débordait l'int (UB, ~13 h).
    g_queuedFrames = std::min(g_queuedFrames + n, int(g_audioRate));
    EM_ASM({ if (window.neostAudioPush) window.neostAudioPush($0, $1); }, st, n);
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

    // Joystick hôte → IKBD (manettes via l'API Gamepad du navigateur + émulation
    // clavier). Scruté chaque trame, comme le frontend natif.
    {
        uint8_t joy0 = 0, joy1 = 0;
        stjoy::compose(g_window, g_kbdJoy, g_kbdJoyPort, g_joyDeadzone, joy0, joy1);
        g_machine->ikbd.setJoystick(joy0, joy1);
        g_machine->bus.stePads.setJoystick(joy0, joy1);   // joypads STE ($FF9200/02)
    }

    g_machine->cpu.updateIpl();

    // CADENCE : sur le TEMPS ÉMULÉ, pas sur requestAnimationFrame.
    //
    // La boucle exécutait UNE trame par tick rAF, c'est-à-dire à la fréquence de
    // rafraîchissement de l'ÉCRAN. Sur un moniteur 60 Hz, une machine PAL (50 Hz)
    // tournait donc à 120 % : musique et bruitages trop rapides, et pire encore
    // sur un écran 120/144 Hz. Le son sortait « bizarre » sans que rien ne soit
    // faux dans la synthèse — c'est l'horloge de la machine qui était fausse.
    //
    // Même modèle que le frontend natif : `g_emuNextMs` est l'échéance réelle de
    // la PROCHAINE trame émulée, et chaque trame la repousse de SA durée (la
    // géométrie vidéo décide : 50, 60 ou 71 Hz). Un tour rAF exécute donc 0, 1 ou
    // 2 trames selon ce que le temps réel réclame. Plafond à 4 pour ne pas
    // spiraler après un onglet mis en arrière-plan (rAF y est suspendu).
    // A28 : la boucle de rattrapage est PARTAGÉE (core/Pacing.hpp) — même
    // algorithme, au caractère près, que celle du frontend Android.
    static neost::pacing::FramePacer g_framePacer;
    const double nowMs = emscripten_get_now();
    const int ran = g_framePacer.runDue(nowMs, [] {
        g_machine->runFrame();
        produceAudioFrame();          // le son suit la trame, pas le rythme de l'écran
        return g_machine->frameCycles();
    });

    // Aucune trame due (écran plus rapide que la machine) : rien de neuf à
    // montrer, on garde l'image précédente plutôt que de re-téléverser la même.
    if (ran == 0) return;

    const int w = g_machine->shifter.width(), h = g_machine->shifter.height();
    uploadFrame(g_machine->shifter.pixels(), w, h);

    // Région de contenu calculée à CHAQUE trame rendue, plein écran ou non :
    // l'hystérésis se compte en trames, et le passage en plein écran hérite
    // ainsi d'un latch déjà à jour au lieu de partir à froid.
    int cTop = 0, cH = h, cW = w;
    neost::stContentRegion(g_machine->shifter, cTop, cH, cW);

    float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    int viewW = w, viewH = h;
    if (g_fullscreen || g_autoZoom) {
        // Bornage défensif, comme le bureau : la région vient du Glue LIVE, une
        // trame de transition (changement de résolution) peut la donner hors du
        // buffer courant.
        const int visTop = std::max(0, std::min(cTop, std::max(0, h - 1)));
        const int visH   = std::max(1, std::min(cH, h - visTop));
        const int visW   = std::max(1, std::min(cW, w));
        const int visX   = (w - visW) / 2;             // zone active centrée
        u0 = float(visX) / float(w);   u1 = float(visX + visW) / float(w);
        v0 = float(visTop) / float(h); v1 = float(visTop + visH) / float(h);
        viewW = visW; viewH = visH;
    }
    // En plein écran, le canvas appartient à Emscripten (taille écran) : le poser
    // nous-mêmes déclencherait un bras de fer à chaque trame. Le ratio est garanti
    // par le viewport de drawScreen, pas par le canvas.
    if (!g_fullscreen) syncCanvasSize(viewW, viewH);
    int dispW = 0, dispH = 0;
    displaySize(viewW, viewH, dispW, dispH);
    drawScreen(u0, v0, u1, v1, (dispH > 0) ? float(dispW) / float(dispH) : 4.f / 3.f);
    glfwSwapBuffers(g_window);
}

} // namespace

// =============================================================================
//  API exportée vers le JavaScript de la page (shell HTML).
// =============================================================================
extern "C" {

// Reset À CHAUD (bouton RESET de la machine) : les puces repartent aux valeurs de
// reset, la RAM est CONSERVÉE — le TOS voit son « memvalid » intact et refait un
// boot court.
EMSCRIPTEN_KEEPALIVE void neost_reset() {
    if (g_machine) g_machine->reset();
}

// Reset À FROID (power-cycle) : efface la ST-RAM, donc le TOS refait un boot
// COMPLET (re-détection mémoire, re-init OS). C'est ce que fait « Hard Reset »
// dans le GUI bureau (AppLoop.cpp) et « Redémarrer » dans le menu Android — le
// web était le seul frontend à ne pas l'exposer, alors même que son bouton
// s'annonçait « Cold reset of the machine ».
EMSCRIPTEN_KEEPALIVE void neost_hard_reset() {
    if (g_machine) g_machine->hardReset();
}

// Charge une autre ROM TOS (déjà présente dans le FS virtuel) et reset, comme un
// changement de cartouche : permet de tester EmuTOS US/FR et TOS 1.02 à distance.
EMSCRIPTEN_KEEPALIVE void neost_load_tos(const char* path) {
    if (!g_machine || !path) return;
    // Garde machine/TOS comme le GUI et le headless (adjustMachineForTos) : un
    // TOS ≤ 1.04 chargé sur STE/Mega STE (menu ROM du shell) haltait le CPU —
    // écran figé sans message. On bascule le profil comme les autres frontends.
    // Ajuster depuis la machine DEMANDÉE (pas la courante) : sinon la bascule
    // STE→ST d'un vieux TOS était définitive — recharger ensuite un TOS moderne
    // laissait la session en ST jusqu'au rechargement de la page.
    const MachineType adj = Machine::adjustMachineForTos(g_reqMachine, path);
    if (adj != g_machine->bus.machine)
        g_machine->reconfigure(g_machine->bus.ram.size(), CpuCore::Moira, adj);
    g_machine->loadTos(path);
    neost::stkeys::setCountryFromTos(g_machine->bus.rom);   // pays clavier de la nouvelle ROM
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
    // Boot à FROID, comme le montage à chaud du GUI bureau (AppLoop.cpp pose
    // reqHardReset). Un reset à chaud laisse la RAM de la session précédente et
    // son « memvalid » : le TOS saute le boot complet, et une démo chargée par
    // dessus les restes d'une autre peut démarrer de travers.
    g_machine->hardReset();
}

// Monte une image dans le lecteur B (secondaire) — pas de reset (B ne boote pas).
EMSCRIPTEN_KEEPALIVE void neost_mount_disk_b(const char* path) {
    if (!g_machine || !path) return;
    g_machine->fdc.loadImage(path, 1);
}

// Émulation joystick au clavier (flèches + Ctrl droit) : enabled != 0 active,
// port = 0/1 (défaut 1 = port « jeux »). Les manettes physiques (API Gamepad du
// navigateur) sont toujours scrutées, indépendamment de ce réglage.
EMSCRIPTEN_KEEPALIVE void neost_set_kbd_joystick(int enabled, int port) {
    g_kbdJoy     = (enabled != 0);
    g_kbdJoyPort = (port == 0) ? 0 : 1;
}

// Zone morte centrale des sticks analogiques (anti-drift), fraction [0,0.95].
// Le D-pad numérique n'est pas concerné.
EMSCRIPTEN_KEEPALIVE void neost_set_joy_deadzone(float dz) {
    g_joyDeadzone = (dz < 0.0f) ? 0.0f : (dz > 0.95f ? 0.95f : dz);
}

// --- Sortie audio pilotée par la page ---------------------------------------
// La page ouvre la sortie une fois son AudioContext créé (elle seule connaît la
// fréquence réelle : 48 000 chez les uns, 44 100 chez les autres) et annonce le
// coussin qu'elle tient. Tant que ce n'est pas fait, rien n'est produit — mais les
// horodatages sont drainés à chaque trame (cf. produceAudioFrame).
// Le shell nous signale l'état plein écran (fullscreenchange) : c'est LUI qui
// sait, le port GLFW d'Emscripten ne voit pas un requestFullscreen fait en JS.
EMSCRIPTEN_KEEPALIVE void neost_set_fullscreen(int on) { g_fullscreen = (on != 0); }

// Zoom adaptatif (cadrage sur la zone active) — actif par défaut, cf. g_autoZoom.
// Le couper redonne le buffer entier, bordures noires comprises.
EMSCRIPTEN_KEEPALIVE void neost_set_auto_zoom(int on) { g_autoZoom = (on != 0); }

EMSCRIPTEN_KEEPALIVE void neost_audio_open(int rate, int cushionMs) {
    if (rate <= 0) return;
    if (cushionMs < 20)  cushionMs = 20;
    if (cushionMs > 250) cushionMs = 250;
    g_audioRate     = static_cast<uint32_t>(rate);
    g_cushionFrames = rate * cushionMs / 1000;
    g_pacer.reset();
    g_queuedFrames  = 0;
    std::fprintf(stderr, "[web] audio out: %d Hz stereo, cushion %d ms (%d frames)\n",
                 rate, cushionMs, g_cushionFrames);
}

// Ferme la sortie (onglet muet, échec Web Audio) : la production s'arrête, le cœur
// continue de tourner. Rouvrir avec neost_audio_open repart d'un coussin neuf.
EMSCRIPTEN_KEEPALIVE void neost_audio_close() { g_audioRate = 0; g_queuedFrames = 0; }

// La page annonce ce qu'il lui reste en file (frames par canal) : c'est l'entrée de
// l'asservissement de débit. `underruns` = compteur cumulé, pour le diagnostic.
EMSCRIPTEN_KEEPALIVE void neost_audio_set_queued(int frames, int underruns) {
    g_queuedFrames = frames < 0 ? 0 : frames;
    if (uint32_t(underruns) != g_audioUnderruns) {
        g_audioUnderruns = uint32_t(underruns);
        std::fprintf(stderr, "[web] audio underrun (total %u) — the emulation loop is "
                             "not keeping up with real time\n", g_audioUnderruns);
    }
}

// Volume maître utilisateur (0..1), appliqué en rampe au mix final. Indépendant du
// LMC1992 ÉMULÉ, qui appartient à la machine.
EMSCRIPTEN_KEEPALIVE void neost_set_volume(float v) {
    g_masterVol = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
EMSCRIPTEN_KEEPALIVE float neost_get_volume() { return g_masterVol; }

} // extern "C"

int main(int argc, char** argv) {
    const std::string diskPath = (argc > 2) ? argv[2] : "/disks/diskA.st";

    // NeoST n'a plus qu'un seul cœur 68000 : Moira (cycle-exact). Le paramètre d'URL
    // ?cpu= est encore lu pour rétro-compat (ancienne valeur "musashi" tolérée puis
    // ramenée à Moira avec un avertissement, cf. Cpu68k::parseCore).
    char cpuBuf[16] = "moira";
    EM_ASM({
        var c = new URLSearchParams(location.search).get('cpu') || 'moira';
        stringToUTF8(c, $0, 16);
    }, cpuBuf);
    const CpuCore cpuCore = Cpu68k::parseCore(cpuBuf);

    // Profil machine choisi par ?machine=st|megast|ste|megaste (défaut ST).
    // ST et non Mega STE : c'est la machine de référence des jeux et démos de
    // l'époque, et celle qu'attend un visiteur qui découvre la démo sans lire
    // la doc. Le Mega STE reste à un paramètre d'URL.
    char machBuf[16] = "st";
    EM_ASM({
        var m = new URLSearchParams(location.search).get('machine') || 'st';
        stringToUTF8(m, $0, 16);
    }, machBuf);
    const MachineType machType = parseMachine(machBuf);

    // Taille de ST-RAM choisie par ?mem=256k|512k|1m|2m|2176k|2560k|4m (défaut 1 Mo).
    char memBuf[16] = "1m";
    EM_ASM({
        var m = new URLSearchParams(location.search).get('mem') || '1m';
        stringToUTF8(m, $0, 16);
    }, memBuf);
    const std::size_t ramBytes = parseRamBytes(memBuf);

    // ROM par défaut adaptée à la machine, et TOUTES libres — le bundle web est
    // public. ST / Mega ST : **EmuTOS 192 Ko**, qui est justement le build
    // « Atari ST ». STE et Mega STE : **EmuTOS 256 Ko**, le seul EmuTOS qui
    // programme le SCU (cf. CLAUDE.md : 192 Ko = ST/Mega ST, 256 Ko = STE/Mega STE).
    // ⚠ Le défaut du STE était resté `tos162uk.img`, posé le 2026-08-02 et jamais
    // revu — or la purge du 2026-08-30 a sorti cette ROM du bundle. L'enchaînement
    // était silencieux et faux : `adjustMachineForTos` ne pouvant pas OUVRIR le
    // fichier rendait la machine demandée telle quelle, la Machine était donc
    // construite en STE, `loadTos` échouait, et le repli chargeait EmuTOS 192 Ko —
    // une ROM ST-only — SANS rejouer la garde. Le STE public démarrait ainsi sur un
    // TOS qui ne le connaît pas. Pointer une ROM réellement embarquée referme le cas
    // à la source, avant même que le repli n'entre en jeu.
    const bool wantsSte     = (machType == MachineType::Ste);
    const bool wantsMegaSte = (machType == MachineType::MegaSte);
    const std::string romPath = (argc > 1) ? argv[1]
        : (wantsSte || wantsMegaSte ? "/roms/etos256us.img"
                                    : "/roms/etos192us.img");

    if (!glfwInit()) { std::fprintf(stderr, "[web] glfwInit failed\n"); return 1; }

    // L'écran ST le plus grand est 640×400 (mono) ; le canvas est dimensionné par
    // la page. Pas de hint de profil : le port GLFW d'Emscripten crée un contexte
    // WebGL (GLES2) compatible avec nos shaders.
    // 640×400 n'est qu'une amorce : uploadFrame() redimensionne le canvas à la
    // taille d'affichage réelle dès la première trame, puis à chaque changement
    // de résolution.
    g_window = glfwCreateWindow(640, 400, "NeoST — Atari ST (WASM)", nullptr, nullptr);
    if (!g_window) { std::fprintf(stderr, "[web] window creation failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(g_window);

    // Garde machine/TOS au boot, comme le headless (main_headless.cpp) : un
    // ?machine=ste|megaste avec un vieux TOS en argv figeait le CPU sans message.
    g_reqMachine = machType;               // mémorisé pour neost_load_tos (menu ROM)
    const MachineType machTypeAdj = Machine::adjustMachineForTos(machType, romPath);
    static Machine machine(ramBytes, cpuCore, machTypeAdj); // RAM+cœur+machine (statique)
    g_machine = &machine;
    std::fprintf(stderr, "[web] CPU core: %s | machine: %s | RAM: %s\n",
                 Cpu68k::coreName(machine.cpu.core()), machineName(machTypeAdj), ramLabel(ramBytes));
    if (!machine.loadTos(romPath)) {
        std::fprintf(stderr, "[web] TOS not found (%s) — falling back to EmuTOS 192 KB.\n", romPath.c_str());
        if (romPath != "/roms/etos192us.img" && !machine.loadTos("/roms/etos192us.img"))
            std::fprintf(stderr, "[web] no loadable TOS — CPU running on nothing.\n");
    }
    neost::stkeys::setCountryFromTos(machine.bus.rom);   // pays clavier du TOS de boot
    if (!machine.loadDisk(diskPath))
        std::fprintf(stderr, "[web] floppy not found (%s).\n", diskPath.c_str());
    machine.mfp.setColorMonitor(true);  // couleur (basse rés) par défaut

    // FDC RAPIDE par défaut en WASM (délais commande/transfert ÷10, ≙ --fastfdc du
    // headless et la case « Fast floppy » du GUI). Le navigateur est le seul frontend
    // où l'on ne peut RIEN régler avant de démarrer : pas de neost.cfg, pas de ligne
    // de commande. Or une disquette chargée à la vitesse d'un vrai lecteur donne un
    // écran figé de plusieurs dizaines de secondes — le visiteur d'une page conclut
    // que l'émulateur est planté avant d'avoir vu quoi que ce soit. Les autres
    // frontends gardent le défaut FIDÈLE (lent), qui reste ce que les étalons mesurent.
    machine.fdc.setFastFdc(true);

    // MODÈLE « PUSH » (comme le GUI et le headless) : on ARME l'horodatage des
    // écritures du PSG et des transitions PLAY/STOP du son DMA. Dès lors chaque
    // écriture porte son cycle DANS la trame, et produceAudioFrame les REJOUE à
    // leur instant — c'est ce qui rend les digidrums, le sync-buzzer et les
    // bruitages courts. Ces deux lignes ne sont pas un réglage : sans elles,
    // synthesizeFrame rend le jeu de registres « audio » que plus rien ne met à
    // jour, et la machine devient MUETTE.
    machine.psg.setCycleClock([] { return g_machine->frameRelCycle(); });
    machine.dmasnd.setCycleClock([] { return g_machine->frameRelCycle(); });

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

    std::printf("[web] NeoST started. Middle mouse button or Ctrl+Alt+G = capture/release.\n");

    // fps=0 → requestAnimationFrame ; simulate_infinite_loop=1 → main() ne rend
    // pas la main (le cœur reste vivant via la Machine statique).
    emscripten_set_main_loop(mainLoop, 0, 1);
    return 0;
}
