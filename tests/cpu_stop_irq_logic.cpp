// [interne] Un événement sans effet placé avant HBL ne doit pas retarder le CPU.
// Deux STOP successifs, comme le synchroniseur de shforstv. Le chemin préarmé
// doit rejoindre le même réveil que le chemin dispatché, sans recopier ses cycles.
#include "core/Machine.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
std::vector<uint8_t> probeRom() {
    std::vector<uint8_t> rom(256 * 1024, 0xFF);
    size_t at = 0;
    auto word = [&](uint16_t v) { rom[at++] = uint8_t(v >> 8); rom[at++] = uint8_t(v); };
    auto lng = [&](uint32_t v) { word(uint16_t(v >> 16)); word(uint16_t(v)); };
    auto byteWrite = [&](uint8_t v, uint32_t a) { word(0x13FC); word(v); lng(a); };
    word(0x602E); word(0x0206); lng(0xE00030);
    while (at < 0x30) word(0);
    word(0x46FC); word(0x2700);                 // move #$2700,sr
    word(0x4FF9); lng(0x8000);                  // lea pile,sp
    byteWrite(0x0A, 0xFF8001);                  // 4 Mo
    byteWrite(1, 0xFF8201); byteWrite(0, 0xFF8203); byteWrite(0, 0xFF820D);
    byteWrite(0, 0xFF8260); byteWrite(2, 0xFF820A);
    word(0x21FC); const size_t hbl = at; lng(0); word(0x0068);
    word(0x21FC); const size_t vbl = at; lng(0); word(0x0070);
    word(0x41F9); lng(0x20000);                 // lea résultats,a0
    word(0x43F9); lng(0xFF8209);                // lea compteur vidéo,a1
    for (int i = 0; i < 3; ++i) { word(0x4E72); word(0x2300); }
    word(0x303C); word(511);
    const size_t loop = at;
    word(0x4E72); word(0x2100); word(0x4E72); word(0x2100);
    for (int i = 0; i < 40; ++i) word(0x4E71); // lire en plein DE
    word(0x10D1);                             // move.b (a1),(a0)+
    word(0x51C8); word(uint16_t(int(loop) - int(at)));
    word(0x4E72); word(0x2300); word(0x60FA);   // attente finale
    const uint32_t irq = 0xE00000 + uint32_t(at);
    word(0x4E73);                             // les deux IRQ retournent aussitôt
    at = hbl; lng(irq); at = vbl; lng(irq);
    return rom;
}
struct Result { std::array<uint8_t, 512> data; bool halted; };
Result runProbe(int phase) {
    auto m = std::make_unique<Machine>(4 * 1024 * 1024, CpuCore::Moira, MachineType::Ste);
    m->bus.rom = probeRom(); m->bus.romBase = 0xE00000;
    m->reset(); m->stepInstruction();
    for (int i = 0; i < 3; ++i) m->runFrame();
    m->stepInstruction();                     // amorcer la trame et armer HBL
    int64_t next = m->sched.liveNow() + m->sched.rawCyclesUntil(Scheduler::HBL) + phase - 512;
    if (phase >= 0) {
        // Réutiliser une source inactive ; le callback ne touche aucune puce.
        m->sched.setCallback(Scheduler::MICROWIRE, [&] {
            next += 512;
            m->sched.schedule(Scheduler::MICROWIRE, next);
        });
        m->sched.schedule(Scheduler::MICROWIRE, next);
    }
    for (int i = 0; i < 10; ++i) m->runFrame();
    Result r{};
    std::copy_n(m->bus.ram.begin() + 0x20000, r.data.size(), r.data.begin());
    r.halted = m->cpu.halted();
    return r;
}
}

int testCpuStopIrq() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[cpu-stop-irq] FAIL: %s\n", label); }
    };
    const auto baseline = runProbe(-1);
    check(!baseline.halted, "baseline executes without halting");
    check(std::any_of(baseline.data.begin(), baseline.data.end(), [](uint8_t v) { return v != 0; }),
          "probe reads video during display");
    for (int phase = 504; phase < 512; ++phase) {
        const auto candidate = runProbe(phase);
        check(!candidate.halted && candidate.data == baseline.data,
              "no-op event before HBL leaves all 512 reads unchanged");
    }
    std::printf("[cpu-stop-irq] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
