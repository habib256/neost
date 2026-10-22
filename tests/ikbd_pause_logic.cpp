// Pause firmware et octet SCI engagé : Hatari ikbd.c SCI_Set_Line_TX /
// Check_New_TDR. Le TDR n'est rechargé qu'en IDLE, jamais pendant DATA/STOP.
// Sémantique de $13 d'après la ROM 6301 ($F9AF, $FD68) : l'index d'écriture est
// mémorisé, les octets DÉJÀ en file sortent encore ; seuls les suivants attendent.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <initializer_list>
#include <vector>
namespace {
struct Rig {
    Scheduler sched; Mfp mfp; Ikbd ikbd{mfp}; int64_t live=0;
    Rig() {
        ikbd.setScheduler(&sched); sched.setLiveClock([this]{return live;});
        sched.setCallback(Scheduler::IKBD_RX,[this]{ikbd.onRxDeliver();});
        sched.setCallback(Scheduler::IKBD,[this]{ikbd.onResetResponse();});
        ikbd.write8(0xFFFC00,0x96);
    }
    void cmd(std::initializer_list<uint8_t> b) {for(auto v:b) ikbd.receiveSerialByte(v);}
    void advance(int64_t t){live=t;sched.runTo(t);}
    int ready(){return ikbd.read8(0xFFFC00)&1;}
    int data(){return ikbd.read8(0xFFFC02);}
    int64_t due(){auto t=sched.rawCyclesUntil(Scheduler::IKBD_RX);return t==INT64_MIN?-1:live+t;}
    void keys(){ikbd.keyEvent(0x39,true);ikbd.keyEvent(0x3A,true);ikbd.keyEvent(0x3B,true);}
};
}
int testIkbdPause() {
    int checks=0,failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want){++checks;if(got==want)return;++failures;std::fprintf(stderr,"[ikbd-pause] %s: %lld != %lld\n",label,(long long)got,(long long)want);};
    {
        Rig r;r.keys();r.advance(1000);r.cmd({0x13});
        eq("pause preserves active deadline",r.due(),10240);
        r.advance(10239);eq("active byte not early",r.ready(),0);
        r.advance(10240);eq("active byte survives pause",r.ready(),1);eq("active payload",r.data(),0x39);
        eq("pre-pause byte still starts",r.due(),20480);
        r.advance(20480);eq("pre-pause second byte",r.data(),0x3A);
        r.advance(30720);eq("pre-pause third byte",r.data(),0x3B);
        r.ikbd.keyEvent(0x3C,true);eq("byte queued during pause waits",r.due(),-1);
        r.advance(40000);r.cmd({0x13,0x00});eq("repeat pause and unknown command keep queue stopped",r.due(),-1);
        r.cmd({0x11});eq("resume dates next byte",r.due(),50240);
        r.advance(50240);eq("held byte after resume",r.data(),0x3C);eq("no duplicate byte",r.due(),-1);
    }
    {
        Rig r;r.keys();r.advance(1000);r.cmd({0x13});r.advance(2000);r.cmd({0x11});
        eq("resume during active byte preserves deadline",r.due(),10240);
        r.advance(10240);eq("early resume preserves active byte",r.data(),0x39);
        eq("early resume preserves following phase",r.due(),20480);
    }
    {
        Rig r;r.cmd({0x13});r.keys();r.advance(20000);eq("pause before start delivers nothing",r.ready(),0);eq("paused queue has no deadline",r.due(),-1);
        r.cmd({0x0B,2});eq("partial valid command cannot resume",r.due(),-1);
        r.cmd({2});eq("completed valid command resumes",r.due(),30240);
    }
    {
        // La réponse $1C était en file avant $13 : elle part après son attente.
        Rig r;r.cmd({0x1C});r.advance(1000);r.cmd({0x13});r.advance(7168);
        eq("pre-pause reply leaves after firmware wait",r.due(),7168+10240);eq("wait is not an active byte",r.ready(),0);
        r.advance(17408);eq("response header delivered",r.data(),0xFC);
    }
    {
        Rig r;r.ikbd.keyEvent(0x39,true);r.advance(1000);r.cmd({0x13});
        r.ikbd.keyEvent(0x3A,true);r.ikbd.keyEvent(0x3B,true);
        std::vector<uint8_t> bytes;auto save=StateArchive::saver(bytes);r.ikbd.serialize(save);r.sched.serialize(save);
        Rig copy;auto load=StateArchive::loader(bytes.data(),bytes.size());copy.ikbd.serialize(load);copy.sched.serialize(load);copy.live=copy.sched.now();
        eq("paused active state loads",load.ok(),1);copy.advance(10240);eq("restored active byte",copy.data(),0x39);eq("restored queue stays paused",copy.due(),-1);
        copy.cmd({0x11});copy.advance(20480);eq("restored queue order",copy.data(),0x3A);
    }
    {
        Rig r;r.ikbd.keyEvent(0x39,true);r.advance(1000);r.cmd({0x13});r.ikbd.keyEvent(0x3A,true);
        r.ikbd.write8(0xFFFC00,3);r.ikbd.write8(0xFFFC00,0x96);
        eq("ACIA master reset preserves SCI deadline",r.due(),10240);r.advance(10240);eq("ACIA master reset preserves SCI byte",r.data(),0x39);eq("master reset preserves firmware pause",r.due(),-1);
    }
    {
        Rig r;r.keys();r.advance(1000);r.cmd({0x80,1});r.advance(10240);
        eq("ROM boot preserves engaged byte",r.ready(),1);eq("ROM boot preserves engaged payload",r.data(),0x39);eq("ROM boot flushes queued bytes",r.due(),-1);
    }
    {
        Rig r;r.keys();r.advance(1000);r.cmd({0x13});r.ikbd.resetHw();eq("hardware reset cancels active byte",r.due(),-1);r.advance(10240);eq("hardware reset delivers no stale byte",r.ready(),0);
    }
    std::printf("[ikbd-pause] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_IKBD_PAUSE_TEST_MAIN
int main(){return testIkbdPause()?1:0;}
#endif
