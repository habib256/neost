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
        // Netlist Atari du blitter (4082) : un registre mot ne se charge que sur une
        // écriture mot (strobe WW = UDS & LDS) et se relit VIVANT, y compris en octet.
        for (uint32_t off = 0; off < 0x3A; off += 2) {
            bus.write16(base + off, 0xA5FF);
            const uint16_t live = bus.read16(base + off);
            // Le mot écrit est bien chargé (aux bits non câblés près).
            const uint16_t mask = uint16_t((off == 0x20 || off == 0x22 || off == 0x2E || off == 0x30
                                            || off == 0x26 || off == 0x34) ? 0xFFFE
                                           : (off == 0x24 || off == 0x32) ? 0x00FF : 0xFFFF);
            check(live == (0xA5FF & mask), "word write loads the word register");
            check(bus.read8(base + off) == uint8_t(live >> 8)
                  && bus.read8(base + off + 1) == uint8_t(live),
                  "byte read returns the live word register");
            bus.write8(base + off, 0x12);
            bus.write8(base + off + 1, 0x35);
            check(bus.read16(base + off) == live, "byte writes do not modify word register");
            check(bus.read8(base + off) == uint8_t(live >> 8)
                  && bus.read8(base + off + 1) == uint8_t(live),
                  "rejected byte writes leave no trace");
        }
        // Adresses source/destination : deux mots chargés indépendamment.
        for (uint32_t off : {0x24u, 0x32u}) {
            bus.write32(base + off, 0xFF123457);
            check(bus.read32(base + off) == 0x123456, "long write masks to 24-bit even address");
            check(bus.read8(base + off) == 0x00 && bus.read8(base + off + 3) == 0x56,
                  "byte reads return live address bytes");
            bus.write8(base + off + 3, 0xA7);
            bus.write16(base + off, 0x0023);
            check(bus.read32(base + off) == 0x233456,
                  "high word write keeps live low word");
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
        check(bus.read32(base + 0x3A) == 0x030F208B,
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
        check(bus.read8(base + 0x39) == 0, "byte read sees live count after completed blit");
        check(bus.read16(base + 0x38) == 0, "word count reports completed blit");
        bus.write16(base + 0x20, 0x0058);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state); blit.serialize(save);
        bus.write16(base + 0x20, 0x0098);
        auto load = StateArchive::loader(state.data(), state.size()); blit.serialize(load);
        check(load.ok() && bus.read8(base + 0x21) == 0x58, "save/load preserves word register");
        // Netlist 4051/4082 : /RESET n'atteint que BUSY et HOG.
        blit.reset();
        check(bus.read16(base + 0x20) == 0x0058, "warm reset keeps word registers");
        check(bus.read8(base + 0x3A) == 2 && bus.read8(base + 0x3B) == 3,
              "warm reset keeps HOP and OP");
        check((bus.read8(base + 0x3C) & 0xC0) == 0, "warm reset clears BUSY and HOG");
        blit.reset(true);
        check(bus.read16(base + 0x20) == 0, "cold reset clears registers");
    }
    {
        // Pads STE (netlist du GST MCU, gstmcu.v:352-360) : la sélection n'est valide
        // qu'après une ÉCRITURE de $FF9203 ; le reset ou une LECTURE de cet octet la
        // coupe — tout se relit alors au repos.
        Bus bus;
        bus.machine = MachineType::Ste;
        bus.stePads.setJoystick(0, 0x01);              // pad A : haut
        check(bus.read16(0xFF9202) == 0xFFFF, "pads: no row selected after reset");
        bus.write8(0xFF9203, 0xFE);                    // ligne « directions » du pad A
        check(bus.read16(0xFF9202) != 0xFFFF, "pads: selected row reads the pad");
        check(bus.read16(0xFF9202) == 0xFFFF, "pads: reading $FF9203 deselects");
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
