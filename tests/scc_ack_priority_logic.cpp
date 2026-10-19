// UM0109 p.41–42 : la chaîne des IUS qualifie aussi les acquittements.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct PriorityRig {
    Scc scc;
    void command(int channel, uint8_t value) { scc.write8(0x81 + 4*channel, value); }
    void write(int channel, int reg, uint8_t value) {
        command(channel, uint8_t(reg)); command(channel, value);
    }
    uint8_t read(int channel, int reg) {
        command(channel, uint8_t(reg));
        return scc.read8(0x81 + 4*channel);
    }
    uint8_t pop(int channel) { return scc.read8(0x83 + 4*channel); }
    void acknowledge(int path) {
        if (path == 2) scc.processIack();
        else read(path, 2);
    }
    PriorityRig() {
        for (int channel = 0; channel < 2; ++channel) {
            write(channel, 15, 0);
            write(channel, 14, 0);
            write(channel, 3, 0xc1);
            write(channel, 1, 0x12);
        }
        write(0, 2, 0xa0);
        write(0, 9, 0x28); // MIE et software INTACK.
    }
};
}

bool testSccAckPriority() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC ACK PRIORITY FAIL: %s\n", label);
        }
    };

    for (int path = 0; path < 3; ++path) {
        for (int servicedChannel = 0; servicedChannel < 2; ++servicedChannel) {
            PriorityRig rig;
            rig.scc.receiveByte(servicedChannel, 0x41);
            rig.scc.processIack();
            rig.pop(servicedChannel);
            if (servicedChannel == 0) rig.scc.receiveByte(1, 0x42);
            else rig.scc.write8(0x87, 0x55);
            const uint8_t pending = servicedChannel == 0 ? 4 : 2;
            check(!rig.scc.irqActive() && rig.read(0, 3) == pending,
                  "existing IUS blocks a lower priority pending source");
            rig.acknowledge(path);
            rig.acknowledge(path);
            check(!rig.scc.irqActive() && rig.read(0, 3) == pending,
                  "blocked acknowledge preserves pending source");
            rig.write(0, 9, 0x20);
            rig.acknowledge(path);
            rig.write(0, 9, 0x28);
            check(!rig.scc.irqActive(), "MIE mask and reenable retain earlier IUS");

            std::vector<uint8_t> state;
            auto save = StateArchive::saver(state);
            rig.scc.serialize(save);
            PriorityRig restored;
            auto load = StateArchive::loader(state.data(), state.size());
            restored.scc.serialize(load);
            check(load.ok(), "blocked acknowledge snapshot is accepted");
            for (PriorityRig* candidate : {&rig, &restored}) {
                candidate->command(0, 0x38);
                check(candidate->scc.irqActive(),
                      "one reset highest IUS releases lower source without phantom IUS");
            }
        }

        // Un IUS B n'empêche pas un RX A de priorité supérieure d'être acquitté.
        PriorityRig rig;
        rig.scc.receiveByte(1, 0x42);
        rig.scc.processIack();
        rig.pop(1);
        rig.scc.receiveByte(0, 0x41);
        check(rig.scc.irqActive(), "higher priority receive preempts existing IUS");
        rig.acknowledge(path);
        rig.pop(0);
        rig.scc.write8(0x87, 0x55);
        check(!rig.scc.irqActive(), "nested IUS blocks lower B transmitter");
        rig.command(0, 0x38);
        check(!rig.scc.irqActive(), "first reset clears nested A IUS but retains B IUS");
        rig.command(0, 0x38);
        check(rig.scc.irqActive(), "second reset releases remaining B IUS");
    }
    std::fprintf(stderr, "[scc-ack-priority] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_ACK_PRIORITY_STANDALONE
int main() { return testSccAckPriority() ? 0 : 1; }
#endif
