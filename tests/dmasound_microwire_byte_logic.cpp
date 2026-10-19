// Hatari dmaSnd.c1195-1204 : une écriture octet fusionne l'IoMem visible,
// pas la commande latchée avant les seize décalages précédents.
#include "core/Bus.hpp"
#include "core/DmaSound.hpp"
#include "core/Scheduler.hpp"
#include <cstdio>

namespace {
struct Rig {
    Bus bus;
    Scheduler scheduler;
    DmaSound sound{bus};
    int64_t live = 0;
    Rig() {
        bus.machine = MachineType::Ste;
        bus.dmasnd = &sound;
        sound.setScheduler(&scheduler);
        scheduler.setLiveClock([this] { return live; });
        scheduler.setCallback(Scheduler::MICROWIRE, [this] { sound.onMicrowireShift(); });
    }
};
}
int testDmaSoundMicrowireByte() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, uint32_t actual, uint32_t expected) {
        ++checks;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[microwire-byte] FAIL %s: %X != %X\n", label, actual, expected);
    };
    for (bool low : {false, true}) {
        for (bool delayed : {false, true}) {
            Rig rig;
            rig.bus.write16(0xFF8922, 0x1234);
            rig.live = 128;
            if (!delayed) rig.scheduler.runTo(128);
            rig.bus.write8(low ? 0xFF8923 : 0xFF8922, 0xAB);
            equal("nouvel octet fusionne donnée décalée", rig.bus.read16(0xFF8922),
                  low ? 0x00AB : 0xAB00);
            equal("départ nouveau transfert", rig.scheduler.rawCyclesUntil(Scheduler::MICROWIRE), 8);
            rig.live = 136;
            rig.scheduler.runTo(136);
            equal("shift nouveau transfert", rig.bus.read16(0xFF8922),
                  low ? 0x0156 : 0x5600);
        }
    }
    {
        Rig rig;
        rig.bus.write16(0xFF8922, 0x1234);
        rig.live = 128;
        rig.scheduler.runTo(128);
        rig.bus.write16(0xFF8922, 0xABCD);
        equal("mot conserve ses deux octets", rig.bus.read16(0xFF8922), 0xABCD);
        rig.bus.write8(0xFF8923, 0xEF);
        equal("octet pendant transfert ignoré", rig.bus.read16(0xFF8922), 0xABCD);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.sound.serialize(save); rig.scheduler.serialize(save);
        rig.live = 256;
        rig.scheduler.runTo(256);
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        rig.sound.serialize(load); rig.scheduler.serialize(load);
        equal("archive commande latchée", load.ok(), true);
        rig.live = 256;
        rig.bus.write8(0xFF8923, 0x56);
        equal("octet après archive et fin physique", rig.bus.read16(0xFF8922), 0x0056);
    }
    for (bool scheduled : {false, true}) {
        Rig rig;
        if (!scheduled) rig.sound.setScheduler(nullptr);
        rig.bus.write16(0xFF8924, 0x07FF);
        rig.bus.write16(0xFF8922, 0x04C0); // volume maître au minimum
        rig.live = 128;
        if (scheduled) rig.scheduler.runTo(128);
        equal("première commande LMC reçue", rig.sound.masterGain() < 0.001f, true);
        rig.bus.write8(0xFF8923, 0xE8);
        rig.live = 256;
        if (scheduled) rig.scheduler.runTo(256);
        // 00E8 est invalide ; réutiliser le haut04 créerait 04E8 (volume max).
        equal("octet seul ne ressuscite pas commande LMC", rig.sound.masterGain() < 0.001f, true);
        rig.bus.write16(0xFF8922, 0x04E8);
        rig.live = 384;
        if (scheduled) rig.scheduler.runTo(384);
        equal("mot suivant décode commande complète", rig.sound.masterGain() > 1.9f, true);
    }
    std::fprintf(stderr, "[microwire-byte] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_DMASOUND_MICROWIRE_BYTE_TEST_MAIN
int main() { return testDmaSoundMicrowireByte() ? 1 : 0; }
#endif
