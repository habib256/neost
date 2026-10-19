// UM0109 pp.69/172 : Auto Echo déconnecte le TSR et prime sur Local Loopback.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccAutoEcho(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC AUTO ECHO FAIL: %s\n",name);}};
    for(int c=0;c<2;++c)for(int loop:{0,0x10}){
        Scheduler sched;Scc s;std::vector<uint8_t> out;s.setScheduler(&sched);s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
        sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
        sched.setCallback(c?Scheduler::SCC_RX_B:Scheduler::SCC_RX_A,[&]{s.onSerialEvent(c,false);});
        auto wr=[&](int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));};
        auto rr=[&](int r){s.write8(0x81+4*c,uint8_t(r));return s.read8(0x81+4*c);};
        wr(15,0);wr(4,4);wr(3,0xc1);wr(5,0x68);wr(11,0x50);wr(12,0);wr(13,0);wr(14,0xb|loop);wr(1,2);wr(9,8);
        s.write8(0x83+4*c,0x51);s.write8(0x83+4*c,0x52);sched.runTo(40);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);s.serialize(save);sched.serialize(save);
        sched.runTo(80);
        check(out.empty()&&!(rr(0)&1),"Auto Echo disconnects transmitted bytes from both external output and RX");
        check((rr(0)&4)&&(rr(1)&1)&&s.irqActive(),"disconnected transmitter still drains TSR and TDR and raises TX empty IRQ");
        auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);sched.serialize(load);sched.runTo(80);
        check(load.ok()&&out.empty()&&!(rr(0)&1)&&(rr(1)&1),"snapshot preserves Auto Echo TSR routing");
        wr(5,0x60);s.write8(0x83+4*c,0x53);sched.runTo(160);
        check(!(rr(1)&1)&&out.empty(),"Auto Echo does not bypass software TX enable for TSR shifting");
        wr(5,0x68);sched.runTo(200);check((rr(1)&1)&&out.empty()&&!(rr(0)&1),"re-enabled TSR completes without visible byte");
        s.setLoopback(true);wr(3,0xe1);wr(5,0x68);s.receiveByte(c,0x61);sched.runTo(240);
        check(bool(rr(0)&1)==bool(loop),"combined Auto Echo/Local Loopback ignores DCD, Auto Echo alone qualifies RX");
    }
    return failures==0;
}
#ifdef SCC_AUTO_ECHO_STANDALONE
int main(){return testSccAutoEcho()?0:1;}
#endif
