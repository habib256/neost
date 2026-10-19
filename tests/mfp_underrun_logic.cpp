// UE mémorisé : MAME tx_clock/REGISTER_UDR et MC68901UM §7.3.2.
#include "io/Mfp.hpp"
#include <cstdio>
int testMfpUnderrun() {
    int checks=0,failures=0;
    auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::printf("  FAIL MFP underrun %s: %lld attendu %lld\n",name,(long long)got,(long long)want);}};
    for(int saved=0;saved<2;++saved) {
        Scheduler s;Mfp m;m.setScheduler(&s);std::vector<uint8_t> output;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        m.setSerialSink([&](uint8_t b){output.push_back(b);});m.write8(0xFFFA29,0x88);
        m.write8(0xFFFA07,6);m.write8(0xFFFA13,6);m.write8(0xFFFA2D,1);
        m.write8(0xFFFA2F,0x11);eq("TBE initial",m.ipra&6,4);
        m.write8(0xFFFA0B,0xF9);s.runTo(s.nextDue());m.flushIrqUpdate();
        eq("premier underrun IRQ",m.ipra&6,2);
        const auto boundary=s.now();
        s.runTo(boundary+3);eq("TXERR propagation moins un",m.irqPending(),0);
        s.runTo(boundary+4);eq("TXERR propagation exacte",m.irqPending(),1);
        // Aucun read TSR : UE reste mémorisé malgré un nouveau TDR.
        m.write8(0xFFFA0B,0xF9);m.write8(0xFFFA2F,0x22);
        eq("nouveau TDR IRQ TBE",m.ipra&6,4);
        m.write8(0xFFFA0B,0xF9);
        if(saved) {
            std::vector<uint8_t> state;auto out=StateArchive::saver(state);s.serialize(out);m.serialize(out);
            m.read8(0xFFFA2D);auto in=StateArchive::loader(state.data(),state.size());s.serialize(in);m.serialize(in);
            eq("archive UE en vol",in.ok(),1);
        }
        // nextDue peut être l'événement de propagation d'une ancienne IRQ.
        while(output.size()<2)s.runTo(s.nextDue());
        eq("UE mémorisé pas nouveau TXERR",m.ipra&2,0);
        eq("TSR UE reste posé",m.read8(0xFFFA2D)&0x40,0x40);
        eq("lecture TSR efface UE",m.read8(0xFFFA2D)&0x40,0);
        eq("octet TSR préservé",output[1],0x22);
        m.write8(0xFFFA2F,0x33);m.write8(0xFFFA0B,0xF9);
        while(output.size()<3)s.runTo(s.nextDue());m.flushIrqUpdate();
        eq("read réarme IRQ UE",m.ipra&2,2);
        const auto pending=m.ipra;
        m.write8(0xFFFA2D,0);eq("TE0 efface UE",m.read8(0xFFFA2D)&0x40,0);
        eq("TE0 conserve IRQ pendante",m.ipra&2,pending&2);
        m.write8(0xFFFA2F,0x44);eq("TE0 retient TDR",m.read8(0xFFFA2D)&0x80,0);
        m.write8(0xFFFA2D,1);m.write8(0xFFFA0B,0xF9);
        while(output.size()<4)s.runTo(s.nextDue());
        eq("disable réarme UE",m.ipra&2,2);eq("TDR retenu livré",output[3],0x44);
    }
    std::printf("[selftest-mfp-underrun] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_UNDERRUN_TEST_MAIN
int main(){return testMfpUnderrun()?1:0;}
#endif
