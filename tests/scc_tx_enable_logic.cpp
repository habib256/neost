// UM0109 p.153 : TX Enable bas termine le caractère déjà parti.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccTxEnable() {
    int failures=0;
    auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC TX ENABLE FAIL: %s\n",name);}};
    struct Rig {
        Scheduler sched; Scc s; int c; std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel){
            s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            wr(15,0); // Mesurer CTS vivant, indépendamment des verrous de statut.
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            wr(4,4);wr(3,0xc1);wr(5,0x48);wr(11,0x50);wr(12,0);wr(13,0);wr(14,3);
        }
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(int r){s.write8(0x81+4*c,uint8_t(r));return s.read8(0x81+4*c);}
        void tx(uint8_t b){s.write8(0x83+4*c,b);}
        uint8_t pop(){return s.read8(0x83+4*c);}
    };
    for(int c=0;c<2;++c){
        Rig r(c);r.tx(0xff);r.sched.runTo(10);r.wr(5,0x60);r.tx(0xa5);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);r.s.serialize(save);
        r.sched.runTo(31);check(r.out.empty(),"disable retains initial deadline");r.sched.runTo(32);
        check(r.out.size()==1 && r.out[0]==0x3f,"active frame completes with latched mask");
        check(!(r.rr(1)&1) && !(r.rr(0)&4),"queued TDR keeps All Sent and TBE low");
        r.sched.runTo(100);check(r.out.size()==1,"TDR held while disabled");r.wr(5,0x68);
        r.sched.runTo(139);check(r.out.size()==1,"enable starts full next frame");r.sched.runTo(140);
        check(r.out.size()==2 && r.out[1]==0xa5 && (r.rr(1)&1),"TDR resumes with new format and All Sent");
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);
        restored.s.onSerialEvent(c,true);check(load.ok() && restored.out.size()==1 && restored.out[0]==0x3f,"snapshot restores active disabled frame");
        Rig idle(c);idle.wr(5,0x60);idle.tx(0x61);idle.sched.runTo(80);
        check(idle.out.empty(),"TSR preloaded with TX disabled does not start");idle.wr(5,0x68);idle.sched.runTo(120);
        check(idle.out.size()==1 && idle.out[0]==0x61,"preloaded TSR begins on enable");
        Rig one(c);one.tx(0x51);one.sched.runTo(10);one.wr(5,0x40);one.sched.runTo(32);
        check(one.out.size()==1 && (one.rr(1)&1),"single disabled frame reaches All Sent");
        Rig loop(c);loop.s.setLoopback(true);loop.wr(3,0xe1);loop.wr(5,0xca);loop.tx(0x31);loop.tx(0x32);
        loop.sched.runTo(10);loop.wr(5,0xc0);loop.sched.runTo(32);
        check(loop.pop()==0x31 && (loop.rr(0)&0x20),"deferred RTS stays asserted for held TDR");
        loop.wr(5,0xc8);loop.sched.runTo(64);
        check(loop.pop()==0x32 && !(loop.rr(0)&0x20),"RTS drops after resumed TDR completes");
        Rig cts(c);cts.s.setLoopback(true);cts.wr(3,0xc1);cts.wr(5,0xca);cts.tx(0x21);cts.tx(0x22);
        cts.sched.runTo(10);cts.wr(5,0xc0);cts.wr(3,0xe1);cts.sched.runTo(32);
        check(cts.pop()==0x21 && !(cts.rr(0)&0x20),"active frame finishes despite TXEN and CTS drop");
        cts.wr(5,0xc8);cts.sched.runTo(80);check(!(cts.rr(0)&4),"CTS still holds TDR after software enable");
        cts.wr(5,0xca);cts.sched.runTo(112);check(cts.pop()==0x22,"CTS assertion resumes held TDR");
        Rig irq(c);irq.wr(1,2);irq.wr(9,8);irq.tx(0x11);irq.tx(0x12);irq.sched.runTo(10);irq.wr(5,0x40);irq.sched.runTo(32);
        check(!irq.s.irqActive(),"disabled held TDR does not signal TX buffer empty");
        irq.wr(5,0x48);check(irq.s.irqActive() && (irq.rr(0)&4),"resumed TDR transfer signals buffer empty IRQ");
        Rig reset(c);reset.tx(0x71);reset.sched.runTo(10);reset.wr(9,c?0x40:0x80);reset.sched.runTo(80);
        check(reset.out.empty(),"channel reset cancels active frame");
    }
    return failures==0;
}
#ifdef SCC_TX_ENABLE_STANDALONE
int main(){return testSccTxEnable()?0:1;}
#endif
