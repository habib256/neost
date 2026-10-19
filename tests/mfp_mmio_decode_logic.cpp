// Hatari ioMemTabST/STE : registres impairs ; ioMem.c835 : filler FF pair.
#include "core/Bus.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Checks {
    int count = 0;
    int failures = 0;
    void equal(const char* name, uint32_t got, uint32_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 12)
            std::printf("  FAIL MFP MMIO %s: %08X attendu %08X\n", name, got, expected);
    }
};
}

int testMfpMmioDecode() {
    Checks check;
    for (MachineType model : {MachineType::St, MachineType::Ste, MachineType::MegaSte}) {
        Bus bus;
        Mfp mfp;
        bus.machine = model;
        bus.mfp = &mfp;
        int transmitted = 0;
        mfp.setSerialSink([&](uint8_t) { ++transmitted; });
        bus.write8(0xFFFA03, 0x55);
        bus.write8(0xFFFA05, 0xAA);
        bus.write8(0xFFFA07, 0xFF);
        mfp.raise(Mfp::SRC_TIMERA);
        const uint8_t pending = mfp.ipra;
        for (uint32_t offset = 0; offset < 0x40; offset += 2) {
            const uint32_t address = 0xFFFA00 + offset;
            check.equal("pair byte fautif", bus.busFaultN(address, 1, false), true);
            check.equal("pair byte écrit fautif", bus.busFaultN(address, 1, true), true);
            // L'API Bus ne lève pas elle-même l'exception CPU : on peut aussi
            // vérifier la neutralité de son routage indépendamment du CPU.
            bus.write8(address, 0x12);
            check.equal("pair non mémorisé", bus.read8(address), 0xFF);
        }
        check.equal("pairs sans effet IRQ", mfp.ipra, pending);
        check.equal("pairs sans effet TX", transmitted, 0);
        check.equal("impair AER intact", bus.read8(0xFFFA03), 0x55);
        check.equal("impair DDR intact", bus.read8(0xFFFA05), 0xAA);
        check.equal("mot partiel accepté", bus.busFaultN(0xFFFA02, 2, false), false);
        check.equal("mot filler pair", bus.read16(0xFFFA02), 0xFF55);
        bus.write32(0xFFFA02, 0x12873419);
        check.equal("long deux registres impairs", bus.read32(0xFFFA02), 0xFF87FF19);
        check.equal("miroir bus 24 bits", bus.read32(0xFFFFFA02), 0xFF87FF19);
        bus.write16(0xFFFFFA02, 0x2865);
        check.equal("écriture miroir 24 bits", bus.read16(0xFFFA02), 0xFF65);
        for (uint32_t address = 0xFFFA31; address <= 0xFFFA3F; address += 2) {
            check.equal("impair void sans faute", bus.busFaultN(address, 1, false), false);
            bus.write8(address, 0x12);
            check.equal("impair void non mémorisé", bus.read8(address), 0xFF);
        }
    }
    std::printf("[mfp-mmio-decode] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_MFP_MMIO_DECODE_TEST_MAIN
int main() { return testMfpMmioDecode() ? 1 : 0; }
#endif
