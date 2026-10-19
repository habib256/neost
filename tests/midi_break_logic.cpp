// Break MIDI borné : acia.c Clock_TX IDLE et STOP_BIT, sans onde physique.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <utility>
#include <vector>

int testMidiBreak() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* label, int64_t got, int64_t want) {
        ++checks;
        if (got != want) { ++failures; std::fprintf(stderr,
            "[midi-break] FAIL %s: %lld != %lld\n", label, (long long)got, (long long)want); }
    };
    Scheduler sched;
    Mfp mfp;
    MidiAcia midi(mfp);
    int64_t live = 0;
    std::vector<std::pair<uint8_t, int64_t>> out;
    midi.setScheduler(&sched);
    sched.setLiveClock([&] { return live; });
    sched.setCallback(Scheduler::MIDI_TX, [&] { midi.onTxEmpty(); });
    sched.setCallback(Scheduler::MIDI_RX, [&] { midi.onRxPace(); });
    midi.setMidiSinkTimed([&](uint8_t b, int64_t t) { out.emplace_back(b,t); });
    auto cr = [&](uint8_t b) { midi.write8(4,b); };
    auto data = [&](uint8_t b) { midi.write8(6,b); };
    auto sr = [&] { return midi.read8(4); };
    auto run = [&](int64_t t) { live = t; sched.runTo(t); };
    cr(0x75); data(0x11); data(0x22);
    eq("break holds TDR", sr() & 2, 0);
    eq("break has no TX event", sched.cyclesUntil(Scheduler::MIDI_TX), -1);
    run(1000); eq("break emits nothing",out.size(),0);
    cr(0x35); eq("release next bit edge",sched.rawCyclesUntil(Scheduler::MIDI_TX),24);
    run(1023); eq("before release edge",sr() & 2,0);
    run(1024); eq("release frees TDR",sr() & 0x82,0x82);
    data(0x33); cr(0x75);
    run(3328); eq("active TSR finishes",out.size(),1);
    eq("overwrite retained",out.at(0).first,0x22);
    eq("active timestamp",out.at(0).second,3328);
    eq("pending held after stop",sr() & 2,0);
    eq("pending unscheduled",sched.cyclesUntil(Scheduler::MIDI_TX),-1);
    // CR et attente suffisent à sérialiser le break ; aucune nouvelle case.
    std::vector<uint8_t> saved;
    { auto ar=StateArchive::saver(saved); sched.serialize(ar); midi.serialize(ar); }
    run(4000); cr(0x15); run(6400);
    eq("first release emits held byte",out.size(),2);
    { auto ar=StateArchive::loader(saved.data(),saved.size()); sched.serialize(ar); midi.serialize(ar);
      eq("snapshot in break valid",ar.ok(),true); }
    live=3328; out.clear(); run(5000); eq("restored break holds",out.size(),0);
    cr(0x15); run(7424); eq("restored pending emitted",out.size(),1);
    eq("restored pending byte",out.at(0).first,0x33);
    eq("restored release cadence",out.at(0).second,7424);
    // MMIO en avance sur le dispatch rattrape sous l'ancien CR.
    live=7500; data(0x44); live=8000; cr(0x75);
    run(10060); eq("late CR preserves started TSR",out.size(),2);
    eq("late CR timestamp",out.at(1).second,9984);
    live=11000; cr(0x15); data(0x55); live=11005; cr(0x75); // avant le front réel 11008
    run(14000); eq("break before load cancels departure",out.size(),2);
    // RX et son horloge restent actifs en break ; IRQ RX survit au TIE coupé.
    int polls=0;
    midi.setRxSource([&](uint8_t& b) { ++polls; b=0x66; return true; });
    const auto rxDue=sched.rawCyclesUntil(Scheduler::MIDI_RX);
    cr(0xF5); eq("break preserves RX phase",sched.rawCyclesUntil(Scheduler::MIDI_RX),rxDue);
    run(16560); eq("RX polled in break",polls,1);
    eq("RX IRQ in break",sr() & 0x81,0x81);
    cr(0xB5); eq("RX and TX initially held",sr() & 0x82,0x80);
    run(16640); eq("TX and RX IRQ combined",sr() & 0x83,0x83);
    eq("receive data in break",midi.read8(6),0x66);
    eq("TX IRQ survives RX read",sr() & 0x80,0x80);
    eq("TX drives GPIP4 low",mfp.read8(0xFFFA01) & 0x10,0);
    mfp.setAciaLineKbd(true); cr(0xF5);
    eq("keyboard retains wire OR in break",mfp.read8(0xFFFA01) & 0x10,0);
    midi.receiveExternal(0x67); mfp.setAciaLineKbd(false);
    eq("RX retains wire OR in break",mfp.read8(0xFFFA01) & 0x10,0);
    midi.read8(6);
    eq("last IRQ cause releases GPIP4",mfp.read8(0xFFFA01) & 0x10,0x10);
    cr(0x75); data(0x77); cr(3);
    eq("master reset clears pending",sr(),2);
    eq("master reset cancels TX",sched.cyclesUntil(Scheduler::MIDI_TX),-1);
    eq("master reset preserves RX",sched.rawCyclesUntil(Scheduler::MIDI_RX),2480);
    cr(0x75); data(0x88); midi.reset();
    eq("hardware reset clears break pending",sr(),2);
    eq("hardware reset preserves RX",sched.rawCyclesUntil(Scheduler::MIDI_RX),2480);
    std::fprintf(stderr,"[midi-break] %d checks, %d failures\n",checks,failures);
    return failures;
}
