// UM0109 p.69 : Local Loopback sélectionne TX pour RX sans déconnecter TxD.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccLocalLoopback(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC LOCAL LOOPBACK FAIL: %s\n",name);}};
    struct Rig{
        Scheduler sched;Scc s;int c;std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel){s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            sched.setCallback(c?Scheduler::SCC_RX_B:Scheduler::SCC_RX_A,[&]{s.onSerialEvent(c,false);});
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            wr(15,0);wr(4,4);wr(3,0xc1);wr(5,0x68);wr(11,0x50);wr(12,0);wr(13,0);wr(14,0x13);}
        void wr(int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));}
        uint8_t rr(int r=0){s.write8(0x81+4*c,uint8_t(r));return s.read8(0x81+4*c);}
        void tx(int b){s.write8(0x83+4*c,uint8_t(b));}
        uint8_t pop(){return s.read8(0x83+4*c);}
    };
    for(int c=0;c<2;++c){
        Rig r(c);r.wr(1,0x12);r.wr(9,8);r.tx(0x51);r.sched.runTo(39);
        check(r.out.empty()&&!(r.rr()&1),"local TX appears only at completed character");r.sched.runTo(40);
        check(r.out.size()==1&&r.out[0]==0x51&&(r.rr()&1)&&r.pop()==0x51,"local loop delivers TX both to sink and internal receiver");
        check((r.rr(1)&1)&&r.s.irqActive(),"local transmitter preserves All Sent and TX empty IRQ");
        Rig input(c);input.s.receiveByte(c,0x61);input.sched.runTo(80);
        check(!(input.rr()&1)&&input.out.empty(),"external RxD is disconnected in local mode");
        Rig disabled(c);disabled.wr(3,0xc0);disabled.tx(0x62);disabled.sched.runTo(40);
        check(disabled.out.size()==1&&!(disabled.rr()&1),"RX software disable does not disconnect external TX");
        Rig plug(c);plug.s.setLoopback(true);plug.tx(0x63);plug.sched.runTo(40);
        check((plug.rr()&1)&&plug.pop()==0x63&&!(plug.rr()&1),"internal plus external plugs do not duplicate RX byte");
        Rig mode(c);mode.wr(14,3);mode.s.receiveByte(c,0x31);mode.sched.runTo(10);mode.wr(14,0x13);mode.wr(14,3);mode.sched.runTo(40);
        check(!(mode.rr()&1),"local mux interruption invalidates pending external receive frame");
        Rig fifo(c);fifo.wr(14,3);fifo.s.receiveByte(c,0x71);fifo.sched.runTo(40);
        fifo.s.receiveByte(c,0x72);fifo.sched.runTo(50);fifo.wr(14,0x13);fifo.wr(14,3);fifo.sched.runTo(80);
        check((fifo.rr()&1)&&fifo.pop()==0x71&&!(fifo.rr()&1),"local mux change preserves completed FIFO but discards interrupted external frame");
        Rig echo(c);echo.wr(14,0x1b);echo.s.receiveByte(c,0x32);echo.tx(0x33);echo.sched.runTo(40);
        check(echo.out.size()==1&&echo.out[0]==0x32&&(echo.rr()&1)&&echo.pop()==0x32&&!(echo.rr()&1),"Auto Echo priority keeps external RX selected and internal TX disconnected");
        Rig saved(c);saved.tx(0x41);saved.sched.runTo(10);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);saved.s.serialize(save);saved.sched.serialize(save);
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);restored.sched.serialize(load);restored.sched.runTo(40);
        check(load.ok()&&restored.out.size()==1&&restored.out[0]==0x41&&restored.pop()==0x41,"snapshot retains local output and input routing");
    }
    return failures==0;
}
#ifdef SCC_LOCAL_LOOPBACK_STANDALONE
int main(){return testSccLocalLoopback()?0:1;}
#endif
