// =============================================================================
//  JoystickInput.hpp — Entrée joystick côté HÔTE pour les frontends GLFW.
//
//  Le cœur (neost_core) ne dépend ni de GLFW ni de l'OS : il expose seulement
//  Ikbd::setJoystick(joy0, joy1) qui pose l'état des deux ports joystick ST.
//  Ce header (inclus par les frontends natif et web — donc DÉPENDANT de GLFW,
//  jamais compilé dans le cœur) traduit les manettes USB détectées par GLFW et
//  une éventuelle émulation au clavier en ces deux octets.
//
//  Format d'octet IKBD (cf. Hatari ATARIJOY_BITMASK_*, includes/joy.h) :
//    bit0 = haut, bit1 = bas, bit2 = gauche, bit3 = droite, bit7 = feu.
//
//  Affectation des ports (comme la plupart des jeux ST) : la 1re manette physique
//  va au PORT 1 (le port « jeux »), la 2e au PORT 0 (partagé avec la souris).
//  L'émulation clavier vise un port configurable (défaut port 1). Les sources
//  visant le même port sont OR-ées : clavier OU manette font bouger pareil.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <GLFW/glfw3.h>
#include <cstdint>

namespace stjoy {

// Bits d'état joystick ST (cf. Hatari joy.h).
enum : uint8_t { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, FIRE = 0x80 };

// Mapping clavier de l'émulation : flèches + Ctrl droit (défaut façon Hatari).
// Renvoie le bit ST d'une touche GLFW, ou 0. Sert AUSSI au frontend pour
// DÉTOURNER ces touches du clavier ST quand l'émulation est active (sans quoi la
// touche aurait un double effet : direction joystick ET scancode clavier).
inline uint8_t kbdBit(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_UP:            return UP;
        case GLFW_KEY_DOWN:          return DOWN;
        case GLFW_KEY_LEFT:          return LEFT;
        case GLFW_KEY_RIGHT:         return RIGHT;
        case GLFW_KEY_RIGHT_CONTROL: return FIRE;
        default:                     return 0;
    }
}

// État d'une manette physique GLFW `jid` → octet ST. 0 si absente. `deadzone` =
// zone morte centrale [0,1) : un axe analogique n'est considéré « poussé » que si
// |valeur| dépasse ce seuil — évite le drift d'un stick mal centré (réglable par
// l'utilisateur). Le D-pad (numérique) n'est pas concerné.
//  - Natif (GLFW complet) : API gamepad (mapping SDL : Xbox/PS/Switch… reconnus)
//    quand disponible, sinon repli axes/boutons bruts.
//  - Web (port GLFW d'Emscripten) : pas de glfwGetGamepadState → on lit directement
//    les axes/boutons bruts (l'API Gamepad du navigateur expose le « standard
//    mapping » : axes 0/1 = stick gauche, boutons 12-15 = D-pad).
inline uint8_t readStick(int jid, float deadzone) {
    if (!glfwJoystickPresent(jid)) return 0;
    const float thr = (deadzone < 0.0f) ? 0.0f : (deadzone > 0.95f ? 0.95f : deadzone);
    uint8_t b = 0;

#ifndef __EMSCRIPTEN__
    GLFWgamepadstate gs;
    if (glfwGetGamepadState(jid, &gs)) {         // manette mappée (gamepad)
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP])    b |= UP;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN])  b |= DOWN;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT])  b |= LEFT;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]) b |= RIGHT;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -thr) b |= UP;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] >  thr) b |= DOWN;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -thr) b |= LEFT;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X] >  thr) b |= RIGHT;
        // Joystick ST = un seul bouton de feu : n'importe quel bouton d'action
        // (A/B/X/Y + gâchettes) le déclenche (D-pad et boutons système exclus).
        for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i)
            if (gs.buttons[i] &&
                i != GLFW_GAMEPAD_BUTTON_DPAD_UP   && i != GLFW_GAMEPAD_BUTTON_DPAD_DOWN &&
                i != GLFW_GAMEPAD_BUTTON_DPAD_LEFT && i != GLFW_GAMEPAD_BUTTON_DPAD_RIGHT &&
                i != GLFW_GAMEPAD_BUTTON_START     && i != GLFW_GAMEPAD_BUTTON_BACK &&
                i != GLFW_GAMEPAD_BUTTON_GUIDE) { b |= FIRE; break; }
        return b;
    }
#endif

    // Manette brute : stick gauche (axes 0/1) + boutons. Si la manette expose au
    // moins 16 boutons (« standard mapping » navigateur/SDL), les boutons 12-15
    // sont le D-pad → traités comme directions (et non comme feu) ; sinon tout
    // bouton est du feu (vieux sticks à un seul bouton réel).
    int axN = 0, btN = 0;
    const float*         ax = glfwGetJoystickAxes(jid, &axN);
    const unsigned char* bt = glfwGetJoystickButtons(jid, &btN);
    if (ax && axN >= 2) {
        if (ax[0] < -thr) b |= LEFT;  if (ax[0] > thr) b |= RIGHT;
        if (ax[1] < -thr) b |= UP;    if (ax[1] > thr) b |= DOWN;
    }
    if (bt) {
        const bool standard = btN >= 16;
        if (standard) {
            if (bt[12]) b |= UP;   if (bt[13]) b |= DOWN;
            if (bt[14]) b |= LEFT; if (bt[15]) b |= RIGHT;
        }
        const int fireMax = standard ? 12 : btN;   // exclut le D-pad du feu
        for (int i = 0; i < fireMax; ++i) if (bt[i]) { b |= FIRE; break; }
    }
    return b;
}

// Compose l'état des deux ports ST à partir de l'hôte et le pose sur l'IKBD.
//  - `kbdEnabled` : émulation clavier active (le frontend l'a déjà gatée sur le
//    focus, p.ex. pas pendant une saisie ImGui) ; vise le port `kbdPort` (0/1).
//  - `deadzone` : zone morte centrale des sticks analogiques (cf. readStick).
//  - 1re manette → port 1, 2e manette → port 0 (le reste est ignoré).
// `out0`/`out1` reçoivent les octets composés (port 0 / port 1).
inline void compose(GLFWwindow* win, bool kbdEnabled, int kbdPort, float deadzone,
                    uint8_t& out0, uint8_t& out1) {
    uint8_t p[2] = {0, 0};

    if (kbdEnabled) {
        uint8_t k = 0;
        if (glfwGetKey(win, GLFW_KEY_UP)            == GLFW_PRESS) k |= UP;
        if (glfwGetKey(win, GLFW_KEY_DOWN)          == GLFW_PRESS) k |= DOWN;
        if (glfwGetKey(win, GLFW_KEY_LEFT)          == GLFW_PRESS) k |= LEFT;
        if (glfwGetKey(win, GLFW_KEY_RIGHT)         == GLFW_PRESS) k |= RIGHT;
        if (glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) k |= FIRE;
        p[kbdPort & 1] |= k;
    }

    int found = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST && found < 2; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        p[(found == 0) ? 1 : 0] |= readStick(jid, deadzone);   // 1re → port 1, 2e → port 0
        ++found;
    }

    out0 = p[0];
    out1 = p[1];
}

} // namespace stjoy
