// END/AT : MC68901UM §7.3.2 ; fin de caractère MAME tx_clock.
#include "io/Mfp.hpp"
#include <cstdio>
int testMfpTurnaround() {
    int checks=0,failures=0,context=0;
    auto eq=[&](const char* name,int64_t got,int64_t want){++checks;if(got!=want){++failures;std::printf("  FAIL MFP turnaround %s (%d): %lld attendu %lld\n",name,context,(long long)got,(long long)want);}};
    for(int busy=0;busy<2;++busy) for(int at=0;at<2;++at) for(int saved=0;saved<2;++saved) {
        context=busy*4+at*2+saved;
        Scheduler s;Mfp m;m.setScheduler(&s);int emitted=0;
        s.setCallback(Scheduler::SERIAL_TX,[&]{m.onSerialTxEvent();});
        s.setCallback(Scheduler::SERIAL_RX,[&]{m.onSerialRxEvent();});
        s.setCallback(Scheduler::MFP_IRQ,[&]{m.flushIrqUpdate();});
        m.setSerialSink([&](uint8_t){++emitted;});m.write8(0xFFFA29,0x88);
        // Un RDR non lu doit survivre au retournement.
        m.write8(0xFFFA2B,1);m.receiveByte(0x55);s.runTo(s.nextDue());
        m.write8(0xFFFA07,2);m.write8(0xFFFA13,2);m.write8(0xFFFA2D,1);
        if(busy)m.write8(0xFFFA2F,0x11);
        if(busy)m.write8(0xFFFA2F,0x22); // TDR retenu après désactivation en vol
        const auto boundary=busy?s.nextDue():s.now();
        m.write8(0xFFFA2D,at?0x20:0);m.flushIrqUpdate();
        if(busy)eq("END différé",m.read8(0xFFFA2D)&0x10,0);
        if(saved) {
            std::vector<uint8_t> state;auto out=StateArchive::saver(state);s.serialize(out);m.serialize(out);
            m.write8(0xFFFA2B,0);auto in=StateArchive::loader(state.data(),state.size());s.serialize(in);m.serialize(in);
            eq("archive valide",in.ok(),1);
        }
        if(busy)s.runTo(boundary);
        m.flushIrqUpdate();
        eq("END à la frontière",m.read8(0xFFFA2D)&0x10,0x10);
        eq("AT consommé",m.read8(0xFFFA2D)&0x20,0);
        eq("RE préservé ou activé",m.read8(0xFFFA2B)&1,1);
        eq("RDR non lu conservé",m.read8(0xFFFA2B)&0x80,0x80);
        eq("donnée conservée",m.read8(0xFFFA2F),0x55);
        m.flushIrqUpdate();
        eq("IRQ END pendante",m.ipra&2,2);
        eq("pas IRQ CPU anticipée",m.irqPending(),0);
        const auto irqDue=boundary+(busy?0:Mfp::mmioWriteEnd())+4;
        s.runTo(irqDue-1);eq("IRQ CPU moins un cycle",m.irqPending(),0);
        s.runTo(irqDue);eq("IRQ CPU après délai",m.irqPending(),1);
        const auto count=emitted;
        m.write8(0xFFFA0B,0xFD);m.write8(0xFFFA2D,0);
        eq("END déjà posé sans nouvelle IRQ",m.ipra&2,0);
        m.write8(0xFFFA2D,1);eq("TE efface END",m.read8(0xFFFA2D)&0x10,0);
        if(busy) {eq("TDR reprend",m.read8(0xFFFA2D)&0x80,0x80);s.runTo(s.nextDue());eq("TDR émis",emitted,count+1);}
    }
    {
        Scheduler s;Mfp m;m.setScheduler(&s);m.write8(0xFFFA07,2);
        m.write8(0xFFFA2D,0);eq("TE déjà nul pose END",m.read8(0xFFFA2D)&0x10,0x10);
        m.write8(0xFFFA0B,0xFD);m.write8(0xFFFA2D,0);
        eq("TE0 répété sans front",m.ipra&2,0);
        m.write8(0xFFFA2D,0x20);eq("AT au repos active RE",m.read8(0xFFFA2B)&1,1);
        eq("AT au repos désarmé",m.read8(0xFFFA2D)&0x20,0);
        eq("AT seul sans nouvel END IRQ",m.ipra&2,0);
        m.write8(0xFFFA2B,0);m.write8(0xFFFA2D,0x20);
        eq("AT réarmé active RE",m.read8(0xFFFA2B)&1,1);
    }
    std::printf("[selftest-mfp-turnaround] %d checks, %d FAIL\n",checks,failures);return failures;
}
#ifdef NEOST_MFP_TURNAROUND_TEST_MAIN
int main(){return testMfpTurnaround()?1:0;}
#endif
