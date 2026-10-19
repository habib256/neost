// UM0109 p.95 / p.154 : /RTS attend le dernier stop en asynchrone Auto Enables.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccRts() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { ++failures; std::fprintf(stderr, "SCC RTS FAIL: %s\n", name); }
    };
    struct Rig {
        Scheduler sched; Scc s; int c;
        explicit Rig(int channel) : c(channel) {
            s.setScheduler(&sched);
            wr(15, 0); // Lire les broches vivantes ; WR1 seul ne désarme pas les verrous.
            sched.setCallback(c ? Scheduler::SCC_TX_B : Scheduler::SCC_TX_A, [&] { s.onSerialEvent(c, true); });
            wr(4, 4); wr(3, 0xe1); wr(5, 0xea); wr(11, 0x50); wr(12, 0); wr(13, 0); wr(14, 3);
            s.setLoopback(true);
        }
        void wr(uint8_t reg, uint8_t value) { s.write8(0x81+4*c, reg); s.write8(0x81+4*c, value); }
        uint8_t rr(uint8_t reg) { s.write8(0x81+4*c, reg); return s.read8(0x81+4*c); }
        void tx(uint8_t b) { s.write8(0x83+4*c, b); }
        uint8_t pop() { return s.read8(0x83+4*c); }
    };
    for (int c=0; c<2; ++c) {
        Rig r(c); r.tx(0x31); r.tx(0x32); r.sched.runTo(10); r.wr(5, 0xe8);
        check(r.rr(0)&0x20, "CTS follows held RTS after WR5 clear");
        r.sched.runTo(40);
        check((r.rr(0)&0x20) && r.pop()==0x31 && !(r.rr(1)&1), "pending TDR drains with asserted CTS");
        r.sched.runTo(79); check(r.rr(0)&0x20, "RTS held through final stop");
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); r.s.serialize(save);
        r.sched.runTo(80);
        check(!(r.rr(0)&0x20) && (r.rr(1)&1) && r.pop()==0x32, "RTS deasserts after final frame");
        Rig restored(c);
        auto load = StateArchive::loader(state.data(), state.size()); restored.s.serialize(load);
        check(load.ok() && (restored.rr(0)&0x20), "snapshot preserves delayed output latch");
        restored.s.onSerialEvent(c, true);
        check(!(restored.rr(0)&0x20) && restored.pop()==0x32, "restored pending RTS completes");
        r.wr(5, 0xea); r.tx(0x41); r.sched.runTo(90); r.wr(3, 0xc1); r.wr(5, 0xe8);
        check(!(r.rr(0)&0x20), "Auto Enables clear makes RTS immediate");
        r.sched.runTo(120); check(r.pop()==0x41, "active character finishes after immediate CTS drop");
        r.wr(5, 0xea); r.wr(3, 0xe1); r.wr(4, 0); r.tx(0x51); r.wr(5, 0xe8);
        check(!(r.rr(0)&0x20), "synchronous RTS follows WR5 immediately");
        Rig latched(c);
        latched.wr(1, 1); latched.wr(15, 0x20); latched.wr(5, 0xe8); latched.wr(0, 0x10); latched.wr(5, 0xea);
        check(latched.rr(0)&0x20, "External/Status captures asserted CTS");
        latched.tx(0x61); latched.wr(5, 0xe8); latched.sched.runTo(40); latched.pop();
        latched.tx(0x62); latched.sched.runTo(80);
        check(!(latched.rr(0)&4) && !(latched.rr(0)&1), "raw deasserted CTS gates TX despite latched status");
        latched.wr(5, 0xea); latched.sched.runTo(120);
        check(latched.pop()==0x62, "RTS reassertion resumes blocked TDR");
        r.wr(9, c ? 0x40 : 0x80); check(!(r.rr(0)&0x20), "channel reset clears RTS output");
    }
    return failures==0;
}
#ifdef SCC_RTS_STANDALONE
int main() { return testSccRts() ? 0 : 1; }
#endif
