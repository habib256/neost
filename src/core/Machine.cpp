// =============================================================================
//  Machine.cpp — Câblage des composants + boucle d'horloge d'une trame.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"

Machine::Machine(std::size_t ramBytes) : bus(ramBytes) {
    // Branchement des puces sur le bus (le bus ne possède pas les composants).
    bus.shifter = &shifter;
    bus.psg     = &psg;
    bus.glue    = &glue;
    bus.mfp     = &mfp;
    bus.ikbd    = &ikbd;
    bus.fdc     = &fdc;
    bus.cpu     = &cpu;     // pour rafraîchir l'IPL après chaque accès MMIO
}

void Machine::runFrame() {
    for (int line = 0; line < LINES_PER_FRAME; ++line) {
        cpu.run(CYCLES_PER_LINE);              // cycles CPU de la ligne
        // Timer B compte le Display Enable + HBL niveau 2 : actifs sur les lignes
        // visibles seulement. Le HBL est gaté par le masque du SR (jeux/rasters).
        if (line < VISIBLE_LINES) { mfp.hblank(); cpu.raiseHbl(); }
        // Timer C du MFP ≈ 200 Hz : 4 tics répartis sur la trame (50 Hz). Ce tic
        // système débloque l'accueil EmuTOS et fait vivre le bureau/horloge.
        if (line == 78 || line == 156 || line == 234 || line == 312) {
            mfp.raise(Mfp::SRC_TIMERC);
            cpu.updateIpl();
        }
        if (line == VISIBLE_LINES)
            cpu.raiseVbl();                    // début du VBlank → interruption trame (niv. 4)
    }
    shifter.renderFrame();                     // décode tout l'écran (rés. courante)
}
