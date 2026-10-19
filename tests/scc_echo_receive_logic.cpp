// UM0109 pp.69/172 : pont RxD→TxD, limité aux octets hôtes async huit bits sans parité (un stop RX vérifié).
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccEchoReceive(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC ECHO RECEIVE FAIL: %s\n",name);}};
    struct Rig{
        Scheduler sched;Scc s;int c;std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel){s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            sched.setCallback(c?Scheduler::SCC_RX_B:Scheduler::SCC_RX_A,[&]{s.onSerialEvent(c,false);});
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            wr(15,0);wr(4,4);wr(3,0xc1);wr(5,0x60);wr(11,0x50);wr(12,0);wr(13,0);wr(14,0xb);}
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(int r=0){s.write8(0x81+4*c,uint8_t(r));return s.read8(0x81+4*c);}
        uint8_t pop(){return s.read8(0x83+4*c);}
    };
    for(int c=0;c<2;++c){
        Rig r(c);r.s.receiveByte(c,0x51);r.sched.runTo(39);check(r.out.empty(),"echo not before abstract RX stop");
        r.sched.runTo(40);check(r.out.size()==1&&r.out[0]==0x51&&(r.rr()&1)&&r.pop()==0x51,"echo follows RX deadline with TX disabled");
        check((r.rr()&4)&&!(r.rr(1)&1),"echo does not use or complete internal TSR");
        Rig disabled(c);disabled.wr(3,0xc0);disabled.s.receiveByte(c,0x61);disabled.sched.runTo(40);
        check(disabled.out.size()==1&&!(disabled.rr()&1),"RX disabled does not disconnect physical echo");
        Rig dcd(c);dcd.wr(3,0xe1);dcd.s.setLoopback(true);dcd.wr(5,0x60);dcd.s.receiveByte(c,0x62);dcd.sched.runTo(10);dcd.s.setLoopback(false);dcd.sched.runTo(40);
        check(dcd.out.size()==1&&!(dcd.rr()&1),"DCD inactive at frame start blocks RX but not echo, even if later restored");
        Rig drop(c);drop.s.receiveByte(c,0x63);drop.sched.runTo(10);drop.wr(3,0xc0);drop.wr(3,0xc1);drop.sched.runTo(40);
        check(drop.out.size()==1&&!(drop.rr()&1),"RX enable loss corrupts only receiver, not echo");
        Rig full(c);for(int b=0x70;b<0x75;++b)full.s.receiveByte(c,uint8_t(b));
        for(int t=40;t<=200;t+=40)full.sched.runTo(t);
        full.pop();full.pop();check(full.out.size()==5&&(full.rr(1)&0x20),"FIFO overflow never stops echo");
        Rig mode(c);mode.s.receiveByte(c,0x31);mode.s.receiveByte(c,0x32);mode.sched.runTo(10);mode.wr(14,3);mode.wr(14,0xb);
        mode.sched.runTo(40);check(mode.out.empty(),"mode change invalidates current echo frame even when restored");
        mode.sched.runTo(80);check(mode.out.size()==1&&mode.out[0]==0x32,"next queued frame takes mode at start");
        Rig lateEnable(c);lateEnable.wr(14,3);lateEnable.s.receiveByte(c,0x33);lateEnable.s.receiveByte(c,0x34);
        lateEnable.sched.runTo(10);lateEnable.wr(14,0xb);lateEnable.sched.runTo(40);
        check(lateEnable.out.empty(),"enabling Auto Echo mid-frame does not fabricate a complete character");
        lateEnable.sched.runTo(80);check(lateEnable.out.size()==1&&lateEnable.out[0]==0x34,"queued character starts with new Auto Echo mode");
        Rig txBreak(c);txBreak.wr(5,0x70);txBreak.s.receiveByte(c,0x35);txBreak.sched.runTo(40);
        check(txBreak.out.size()==1&&txBreak.out[0]==0x35,"internal TX break does not replace RxD echo");
        Rig format(c);format.s.receiveByte(c,0x41);format.sched.runTo(10);format.wr(4,7);format.wr(4,4);format.sched.runTo(40);
        check(format.out.empty(),"format change cannot fabricate intact echo");
        Rig clock(c);clock.s.receiveByte(c,0x42);clock.sched.runTo(10);clock.wr(11,0);clock.wr(11,0x50);clock.sched.runTo(40);
        check(clock.out.empty(),"RX clock reconfiguration invalidates intact echo");
        Rig twoStop(c);twoStop.wr(4,12);twoStop.s.receiveByte(c,0x43);twoStop.sched.runTo(39);
        check(twoStop.out.empty(),"TX two-stop selection does not delay receiver echo");twoStop.sched.runTo(40);
        check(twoStop.out.size()==1&&twoStop.out[0]==0x43,"echo completes after one checked RX stop");
        Rig snap(c);snap.wr(3,0xc0);snap.s.receiveByte(c,0x21);snap.s.receiveByte(c,0x22);snap.sched.runTo(10);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);snap.s.serialize(save);snap.sched.serialize(save);
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);restored.sched.serialize(load);
        restored.sched.runTo(40);restored.sched.runTo(80);
        check(load.ok()&&restored.out.size()==2&&restored.out[0]==0x21&&restored.out[1]==0x22&&!(restored.rr()&1),"snapshot restores frame eligibility and raw future queue");
        Rig shortRx(c);shortRx.wr(3,0x41);shortRx.s.receiveByte(c,0x11);shortRx.sched.runTo(100);
        check(shortRx.out.empty(),"short host frame is outside the known eight-bit echo contract");
        for(int wr4:{5,0}){Rig unknown(c);unknown.wr(4,wr4);unknown.s.receiveByte(c,0x11);unknown.sched.runTo(100);
            check(unknown.out.empty(),"unknown wire parity or synchronous framing is not echoed as intact byte");}
    }
    return failures==0;
}
#ifdef SCC_ECHO_RECEIVE_STANDALONE
int main(){return testSccEchoReceive()?0:1;}
#endif
