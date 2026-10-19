// Le pipeline série MIDI doit fonctionner via Bus et les callbacks de Machine.
#include "core/Machine.hpp"
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

int testMachineMidi() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[machine-midi] FAIL %s\n", label); }
    };
    for (auto profile : {MachineType::St, MachineType::MegaSte}) {
        auto m = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, profile);
        std::vector<std::pair<uint8_t, int64_t>> sent;
        m->midi.setMidiSinkTimed([&](uint8_t b, int64_t cycle) { sent.emplace_back(b, cycle); });
        m->bus.write8(0xFFFC04, 0xB5); // /16, 8N1, RX et TX IRQ
        m->bus.write8(0xFFFC06, 0x90);
        check(sent.empty(), "Bus write waits for physical MIDI output");
        m->sched.runTo(256);
        check((m->bus.read8(0xFFFC04) & 0x82) == 0x82, "TDR load raises ready IRQ");
        check(sent.empty(), "ready does not mean character completed");
        m->bus.write8(0xFFFC06, 0x3C);
        m->sched.runTo(2560);
        check(sent == std::vector<std::pair<uint8_t, int64_t>>{{0x90, 2560}},
              "Machine callback emits first timestamped byte");
        m->sched.runTo(2816);
        m->sched.runTo(5120);
        check(sent == std::vector<std::pair<uint8_t, int64_t>>{{0x90, 2560}, {0x3C, 5120}},
              "pending character follows at wire speed");
        m->bus.write8(0xFFFC06, 0x40);
        m->reset();
        check(m->sched.cyclesUntil(Scheduler::MIDI_TX) < 0, "reset cancels MIDI transmission");
        m->sched.runTo(8000);
        check(sent.size() == 2, "cancelled byte never reaches sink");
        m->midi.setMidiSinkTimed({});
        m->midi.setLoopback(true);
        m->bus.write8(0xFFFC04, 0x95);
        m->bus.write8(0xFFFC06, 0x51);
        m->sched.runTo(8256);
        check(!(m->bus.read8(0xFFFC04) & 1), "loopback waits for stop bit");
        m->sched.runTo(10560);
        check((m->bus.read8(0xFFFC04) & 1) && m->bus.read8(0xFFFC06) == 0x51,
              "loopback reaches MIDI receiver through Machine callback");
        // Deux caractères achevés sans lecture intermédiaire : un seul RDR.
        int next = 0;
        m->midi.setRxSource([&](uint8_t& b) {
            if (next == 2) return false;
            b = uint8_t(0x61 + next++); return true;
        });
        m->sched.runTo(13120);
        m->sched.runTo(15680);
        m->midi.setRxSource({});
        check((m->bus.read8(0xFFFC04) & 0xA1) == 0x81,
              "overrun waits for RDR read before exposing OVRN");
        check((m->bus.read8(0xFFFA01) & 0x10) == 0, "MIDI receiver pulls shared GPIP4 low");
        check(m->bus.read8(0xFFFC06) == 0x61, "full RDR retains oldest completed byte");
        check((m->bus.read8(0xFFFC04) & 0xA1) == 0x20,
              "RDR read publishes overrun and releases receiver IRQ");
        check((m->bus.read8(0xFFFA01) & 0x10) != 0, "RX acknowledgement releases GPIP4");
        check(m->bus.read8(0xFFFC06) == 0x61 && !(m->bus.read8(0xFFFC04) & 0x21),
              "SR then RDR clears OVRN without a phantom second byte");
    }
    std::printf("[machine-midi] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
