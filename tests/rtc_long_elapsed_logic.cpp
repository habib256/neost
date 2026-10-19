// RP5C15 : grands intervalles, même calendrier/phase que les retenues scalaires.
#include "io/Rtc.hpp"
#include <cstdio>
#include <limits>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 16)
            std::printf("  FAIL RTC elapsed %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    }
    void date(Rtc& rtc, const Rtc::DateTime& expected) {
        const auto actual = rtc.getDateTime();
        equal("secondes", actual.sec, expected.sec);
        equal("minutes", actual.min, expected.min);
        equal("heures", actual.hour, expected.hour);
        equal("weekday", actual.wday, expected.wday);
        equal("jour", actual.day, expected.day);
        equal("mois", actual.month, expected.month);
        equal("année", actual.year, expected.year);
    }
};
// Oracle borné : division jours/secondes puis années et mois, depuis 01/01/00.
Rtc::DateTime afterEpoch(uint64_t seconds) {
    const uint64_t days = seconds / 86400;
    int remaining = int(days % 36525);
    int year = 0;
    while (remaining >= ((year % 4 == 0) ? 366 : 365))
        remaining -= (year++ % 4 == 0) ? 366 : 365;
    int month = 1;
    static constexpr int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    while (true) {
        const int length = lengths[month-1] + (month == 2 && year % 4 == 0);
        if (remaining < length) break;
        remaining -= length;
        ++month;
    }
    const int time = int(seconds % 86400);
    return {time % 60, time / 60 % 60, time / 3600, int(days % 7), remaining + 1, month, year};
}
}

int testRtcLongElapsed() {
    Checks check;
    const Rtc::DateTime epoch{0,0,0,0,1,1,0};
    for (int64_t seconds : {int64_t(0), int64_t(-1), int64_t(172805),
                            int64_t(86400)*400, int64_t(36525)*86400*3+12345,
                            std::numeric_limits<int64_t>::max()}) {
        Rtc rtc;
        rtc.setDateTime(epoch);
        rtc.advanceSeconds(seconds);
        check.date(rtc, afterEpoch(seconds > 0 ? uint64_t(seconds) : 0));
    }
    {
        Rtc rtc;
        rtc.setDateTime({58,59,23,5,31,12,99});
        rtc.advanceSeconds(172803);
        check.date(rtc, {1,0,0,1,3,1,0});
    }
    {
        Rtc rtc;
        int64_t now = 0;
        rtc.setDateTime(epoch);
        rtc.setClock([&] { return now; });
        rtc.setSecondCycles(10);
        rtc.getDateTime();
        now = 1728000 + 7;
        check.date(rtc, afterEpoch(172800));
        now += 2;
        check.date(rtc, afterEpoch(172800));
        ++now;
        check.date(rtc, afterEpoch(172801));
    }
    {
        Rtc rtc;
        int64_t now = 0;
        rtc.setDateTime(epoch);
        rtc.setClock([&] { return now; });
        rtc.setSecondCycles(10);
        rtc.write8(0xFFFC3B, 0); // TIMER EN arrêté, phase continue
        now = std::numeric_limits<int64_t>::max();
        check.date(rtc, epoch);
        rtc.write8(0xFFFC3B, 8);
        check.date(rtc, afterEpoch(1));
        check.date(rtc, afterEpoch(1)); // pas de rattrapage répété
    }
    {
        Rtc rtc;
        int64_t now = std::numeric_limits<int64_t>::min();
        rtc.setDateTime(epoch);
        rtc.setClock([&] { return now; });
        rtc.setSecondCycles(1);
        rtc.getDateTime();
        now = std::numeric_limits<int64_t>::max();
        check.date(rtc, afterEpoch(std::numeric_limits<uint64_t>::max()));
    }
    {
        Rtc rtc;
        rtc.setDateTime({0,0,0,0,1,1,10});
        rtc.write8(0xFFFC37, 0xA); // valeur 10, mais chiffres année non canoniques
        rtc.write8(0xFFFC39, 0);
        rtc.advanceSeconds(int64_t(36525) * 86400);
        check.equal("année brute canonicalisée au rollover", rtc.read8(0xFFFC37), 0);
        check.equal("dizaine année après cycle", rtc.read8(0xFFFC39), 1);
    }
    {
        Rtc rtc;
        rtc.setDateTime({58,0,1,0,1,1,0});
        rtc.write8(0xFFFC3B, 9);
        rtc.write8(0xFFFC35, 0); // sélection 12 h
        rtc.write8(0xFFFC3B, 8);
        rtc.write8(0xFFFC29, 15);
        rtc.write8(0xFFFC2B, 1); // heure brute 25 modulo12 = 01 AM
        rtc.advanceSeconds(2);
        check.equal("heure12 brute sans retenue horaire", rtc.read8(0xFFFC29), 15);
        check.equal("dizaine12 brute inchangée", rtc.read8(0xFFFC2B), 1);
        rtc.advanceSeconds(3540); // une vraie retenue horaire canonicalise à02 AM
        check.equal("heure12 canonicalisée après retenue", rtc.read8(0xFFFC29), 2);
        check.equal("dizaine12 canonicalisée après retenue", rtc.read8(0xFFFC2B), 0);
    }
    std::printf("[rtc-long-elapsed] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_RTC_LONG_ELAPSED_TEST_MAIN
int main() { return testRtcLongElapsed() ? 1 : 0; }
#endif
