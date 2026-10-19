// Table MC6850 (acia.c ACIA_Serial_Params), pont de sortie par octet.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <utility>
#include <vector>

int testMidiTxFormat() {
    int checks=0, failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) {
        ++checks;
        if(got!=want) { ++failures; std::fprintf(stderr,"[midi-tx-format] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)want); }
    };
    // Attendus indépendants : longueurs start+données+parité+stops de la table.
    constexpr int lengths[8]={11,11,10,10,11,10,11,11};
    constexpr int periods[3]={16,256,1024};
    for(int divider=0;divider<3;++divider) for(int format=0;format<8;++format) {
        Scheduler sched; Mfp mfp; MidiAcia midi(mfp);
        midi.setScheduler(&sched);
        sched.setCallback(Scheduler::MIDI_TX,[&] { midi.onTxEmpty(); });
        std::vector<std::pair<uint8_t,int64_t>> out;
        midi.setMidiSinkTimed([&](uint8_t b,int64_t t) { out.emplace_back(b,t); });
        midi.write8(4,uint8_t((format<<2)|divider|0x20));
        midi.write8(6,0xe5);
        eq("load delay",sched.cyclesUntil(Scheduler::MIDI_TX),periods[divider]);
        sched.runTo(periods[divider]-1); eq("TDRE before load",midi.read8(4)&2,0);
        sched.runTo(periods[divider]); eq("TDRE IRQ at load",midi.read8(4)&0x82,0x82);
        const int done=lengths[format]*periods[divider];
        sched.runTo(done-1); eq("not before final stop",out.size(),0);
        sched.runTo(done); eq("one completion",out.size(),1);
        eq("completion cycle",out.empty()?-1:out[0].second,done);
        eq("data width",out.empty()?-1:out[0].first,format<4?0x65:0xe5);
    }
    Scheduler sched; Mfp mfp; MidiAcia midi(mfp); int64_t live=0;
    midi.setScheduler(&sched);sched.setLiveClock([&] {return live;});
    sched.setCallback(Scheduler::MIDI_TX,[&] {midi.onTxEmpty();});
    sched.setCallback(Scheduler::MIDI_RX,[&] {midi.onRxPace();});
    std::vector<std::pair<uint8_t,int64_t>> out;
    midi.setMidiSinkTimed([&](uint8_t b,int64_t t) {out.emplace_back(b,t);});
    auto run=[&](int64_t t) {live=t;sched.runTo(t);};
    midi.write8(4,0x15);midi.write8(6,0xe1);run(256);
    midi.write8(6,0xe2);live=300;midi.write8(4,0x02); // /64 7E2, données/format TSR restent 8N1
    std::vector<uint8_t> saved;
    {auto ar=StateArchive::saver(saved);sched.serialize(ar);midi.serialize(ar);}
    run(9515);eq("active divider before reclocked stop",out.size(),0);
    run(9516);eq("active format with reclocked divider",out.size(),1);
    if(!out.empty())eq("active width stable",out[0].first,0xe1);
    run(10540);eq("pending loads new divider",midi.read8(4)&2,2);
    run(20780);eq("pending completes 7E2 /64",out.size(),2);
    if(out.size()>1){eq("pending width",out[1].first,0x62);eq("pending date",out[1].second,20780);}
    {auto ar=StateArchive::loader(saved.data(),saved.size());sched.serialize(ar);midi.serialize(ar);eq("snapshot format valid",ar.ok(),true);}
    live=300;out.clear();run(20780);eq("snapshot latched format completes both",out.size(),2);
    if(out.size()>1)eq("snapshot pending date",out[1].second,20780);
    constexpr int64_t tailShift=20780-13824; // garder toute la suite chronologique
    midi.write8(4,0x76);midi.write8(6,0xff);run(14000+tailShift);midi.write8(4,0x14);
    eq("break resumes /1 edge",sched.rawCyclesUntil(Scheduler::MIDI_TX),16);
    run(14160+tailShift);eq("break /1 output",out.size(),3);
    midi.write8(6,0xee);live=14165+tailShift;midi.write8(4,0x16);
    eq("pending divider reprogrammed",sched.rawCyclesUntil(Scheduler::MIDI_TX),1024);
    int polls=0;midi.setRxSource([&](uint8_t& b){++polls;b=0x90;return true;});
    live=14200+tailShift;midi.write8(4,0x00);
    eq("TX CR preserves RX deadline",sched.rawCyclesUntil(Scheduler::MIDI_RX),2525);
    run(16725+tailShift);eq("RX fixed MIDI pace",polls,1);
    midi.write8(6,0xaa);midi.write8(4,3);run(30000);
    eq("master reset aborts TX",sched.cyclesUntil(Scheduler::MIDI_TX),-1);
    std::fprintf(stderr,"[midi-tx-format] %d checks, %d failures\n",checks,failures);
    return failures;
}
