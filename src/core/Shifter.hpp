// =============================================================================
//  Shifter.hpp — Puce vidéo de l'Atari ST (extraction du framebuffer).
//
//  PUR DÉCODEUR : le Shifter lit la RAM vidéo de façon planaire et produit un
//  buffer ARGB linéaire (Data-Oriented). Aucune dépendance graphique ici — le
//  frontend (GUI) téléverse pixels() dans une texture, le mode headless les
//  ignore ou les dump. C'est ce découplage qui permet de tourner sans GL/GLFW.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <vector>

#include "core/Bus.hpp"

class Shifter {
public:
    // Résolutions ST, sélectionnées par le registre $FF8260 :
    //   0 = basse  (320x200, 16 couleurs, 4 plans)
    //   1 = moyenne(640x200,  4 couleurs, 2 plans)
    //   2 = haute  (640x400, monochrome,  1 plan)
    enum class Mode : uint8_t { Low = 0, Medium = 1, High = 2 };

    explicit Shifter(Bus& bus);

    // Décode tout le framebuffer visible selon la résolution courante.
    void renderFrame();

    // --- Rendu scanline-par-scanline (cycle-accuracy, cf. docs/CYCLE_ACCURACY.md)
    //  `beginFrame()` verrouille la résolution ET la fréquence (50/60/71 Hz) de la
    //  trame (ni l'une ni l'autre ne peut changer en cours de décodage) ;
    //  `renderLine(y)` décode UNE ligne avec l'état COURANT des registres
    //  (palette/base vidéo) → les changements en cours de trame (rasters, scroll
    //  par base) s'appliquent ligne à ligne.
    void beginFrame();
    void renderLine(int y);

    // Fin de trame : si une image Spectrum 512 / du color-cycling a été détecté
    // (palette réécrite intra-ligne, cf. recordColorWrite), re-rend TOUTES les
    // lignes affichées avec une palette qui change AU CYCLE de chaque écriture
    // (port du modèle Hatari spec512.c → jusqu'à 512 couleurs). Sinon ne fait
    // rien : le rendu ligne-à-ligne (palette figée par ligne) suffit et reste
    // strictement inchangé (zéro régression hors spec512).
    void finishFrame();
    bool spec512Active() const { return spec512Active_; }

    // Auto-test DÉTERMINISTE de la machine Glue (chemin STF) : injecte des écritures
    // freq/res synthétiques à des cycles EXACTS et vérifie l'état d'affichage résultant
    // (DisplayStartCycle/EndCycle/BorderMask, nStartHBL/nEndHBL) contre les valeurs
    // documentées d'Hatari (Video_Update_Glue_State). Valide les retraits gauche/droite/
    // haut/bas sans dépendre du timing CPU (≠ test 68k cycle-exact). Renvoie true si OK ;
    // détaille les échecs sur stderr. Appelé par neost-headless --glue-selftest.
    bool glueSelfTest();

    // Horloge « live » = cycle EXACT dans la trame (delta intra-quantum CPU inclus)
    // au moment d'une écriture palette. Indispensable au spec512 : plusieurs
    // écritures par ligne doivent être datées au cycle près, pas au quantum.
    // Posée par Machine (sched.liveNow() - frameStart_). Cf. setBeamClock.
    void setLiveFrameClock(std::function<int64_t()> fn) { liveFrameClock_ = std::move(fn); }

    // Géométrie d'une trame, dérivée de la résolution (mono = 71 Hz) et, en
    // basse/moyenne, de la fréquence 50/60 Hz ($FF820A bit1). Port des constantes
    // STF de `extern/hatari/src/includes/video.h` (CYCLES_PER_LINE_*,
    // SCANLINES_PER_FRAME_*, LINE_START/END_CYCLE_*). Verrouillée à beginFrame.
    struct Geometry {
        int cyclesPerLine;    // 512 (50 Hz) / 508 (60 Hz) / 224 (71 Hz mono)
        int linesPerFrame;    // 313 / 263 / 501
        int displayLines;     // scanlines affichées (= height) : 200 couleur / 400 mono
        int lineStartCycle;   // début Display-Enable : 56 / 52 / 0
        int lineEndCycle;     // fin Display-Enable (→ rendu de la scanline) : 376 / 372 / 160
        // Numéro de la PREMIÈRE scanline affichée dans la trame (VDE_On), port des
        // constantes Hatari VIDEO_START_HBL_* : 63 (50 Hz) / 34 (60 Hz) / 34 (71 Hz).
        // Avant ce champ, NeoST faisait commencer l'affichage actif à la ligne 0 (pas
        // de bordure HAUTE dans la timeline) ; aligner sur VDE_On place l'affichage au
        // bon endroit de la trame (lignes 63..262 en 50 Hz) — prérequis du retrait de
        // bordures (les manipulations 50/60 Hz se font DANS les bordures haut/bas) et
        // corrige le décalage dLine du spec512. La fin d'affichage = dispStartLine +
        // displayLines (VDE_Off : 263 / 234 / 434).
        int dispStartLine;
    };
    // Géométrie de la trame VERROUILLÉE (cf. frameMode_/frameSync_, posés par beginFrame).
    Geometry geometry() const { return geometryFor(frameMode_, frameSync_); }

    // Accès au buffer décodé (ARGB8888) pour le frontend ou un dump.
    const uint32_t* pixels() const { return frame_.data(); }
    int width()  const { return curW_; }      // largeur du buffer (overscan inclus)
    int height() const { return curH_; }      // hauteur du buffer (overscan inclus)
    // Nombre de lignes ACTIVES (display-enable) à décoder : 200 (couleur) / 400 (mono).
    // ≠ height() quand l'overscan ajoute des bordures haut/bas. La boucle de rendu de
    // Machine itère sur activeHeight() ; renderLine(y) place la ligne active y à
    // l'offset bordure-haut dans le buffer (cf. activeY_).
    int activeHeight() const { return curAH_; }

    // Fréquence de rafraîchissement COURANTE (mono = 71 Hz, sinon $FF820A bit1 :
    // 50 Hz PAL / 60 Hz NTSC). Pour l'affichage / le débogage (la trame est cadencée
    // par cette fréquence depuis les géométries vidéo, cf. geometry()).
    int refreshHz() const {
        if (mode == Mode::High) return 71;
        return (sync & 0x02) ? 50 : 60;
    }

    // Interface MMIO ($FF8200-$FF8260) appelée par le Bus.
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Position (cycle DANS la ligne) du tic Timer B en mode event-count, portée de
    // Hatari `Video_TimerB_GetDefaultPos` : on compte les FINS de ligne (DE_end+24)
    // par défaut, ou les DÉBUTS (DE_start+24) si l'AER du MFP sélectionne le front de
    // début (`startOfLine`). Les positions Display-Enable dépendent de la résolution
    // (haute = 71 Hz) et, en basse/moyenne, de la fréquence 50/60 Hz ($FF820A bit1).
    // Constantes de `extern/hatari/src/includes/video.h` (LINE_START/END_CYCLE_*).
    // Remplace l'ancienne position figée au cycle 400 (≙ 50 Hz / fin de ligne seule).
    int timerBLinePos(bool startOfLine) const {
        constexpr int kOffset = 24;          // TIMERB_VIDEO_CYCLE_OFFSET
        int de;
        if (mode == Mode::High)   de = startOfLine ? 0  : 160;   // 71 Hz mono
        else if (sync & 0x02)     de = startOfLine ? 56 : 376;   // 50 Hz (défaut PAL)
        else                      de = startOfLine ? 52 : 372;   // 60 Hz
        return de + kOffset;
    }

    // Horloge faisceau : renvoie le nombre de cycles écoulés DANS la trame courante
    // (0 au début de trame). Posée par Machine ; sert à reconstruire le compteur
    // d'adresse vidéo $FF8205/07/09 (position courante du balayage). Cf. Hatari
    // Video_ScreenCounter_ReadByte / Video_CalculateAddress.
    void setBeamClock(std::function<int64_t()> fn) { beamClock_ = std::move(fn); }

    // --- État exposé au débogueur (lecture directe) -------------------------
    uint32_t videoBase = 0;                 // adresse RAM du framebuffer (registres haut/milieu/bas)
    std::array<uint16_t, 16> palette{};     // 16 registres couleur $FF8240 ($0RGB, 3 bits/canal)
    Mode mode = Mode::Low;                  // moniteur couleur → basse résolution par défaut
    // Registre de synchro $FF820A : bit1 = 50/60 Hz (1 = 50 Hz), bit0 = sync externe.
    // NeoST cadence une trame PAL 50 Hz (313 lignes, cf. Machine), donc ce registre
    // doit refléter 50 Hz (bit1=1) — sinon un logiciel qui LIT la fréquence ici
    // (diagnostics : « 50/60 Hz ») la croit 60 Hz et ses mesures timer/VBL faussent.
    uint8_t sync = 0x02;                    // défaut : 50 Hz PAL (cohérent avec 313 lignes)

    // --- Registres STE supplémentaires (gardés à machineIsSte) ---------------
    // Scroll fin horizontal $FF8264 (sans prefetch) / $FF8265 (avec prefetch) :
    // décalage de 0-15 px CÂBLÉ dans renderLine (décalage à gauche + groupe de 16 px
    // lu en plus à droite, modèle prefetch). Cf. Hatari Video_HorScroll_Write
    // (HWScrollCount/HWScrollPrefetch). Une écriture PENDANT l'affichage d'une ligne
    // est DIFFÉRÉE à la fin de cette ligne (port NewHWScrollCount, cf. write8/endVideoLine).
    uint8_t hwScrollCount = 0;              // 4 bits de scroll fin ($FF8264/65 & 0x0F)
    bool    hwScrollPrefetch = false;       // écriture via $FF8265 → prefetch
    // Largeur de ligne STE $FF820F (line-offset, en MOTS, ajoutés au stride en fin
    // de ligne) — CÂBLÉE dans renderLine et videoCounter. Une écriture APRÈS la fin
    // du Display-Enable de la ligne courante est DIFFÉRÉE (port NewLineWidth).
    uint8_t lineWidth = 0;

private:
    static uint32_t stColorToArgb(uint16_t c);   // $0RGB → ARGB8888
    void resizeFor(Mode m);                       // ajuste le buffer si la rés. change
    uint32_t videoCounter() const;                // adresse vidéo courante ($FF8205/07/09)

    // --- Compteur vidéo MATÉRIALISÉ (port pVideoRaster d'Hatari, video.c) --------
    // Le compteur n'est plus purement analytique (base + y×stride) : il est LATCHÉ
    // depuis $FF8201/03 au début de trame (≙ Video_ClearOnVBL → RestartVideoCounter)
    // puis AVANCE d'un stride à chaque fin de ligne active (endVideoLine, ≙ fin de
    // Video_CopyScreenLine). Conséquences fidèles au matériel : une écriture de la
    // BASE en cours de trame ne s'applique qu'à la trame suivante ; les écritures du
    // COMPTEUR $FF8205/07/09 (STE) et les changements LINEWIDTH/HSCROLL différés
    // s'accumulent ligne à ligne au lieu de rétro-s'appliquer.
    uint32_t vcFrameBase_ = 0;     // base latchée au début de trame (lecture bordure haute)
    uint32_t vcLineBase_  = 0;     // adresse de début de la PROCHAINE ligne active à rendre
    int      vcLineY_     = 0;     // index (0..disp-1) de cette ligne active
    // Écritures STE différées, appliquées en fin de ligne (port video.c : NewHWScrollCount,
    // NewLineWidth, VideoCounterDelayedOffset). -1 = rien en attente.
    int      newHwScrollCount_   = -1;
    bool     newHwScrollPrefetch_ = false;
    int      newLineWidth_       = -1;
    int      vcDelayedOffset_    = 0;   // écart compteur (écriture $FF8205/07/09 pendant le DE)
    // Fin de ligne active (≙ fin de Video_CopyScreenLine) : avance vcLineBase_ du
    // stride (+2 si scroll fin = prefetch d'un mot), applique l'offset compteur
    // différé puis les valeurs HSCROLL/LINEWIDTH en attente. Appelée par renderLine.
    void endVideoLine();
    // Position du faisceau : ligne absolue + cycle dans la ligne. false si pas d'horloge.
    bool beamPos(int& line, int& lineCyc) const;
    // Écriture du compteur vidéo $FF8205/07/09 (STE) — port Video_ScreenCounter_WriteByte.
    void writeVideoCounterByte(uint32_t addr, uint8_t v);

    // Décode les index de palette (ou bit mono) d'une ligne dans `idx` selon la
    // résolution VERROUILLÉE (lecture planaire + scroll fin STE). Partagé par
    // renderLine (palette figée) et finishFrame (palette intra-ligne spec512).
    // `idx` doit pouvoir tenir W + scroll pixels (≤ 656). Renvoie le décalage scroll.
    int decodeLineIndices(int y, uint8_t* idx) const;

    // Enregistre une écriture palette (registre `index`) avec son cycle live dans
    // la trame, pour le re-rendu spec512. Met à jour le compteur de détection.
    void recordColorWrite(int index);

    // Enregistre une écriture sync $FF820A (50/60 Hz) ou résolution $FF8260 au cycle
    // live, pour la détection de RETRAIT de bordures (port machine Glue Hatari,
    // Video_Update_Glue_State). `isRes` = $FF8260, sinon $FF820A.
    void recordSyncWrite(bool isRes, uint8_t val);

    // Wait state de bus 4 cycles (port LIVE de Hatari M68000_SyncCpuBus) : appelé AU
    // DÉBUT d'un accès CPU à un registre couleur ($FF8240-5F) / résolution ($FF8260) /
    // scroll fin ($FF8264/65) du Shifter. Aligne l'accès sur la frontière de bus 4
    // cycles en faisant patienter le CPU (cf. Cpu68k::addBusWaitCycles) → l'écriture
    // palette suivante est datée au cycle ALIGNÉ (recordColorWrite), exactement comme le
    // faisait l'ancien recalage HORS-LIGNE (applyShifterBusAlignment, désormais no-op car
    // les cycles enregistrés sont déjà alignés). Indispensable au timing cycle-exact des
    // démos (spec512, boucles d'auto-synchro fullscreen).
    void syncCpuBus();

    // VDE_On LIVE pour le compteur vidéo $FF8205/07/09 (port du retrait de bordure
    // HAUTE de Hatari Video_Update_Glue_State / Video_EndHBL). Le compteur d'adresse
    // n'avance qu'à partir de la 1ʳᵉ ligne AFFICHÉE (VDE_On) ; une bascule 60 Hz dans
    // la bordure haute (ligne < 63) avance ce VDE_On à 34 (retrait haut), ce qui
    // fait monter $FF8209 PLUS TÔT. Les boucles d'auto-synchro fullscreen (Cuddly Demo)
    // sondent $FF8209 et S'EN SERVENT pour se verrouiller : sans VDE_On live, le compteur
    // ne monte qu'à la ligne 63 (50 Hz) et le verrouillage échoue → flicker. Mis à jour
    // par recordSyncWrite (écritures freq), lu par videoCounter. 50 Hz normal → reste 63
    // (zéro régression). Réinitialisé à beginFrame.
    int liveStartHBL_ = 63;
    void updateLiveStartHBL(int32_t frameCycle, bool isRes, uint8_t val);

    // --- Retrait de bordures : MACHINE GLUE (port Hatari Video_Update_Glue_State +
    //     Video_StartHBL + Video_EndHBL, video.c) -------------------------------
    // Une écriture freq($FF820A)/res($FF8260) datée, pour rejouer la machine Glue
    // hors-ligne en fin de trame (la timeline live est inchangée → zéro régression).
    struct SyncWrite { int32_t frameCycle; uint8_t val; bool isRes; };
    std::vector<SyncWrite> syncWrites_;             // écritures freq/res de la trame
    bool   bordersTrick_ = false;                   // ≥1 ligne avec une bordure retirée

    // État d'affichage d'UNE scanline, port de Hatari SHIFTER_LINE. Calculé par le
    // replay Glue (replayGlue) puis consommé par le rendu fenêtré (renderGlueFrame).
    struct GlueLine {
        int16_t  displayStartCycle;   // début DE (cycle dans la ligne) ; -1 = pas encore posé
        int16_t  displayEndCycle;     // fin DE (0/160/372/376/458/512…)
        int16_t  displayPixelShift;   // décalage pixels (<0 = vers la gauche)
        uint32_t borderMask;          // BORDERMASK_* (cf. Shifter.cpp)
    };
    std::vector<GlueLine> glueLines_;               // état par scanline (taille lpf+2)
    int  glueStartHBL_   = 63;                       // nStartHBL : 1ʳᵉ ligne affichée (peut baisser → top retiré)
    int  glueEndHBL_     = 263;                      // nEndHBL : dernière ligne+1 (peut monter → bottom retiré)
    uint32_t glueVOverscan_ = 0;                     // V_OVERSCAN_* (NO_TOP/NO_BOTTOM/NO_DE…)
    int  glueBlankLines_ = 0;                        // lignes blanches insérées (no-sync)
    int  nScreenRefreshRate_ = 50;                   // fréquence NOMINALE de l'écran (50/60), cf. replayGlue

    // Rejoue la machine Glue sur les syncWrites_ de la trame (ligne par ligne :
    // StartHBL defaults + Update_Glue_State par écriture + détection top/bottom) →
    // remplit glueLines_ / glueStartHBL_ / glueEndHBL_ et arme bordersTrick_.
    void replayGlue();
    // Port fidèle de Video_Update_Glue_State (chemin STF) : applique une écriture
    // freq/res au cycle `lineCycles` de la scanline `line` → met à jour la GlueLine
    // (DE start/end, BorderMask, PixelShift) et les bordures haut/bas (nStartHBL/End).
    void updateGlueState(int line, int lineCycles, bool writeToRes, int curFreqHz);
    // Valeurs par défaut d'une ligne selon res/freq courants (port Video_StartHBL).
    void startHBL(int line, int curRes, int curFreqHz);
    // Machine Glue LIVE : curseur incrémental qui fait tourner startHBL/updateGlueState
    // AU FIL de la trame (mêmes structures que replayGlue, qui ré-écrase tout en fin de
    // trame). Permet à videoCounter() de refléter EN DIRECT la fenêtre DE réelle de la
    // ligne courante (bascules 60/50 mi-ligne : right-2, stop, retraits) — c'est ce que
    // mesurent les routines de calibration fullscreen (Enchanted Land) sur $FF8209.
    void liveGlueCatchUp(int targetLine);
    int         liveGlueLine_   = -1;   // dernière ligne initialisée (startHBL) par le live
    std::size_t liveGlueWi_     = 0;    // prochaine écriture syncWrites_ à consommer
    int         liveGlueRes_    = 0;    // res courante du curseur live (0/1/2)
    int         liveGlueFreq50_ = 1;    // freq courante du curseur live (bit1 $FF820A)
    // Re-rendu fenêtré : pour chaque scanline affichée [glueStartHBL_, glueEndHBL_),
    // décode [displayStartCycle, displayEndCycle) avec adresse vidéo ACCUMULÉE
    // (Video_CalculateAddress) + palette roulante (raster/spec512). Hors fenêtre =
    // couleur de bordure (registre 0 au cycle courant).
    void renderGlueFrame();

    // Décode `nPix` pixels d'une ligne à partir de l'adresse vidéo `base` (modèle
    // fenêtré pour les bordures) dans `idx`. Comme decodeLineIndices mais largeur
    // explicite et base fournie (pas de stride interne).
    int decodeWindowIndices(uint32_t base, int nPix, uint8_t* idx) const;

    // --- Spec512 : palette intra-ligne (port Hatari spec512.c) --------------
    // Une écriture palette dans la trame, datée au cycle (façon CyclePalettes[]).
    struct ColorWrite { int32_t frameCycle; uint16_t colour; uint8_t index; uint32_t pc; };
    std::vector<ColorWrite>  colorWrites_;          // écritures palette de la trame (ordre d'exécution)
    std::array<uint16_t, 16> frameStartPalette_{};  // palette au début de trame (base du replay)
    int  paletteAccesses_ = 0;                      // nb d'écritures palette dans la trame
    bool spec512Active_   = false;                  // seuil franchi → image spec512
    std::function<int64_t()> liveFrameClock_;       // cycle live dans la trame (cf. setLiveFrameClock)

    // Aligne les écritures palette sur la frontière de bus 4 cycles du shifter et
    // propage les wait states (port HORS-LIGNE de Hatari M68000_SyncCpuBus). Les
    // registres couleur/résolution ne s'accèdent que tous les 4 cycles : une écriture
    // qui tombe à un cycle non multiple de 4 fait attendre le CPU (4-(cyc&3)) cycles,
    // ce qui DÉCALE toutes les écritures suivantes (le CPU est gelé). Moira est un
    // 68000 PUR (pas de wait states) → sans ce modèle la boucle spec512 (24× move.l
    // + dbra = 510 cyc/ligne) dérive de ~2 cyc/ligne ; avec, elle tient les 512 cyc/
    // ligne du matériel (dérive nulle). Rejoué offline sur colorWrites_ (timeline live
    // INCHANGÉE = zéro régression). Voir CHANGELOG « spec512 ».
    void applyShifterBusAlignment();

    // Géométrie (cycles/ligne, lignes/trame, DE) pour une résolution + fréquence
    // données. Statique : ne dépend que de (mode, sync) → réutilisée pour la trame
    // verrouillée (geometry()) comme pour un calcul ponctuel.
    static Geometry geometryFor(Mode m, uint8_t syncReg) {
        if (m == Mode::High)      return {224, 501, 400, 0, 160, 34};   // 71 Hz monochrome
        if (syncReg & 0x02)       return {512, 313, 200, 56, 376, 63};  // 50 Hz PAL (défaut)
        return                           {508, 263, 200, 52, 372, 34};  // 60 Hz NTSC
    }

    // --- Bordures (overscan) — port des dimensions visibles Hatari (conv_st.h) ----
    // Buffer visible basse rés couleur = 48+320+48 px × 29+200+47 lignes = 416×276,
    // l'écran actif 320×200 centré (bordures = couleur registre 0). Phase 1 :
    // bordures VISIBLES, sans encore les tricks de RETRAIT (50/60 Hz, hi/lo res) —
    // ceux-ci élargiront la fenêtre d'affichage par ligne (cf. TODO §Vidéo bordures).
    // Médium/mono restent sans bordure pour l'instant (rares en démo / spec512 = low).
    static constexpr int kBorderLeftPx   = 48;
    static constexpr int kBorderRightPx  = 48;
    static constexpr int kBorderTopLines = 29;    // OVERSCAN_TOP
    static constexpr int kBorderBotLines = 47;    // MAX_OVERSCAN_BOTTOM
    static constexpr bool kBordersEnabled = true;
    bool bordered() const { return frameMode_ == Mode::Low && kBordersEnabled; }
    // Largeur de l'écran ACTIF (sans les bordures) : 320 (low) / 640 (med/mono).
    int activeWidth() const { return curW_ - (bordered() ? (kBorderLeftPx + kBorderRightPx) : 0); }

    Bus&          bus_;
    int           curW_ = 0, curH_ = 0;     // dimensions du buffer (overscan inclus)
    int           curAH_ = 0;               // lignes actives à décoder (200/400)
    int           activeX_ = 0, activeY_ = 0;  // offset de l'écran actif dans le buffer
    Mode          frameMode_ = Mode::Low;   // résolution verrouillée pour la trame
    uint8_t       frameSync_ = 0x02;        // fréquence ($FF820A) verrouillée pour la trame
    std::vector<uint32_t> frame_;           // curW_*curH_ pixels ARGB
    std::function<int64_t()> beamClock_;    // cycles dans la trame (cf. setBeamClock)
};
