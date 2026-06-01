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
    enum Source { RENDER, TIMER_A, TIMER_B, TIMER_C, TIMER_D, HBL, VBL, SRC_COUNT };

    using Callback = std::function<void()>;

    Scheduler() { due_.fill(kInactive); }

    void setCallback(Source s, Callback cb) { cb_[s] = std::move(cb); }

    // Réarme l'horloge à 0 et désactive tous les événements (début de trame).
    void reset() {
        now_ = 0;
        for (auto& d : due_) d = kInactive;
    }

    // Programme (ou reprogramme) l'événement `s` au cycle absolu `atCycle`.
    void schedule(Source s, int64_t atCycle) { due_[s] = atCycle; }
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
                due_[s] = kInactive;                 // consommé avant l'appel…
                if (cb_[s]) cb_[s]();                 // …le callback peut replanifier
            }
        }
    }

private:
    static constexpr int64_t kInactive = -1;
    std::array<int64_t, SRC_COUNT>  due_{};      // (rempli à kInactive au ctor)
    std::array<Callback, SRC_COUNT> cb_{};
    int64_t now_ = 0;
};
