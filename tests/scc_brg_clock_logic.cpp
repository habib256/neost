// UM0109 WR12/13/14 : baud = source / (2 * diviseur * (TC+2)).
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccBrgClock(){
    int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC BRG CLOCK FAIL: %s\n",name);}};
    struct Rig{
        Scheduler sched;Scc s;int c;std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel){s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            sched.setCallback(c?Scheduler::SCC_RX_B:Scheduler::SCC_RX_A,[&]{s.onSerialEvent(c,false);});
            wr(4,4);wr(3,0xc1);wr(5,0x68);wr(11,0x50);wr(12,0);wr(13,0);wr(14,3);}
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(){s.write8(0x81+4*c,0);return s.read8(0x81+4*c);}
        void tx(int b){s.write8(0x83+4*c,uint8_t(b));}
        uint8_t pop(){return s.read8(0x83+4*c);}
    };
    for(int c=0;c<2;++c)for(int pclk=0;pclk<2;++pclk){
        Rig r(c);r.wr(4,0x48);r.wr(12,7);r.wr(14,pclk?3:1);r.tx(0x51);r.tx(0x52);
        Rig rx(c);rx.wr(4,0x48);rx.wr(12,7);rx.wr(14,pclk?3:1);
        for(int n=0;n<128;++n)rx.s.receiveByte(c,0x61);
        const int64_t freq=pclk?neost::pacing::kCpuHzInt:3672000;
        const int64_t numerator=neost::pacing::kCpuHzInt*16*21*18,denominator=2*freq;
        for(int n=1;n<=128;++n){const int64_t due=(n*numerator+denominator-1)/denominator;
            r.sched.runTo(due-1);check(int(r.out.size())==n-1,"BRG TX not early");
            r.sched.runTo(due);check(int(r.out.size())==n,"BRG TX retains rational clock");
            if(n<127)r.tx(0x52);
        }
        const int64_t rxNumerator=neost::pacing::kCpuHzInt*16*20*18;
        for(int n=1;n<=128;++n){
            const int64_t due=(n*rxNumerator+denominator-1)/denominator;
            rx.sched.runTo(due-1);check(!(rx.rr()&1),"BRG RX not early with one checked stop");
            rx.sched.runTo(due);check((rx.rr()&1)&&rx.pop()==0x61,"BRG RX retains independent rational clock");
        }
        Rig slow(c);slow.wr(4,0xc4);slow.wr(12,255);slow.wr(13,255);slow.wr(14,pclk?3:1);slow.tx(0x71);
        const int64_t due=(neost::pacing::kCpuHzInt*64*20*131074+denominator-1)/denominator;
        Rig slowPause(c);slowPause.wr(4,0xc4);slowPause.wr(12,255);slowPause.wr(13,255);slowPause.wr(14,pclk?3:1);slowPause.tx(0x72);
        slowPause.sched.runTo(10);slowPause.wr(5,0x78);
        std::vector<uint8_t> slowState;auto slowSave=StateArchive::saver(slowState);slowPause.s.serialize(slowSave);
        Rig slowLoad(c);auto slowArchive=StateArchive::loader(slowState.data(),slowState.size());slowLoad.s.serialize(slowArchive);
        check(slowArchive.ok(),"snapshot accepts TC max BREAK residual without one-baud clamp");
        slow.sched.runTo(due-1);check(slow.out.empty(),"sub-one-baud BRG is not clamped to one baud");
        slow.sched.runTo(due);check(slow.out.size()==1,"TC max frame completes at exact slow deadline");
    }
    for(int c=0;c<2;++c){
        Rig changed(c);changed.tx(0x21);changed.tx(0x22);changed.sched.runTo(10);changed.wr(12,1);
        changed.sched.runTo(40);check(changed.out.size()==1,"TC change keeps active deadline");
        changed.sched.runTo(99);check(changed.out.size()==1,"next frame uses new TC");
        changed.sched.runTo(100);check(changed.out.size()==2,"new TC rational frame completes");
        Rig paused(c);paused.wr(14,1);paused.tx(0x31);paused.tx(0x32);paused.sched.runTo(10);paused.wr(5,0x78);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);paused.s.serialize(save);paused.sched.serialize(save);
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);restored.sched.serialize(load);
        restored.sched.runTo(100);restored.wr(5,0x68);restored.sched.runTo(264);
        check(restored.out.empty(),"BREAK preserves BRG fraction and remaining frame");
        restored.sched.runTo(265);check(load.ok()&&restored.out.size()==1&&restored.out[0]==0x32,"BRG BREAK snapshot resumes exact queued deadline");
    }
    return failures==0;
}
#ifdef SCC_BRG_CLOCK_STANDALONE
int main(){return testSccBrgClock()?0:1;}
#endif
