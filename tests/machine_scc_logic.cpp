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
        const int64_t due = m->sched.liveNow() + 4;
        check(m->sched.rawCyclesUntil(Scheduler::SCC_BRG_A) > 0, "BRG scheduled after model switch");
        m->sched.runTo(due);
        check(m->scc.irqActive(), "BRG callback raises IRQ after model switch");
        m->reconfigure(512u * 1024u, CpuCore::Moira, MachineType::St);
        m->reset();
        check(m->bus.scc == nullptr, "SCC unmapped on ST");
        check(m->sched.rawCyclesUntil(Scheduler::SCC_BRG_A) < 0, "reset cancels previous BRG");
        check(!m->scc.irqActive(), "reset clears previous SCC IRQ");
    }
    std::printf("[machine-scc] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_MACHINE_SCC_TEST_MAIN
int main() { return testMachineScc() ? 1 : 0; }
#endif
