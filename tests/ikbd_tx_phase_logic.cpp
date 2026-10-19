// Hatari acia.c ACIA_Set_Timers_IKBD/ACIA_Clock_TX : TDR chargé au front TX.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
int testIkbdTxPhase() {
    int checks=0,failures=0;auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::printf("  FAIL IKBD TX phase %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    for(int saved=0;saved<2;++saved) {
        Scheduler s;Mfp m;Ikbd k(m);k.setScheduler(&s);s.setCallback(Scheduler::IKBD_TX,[&]{k.onTxEmpty();});
        s.runTo(73);k.write8(0,0x96);s.runTo(100);k.write8(2,0);
        eq("premier front réancré",s.nextDue(),1097);
        s.runTo(150);k.write8(0,0xB6);eq("TIE ne recale pas",s.nextDue(),1097);
        if(saved){std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);k.serialize(out);s.serialize(out);
            k.write8(0,3);auto in=StateArchive::loader(bytes.data(),bytes.size());k.serialize(in);s.serialize(in);eq("archive phase TX",in.ok(),1);}
        s.runTo(1096);eq("avant front TDR plein",k.read8(0)&2,0);s.runTo(1097);
        eq("front TDR vide",k.read8(0)&2,2);k.write8(2,0);
        eq("premier stop",s.nextDue(),10313);s.runTo(10313);eq("suivant même phase",s.nextDue(),11337);
        s.runTo(11337);eq("second stop",s.nextDue(),20553);s.runTo(20553);
        s.runTo(22000);k.write8(2,0);eq("nouveau flux même phase",s.nextDue(),22601);
    }
    {
        Scheduler s;Mfp m;Ikbd k(m);k.setScheduler(&s);s.setCallback(Scheduler::IKBD_TX,[&]{k.onTxEmpty();});
        k.write8(0,0x96);k.write8(2,0);s.runTo(100);k.write8(0,0x95);
        s.runTo(333);k.write8(0,0x96);eq("diviseur recale TDR attente",s.nextDue(),1357);
        s.runTo(1357);k.write8(2,0);s.runTo(1500);k.write8(0,0x95);
        s.runTo(2000);k.write8(0,0x96);
        eq("TSR engagé conserve stop",s.nextDue(),10573);
        s.runTo(10573);eq("suivant nouvelle phase CR",s.nextDue(),11216);
    }
    std::printf("[selftest-ikbd-tx-phase] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_IKBD_TX_PHASE_TEST_MAIN
int main(){return testIkbdTxPhase()?1:0;}
#endif
