// Phase du diviseur à la sortie de BREAK : acia.c Write_CR / Set_Timers.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <utility>
#include <vector>

int testMidiTxControl() {
    int checks=0, failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) {
        ++checks; if(got!=want) { ++failures;
            std::fprintf(stderr,"[midi-tx-control] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)want); }
    };
    constexpr int periods[]={16,256,1024};
    for(int div=0;div<3;++div) for(uint8_t tx:{0x00,0x20,0x40}) {
        Scheduler sched; Mfp mfp; MidiAcia midi(mfp); int64_t live=0;
        midi.setScheduler(&sched); sched.setLiveClock([&] {return live;});
        sched.setCallback(Scheduler::MIDI_TX,[&] {midi.onTxEmpty();});
        std::vector<std::pair<uint8_t,int64_t>> out;
        midi.setMidiSinkTimed([&](uint8_t b,int64_t t) {out.emplace_back(b,t);});
        auto run=[&](int64_t t) {live=t;sched.runTo(t);};
        // Forcer un autre diviseur avant CR pour ancrer chacun des trois cas.
        live=7; midi.write8(4,div==2?0x15:0x16);
        live=37; midi.write8(4,uint8_t(0x74|div)); midi.write8(6,0x55);
        const int period=periods[div];
        const int64_t release=37+2*period+5, edge=37+3*period;
        run(release);
        std::vector<uint8_t> saved;
        {auto ar=StateArchive::saver(saved);sched.serialize(ar);midi.serialize(ar);}
        midi.write8(4,uint8_t(0x14|div|tx));
        eq("libération garde phase CR",sched.rawCyclesUntil(Scheduler::MIDI_TX),edge-release);
        run(edge-1); eq("TDR plein avant front",midi.read8(4)&2,0);
        run(edge); eq("TDR vide au front",midi.read8(4)&0x82,tx==0x20?0x82:2);
        run(edge+9*period); eq("octet émis",out.size(),1);
        if(!out.empty()) {eq("donnée",out[0].first,0x55);eq("date dernier stop",out[0].second,edge+9*period);}
        {auto ar=StateArchive::loader(saved.data(),saved.size());sched.serialize(ar);midi.serialize(ar);eq("snapshot valide",ar.ok(),true);}
        live=release; out.clear(); midi.write8(4,uint8_t(0x14|div|tx));
        eq("phase restaurée",sched.rawCyclesUntil(Scheduler::MIDI_TX),edge-release);
    }
    // Changer le diviseur pendant BREAK doit réancrer la phase immédiatement.
    Scheduler sched; Mfp mfp; MidiAcia midi(mfp); int64_t live=0;
    midi.setScheduler(&sched); sched.setLiveClock([&] {return live;});
    sched.setCallback(Scheduler::MIDI_TX,[&] {midi.onTxEmpty();});
    live=37; midi.write8(4,0x76); midi.write8(6,0x66);
    live=160; midi.write8(4,0x75);
    live=300; midi.write8(4,0x35);
    eq("diviseur changé en BREAK",sched.rawCyclesUntil(Scheduler::MIDI_TX),116);
    std::fprintf(stderr,"[midi-tx-control] %d checks, %d failures\n",checks,failures);
    return failures;
}
#ifdef NEOST_MIDI_TX_CONTROL_TEST_MAIN
int main() {return testMidiTxControl()?1:0;}
#endif
