// Cadence SCC : scc.c 1660-1679, 1986-2040 et 2139-2173.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>
struct Rig {
    Scheduler sched; Scc scc; std::vector<uint8_t> output;
    Rig() {
        scc.setScheduler(&sched);
        sched.setCallback(Scheduler::SCC_BRG_A,[this]{scc.onBrgEvent(0);});
        sched.setCallback(Scheduler::SCC_TX_A,[this]{scc.onSerialEvent(0,true);});
        sched.setCallback(Scheduler::SCC_RX_A,[this]{scc.onSerialEvent(0,false);});
        scc.setSerialSink([this](int,uint8_t b){output.push_back(b);});
    }
    void wr(uint8_t r,uint8_t v){scc.write8(0xff8c81,r);scc.write8(0xff8c81,v);}
    uint8_t rr(uint8_t r){scc.write8(0xff8c81,r);return scc.read8(0xff8c81);}
    void arm(){wr(4,4);wr(3,0xc1);wr(5,0x68);wr(11,0x50);wr(12,0);wr(13,0);wr(14,3);}
};
bool testSccSerial(){
    int fails=0;
    auto check=[&](bool b,const char* name){if(!b){++fails;std::fprintf(stderr,"SCC serial FAIL: %s\n",name);}};
    {
        Rig r;r.arm();r.wr(1,2);r.wr(9,8);
        r.scc.write8(0xff8c83,0x41);
        check(r.output.empty(),"TX waits a character");
        check((r.rr(0)&4) && !(r.rr(1)&1),"shift register full, holding empty");
        check(r.scc.irqActive(),"TX IRQ at TDR promotion");
        r.scc.write8(0xff8c83,0x42);
        check(!(r.rr(0)&4) && !r.scc.irqActive(),"second byte held, TX IRQ clears");
        r.sched.runTo(39);check(r.output.empty(),"8N1 TC0 not before cycle40");
        r.sched.runTo(40);check(r.output==std::vector<uint8_t>{0x41},"first character completes at40");
        check((r.rr(0)&4) && !(r.rr(1)&1) && r.scc.irqActive(),"promote pending byte, all sent stays clear");
        r.sched.runTo(80);check(r.output==std::vector<uint8_t>({0x41,0x42}),"second character completes at80");
        check(r.rr(1)&1,"all sent after second byte");
    }
    {
        Rig r;r.arm();r.scc.receiveByte(0,0x51);r.scc.receiveByte(0,0x52);
        check(!(r.rr(0)&1),"RX queued until frame completes");
        r.sched.runTo(40);check(r.scc.read8(0xff8c83)==0x51,"RX first frame");
        r.sched.runTo(80);check(r.scc.read8(0xff8c83)==0x52,"RX second frame");
        check(!(r.rr(1)&0x20),"no artificial overrun from burst injection");
        r.scc.receiveByte(0,1);r.wr(9,0x80);
        check(r.sched.cyclesUntil(Scheduler::SCC_RX_A)==-1,"channel reset cancels RX");
    }
    {
        Rig r;r.arm();r.wr(14,0x13);r.scc.write8(0xff8c83,0x65);
        r.sched.runTo(39);check(!(r.rr(0)&1),"loopback waits TX");
        r.sched.runTo(40);check(r.scc.read8(0xff8c83)==0x65 && r.output.size()==1 && r.output[0]==0x65,
                               "local loopback receives internally and drives TxD at completion");
    }
    {
        Rig r;r.arm();r.wr(4,9);r.wr(5,8); // 5 bits, parité, 1,5 stop = 8,5 bits.
        r.scc.write8(0xff8c83,0x17);
        check(r.sched.cyclesUntil(Scheduler::SCC_TX_A)==34,"5P1.5 frame length");
        r.sched.runTo(34);check(r.output.size()==1,"fractional stop completion");
        r.wr(14,0);r.scc.write8(0xff8c83,0x18);
        check(r.sched.cyclesUntil(Scheduler::SCC_TX_A)==-1,"disabled BRG stops selected TX clock");
        r.wr(14,3);check(r.sched.cyclesUntil(Scheduler::SCC_TX_A)==34,"BRG resumes pending TX");
        r.wr(9,0xc0);check(r.sched.cyclesUntil(Scheduler::SCC_TX_A)==-1,"hardware reset cancels TX");
    }
    {
        Rig r;r.arm();r.scc.write8(0xff8c83,0x31);r.scc.write8(0xff8c83,0x32);
        r.scc.receiveByte(0,0x71);r.scc.receiveByte(0,0x72);
        r.sched.runTo(17);
        std::vector<uint8_t> state;auto save=StateArchive::saver(state);
        r.scc.serialize(save);r.sched.serialize(save);
        Rig resumed;auto load=StateArchive::loader(state.data(),state.size());
        resumed.scc.serialize(load);resumed.sched.serialize(load);
        check(load.ok(),"mid-frame state reload");
        check(resumed.sched.cyclesUntil(Scheduler::SCC_TX_A)==23,"restored TX phase");
        resumed.sched.runTo(40);check(resumed.output==std::vector<uint8_t>{0x31},"restored first TSR");
        check(resumed.scc.read8(0xff8c83)==0x71,"restored first RX queue byte");
        resumed.sched.runTo(80);check(resumed.output==std::vector<uint8_t>({0x31,0x32}),"restored second TDR");
        check(resumed.scc.read8(0xff8c83)==0x72,"restored second RX queue byte");
    }
    {
        Rig r;r.arm();r.scc.write8(0xff8c83,1);r.scc.write8(0xff8c83,2);
        r.sched.runTo(47);
        check(r.sched.rawCyclesUntil(Scheduler::SCC_TX_A)==33,"late callback preserves cycle80 deadline");
        r.sched.runTo(80);check(r.output.size()==2,"late callback does not drift");
    }
    {
        Rig r;
        r.sched.setCallback(Scheduler::SCC_TX_B,[&]{r.scc.onSerialEvent(1,true);});
        r.sched.setCallback(Scheduler::SCC_RX_B,[&]{r.scc.onSerialEvent(1,false);});
        auto wrB=[&](uint8_t reg,uint8_t value){r.scc.write8(0xff8c85,reg);r.scc.write8(0xff8c85,value);};
        wrB(4,4);wrB(3,0xc1);wrB(5,0x68);wrB(11,0x50);wrB(12,0);wrB(13,0);wrB(14,3);
        r.scc.write8(0xff8c87,0x61);r.scc.receiveByte(1,0x62);
        check(r.sched.cyclesUntil(Scheduler::SCC_TX_B)==40 && r.sched.cyclesUntil(Scheduler::SCC_RX_B)==40,"B schedules independent TX/RX");
        r.sched.runTo(40);check(r.output==std::vector<uint8_t>{0x61} && r.scc.read8(0xff8c87)==0x62,"B TX/RX callbacks");
    }
    return fails==0;
}
#ifdef SCC_SERIAL_STANDALONE
int main(){return testSccSerial()?0:1;}
#endif
