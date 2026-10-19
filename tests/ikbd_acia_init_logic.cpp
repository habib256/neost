// Hatari ikbd.c1027 : les octets firmware sont jetés avant Clock_Divider !=0.
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Rig {
    Mfp mfp;
    Ikbd ikbd{mfp};
    Scheduler scheduler;
    int64_t live = 0;
    Rig() {
        ikbd.setScheduler(&scheduler);
        scheduler.setLiveClock([this] { return live; });
        scheduler.setCallback(Scheduler::IKBD, [this] { ikbd.onResetResponse(); });
        scheduler.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); });
        scheduler.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); });
    }
    void advance(int64_t at) { live = at; scheduler.runTo(at); }
    bool ready() { return ikbd.read8(0xFFFC00) & 1; }
    uint8_t data() { return ikbd.read8(0xFFFC02); }
};
}
int testIkbdAciaInit() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, int64_t actual, int64_t expected) {
        ++checks;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[ikbd-acia-init] FAIL %s: %lld != %lld\n", label,
                     (long long)actual, (long long)expected);
    };
    {
        Rig rig;
        rig.ikbd.keyEvent(0x39, true);
        equal("pas de SCI avant initialisation", rig.scheduler.cyclesUntil(Scheduler::IKBD_RX), -1);
        rig.advance(10240);
        equal("aucun octet avant initialisation", rig.ready(), false);
        rig.ikbd.write8(0xFFFC00, 3);
        rig.ikbd.keyEvent(0x3A, true);
        rig.advance(20480);
        equal("master reset seul ne configure pas diviseur", rig.ready(), false);
        rig.ikbd.write8(0xFFFC00, 0x96);
        rig.ikbd.keyEvent(0x3B, true);
        rig.advance(30720);
        equal("configuration usuelle permet réception", rig.ready(), true);
        equal("octets antérieurs jetés, pas différés", rig.data(), 0x3B);
        rig.ikbd.write8(0xFFFC00, 3);
        rig.ikbd.keyEvent(0x3C, true);
        rig.advance(40960);
        equal("master reset conserve diviseur déjà établi", rig.ready(), true);
        equal("donnée après master reset", rig.data(), 0x3C);
    }
    for (bool initialized : {false, true}) {
        Rig rig;
        if (initialized) rig.ikbd.write8(0xFFFC00, 0x96);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes);
        rig.ikbd.serialize(save);
        Rig restored;
        auto load = StateArchive::loader(bytes.data(), bytes.size());
        restored.ikbd.serialize(load);
        equal("archive état initialisation valide", load.ok(), true);
        restored.ikbd.keyEvent(0x40, true);
        equal("archive conserve garde émission", restored.scheduler.cyclesUntil(Scheduler::IKBD_RX),
              initialized ? 10240 : -1);

        rig.ikbd.resetHw();
        rig.advance(502000);
        equal("reset matériel conserve état initialisation", rig.scheduler.cyclesUntil(Scheduler::IKBD_RX),
              initialized ? 10240 : -1);
        rig.advance(512240);
        equal("réponse boot selon initialisation conservée", rig.ready(), initialized);
        if (initialized) equal("réponse boot intacte", rig.data(), 0xF1);
    }
    {
        Rig rig;
        rig.ikbd.write8(0xFFFC00, 0); // aucun changement CR0..1 depuis ACIA_Init
        rig.ikbd.keyEvent(0x41, true);
        equal("CR00 identique ne programme pas timer Hatari", rig.scheduler.nextDue(), -1);
        rig.ikbd.write8(0xFFFC00, 3);
        rig.ikbd.write8(0xFFFC00, 0);
        rig.ikbd.keyEvent(0x42, true);
        // Qualifie seulement la garde. Le décodage physique /1 reste hors modèle.
        equal("transition reset vers CR00 initialise la garde", rig.scheduler.nextDue(), 10240);
    }
    {
        Mfp mfp;
        Ikbd ikbd(mfp); // repli protocole sans scheduler : même garde firmware
        ikbd.write8(0xFFFC02, 0x1C); // réponse horloge avec délai global
        equal("réponse avant init jetée sans scheduler", ikbd.read8(0xFFFC00) & 1, 0);
        ikbd.write8(0xFFFC00, 0x96);
        ikbd.keyEvent(0x43, true);
        equal("pas de réponse différée ressuscitée", ikbd.read8(0xFFFC02), 0x43);
    }
    {
        Rig rig;
        // La tranche TX historique reste active avant init ; sa réponse firmware
        // doit être jetée AVANT de poser le délai global de sept bits.
        rig.ikbd.write8(0xFFFC02, 0x1C);
        rig.advance(11264);
        equal("réponse non initialisée ne pose aucun délai", rig.scheduler.cyclesUntil(Scheduler::IKBD_RX), -1);
        rig.ikbd.write8(0xFFFC00, 0x96);
        rig.ikbd.keyEvent(0x44, true);
        equal("initialisation ne conserve pas ancien délai", rig.scheduler.cyclesUntil(Scheduler::IKBD_RX), 10240);
    }
    std::fprintf(stderr, "[ikbd-acia-init] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_IKBD_ACIA_INIT_TEST_MAIN
int main() { return testIkbdAciaInit() ? 1 : 0; }
#endif
