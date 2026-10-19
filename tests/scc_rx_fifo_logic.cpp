// FIFO SCC : Zilog UM0109 §2.4.3 et WR0/WR1 ; MAME z80scc receive_data.
#include "io/Scc.hpp"
#include <cstdio>
#include <vector>
bool testSccRxFifo() {
    int fails=0;
    auto check=[&](bool ok,const char* msg){if(!ok){++fails;std::fprintf(stderr,"SCC RX FIFO FAIL: %s\n",msg);}};
    struct Rig {Scc s; void wr(int r,int v){s.write8(0x81,r);s.write8(0x81,v);} int rr(int r){s.write8(0x81,r);return s.read8(0x81);} int pop(){return s.read8(0x83);} Rig(){wr(14,0);s.write8(0x85,14);s.write8(0x85,0);wr(3,0xc1);wr(9,8);} /* RxD externe, sans bouclage local. */ };
    {Rig r; r.wr(1,0x10);for(int i=1;i<=3;++i)r.s.receiveByte(0,i);
     check(!(r.rr(1)&0x20),"three bytes fit without overrun");
     for(int i=1;i<=3;++i){check(r.pop()==i,"FIFO preserves arrival order");check(bool(r.rr(0)&1)==(i<3),"RR0 tracks remaining bytes");}
     check(!r.s.irqActive(),"all-character IRQ clears after final read");}
    {Rig r;r.wr(1,0x10);for(int i=1;i<=4;++i)r.s.receiveByte(0,i);
     check(r.pop()==1 && r.pop()==2,"overrun preserves oldest two bytes");
     check(r.rr(1)&0x20,"overrun follows overwritten newest slot");check(r.pop()==4,"overrun replaces newest slot");
     check(r.rr(1)&0x20,"overrun latched until error reset");r.s.write8(0x81,0x30);check(!(r.rr(1)&0x20),"WR0 resets error");}
    {Rig r;r.wr(1,8);r.s.receiveByte(0,1);r.s.receiveByte(0,2);check(r.s.irqActive(),"first-character IRQ");r.pop();check(!r.s.irqActive(),"second character does not interrupt in first mode");r.s.write8(0x81,0x20);check(r.s.irqActive(),"WR0 next-character rearms stored FIFO character");}
    {Rig r;r.wr(1,0x18);for(int i=1;i<=4;++i)r.s.receiveByte(0,i);check(!r.s.irqActive(),"special mode waits for erroneous read");r.pop();r.pop();check(!r.s.irqActive(),"special mode waits at error head");check(r.pop()==4 && r.s.irqActive(),"error read locks FIFO and interrupts");check(r.pop()==4 && (r.rr(0)&1),"locked error remains readable");r.s.write8(0x81,0x30);check(!(r.rr(0)&1) && !r.s.irqActive(),"error reset releases locked byte");}
    {Rig r;for(int i=1;i<=3;++i)r.s.receiveByte(0,i);std::vector<uint8_t> bytes;auto save=StateArchive::saver(bytes);r.s.serialize(save);Rig t;auto load=StateArchive::loader(bytes.data(),bytes.size());t.s.serialize(load);check(load.ok() && t.pop()==1 && t.pop()==2 && t.pop()==3,"state restores FIFO");r.wr(9,0x80);check(!(r.rr(0)&1),"channel reset empties FIFO");}
    {Rig r;Scheduler sched;r.s.setScheduler(&sched);
     sched.setCallback(Scheduler::SCC_RX_A,[&]{r.s.onSerialEvent(0,false);});
     r.wr(4,4);r.wr(11,0x40);r.wr(12,0);r.wr(13,0);r.wr(14,3);
     for(int i=1;i<=4;++i)r.s.receiveByte(0,i);
     sched.runTo(40);check(r.rr(0)&1,"first queued byte reaches hardware at character deadline");
     sched.runTo(80);sched.runTo(120);check(!(r.rr(1)&0x20),"three timed arrivals fit hardware FIFO");
     sched.runTo(160);check(r.pop()==1 && r.pop()==2 && r.pop()==4,"host queue feeds hardware overrun after four frames");}
    {Rig r;r.wr(1,0x18);for(int i=1;i<=4;++i)r.s.receiveByte(0,i);r.pop();r.pop();r.pop();
     std::vector<uint8_t> bytes;auto save=StateArchive::saver(bytes);r.s.serialize(save);
     Rig t;auto load=StateArchive::loader(bytes.data(),bytes.size());t.s.serialize(load);
     check(load.ok() && t.s.irqActive() && t.pop()==4,"state restores locked error and pending IRQ");
     t.s.write8(0x81,0x30);check(!(t.rr(0)&1),"restored locked FIFO releases");}
    {Rig r;r.s.receiveByte(0,1);r.s.receiveByte(0,2);r.s.receiveByte(0,3);
     check(r.pop()==1 && r.pop()==2,"partial drain advances FIFO head");
     r.s.receiveByte(0,4);r.s.receiveByte(0,5);
     check(r.pop()==3 && r.pop()==4 && r.pop()==5,"pop/push wraps FIFO in order");}
    {Rig r;r.s.write8(0x85,3);r.s.write8(0x85,0xc1);
     r.s.receiveByte(0,0x41);for(int i=1;i<=4;++i)r.s.receiveByte(1,i);
     check(r.pop()==0x41 && !(r.rr(1)&0x20),"B overrun leaves A intact");
     check(r.s.read8(0x87)==1 && r.s.read8(0x87)==2 && r.s.read8(0x87)==4,"B owns independent FIFO");}
    {Rig r;r.wr(1,0x10);for(int i=1;i<=4;++i)r.s.receiveByte(0,i);r.pop();r.pop();r.pop();
     r.s.receiveByte(0,5);r.wr(1,0x18);
     r.s.write8(0x81,0x30);check(r.pop()==5,"sticky RR1 error does not discard clean slot on error reset");}
    {Rig r;r.wr(1,0x10);for(int i=1;i<=4;++i)r.s.receiveByte(0,i);r.pop();r.pop();r.pop();
     r.s.receiveByte(0,5);r.wr(1,0x18);
     check(r.pop()==5 && !(r.rr(0)&1),"sticky RR1 error does not lock clean slot");}
    {Rig r;r.wr(1,0x18);for(int i=1;i<=4;++i)r.s.receiveByte(0,i);r.pop();r.pop();
     r.s.write8(0x81,0x30);check(!(r.rr(0)&1),"error reset before erroneous read discards that slot");}
    return fails==0;
}
#ifdef SCC_RX_FIFO_STANDALONE
int main(){return testSccRxFifo()?0:1;}
#endif
