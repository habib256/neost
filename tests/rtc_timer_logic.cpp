// Ricoh EK-086-9908 p7 note TIMER EN : retenue sous-seconde conservée à l'arrêt.
#include "io/Rtc.hpp"
#include <cstdio>
int testRtcTimer() {
    int checks=0,failures=0;auto eq=[&](const char* name,int got,int want){++checks;if(got!=want){++failures;std::printf("  FAIL RTC timer %s: %d attendu %d\n",name,got,want);}};
    for(int observed=0;observed<2;++observed) for(int archived=0;archived<2;++archived) {
        int64_t clock=0;Rtc r;r.setClock([&]{return clock;});r.setSecondCycles(100);
        r.setDateTime({59,59,23,6,31,12,99});r.getDateTime();
        clock=75;r.write8(0xFFFC3B,0);clock=110;
        if(observed)eq("compteur arrêté",r.getDateTime().sec,59);
        if(archived){
            std::vector<uint8_t> state;auto out=StateArchive::saver(state);r.serialize(out);
            r.write8(0xFFFC3B,8);auto in=StateArchive::loader(state.data(),state.size());r.serialize(in);
            eq("archive retenue",in.ok(),1);
        }
        clock=125;r.write8(0xFFFC3B,8);auto dt=r.getDateTime();
        eq("carry délivré reprise",dt.sec,0);eq("carry calendaire",dt.year,0);
        clock=199;eq("phase avant front",r.getDateTime().sec,0);
        clock=200;eq("phase front exact",r.getDateTime().sec,1);
    }
    {
        int64_t clock=0;Rtc r;r.setClock([&]{return clock;});r.setSecondCycles(100);
        r.setDateTime({0,0,0,0,1,1,26});r.getDateTime();clock=20;r.write8(0xFFFC3B,0);
        clock=120;r.getDateTime();clock=350;r.write8(0xFFFC3B,1); // banque seule
        eq("arrêt long reste figé",r.getDateTime().sec,0);
        clock=420;r.write8(0xFFFC3B,9);eq("carry saturante",r.getDateTime().sec,1);
        clock=499;eq("phase arrêt long",r.getDateTime().sec,1);
        clock=500;eq("phase continue après arrêt",r.getDateTime().sec,2);
        clock=520;r.write8(0xFFFC3B,0);clock=550;r.write8(0xFFFC3B,8);
        eq("pas carry si pas front",r.getDateTime().sec,2);
        clock=600;eq("arrêt bref sans retard",r.getDateTime().sec,3);
    }
    // Déduction du reset des étages sous-seconde (p7), non mesure matérielle :
    // ADJ/RESET1 doivent aussi annuler la retenue produite par ces étages.
    for(int operation=0;operation<3;++operation) {
        int64_t clock=0;Rtc r;r.setClock([&]{return clock;});r.setSecondCycles(100);
        r.setDateTime({0,0,0,0,1,1,26});r.getDateTime();clock=75;r.write8(0xFFFC3B,1);
        clock=110;r.getDateTime();clock=125;
        if(operation==0)r.write8(0xFFFC3F,2);
        if(operation==1)r.write8(0xFFFC23,1); // ADJ banque1
        if(operation==2)r.setDateTime({0,0,0,0,1,1,26});
        clock=150;r.write8(0xFFFC3B,9);
        eq("reset annule carry retenu",r.getDateTime().sec,0);
        const int64_t next=operation==2?250:225;
        clock=next-1;eq("reset nouvelle phase avant",r.getDateTime().sec,0);
        clock=next;eq("reset nouvelle phase exacte",r.getDateTime().sec,1);
    }
    std::printf("[selftest-rtc-timer] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_RTC_TIMER_TEST_MAIN
int main(){return testRtcTimer()?1:0;}
#endif
