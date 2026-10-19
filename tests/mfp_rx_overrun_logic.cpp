// MC68HC901 §7.2.2 OE ; MAME mc68901 REGISTER_UDR/rx_async_frame_complete.
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 20)
            std::printf("  FAIL MFP RX overrun %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Scheduler scheduler;
    Mfp mfp;
    int64_t live = 0;
    int route;
    explicit Rig(int selected) : route(selected) {
        if (route != 2) {
            scheduler.setLiveClock([this] { return live; });
            mfp.setScheduler(&scheduler);
            scheduler.setCallback(Scheduler::SERIAL_RX, [this] { mfp.onSerialRxEvent(); });
            scheduler.setCallback(Scheduler::SERIAL_TX, [this] { mfp.onSerialTxEvent(); });
        }
        mfp.setLoopback(route != 0);
        mfp.write8(0xFFFA29, 0x88);
        mfp.write8(0xFFFA2B, 1);
        mfp.write8(0xFFFA2D, 1);
        mfp.write8(0xFFFA07, 0x18);
        mfp.write8(0xFFFA13, 0x18);
    }
    void deliver(uint8_t byte) {
        if (route == 0) mfp.receiveByte(byte);
        else mfp.write8(0xFFFA2F, byte);
        if (route != 2) {
            const auto source = route == 0 ? Scheduler::SERIAL_RX : Scheduler::SERIAL_TX;
            live += scheduler.rawCyclesUntil(source);
            scheduler.runTo(live);
        }
        mfp.flushIrqUpdate();
    }
    void clearPending() { mfp.write8(0xFFFA0B, 0); }
};
}

int testMfpRxOverrun() {
    Checks check;
    for (int route : {0, 1, 2}) {
        Rig rig(route);
        rig.deliver(0x11);
        check.equal("premier BF", rig.mfp.read8(0xFFFA2B), 0x81);
        rig.clearPending();
        rig.deliver(0x22);
        rig.deliver(0x33); // plusieurs pertes se coalescent derrière le même RDR
        check.equal("OE invisible avant UDR", rig.mfp.read8(0xFFFA2B), 0x81);
        check.equal("RSR prématuré ne publie pas RXERR", rig.mfp.ipra & 0x18, 0);
        if (route != 2) rig.live += 17; // MMIO hors dispatch : sched.now reste antérieur
        check.equal("UDR conserve premier octet", rig.mfp.read8(0xFFFA2F), 0x11);
        rig.mfp.flushIrqUpdate();
        check.equal("UDR publie RXERR", rig.mfp.ipra & 0x18, 8);
        if (route != 2) {
            check.equal("RXERR délai commence lecture UDR", rig.mfp.irqPending(), false);
            rig.live += 3;
            check.equal("RXERR pas encore visible", rig.mfp.irqPending(), false);
            ++rig.live;
            check.equal("RXERR visible après quatre cycles", rig.mfp.irqPending(), true);
        }
        rig.deliver(0x44);
        check.equal("assemblage bloqué tant que OE visible", rig.mfp.read8(0xFFFA2B), 0x41);
        check.equal("RDR inchangé pendant blocage", rig.mfp.read8(0xFFFA2F), 0x11);
        check.equal("RSR a acquitté OE", rig.mfp.read8(0xFFFA2B), 1);
        rig.clearPending();
        rig.deliver(0x55);
        check.equal("reprise après RSR", rig.mfp.read8(0xFFFA2B), 0x81);
        check.equal("nouvelle donnée après RSR", rig.mfp.read8(0xFFFA2F), 0x55);
    }
    for (bool publish : {false, true}) {
        Rig rig(0);
        rig.deliver(0x66);
        rig.clearPending();
        rig.deliver(0x77);
        if (publish) rig.mfp.read8(0xFFFA2F);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.scheduler.serialize(save);
        rig.mfp.serialize(save);
        rig.mfp.write8(0xFFFA2B, 0);
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        rig.scheduler.serialize(load);
        rig.mfp.serialize(load);
        check.equal("archive état overrun", load.ok(), true);
        if (!publish) {
            check.equal("archive pending conserve BF", rig.mfp.read8(0xFFFA2B), 0x81);
            check.equal("archive UDR déclenche OE", rig.mfp.read8(0xFFFA2F), 0x66);
        }
        check.equal("archive OE visible", rig.mfp.read8(0xFFFA2B), 0x41);
    }
    for (bool machineReset : {false, true})
    for (bool publish : {false, true}) {
        Rig rig(0);
        rig.deliver(0x88);
        rig.deliver(0x99);
        rig.clearPending();
        if (publish) rig.mfp.read8(0xFFFA2F);
        if (machineReset) rig.mfp.reset();
        else rig.mfp.write8(0xFFFA2B, 0);
        rig.mfp.read8(0xFFFA2F);
        check.equal("RE0/reset annule OE retenu", rig.mfp.read8(0xFFFA2B), 0);
        check.equal("RE0 conserve IRQ publiée, reset l’efface", rig.mfp.ipra & 8, !machineReset && publish ? 8 : 0);
    }
    {
        Rig rig(0);
        rig.deliver(0xAA);
        rig.deliver(0xBB);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.mfp.serialize(save);
        // Début MFP : 12 registres +11 lignes, puis RDR/BF/OE/pending.
        bytes[24] = 0; // pending retenu mais RDR prétendument vide
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        rig.mfp.serialize(load);
        check.equal("archive pending sans BF rejetée", load.ok(), false);
    }
    std::printf("[mfp-rx-overrun] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_MFP_RX_OVERRUN_TEST_MAIN
int main() { return testMfpRxOverrun() ? 1 : 0; }
#endif
