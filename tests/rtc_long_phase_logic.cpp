// Ricoh RP5C15 EK-086-9908 p.7 : TIMER EN retient une seule retenue sous-seconde.
#include "io/Rtc.hpp"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t rtcAddress(int reg) { return 0xfffc21 + 2*reg; }

struct RtcPhaseRig {
    int64_t clock = 0;
    Rtc rtc;

    explicit RtcPhaseRig(int64_t secondCycles = 100) {
        rtc.setClock([&] { return clock; });
        rtc.setSecondCycles(secondCycles);
        rtc.setDateTime({10, 20, 3, 2, 15, 6, 24});
    }
};
}

int testRtcLongPhase() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* name, int64_t got, int64_t expected) {
        ++checks;
        if (got != expected) {
            ++failures;
            std::fprintf(stderr, "RTC LONG PHASE FAIL: %s: %lld != %lld\n",
                         name, (long long)got, (long long)expected);
        }
    };

    {
        RtcPhaseRig rig;
        equal("prime time", rig.rtc.getDateTime().sec, 10);
        rig.clock = 25;
        rig.rtc.write8(rtcAddress(13), 0); // arrêt du compteur, pas du diviseur
        rig.clock = 20'000'025;           // plus de 25 h à 100 cycles/seconde
        equal("long stopped interval leaves seconds frozen", rig.rtc.getDateTime().sec, 10);
        equal("long stopped interval leaves date frozen", rig.rtc.getDateTime().day, 15);

        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.rtc.serialize(save);
        RtcPhaseRig restored;
        restored.clock = rig.clock;
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        restored.rtc.serialize(load);
        equal("stopped snapshot valid", load.ok(), true);
        equal("restored stopped timer stays frozen", restored.rtc.getDateTime().sec, 10);

        // Comparer la reprise directe et restaurée, puis le reliquat de 75 cycles.
        for (RtcPhaseRig* current : {&rig, &restored}) {
            current->rtc.write8(rtcAddress(13), 8);
            equal("restart releases one carry only", current->rtc.getDateTime().sec, 11);
            current->clock += 74;
            equal("subsecond phase preserved before next tick", current->rtc.getDateTime().sec, 11);
            ++current->clock;
            equal("subsecond phase reaches next tick after 75 cycles", current->rtc.getDateTime().sec, 12);
        }
    }

    {
        // Intervalle monotone > INT64_MAX, mais seulement deux secondes RTC :
        // cela vérifie l'arithmétique de phase sans reproduire le calendrier par lots.
        constexpr int64_t second = int64_t(1) << 62;
        RtcPhaseRig rig(second);
        rig.clock = std::numeric_limits<int64_t>::min() + 50;
        rig.rtc.getDateTime();
        rig.clock = 100;
        equal("unsigned elapsed crosses signed range without losing ticks", rig.rtc.getDateTime().sec, 12);
        rig.clock = second + 49;
        equal("extreme elapsed preserves remainder before next tick", rig.rtc.getDateTime().sec, 12);
        ++rig.clock;
        equal("extreme elapsed next tick remains exact", rig.rtc.getDateTime().sec, 13);
    }

    std::printf("[rtc-long-phase] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef RTC_LONG_PHASE_STANDALONE
int main() { return testRtcLongPhase() ? 1 : 0; }
#endif
