// =============================================================================
// SCC BRG / Zero Count et miroirs : puce nue et Scheduler, MMIO public uniquement.
// Référence : Zilog UM0109 p.57/p.71/p.177 : compteur avant /2 et WR4.
// =============================================================================
#include "io/Scc.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
struct Checks {
    int passed = 0, failed = 0;
    void equal(const char* name, int64_t actual, int64_t expected) {
        if (actual == expected) { ++passed; return; }
        ++failed;
        std::fprintf(stderr, "[scc-brg] FAIL %s: got %lld, expected %lld\n", name,
                     static_cast<long long>(actual), static_cast<long long>(expected));
    }
};
struct Rig {
    Scheduler sched;
    Scc scc;
    int events[2] = {};
    Rig() {
        scc.setScheduler(&sched);
        sched.setCallback(Scheduler::SCC_BRG_A, [this] { ++events[0]; scc.onBrgEvent(0); });
        sched.setCallback(Scheduler::SCC_BRG_B, [this] { ++events[1]; scc.onBrgEvent(1); });
    }
    void wr(int ch, uint8_t reg, uint8_t value) {
        const uint32_t addr = 0xFF8C81u + uint32_t(ch) * 4;
        scc.write8(addr, reg); scc.write8(addr, value);
    }
    uint8_t rr(int ch, uint8_t reg) {
        const uint32_t addr = 0xFF8C81u + uint32_t(ch) * 4;
        scc.write8(addr, reg); return scc.read8(addr);
    }
    void command(int ch, uint8_t value) { scc.write8(0xFF8C81u + uint32_t(ch) * 4, value); }
    void arm(int ch, uint8_t wr4 = 4, uint8_t wr14 = 3) {
        wr(ch, 15, 2); wr(ch, 1, 1); wr(ch, 4, wr4);
        wr(ch, 12, 0); wr(ch, 13, 0); wr(ch, 14, wr14);
    }
};
} // namespace

bool testSccBrg() {
    Checks c;
    {
        Rig r;
        c.equal("reset: no event", r.sched.nextDue(), -1);
        r.wr(0, 2, 0x40); r.wr(0, 9, 9); r.arm(0);
        c.equal("PCLK TC0 x1: 2 cycles", r.sched.nextDue(), 2);
        r.sched.runTo(1); c.equal("no IRQ before count zero", r.scc.irqActive(), false);
        r.sched.runTo(2);
        c.equal("A zero count: IRQ", r.scc.irqActive(), true);
        c.equal("A zero count: external IP", r.rr(0, 3), 8);
        c.equal("RR0 current zero count pulse", r.rr(0, 0) & 2, 2);
        c.equal("vector A external", r.scc.processIack(), 0x4A);
        c.equal("IACK hides IRQ through IUS", r.scc.irqActive(), false);
        r.sched.runTo(4);
        c.equal("IUS blocks next zero count", r.scc.irqActive(), false);
        r.command(0, 0x38);
        c.equal("release IUS reveals pending IP", r.scc.irqActive(), true);
        r.command(0, 0x10);
        c.equal("reset external status clears IP", r.rr(0, 3), 0);
        c.equal("reset external status clears IRQ", r.scc.irqActive(), false);
        r.sched.runTo(6);
        c.equal("later zero count retriggers", r.scc.irqActive(), true);
    }
    {
        Rig r;
        r.wr(0, 9, 8); r.arm(0);
        r.wr(0, 1, 0);
        r.sched.runTo(2);
        c.equal("WR1 masks zero count", r.rr(0, 3), 0);
        c.equal("masked BRG still runs", r.events[0], 1);
        r.wr(0, 1, 1); r.wr(0, 15, 0);
        // WR1 réactive la cause déjà verrouillée ; la servir avant de tester WR15.
        r.command(0, 0x10);
        r.sched.runTo(4);
        c.equal("WR15 masks zero count", r.rr(0, 3), 0);
        r.wr(0, 15, 2); r.wr(0, 9, 0);
        r.sched.runTo(6);
        c.equal("MIE off preserves external IP", r.rr(0, 3), 8);
        c.equal("MIE off masks IRQ", r.scc.irqActive(), false);
        r.wr(0, 9, 8);
        c.equal("MIE on reveals pending IRQ", r.scc.irqActive(), true);
        r.command(0, 0x10); r.wr(0, 14, 2);
        c.equal("WR14 disable cancels event", r.sched.nextDue(), -1);
        r.sched.runTo(100);
        c.equal("disabled BRG does not retrigger", r.scc.irqActive(), false);
        c.equal("disabled BRG event count", r.events[0], 3);
    }
    {
        Rig r;
        r.arm(0, 0x44);
        c.equal("WR4 x16 does not divide Zero Count", r.sched.nextDue(), 2);
        r.sched.runTo(1); r.wr(0, 12, 10); r.wr(0, 13, 1);
        c.equal("TC writes preserve active counter", r.sched.nextDue(), 2);
        r.sched.runTo(2);
        c.equal("new TC applied at reload", r.sched.nextDue(), 270);
        r.wr(0, 4, 0xC4); r.wr(0, 11, 0xF8);
        c.equal("WR4 and WR11 leave counter phase", r.sched.nextDue(), 270);
        r.wr(0, 14, 1);
        c.equal("RTxC source restarts with exact fraction", r.sched.nextDue(), 588);
        r.wr(0, 14, 0); r.wr(0, 12, 255); r.wr(0, 13, 255); r.wr(0, 14, 1);
        c.equal("TC max counter has no baud clamp", r.sched.nextDue(),
                2 + (neost::pacing::kCpuHzInt*65537 + 3671999)/3672000);
    }
    {
        Rig r;
        r.arm(0); r.arm(1);
        r.wr(0, 2, 0x40); r.wr(0, 9, 9);
        r.sched.runTo(4);
        c.equal("both channels external IP", r.rr(0, 3), 9);
        c.equal("A has priority", r.scc.processIack(), 0x4A);
        r.command(0, 0x10);
        c.equal("A IUS blocks B", r.scc.irqActive(), false);
        r.command(0, 0x38);
        c.equal("B visible after release A IUS", r.scc.irqActive(), true);
        c.equal("vector B external", r.scc.processIack(), 0x42);
        r.command(1, 0x10); r.command(1, 0x38);
        c.equal("both acknowledgements leave no IRQ", r.scc.irqActive(), false);
    }
    {
        Rig r;
        r.arm(0, 0xC0);             // sync x1, reset soft rend WR4 async x64
        r.arm(1, 0x44);
        r.sched.runTo(1);
        r.wr(0, 9, 0x80);           // reset canal A : BRG enable/source conservés
        c.equal("channel reset preserves BRG phase", r.sched.nextDue(), 2);
        r.sched.runTo(2);
        c.equal("channel reset preserves counter cadence", r.sched.nextDue(), 4);
        c.equal("channel reset clears WR1 interrupt enable", r.rr(0, 3) & 8, 0);
        r.wr(0, 14, 2);
        c.equal("A disabled leaves B event", r.sched.nextDue(), 4);
        r.wr(1, 9, 0x40);
        c.equal("channel B reset preserves BRG phase", r.sched.nextDue(), 4);
        r.wr(0, 9, 0xC0);
        c.equal("WR9 full hardware reset cancels both BRGs", r.sched.nextDue(), -1);
        r.arm(0); r.scc.reset();
        c.equal("machine reset cancels BRG", r.sched.nextDue(), -1);
    }
    {
        Rig r;
        r.wr(0, 9, 8); r.arm(0, 0x44); r.sched.runTo(1);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); r.scc.serialize(save); r.sched.serialize(save);
        r.sched.runTo(64); r.command(0, 0x10); r.scc.reset();
        auto load = StateArchive::loader(state.data(), state.size());
        r.scc.serialize(load); r.sched.serialize(load);
        c.equal("save-state valid", load.ok(), true);
        c.equal("save-state restores clock", r.sched.now(), 1);
        c.equal("save-state preserves remaining BRG phase", r.sched.nextDue(), 2);
        r.sched.runTo(1); c.equal("restored BRG not early", r.scc.irqActive(), false);
        r.sched.runTo(2); c.equal("restored BRG IRQ", r.scc.irqActive(), true);
        c.equal("restored BRG next period", r.sched.nextDue(), 4);
    }
    {
        Rig r;
        int64_t live = 100;
        r.sched.setLiveClock([&live] { return live; });
        r.arm(0);
        c.equal("programming uses live CPU clock", r.sched.nextDue(), 102);
        live = 106;
        r.sched.runTo(106);
        c.equal("dispatch overshoot does not drift", r.sched.nextDue(), 104);
    }
    {
        Rig r;
        r.arm(0, 0x44);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); r.scc.serialize(save);
        // Les deux périodes sont les derniers champs sérialisés du SCC.
        const std::size_t at = state.size() - 2 * sizeof(int64_t);
        for (int64_t bad : {int64_t(-1), neost::pacing::kCpuHzInt + 1, int64_t(1)}) {
            auto damaged = state;
            std::memcpy(damaged.data() + at, &bad, sizeof(bad));
            auto load = StateArchive::loader(damaged.data(), damaged.size());
            r.scc.serialize(load);
            c.equal("forged BRG period rejected", load.ok(), false);
        }
        const int64_t stopped = 0;
        std::memcpy(state.data() + at, &stopped, sizeof(stopped));
        auto load = StateArchive::loader(state.data(), state.size()); r.scc.serialize(load);
        c.equal("zero BRG period valid", load.ok(), true);
        r.sched.runTo(64);
        c.equal("zero period pending event does not loop", r.sched.nextDue(), -1);
    }
    {
        // UM0109 : RR6/RR7 sont les images de RR2/RR3 du même canal.
        // Aucun accès RR2 préalable : le miroir doit calculer le vecteur courant.
        Rig r;
        r.wr(0, 2, 0x40); r.wr(0, 9, 9); r.arm(0);
        r.sched.runTo(4);
        c.equal("RR6A refreshes base vector", r.rr(0, 6), 0x40);
        c.equal("RR6B uses modified vector", r.rr(1, 6), 0x4A);
        c.equal("RR7A mirrors pending sources", r.rr(0, 7), 8);
        c.equal("RR7B is zero despite A pending", r.rr(1, 7), 0);
        r.wr(0, 2, 0x60);
        c.equal("RR6B reflects new WR2 without RR2 read", r.rr(1, 6), 0x6A);
        // Les quatre combinaisons WR15A/B vérifient les deux sélecteurs locaux.
        // FIFO SDLC non remplie dans ce modèle : les deux octets restent nuls.
        for (uint8_t fifoA : {uint8_t(0), uint8_t(4)}) {
            for (uint8_t fifoB : {uint8_t(0), uint8_t(4)}) {
                r.wr(0, 15, uint8_t(2 | fifoA)); r.wr(1, 15, fifoB);
                c.equal("RR6A local FIFO selection", r.rr(0, 6), fifoA ? 0 : 0x60);
                c.equal("RR7A local FIFO selection", r.rr(0, 7), fifoA ? 0 : 8);
                c.equal("RR6B local FIFO selection", r.rr(1, 6), fifoB ? 0 : 0x6A);
                c.equal("RR7B local FIFO selection", r.rr(1, 7), 0);
                r.rr(0, 11); r.rr(1, 15);
                c.equal("RR11 read preserves A FIFO selection", r.rr(0, 6), fifoA ? 0 : 0x60);
                c.equal("RR15 read preserves B FIFO selection", r.rr(1, 6), fifoB ? 0 : 0x6A);
            }
        }
        for (int ch : {0, 1}) {
            r.wr(ch, 13, uint8_t(0x31 + ch));
            c.equal("RR9 mirrors local RR13", r.rr(ch, 9), 0x31 + ch);
            c.equal("RR4 mirrors local RR0", r.rr(ch, 4), r.rr(ch, 0));
            c.equal("RR5 mirrors local RR1", r.rr(ch, 5), r.rr(ch, 1));
            c.equal("RR14 mirrors local RR10", r.rr(ch, 14), r.rr(ch, 10));
            c.equal("RR11 mirrors local RR15", r.rr(ch, 11), r.rr(ch, 15));
        }
    }
    {
        Rig r;
        r.wr(0, 2, 0x40); r.wr(0, 9, 0x29); r.arm(0);
        r.sched.runTo(4);
        c.equal("RR6 mirrors software IACK vector", r.rr(1, 6), 0x4A);
        c.equal("RR6 software IACK sets IUS", r.scc.irqActive(), false);
        r.command(0, 0x38);
        c.equal("RR6 software IACK released by reset IUS", r.scc.irqActive(), true);
    }
    std::fprintf(stderr, "[scc-brg] %d OK, %d FAIL\n", c.passed, c.failed);
    return c.failed == 0;
}
