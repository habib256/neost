// Scrutation normale du firmware : timer 150000 cycles, indépendant du VBL.
// Référence primaire : Hatari ikbd.c IKBD_Boot_ROM / InterruptHandler_AutoSend.
// Les commandes sont injectées à la sortie du récepteur série : ce banc
// mesure le firmware ; le transport CPU est couvert par ikbd_cpu_serial_logic.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>
namespace {
struct Rig {
    Scheduler sched;
    Mfp mfp;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    Rig() {
        ikbd.setScheduler(&sched);
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::IKBD_AUTOSEND, [this] { ikbd.onAutoSend(); });
        sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        sched.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        sched.setCallback(Scheduler::IKBD_MONITOR, [this] { ikbd.onJoystickMonitor(); });
        ikbd.write8(0xFFFC00, 0x96);
        ikbd.startAutoSend();
    }
    void warm() { for (int i=0; i<21; ++i) ikbd.onVbl(20000); }
    void advance(int64_t t) { live=t; sched.runTo(t); }
    int64_t due(Scheduler::Source s) { const auto r=sched.rawCyclesUntil(s); return r==INT64_MIN ? -1 : live+r; }
};
}
int testIkbdAutoSend() {
    int count=0, failed=0;
    auto eq=[&](const char* label,int64_t got,int64_t want) {
        ++count; if(got==want) return; ++failed;
        std::fprintf(stderr,"[ikbd-autosend] FAIL %s: got %lld expected %lld\n", label,(long long)got,(long long)want);
    };
    {
        // ROM 6301 ($F54A) : le paquet $F8 part dès que la ligne est libre, sans
        // attendre une scrutation ; le mouvement arrivé pendant l'émission s'accumule
        // et part à la libération suivante.
        Rig r; r.warm(); r.ikbd.mouseEvent(3,4,false,false);
        eq("packet starts as soon as the line is free",r.due(Scheduler::IKBD_RX),10240);
        r.advance(10240); eq("mouse header",r.ikbd.read8(0xFFFC02),0xF8);
        r.advance(15000); r.ikbd.mouseEvent(1,2,false,false);
        r.advance(20480); eq("mouse dx",r.ikbd.read8(0xFFFC02),3);
        r.advance(30720); eq("mouse dy",r.ikbd.read8(0xFFFC02),4);
        eq("motion during transfer leaves at next free line",r.due(Scheduler::IKBD_RX),40960);
        r.advance(40960); eq("second header",r.ikbd.read8(0xFFFC02),0xF8);
        r.advance(51200); eq("accumulated dx",r.ikbd.read8(0xFFFC02),1);
        r.advance(61440); eq("accumulated dy",r.ikbd.read8(0xFFFC02),2);
        r.advance(150000); r.advance(300009); eq("late callback preserves timer grid",r.due(Scheduler::IKBD_AUTOSEND),450000);
    }
    {
        Rig r; r.warm(); r.ikbd.receiveSerialByte(0x12);
        r.ikbd.setJoystick(0,0x09); r.advance(150000);
        r.advance(160240); eq("joystick auto header",r.ikbd.read8(0xFFFC02),0xFF);
        r.advance(170480); eq("joystick auto state",r.ikbd.read8(0xFFFC02),0x09);
        r.advance(300000); eq("unchanged joystick not repeated",r.due(Scheduler::IKBD_RX),-1);
    }
    {
        Rig r; for(int i=0;i<20;++i) r.ikbd.onVbl(20000);
        r.ikbd.mouseEvent(1,0,false,false); r.advance(150000);
        eq("twenty VBL guard",r.due(Scheduler::IKBD_RX),-1);
        r.ikbd.onVbl(20000); r.advance(300000);
        eq("input retained across initial guard",r.due(Scheduler::IKBD_RX),310240);
    }
    {
        Rig r; r.warm(); r.advance(10000); r.ikbd.bootRom();
        eq("reset preserves pending scan",r.due(Scheduler::IKBD_AUTOSEND),150000);
        r.advance(20000); r.ikbd.bootRom();
        eq("repeated reset cannot starve scan",r.due(Scheduler::IKBD_AUTOSEND),150000);
        r.ikbd.mouseEvent(1,0,false,false); r.advance(150000);
        eq("reset suppresses automatic output",r.due(Scheduler::IKBD_RX),-1);
    }
    {
        Rig r; r.warm(); r.ikbd.receiveSerialByte(0x13);
        r.ikbd.mouseEvent(2,0,false,false); r.advance(150000);
        eq("pause holds scan output",r.due(Scheduler::IKBD_RX),-1);
        r.ikbd.receiveSerialByte(0x11);
        eq("resume releases queued packet",r.due(Scheduler::IKBD_RX),160240);
    }
    {
        Rig r; r.warm(); r.ikbd.receiveSerialByte(0x17); r.ikbd.receiveSerialByte(10);
        r.advance(150000); eq("normal timer does not duplicate monitor",r.due(Scheduler::IKBD_RX),-1);
        eq("monitor timer unchanged",r.due(Scheduler::IKBD_MONITOR),neost::pacing::kCpuHzInt/10);
    }
    {
        // Sous le seuil ($0B), la ROM garde le cumul (Hatari l'abandonne).
        Rig r; r.warm(); r.ikbd.receiveSerialByte(0x0B); r.ikbd.receiveSerialByte(10); r.ikbd.receiveSerialByte(10);
        r.advance(12345); r.ikbd.mouseEvent(7,0,false,false);
        eq("motion under threshold is not sent",r.due(Scheduler::IKBD_RX),-1);
        std::vector<uint8_t> bytes; auto save=StateArchive::saver(bytes); r.ikbd.serialize(save); r.sched.serialize(save);
        Rig restored; auto load=StateArchive::loader(bytes.data(),bytes.size()); restored.ikbd.serialize(load); restored.sched.serialize(load); restored.live=restored.sched.now();
        eq("snapshot accepted",load.ok(),1);
        eq("snapshot keeps pending deadline",restored.due(Scheduler::IKBD_AUTOSEND),150000);
        restored.ikbd.mouseEvent(4,0,false,false);
        eq("snapshot retains accumulated motion",restored.due(Scheduler::IKBD_RX),12345+10240);
        restored.advance(12345+10240); restored.ikbd.read8(0xFFFC02);
        restored.advance(12345+20480); eq("accumulated dx crosses threshold",restored.ikbd.read8(0xFFFC02),11);
    }
    std::printf("[ikbd-autosend] %d checks, %d failures\n",count,failed);
    return failed;
}
