// Régressions MMU : cpu/memory.c memory_map_Standard_RAM/VoidMem_* et
// stMemory.c STMemory_Reset, oracle Hatari f0736b24. Aucune ROM externe requise.
#include "core/Machine.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>

int testMmuMapping() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[mmu] FAIL: %s\n", label); }
    };

    // Le trou n'est PAS une absence de banque 1 : les deux côtés répondent.
    // Les différentes tailles exercent aussi le cache (préfixe 128K ou nul).
    struct RamCase { std::size_t kb; uint8_t normal; };
    for (MachineType profile : {MachineType::St, MachineType::MegaSt}) {
        for (RamCase ramCase : {RamCase{256, 0}, {640, 4}, {1024, 5}, {4096, 10}}) {
            Bus bus(ramCase.kb * 1024);
            MmuGlue glue;
            bus.glue = &glue;
            bus.machine = profile;
            bus.bootOverlay = false;
            std::fill(bus.ram.begin(), bus.ram.end(), 0x39);
            bus.cpuDb = 0xA65C;
            bus.write8(0xFF8001, ramCase.normal);
            check(bus.read16(0x10000) == 0x3939, "normal mapping warms identity cache");
            bus.write8(0xFF8001, 0x02); // MMU 128K + 2048K
            check(bus.read8(0xFF8001) == 0x02, "MMU config readback");
            check(bus.read8(0x3FFFF) == 0x39, "RAM immediately before hole");
            check(bus.read8(0x80000) == 0x39, "RAM immediately after hole");
            const auto before = bus.ram;
            for (uint32_t addr : {0x40000u, 0x40002u, 0x60000u, 0x7FFFCu}) {
                check(bus.read8(addr) == 0x5C && bus.read8(addr + 1) == 0x5C,
                      "void byte reads low data latch on both lanes");
                check(bus.read16(addr) == 0xA65C, "void word reads full data latch");
                check(bus.read32(addr) == 0xA65CA65Cu, "void long repeats data latch");
                check(!bus.busFaultN(addr, 4, false) && !bus.busFaultN(addr, 4, true),
                      "MMU hole is void, without bus error");
                check(bus.hostRamPtr(addr, 4) == nullptr, "void has no host RAM pointer");
                check(bus.dmaRead8(addr) == 0x5C, "DMA sees void data latch");
                bus.write8(addr, 0x11);
                bus.write16(addr, 0x2233);
                bus.write32(addr, 0x44556677);
                bus.dmaWrite8(addr, 0x88);
            }
            check(bus.read8(0x7FFFF) == 0x5C, "last byte in hole stays void");
            check(bus.ram == before, "CPU and DMA writes into hole leave RAM untouched");
            bus.write8(0xFF8001, 0x01); // 128K + 512K : aucun trou spécial
            check(bus.read16(0x40000) == 0x3939 && bus.read16(0x7FFFE) == 0x3939,
                  "same addresses map RAM with another MMU configuration");
            glue.memConfig_ = 0x02; // changement direct : reset / restauration d'état
            check(bus.read16(0x40000) == 0xA65C, "direct config change invalidates cache");
        }
    }

    // Sans changer RAM/config, ST utilise les bits de banque 1, STE les ignore.
    // Un cache validé seulement sur RAM/config masquerait ce changement de modèle.
    {
        Bus bus(1024 * 1024);
        MmuGlue glue;
        bus.glue = &glue;
        glue.memConfig_ = 4; // 512K + 128K ST, 512K + 512K STE
        std::fill(bus.ram.begin(), bus.ram.end(), 0x39);
        bus.cpuDb = 0xA65C;
        for (MachineType profile : {MachineType::Ste, MachineType::St,
                                    MachineType::MegaSte, MachineType::MegaSt}) {
            bus.machine = profile;
            check(bus.read16(0xC0000) == (machineIsSte(profile) ? 0x3939 : 0xA65C),
                  "bank 1 rule and cache follow machine profile");
        }
    }

    // Le constructeur/reconfigure ont un défaut préprogrammé assumé. Le reset
    // chaud le préserve ; le reset froid doit imposer zéro AVANT le reset CPU.
    for (MachineType profile : {MachineType::St, MachineType::MegaSt,
                                MachineType::Ste, MachineType::MegaSte}) {
        auto m = std::make_unique<Machine>(1024 * 1024, CpuCore::Moira, profile);
        m->bus.rom = {0, 1, 0, 0, 0, 0xE0, 0, 8, 0x4E, 0x71, 0x4E, 0x71};
        m->bus.romBase = 0xE00000;
        m->bus.write8(0xFF8001, 0x05);
        m->bus.ram[0x40000] = 0x39;
        check(m->bus.read8(0x40000) == 0x39, "RAM accessible before reset");
        m->reset();
        check(m->bus.read8(0xFF8001) == 0x05, "warm reset preserves MMU configuration");
        check(m->bus.ram[0x40000] == 0x39, "warm reset preserves RAM");
        m->hardReset();
        check(m->bus.read8(0xFF8001) == 0, "cold reset clears MMU configuration");
        check(m->bus.ram[0x40000] == 0, "cold reset clears physical RAM");
        m->bus.cpuDb = 0xA65C;
        check(m->bus.read16(0x40000) == 0xA65C, "cold reset replaces cached identity by void");
        check(std::equal(m->bus.rom.begin(), m->bus.rom.begin() + 8, m->bus.ram.begin()),
              "cold reset reseeds reset vectors after RAM clear");
        m->bus.write8(0xFF8001, 0x05);
        m->reset();
        check(m->bus.read8(0xFF8001) == 0x05, "warm reset still preserves reprogrammed configuration");
    }
    std::printf("[mmu] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_MMU_TEST_MAIN
int main() { return testMmuMapping() ? 1 : 0; }
#endif
