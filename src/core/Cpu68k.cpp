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

#if defined(NEOST_HAS_MUSASHI)
extern "C" {
#include "m68k.h"
}
#endif

namespace {
    Bus*    g_bus = nullptr;        // bus actif vu par les callbacks Musashi
    bool    g_vblPending = false;   // VBL (niveau 4) en attente d'acquittement
    Tracer* g_tracer = nullptr;     // traceur optionnel (nullptr = aucun surcoût)
}

#if defined(NEOST_HAS_MUSASHI)
// Hook appelé par Musashi avant CHAQUE instruction (activé par
// -DM68K_INSTRUCTION_HOOK=1). Coût quasi nul tant qu'aucun traceur n'est branché.
static void neostInstrHook(unsigned int pc) {
    if (g_tracer) g_tracer->onInstruction(pc);
}
#endif

#if defined(NEOST_HAS_MUSASHI)
// Recalcule le niveau d'IRQ présenté au 68000 : MFP (6) prime sur VBL (4).
static void neostUpdateIpl() {
    int lvl = 0;
    if (g_bus && g_bus->mfp && g_bus->mfp->irqPending()) lvl = 6;
    else if (g_vblPending)                               lvl = 4;
    m68k_set_irq(static_cast<unsigned int>(lvl));
}

// Cycle d'acquittement : le MFP est vectorisé (renvoie son propre vecteur), le
// VBL est auto-vectorisé et se désarme ici.
static int neostIntAck(int level) {
    if (level == 6 && g_bus && g_bus->mfp) {
        const int v = g_bus->mfp->iack();
        if (g_tracer) g_tracer->onInterrupt(level, v);
        neostUpdateIpl();
        return (v >= 0) ? v : static_cast<int>(M68K_INT_ACK_SPURIOUS);
    }
    if (level == 4) {
        if (g_tracer) g_tracer->onInterrupt(level, 24 + level);  // n° de vecteur auto-vectorisé
        g_vblPending = false;
        neostUpdateIpl();
    }
    return static_cast<int>(M68K_INT_ACK_AUTOVECTOR);
}
#endif

Cpu68k::Cpu68k(Bus& bus) {
    g_bus = &bus;
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
#if defined(NEOST_HAS_MUSASHI)
    g_bus->bootOverlay = true;     // SSP/PC doivent venir de la ROM...
    m68k_pulse_reset();            // ...Musashi les lit ici via le bus...
    g_bus->bootOverlay = false;    // ...puis la table des vecteurs RAM reprend la main.
#endif
}

int Cpu68k::run(int cycles) {
#if defined(NEOST_HAS_MUSASHI)
    return m68k_execute(cycles);
#else
    (void)cycles;
    return cycles;   // stub : avance le temps sans rien exécuter
#endif
}

void Cpu68k::updateIpl() {
#if defined(NEOST_HAS_MUSASHI)
    neostUpdateIpl();
#endif
}

void Cpu68k::raiseVbl() {
#if defined(NEOST_HAS_MUSASHI)
    g_vblPending = true;
    neostUpdateIpl();
#endif
}

uint32_t Cpu68k::pc() const {
#if defined(NEOST_HAS_MUSASHI)
    return m68k_get_reg(nullptr, M68K_REG_PC);
#else
    return 0;
#endif
}

uint32_t Cpu68k::reg(int idx) const {
#if defined(NEOST_HAS_MUSASHI)
    // D0-D7 puis A0-A7 : les énums Musashi sont contigus dans cet ordre.
    return m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + idx));
#else
    (void)idx; return 0;
#endif
}

uint16_t Cpu68k::sr() const {
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
