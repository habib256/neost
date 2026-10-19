// UM0109 p.166/p.172 : WR14 D1/D0 ne pilotent que le BRG, pas RX direct.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct BrgControlRig {
    Scheduler scheduler;
    Scc scc;
    int channel;
    std::vector<uint8_t> output;
    explicit BrgControlRig(int selected) : channel(selected) {
        scc.setScheduler(&scheduler);
        scc.setSerialSink([this](int, uint8_t byte) { output.push_back(byte); });
        scheduler.setCallback(channel ? Scheduler::SCC_RX_B : Scheduler::SCC_RX_A,
                              [this] { scc.onSerialEvent(channel, false); });
        write(15, 0); write(4, 4); write(3, 0xc1); write(5, 0x60);
        write(12, 0); write(13, 0); write(14, 0x0b);
    }
    void write(int reg, uint8_t value) {
        scc.write8(0x81 + 4*channel, uint8_t(reg));
        scc.write8(0x81 + 4*channel, value);
    }
    bool available() { return scc.read8(0x81 + 4*channel) & 1; }
    uint8_t pop() { return scc.read8(0x83 + 4*channel); }
};
}

bool testSccEchoBrgControl() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC ECHO BRG CONTROL FAIL: %s\n", label);
        }
    };
    const int first[] = {22, 33};
    const int second[] = {44, 66};
    for (int channel = 0; channel < 2; ++channel) {
        for (int rxSource = 0; rxSource < 2; ++rxSource) {
            for (int changedBits : {1, 2, 3}) {
                BrgControlRig rig(channel);
                rig.write(11, uint8_t((rxSource << 5) | 0x10));
                rig.scc.receiveByte(channel, 0x51);
                rig.scc.receiveByte(channel, 0x52);
                rig.scheduler.runTo(5);
                rig.write(14, uint8_t(0x0b ^ changedBits));

                std::vector<uint8_t> state;
                auto save = StateArchive::saver(state);
                rig.scc.serialize(save); rig.scheduler.serialize(save);
                BrgControlRig restored(channel);
                auto load = StateArchive::loader(state.data(), state.size());
                restored.scc.serialize(load); restored.scheduler.serialize(load);
                check(load.ok(), "snapshot retains direct RX across BRG control changes");
                for (BrgControlRig* candidate : {&rig, &restored}) {
                    candidate->scheduler.runTo(first[rxSource] - 1);
                    check(candidate->output.empty() && !candidate->available(),
                          "BRG enable or source does not advance direct RX");
                    candidate->scheduler.runTo(first[rxSource]);
                    check(candidate->output.size() == 1 && candidate->output[0] == 0x51
                              && candidate->available() && candidate->pop() == 0x51,
                          "BRG control preserves intact direct-clock echo");
                    // Réactiver le BRG pendant le second caractère RX direct.
                    if (changedBits & 1)
                        candidate->write(14, uint8_t(0x0b ^ (changedBits & 2)));
                    candidate->scheduler.runTo(second[rxSource]);
                    check(candidate->output.size() == 2 && candidate->output[1] == 0x52
                              && candidate->available() && candidate->pop() == 0x52,
                          "following direct-clock echo retains its RX cadence");
                }
            }
        }

        // RX sur BRG : le changement PCLK→RTxC demeure une reconfiguration RX.
        BrgControlRig changed(channel);
        changed.write(11, 0x50);
        changed.scc.receiveByte(channel, 0x61);
        changed.scc.receiveByte(channel, 0x62);
        changed.scheduler.runTo(5);
        changed.write(14, 9);
        changed.scheduler.runTo(40);
        check(changed.output.empty() && changed.available() && changed.pop() == 0x61,
              "BRG source change still invalidates engaged BRG-clock echo");
        changed.scheduler.runTo(127);
        check(changed.output.empty(), "new BRG source does not finish next echo early");
        changed.scheduler.runTo(128);
        check(changed.output.size() == 1 && changed.output[0] == 0x62,
              "following echo uses new BRG source and new eligibility");

        BrgControlRig paused(channel);
        paused.write(11, 0x50);
        paused.scc.receiveByte(channel, 0x81);
        paused.scc.receiveByte(channel, 0x82);
        paused.scheduler.runTo(5);
        paused.write(14, 0x0a);
        paused.scheduler.runTo(100);
        check(paused.output.empty() && !paused.available(), "disabled BRG does not complete RX");
        std::vector<uint8_t> pausedState;
        auto pausedSave = StateArchive::saver(pausedState);
        paused.scc.serialize(pausedSave); paused.scheduler.serialize(pausedSave);
        BrgControlRig resumed(channel);
        auto pausedLoad = StateArchive::loader(pausedState.data(), pausedState.size());
        resumed.scc.serialize(pausedLoad); resumed.scheduler.serialize(pausedLoad);
        check(pausedLoad.ok(), "snapshot accepts interrupted BRG-clock echo");
        for (BrgControlRig* candidate : {&paused, &resumed}) {
            candidate->write(14, 0x0b);
            // Reprise au caractère du transport existant ; pas de phase bit
            // physique déduite du passage sans horloge.
            candidate->scheduler.runTo(140);
            check(candidate->output.empty(), "BRG restart cannot restore interrupted echo integrity");
            candidate->scheduler.runTo(180);
            check(candidate->output.size() == 1 && candidate->output[0] == 0x82,
                  "fresh queued frame becomes echo eligible after BRG restart");
        }

        BrgControlRig mode(channel);
        mode.write(11, 0);
        mode.scc.receiveByte(channel, 0x71);
        mode.scheduler.runTo(5);
        mode.write(14, 3); mode.write(14, 0x0b);
        mode.scheduler.runTo(22);
        check(mode.output.empty() && mode.available() && mode.pop() == 0x71,
              "Auto Echo mode changes still invalidate engaged echo on direct clock");
        BrgControlRig local(channel);
        local.write(11, 0);
        local.scc.receiveByte(channel, 0x72);
        local.scheduler.runTo(5);
        local.write(14, 0x1b); local.write(14, 0x0b);
        local.scheduler.runTo(22);
        check(local.output.empty() && local.available() && local.pop() == 0x72,
              "local loopback control changes keep the conservative echo invalidation");
    }
    std::fprintf(stderr, "[scc-echo-brg-control] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_ECHO_BRG_CONTROL_STANDALONE
int main() { return testSccEchoBrgControl() ? 0 : 1; }
#endif
