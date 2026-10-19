// Le bus ST décode 24 bits, y compris avant les écritures blitter atomiques.
// Hatari cpu/memory.c: map_banks2 réplique les banques en adressage 24 bits.
// Moira masque déjà ses cycles CPU ; la régression vise aussi l’API Bus directe.
#include "core/Bus.hpp"
#include "core/Blitter.hpp"
#include <cstdio>

int testBusMmioMirror() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, uint32_t actual, uint32_t expected) {
        ++checks;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[bus-mmio-mirror] FAIL %s: %08X != %08X\n",
                     label, actual, expected);
    };

    for (MachineType machine : {MachineType::MegaSt, MachineType::Ste, MachineType::MegaSte}) {
        for (uint32_t upper : {0x01000000u, 0x7F000000u, 0xFF000000u}) {
            Bus bus;
            Blitter blitter(bus);
            bus.machine = machine;
            bus.blitter = &blitter;
            const uint32_t mirror = upper | 0xFF8A00;

            bus.write16(mirror + 0x20, 2);
            equal("registre mot via miroir", bus.read16(0xFF8A20), 2);
            bus.write32(mirror + 0x24, 0x00123456);
            equal("registre long via miroir", bus.read32(0xFF8A24), 0x00123456);
            equal("lecture long via miroir", bus.read32(mirror + 0x24), 0x00123456);
            bus.write8(mirror + 0x3D, 5);
            equal("registre octet via miroir", bus.read8(0xFF8A3D), 5);
            equal("whitelist via miroir", bus.busFault(mirror + 0x20), false);
            bus.write16(mirror + 0x3E, 0x1234);
            equal("vide via miroir", bus.read16(mirror + 0x3E), 0xFFFF);
        }
    }
    // BUSY et skew doivent être posés ensemble avant le transfert HOG.
    for (bool longAccess : {false, true}) {
        for (uint32_t upper : {0u, 0x01000000u}) {
            Bus bus;
            Blitter blitter(bus);
            bus.machine = MachineType::Ste;
            bus.blitter = &blitter;
            bus.write16(0x1000, 0x1234);
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
            bus.write16(0xFF8A3A, 0x0203);
            if (longAccess) bus.write32(upper | 0xFF8A3A, 0x0203C001);
            else bus.write16(upper | 0xFF8A3C, 0xC001);
            equal("skew pris avant BUSY", bus.read16(0x2000), 0x091A);
            equal("transfert terminé", blitter.busy(), false);
            equal("largeur restaurée", bus.ioAccessWidth(), 1);
        }
    }
    {
        Bus bus;
        bus.write16(0xFFFFFE, 0x1234);
        bus.write32(0xFFFFFFFE, 0x56789ABC);
        // Le second mot reboucle vers les vecteurs RAM ; le Bus brut reste
        // distinct de la protection CPU, vérifiée par busFaultN.
        equal("débordement 24 bits", bus.read16(0), 0x9ABC);
        equal("largeur après rebouclage", bus.ioAccessWidth(), 1);
    }
    std::fprintf(stderr, "[bus-mmio-mirror] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_BUS_MMIO_MIRROR_TEST_MAIN
int main() { return testBusMmioMirror() ? 1 : 0; }
#endif
