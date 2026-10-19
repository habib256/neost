// Horloge RX MIDI : phase série, retard de dispatch et câblage hors snapshot.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>

int testMidiRxPhase() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* name, int64_t got, int64_t want) {
        ++checks;
        if (got != want) {
            ++failures;
            std::fprintf(stderr, "[midi-rx-phase] FAIL %s: %lld != %lld\n",
                         name, (long long)got, (long long)want);
        }
    };
    Scheduler sched;
    Mfp mfp;
    MidiAcia midi(mfp);
    int64_t live = 731;
    int polls = 0;
    midi.setScheduler(&sched);
    sched.setLiveClock([&] { return live; });
    sched.setCallback(Scheduler::MIDI_RX, [&] { midi.onRxPace(); });
    auto source = [&](uint8_t& b) { b = uint8_t(++polls); return true; };
    auto run = [&](int64_t at) { live = at; sched.runTo(at); };
    auto due = [&] { return live + sched.rawCyclesUntil(Scheduler::MIDI_RX); };
    midi.setRxSource(source);
    eq("initial connection uses live CPU clock", due(), 3291);
    run(3290); eq("no early delivery", polls, 0);
    run(3333); eq("late delivery polls once", polls, 1);
    eq("late delivery retains serial phase", due(), 5851);
    midi.read8(0xFFFC06);
    live = 4000;
    midi.setRxSource(source);
    eq("replacement keeps in-flight frame", due(), 5851);
    // Des resets répétés avant l'échéance ne doivent pas affamer l'entrée.
    for (int64_t at : {4100, 4600, 5100, 5800}) {
        live = at; midi.reset();
        eq("reset retains in-flight frame", due(), 5851);
    }
    run(5851); eq("reset cannot starve reception", polls, 2);
    eq("post-reset reception byte", midi.read8(0xFFFC06), 2);
    midi.write8(0xFFFC04, 3);
    eq("master reset retains serial phase", due(), 8411);
    // Même un dépassement de plusieurs trames ne transforme pas la période.
    run(14000); eq("one poll per scheduler dispatch", polls, 3);
    eq("overdue continuation anchored to prior deadline", due(), 10971);
    run(14000); eq("catchup second frame", polls, 4);
    eq("catchup retains phase", due(), 13531);
    run(14000); eq("catchup third frame", polls, 5);
    eq("catchup next future deadline", due(), 16091);
    std::vector<uint8_t> bytes;
    { auto ar = StateArchive::saver(bytes); sched.serialize(ar); midi.serialize(ar); }
    run(17000);
    { auto ar = StateArchive::loader(bytes.data(), bytes.size());
      sched.serialize(ar); midi.serialize(ar); eq("snapshot valid", ar.ok(), true); }
    live = 14000;
    eq("snapshot preserves in-flight phase", due(), 16091);
    midi.setRxSource({});
    eq("disconnect cancels clock", sched.cyclesUntil(Scheduler::MIDI_RX), -1);
    live = 15000; midi.setRxSource(source);
    eq("reconnect starts new live frame", due(), 17560);
    midi.setRxSource({});
    bytes.clear();
    { auto ar = StateArchive::saver(bytes); sched.serialize(ar); midi.serialize(ar); }
    midi.setRxSource(source);
    { auto ar = StateArchive::loader(bytes.data(), bytes.size());
      sched.serialize(ar); midi.serialize(ar); eq("uncabled snapshot valid", ar.ok(), true); }
    eq("live cable rearmed after uncabled snapshot", due(), 17560);
    run(17560); eq("restored cable receives", polls, 7);
    std::fprintf(stderr, "[midi-rx-phase] %d checks, %d failures\n", checks, failures);
    return failures;
}
