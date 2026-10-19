// Hatari PSG_Reset et MAME ay8910_reset_ym : R7=0, volumes=0, silence.
#include "core/Bus.hpp"
#include "core/YM2149.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct PsgResetRig {
    Bus bus;
    YM2149 psg;
    int portACalls = 0, portBCalls = 0;
    std::vector<uint8_t> printed;

    PsgResetRig() {
        bus.psg = &psg;
        psg.reset();
        psg.setPortASink([&](uint8_t) { ++portACalls; });
        psg.setPortBSink([&](uint8_t) { ++portBCalls; });
        psg.setPrinterSink([&](uint8_t value) { printed.push_back(value); });
    }
    void write(int reg, int value) {
        bus.write8(0xff8800, uint8_t(reg));
        bus.write8(0xff8802, uint8_t(value));
    }
    uint8_t read(int reg) {
        bus.write8(0xff8800, uint8_t(reg));
        return bus.read8(0xff8800);
    }
};
}

bool testPsgReset() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "PSG RESET FAIL: %s\n", name);
        }
    };

    for (bool push : {false, true}) {
        PsgResetRig rig;
        int64_t cycle = 50;
        if (push) rig.psg.setCycleClock([&] { return cycle; });

        // Préparer des données, callbacks et événements audio non nuls.
        for (int reg = 0; reg < 16; ++reg) rig.write(reg, 0xff);
        rig.write(8, 15);
        rig.write(9, 15);
        rig.write(10, 15);
        std::vector<float> warmAudio(64);
        if (push) rig.psg.synthesizeFrame(warmAudio.data(), 64, 44100, 160000);
        else rig.psg.synthesize(warmAudio.data(), 64, 44100);
        // Conserver aussi une écriture audio en attente après le bloc de chauffe.
        cycle = 100;
        rig.write(8, 15);
        rig.write(1, 0xfe);
        check(rig.bus.read8(0xff8800) == 0xfe, "setup has unmasked read latch");
        rig.bus.write8(0xff8800, 0x80); // sélecteur invalide avant reset
        const int portABefore = rig.portACalls;
        const int portBBefore = rig.portBCalls;
        const auto printedBefore = rig.printed.size();

        rig.psg.reset();
        check(rig.bus.read8(0xff8800) == 0 && rig.bus.read8(0xff8802) == 0,
              "reset restores selector R0 and zero read latch");
        check(rig.portACalls == portABefore && rig.portBCalls == portBBefore
                  && rig.printed.size() == printedBefore,
              "reset does not issue data callbacks or a printer transfer");
        for (int reg = 0; reg < 16; ++reg) {
            const uint8_t expected = reg == 14 ? 0xff : 0;
            check(rig.read(reg) == expected && rig.bus.read8(0xff8802) == expected,
                  "reset register image is zero except inactive port A");
        }
        check(rig.read(7) == 0, "R7 read latch returns hardware reset value zero");

        // Les volumes nuls suffisent : aucune nécessité de désactiver le mixeur.
        std::vector<float> audio(256, 1.0f);
        if (push) rig.psg.synthesizeFrame(audio.data(), 256, 44100, 160000);
        else rig.psg.synthesize(audio.data(), 256, 44100);
        bool silent = true;
        for (float sample : audio) silent &= std::isfinite(sample) && sample == 0.0f;
        check(silent, "reset clears audio state and queued writes without changing R7");

        // LastStrobe=0 au reset, même si R14 relu vaut FF.
        rig.write(15, 0x66);
        rig.write(14, 0xdf);
        check(rig.printed.size() == printedBefore, "first low strobe after reset is not a falling edge");
        rig.write(14, 0xff);
        rig.write(14, 0xdf);
        check(rig.printed.size() == printedBefore + 1 && rig.printed.back() == 0x66,
              "subsequent real falling strobe transfers port B exactly once");

        rig.psg.reset();
        rig.read(7);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        rig.psg.serialize(save);
        rig.write(7, 0xff);
        auto load = StateArchive::loader(state.data(), state.size());
        rig.psg.serialize(load);
        check(load.ok() && rig.bus.read8(0xff8800) == 0 && rig.bus.read8(0xff8802) == 0,
              "snapshot preserves reset R7 and its read latch");
    }
    return failures == 0;
}

#ifdef PSG_RESET_STANDALONE
int main() { return testPsgReset() ? 0 : 1; }
#endif
