// [interne] Anneau MIDI, extension NeoST : aucune affirmation sur le matériel.
// Test UDP local (127.0.0.1), hors selftest : cible CMake `neost-midi-ring-test`, bâtie
// avec NEOST_WITH_NET et lancée par le palier fast de tools/run_all.py (étapes réseau).
//   cmake --build build --target neost-midi-ring-test && ./build/neost-midi-ring-test
#include "net/MidiRing.hpp"
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
// Réception sans socket : doublon, réordre, source étrangère, ancien format brut,
// pair redémarré et relève d'une source muette.
int ingestChecks() {
    MidiRing r;
    const std::string up = "upstream", other = "intruder";
    auto pkt = [](uint32_t session, uint32_t seq, std::vector<uint8_t> p) {
        return MidiRing::encode(session, seq, p.data(), int(p.size()));
    };
    auto feed = [&](const std::vector<uint8_t>& d, const std::string& src, int64_t now) {
        return r.ingest(d.data(), int(d.size()), src, now);
    };
    int fails = 0;
    auto expect = [&](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "[midi-ring] FAIL: %s\n", what); ++fails; }
    };
    expect(feed(pkt(7, 10, {0x90, 0x40}), up, 0), "first datagram latches the upstream");
    expect(!feed(pkt(7, 10, {0x90, 0x40}), up, 1), "duplicate dropped");
    expect(feed(pkt(7, 12, {0x7F}), up, 2), "datagram after a gap accepted");
    expect(!feed(pkt(7, 11, {0x55}), up, 3), "late (reordered) datagram dropped");
    expect(!feed(pkt(9, 13, {0x66}), other, 4), "foreign source dropped while upstream talks");
    const uint8_t raw = 0x42;
    expect(!r.ingest(&raw, 1, up, 5), "legacy one-byte datagram dropped");
    expect(feed(pkt(8, 0, {0x80}), up, 6), "new session from the same source resyncs");
    expect(!feed(pkt(7, 13, {0x13}), up, 6), "late datagram of the retired session dropped");
    expect(!feed(pkt(8, 0, {0x80}), up, 6), "current session not reset by it (duplicate)");
    expect(feed(pkt(9, 50, {0xF8}), other, 6 + MidiRing::kRelatchMs + 1),
           "silent upstream replaced after kRelatchMs");
    expect(!feed(pkt(8, 1, {0x01}), up, 6 + MidiRing::kRelatchMs + 2),
           "old upstream now foreign");
    const std::vector<uint8_t> want = {0x90, 0x40, 0x7F, 0x80, 0xF8};
    std::vector<uint8_t> got;
    uint8_t b = 0;
    while (r.tryPop(b)) got.push_back(b);
    expect(got == want, "delivered bytes are exactly the accepted payloads, in order");
    const MidiRing::Stats& st = r.stats();
    expect(st.accepted == 4 && st.duplicate == 4 && st.foreign == 2 && st.malformed == 1
           && st.lost == 1, "statistics");
    return fails;
}
} // namespace

// Saisie de l'anneau (champ de la page MIDI du GUI, midi_net= de neost.cfg).
int specChecks() {
    int fails = 0;
    auto expect = [&](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "[midi-ring] FAIL: %s\n", what); ++fails; }
    };
    std::string peer, err;
    int listen = 0;
    expect(MidiRing::parseSpec("192.168.1.20:6820", peer, listen) && peer == "192.168.1.20:6820"
           && listen == MidiRing::kDefaultListen, "host:port, default listen port");
    expect(MidiRing::parseSpec(" peer.local:7000:7001 ", peer, listen) && peer == "peer.local:7000"
           && listen == 7001, "host:port:listen, spaces trimmed");
    expect(!MidiRing::parseSpec("peer.local", peer, listen, &err) && !err.empty(), "port missing");
    expect(!MidiRing::parseSpec(":6820", peer, listen), "host missing");
    expect(!MidiRing::parseSpec("h:0", peer, listen), "peer port 0 refused");
    expect(!MidiRing::parseSpec("h:70000", peer, listen), "peer port above 65535 refused");
    expect(!MidiRing::parseSpec("h:6820:x", peer, listen), "listen port not a number");
    expect(!MidiRing::parseSpec("h:68a0", peer, listen), "peer port not a number");
    return fails;
}

int main() {
    if (const int f = ingestChecks()) return 10 + f;
    if (const int f = specChecks()) return 30 + f;
    MidiRing ring;
    bool opened = false;
    for (int port = 49152; port < 49252 && !opened; ++port)
        opened = ring.open("127.0.0.1:" + std::to_string(port), port);
    if (!opened) {
        std::fprintf(stderr, "[midi-ring] unable to bind local UDP test socket\n");
        return 2;
    }
    for (int i = 0; i < 20; ++i) ring.sendByte(uint8_t(0x40 + i));
    ring.flush();                                // 20 octets → un seul datagramme
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
    if (ring.stats().accepted != 1) {
        std::fprintf(stderr, "[midi-ring] expected one datagram, got %llu\n",
                     (unsigned long long)ring.stats().accepted);
        return 6;
    }
    midi.setRxSource({});
    std::fprintf(stderr, "[midi-ring] 20 ordered bytes in one datagram, 51200 cycles; "
                         "duplicate/reorder/foreign/legacy datagrams rejected\n");
    return 0;
}
