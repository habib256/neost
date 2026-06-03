// =============================================================================
//  Scheduler.hpp — Ordonnanceur d'événements datés (horloge en cycles).
//
//  Calqué sur l'idée d'Hatari (`cycInt.c`) : plutôt que de tester chaque source
//  d'interruption à chaque ligne/trame, on garde, par source, le CYCLE auquel son
//  prochain événement est dû. La boucle d'horloge exécute le CPU jusqu'au plus
//  proche événement, puis déclenche les callbacks échus (qui peuvent se
//  replanifier). Voir docs/CYCLE_ACCURACY.md.
//
//  Phase 1 (refactor iso-comportement) : seules les sources actuelles existent
//  (HBL, Timer C, VBL) et le quantum CPU reste la ligne (512 cycles) — le timing
//  produit est identique au modèle « par blocs » précédent. Les phases suivantes
//  ajouteront des sources (Timers A/B/D, FDC, DMA…) et affineront le quantum.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <array>
#include <cstdint>
#include <functional>

class Scheduler {
public:
    // Sources ordonnancées. L'ORDRE de l'énum = priorité à cycle égal (cf. runTo).
    // RENDER ≈ fin Display-Enable (376) ; TIMER_B = event-count sur DE (400, piloté
    // par Machine) ; HBL niveau 2 = 508 ; VBL. Les Timers A/C/D (mode délai) sont
    // datés par le MFP lui-même (période calculée des registres TxCR/TxDR).
    // FDC = fin de commande (BUSY→INTRQ) ; FDC_INDEX = impulsion d'index du
    // lecteur (1/tour, ~200 ms à 300 tr/min) tant que le moteur tourne ;
    // DMASND = fin de trame du son DMA STE (→ event-count Timer A du MFP).
    // IKBD = réponse différée du clavier (ex. $F1 ~502000 cycles après le reset
    // $80,$01, comme Hatari) : l'IRQ ACIA doit arriver PENDANT que le code attend,
    // pas instantanément (sinon elle est levée avant l'armement et perdue).
    // MICROWIRE = shift série du registre Microwire ($FF8922) du son STE : 16
    // décalages (8 cycles chacun, cf. Hatari) avant que la commande LMC1992 soit
    // décodée et que $FF8922 retombe à 0. Des diagnostics POLLENT $FF8922 jusqu'à 0.
    // TIMER_B = tic event-count de Timer B (une fois/ligne affichée, piloté par Machine).
    // TIMER_B_DELAY = Timer B en mode DÉLAI (prescaler/données comme A/C/D), daté par le
    // MFP. Les deux modes de Timer B sont exclusifs (TBCR = 8 → event-count, 1-7 → délai),
    // d'où deux sources distinctes sans conflit.
    enum Source { RENDER, TIMER_A, TIMER_B, TIMER_B_DELAY, TIMER_C, TIMER_D, FDC, FDC_INDEX, DMASND, IKBD, MICROWIRE, HBL, VBL, SRC_COUNT };

    using Callback = std::function<void()>;

    Scheduler() { due_.fill(kInactive); }

    void setCallback(Source s, Callback cb) { cb_[s] = std::move(cb); }

    // Horloge « live » = cycle CPU absolu EXACT, même au milieu d'une instruction
    // (now_ + cycles déjà consommés dans le quantum run() en cours). Indispensable
    // pour dater un timer programmé en plein milieu d'un bloc CPU : sans ça il
    // serait calé sur le DÉBUT du quantum (jusqu'à ~380 cycles trop tôt). C'est le
    // `Cycles_GetClockCounterImmediate()` d'Hatari (cf. cycInt.c). Sous Moira
    // (cœur cycle-exact) cette horloge est précise à la SOUS-instruction.
    void setLiveClock(std::function<int64_t()> fn) { liveClock_ = std::move(fn); }
    int64_t liveNow() const { return liveClock_ ? liveClock_() : now_; }

    // Préemption du timeslice CPU : quand un événement est posé AVANT la cible du
    // bloc CPU en cours, on coupe le bloc (le CPU finit son instruction puis rend
    // la main) pour que la boucle d'horloge re-planifie et s'arrête PRÈS de cet
    // instant → latence IRQ ~1 instruction, comme Hatari (qui re-teste
    // PendingInterruptCount à chaque instruction). Sans ça, un timer court armé en
    // plein bloc ne se déclenche qu'à la fin du bloc (jusqu'à ~380 cycles de retard).
    void setEndSlice(std::function<void()> fn) { endSlice_ = std::move(fn); }
    void beginRun(int64_t target) { runTarget_ = target; }
    void endRun() { runTarget_ = kInactive; }

    // Réarme l'horloge à 0 et désactive tous les événements (début de trame).
    void reset() {
        now_ = 0;
        for (auto& d : due_) d = kInactive;
        runTarget_ = kInactive;
    }

    // Programme (ou reprogramme) l'événement `s` au cycle absolu `atCycle`.
    void schedule(Source s, int64_t atCycle) {
        due_[s] = atCycle;
        // Si on est en plein bloc CPU (runTarget_ armé) et que cet événement tombe
        // AVANT la cible du bloc, on préempte : le CPU rend la main à la prochaine
        // frontière d'instruction et la boucle d'horloge ré-évaluera nextDue().
        if (runTarget_ != kInactive && atCycle < runTarget_ && endSlice_) {
            runTarget_ = atCycle;   // nouvelle cible effective (évite des coupes redondantes)
            ++preemptions;
            endSlice_();
        }
    }
    void cancel(Source s) { due_[s] = kInactive; }

    int64_t now() const { return now_; }

    // Cycle du prochain événement dû (>= now), ou -1 si aucun n'est armé.
    int64_t nextDue() const {
        int64_t best = -1;
        for (int s = 0; s < SRC_COUNT; ++s)
            if (due_[s] != kInactive && (best < 0 || due_[s] < best)) best = due_[s];
        return best;
    }

    // Avance l'horloge jusqu'à `cycle` puis déclenche, DANS L'ORDRE DES SOURCES,
    // tout événement échu (due <= cycle). Chaque callback peut se replanifier
    // (il pose une nouvelle échéance > now, qui ne re-déclenche pas ce tour-ci).
    void runTo(int64_t cycle) {
        now_ = cycle;
        for (int s = 0; s < SRC_COUNT; ++s) {
            if (due_[s] != kInactive && due_[s] <= now_) {
                // Métrique cycle-accuracy : retard d'un timer MFP daté (now - échéance).
                // Sans préemption il peut atteindre ~1 ligne ; avec, il reste ~1 instr.
                if (isMfpTimer(s)) {
                    const int64_t late = now_ - due_[s];
                    if (late > timerMaxLate) timerMaxLate = late;
                }
                due_[s] = kInactive;                 // consommé avant l'appel…
                if (cb_[s]) cb_[s]();                 // …le callback peut replanifier
            }
        }
    }

    // Diagnostics (chantier précision cycle) : pire retard d'un timer MFP daté, et
    // nombre de préemptions du timeslice CPU déclenchées. Lus par le headless.
    int64_t timerMaxLate = 0;
    long    preemptions  = 0;

private:
    static bool isMfpTimer(int s) {              // sources dont le retard dépend de la préemption
        return s == TIMER_A || s == TIMER_B_DELAY || s == TIMER_C || s == TIMER_D;
    }
    static constexpr int64_t kInactive = -1;
    std::array<int64_t, SRC_COUNT>  due_{};      // (rempli à kInactive au ctor)
    std::array<Callback, SRC_COUNT> cb_{};
    int64_t now_ = 0;
    int64_t runTarget_ = kInactive;              // cible du bloc CPU courant (-1 = hors run)
    std::function<int64_t()> liveClock_{};       // horloge sous-quantum (cf. liveNow)
    std::function<void()>    endSlice_{};         // coupe le timeslice CPU (préemption)
};
