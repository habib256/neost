// =============================================================================
//  Tracer.hpp — Journalisation fine de l'exécution, pour diff avec MAME.
//
//  Branché sur le hook d'instruction du cœur (une entrée par instruction
//  exécutée). Format pensé pour se comparer à la commande `trace` du
//  débogueur MAME :
//
//      00FC0030: move.w  #$2700,SR
//      00FC0034: move.l  #$00FC0000,A7
//
//  Avec --regs, chaque ligne est suivie de l'état des registres (utile pour
//  localiser précisément le point de divergence avec une trace MAME) :
//
//      00FC0030: move.w  #$2700,SR
//                D0=00000000 ... A7=00000000 SR=2700
//
//  Les interruptions prises sont aussi tracées (niveau + vecteur), car c'est un
//  point de divergence classique entre deux émulateurs.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

class Cpu68k;

class Tracer {
public:
    ~Tracer() { close(); }

    // CPU actif : le désassemblage et les registres tracés sont lus via lui.
    void setCpu(const Cpu68k* c) { cpu_ = c; }

    bool open(const std::string& path);     // "" ou "-" → stdout
    void close();
    bool isOpen() const { return f_ != nullptr; }

    void setLogRegs(bool b)       { logRegs_ = b; }
    void setLogInterrupts(bool b) { logIrq_  = b; }

    // Appelé pour chaque instruction exécutée en `pc`.
    void onInstruction(uint32_t pc);
    // Appelé au cycle d'acquittement d'interruption.
    void onInterrupt(int level, int vector);

    uint64_t instructionCount() const { return count_; }

private:
    const Cpu68k* cpu_ = nullptr;
    std::FILE* f_ = nullptr;
    bool ownsFile_ = false;
    bool logRegs_  = false;
    bool logIrq_   = false;
    uint64_t count_ = 0;
};
