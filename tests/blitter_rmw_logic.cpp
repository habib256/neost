// Hatari blitter.c1668–1695 protège cpu_bus_rmw ; Moira execTasEa fait
// readOp, SYNC(2), writeOp. Le modèle BLOC NeoST ne dispatche pas entre eux.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Blitter.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

namespace {
struct Checks {
    int count = 0;
    int failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::printf("  FAIL Blitter RMW %s: %lld attendu %lld\n", label,
                    static_cast<long long>(got), static_cast<long long>(expected));
    }
};

struct Rig {
    Bus bus;
    Cpu68k cpu{bus};
    Scheduler scheduler;
    Mfp mfp;
    Blitter blitter{bus};
    int events = 0;
    int64_t readCycle = -1;
    int64_t writeCycle = -1;
    int64_t dueCycle = -1;
    int64_t serviceCycle = -1;
    bool writeBeforeService = false;

    explicit Rig(bool memory) {
        bus.cpu = &cpu;
        bus.mfp = &mfp;
        bus.blitter = &blitter;
        bus.machine = MachineType::Ste;
        cpu.setScheduler(&scheduler);
        scheduler.setLiveClock([this] { return cpu.busClockNow(); });
        blitter.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::BLITTER, [this] {
            ++events;
            serviceCycle = cpu.busClockNow();
            writeBeforeService = writeCycle >= 0;
            blitter.onSlice();
        });

        // Vecteurs et code en ROM : TAS (A0) ou TAS D0, suivis de NOP.
        bus.romBase = 0xFC0000;
        bus.rom.resize(8192);
        romWord(0, 4);
        romWord(2, 0);
        romWord(4, 0xFC);
        romWord(6, 0x1000);
        romWord(0x1000, memory ? 0x4AD0 : 0x4AC0);
        romWord(0x1002, 0x4E71);
        romWord(0x1004, 0x4E71);
        cpu.reset();
        cpu.setReg(8, 0xFFFA01);
        cpu.setReg(0, 1);
        scheduler.runTo(cpu.busClockNow());
        configureCopy();

        bus.watchBase_ = 0xFFFA00;
        bus.watchHook = [this](uint32_t address, uint8_t) {
            if (address == 0xFFFA01) writeCycle = cpu.busClockNow();
        };
        mfp.setGpipReadHook([this](uint8_t&) {
            // GPIP rend observable le vrai callback lecture de Moira. On place
            // l'échéance entre cette lecture et l'écriture, sans dispatcher ici.
            readCycle = cpu.busClockNow();
            dueCycle = readCycle + 1;
            scheduler.schedule(Scheduler::BLITTER, dueCycle);
        });
        blitter.write8(0xFF8A3C, 0x80);
        if (!memory) {
            dueCycle = cpu.busClockNow() + 1;
            scheduler.schedule(Scheduler::BLITTER, dueCycle);
        }
    }

    void romWord(unsigned offset, uint16_t value) {
        bus.rom[offset] = uint8_t(value >> 8);
        bus.rom[offset + 1] = uint8_t(value);
    }
    void configureCopy() {
        blitter.write16(0xFF8A20, 2);
        blitter.write16(0xFF8A22, 2);
        blitter.write32(0xFF8A24, 0x1000);
        for (uint32_t address : {0xFF8A28, 0xFF8A2A, 0xFF8A2C})
            blitter.write16(address, 0xFFFF);
        blitter.write16(0xFF8A2E, 2);
        blitter.write16(0xFF8A30, 2);
        blitter.write32(0xFF8A32, 0x2000);
        blitter.write16(0xFF8A36, 1);
        blitter.write16(0xFF8A38, 1);
        blitter.write8(0xFF8A3A, 2);
        blitter.write8(0xFF8A3B, 3);
        bus.write16(0x1000, 0x1234);
        bus.write16(0x2000, 0);
    }
};
}

int testBlitterRmw() {
    Checks check;
    for (bool memory : {true, false}) {
        Rig rig(memory);
        rig.cpu.run(1); // Moira termine exactement la première instruction.
        check.equal("aucun service pendant TAS", rig.events, 0);
        check.equal("destination intacte pendant TAS", rig.bus.read16(0x2000), 0);
        if (memory) {
            check.equal("échéance après lecture", rig.dueCycle > rig.readCycle, true);
            check.equal("échéance avant écriture", rig.dueCycle < rig.writeCycle, true);
        } else {
            check.equal("TAS D0 exécuté", rig.cpu.reg(0), 0x81);
        }
        rig.scheduler.runTo(rig.cpu.busClockNow());
        check.equal("événement ultérieur servi", rig.events, 1);
        check.equal("copie après TAS", rig.bus.read16(0x2000), 0x1234);
        check.equal("blit terminé", rig.blitter.busy(), false);
        if (memory) {
            check.equal("écriture TAS précède prise bus", rig.writeBeforeService, true);
            check.equal("service après écriture", rig.serviceCycle >= rig.writeCycle, true);
        }
    }
    std::printf("[blitter-rmw] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_BLITTER_RMW_TEST_MAIN
int main() { return testBlitterRmw() ? 1 : 0; }
#endif
