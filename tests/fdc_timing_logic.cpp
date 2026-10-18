// FDC : wait states asymétriques (CPU réel) et ancrage local anti-dérive.
// Hatari fdc.c DiskController*_Word / DmaModeControl / InterruptHandler_Update.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <initializer_list>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[fdc-timing] FAIL %s: got %lld, expected %lld\n",
                     label, static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct CpuRig {
    Bus bus{512 * 1024};
    Cpu68k cpu{bus};
    YM2149 psg;
    Mfp mfp;
    Fdc fdc{bus, psg, mfp};
    CpuRig(MachineType machine, bool fast) {
        bus.machine = machine; bus.cpu = &cpu; bus.fdc = &fdc;
        fdc.reset(true);
        cpu.setMegaSteSpeed(fast);
    }
    int execute(uint16_t opcode, uint16_t address, uint16_t mode) {
        // Préparation hors run : ne doit facturer aucun wait state d'observation.
        bus.write16(0xFF8606, mode);
        // Code en ROM (FAST à 16 MHz) : un préfetch RAM absorberait parfois
        // l'attente dans son alignement au prochain créneau de bus ST.
        bus.romBase = 0xFC0000; bus.rom.resize(8192);
        auto word = [&](unsigned at, uint16_t value) {
            bus.rom[at] = uint8_t(value >> 8); bus.rom[at + 1] = uint8_t(value);
        };
        word(0, 4); word(2, 0); word(4, 0xFC); word(6, 0x1000);
        word(0x1000, opcode); word(0x1002, address);
        word(0x1004, 0x4E71); word(0x1006, 0x4E71);
        cpu.reset(); cpu.setReg(0, 0);
        return cpu.run(1); // une instruction entière, même si elle dépasse 1 cycle
    }
};
struct EventRig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    EventRig(bool fast) {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched); fdc.setFastFdc(fast);
        psg.reset(); fdc.reset(true);
        fdc.write8(0xFF8606, 0); fdc.write8(0xFF8607, 0x80);
        fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, 0xD0);
    }
};
}

int testFdcTiming() {
    Checks c;
    const struct { MachineType machine; bool fast; } profiles[] = {
        {MachineType::St, false}, {MachineType::Ste, false},
        {MachineType::MegaSte, false}, {MachineType::MegaSte, true}
    };
    // Même instruction vers adresse DMA ou registre FDC : seule la cible MMIO
    // change (la RAM aurait sa propre contention à 16 MHz). Les cycles retournés
    // sont BUS ; à 16 MHz les quatre cycles CPU de WaitState valent 2.
    const struct { uint16_t opcode, address, mode; int waits; } cases[] = {
        {0x3038, 0x8604, 0x80, 1}, // MOVE.W abs.w,D0 : WD
        {0x3038, 0x8604, 0x88, 1}, // HDC
        {0x3038, 0x8604, 0x90, 0}, // SCREG : lecture sans wait
        {0x3038, 0x8604, 0x98, 0}, // SCREG prioritaire sur HDC
        {0x3038, 0x8606, 0x80, 0}, // statut DMA
        {0x31C0, 0x8604, 0x86, 1}, // MOVE.W D0,abs.w : DR
        {0x31C0, 0x8604, 0x88, 1}, // commande HDC
        {0x31C0, 0x8604, 0x90, 1}, // écriture SCREG
        {0x31C0, 0x8606, 0x80, 1}, // contrôle DMA
        {0x2038, 0x8604, 0x80, 1}, // MOVE.L : WD puis statut DMA
        {0x2038, 0x8604, 0x90, 0}, // SCREG puis statut DMA
        {0x21C0, 0x8604, 0x86, 2}, // MOVE.L : donnée puis contrôle, deux handlers
        {0x4278, 0x8604, 0x86, 2}, // CLR.W : lecture ET écriture, pas dédup instruction
        {0x4278, 0x8606, 0x80, 1}, // CLR.W contrôle : seule écriture facture
        {0x1038, 0x8609, 0x80, 0}, // MOVE.B abs.w,D0 : adresse DMA
        {0x11C0, 0x860B, 0x80, 0}, // MOVE.B D0,abs.w : adresse DMA
        {0x1038, 0x860E, 0x80, 1}, // densité : octet haut, Mega STE seulement
        {0x1038, 0x860F, 0x80, 1}, // octet bas accepté sans bus error sur Mega STE
        {0x11C0, 0x860E, 0x80, 1},
        {0x11C0, 0x860F, 0x80, 1},
        {0x3038, 0x860E, 0x80, 1}, // mot entier : une seule attente
        {0x31C0, 0x860E, 0x80, 1},
        {0x2038, 0x860C, 0x80, 1}, // long : adresse DMA puis mot densité
        {0x21C0, 0x860C, 0x80, 1},
        {0x4278, 0x860E, 0x80, 2}, // CLR.W : lecture et écriture densité
    };
    for (const auto& profile : profiles) {
        for (const auto& tc : cases) {
            // STF : $860E seul faute, $860F est void Ricoh ; STE : les deux
            // octets sont void. Le mot $860E est donc lisible dans ces profils.
            const bool byte = tc.opcode == 0x1038 || tc.opcode == 0x11C0;
            if (byte && tc.address == 0x860E && profile.machine == MachineType::St) continue;
            CpuRig r(profile.machine, profile.fast);
            const int baseline = r.execute(tc.opcode, byte ? 0x8609 : 0x8608, tc.mode);
            const int actual = r.execute(tc.opcode, tc.address, tc.mode);
            const bool density = tc.address >= 0x860C;
            const int waits = density && profile.machine != MachineType::MegaSte ? 0 : tc.waits;
            c.equal("wait asymétrique par accès matériel", actual - baseline,
                    waits * (profile.fast ? 2 : 4));
            c.equal("instruction n'a pas fauté", r.cpu.lastFault().valid, false);
        }
        for (uint16_t opcode : {0x1038, 0x11C0}) {
            for (uint16_t address : {0x8604, 0x8605, 0x8606, 0x8607}) {
                CpuRig r(profile.machine, profile.fast);
                CpuRig reference(profile.machine, profile.fast);
                const int baseline = reference.execute(opcode, 0x8600, 0x80); // MMIO sans handler
                const int actual = r.execute(opcode, address, 0x80);
                c.equal("octet fautif sans wait périphérique", actual, baseline);
                c.equal("octet FDC produit bus error", r.cpu.lastFault().valid, true);
                c.equal("adresse fautive exacte", r.cpu.lastFault().addr, 0xFF0000u | address);
            }
        }
        CpuRig r(profile.machine, profile.fast);
        const int64_t before = r.cpu.busClockNow();
        r.bus.write16(0xFF8606, 0x86); r.bus.write16(0xFF8604, 0x35);
        (void)r.bus.read16(0xFF8604); (void)r.bus.read16(0xFF8606);
        r.bus.write8(0xFF860E, 3); (void)r.bus.read16(0xFF860E);
        c.equal("observation hors run ne change pas horloge", r.cpu.busClockNow(), before);
    }
    for (bool fast : {false, true}) {
        EventRig r(fast);
        const int period = fast ? 50 : 500;
        for (int i = 0; i < 5; ++i) {
            const int64_t due = r.sched.nextDue();
            r.live = due + 37; // retard de dispatch inférieur à une période
            r.sched.runTo(r.live);
            c.equal("réarmement depuis échéance, sans dérive", r.sched.nextDue(), due + period);
        }
        // Appel direct sans firingDue : le repli doit rester l'horloge live.
        r.sched.cancel(Scheduler::FDC); r.live += 1000;
        r.fdc.onFdcEvent();
        c.equal("appel direct réarme depuis live", r.sched.nextDue(), r.live + period);
        r.fdc.reset(false);
        c.equal("reset annule l'échéance FDC", r.sched.nextDue(), -1);
    }
    std::fprintf(stderr, "[fdc-timing] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}

#ifdef NEOST_FDC_TIMING_TEST_MAIN
int main() { return testFdcTiming() ? 1 : 0; }
#endif
