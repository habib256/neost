// UM0109 p.166 : les multiplexeurs d'horloge TX et RX sont indépendants.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct EchoClockRig {
    Scheduler scheduler;
    Scc scc;
    int channel;
    std::vector<uint8_t> output;
    explicit EchoClockRig(int selected) : channel(selected) {
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
    Scheduler::Source receiveSource() const {
        return channel ? Scheduler::SCC_RX_B : Scheduler::SCC_RX_A;
    }
};
}

bool testSccEchoTxClock() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC ECHO TX CLOCK FAIL: %s\n", label);
        }
    };
    const int first[] = {22, 33, 40};
    const int second[] = {44, 66, 80};
    for (int channel = 0; channel < 2; ++channel) {
        for (int rxSource = 0; rxSource < 3; ++rxSource) {
            for (int txSource = 0; txSource < 4; ++txSource) {
                EchoClockRig rig(channel);
                rig.write(11, uint8_t((rxSource << 5) | (((txSource + 1) % 4) << 3)));
                rig.scc.receiveByte(channel, 0x51);
                rig.scc.receiveByte(channel, 0x52);
                rig.scheduler.runTo(5);
                rig.write(11, uint8_t((rxSource << 5) | (txSource << 3)));
                check(rig.scheduler.rawCyclesUntil(rig.receiveSource()) == first[rxSource] - 5,
                      "TX source change preserves engaged RX deadline");

                std::vector<uint8_t> state;
                auto save = StateArchive::saver(state);
                rig.scc.serialize(save); rig.scheduler.serialize(save);
                EchoClockRig restored(channel);
                auto load = StateArchive::loader(state.data(), state.size());
                restored.scc.serialize(load); restored.scheduler.serialize(load);
                check(load.ok(), "snapshot retains echo across independent TX clock change");
                for (EchoClockRig* candidate : {&rig, &restored}) {
                    candidate->scheduler.runTo(first[rxSource] - 1);
                    check(candidate->output.empty() && !candidate->available(),
                          "echo and receive do not precede unchanged RX stop");
                    candidate->scheduler.runTo(first[rxSource]);
                    check(candidate->output.size() == 1 && candidate->output[0] == 0x51
                              && candidate->available() && candidate->pop() == 0x51,
                          "TX-only reconfiguration preserves intact current echo");
                    candidate->scheduler.runTo(second[rxSource]);
                    check(candidate->output.size() == 2 && candidate->output[1] == 0x52
                              && candidate->available() && candidate->pop() == 0x52,
                          "following echo retains independent RX cadence");
                }
            }
        }

        // Un vrai changement RX invalide toujours l'écho engagé, pas le suivant.
        EchoClockRig changed(channel);
        changed.write(11, 0x50);
        changed.scc.receiveByte(channel, 0x61);
        changed.scc.receiveByte(channel, 0x62);
        changed.scheduler.runTo(5);
        changed.write(11, 0x30);
        changed.scheduler.runTo(40);
        check(changed.output.empty() && changed.available() && changed.pop() == 0x61,
              "RX source change invalidates current echo without moving engaged deadline");
        changed.scheduler.runTo(72);
        check(changed.output.empty(), "new RX source does not complete following echo early");
        changed.scheduler.runTo(73);
        check(changed.output.size() == 1 && changed.output[0] == 0x62,
              "following frame uses new RX clock and new echo eligibility");
    }
    std::fprintf(stderr, "[scc-echo-tx-clock] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_ECHO_TX_CLOCK_STANDALONE
int main() { return testSccEchoTxClock() ? 0 : 1; }
#endif
