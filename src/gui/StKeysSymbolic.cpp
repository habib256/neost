// =============================================================================
//  StKeysSymbolic.cpp — étage SYMBOLIQUE du clavier (caractère hôte → scancode
//  ST, selon le pays du TOS). Séparé de StKeys.cpp le 2026-11-12 pour ne PAS
//  dépendre de GLFW : neost-selftest (qui ne lie pas GLFW) verrouille ainsi les
//  tables par pays. Voir StKeys.hpp pour la raison d'être du module (A21).
//  Contenu : port de Hatari sdl/keymap.c — ne pas « corriger » une entrée sans
//  confronter à la source Hatari.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/StKeys.hpp"

#include <cstdio>

namespace neost::stkeys {

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
        // Lettres nationales (latin-1 et latin étendu A, mêmes valeurs que
        // Hatari [D] src/sdl/keymap.c:167-198 @ f0736b24) :
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
        case 269:  return 0x05;   // č tchèque
        case 271:  return 0x1B;   // ď tchèque
        case 283:  return 0x03;   // ě tchèque
        case 328:  return 0x2B;   // ň tchèque
        case 345:  return 0x06;   // ř tchèque
        case 353:  return 0x04;   // š tchèque
        case 357:  return 0x28;   // ť tchèque
        case 367:  return 0x27;   // ů tchèque
        case 382:  return 0x07;   // ž tchèque
        default:   return 0xFF;
    }
}

// Surcharges par pays du TOS — port des 12 fonctions
// Keymap_SymbolicToStScanCode_<pays> et de leur aiguillage :
// [D] Hatari src/sdl/keymap.c:265-420 (tables) et 963-1001 (Keymap_SetCountry)
//     @ f0736b24 ; codes pays : src/includes/tos.h:19-41 @ f0736b24.
// FI partage la table SE et CH_FR/CH_DE la table CH, comme chez Hatari. Les
// pays sans table chez Hatari (TR, SA, HU, PL, RU…) et EmuTOS multilingue (127)
// restent sur la table par défaut.
// ⚠ Jusqu'au 2026-11-12 seuls US/DE/FR/UK étaient portés, et un commentaire
// affirmait à tort que les autres pays retombaient correctement sur la table
// par défaut : un hôte tchèque tapait y/z croisés, un hôte italien « à » en
// $0B (le 0 du ST), etc.
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
        case 4:   // TOS espagnol
            switch (cp) {
                case '-': return 0x0C; case ';': return 0x28; case '`': return 0x1B;
                case 231: return 0x29;    // ç espagnol
            }
            break;
        case 5:   // TOS italien
            switch (cp) {
                case '\'': return 0x0C; case '+': return 0x1B;
                case 224:  return 0x28;   // à
                case 232:  return 0x1A;   // è
                case 249:  return 0x29;   // ù
            }
            break;
        case 6:   // TOS suédois
        case 10:  // TOS finnois — même table que le suédois (Hatari)
            switch (cp) {
                case '\'': return 0x29; case '+': return 0x0C;
                case 252:  return 0x1B;   // ü
            }
            break;
        case 7:   // TOS suisse français
        case 8:   // TOS suisse allemand — une seule table pour les deux (Hatari)
            switch (cp) {
                case '^': return 0x0D;
                case 224: return 0x28;    // à
                case 232: return 0x1A;    // è
                case 233: return 0x27;    // é
            }
            break;
        case 11:  // TOS norvégien
            switch (cp) {
                case '\'': return 0x29; case '+': return 0x0C;
                case 230:  return 0x28;   // æ
                case 233:  return 0x0D;   // é
                case 248:  return 0x27;   // ø
                case 252:  return 0x1B;   // ü
            }
            break;
        case 12:  // TOS danois
            switch (cp) {
                case '\'': return 0x0D; case '+': return 0x0C; case '*': return 0x1B;
                case 230:  return 0x27;   // æ
                case 233:  return 0x29;   // é
                case 248:  return 0x28;   // ø
            }
            break;
        case 14:  // TOS néerlandais
            if (cp == '-')  return 0x0C;
            if (cp == '\\') return 0x60;
            break;
        case 15:  // TOS tchèque (QWERTZ : y/z croisés)
            switch (cp) {
                case '#': return 0x29; case '\'': return 0x0D; case '=': return 0x0C;
                case 'y': return 0x2C; case 'z': return 0x15;
                case 233: return 0x0B;    // é
            }
            break;
    }
    return symbolicDefault(cp);
}

// Lit le pays du TOS chargé dans son en-tête ROM (mot os_conf à $1C, pays =
// os_conf >> 1 — cf. Hatari tos.c) et arme les surcharges symboliques.
void setCountryFromTos(const std::vector<uint8_t>& rom) {
    if (rom.size() < 0x1E) { g_kbdCountry = -1; return; }
    g_kbdCountry = ((rom[0x1C] << 8) | rom[0x1D]) >> 1;
    // Codes tos.h 0..15 ; nullptr = pas de table propre chez Hatari (défaut).
    static const char* names[] = {"US", "DE", "FR", "UK", "ES", "IT", "SE", "CH-FR",
                                  "CH-DE", nullptr, "FI", "NO", "DK", nullptr, "NL", "CZ"};
    const char* n = (g_kbdCountry >= 0 && g_kbdCountry <= 15) ? names[g_kbdCountry] : nullptr;
    std::fprintf(stderr, "[kbd] TOS layout: %s (symbolic mapping)\n",
                 n ? n : g_kbdCountry == 127 ? "multilingual (default)" : "other (default)");
}

} // namespace neost::stkeys