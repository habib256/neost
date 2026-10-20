// Parité du rendu mono et du repli stéréo, mêmes octets capturés au faisceau.
// Référence : dmaSnd.c:538-676, routage et FIR par canal AVANT mélange mono.
#include "core/Bus.hpp"
#include "core/DmaSound.hpp"
#include "core/Scheduler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct Rig {
    Bus bus;
    Scheduler sched;
    DmaSound snd{bus};
    Rig(uint8_t mode, bool addYm, bool oddPair = false) {
        bus.machine = MachineType::Ste; bus.dmasnd = &snd;
        snd.setScheduler(&sched);
        sched.setCallback(Scheduler::MICROWIRE, [this] { snd.onMicrowireShift(); });
        bus.write16(0xFF8924, 0x07FF);
        bus.write16(0xFF8922, 0x0400 | unsigned(addYm));
        sched.runTo(128);
        for (unsigned i = 0; i < 4096; ++i)
            bus.ram[0x20000 + i] = oddPair ? uint8_t(12 + (i & 1)) : uint8_t(i * 37 + 11);
        snd.write8(0xFF8903, 2); snd.write8(0xFF8905, 0); snd.write8(0xFF8907, 0);
        snd.write8(0xFF890F, 2); snd.write8(0xFF8911, 0x10); snd.write8(0xFF8913, 0);
        snd.write8(0xFF8921, mode);
        snd.write8(0xFF8901, 3);
        sched.runTo(200000); snd.onHbl();
    }
    void stop() {
        snd.write8(0xFF8901, 0);
        sched.runTo(sched.now() + 20000); snd.onHbl(); // FIFO résiduelle → capture
    }
    std::vector<uint8_t> save() {
        std::vector<uint8_t> bytes;
        auto ar = StateArchive::saver(bytes); snd.serialize(ar);
        return bytes;
    }
    bool load(const std::vector<uint8_t>& bytes) {
        auto ar = StateArchive::loader(bytes.data(), bytes.size()); snd.serialize(ar);
        return ar.ok();
    }
};
}

int testDmaSoundMono() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[dmasound-mono] FAIL: %s\n", label); }
    };
    auto same = [](const std::vector<float>& mono, const std::vector<float>& st) {
        for (std::size_t i = 0; i < mono.size(); ++i)
            if (std::abs(mono[i] - (st[2*i] + st[2*i+1]) * 0.5f) > 1e-7f) return false;
        return true;
    };
    {
        Rig r(0, false, true);
        float out[4] = {};
        r.snd.mix(out, 4, 6258);
        check(out[0] == 0, "disabled FIR preserves one-sample latency");
        check(out[1] == -0.1875f * 12.5f / 128.0f,
              "odd stereo pair retains half-sample in mono downmix");
    }
    for (uint8_t mode : {0u, 1u, 2u, 3u, 0x80u, 0x81u, 0x82u, 0x83u}) {
        for (bool addYm : {false, true}) {
            for (uint32_t rate : {48000u, 6258u}) {
                Rig mono(mode, addYm), stereo(mode, addYm);
                std::vector<float> a(37, 0.125f), ym(37, 0.125f), st(74);
                mono.snd.mix(a.data(), uint32_t(a.size()), rate);
                stereo.snd.mixStereo(st.data(), ym.data(), uint32_t(a.size()), rate, 1);
                check(same(a, st), "running mono equals average of stereo channels");
                mono.stop(); stereo.stop();
                check(mono.snd.read8(0xFF8901) == 0 && stereo.snd.read8(0xFF8901) == 0,
                      "PLAY is clear while residual captured data remains");
                a.assign(2048, 0.125f); ym.assign(2048, 0.125f); st.resize(4096);
                mono.snd.mix(a.data(), uint32_t(a.size()), rate);
                stereo.snd.mixStereo(st.data(), ym.data(), uint32_t(a.size()), rate, 1);
                check(same(a, st), "stopped mono drains same residual data as stereo");
                check(std::any_of(a.begin(), a.end(), [](float x) { return x != 0.125f; }),
                      "PLAY off still renders residual samples");
                check(a.back() == 0.125f, "drained stopped output returns to YM");
                const auto state = mono.save();
                float quiet[] = {0.25f, -0.5f};
                mono.snd.mix(quiet, 2, rate);
                check(quiet[0] == 0.25f && quiet[1] == -0.5f, "quiescent mono leaves YM unchanged");
                check(mono.save() == state, "quiescent mono does not advance state");
            }
        }
    }
    {
        Rig whole(3, true), split(3, true);
        std::vector<float> a(97, 0.1f), b = a;
        whole.snd.mix(a.data(), 97, 48000);
        split.snd.mix(b.data(), 13, 48000);
        const auto state = split.save();
        split.snd.mix(b.data() + 13, 84, 48000);
        check(a == b, "render block boundary preserves fractional phase and FIR");
        check(split.load(state), "mono resampling state restores");
        std::vector<float> replay(84, 0.1f);
        split.snd.mix(replay.data(), 84, 48000);
        check(std::equal(replay.begin(), replay.end(), a.begin() + 13),
              "save/load resumes exact sample and fractional phase");
        const auto unchanged = split.save();
        split.snd.mix(replay.data(), 0, 48000);
        split.snd.mix(replay.data(), 84, 0);
        check(split.save() == unchanged, "zero frames or sample rate do not advance state");
    }
    // PLAY écrit en MILIEU de trame, DMA au repos : le rendu push ne doit pas
    // consommer avant l'écriture les octets que le DAC tire APRÈS elle. Avant le
    // repère capW des événements, l'attaque tombait à l'échantillon 0 quelle que
    // soit la position du PLAY (bruitage avancé jusqu'à une trame, ancien mode).
    for (int64_t playAt : {0, 40000, 80000, 120000}) {
        constexpr int64_t kFrame = 160256;                    // trame PAL STE
        constexpr uint32_t kOut = 960;                        // 48 kHz × 20 ms
        Bus bus; Scheduler sched; DmaSound snd{bus};
        bus.machine = MachineType::Ste; bus.dmasnd = &snd;
        snd.setScheduler(&sched);
        int64_t frameStart = 0;
        snd.setCycleClock([&] { return sched.now() - frameStart; });
        for (unsigned i = 0; i < 8192; ++i) bus.ram[0x20000 + i] = 100;
        snd.write8(0xFF8903, 2); snd.write8(0xFF8905, 0); snd.write8(0xFF8907, 0);
        snd.write8(0xFF890F, 2); snd.write8(0xFF8911, 0x20); snd.write8(0xFF8913, 0);
        snd.write8(0xFF8921, 0x83);                           // mono 50 kHz (défaut reset : 6 kHz stéréo)
        std::vector<float> ym(kOut, 0.0f), st(2 * kOut);
        snd.mixStereo(st.data(), ym.data(), kOut, 48000, kFrame);   // purge l'événement MODE
        frameStart = sched.now();
        for (int64_t c = 512; c <= kFrame; c += 512) {
            if (playAt >= c - 512 && playAt < c) {
                sched.runTo(frameStart + playAt); snd.write8(0xFF8901, 1);
            }
            sched.runTo(frameStart + c); snd.onHbl();
        }
        snd.mixStereo(st.data(), ym.data(), kOut, 48000, kFrame);
        uint32_t onset = kOut;
        for (uint32_t i = 0; i < kOut; ++i) if (st[2 * i] != 0.0f) { onset = i; break; }
        const int64_t expect = playAt * kOut / kFrame;
        char label[96];
        std::snprintf(label, sizeof label, "mid-frame PLAY at cycle %lld starts at its sample (got %u, want %lld)",
                      static_cast<long long>(playAt), onset, static_cast<long long>(expect));
        check(int64_t(onset) >= expect && int64_t(onset) <= expect + 2, label);
    }
    std::printf("[dmasound-mono] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_DMASOUND_MONO_TEST_MAIN
int main() { return testDmaSoundMono() ? 1 : 0; }
#endif
