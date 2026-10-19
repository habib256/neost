// Hatari MFP_InputOnChannel : Pending_Time = cycle réel du front, pas son service.
#include "io/Mfp.hpp"
#include <cstdio>

int testMfpRxIrqTiming() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, int64_t got, int64_t expected) {
        ++checks;
        if (got == expected) return;
        ++failures;
        std::printf("  FAIL MFP RX IRQ %s: %lld attendu %lld\n", label,
                    static_cast<long long>(got), static_cast<long long>(expected));
    };
    for (int delay : {0, 3, 4, 10}) {
        Scheduler scheduler;
        Mfp mfp;
        int64_t live = 0;
        scheduler.setLiveClock([&] { return live; });
        mfp.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::SERIAL_RX, [&] { mfp.onSerialRxEvent(); });
        mfp.write8(0xFFFA29, 0x88);
        mfp.write8(0xFFFA2B, 1);
        mfp.write8(0xFFFA07, 0x10);
        mfp.write8(0xFFFA13, 0x10);
        mfp.receiveByte(0x55);
        const int64_t due = scheduler.nextDue();
        live = due + delay;
        scheduler.runTo(live);
        mfp.flushIrqUpdate();
        equal("IPR à réception", mfp.ipra & 0x10, 0x10);
        equal("propagation datée de la réception", mfp.irqPending(), delay >= 4);
        equal("octet reçu", mfp.read8(0xFFFA2F), 0x55);
        if (delay < 4) {
            live = due + 3;
            equal("avant délai matériel", mfp.irqPending(), false);
            live = due + 4;
            equal("front au délai exact", mfp.irqPending(), true);
        }
    }
    std::printf("[mfp-rx-irq-timing] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_MFP_RX_IRQ_TIMING_TEST_MAIN
int main() { return testMfpRxIrqTiming() ? 1 : 0; }
#endif
