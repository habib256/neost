// Registres MMIO : tables ioMem ST/STE et handlers blitter.c Hatari f0736b24.
#include "core/Bus.hpp"
#include "core/Blitter.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <vector>

int testMmioFidelity() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[mmio] FAIL: %s\n", label); }
    };
    constexpr uint32_t base = 0xFF8A00;
    for (MachineType profile : {MachineType::MegaSt, MachineType::Ste, MachineType::MegaSte}) {
        Bus bus;
        Blitter blit(bus);
        bus.machine = profile;
        bus.blitter = &blit;
        for (uint32_t off = 0; off < 0x3A; off += 2) {
            bus.write16(base + off, 0xA5FF);
            check(bus.read8(base + off) == 0xA5 && bus.read8(base + off + 1) == 0xFF,
                  "byte read retains raw word write before hardware mask");
            const uint16_t live = bus.read16(base + off);
            bus.write8(base + off, 0x12);
            bus.write8(base + off + 1, 0x35);
            check(bus.read8(base + off) == 0x12 && bus.read8(base + off + 1) == 0x35,
                  "rejected byte writes remain visible in IO backing store");
            check(bus.read16(base + off) == live, "byte writes do not modify word register");
            check(bus.read8(base + off) == uint8_t(live >> 8)
                  && bus.read8(base + off + 1) == uint8_t(live),
                  "word read refreshes both bytes of backing store");
        }
        // Le handler d'adresse traite QUATRE octets, même sur un accès mot.
        for (uint32_t off : {0x24u, 0x32u}) {
            bus.write32(base + off, 0xFF123457);
            check(bus.read8(base + off) == 0xFF && bus.read8(base + off + 3) == 0x57,
                  "long write leaves raw address bytes visible");
            check(bus.read16(base + off) == 0x12, "word read masks upper address word");
            check(bus.read8(base + off + 3) == 0x56, "word read also refreshes sibling address word");
            bus.write8(base + off + 3, 0xA7);
            bus.write16(base + off, 0x0023);
            check(bus.read32(base + off) == 0x2334A6,
                  "partial address write consumes full raw long register");
        }
        for (uint32_t off : {0x3Eu, 0x3Fu}) {
            check(!bus.busFault(base + off), "blitter tail is void without bus error");
            bus.write8(base + off, 0x12);
            check(bus.read8(base + off) == 0xFF, "void byte write ignored");
        }
        bus.write16(base + 0x3E, 0x1234);
        check(bus.read16(base + 0x3E) == 0xFFFF, "void word remains FFFF");
        bus.write32(base + 0x3C, 0x00001234);
        check(bus.read32(base + 0x3C) == 0x0000FFFF, "long access mixes control and void tail");
        bus.write8(base + 0x3A, 0xFF); bus.write8(base + 0x3B, 0xFF);
        bus.write8(base + 0x3C, 0x30); bus.write8(base + 0x3D, 0xAB);
        check(bus.read32(base + 0x3A) == 0x030F20AB,
              "byte registers remain live with hardware write masks");
    }
    {
        Bus bus;
        Blitter blit(bus);
        bus.blitter = &blit;
        bus.machine = MachineType::Ste;
        bus.write16(base + 0x20, 2); bus.write16(base + 0x22, 2);
        bus.write32(base + 0x24, 0x20000);
        for (uint32_t off : {0x28u, 0x2Au, 0x2Cu}) bus.write16(base + off, 0xFFFF);
        bus.write16(base + 0x2E, 2); bus.write16(base + 0x30, 2);
        bus.write32(base + 0x32, 0x30000);
        bus.write16(base + 0x36, 1); bus.write16(base + 0x38, 1);
        bus.write8(base + 0x3A, 2); bus.write8(base + 0x3B, 3);
        bus.write16(base + 0x3C, 0xC000); // HOG synchrone sans ordonnanceur
        check(bus.read8(base + 0x39) == 1, "byte count remains stale after completed blit");
        check(bus.read16(base + 0x38) == 0, "word count reports completed blit");
        check(bus.read8(base + 0x39) == 0, "word count read refreshes stale byte");
        bus.write8(base + 0x21, 0x57);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); blit.serialize(save);
        bus.write8(base + 0x21, 0x99);
        auto load = StateArchive::loader(state.data(), state.size()); blit.serialize(load);
        check(load.ok() && bus.read8(base + 0x21) == 0x57, "save/load preserves raw byte backing store");
        blit.reset();
        check(bus.read8(base + 0x21) == 0x57, "hardware reset preserves IO backing store");
        check(bus.read16(base + 0x20) == 0, "hardware reset clears live word register");
        check(bus.read8(base + 0x21) == 0, "word read refreshes backing after reset");
        bus.write8(base + 0x21, 0x57);
        blit.reset(true);
        check(bus.read8(base + 0x21) == 0, "cold reset clears IO backing store");
    }
    {
        Bus bus;
        YM2149 psg;
        Mfp mfp;
        Fdc fdc(bus, psg, mfp);
        bus.fdc = &fdc;
        for (MachineType profile : {MachineType::St, MachineType::MegaSt,
                                    MachineType::Ste, MachineType::MegaSte}) {
            bus.machine = profile;
            check(bus.busFault(0xFF860E) == !machineIsSte(profile), "density high-byte bus error profile");
            check(bus.busFault(0xFF860F) == (profile == MachineType::MegaSt),
                  "density low-byte Ricoh void and IMP bus error");
        }
        bus.machine = MachineType::MegaSte;
        bus.write16(0xFF860E, 0xA503);
        check(bus.read16(0xFF860E) == 0xA503, "Mega STE density word is writable");
        bus.machine = MachineType::Ste;
        bus.write16(0xFF860E, 0x1234);
        check(bus.read16(0xFF860E) == 0xFFFF, "STE density word is void");
        bus.write8(0xFF860E, 0x56); bus.write8(0xFF860F, 0x78);
        check(bus.read8(0xFF860E) == 0xFF && bus.read8(0xFF860F) == 0xFF,
              "STE density bytes are void");
        bus.machine = MachineType::MegaSte;
        check(bus.read16(0xFF860E) == 0xA503, "STE writes did not alter Mega STE density register");
    }
    std::printf("[mmio] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_MMIO_FIDELITY_TEST_MAIN
int main() { return testMmioFidelity() ? 1 : 0; }
#endif
