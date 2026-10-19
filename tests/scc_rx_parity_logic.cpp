// UM0109 WR4 p.152 : parité reçue visible dans RR8 si RX < 8 bits.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccRxParity(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC RX PARITY FAIL: %s\n",name);}};
    for(int c=0;c<2;++c){
        Scc s;auto wr=[&](int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));};
        auto pop=[&]{return s.read8(0x83+4*c);};
        const int rxRegs[]={0,0x80,0x40,0xc0},txRegs[]={0,0x40,0x20,0x60};
        for(int bits=5;bits<=8;++bits)for(int even=0;even<2;++even)for(int byte:{0,1}){
            wr(4,5+2*even);wr(3,rxRegs[bits-5]|1);wr(5,txRegs[bits-5]|8);wr(14,0x10);
            s.write8(0x83+4*c,uint8_t(byte));
            const int parity=(byte?1:0)^(even?0:1);
            const uint8_t expected=bits==8?uint8_t(byte):uint8_t(byte|(parity<<bits)|(0xffu<<(bits+1)));
            check(pop()==expected,"local loopback exposes actual parity and marks only unused bits");
        }
        wr(4,4);wr(3,1);wr(5,8);s.write8(0x83+4*c,0);check(pop()==0xe0,"parity disabled leaves high bits unused");
        wr(4,7);s.receiveByte(c,0);check(pop()==0xe0,"host byte API does not invent unavailable wire parity");
        s.setLoopback(true);wr(14,0);wr(4,7);wr(3,1);wr(5,8);s.write8(0x83+4*c,0);
        wr(3,0xc1);check(pop()==0xc0,"external loopback carries known parity and FIFO keeps received format");
        s.setLoopback(false);
        Scheduler sched;Scc timed;timed.setScheduler(&sched);
        sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{timed.onSerialEvent(c,true);});
        auto tw=[&](int r,int v){timed.write8(0x81+4*c,uint8_t(r));timed.write8(0x81+4*c,uint8_t(v));};
        tw(4,7);tw(3,1);tw(5,8);tw(11,0x50);tw(12,0);tw(13,0);tw(14,0x13);
        timed.write8(0x83+4*c,0);sched.runTo(10);tw(4,5);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);timed.serialize(save);sched.serialize(save);
        sched.runTo(32);check(timed.read8(0x83+4*c)==0xc0,"TX parity bit is latched before WR4 polarity changes");
        auto load=StateArchive::loader(state.data(),state.size());timed.serialize(load);sched.serialize(load);sched.runTo(32);
        check(load.ok()&&timed.read8(0x83+4*c)==0xc0,"snapshot preserves known TX parity");
    }
    return failures==0;
}
#ifdef SCC_RX_PARITY_STANDALONE
int main(){return testSccRxParity()?0:1;}
#endif
