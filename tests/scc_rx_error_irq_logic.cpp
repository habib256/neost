// UM0109 p.47–48/p.179 : RR1 collant ne crée pas un RX All sans caractère.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct ErrorIrqRig {
    Scc scc;
    int channel;
    explicit ErrorIrqRig(int selected) : channel(selected) {
        write(15, 0); write(14, 0); write(4, 4); write(3, 0xc1);
        write(1, 0x10); write(2, 0xa0); write(9, 9);
    }
    void command(uint8_t value) { scc.write8(0x81 + 4*channel, value); }
    void write(int reg, uint8_t value) {
        command(uint8_t(reg)); command(value);
    }
    uint8_t read(int reg) {
        command(uint8_t(reg));
        return scc.read8(0x81 + 4*channel);
    }
    uint8_t pending() { scc.write8(0x81, 3); return scc.read8(0x81); }
    uint8_t vector() { scc.write8(0x85, 2); return scc.read8(0x85); }
    uint8_t pop() { return scc.read8(0x83 + 4*channel); }
    void overflow() { for (int byte = 1; byte <= 4; ++byte) scc.receiveByte(channel, uint8_t(byte)); }
};
}

bool testSccRxErrorIrq() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC RX ERROR IRQ FAIL: %s\n", label);
        }
    };
    for (int channel = 0; channel < 2; ++channel) {
        ErrorIrqRig rig(channel);
        const uint8_t rxPending = channel ? 4 : 0x20;
        const uint8_t specialVector = channel ? 0xa6 : 0xae;
        const uint8_t ordinaryVector = channel ? 0xa4 : 0xac;
        rig.overflow();
        check(rig.scc.irqActive() && rig.pending() == rxPending, "full FIFO requests RX interrupt");
        check(rig.pop() == 1 && rig.pop() == 2, "overrun preserves oldest two characters");
        check((rig.read(1) & 0x20) && rig.vector() == specialVector,
              "overrun reaches exit with special vector");
        check(rig.pop() == 4 && !(rig.read(0) & 1), "all-character mode drains erroneous last character");
        check((rig.read(1) & 0x20) != 0, "overrun status remains sticky after FIFO drains");
        check(rig.pending() == 0 && !rig.scc.irqActive(), "empty FIFO clears RX IP and IRQ despite sticky RR1");
        rig.write(1, 0); rig.write(1, 0x10);
        check(rig.pending() == 0 && !rig.scc.irqActive(), "reenabling all mode does not invent empty receive IRQ");

        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        rig.scc.serialize(save);
        ErrorIrqRig restored(channel);
        auto load = StateArchive::loader(state.data(), state.size());
        restored.scc.serialize(load);
        check(load.ok(), "snapshot retains sticky error with empty FIFO");
        for (ErrorIrqRig* candidate : {&rig, &restored}) {
            check(!candidate->scc.irqActive() && candidate->pending() == 0,
                  "restored empty FIFO has no special receive request");
            candidate->scc.receiveByte(channel, 0x51);
            check(candidate->scc.irqActive() && candidate->vector() == specialVector,
                  "next received character still uses sticky special condition vector");
            check(candidate->pop() == 0x51 && !candidate->scc.irqActive(),
                  "next all-mode data read clears RX request without error reset");
            candidate->command(0x30);
            check(!(candidate->read(1) & 0x20), "Error Reset clears sticky overrun status");
            candidate->scc.receiveByte(channel, 0x52);
            check(candidate->vector() == ordinaryVector && candidate->pop() == 0x52,
                  "Error Reset restores ordinary receive vector");
        }

        // Les modes premier/spécial gardent au contraire le caractère en erreur
        // à la sortie de FIFO et son IP jusqu'à Error Reset.
        for (int mode : {1, 3}) {
            ErrorIrqRig locked(channel);
            locked.write(1, uint8_t(mode << 3));
            locked.overflow(); locked.pop(); locked.pop();
            check(locked.pop() == 4 && locked.scc.irqActive(), "first/special read locks error and requests IRQ");
            check(locked.pop() == 4 && (locked.read(0) & 1), "locked character remains readable");
            locked.command(0x30);
            check(!locked.scc.irqActive() && locked.pending() == 0 && !(locked.read(0) & 1),
                  "Error Reset releases first/special lock and receive request");
        }
    }
    std::fprintf(stderr, "[scc-rx-error-irq] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_RX_ERROR_IRQ_STANDALONE
int main() { return testSccRxErrorIrq() ? 0 : 1; }
#endif
