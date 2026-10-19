// RE : désactivation immédiate du récepteur (MAME REGISTER_RSR/rc_w).
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
int testMfpRxEnable() {
    int checks=0,failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) { ++checks;if(got!=want){++failures;std::printf("  FAIL MFP RX enable %s: %lld attendu %lld\n",label,(long long)got,(long long)want);} };
    for(int enabledIrq=0;enabledIrq<2;++enabledIrq) { // Le buffer matériel conserve ses données, mais BF/erreurs sont effacés.
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        m.write8(0xFFFA2B,1);m.write8(0xFFFA07,enabledIrq?0x18:0);m.receiveByte(0x55);m.receiveByte(0x66);
        s.runTo(s.nextDue());s.runTo(s.nextDue());const auto pending=m.ipra&0x18;
        m.write8(0xFFFA2B,0xFE);
        eq("RSR désactivé effacé",m.read8(0xFFFA2B),0);
        eq("données RDR conservées",m.read8(0xFFFA2F),0x55);
        m.write8(0xFFFA2F,0xA5); // TX et RX ont deux registres distincts.
        eq("UDR vide persistant",m.read8(0xFFFA2F),0x55);
        eq("IPR indépendant conservé",m.ipra&0x18,pending);
        m.write8(0xFFFA2B,1);m.receiveByte(0x77);s.runTo(s.nextDue());
        eq("nouveau buffer sans overrun",m.read8(0xFFFA2B)&0xC0,0x80);eq("nouvelle donnée",m.read8(0xFFFA2F),0x77);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        m.write8(0xFFFA2B,1);m.receiveByte(0x33);m.receiveByte(0x44);
        const auto due=s.nextDue();s.runTo(due/2);m.write8(0xFFFA2B,0);
        eq("désactivation garde échéance fil",s.nextDue(),due);
        eq("désactivation garde file fil",m.hostRxPending(),2);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);s.serialize(save);m.serialize(save);
        s.runTo(due);auto load=StateArchive::loader(state.data(),state.size());s.serialize(load);m.serialize(load);
        eq("archive RX désactivé",load.ok(),1);eq("archive échéance",s.nextDue(),due);
        s.runTo(due);eq("RE coupé pas livraison",m.read8(0xFFFA2B)&0xC0,0);
        eq("file écoulée",m.hostRxPending(),1);
    }
    std::printf("[selftest-mfp-rx-enable] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_RX_ENABLE_TEST_MAIN
int main(){return testMfpRxEnable()?1:0;}
#endif
