// UM0109 WR9 p.160 : un reset canal efface ses IP/IUS, sans toucher l'autre.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct ResetRig {
    Scc scc;

    void control(int channel, uint8_t value) { scc.write8(0x81 + 4*channel, value); }
    void write(int channel, int reg, uint8_t value) {
        control(channel, uint8_t(reg));
        control(channel, value);
    }
    uint8_t read(int channel, int reg) {
        control(channel, uint8_t(reg));
        return scc.read8(0x81 + 4*channel);
    }
    void configure(int channel) {
        write(channel, 15, 0);
        write(channel, 14, 0); // RxD externe, sans bouclage local du reset matériel.
        write(channel, 3, 0xc1);
        write(channel, 1, 0x12); // RX chaque caractère et TX buffer empty.
    }
    void resetChannel(int channel) { write(0, 9, uint8_t((channel ? 0x40 : 0x80) | 8)); }
    uint8_t pending() { return read(0, 3); }
    uint8_t pop(int channel) { return scc.read8(0x83 + 4*channel); }

    ResetRig() {
        configure(0);
        configure(1);
        write(0, 2, 0xa0);
        write(0, 9, 8);
    }
};
}

bool testSccChannelReset() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC CHANNEL RESET FAIL: %s\n", label);
        }
    };

    // Chaque sens : effacer RX/TX du canal visé et conserver ceux du voisin.
    for (int target = 0; target < 2; ++target) {
        ResetRig rig;
        for (int channel = 0; channel < 2; ++channel) {
            rig.scc.receiveByte(channel, uint8_t(0x41 + channel));
            rig.scc.write8(0x83 + 4*channel, 0x55);
        }
        check(rig.pending() == 0x36, "both channels have RX and TX pending");
        rig.resetChannel(target);
        check(rig.pending() == (target ? 0x30 : 6), "channel reset preserves other channel IPs");
        check(rig.scc.irqActive(), "other channel IRQ remains asserted with MIE retained");
        check(rig.pop(1-target) == 0x42-target, "other channel FIFO survives reset");
        check(rig.read(0, 2) == 0xa0, "shared interrupt vector survives channel reset");
        rig.control(1-target, 0x28);
        check(!rig.scc.irqActive(), "surviving TX interrupt can be reset normally");
    }

    // Un IUS A bloque B : le reset A doit rendre B à nouveau éligible.
    {
        ResetRig rig;
        rig.scc.receiveByte(0, 0x41);
        check(rig.scc.processIack() == 0xa0, "IACK uses preserved shared vector");
        rig.pop(0);
        rig.scc.receiveByte(1, 0x42);
        check(!rig.scc.irqActive() && rig.pending() == 4, "A IUS blocks pending B receive");

        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        rig.scc.serialize(save);
        ResetRig restored;
        auto load = StateArchive::loader(state.data(), state.size());
        restored.scc.serialize(load);
        check(load.ok() && !restored.scc.irqActive(), "snapshot retains blocking IUS");
        for (ResetRig* candidate : {&rig, &restored}) {
            candidate->resetChannel(0);
            check(candidate->scc.irqActive() && candidate->pending() == 4,
                  "reset clears own IUS and releases other channel IRQ");
        }
    }

    // Le reset A ne doit pas retirer l'IUS B, même quand une source B attend.
    {
        ResetRig rig;
        rig.scc.receiveByte(1, 0x42);
        rig.scc.processIack();
        rig.pop(1);
        rig.scc.write8(0x87, 0x55);
        rig.resetChannel(0);
        check(rig.pending() == 2 && !rig.scc.irqActive(), "reset preserves other channel IUS");
        rig.control(1, 0x38);
        check(rig.scc.irqActive(), "normal reset highest IUS releases preserved TX pending");
    }

    // Reset B retire aussi son IUS : une nouvelle réception B doit interrompre.
    {
        ResetRig rig;
        rig.scc.receiveByte(1, 0x42);
        rig.scc.processIack();
        rig.resetChannel(1);
        rig.configure(1);
        rig.scc.receiveByte(1, 0x43);
        check(rig.scc.irqActive(), "B reset clears B IUS before subsequent reception");
        rig.write(0, 9, 0xc8);
        check(rig.pending() == 0 && !rig.scc.irqActive(), "forced hardware reset clears both IP and IUS");
        check(rig.read(0, 2) == 0xa0, "forced hardware reset preserves interrupt vector");
        rig.configure(0);
        rig.scc.receiveByte(0, 0x44);
        check(rig.scc.irqActive(), "forced hardware reset keeps explicitly programmed MIE");
    }

    std::fprintf(stderr, "[scc-channel-reset] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_CHANNEL_RESET_STANDALONE
int main() { return testSccChannelReset() ? 0 : 1; }
#endif
