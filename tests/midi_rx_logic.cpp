// Réception 6850 : acia.c STOP_BIT, Read_SR, Read_RDR et MasterReset.
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>

int testMidiRx() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* name, int got, int want) {
        ++checks;
        if (got != want) {
            ++failures;
            std::fprintf(stderr, "[midi-rx] FAIL %s: %d != %d\n", name, got, want);
        }
    };
    Mfp mfp;
    MidiAcia midi(mfp);
    auto sr = [&] { return midi.read8(0xFFFC04); };
    auto rdr = [&] { return midi.read8(0xFFFC06); };
    midi.write8(0xFFFC04, 0x95);
    midi.receiveExternal(0x90);
    eq("one completed byte fills RDR", midi.rxCanAccept(), false);
    midi.receiveExternal(0x3C);
    eq("overrun deferred until RDR read", sr(), 0x83);
    eq("unread RDR survives second completion", rdr(), 0x90);
    eq("overrun latched without RX IRQ", sr(), 0x22);
    eq("lost byte never queued", rdr(), 0x90);
    eq("SR then RDR acknowledges OVRN", sr(), 2);
    midi.receiveExternal(0x41); midi.receiveExternal(0x42);
    eq("RDR without SR retains oldest", rdr(), 0x41);
    eq("OVRN survives repeated RDR without SR", rdr(), 0x41);
    eq("OVRN remains latched", sr(), 0x22);
    rdr(); eq("acknowledged", sr(), 2);
    midi.receiveExternal(0x51); midi.receiveExternal(0x52);
    std::vector<uint8_t> bytes;
    auto save = StateArchive::saver(bytes); midi.serialize(save);
    midi.reset();
    auto load = StateArchive::loader(bytes.data(), bytes.size()); midi.serialize(load);
    eq("snapshot valid", load.ok(), true);
    eq("snapshot keeps completed RDR", rdr(), 0x51);
    eq("snapshot keeps pending overrun", sr(), 0x22);
    midi.receiveExternal(0x61); midi.receiveExternal(0x62);
    midi.write8(0xFFFC04, 3);
    eq("master reset clears flags", sr(), 2);
    eq("master reset retains RDR contents", rdr(), 0x61);
    midi.receiveExternal(0x63);
    eq("new completion replaces reset RDR", rdr(), 0x63);
    midi.reset(); eq("machine reset initializes RDR", rdr(), 1);
    eq("machine reset clears status", sr(), 2);
    midi.write8(0xFFFC04, 0xB5);
    midi.receiveExternal(0x71); midi.receiveExternal(0x72); rdr();
    eq("TX IRQ survives RX acknowledgement", sr(), 0xA2);
    mfp.setAciaLineKbd(true); midi.write8(0xFFFC04, 0x15);
    eq("keyboard holds shared GPIP4", mfp.read8(0xFFFA01) & 0x10, 0);
    mfp.setAciaLineKbd(false);
    eq("shared GPIP4 releases", mfp.read8(0xFFFA01) & 0x10, 0x10);
    // La source continue à 2560 cycles même si RDR plein perd une trame.
    Scheduler sched;
    MidiAcia paced(mfp);
    paced.setScheduler(&sched);
    sched.setCallback(Scheduler::MIDI_RX, [&] { paced.onRxPace(); });
    int received = 0;
    paced.setRxSource([&](uint8_t& b) { b = uint8_t(++received); return true; });
    sched.runTo(2559);
    eq("source before frame completion", received, 0);
    sched.runTo(2560);
    sched.runTo(5120);
    eq("source advances despite unread RDR", received, 2);
    eq("paced source retains first byte", paced.read8(0xFFFC06), 1);
    eq("paced source signals overrun", paced.read8(0xFFFC04), 0x22);
    // SR_Read et OVRN visible doivent survivre ensemble au snapshot.
    bytes.clear();
    auto saveAck = StateArchive::saver(bytes); paced.serialize(saveAck);
    paced.reset();
    auto loadAck = StateArchive::loader(bytes.data(), bytes.size()); paced.serialize(loadAck);
    paced.read8(0xFFFC06);
    eq("snapshot preserves SR then RDR acknowledgement", paced.read8(0xFFFC04), 2);
    std::fprintf(stderr, "[midi-rx] %d checks, %d failures\n", checks, failures);
    return failures;
}
