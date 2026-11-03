// =============================================================================
//  moira_bare_bench.cpp — Moira nu sur une RAM plate : trace de ses cycles bus.
//
//  POURQUOI. Pendant du pilote fx68k (tools/fx68k_bench.cpp) : le même programme, le
//  même modèle mémoire idéal (16 Mio de RAM, aucun état d'attente, interruptions
//  vectorisées par le périphérique), et une trace au même format, pour que
//  tools/run_fx68k_bench.py compare les deux cœurs accès par accès. Ni Bus, ni
//  Machine : la copie vendorisée de Moira telle que NeoST la compile (MoiraConfig.h de
//  build/generated : PRECISE_TIMING, FC émulés), seule. Ce qu'on mesure est le cœur,
//  pas la carte mère.
//
//  CONVENTION DE DATATION. Moira encadre chaque cycle bus de deux SYNC(2) :
//  willAccessBus (S0), SYNC(2), lecture/écriture, SYNC(2) — MoiraDataflow_cpp.h, read()
//  et write(). Le rappel tombe donc à S0 + 2 cycles. [B] M68000UM Rev 8 § 5.1 : AS tombe
//  au début de S2, un cycle après S0. Chaque accès est daté `clock − 1`, soit le front
//  descendant d'AS, comme côté fx68k. Les lectures des vecteurs de reset
//  (read16OnReset) et l'IACK (readIrqUserVector) n'ont pas de willAccessBus : même
//  convention, appliquée à leur rappel. L'IACK est daté `clock − 3` : Moira l'encadre de
//  SYNC(iackSyncBefore = 4) et SYNC(4) (MoiraExceptions_cpp.h, execInterrupt), et les
//  4 cycles qui PRÉCÈDENT la lecture du vecteur sont le cycle bus d'IACK lui-même —
//  fx68k enchaîne l'IACK aussitôt après l'écriture du PC bas, sans cycle mort. Le
//  comparateur rapporte de toute façon l'écart de cette date à part.
//
//  Usage : neost-moira-bare <image.bin> <trace.txt> [--limit N] [--irq T:L]... [--ipl-fetch]
//  Mêmes arguments et même sortie que Vfx68k_bench. --ipl-fetch active le port de
//  WinUAE ipl_fetch_next (seuils 4/2, ce que NeoST fait sous NEOST_IPLFETCH=1) : le
//  banc dit alors si cette option rapproche Moira du RTL sur la prise d'IPL.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Moira.h"

namespace {

using moira::u8;
using moira::u16;
using moira::u32;
using moira::i64;

class BareMoira : public moira::Moira {
public:
    mutable std::vector<u8> ram;   // écrit depuis write8/16, déclarés const par Moira
    FILE* out = nullptr;
    struct IrqEv { i64 t; int level; };
    std::vector<IrqEv> irqs;
    size_t irqIdx = 0;
    mutable i64 tFirst = -1;
    mutable bool done = false;

    BareMoira() : ram(1u << 24) {
        setModel(moira::Model::M68000);
        irqMode = moira::IrqMode::USER;
    }

    int fc() const { return (fcl & 3) | (reg.sr.s ? 4 : 0); }

    void emit(int fcode, bool write, int size, u32 addr, u16 data, int lead = 1) const {
        const i64 t = clock - lead;
        if (tFirst < 0) tFirst = t;
        std::fprintf(out, "%lld %d %c %d %06X %0*X\n", (long long)t, fcode, write ? 'W' : 'R',
                     size, addr & 0xFFFFFF, size == 2 ? 4 : 2, data);
        if (write && (addr & 0xFF0000) == 0xF00000) done = true;
    }

    u8 read8(u32 a) const override {
        const u8 v = ram[a & 0xFFFFFF];
        emit(fc(), false, 1, a, v);
        return v;
    }
    u16 read16(u32 a) const override {
        a &= 0xFFFFFE;
        const u16 v = u16(ram[a] << 8 | ram[a + 1]);
        emit(fc(), false, 2, a, v);
        return v;
    }
    u16 read16OnReset(u32 a) const override { return read16(a); }
    void write8(u32 a, u8 v) const override {
        ram[a & 0xFFFFFF] = v;
        emit(fc(), true, 1, a, v);
    }
    void write16(u32 a, u16 v) const override {
        a &= 0xFFFFFE;
        ram[a] = u8(v >> 8); ram[a + 1] = u8(v);
        emit(fc(), true, 2, a, v);
    }
    u16 readIrqUserVector(u8 level) const override {
        // [B] M68000UM § 6.3.2 : A1-A3 = niveau, A4-A23 à 1, FC = 7, LDS seul.
        emit(7, false, 1, 0xFFFFF1 | (u32(level) << 1), u16(0x40 + level), 3);
        const_cast<BareMoira*>(this)->setIPL(0);     // le périphérique est acquitté
        return u16(0x40 + level);
    }
    // Les --irq sont appliqués dans sync(), en cours d'instruction, comme NeoST lève
    // la broche depuis son ordonnanceur (Cpu68k.cpp, hook sync).
    void sync(int cycles) override {
        clock += cycles;
        if (tFirst < 0) return;
        while (irqIdx < irqs.size() && (clock - 1) - tFirst >= irqs[irqIdx].t)
            setIPL(u8(irqs[irqIdx++].level));
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s image.bin trace.txt [--limit N] [--irq T:L]...\n", argv[0]); return 2; }
    i64 limit = 400000;
    BareMoira cpu;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--limit") && i + 1 < argc) limit = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--irq") && i + 1 < argc) {
            const char* s = argv[++i]; const char* c = std::strchr(s, ':');
            if (!c) { std::fprintf(stderr, "--irq attend T:L\n"); return 2; }
            cpu.irqs.push_back({std::atoll(s), std::atoi(c + 1)});
        } else if (!std::strcmp(argv[i], "--ipl-fetch")) cpu.setIplDelay(4, 2);
        else { std::fprintf(stderr, "argument inconnu : %s\n", argv[i]); return 2; }
    }
    if (FILE* f = std::fopen(argv[1], "rb")) {
        const size_t n = std::fread(cpu.ram.data(), 1, cpu.ram.size(), f);
        std::fclose(f);
        if (!n) { std::fprintf(stderr, "image vide : %s\n", argv[1]); return 2; }
    } else { std::fprintf(stderr, "image illisible : %s\n", argv[1]); return 2; }
    cpu.out = std::fopen(argv[2], "w");
    if (!cpu.out) { std::fprintf(stderr, "trace inécrivable : %s\n", argv[2]); return 2; }

    std::string reason = "limit";
    cpu.reset();
    while (!cpu.done && cpu.getClock() < limit) {
        if (cpu.isHalted()) { reason = "halt"; break; }
        cpu.execute();
    }
    if (cpu.done) reason = "sentinel";
    std::fprintf(cpu.out, "END t=%lld reason=%s\n", (long long)cpu.getClock(), reason.c_str());
    std::fclose(cpu.out);
    return 0;
}
