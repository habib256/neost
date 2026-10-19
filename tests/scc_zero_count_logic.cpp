// UM0109 p.57/p.71/p.177 : ZC compteur avant /2, statut courant non latché.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccZeroCount(){
    int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC ZERO COUNT FAIL: %s\n",name);}};
    struct Rig{
        Scheduler sched;Scc s;int c,count=0;
        explicit Rig(int channel):c(channel){s.setScheduler(&sched);
            sched.setCallback(c?Scheduler::SCC_BRG_B:Scheduler::SCC_BRG_A,[&]{++count;s.onBrgEvent(c);});
            wr(4,4);wr(12,0);wr(13,0);wr(15,2);wr(1,1);wr(9,8);wr(14,3);}
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(int r){s.write8(0x81+4*c,uint8_t(r));return s.read8(0x81+4*c);}
    };
    for(int c=0;c<2;++c){
        Rig r(c);r.sched.runTo(1);check(!r.s.irqActive(),"PCLK TC0 not before two clocks");r.sched.runTo(2);
        check(r.count==1&&r.s.irqActive()&&(r.rr(0)&2),"zero counter triggers at TC+2, before output divider");
        r.sched.runTo(3);check(!(r.rr(0)&2)&&r.s.irqActive(),"ZC bit follows one-clock pulse while IP persists");
        r.wr(4,0xc4);r.wr(11,0xf8);check(r.sched.nextDue()==4,"WR4 and serial mux do not restart counter");
        r.wr(12,10);check(r.sched.nextDue()==4,"new TC waits for counter reload");r.sched.runTo(4);
        check(r.sched.nextDue()==16,"next reload takes new TC");
        r.wr(15,0);check(!(r.rr(0)&2),"WR15 masks live ZC status");
        r.wr(0,0x10);r.sched.runTo(16);check(!r.s.irqActive(),"masked counter keeps running without IRQ");
        r.wr(15,2);r.sched.runTo(28);check(r.s.irqActive(),"unmasked next zero retriggers");
        Rig q(c);q.wr(14,1);const int64_t num=neost::pacing::kCpuHzInt*2,den=3672000;
        for(int n=1;n<=1000;++n){const int64_t due=(n*num+den-1)/den;
            q.sched.runTo(due-1);check(q.count==n-1,"RTxC ZC not early");q.sched.runTo(due);check(q.count==n,"RTxC rational cumulative deadline");}
        Rig snapshot(c);snapshot.wr(14,1);snapshot.sched.runTo(7);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);snapshot.s.serialize(save);snapshot.sched.serialize(save);
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);restored.sched.serialize(load);
        restored.sched.runTo(100);snapshot.sched.runTo(100);
        check(load.ok()&&restored.count==snapshot.count-1&&restored.sched.nextDue()==snapshot.sched.nextDue(),"snapshot preserves fractional counter phase");
        Rig pulse(c);pulse.sched.runTo(2);std::vector<uint8_t> pulseState;
        auto pulseSave=StateArchive::saver(pulseState);pulse.s.serialize(pulseSave);pulse.sched.serialize(pulseSave);
        Rig pulseRestored(c);auto pulseLoad=StateArchive::loader(pulseState.data(),pulseState.size());pulseRestored.s.serialize(pulseLoad);pulseRestored.sched.serialize(pulseLoad);
        check(pulseLoad.ok()&&(pulseRestored.rr(0)&2),"snapshot preserves current ZC pulse");
        pulseRestored.sched.runTo(3);check(!(pulseRestored.rr(0)&2),"restored ZC pulse expires without extra event slot");
        Rig late(c);int64_t live=7;late.sched.setLiveClock([&]{return live;});late.sched.runTo(7);
        check(late.s.irqActive()&&!(late.rr(0)&2),"overshot pulse still latches interrupt without stale RR0");
    }
    return failures==0;
}
#ifdef SCC_ZERO_COUNT_STANDALONE
int main(){return testSccZeroCount()?0:1;}
#endif
