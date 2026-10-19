// UM0109 pp.56–59 : WR15 contrôle les latches ; WR1 contrôle seulement leur IP.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccExtStatus(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC EXT STATUS FAIL: %s\n",name);}};
    for(int c=0;c<2;++c){
        Scheduler sched;Scc s;s.setScheduler(&sched);
        sched.setCallback(c?Scheduler::SCC_BRG_B:Scheduler::SCC_BRG_A,[&]{s.onBrgEvent(c);});
        auto wr=[&](int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));};
        auto rr=[&]{s.write8(0x81+4*c,0);return s.read8(0x81+4*c);};
        wr(15,0);s.setLoopback(true);wr(5,0x80);wr(15,0x20);wr(9,8);wr(1,0);
        wr(5,0x82);wr(5,0x80);
        check((rr()&0x20)&&!s.irqActive(),"CTS latch holds asserted edge while WR1 master disabled");
        wr(5,2);check((rr()&0x20)&&!(rr()&8)&&!s.irqActive(),"WR15-disabled DCD remains live while CTS is latched");wr(5,0x80);
        wr(1,1);check(s.irqActive(),"WR1 enable reveals existing latch");
        wr(1,0);check(!s.irqActive()&&(rr()&0x20),"WR1 disable clears IP but preserves latch");
        wr(1,1);wr(0,0x10);check(s.irqActive()&&!(rr()&0x20),"odd CTS transition during latch relatches on reset");
        wr(0,0x10);check(!s.irqActive(),"second reset opens stable CTS latch");
        wr(5,0x82);wr(5,0x80);wr(5,0x82);wr(0,0x10);
        check(!s.irqActive()&&(rr()&0x20),"even transitions during latch leave no extra pending condition");
        wr(15,0);wr(5,0x80);check(!(rr()&0x20)&&!s.irqActive(),"WR15 clear bypasses CTS latch");
        wr(15,0x20);wr(1,0);wr(5,0x82);wr(5,0x80);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);s.serialize(save);
        wr(0,0x10);auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);wr(1,1);
        check(load.ok()&&s.irqActive()&&(rr()&0x20),"snapshot preserves masked-master latch");
        wr(0,0x10);wr(0,0x10);wr(15,2);wr(1,0);wr(12,0);wr(13,0);wr(14,3);
        sched.runTo(2);sched.runTo(3);check(!(rr()&2)&&!s.irqActive(),"ZC pulse unlatched with WR1 disabled");
        wr(1,1);check(s.irqActive(),"ZC closes latch even when WR1 disabled");wr(0,0x10);
        check(!s.irqActive(),"expired ZC does not relatch on external reset");
    }
    return failures==0;
}
#ifdef SCC_EXT_STATUS_STANDALONE
int main(){return testSccExtStatus()?0:1;}
#endif
