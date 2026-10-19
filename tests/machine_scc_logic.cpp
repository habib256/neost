// Le câblage BRG doit survivre à un changement de profil sans recréer Machine.
#include "core/Machine.hpp"
#include <cstdio>
#include <memory>

int testMachineScc() {
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[machine-scc] FAIL: %s\n", label); }
    };
    auto m = std::make_unique<Machine>(512u * 1024u, CpuCore::Moira, MachineType::St);
    auto wr = [&](uint8_t reg, uint8_t value) {
        m->bus.write8(0xFF8C81, reg); m->bus.write8(0xFF8C81, value);
    };
    auto arm = [&] {
        wr(9, 9); wr(15, 2); wr(1, 1); wr(4, 4);
        wr(12, 0); wr(13, 0); wr(14, 3);
    };
    for (int pass = 0; pass < 2; ++pass) {
        m->reconfigure(512u * 1024u, CpuCore::Moira, MachineType::MegaSte);
        m->reset();
        check(m->bus.scc == &m->scc, "SCC mapped after switching to Mega STE");
        arm();
        const int64_t due = m->sched.liveNow() + 2; // TC=0 : ZC après TC+2 clocks PCLK
        check(m->sched.rawCyclesUntil(Scheduler::SCC_BRG_A) > 0, "BRG scheduled after model switch");
        m->sched.runTo(due - 1);
        check(!m->scc.irqActive(), "Zero Count waits for counter expiry");
        m->sched.runTo(due);
        check(m->scc.irqActive(), "BRG callback raises IRQ after model switch");
        // Réarmer chaque canal et observer les quatre callbacks série, après
        // reset/reconfigure : les échéances seules ne prouvent pas le câblage.
        m->reset();
        int sent[2] = {};
        m->scc.setSerialSink([&](int channel, uint8_t) { ++sent[channel]; });
        const int64_t serialStart = m->sched.liveNow();
        for (int channel = 0; channel < 2; ++channel) {
            auto serialWr = [&](uint8_t reg, uint8_t value) {
                const uint32_t address = 0xFF8C81 + 4 * channel;
                m->bus.write8(address, reg); m->bus.write8(address, value);
            };
            serialWr(4, 4); serialWr(3, 0xc1); serialWr(5, 0x68);
            serialWr(11, 0x50); serialWr(12, 0); serialWr(13, 0); serialWr(14, 3);
            m->bus.write8(0xFF8C83 + 4 * channel, uint8_t(0x41 + channel));
            m->scc.receiveByte(channel, uint8_t(0x51 + channel));
        }
        m->sched.runTo(serialStart + 39);
        check(sent[0] == 0 && sent[1] == 0, "serial callbacks wait character time");
        m->sched.runTo(serialStart + 40);
        check(sent[0] == 1 && sent[1] == 1, "both TX callbacks survive model switch");
        check(m->bus.read8(0xFF8C83) == 0x51 && m->bus.read8(0xFF8C87) == 0x52,
              "both RX callbacks survive model switch");
        // Laisser quatre transferts en vol avant de quitter le profil SCC.
        for (int channel = 0; channel < 2; ++channel) {
            m->bus.write8(0xFF8C83 + 4 * channel, 0x71);
            m->scc.receiveByte(channel, 0x72);
        }
        m->scc.setSerialSink({});
        m->reconfigure(512u * 1024u, CpuCore::Moira, MachineType::St);
        m->reset();
        check(m->bus.scc == nullptr, "SCC unmapped on ST");
        check(m->sched.rawCyclesUntil(Scheduler::SCC_BRG_A) < 0, "reset cancels previous BRG");
        for (auto source : {Scheduler::SCC_TX_A, Scheduler::SCC_TX_B, Scheduler::SCC_RX_A, Scheduler::SCC_RX_B})
            check(m->sched.rawCyclesUntil(source) < 0, "reset cancels previous serial event");
        check(!m->scc.irqActive(), "reset clears previous SCC IRQ");
    }
    std::printf("[machine-scc] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_MACHINE_SCC_TEST_MAIN
int main() { return testMachineScc() ? 1 : 0; }
#endif
