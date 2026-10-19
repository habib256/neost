// MC68HC901 p7-8 (famille68901), MAME mc68901 : TXERR à chaque frontière BREAK, TSR inchangé, TE0 différé.
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
int testMfpAsyncBreak() {
    int checks=0,failures=0;auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;if(failures<8)std::printf("  FAIL MFP async BREAK %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    const int64_t hz=neost::pacing::kCpuHzInt;
    for(int archive=0;archive<2;++archive) {
        Scheduler s;Mfp m;m.setScheduler(&s);int output=0;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});m.setSerialSink([&](uint8_t){++output;});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA25,7);m.write8(0xFFFA1D,1);s.cancel(Scheduler::TIMER_D);
        m.write8(0xFFFA07,2);m.write8(0xFFFA2F,0x55);m.write8(0xFFFA2D,9);
        eq("intervalle BREAK programmé",s.cyclesUntil(Scheduler::SERIAL_TX),(hz*70+19199)/19200);
        if(s.cyclesUntil(Scheduler::SERIAL_TX)<0)continue;
        for(int i=1;i<=32;++i) {
            const auto expected=(hz*70*i+19199)/19200;
            eq("phase rationnelle BREAK",s.nextDue(),expected);s.runTo(expected);
            eq("IRQ frontière",m.ipra&2,2);eq("TSR pas UE ni END",m.read8(0xFFFA2D)&0x50,0);
            eq("TDR retenu pendant BREAK",m.read8(0xFFFA2D)&0x80,0);m.write8(0xFFFA0B,0xFD);
            if(archive && i==13) {std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);m.serialize(out);s.serialize(out);
                m.write8(0xFFFA2D,0);auto in=StateArchive::loader(bytes.data(),bytes.size());m.serialize(in);s.serialize(in);eq("archive BREAK",in.ok(),1);}
        }
        eq("pas octet BREAK inventé",output,0);
        m.write8(0xFFFA2D,0x20);eq("TE0 attend frontière",m.read8(0xFFFA2D)&0x10,0);
        s.runTo(s.nextDue());eq("TE0 END",m.read8(0xFFFA2D)&0x10,0x10);
        eq("TE0 pas UE",m.read8(0xFFFA2D)&0x40,0);eq("AT active RE",m.read8(0xFFFA2B)&1,1);
        eq("TE0 TDR retenu",m.read8(0xFFFA2D)&0x80,0);eq("TE0 arrête cadence",s.cyclesUntil(Scheduler::SERIAL_TX),-1);
    }
    for(int full=0;full<2;++full) {
        Scheduler s;Mfp m;m.setScheduler(&s);std::vector<uint8_t> output;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});m.setSerialSink([&](uint8_t b){output.push_back(b);});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,6);m.write8(0xFFFA2D,9);
        if(full)m.write8(0xFFFA2F,0xA5);
        s.runTo(s.nextDue());m.write8(0xFFFA0B,0xF9);
        const auto release=s.now();m.write8(0xFFFA2D,1);
        const auto mark=release+(hz+9599)/9600;
        eq("fin BREAK stop un bit",s.cyclesUntil(Scheduler::SERIAL_TX),mark-release);
        eq("fin BREAK ne livre rien",output.size(),0);
        std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);m.serialize(out);s.serialize(out);
        m.write8(0xFFFA2D,0);auto in=StateArchive::loader(bytes.data(),bytes.size());m.serialize(in);s.serialize(in);
        eq("archive stop BREAK",in.ok(),1);s.runTo(mark-1);eq("pas UE avant stop",m.read8(0xFFFA2D)&0x40,0);
        s.runTo(mark);
        if(full) {
            eq("stop charge TDR",m.read8(0xFFFA2D)&0x80,0x80);eq("stop IRQ TBE",m.ipra&6,4);
            s.runTo(s.nextDue());eq("TDR livré après stop",output.size(),1);
            if(!output.empty())eq("donnée TDR préservée",output[0],0xA5);
        } else {
            eq("vide après stop UE",m.read8(0xFFFA2D)&0x40,0x40);eq("UE stop IRQ",m.ipra&6,2);
            eq("UE stop inhibition un bit",m.read8(0xFFFA2D)&0x40,0x40);
        }
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,2);m.write8(0xFFFA2D,1);m.write8(0xFFFA2F,1);
        s.runTo(s.nextDue());m.write8(0xFFFA2D,9);m.write8(0xFFFA0B,0xFD);
        s.runTo(s.nextDue());eq("UE mémorisé inhibe BREAK IRQ",m.ipra&2,0);
        eq("lecture UE antérieur",m.read8(0xFFFA2D)&0x40,0x40);
        s.runTo(s.nextDue());eq("UE effacé autorise BREAK IRQ",m.ipra&2,2);
        m.reset();eq("reset annule BREAK",s.cyclesUntil(Scheduler::SERIAL_TX),-1);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA2D,9);m.write8(0xFFFA2F,0x55);
        m.write8(0xFFFA29,0x80);eq("synchrone annule intervalle BREAK",m.read8(0xFFFA2D)&0x80,0x80);
        s.runTo(s.nextDue());eq("synchrone reprend TDR",m.read8(0xFFFA2D)&0x40,0x40);
        std::vector<uint8_t> bytes;auto save=StateArchive::saver(bytes);m.serialize(save);
        const size_t tail=2*sizeof(uint8_t)+2*sizeof(int64_t)+sizeof(int)+2*sizeof(int64_t)
            +sizeof(bool)+2*sizeof(int64_t)+sizeof(int);
        bytes[bytes.size()-tail-1]=3;
        auto load=StateArchive::loader(bytes.data(),bytes.size());m.serialize(load);
        eq("kind invalide rejeté",load.ok(),0);
        bytes[bytes.size()-tail-1]=1;
        auto inconsistent=StateArchive::loader(bytes.data(),bytes.size());m.serialize(inconsistent);
        eq("kind actif sans busy rejeté",inconsistent.ok(),0);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,6);m.write8(0xFFFA2D,1);
        eq("BE antérieur vide",m.read8(0xFFFA2D)&0x80,0x80);
        m.write8(0xFFFA2D,9);eq("BREAK préserve BE antérieur",m.read8(0xFFFA2D)&0x80,0x80);
        eq("BREAK aucun nouvel TXEMPTY",m.ipra&4,0);
        m.write8(0xFFFA2F,0x55);eq("UDR pendant BREAK baisse BE",m.read8(0xFFFA2D)&0x80,0);
        s.runTo(s.nextDue());eq("BREAK garde TDR plein",m.read8(0xFFFA2D)&0x80,0);
        eq("frontière pas TXEMPTY",m.ipra&4,0);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);int64_t live=0;s.setLiveClock([&]{return live;});
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,2);m.write8(0xFFFA2D,9);
        const auto due=s.nextDue();live=due+100;m.write8(0xFFFA2D,1);
        eq("MMIO frontière échue servie",m.ipra&2,2);
        eq("MMIO stop ancré au live",s.liveNow()+s.rawCyclesUntil(Scheduler::SERIAL_TX),live+(hz+9599)/9600);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);int64_t live=0;s.setLiveClock([&]{return live;});
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,2);m.write8(0xFFFA2D,1);m.write8(0xFFFA2F,0x55);
        live=s.nextDue()+100;m.write8(0xFFFA2D,9);
        eq("BREAK ne masque pas UE déjà échue",m.read8(0xFFFA2D)&0x40,0x40);
        eq("BREAK conserve TXERR déjà échue",m.ipra&2,2);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);int64_t live=0;s.setLiveClock([&]{return live;});
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,4);m.write8(0xFFFA13,4);m.write8(0xFFFA2D,1);
        m.write8(0xFFFA2F,0x11);m.write8(0xFFFA0B,0xFB);m.write8(0xFFFA2F,0x22);
        const auto first=s.liveNow()+s.rawCyclesUntil(Scheduler::SERIAL_TX);
        live=first+100;m.write8(0xFFFA2D,9);
        eq("TDR échéance hors dispatch exacte",s.liveNow()+s.rawCyclesUntil(Scheduler::SERIAL_TX),(hz*20+9599)/9600);
        eq("TDR hors dispatch TXEMPTY",m.ipra&4,4);
        eq("TXEMPTY date passée visible CPU",m.irqPending(),1);
    }
    std::printf("[selftest-mfp-async-break] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_ASYNC_BREAK_TEST_MAIN
int main(){return testMfpAsyncBreak()?1:0;}
#endif
