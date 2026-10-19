// Réception asynchrone : Zilog UM010904-0318, WR3 p.148.
#include "io/Scc.hpp"
#include <cstdio>

bool testSccAsync() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) { ++failures; std::fprintf(stderr, "SCC ASYNC FAIL: %s\n", label); }
    };
    struct Rig {
        Scc s;
        Rig() { wr(0,14,0); wr(1,14,0); } // Les données hôtes arrivent par RxD externe.
        void wr(int channel, uint8_t reg, uint8_t value) {
            const uint32_t addr = 0x81 + 4 * channel;
            s.write8(addr, reg); s.write8(addr, value);
        }
        uint8_t pop(int channel) { return s.read8(0x83 + 4 * channel); }
    };
    // Les codes WR3 sont dans l'ordre 5,7,6,8 bits, pas dans l'ordre naturel.
    constexpr uint8_t unused[] = {0xe0, 0x80, 0xc0, 0};
    for (int channel = 0; channel < 2; ++channel) {
        for (int format = 0; format < 4; ++format) {
            for (uint8_t stop : {uint8_t(4), uint8_t(8), uint8_t(12)}) {
                Rig r;
                r.wr(channel, 4, stop);
                r.wr(channel, 3, uint8_t((format << 6) | 1));
                for (uint8_t byte : {uint8_t(0), uint8_t(0x55), uint8_t(0xff)}) {
                    r.s.receiveByte(channel, byte);
                    check(r.pop(channel) == uint8_t(byte | unused[format]),
                          "async RR8 fills unused bits with ones on either channel");
                }
            }
            Rig r;
            r.wr(channel, 4, 0); r.wr(channel, 3, uint8_t((format << 6) | 1));
            r.s.receiveByte(channel, 0x15);
            check(r.pop(channel) == 0x15, "synchronous receive preserves eight-bit section");
        }
    }
    {
        Rig r;
        r.wr(0, 4, 4); r.wr(0, 3, 1);
        r.s.receiveByte(0, 0x15);
        r.wr(0, 3, 0xc1); r.s.receiveByte(0, 0x15);
        check(r.pop(0) == 0xf5 && r.pop(0) == 0x15,
              "FIFO retains each character format across WR3 changes");
    }
    {
        Rig r; Scheduler scheduler;
        r.s.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::SCC_RX_A, [&] { r.s.onSerialEvent(0, false); });
        r.wr(0, 4, 4); r.wr(0, 3, 1); r.wr(0, 11, 0x40);
        r.wr(0, 12, 0); r.wr(0, 13, 0); r.wr(0, 14, 3);
        r.s.receiveByte(0, 0x15); scheduler.runTo(28);
        check(r.pop(0) == 0xf5, "scheduled five-bit reception applies unused-bit fill");
    }
    {
        Rig r;
        r.wr(0, 4, 4); r.wr(0, 3, 1); r.wr(0, 5, 0x68); r.wr(0, 14, 0x10);
        r.s.write8(0x83, 0x15);
        check(r.pop(0) == 0xf5, "local loopback applies receiver format independently of TX");
    }
    return failures == 0;
}
#ifdef SCC_ASYNC_STANDALONE
int main() { return testSccAsync() ? 0 : 1; }
#endif
