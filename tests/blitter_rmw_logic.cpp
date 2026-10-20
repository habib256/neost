// Hatari blitter.c1668–1695 protège cpu_bus_rmw ; Moira execTasEa fait
// readOp, SYNC(2), writeOp sous verrou explicite ; préfetch ensuite déverrouillé.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Blitter.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
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
    int videoEvents = 0;
    int64_t readCycle = -1;
    int64_t writeCycle = -1;
    int64_t dueCycle = -1;
    int64_t serviceCycle = -1;
    bool writeBeforeService = false;

    explicit Rig(bool memory, bool overlapResume = false, bool hog = false) {
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
        scheduler.setCallback(Scheduler::RENDER, [this] { ++videoEvents; });

        // Vecteurs et code en ROM : TAS (A0) ou TAS D0, suivis de NOP.
        bus.romBase = 0xFC0000;
        bus.rom.resize(8192);
        romWord(0, 4);
        romWord(2, 0);
        romWord(4, 0xFC);
        romWord(6, 0x1000);
        romWord(0x1000, memory ? 0x4AD0 : 0x4AC0);
        romWord(0x1002, overlapResume ? 0x6002 : 0x4E71); // BRA.s +2
        romWord(0x1004, 0x4E71);
        romWord(0x1006, 0x4E71);
        romWord(0x1008, 0x4E71);
        cpu.reset();
        cpu.setReg(8, 0xFFFA01);
        cpu.setReg(0, 1);
        cpu.setReg(1, 3);
        cpu.setReg(2, 99);
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
            scheduler.schedule(Scheduler::RENDER, dueCycle);
        });
        blitter.write8(0xFF8A3C, hog ? 0xC0 : 0x80);
        if (!memory) {
            dueCycle = cpu.busClockNow() + 1;
            scheduler.schedule(Scheduler::BLITTER, dueCycle);
            scheduler.schedule(Scheduler::RENDER, dueCycle);
        }
    }

    void serialize(StateArchive& archive) {
        cpu.serialize(archive);
        bus.serialize(archive);
        mfp.serialize(archive);
        blitter.serialize(archive);
        scheduler.serialize(archive);
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

// Vrai CPU à 16 MHz, code ROM et données RAM : coûts exprimés en cycles bus
// 8 MHz. Hatari m68000.c wait_cpu_cycle_*_megaste_16 aligne AVANT les cycles.
struct MegaSteRig {
    Bus bus;
    Cpu68k cpu{bus};
    Scheduler scheduler;
    Blitter blitter{bus};
    explicit MegaSteRig(uint16_t opcode = 0x3010, bool cache = false,
                        bool sixteen = true, uint8_t arm = 0) {
        bus.cpu = &cpu; bus.blitter = &blitter; bus.machine = MachineType::MegaSte;
        cpu.setScheduler(&scheduler);
        scheduler.setLiveClock([this] { return cpu.busClockNow(); });
        blitter.setScheduler(&scheduler);
        scheduler.setCallback(Scheduler::BLITTER, [this] { blitter.onSlice(); });
        bus.romBase = 0xFC0000; bus.rom.resize(8192);
        auto word = [this](unsigned offset, uint16_t v) {
            bus.rom[offset] = uint8_t(v >> 8); bus.rom[offset + 1] = uint8_t(v);
        };
        word(0, 4); word(2, 0); word(4, 0xFC); word(6, 0x1000);
        word(0x1000, opcode);
        word(0x1002, arm ? arm : 0x4E71); word(0x1004, arm ? 0x8A3C : 0x4E71);
        for (unsigned a = 0x1006; a < 0x1020; a += 2) word(a, 0x4E71);
        cpu.reset(); cpu.setReg(8, 0x1000);
        bus.write16(0x1000, 0x1234); bus.write16(0x1002, 0x5678);
        bus.write8(0xFF8E21, sixteen ? (cache ? 3 : 2) : 0);
        scheduler.runTo(cpu.busClockNow());
    }
    int64_t step() {
        const int64_t before = cpu.busClockNow();
        cpu.run(1);
        scheduler.runTo(cpu.busClockNow());
        return cpu.busClockNow() - before;
    }
    void copyToOperand(bool start = true) {
        bus.write16(0x2000, 0xABCD);
        blitter.write16(0xFF8A20, 2); blitter.write16(0xFF8A22, 2);
        blitter.write32(0xFF8A24, 0x2000);
        for (uint32_t a : {0xFF8A28u, 0xFF8A2Au, 0xFF8A2Cu}) blitter.write16(a, 0xFFFF);
        blitter.write16(0xFF8A2E, 2); blitter.write16(0xFF8A30, 2);
        blitter.write32(0xFF8A32, 0x1000);
        blitter.write16(0xFF8A36, 1); blitter.write16(0xFF8A38, 1);
        blitter.write16(0xFF8A3A, 0x0203);
        if (start) {
            blitter.write8(0xFF8A3C, 0xC0);
            scheduler.schedule(Scheduler::BLITTER, cpu.busClockNow() + 1);
        }
    }
};

}

int testBlitterRmw() {
    Checks check;
    {
        MegaSteRig rig(0x3038, false, false);
        rig.bus.machine = MachineType::St;
        auto word = [&](unsigned off, uint16_t value) {
            rig.bus.rom[off] = uint8_t(value >> 8);
            rig.bus.rom[off + 1] = uint8_t(value);
        };
        word(0x1002, 0x8A3C); // sondage blitter absent, comme EmuTOS
        word(0x1040, 0x2E7C); word(0x1042, 0); word(0x1044, 0x1001);
        word(0x1046, 0x3010); // MOVE.W (A0),D0 avec pile et adresse impaires
        rig.bus.write32(8, 0xFC1040);
        rig.cpu.reset(); rig.cpu.setReg(8, 0x1001);
        rig.step(); rig.step(); rig.step();
        check.equal("double faute d'adresse observée", rig.cpu.halted(), true);
        check.equal("faute courante remplace sondage blitter", rig.cpu.lastFault().addr, 0x1001);
        check.equal("vecteur courant est address error", rig.cpu.lastFault().vector, 3);
        rig.cpu.reset();
        check.equal("reset efface l'ancien diagnostic", rig.cpu.lastFault().valid, false);
    }
    // MOVE D0,SR doit relire le prochain opcode, déjà en IRC au reset.
    // Modifier la ROM après le reset distingue un vrai rechargement de la
    // file d'une simple lecture fictive, même sans cache ni wait-state.
    for (bool sixteen : {false, true}) {
        MegaSteRig rig(0x46C0, false, sixteen);
        rig.cpu.setReg(0, 0x2700);
        rig.cpu.setReg(1, 0);
        rig.bus.rom[0x1002] = 0x72; // MOVEQ #7,D1, remplace le NOP préfetché
        rig.bus.rom[0x1003] = 0x07;
        check.equal("MOVE SR durée ROM", rig.step(), sixteen ? 6 : 12);
        rig.step();
        check.equal("MOVE SR recharge l'opcode suivant", rig.cpu.reg(1), 7);
    }
    // Même queue microcode pour MOVE Dn,CCR (44c0 : 301 rstw1 → 1e1 malw3 → 363 b).
    for (bool sixteen : {false, true}) {
        MegaSteRig rig(0x44C0, false, sixteen);
        rig.cpu.setReg(0, 0x1F);
        rig.cpu.setReg(1, 0);
        rig.bus.rom[0x1002] = 0x72; // MOVEQ #7,D1, remplace le NOP préfetché
        rig.bus.rom[0x1003] = 0x07;
        check.equal("MOVE CCR durée ROM", rig.step(), sixteen ? 6 : 12);
        check.equal("MOVE CCR écrit CCR", rig.cpu.sr() & 0x1F, 0x1F);
        rig.step();
        check.equal("MOVE CCR recharge l'opcode suivant", rig.cpu.reg(1), 7);
    }
    // ANDI/ORI/EORI #,CCR (003c/023c/0a3c) : queue identique, durée inchangée.
    for (bool sixteen : {false, true}) {
        MegaSteRig rig(0x003C, false, sixteen);
        rig.bus.rom[0x1002] = 0x00; rig.bus.rom[0x1003] = 0x15;   // ORI #$15,CCR
        rig.bus.rom[0x1004] = 0x72; rig.bus.rom[0x1005] = 0x07;   // MOVEQ #7,D1
        rig.cpu.reset();                                          // re-préfetch de l'immédiat
        rig.scheduler.runTo(rig.cpu.busClockNow());
        rig.cpu.setReg(1, 0);
        check.equal("ORI CCR durée ROM", rig.step(), sixteen ? 10 : 20);
        check.equal("ORI CCR écrit CCR", rig.cpu.sr() & 0x1F, 0x15);
        rig.step();
        check.equal("ORI CCR enchaîne l'opcode suivant", rig.cpu.reg(1), 7);
    }
    // ANDI/EORI #,SR qui repassent en UTILISATEUR (027c/0a7c) : le microcode relit
    // pc0+4 APRÈS l'écriture de SR, donc avec le FC utilisateur. Placé en $7FE, ce
    // mot est protégé par le GLUE en mode utilisateur → bus error sur $7FE. L'ancien
    // code exécutait le mot préfetché en superviseur (MOVEQ → D1 = 7).
    for (bool sixteen : {false, true}) {
        for (uint16_t op : {uint16_t(0x027C), uint16_t(0x0A7C)}) {
            MegaSteRig rig(0x4E71, false, sixteen);
            auto word = [&](unsigned off, uint16_t value) {
                rig.bus.rom[off] = uint8_t(value >> 8);
                rig.bus.rom[off + 1] = uint8_t(value);
            };
            word(4, 0); word(6, 0x07FA);                       // PC de reset = $7FA
            word(0x1040, 0x4E71);
            rig.bus.write32(8, 0xFC1040);                      // vecteur bus error
            rig.bus.write16(0x7FA, op);
            rig.bus.write16(0x7FC, op == 0x027C ? 0xDFFF : 0x2000);
            rig.bus.write16(0x7FE, 0x7207);                    // MOVEQ #7,D1
            rig.bus.write16(0x800, 0x4E71);
            rig.cpu.reset();
            rig.scheduler.runTo(rig.cpu.busClockNow());
            rig.cpu.setReg(1, 0);
            rig.step();
            const Cpu68k::Fault f = rig.cpu.lastFault();
            check.equal("#,SR → user : bus error prise", f.valid, true);
            check.equal("#,SR → user : handler superviseur", (rig.cpu.sr() & 0x2000) != 0, true);
            check.equal("#,SR → user : relecture pc0+4 en FC user", f.addr, 0x7FE);
            rig.step();
            check.equal("#,SR → user : ancien opcode non exécuté", rig.cpu.reg(1), 0);
        }
    }
    // Lecture HÔTE d'un registre qui faute en accès octet ($FF9200 Mega STE) : hors
    // exécution d'instruction, aucune exception ne doit traverser l'hôte ni polluer
    // le diagnostic de faute du CPU émulé.
    {
        MegaSteRig rig(0x4E71, false, false);
        bool threw = false;
        uint8_t v = 0;
        try { v = rig.bus.read8(0xFF9200); } catch (...) { threw = true; }
        check.equal("lecture hôte $FF9200 sans exception", threw, false);
        check.equal("lecture hôte $FF9200 valeur neutre", v, 0xFF);
        check.equal("lecture hôte sans faute CPU", rig.cpu.lastFault().valid, false);
        rig.step();
        check.equal("CPU intact après lecture hôte", rig.cpu.halted(), false);
        check.equal("pas de faute après lecture hôte", rig.cpu.lastFault().valid, false);
    }
    {
        Scheduler scheduler;
        int blits = 0, renders = 0;
        int64_t firing = -1;
        scheduler.setCallback(Scheduler::BLITTER, [&] { ++blits; firing = scheduler.firingDue(); });
        scheduler.setCallback(Scheduler::RENDER, [&] { ++renders; });
        scheduler.schedule(Scheduler::RENDER, 2);
        scheduler.schedule(Scheduler::BLITTER, 4);
        check.equal("PRE_START encore futur", scheduler.runBlitterTo(3), false);
        check.equal("PRE_START échu", scheduler.runBlitterTo(4), true);
        check.equal("une seule reprise", blits, 1);
        check.equal("date de reprise conservée", firing, 4);
        check.equal("helper seul ne sert pas la vidéo", renders, 0);
        check.equal("helper ne crédite pas le quantum", scheduler.now(), 0);
        check.equal("cache conserve l'autre échéance", scheduler.nextDue(), 2);
        scheduler.runTo(4);
        check.equal("autre échéance servie ensuite", renders, 1);
    }
    for (bool hog : {false, true}) {
        for (bool memory : {true, false}) {
            Rig rig(memory, false, hog);
            check.equal("PRE_START ne copie pas au write", rig.bus.read16(0x2000), 0);
            check.equal("BUSY avant PRE_START", rig.blitter.busy(), true);
            rig.cpu.run(1); // Moira termine exactement la première instruction.
            check.equal("reprise après RMW au préfetch", rig.events, 1);
            check.equal("copie achevée avant fin instruction", rig.bus.read16(0x2000), 0x1234);
            check.equal("vidéo servie pendant la DMA", rig.videoEvents, 1);
            if (memory) {
                check.equal("échéance après lecture", rig.dueCycle > rig.readCycle, true);
                check.equal("échéance avant écriture", rig.dueCycle < rig.writeCycle, true);
            } else {
                check.equal("TAS D0 exécuté", rig.cpu.reg(0), 0x81);
            }
            rig.scheduler.runTo(rig.cpu.busClockNow());
            check.equal("vidéo servie ensuite", rig.videoEvents, 1);
            check.equal("événement ultérieur servi", rig.events, 1);
            check.equal("copie après TAS", rig.bus.read16(0x2000), 0x1234);
            check.equal("blit terminé", rig.blitter.busy(), false);
            if (memory) {
                check.equal("écriture TAS précède prise bus", rig.writeBeforeService, true);
                check.equal("service après écriture", rig.serviceCycle >= rig.writeCycle, true);
            }
        }
    }
    // TAS D0 préfetche pendant la reprise : le crédit CPU/blitter restant
    // traverse la frontière d'instruction. BRA consomme deux cycles internes
    // avant son prochain accès bus : oublier ce crédit au load change son coût.
    for (bool hog : {false, true}) {
        Rig original(false, true, hog);
        original.cpu.run(1);
        std::vector<uint8_t> state;
        auto save = StateArchive::saver(state);
        original.serialize(save);
        check.equal("snapshot CPU/blitter écrit", save.ok(), true);
        Rig restored(false, true, hog);
        auto load = StateArchive::loader(state.data(), state.size());
        restored.serialize(load);
        check.equal("snapshot CPU/blitter relu", load.ok(), true);
        const int64_t beforeBranch = original.cpu.busClockNow();
        original.cpu.run(1);
        restored.cpu.run(1);
        check.equal("BRA recouvre deux cycles internes", original.cpu.busClockNow() - beforeBranch, 8);
        check.equal("BRA reprend avec même coût", restored.cpu.busClockNow(), original.cpu.busClockNow());
        check.equal("BRA reprend avec même PC", restored.cpu.pc(), original.cpu.pc());
        check.equal("cible BRA réelle", original.cpu.pc(), 0xFC1006);
    }
    // Miss RAM : huit cycles CPU par mot, hit : quatre. Vérifier les quatre
    // phases bus, les voies octet et les deux mots d'un accès long réel.
    for (int phase = 0; phase < 4; ++phase) {
        MegaSteRig rig;
        rig.cpu.addBusWaitCycles(phase);
        check.equal("16 MHz miss : alignement début", rig.step(), 6 + ((4 - phase) & 3));
        check.equal("16 MHz miss : donnée", rig.cpu.reg(0), 0x1234);
    }
    for (uint16_t op : {uint16_t(0x1010), uint16_t(0x3010), uint16_t(0x2010)}) {
        const bool isLong = op == 0x2010;
        const uint32_t expected = isLong ? 0x12345678u : op == 0x1010 ? 0x12u : 0x1234u;
        for (bool hit : {false, true}) {
            MegaSteRig rig(op, true);
            if (hit) {
                rig.bus.megaSteCacheUpdate(0x1000, 2, 0x1234, false, true);
                rig.bus.megaSteCacheUpdate(0x1002, 2, 0x5678, false, true);
            }
            check.equal("16 MHz coût byte/word/long", rig.step(), hit ? (isLong ? 6 : 4) : (isLong ? 10 : 6));
            check.equal("16 MHz donnée byte/word/long", rig.cpu.reg(0), expected);
        }
    }
    {
        MegaSteRig rig(0x3080, true); // MOVE.W D0,(A0), write-through
        rig.cpu.setReg(0, 0xBEEF);
        rig.bus.megaSteCacheUpdate(0x1000, 2, 0x1234, false, true);
        check.equal("16 MHz écriture RAM", rig.step(), 6);
        check.equal("write-through mémoire", rig.bus.read16(0x1000), 0xBEEF);
        uint16_t cached = 0;
        check.equal("write-through hit", rig.bus.megaSteCacheRead(0x1000, 2, cached, true), true);
        check.equal("write-through valeur", cached, 0xBEEF);
    }
    {
        MegaSteRig rig(0x3010, true);
        rig.bus.megaSteCacheUpdate(0x1000, 2, 0x1234, false, true);
        rig.copyToOperand();
        int observations = 0;
        rig.scheduler.setCallback(Scheduler::RENDER, [&] {
            ++observations;
            uint16_t cached = 0;
            check.equal("cache invalidé pendant arbitration", rig.bus.megaSteCacheRead(0x1000, 2, cached, true), false);
        });
        rig.scheduler.schedule(Scheduler::RENDER, rig.cpu.busClockNow() + 2);
        rig.step();
        check.equal("observation pendant arbitration exécutée", observations, 1);
        check.equal("hit choisi avant DMA conserve sa donnée", rig.cpu.reg(0), 0x1234);
        check.equal("DMA a changé la RAM", rig.bus.read16(0x1000), 0xABCD);
        check.equal("transfert HOG terminé", rig.blitter.busy(), false);
    }
    // Le contrôleur blitter reste cadencé à 8 MHz quand le CPU passe à 16.
    // Après la FIN de l'écriture, PRE_START dure quatre cycles bus dans les
    // deux cas : depuis son callback médian, 2+4 à 8 MHz, 1+4 à 16 MHz.
    for (bool sixteen : {false, true}) {
        for (bool cache : {false, true}) {
            for (uint8_t ctrl : {uint8_t(0x80), uint8_t(0xC0)}) {
                MegaSteRig rig(0x11FC, cache, sixteen, ctrl);
                rig.copyToOperand(false);
                int starts = 0;
                int64_t write = -1, grant = -1;
                rig.bus.watchBase_ = 0xFF8A00;
                rig.bus.watchHook = [&](uint32_t a, uint8_t) {
                    if (a == 0xFF8A3C) write = rig.cpu.busClockNow();
                };
                rig.scheduler.setCallback(Scheduler::BLITTER, [&] {
                    ++starts; grant = rig.cpu.busClockNow(); rig.blitter.onSlice();
                });
                for (int i = 0; i < 8 && !starts; ++i) rig.cpu.run(1);
                check.equal("écriture contrôle CPU exécutée", write >= 0, true);
                check.equal("une seule prise bus", starts, 1);
                check.equal("PRE_START indépendant fréquence CPU", grant - write, sixteen ? 5 : 6);
                check.equal("copie après prise bus datée", rig.bus.read16(0x1000), 0xABCD);
            }
        }
    }
    // Le manuel Atari décrit la ligne d'interruption comme une copie de BUSY.
    // Un autre événement peut observer la restitution du bus, après le dernier
    // mot mais avant GPU_DONE : le statut doit encore indiquer un blit actif.
    for (bool sixteen : {false, true}) {
        for (uint8_t ctrl : {uint8_t(0x80), uint8_t(0xC0)}) {
            MegaSteRig rig(0x4E71, false, sixteen);
            Mfp mfp;
            mfp.setHasBlitter(true);       // Mega STE : GPIP3 pilotée par le blitter (sinon pull-up R4)
            rig.bus.mfp = &mfp;
            rig.copyToOperand(false);
            rig.blitter.write8(0xFF8A3C, ctrl);
            const int64_t grant = rig.cpu.busClockNow();
            int observations = 0;
            rig.scheduler.setCallback(Scheduler::RENDER, [&] {
                ++observations;
                check.equal("dernier mot écrit avant restitution", rig.bus.read16(0x1000), 0xABCD);
                check.equal("BUSY maintenu jusqu'à GPU_DONE", rig.blitter.busy(), true);
                check.equal("GPU_DONE pas encore actif", mfp.read8(0xFFFA01) & 8, 8);
            });
            rig.scheduler.schedule(Scheduler::RENDER, grant + 8 + 8 + 2);
            rig.scheduler.schedule(Scheduler::BLITTER, grant);
            rig.scheduler.runTo(grant);
            check.equal("observation pendant restitution", observations, 1);
            check.equal("BUSY effacé avec GPU_DONE", rig.blitter.busy(), false);
            check.equal("GPU_DONE actif à la fin", mfp.read8(0xFFFA01) & 8, 0);
            check.equal("DMA et arbitration restent à 8 MHz", rig.cpu.busClockNow() - grant, 20);
        }
    }
    // Isoler l'IACK vidéo du créneau RAM : placer la pile dans le miroir void
    // au-delà des ACIA/RTC ($FFFC70-7F). Les écritures sont ignorées, sans
    // bus error ni attente périphérique. Aucun RTE n'est exécuté.
    // Balayer les dix phases E-clock pour mesurer aussi le cas sans attente.
    for (bool sixteen : {false, true}) {
        for (int level : {2, 4}) {
            int64_t shortest = 1000;
            for (int phase = 0; phase < 10; ++phase) {
                MegaSteRig rig(0x4E71, false, sixteen);
                rig.bus.scu.write8(0xFF8E01, 0x14);
                rig.bus.write32(uint32_t((24 + level) * 4), 0xFC1000);
                rig.cpu.setReg(15, 0xFFFC80);
                rig.cpu.setSr(0x2000);
                rig.cpu.addBusWaitCycles(phase);
                rig.scheduler.runTo(rig.cpu.busClockNow());
                int64_t pclo = -1, sr = -1;
                rig.bus.watchBase_ = 0xFFFC00;
                rig.bus.watchHook = [&](uint32_t a, uint8_t) {
                    if (a == 0xFFFC7E) pclo = rig.cpu.busClockNow();
                    if (a == 0xFFFC7A) sr = rig.cpu.busClockNow();
                };
                if (level == 2) rig.cpu.raiseHbl(); else rig.cpu.raiseVbl();
                rig.cpu.updateIplNow();
                for (int instruction = 0; instruction < 8 && sr < 0; ++instruction)
                    rig.cpu.run(1);
                check.equal("IACK vidéo observé entre les écritures de pile", pclo >= 0 && sr > pclo, true);
                if (pclo >= 0 && sr > pclo) shortest = std::min(shortest, sr - pclo);
            }
            // Deux demi-accès (4 CPU), IACK vidéo (10 bus), idle (4 CPU).
            check.equal("IACK vidéo distingue horloges bus et CPU", shortest, sixteen ? 14 : 18);
        }
    }
    std::printf("[blitter-rmw] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_BLITTER_RMW_TEST_MAIN
int main() { return testBlitterRmw() ? 1 : 0; }
#endif
