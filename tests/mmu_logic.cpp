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
    // Vecteurs reset $0-$7 : toute lecture CPU sert la ROM, même quand un alias MMU
    // (banque sur-déclarée) a réécrit la copie RAM. La vidéo et le son DMA, cycles
    // RAM, voient cette copie. [D] gstmcu.v:189-195 @ 4a6db7cc (rvec → /ROM2).
    for (uint8_t conf : {uint8_t(0x04), uint8_t(0x0A)}) {   // 512K identité, puis 2M+2M sur 512K
        Bus bus(512 * 1024);
        MmuGlue glue;
        bus.glue = &glue;
        bus.machine = MachineType::St;
        bus.rom = {0x00, 0x00, 0x08, 0x00, 0x00, 0xFC, 0x00, 0x30, 0x4E, 0x71};
        bus.romBase = 0xFC0000;
        bus.seedResetVectors();
        bus.write8(0xFF8001, conf);
        uint32_t alias = 0;
        for (uint32_t a = 8; a < 0x400000 && !alias; a += 2)
            if (bus.hostRamPtr(a, 1) == &bus.ram[0]) alias = a;
        if (conf == 0x0A) check(alias != 0, "over-declared bank aliases physical $0");
        if (alias) bus.write16(alias, 0xDEAD);
        else       bus.ram[0] = 0xDE, bus.ram[1] = 0xAD;     // config identité : copie touchée en direct
        check(bus.ram[0] == 0xDE, "RAM copy of reset vectors is rewritten");
        check(bus.read8(0) == 0x00 && bus.read8(5) == 0xFC, "CPU byte read of $0-$7 serves ROM");
        check(bus.read16(0) == 0x0000 && bus.read16(4) == 0x00FC, "CPU word read of $0-$7 serves ROM");
        check(bus.read32(4) == 0x00FC0030u, "CPU long read of $4 serves ROM PC");
        check(bus.peek8(0) == 0x00, "debugger peek of $0 follows CPU decode");
        check(bus.ramCycleRead16(0) == 0xDEAD, "video/sound RAM cycle sees the RAM copy");
        check(bus.ramCycleRead8(0x400001) == 0xAD, "RAM cycle wraps on 22 bits, never ROM");
    }

    std::printf("[mmu] %d checks, %d FAIL\n", checks, failures);
    return failures;
}

#ifdef NEOST_MMU_TEST_MAIN
int main() { return testMmuMapping() ? 1 : 0; }
#endif
