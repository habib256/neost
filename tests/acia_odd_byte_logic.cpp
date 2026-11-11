// Octets impairs de l'ACIA ($FFFC05/$FFFC07 pour la MIDI) : sur STE le GST MCU sélectionne
// la puce sans UDS/LDS ([D] gstmcu.v:181 @ 4a6db7cc) et le 68000 recopie un octet écrit sur
// D8-D15 ([D] fx68k.sv:1783-1786 @ 0602ee46) : une écriture octet impaire atteint le registre.
// Sur ST (pas de netlist du GLUE) l'octet impair reste « void », comme Hatari.
#include "core/Bus.hpp"
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

int testAciaOddByte() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* label, unsigned got, unsigned want) {
        ++checks;
        if (got == want) return;
        ++failures;
        std::fprintf(stderr, "[acia-odd-byte] FAIL %s: %02X != %02X\n", label, got, want);
    };
    for (MachineType m : {MachineType::St, MachineType::Ste, MachineType::MegaSte}) {
        const bool ste = m == MachineType::Ste;
        Scheduler sched; Mfp mfp; MidiAcia midi(mfp); int64_t live = 0;
        midi.setScheduler(&sched); sched.setLiveClock([&] { return live; });
        Bus bus; bus.machine = m; bus.midi = &midi;
        bus.write8(0xFFFC04, 0x95);                       // /16 8N1, RIE : TDRE à 1
        eq("TDRE après CR pair", bus.read8(0xFFFC04) & 2, 2);
        bus.write8(0xFFFC05, 0x03);                       // octet impair : master reset ?
        eq(ste ? "STE : $FFFC05 = master reset (TDRE inhibé)" : "ST/MSTE : $FFFC05 void",
           bus.read8(0xFFFC04) & 2, ste ? 0 : 2);
        bus.write8(0xFFFC04, 0x95);
        bus.write16(0xFFFC04, 0x9503);                    // MOT : un seul cycle, octet pair seul
        eq("mot : l'octet impair n'est pas un second cycle", bus.read8(0xFFFC04) & 2, 2);
        eq("lecture octet impair : D0-D7 non pilotés", bus.read8(0xFFFC05), 0xFF);
        eq("largeur restaurée", bus.ioAccessWidth(), 1);
    }
    std::fprintf(stderr, "[acia-odd-byte] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_ACIA_ODD_BYTE_TEST_MAIN
int main() { return testAciaOddByte() ? 1 : 0; }
#endif
