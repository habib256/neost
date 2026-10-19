// USART TX : TDR/TSR bornés, cadence des caractères et désactivation différée.
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"
#include <cstdio>
#include <vector>

int testMfpTx() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* name, int64_t got, int64_t expected) {
        ++checks;
        if (got != expected) { ++failures; std::printf("  FAIL MFP TX %s: %lld attendu %lld\n", name, (long long)got, (long long)expected); }
    };
    for (int bits = 5; bits <= 8; ++bits) for (int stop = 1; stop <= 3; ++stop)
    for (int parity = 0; parity <= 1; ++parity) {
        Scheduler sched; Mfp serial; serial.setScheduler(&sched);
        sched.setCallback(Scheduler::SERIAL_TX, [&] { serial.onSerialTxEvent(); });
        uint8_t output = 0;
        serial.setSerialSink([&](uint8_t b) { output = b; });
        serial.write8(0xFFFA29, 0x80 | ((8-bits)<<5) | (stop<<3) | (parity<<2));
        serial.write8(0xFFFA2D, 1); serial.write8(0xFFFA2F, 0xFF);
        const int half = 2*(1+bits+parity)+(stop == 1 ? 2 : stop == 2 ? 3 : 4);
        const int64_t due = (neost::pacing::kCpuHzInt*half+19199)/19200;
        eq("format échéance", sched.nextDue(), due);
        sched.runTo(due);
        eq("format masque données", output, (1<<bits)-1);
    }
    Scheduler clock; Mfp mfp; mfp.setScheduler(&clock);
    clock.setCallback(Scheduler::SERIAL_TX, [&] { mfp.onSerialTxEvent(); });
    std::vector<uint8_t> bytes;
    mfp.setSerialSink([&](uint8_t b) { bytes.push_back(b); });
    mfp.write8(0xFFFA29, 0x88); // 8N1, débit de repli 9600
    mfp.write8(0xFFFA07, 0x1E);
    mfp.write8(0xFFFA2B, 1); mfp.setLoopback(true);
    mfp.write8(0xFFFA2F, 0x11); // TE coupé : garde TDR
    eq("TX coupé garde TDR", mfp.read8(0xFFFA2D) & 0x80, 0);
    eq("TX coupé silencieux", bytes.size(), 0);
    mfp.write8(0xFFFA2D, 1);
    const int64_t frame = (neost::pacing::kCpuHzInt*10+9599)/9600;
    eq("première échéance", clock.nextDue(), frame);
    eq("TDR vidé vers TSR", mfp.read8(0xFFFA2D)&0x80, 0x80);
    eq("IRQ TDR vidé", mfp.ipra & 4, 4);
    eq("pas de sortie anticipée", bytes.size(), 0);
    eq("pas de réception anticipée", mfp.read8(0xFFFA2B)&0x80, 0);
    eq("pas underrun en vol", mfp.read8(0xFFFA2D)&0x40, 0);
    mfp.write8(0xFFFA2F, 0x22); mfp.write8(0xFFFA2F, 0x33);
    eq("TDR plein", mfp.read8(0xFFFA2D)&0x80, 0);
    clock.runTo(frame-1); eq("fin moins un cycle", bytes.size(), 0);
    clock.runTo(frame);
    eq("premier octet", bytes.size(), 1); eq("TSR préservé", bytes[0], 0x11);
    eq("bouclage à la fin", mfp.read8(0xFFFA2F), 0x11);
    eq("TDR deuxième transfert", mfp.read8(0xFFFA2D)&0x80, 0x80);
    const int64_t second = (neost::pacing::kCpuHzInt*20+9599)/9600;
    eq("phase sans dérive", clock.nextDue(), second);
    std::vector<uint8_t> state;
    auto save = StateArchive::saver(state); clock.serialize(save); mfp.serialize(save);
    mfp.write8(0xFFFA2D, 0); // le caractère courant finit
    clock.runTo(second);
    eq("écrasement TDR", bytes.back(), 0x33);
    eq("EOT différé", mfp.read8(0xFFFA2D)&0x10, 0x10);
    auto load = StateArchive::loader(state.data(), state.size()); clock.serialize(load); mfp.serialize(load);
    eq("archive valide", load.ok(), true);
    eq("archive échéance", clock.nextDue(), second);
    clock.runTo(second);
    eq("underrun fin", mfp.read8(0xFFFA2D)&0x40, 0x40);
    // MC68901UM §7.3.2 : la lecture immédiate ne peut pas effacer UE.
    eq("underrun lecture précoce conserve", mfp.read8(0xFFFA2D)&0x40, 0x40);
    clock.runTo((neost::pacing::kCpuHzInt*21+9599)/9600);
    eq("underrun lecture après période", mfp.read8(0xFFFA2D)&0x40, 0x40);
    eq("underrun lecture efface", mfp.read8(0xFFFA2D)&0x40, 0);
    eq("IRQ underrun", mfp.ipra & 2, 2);
    mfp.reset(); eq("reset annule", clock.cyclesUntil(Scheduler::SERIAL_TX), -1);
    eq("reset TDR vide", mfp.read8(0xFFFA2D)&0x80, 0x80);
    std::printf("  MFP TX : %d checks, %d failures\n", checks, failures);
    return failures;
}

#ifdef NEOST_MFP_TX_STANDALONE
int main() { return testMfpTx() ? 1 : 0; }
#endif
