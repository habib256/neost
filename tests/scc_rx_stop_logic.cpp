// UM0109 p.93/p.151 : WR4 choisit les stops TX ; RX vérifie toujours un stop.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct StopRig {
    Scheduler scheduler;
    Scc scc;
    int channel;
    std::vector<uint8_t> output;
    explicit StopRig(int selected) : channel(selected) {
        scc.setScheduler(&scheduler);
        scc.setSerialSink([this](int, uint8_t byte) { output.push_back(byte); });
        scheduler.setCallback(channel ? Scheduler::SCC_RX_B : Scheduler::SCC_RX_A,
                              [this] { scc.onSerialEvent(channel, false); });
        scheduler.setCallback(channel ? Scheduler::SCC_TX_B : Scheduler::SCC_TX_A,
                              [this] { scc.onSerialEvent(channel, true); });
        write(15, 0); write(3, 0xc1); write(5, 0x68);
        write(11, 0x50); write(12, 0); write(13, 0); write(14, 3);
    }
    void write(int reg, uint8_t value) {
        scc.write8(0x81 + 4*channel, uint8_t(reg));
        scc.write8(0x81 + 4*channel, value);
    }
    uint8_t status() { return scc.read8(0x81 + 4*channel); }
    uint8_t pop() { return scc.read8(0x83 + 4*channel); }
    void transmit(uint8_t byte) { scc.write8(0x83 + 4*channel, byte); }
};
}

bool testSccRxStop() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC RX STOP FAIL: %s\n", label);
        }
    };
    for (int channel = 0; channel < 2; ++channel) {
        for (int stops = 1; stops <= 3; ++stops) {
            StopRig rig(channel);
            rig.write(4, uint8_t(0x40 | (stops << 2))); // /16, 8N1/1.5/2.
            rig.write(1, 0x10); rig.write(9, 8);
            rig.transmit(0x55);
            rig.scc.receiveByte(channel, 0x61);
            rig.scc.receiveByte(channel, 0x62);
            // BRG PCLK TC0 : 4 cycles par bit, puis /16.
            rig.scheduler.runTo(639);
            check(!(rig.status() & 1), "receiver does not finish before one stop bit");
            rig.scheduler.runTo(640);
            check((rig.status() & 1) && rig.scc.irqActive() && rig.pop() == 0x61,
                  "receiver finishes after one stop independent of TX selection");
            const int txDeadline = stops == 1 ? 640 : (stops == 2 ? 672 : 704);
            if (txDeadline > 640) {
                rig.scheduler.runTo(txDeadline - 1);
                check(rig.output.empty(), "transmitter retains programmed extra stop duration");
            }
            rig.scheduler.runTo(txDeadline);
            check(rig.output.size() == 1, "transmitter completes at programmed stop deadline");

            std::vector<uint8_t> state;
            auto save = StateArchive::saver(state);
            rig.scc.serialize(save); rig.scheduler.serialize(save);
            StopRig restored(channel);
            auto load = StateArchive::loader(state.data(), state.size());
            restored.scc.serialize(load); restored.scheduler.serialize(load);
            restored.scheduler.runTo(1279);
            check(!(restored.status() & 1), "snapshot retains next one-stop RX boundary");
            restored.scheduler.runTo(1280);
            check(load.ok() && (restored.status() & 1) && restored.pop() == 0x62,
                  "snapshot continues RX cadence independently of TX stop count");

            StopRig echo(channel);
            echo.write(4, uint8_t(0x40 | (stops << 2)));
            echo.write(5, 0x60); echo.write(14, 0x0b);
            echo.scc.receiveByte(channel, 0x71);
            echo.scheduler.runTo(639);
            check(echo.output.empty(), "Auto Echo does not precede abstract RX completion");
            echo.scheduler.runTo(640);
            check(echo.output.size() == 1 && echo.output[0] == 0x71,
                  "Auto Echo accepts all asynchronous TX stop selectors");
        }

        StopRig changed(channel);
        changed.write(4, 0x4c); changed.write(14, 0x0b);
        changed.scc.receiveByte(channel, 0x31);
        changed.scc.receiveByte(channel, 0x32);
        changed.scheduler.runTo(100);
        changed.write(4, 0x44);
        changed.scheduler.runTo(639);
        check(!(changed.status() & 1), "stop selection change retains active RX deadline");
        changed.scheduler.runTo(640);
        check((changed.status() & 1) && changed.pop() == 0x31
                  && changed.output.size() == 1 && changed.output[0] == 0x31,
              "TX stop change preserves active receive and intact echo");
        changed.scheduler.runTo(1279);
        check(!(changed.status() & 1), "next receive retains independent one-stop cadence");
        changed.scheduler.runTo(1280);
        check((changed.status() & 1) && changed.pop() == 0x32
                  && changed.output.size() == 2 && changed.output[1] == 0x32,
              "next queued character takes new echo configuration at start");
    }
    std::fprintf(stderr, "[scc-rx-stop] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_RX_STOP_STANDALONE
int main() { return testSccRxStop() ? 0 : 1; }
#endif
