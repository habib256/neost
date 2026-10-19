// MC68901 UCR CLK : /1 ou /16, horloge Timer D /2 et changement au caractère suivant.
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
int testMfpClock() {
    int checks=0, failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) {
        ++checks; if(got!=want) { ++failures; std::printf("  FAIL MFP clock %s: %lld attendu %lld\n",label,(long long)got,(long long)want); }
    };
    auto duration=[](int baud,int half=20) { return (neost::pacing::kCpuHzInt*half+2*baud-1)/(2*baud); };
    // Débits connus indépendants du calcul interne, tous les prescalers, zéro=256.
    struct Rate { int ctrl,data,baud; };
    for (const auto r : {Rate{1,1,307200},Rate{1,2,153600},Rate{2,1,122880},
            Rate{3,1,76800},Rate{4,1,24576},Rate{5,1,19200},Rate{6,1,12288},
            Rate{7,1,6144},Rate{7,0,24},Rate{1,0,1200},Rate{5,160,120}})
    for(int tx=0;tx<2;++tx) for(int divide16=0;divide16<2;++divide16) {
        Scheduler s; Mfp m; m.setScheduler(&s);
        m.write8(0xFFFA29,divide16?0x88:8);
        m.write8(0xFFFA25,r.data); m.write8(0xFFFA1D,r.ctrl); s.cancel(Scheduler::TIMER_D);
        if(tx) { m.write8(0xFFFA2D,1); m.write8(0xFFFA2F,0x55); } else m.receiveByte(0x55);
        eq("débit connu",s.nextDue(),(neost::pacing::kCpuHzInt*20*(divide16?16:1)+2*r.baud-1)/(2*r.baud));
        std::vector<uint8_t> bytes; auto save=StateArchive::saver(bytes); m.serialize(save);
        auto load=StateArchive::loader(bytes.data(),bytes.size()); m.serialize(load);
        eq("archive débit valide",load.ok(),1);
        if(r.ctrl==7 && r.data==0 && divide16 && tx) {
            const int forged=819201;
            const size_t tail=sizeof(uint8_t)+sizeof(uint32_t)+4*sizeof(bool)+3*sizeof(uint8_t)
                +4*sizeof(int64_t)+sizeof(int)+sizeof(bool)+2*sizeof(int64_t)+sizeof(int);
            std::memcpy(bytes.data()+bytes.size()-tail-sizeof(int),&forged,sizeof(forged));
            auto invalid=StateArchive::loader(bytes.data(),bytes.size()); m.serialize(invalid);
            eq("facteur hors domaine rejeté",invalid.ok(),0);
        }
    }
    // Synchrone reste un transport hôte 8N1 /16 explicite ; 1,5 stop /1
    // n'est pas défini par le manuel, MAME le traite comme un stop.
    for (const int ucr : {0, 0x80, 0x10}) for(int tx=0;tx<2;++tx) {
        Scheduler s; Mfp m; m.setScheduler(&s);
        m.write8(0xFFFA29,ucr); m.write8(0xFFFA25,1); m.write8(0xFFFA1D,1); s.cancel(Scheduler::TIMER_D);
        if(tx) { m.write8(0xFFFA2D,1); m.write8(0xFFFA2F,1); } else m.receiveByte(1);
        eq("repli sync ou stop /1",s.nextDue(),duration(ucr==0x10?307200:19200));
    }
    for(int tx=0;tx<2;++tx) {
        Scheduler s; Mfp m; m.setScheduler(&s);
        s.setCallback(tx?Scheduler::SERIAL_TX:Scheduler::SERIAL_RX,[&] { if(tx)m.onSerialTxEvent(); else m.onSerialRxEvent(); });
        m.write8(0xFFFA29,0x88); m.write8(0xFFFA25,1); m.write8(0xFFFA1D,1); s.cancel(Scheduler::TIMER_D);
        if(tx) { m.write8(0xFFFA2D,1); m.write8(0xFFFA2F,1); m.write8(0xFFFA2F,2); }
        else { m.receiveByte(1); m.receiveByte(2); }
        const int64_t first=duration(19200);
        m.write8(0xFFFA29,8);
        eq("caractère en vol stable",s.nextDue(),first);
        s.runTo(first);
        // La fraction du caractère précédent reste dans la même base exacte.
        eq("caractère suivant /1",s.nextDue(),(neost::pacing::kCpuHzInt*170+307199)/307200);
    }
    // Banc long : 19 200/7 bauds, puis 307 200/11. Les valeurs de référence
    // comptent les fronts Timer D directement, sans passer par serialBaud().
    for(int tx=0;tx<2;++tx) {
        Scheduler s; Mfp m; m.setScheduler(&s);
        int delivered=0;
        s.setCallback(tx?Scheduler::SERIAL_TX:Scheduler::SERIAL_RX,[&] {
            if(tx) m.onSerialTxEvent(); else m.onSerialRxEvent();
            ++delivered;
        });
        m.write8(0xFFFA29,0x88); m.write8(0xFFFA25,7); m.write8(0xFFFA1D,1); s.cancel(Scheduler::TIMER_D);
        if(tx) { m.write8(0xFFFA2D,1); m.write8(0xFFFA2F,0); m.write8(0xFFFA2F,1); }
        else for(int i=0;i<2048;++i)m.receiveByte(uint8_t(i));
        int mismatches=0;
        int64_t numerator=0;
        for(int i=1;i<=2000;++i) {
            numerator += neost::pacing::kCpuHzInt*10*(i<=1000?112:11);
            const int64_t expected=(numerator+307199)/307200;
            if(s.nextDue()!=expected)++mismatches;
            if(i==1000) {
                // La nouvelle config ne modifie pas l'échéance en vol.
                m.write8(0xFFFA25,11); m.write8(0xFFFA29,8); s.cancel(Scheduler::TIMER_D);
                eq("reconfiguration en vol",s.nextDue(),expected);
            }
            if(i==1200) m.write8(0xFFFA1D,0); // garde le facteur effectif, pas les registres seuls
            if(i==1234) {
                std::vector<uint8_t> state; auto save=StateArchive::saver(state); s.serialize(save);m.serialize(save);
                s.runTo(s.nextDue()); --delivered; // perturbe les phases avant restauration
                auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);m.serialize(load);
                eq("restauration fraction non entière",load.ok(),1);
            }
            s.runTo(s.nextDue());
            if(tx)m.write8(0xFFFA2F,uint8_t(i+1));
        }
        eq("2000 caractères exacts",mismatches,0); eq("2000 livraisons",delivered,2000);
    }
    std::printf("[selftest-mfp-clock] %d checks, %d FAIL\n",checks,failures);
    return failures;
}
#ifdef NEOST_MFP_CLOCK_TEST_MAIN
int main() { return testMfpClock()?1:0; }
#endif
