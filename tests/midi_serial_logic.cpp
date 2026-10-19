// TDR/TSR MIDI standard /16 8N1 : midi.c (temps) + acia.c (case TDR).
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

int testMidiSerial() {
    int failures = 0, checks = 0;
    auto eq = [&](const char* name, int64_t got, int64_t want) {
        ++checks;
        if (got != want) {
            ++failures;
            std::fprintf(stderr, "[midi-serial] FAIL %s: %lld != %lld\n", name,
                         (long long)got, (long long)want);
        }
    };
    {
        Mfp bareMfp;
        MidiAcia bare(bareMfp);
        int data = -1;
        bare.setMidiSink([&](uint8_t b) { data = b; });
        bare.write8(0xFFFC04, 0x15); // /16 8N1 explicite
        bare.write8(0xFFFC06, 0x42);
        eq("without scheduler synchronous fallback", data, 0x42);
        eq("without scheduler remains ready", bare.read8(0xFFFC04), 2);
    }
    Scheduler sched;
    Mfp mfp;
    MidiAcia midi(mfp);
    int64_t live = 0;
    std::vector<std::pair<uint8_t, int64_t>> out;
    midi.setScheduler(&sched);
    sched.setLiveClock([&] { return live; });
    sched.setCallback(Scheduler::MIDI_TX, [&] { midi.onTxEmpty(); });
    midi.setMidiSinkTimed([&](uint8_t b, int64_t cycle) { out.emplace_back(b, cycle); });
    auto advance = [&](int64_t t) { live = t; sched.runTo(t); };
    auto status = [&] { return midi.read8(0xFFFC04); };
    auto write = [&](uint8_t b) { midi.write8(0xFFFC06, b); };
    midi.write8(0xFFFC04, 0x15);
    write(0x90);
    eq("write clears TDRE without TIE", status() & 2, 0);
    eq("no immediate output", out.size(), 0);
    advance(255); eq("before bit edge", status() & 2, 0);
    advance(256); eq("TDR to TSR", status() & 2, 2);
    eq("still no output", out.size(), 0);
    write(0x30); write(0x40);
    advance(2559); eq("pending TDR stays full", status() & 2, 0);
    advance(2560); eq("first stop outputs", out.size(), 1);
    eq("first data", out.at(0).first, 0x90);
    eq("first timestamp", out.at(0).second, 2560);
    advance(2815); eq("pending before next bit", status() & 2, 0);
    advance(2816); eq("pending copied after stop", status() & 2, 2);
    midi.write8(0xFFFC04, 0x35);
    eq("TIE raises ready IRQ", status() & 0x80, 0x80);
    midi.write8(0xFFFC04, 0x15);
    eq("TIE off only IRQ", status() & 0x80, 0);
    advance(5200);
    eq("second byte output", out.size(), 2);
    eq("pending overwrite data", out.at(1).first, 0x40);
    eq("late callback preserves timestamp", out.at(1).second, 5120);

    // Horloge CPU en avance sur le dispatch : MMIO rattrape le matériel.
    live = 5300; write(0x71);
    eq("write waits existing clock phase", sched.rawCyclesUntil(Scheduler::MIDI_TX), 76);
    live = 5600;
    eq("live read observes TDRE", status() & 2, 2);
    live = 5700; write(0x72);
    std::vector<uint8_t> bytes;
    size_t midiEnd = 0;
    { auto ar = StateArchive::saver(bytes); midi.serialize(ar);
      midiEnd = ar.saveSize(); sched.serialize(ar); }
    {
        Mfp invalidMfp;
        MidiAcia invalid(invalidMfp);
        auto corrupt = bytes;
        const int64_t impossible = INT64_MAX;
        std::memcpy(corrupt.data() + midiEnd - sizeof(int64_t), &impossible, sizeof(impossible));
        auto ar = StateArchive::loader(corrupt.data(), corrupt.size());
        invalid.serialize(ar);
        eq("forged deadline rejected before arithmetic", ar.ok(), false);
    }
    advance(11000);
    eq("late dispatch drains two registers", out.size(), 4);
    eq("live first emission", out.at(2).second, 7680);
    eq("live second emission", out.at(3).second, 10240);
    out.clear();
    { auto ar = StateArchive::loader(bytes.data(), bytes.size());
      midi.serialize(ar); sched.serialize(ar); eq("snapshot valid", ar.ok(), true); }
    live = 5700;
    advance(11000);
    eq("snapshot emits two pending bytes", out.size(), 2);
    eq("snapshot TSR", out.at(0).first, 0x71);
    eq("snapshot TDR", out.at(1).first, 0x72);
    eq("snapshot timestamp", out.at(1).second, 10240);

    out.clear();
    midi.setMidiSinkTimed({});
    midi.setLoopback(true);
    live = 12000; write(0x55);
    eq("loopback deferred", status() & 1, 0);
    advance(14335); eq("loopback before stop", status() & 1, 0);
    advance(14336); eq("loopback at stop", status() & 1, 1);
    eq("loopback byte", midi.read8(0xFFFC06), 0x55);
    live = 15000; write(0x66);
    midi.write8(0xFFFC04, 3);
    eq("master reset cancels TX", sched.cyclesUntil(Scheduler::MIDI_TX), -1);
    advance(18000); eq("master reset no ghost loopback", status() & 1, 0);
    midi.write8(0xFFFC04, 0x15); // master reset n'implique pas 8N1 /16
    write(0x77); advance(18256); write(0x78);
    midi.reset();
    eq("hardware reset cancels TX", sched.cyclesUntil(Scheduler::MIDI_TX), -1);
    advance(24000); eq("hardware reset TDRE only", status(), 2);
    std::fprintf(stderr, "[midi-serial] %d checks, %d failures\n", checks, failures);
    return failures;
}
