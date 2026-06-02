// =============================================================================
//  Blitter.hpp — Blitter ("BLiTTER") de l'Atari ST (Mega ST / STE / Mega STE).
//
//  Copieur de blocs mémoire câblé ($FF8A00-$FF8A3F) : pour chaque mot, combine une
//  source (décalée + masque "skew"), un opérateur halftone (HOP) et un opérateur
//  logique (LOP) avec la destination, sous masques de bord (endmask). NeoST en
//  fait un port FONCTIONNEL de Hatari (blitter.c) : la logique de données (HOP,
//  LOP, FXSR/NFSR, smudge, halftone, comptes X/Y, incréments) est fidèle, mais le
//  transfert s'exécute en MODE HOG (instantané) — on ne modélise pas le partage de
//  bus cycle-exact (64 accès). Suffit pour le résultat (diagnostics, VDI EmuTOS).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class Bus;

class Blitter {
public:
    explicit Blitter(Bus& bus) : bus_(bus) {}

    uint8_t read8(uint32_t addr);            // $FF8A00-$FF8A3F (relisible)
    void    write8(uint32_t addr, uint8_t v);

    void reset();

    // Le blit est-il en cours ? (bit BUSY de $FF8A3C). En mode HOG instantané il
    // retombe à 0 dès la fin du transfert lancé par l'écriture du registre contrôle.
    bool busy() const { return (reg_[0x3C] & 0x80) != 0; }

private:
    void run();                              // exécute tout le bloc (mode HOG)
    uint16_t readWord(uint32_t addr);
    void     writeWord(uint32_t addr, uint16_t v);

    Bus&    bus_;
    uint8_t reg_[0x40] = {};                 // backing store big-endian ($FF8A00 base)
};
