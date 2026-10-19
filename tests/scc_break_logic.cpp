// UM0109 WR5 p.153 : BREAK force TxD, sans attendre la fin du caractère.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccBreak() {
    int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC BREAK FAIL: %s\n",name);}};
    struct Rig {
        Scheduler sched; Scc s; int c; std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel){
            s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            wr(15,0); // BREAK et CTS vivants, sans verrou RR0 implicite.
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            wr(4,4);wr(3,0xc1);wr(5,0x68);wr(11,0x50);wr(12,0);wr(13,0);wr(14,3);
        }
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(int r){s.write8(0x81+4*c,uint8_t(r));return s.read8(0x81+4*c);}
        void tx(uint8_t b){s.write8(0x83+4*c,b);}
    };
    for(int c=0;c<2;++c){
        Rig idle(c);idle.wr(5,0x78);idle.tx(0x41);idle.tx(0x42);idle.sched.runTo(100);
        check(idle.out.empty() && !(idle.rr(1)&1),"BREAK before TX holds TSR and TDR");
        idle.wr(5,0x68);idle.sched.runTo(139);check(idle.out.empty(),"release starts full first frame");
        idle.sched.runTo(140);idle.sched.runTo(180);
        check(idle.out.size()==2 && idle.out[0]==0x41 && idle.out[1]==0x42,"intact queued bytes resume after BREAK");
        Rig active(c);active.tx(0x51);active.tx(0x52);active.sched.runTo(10);active.wr(5,0x78);
        active.sched.runTo(100);check(active.out.empty(),"mid-frame BREAK never emits intact interrupted byte");
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);active.s.serialize(save);
        active.wr(5,0x68);active.sched.runTo(129);check(!(active.rr(0)&4),"residual TSR time retained");
        active.sched.runTo(130);check((active.rr(0)&4) && active.out.empty(),"interrupted TSR completes without byte callback");
        active.sched.runTo(170);check(active.out.size()==1 && active.out[0]==0x52,"pending intact TDR follows interrupted TSR");
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);
        restored.wr(5,0x68);restored.sched.runTo(30);restored.sched.runTo(70);
        check(load.ok() && restored.out.size()==1 && restored.out[0]==0x52,"snapshot preserves BREAK residual and interrupted status");
        Rig disabled(c);disabled.tx(0x61);disabled.tx(0x62);disabled.sched.runTo(10);disabled.wr(5,0x70);disabled.sched.runTo(100);
        disabled.wr(5,0x60);disabled.sched.runTo(130);
        check(disabled.out.empty() && !(disabled.rr(0)&4),"interrupted TSR resumes with TX disabled, TDR held");
        disabled.wr(5,0x68);disabled.sched.runTo(170);
        check(disabled.out.size()==1 && disabled.out[0]==0x62,"TX enable resumes intact held TDR");
        Rig late(c);late.tx(0x21);int64_t live=41;late.sched.setLiveClock([&]{return live;});late.wr(5,0x78);
        check(late.out.size()==1 && late.out[0]==0x21,"late MMIO BREAK preserves already completed byte");
        Rig lateQueued(c);lateQueued.tx(0x22);lateQueued.tx(0x23);int64_t queuedLive=41;
        lateQueued.sched.setLiveClock([&]{return queuedLive;});lateQueued.wr(5,0x78);
        check(lateQueued.out.size()==1 && lateQueued.out[0]==0x22,"late BREAK delivers first frame and starts pending TDR on original deadline");
        queuedLive=100;lateQueued.wr(5,0x68);lateQueued.sched.setLiveClock({});lateQueued.sched.runTo(139);
        check(lateQueued.out.size()==1 && (lateQueued.rr(1)&1),"late BREAK interrupts second frame with 39-cycle residual");
        Rig bothLate(c);bothLate.tx(0x24);bothLate.tx(0x25);int64_t bothLive=81;
        bothLate.sched.setLiveClock([&]{return bothLive;});bothLate.wr(5,0x78);
        check(bothLate.out.size()==2 && bothLate.out[0]==0x24 && bothLate.out[1]==0x25 && (bothLate.rr(1)&1),
              "late BREAK after two deadlines preserves both completed bytes");
        Rig loop(c);loop.s.setLoopback(true);loop.wr(3,0xe1);loop.wr(5,0xea);loop.tx(0x31);loop.tx(0x32);
        loop.sched.runTo(10);loop.wr(3,0xc1);loop.wr(5,0xf8);loop.wr(3,0xe1);loop.sched.runTo(100);
        check((loop.rr(0)&0x80) && !(loop.rr(0)&0x20),"BREAK visible in loopback even with CTS inactive");
        loop.wr(5,0xe8);loop.sched.runTo(130);
        check(!(loop.rr(0)&1) && !(loop.rr(0)&4),"interrupted frame omitted from RX and CTS retains TDR");
        loop.wr(5,0xea);loop.sched.runTo(170);
        check((loop.rr(0)&1),"CTS assertion resumes intact TDR after BREAK");
        Rig reset(c);reset.tx(0x71);reset.sched.runTo(10);reset.wr(5,0x78);reset.wr(9,c?0x40:0x80);reset.sched.runTo(100);
        check(reset.out.empty() && (reset.rr(0)&4),"reset clears suspended BREAK frame");
    }
    return failures==0;
}
#ifdef SCC_BREAK_STANDALONE
int main(){return testSccBreak()?0:1;}
#endif
