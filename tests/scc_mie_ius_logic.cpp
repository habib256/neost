// UM0109 p.45/p.161 : MIE masque les IRQ, préserve les IUS et interdit leur pose.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

namespace {
struct InterruptRig {
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
    uint8_t pop(int channel) { return scc.read8(0x83 + 4*channel); }
    void master(bool enabled) { write(0, 9, uint8_t(0x20 | (enabled ? 8 : 0))); }
    void receive(int channel, int mode) {
        write(channel, 1, uint8_t(mode << 3));
        scc.receiveByte(channel, 0x41);
        if (mode == 3) {
            // L'overrun devient spécial après lecture du caractère en erreur.
            scc.receiveByte(channel, 0x42);
            scc.receiveByte(channel, 0x43);
            scc.receiveByte(channel, 0x44);
            pop(channel); pop(channel); pop(channel);
        }
    }
    InterruptRig() {
        for (int channel = 0; channel < 2; ++channel) {
            write(channel, 15, 0);
            write(channel, 14, 0);
            write(channel, 3, 0xc1);
        }
        write(0, 2, 0xa0);
        master(true);
    }
};
}

bool testSccMieIus() {
    int checks = 0, failures = 0;
    auto check = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "SCC MIE/IUS FAIL: %s\n", label);
        }
    };

    // RR2A, RR2B et INTACK matériel ne doivent pas acquérir d'IUS avec MIE=0.
    // Vérifier chaque mode RX sur les deux canaux, dont le verrou spécial FIFO.
    for (int channel = 0; channel < 2; ++channel) {
        for (int mode = 1; mode <= 3; ++mode) {
            for (int acknowledge = 0; acknowledge < 3; ++acknowledge) {
                InterruptRig rig;
                rig.master(false);
                rig.receive(channel, mode);
                const uint8_t pending = channel ? 4 : 0x20;
                check(!rig.scc.irqActive() && rig.read(0, 3) == pending,
                      "masked RX remains pending without requesting IRQ");
                const int vector = acknowledge == 2 ? rig.scc.processIack()
                                                   : rig.read(acknowledge, 2);
                const int status = channel ? (mode == 3 ? 3 : 2) : (mode == 3 ? 7 : 6);
                check(vector == (acknowledge == 1 ? (0xa0 | (status << 1)) : 0xa0),
                      "masked acknowledge retains documented vector readback");
                rig.master(true);
                check(rig.scc.irqActive(), "masked acknowledge did not create blocking IUS");
            }
        }
    }

    // Un IUS déjà acquis survit au masquage, aux lectures et à l'archive.
    for (int acknowledge = 0; acknowledge < 3; ++acknowledge) {
        InterruptRig rig;
        rig.receive(0, 2);
        if (acknowledge == 2) rig.scc.processIack();
        else rig.read(acknowledge, 2);
        rig.pop(0);
        rig.receive(1, 2);
        check(!rig.scc.irqActive(), "A IUS blocks lower priority B receive");
        rig.master(false);

        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        rig.scc.serialize(save);
        InterruptRig restored;
        auto load = StateArchive::loader(state.data(), state.size());
        restored.scc.serialize(load);
        check(load.ok(), "masked IUS snapshot is accepted");
        for (InterruptRig* candidate : {&rig, &restored}) {
            candidate->master(true);
            check(!candidate->scc.irqActive() && candidate->read(0, 3) == 4,
                  "MIE reenable preserves earlier IUS blocking B");
            candidate->control(0, 0x38);
            check(candidate->scc.irqActive(), "reset highest IUS releases waiting B receive");
        }
    }

    // La commande explicite de libération reste effective quand MIE masque INT.
    {
        InterruptRig rig;
        rig.receive(0, 2);
        rig.scc.processIack();
        rig.pop(0);
        rig.receive(1, 2);
        rig.master(false);
        rig.control(0, 0x38);
        check(!rig.scc.irqActive(), "reset highest IUS does not bypass MIE mask");
        rig.master(true);
        check(rig.scc.irqActive(), "reset highest IUS remains effective while masked");
    }

    std::fprintf(stderr, "[scc-mie-ius] %d checks, %d FAIL\n", checks, failures);
    return failures == 0;
}

#ifdef SCC_MIE_IUS_STANDALONE
int main() { return testSccMieIus() ? 0 : 1; }
#endif
