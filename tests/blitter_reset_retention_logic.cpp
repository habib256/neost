// Hatari blitter.c:271-307 : Blitter_Reset conserve BlitterHalftone et buffer.
#include "core/Bus.hpp"
#include "core/Blitter.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Rig {
    Bus bus;
    Blitter blitter{bus};
    Rig() {
        bus.machine = MachineType::Ste;
        bus.blitter = &blitter;
    }
    void transfer(uint8_t hop, uint8_t skew = 0) {
        bus.write16(0xFF8A20, 2);
        bus.write16(0xFF8A22, 2);
        bus.write32(0xFF8A24, 0x1000);
        for (uint32_t address : {0xFF8A28u, 0xFF8A2Au, 0xFF8A2Cu})
            bus.write16(address, 0xFFFF);
        bus.write16(0xFF8A2E, 2);
        bus.write16(0xFF8A30, 2);
        bus.write32(0xFF8A32, 0x2000);
        bus.write16(0xFF8A36, 1);
        bus.write16(0xFF8A38, 1);
        bus.write16(0xFF8A3A, uint16_t(hop << 8) | 3);
        bus.write16(0xFF8A3C, 0xC000 | skew);
    }
};
}

int testBlitterResetRetention() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, uint32_t actual, uint32_t expected) {
        ++checks;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[blitter-reset-retention] FAIL %s: %X != %X\n",
                     label, actual, expected);
    };
    {
        Rig rig;
        for (unsigned line = 0; line < 16; ++line)
            rig.bus.write16(0xFF8A00 + line * 2, 0xA500 | line);
        rig.blitter.reset(false);
        for (unsigned line = 0; line < 16; ++line)
            equal("halftone conservée au reset chaud",
                  rig.bus.read16(0xFF8A00 + line * 2), 0xA500 | line);
        equal("contrôle réinitialisé", rig.bus.read8(0xFF8A3C), 0);
        equal("compteur réinitialisé", rig.bus.read16(0xFF8A38), 0);
        rig.transfer(1);
        equal("HOP utilise halftone conservée", rig.bus.read16(0x2000), 0xA500);
        rig.blitter.reset(true);
        equal("nouvelle session initialise halftone", rig.bus.read16(0xFF8A00), 0);
    }
    {
        Rig rig;
        rig.bus.write16(0x1000, 0x1234);
        rig.transfer(2);
        rig.blitter.reset(false);
        rig.bus.write16(0x1000, 0xABCD);
        rig.transfer(2, 4);
        equal("pipeline source conservé au reset chaud", rig.bus.read16(0x2000), 0x4ABC);
        rig.blitter.reset(true);
        rig.transfer(2, 4);
        equal("nouvelle session initialise pipeline", rig.bus.read16(0x2000), 0x0ABC);
    }
    {
        // GPU_DONE (GPIP3) recopie BUSY : un reset chaud en plein blit doit la
        // relâcher, sinon $FFFA01 bit3 reste à 1 (blit fantôme) après le reset.
        Rig rig;
        Mfp mfp;
        mfp.setHasBlitter(true);
        rig.bus.mfp = &mfp;
        equal("GPIP3 au repos", mfp.read8(0xFFFA01) & 0x08, 0);
        mfp.setBlitterLine(false);               // départ d'un blit (Blitter::start)
        equal("GPIP3 pendant le blit", mfp.read8(0xFFFA01) & 0x08, 0x08);
        mfp.reset();                             // ordre de Machine::reset
        rig.blitter.reset(false);
        equal("GPIP3 relâché par le reset du blitter", mfp.read8(0xFFFA01) & 0x08, 0);
        equal("reset : aucune requête GPIP3 pendante", mfp.iprb & 0x08, 0);
    }
    std::fprintf(stderr, "[blitter-reset-retention] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_BLITTER_RESET_RETENTION_TEST_MAIN
int main() { return testBlitterResetRetention() ? 1 : 0; }
#endif
