// Hatari acia.c334-347 conserve l'ACIA ; ikbd.c499-519 annule le SCI 6301.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
namespace {
struct Rig {
    Mfp mfp;
    Scheduler scheduler;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    Rig() {
        ikbd.setScheduler(&scheduler);
        scheduler.setLiveClock([this] { return live; });
        scheduler.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        scheduler.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        scheduler.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        ikbd.write8(0, 0x96);
    }
    void advance(int64_t at) { live = at; scheduler.runTo(at); }
    uint8_t status() { return ikbd.read8(0); }
    uint8_t data() { return ikbd.read8(2); }
};
}
int testIkbdAciaReset() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, int64_t actual, int64_t expected) {
        ++checks;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[ikbd-acia-reset] FAIL %s: %lld != %lld\n",
                     label, (long long)actual, (long long)expected);
    };
    {
        Rig rig;
        rig.ikbd.keyEvent(0x39, true); rig.advance(10240);
        equal("RDR plein avant reset", rig.status(), 0x83);
        rig.ikbd.keyEvent(0x39, false);   // relâchée : pas de code de reset (cf. plus bas)
        rig.mfp.reset(); rig.ikbd.resetHw();
        equal("RDR et RIE conservés", rig.status(), 0x83);
        equal("IRQ clavier republiée après reset MFP", rig.mfp.read8(0xFFFA01) & 0x10, 0);
        equal("ancienne donnée lisible", rig.data(), 0x39);
        equal("lecture acquitte RX", rig.status(), 2);
        rig.advance(512240); rig.advance(522480);
        equal("boot répond avec RIE conservé", rig.status(), 0x83);
        equal("réponse boot intacte", rig.data(), 0xF1);
    }
    {
        Rig rig;
        rig.ikbd.keyEvent(0x39, true); rig.ikbd.keyEvent(0x3A, true);
        rig.advance(10240); rig.advance(20480);
        equal("overrun latent avant reset", rig.status(), 0x83);
        rig.ikbd.resetHw();
        equal("RDR original avec overrun latent", rig.data(), 0x39);
        equal("overrun publié après UDR", rig.status(), 0x22);
        rig.ikbd.resetHw();
        equal("overrun visible conservé", rig.status(), 0x22);
        (void)rig.data();
        equal("séquence SR RDR acquitte toujours", rig.status(), 2);
    }
    {
        Rig rig;
        rig.ikbd.write8(0, 0xB6);
        equal("TIE active au repos", rig.status(), 0x82);
        rig.mfp.reset(); rig.ikbd.resetHw();
        equal("TIE conservée au reset", rig.status(), 0x82);
        equal("IRQ TX republiée", rig.mfp.read8(0xFFFA01) & 0x10, 0);
    }
    {
        Rig rig;
        rig.ikbd.write8(2, 0); // NOP : aucun effet firmware à interpréter.
        rig.advance(500); rig.ikbd.resetHw();
        equal("TDR reste plein", rig.status() & 2, 0);
        equal("horloge TDR réarmée au reset", rig.scheduler.rawCyclesUntil(Scheduler::IKBD_TX), 1024);
        rig.advance(1524);
        equal("TDR chargé au front réarmé", rig.status() & 2, 2);
        rig.ikbd.write8(2, 0); rig.advance(2000); rig.ikbd.resetHw();
        equal("second TDR reste plein pendant TSR", rig.status() & 2, 0);
        equal("bits TSR conservés et horloge réarmée", rig.scheduler.rawCyclesUntil(Scheduler::IKBD_TX), 9216);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        rig.ikbd.serialize(save); rig.scheduler.serialize(save);
        Rig restored;
        restored.live = 2000;
        auto load = StateArchive::loader(state.data(), state.size());
        restored.ikbd.serialize(load); restored.scheduler.serialize(load);
        equal("archive pipeline conservé", load.ok(), true);
        restored.advance(11216);
        equal("TDR pas consommé à la fin du TSR", restored.status() & 2, 0);
        restored.advance(12240);
        equal("second TDR consommé au front suivant", restored.status() & 2, 2);
    }
    {
        Rig rig;
        rig.ikbd.write8(0, 0xF6); rig.ikbd.write8(2, 0);
        rig.ikbd.resetHw();
        equal("BREAK conserve TDR bloqué", rig.status() & 2, 0);
        equal("BREAK conserve absence échéance", rig.scheduler.cyclesUntil(Scheduler::IKBD_TX), -1);
        rig.ikbd.write8(0, 0x96);
        equal("libération BREAK reprend TDR", rig.scheduler.cyclesUntil(Scheduler::IKBD_TX), 1024);
    }
    {
        // ROM $F086-$F0DC : touches tenues pendant l'auto-test → le relâchement de la
        // PREMIÈRE du balayage (Ctrl avant Espace) part juste avant $F1.
        Rig rig;
        rig.ikbd.keyEvent(0x39, true); rig.ikbd.keyEvent(0x1D, true);
        rig.advance(30000); rig.data(); rig.advance(30000 + 10240); rig.data();
        rig.ikbd.resetHw();
        const int64_t t = rig.scheduler.now();
        rig.advance(t + 502000 + 10240); equal("touche tenue : relâchement avant $F1", rig.data(), 0x9D);
        rig.advance(t + 502000 + 20480); equal("puis $F1", rig.data(), 0xF1);
    }
    {
        Rig rig;
        rig.ikbd.keyEvent(0x39, true); rig.advance(1000); rig.ikbd.keyEvent(0x39, false);
        rig.ikbd.resetHw();
        equal("SCI sortant annulé", rig.scheduler.cyclesUntil(Scheduler::IKBD_RX), -1);
        rig.advance(10240);
        equal("pas de caractère SCI obsolète", rig.status() & 1, 0);
        rig.advance(503000); rig.advance(513240);
        equal("nouveau boot reçu", rig.data(), 0xF1);
        equal("RIE survit pour prochain caractère", rig.status(), 2);
    }
    std::fprintf(stderr, "[ikbd-acia-reset] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_IKBD_ACIA_RESET_TEST_MAIN
int main() { return testIkbdAciaReset() ? 1 : 0; }
#endif
