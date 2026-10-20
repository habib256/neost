// ACSI/DMA : protocole des registres et datation de l'IRQ, sans ROM ni CPU.
// Référence : Hatari hdc.c Acsi_DmaTransfer / HDC_ACSI_InterruptHandler_Update.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void expect(const char* label, int64_t actual, int64_t expected) {
        ++count;
        if (actual == expected) return;
        ++failures;
        std::printf("  FAIL ACSI %-43s = %lld (attendu %lld)\n", label,
                    (long long)actual, (long long)expected);
    }
};
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    Rig() {
        fdc.setScheduler(&sched);
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        sched.setCallback(Scheduler::ACSI_IRQ, [this] { fdc.onAcsiIrq(); });
        sched.setLiveClock([this] { return live; });
        reset();
    }
    void reset() { fdc.reset(true); mfp.reset(); sched.reset(); live = 0; }
    void advance(int64_t cycle) { live = cycle; sched.runTo(cycle); }
    void mode(uint16_t v) {
        fdc.write8(0xFF8606, uint8_t(v >> 8)); fdc.write8(0xFF8607, uint8_t(v));
    }
    void data(uint8_t v) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, v); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    void ack() { mode(0x80); (void)fdc.read8(0xFF8605); }
    void prepare(uint8_t opcode = 8, bool write = false, bool dmaEnabled = false,
                 uint8_t dmaCount = 1, uint8_t blocks = 1) {
        mode(write ? 0x190 : 0x90); data(dmaCount);
        fdc.write8(0xFF8609, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF860D, 0);
        const uint16_t commandMode = (write ? 0x108 : 8) | (dmaEnabled ? 0 : 0x80);
        mode(commandMode); data(opcode);
        mode(commandMode | 2);
        for (uint8_t v : {uint8_t(0), uint8_t(0), uint8_t(1), blocks, uint8_t(0)}) data(v);
    }
    void start(bool write = false) { mode(write ? 0x100 : 0); }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
};
}

int testAcsiIrqTiming() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-acsi-irq-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    c.expect("répertoire temporaire exclusif", made, true);
    if (!made) return c.failures;
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    } cleanup{work};
    const auto path = work / "acsi.img";
    std::vector<uint8_t> image(2048, 0xA5);
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    c.expect("création image", fp != nullptr, true);
    if (!fp) return c.failures;
    const bool written = std::fwrite(image.data(), 1, image.size(), fp) == image.size();
    const bool closed = std::fclose(fp) == 0;
    c.expect("image complète", written && closed, true);
    if (!written || !closed) return c.failures;
    Rig r;
    c.expect("montage cible0", r.fdc.mountAcsi(path.string()), true);
    r.fdc.setFastFdc(true); // le raccourci disquette ne doit pas accélérer l'ACSI.

    // L'acquittement des octets est immédiat ; seuls les 1000 cycles post-DMA
    // sont différés. L'horloge live peut avancer pendant l'instruction CPU.
    r.advance(100); r.live = 137;
    r.prepare();
    c.expect("acquittement paquet immédiat", r.irq(), true);
    r.start();
    c.expect("données DMA immédiates", r.bus.ram[0x2000], 0xA5);
    c.expect("adresse DMA immédiate", r.address(), 0x2200);
    c.expect("IRQ abaissée pendant DMA", r.irq(), false);
    c.expect("date basée sur liveNow", r.sched.nextDue(), 1137);
    r.advance(1136); c.expect("IRQ absente à live+999", r.irq(), false);
    r.advance(1137); c.expect("IRQ présente à live+1000", r.irq(), true);
    r.advance(1138); c.expect("IRQ tenue à live+1001", r.irq(), true);
    r.ack(); r.advance(3000);
    c.expect("échéance consommée une seule fois", r.irq(), false);

    // INT_CPU_CYCLE chez Hatari : seul le Mega STE à 16 MHz divise le délai
    // dans le domaine bus. Un contrôle parasite ne change rien sur ST/STE.
    const struct { MachineType machine; uint8_t control; int delay; } speeds[] = {
        {MachineType::MegaSte, 0, 1000}, {MachineType::MegaSte, 2, 500},
        {MachineType::St, 2, 1000}, {MachineType::Ste, 2, 1000}
    };
    for (const auto& speed : speeds) {
        r.reset(); r.bus.machine = speed.machine; r.bus.megaSteCacheCtrl = speed.control;
        r.advance(100); r.live = 137; r.prepare(); r.start();
        c.expect("fréquence CPU : date bus", r.sched.nextDue(), 137 + speed.delay);
        r.advance(136 + speed.delay); c.expect("fréquence CPU : avant échéance", r.irq(), false);
        r.advance(137 + speed.delay); c.expect("fréquence CPU : à échéance", r.irq(), true);
    }
    r.bus.machine = MachineType::MegaSte;
    r.bus.fdc = &r.fdc; // accès réel au registre cache/fréquence via le Bus
    r.reset(); r.bus.megaSteCacheCtrl = 0;
    r.prepare(); r.start(); r.advance(400);
    r.bus.write8(0xFF8E21, 2);
    c.expect("8→16 MHz : ticks CPU restants conservés", r.sched.nextDue(), 700);
    r.advance(699); c.expect("8→16 MHz : avant échéance", r.irq(), false);
    r.advance(700); c.expect("8→16 MHz : à échéance", r.irq(), true);
    r.reset(); r.bus.megaSteCacheCtrl = 2;
    r.prepare(); r.start(); r.advance(200);
    r.bus.write8(0xFF8E21, 0);
    c.expect("16→8 MHz : ticks CPU restants conservés", r.sched.nextDue(), 800);
    r.advance(799); c.expect("16→8 MHz : avant échéance", r.irq(), false);
    r.advance(800); c.expect("16→8 MHz : à échéance", r.irq(), true);
    r.reset(); r.bus.megaSteCacheCtrl = 0;
    r.prepare(); r.start(); r.live = 1001; // échéance dépassée, pas encore dispatchée
    r.bus.write8(0xFF8E21, 2);
    c.expect("bascule ne repousse pas une IRQ échue", r.sched.nextDue(), 1000);
    r.advance(1001); c.expect("IRQ échue livrée après bascule", r.irq(), true);
    r.bus.machine = MachineType::Ste; r.bus.megaSteCacheCtrl = 0;

    // Écriture et lecture utilisent le même mécanisme d'IRQ.
    r.reset(); std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0x66));
    r.prepare(0x0A, true); r.start(true);
    c.expect("écriture IRQ différée", r.irq(), false);
    c.expect("écriture adresse immédiate", r.address(), 0x2200);
    r.advance(999); c.expect("écriture avant échéance", r.irq(), false);
    r.advance(1000); c.expect("écriture à échéance", r.irq(), true);

    // Une IRQ disquette déjà active n'est pas effacée par ClearHdcIRQ.
    // À échéance, FDC_SetIRQ(HDC) reprend la source, sans nouveau front GPIP.
    r.reset(); r.prepare();
    r.mfp.write8(0xFFFA09, 0x80); // autorise le latch de fronts GPIP5, canal7
    r.mode(0x80); r.data(0xD8); // Force Interrupt immédiate du WD1772
    r.mfp.write8(0xFFFA0D, 0); // acquitte le front, garde la ligne active
    r.start(); c.expect("DMA préserve IRQ FDC", r.irq(), true);
    r.advance(1000);
    c.expect("IRQ HDC ne génère pas un second front", r.mfp.read8(0xFFFA0D) & 0x80, 0);
    r.ack(); c.expect("source HDC remplace FDC comme Hatari", r.irq(), false);

    // Un paquet suivant n'annule pas le timer amont, même si sa cible est vide.
    r.reset(); r.prepare(); r.start(); r.advance(400);
    r.mode(0x88); r.data(0x20); // TEST UNIT READY cible1 absente : aucun acquittement
    c.expect("cible vide sans IRQ immédiate", r.irq(), false);
    r.advance(999); c.expect("ancienne IRQ encore différée", r.irq(), false);
    r.advance(1000); c.expect("paquet suivant conserve ancienne échéance", r.irq(), true);

    // CycInt remplace une échéance de même source : le deuxième DMA réarme.
    r.reset(); r.prepare(); r.start(); r.advance(400); r.prepare(); r.start();
    r.advance(1000); c.expect("nouveau DMA remplace ancienne échéance", r.irq(), false);
    r.advance(1399); c.expect("deuxième DMA avant échéance", r.irq(), false);
    r.advance(1400); c.expect("deuxième DMA à échéance", r.irq(), true);

    // DMA déjà activée pendant le paquet : Hatari conserve son acquittement
    // immédiat APRÈS la copie, tout en programmant aussi la fin différée.
    r.reset(); r.prepare(8, false, true);
    c.expect("paquet avec DMA active : données", r.address(), 0x2200);
    c.expect("paquet avec DMA active : acquittement", r.irq(), true);
    r.ack(); r.advance(999); c.expect("ack ne consomme pas le timer DMA", r.irq(), false);
    r.advance(1000); c.expect("DMA active : fin différée", r.irq(), true);

    // Pas de DMA effective : ni adresse avancée ni fausse fin différée.
    r.reset(); r.prepare(8, true); r.ack(); r.start(true);
    c.expect("sens DMA incorrect : adresse inchangée", r.address(), 0x2000);
    c.expect("sens DMA incorrect : aucun timer", r.sched.nextDue(), -1);
    r.advance(1000); c.expect("sens DMA incorrect : aucune IRQ", r.irq(), false);
    r.reset(); r.prepare(0); r.ack(); r.start();
    c.expect("commande sans données : aucun timer", r.sched.nextDue(), -1);

    // Compteur de secteurs DMA (même puce que la disquette) : décompté par bloc de
    // 512 o jusqu'à zéro ($8606 bit1 = 0, cf. diagnostic Field Service), aucun
    // octet acquitté à compte nul, transfert borné à count×512. Hatari l'ignore.
    auto dmaStatus = [&] { r.mode(0x80); return int(r.fdc.read8(0xFF8607)); };
    r.reset(); std::fill_n(r.bus.ram.begin() + 0x2000, 1536, uint8_t(0));
    r.prepare(8, false, false, 2, 2); r.start();
    c.expect("compte 2, 2 blocs : données", r.bus.ram[0x23FF], 0xA5);
    c.expect("compte 2, 2 blocs : adresse", r.address(), 0x2400);
    c.expect("compte 2, 2 blocs : bit1 compteur nul", dmaStatus() & 2, 0);
    r.reset(); std::fill_n(r.bus.ram.begin() + 0x2000, 1536, uint8_t(0));
    r.prepare(8, false, false, 1, 2); r.start();
    c.expect("compte 1, 2 blocs : 1er bloc", r.bus.ram[0x21FF], 0x66);  // LBA1 réécrit plus haut
    c.expect("compte 1, 2 blocs : 2e bloc non acquitté", r.bus.ram[0x2200], 0);
    c.expect("compte 1, 2 blocs : adresse", r.address(), 0x2200);
    r.reset(); std::fill_n(r.bus.ram.begin() + 0x2000, 1536, uint8_t(0));
    r.prepare(8, false, false, 0, 1); r.start();
    c.expect("compte nul : aucune donnée", r.bus.ram[0x2000], 0);
    c.expect("compte nul : adresse inchangée", r.address(), 0x2000);
    c.expect("compte nul : aucun timer", r.sched.nextDue(), -1);
    // Compte programmé ensuite (pilote tardif) : le retour en mode DMA débloque.
    r.mode(0x90); r.data(1); r.start();
    c.expect("compte tardif : données livrées", r.bus.ram[0x2000], 0x66);
    c.expect("compte tardif : bit1 compteur nul", dmaStatus() & 2, 0);

    for (bool cold : {false, true}) {
        r.reset(); r.prepare(); r.start(); r.advance(400); r.fdc.reset(cold);
        c.expect("reset annule l'échéance ACSI", r.sched.nextDue(), -1);
        r.advance(1000); c.expect("reset empêche l'IRQ tardive", r.irq(), false);
    }

    // Le Scheduler sérialise la date et l'activité ; le FDC garde la source
    // d'IRQ, le MFP son niveau. Reprise dans une autre carte à configuration égale.
    r.reset(); r.prepare(); r.start(); r.advance(400);
    std::vector<uint8_t> state;
    auto save = StateArchive::saver(state);
    r.sched.serialize(save); r.mfp.serialize(save); r.fdc.serialize(save);
    Rig restored;
    c.expect("montage cible pour reprise", restored.fdc.mountAcsi(path.string()), true);
    auto load = StateArchive::loader(state.data(), state.size());
    restored.sched.serialize(load); restored.mfp.serialize(load); restored.fdc.serialize(load);
    restored.live = 400;
    c.expect("archive de reprise valide", load.ok(), true);
    c.expect("reprise conserve date absolue", restored.sched.nextDue(), 1000);
    c.expect("reprise avant IRQ", restored.irq(), false);
    restored.advance(999); c.expect("reprise à échéance-1", restored.irq(), false);
    restored.advance(1000); c.expect("reprise à échéance", restored.irq(), true);
    restored.ack(); restored.advance(2000);
    c.expect("reprise : événement unique", restored.irq(), false);
    std::printf("[selftest-acsi] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}

#ifdef NEOST_ACSI_TEST_MAIN
int main() { return testAcsiIrqTiming() ? 1 : 0; }
#endif
