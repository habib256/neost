// Break ACIA : acia.c ACIA_Write_CR / ACIA_Clock_TX (repos seulement).
// Formats du 6850 émis bit à bit au /64 ; les diviseurs /16 et /1 restent sans échantillonnage SCI.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>
namespace {
struct Rig {
    Scheduler s; Mfp m; Ikbd k{m}; int64_t live = 0;
    Rig() {
        k.setScheduler(&s); s.setLiveClock([this] { return live; });
        s.setCallback(Scheduler::IKBD_TX, [this] { k.onTxEmpty(); });
        s.setCallback(Scheduler::IKBD_RX, [this] { k.onRxDeliver(); });
        k.write8(0, 0x96);
    }
    void at(int64_t t) { live=t; s.runTo(t); }
    int status() { return k.read8(0); }
    int64_t response() { const auto t=s.rawCyclesUntil(Scheduler::IKBD_RX); return t<0 ? -1 : t; }
};
}
int testIkbdAciaFormat() {
    int failures=0, checks=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) {
        ++checks; if(got!=want) { ++failures; std::fprintf(stderr,
            "[ikbd-acia-format] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)want); }
    };
    {
        Rig r; r.k.write8(0,0xF6); r.k.write8(2,0x1C);
        eq("break holds TDR",r.status()&2,0);
        eq("break disables TX IRQ",r.status()&0x80,0);
        // Le BREAK garde une échéance : le SCI doit voir sa trame nulle (erreur de trame, ROM $FF40).
        eq("BREAK deadline serves the SCI framing error",r.s.cyclesUntil(Scheduler::IKBD_TX),1024);
        r.at(20000); eq("break does not run command",r.response(),-1);
        eq("break does not deliver command response",r.status()&1,0);
        r.k.write8(0,0xB6);
        eq("break release waits next clock",r.s.now()+r.s.rawCyclesUntil(Scheduler::IKBD_TX),20480);
        r.at(20480); eq("release empties TDR and raises IRQ",r.status()&0x82,0x82);
        r.at(29696); eq("BREAK fragment prevents intact retained command",r.response(),-1);
    }
    {
        Rig r; r.k.write8(2,0x1C); r.at(1024);
        r.k.write8(2,0x16); r.k.write8(0,0xF6);
        r.at(10240); eq("active TSR finishes during break",r.response(),7168);
        eq("queued TDR remains full",r.status()&2,0);
        eq("BREAK frame deadline, not a queued transmission",r.s.cyclesUntil(Scheduler::IKBD_TX),1024);
        r.k.write8(0,0x96); r.at(11264);
        eq("queued TDR resumes",r.status()&2,2);
    }
    {
        Rig r; r.k.write8(2,0x1C); r.at(500); r.k.write8(0,0xF6);
        eq("break before start: deadline now serves the BREAK frame",r.s.cyclesUntil(Scheduler::IKBD_TX),524);
        r.at(12000); eq("cancelled start did not parse",r.response(),-1);
        std::vector<uint8_t> b; auto save=StateArchive::saver(b); r.k.serialize(save); r.s.serialize(save);
        Rig restored; auto load=StateArchive::loader(b.data(),b.size()); restored.k.serialize(load); restored.s.serialize(load);
        restored.live=restored.s.now(); eq("break state loads",load.ok(),1);
        restored.k.write8(0,0x96); restored.at(12288);
        eq("restored break releases TDR",restored.status()&2,2);
        restored.at(21504); eq("restored BREAK fragment prevents intact command",restored.response(),-1);
    }
    {
        Rig r; r.k.write8(0,0xF6); r.k.write8(2,0x1C); r.k.write8(0,3);
        // [B] MC6850 § Interrupt Request p. 6 : TDRE inhibé sous master reset, relu à 1 à sa sortie.
        eq("master reset discards break TDR",r.status()&2,0);
        eq("master reset leaves no TX deadline",r.s.cyclesUntil(Scheduler::IKBD_TX),-1);
        r.k.write8(0,0x96); eq("leaving master reset: TDR empty",r.status()&2,2);
    }
    std::printf("[ikbd-acia-format] %d checks, %d failures\n",checks,failures);
    return failures;
}
