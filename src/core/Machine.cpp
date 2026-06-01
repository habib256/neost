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
    sched.setCallback(Scheduler::RENDER,  [this] { onRender(); });
    sched.setCallback(Scheduler::TIMER_B, [this] { onTimerB(); });
    sched.setCallback(Scheduler::HBL,     [this] { onHbl(); });
    sched.setCallback(Scheduler::TIMER_C, [this] { onTimerC(); });
    sched.setCallback(Scheduler::VBL,     [this] { onVbl(); });
}

// Arme le PREMIER événement de chaque source pour la trame courante. Les sources
// se replanifient ensuite elles-mêmes dans leur handler.
void Machine::scheduleFrameEvents() {
    timerCIndex_ = 0;
    renderLine_  = 0;
    tbLine_      = 0;
    hblLine_     = 0;
    shifter.beginFrame();                          // verrouille la résolution

    // Premiers événements de la ligne 0, à leur CYCLE EXACT dans la ligne.
    sched.schedule(Scheduler::RENDER,  DE_END_CYCLE);   // 376 : rendu de la ligne 0
    sched.schedule(Scheduler::TIMER_B, TIMERB_CYCLE);   // 400 : tic event-count
    sched.schedule(Scheduler::HBL,     HBL_CYCLE);      // 508 : HBL niveau 2
    // Timer C ≈ 200 Hz : 4 tics aux lignes 78/156/234/312 (fin de ligne).
    sched.schedule(Scheduler::TIMER_C, static_cast<int64_t>(78 + 1) * CYCLES_PER_LINE);
    // VBL niveau 4 : fin de la ligne 200 (début du VBlank).
    sched.schedule(Scheduler::VBL, static_cast<int64_t>(VISIBLE_LINES + 1) * CYCLES_PER_LINE);
}

void Machine::onRender() {
    // Décode la scanline à la fin de son Display-Enable (cycle 376), avec l'état
    // COURANT des registres (palette/base) — AVANT le tic Timer B (400) et le HBL
    // (508) de la même ligne, dont les handlers changeront les registres pour la
    // ligne SUIVANTE (rasters). Rendu purement « sortie » : n'altère ni CPU ni IRQ.
    const int h = shifter.height();
    if (renderLine_ < h) shifter.renderLine(renderLine_);
    ++renderLine_;
    const int64_t due = static_cast<int64_t>(renderLine_) * CYCLES_PER_LINE + DE_END_CYCLE;
    if (renderLine_ < h && due < static_cast<int64_t>(LINES_PER_FRAME) * CYCLES_PER_LINE)
        sched.schedule(Scheduler::RENDER, due);
}

void Machine::onTimerB() {
    // Timer B en event-count : décompte une fois par ligne affichée (sur DE).
    mfp.hblank();
    cpu.updateIpl();                               // un underflow Timer B → IPL 6
    ++tbLine_;
    if (tbLine_ < VISIBLE_LINES)
        sched.schedule(Scheduler::TIMER_B,
                       static_cast<int64_t>(tbLine_) * CYCLES_PER_LINE + TIMERB_CYCLE);
}

void Machine::onHbl() {
    cpu.raiseHbl();                                // HBL niveau 2 (gaté par le SR)
    ++hblLine_;
    if (hblLine_ < VISIBLE_LINES)
        sched.schedule(Scheduler::HBL,
                       static_cast<int64_t>(hblLine_) * CYCLES_PER_LINE + HBL_CYCLE);
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
        if (next < 0 || next > frameEnd) next = frameEnd;

        // Exécute le CPU jusqu'au prochain événement. m68k_execute termine son
        // instruction en cours et peut DÉPASSER la cible : on AVANCE l'horloge du
        // nombre RÉELLEMENT consommé (carry du dépassement, comme Hatari) → pas de
        // dérive ; l'événement échu est déclenché « en retard » de quelques cycles.
        const int64_t want = next - sched.now();
        const int ran = cpu.run(static_cast<int>(want > 0 ? want : 1));
        sched.runTo(sched.now() + ran);                  // déclenche les handlers échus
    }

    // Lignes restantes : en haute-rés mono (400 lignes), le cadre PAL 313 lignes
    // ne fournit pas un créneau par ligne → on finit le décodage ici. En couleur
    // (≤ 200 lignes) tout a déjà été décodé au fil de la trame : rien à faire.
    const int h = shifter.height();
    while (renderLine_ < h) shifter.renderLine(renderLine_++);
}
