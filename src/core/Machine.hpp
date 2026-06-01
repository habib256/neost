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
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"
#include "io/Fdc.hpp"

class Machine {
public:
    // Timing PAL basse résolution (cf. en-tête de main.cpp).
    static constexpr int CYCLES_PER_LINE = 512;
    static constexpr int LINES_PER_FRAME = 313;
    static constexpr int VISIBLE_LINES   = 200;

    explicit Machine(std::size_t ramBytes = 512u * 1024u);

    bool loadTos(const std::string& path)  { return bus.loadTos(path); }
    bool loadDisk(const std::string& path) { return fdc.loadImage(path); }
    void reset() { cpu.reset(); }

    // Exécute UNE trame complète : 313 lignes de cycles CPU, 4 tics Timer C
    // (≈200 Hz) et un VBL niveau 4. Décode l'image en fin de trame.
    //
    //  Depuis la Phase 1 de cycle-accuracy (cf. docs/CYCLE_ACCURACY.md), la trame
    //  est pilotée par `sched` : on exécute le CPU jusqu'au prochain événement
    //  daté (HBL/Timer C/VBL) puis on déclenche son handler. Le quantum CPU reste
    //  la ligne (512 cycles) → timing IDENTIQUE au modèle « par blocs » d'avant.
    void runFrame();

    // Accès direct aux composants (frontend, débogueur, headless).
    Bus       bus;
    Shifter   shifter{bus};
    YM2149    psg;
    Glue      glue;
    Mfp       mfp;
    Ikbd      ikbd{mfp};
    Fdc       fdc{bus, psg, mfp};
    Cpu68k    cpu{bus};
    Scheduler sched;

private:
    // Câble les callbacks de l'ordonnanceur (appelé une fois, au constructeur).
    void installSchedulerCallbacks();
    // Arme le premier événement de chaque source pour la trame courante.
    void scheduleFrameEvents();
    // Handlers des événements datés.
    void onRender();        // décode la prochaine scanline (Phase 2)
    void onHbl();
    void onTimerC();
    void onVbl();

    int timerCIndex_ = 0;   // tic Timer C courant (0..3) dans la trame
    int renderLine_  = 0;   // prochaine scanline à décoder dans la trame
};
