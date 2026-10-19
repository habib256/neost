// MC68901UM §7.3.2 et MAME tx_clock : B ne fonctionne qu'en asynchrone.
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
int testMfpBreak() {
    int checks=0,failures=0;auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::printf("  FAIL MFP BREAK %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    const int64_t frame=(neost::pacing::kCpuHzInt*10+9599)/9600;
    for(int b=0;b<2;++b) {
        Scheduler s;Mfp m;m.setScheduler(&s);std::vector<uint8_t> output;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});m.setSerialSink([&](uint8_t v){output.push_back(v);});
        m.write8(0xFFFA29,0x80); // le transport synchrone reste le repli8N1 explicite
        m.write8(0xFFFA07,6);m.write8(0xFFFA2D,b?9:1);m.write8(0xFFFA2F,0xA5);
        eq("sync B ignore TDR",m.read8(0xFFFA2D)&0x80,0x80);
        eq("sync B cadence identique",s.cyclesUntil(Scheduler::SERIAL_TX),frame);
        if(s.cyclesUntil(Scheduler::SERIAL_TX)>=0)s.runTo(frame);
        eq("sync octet livré",output.size(),1);
        if(!output.empty())eq("sync octet intact",output[0],0xA5);
        eq("sync UE non inhibé",m.read8(0xFFFA2D)&0x40,0x40);
        eq("sync IRQ underrun",m.ipra&2,2);
    }
    for(int archive=0;archive<2;++archive) {
        Scheduler s;Mfp m;m.setScheduler(&s);std::vector<uint8_t> output;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});m.setSerialSink([&](uint8_t v){output.push_back(v);});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA07,6);m.write8(0xFFFA2D,1);
        m.write8(0xFFFA2F,0x11);m.write8(0xFFFA2F,0x22);m.write8(0xFFFA2D,9);
        eq("async TSR en vol inchangé",s.cyclesUntil(Scheduler::SERIAL_TX),frame);
        s.runTo(frame);eq("async TSR finit",output.size(),1);eq("async TDR retenu",m.read8(0xFFFA2D)&0x80,0);
        eq("async intervalle BREAK sans transfert TDR",s.cyclesUntil(Scheduler::SERIAL_TX)>0,1);
        if(archive){std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);m.serialize(out);s.serialize(out);
            m.write8(0xFFFA2D,1);auto in=StateArchive::loader(bytes.data(),bytes.size());m.serialize(in);s.serialize(in);eq("archive BREAK TDR",in.ok(),1);}
        m.write8(0xFFFA2D,1);eq("release stop retient TDR",m.read8(0xFFFA2D)&0x80,0);
        s.runTo(s.nextDue());eq("après stop charge TDR",m.read8(0xFFFA2D)&0x80,0x80);
        s.runTo(s.nextDue());eq("release livre retenu",output.size(),2);
        if(output.size()==2)eq("TDR données intactes",output[1],0x22);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);std::vector<uint8_t> output;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});m.setSerialSink([&](uint8_t v){output.push_back(v);});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA2D,9);m.write8(0xFFFA2F,0x33);
        eq("async programme BREAK sans octet",s.cyclesUntil(Scheduler::SERIAL_TX)>0,1);
        m.write8(0xFFFA29,0x80);eq("entrée sync libère TDR",s.cyclesUntil(Scheduler::SERIAL_TX),frame);
        m.write8(0xFFFA2F,0x44);m.write8(0xFFFA29,0x88);
        eq("sortie sync préserve TSR",s.cyclesUntil(Scheduler::SERIAL_TX),frame);
        s.runTo(frame);eq("TSR mode changé livré",output.size(),1);
        if(!output.empty())eq("TSR données mode changé",output[0],0x33);
        eq("async retient second TDR",m.read8(0xFFFA2D)&0x80,0);
        eq("async cadence pendant attente sortie BREAK",s.cyclesUntil(Scheduler::SERIAL_TX)>0,1);
        m.write8(0xFFFA29,0x80);s.runTo(frame+frame);
        eq("retour sync livre TDR",output.size(),2);
        if(output.size()==2)eq("TDR mode changé intact",output[1],0x44);
    }
    std::printf("[selftest-mfp-break] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_BREAK_TEST_MAIN
int main(){return testMfpBreak()?1:0;}
#endif
