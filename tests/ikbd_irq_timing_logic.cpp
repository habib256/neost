// ACIA Clock_TX/Clock_RX → GPIP4 : front série, puis propagation MFP de 4 cycles.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[ikbd-irq-timing] FAIL %s: %lld != %lld\n", label,
                     (long long)got, (long long)expected);
    }
};
struct Rig {
    Scheduler scheduler;
    Mfp mfp;
    Ikbd ikbd{mfp};
    int64_t live = 0;
    explicit Rig(bool transmit) {
        scheduler.setLiveClock([this] { return live; });
        mfp.setScheduler(&scheduler);
        ikbd.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
        scheduler.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        mfp.write8(0xFFFA09, 0x40);
        mfp.write8(0xFFFA15, 0x40);
        ikbd.write8(0, transmit ? 0x36 : 0x96); // /64 8N1 : TIE ou RIE
        if (transmit) ikbd.write8(2, 0); // NOP, sans réponse firmware
        else ikbd.keyEvent(0x1E, true);
        mfp.write8(0xFFFA0D, 0); // efface l'assertion TIE initiale
        mfp.flushIrqUpdate();
    }
    void run(int64_t at) { live = at; scheduler.runTo(at); mfp.flushIrqUpdate(); }
};
}

int testIkbdIrqTiming() {
    Checks check;
    for (bool transmit : {false, true})
    for (int delay : {0, 1, 3, 4, 40}) {
        Rig rig(transmit);
        const int64_t deadline = transmit ? 1024 : 10240;
        rig.run(deadline + delay);
        check.equal("front GPIP4", rig.mfp.iprb & 0x40, 0x40);
        check.equal("propagation depuis front série", rig.mfp.irqPending(), delay >= 4);
        check.equal("IRQ ACIA", rig.ikbd.read8(0) & 0x80, 0x80);
        if (delay < 4) {
            rig.live = deadline + 3;
            check.equal("avant délai", rig.mfp.irqPending(), false);
            rig.live = deadline + 4;
            check.equal("délai exact", rig.mfp.irqPending(), true);
        }
    }
    for (bool controlAccess : {false, true}) {
        Rig rig(true);
        // Le CPU accède avant le dispatch TX : le front TDR→TSR est déjà passé.
        // Rattraper jusqu'après le stop prouve qu'il faut publier au START,
        // et non simplement dater l'IRQ à la fin du rattrapage.
        rig.live = 10300;
        rig.ikbd.write8(controlAccess ? 0 : 2, controlAccess ? 0x36 : 0);
        rig.mfp.flushIrqUpdate();
        check.equal("rattrapage MMIO front conservé", rig.mfp.iprb & 0x40, 0x40);
        check.equal("rattrapage MMIO délai déjà écoulé", rig.mfp.irqPending(), true);
    }
    for (bool transmit : {false, true}) {
        Rig rig(transmit);
        rig.mfp.setAciaLineMidi(true);
        rig.mfp.write8(0xFFFA0D, 0);
        rig.run((transmit ? 1024 : 10240) + 40);
        check.equal("MIDI masque nouveau front clavier", rig.mfp.iprb & 0x40, 0);
        if (transmit) rig.ikbd.write8(0, 0x16);
        else rig.ikbd.read8(2);
        check.equal("acquittement conserve MIDI", rig.mfp.read8(0xFFFA01) & 0x10, 0);
        rig.mfp.setAciaLineMidi(false);
        check.equal("libération wire-OR", rig.mfp.read8(0xFFFA01) & 0x10, 0x10);
    }
    for (bool transmit : {false, true}) {
        Rig rig(transmit);
        rig.mfp.write8(0xFFFA05, 0x10);
        rig.run((transmit ? 1024 : 10240) + 40);
        check.equal("DDR sortie bloque front", rig.mfp.iprb & 0x40, 0);
        check.equal("DDR préserve IRQ ACIA", rig.ikbd.read8(0) & 0x80, 0x80);
    }
    for (bool transmit : {false, true}) {
        Rig rig(transmit);
        rig.ikbd.write8(0, 0x16); // le front série ne doit pas armer d'IRQ
        rig.run((transmit ? 1024 : 10240) + 40);
        check.equal("interruptions ACIA désactivées", rig.mfp.iprb & 0x40, 0);
        rig.live += 20;
        const int64_t access = rig.live;
        rig.ikbd.write8(0, transmit ? 0x36 : 0x96);
        rig.mfp.flushIrqUpdate();
        check.equal("activation CR pose IRQ", rig.mfp.iprb & 0x40, 0x40);
        check.equal("activation CR datée accès", rig.mfp.irqPending(), false);
        rig.live = access + 3;
        check.equal("CR avant délai", rig.mfp.irqPending(), false);
        ++rig.live;
        check.equal("CR délai exact", rig.mfp.irqPending(), true);
    }
    for (bool transmit : {false, true}) {
        Rig rig(transmit);
        rig.mfp.write8(0xFFFA03, 0x10); // AER montant : IRQ à la lecture RDR
        rig.mfp.write8(0xFFFA0D, 0);
        rig.run((transmit ? 1024 : 10240) + 40);
        check.equal("AER montant ignore assertion", rig.mfp.iprb & 0x40, 0);
        rig.live += 20;
        const int64_t access = rig.live;
        if (transmit) rig.ikbd.write8(0, 0x16); // coupe TIE, front montant courant
        else check.equal("donnée clavier", rig.ikbd.read8(2), 0x1E);
        rig.mfp.flushIrqUpdate();
        check.equal("accès produit front montant", rig.mfp.iprb & 0x40, 0x40);
        check.equal("front montant daté accès courant", rig.mfp.irqPending(), false);
        rig.live = access + 3;
        check.equal("front montant avant délai", rig.mfp.irqPending(), false);
        rig.live = access + 4;
        check.equal("front montant délai exact", rig.mfp.irqPending(), true);
    }
    std::printf("[ikbd-irq-timing] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}

#ifdef NEOST_IKBD_IRQ_TIMING_TEST_MAIN
int main() { return testIkbdIrqTiming() ? 1 : 0; }
#endif
