// =============================================================================
//  Fpu.hpp — Interface mémoire du coprocesseur MC68881 du Mega STE (OPTIONNEL).
//
//  Sur Mega STE, le 68000 ne possède pas le protocole coprocesseur des 68020+ :
//  le 68881 (socket interne, ou carte SFP004 sur Mega ST) est câblé en
//  PÉRIPHÉRIQUE, ses registres d'interface coprocesseur (CIR) étant mappés en
//  $FFFA40-$FFFA5F. Le logiciel dialogue alors « à la main » : écrire le mot de
//  commande dans le Command CIR, scruter le Response CIR, transférer les
//  opérandes via l'Operand CIR (cf. MC68881/MC68882 User's Manual, §7 ; MAME
//  mc68881 ; Hatari configuration.h n_FPUType — Hatari N'ÉMULE PAS ce socket :
//  $FFFA40 reste une bus error, et TOS comme les diagnostics concluent
//  « FPU not found », comportement NeoST par défaut).
//
//  Niveau d'émulation NeoST = « sonde + trapping » : quand le FPU est activé
//  (--fpu / option GUI), la zone $FFFA40-$FFFA5F répond (plus de bus error) →
//  la sonde du TOS (cookie _FPU) et du diagnostic détecte un 68881. Le dialogue
//  CIR est JOURNALISÉ sur stderr et reçoit des réponses neutres (« null
//  primitive, processing finished ») : tout programme qui tente du calcul
//  flottant matériel est ainsi visible et n'attend pas indéfiniment, mais
//  l'ARITHMÉTIQUE n'est pas émulée (cf. TODO.md). Par défaut : ABSENT.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>

class Fpu {
public:
    // Présence du coprocesseur (socket peuplé). false = fidèle Hatari : la zone
    // $FFFA40-$FFFA5F déclenche une bus error et la sonde conclut « not found ».
    bool present = false;

    static constexpr uint32_t BASE = 0xFFFA40;   // premier CIR (Response)
    static constexpr uint32_t END  = 0xFFFA60;   // exclu : $FFFA40-$FFFA5F

    // Registres CIR (offsets pairs, accès octet/mot big-endian) :
    //   $00 Response (R)  $02 Control (W)   $04 Save (R)      $06 Restore (R/W)
    //   $08 Operation (W) $0A Command (W)   $0E Condition (W)
    //   $10-$13 Operand   $14 Register Select (R)  $18 Instr Addr  $1C Operand Addr
    uint8_t read8(uint32_t addr) {
        const uint32_t off = (addr - BASE) & 0x1F;
        uint16_t v;
        switch (off & ~1u) {
            // Response : « null primitive, PF=1 (processing finished), CA=0,
            // pas d'exception, TF=0 (prédicat faux) » — le scrutateur sort
            // aussitôt de sa boucle, sans transfert d'opérande demandé.
            case 0x00: v = 0x0802; break;
            // Save : mot de format d'une trame IDLE de 68881 (version $1F,
            // longueur $18) — état « au repos, rien en cours ».
            case 0x04: v = 0x1F18; break;
            default:   trace("lecture", off, latch_[off]);
                       return latch_[off];          // Restore/Operand/latches
        }
        trace("lecture", off, uint8_t(off & 1 ? v : v >> 8));
        return uint8_t(off & 1 ? v : v >> 8);       // big-endian : octet pair = poids fort
    }

    void write8(uint32_t addr, uint8_t v) {
        const uint32_t off = (addr - BASE) & 0x1F;
        latch_[off] = v;
        trace("écriture", off, v);
    }

    void reset() {
        for (auto& b : latch_) b = 0;
        traceCount_ = 0;
    }

private:
    // Journalise le dialogue CIR (les 64 premiers accès — anti-spam) : c'est le
    // « trapping » qui rend visible tout usage réel du FPU non encore émulé.
    void trace(const char* op, uint32_t off, uint8_t v) {
        if (traceCount_ >= 64) return;
        static const char* names[16] = {
            "Response", "Control", "Save", "Restore", "Operation", "Command",
            "(réservé)", "Condition", "Operand", "Operand+2", "RegSelect", "(réservé)",
            "InstrAddr", "InstrAddr+2", "OperandAddr", "OperandAddr+2"};
        std::fprintf(stderr, "[fpu] %s CIR $%02X %s = $%02X%s\n", op, off,
                     names[(off >> 1) & 15], v,
                     ++traceCount_ == 64 ? " (suite du dialogue non journalisée)" : "");
    }

    uint8_t latch_[0x20] = {};   // octets écrits, relus tels quels (Restore...)
    int traceCount_ = 0;
};
