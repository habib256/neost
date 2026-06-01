// =============================================================================
//  Machine.cpp — Câblage des composants + boucle d'horloge d'une trame.
//
//  Depuis la Phase 1 de cycle-accuracy (cf. docs/CYCLE_ACCURACY.md), la trame est
//  pilotée par un ordonnanceur d'événements datés (`Scheduler`) au lieu d'une
//  boucle « 313 lignes × 512 cycles » avec des `if` en ligne. À ce stade le
//  timing produit reste STRICTEMENT IDENTIQUE (quantum CPU = la ligne) : c'est un
//  refactor de structure, validé par diff de trace. Les phases suivantes
//  resserreront le quantum et ajouteront des sources (Timers A/B/D, FDC, DMA…).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"

Machine::Machine(std::size_t ramBytes) : bus(ramBytes) {
    // Branchement des puces sur le bus (le bus ne possède pas les composants).
    bus.shifter = &shifter;
    bus.psg     = &psg;
    bus.glue    = &glue;
    bus.mfp     = &mfp;
    bus.ikbd    = &ikbd;
    bus.fdc     = &fdc;
    bus.cpu     = &cpu;     // pour rafraîchir l'IPL après chaque accès MMIO

    installSchedulerCallbacks();
}

// -----------------------------------------------------------------------------
//  Ordonnanceur : câblage des handlers et programmation d'une trame.
// -----------------------------------------------------------------------------
void Machine::installSchedulerCallbacks() {
    sched.setCallback(Scheduler::HBL,     [this] { onHbl(); });
    sched.setCallback(Scheduler::TIMER_C, [this] { onTimerC(); });
    sched.setCallback(Scheduler::VBL,     [this] { onVbl(); });
}

// Arme le PREMIER événement de chaque source pour la trame courante. Les sources
// se replanifient ensuite elles-mêmes dans leur handler.
void Machine::scheduleFrameEvents() {
    timerCIndex_ = 0;
    // HBL : fin de la ligne 0 (cycle 512), puis chaque ligne visible.
    sched.schedule(Scheduler::HBL, CYCLES_PER_LINE);
    // Timer C ≈ 200 Hz : 4 tics aux lignes 78/156/234/312 (fin de ligne).
    sched.schedule(Scheduler::TIMER_C, static_cast<int64_t>(78 + 1) * CYCLES_PER_LINE);
    // VBL niveau 4 : fin de la ligne 200 (début du VBlank).
    sched.schedule(Scheduler::VBL, static_cast<int64_t>(VISIBLE_LINES + 1) * CYCLES_PER_LINE);
}

void Machine::onHbl() {
    // Timer B (event-count sur Display Enable) + HBL niveau 2 : lignes visibles.
    mfp.hblank();
    cpu.raiseHbl();
    // Replanifie pour la prochaine ligne visible (HBL aux fins de lignes 0..199).
    const int64_t next = sched.now() + CYCLES_PER_LINE;
    if (next <= static_cast<int64_t>(VISIBLE_LINES) * CYCLES_PER_LINE)
        sched.schedule(Scheduler::HBL, next);
}

void Machine::onTimerC() {
    // Tic système 200 Hz : débloque l'accueil EmuTOS, fait vivre bureau/horloge.
    mfp.raise(Mfp::SRC_TIMERC);
    cpu.updateIpl();
    static constexpr int kLines[4] = {78, 156, 234, 312};   // lignes des 4 tics
    if (++timerCIndex_ < 4)
        sched.schedule(Scheduler::TIMER_C,
                       static_cast<int64_t>(kLines[timerCIndex_] + 1) * CYCLES_PER_LINE);
}

void Machine::onVbl() {
    cpu.raiseVbl();   // interruption trame (niveau 4) — une fois par trame
}

// -----------------------------------------------------------------------------
//  Une trame : exécute le CPU d'événement en événement, puis décode l'écran.
// -----------------------------------------------------------------------------
void Machine::runFrame() {
    sched.reset();                 // horloge de trame à 0
    scheduleFrameEvents();

    const int64_t frameEnd = static_cast<int64_t>(LINES_PER_FRAME) * CYCLES_PER_LINE;
    while (sched.now() < frameEnd) {
        int64_t next = sched.nextDue();
        // Quantum CPU = une ligne (512 cycles) : on borne l'avance même si le
        // prochain événement est plus loin. Comme toutes les échéances sont sur
        // la grille de 512 cycles, chaque pas exécute exactement une ligne →
        // chunking (et donc trace) IDENTIQUES à l'ancien modèle « par blocs ».
        const int64_t cap = sched.now() + CYCLES_PER_LINE;
        if (next < 0 || next > cap) next = cap;
        if (next > frameEnd)        next = frameEnd;

        cpu.run(static_cast<int>(next - sched.now()));   // cycles CPU jusqu'à l'événement
        sched.runTo(next);                               // déclenche les handlers échus
    }

    shifter.renderFrame();         // décode tout l'écran (rés. courante)
}
