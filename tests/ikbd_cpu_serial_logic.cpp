// Transit CPU→IKBD : acia.c Write_TDR/Clock_TX et ikbd.c SCI_Get_Line_RX.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>
namespace {
struct Rig {
    Scheduler s; Mfp m; Ikbd k{m}; int64_t live = 0;
    Rig() {
        k.setScheduler(&s); s.setLiveClock([this] { return live; });
        s.setCallback(Scheduler::IKBD_TX, [this] { k.onTxEmpty(); });
        k.write8(0, 0x96);
    }
    void at(int64_t t) { live = t; s.runTo(t); }
    void send(uint8_t v) { k.write8(2, v); }
    int status() { return k.read8(0); }
    int64_t responseDue() { return s.rawCyclesUntil(Scheduler::IKBD_RX) < 0 ? -1 : s.now() + s.rawCyclesUntil(Scheduler::IKBD_RX); }
    std::vector<uint8_t> save() { std::vector<uint8_t> b; auto a=StateArchive::saver(b); k.serialize(a); s.serialize(a); return b; }
    bool load(const std::vector<uint8_t>& b) { auto a=StateArchive::loader(b.data(),b.size()); k.serialize(a); s.serialize(a); live=s.now(); return a.ok(); }
};
}
int testIkbdCpuSerial() {
    int failures=0, checks=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) { ++checks; if(got!=want) { ++failures; std::fprintf(stderr,"[ikbd-cpu-serial] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)want); } };
    {
        Rig r; r.live=100; r.send(0x1C);
        eq("TDRE falls without TIE",r.status()&2,0);
        eq("no immediate parser response",r.responseDue(),-1);
        r.at(1023); eq("TDR waits bit clock",r.status()&2,0);
        r.at(1024); eq("TDR empty at start",r.status()&2,2);
        eq("start not command arrival",r.responseDue(),-1);
        r.at(10239); eq("no early stop",r.responseDue(),-1);
        r.at(10240); eq("firmware delay starts at stop",r.responseDue(),17408);
    }
    {
        Rig r; r.send(0); r.at(1024); r.send(0x16); r.send(0x1C);
        eq("queued TDR busy",r.status()&2,0);
        r.at(10240); eq("overwrite not parsed early",r.responseDue(),-1);
        r.at(11264); eq("queued TDR moves to TSR",r.status()&2,2);
        r.at(20480); eq("latest queued byte wins",r.responseDue(),27648);
    }
    {
        Rig r; r.send(0); r.at(1300);
        eq("late start callback preserves stop grid",
           r.s.now() + r.s.rawCyclesUntil(Scheduler::IKBD_TX), 10240);
        r.send(0x1C); r.at(10300);
        eq("late stop preserves next start grid",
           r.s.now() + r.s.rawCyclesUntil(Scheduler::IKBD_TX), 11264);
        r.at(11400);
        eq("late queued start preserves stop grid",
           r.s.now() + r.s.rawCyclesUntil(Scheduler::IKBD_TX), 20480);
        r.at(20480); eq("queued byte delivered on original grid", r.responseDue(), 27648);
    }
    {
        Rig r; r.k.write8(0,0xB6); r.send(0x1C);
        eq("TIE busy IRQ low",r.status()&0x80,0);
        r.at(1024); eq("TIE IRQ on TDR to TSR",r.status()&0x80,0x80);
        r.k.write8(0,0x96); eq("TIE off IRQ low",r.status()&0x80,0);
        r.at(10240); eq("TIE off preserves transit",r.responseDue(),17408);
    }
    for (bool shifting : {false,true}) {
        Rig r; r.send(0x1C); r.at(shifting?1024:500);
        auto saved=r.save(); Rig restored; eq("load serial state",restored.load(saved),1);
        if(!shifting) restored.at(1024);
        restored.at(10240); eq("restored stop deadline",restored.responseDue(),17408);
    }
    {
        Rig r; r.send(0x1C); r.at(1024); r.k.write8(0,3); r.at(10240);
        eq("master reset discards active TSR",r.responseDue(),-1);
        eq("master reset restores TDRE",r.status()&2,2);
    }
    {
        Rig r; r.send(0x1C); r.k.resetHw();
        eq("hardware reset preserves pending TDR deadline",r.s.cyclesUntil(Scheduler::IKBD_TX),1024);
        eq("hardware reset preserves full TDR",r.status()&2,0);
    }
    std::printf("[ikbd-cpu-serial] %d checks, %d failures\n",checks,failures);
    return failures;
}
