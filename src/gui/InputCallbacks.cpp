// =============================================================================
//  InputCallbacks.cpp — cf. InputCallbacks.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/InputCallbacks.hpp"

#include <cstdio>

#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#endif

#include "core/Machine.hpp"
#include "gui/App.hpp"
#include "gui/StKeys.hpp"
#include "io/JoystickInput.hpp"

namespace {
App& A = app();          // les callbacks GLFW n'ont pas d'argument de contexte
}

bool mouseReachesSt() { return !A.port0Joystick && !A.kioskDiskMenu; }

void onGlfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Callback bouton souris : ÉVÉNEMENTIEL (capte chaque transition, même un
// double-clic rapide qu'une scrutation par trame manquerait). Envoie un paquet
// IKBD sans mouvement portant l'état courant des boutons.
void onMouseButton(GLFWwindow* w, int button, int action, int /*mods*/) {
    // Le bouton central est un interrupteur hôte : il accroche/décroche la souris
    // sans envoyer de clic à l'Atari. La borne conserve son invariant de capture.
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS && !A.kiosk) A.mouseCaptureToggleReq = true;
        return;
    }
    if (!A.ikbd || !mouseReachesSt()) return;
    if (A.desktopMouse && !A.mouseCaptured && !A.kiosk) {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
            double mx, my; glfwGetCursorPos(w, &mx, &my);
            A.desktopPointer.button(button == GLFW_MOUSE_BUTTON_RIGHT, action == GLFW_PRESS, mx, my);
        }
        return;
    }
    if (!A.mouseCaptured) return;
    const bool l = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    const bool r = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (A.dbgMouse) std::fprintf(stderr, "[mouse] button  L=%d R=%d\n", l, r);
    A.ikbd->mouseEvent(0, 0, l, r);
}

// Clavier : touche GLFW → scancode ST. Tables et keymap international extraits
// vers gui/StKeys (A21, 2026-08-27) — module PARTAGÉ avec le frontend web, qui
// vivait sur une copie amputée (pavé numérique, Undo, Help et pays TOS absents).

// Callback clavier GLFW → IKBD. Les touches Atari, dont Suppr et Échap, sont
// transmises au ST (beaucoup de jeux/applications s'en servent).
void onKey(GLFWwindow*, int key, int scancode, int action, int mods) {
    if (!A.ikbd || action == GLFW_REPEAT) return;   // TOS gère sa propre répétition (pas l'IKBD)
    // Raccourci hôte : absorber aussi le relâchement si Alt est déjà relâché.
    static bool fullscreenChordHeld = false;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (action == GLFW_PRESS && (mods & GLFW_MOD_ALT)) {
            fullscreenChordHeld = true;
            if (A.kiosk && A.kioskDiskMenu) A.resumeAtariReq = true;
            else A.fullscreenToggleReq = !A.fullscreenToggleReq;
            return;
        }
        if (action == GLFW_RELEASE && fullscreenChordHeld) {
            fullscreenChordHeld = false;
            return;
        }
    }
    // Secours pour les trackpads et souris à deux boutons : même bascule que le clic
    // molette. Ctrl+Alt+G est réservé à l'hôte, mais G SEUL poursuit normalement
    // vers l'IKBD. Le latch absorbe aussi le BREAK du raccourci si Ctrl/Alt sont
    // relâchés avant G. En kiosk la capture reste imposée.
    static bool mouseCaptureChordHeld = false;
    if (key == GLFW_KEY_G) {
        if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
            mouseCaptureChordHeld = true;
            if (!A.kiosk) A.mouseCaptureToggleReq = true;
            return;
        }
        if (action == GLFW_RELEASE && mouseCaptureChordHeld) {
            mouseCaptureChordHeld = false;
            return;
        }
    }
    // Touches réservées HÔTE : F5/F7 (save-state),
    // F11 (bascule joystick clavier), + F9/F10 en kiosk (menu disques, zoom). Sans
    // cette exclusion, le ST recevait la touche F5/F7 EN MÊME TEMPS que l'état était
    // écrasé/rechargé sous ses pieds (beaucoup de jeux/GEM mappent les touches de fonction).
    // Ctrl+Alt+F : bascule bureau ⇄ borne, même depuis le menu, sous forme de chord hôte
    // (même discipline que Ctrl+Alt+G : F SEUL continue vers l'IKBD, le latch
    // absorbe le BREAK du chord si Ctrl/Alt sont relâchés avant F).
    static bool kioskChordHeld = false;
    if (key == GLFW_KEY_F) {
        if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_ALT)) {
            kioskChordHeld = true;
            A.kioskSwitchReq = A.kiosk ? 2 : 1;
            return;
        }
        if (action == GLFW_RELEASE && kioskChordHeld) {
            kioskChordHeld = false;
            return;
        }
    }
    // F5/F7 : latchés ICI comme Alt+Entrée (même justification anti-scrutation — un appui
    // bref entre deux tours de boucle était perdu, l'utilisateur croyait l'état
    // sauvé sans qu'il le soit). Consommés à la frontière de trame.
    if (key == GLFW_KEY_F5) { if (action == GLFW_PRESS) A.saveStateReq = true; return; }
    if (key == GLFW_KEY_F7) { if (action == GLFW_PRESS) A.loadStateReq = true; return; }
    if (key == GLFW_KEY_F11) return;
    // F12 n'a PAS de scancode ST (positionalScancode ne le mappe pas) : il ne peut donc
    // jamais laisser de touche collée, et se filtre ici sans risque.
    if (A.kiosk && key == GLFW_KEY_F12) return;
    const uint8_t sc = neost::stkeys::scancodeFor(key, scancode);   // symbolique (layout hôte + pays TOS) → positionnel
    if (!sc) return;
    // Suivi des touches dont le MAKE a été transmis au ST : leur BREAK doit
    // TOUJOURS partir, même si entre-temps un widget ImGui a pris le focus ou
    // que l'émulation joystick a été (dés)activée. Sinon la touche reste
    // « collée » côté ST (make sans break) et le clavier semble en panne.
    static bool stHeld[128] = {};
    if (action != GLFW_PRESS) {
        if (stHeld[sc & 0x7F]) {
            stHeld[sc & 0x7F] = false;
            A.ikbd->keyEvent(sc, false);
        }
        return;
    }
#if defined(NEOST_WITH_IMGUI)
    // On ne cède le clavier à ImGui (saisie d'un champ) QUE hors capture souris :
    // souris capturée = l'utilisateur « est dans » le ST, les touches (espace
    // inclus) doivent toujours l'atteindre, jamais être avalées par un widget
    // resté focalisé (sinon le clavier ST « se déconnecte »).
    if (!A.mouseCaptured && ImGui::GetIO().WantCaptureKeyboard) return;
#endif
    // F9/F10 sont des raccourcis HÔTE du kiosk : ils ne partent pas au ST. K a été
    // ABANDONNÉ comme raccourci — c'est une lettre, donc du jeu (taper ses initiales dans
    // une table des scores ouvrait le bandeau clavier) ; l'intercepter privait en plus le
    // ST du K sans même supprimer l'ouverture parasite, qui venait de la scrutation.
    // ⚠ APRÈS le bloc de relâchement, et pas avant. `A.kiosk` est une condition VOLATILE
    // — la bascule bureau ⇄ borne se fait au clavier — et F9/F10 ont, eux, un scancode ST
    // ($43 et $44). Placé plus haut, ce filtre était le SEUL à pouvoir avaler un BREAK :
    // un MAKE transmis en mode bureau, puis la bascule en borne pendant l'appui, et la
    // touche restait collée côté ST. C'est exactement l'invariant que le commentaire du
    // bloc de relâchement énonce, et que les trois filtres voisins respectent déjà.
    if (A.kiosk && (key == GLFW_KEY_F9 || key == GLFW_KEY_F10)) return;
    // Émulation joystick clavier active : les touches du joystick (flèches + Ctrl
    // droit) pilotent la manette et NE sont PAS transmises au clavier ST (sinon
    // double effet) ; elles sont scrutées par trame dans la boucle (cf. stjoy::compose).
    if (A.kbdJoy && stjoy::kbdBit(key)) return;
    // Overlay kiosk de choix de disquette ouvert : le clavier pilote l'overlay
    // (flèches/Entrée/Échap), on ne transmet PAS le MAKE au ST. Les BREAK des
    // touches déjà tenues sont gérés plus haut → pas de touche « collée ».
    if (A.kioskDiskMenu) return;
    stHeld[sc & 0x7F] = true;
    A.ikbd->keyEvent(sc, true);
}
