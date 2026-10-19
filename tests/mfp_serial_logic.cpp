// USART MFP : conserver l'octet non lu lors d'un débordement RX hôte.
// Référence : Hatari rs232.c RS232_Update (!ByteReceived), comme le bouclage.
#include "io/Mfp.hpp"
#include "core/Scheduler.hpp"
#include <cstdio>

int testMfpSerial() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, unsigned got, unsigned expected) {
        ++checks;
        if (got == expected) return;
        ++failures;
        std::printf("  FAIL MFP serial %s = $%02X (attendu $%02X)\n", label, got, expected);
    };
    Scheduler sched;
    Mfp mfp;
    mfp.setScheduler(&sched);
    sched.setCallback(Scheduler::SERIAL_RX, [&] { mfp.onSerialRxEvent(); });
    mfp.write8(0xFFFA07, 0x18); // IERA : RXFULL + RXERR
    mfp.write8(0xFFFA2B, 1);    // récepteur actif
    auto deliver = [&](uint8_t byte) {
        mfp.receiveByte(byte);
        sched.runTo(sched.nextDue());
    };
    deliver(0x31);
    equal("premier octet plein", mfp.read8(0xFFFA2B), 0x81);
    equal("première IRQ RXFULL", mfp.read8(0xFFFA0B) & 0x18, 0x10);
    mfp.write8(0xFFFA0B, 0); // acquitte les demandes, sans consommer UDR
    deliver(0x72);
    equal("débordement retenu sans IRQ avant UDR", mfp.read8(0xFFFA0B) & 0x18, 0);
    equal("UDR conserve le premier octet", mfp.read8(0xFFFA2F), 0x31);
    equal("UDR publie IRQ RXERR", mfp.read8(0xFFFA0B) & 0x18, 8);
    equal("UDR ne vide pas l'erreur", mfp.read8(0xFFFA2B), 0x41);
    equal("RSR vide l'erreur", mfp.read8(0xFFFA2B), 1);
    mfp.write8(0xFFFA0B, 0);
    deliver(0xA5);
    equal("réception reprend après consommation", mfp.read8(0xFFFA2B), 0x81);
    equal("octet suivant intact", mfp.read8(0xFFFA2F), 0xA5);
    mfp.write8(0xFFFA2B, 0);
    mfp.write8(0xFFFA0B, 0);
    deliver(0xEE);
    equal("récepteur arrêté : aucun octet", mfp.read8(0xFFFA2B), 0);
    equal("récepteur arrêté : aucune IRQ", mfp.read8(0xFFFA0B) & 0x18, 0);
    std::printf("[selftest-mfp-serial] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_MFP_SERIAL_TEST_MAIN
int main() { return testMfpSerial() ? 1 : 0; }
#endif
