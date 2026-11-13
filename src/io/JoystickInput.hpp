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
//  Affectation des ports : par défaut (AUTO, comme la plupart des jeux ST) la
//  1re manette physique va au PORT 1 (le port « jeux »), la 2e au PORT 0 (partagé
//  avec la souris). Chaque manette peut être ÉPINGLÉE à un port ou coupée (OFF)
//  via `roles` (menu kiosk « Joysticks », persisté par GUID — cf. resolveAssign).
//  L'émulation clavier vise un port configurable (défaut port 1). Les sources
//  visant le même port sont OR-ées : clavier OU manette font bouger pareil.
//  Boutons X/Y (ou équivalents bruts) = touches ESPACE/RETURN (cf. readAux).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <GLFW/glfw3.h>
#include <cstdint>
#include <cstdio>

namespace stjoy {

// Bits d'état joystick ST (cf. Hatari joy.h).
enum : uint8_t { UP = 0x01, DOWN = 0x02, LEFT = 0x04, RIGHT = 0x08, FIRE = 0x80 };

// Rôle d'une manette HÔTE : à quel port ST elle est affectée. AUTO = affectation
// historique (1re manette présente → port 1, 2e → port 0) sur les ports qu'aucune
// manette ÉPINGLÉE n'occupe ; PORT0/PORT1 = épinglée (plusieurs manettes sur le
// même port sont OR-ées) ; OFF = ignorée. Choisi dans le menu kiosk « Joysticks »
// et persisté par GUID (cf. main.cpp joymap=).
enum : int8_t { ROLE_AUTO = -1, ROLE_PORT0 = 0, ROLE_PORT1 = 1, ROLE_OFF = 2 };

// Boutons AUXILIAIRES d'une manette → touches ST (jeux « PRESS SPACE / RETURN »
// jouables à la borne sans clavier) : X = ESPACE, Y = RETURN (manette mappée
// gamepad) ; boutons 2/3 (« standard mapping ») ou 1/2 (encodeur arcade brut)
// sinon. Ces boutons sont EXCLUS du feu (cf. readStickRaw). Le frontend lit
// composeAux() chaque trame et envoie make/break IKBD sur les FRONTS.
enum : uint8_t { AUX_SPACE = 0x01, AUX_RETURN = 0x02 };

// Mapping clavier de l'émulation : flèches + Ctrl, GAUCHE OU DROIT. Hatari ne prend que
// le Ctrl droit ; un MacBook n'en a pas (Control à gauche seulement) et le tir y était
// inaccessible (signalé par l'auteur, 2026-11-13). Le Ctrl gauche tire donc aussi — tant
// que l'émulation est active, il est détourné du clavier ST, comme les flèches.
// Renvoie le bit ST d'une touche GLFW, ou 0. Sert AUSSI au frontend pour
// DÉTOURNER ces touches du clavier ST quand l'émulation est active (sans quoi la
// touche aurait un double effet : direction joystick ET scancode clavier).
inline uint8_t kbdBit(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_UP:            return UP;
        case GLFW_KEY_DOWN:          return DOWN;
        case GLFW_KEY_LEFT:          return LEFT;
        case GLFW_KEY_RIGHT:         return RIGHT;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL: return FIRE;
        default:                     return 0;
    }
}

// Octet ST d'une manette, AVANT filtrage anti-bloqué (cf. readStick). Sépare deux
// classes de bits dans `analog`/`digital` :
//  - analog  : directions venant d'un STICK analogique → soumises à la `deadzone`
//    (un axe au repos non centré / un léger drift sont filtrés par le seuil).
//  - digital : directions venant d'un D-PAD/HAT + le FEU des boutons → numériques.
// Cette séparation permet d'appliquer le filtre anti-bloqué AU SEUL numérique
// (cf. readStick), sans jamais brider la même direction venant du stick.
//  - Natif (GLFW complet) : API gamepad (mapping SDL : Xbox/PS/Switch… reconnus)
//    quand disponible, sinon repli axes/boutons/hat bruts.
//  - Web (port GLFW d'Emscripten) : pas de glfwGetGamepadState → lecture brute
//    (l'API Gamepad du navigateur expose le « standard mapping » : axes 0/1 =
//    stick gauche, boutons 12-15 = D-pad).
inline void readStickRaw(int jid, float thr, uint8_t& analog, uint8_t& digital) {
    analog = digital = 0;

#ifndef __EMSCRIPTEN__
    GLFWgamepadstate gs;
    if (glfwGetGamepadState(jid, &gs)) {         // manette mappée (gamepad)
        // Stick gauche → analogique (deadzone).
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -thr) analog |= UP;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] >  thr) analog |= DOWN;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -thr) analog |= LEFT;
        if (gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X] >  thr) analog |= RIGHT;
        // D-pad → numérique (anti-bloqué : un hat/dpad lu « collé » au repos, ex.
        // DualShock 4 en Bluetooth/macOS, est ainsi neutralisé en aval).
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP])    digital |= UP;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN])  digital |= DOWN;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT])  digital |= LEFT;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT]) digital |= RIGHT;
        // Joystick ST = un seul bouton de feu : n'importe quel bouton d'action
        // (A/B + gâchettes) le déclenche (D-pad et boutons système exclus ; X et Y
        // sont RÉSERVÉS aux touches ESPACE/RETURN — cf. readAux — donc exclus aussi).
        for (int i = 0; i <= GLFW_GAMEPAD_BUTTON_LAST; ++i)
            if (gs.buttons[i] &&
                i != GLFW_GAMEPAD_BUTTON_DPAD_UP   && i != GLFW_GAMEPAD_BUTTON_DPAD_DOWN &&
                i != GLFW_GAMEPAD_BUTTON_DPAD_LEFT && i != GLFW_GAMEPAD_BUTTON_DPAD_RIGHT &&
                i != GLFW_GAMEPAD_BUTTON_START     && i != GLFW_GAMEPAD_BUTTON_BACK &&
                i != GLFW_GAMEPAD_BUTTON_X         && i != GLFW_GAMEPAD_BUTTON_Y &&
                i != GLFW_GAMEPAD_BUTTON_GUIDE) { digital |= FIRE; break; }
        return;
    }
#endif

    // Manette brute : stick gauche (axes 0/1) → analogique ; hat + boutons →
    // numérique. Si la manette expose au moins 16 boutons (« standard mapping »
    // navigateur/SDL), les boutons 12-15 sont le D-pad ; sinon tout bouton = feu.
    int axN = 0, btN = 0, hatN = 0;
    const float*         ax  = glfwGetJoystickAxes(jid, &axN);
    const unsigned char* bt  = glfwGetJoystickButtons(jid, &btN);
    // ⚠ PAS de glfwGetJoystickHats sous Emscripten : son port GLFW la définit en
    // `abort('glfwGetJoystickHats is not implemented')` — un abort() DU RUNTIME,
    // pas un code d'erreur. Le navigateur n'expose une manette qu'après la
    // première pression de bouton ; jusque-là glfwJoystickPresent est faux et
    // readStick sort tôt, si bien que le web tournait très bien... jusqu'à ce
    // qu'on TOUCHE une manette, et là l'émulateur mourait. Aucun hat à perdre :
    // la Gamepad API ne rapporte pas de hat, elle donne le D-pad en boutons
    // 12-15 du « standard mapping », lus juste en dessous.
    const unsigned char* hat = nullptr;
#ifndef __EMSCRIPTEN__
    hat = glfwGetJoystickHats(jid, &hatN);
#endif
    if (ax && axN >= 2) {
        if (ax[0] < -thr) analog |= LEFT;
        if (ax[0] >  thr) analog |= RIGHT;
        if (ax[1] < -thr) analog |= UP;
        if (ax[1] >  thr) analog |= DOWN;
    }
    if (hat && hatN >= 1) {
        if (hat[0] & GLFW_HAT_UP)    digital |= UP;
        if (hat[0] & GLFW_HAT_DOWN)  digital |= DOWN;
        if (hat[0] & GLFW_HAT_LEFT)  digital |= LEFT;
        if (hat[0] & GLFW_HAT_RIGHT) digital |= RIGHT;
    }
    if (bt) {
        const bool standard = btN >= 16;
        if (standard) {
            if (bt[12]) digital |= UP;
            if (bt[13]) digital |= DOWN;
            if (bt[14]) digital |= LEFT;
            if (bt[15]) digital |= RIGHT;
        }
        // Feu = tout bouton SAUF le D-pad (standard : 12-15) et les boutons
        // réservés ESPACE/RETURN (cf. readAux : standard 2/3 = X/Y ; brut 1/2).
        const int fireMax = standard ? 12 : btN;
        for (int i = 0; i < fireMax; ++i) {
            if (standard ? (i == 2 || i == 3)
                         : (btN >= 2 && (i == 1 || (btN >= 3 && i == 2)))) continue;
            if (bt[i]) { digital |= FIRE; break; }
        }
    }
}

// Boutons auxiliaires d'une manette → bits AUX_SPACE/AUX_RETURN. Manette mappée
// gamepad : X = ESPACE, Y = RETURN (boutons d'action secondaires — A/B restent
// le feu). Repli brut : « standard mapping » (≥ 16 boutons) → boutons 2/3 (= X/Y
// du layout navigateur/SDL) ; encodeur arcade (< 16 boutons) → bouton 1 = ESPACE,
// bouton 2 = RETURN (le bouton 0 reste le feu). Ces boutons sont exclus du FEU
// dans readStickRaw — sans quoi « PRESS SPACE » tirerait en même temps.
inline uint8_t readAux(int jid) {
    if (!glfwJoystickPresent(jid)) return 0;
    uint8_t aux = 0;
#ifndef __EMSCRIPTEN__
    GLFWgamepadstate gs;
    if (glfwGetGamepadState(jid, &gs)) {
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_X]) aux |= AUX_SPACE;
        if (gs.buttons[GLFW_GAMEPAD_BUTTON_Y]) aux |= AUX_RETURN;
        return aux;
    }
#endif
    int btN = 0;
    const unsigned char* bt = glfwGetJoystickButtons(jid, &btN);
    if (!bt) return 0;
    if (btN >= 16) {                       // « standard mapping » : 2 = X, 3 = Y
        if (bt[2]) aux |= AUX_SPACE;
        if (bt[3]) aux |= AUX_RETURN;
    } else {                               // encodeur brut : 1 = ESPACE, 2 = RETURN
        if (btN >= 2 && bt[1]) aux |= AUX_SPACE;
        if (btN >= 3 && bt[2]) aux |= AUX_RETURN;
    }
    return aux;
}

// État d'une manette physique GLFW `jid` → octet ST (bits UP/DOWN/LEFT/RIGHT/FIRE).
// 0 si absente. `deadzone` = zone morte centrale [0,0.95] des STICKS analogiques.
//
// Filtre ANTI-BLOQUÉ sur les bits NUMÉRIQUES (D-pad/hat/feu) : un bit numérique
// n'est émis que s'il a été observé RELÂCHÉ (à 0) au moins une fois depuis la
// connexion de la manette. Cela neutralise une entrée numérique « collée » dès le
// départ (hat de DualShock 4 lu LEFT au repos, bouton fantôme…) tout en laissant
// passer la MÊME direction venant du stick analogique (jamais filtrée). Une vraie
// pression (qui démarre relâchée) fonctionne normalement. La calibration est par
// manette et repart à zéro à la déconnexion.
inline uint8_t readStick(int jid, float deadzone) {
    static uint8_t trustedDigital[GLFW_JOYSTICK_LAST + 1] = {0};  // bits relâchés ≥ 1 fois
    if (!glfwJoystickPresent(jid)) { trustedDigital[jid] = 0; return 0; }
    const float thr = (deadzone < 0.0f) ? 0.0f : (deadzone > 0.95f ? 0.95f : deadzone);

    uint8_t analog = 0, digital = 0;
    readStickRaw(jid, thr, analog, digital);

    trustedDigital[jid] |= uint8_t(~digital);   // tout bit numérique à 0 → « de confiance »
    return uint8_t(analog | (digital & trustedDigital[jid]));
}

// Résout l'affectation EFFECTIVE de chaque manette présente → port ST.
//  - `roles` : rôle voulu par manette (ROLE_*) — nullptr = tout AUTO (historique).
//  - `assign[jid]` en sortie : 0/1 = port ST, -1 = non affectée (absente/OFF/
//    ports déjà pris). Les manettes ÉPINGLÉES (ROLE_PORT0/1) prennent leur port
//    (plusieurs sur le même port = OR-ées) ; les AUTO remplissent ensuite, dans
//    l'ordre des jid, les ports qu'aucune épinglée n'occupe — port 1 (« jeux »)
//    d'abord, puis port 0 — comme l'affectation historique 1re→P1, 2e→P0.
// Partagé entre compose() (état IKBD) et le menu kiosk « Joysticks » (affichage
// du port effectif d'une manette AUTO).
//  - `autoPort0` : les manettes AUTO peuvent-elles remplir le port 0 (port souris) ?
//    Faux par défaut côté natif (« port 0 = souris », page Input) : une 2e manette
//    n'y va que si on L'Y BRANCHE — comme un vrai ST, où mettre un joystick dans le
//    port 0 est un geste délibéré qui retire la souris.
inline void resolveAssign(const int8_t* roles, int8_t assign[GLFW_JOYSTICK_LAST + 1],
                          bool autoPort0 = true) {
    bool pinned[2] = {false, false};
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        assign[jid] = -1;
        if (!glfwJoystickPresent(jid)) continue;
        const int8_t r = roles ? roles[jid] : int8_t(ROLE_AUTO);
        if (r == ROLE_PORT0 || r == ROLE_PORT1) { assign[jid] = r; pinned[r] = true; }
    }
    bool autoTaken[2] = {pinned[0] || !autoPort0, pinned[1]};
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        if ((roles ? roles[jid] : int8_t(ROLE_AUTO)) != ROLE_AUTO) continue;
        if      (!autoTaken[1]) { assign[jid] = 1; autoTaken[1] = true; }
        else if (!autoTaken[0]) { assign[jid] = 0; autoTaken[0] = true; }
    }
}

// Compose l'état des deux ports ST à partir de l'hôte et le pose sur l'IKBD.
//  - `kbdEnabled` : émulation clavier active (le frontend l'a déjà gatée sur le
//    focus, p.ex. pas pendant une saisie ImGui) ; vise le port `kbdPort` (0/1).
//  - `deadzone` : zone morte centrale des sticks analogiques (cf. readStick).
//  - `roles` : affectation par manette (cf. resolveAssign) — nullptr = AUTO
//    historique (1re manette → port 1, 2e → port 0, le reste ignoré).
// `out0`/`out1` reçoivent les octets composés (port 0 / port 1).
inline void compose(GLFWwindow* win, bool kbdEnabled, int kbdPort, float deadzone,
                    uint8_t& out0, uint8_t& out1, const int8_t* roles = nullptr,
                    bool autoPort0 = true) {
    uint8_t p[2] = {0, 0};

    if (kbdEnabled) {
        uint8_t k = 0;
        if (glfwGetKey(win, GLFW_KEY_UP)            == GLFW_PRESS) k |= UP;
        if (glfwGetKey(win, GLFW_KEY_DOWN)          == GLFW_PRESS) k |= DOWN;
        if (glfwGetKey(win, GLFW_KEY_LEFT)          == GLFW_PRESS) k |= LEFT;
        if (glfwGetKey(win, GLFW_KEY_RIGHT)         == GLFW_PRESS) k |= RIGHT;
        if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS
            || glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) k |= FIRE;
        p[kbdPort & 1] |= k;
    }

    int8_t assign[GLFW_JOYSTICK_LAST + 1];
    resolveAssign(roles, assign, autoPort0);
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
        if (assign[jid] >= 0) p[assign[jid]] |= readStick(jid, deadzone);

    out0 = p[0];
    out1 = p[1];
}

// Compose les bits AUXILIAIRES (AUX_SPACE/AUX_RETURN) de toutes les manettes
// AFFECTÉES à un port (les OFF/non affectées n'injectent pas de touches). Le
// frontend compare au tick précédent et envoie make/break IKBD sur les fronts.
inline uint8_t composeAux(const int8_t* roles = nullptr, bool autoPort0 = true) {
    int8_t assign[GLFW_JOYSTICK_LAST + 1];
    resolveAssign(roles, assign, autoPort0);
    uint8_t aux = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
        if (assign[jid] >= 0) aux |= readAux(jid);
    return aux;
}

// Diagnostic (NEOST_DEBUG_JOY=1) : imprime sur stderr l'état BRUT de chaque manette
// présente — nom, reconnue « gamepad » ou non, tous les axes (bruts + gamepad si
// mappée), boutons pressés, et l'octet ST composé. Sert à identifier un axe non
// centré au repos / un mauvais index d'axe (cause d'un « toujours à gauche/droite »).
inline void debug(GLFWwindow* win, bool kbdEnabled, int kbdPort, float deadzone) {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        const char* nm = glfwGetJoystickName(jid);
        std::fprintf(stderr, "[joy] jid=%d \"%s\"", jid, nm ? nm : "?");

#ifndef __EMSCRIPTEN__
        GLFWgamepadstate gs;
        const bool mapped = glfwGetGamepadState(jid, &gs);
        std::fprintf(stderr, " gamepad=%s", mapped ? "YES" : "no");
        if (mapped) {
            std::fprintf(stderr, " | LX=%+.2f LY=%+.2f RX=%+.2f RY=%+.2f",
                         gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y],
                         gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        }
#endif
        int axN = 0, btN = 0;
        const float*         ax = glfwGetJoystickAxes(jid, &axN);
        const unsigned char* bt = glfwGetJoystickButtons(jid, &btN);
        std::fprintf(stderr, " | axes(%d):", axN);
        for (int i = 0; i < axN; ++i) std::fprintf(stderr, " a%d=%+.2f", i, ax ? ax[i] : 0.0f);
        std::fprintf(stderr, " | btns(%d):", btN);
        for (int i = 0; i < btN; ++i) if (bt && bt[i]) std::fprintf(stderr, " %d", i);
        std::fprintf(stderr, " -> ST=$%02X\n", readStick(jid, deadzone));
    }
    uint8_t j0 = 0, j1 = 0;
    compose(win, kbdEnabled, kbdPort, deadzone, j0, j1);
    std::fprintf(stderr, "[joy] composed: port0=$%02X port1=$%02X (dz=%.2f)\n", j0, j1, deadzone);
}

} // namespace stjoy
