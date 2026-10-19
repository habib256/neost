// UM0109 p.47/p.142 : retirer le premier caractère via Error Reset efface son IP.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct FirstResetRig {
    Scc scc;
    int channel;
    explicit FirstResetRig(int selected) : channel(selected) {
        write(15, 0); write(14, 0); write(4, 4); write(3, 0xc1); write(9, 8);
    }
    void command(uint8_t value) { scc.write8(0x81 + 4*channel, value); }
    void write(int reg, uint8_t value) { command(uint8_t(reg)); command(value); }
    uint8_t read(int reg) { command(uint8_t(reg)); return scc.read8(0x81 + 4*channel); }
    uint8_t pop() { return scc.read8(0x83 + 4*channel); }
};
}

bool testSccFirstErrorReset() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC FIRST ERROR RESET FAIL: %s\n", label);
        }
    };
    for (int channel = 0; channel < 2; ++channel) {
        for (bool selectMode : {false, true}) {
            for (bool storedFollower : {false, true}) {
                FirstResetRig rig(channel);
                rig.write(1, selectMode ? 0x10 : 8);
                for (int byte = 1; byte <= 4; ++byte) rig.scc.receiveByte(channel, uint8_t(byte));
                check(rig.pop() == 1 && rig.pop() == 2, "overrun character reaches FIFO exit");
                if (selectMode) rig.write(1, 8);
                else rig.command(0x20);
                check(rig.scc.irqActive() && (rig.read(1) & 0x20),
                      "stored erroneous first character requests interrupt");
                if (storedFollower) rig.scc.receiveByte(channel, 0x51);
                rig.command(0x30); // Error Reset avant toute lecture du slot erroné.
                check(!rig.scc.irqActive(), "discarding first character clears its receive request");
                check(!(rig.read(1) & 0x20), "Error Reset clears overrun status");

                std::vector<uint8_t> state;
                auto save = StateArchive::saver(state);
                rig.scc.serialize(save);
                FirstResetRig restored(channel);
                auto load = StateArchive::loader(state.data(), state.size());
                restored.scc.serialize(load);
                check(load.ok(), "snapshot retains consumed first-character request");
                for (FirstResetRig* candidate : {&rig, &restored}) {
                    if (!storedFollower) candidate->scc.receiveByte(channel, 0x51);
                    check(!candidate->scc.irqActive() && (candidate->read(0) & 1),
                          "clean following character is available without inheriting first IP");
                    candidate->command(0x20);
                    check(candidate->scc.irqActive(), "explicit Enable Next rearms stored follower");
                    check(candidate->pop() == 0x51 && !candidate->scc.irqActive(),
                          "rearmed follower is intact and its read clears first IP");
                    candidate->scc.receiveByte(channel, 0x52);
                    check(!candidate->scc.irqActive() && candidate->pop() == 0x52,
                          "later clean character still needs explicit first interrupt rearm");
                }
            }
        }

        // Error Reset sans erreur ne retire pas le premier caractère propre.
        FirstResetRig clean(channel);
        clean.write(1, 8); clean.scc.receiveByte(channel, 0x61);
        clean.command(0x30);
        check(clean.scc.irqActive() && clean.pop() == 0x61,
              "Error Reset without discard preserves clean first-character interrupt");
    }
    std::fprintf(stderr, "[scc-first-error-reset] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_FIRST_ERROR_RESET_STANDALONE
int main() { return testSccFirstErrorReset() ? 0 : 1; }
#endif
