// Créneau de 4 cycles hors RAM : $FF8920-$FF893F sur STE seulement, pas $FF820A.
// [B] ST-4081 feuilles 8, 19 et 21 (ST4081S.PDF, sha256 935c4dd9) : CMAP = VMAPB ou SMAPB, cadencé
// par CYCSEL comme RAMSEL ; SYNCSEL pose DTACK sans cadencement. [D] gstmcu.v @ 4a6db7cc, Vste_tb --dtack.
// Le CPU décale sa phase par « move.w -(a0),d0 » : 2 cycles internes avant la lecture. Sur ST, $FF8922
// est une bus error, et la condition sur la machine se lit dans le code.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
// Durée de move.w -(a0),d0 exécuté en ROM, sans créneau : aucun accès suivant ne rattrape la phase.
// L'instruction part alignée sur 4 ; la lecture tombe donc à 4n + 2.
int64_t duration(MachineType machine, uint32_t target) {
    Bus bus(1024u * 1024u);
    Cpu68k cpu(bus);
    Scheduler scheduler;
    Mfp mfp;
    bus.cpu = &cpu; bus.mfp = &mfp; bus.machine = machine;
    cpu.setScheduler(&scheduler);
    scheduler.setLiveClock([&] { return cpu.busClockNow(); });
    bus.romBase = 0xFC0000;
    bus.rom.assign(0x4000, 0);
    const auto word = [&](uint32_t off, uint16_t w) { bus.rom[off] = uint8_t(w >> 8); bus.rom[off + 1] = uint8_t(w); };
    word(0, 0); word(2, 0x8000);                    // SSP = $8000
    word(4, 0xFC); word(6, 0x1000);                 // PC = $FC1000
    for (uint32_t off = 0x1000; off < 0x1008; off += 2) word(off, 0x4E71);
    word(0x1008, 0x3020);                           // move.w -(a0),d0
    for (uint32_t off = 0x100A; off < 0x1020; off += 2) word(off, 0x4E71);
    cpu.reset();
    cpu.setReg(8, target + 2);
    while (cpu.pc() < 0xFC1008) cpu.run(1);
    cpu.addBusWaitCycles((4 - (cpu.busClockNow() & 3)) & 3);
    const int64_t start = cpu.busClockNow();
    while (cpu.pc() < 0xFC100C) cpu.run(1);
    return cpu.busClockNow() - start;
}
}  // namespace

int testSteCmapSlot() {
    struct Case { const char* name; MachineType machine; uint32_t target; int64_t extra; };
    // Attendu : écart à la même lecture en ROM (sans créneau) sur la même machine.
    static const Case cases[] = {
        {"STE $FF8922 attend le créneau", MachineType::Ste, 0xFF8922, 2},
        {"STE $FF8938 attend le créneau", MachineType::Ste, 0xFF8938, 2},
        {"STE $FF8900 répond aussitôt", MachineType::Ste, 0xFF8900, 0},
        {"STE $FF8200 répond aussitôt", MachineType::Ste, 0xFF8200, 0},
    };
    int failures = 0;
    for (const Case& c : cases) {
        const int64_t base = duration(c.machine, 0xFC0100);
        const int64_t got = duration(c.machine, c.target) - base;
        if (got != c.extra) {
            std::printf("  FAIL %s : %lld cycles de plus, attendu %lld\n", c.name, static_cast<long long>(got),
                        static_cast<long long>(c.extra));
            ++failures;
        }
    }
    return failures;
}
