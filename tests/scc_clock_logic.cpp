// UM0109 WR4/WR11 : division d'horloge ; garder la fraction jusqu'au caractère suivant.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccClock() {
    int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC CLOCK FAIL: %s\n",name);}};
    struct Rig {
        Scheduler sched; Scc s; int c; std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel){
            s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            sched.setCallback(c?Scheduler::SCC_RX_B:Scheduler::SCC_RX_A,[&]{s.onSerialEvent(c,false);});
            wr(4,4);wr(3,0xc1);wr(5,0x68);wr(11,0);wr(14,0);
        }
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(){s.write8(0x81+4*c,0);return s.read8(0x81+4*c);}
        void tx(uint8_t b){s.write8(0x83+4*c,b);}
        uint8_t pop(){return s.read8(0x83+4*c);}
    };
    const int mults[]={1,16,32,64};
    // TRxC x64, 1.5 stop : le reste revient exactement à zéro au 100e caractère.
    for(int c=0;c<2;++c)for(int source=0;source<2;++source)for(int div=0;div<4;++div){
        Rig r(c);r.wr(4,(div?8:4)+64*div);r.wr(11,(source<<3)|((1-source)<<5));r.tx(0x55);r.tx(0x55);
        for(int n=0;n<128;++n)r.s.receiveByte(c,0x66);
        const int txFreq=source?2457600:3672000;
        const int64_t numerator=neost::pacing::kCpuHzInt*mults[div]*(div?21:20);
        for(int n=1;n<=128;++n){
            const int64_t due=(n*numerator+2*txFreq-1)/(2*txFreq);
            r.sched.runTo(due-1);check(int(r.out.size())==n-1,"direct TX retains cumulative fractional deadline");
            r.sched.runTo(due);check(int(r.out.size())==n,"direct TX completes at rational deadline");
            if(n<127)r.tx(0x55);
            // RX peut être plus rapide : vider sa FIFO ne change pas sa cadence.
            while(r.rr()&1)r.pop();
        }
        Rig rx(c);rx.wr(4,(div?8:4)+64*div);rx.wr(11,source<<5);
        for(int n=0;n<128;++n)rx.s.receiveByte(c,0x67);
        const int64_t rxNumerator=neost::pacing::kCpuHzInt*mults[div]*20;
        for(int n=1;n<=128;++n){
            const int64_t due=(n*rxNumerator+2*txFreq-1)/(2*txFreq);
            rx.sched.runTo(due-1);check(!(rx.rr()&1),"direct RX not early");
            rx.sched.runTo(due);check((rx.rr()&1)&&rx.pop()==0x67,"direct RX cumulative fraction");
        }

    }
    for(int c=0;c<2;++c){
        Rig r(c);r.tx(0x11);r.tx(0x12);r.sched.runTo(22);r.tx(0x13);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);r.s.serialize(save);r.sched.serialize(save);
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);restored.sched.serialize(load);
        restored.sched.runTo(65);check(restored.out.size()==1,"snapshot preserves next fractional deadline");
        restored.sched.runTo(66);check(load.ok() && restored.out.size()==2 && restored.out[1]==0x13,"snapshot preserves cumulative clock remainder");
        Rig config(c);config.tx(0x21);config.tx(0x22);config.sched.runTo(10);config.wr(11,8);
        config.sched.runTo(21);check(config.out.empty(),"clock reconfiguration preserves active frame deadline");
        config.sched.runTo(22);check(config.out.size()==1,"active old-clock frame finishes");
        config.sched.runTo(54);check(config.out.size()==1,"new source starts with fresh fractional phase");
        config.sched.runTo(55);check(config.out.size()==2,"queued frame uses new source rational period");
        Rig paused(c);paused.tx(0x41);paused.tx(0x42);paused.sched.runTo(10);paused.wr(5,0x78);
        paused.sched.runTo(100);paused.wr(5,0x68);paused.sched.runTo(133);
        check(paused.out.empty(),"BREAK remainder resumes without consuming clock fraction twice");
        paused.sched.runTo(134);check(paused.out.size()==1 && paused.out[0]==0x42,"BREAK preserves next rational frame deadline");
        Rig idle(c);idle.tx(0x31);idle.sched.runTo(30);idle.tx(0x32);idle.sched.runTo(51);
        check(idle.out.size()==1,"new idle-separated stream starts from LIVE clock");
        idle.sched.runTo(52);check(idle.out.size()==2,"idle resets fractional phase");
    }
    return failures==0;
}
#ifdef SCC_CLOCK_STANDALONE
int main(){return testSccClock()?0:1;}
#endif
