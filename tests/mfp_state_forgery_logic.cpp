// Save-state MFP forgé : une source pendante doit porter une date d'arrivée finie.
// raiseAt pose toujours le bit IPR ET pendingTime_ ; un bit sans date (kNever) n'existe
// que dans un fichier forgé, et faisait déborder irqTime_ + kIrqDelayToCpu dans
// updateIrq (UBSan, fuzz des save-states du 2026-11-16).
#include "io/Mfp.hpp"
#include "core/StateArchive.hpp"
#include <cstdio>
#include <vector>

int testMfpStateForgery() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* name, int got, int want) {
        ++checks;
        if (got != want) { ++failures; std::printf("  FAIL MFP state forgery %s: %d attendu %d\n", name, got, want); }
    };
    auto roundTrip = [](Mfp& m) {
        std::vector<uint8_t> bytes;
        auto out = StateArchive::saver(bytes); m.serialize(out);
        Mfp back;
        auto in = StateArchive::loader(bytes.data(), bytes.size()); back.serialize(in);
        return in.ok();
    };
    {   // Témoin : requête réelle sur un canal activé, bit ET date posés.
        Mfp m; m.reset();
        m.ierb = 0x01; m.raiseAt(0, 1000);
        eq("pendant daté accepté", m.iprb & 0x01, 0x01);
        eq("pendant daté rechargé", roundTrip(m), 1);
    }
    {   // Forgé : bit IPR posé à la main, date restée à kNever depuis le reset.
        Mfp m; m.reset();
        m.iprb = 0x01;
        eq("pendant sans date rejeté", roundTrip(m), 0);
    }
    {   // Une date périmée sur une source NON pendante reste sans effet.
        Mfp m; m.reset();
        eq("repos accepté", roundTrip(m), 1);
    }
    std::printf("[selftest-mfp-state-forgery] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
