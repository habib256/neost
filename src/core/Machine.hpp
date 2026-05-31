// =============================================================================
//  Machine.hpp — La carte mère Atari ST assemblée + la boucle d'horloge.
//
//  Regroupe tous les composants (Bus, CPU, Shifter, PSG, MFP, IKBD, GLUE) et
//  encapsule le timing d'une trame. AUCUNE dépendance GUI : c'est ce qui permet
//  d'exécuter exactement la même machine en mode fenêtré (neost) ou en headless
//  (neost-headless), garantissant des traces reproductibles.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <string>

#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Shifter.hpp"
#include "core/YM2149.hpp"
#include "core/Glue.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"

class Machine {
public:
    // Timing PAL basse résolution (cf. en-tête de main.cpp).
    static constexpr int CYCLES_PER_LINE = 512;
    static constexpr int LINES_PER_FRAME = 313;
    static constexpr int VISIBLE_LINES   = 200;

    explicit Machine(std::size_t ramBytes = 512u * 1024u);

    bool loadTos(const std::string& path) { return bus.loadTos(path); }
    void reset() { cpu.reset(); }

    // Exécute UNE trame complète : 313 lignes de cycles CPU, 4 tics Timer C
    // (≈200 Hz) et un VBL niveau 4. Décode l'image en fin de trame.
    void runFrame();

    // Accès direct aux composants (frontend, débogueur, headless).
    Bus      bus;
    Shifter  shifter{bus};
    YM2149   psg;
    Glue     glue;
    Mfp      mfp;
    Ikbd     ikbd{mfp};
    Cpu68k   cpu{bus};
};
