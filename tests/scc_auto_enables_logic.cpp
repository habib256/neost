// Zilog UM010904-0318 : WR3 p.149, bouclages p.69-70, DCD p.106.
#include "io/Scc.hpp"
#include <cstdio>

bool testSccAutoEnables() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { ++failures; std::fprintf(stderr, "SCC AUTO ENABLES FAIL: %s\n", name); }
    };
    struct Rig {
        Scheduler sched; Scc s; int c;
        explicit Rig(int channel) : c(channel) {
            s.setScheduler(&sched);
            wr(15, 0); // Les tests de broches brutes n'activent pas les verrous RR0.
            sched.setCallback(c ? Scheduler::SCC_TX_B : Scheduler::SCC_TX_A, [&] { s.onSerialEvent(c, true); });
            sched.setCallback(c ? Scheduler::SCC_RX_B : Scheduler::SCC_RX_A, [&] { s.onSerialEvent(c, false); });
            wr(4, 4); wr(3, 0xe1); wr(5, 0x68); wr(11, 0x50); wr(12, 0); wr(13, 0); wr(14, 3);
            s.setLoopback(true);
        }
        void wr(uint8_t reg, uint8_t value) { s.write8(0x81 + 4*c, reg); s.write8(0x81 + 4*c, value); }
        uint8_t rr(uint8_t reg) { s.write8(0x81 + 4*c, reg); return s.read8(0x81 + 4*c); }
        void tx(uint8_t byte) { s.write8(0x83 + 4*c, byte); }
        uint8_t pop() { return s.read8(0x83 + 4*c); }
    };
    for (int c = 0; c < 2; ++c) {
        {
            Rig r(c); r.s.receiveByte(c, 0x11); r.tx(0x22); r.sched.runTo(80);
            check(!(r.rr(0)&1), "DCD blocks RX and CTS blocks TX");
            check(!(r.rr(0)&4), "CTS holds byte in TDR");
            r.wr(5, 0xea); r.sched.runTo(119);
            check(!(r.rr(0)&1), "CTS assertion starts a full character period");
            r.sched.runTo(120); check((r.rr(0)&1) && r.pop()==0x22, "CTS resumes held TDR");
        }
        {
            Rig r(c); r.wr(5, 0xea); r.tx(0x31); r.tx(0x32);
            r.sched.runTo(10);
            // Sans Auto Enables, RTS tombe immédiatement ; réactiver ensuite
            // la qualification CTS isole la chute de broche du RTS différé.
            r.wr(3, 0xc1); r.wr(5, 0xe8); r.wr(3, 0xe1);
            r.sched.runTo(40);
            check((r.rr(0)&1) && r.pop()==0x31, "CTS drop completes active character");
            r.sched.runTo(80); check(!(r.rr(0)&1), "CTS drop holds next character");
            r.wr(5, 0xea); r.sched.runTo(120);
            check((r.rr(0)&1) && r.pop()==0x32, "next character resumes after CTS");
        }
        {
            Rig r(c); r.wr(5, 0xea); r.s.receiveByte(c, 0x41); r.sched.runTo(40);
            r.s.receiveByte(c, 0x42); r.sched.runTo(50); r.wr(5, 0x6a);
            r.wr(5, 0xea); r.sched.runTo(100);
            check((r.rr(0)&1) && r.pop()==0x41 && !(r.rr(0)&1), "DCD drop loses active RX but preserves FIFO");
        }
        {
            Rig r(c); r.wr(3, 0xc1); r.s.receiveByte(c, 0x51); r.tx(0x52); r.sched.runTo(40);
            check(r.pop()==0x52 && r.pop()==0x51, "Auto Enables clear ignores inactive pins");
        }
        {
            Rig r(c); r.wr(14, 0x13); r.tx(0x61); r.sched.runTo(40);
            check((r.rr(0)&1) && r.pop()==0x61, "local loopback ignores both pins");
            r.wr(3, 0xe0); r.tx(0x62); r.sched.runTo(80);
            check(!(r.rr(0)&1), "local loopback still requires software RX enable");
        }
        {
            Rig r(c); r.wr(1, 1); r.wr(15, 0x28); r.wr(5, 0xea);
            r.wr(5, 0x68); // RR0 reste verrouillé sur les broches assertées.
            check((r.rr(0)&0x28)==0x28, "External/Status latches asserted pins");
            r.tx(0x71); r.s.receiveByte(c, 0x72); r.sched.runTo(80);
            check(!(r.rr(0)&1) && !(r.rr(0)&4), "qualification uses raw pins despite RR0 latch");
            r.s.setLoopback(false); r.sched.runTo(120);
            check(r.rr(0)&4, "disconnect restores idle asserted pins and resumes TX");
        }
        {
            Rig r(c); r.wr(5, 0xea); r.s.receiveByte(c, 0x81); r.s.receiveByte(c, 0x82);
            r.sched.runTo(10); r.wr(5, 0x6a); r.wr(5, 0xea); r.sched.runTo(50);
            check((r.rr(0)&1) && r.pop()==0x82 && !(r.rr(0)&1), "DCD loses current frame while future host bytes remain queued");
        }
        {
            Rig r(c); r.wr(14, 0x0b); r.tx(0x91); r.sched.runTo(40);
            check(r.rr(0)&4, "Auto Echo ignores CTS");
            r.s.receiveByte(c, 0x92); r.sched.runTo(80);
            check(!(r.rr(0)&1), "Auto Echo still qualifies receiver with DCD");
        }
    }
    return failures == 0;
}
#ifdef SCC_AUTO_ENABLES_STANDALONE
int main() { return testSccAutoEnables() ? 0 : 1; }
#endif
