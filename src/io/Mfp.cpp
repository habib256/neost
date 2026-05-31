// =============================================================================
//  Mfp.cpp — Logique d'interruption du MC68901.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Mfp.hpp"
#include <cstdio>

// Les registres MFP sont sur les adresses IMPAIRES à partir de $FFFA00.
// On indexe par l'offset bas (addr & 0x3F).
uint8_t Mfp::read8(uint32_t addr) {
    switch (addr & 0x3F) {
        case 0x01: {                // GPIP : bit4 = ligne ACIA (active BAS)
            uint8_t v = gpip;
            if (aciaLine_) v &= ~0x10; else v |= 0x10;
            return v;
        }
        case 0x03: return aer;
        case 0x05: return ddr;
        case 0x07: return iera;
        case 0x09: return ierb;
        case 0x0B: return ipra;
        case 0x0D: return iprb;
        case 0x0F: return isra;
        case 0x11: return isrb;
        case 0x13: return imra;
        case 0x15: return imrb;
        case 0x17: return vr;
        default:   return 0xFF;      // timers/USART non modélisés
    }
}

void Mfp::write8(uint32_t addr, uint8_t v) {
    switch (addr & 0x3F) {
        case 0x01: gpip = v; break;
        case 0x03: aer  = v; break;
        case 0x05: ddr  = v; break;
        // Désactiver un canal (IER=0) efface aussi son interruption pendante.
        case 0x07: iera = v; ipra &= iera; break;
        case 0x09: ierb = v; iprb &= ierb; break;
        // IPR/ISR : on n'EFFACE que les bits écrits à 0 (les 1 laissent inchangé).
        case 0x0B: ipra &= v; break;
        case 0x0D: iprb &= v; break;
        case 0x0F: isra &= v; break;
        case 0x11: isrb &= v; break;
        case 0x13: imra = v; break;
        case 0x15: imrb = v; break;
        case 0x17: vr   = v; break;
        default: break;             // timers : EmuTOS les programme, on les ignore
    }
}

void Mfp::raise(int source) {
    if (source >= 8) {
        const uint8_t bit = uint8_t(1u << (source - 8));
        if (iera & bit) ipra |= bit;     // l'IRQ ne devient pendante que si activée
    } else {
        const uint8_t bit = uint8_t(1u << source);
        if (ierb & bit) iprb |= bit;
    }
}

int Mfp::highestPending() const {
    const uint8_t pa = ipra & imra;      // pendant ET non masqué
    const uint8_t pb = iprb & imrb;
    for (int b = 7; b >= 0; --b) if (pa & (1u << b)) return 8 + b;   // sources 15..8
    for (int b = 7; b >= 0; --b) if (pb & (1u << b)) return b;       // sources 7..0
    return -1;
}

int Mfp::highestInService() const {
    for (int b = 7; b >= 0; --b) if (isra & (1u << b)) return 8 + b;
    for (int b = 7; b >= 0; --b) if (isrb & (1u << b)) return b;
    return -1;
}

bool Mfp::irqPending() const {
    const int s = highestPending();
    if (s < 0) return false;
    // En mode "software EOI" (VR bit3), une source en service bloque les sources
    // de priorité ≤ tant que son bit ISR n'est pas effacé par le handler.
    if (vr & 0x08) return s > highestInService();
    return true;
}

int Mfp::iack() {
    const int s = highestPending();
    if (s < 0) return -1;
    const uint8_t bit = uint8_t(1u << (s & 7));
    if (s >= 8) { ipra &= ~bit; if (vr & 0x08) isra |= bit; }
    else        { iprb &= ~bit; if (vr & 0x08) isrb |= bit; }
    return (vr & 0xF0) | s;               // vecteur MFP
}
