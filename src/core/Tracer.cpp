// =============================================================================
//  Tracer.cpp — Désassemblage + dump registres via l'API Musashi.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Tracer.hpp"
#include "core/Cpu68k.hpp"

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
        // Registres lus via le CPU actif (core-aware) ; sans CPU câblé, repli sur
        // l'API Musashi pour ne rien casser des anciens points d'appel.
        uint32_t d[8], a[8];
        uint16_t sr;
        if (cpu_) {
            for (int i = 0; i < 8; ++i) { d[i] = cpu_->reg(i); a[i] = cpu_->reg(8 + i); }
            sr = cpu_->sr();
        } else {
            for (int i = 0; i < 8; ++i) {
                d[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i));
                a[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i));
            }
            sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
        }
        std::fprintf(f_, "%06X: %-38s "
            "D0=%08X D1=%08X D2=%08X D3=%08X D4=%08X D5=%08X D6=%08X D7=%08X "
            "A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X A7=%08X SR=%04X\n",
            pc, dis,
            d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
            a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], sr);
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
