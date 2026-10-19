// Test manuel UDP local, hors selftest (les CI peuvent interdire les sockets).
// c++ -std=c++17 -Isrc tests/midi_ring_logic.cpp src/net/MidiRing.cpp \
//   src/net/Socket.cpp src/io/MidiAcia.cpp src/io/Mfp.cpp -pthread -o /tmp/midi-ring-test
// /tmp/midi-ring-test
#include "net/MidiRing.hpp"
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    MidiRing ring;
    bool opened = false;
    for (int port = 49152; port < 49252 && !opened; ++port)
        opened = ring.open("127.0.0.1:" + std::to_string(port), port);
    if (!opened) {
        std::fprintf(stderr, "[midi-ring] unable to bind local UDP test socket\n");
        return 2;
    }
    for (int i = 0; i < 20; ++i) ring.sendByte(uint8_t(0x40 + i));
    // Le trafic reste sur 127.0.0.1 ; le délai ne date pas l'entrée ST.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ring.poll([](uint8_t) { return false; });
    Mfp mfp;
    Scheduler sched;
    MidiAcia midi(mfp);
    midi.setScheduler(&sched);
    sched.setCallback(Scheduler::MIDI_RX, [&] { midi.onRxPace(); });
    midi.setRxSource([&](uint8_t& b) { return ring.tryPop(b); });
    for (int i = 0; i < 20; ++i) {
        sched.runTo((i + 1) * 2560 - 1);
        if (midi.read8(0xFFFC04) & 1) return 3;
        sched.runTo((i + 1) * 2560);
        if (!(midi.read8(0xFFFC04) & 1)
            || midi.read8(0xFFFC06) != 0x40 + i) {
            std::fprintf(stderr, "[midi-ring] missing or reordered byte %d\n", i);
            return 4;
        }
    }
    uint8_t extra = 0;
    if (ring.tryPop(extra)) return 5;
    midi.setRxSource({});
    std::fprintf(stderr, "[midi-ring] 20 ordered bytes in 51200 cycles, no per-byte UDP poll\n");
    return 0;
}
