// UM0109 p.146 : First/Special et Special Only conservent l'erreur verrouillée.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct ErrorModeRig {
    Scc scc;
    int channel;
    explicit ErrorModeRig(int selected) : channel(selected) {
        write(15, 0); write(14, 0); write(4, 4); write(3, 0xc1); write(9, 8);
    }
    void write(int reg, uint8_t value) {
        scc.write8(0x81 + 4*channel, uint8_t(reg));
        scc.write8(0x81 + 4*channel, value);
    }
    uint8_t read(int reg) {
        scc.write8(0x81 + 4*channel, uint8_t(reg));
        return scc.read8(0x81 + 4*channel);
    }
    uint8_t pending() { scc.write8(0x81, 3); return scc.read8(0x81); }
    uint8_t pop() { return scc.read8(0x83 + 4*channel); }
    void resetError() { scc.write8(0x81 + 4*channel, 0x30); }
    void lockError(int mode) {
        write(1, uint8_t(mode << 3));
        for (int byte = 1; byte <= 4; ++byte) scc.receiveByte(channel, uint8_t(byte));
        pop(); pop(); pop();
    }
};
}

bool testSccRxErrorMode() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC RX ERROR MODE FAIL: %s\n", label);
        }
    };
    for (int channel = 0; channel < 2; ++channel) {
        for (int initial : {1, 3}) {
            ErrorModeRig rig(channel);
            const uint8_t rxPending = channel ? 4 : 0x20;
            rig.lockError(initial);
            check(rig.scc.irqActive() && rig.pending() == rxPending,
                  "erroneous read locks FIFO and asserts receive IP");
            rig.write(1, uint8_t((4-initial) << 3));
            check(rig.scc.irqActive() && rig.pending() == rxPending,
                  "transition between locking modes retains special receive IP");
            check((rig.read(0) & 1) && (rig.read(1) & 0x20),
                  "mode change preserves locked data and overrun status");

            std::vector<uint8_t> state;
            auto save = StateArchive::saver(state);
            rig.scc.serialize(save);
            ErrorModeRig restored(channel);
            auto load = StateArchive::loader(state.data(), state.size());
            restored.scc.serialize(load);
            check(load.ok(), "snapshot accepts locked error after mode change");
            for (ErrorModeRig* candidate : {&rig, &restored}) {
                check(candidate->scc.irqActive() && candidate->pending() == rxPending,
                      "snapshot retains special IP without rereading data");
                check(candidate->pop() == 4 && candidate->scc.irqActive(),
                      "locked error remains readable after transition");
                candidate->resetError();
                check(!candidate->scc.irqActive() && candidate->pending() == 0
                          && !(candidate->read(0) & 1) && !(candidate->read(1) & 0x20),
                      "Error Reset discards locked error and clears IP and status");
            }
        }

        // Sans lecture spéciale préalable, entrer en Special Only ne crée pas
        // de verrou ni d'IRQ avant la lecture du caractère en erreur.
        ErrorModeRig unread(channel);
        unread.write(1, 8);
        for (int byte = 1; byte <= 4; ++byte) unread.scc.receiveByte(channel, uint8_t(byte));
        unread.pop(); unread.pop();
        unread.write(1, 0x18);
        check(!unread.scc.irqActive(), "unread error does not acquire a lock through mode selection");
        check(unread.pop() == 4 && unread.scc.irqActive(), "special-only erroneous read still creates lock");

        // Les autres politiques WR1 sont conservées : 00 masque les demandes,
        // 10 permet de retirer le caractère sans verrou (p.47–48).
        for (int next : {0, 2}) {
            ErrorModeRig changed(channel);
            changed.lockError(1);
            changed.write(1, uint8_t(next << 3));
            check(changed.scc.irqActive() == (next == 2), "disabled/all mode keeps its own interrupt policy");
            check(changed.pop() == 4 && !(changed.read(0) & 1), "disabled/all mode can remove prior locked data");
            check(!changed.scc.irqActive() && changed.pending() == 0,
                  "empty FIFO does not regain a special interrupt in disabled/all mode");
        }
    }
    std::fprintf(stderr, "[scc-rx-error-mode] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_RX_ERROR_MODE_STANDALONE
int main() { return testSccRxErrorMode() ? 0 : 1; }
#endif
