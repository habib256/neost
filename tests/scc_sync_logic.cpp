// UM010904-0318, WR3 p.149 : filtrage avant FIFO, sans synchroniseur de bits.
#include "io/Scc.hpp"
#include <cstdio>

bool testSccSync() {
    int failures=0;
    auto check=[&](bool ok,const char* name) {
        if (!ok) { ++failures; std::fprintf(stderr,"SCC SYNC FAIL: %s\n",name); }
    };
    for (int c=0;c<2;++c) {
        Scc s;
        auto wr=[&](int r,int v) { s.write8(0x81+4*c,uint8_t(r)); s.write8(0x81+4*c,uint8_t(v)); };
        auto rr=[&](int r) { s.write8(0x81+4*c,uint8_t(r)); return s.read8(0x81+4*c); };
        auto pending=[&] { s.write8(0x81,3); return s.read8(0x81); };
        auto pop=[&] { return s.read8(0x83+4*c); };
        wr(14,0); // La source RxD externe est déconnectée en Local Loopback au reset.
        wr(3,0xc3); wr(6,0x16); wr(7,0x32);
        for (int mode : {0x00,0x10,0x30}) {
            wr(4,mode); s.receiveByte(c,0x16);
            check(!(rr(0)&1),"WR6 matching byte inhibited in mono/bi/external sync");
            s.receiveByte(c,0x32); check((rr(0)&1) && pop()==0x32,"WR7 is not the FIFO inhibit comparator");
        }
        wr(4,0); wr(3,0xc1); s.receiveByte(c,0x16);
        check((rr(0)&1) && pop()==0x16,"inhibit disabled preserves matching byte");
        wr(3,0xc3); wr(4,0x20); s.receiveByte(c,0x16);
        check((rr(0)&1) && pop()==0x16,"SDLC bit has address semantics, no generic filter");
        wr(4,0x10); wr(10,1); s.receiveByte(c,0x16);
        check((rr(0)&1) && pop()==0x16,"12-bit bisync ignores inhibit");
        wr(4,0); s.receiveByte(c,0x16);
        check(!(rr(0)&1),"6-bit monosync comparator still uses all eight bits");
        s.receiveByte(c,0x96); check(pop()==0x96,"6-bit monosync does not mask comparator");
        wr(10,0); wr(4,4); s.receiveByte(c,0x16);
        check(!(rr(0)&1),"async also inhibits WR6 match, per manual caution");
        wr(3,3); wr(6,0xf6); s.receiveByte(c,0x16);
        check(!(rr(0)&1),"async short character compares FIFO byte with high bits set");
        wr(6,0x16); s.receiveByte(c,0x16);
        check((rr(0)&1) && pop()==0xf6,"async comparator uses full formatted byte");
        wr(4,0); wr(3,0xc3); wr(10,0); wr(1,8); wr(9,8);
        s.receiveByte(c,0x16); check(!(pending()&(c?4:32)),"inhibited byte does not trigger first-character IRQ");
        for (int b : {0x41,0x42,0x43,0x16}) s.receiveByte(c,uint8_t(b));
        check((pending()&(c?4:32)) && pop()==0x41 && pop()==0x42 && pop()==0x43 && !(rr(1)&0x20),
              "inhibited byte does not consume FIFO or cause overrun");
    }
    return failures==0;
}
#ifdef SCC_SYNC_STANDALONE
int main() { return testSccSync()?0:1; }
#endif
