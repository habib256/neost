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
#include "core/DmaSound.hpp"
#include "core/Blitter.hpp"
#include "core/Glue.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"
#include "io/Fdc.hpp"
#include "io/Rtc.hpp"
#include "io/MidiAcia.hpp"

class Machine {
public:
    // Timing PAL basse résolution 50 Hz = valeurs de RÉFÉRENCE (et défaut). La
    // géométrie RÉELLE d'une trame est désormais dynamique (50/60/71 Hz), dérivée
    // de la résolution + $FF820A et verrouillée à beginFrame — cf. Shifter::Geometry
    // et les membres cpl_/lpf_/disp_ ci-dessous.
    static constexpr int CYCLES_PER_LINE = 512;
    static constexpr int LINES_PER_FRAME = 313;
    static constexpr int VISIBLE_LINES   = 200;

    // cpuCore : cœur 68000 à utiliser ; machine : profil matériel (ST/STE/…).
    // Tous deux choisis AVANT le démarrage (figés à la construction).
    explicit Machine(std::size_t ramBytes = 512u * 1024u,
                     CpuCore cpuCore = CpuCore::Musashi,
                     MachineType machine = MachineType::Ste);

    MachineType machineType() const { return machineType_; }

    // Abaisse le type machine si le TOS de `romPath` ne le supporte pas — port de
    // Hatari `TOS_CheckSysConfig` : un TOS <= 1.04 (TOS 1.0x, EmuTOS 192 Ko qui se
    // présente en « Atari ST » 1.4) ne tourne qu'en mode ST/68000 → sur STE/Mega STE
    // on bascule en ST (avertissement sur stderr). Le Mega ST, lui, tourne nativement
    // sous TOS 1.0x → conservé. À appeler AVANT de construire la Machine (lit la
    // version dans l'en-tête ROM, mot big-endian à l'offset 2).
    static MachineType adjustMachineForTos(MachineType requested, const std::string& romPath);

    bool loadTos(const std::string& path)  { return bus.loadTos(path); }
    bool loadCart(const std::string& path) { return bus.loadCart(path); }
    void ejectCart() { bus.ejectCart(); }
    bool loadDisk(const std::string& path)  { return fdc.loadImage(path, 0); }   // lecteur A
    bool loadDiskB(const std::string& path) { return fdc.loadImage(path, 1); }   // lecteur B (optionnel)
    void reset() { psg.reset(); dmasnd.reset(); cpu.reset(); }
    // Reset à FROID (power-cycle) : efface toute la ST-RAM, ce qui invalide le
    // « memvalid » de TOS — il refait alors un boot COMPLET (re-détection mémoire,
    // re-init OS) au lieu du boot à chaud d'un simple reset. Puis reset matériel.
    void hardReset() { bus.ram.assign(bus.ram.size(), 0); psg.reset(); dmasnd.reset(); cpu.reset(); }

    // Reconfigure la machine À CHAUD sans recréer l'objet (son adresse reste
    // stable → les références externes, p.ex. Audio→psg/dmasnd, restent valides) :
    // change la taille de ST-RAM, le modèle matériel et le cœur 68000. Efface la
    // RAM (boot à froid). Les composants déjà câblés au bus sont conservés.
    // L'appelant recharge la ROM si besoin, repose le moniteur, puis reset().
    void reconfigure(std::size_t ramBytes, CpuCore cpuCore, MachineType machine) {
        bus.ram.assign(ramBytes, 0);
        bus.machine     = machine;
        machineType_    = machine;
        glue.memConfig_ = memConfigForBytes(ramBytes);
        cpu.setCore(cpuCore);              // bascule de cœur 68000 si nécessaire
    }

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
    DmaSound  dmasnd{bus};
    Blitter   blitter{bus};
    Glue      glue;
    Mfp       mfp;
    Ikbd      ikbd{mfp};
    Fdc       fdc{bus, psg, mfp};
    Rtc       rtc;
    MidiAcia  midi{mfp};
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
    void onTimerB();        // Timer B event-count sur DE (position DE-dépendante)
    void onHbl();           // HBL niveau 2 (cycle 508)
    void onVbl();

    // Position (cycle DANS la ligne) du tic Timer B event-count, dérivée du
    // Display-Enable (résolution + 50/60 Hz du Shifter, front début/fin via l'AER du
    // MFP) — cf. Shifter::timerBLinePos / Hatari Video_TimerB_GetDefaultPos.
    int timerBPos() const { return shifter.timerBLinePos(mfp.timerBStartOfLine()); }

    MachineType machineType_ = MachineType::Ste;   // profil matériel (figé au boot)

    int64_t frameStart_ = 0;  // cycle (horloge continue) du début de la trame courante
    int renderLine_  = 0;     // prochaine scanline à décoder
    int tbLine_      = 0;     // prochaine ligne pour le tic Timer B
    int hblLine_     = 0;     // prochaine ligne pour le HBL niveau 2
    // (Le RTC avance en paresseux à la lecture, cf. Rtc::catchUp — plus de compteur ici.)

    // Géométrie de la trame COURANTE (50/60/71 Hz), verrouillée par scheduleFrameEvents
    // depuis Shifter::geometry() juste après beginFrame. Défauts = 50 Hz PAL.
    int cpl_   = CYCLES_PER_LINE;   // cycles par ligne (512/508/224)
    int lpf_   = LINES_PER_FRAME;   // lignes par trame (313/263/501)
    int disp_  = VISIBLE_LINES;     // scanlines affichées = Timer B + rendu (200/400)
    int deEnd_ = DE_END_CYCLE;      // fin Display-Enable dans la ligne (376/372/160)
    // Numéro de la PREMIÈRE scanline affichée (VDE_On : 63/34/34, cf. Shifter::Geometry).
    // L'affichage actif occupe les scanlines [dispStart_, dispStart_+disp_) ; les lignes
    // 0..dispStart_-1 sont la bordure HAUTE, dispStart_+disp_..lpf_-1 la bordure BASSE.
    // Avant, l'affichage commençait à la ligne 0 (pas de bordure haute dans la timeline).
    int dispStart_ = 63;            // VDE_On de la trame courante (verrouillé par scheduleFrameEvents)

    // Positions au cycle DANS la ligne (STF PAL 50 Hz, cf. Hatari video.h).
    static constexpr int DE_END_CYCLE   = 376;   // fin Display-Enable → rendu ligne
    // (Timer B : position dérivée du Display-Enable, cf. timerBPos / Shifter::timerBLinePos.)
    static constexpr int HBL_CYCLE      = 508;   // 512 - 4 (Hbl_Int_Pos_Low_50)
};
