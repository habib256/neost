// Calendrier par lots comparé au chemin public n=1 (tickOneSecond indépendant).
#include "io/Rtc.hpp"
#include <cstdio>
#include <vector>

namespace {
constexpr uint32_t address(int index) { return 0xFFFC21 + 2*index; }
void configure(Rtc& rtc, const Rtc::DateTime& date, bool twelveHours, int leap) {
    rtc.write8(address(13),9);
    rtc.write8(address(10),twelveHours?0:1);
    rtc.write8(address(13),8);
    rtc.setDateTime(date);
    rtc.write8(address(13),9);
    rtc.write8(address(11),uint8_t(leap));
    rtc.write8(address(13),8);
}
}

int testRtcElapsedDifferential() {
    int checks=0,failures=0;
    auto equal=[&](const char* label,int64_t got,int64_t wanted) {
        ++checks;
        if(got==wanted) return;
        ++failures;
        if(failures<20) std::fprintf(stderr,"[rtc-elapsed-differential] FAIL %s: %lld != %lld\n",
                                    label,(long long)got,(long long)wanted);
    };
    auto same=[&](Rtc& batch,Rtc& reference) {
        for(int digit=0;digit<13;++digit)
            equal("chiffre calendrier différentiel",batch.read8(address(digit)),reference.read8(address(digit)));
        batch.write8(address(13),9);
        reference.write8(address(13),9);
        equal("compteur bissextile indépendant",batch.read8(address(11)),reference.read8(address(11)));
        equal("mode 12/24 h",batch.read8(address(10)),reference.read8(address(10)));
        batch.write8(address(13),8);
        reference.write8(address(13),8);
    };
    struct Case { Rtc::DateTime date; bool twelveHours; int leap; int64_t seconds; };
    const Case cases[] = {
        {{58,59,23,6,28,2,25},false,0,3*86400+7}, // année impaire mais leap=0
        {{58,59,11,0,28,2,24},true,2,3*86400+7},  // année paire mais pas bissextile
        {{59,59,23,5,30,12,99},true,3,3*86400+1}, // année00 et leap3->0
        {{59,59,23,2,30,4,0},false,1,2*86400+13}, // mois30jours et weekday
        {{1,2,3,4,31,1,42},false,0,90001},       // première seconde au-delà clamp
    };
    for(const auto& test:cases) {
        Rtc batch,reference;
        configure(batch,test.date,test.twelveHours,test.leap);
        configure(reference,test.date,test.twelveHours,test.leap);
        batch.advanceSeconds(test.seconds);
        for(int64_t n=0;n<test.seconds;++n) reference.advanceSeconds(1);
        same(batch,reference);
    }
    {
        // Chiffres intermédiaires autorisés par les masques MMIO, mais pas
        // une date valide. Le lot doit conserver la sémantique du premier tick.
        Rtc batch,reference;
        for(Rtc* rtc:{&batch,&reference}) {
            configure(*rtc,{0,59,23,6,31,12,99},false,3);
            rtc->write8(address(0),15);
            rtc->write8(address(1),7); // secondes85 avant normalisation
        }
        batch.advanceSeconds(90001);
        for(int n=0;n<90001;++n) reference.advanceSeconds(1);
        same(batch,reference);
    }
    // Chaque autre étage peut être transitoirement non BCD entre deux
    // écritures logicielles. Le premier tick reste la référence de normalisation.
    const int invalid[][4] = {
        {2,15,3,7}, {4,15,5,3}, {7,15,8,3}, {9,15,10,1}, {11,15,12,15}
    };
    for(const auto& registers:invalid) {
        Rtc batch,reference;
        for(Rtc* rtc:{&batch,&reference}) {
            configure(*rtc,{59,59,23,7,31,12,99},false,3);
            rtc->write8(address(registers[0]),uint8_t(registers[1]));
            rtc->write8(address(registers[2]),uint8_t(registers[3]));
        }
        batch.advanceSeconds(90001);
        for(int n=0;n<90001;++n) reference.advanceSeconds(1);
        same(batch,reference);
    }
    {
        Rtc batch,reference;
        int64_t clock=0;
        batch.setClock([&]{return clock;});
        batch.setSecondCycles(1000);
        const Rtc::DateTime date{59,59,23,6,31,12,99};
        configure(batch,date,true,3);
        configure(reference,date,true,3);
        (void)batch.getDateTime(); // amorcer la phase
        constexpr int64_t elapsed=2*86400+13;
        clock=elapsed*1000+123;
        (void)batch.getDateTime();
        for(int64_t n=0;n<elapsed;++n) reference.advanceSeconds(1);
        same(batch,reference);
        clock+=876; // reliquat999 cycles, pas de seconde supplémentaire
        same(batch,reference);
        ++clock;
        reference.advanceSeconds(1);
        same(batch,reference);
        std::vector<uint8_t> state;
        {auto ar=StateArchive::saver(state);batch.serialize(ar);}
        Rtc restored;
        restored.setClock([&]{return clock;});
        restored.setSecondCycles(1000);
        {auto ar=StateArchive::loader(state.data(),state.size());restored.serialize(ar);equal("snapshot phase valide",ar.ok(),true);}
        clock+=999;
        same(restored,reference);
        ++clock;
        reference.advanceSeconds(1);
        same(restored,reference);
    }
    std::fprintf(stderr,"[rtc-elapsed-differential] %d checks, %d FAIL\n",checks,failures);
    return failures;
}
#ifdef NEOST_RTC_ELAPSED_DIFFERENTIAL_TEST_MAIN
int main(){return testRtcElapsedDifferential()?1:0;}
#endif
