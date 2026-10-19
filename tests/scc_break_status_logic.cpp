// UM0109 p.58 : les deux fronts BREAK sont garantis malgré un autre statut verrouillé.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccBreakStatus(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC BREAK STATUS FAIL: %s\n",name);}};
    for(int c=0;c<2;++c)for(int master=0;master<2;++master){
        Scc s;auto wr=[&](int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));};
        auto rr=[&]{s.write8(0x81+4*c,0);return s.read8(0x81+4*c);};
        wr(15,0);s.setLoopback(true);wr(5,0x82);wr(3,0xc1);wr(4,4);wr(14,0);wr(15,0xa0);wr(1,master);wr(9,8);
        wr(5,0x80);wr(5,0x90);wr(5,0x80);
        check(!(rr()&0x80)&&s.irqActive()==bool(master),"CTS captures initial image before both BREAK edges");
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);s.serialize(save);
        wr(0,0x10);check((rr()&0x80)&&s.irqActive()==bool(master),"first reset reveals captured BREAK assertion");
        wr(0,0x10);check(!(rr()&0x80)&&s.irqActive()==bool(master),"second reset reveals captured BREAK release");
        wr(0,0x10);check(!s.irqActive(),"third reset opens stable latch without repeated synthetic edges");
        auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);wr(1,1);
        wr(0,0x10);check(load.ok()&&(rr()&0x80)&&s.irqActive(),"snapshot preserves both pending BREAK transitions");
        wr(0,0x10);check(!(rr()&0x80)&&s.irqActive(),"snapshot preserves pending release");wr(0,0x10);
        wr(5,0x82);wr(0,0x10);wr(5,0x80); // CTS ferme de nouveau l'image basse.
        for(int n=0;n<20;++n){wr(5,0x90);wr(5,0x80);}
        for(int n=0;n<10;++n)rr();
        wr(0,0x10);check(rr()&0x80,"repeated BREAK pulses coalesce to one assertion");
        for(int n=0;n<10;++n)rr();
        wr(0,0x10);check(!(rr()&0x80)&&s.irqActive(),"repeated BREAK pulses coalesce to one release");
        wr(0,0x10);check(!s.irqActive(),"ordinary RR0 reads never regenerate serviced edges");
        wr(15,0x20);wr(5,0x82);wr(5,0x92);wr(5,0x82);wr(0,0x10);
        check(!s.irqActive(),"WR15 BREAK mask prevents queued transitions");
        wr(15,0xa0);wr(5,0x80);wr(5,0x90);wr(5,0x80);wr(9,c?0x40:0x80);wr(1,1);wr(0,0x10);
        check(!(rr()&0x80)&&!s.irqActive(),"channel reset clears pending BREAK transitions");
    }
    return failures==0;
}
#ifdef SCC_BREAK_STATUS_STANDALONE
int main(){return testSccBreakStatus()?0:1;}
#endif
