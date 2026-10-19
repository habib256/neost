// acia.c ACIA_Write_CR/ACIA_Set_Timers_IKBD : le diviseur réancre la phase.
// Port borné à la reprise BREAK standard /64 8N1 ; pas de bitstream SCI.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
int testIkbdTxControl() {
    int checks=0,failures=0;auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::printf("  FAIL IKBD TX control %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    for(int archive=0;archive<2;++archive) {
        Scheduler s;Mfp m;Ikbd k(m);k.setScheduler(&s);s.setCallback(Scheduler::IKBD_TX,[&]{k.onTxEmpty();});
        s.runTo(73);k.write8(0,0xF6);k.write8(2,0x1C);
        s.runTo(200);k.write8(0,0x76); // RX IRQ seule, phase inchangée
        if(archive){std::vector<uint8_t> state;auto out=StateArchive::saver(state);k.serialize(out);s.serialize(out);
            k.write8(0,3);auto in=StateArchive::loader(state.data(),state.size());k.serialize(in);s.serialize(in);
            eq("archive break phase",in.ok(),1);}
        s.runTo(500);k.write8(0,0xB6);
        eq("front phase CR",s.nextDue(),1097);s.runTo(1096);eq("avant front TDR plein",k.read8(0)&2,0);
        s.runTo(1097);eq("front TDR vide",k.read8(0)&2,2);
        eq("stop phase reprise",s.nextDue(),10313);
    }
    {
        Scheduler s;Mfp m;Ikbd k(m);k.setScheduler(&s);s.setCallback(Scheduler::IKBD_TX,[&]{k.onTxEmpty();});
        s.runTo(73);k.write8(0,0xF6);k.write8(2,0x1C);
        s.runTo(200);k.write8(0,0xF5);s.runTo(333);k.write8(0,0xF6);
        s.runTo(600);k.write8(0,0x96);eq("diviseur réancre BREAK",s.nextDue(),1357);
        // Un master reset annule le TDR, puis une nouvelle programmation /64
        // réancre la prochaine reprise sans ressusciter une commande ancienne.
        k.write8(0,3);eq("reset annule TX",s.cyclesUntil(Scheduler::IKBD_TX),-1);
        s.runTo(701);k.write8(0,0xF6);k.write8(2,0);s.runTo(800);k.write8(0,0x96);
        eq("phase après master reset",s.nextDue(),1725);
    }
    for(int controlWrite=0;controlWrite<2;++controlWrite) {
        Scheduler s;Mfp m;Ikbd k(m);int64_t live=0;k.setScheduler(&s);s.setLiveClock([&]{return live;});
        s.setCallback(Scheduler::IKBD_TX,[&]{k.onTxEmpty();});k.write8(0,0x96);k.write8(2,0x1C);
        live=1500; // le front1024 a eu lieu sur le fil, dispatch CPU pas encore servi
        k.write8(controlWrite?0:2,controlWrite?0xF6:0);
        eq("MMIO rattrape TSR engagé",s.liveNow()+s.rawCyclesUntil(Scheduler::IKBD_TX),10240);
        eq("MMIO TDR état réel",k.read8(0)&2,controlWrite?2:0);
        live=10240;s.runTo(10240);
        eq("ancienne commande livrée",s.liveNow()+s.rawCyclesUntil(Scheduler::IKBD_RX),17408);
    }
    std::printf("[selftest-ikbd-tx-control] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_IKBD_TX_CONTROL_TEST_MAIN
int main(){return testIkbdTxControl()?1:0;}
#endif
