// MC68HC901 §5.1.2/5.1.3, Hatari psg.c389 et mfp.c GPIP_Update_Interrupt.
#include "io/Mfp.hpp"
#include <cstdio>
int testMfpGpipLines() {
    int checks=0,failures=0;auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;if(failures<8)std::printf("  FAIL MFP GPIP %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    for(int rising=0;rising<2;++rising)for(int output=0;output<2;++output)for(int enabled=0;enabled<2;++enabled){
        Mfp m;m.write8(0xFFFA01,0);m.setBusyLine(rising);m.write8(0xFFFA03,rising);m.write8(0xFFFA05,output);m.write8(0xFFFA09,enabled);
        m.setBusyLine(!rising);eq("BUSY front sélectionné",m.iprb&1,enabled&&!output);
        eq("BUSY lecture pin/latch",m.read8(0xFFFA01)&1,output?0:rising);
        m.write8(0xFFFA0D,0xFE);m.setBusyLine(!rising);eq("BUSY même niveau",m.iprb&1,0);
        m.setBusyLine(rising);eq("BUSY front opposé",m.iprb&1,0);
        m.setBusyLine(!rising);eq("BUSY front suivant",m.iprb&1,enabled&&!output);
    }
    for(int pin=0;pin<2;++pin){
        Mfp m;m.setBusyLine(!pin);m.write8(0xFFFA03,!pin);m.write8(0xFFFA09,1);
        m.write8(0xFFFA03,pin);eq("AER produit front XOR",m.iprb&1,1);
        m.write8(0xFFFA0D,0xFE);m.write8(0xFFFA03,pin);eq("AER identique sans front",m.iprb&1,0);
        m.write8(0xFFFA05,1);m.write8(0xFFFA05,0);eq("DDR seul pas front",m.iprb&1,0);
    }
    for(int color=0;color<2;++color)for(int output=0;output<2;++output){
        Mfp m;m.setColorMonitor(color);m.setHasDmaSound(true);m.write8(0xFFFA03,color?0:0x80);
        m.write8(0xFFFA05,output?0x80:0);m.write8(0xFFFA07,0x80);m.setXsintLine(true);
        eq("XSINT DDR et XOR",m.ipra&0x80,output?0:0x80);
        m.write8(0xFFFA0B,0x7F);m.setXsintLine(true);eq("XSINT niveau répété",m.ipra&0x80,0);
    }
    for(int rising=0;rising<2;++rising){
        Mfp m;m.write8(0xFFFA03,rising?0x10:0);m.write8(0xFFFA05,0x10);
        m.write8(0xFFFA1F,1);m.write8(0xFFFA19,8);m.write8(0xFFFA07,0x20);
        m.timerA_setLineInput(!rising);m.write8(0xFFFA0B,0xDF);m.timerA_setLineInput(rising);
        eq("TAI AER sans DDR GPIP4",m.ipra&0x20,0x20);
        m.write8(0xFFFA0B,0xDF);m.timerA_setLineInput(rising);eq("TAI même niveau",m.ipra&0x20,0);
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);m.write8(0xFFFA09,1);m.write8(0xFFFA15,1);s.runTo(100);
        m.setBusyLine(true);m.flushIrqUpdate();s.runTo(103);eq("BUSY IRQ propagation avant",m.irqPending(),0);
        s.runTo(104);eq("BUSY IRQ propagation exacte",m.irqPending(),1);
        std::vector<uint8_t> bytes;auto save=StateArchive::saver(bytes);m.serialize(save);s.serialize(save);
        m.setBusyLine(false);auto load=StateArchive::loader(bytes.data(),bytes.size());m.serialize(load);s.serialize(load);
        eq("archive BUSY valide",load.ok(),1);eq("archive niveau BUSY",m.read8(0xFFFA01)&1,0);
        eq("archive BUSY IRQ",m.iprb&1,1);
    }
    std::printf("[selftest-mfp-gpip-lines] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_GPIP_LINES_TEST_MAIN
int main(){return testMfpGpipLines()?1:0;}
#endif
