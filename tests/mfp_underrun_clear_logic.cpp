// MC68901UM §7.3.2 : une période TX complète avant effacement UE.
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
int testMfpUnderrunClear() {
    int checks=0,failures=0;
    auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::printf("  FAIL MFP UE clear %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    for(int data : {1,7,255}) for(int divide : {1,16}) for(int archive=0;archive<2;++archive) {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA29,divide==16?0x88:8);m.write8(0xFFFA25,data);m.write8(0xFFFA1D,1);s.cancel(Scheduler::TIMER_D);
        m.write8(0xFFFA07,2);m.write8(0xFFFA2D,1);m.write8(0xFFFA2F,0x55);
        // Référence : 11 bits (trame8N1 + période d'inhibition), Timer D /4 /2.
        const int64_t numerator=neost::pacing::kCpuHzInt*11*data*divide;
        const int64_t clear=(numerator+307199)/307200;
        const auto end=s.nextDue();s.runTo(end);
        eq("UE au front",m.read8(0xFFFA2D)&0x40,0x40);
        eq("UE lecture précoce conserve",m.read8(0xFFFA2D)&0x40,0x40);
        // Changement de débit puis UDR ne modifient pas l'inhibition engagée.
        m.write8(0xFFFA25,2);m.write8(0xFFFA29,8);s.cancel(Scheduler::TIMER_D);
        m.write8(0xFFFA2F,0x66);
        if(archive) {
            std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);s.serialize(out);m.serialize(out);
            m.write8(0xFFFA2D,0);auto in=StateArchive::loader(bytes.data(),bytes.size());s.serialize(in);m.serialize(in);
            eq("archive inhibition",in.ok(),1);
        }
        s.runTo(clear-1);eq("avant limite UE",m.read8(0xFFFA2D)&0x40,0x40);
        s.runTo(clear);eq("limite retourne UE",m.read8(0xFFFA2D)&0x40,0x40);
        eq("limite efface UE",m.read8(0xFFFA2D)&0x40,0);
        eq("lecture conserve IRQ",m.ipra&2,2);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA2D,1);m.write8(0xFFFA2F,1);s.runTo(s.nextDue());
        m.write8(0xFFFA2D,0);eq("TE0 efface sans délai",m.read8(0xFFFA2D)&0x40,0);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.write8(0xFFFA2D,1);m.write8(0xFFFA2F,1);s.runTo(s.nextDue());
        const auto nextClear=s.now()+(neost::pacing::kCpuHzInt*11+9599)/9600;
        // Le TSR suivant commence avec UE ancien encore posé.
        m.write8(0xFFFA2F,2);
        s.runTo((neost::pacing::kCpuHzInt*11+9599)/9600);
        eq("ancien UE retourné en vol",m.read8(0xFFFA2D)&0x40,0x40);
        eq("ancien UE effacé en vol",m.read8(0xFFFA2D)&0x40,0);
        std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);s.serialize(out);m.serialize(out);
        m.write8(0xFFFA2D,0);auto in=StateArchive::loader(bytes.data(),bytes.size());s.serialize(in);m.serialize(in);
        eq("archive nouveaux seuils indépendants",in.ok(),1);
        s.runTo(s.nextDue());
        eq("nouvel UE retourné",m.read8(0xFFFA2D)&0x40,0x40);
        eq("nouvel UE inhibition propre",m.read8(0xFFFA2D)&0x40,0x40);
        s.runTo(nextClear-1);eq("nouveau seuil moins un",m.read8(0xFFFA2D)&0x40,0x40);
        s.runTo(nextClear);eq("nouveau seuil retourne UE",m.read8(0xFFFA2D)&0x40,0x40);
        eq("nouveau seuil efface UE",m.read8(0xFFFA2D)&0x40,0);
    }
    {
        Mfp m;std::vector<uint8_t> bytes;auto out=StateArchive::saver(bytes);m.serialize(out);
        const int64_t forged=INT64_MAX;
        const size_t tail=sizeof(bool)+2*sizeof(int64_t)+sizeof(int);
        std::memcpy(bytes.data()+bytes.size()-tail-sizeof(int64_t),&forged,sizeof(forged));
        auto in=StateArchive::loader(bytes.data(),bytes.size());m.serialize(in);
        eq("deadline forgée rejetée",in.ok(),0);
    }
    std::printf("[selftest-mfp-underrun-clear] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_UNDERRUN_CLEAR_TEST_MAIN
int main(){return testMfpUnderrunClear()?1:0;}
#endif
