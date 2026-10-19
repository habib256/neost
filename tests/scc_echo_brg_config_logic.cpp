// UM0109 p.166–168 : TC du BRG ne modifie pas les horloges RX directes.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct BrgEchoRig {
    Scheduler scheduler;
    Scc scc;
    int channel;
    std::vector<uint8_t> output;
    explicit BrgEchoRig(int selected) : channel(selected) {
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

bool testSccEchoBrgConfig() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC ECHO BRG CONFIG FAIL: %s\n", label);
        }
    };
    const int first[] = {22, 33};
    const int second[] = {44, 66};
    for (int channel = 0; channel < 2; ++channel) {
        for (int rxSource = 0; rxSource < 2; ++rxSource) {
            for (int reg : {12, 13}) {
                BrgEchoRig rig(channel);
                rig.write(11, uint8_t((rxSource << 5) | 0x10));
                rig.scc.receiveByte(channel, 0x51);
                rig.scc.receiveByte(channel, 0x52);
                rig.scheduler.runTo(5);
                rig.write(reg, reg == 12 ? 7 : 1);

                std::vector<uint8_t> state;
                auto save = StateArchive::saver(state);
                rig.scc.serialize(save); rig.scheduler.serialize(save);
                BrgEchoRig restored(channel);
                auto load = StateArchive::loader(state.data(), state.size());
                restored.scc.serialize(load); restored.scheduler.serialize(load);
                check(load.ok(), "snapshot retains direct RX and reconfigured BRG");
                for (BrgEchoRig* candidate : {&rig, &restored}) {
                    candidate->scheduler.runTo(first[rxSource] - 1);
                    check(candidate->output.empty() && !candidate->available(),
                          "TC write does not advance direct RX completion");
                    candidate->scheduler.runTo(first[rxSource]);
                    check(candidate->output.size() == 1 && candidate->output[0] == 0x51
                              && candidate->available() && candidate->pop() == 0x51,
                          "TC write preserves engaged direct-clock echo");
                    candidate->scheduler.runTo(second[rxSource]);
                    check(candidate->output.size() == 2 && candidate->output[1] == 0x52
                              && candidate->available() && candidate->pop() == 0x52,
                          "following direct-clock echo remains independent of BRG");
                }
            }
        }

        // Quand RX utilise le BRG, le même changement TC conserve le garde
        // d'intégrité du caractère engagé et s'applique au caractère suivant.
        for (int reg : {12, 13}) {
            BrgEchoRig rig(channel);
            rig.write(11, 0x50);
            rig.scc.receiveByte(channel, 0x61);
            rig.scc.receiveByte(channel, 0x62);
            rig.scheduler.runTo(5);
            rig.write(reg, reg == 12 ? 7 : 1);
            rig.scheduler.runTo(39);
            check(!rig.available(), "BRG TC change retains engaged RX deadline");
            rig.scheduler.runTo(40);
            check(rig.output.empty() && rig.available() && rig.pop() == 0x61,
                  "BRG-clock reconfiguration still invalidates engaged echo");
            const int next = reg == 12 ? 220 : 5200;
            rig.scheduler.runTo(next - 1);
            check(rig.output.empty(), "new BRG TC does not complete next frame early");
            rig.scheduler.runTo(next);
            check(rig.output.size() == 1 && rig.output[0] == 0x62,
                  "following BRG echo uses reconfigured time constant");
        }
    }
    std::fprintf(stderr, "[scc-echo-brg-config] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_ECHO_BRG_CONFIG_STANDALONE
int main() { return testSccEchoBrgConfig() ? 0 : 1; }
#endif
