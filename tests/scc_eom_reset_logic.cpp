// UM0109 WR0 p.139 et RR0 p.176 : reset EOM indépendant de la commande/pointeur.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccEomReset(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC EOM RESET FAIL: %s\n",name);}};
    for(int c=0;c<2;++c){
        Scc s;auto control=[&](int v){s.write8(0x81+4*c,uint8_t(v));};
        auto wr=[&](int r,int v){control(r);control(v);};
        auto rr=[&](int r=0){control(r);return s.read8(0x81+4*c);};
        wr(15,0);wr(1,1);wr(9,8);
        for(int mode:{0,0x10,0x20,0x30,4}){
            wr(4,4);wr(4,mode);wr(15,0x40);control(0x10);control(0x10);
            check(rr()&0x40,"reset or async setup provides EOM baseline");
            control(0x40);control(0x80);check(rr()&0x40,"CRC initializer codes do not reset EOM");
            control(0xc0);check(!(rr()&0x40)&&!s.irqActive(),"EOM reset falling edge does not close external latch");
            wr(15,0);
        }
        control(0xcc);control(0x2a);check(rr(12)==0x2a,"high CRC command combines with Point High register selection");
        control(0xc4);control(4);check(rr()&0x40,"high CRC command combines with low register selection");
        wr(15,0);s.setLoopback(true);wr(5,0x82);wr(15,0x60);wr(5,0x80);
        check(s.irqActive()&&(rr()&0x40),"CTS closes external image while EOM high");
        control(0xc0);check(s.irqActive()&&(rr()&0x40),"reset EOM preserves preexisting external latch image");
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);s.serialize(save);
        control(0xd0);check(!s.irqActive()&&!(rr()&0x40),"combined EOM and external reset exposes cleared underlying EOM");
        auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);control(0x10);
        check(load.ok()&&!s.irqActive()&&!(rr()&0x40),"snapshot retains independent raw EOM and external image");
        wr(15,0);wr(4,4);wr(1,2);s.write8(0x83+4*c,0x51);
        check(s.irqActive(),"TX empty interrupt armed before combined command");
        control(0xe8);check(!s.irqActive()&&!(rr()&0x40),"high EOM reset combines with low TX interrupt reset");
    }
    return failures==0;
}
#ifdef SCC_EOM_RESET_STANDALONE
int main(){return testSccEomReset()?0:1;}
#endif
