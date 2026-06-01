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

    // cpuCore : cœur 68000 à utiliser (choisi avant le démarrage).
    explicit Machine(std::size_t ramBytes = 512u * 1024u,
                     CpuCore cpuCore = CpuCore::Musashi);

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
    // Handlers des événements datés vidéo (positions au cycle dans la ligne).
    // Les Timers A/C/D (mode délai) sont datés par le MFP lui-même.
    void onRender();        // décode la scanline (≈ fin Display-Enable, cycle 376)
    void onTimerB();        // Timer B event-count sur DE (cycle 400)
    void onHbl();           // HBL niveau 2 (cycle 508)
    void onVbl();

    int64_t frameStart_ = 0;  // cycle (horloge continue) du début de la trame courante
    int renderLine_  = 0;     // prochaine scanline à décoder
    int tbLine_      = 0;     // prochaine ligne pour le tic Timer B
    int hblLine_     = 0;     // prochaine ligne pour le HBL niveau 2

    // Positions au cycle DANS la ligne (STF PAL 50 Hz, cf. Hatari video.h).
    static constexpr int DE_END_CYCLE   = 376;   // fin Display-Enable → rendu ligne
    static constexpr int TIMERB_CYCLE   = 400;   // 376 + 24 (TIMERB_VIDEO_CYCLE_OFFSET)
    static constexpr int HBL_CYCLE      = 508;   // 512 - 4 (Hbl_Int_Pos_Low_50)
};
