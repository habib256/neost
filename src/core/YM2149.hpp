// =============================================================================
//  YM2149.hpp — PSG (Programmable Sound Generator) de l'Atari ST.
//
//  Le YM2149 (clone du AY-3-8910) : 3 voies carrées + bruit + enveloppe, piloté
//  par 16 registres. L'accès se fait en 2 temps via $FF8800 (sélection registre)
//  puis $FF8802 (donnée). Squelette : on stocke les registres ; la synthèse
//  synthèse audio (ex. miniaudio) viendra se brancher sur regs_ plus tard.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <array>

class YM2149 {
public:
    uint8_t read8(uint32_t addr) {
        // $FF8800 en lecture renvoie le registre courant (ex. ports I/O / IKBD).
        if ((addr & 3) == 0) return regs_[selected_];
        return 0xFF;
    }

    void write8(uint32_t addr, uint8_t v) {
        switch (addr & 3) {
            case 0: selected_ = v & 0x0F;  break;   // $FF8800 : choix du registre
            case 2: regs_[selected_] = v;  break;   // $FF8802 : écriture donnée
            default: break;
        }
    }

    // Exposé au débogueur : les 16 registres bruts du PSG.
    std::array<uint8_t, 16> regs_{};
    uint8_t selected_ = 0;
};
