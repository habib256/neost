// Droits d'écriture USART : MAME REGISTER_RSR/REGISTER_TSR, MC68901UM §7.2.2/7.3.2.
#include "io/Mfp.hpp"
#include <cstdio>
int testMfpUsartControl() {
    int checks=0,failures=0;
    auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;if(failures<8)std::printf("  FAIL MFP control %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    for(int sync=0;sync<2;++sync) for(int v=0;v<256;++v) {
        Scheduler s;Mfp m;m.setScheduler(&s);m.write8(0xFFFA29,sync?0x80:0x88);
        m.write8(0xFFFA2B,uint8_t(v));
        eq("RSR droits écriture",m.read8(0xFFFA2B),(v&1)?v&(sync?0x0B:3):0);
        eq("RSR pas IRQ écriture",m.ipra,0);
    }
    { // F/S contrôlable en synchrone, non effacé par une écriture async.
        Mfp m;m.write8(0xFFFA29,0x80);m.write8(0xFFFA2B,0x0B);
        eq("F/S et strip sync",m.read8(0xFFFA2B),0x0B);
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA2B,1);
        eq("async préserve F/S existant",m.read8(0xFFFA2B),9);
        m.write8(0xFFFA2B,0);eq("RE0 efface RSR",m.read8(0xFFFA2B),0);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        m.write8(0xFFFA29,0x88);m.write8(0xFFFA2B,1);m.write8(0xFFFA07,0x18);
        m.receiveByte(0x55);m.receiveByte(0x66);s.runTo(s.nextDue());s.runTo(s.nextDue());
        const auto pending=m.ipra;
        m.write8(0xFFFA2B,1);eq("BF préservé, OE retenu avant UDR",m.read8(0xFFFA2B),0x81);
        m.write8(0xFFFA2B,0xFF);eq("pas faux statut",m.read8(0xFFFA2B),0x83);
        m.write8(0xFFFA2B,0xFE);eq("RE0 clear",m.read8(0xFFFA2B),0);
        eq("RE0 garde données",m.read8(0xFFFA2F),0x55);eq("RE0 garde IRQ",m.ipra,pending);
    }
    for(int v=1;v<256;v+=2) {
        Scheduler s;Mfp m;m.setScheduler(&s);m.write8(0xFFFA2D,uint8_t(v));
        eq("TSR statut readonly",m.read8(0xFFFA2D),0x80|(v&0x2F));
    }
    { // BE reste bas quand TDR plein ; écrire 1 ne le force pas à vide.
        Scheduler s;Mfp m;m.setScheduler(&s);m.write8(0xFFFA2F,0x55);
        m.write8(0xFFFA2D,0xD0);eq("TSR BE réel",m.read8(0xFFFA2D)&0xC0,0);
        m.write8(0xFFFA2D,1);eq("TE charge TDR",m.read8(0xFFFA2D)&0x80,0x80);
    }
    std::printf("[selftest-mfp-usart-control] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_USART_CONTROL_TEST_MAIN
int main(){return testMfpUsartControl()?1:0;}
#endif
