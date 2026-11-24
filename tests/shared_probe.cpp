// =============================================================================
//  shared_probe.cpp — neost_core lié dans une bibliothèque PARTAGÉE (option
//  NEOST_PIC, cf. CMakeLists.txt). La forme d'un futur cœur libretro : une .so qui
//  n'exporte que des symboles C et embarque tout le cœur (docs/LIBRETRO.md).
//
//  Sans ROM : la Machine tourne sur une ROM nulle (BRA sur place).
// =============================================================================
#include "core/Machine.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

#if defined(_WIN32)
#define NEOST_PROBE_EXPORT extern "C" __declspec(dllexport)
#else
#define NEOST_PROBE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Taille de l'état autonome après une trame, ou -1.
NEOST_PROBE_EXPORT long neost_shared_probe(void) {
    Machine m(512u * 1024u, CpuCore::Moira, MachineType::St);
    m.bus.rom.assign(0x30000, 0);
    m.bus.romBase = 0xE00000;
    const uint8_t rom[] = {0, 1, 0, 0, 0, 0xE0, 0, 8, 0x60, 0xFE};   // BRA sur place
    std::copy(std::begin(rom), std::end(rom), m.bus.rom.begin());
    m.reset();
    m.runFrame();
    std::vector<uint8_t> st;
    if (!m.saveStateWithMedia(st) || !m.loadStateWithMedia(st.data(), st.size())) return -1;
    return long(st.size());
}
