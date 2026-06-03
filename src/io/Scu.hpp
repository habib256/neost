// =============================================================================
//  Scu.hpp — SCU (System Control Unit) du MegaSTE : gate d'interruptions.
//
//  Sur MegaSTE (et TT), TOUTES les IRQ matérielles transitent par le SCU avant
//  d'atteindre l'IPL du 68000 : deux masques (SysIntMask, VmeIntMask) décident
//  quels niveaux sont présentés. Port de Hatari `scu_vme.c`.
//    IPL = (SysIntState & SysIntMask & 0x9F) | (VmeIntState & VmeIntMask & 0x60)
//  MFP (niveau 6) et SCC (niveau 5) sont câblés au bus VME → gatés par VmeIntMask ;
//  les autres niveaux (VSYNC=4, HSYNC=2, soft IRQ1=1) par SysIntMask.
//
//  Le gating est TOUJOURS actif sur MegaSTE (comme `SCU_IsEnabled` chez Hatari) : tout
//  OS MegaSTE réel programme le SCU tôt au boot — vérifié sur TOS 2.06, EmuTOS 256K
//  (`Machine: Atari Mega STe`) et le diagnostic MegaSTE, qui écrivent tous SysIntMask=0x14
//  et VmeIntMask=0x40/0x60. (L'EmuTOS 192 Ko est un build « Atari ST »/TOS 1.4 qu'Hatari
//  refuse lui-même sur MegaSTE : pour MegaSTE, utiliser etos256us/fr ou TOS 2.06.)
//
//  L'état (SysIntState/VmeIntState) est piloté par les sources VIVANTES de NeoST
//  (MFP/VBL/HBL via syncState, appelé par Cpu68k::neostUpdateIpl) plutôt que par
//  un latch séparé : NeoST n'a pas de file d'IRQ, ses sources sont déjà « niveau ».
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class Scu {
public:
    // Registres SCU aux adresses IMPAIRES $FF8E01-$FF8E0F (le reste = void).
    uint8_t read8(uint32_t addr) const {
        switch (addr & 0x0F) {
            case 0x01: return sysIntMask;
            case 0x03: return sysIntState;     // statut (lecture seule)
            case 0x05: return sysInterrupter;
            case 0x07: return vmeInterrupter;
            case 0x09: return gpr1;            // général ; reset = 0x01 (cf. ci-dessous)
            case 0x0B: return gpr2;
            case 0x0D: return vmeIntMask;
            case 0x0F: return vmeIntState;     // statut (lecture seule)
            default:   return 0xFF;            // octets void entre registres
        }
    }

    // Renvoie true si l'écriture a (dé)masqué des IRQ → l'appelant doit recalculer
    // l'IPL CPU. Écrire un masque remet l'état pending à 0 (cf. Hatari).
    bool write8(uint32_t addr, uint8_t v) {
        switch (addr & 0x0F) {
            case 0x01: sysIntMask = v; sysIntState = 0; return true;  // reset pending (cf. Hatari)
            case 0x05: sysInterrupter = v; return true;       // bit0 → soft IRQ1 (niveau 1)
            case 0x07: vmeInterrupter = v; return false;      // bit0 → IRQ3 VME (ignoré, pas de bus VME)
            case 0x09: gpr1 = v; return false;
            case 0x0B: gpr2 = v; return false;
            case 0x0D: vmeIntMask = v; vmeIntState = 0; return true;  // reset pending (cf. Hatari)
            // $FF8E03 / $FF8E0F (états) sont en LECTURE SEULE ; void ignoré.
            default:   return false;
        }
    }

    // Synchronise l'état SCU depuis les sources vivantes de NeoST (appelé avant le
    // gating). MFP→niveau 6 (VmeIntState), VBL→4, HBL→2, soft IRQ1→1 (SysIntState).
    void syncState(bool mfp6, bool vbl, bool hbl) {
        vmeIntState = uint8_t((vmeIntState & ~0x40) | (mfp6 ? 0x40 : 0));            // niveau 6
        uint8_t s = sysIntState & ~0x16;                                            // niveaux 4,2,1
        if (vbl)                  s |= 0x10;                                         // niveau 4
        if (hbl)                  s |= 0x04;                                         // niveau 2
        if (sysInterrupter & 0x01) s |= 0x02;                                       // niveau 1 (soft)
        sysIntState = s;
    }

    // Niveau d'IRQ le plus prioritaire autorisé à atteindre le CPU (state & mask),
    // 0 si aucun. MFP(6)/SCC(5) via VmeIntMask, le reste via SysIntMask.
    int gatedLevel() const {
        const uint8_t sys = sysIntState & sysIntMask;
        const uint8_t vme = vmeIntState & vmeIntMask;
        for (int l = 7; l >= 1; --l) {
            const uint8_t bit = uint8_t(1u << l);
            const bool on = (l == 6 || l == 5) ? (vme & bit) : (sys & bit);
            if (on) return l;
        }
        return 0;
    }

    // Registres exposés (débogueur). GPR1 = 0x01 au reset : contournement Hatari
    // (« TOS v2/v3 crash sinon » sur MegaSTE/TT).
    uint8_t sysIntMask = 0, sysIntState = 0, sysInterrupter = 0;
    uint8_t vmeIntMask = 0, vmeIntState = 0, vmeInterrupter = 0;
    uint8_t gpr1 = 0x01, gpr2 = 0;
};
