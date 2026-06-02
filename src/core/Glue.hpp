// =============================================================================
//  Glue.hpp — GLUE + MMU : la "colle" logique du système Atari ST.
//
//  Sur ST, deux puces se partagent la gestion bas niveau :
//   - le MMU décode les banques RAM et arbitre l'accès RAM CPU/Shifter ;
//   - le GLUE génère les signaux de sélection (chip select) et route les IRQ
//     (HBL, VBL) ainsi que les accès MFP/ACIA.
//  Ici, squelette unifié : il sert de "fourre-tout" MMIO par défaut tant que
//  MFP/ACIA ne sont pas modélisés, et porte la config mémoire.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>

class Glue {
public:
    uint8_t read8(uint32_t addr) {
        if (addr == 0xFF8001) return memConfig_;   // config banques RAM
        // MFP ($FFFA00), ACIA ($FFFC00)... : stubs renvoyant des lignes au repos.
        return 0xFF;
    }

    void write8(uint32_t addr, uint8_t v) {
        if (addr == 0xFF8001) { std::fprintf(stderr, "[CFG] $FF8001 <- %02X\n", v); memConfig_ = v; return; }
        // Écritures MFP/ACIA ignorées pour l'instant (à brancher plus tard).
    }

    // $FF8001 : 2 bits par banque (00=128Ko, 01=512Ko, 10=2Mo). Défaut : 512 Ko.
    uint8_t memConfig_ = 0b0100;
};
