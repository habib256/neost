// UM0109 pp.58–59 : qualification Sync/Hunt et Break/Abort selon le mode.
#include "io/Scc.hpp"
#include <cstdio>

bool testSccStatusModes(){
    int failures=0;auto check=[&](bool ok,const char* name){if(!ok){++failures;std::fprintf(stderr,"SCC STATUS MODES FAIL: %s\n",name);}};
    for(int c=0;c<2;++c){
        Scc s;auto wr=[&](int r,int v){s.write8(0x81+4*c,uint8_t(r));s.write8(0x81+4*c,uint8_t(v));};
        auto rr=[&]{s.write8(0x81+4*c,0);return s.read8(0x81+4*c);};
        wr(15,0);wr(14,0);wr(3,0xc1);wr(5,0x68);s.setLoopback(true);wr(9,8);wr(1,1);
        for(int mode:{4,0x30}){
            wr(15,0);wr(4,mode);wr(11,0x80);wr(5,0x68);wr(0,0x10);wr(0,0x10);wr(15,0x10);
            wr(5,0xe8);check(!(rr()&0x10)&&!s.irqActive(),"crystal forces SYNC low without interrupt on pin assertion");
            wr(5,0x68);check(!(rr()&0x10)&&!s.irqActive(),"crystal ignores both SYNC pin edges");
            wr(11,0);wr(5,0xe8);check((rr()&0x10)&&s.irqActive(),"without crystal SYNC pin is observable");
            wr(11,0x80);check(!(rr()&0x10),"crystal low overrides previously latched SYNC status");
            wr(0,0x10);wr(0,0x10);wr(11,0);wr(0,0x10);wr(0,0x10);
            check(rr()&0x10,"removing crystal restores current pin");
            wr(11,0x80);check(!(rr()&0x10)&&!s.irqActive(),"forcing a stable high SYNC low does not create a false edge IRQ");
            wr(15,0);wr(0,0x10);wr(0,0x10);
        }
        for(int mode:{0,0x10,0x30}){
            wr(15,0);wr(11,0);wr(4,mode);wr(5,0x68);wr(0,0x10);wr(0,0x10);wr(15,0x80);
            wr(5,0x78);check(!(rr()&0x80)&&!s.irqActive(),"BREAK is zero in synchronous non-SDLC modes");
            wr(5,0x68);wr(0,0x10);check(!s.irqActive(),"non-SDLC BREAK does not create pending edges");
        }
        for(int mode:{0,0x10,0x30}){
            wr(15,0);wr(11,0);wr(4,4);wr(5,0x68);wr(0,0x10);wr(0,0x10);wr(15,0x80);
            wr(5,0x78);wr(5,0x68);wr(4,mode);
            check(!(rr()&0x80),"changing mode forces low even with a previously latched BREAK");
            wr(0,0x10);wr(0,0x10);check(!s.irqActive(),"mode change discards invalid BREAK pending release without synthetic IRQ");
        }
        for(int mode:{4,0x20}){
            wr(15,0);wr(4,mode);wr(5,0x68);wr(0,0x10);wr(0,0x10);wr(15,0x80);
            wr(5,0x78);check((rr()&0x80)&&s.irqActive(),"async and SDLC retain existing byte-model status path");
            wr(15,0);wr(5,0x68);wr(0,0x10);wr(0,0x10);
        }
    }
    return failures==0;
}
#ifdef SCC_STATUS_MODES_STANDALONE
int main(){return testSccStatusModes()?0:1;}
#endif
