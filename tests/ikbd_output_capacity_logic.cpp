// Les 1024 octets d'ikbd.h sont ceux EN ATTENTE dans Keyboard.Buffer.
// SCI engagé et RDR ACIA plein n'en occupent aucune place (ikbd.c:918-958).
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
namespace {
struct Rig {
    Scheduler sched; Mfp mfp; Ikbd ikbd{mfp}; int64_t live=0;
    Rig() {
        ikbd.setScheduler(&sched);sched.setLiveClock([this]{return live;});
        sched.setCallback(Scheduler::IKBD_RX,[this]{ikbd.onRxDeliver();});
        ikbd.write8(0xFFFC00,0x96);ikbd.setClock(26,10,18,12,34,56);
    }
    void cmd(std::initializer_list<uint8_t> bytes){for(auto b:bytes)ikbd.receiveSerialByte(b);}
    void advance(int64_t t){live=t;sched.runTo(t);}
    int data(){return ikbd.read8(0xFFFC02);}
    int64_t due(){auto d=sched.rawCyclesUntil(Scheduler::IKBD_RX);return d==INT64_MIN?-1:live+d;}
    uint32_t queued(){std::vector<uint8_t> bytes;auto a=StateArchive::saver(bytes);ikbd.serialize(a);uint32_t n=0;std::memcpy(&n,bytes.data(),sizeof(n));return n;}
    void keys(int n){for(int i=0;i<n;++i)ikbd.keyEvent(0x3B,true);}
    void fullRdr(){ikbd.keyEvent(0x39,true);advance(10240);}
    int next(){auto d=due();if(d<0)return -1;advance(d);return data();}
    bool drainKeys(int n){bool good=true;for(int i=0;i<n;++i)good=(next()==0x3B)&&good;return good&&due()==-1;}
    void froggies(){cmd({0x20,0,0x80,4,0x02,0x39,0xFF,0x95,0x22,0,0x80});for(int i=0;i<163;++i)cmd({0});cmd({0xAA,0x0A,0xF0,0xB8});}
};
}
int testIkbdOutputCapacity(){
    int checks=0,failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want){++checks;if(got==want)return;++failures;std::fprintf(stderr,"[ikbd-output-capacity] %s: %lld != %lld\n",label,(long long)got,(long long)want);};
    {
        Rig r;r.fullRdr();r.cmd({0x13});r.keys(1025);
        eq("RDR full leaves all 1024 firmware slots",r.queued(),1024);
        eq("paused full queue has no active SCI",r.due(),-1);
        eq("queued writes preserve unread RDR",r.data(),0x39);r.cmd({0x11});
        eq("firmware full drops only excess byte",r.drainKeys(1024),1);
    }
    {
        Rig r;r.fullRdr();r.ikbd.keyEvent(0x3A,true);r.cmd({0x13});r.keys(1025);
        eq("RDR and active SCI leave 1024 firmware slots",r.queued(),1024);
        r.advance(20480);eq("SCI overrun preserves unread RDR",r.data(),0x39);
        eq("SCI overrun becomes visible downstream",r.ikbd.read8(0xFFFC00)&0x20,0x20);
        eq("SCI completion leaves firmware untouched under pause",r.queued(),1024);
        r.cmd({0x11});eq("overrun does not consume queued bytes",r.drainKeys(1024),1);
    }
    {
        Rig r;r.fullRdr();r.cmd({0x13});r.keys(1017);r.cmd({0x1C});
        eq("seven byte reply fits exactly despite full RDR",r.queued(),1024);
        r.data();r.advance(17408);
        bool good=true;for(int i=0;i<1017;++i)good=(r.next()==0x3B)&&good;
        eq("accepted packet follows all earlier keys",good,1);
        const uint8_t expected[]={0xFC,0x26,0x10,0x18,0x12,0x34,0x56};
        for(auto b:expected)eq("entire clock packet retained",r.next(),b);
        eq("exactly one packet",r.due(),-1);
    }
    {
        Rig r;r.fullRdr();r.cmd({0x13});r.keys(1018);r.cmd({0x1C});r.data();
        eq("insufficient room rejects whole response",r.drainKeys(1018),1);
    }
    {
        Rig r;r.froggies();r.cmd({1});r.advance(10240);
        for(int i=0;i<1026;++i)r.cmd({1});
        eq("custom raw pushes respect firmware capacity only",r.queued(),1024);
        r.advance(20480);eq("custom overrun preserves old RDR",r.data(),0);
        bool good=true;for(int i=0;i<1024;++i)good=(r.next()==0)&&good;
        eq("custom pushes keep exactly 1024 waiting bytes",good&&r.due()==-1,1);
    }
    {
        Rig r;r.cmd({0x13});r.keys(1024);
        std::vector<uint8_t> bytes;auto save=StateArchive::saver(bytes);r.ikbd.serialize(save);
        // Archive structurellement complète : un octet de file supplémentaire,
        // puis tous les champs décalés d'autant, sans dépendre d'une troncature.
        bytes.insert(bytes.begin()+sizeof(uint32_t),uint8_t(0));
        uint32_t n=1025;std::memcpy(bytes.data(),&n,sizeof(n));
        Rig forged;auto load=StateArchive::loader(bytes.data(),bytes.size());forged.ikbd.serialize(load);
        eq("oversized firmware queue state rejected",load.ok(),0);
    }
    std::printf("[ikbd-output-capacity] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_IKBD_OUTPUT_CAPACITY_TEST_MAIN
int main(){return testIkbdOutputCapacity()?1:0;}
#endif
