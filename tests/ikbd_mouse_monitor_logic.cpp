// Hatari ikbd.c : MouseAction et position interne précèdent le monitoring ;
// AutoSendCycles reste celui de $17 jusqu'au boot ROM.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <initializer_list>
#include <vector>
namespace {
struct Rig {
    Scheduler sched; Mfp mfp; Ikbd ikbd{mfp}; int64_t live = 0;
    Rig() {
        ikbd.setScheduler(&sched); sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::IKBD_MONITOR, [this] { ikbd.onJoystickMonitor(); });
        sched.setCallback(Scheduler::IKBD_AUTOSEND, [this] { ikbd.onAutoSend(); });
        sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        sched.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        ikbd.write8(0xFFFC00, 0x96);
        for (int i=0; i<21; ++i) ikbd.onVbl(20000);
    }
    void command(std::initializer_list<uint8_t> bytes) { for(auto b: bytes) ikbd.receiveSerialByte(b); }
    void advance(int64_t t) { live=t; sched.runTo(t); }
    int data() { return ikbd.read8(0xFFFC02); }
    int64_t due(Scheduler::Source s) { auto d=sched.rawCyclesUntil(s); return d==INT64_MIN ? -1 : live+d; }
};
}
int testIkbdMouseMonitor() {
    int count=0, fails=0;
    auto eq=[&](const char* name, int64_t got, int64_t want) {
        ++count; if(got==want) return; ++fails;
        std::fprintf(stderr,"[ikbd-mouse-monitor] %s: %lld != %lld\n",name,(long long)got,(long long)want);
    };
    constexpr int64_t period=80212;
    {
        Rig r; r.ikbd.startAutoSend(); r.command({0x07,4,0x17,1});
        eq("$17 replaces normal timer",r.due(Scheduler::IKBD_AUTOSEND),-1);
        // ROM : en $17 la boucle ($F171 → $F16B → $F681) ne passe pas par le
        // traitement souris ($F371) — pas de touche $74 ; le bouton gauche n'est vu
        // que comme feu du joystick 0.
        r.ikbd.mouseEvent(7,9,true,false); r.advance(period);
        eq("monitor sample first serial deadline",r.due(Scheduler::IKBD_RX),period+10240);
        r.advance(period+10240); eq("monitor fire",r.data(),2);
        r.advance(period+20480); eq("monitor directions",r.data(),0);
        eq("no MouseAction key while monitoring",r.due(Scheduler::IKBD_RX),-1);
    }
    {
        Rig r; r.command({0x17,1}); r.ikbd.mouseEvent(7,9,false,false);
        r.advance(period); r.advance(period+10240); r.data(); r.advance(period+20480); r.data();
        // ROM $FB39 : $0D ne répond qu'en mode absolu (ici $17 tient le port 0).
        r.command({0x0D});
        eq("$0D outside absolute mode is ignored",r.due(Scheduler::IKBD_RX),-1);
    }
    {
        // $14 quitte la surveillance : la scrutation normale reprend sur son tick.
        Rig r; r.command({0x17,1,0x14}); r.ikbd.setJoystick(1,0);
        eq("exit stops monitor timer",r.due(Scheduler::IKBD_MONITOR),-1);
        eq("exit hands its tick to the scan",r.due(Scheduler::IKBD_AUTOSEND),period);
        r.advance(period); r.advance(period+10240); eq("exit reports auto joystick at retained tick",r.data(),0xFE);
        std::vector<uint8_t> bytes; auto save=StateArchive::saver(bytes); r.ikbd.serialize(save); r.sched.serialize(save);
        Rig copy; auto load=StateArchive::loader(bytes.data(),bytes.size()); copy.ikbd.serialize(load); copy.sched.serialize(load); copy.live=copy.sched.now();
        eq("scan state loads",load.ok(),1);
        eq("scan deadline restored",copy.due(Scheduler::IKBD_AUTOSEND),period+150000);
        copy.ikbd.bootRom(); eq("boot keeps monitor source idle",copy.due(Scheduler::IKBD_MONITOR),-1);
        eq("boot preserves active deadline",copy.due(Scheduler::IKBD_AUTOSEND),period+150000);
    }
    std::printf("[ikbd-mouse-monitor] %d checks, %d FAIL\n",count,fails); return fails;
}
#ifdef NEOST_IKBD_MOUSE_MONITOR_TEST_MAIN
int main() { return testIkbdMouseMonitor() ? 1 : 0; }
#endif
