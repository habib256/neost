// UM0109 p.32–33/p.157–158 : lecture étendue du Z85C30 via WR7' D6.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct ExtendedRig {
    Scc scc;
    void write(int channel, int reg, uint8_t value) {
        scc.write8(0x81 + 4*channel, uint8_t(reg));
        scc.write8(0x81 + 4*channel, value);
    }
    uint8_t read(int channel, int reg) {
        scc.write8(0x81 + 4*channel, uint8_t(reg));
        return scc.read8(0x81 + 4*channel);
    }
    void extended(int channel, uint8_t value) {
        write(channel, 15, 1);
        write(channel, 7, value);
    }
    ExtendedRig() {
        for (int channel = 0; channel < 2; ++channel) {
            write(channel, 15, 0); write(channel, 14, 0);
            write(channel, 4, 0x44); write(channel, 3, 0xc1);
            write(channel, 5, 0xe8); write(channel, 10, 0x60);
            write(channel, 13, 0x25);
        }
    }
};
}

bool testSccExtendedRead() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC EXTENDED READ FAIL: %s\n", label);
        }
    };
    auto legacy = [&](ExtendedRig& rig, int channel) {
        check(rig.read(channel, 4) == rig.read(channel, 0), "disabled RR4 mirrors RR0");
        check(rig.read(channel, 5) == rig.read(channel, 1), "disabled RR5 mirrors RR1");
        check(rig.read(channel, 9) == rig.read(channel, 13), "disabled RR9 mirrors RR13");
        check(rig.read(channel, 11) == rig.read(channel, 15), "disabled RR11 mirrors RR15");
        check(rig.read(channel, 14) == rig.read(channel, 10), "disabled RR14 mirrors RR10");
    };
    auto extended = [&](ExtendedRig& rig, int channel) {
        check(rig.read(channel, 4) == 0x44, "extended RR4 reads WR4");
        check(rig.read(channel, 5) == 0xe8, "extended RR5 reads WR5");
        check(rig.read(channel, 9) == 0xc1, "extended RR9 reads WR3");
        check(rig.read(channel, 11) == 0x60, "extended RR11 reads WR10");
        check(rig.read(channel, 14) == 0x60, "extended RR14 reads WR7 prime");
    };

    for (int channel = 0; channel < 2; ++channel) {
        ExtendedRig rig;
        legacy(rig, channel);
        rig.extended(channel, 0x60);
        extended(rig, channel);
        legacy(rig, 1-channel);
        // WR15 D0 ne fait que choisir la destination des écritures WR7.
        // D2, réservé à la FIFO de statut, ne commande pas la lecture étendue.
        for (int wr15 : {0, 1, 4, 5}) {
            rig.write(channel, 15, uint8_t(wr15));
            extended(rig, channel);
        }
        rig.write(channel, 15, 0);
        rig.write(channel, 7, 0x19);
        check(rig.read(channel, 14) == 0x60, "ordinary WR7 write preserves extended control");
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        rig.scc.serialize(save);
        ExtendedRig restored;
        auto load = StateArchive::loader(state.data(), state.size());
        restored.scc.serialize(load);
        check(load.ok(), "snapshot retains extended read control");
        extended(restored, channel);
        rig.extended(channel, 0x20);
        legacy(rig, channel);
        restored.write(0, 9, channel ? 0x40 : 0x80);
        legacy(restored, channel);
    }

    // Lire une image WR ne doit ni consommer la FIFO ni acquitter une IRQ RX.
    {
        ExtendedRig rig;
        rig.write(0, 1, 0x10); rig.write(0, 9, 8);
        rig.scc.receiveByte(0, 0x51);
        rig.extended(0, 0x60);
        extended(rig, 0);
        check(rig.scc.irqActive() && (rig.read(0, 0) & 1),
              "extended reads preserve pending receive and FIFO contents");
        check(rig.scc.read8(0x83) == 0x51 && !rig.scc.irqActive(),
              "normal data read still releases received character");
        rig.write(0, 9, 0xc0);
        legacy(rig, 0); legacy(rig, 1);
    }
    std::fprintf(stderr, "[scc-extended-read] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_EXTENDED_READ_STANDALONE
int main() { return testSccExtendedRead() ? 0 : 1; }
#endif
