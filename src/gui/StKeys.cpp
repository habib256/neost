// =============================================================================
//  StKeys.cpp — implémentation. Voir StKeys.hpp pour la raison d'être (A21).
//  Tables déplacées TELLES QUELLES depuis src/main.cpp (2026-08-27) — le
//  contenu vient du port de Hatari sdl/keymap.c, ne pas « corriger » une
//  entrée sans confronter à la source Hatari.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/StKeys.hpp"

#include <GLFW/glfw3.h>

#include <cstdio>

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

// Pays du TOS chargé (codes Hatari tos.h : 0=US, 1=DE, 2=FR, 3=UK… 127=EmuTOS
// multilangue → table par défaut). -1 tant qu'aucune ROM n'est chargée.
static int g_kbdCountry = -1;

// Premier point de code Unicode d'une chaîne UTF-8 (les noms de touches GLFW
// sont en UTF-8 : « é », « ù », « § »… sur les claviers nationaux).
uint32_t utf8First(const char* s) {
    const auto* u = reinterpret_cast<const unsigned char*>(s);
    if (u[0] < 0x80) return u[0];
    if ((u[0] & 0xE0) == 0xC0 && u[1]) return uint32_t(u[0] & 0x1F) << 6 | (u[1] & 0x3F);
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2])
        return uint32_t(u[0] & 0x0F) << 12 | uint32_t(u[1] & 0x3F) << 6 | (u[2] & 0x3F);
    return 0;
}

// Table SYMBOLIQUE par défaut (port de Keymap_SymbolicToStScanCode_default,
// partie imprimable — le reste passe par le mapping positionnel). 0xFF = pas de
// correspondance symbolique → repli positionnel.
static uint8_t symbolicDefault(uint32_t cp) {
    switch (cp) {
        case '!':  return 0x09;   // hôte azerty
        case '"':  return 0x04;
        case '#':  return 0x2B;   // hôte DE/UK, pour TOS FR/UK/DK/NL
        case '$':  return 0x1B;
        case '&':  return 0x02;
        case '\'': return 0x28;
        case '(':  return 0x63;   // ( pavé num. ST
        case ')':  return 0x64;
        case '*':  return 0x66;
        case '+':  return 0x4E;
        case ',':  return 0x33;
        case '-':  return 0x35;   // défaut DE/IT/SE/CH/FI/NO/DK/CZ
        case '.':  return 0x34;
        case '/':  return 0x35;
        case '0':  return 0x0B;
        case '1':  return 0x02; case '2': return 0x03; case '3': return 0x04;
        case '4':  return 0x05; case '5': return 0x06; case '6': return 0x07;
        case '7':  return 0x08; case '8': return 0x09; case '9': return 0x0A;
        case ':':  return 0x34;
        case ';':  return 0x27;
        case '<':  return 0x60;
        case '=':  return 0x0D;
        case '>':  return 0x34;
        case '?':  return 0x35;
        case '@':  return 0x28;
        case '[':  return 0x1A;
        case '\\': return 0x2B;
        case ']':  return 0x1B;
        case '^':  return 0x2B;
        case '_':  return 0x0C;
        case '`':  return 0x29;
        case 'a':  return 0x1E; case 'b': return 0x30; case 'c': return 0x2E;
        case 'd':  return 0x20; case 'e': return 0x12; case 'f': return 0x21;
        case 'g':  return 0x22; case 'h': return 0x23; case 'i': return 0x17;
        case 'j':  return 0x24; case 'k': return 0x25; case 'l': return 0x26;
        case 'm':  return 0x32; case 'n': return 0x31; case 'o': return 0x18;
        case 'p':  return 0x19; case 'q': return 0x10; case 'r': return 0x13;
        case 's':  return 0x1F; case 't': return 0x14; case 'u': return 0x16;
        case 'v':  return 0x2F; case 'w': return 0x11; case 'x': return 0x2D;
        case 'y':  return 0x15; case 'z': return 0x2C;
        // Lettres nationales (latin-1+, mêmes valeurs que Hatari) :
        case 167:  return 0x29;   // § suisse
        case 168:  return 0x1B;   // ¨ suisse
        case 176:  return 0x35;   // ° espagnol
        case 178:  return 0x29;   // ² français
        case 180:  return 0x0D;   // ´ allemand
        case 223:  return 0x0C;   // ß allemand
        case 224:  return 0x0B;   // à français
        case 225:  return 0x09;   // á tchèque
        case 228:  return 0x28;   // ä allemand
        case 229:  return 0x1A;   // å suédois
        case 231:  return 0x0A;   // ç français
        case 232:  return 0x08;   // è français
        case 233:  return 0x03;   // é français
        case 236:  return 0x0D;   // ì italien
        case 237:  return 0x0A;   // í tchèque
        case 241:  return 0x27;   // ñ espagnol
        case 242:  return 0x27;   // ò italien
        case 243:  return 0x02;   // ó tchèque
        case 246:  return 0x27;   // ö allemand
        case 249:  return 0x28;   // ù français
        case 250:  return 0x1A;   // ú tchèque
        case 252:  return 0x1A;   // ü allemand
        case 253:  return 0x08;   // ý tchèque
        default:   return 0xFF;
    }
}

// Surcharges par pays du TOS (ports de Keymap_SymbolicToStScanCode_US/DE/FR/UK).
// Les autres pays Hatari (ES/IT/SE/CH/NO/DK/NL/CZ) retombent sur la table par
// défaut — leurs lettres nationales y sont déjà.
uint8_t symbolicForCountry(uint32_t cp) {
    switch (g_kbdCountry) {
        case 0:   // TOS US
            if (cp == '-') return 0x0C;
            break;
        case 1:   // TOS allemand (QWERTZ : y/z croisés, # + / déplacés)
            switch (cp) {
                case '#': return 0x29; case '+': return 0x1B; case '/': return 0x65;
                case 'y': return 0x2C; case 'z': return 0x15;
            }
            break;
        case 2:   // TOS français (AZERTY : a/q, z/w, m, ponctuation déplacée)
            switch (cp) {
                case '\'': return 0x05; case '(': return 0x06; case ')': return 0x0C;
                case ',':  return 0x32; case '-': return 0x0D; case ';': return 0x33;
                case '=':  return 0x35; case '^': return 0x1A;
                case 'a':  return 0x10; case 'm': return 0x27; case 'q': return 0x1E;
                case 'w':  return 0x2C; case 'z': return 0x11;
                case 167:  return 0x07;   // §
            }
            break;
        case 3:   // TOS UK
            if (cp == '-')  return 0x0C;
            if (cp == '\\') return 0x60;
            break;
    }
    return symbolicDefault(cp);
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

// Lit le pays du TOS chargé dans son en-tête ROM (mot os_conf à $1C, pays =
// os_conf >> 1 — cf. Hatari tos.c) et arme les surcharges symboliques.
void setCountryFromTos(const std::vector<uint8_t>& rom) {
    if (rom.size() < 0x1E) { g_kbdCountry = -1; return; }
    g_kbdCountry = ((rom[0x1C] << 8) | rom[0x1D]) >> 1;
    static const char* names[] = {"US", "DE", "FR", "UK"};
    std::fprintf(stderr, "[kbd] TOS layout: %s (symbolic mapping)\n",
                 g_kbdCountry >= 0 && g_kbdCountry <= 3 ? names[g_kbdCountry]
                 : g_kbdCountry == 127 ? "multilingual (default)" : "other (default)");
}

} // namespace neost::stkeys
