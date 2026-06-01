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

    moira::u8  read8 (moira::u32 a) const override { return g_bus->read8(a); }
    moira::u16 read16(moira::u32 a) const override { return g_bus->read16(a); }
    void write8 (moira::u32 a, moira::u8  v) const override { g_bus->write8(a, v); }
    void write16(moira::u32 a, moira::u16 v) const override { g_bus->write16(a, v); }
    moira::u16 read16OnReset(moira::u32 a) const override { return g_bus->read16(a); }

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
    int lvl = 0;
    if (g_bus && g_bus->mfp && g_bus->mfp->irqPending()) lvl = 6;
    else if (g_vblPending)                               lvl = 4;
    else if (g_hblPending)                               lvl = 2;
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
    if (g_tracer) g_tracer->onInstruction(pc);
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

void Cpu68k::setTracer(Tracer* t) {
    g_tracer = t;
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
#if defined(NEOST_HAS_MOIRA)
    if (g_moira) {
        const moira::i64 c0 = g_moira->getClock();
        const moira::i64 target = c0 + cycles;
        if (g_tracer) {                      // trace instruction par instruction
            while (g_moira->getClock() < target) {
                g_moira->execute();
                g_tracer->onInstruction(g_moira->getPC0());
            }
        } else {
            g_moira->executeUntil(target);
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
unsigned int m68k_read_memory_8 (unsigned int a) { if (g_bus->busFault(a)) { m68k_pulse_bus_error(); return 0; } return g_bus->read8 (a); }
unsigned int m68k_read_memory_16(unsigned int a) { if (g_bus->busFault(a)) { m68k_pulse_bus_error(); return 0; } return g_bus->read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { if (g_bus->busFault(a)) { m68k_pulse_bus_error(); return 0; } return g_bus->read32(a); }

void m68k_write_memory_8 (unsigned int a, unsigned int v) { if (g_bus->busFault(a)) { m68k_pulse_bus_error(); return; } g_bus->write8 (a, static_cast<uint8_t>(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { if (g_bus->busFault(a)) { m68k_pulse_bus_error(); return; } g_bus->write16(a, static_cast<uint16_t>(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { if (g_bus->busFault(a)) { m68k_pulse_bus_error(); return; } g_bus->write32(a, v); }

// Accès "immuables" utilisés par le désassembleur : pas d'effet de bord MMIO.
unsigned int m68k_read_disassembler_16(unsigned int a) { return g_bus->read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return g_bus->read32(a); }

} // extern "C"
#endif
