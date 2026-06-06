// =============================================================================
//  Cpu68k.cpp — Liaison Musashi <-> Bus NeoST.
//
//  Musashi communique avec le monde extérieur via des fonctions C GLOBALES à
//  nom imposé (m68k_read_memory_8, ...). On les redirige ici vers un Bus unique
//  pointé par g_bus. C'est le seul couplage "global" du projet, inhérent à l'API
//  C de Musashi.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Cpu68k.hpp"
#include "core/Bus.hpp"
#include "core/Tracer.hpp"
#include "io/Mfp.hpp"

#include <cstdio>

#if defined(NEOST_HAS_MUSASHI)
extern "C" {
#include "m68k.h"
}
#endif

#if defined(NEOST_HAS_MOIRA)
#include "Moira.h"
#endif

namespace {
    Bus*    g_bus = nullptr;        // bus actif vu par les callbacks CPU
    bool    g_vblPending = false;   // VBL (niveau 4) en attente d'acquittement
    bool    g_hblPending = false;   // HBL (niveau 2) en attente d'acquittement
    Tracer* g_tracer = nullptr;     // traceur optionnel (nullptr = aucun surcoût)
    // Garde « double bus fault » : armée quand on déclenche une bus error, désarmée
    // au début de l'instruction SUIVANTE (hook). Si une NOUVELLE bus error survient
    // alors qu'elle est armée, c'est qu'un accès a fauté PENDANT l'empilement de la
    // trame d'exception (SSP/PC corrompus, code parti en vrille) : sur un vrai 68000
    // cela halte le CPU. On reproduit ce halt au lieu de récurser → l'hôte ne
    // segfault plus et le mode headless peut vider sa trace/série.
    bool    g_inBusError = false;
    // Préemption du timeslice sous Moira : posé par endTimeslice() (depuis un
    // callback de l'ordonnanceur, en plein milieu d'une instruction), testé après
    // chaque instruction dans la boucle run() pour rendre la main à l'horloge.
    // Musashi a son propre mécanisme (m68k_end_timeslice), ce drapeau ne sert qu'à Moira.
    bool    g_endSlice = false;
    void    neostUpdateIpl();       // recalcule l'IPL (dispatch Musashi/Moira)
}

// -----------------------------------------------------------------------------
//  Backend Moira (cœur 68000 cycle-exact, MIT) — sous-classe routant la mémoire
//  vers le Bus et reproduisant le vectoring ST (MFP vectorisé niveau 6,
//  VBL/HBL auto-vectorisés) via readIrqUserVector (irqMode USER).
// -----------------------------------------------------------------------------
#if defined(NEOST_HAS_MOIRA)
namespace {
class NeostMoira : public moira::Moira {
public:
    NeostMoira() { setModel(moira::Model::M68000); irqMode = moira::IrqMode::USER; }

    // Une adresse non décodée déclenche une bus error : on lève l'exception
    // moira::BusError (rattrapée par Moira::execute → execBusError) avec une trame
    // d'exception de groupe 0 identique à celle que construit Moira::makeFrame
    // (privée, donc reproduite ici). C'est ainsi qu'EmuTOS sonde le matériel
    // optionnel — sans ça, Moira lit l'adresse fantôme et la détection HW d'EmuTOS
    // part en vrille (bureau GEM sans menu ni curseur).
    [[noreturn]] void raiseBusError(moira::u32 addr, bool write) const {
        moira::StackFrame f{};
        const moira::u16 ird = getIRD();
        // code = IR(15..5) | function-code(2..0) | bit4 R/W (1 = lecture sur 68000).
        f.code = (ird & 0xFFE0) | readFC() | (write ? 0 : 0x10);
        f.addr = addr;
        f.ird  = ird;
        f.sr   = getSR();
        f.pc   = getPC();
        f.fc   = readFC();
        f.ssw  = f.fc;
        throw moira::BusError(f);
    }

    // Déclenche la bus error, SAUF si une faute est déjà en cours (double bus
    // fault pendant l'empilement de trame) → on halte le CPU comme le vrai 68000,
    // au lieu de relancer une exception (qui aborterait l'hôte). Renvoie true si
    // halté (l'appelant doit alors fournir une valeur neutre).
    bool faultOrHalt(moira::u32 a, bool write) const {
        if (g_inBusError) { const_cast<NeostMoira*>(this)->flags |= moira::State::HALTED; return true; }
        g_inBusError = true;
        raiseBusError(a, write);            // [[noreturn]] : lève moira::BusError
        return true;                        // inatteignable
    }
    moira::u8  read8 (moira::u32 a) const override { if (g_bus->busFaultN(a, 1) && faultOrHalt(a, false)) return 0; return g_bus->read8(a); }
    moira::u16 read16(moira::u32 a) const override { if (g_bus->busFaultN(a, 2) && faultOrHalt(a, false)) return 0; return g_bus->read16(a); }
    void write8 (moira::u32 a, moira::u8  v) const override { if (g_bus->busFaultN(a, 1)) { if (faultOrHalt(a, true)) return; } g_bus->write8(a, v); }
    void write16(moira::u32 a, moira::u16 v) const override { if (g_bus->busFaultN(a, 2)) { if (faultOrHalt(a, true)) return; } g_bus->write16(a, v); }
    // Lecture du vecteur de reset (SSP/PC) via l'overlay ROM : jamais de bus error.
    moira::u16 read16OnReset(moira::u32 a) const override { return g_bus->read16(a); }

    // Le 68000 est-il en attente (instruction STOP) ? Permet à la boucle d'horloge
    // de SAUTER l'attente au lieu de la simuler cycle par cycle (cf. run()).
    bool isStopped() const { return (flags & moira::State::STOPPED) != 0; }

    moira::u16 readIrqUserVector(moira::u8 level) const override {
        if (level == 6 && g_bus->mfp) {                 // MFP : vecteur fourni par le 68901
            const int v = g_bus->mfp->iack();
            if (g_tracer) g_tracer->onInterrupt(level, v);
            neostUpdateIpl();
            return (v >= 0) ? moira::u16(v) : moira::u16(24);
        }
        if (g_tracer) g_tracer->onInterrupt(level, 24 + level);   // VBL/HBL auto-vectorisés
        if (level == 4) g_vblPending = false; else if (level == 2) g_hblPending = false;
        neostUpdateIpl();
        return moira::u16(24 + level);
    }
};
NeostMoira* g_moira = nullptr;     // cœur Moira actif (nullptr = Musashi)
}
#endif

namespace {
// Recalcule l'IPL présenté au CPU ACTIF : MFP (6) > VBL (4) > HBL (2).
void neostUpdateIpl() {
    const bool mfp6 = g_bus && g_bus->mfp && g_bus->mfp->irqPending();
    int lvl;
    // MegaSTE : TOUTES les IRQ sont GATÉES par le SCU (SysIntMask/VmeIntMask) avant
    // d'atteindre le CPU — toujours actif comme `SCU_IsEnabled()` d'Hatari (= MegaSTE/TT).
    // Tout OS MegaSTE programme le SCU tôt au boot (TOS 2.06, EmuTOS 256K, diagnostic).
    if (g_bus && g_bus->machine == MachineType::MegaSte) {
        g_bus->scu.syncState(mfp6, g_vblPending, g_hblPending);   // état ← sources vivantes
        lvl = g_bus->scu.gatedLevel();                            // plus haut niveau autorisé
    } else {
        lvl = mfp6 ? 6 : g_vblPending ? 4 : g_hblPending ? 2 : 0;
    }
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) { g_moira->setIPL(static_cast<moira::u8>(lvl)); return; }
#endif
#if defined(NEOST_HAS_MUSASHI)
    m68k_set_irq(static_cast<unsigned int>(lvl));
#endif
}
}

#if defined(NEOST_HAS_MUSASHI)
// Hook appelé par Musashi avant CHAQUE instruction (M68K_INSTRUCTION_HOOK=1).
static void neostInstrHook(unsigned int pc) {
    g_inBusError = false;          // une instruction démarre → la dernière bus error est retombée
    if (g_tracer) g_tracer->onInstruction(pc);
}

// Variables internes Musashi remplissant la trame de bus error (adresse fautive,
// R/W, code fonction). On les renseigne avant m68k_pulse_bus_error pour que la
// trame de groupe 0 contienne la VRAIE adresse d'accès — les ROMs de diagnostic
// la lisent et l'affichent (« Bus Error Access Address: ... »).
extern "C" {
    extern unsigned int m68ki_aerr_address;
    extern unsigned int m68ki_aerr_write_mode;
    extern unsigned int m68ki_aerr_fc;
}

// Déclenche une bus error Musashi (avec adresse fautive `addr`), ou HALTE le CPU
// en cas de double faute (cf. g_inBusError). Retourne true si halté (l'appelant
// doit renvoyer une valeur neutre).
static bool neostBusError(unsigned int addr, bool write) {
    if (g_inBusError) {            // faute pendant le traitement d'une faute → double bus fault
        m68k_pulse_halt();
        m68k_end_timeslice();
        return true;
    }
    g_inBusError = true;
    m68ki_aerr_address    = addr & 0x00FFFFFF;   // adresse d'accès empilée dans la trame
    m68ki_aerr_write_mode = write ? 0x00 : 0x10; // SSW bit R/W (1 = lecture sur 68000)
    m68ki_aerr_fc         = 0x05;                // code fonction : superviseur, donnée
    m68k_pulse_bus_error();
    return false;
}

// Cycle d'acquittement Musashi : MFP vectorisé, VBL/HBL auto-vectorisés/désarmés.
static int neostIntAck(int level) {
    if (level == 6 && g_bus && g_bus->mfp) {
        const int v = g_bus->mfp->iack();
        if (g_tracer) g_tracer->onInterrupt(level, v);
        neostUpdateIpl();
        return (v >= 0) ? v : static_cast<int>(M68K_INT_ACK_SPURIOUS);
    }
    if (level == 4 || level == 2) {
        if (g_tracer) g_tracer->onInterrupt(level, 24 + level);
        if (level == 4) g_vblPending = false; else g_hblPending = false;
        neostUpdateIpl();
    }
    return static_cast<int>(M68K_INT_ACK_AUTOVECTOR);
}
#endif

CpuCore Cpu68k::parseCore(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (l == "moira") return CpuCore::Moira;
    return CpuCore::Musashi;
}

const char* Cpu68k::coreName(CpuCore c) {
    return c == CpuCore::Moira ? "moira" : "musashi";
}

Cpu68k::Cpu68k(Bus& bus, CpuCore core) : core_(core) {
    g_bus = &bus;
    initCore();
}

// (Ré)initialise le cœur actif. Appelé par le constructeur ET par setCore() (bascule
// à chaud). Suppose qu'un éventuel ancien cœur Moira a déjà été libéré par setCore().
void Cpu68k::initCore() {
#if defined(NEOST_HAS_MOIRA)
    if (core_ == CpuCore::Moira) {
        g_moira = new NeostMoira();         // backend cycle-exact (irqMode/USER, M68000)
        return;                              // pas d'init Musashi quand Moira est actif
    }
#else
    if (core_ == CpuCore::Moira) {
        std::fprintf(stderr, "[cpu] cœur Moira non compilé — repli sur Musashi.\n");
        core_ = CpuCore::Musashi;
    }
#endif

#if defined(NEOST_HAS_MUSASHI)
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);   // le ST d'origine est un 68000 8 MHz
    m68k_set_int_ack_callback(neostIntAck);   // vectorisation MFP + désarmement VBL
    m68k_set_instr_hook_callback(neostInstrHook);  // alimente le traceur (si branché)
#endif
}

// Bascule de cœur à chaud : libère l'ancien Moira s'il existe, puis ré-init.
void Cpu68k::setCore(CpuCore core) {
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) { delete g_moira; g_moira = nullptr; }   // libère l'ancien cœur Moira
#endif
    core_ = core;
    initCore();
}

void Cpu68k::setTracer(Tracer* t) {
    g_tracer = t;
    if (t) t->setCpu(this);    // le Tracer lit les registres via ce CPU (core-aware)
}

void Cpu68k::reset() {
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) {
        g_bus->bootOverlay = true;
        g_moira->reset();                    // lit SSP/PC via read16OnReset (overlay ROM)
        g_bus->bootOverlay = false;
        return;
    }
#endif
#if defined(NEOST_HAS_MUSASHI)
    g_bus->bootOverlay = true;     // SSP/PC doivent venir de la ROM...
    m68k_pulse_reset();            // ...Musashi les lit ici via le bus...
    g_bus->bootOverlay = false;    // ...puis la table des vecteurs RAM reprend la main.
#endif
}

int Cpu68k::run(int cycles) {
    inRun_ = true;
    struct RunGuard { bool& f; ~RunGuard() { f = false; } } guard{inRun_};   // hors run → delta intra-quantum = 0
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) {
        const moira::i64 c0 = g_moira->getClock();
        quantumStartClock_ = static_cast<int64_t>(c0);   // pour cyclesRunInQuantum()
        g_endSlice = false;                              // un éventuel résidu de préemption ne doit pas couper le 1er pas
        const moira::i64 target = c0 + cycles;
        while (g_moira->getClock() < target) {
            g_inBusError = false;                        // nouvelle instruction → faute précédente retombée
            if (g_moira->isHalted()) { g_moira->setClock(target); break; }  // double bus fault → CPU arrêté
            g_moira->execute();                          // une instruction
            if (g_tracer) g_tracer->onInstruction(g_moira->getPC0());
            // Préemption : une écriture matérielle pendant cette instruction a pu
            // armer un événement plus proche que la cible → on rend la main pour
            // que la boucle d'horloge le serve (cf. Scheduler::setEndSlice).
            if (g_endSlice) { g_endSlice = false; break; }
            // Si le CPU est en STOP après cette instruction, aucune IRQ pendante ne
            // l'a réveillé (execute() les teste) : rien ne se passera avant le
            // prochain événement (= `target`, fixé par l'ordonnanceur). On SAUTE
            // l'attente au lieu de la simuler cycle par cycle (sinon ~25× plus lent
            // et l'émulation rame en temps réel sur les STOP d'EmuTOS/TOS).
            if (g_moira->isStopped() && g_moira->getClock() < target) {
                g_moira->setClock(target);
                break;
            }
        }
        return static_cast<int>(g_moira->getClock() - c0);
    }
#endif
#if defined(NEOST_HAS_MUSASHI)
    return m68k_execute(cycles);
#else
    (void)cycles;
    return cycles;   // stub : avance le temps sans rien exécuter
#endif
}

// Wait states de bus (cf. en-tête / Hatari M68000_SyncCpuBus). Appelé par le Shifter
// PENDANT l'exécution d'une instruction (depuis read8/write8 d'un registre aligné 4
// cycles) : on avance l'horloge du cœur, ce qui rallonge l'instruction en cours et
// décale tous les accès suivants. Moira (cycle-exact) avance son clock ; Musashi ne
// modélise pas la contention → no-op.
void Cpu68k::addBusWaitCycles(int n) {
    if (n <= 0) return;
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) { g_moira->setClock(g_moira->getClock() + n); return; }
#endif
    (void)n;
}

// Cycles écoulés depuis le début du quantum run() courant (cf. en-tête).
int64_t Cpu68k::cyclesRunInQuantum() const {
    if (!inRun_) return 0;     // hors run : l'horloge sched.now() est déjà à jour
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) return static_cast<int64_t>(g_moira->getClock()) - quantumStartClock_;
#endif
#if defined(NEOST_HAS_MUSASHI)
    return static_cast<int64_t>(m68k_cycles_run());
#endif
    return 0;
}

void Cpu68k::endTimeslice() {
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) { g_endSlice = true; return; }   // testé après l'instruction courante (cf. run)
#endif
#if defined(NEOST_HAS_MUSASHI)
    m68k_end_timeslice();   // le CPU finit son instruction puis m68k_execute rend la main
#endif
}

void Cpu68k::updateIpl() {
    neostUpdateIpl();
}

void Cpu68k::raiseVbl() {
    g_vblPending = true;
    neostUpdateIpl();
}

void Cpu68k::raiseHbl() {
    g_hblPending = true;
    neostUpdateIpl();
}

uint32_t Cpu68k::pc() const {
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) return g_moira->getPC0();
#endif
#if defined(NEOST_HAS_MUSASHI)
    return m68k_get_reg(nullptr, M68K_REG_PC);
#else
    return 0;
#endif
}

uint32_t Cpu68k::reg(int idx) const {
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) return idx < 8 ? g_moira->getD(idx) : g_moira->getA(idx - 8);
#endif
#if defined(NEOST_HAS_MUSASHI)
    // D0-D7 puis A0-A7 : les énums Musashi sont contigus dans cet ordre.
    return m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + idx));
#else
    (void)idx; return 0;
#endif
}

uint16_t Cpu68k::sr() const {
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) return g_moira->getSR();
#endif
#if defined(NEOST_HAS_MUSASHI)
    return static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
#else
    return 0;
#endif
}

// -----------------------------------------------------------------------------
//  Callbacks mémoire imposés par Musashi → redirigés vers le Bus.
// -----------------------------------------------------------------------------
#if defined(NEOST_HAS_MUSASHI)
extern "C" {

// Une adresse non décodée déclenche une bus error (longjmp Musashi : avorte
// l'instruction). C'est ainsi qu'EmuTOS sonde le matériel optionnel.
unsigned int m68k_read_memory_8 (unsigned int a) { if (g_bus->busFaultN(a, 1)) { neostBusError(a, false); return 0; } return g_bus->read8 (a); }
unsigned int m68k_read_memory_16(unsigned int a) { if (g_bus->busFaultN(a, 2)) { neostBusError(a, false); return 0; } return g_bus->read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { if (g_bus->busFaultN(a, 4)) { neostBusError(a, false); return 0; } return g_bus->read32(a); }

void m68k_write_memory_8 (unsigned int a, unsigned int v) { if (g_bus->busFaultN(a, 1)) { neostBusError(a, true); return; } g_bus->write8 (a, static_cast<uint8_t>(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { if (g_bus->busFaultN(a, 2)) { neostBusError(a, true); return; } g_bus->write16(a, static_cast<uint16_t>(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { if (g_bus->busFaultN(a, 4)) { neostBusError(a, true); return; } g_bus->write32(a, v); }

// Accès "immuables" utilisés par le désassembleur : pas d'effet de bord MMIO.
unsigned int m68k_read_disassembler_16(unsigned int a) { return g_bus->read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return g_bus->read32(a); }

} // extern "C"
#endif
