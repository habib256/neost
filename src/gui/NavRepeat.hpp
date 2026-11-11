// =============================================================================
//  NavRepeat.hpp — Répétition d'une navigation au maintien (menu de la borne).
//
//  Un pas au front, une pause, puis un pas à cadence fixe. Le front se juge PAR
//  DIRECTION : avec un seul drapeau « une direction est tenue », appuyer sur haut en
//  tenant déjà gauche n'était pas un front, et le pas attendait la fin de la pause.
//  Logique pure, en millisecondes : testée par tests/selftest_logic.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

namespace neost {

class NavRepeat {
public:
    static constexpr int64_t kFirstDelayMs = 400;   // pause avant la répétition
    static constexpr int64_t kRepeatMs     = 150;   // cadence de la répétition

    // `held` : une bit par direction tenue. Vrai si la navigation avance d'un pas.
    bool tick(uint32_t held, int64_t nowMs) {
        const bool fresh = (held & ~previous_) != 0;
        previous_ = held;
        if (fresh) { next_ = nowMs + kFirstDelayMs; return true; }
        if (held && nowMs >= next_) { next_ = nowMs + kRepeatMs; return true; }
        return false;
    }

private:
    uint32_t previous_ = 0;
    int64_t  next_ = 0;
};

}  // namespace neost
