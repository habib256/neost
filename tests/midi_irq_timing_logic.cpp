// Hatari ACIA_Set_Line_IRQ_MFP/MFP_InputOnChannel : front daté, délai MFP de4cycles.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 16)
            std::printf("  FAIL MIDI IRQ timing %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Scheduler scheduler;
    Mfp mfp;
    MidiAcia midi{mfp};
    int64_t live = 0;
    bool sourceAvailable = true;
    explicit Rig(int path) {
        scheduler.setLiveClock([this] { return live; });
        mfp.setScheduler(&scheduler);
        midi.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::MIDI_RX, [this] { midi.onRxPace(); });
        scheduler.setCallback(Scheduler::MIDI_TX, [this] { midi.onTxEmpty(); });
        mfp.write8(0xFFFA09, 0x40);
        mfp.write8(0xFFFA15, 0x40);
        midi.write8(4, path == 1 ? 0x35 : 0x95); // 8N1 /16 ; TIE ou RIE
        if (path == 0) {
            midi.setRxSource([this](uint8_t& value) {
                value = 0x55;
                if (!sourceAvailable) return false;
                sourceAvailable = false;
                return true;
            });
        } else {
            midi.setLoopback(path == 2);
            midi.write8(6, 0x55);
        }
        mfp.write8(0xFFFA0D, 0); // efface le front TIE initial, TDR est maintenant plein
    }
    int64_t due(Scheduler::Source source) {
        return live + scheduler.rawCyclesUntil(source);
    }
    void run(int64_t time) { live = time; scheduler.runTo(time); mfp.flushIrqUpdate(); }
};
}

int testMidiIrqTiming() {
    Checks check;
    for (int path : {0, 1, 2})
    for (int delay : {0, 3, 4, 40}) {
        Rig rig(path);
        const auto source = path == 0 ? Scheduler::MIDI_RX : Scheduler::MIDI_TX;
        if (path == 2) rig.run(rig.due(source)); // charge TSR, réception au stop seulement
        const int64_t deadline = rig.due(source);
        rig.run(deadline + delay);
        check.equal("front reçu par GPIP4", rig.mfp.iprb & 0x40, 0x40);
        check.equal("propagation depuis échéance", rig.mfp.irqPending(), delay >= 4);
        check.equal("IRQ ACIA propre", rig.midi.read8(4) & 0x80, 0x80);
        if (delay < 4) {
            rig.live = deadline + 3;
            check.equal("avant quatre cycles", rig.mfp.irqPending(), false);
            rig.live = deadline + 4;
            check.equal("quatre cycles exacts", rig.mfp.irqPending(), true);
        }
    }
    {
        Rig rig(0);
        rig.mfp.setAciaLineKbd(true);
        rig.mfp.write8(0xFFFA0D, 0);
        rig.run(rig.due(Scheduler::MIDI_RX) + 40);
        check.equal("clavier masque nouveau front MIDI", rig.mfp.iprb & 0x40, 0);
        rig.midi.read8(6);
        check.equal("acquittement MIDI conserve clavier bas", rig.mfp.read8(0xFFFA01) & 0x10, 0);
        rig.mfp.setAciaLineKbd(false);
        check.equal("libération wireOR", rig.mfp.read8(0xFFFA01) & 0x10, 0x10);
    }
    {
        Rig rig(0);
        rig.mfp.write8(0xFFFA05, 0x10);
        rig.run(rig.due(Scheduler::MIDI_RX) + 40);
        check.equal("DDR sortie bloque IRQ GPIP", rig.mfp.iprb & 0x40, 0);
        check.equal("DDR ne masque pas IRQ ACIA propre", rig.midi.read8(4) & 0x80, 0x80);
    }
    {
        Rig rig(0);
        rig.mfp.write8(0xFFFA03, 0x10); // front remontant : IRQ à l'acquittement RDR
        rig.mfp.write8(0xFFFA0D, 0); // AER a lui-même pu produire un front
        rig.run(rig.due(Scheduler::MIDI_RX) + 40);
        check.equal("AER montant ignore assertion basse", rig.mfp.iprb & 0x40, 0);
        rig.live += 17; // accès MMIO plus récent que l'événement série
        check.equal("lecture RDR", rig.midi.read8(6), 0x55);
        rig.mfp.flushIrqUpdate();
        check.equal("AER montant voit acquittement", rig.mfp.iprb & 0x40, 0x40);
        check.equal("acquittement daté accès courant", rig.mfp.irqPending(), false);
        rig.live += 3;
        check.equal("acquittement avant délai", rig.mfp.irqPending(), false);
        ++rig.live;
        check.equal("acquittement délai exact", rig.mfp.irqPending(), true);
    }
    std::printf("[midi-irq-timing] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_MIDI_IRQ_TIMING_TEST_MAIN
int main() { return testMidiIrqTiming() ? 1 : 0; }
#endif
