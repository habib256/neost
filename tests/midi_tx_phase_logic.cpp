// ACIA Clock_TX IDLE consomme TDR au prochain front du timer déjà actif.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
int testMidiTxPhase() {
    int checks=0,failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::fprintf(stderr,"[midi-tx-phase] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)want);}};
    constexpr int periods[]={16,256,1024};
    for(int div=0;div<3;++div) for(bool snapshot:{false,true}) {
        Scheduler s;Mfp m;MidiAcia a(m);int64_t live=0;
        a.setScheduler(&s);s.setLiveClock([&]{return live;});
        s.setCallback(Scheduler::MIDI_TX,[&]{a.onTxEmpty();});
        std::vector<std::pair<uint8_t,int64_t>> out;
        a.setMidiSinkTimed([&](uint8_t b,int64_t at){out.emplace_back(b,at);});
        auto run=[&](int64_t at){live=at;s.runTo(at);};
        live=7;a.write8(4,div==2?0x15:0x16);
        live=37;a.write8(4,uint8_t(0x34|div));
        const int bit=periods[div];
        run(37+2*bit+5);a.write8(6,0x51);
        const int64_t edge=37+3*bit;
        eq("départ phase CR",s.nextDue(),edge);
        // Réécriture de TDR et TIE sans changer CR0..1 : ni octet ni phase perdus.
        a.write8(6,0x52);a.write8(4,uint8_t(0x14|div));
        eq("TDR plein sans TIE",a.read8(4)&0x82,0);
        eq("TIE conserve front",s.nextDue(),edge);
        if(snapshot) {
            std::vector<uint8_t> state;
            {auto ar=StateArchive::saver(state);s.serialize(ar);a.serialize(ar);}
            a.write8(4,3);
            {auto ar=StateArchive::loader(state.data(),state.size());s.serialize(ar);a.serialize(ar);eq("snapshot phase valide",ar.ok(),true);}
        }
        run(edge-1);eq("avant front",a.read8(4)&2,0);
        run(edge);eq("au front",a.read8(4)&2,2);
        run(edge+9*bit);eq("sortie",out.size(),1);
        if(!out.empty()){eq("date stop",out[0].second,edge+9*bit);eq("TDR écrasé",out[0].first,0x52);}
        // Un nouvel octet après une période de repos retrouve la même grille.
        run(edge+11*bit+3);a.write8(6,0x53);
        eq("repos conserve phase",s.nextDue(),edge+12*bit);
        // Changer le diviseur avec TDR plein redémarre le timer à now+nouvelle période.
        a.write8(4,uint8_t(0x34|((div+1)%3)));
        eq("CR recadence TDR",s.nextDue(),live+periods[(div+1)%3]);
        a.write8(4,3);eq("reset annule",s.cyclesUntil(Scheduler::MIDI_TX),-1);
    }
    {
        // Ancien état valide : le TSR peut terminer hors grille. Garder ce
        // stop mémorisé, puis recaler uniquement le prochain chargement TDR.
        Scheduler s;Mfp m;MidiAcia a(m);
        a.setScheduler(&s);s.setCallback(Scheduler::MIDI_TX,[&]{a.onTxEmpty();});
        std::vector<std::pair<uint8_t,int64_t>> out;
        a.setMidiSinkTimed([&](uint8_t b,int64_t at){out.emplace_back(b,at);});
        a.write8(4,0x35);a.write8(6,0x61);s.runTo(256);a.write8(6,0x62);
        std::vector<uint8_t> state;
        {auto ar=StateArchive::saver(state);s.serialize(ar);a.serialize(ar);}
        const int64_t oldStop=2565;
        std::memcpy(state.data()+state.size()-sizeof(oldStop),&oldStop,sizeof(oldStop));
        {auto ar=StateArchive::loader(state.data(),state.size());s.serialize(ar);a.serialize(ar);eq("ancien stop hors phase valide",ar.ok(),true);}
        s.runTo(oldStop);eq("ancien TSR conservé",out.size(),1);
        if(!out.empty())eq("ancien stop inchangé",out[0].second,oldStop);
        eq("attente recale prochain front",s.nextDue(),2816);
        s.runTo(2816);eq("TDR libéré sur grille",a.read8(4)&0x82,0x82);
        s.runTo(5120);eq("deuxième sortie resynchronisée",out.size(),2);
        if(out.size()>1)eq("deuxième stop sur grille",out[1].second,5120);
    }
    std::fprintf(stderr,"[midi-tx-phase] %d checks, %d failures\n",checks,failures);return failures;
}
#ifdef NEOST_MIDI_TX_PHASE_TEST_MAIN
int main(){return testMidiTxPhase()?1:0;}
#endif
