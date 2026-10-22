// Commande $17 : cadence commune de scrutation, sans dépendance au VBL.
// Référence : Hatari ikbd.c SetJoystickMonitoring / InterruptHandler_AutoSend.
// Les commandes sont injectées à la sortie du récepteur série : ce banc
// mesure le firmware ; le transport CPU est couvert par ikbd_cpu_serial_logic.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
struct Checks {
    int count = 0, failed = 0;
    void equal(const char* label, int64_t got, int64_t want) {
        ++count;
        if (got == want) return;
        ++failed;
        std::fprintf(stderr, "[ikbd-monitor] FAIL %s: got %lld, expected %lld\n",
                     label, (long long)got, (long long)want);
    }
};
struct Rig {
    Scheduler sched;
    Mfp mfp;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    Rig() {
        ikbd.setScheduler(&sched);
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::IKBD_MONITOR, [this] { ikbd.onJoystickMonitor(); });
        sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        sched.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        ikbd.write8(0xFFFC00, 0x96);
    }
    void command(std::initializer_list<uint8_t> bytes) {
        for (auto b : bytes) ikbd.receiveSerialByte(b);
    }
    void warm() { for (int i = 0; i < 21; ++i) ikbd.onVbl(20000); }
    void advance(int64_t cycle) { live = cycle; sched.runTo(cycle); }
    int ready() { return ikbd.read8(0xFFFC00) & 1; }
    int data() { return ikbd.read8(0xFFFC02); }
    int64_t monitorDue() {
        const auto rem = sched.rawCyclesUntil(Scheduler::IKBD_MONITOR);
        return rem == INT64_MIN ? -1 : live + rem;
    }
    int64_t rxDue() {
        const auto rem = sched.rawCyclesUntil(Scheduler::IKBD_RX);
        return rem == INT64_MIN ? -1 : live + rem;
    }
};
// Taux 0 : envoi continu (ROM $F778), au débit de la ligne — 2 octets par échantillon.
constexpr int64_t period(int rate) { return rate ? int64_t(8021247) * rate / 100 : 2 * 10240; }
}

int testIkbdMonitor() {
    Checks c;
    {
        // Taux 0 (ROM $F778) : pas de minuterie, un échantillon chaque fois que la
        // ligne se libère.
        Rig r; r.warm(); r.ikbd.setJoystick(0x89, 0x86);
        r.command({0x17, 0});
        c.equal("rate 0 uses no timer", r.monitorDue(), -1);
        c.equal("rate 0 samples at once on a free line", r.rxDue(), 10240);
        r.advance(10240); c.equal("rate 0 fire bits", r.data(), 3);
        r.advance(20480); c.equal("rate 0 directions", r.data(), 0x96);
        c.equal("rate 0 next sample when the line frees", r.rxDue(), 30720);
    }
    for (int rate : {1, 2, 10, 255}) {
        Rig r; r.warm(); r.live = 123;
        r.command({0x17, uint8_t(rate)});
        const int64_t due = 123 + period(rate);
        c.equal("rate and live clock determine first sample", r.monitorDue(), due);
        for (int i = 0; i < 3; ++i) r.ikbd.onVbl(20000);
        c.equal("VBL does not also emit monitoring packet", r.rxDue(), -1);
        r.ikbd.setJoystick(0x89, 0x86);
        r.advance(due - 1); c.equal("no sample before timer", r.rxDue(), -1);
        r.advance(due);
        c.equal("timer immediately enqueues first byte", r.rxDue(), due + 10240);
        c.equal("timer rearms on its grid", r.monitorDue(), due + period(rate));
        r.advance(due + 10240); c.equal("fire bits combine both ports", r.data(), 3);
        r.advance(due + 20480); c.equal("direction nibbles combine both ports", r.data(), 0x96);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1}); r.advance(12345);
        r.command({0x17, 3});
        c.equal("new rate replaces previous timer", r.monitorDue(), 12345 + period(3));
        r.advance(period(1)); c.equal("previous timer no longer samples", r.rxDue(), -1);
    }
    {
        Rig r; r.command({0x17, 1});
        for (int i = 0; i < 20; ++i) r.ikbd.onVbl(20000);
        r.advance(period(1));
        c.equal("first twenty VBL suppress samples", r.rxDue(), -1);
        c.equal("suppressed timer keeps phase", r.monitorDue(), period(1) * 2);
        r.ikbd.onVbl(20000); r.advance(period(1) * 2);
        c.equal("sampling starts after twenty VBL", r.rxDue(), period(1) * 2 + 10240);
    }
    for (uint8_t exit : {uint8_t(0x14), uint8_t(0x15)}) {
        // $14/$15 quittent la surveillance : retour à la scrutation normale.
        Rig r; r.warm(); r.command({0x17, 1}); r.command({exit});
        c.equal("leaving monitoring stops its timer", r.monitorDue(), -1);
        r.advance(period(1)); c.equal("new mode emits no stale monitor sample", r.rxDue(), -1);
    }
    {
        // $1A n'efface que le bit 5 de $CA : la cadence continue, sans échantillon.
        Rig r; r.warm(); r.command({0x17, 1}); r.command({0x1A});
        c.equal("disabled joysticks keep monitor cadence", r.monitorDue(), period(1));
        r.ikbd.setJoystick(0x81, 0x81);
        r.advance(period(1)); c.equal("disabled joysticks emit no sample", r.rxDue(), -1);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1});
        r.command({0x13});
        r.advance(period(1)); r.advance(period(1) * 2);
        // ROM $FEAC : sous PAUSE, le décompte de surveillance est GELÉ.
        c.equal("paused monitoring freezes its countdown", r.monitorDue(), -1);
        c.equal("pause holds output queue", r.rxDue(), -1);
        // Protocole IKBD Atari (PAUSE OUTPUT) : « the PAUSE OUTPUT command also
        // temporarily stops the monitoring process (i.e. the samples are not
        // enqueued for transmission) ». Hatari les empile : défaut non reproduit.
        r.command({0x11});
        c.equal("paused samples are not queued", r.rxDue(), -1);
        r.ikbd.setJoystick(0x00, 0x81);
        r.advance(period(1) * 3);
        c.equal("sampling resumes on its grid", r.rxDue(), period(1) * 3 + 10240);
        r.advance(period(1) * 3 + 10240); c.equal("resumed sample is current fire", r.data(), 1);
        r.advance(period(1) * 3 + 20480); c.equal("resumed sample is current direction", r.data(), 1);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1});
        r.ikbd.mouseEvent(0, 0, true, true);
        r.advance(period(1)); r.advance(period(1) + 10240);
        c.equal("mouse buttons duplicate into joystick fires", r.data(), 3);
    }
    {
        // ROM 6301 $FBAF : une commande de mode souris, joystick en surveillance
        // ($CA = $22), ramène le joystick en mode événement ($CA = $28) — la
        // surveillance s'arrête (MiSTery ikbd/rom/IKBD.ASM).
        Rig r; r.warm(); r.command({0x17, 1}); r.command({0x08});
        c.equal("mouse mode command ends joystick monitoring", r.monitorDue(), -1);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1}); r.advance(1000);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); r.ikbd.serialize(save); r.sched.serialize(save);
        Rig restored;
        auto load = StateArchive::loader(state.data(), state.size());
        restored.ikbd.serialize(load); restored.sched.serialize(load); restored.live = 1000;
        c.equal("monitor state loads", load.ok(), true);
        c.equal("load preserves remaining sample phase", restored.monitorDue(), period(1));
        restored.advance(period(1));
        c.equal("restored timer enqueues sample", restored.rxDue(), period(1) + 10240);
        c.equal("restored timer keeps period", restored.monitorDue(), period(1) * 2);
    }
    for (bool hardware : {false, true}) {
        Rig r; r.warm(); r.command({0x17, 2}); r.advance(1000);
        if (hardware) r.ikbd.resetHw(); else r.ikbd.bootRom();
        c.equal("reset cancels monitoring", r.monitorDue(), -1);
        // $17 $01 pendant l'init de la ROM : le second octet déborde, tout est perdu.
        r.command({0x17, 1}); r.advance(1000 + period(1));
        c.equal("reset critical window suppresses sample", r.rxDue(), -1);
        c.equal("monitor commanded during init is lost", r.monitorDue(), -1);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1});
        r.advance(period(1) + 21);
        c.equal("late dispatch does not drift next sample", r.monitorDue(), period(1) * 2);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1});
        // Boot-stub reconnu puis $22 : ExeMode CW_BOOT (cf. Ikbd::armCommonBootForTest).
        r.ikbd.armCommonBootForTest();
        r.advance(period(1));
        c.equal("custom execution suppresses monitoring output", r.rxDue(), -1);
        c.equal("custom execution preserves timer grid", r.monitorDue(), period(1) * 2);
    }
    {
        Rig r; r.warm(); r.command({0x17, 1}); r.command({0x13});
        // 512 paquets remplissent exactement la file. Les ticks suivants sont
        // jetés en entier, puis un emplacement libéré permet le paquet suivant.
        for (int i = 1; i <= 513; ++i) r.advance(period(1) * i);
        r.command({0x11});
        const int64_t base = r.live;
        for (int i = 1; i <= 2; ++i) { r.advance(base + i * 10240); r.data(); }
        c.equal("full queue does not stop monitoring timer", r.monitorDue(), period(1) * 514);
    }
    {
        Rig r;
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); r.ikbd.serialize(save);
        const auto at = state.size() - sizeof(int64_t) - sizeof(int) - sizeof(bool);
        for (int64_t bad : {int64_t(-1), int64_t(1), period(255) + 1}) {
            auto forged = state; std::memcpy(forged.data() + at, &bad, sizeof(bad));
            auto load = StateArchive::loader(forged.data(), forged.size()); r.ikbd.serialize(load);
            c.equal("forged monitor period rejected", load.ok(), false);
        }
    }
    std::printf("[ikbd-monitor] %d checks, %d FAIL\n", c.count, c.failed);
    return c.failed;
}

#ifdef NEOST_IKBD_MONITOR_TEST_MAIN
int main() { return testIkbdMonitor() ? 1 : 0; }
#endif
