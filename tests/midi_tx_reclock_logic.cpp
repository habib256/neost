// Diviseur changé pendant TSR : acia.c Write_CR redémarre le timer de bit.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <utility>
#include <vector>
int testMidiTxReclock() {
    int checks=0,failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) {++checks;if(got!=want){++failures;std::fprintf(stderr,"[midi-tx-reclock] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)want);}};
    constexpr int periods[]={16,256,1024};
    for(int old=0;old<3;++old) for(int fresh=0;fresh<3;++fresh) if(old!=fresh) for(bool brk:{false,true}) {
        Scheduler sched; Mfp mfp; MidiAcia midi(mfp); int64_t live=0;
        midi.setScheduler(&sched);sched.setLiveClock([&]{return live;});
        sched.setCallback(Scheduler::MIDI_TX,[&]{midi.onTxEmpty();});
        std::vector<std::pair<uint8_t,int64_t>> out;
        midi.setMidiSinkTimed([&](uint8_t b,int64_t t){out.emplace_back(b,t);});
        auto run=[&](int64_t t){live=t;sched.runTo(t);};
        midi.write8(4,uint8_t(0x34|old)); midi.write8(6,0xE1);
        run(periods[old]); midi.write8(6,0xE2);
        const int64_t change=4*periods[old]+periods[old]/2;
        run(change); midi.write8(4,uint8_t((brk?0x74:0x34)|fresh));
        const int64_t done=change+6*periods[fresh];
        eq("six fronts restants",sched.rawCyclesUntil(Scheduler::MIDI_TX),6*periods[fresh]);
        std::vector<uint8_t> saved;
        {auto ar=StateArchive::saver(saved);sched.serialize(ar);midi.serialize(ar);}
        run(done-1);eq("pas de sortie avant stop",out.size(),0);
        run(done);eq("sortie au stop recalé",out.size(),1);
        if(!out.empty()){eq("date sortie",out[0].second,done);eq("donnée mémorisée",out[0].first,0xE1);}
        eq("TDR encore plein",midi.read8(4)&2,0);
        if(brk) {
            eq("BREAK retient attente",sched.cyclesUntil(Scheduler::MIDI_TX),-1);
            run(done+5); midi.write8(4,uint8_t(0x34|fresh));
        }
        run(done+periods[fresh]);eq("TDR libre au front suivant",midi.read8(4)&0x82,0x82);
        run(done+10*periods[fresh]);eq("deuxième octet",out.size(),2);
        if(out.size()>1){eq("date deuxième",out[1].second,done+10*periods[fresh]);eq("donnée deuxième",out[1].first,0xE2);}
        {auto ar=StateArchive::loader(saved.data(),saved.size());sched.serialize(ar);midi.serialize(ar);eq("snapshot valide",ar.ok(),true);}
        live=change;out.clear();run(done);eq("snapshot date conservée",out.size(),1);
        midi.write8(4,3);run(done+20*periods[fresh]);eq("reset abandonne attente",out.size(),1);
    }
    std::fprintf(stderr,"[midi-tx-reclock] %d checks, %d failures\n",checks,failures);return failures;
}
#ifdef NEOST_MIDI_TX_RECLOCK_TEST_MAIN
int main(){return testMidiTxReclock()?1:0;}
#endif
