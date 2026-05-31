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

#if defined(NEOST_HAS_MUSASHI)
extern "C" {
#include "m68k.h"
}
#endif

namespace {
    Bus* g_bus = nullptr;   // bus actif vu par les callbacks Musashi
}

Cpu68k::Cpu68k(Bus& bus) {
    g_bus = &bus;
#if defined(NEOST_HAS_MUSASHI)
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);   // le ST d'origine est un 68000 8 MHz
#endif
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

void Cpu68k::setIrq(int level) {
#if defined(NEOST_HAS_MUSASHI)
    m68k_set_irq(static_cast<unsigned int>(level));
#else
    (void)level;
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

unsigned int m68k_read_memory_8 (unsigned int a) { return g_bus->read8 (a); }
unsigned int m68k_read_memory_16(unsigned int a) { return g_bus->read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return g_bus->read32(a); }

void m68k_write_memory_8 (unsigned int a, unsigned int v) { g_bus->write8 (a, static_cast<uint8_t>(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { g_bus->write16(a, static_cast<uint16_t>(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { g_bus->write32(a, v); }

// Accès "immuables" utilisés par le désassembleur : pas d'effet de bord MMIO.
unsigned int m68k_read_disassembler_16(unsigned int a) { return g_bus->read16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return g_bus->read32(a); }

} // extern "C"
#endif
