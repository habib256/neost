// UM0109 p.94/p.153 : encodage 1–5 bits ; format figé au chargement du TSR.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>

bool testSccTxFormat() {
    int failures=0;
    auto check=[&](bool ok,const char* name) { if(!ok) {++failures; std::fprintf(stderr,"SCC TX FORMAT FAIL: %s\n",name);} };
    struct Rig {
        Scheduler sched; Scc s; int c; std::vector<uint8_t> out;
        explicit Rig(int channel):c(channel) {
            s.setScheduler(&sched); s.setSerialSink([&](int,uint8_t b){out.push_back(b);});
            sched.setCallback(c?Scheduler::SCC_TX_B:Scheduler::SCC_TX_A,[&]{s.onSerialEvent(c,true);});
            wr(4,4); wr(5,0x68); wr(11,0x50); wr(12,0); wr(13,0); wr(14,3);
        }
        void wr(int reg,int v){s.write8(0x81+4*c,uint8_t(reg));s.write8(0x81+4*c,uint8_t(v));}
        void tx(uint8_t b){s.write8(0x83+4*c,b);}
    };
    struct Format {uint8_t wr5,byte,value;int bits;};
    const Format formats[]={{8,0xf1,1,1},{8,0xe3,3,2},{8,0xc5,5,3},{8,0x8b,11,4},
        {8,0x15,21,5},{0x48,0xff,0x3f,6},{0x28,0xff,0x7f,7},{0x68,0xff,0xff,8}};
    for(int c=0;c<2;++c) {
        for(const auto& f:formats) {
            Rig r(c);r.wr(5,f.wr5);r.tx(f.byte);
            const int due=4*(f.bits+2);r.sched.runTo(due-1);
            check(r.out.empty(),"character not emitted before final stop");r.sched.runTo(due);
            check(r.out.size()==1 && r.out[0]==f.value,"short payload and duration follow encoded length");
        }
        Rig r(c);r.tx(0xff);r.tx(0xff);r.sched.runTo(10);r.wr(5,0x48);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);r.s.serialize(save);
        r.sched.runTo(40);check(r.out.size()==1 && r.out[0]==0xff,"active TSR keeps eight-bit payload");
        r.sched.runTo(71);check(r.out.size()==1,"queued TDR starts complete six-bit frame");
        r.sched.runTo(72);check(r.out.size()==2 && r.out[1]==0x3f,"queued TDR takes new format at TSR load");
        Rig shortSaved(c); shortSaved.wr(5,8); shortSaved.tx(0xe3); shortSaved.wr(5,0x68);
        std::vector<uint8_t> shortState;auto shortSave=StateArchive::saver(shortState);shortSaved.s.serialize(shortSave);
        Rig shortRestored(c);auto shortLoad=StateArchive::loader(shortState.data(),shortState.size());shortRestored.s.serialize(shortLoad);
        shortRestored.s.onSerialEvent(c,true);
        check(shortLoad.ok() && shortRestored.out.size()==1 && shortRestored.out[0]==3,"snapshot preserves encoded short payload after WR5 change");
        Rig parity(c);parity.wr(4,0x0d);parity.wr(5,8);parity.tx(0xf1);
        parity.sched.runTo(19);check(parity.out.empty(),"short format retains parity and stop duration");
        parity.sched.runTo(20);check(parity.out.size()==1 && parity.out[0]==1,"short format parity/two-stop frame completes");
        Rig restored(c);auto load=StateArchive::loader(state.data(),state.size());restored.s.serialize(load);
        restored.s.onSerialEvent(c,true);
        check(load.ok() && restored.out.size()==1 && restored.out[0]==0xff,"snapshot keeps active TSR format");
    }
    return failures==0;
}
#ifdef SCC_TX_FORMAT_STANDALONE
int main(){return testSccTxFormat()?0:1;}
#endif
