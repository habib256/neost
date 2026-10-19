// MC68HC901 §2.10.1/2.10.3 et TSR H/L ; MAME mc68901 rc_w/tc_w.
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 16)
            std::printf("  FAIL MFP internal loopback %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Scheduler scheduler;
    Mfp mfp;
    std::vector<uint8_t> sent;
    explicit Rig(bool timed = true) {
        if (timed) {
            mfp.setScheduler(&scheduler);
            scheduler.setCallback(Scheduler::SERIAL_RX, [this] { mfp.onSerialRxEvent(); });
            scheduler.setCallback(Scheduler::SERIAL_TX, [this] { mfp.onSerialTxEvent(); });
        }
        mfp.setSerialSink([this](uint8_t byte) { sent.push_back(byte); });
        mfp.write8(0xFFFA29, 0x88);
        mfp.write8(0xFFFA2B, 1);
        mfp.write8(0xFFFA07, 0x18);
    }
    void finish(Scheduler::Source source) {
        const int64_t remaining = scheduler.rawCyclesUntil(source);
        if (remaining != INT64_MIN) scheduler.runTo(scheduler.liveNow() + remaining);
    }
};
}

int testMfpInternalLoopback() {
    Checks check;
    for (int outputs : {0, 2, 4, 6}) {
        Rig rig;
        rig.mfp.write8(0xFFFA2D, uint8_t(outputs | 1));
        rig.mfp.write8(0xFFFA2F, 0x55);
        check.equal("pas de réception anticipée", rig.mfp.read8(0xFFFA2B) & 0x80, 0);
        rig.finish(Scheduler::SERIAL_TX);
        check.equal("SO reste observable", rig.sent.size(), 1);
        check.equal("SO donnée", rig.sent.front(), 0x55);
        check.equal("seul H/L11 boucle en interne", rig.mfp.read8(0xFFFA2B) & 0x80, outputs == 6 ? 0x80 : 0);
        check.equal("IRQ RXFULL interne", rig.mfp.ipra & 0x18, outputs == 6 ? 0x10 : 0);
        if (outputs == 6) check.equal("RDR interne", rig.mfp.read8(0xFFFA2F), 0x55);
    }
    for (int outputs : {0, 2, 4, 6})
    for (bool transmitterEnabled : {false, true}) {
        Rig rig;
        rig.mfp.write8(0xFFFA2D, uint8_t(outputs | (transmitterEnabled ? 1 : 0)));
        rig.mfp.receiveByte(0x33);
        rig.finish(Scheduler::SERIAL_RX);
        check.equal("SI ignorée en interne seulement", rig.mfp.read8(0xFFFA2B) & 0x80, outputs == 6 ? 0 : 0x80);
        check.equal("file hôte consommée", rig.mfp.hostRxPending(), 0);
    }
    for (bool timed : {false, true}) {
        Rig rig(timed);
        rig.mfp.setLoopback(true); // le câble externe ne doit pas doubler la livraison
        rig.mfp.write8(0xFFFA2D, 7);
        rig.mfp.write8(0xFFFA2F, 0x66);
        if (timed) rig.finish(Scheduler::SERIAL_TX);
        check.equal("bouclages combinés livraison unique", rig.mfp.read8(0xFFFA2B), 0x81);
        check.equal("première donnée", rig.mfp.read8(0xFFFA2F), 0x66);
        check.equal("pas OE après lecture UDR", rig.mfp.read8(0xFFFA2B), 1);
        rig.mfp.setLoopback(false);
        rig.mfp.write8(0xFFFA2B, 0);
        rig.mfp.write8(0xFFFA2F, 0x77);
        if (timed) rig.finish(Scheduler::SERIAL_TX);
        check.equal("RE0 bloque interne", rig.mfp.read8(0xFFFA2B), 0);
    }
    {
        Rig rig;
        rig.mfp.write8(0xFFFA29, 0xE8); // cinq bits, configuration commune TX/RX
        rig.mfp.write8(0xFFFA2D, 7);
        rig.mfp.write8(0xFFFA2F, 0xFF);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.scheduler.serialize(save);
        rig.mfp.serialize(save);
        rig.mfp.write8(0xFFFA2D, 1);
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        rig.scheduler.serialize(load);
        rig.mfp.serialize(load);
        check.equal("archive mode interne", load.ok(), true);
        rig.finish(Scheduler::SERIAL_TX);
        check.equal("cinq bits restaurés", rig.mfp.read8(0xFFFA2F), 0x1F);
    }
    std::printf("[mfp-internal-loopback] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_MFP_INTERNAL_LOOPBACK_TEST_MAIN
int main() { return testMfpInternalLoopback() ? 1 : 0; }
#endif
