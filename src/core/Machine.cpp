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
#include <cstdio>

Machine::Machine(std::size_t ramBytes, CpuCore cpuCore, MachineType machine)
    : bus(ramBytes), cpu(bus, cpuCore) {
    machineType_ = machine;
    bus.machine  = machine;         // profil matériel (gating MMIO / bus errors)
    glue.memConfig_ = memConfigForBytes(ramBytes);   // $FF8001 cohérent (EmuTOS recalcule)
    // Branchement des puces sur le bus (le bus ne possède pas les composants).
    bus.shifter = &shifter;
    bus.psg     = &psg;
    bus.glue    = &glue;
    bus.mfp     = &mfp;
    bus.ikbd    = &ikbd;
    bus.fdc     = &fdc;
    bus.dmasnd  = &dmasnd;
    bus.blitter = &blitter;
    bus.rtc     = &rtc;     // horloge RP5C15 (Mega ST / Mega STE)
    bus.midi    = &midi;    // ACIA MIDI ($FFFC04) — bouclage OUT→IN
    bus.cpu     = &cpu;     // pour rafraîchir l'IPL après chaque accès MMIO
    // Horloge faisceau pour le compteur d'adresse vidéo $FF8205/07/09 : cycles
    // écoulés depuis le début de la trame courante (cf. Shifter::videoCounter).
    shifter.setBeamClock([this] { return sched.now() - frameStart_; });
    // Horloge RTC : cycle CPU ABSOLU exact, même au milieu d'une lecture MMIO (on
    // ajoute le delta intra-quantum, car sched.now() ne bouge qu'aux frontières).
    rtc.setClock([this] { return sched.now() + cpu.cyclesRunInQuantum(); });
    // Connecteur de bouclage RS232 : les sorties RTS (port A bit3) et DTR (bit4) du
    // PSG recopient les entrées de contrôle du MFP — RTS→CTS (GPIP2), DTR→DCD (GPIP1)
    // ET DTR→RI (GPIP6) — comme le câble de test du diagnostic « S RS232 ». Le port A
    // est actif BAS (bit=0 → ligne assertée). On rafraîchit l'IPL (un canal a pu lever).
    psg.setPortASink([this](uint8_t a) {
        if (!mfp.loopback()) return;        // connecteur non branché → lignes inertes
        const bool rts = (a & 0x08) != 0;   // bit3 = 1 → RTS assertée (repos bit=0 → désassertée)
        const bool dtr = (a & 0x10) != 0;   // bit4 = 1 → DTR assertée
        mfp.setRs232Cts(rts);
        mfp.setRs232Dcd(dtr);
        mfp.setRs232Ri(dtr);
        cpu.updateIpl();
    });
    mfp.setScheduler(&sched);   // le MFP date lui-même ses timers (A/C/D, mode délai)
    ikbd.setScheduler(&sched);  // l'IKBD diffère sa réponse de reset ($F1)
    fdc.setScheduler(&sched);   // le FDC diffère la fin de commande (BUSY → INTRQ)
    dmasnd.setScheduler(&sched);   // le son DMA date sa fin de trame (→ Timer A)
    dmasnd.setMfp(&mfp);

    installSchedulerCallbacks();
}

// -----------------------------------------------------------------------------
//  Ordonnanceur : câblage des handlers et programmation d'une trame.
// -----------------------------------------------------------------------------
void Machine::installSchedulerCallbacks() {
    sched.setCallback(Scheduler::RENDER,  [this] { onRender(); });
    sched.setCallback(Scheduler::TIMER_B, [this] { onTimerB(); });
    sched.setCallback(Scheduler::HBL,     [this] { onHbl(); });
    sched.setCallback(Scheduler::VBL,     [this] { onVbl(); });
    // Timers MFP en mode délai : datés par le MFP, déclenchés ici (IRQ + IPL).
    sched.setCallback(Scheduler::TIMER_A, [this] { mfp.onTimerExpire(0); cpu.updateIpl(); });
    sched.setCallback(Scheduler::TIMER_C, [this] { mfp.onTimerExpire(2); cpu.updateIpl(); });
    sched.setCallback(Scheduler::TIMER_D, [this] { mfp.onTimerExpire(3); cpu.updateIpl(); });
    // Timer B en mode DÉLAI (≠ event-count) : daté par le MFP, déclenché ici.
    sched.setCallback(Scheduler::TIMER_B_DELAY, [this] { mfp.onTimerExpire(1); cpu.updateIpl(); });
    // Fin de commande disque : BUSY tombe, INTRQ levée (GPIP5 + canal 7).
    sched.setCallback(Scheduler::FDC,     [this] { fdc.onCommandComplete(); cpu.updateIpl(); });
    // Impulsion d'index du lecteur (1/tour) : purement FDC (pas d'IRQ sur ST).
    sched.setCallback(Scheduler::FDC_INDEX, [this] { fdc.onIndexPulse(); });
    // Fin de trame du son DMA STE : pulse Timer A (event-count) → IRQ canal 13.
    sched.setCallback(Scheduler::DMASND, [this] { dmasnd.onFrameEnd(); cpu.updateIpl(); });
    // Réponse de reset du clavier ($F1) : l'IKBD l'a datée → on l'émet + IRQ ACIA.
    sched.setCallback(Scheduler::IKBD,   [this] { ikbd.onResetResponse(); cpu.updateIpl(); });
    // Étape de shift série Microwire ($FF8922 → 0) du son STE.
    sched.setCallback(Scheduler::MICROWIRE, [this] { dmasnd.onMicrowireShift(); });
}

// Arme les événements VIDÉO de la trame courante, à des cycles ABSOLUS (horloge
// continue) = frameStart_ + position dans la trame. Les Timers A/C/D persistent
// d'une trame à l'autre (datés par le MFP) et ne sont PAS réarmés ici.
void Machine::scheduleFrameEvents() {
    renderLine_ = 0;
    tbLine_     = 0;
    hblLine_    = 0;
    shifter.beginFrame();                          // verrouille la résolution

    // Premiers événements de la ligne 0, à leur CYCLE EXACT dans la ligne.
    sched.schedule(Scheduler::RENDER,  frameStart_ + DE_END_CYCLE);   // 376 : rendu ligne 0
    sched.schedule(Scheduler::TIMER_B, frameStart_ + TIMERB_CYCLE);   // 400 : tic event-count
    sched.schedule(Scheduler::HBL,     frameStart_ + HBL_CYCLE);      // 508 : HBL niveau 2
    // VBL niveau 4 : fin de la ligne 200 (début du VBlank).
    sched.schedule(Scheduler::VBL,
                   frameStart_ + static_cast<int64_t>(VISIBLE_LINES + 1) * CYCLES_PER_LINE);
}

void Machine::onRender() {
    // Décode la scanline à la fin de son Display-Enable (cycle 376), avec l'état
    // COURANT des registres (palette/base) — AVANT le tic Timer B (400) et le HBL
    // (508) de la même ligne, dont les handlers changeront les registres pour la
    // ligne SUIVANTE (rasters). Rendu purement « sortie » : n'altère ni CPU ni IRQ.
    const int h = shifter.height();
    if (renderLine_ < h) shifter.renderLine(renderLine_);
    ++renderLine_;
    if (renderLine_ < h && renderLine_ < LINES_PER_FRAME)
        sched.schedule(Scheduler::RENDER,
                       frameStart_ + static_cast<int64_t>(renderLine_) * CYCLES_PER_LINE + DE_END_CYCLE);
}

void Machine::onTimerB() {
    // Timer B en event-count : décompte une fois par ligne affichée (sur DE).
    mfp.hblank();
    cpu.updateIpl();                               // un underflow Timer B → IPL 6
    ++tbLine_;
    if (tbLine_ < VISIBLE_LINES)
        sched.schedule(Scheduler::TIMER_B,
                       frameStart_ + static_cast<int64_t>(tbLine_) * CYCLES_PER_LINE + TIMERB_CYCLE);
}

void Machine::onHbl() {
    cpu.raiseHbl();                                // HBL niveau 2 (gaté par le SR)
    ++hblLine_;
    if (hblLine_ < VISIBLE_LINES)
        sched.schedule(Scheduler::HBL,
                       frameStart_ + static_cast<int64_t>(hblLine_) * CYCLES_PER_LINE + HBL_CYCLE);
}

void Machine::onVbl() {
    cpu.raiseVbl();   // interruption trame (niveau 4) — une fois par trame
}

// -----------------------------------------------------------------------------
//  Une trame : horloge CONTINUE (les timers MFP la traversent). On exécute le CPU
//  d'événement en événement (carry du dépassement), puis on finit le décodage.
// -----------------------------------------------------------------------------
void Machine::runFrame() {
    frameStart_ = sched.now();
    // Le RTC avance désormais en PARESSEUX à la lecture (cf. Rtc::catchUp), piloté
    // par l'horloge émulée — rien à cadencer ici.
    scheduleFrameEvents();

    const int64_t frameEnd = frameStart_ + static_cast<int64_t>(LINES_PER_FRAME) * CYCLES_PER_LINE;
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
