// Hatari mfp.c ActiveEdge_WriteByte + video.c Video_AddInterruptTimerB (strict >).
#include "core/Machine.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>

namespace {
struct Checks {
    int count = 0;
    int failures = 0;

    void equal(const char* name, int64_t got, int64_t wanted) {
        ++count;
        if (got == wanted) return;
        ++failures;
        if (failures < 12)
            std::printf("  FAIL Machine Timer B AER %s: %lld attendu %lld\n",
                        name, (long long)got, (long long)wanted);
    }
};

struct Rig {
    std::unique_ptr<Machine> machine;
    Shifter::Geometry geometry;
    int64_t live = 0;
    int64_t frameStart = 0;

    explicit Rig(int videoMode)
        : machine(std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, MachineType::St)) {
        machine->shifter.mode = videoMode == 2 ? Shifter::Mode::High : Shifter::Mode::Low;
        machine->shifter.sync = videoMode == 0 ? 2 : 0;
        // Une ROM synthétique contient les vecteurs de reset et un seul NOP.
        // Le pas CPU amorce une vraie trame Machine, sans exécuter un TOS.
        machine->bus.rom.assign(0x30000, 0);
        machine->bus.romBase = 0xE00000;
        const uint8_t rom[] = {0, 1, 0, 0, 0, 0xE0, 0, 8, 0x4E, 0x71};
        std::copy(std::begin(rom), std::end(rom), machine->bus.rom.begin());
        machine->cpu.reset();
        machine->stepInstruction();
        geometry = machine->shifter.geometry();
        live = machine->sched.liveNow();
        frameStart = live - machine->frameRelCycle();
        machine->sched.setLiveClock([this] { return live; });
    }

    int64_t cycle(int line, int position) const {
        return frameStart + int64_t(line) * geometry.cyclesPerLine + position;
    }
    int edge(int line, bool start) { return machine->shifter.timerBPosForLine(line, start); }
    int64_t deadline(Scheduler::Source source = Scheduler::TIMER_B) {
        const int64_t remaining = machine->sched.rawCyclesUntil(source);
        return remaining == INT64_MIN ? -1 : live + remaining;
    }
    void advance(int64_t target) {
        // runTo sert chaque source au plus une fois par lot. Avancer d'événement
        // en événement évite d'introduire des lignes non dispatchées dans le banc.
        while (machine->sched.nextDue() >= 0 && machine->sched.nextDue() <= target) {
            live = machine->sched.nextDue();
            machine->sched.runTo(live);
        }
        live = target;
        machine->sched.runTo(target);
    }
    void selectStart(bool start) { machine->bus.write8(0xFFFA03, start ? 8 : 0); }
    void arm(int count, int mode) {
        machine->bus.write8(0xFFFA21, uint8_t(count));
        machine->bus.write8(0xFFFA1B, uint8_t(mode));
    }
    int count() { return machine->bus.read8(0xFFFA21); }
};
}

int testMachineTimerBAer() {
    Checks check;
    for (int videoMode = 0; videoMode < 3; ++videoMode)
    for (bool displayed : {false, true})
    for (bool eventMode : {false, true}) {
        Rig rig(videoMode);
        const int line = displayed ? rig.geometry.dispStartLine : 0;
        const int start = rig.edge(line, true);
        const int end = rig.edge(line, false);
        rig.advance(rig.cycle(line, start - 8));
        rig.arm(200, eventMode ? 8 : 0);
        rig.selectStart(true);
        check.equal("fin vers début futur", rig.deadline(), rig.cycle(line, start));
        rig.advance(rig.cycle(line, start));
        check.equal("compte DE sélectionné", rig.count(), 200 - (eventMode && displayed));

        // Le début a déjà été compté : la fin est un second front physique valide.
        rig.advance(rig.cycle(line, (start + end) / 2));
        const int previous = rig.count();
        rig.selectStart(false);
        check.equal("début vers fin même ligne", rig.deadline(), rig.cycle(line, end));
        rig.advance(rig.cycle(line, end));
        check.equal("second front physique", rig.count(), previous - (eventMode && displayed));
    }

    for (int videoMode = 0; videoMode < 3; ++videoMode) {
        Rig rig(videoMode);
        const int line = rig.geometry.dispStartLine;
        const int start = rig.edge(line, true);
        const int end = rig.edge(line, false);
        rig.advance(rig.cycle(line, (start + end) / 2));
        rig.arm(200, 8);
        rig.selectStart(true);
        check.equal("nouveau début passé", rig.deadline(), rig.cycle(line + 1, start));
        rig.advance(rig.cycle(line, end));
        check.equal("ancienne fin annulée sans tic", rig.count(), 200);
    }

    for (int videoMode = 0; videoMode < 3; ++videoMode)
    for (bool oldStart : {false, true})
    for (int late : {0, 10}) {
        Rig rig(videoMode);
        const int line = rig.geometry.dispStartLine;
        rig.selectStart(oldStart);
        const int oldEdge = rig.edge(line, oldStart);
        const int newEdge = rig.edge(line, !oldStart);
        const int64_t oldDeadline = rig.cycle(line, oldEdge);
        rig.advance(oldDeadline - 1);
        rig.arm(1, 8);
        rig.machine->bus.write8(0xFFFA07, 1);
        rig.machine->bus.write8(0xFFFA13, 1);
        // L'horloge CPU a atteint/dépassé le front, mais pas encore le dispatch.
        rig.live = oldDeadline + late;
        rig.selectStart(!oldStart);
        check.equal("ancien front échu consommé", rig.machine->mfp.ipra & 1, 1);
        check.equal("IRQ ancien front datée", rig.machine->mfp.irqPending(), late >= 4);
        const int nextLine = newEdge > oldEdge + late ? line : line + 1;
        check.equal("front suivant après rattrapage", rig.deadline(), rig.cycle(nextLine, newEdge));
    }

    for (int videoMode = 0; videoMode < 3; ++videoMode) {
        Rig rig(videoMode);
        const int start = rig.edge(0, true);
        rig.advance(rig.cycle(0, start - 8));
        rig.arm(200, 1); // mode délai : sa source indépendante ne bouge pas
        const int64_t timerDelay = rig.deadline(Scheduler::TIMER_B_DELAY);
        rig.selectStart(true);
        check.equal("mode délai conserve phase", rig.deadline(Scheduler::TIMER_B_DELAY), timerDelay);
        check.equal("mode délai grille DE replanifiée", rig.deadline(), rig.cycle(0, start));
    }

    {
        Rig rig(0);
        const int line = rig.geometry.dispStartLine;
        const int end = rig.edge(line, false);
        rig.advance(rig.cycle(line, end - 1));
        rig.arm(200, 8);
        rig.live = rig.cycle(line, end + 10);
        rig.selectStart(true);
        const int64_t savedDeadline = rig.deadline();
        std::vector<uint8_t> saved;
        rig.machine->saveState(saved);
        check.equal("sauvegarde après AER MMIO", saved.empty(), false);
        rig.selectStart(false);
        check.equal("chargement après AER MMIO", rig.machine->loadState(saved.data(), saved.size()), true);
        check.equal("archive échéance TIMER_B", rig.deadline(), savedDeadline);
        const int savedCount = rig.count();
        // Un seul dispatch : une reconstruction anticipée replanifierait au même
        // instant sans compter, ce qu'une boucle de rattrapage pourrait masquer.
        rig.live = savedDeadline;
        rig.machine->sched.runTo(rig.live);
        check.equal("archive tic restauré à son échéance", rig.count(), savedCount - 1);
        rig.selectStart(false);
        check.equal("callback restauré toujours câblé", rig.deadline(), rig.cycle(line + 1, end));
    }

    std::printf("[machine-timer-b-aer] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_MACHINE_TIMER_B_AER_TEST_MAIN
int main() { return testMachineTimerBAer() ? 1 : 0; }
#endif
