// Hatari dmaSnd.c1037 : PendingCyclesOver rattrape tous les shifts de8cycles.
#include "core/Bus.hpp"
#include "core/DmaSound.hpp"
#include "core/Scheduler.hpp"
#include <cstdio>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 16)
            std::printf("  FAIL Microwire timing %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Bus bus;
    Scheduler scheduler;
    DmaSound sound{bus};
    int64_t live = 0;
    Rig() {
        bus.machine = MachineType::Ste;
        bus.dmasnd = &sound;
        scheduler.setLiveClock([this] { return live; });
        sound.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::MICROWIRE, [this] { sound.onMicrowireShift(); });
    }
    void start(uint16_t data = 0x04C0) {
        bus.write16(0xFF8924, 0x07FF);
        bus.write16(0xFF8922, data); // maître=0 : observable par masterGain après16shifts
    }
    int64_t due() {
        const int64_t remaining = scheduler.rawCyclesUntil(Scheduler::MICROWIRE);
        return remaining == INT64_MIN ? -1 : live + remaining;
    }
};
}

int testDmaSoundMicrowireTiming() {
    Checks check;
    for (int lateness : {0, 3, 8, 32, 119, 120, 180}) {
        Rig rig;
        rig.start();
        rig.live = 8 + lateness;
        rig.scheduler.runTo(rig.live);
        const int steps = int(std::min<int64_t>(16, rig.live / 8));
        check.equal("données après rattrapage", rig.bus.read16(0xFF8922), uint16_t(uint32_t(0x04C0) << steps));
        const uint32_t mask = 0x07FF;
        const uint16_t rotated = steps == 16 ? uint16_t(mask)
            : uint16_t((mask << steps) | (mask >> (16 - steps)));
        check.equal("masque après rattrapage", rig.bus.read16(0xFF8924), rotated);
        check.equal("prochain front garde grille", rig.due(), steps == 16 ? -1 : 8 * (steps + 1));
        check.equal("commande appliquée exactement au bout", rig.sound.masterGain() < 0.001f, steps == 16);
    }
    {
        Rig rig;
        rig.live = 37; // instruction CPU en cours, Scheduler encore à zéro
        rig.start();
        check.equal("départ au temps MMIO vivant", rig.due(), 45);
    }
    {
        Rig rig;
        rig.start();
        rig.live = 40;
        rig.scheduler.runTo(40);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.scheduler.serialize(save);
        rig.sound.serialize(save);
        rig.live = 128;
        rig.scheduler.runTo(128);
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        rig.scheduler.serialize(load);
        rig.sound.serialize(load);
        check.equal("archive transfert en cours", load.ok(), true);
        rig.live = 40;
        check.equal("archive phase restaurée", rig.due(), 48);
        rig.live = 127;
        rig.scheduler.runTo(127);
        check.equal("commande pas encore appliquée", rig.sound.masterGain() < 0.001f, false);
        rig.live = 128;
        rig.scheduler.runTo(128);
        check.equal("commande appliquée après archive", rig.sound.masterGain() < 0.001f, true);
    }
    {
        Rig rig;
        rig.start(0x04C0);
        rig.live = 40; // aucun dispatch depuis le début
        check.equal("lecture MMIO rattrape données", rig.bus.read16(0xFF8922), uint16_t(0x04C0u << 5));
        check.equal("lecture MMIO garde phase", rig.due(), 48);
        rig.bus.write16(0xFF8924, 0xFFFF);
        rig.bus.write16(0xFF8922, 0xFFFF);
        check.equal("écritures pendant transfert ignorées", rig.bus.read16(0xFF8922), uint16_t(0x04C0u << 5));
        check.equal("masque pendant transfert intact", rig.bus.read16(0xFF8924), uint16_t((0x07FFu << 5) | (0x07FFu >> 11)));
        rig.live = 128;
        rig.bus.write16(0xFF8924, 0x07FF); // termine l'ancienne commande avant la nouvelle
        check.equal("fin MMIO applique ancienne commande", rig.sound.masterGain() < 0.001f, true);
        rig.bus.write16(0xFF8922, 0x04E8); // maître40 : retour0dB après128cycles
        check.equal("nouveau mot complet non altéré", rig.bus.read16(0xFF8922), 0x04E8);
        check.equal("nouveau départ après fin physique", rig.due(), 136);
        rig.live = 256;
        rig.scheduler.runTo(256);
        check.equal("nouvelle commande appliquée", rig.sound.masterGain() > 0.99f, true);
    }
    {
        Rig rig;
        rig.start();
        rig.live = 7;
        check.equal("avant premier front", rig.bus.read16(0xFF8922), 0x04C0);
        check.equal("masque avant premier front", rig.bus.read16(0xFF8924), 0x07FF);
        rig.live = 8;
        check.equal("premier front exact via MMIO", rig.bus.read16(0xFF8924), 0x0FFE);
        check.equal("deuxième front après lecture", rig.due(), 16);
        rig.live = 127;
        rig.bus.write16(0xFF8922, 0xFFFF);
        rig.bus.write16(0xFF8924, 0xFFFF);
        check.equal("écriture juste avant fin reste ignorée", rig.due(), 128);
        check.equal("avant fin pas décodage", rig.sound.masterGain() > 0.99f, true);
        rig.live = 128;
        check.equal("seizième front exact via MMIO", rig.bus.read16(0xFF8922), 0);
        check.equal("masque original après fin", rig.bus.read16(0xFF8924), 0x07FF);
        check.equal("fin annule source échue", rig.due(), -1);
        check.equal("décodage exact à fin", rig.sound.masterGain() < 0.001f, true);
    }
    std::printf("[dmasound-microwire-timing] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_DMASOUND_MICROWIRE_TIMING_TEST_MAIN
int main() { return testDmaSoundMicrowireTiming() ? 1 : 0; }
#endif
