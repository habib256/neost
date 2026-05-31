// =============================================================================
//  Tracer.cpp — Désassemblage + dump registres via l'API Musashi.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Tracer.hpp"

#if defined(NEOST_HAS_MUSASHI)
extern "C" {
#include "m68k.h"
}
#endif

bool Tracer::open(const std::string& path) {
    close();
    if (path.empty() || path == "-") {
        f_ = stdout;
        ownsFile_ = false;
    } else {
        f_ = std::fopen(path.c_str(), "w");
        ownsFile_ = (f_ != nullptr);
    }
    count_ = 0;
    return f_ != nullptr;
}

void Tracer::close() {
    if (f_ && ownsFile_) std::fclose(f_);
    f_ = nullptr;
    ownsFile_ = false;
}

void Tracer::onInstruction(uint32_t pc) {
    if (!f_) return;
    ++count_;
#if defined(NEOST_HAS_MUSASHI)
    char dis[256];
    // Désassemble via les callbacks "disassembler" (lecture sans effet de bord).
    m68k_disassemble(dis, pc, M68K_CPU_TYPE_68000);
    if (logRegs_) {
        std::fprintf(f_, "%06X: %-38s "
            "D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X "
            "A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X A7=%08X SR=%04X\n",
            pc, dis,
            m68k_get_reg(nullptr, M68K_REG_D0), m68k_get_reg(nullptr, M68K_REG_D1),
            m68k_get_reg(nullptr, M68K_REG_D2), m68k_get_reg(nullptr, M68K_REG_D3),
            m68k_get_reg(nullptr, M68K_REG_D4), m68k_get_reg(nullptr, M68K_REG_D5),
            m68k_get_reg(nullptr, M68K_REG_D6), m68k_get_reg(nullptr, M68K_REG_D7),
            m68k_get_reg(nullptr, M68K_REG_A0), m68k_get_reg(nullptr, M68K_REG_A1),
            m68k_get_reg(nullptr, M68K_REG_A2), m68k_get_reg(nullptr, M68K_REG_A3),
            m68k_get_reg(nullptr, M68K_REG_A4), m68k_get_reg(nullptr, M68K_REG_A5),
            m68k_get_reg(nullptr, M68K_REG_A6), m68k_get_reg(nullptr, M68K_REG_A7),
            m68k_get_reg(nullptr, M68K_REG_SR));
    } else {
        std::fprintf(f_, "%06X: %s\n", pc, dis);
    }
#else
    std::fprintf(f_, "%06X: <musashi absent>\n", pc);
#endif
}

void Tracer::onInterrupt(int level, int vector) {
    if (!f_ || !logIrq_) return;
    std::fprintf(f_, ">>> IRQ niveau %d, vecteur $%02X\n", level, vector & 0xFF);
}
