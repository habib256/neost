// Hatari psg.c PSG_ff8801/03_WriteByte : ombres A0 seulement en BYTE/MOVEP.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/YM2149.hpp"
#include <cstdio>
#include <vector>

namespace {
struct PsgBusRig {
    Bus bus;
    YM2149 psg;
    std::vector<uint8_t> portA, portB, printed;

    PsgBusRig() {
        bus.psg = &psg;
        psg.reset();
        psg.setPortASink([&](uint8_t value) { portA.push_back(value); });
        psg.setPortBSink([&](uint8_t value) { portB.push_back(value); });
        psg.setPrinterSink([&](uint8_t value) { printed.push_back(value); });
    }
};
}

bool testPsgOddWrite() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) {
            ++failures;
            std::fprintf(stderr, "PSG ODD WRITE FAIL: %s\n", name);
        }
    };

    for (uint32_t offset : {0u, 4u, 0x80u, 0xfcu}) {
        PsgBusRig rig;
        const uint32_t address = 0xff8800 + offset;

        // L'ombre octet utilise le même masque et le même latch que l'adresse paire.
        rig.bus.write8(address + 1, 8);
        rig.bus.write8(address + 3, 0xfe);
        check(rig.bus.read8(address) == 0xfe && rig.bus.read8(address + 2) == 0x1e,
              "odd byte aliases preserve raw latch and masked diagnostic data read");
        rig.bus.write8(address + 1, 8);
        check(rig.bus.read8(address) == 0x1e, "odd selection reloads masked register value");
        check(rig.bus.read8(address + 1) == 0xff && rig.bus.read8(address + 3) == 0xff,
              "odd reads remain void");

        // Les octets bas d'accès MOT/LONG ne sont pas des écritures indépendantes.
        rig.bus.write16(address, 0x0e0f);
        rig.bus.write16(address + 2, 0xabcd);
        check(rig.portA.size() == 1 && rig.portA[0] == 0xab && rig.portB.empty(),
              "word low bytes do not select or write a second register");
        rig.bus.write32(address, 0x0e550f66);
        check(rig.portA.size() == 2 && rig.portA.back() == 0x0f && rig.portB.empty(),
              "long write handles high bytes once and ignores odd shadows");

        // Une écriture mot ne doit produire qu'un seul callback et un seul strobe.
        rig.bus.write8(address + 1, 15);
        rig.bus.write8(address + 3, 0x66);
        rig.bus.write8(address + 1, 14);
        rig.bus.write8(address + 3, 0xff);
        const auto portWritesBefore = rig.portA.size();
        const auto strobesBefore = rig.printed.size();
        rig.bus.write16(address + 2, 0xdf20);
        check(rig.portA.size() == portWritesBefore + 1 && rig.portA.back() == 0xdf
                  && rig.printed.size() == strobesBefore + 1 && rig.printed.back() == 0x66,
              "word shadow does not duplicate port callback or Centronics strobe");

        rig.bus.write8(address + 1, 0x80);
        rig.bus.write8(address + 3, 0x77);
        check(rig.bus.read8(address) == 0xff && rig.bus.read8(address + 2) == 0xff
                  && rig.portA.size() == portWritesBefore + 1,
              "odd selector keeps eight bits and invalid selection ignores data");
    }

    // Vraies instructions Moira, pas une imitation par deux appels write8.
    for (bool isLong : {false, true}) {
        PsgBusRig rig;
        Cpu68k cpu(rig.bus);
        rig.bus.cpu = &cpu;
        rig.bus.romBase = 0xfc0000;
        rig.bus.rom.resize(8192);
        auto romWord = [&](unsigned offset, uint16_t value) {
            rig.bus.rom[offset] = uint8_t(value >> 8);
            rig.bus.rom[offset + 1] = uint8_t(value);
        };
        romWord(0, 4);                 // SSP = $00040000
        romWord(2, 0);
        romWord(4, 0xfc);              // PC = $00FC1000
        romWord(6, 0x1000);
        romWord(0x1000, isLong ? 0x01c8 : 0x0188); // MOVEP.L/W D0,1(A0)
        romWord(0x1002, 1);
        romWord(0x1004, 0x4e71);
        romWord(0x1006, 0x4e71);

        cpu.reset();
        cpu.setReg(8, 0xff8800);       // A0 : base PSG ; déplacement impair = ombre
        cpu.setReg(0, isLong ? 0x0e550f66 : 0x0e55);
        cpu.run(1);                    // une instruction entière
        check(rig.portA.size() == 1 && rig.portA[0] == 0x55,
              "MOVEP odd lane selects then writes port A");
        check(isLong ? (rig.portB.size() == 1 && rig.portB[0] == 0x66) : rig.portB.empty(),
              "MOVEP long continues through PSG mirrors with byte access width");
    }
    return failures == 0;
}

#ifdef PSG_ODD_WRITE_STANDALONE
int main() { return testPsgOddWrite() ? 0 : 1; }
#endif
