// Garde d'intégration réelle du callback IKBD_MONITOR, à travers Bus et Machine.
#include "core/Machine.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <memory>

int testIkbdMonitorMachine() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[ikbd-monitor-machine] FAIL %s\n", label); }
    };
    auto m = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, MachineType::St);
    constexpr int64_t samplePeriod = neost::pacing::kCpuHzInt / 100;
    for (MachineType profile : {MachineType::St, MachineType::Ste}) {
        m->reconfigure(1024u * 1024u, CpuCore::Moira, profile);
        // L'autosend tourne aussi pendant le drainage du reset : neutraliser
        // les entrées conservées avant toute avance de l'ordonnanceur.
        m->ikbd.setJoystick(0, 0);
        m->reset();
        const int64_t resetDue = m->sched.liveNow() + 502000;
        m->sched.runTo(resetDue); m->sched.runTo(resetDue + 10240);
        (void)m->bus.read8(0xFFFC02); // vider le $F1 avant les paquets de monitoring
        // Les entrées hôte survivent au reset/reconfigure. Ne pas laisser le
        // joystick du profil précédent produire un paquet JOY_AUTO au 21e VBL.
        m->ikbd.setJoystick(0, 0);
        for (int i = 0; i < 21; ++i) m->ikbd.onVbl(20000);
        m->bus.write8(0xFFFC00, 0x96);
        auto send = [&](uint8_t byte) {
            m->bus.write8(0xFFFC02, byte);
            // Attendre le registre à décalage, pas seulement TDRE : le banc
            // mesure ensuite la période firmware après réception complète.
            while (m->sched.cyclesUntil(Scheduler::IKBD_TX) >= 0)
                m->sched.runTo(m->sched.now() + m->sched.cyclesUntil(Scheduler::IKBD_TX));
        };
        send(0x17); send(1);
        m->ikbd.setJoystick(0x89, 0x86);
        const int64_t due = m->sched.liveNow() + samplePeriod;
        check(m->sched.cyclesUntil(Scheduler::IKBD_MONITOR) == samplePeriod,
              "command through Bus arms monitoring");
        m->sched.runTo(due);
        check(m->sched.cyclesUntil(Scheduler::IKBD_RX) == 10240,
              "Machine monitoring callback enqueues serial data");
        check((m->bus.read8(0xFFFC00) & 1) == 0, "sample waits for serial transfer");
        m->sched.runTo(due + 10240);
        check(m->bus.read8(0xFFFC02) == 3, "Machine serial callback delivers fire byte");
        m->sched.runTo(due + 20480);
        check(m->bus.read8(0xFFFC02) == 0x96, "Machine serial callback delivers direction byte");
        check(m->sched.cyclesUntil(Scheduler::IKBD_MONITOR) == samplePeriod - 20480,
              "monitoring phase survives serial callbacks");
        m->reset();
        check(m->sched.cyclesUntil(Scheduler::IKBD_MONITOR) < 0,
              "Machine reset cancels monitoring phase");
        check(m->sched.cyclesUntil(Scheduler::IKBD_RX) < 0,
              "Machine reset cancels queued monitoring data");
    }
    for (MachineType profile : {MachineType::St, MachineType::MegaSte}) {
        // Le constructeur doit armer la scrutation même avant le premier reset.
        auto normal = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, profile);
        normal->bus.write8(0xFFFC00, 0x96);
        for (int i = 0; i < 21; ++i) normal->ikbd.onVbl(20000);
        normal->ikbd.mouseEvent(3, 0, false, false);
        check(normal->sched.cyclesUntil(Scheduler::IKBD_AUTOSEND) == 150000,
              "constructor arms normal autosend");
        // ROM 6301 : le paquet $F8 part dès que la ligne est libre, sans attendre
        // la scrutation.
        check(normal->sched.cyclesUntil(Scheduler::IKBD_RX) == 10240,
              "mouse packet starts on a free line");
        normal->sched.runTo(10240);
        check(normal->bus.read8(0xFFFC02) == 0xF8, "normal packet header through Bus");
        normal->sched.runTo(20480);
        check(normal->bus.read8(0xFFFC02) == 3, "normal mouse movement through Bus");
    }
    std::printf("[ikbd-monitor-machine] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_IKBD_MONITOR_MACHINE_TEST_MAIN
int main() { return testIkbdMonitorMachine() ? 1 : 0; }
#endif
