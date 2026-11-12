// =============================================================================
//  StKeys.cpp — implémentation. Voir StKeys.hpp pour la raison d'être (A21).
//  Tables déplacées TELLES QUELLES depuis src/main.cpp (2026-08-27) — le
//  contenu vient du port de Hatari sdl/keymap.c, ne pas « corriger » une
//  entrée sans confronter à la source Hatari.
//  Ici : l'étage POSITIONNEL et l'aiguillage scancodeFor (dépendent de GLFW).
//  L'étage SYMBOLIQUE (tables par pays) vit dans StKeysSymbolic.cpp, sans GLFW,
//  pour que neost-selftest le verrouille.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/StKeys.hpp"

#include <GLFW/glfw3.h>

namespace neost::stkeys {

// Traduit une touche GLFW en scancode "make" du clavier Atari ST (0 = ignorée).
// Les scancodes ST suivent la matrice de l'IKBD, pas l'ASCII (cf. doc Atari).
uint8_t positionalScancode(int key) {
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
        // Touches spécifiques ST sans équivalent direct (mapping Hatari sdl/keymap.c) :
        // Help/Undo + parenthèses du pavé numérique ST.
        case GLFW_KEY_PRINT_SCREEN: return 0x62;            // Help
        case GLFW_KEY_END: return 0x61;                     // Undo
        case GLFW_KEY_PAGE_UP: return 0x63;                 // ( pavé num. ST
        case GLFW_KEY_PAGE_DOWN: return 0x64;               // ) pavé num. ST
        // Pavé numérique (scancodes ST 0x65-0x72 + 0x4A/0x4E, cf. Hatari).
        case GLFW_KEY_KP_0: return 0x70; case GLFW_KEY_KP_1: return 0x6D;
        case GLFW_KEY_KP_2: return 0x6E; case GLFW_KEY_KP_3: return 0x6F;
        case GLFW_KEY_KP_4: return 0x6A; case GLFW_KEY_KP_5: return 0x6B;
        case GLFW_KEY_KP_6: return 0x6C; case GLFW_KEY_KP_7: return 0x67;
        case GLFW_KEY_KP_8: return 0x68; case GLFW_KEY_KP_9: return 0x69;
        case GLFW_KEY_KP_DECIMAL: return 0x71;
        case GLFW_KEY_KP_DIVIDE: return 0x65;
        case GLFW_KEY_KP_MULTIPLY: return 0x66;
        case GLFW_KEY_KP_SUBTRACT: return 0x4A;
        case GLFW_KEY_KP_ADD: return 0x4E;
        case GLFW_KEY_KP_ENTER: return 0x72;
        case GLFW_KEY_KP_EQUAL: return 0x61;                // Undo (comme Hatari)
        default: return 0x00;
    }
}

// Scancode ST d'une touche GLFW : d'abord le SYMBOLIQUE (touches imprimables,
// caractère donné par la disposition hôte via glfwGetKeyName), sinon repli
// POSITIONNEL (fonctions, flèches, pavé, modificateurs — et touches sans nom,
// dont les hôtes où glfwGetKeyName ne répond pas).
// ⚠ Emscripten : glfwGetKeyName n'y renvoie PAS nullptr, c'est un stub
// abort("glfwGetKeyName not implemented.") — la 1re lettre tapée tuait le runtime
// (écran figé sur le démarrage EmuTOS). Le port GLFW web dérive la touche de
// KeyboardEvent.keyCode, qui pour les LETTRES suit déjà la disposition hôte
// (A d'un AZERTY = 65) : on en tire le caractère ; le reste passe en positionnel.
uint8_t scancodeFor(int key, int scancode) {
#ifdef __EMSCRIPTEN__
    (void)scancode;
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        const uint8_t sc = symbolicForCountry(uint32_t(key - GLFW_KEY_A + 'a'));
        if (sc != 0xFF) return sc;
    }
#else
    if (key >= GLFW_KEY_SPACE && key < GLFW_KEY_ESCAPE) {        // plage « imprimable »
        const char* name = glfwGetKeyName(key, scancode);
        if (name && name[0]) {
            const uint8_t sc = symbolicForCountry(utf8First(name));
            if (sc != 0xFF) return sc;
        }
    }
#endif
    return positionalScancode(key);
}

} // namespace neost::stkeys
