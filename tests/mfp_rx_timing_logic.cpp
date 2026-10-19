// Cadence RX USART : format UCR, phase absolue et restauration de la fraction.
// Référence : Hatari rs232.c RS232_HandleUCR et RS232_SetBaudRateFromTimerD.
#include "io/Mfp.hpp"
#include "core/Scheduler.hpp"
#include "core/Pacing.hpp"
#include <cstdio>

int testMfpRxTiming() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* label, int64_t got, int64_t expected) {
        ++checks;
        if (got != expected) {
            ++failures;
            std::printf("  FAIL MFP RX %s: %lld attendu %lld\n", label,
                        (long long)got, (long long)expected);
        }
    };
    for (int bits = 5; bits <= 8; ++bits) for (int stop = 1; stop <= 3; ++stop)
    for (int parity = 0; parity <= 1; ++parity) {
        Scheduler sched; Mfp mfp; mfp.setScheduler(&sched);
        mfp.write8(0xFFFA29, 0x80 | ((8-bits)<<5) | (stop<<3) | (parity<<2));
        const int half = 2*(1+bits+parity)+(stop == 1 ? 2 : stop == 2 ? 3 : 4);
        mfp.receiveByte(1);
        eq("format", sched.nextDue(), (neost::pacing::kCpuHzInt*half + 19199)/19200);
    }
    {
        Scheduler clock; Mfp serial; serial.setScheduler(&clock);
        clock.setCallback(Scheduler::SERIAL_RX, [&] { serial.onSerialRxEvent(); });
        serial.write8(0xFFFA29, 0x88); // 8N1
        serial.receiveByte(1); serial.receiveByte(2);
        const int64_t first = clock.nextDue();
        serial.write8(0xFFFA29, 0xE8); // 5N1, le caractère en vol finit inchangé
        eq("format en vol", clock.nextDue(), first);
        clock.runTo(first);
        eq("nouveau format suivant", clock.nextDue(),
           (neost::pacing::kCpuHzInt*17 + 9599)/9600);
    }
    {
        Scheduler clock; Mfp serial; serial.setScheduler(&clock);
        serial.write8(0xFFFA29, 0x88);
        serial.write8(0xFFFA25, 1);
        serial.write8(0xFFFA1D, 1); // 2,4576MHz /2 /16 /4 = 19200
        clock.cancel(Scheduler::TIMER_D);
        serial.receiveByte(1);
        eq("baud Timer D", clock.nextDue(), (neost::pacing::kCpuHzInt*10+19199)/19200);
    }
    Scheduler sched; Mfp mfp; mfp.setScheduler(&sched);
    int64_t live = 0;
    sched.setLiveClock([&] { return live; });
    sched.setCallback(Scheduler::SERIAL_RX, [&] { mfp.onSerialRxEvent(); });
    // Défaut 9600 8N1 : un callback servi en retard doit garder la phase du fil.
    for (int i=0; i<128; ++i) mfp.receiveByte(uint8_t(i));
    for (int i=1; i<=70; ++i) {
        const int64_t expected = (neost::pacing::kCpuHzInt*10*i + 9599)/9600;
        eq("phase fractionnaire", sched.nextDue(), expected);
        live = expected + 37;
        sched.runTo(expected);
    }
    std::vector<uint8_t> saved;
    auto out = StateArchive::saver(saved); sched.serialize(out); mfp.serialize(out);
    const int64_t due = sched.nextDue();
    live = due + 37; sched.runTo(due);
    const int64_t following = sched.nextDue();
    auto in = StateArchive::loader(saved.data(), saved.size());
    sched.serialize(in); mfp.serialize(in);
    eq("archive valide", in.ok(), true);
    eq("archive échéance", sched.nextDue(), due);
    sched.runTo(due);
    eq("archive fraction", sched.nextDue(), following);
    // Une nouvelle file après silence part de l'horloge hôte courante.
    while (mfp.hostRxPending()) { live = sched.nextDue(); sched.runTo(live); }
    live += 100000;
    mfp.receiveByte(0x55);
    eq("reprise après silence", sched.nextDue(), live + (neost::pacing::kCpuHzInt*10+9599)/9600);
    {
        Scheduler clock; Mfp serial; serial.setScheduler(&clock);
        const int64_t longTime = neost::pacing::kCpuHzInt * 1000;
        clock.setLiveClock([&] { return longTime; });
        serial.receiveByte(1);
        eq("échéance après 1000 secondes", clock.nextDue(),
           longTime + (neost::pacing::kCpuHzInt*10+9599)/9600);
        std::vector<uint8_t> bytes;
        auto save = StateArchive::saver(bytes); serial.serialize(save);
        const std::vector<uint8_t> validBytes = bytes;
        const int64_t forged = INT64_MAX;
        // Les trois champs de phase terminent l'archive MFP.
        std::memcpy(bytes.data()+bytes.size()-2*sizeof(int64_t)-sizeof(int), &forged, sizeof(forged));
        auto load = StateArchive::loader(bytes.data(), bytes.size()); serial.serialize(load);
        eq("échéance forgée rejetée", load.ok(), false);
        bytes = validBytes;
        const int forgedBaud = INT32_MAX;
        // Débit, UCR, file, état TX puis état RX (ordre Mfp::serialize).
        const size_t baudOffset = bytes.size() - (2*sizeof(int)+2*sizeof(uint8_t)+sizeof(uint32_t)
                                  +1+4*sizeof(bool)+3*sizeof(uint8_t)+4*sizeof(int64_t)+sizeof(int)
                                  +sizeof(bool)+2*sizeof(int64_t)+sizeof(int));
        std::memcpy(bytes.data()+baudOffset, &forgedBaud, sizeof(forgedBaud));
        auto badBaud = StateArchive::loader(bytes.data(), bytes.size()); serial.serialize(badBaud);
        eq("débit forgé rejeté", badBaud.ok(), false);
    }
    std::printf("[selftest-mfp-rx-timing] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_MFP_RX_TIMING_TEST_MAIN
int main() { return testMfpRxTiming() ? 1 : 0; }
#endif
