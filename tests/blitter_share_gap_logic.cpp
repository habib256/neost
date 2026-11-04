// Tranches du blitter en partagé selon l'instruction du CPU : garde sans Verilator du résultat
// du banc tools/run_stblitter_bench.py, le seul qui voie stBlitter.sv.
// [D] stBlitter.sv:238-250 @ a00deb5b et fx68k @ 0602ee46 : le compteur de 64 voit tout front d'AS.
// Attendus relevés sur ce banc, programmes gap0_nop, gap2_exg, gap4_addql, créneaux MMU actifs.
#include "core/Blitter.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>

namespace {
struct Slice { int64_t start; int accesses; int64_t last; };

std::vector<Slice> run(uint16_t opcode) {
    Bus bus(1024u * 1024u);
    Cpu68k cpu(bus);
    Scheduler scheduler;
    Mfp mfp;
    Blitter blitter(bus);
    bus.cpu = &cpu; bus.mfp = &mfp; bus.blitter = &blitter; bus.machine = MachineType::Ste;
    cpu.setScheduler(&scheduler);
    scheduler.setLiveClock([&] { return cpu.busClockNow(); });
    blitter.setScheduler(&scheduler);
    scheduler.setCallback(Scheduler::BLITTER, [&] { blitter.onSlice(); });
    bus.romBase = 0xFC0000;
    bus.rom.assign(8192, 0);
    bus.rom[0] = 0; bus.rom[1] = 0; bus.rom[2] = 0x80; bus.rom[3] = 0;   // SSP = $8000
    bus.rom[4] = 0; bus.rom[5] = 0; bus.rom[6] = 0x10; bus.rom[7] = 0;   // PC = $1000

    // move.w #$8000,$FF8A3C.l puis l'instruction répétée : aucun autre accès que ses préfetchs.
    uint32_t pc = 0x1000;
    const auto put = [&](uint16_t w) { bus.write16(pc, w); pc += 2; };
    put(0x33FC); put(0x8000); put(0x00FF); put(0x8A3C);
    while (pc < 0x3000) put(opcode);

    for (uint32_t a : {0xFF8A20u, 0xFF8A22u, 0xFF8A2Eu, 0xFF8A30u}) blitter.write16(a, 2);
    blitter.write32(0xFF8A24, 0x10000);
    blitter.write32(0xFF8A32, 0x18000);
    for (uint32_t a : {0xFF8A28u, 0xFF8A2Au, 0xFF8A2Cu}) blitter.write16(a, 0xFFFF);
    blitter.write16(0xFF8A36, 100);
    blitter.write16(0xFF8A38, 4);
    blitter.write16(0xFF8A3A, 0x0203);

    std::vector<Slice> slices;
    blitter.accessObserver = [&](bool, uint32_t, uint16_t) {
        const int64_t t = scheduler.liveNow();
        if (slices.empty() || t - slices.back().last > 4) slices.push_back({t, 0, t});
        ++slices.back().accesses;
        slices.back().last = t;
    };
    cpu.reset();
    while (slices.size() < 5 && cpu.busClockNow() < 40000) cpu.run(64);
    return slices;
}
}  // namespace

int testBlitterShareGap() {
    struct Case { const char* name; uint16_t opcode; int accesses[4]; int64_t gap; };
    // gap : d'une date d'accès du blitter à la suivante, à travers la part du CPU.
    static const Case cases[] = {
        {"nop (accès suivant à +0)", 0x4E71, {63, 63, 63, 63}, 272},
        {"exg (accès suivant à +2)", 0xC141, {63, 64, 64, 64}, 524},
        {"addq.l (accès suivant à +4)", 0x5280, {63, 64, 64, 64}, 524},
    };
    int failures = 0;
    for (const Case& c : cases) {
        const std::vector<Slice> s = run(c.opcode);
        if (s.size() < 5) { std::printf("  FAIL %s : %zu tranches\n", c.name, s.size()); ++failures; continue; }
        for (int i = 0; i < 4; ++i) {
            if (s[i].accesses != c.accesses[i]) {
                std::printf("  FAIL %s : tranche %d, %d accès, attendu %d\n", c.name, i, s[i].accesses, c.accesses[i]);
                ++failures;
            }
            if (s[i + 1].start - s[i].last != c.gap) {
                std::printf("  FAIL %s : après la tranche %d, %lld cycles, attendu %lld\n", c.name, i,
                            static_cast<long long>(s[i + 1].start - s[i].last), static_cast<long long>(c.gap));
                ++failures;
            }
        }
    }
    return failures;
}
