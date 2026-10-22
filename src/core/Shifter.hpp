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

#include "core/StateArchive.hpp"
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
    // Commit des scanlines TERMINÉES, appelé au HBL de chaque ligne (port de
    // l'appel Video_EndHBL du handler HBL d'Hatari, video.c:3319) : la capture
    // lineSnap_ d'une ligne se fait à SA fin de ligne (~cycle 512). Cf. Shifter.cpp.
    void commitScanline(int line);
    bool spec512Active() const { return spec512Active_; }

    // Remise à zéro au RESET machine — port de Video_Reset (video.c:810) : base
    // vidéo, registres STE (line-offset/scroll + modifications différées), compteur
    // vidéo matérialisé, dernier Freq/Res vus par la Glue (ShifterFrame.Freq/Res =
    // −1 → le filtre « même valeur ignorée » repart neutre) et état glue par-trame.
    // Ne touche PAS la palette (Video_Reset ne la réinitialise pas : le contenu des
    // registres couleur survit au reset, seul TOS la reprogramme). Appelé par
    // Machine::reset() (branché par l'orchestrateur).
    void reset();
    // Broche /RESET du 68000 (instruction RESET) : port de Video_Reset_Glue
    // (video.c:904) — SEULEMENT la fréquence et la résolution, PAS l'adresse écran ni
    // la palette (Video_Reset, lui, remet videoBase à 0 ; customreset() n'appelle que
    // Video_Reset_Glue). `mono` vient du bit 7 du GPIP (moniteur branché).
    void resetGlue(bool mono) {
        sync = 0;                                   // $FF820A = 0 → 60 Hz
        mode = mono ? Mode::High : Mode::Low;       // ST_HIGH_RES / ST_LOW_RES
        // Video_Reset_Glue écrit IoMem[$FF8260] sans passer par le handler : la valeur
        // est la même côté Shifter, sans toucher au filtre Res_Shifter.
        shifterRes_ = static_cast<uint8_t>(mode);
    }

    // Auto-test DÉTERMINISTE de la machine Glue (chemin STF) : injecte des écritures
    // freq/res synthétiques à des cycles EXACTS et vérifie l'état d'affichage résultant
    // (DisplayStartCycle/EndCycle/BorderMask, nStartHBL/nEndHBL) contre les valeurs
    // documentées d'Hatari (Video_Update_Glue_State). Valide les retraits gauche/droite/
    // haut/bas sans dépendre du timing CPU (≠ test 68k cycle-exact). Renvoie true si OK ;
    // détaille les échecs sur stderr. Appelé par neost-headless --glue-selftest.
    bool glueSelfTest();

    // NEOST_LINELEN : l'UNIQUE lecteur du verrou « longueurs de ligne par-ligne »
    // côté Machine (défaut ON, =0 pour l'A/B). L'attribution expérimentale côté
    // Shifter vit sur SON verrou (NEOST_LINELEN_ATTR, ON depuis le 2026-09-01) — l'historique du défaut
    // divergent et le segfault qui ont motivé la séparation : définition (A16).
    static bool lineLenEnv();

    // Auto-test DÉTERMINISTE du re-rendu Spectrum 512 (palette intra-ligne). Sans
    // boot ni oracle : remplit une RAM vidéo synthétique (tous pixels = index 1),
    // injecte des écritures palette DATÉES au cycle sur l'index 1, force le rendu
    // spec512 (finishFrame) et vérifie OCTET-EXACT la couleur de chaque pixel contre
    // le modèle attendu (position de bascule = f(kSpec512AlignCyc, géométrie)). Toute
    // dérive de l'alignement palette↔pixel (la cause classique du « scramble spec512 »)
    // décale les frontières et fait échouer le test. Renvoie true si OK ; détaille les
    // échecs sur stderr. Appelé par neost-headless --spec512-selftest.
    bool spec512SelfTest();

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
    // Longueur par défaut de la ligne qui COMMENCE d'après la FRÉQUENCE live ($FF820A
    // bit 1 : 512 / 508), lue par Machine::onHbl à chaque HBL indépendamment du verrou de
    // trame (≙ Hatari Video_StartHBL pour la fréquence ; mux de rechargement du compteur
    // hsc du GST MCU). La RÉSOLUTION n'entre pas ici : l'événement HBL de NeoST tombe
    // quelques cycles après le début de la ligne, quand une démo plein écran est encore
    // dans son impulsion hi-res de retrait de bordure gauche (504→8) — lire 224 à cet
    // instant raccourcissait des lignes que le GLUE (décision au rechargement, ~56)
    // garde pleines (Cuddly : 11 trames d'avance). La résolution vient donc du VERROU de
    // trame (frameMode_ : trame mono = 224) ; les lignes 224 isolées restent décidées par
    // les Freq_match 71 Hz du Glue (re-pose du HBL) et setHblShorten.
    int liveLineLength() const {
        return ((static_cast<int>(frameMode_) & 3) == 2) ? 224 : (sync & 0x02) ? 512 : 508;
    }
    bool frameTimingChanged() const {
        const Geometry current = geometryFor(mode, sync);
        const Geometry latched = geometry();
        return current.cyclesPerLine != latched.cyclesPerLine
            || current.linesPerFrame != latched.linesPerFrame;
    }

    // Accès au buffer décodé (ARGB8888) pour le frontend ou un dump.
    const uint32_t* pixels() const { return frame_.data(); }
    int width()  const { return curW_; }      // largeur du buffer (overscan inclus)
    int height() const { return curH_; }      // hauteur du buffer (overscan inclus)
    // Nombre de lignes ACTIVES (display-enable) à décoder : 200 (couleur) / 400 (mono).
    // ≠ height() quand l'overscan ajoute des bordures haut/bas. La boucle de rendu de
    // Machine itère sur activeHeight() ; renderLine(y) place la ligne active y à
    // l'offset bordure-haut dans le buffer (cf. activeY_).
    int activeHeight() const { return curAH_; }
    // Largeur de l'écran ACTIF (sans les bordures) : 320 (low) / 640 (med/mono).
    // Public au même titre qu'activeHeight() : le cadrage du frontend s'en sert pour
    // garantir que le zoom ne rogne JAMAIS dans l'image (cf. main.cpp stContentRegion).
    int activeWidth() const { return curW_ - (bordered() ? (kBorderLeftPx + kBorderRightPx) : 0); }
    // Offset de l'écran actif dans le buffer (bordures overscan tout autour).
    int activeLeft() const { return activeX_; }
    int activeTop()  const { return activeY_; }
    // La trame COURANTE retire-t-elle une bordure (overscan démo : haut/bas via
    // glueVOverscan_, gauche/droite via bordersTrick_) ? Signal MATÉRIEL de la Glue,
    // STABLE — PAS une détection au pixel. Le zoom kiosk s'en sert : cadre FIXE sur la
    // zone active par défaut, élargi à la zone LIVE uniquement quand ceci est vrai.
    bool bordersOpen() const { return bordersTrick_ || glueVOverscan_ != 0; }
    // Zone affichée par la Glue LIVE en coordonnées buffer (kiosk overscan).
    // En mode standard : == activeTop()/activeHeight(). En overscan haut (EL, LX…) :
    // glueStartHBL_ < 63 → liveTop() < activeTop(), liveHeight() > activeHeight().
    // Les lignes basses vides (glueBlankLines_) sont incluses dans liveHeight().
    // 63 serait le début d'affichage 50 Hz seulement : en 60 Hz il vaut 34, et coder
    // la valeur PAL en dur faisait rendre 0 sur une trame 60 Hz à bordure retirée —
    // le cadrage kiosk en concluait alors qu'aucune bordure basse n'était ouverte.
    int liveTop() const {
        return std::max(0, activeY_ + (glueStartHBL_ - geometry().dispStartLine));
    }
    int liveHeight() const {
        const int lt = liveTop();
        return std::min(curH_ - lt, (glueEndHBL_ + glueBlankLines_) - glueStartHBL_);
    }
    // Snapshot de l'état Glue de la DERNIÈRE trame RENDUE (capturé en fin de
    // finishFrame, après replayGlue). Stable entre trames : les valeurs ne sont pas
    // écrasées par beginFrame_() du cycle suivant, contrairement à glueStartHBL_ /
    // glueEndHBL_ qui sont resettés à chaque début de trame. Le zoom kiosk DOIT
    // utiliser ces accesseurs (et non liveTop/liveHeight) pour éviter de lire l'état
    // de début de trame N+1 au lieu de fin de trame N.
    int snapLiveTop() const {
        return std::max(0, activeY_ + (snapGlueStart_ - geometry().dispStartLine));
    }
    int snapLiveHeight() const {
        const int lt = snapLiveTop();
        return std::min(curH_ - lt, (snapGlueEnd_ + snapGlueBlankLines_) - snapGlueStart_);
    }
    bool snapBordersOpen() const { return snapBordersTrick_ || snapGlueVOverscan_ != 0; }
    // Les DEUX MOITIÉS du signal ci-dessus, séparément. Le cadrage adaptatif en a
    // besoin : une bordure LATÉRALE retirée étend l'image en LARGEUR, une bordure
    // haute/basse en HAUTEUR — et les confondre élargit à 416 px une image qui n'en
    // occupe que 320 (cf. core/Framing.cpp, chantier Enchanted Land du 2026-09-02).
    bool snapSideBordersOpen() const { return snapSideTrick_; }
    // Nombre de lignes AFFICHÉES dont le DE déborde des cycles nominaux — la moitié
    // LATÉRALE du trick, en proportion et non en booléen (cf. Framing.cpp).
    int  snapSideTrickLines() const { return snapSideTrickLines_; }
    bool snapVertBordersOpen() const { return snapGlueVOverscan_ != 0; }

    // Fréquence verrouillée pour la trame. Une impulsion hi-res de bordure
    // ne transforme pas l'affichage couleur en écran mono dans le frontend.
    int refreshHz() const {
        if (frameMode_ == Mode::High) return 71;
        return (frameSync_ & 0x02) ? 50 : 60;
    }

    // Interface MMIO ($FF8200-$FF8260) appelée par le Bus.
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Wakeup state STF — TRANCHÉ WS3 (défaut oracle Hatari ; NEOST_WS=1..4 pour
    // A/B). Détermine les positions horizontales de la Glue (+0/+3/+1/+2), la
    // position de l'IRQ HBL (cpl−4 en WS1, cpl sinon) et la VBL STF (60/64).
    // Machine s'en sert pour HBL/VBL ; le détail vit dans glue:: (Shifter.cpp).
    static int wakestate();

    // V2 — post-traitement d'une écriture RES ($FF8260) APRÈS updateGlueState :
    // détections spécifiques aux bascules de résolution (Video_WriteToGlueRes,
    // video.c:1637-1753) — overscan MED-RES (No Cooper/PYM), retrait gauche med
    // (LEFT_OFF_MED), variante courte STE, stab med et scrolls « hardware »
    // 13/9/5/1 px. `prevRes`/`newRes` = valeur $FF8260&3 avant/après l'écriture.
    void updateGlueRes(int line, int lineCycles, int prevRes, int newRes);

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

    // Position du tic Timer B pour UNE ligne donnée, DE réel de la ligne compris —
    // port de `Video_TimerB_GetPosFromDE` appliqué à `ShifterLines[n]` : sur une
    // trame à tricks, le DE de la ligne vient de la machine Glue (retrait droit →
    // DE_end 462 → tic à ~488, etc.). Hatari REPROGRAMME l'interruption TB à chaque
    // écriture qui change le DE (video.c:2880-2891) ; côté NeoST, Machine::onTimerB
    // re-vérifie cette position au callback et se replanifie si elle a reculé.
    // Chantier Closure (docs/CLOSURE_CHANTIER.md) : la démo CHRONOMÈTRE les tics TB
    // de lignes sondées une à une — position globale seule = mesure hors barème.
    // Hors trame à écritures freq/res : le défaut global historique (zéro régression).
    int timerBPosForLine(int line, bool startOfLine);   // implémentation : Shifter.cpp
    int64_t timerBFrameCycleForLine(int line, bool startOfLine);   // grille RÉELLE ; -1 = indispo

    // Horloge faisceau : renvoie le nombre de cycles écoulés DANS la trame courante
    // (0 au début de trame). Posée par Machine ; sert à reconstruire le compteur
    // d'adresse vidéo $FF8205/07/09 (position courante du balayage). Cf. Hatari
    // Video_ScreenCounter_ReadByte / Video_CalculateAddress.
    void setBeamClock(std::function<int64_t()> fn) { beamClock_ = std::move(fn); }
    // V2 res-switch : signalé à CHAQUE écriture hi-res $FF8260=2 ; la Machine décide
    // si l'impulsion est PRÉCOCE et raccourcit la ligne (cf. Machine setHblShorten).
    void setHblShorten(std::function<void()> fn) { hblShorten_ = std::move(fn); }
    // Canal HBL_Pos/nCyclesPerLine (port Video_Update_Glue_State → Video_AddInterruptHBL,
    // video.c 2849-2877) : appelé par liveGlueCatchUp après CHAQUE écriture freq/res dont
    // la branche « Freq_match » fixe la géométrie de la ligne courante — (ligne, position
    // de l'IRQ HBL dans la ligne, longueur de la ligne en cycles : 224/508/512). La
    // Machine reprogramme l'événement HBL de la ligne et cumule le raccourcissement
    // (lineCarry_) pour décaler les lignes suivantes. Gated NEOST_LINELEN côté Machine.
    void setLineGeom(std::function<void(int, int, int)> fn) { lineGeom_ = std::move(fn); }

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
    // Dernière valeur BRUTE écrite dans $FF8264 (STE) : Hatari n'intercepte PAS la
    // relecture de ce registre (ioMemTabSTE → Video_HorScroll_Read_8264, video.c:5813
    // — IoMem conserve l'octet écrit, y compris les bits hauts non masqués).
    uint8_t hwScrollReg8264_ = 0;

private:
    // Latch STE 336 px : $8265>0 puis $8264=0, même trame et au plus 40 cycles.
    // Le timestamp ne représente que la dernière écriture POSITIVE à $8265 ;
    // −1 signifie aucune écriture positive valable dans la trame courante.
    bool steBorderFlag_ = false;
    bool newSteBorderFlag_ = false;
    int32_t lastScroll8265Cycle_ = -1;
    // Signal VBlank reconstruit depuis la même histoire freq/res, en live et
    // au replay : aucun état supplémentaire dans les save-states.
    bool vblankAtLine(int scanline) const;
    static uint32_t stColorToArgb(uint16_t c);   // $0RGB → ARGB8888
    // Géométrie du tampon imposée par une résolution verrouillée (seule source de
    // vérité, partagée par resizeFor et la validation du save-state).
    struct FrameGeometry { int w, h, ah, x, y; };
    static FrameGeometry geometryFor(Mode m);
    void resizeFor(Mode m);                       // ajuste le buffer si la rés. change
    // A32 : NON const. Elle l'était, et se rattrapait avec quatre `const_cast`
    // pour appeler liveGlueCatchUp — une méthode qui AVANCE la machine Glue.
    // Un « const » qui ment coûte plus cher qu'il ne rapporte : le seul appelant
    // est Shifter::read8, qui n'est pas const non plus.
    uint32_t videoCounter();                      // adresse vidéo courante ($FF8205/07/09)

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
    // RESTART du compteur en fin de trame (port Video_RestartVideoCounter : ligne
    // 310/260, cycle 56 [+4 STE], AVANT le VBL) : base RE-LATCHÉE depuis $FF8201/03
    // à cet instant. −1 = pas (encore) redémarré cette trame. Les lectures
    // $FF8205/07/09 des lignes ≥ ligne de restart renvoient CETTE base (compteur
    // rechargé, figé jusqu'au DE de la trame suivante) ; beginFrame la reprend comme
    // vcFrameBase_. C'est CE relatch (pas le début de trame) que sondent les moteurs
    // double-buffer beam-syncés (Enchanted Land en jeu : le stabilisateur lit $8209
    // et doit voir la base posée par le handler VBL, écrite APRÈS la ligne 0 NeoST).
    int64_t  vcRestartBase_      = -1;
    int      vcRestartLine_      = 0;   // ligne absolue du restart effectué
    // Fin de ligne active (≙ fin de Video_CopyScreenLine) : avance vcLineBase_ du
    // stride (+2 si scroll fin = prefetch d'un mot), applique l'offset compteur
    // différé puis les valeurs HSCROLL/LINEWIDTH en attente. Appelée par renderLine.
    void endVideoLine();
    // --- Capture PAR LIGNE des octets lus par le shifter (esprit Video_CopyScreenLine
    // d'Hatari : la RAM vidéo est échantillonnée AU FIL DU FAISCEAU, ligne par ligne,
    // jamais en fin de trame). Indispensable aux moteurs à UN buffer qui dessinent
    // puis effacent un sprite EN COURSE avec le faisceau (robot du menu Cuddly) :
    // en fin de trame le sprite n'est déjà plus en RAM, seul l'échantillon daté le
    // voit. Remplie par endVideoLine (commit de la ligne) ; consommée par
    // renderGlueFrame à la place d'une relecture RAM. Indexée par SCANLINE absolue ;
    // len 0 = pas de capture (repli : relecture RAM, comportement historique).
    // Largeur RÉELLE du compteur/de la base vidéo : 22 bits (A1..A21, 4 Mo) sur
    // TOUTES les machines émulées — ST, Mega ST, STE, Mega STE plafonnent à 4 Mo et
    // leur MMU n'a pas de lignes d'adresse au-delà ($FF8201/$FF8205 n'ont que 6 bits
    // utiles). Hatari : Video_GetAddrMask() = DMA_MaskAddressHigh()<<16 | $FFFF =
    // $3FFFFF pour ST/STE ≤ 4 Mo (m68000.c:958 « same as real HW ») ; les masques
    // $7F/$FF n'existent que pour la RAM « fictive » > 4 Mo ou TT/Falcon (24 bits),
    // non émulés ici. Le compteur se REPLIE donc modulo 4 Mo quand il avance, et
    // chaque mot fetché par le DMA vidéo est adressé modulo 4 Mo (video_memcpy_mmu,
    // video.c:3797) — jamais la MMIO $FFxxxx (lectures à effets de bord).
    static constexpr uint32_t kVideoAddrMask = 0x3FFFFFu;
    static constexpr int kLineSnapBytes = 256;   // ≥ kSnapLead + 230 (LEFT+RIGHT_OFF) + marge décodage
    // Marge de TÊTE de chaque slot de capture : les octets [base−kSnapLead, base)
    // précèdent la base de ligne — nécessaires quand l'offset source d'une ligne
    // est NÉGATIF (scroll hard 1 px / stab med, cf. renderGlueFrame : srcOff −2).
    // Sans elle, le repli RAM fin-de-trame ré-introduisait l'artefact « dessin en
    // course avec le faisceau » (logo d'intro de Closure : effacé en fin de trame
    // → écran noir).
    static constexpr int kSnapLead = 8;
    std::vector<uint8_t>  lineSnap_;             // [scanline * kLineSnapBytes]
    std::vector<uint16_t> lineSnapLen_;          // octets valides par scanline
    std::vector<uint8_t>  lineScrollSnap_;       // scroll fin STE par scanline committée
                                                 // (bits 0-3 : compteur ; bit 4 : prefetch $FF8265)
    // Octets lus par le shifter sur une scanline (160 nominal, modulé par les
    // drapeaux de bordure glue — port BORDERBYTES_*). Hors line-offset/scroll STE.
    int  glueLineBytes(int scanline) const;
    // Ancre verticale du commit de scanlines (cf. Shifter.cpp) : max(liveStartHBL_,
    // glueStartHBL_) sur trame à tricks, latchée au 1er commit (commitAnchor_).
    int  commitAnchor();
    int  commitAnchor_ = -1;

public:
    // La scanline est-elle AFFICHÉE (Display-Enable vertical) d'après la machine
    // Glue LIVE ? Fenêtre nominale [VDE_On, VDE_Off) sans écriture freq/res ; avec
    // tricks, glueStartHBL_/glueEndHBL_ + NO_DE par ligne (avance liveGlueCatchUp).
    // Pilote les tics Timer B event-count (Machine::onTimerB), comme Hatari
    // Video_AddInterruptTimerB recalculé par ligne.
    bool liveLineDisplayed(int line);

    // RESTART du compteur vidéo en fin de trame (port Video_RestartVideoCounter,
    // cf. vcRestartBase_). Appelé par Machine à la ligne 310 (50 Hz) / 260 (60 Hz),
    // cycle 56 (STF) / 60 (STE), si la fréquence du registre sync correspond
    // (check LIVE, comme Hatari qui relit $FF820A à cet instant).
    void restartVideoCounter(int line);

private:
    // Position du faisceau : ligne absolue + cycle dans la ligne. false si pas d'horloge.
    bool beamPos(int& line, int& lineCyc) const;
    // Écriture du compteur vidéo $FF8205/07/09 (STE) — port Video_ScreenCounter_WriteByte.
    void writeVideoCounterByte(uint32_t addr, uint8_t v);

    // Décode les index de palette (ou bit mono) d'une ligne dans `idx` selon la
    // résolution VERROUILLÉE (lecture planaire + scroll fin STE). Partagé par
    // renderLine (palette figée) et finishFrame (palette intra-ligne spec512).
    // `idx` doit pouvoir tenir W + scroll pixels (≤ 656). Renvoie le décalage scroll.
    int decodeLineIndices(int y, uint8_t* idx) const;

    // Avance compteur SUPPLÉMENTAIRE par ligne due au scroll fin (port des
    // `pVideoRaster += n*2` de Video_CopyScreenLine*) : prefetch ($FF8265) =
    // 1 mot PAR PLAN (+8 basse rés, +4 moyenne) ; $FF8264 = 0 ; mono = +2.
    // Le latch 336 px ajoute 8 octets, selon les priorités des bordures Glue.
    int scrollCounterAdvance(int scanline = -1) const;
    static int steBorderBytes(uint32_t borderMask);

    // Enregistre une écriture palette (registre `index`) avec son cycle live dans
    // la trame, pour le re-rendu spec512. Met à jour le compteur de détection.
    void recordColorWrite(int index);

    // Enregistre une écriture sync $FF820A (50/60 Hz) ou résolution $FF8260 au cycle
    // live, pour la détection de RETRAIT de bordures (port machine Glue Hatari,
    // Video_Update_Glue_State). `isRes` = $FF8260, sinon $FF820A.
    void recordSyncWrite(bool isRes, uint8_t val);
    int64_t datedResWriteCycle() const;           // datation commune des écritures freq/res (−1 hors trame)
    // Dernier Freq (bit1 de $FF820A) / dernière Res ($FF8260 & 3) VUS par la Glue —
    // port de ShifterFrame.Freq / ShifterFrame.Res : une réécriture de la MÊME
    // valeur est IGNORÉE par la machine Glue (Video_Sync_WriteByte video.c:3056,
    // Video_WriteToGlueRes video.c:1630 — `if (Freq == ShifterFrame.Freq) return;`).
    // Persistants À TRAVERS les trames ; remis à −1 SEULEMENT au reset (Video_Reset
    // video.c:824-825), jamais à beginFrame. −1 = aucune écriture vue.
    int lastGlueFreq_ = -1;
    int lastGlueRes_  = -1;

    // --- Résolution VUE PAR LE SHIFTER ($FF8260 ET son alias $FF8261) ----------
    // Le registre de résolution existe en DOUBLE : dans la GLUE (timings, DE,
    // bordures) et dans le SHIFTER (décodage des mots livrés par le MMU). $FF8260
    // écrit les deux ; $FF8261 n'écrit QUE le Shifter (ioMemTabST.c:71,
    // ioMemTabSTE.c:120 — « Resolution in Shifter only », mêmes sur STF et STE).
    // Le Shifter n'a ni UDS ni LDS : il lit TOUJOURS les bits 8-9 du bus de données
    // (Hatari Video_Res_WriteByte, video.c:5700-5770). D'où :
    //   • move.b #x,$FF8261 → le 68000 duplique l'octet sur D0-D15 → Shifter = x ;
    //   • move.w #xyab,$FF8260 → Shifter = Glue = xy (l'octet ab, adressé à $8261,
    //     est ignoré par le Shifter) ;
    //   • move.b #x,$FF8260 → les deux reçoivent x.
    // Valeur 3 = Shifter ARRÊTÉ (n'absorbe plus les mots du MMU) ; côté GLUE, une
    // valeur 3 se lit comme « haute rés » (bit 1, déjà géré par la machine Glue).
    uint8_t shifterRes_      = 0;    // ≙ registre res du Shifter (0..3)
    int     lastShifterRes_  = -1;   // ≙ ShifterFrame.Res_Shifter (−1 = aucun, filtre « même valeur »)
    int     shifterResCyc_   = -1;   // ≙ ResPosStop_Shifter.LineCycles (cycle-ligne du dernier changement)
    int     shifterResLine_  = -1;   // ≙ ResPosStop_Shifter.HBL (ligne RÉELLE du dernier changement, −1 = trame précédente)
    // Intervalles « Shifter arrêté » (res 3) en cycles TRAME, pour noircir une ligne
    // d'après sa fenêtre DE et non d'après l'état au HBL (cf. lineStoppedOver) :
    // l'arrêt en cours (−1 = Shifter en marche) et le dernier arrêt terminé.
    int64_t stopFromFc_      = -1;
    int64_t lastStopFromFc_  = -1;
    int64_t lastStopToFc_    = -1;
    // Scroll hard STF « 4 px » par Shifter arrêté (Troed/Sync, port video.c:1906-1973) :
    // res=3 pendant 8..32 cycles alors que le DE est actif → toutes les lignes
    // SUIVANTES de la trame sont décalées de 0/−4/−8/−12 px et le compteur vidéo
    // reçoit −0/−2/−4/−6 octets à la fin de la ligne courante (VideoRasterDelayedInc).
    int     vcStopInc_       = 0;    // ≙ VideoRasterDelayedInc (appliqué par endVideoLine)
    int     shifterStopLine_ = -1;   // ligne du stop (les lignes > celle-ci sont décalées), −1 = aucun
    int     shifterStopShift_ = 0;   // décalage px (≙ DisplayPixelShift) des lignes suivantes
    // Écriture de la résolution côté Shifter (port Video_WriteToShifterRes).
    void writeShifterRes(uint8_t res);
    // Position (ligne, cycle-ligne) d'un cycle trame sur la grille RÉELLE des débuts
    // de ligne (≙ Video_ConvertPosition d'Hatari) — repli fc/cpl hors NEOST_LINELEN_ATTR.
    void realLinePos(int64_t fc, int& line, int& lineCyc);
    // Le Shifter est-il resté arrêté sur TOUTE la fenêtre DE de la scanline sl ?
    bool lineStoppedOver(int sl) const;

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
    // Moitié LATÉRALE du signal ci-dessus (≥1 ligne affichée dont le DE déborde des
    // cycles nominaux) — calculée séparément par replayGlue. NON SÉRIALISÉE À DESSEIN :
    // entièrement re-dérivée à chaque finishFrame, elle ne pilote que le CADRAGE de
    // l'interface. La sérialiser imposerait un bump du format de save-state pour un
    // champ cosmétique dont la valeur est reconstruite à la trame suivante.
    bool   sideTrick_ = false;
    int    sideTrickLines_ = 0;                     // NOMBRE de lignes élargies (cf. sideTrick_)

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
    // Snapshot Glue de la DERNIÈRE trame RENDUE — capturé à la fin de finishFrame()
    // après replayGlue(). Non écrasé par beginFrame_() : stable entre trames.
    int      snapGlueStart_      = 63;
    int      snapGlueEnd_        = 263;
    uint32_t snapGlueVOverscan_  = 0;
    int      snapGlueBlankLines_ = 0;
    bool     snapBordersTrick_   = false;
    bool     snapSideTrick_      = false;   // cf. sideTrick_ (non sérialisé)
    int      snapSideTrickLines_ = 0;
    // Sortie du canal HBL_Pos/nCyclesPerLine de updateGlueState (−1 = pas de match
    // freq sur cette écriture ; sinon position IRQ HBL et longueur de ligne).
    int  glueHblPos_     = -1;
    int  glueCyclesLine_ = -1;
    // LINELEN : grille réelle du chemin live — début (cycle-trame) de chaque ligne
    // atteinte, et longueur courante de la ligne en cours (déplacée par les matches).
    std::vector<int64_t> glueLineStart_;
    int  liveGlueLen_ = 512;
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
    // explicite et base fournie (pas de stride interne). Applique le scroll fin STE
    // (même modèle prefetch/sans-prefetch que decodeLineIndices) et renvoie le
    // décalage scroll (l'appelant lit idx[s + scroll]). `scroll`/`prefetch` sont
    // fournis PAR LIGNE par l'appelant (valeur snappée au commit, cf.
    // lineScrollSnap_) — les membres vivants portent la valeur de fin de trame,
    // fausse pour un split $FF8264/65 à mi-trame.
    int decodeWindowIndices(uint32_t base, int nPix, uint8_t* idx, bool medLine,
                            int scroll, bool prefetch) const;
    // Variante depuis une CAPTURE de ligne (octets déjà échantillonnés au faisceau,
    // cf. lineSnap_) : même décodage planaire, source tampon au lieu du bus.
    int decodeWindowIndicesFromBytes(const uint8_t* src, int srcLen, int nPix, uint8_t* idx, bool medLine,
                                     int scroll, bool prefetch) const;

    // --- Spec512 : palette intra-ligne (port Hatari spec512.c) --------------
    // Une écriture palette dans la trame, datée au cycle (façon CyclePalettes[]).
    struct ColorWrite { int32_t frameCycle; uint16_t colour; uint8_t index; uint32_t pc; };
    std::vector<ColorWrite>  colorWrites_;          // écritures palette de la trame (ordre d'exécution)
    std::array<uint16_t, 16> frameStartPalette_{};  // palette au début de trame (base du replay)
    uint16_t leftBorderPal0_ = 0;                   // registre 0 de la ligne PRÉCÉDENTE (couleur du
                                                    // bord GAUCHE : sorti cyc ~0-56 AVANT l'écriture
                                                    // palette du handler HBL → latché en fin de ligne
                                                    // précédente ; le bord DROIT sort après = courant)
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

    Bus&          bus_;
    int           curW_ = 0, curH_ = 0;     // dimensions du buffer (overscan inclus)
    int           curAH_ = 0;               // lignes actives à décoder (200/400)
    int           activeX_ = 0, activeY_ = 0;  // offset de l'écran actif dans le buffer
    Mode          frameMode_ = Mode::Low;   // résolution verrouillée pour la trame
    uint8_t       frameSync_ = 0x02;        // fréquence ($FF820A) verrouillée pour la trame
    std::vector<uint32_t> frame_;           // curW_*curH_ pixels ARGB
    std::function<int64_t()> beamClock_;    // cycles dans la trame (cf. setBeamClock)
    std::function<void()>    hblShorten_;   // V2 : signal d'impulsion hi-res (cf. setHblShorten)
    std::function<void(int, int, int)> lineGeom_;   // (ligne, hblPos, cyclesLine) — cf. setLineGeom

public:
    // Sérialisation save-state SYMÉTRIQUE (save = append, load = lecture). Transfère
    // TOUS les membres d'état runtime du Shifter : registres vidéo ($FF82xx), palette,
    // géométrie/mode verrouillés, compteur d'adresse matérialisé + écritures STE
    // différées, captures par-ligne, état machine Glue (retrait de bordures), buffers
    // de détection spec512, latches de bordure et le framebuffer ARGB décodé.
    // SKIP : bus_ (référence), les std::function (branchées par Machine à l'init),
    // les membres static/constexpr. Inline dans l'en-tête pour l'accès aux privés.
    void serialize(StateArchive& ar) {
        // --- Registres vidéo publics ($FF8201/03/0D/09/0B, $FF8260/820A, palette) ---
        ar(videoBase);
        ar(palette);
        ar(mode);
        // $FF8260 accepte les 2 bits : la valeur 3 (« stop the shifter », trick
        // Troed/Sync) est un état légalement atteignable par MMIO — un .state
        // pris dans cet état doit rester rechargeable. Seules les valeurs > 3
        // (fichier forgé) sont rejetées.
        ar.check(static_cast<uint8_t>(mode) <= 3);
        ar(sync);
        // Registres STE ($FF8264/65, $FF820F)
        ar(hwScrollCount);
        // Le scroll fin STE est un compteur 4 bits : le chemin MMIO le masque
        // toujours `& 0x0F` ($FF8264/65, Shifter.cpp). Il sert d'OFFSET de lecture
        // `idx[c + hwScrollCount]` dans renderLine (tampon de pile de 660 o) : un
        // .state forgé (CRC valide, cf. campagne de durcissement) posant
        // hwScrollCount=255 provoquerait une lecture hors pile de ~234 o (fuite
        // d'info / crash). On revalide donc l'invariant matériel au chargement.
        ar.check(hwScrollCount < 16);
        ar(hwScrollPrefetch);
        ar(lineWidth);
        ar(hwScrollReg8264_);
        ar(steBorderFlag_);
        ar(newSteBorderFlag_);
        ar(lastScroll8265Cycle_);
        ar.check(lastScroll8265Cycle_ >= -1 && lastScroll8265Cycle_ <= 1048576,
                 "Shifter::lastScroll8265Cycle_ hors bornes");

        // --- Compteur d'adresse vidéo matérialisé + écritures STE différées ---
        ar(vcFrameBase_);
        ar(vcLineBase_);
        ar(vcLineY_);
        ar(newHwScrollCount_);
        // Écriture STE différée du même compteur 4 bits : sentinelle -1 (« aucune »)
        // sinon 0..15. Appliqué à hwScrollCount en fin de ligne (Shifter.cpp) → même
        // vecteur de lecture hors-tampon que ci-dessus ; on borne pareil.
        ar.check(newHwScrollCount_ >= -1 && newHwScrollCount_ < 16);
        ar(newHwScrollPrefetch_);
        ar(newLineWidth_);
        ar(vcDelayedOffset_);
        ar(vcRestartBase_);
        ar(vcRestartLine_);
        // Résolution côté Shifter + scroll hard « Shifter arrêté » (format v50).
        ar(shifterRes_);
        ar.check(shifterRes_ <= 3, "Shifter::shifterRes_ > 3");
        ar(lastShifterRes_);
        ar.check(lastShifterRes_ >= -1 && lastShifterRes_ <= 3, "Shifter::lastShifterRes_ hors bornes");
        ar(shifterResCyc_);
        ar(shifterResLine_);                                  // format v51
        ar.check(shifterResLine_ >= -1 && shifterResLine_ <= 4096, "Shifter::shifterResLine_ hors bornes");
        ar(stopFromFc_);
        ar(lastStopFromFc_);
        ar(lastStopToFc_);
        ar(vcStopInc_);
        ar.check(vcStopInc_ >= -6 && vcStopInc_ <= 0, "Shifter::vcStopInc_ hors bornes");
        ar(shifterStopLine_);
        ar.check(shifterStopLine_ >= -1 && shifterStopLine_ <= 4096, "Shifter::shifterStopLine_ hors bornes");
        ar(shifterStopShift_);
        ar.check(shifterStopShift_ >= -12 && shifterStopShift_ <= 0, "Shifter::shifterStopShift_ hors bornes");

        // --- Captures par-ligne (échantillon faisceau) ---
        ar.vec(lineSnap_);          // std::vector<uint8_t>
        ar.podVec(lineSnapLen_);    // std::vector<uint16_t>
        ar.vec(lineScrollSnap_);    // bits 0-3 = scroll, 4 = prefetch, 5 = 336 px, 6 = valide, 7 = Shifter arrêté
        // Invariant : les tableaux sont indexés ENSEMBLE (lineSnap_ par tranches
        // de kLineSnapBytes, borné par lineSnapLen_.size()) — un fichier forgé
        // désynchronisé ferait écrire endLine() hors du tas ; lineScrollSnap_ est
        // lu par renderGlueFrame sous la même borne : compteur 4 bits, bit 4 de
        // mode prefetch, bit 5 du latch 336 px et bit 6 de validité (format v20).
        ar.check(lineSnap_.size() == lineSnapLen_.size() * kLineSnapBytes);
        ar.check(lineScrollSnap_.size() == lineSnapLen_.size());
        // Bit 7 (format v50) = Shifter arrêté (res 3) au commit de la ligne : les
        // 8 bits sont donc tous légitimes, plus de borne à revalider.

        // --- Filtre Glue « même valeur ignorée » (persistant inter-trames) ---
        ar(lastGlueFreq_);
        ar(lastGlueRes_);
        ar(liveStartHBL_);
        ar(commitAnchor_);   // ancre verticale du commit (latchée mi-trame)

        // --- Machine Glue : retrait de bordures / overscan ---
        // SyncWrite/GlueLine/ColorWrite ont du PADDING interne → champ par champ
        // (objVec) : podVec sérialiserait des octets non initialisés (fichier non
        // byte-déterministe, cf. StateArchive).
        ar.objVec(syncWrites_, 6, [](StateArchive& a, SyncWrite& w) {
            a(w.frameCycle); a(w.val); a(w.isRes);
        });
        ar(bordersTrick_);
        ar.objVec(glueLines_, 10, [](StateArchive& a, GlueLine& g) {
            a(g.displayStartCycle); a(g.displayEndCycle);
            a(g.displayPixelShift); a(g.borderMask);
        });
        // commitAnchor_ nourrit « sl = commitAnchor_ + vcLineY_ » : borne LARGE
        // (convention cpl_/lpf_ ≤ 4096 de Machine) — un INT_MAX forgé rendait
        // l'addition UB, même si les indexeurs aval re-bornent sl. Pas de borne
        // sur glueLines_.size() : replayGlue() peut légitimement redimensionner
        // glueLines_ en cours de trame (lpf changé), l'ancre restant à l'ancienne
        // géométrie jusqu'au beginFrame suivant.
        ar.check(commitAnchor_ >= -1 && commitAnchor_ <= 4096,
                 "Shifter::commitAnchor_ hors [-1, 4096]");
        ar(glueStartHBL_);
        ar(glueEndHBL_);
        ar(glueVOverscan_);
        ar(glueBlankLines_);
        ar(glueHblPos_);
        ar(glueCyclesLine_);
        ar.podVec(glueLineStart_);  // std::vector<int64_t>
        // Invariant : glueLineStart_ est TOUJOURS assigné à glueLines_.size().
        // Tenu aux DEUX sites qui dimensionnent glueLines_ : beginFrame (assign) et
        // replayGlue (resize — A16b, 2026-08-28). replayGlue ne le tenait PAS : le
        // glue-selftest, qui l'appelle sans beginFrame, déréférençait un vecteur vide
        // (SIGSEGV), et une trame dont lpf change en cours de route lisait hors du tas
        // en silence. L'auto-test `glue_selftest_attr` (etalons.json) garde le cas.
        // liveGlueCatchUp indexe glueLineStart_[wl]/[liveGlueLine_] (VideoGlue.cpp:116,
        // 120,123,160) où wl ≤ liveGlueLine_ < glueLines_.size() — SANS garde de lecture
        // (seule l'écriture, ligne 169, en a une : le test de taille ligne 168 ; numéros
        // vérifiés le 2026-10-20). Un .state forgé posant un glueLineStart_
        // plus court → lecture hors-tas d'un int64_t. On revalide l'invariant au load.
        ar.check(glueLineStart_.size() == glueLines_.size(),
                 "Shifter::glueLineStart_ désynchronisé de glueLines_");
        ar(liveGlueLen_);
        ar(nScreenRefreshRate_);
        ar(liveGlueLine_);
        // liveGlueLine_ indexe glueLines_ via startHBL (curseur de rattrapage) :
        // −1 = trame pas encore commencée, sinon une ligne existante. Une valeur
        // négative forgée faisait écrire AVANT le tampon (cf. garde de startHBL).
        ar.check(liveGlueLine_ >= -1
                 && liveGlueLine_ < static_cast<int>(glueLines_.size()),
                 "Shifter::liveGlueLine_ hors de glueLines_");
        ar(liveGlueWi_);            // std::size_t
        ar(liveGlueRes_);
        ar(liveGlueFreq50_);

        // --- Spec512 : palette intra-ligne ---
        ar.objVec(colorWrites_, 11, [](StateArchive& a, ColorWrite& c) {
            a(c.frameCycle); a(c.colour); a(c.index); a(c.pc);
            // `index` désigne un des 16 registres palette, et le replay spec512
            // en fait un INDICE D'ÉCRITURE : « pal[c.index] = c.colour » sur un
            // std::array<uint16_t,16> de la pile (renderGlueFrame). Non borné, un
            // .state forgé écrivait jusqu'à 510 octets au-delà, à un offset ET
            // avec une valeur choisis. Le chemin live pose toujours 0..15
            // (recordColorWrite) — même garde que YM2149 sur RegEvent::reg.
            a.check(c.index < 16, "Shifter::colorWrites_[].index >= 16");
        });
        ar(frameStartPalette_);
        ar(leftBorderPal0_);
        ar(paletteAccesses_);
        ar(spec512Active_);

        // --- Géométrie / mode verrouillés pour la trame + framebuffer ---
        ar(curW_);
        ar(curH_);
        ar(curAH_);
        ar(activeX_);
        ar(activeY_);
        ar(frameMode_);
        ar(frameSync_);
        ar.podVec(frame_);          // std::vector<uint32_t> — framebuffer ARGB décodé
        // Invariant : le framebuffer est indexé par la géométrie VERROUILLÉE ci-dessus
        // (renderLine écrit à frame_ + (activeY_+y)*curW_ + activeX_). Un fichier forgé
        // qui grossit curW_/curH_ sans grossir frame_ ferait écrire hors du tas — et
        // resizeFor() ne rattrape rien, son court-circuit « même w/h » saute la
        // réallocation. On vérifie donc APRÈS relecture du vecteur.
        ar.check(static_cast<uint8_t>(frameMode_) <= 3);   // 3 possible, cf. `mode` plus haut
        // La géométrie n'est PAS libre : le chemin live la dérive toujours de frameMode_
        // (beginFrame → resizeFor). Des bornes lâches (≤ 1024×512) laissaient passer
        // curW_ = 1024 avec frame_ assorti ; Machine::frameInProgress_ restauré saute
        // le beginFrame qui l'aurait corrigée, et renderLine décodait alors 928 px
        // dans son tampon de pile idx[660] (débordement, SIGABRT). On exige donc
        // EXACTEMENT ce que resizeFor(frameMode_) produirait.
        {
            const FrameGeometry g = geometryFor(frameMode_);
            ar.check(curW_ == g.w && curH_ == g.h && curAH_ == g.ah
                     && activeX_ == g.x && activeY_ == g.y,
                     "Shifter geometry does not match the locked resolution");
        }
        ar.check(frame_.size() == static_cast<std::size_t>(curW_) * static_cast<std::size_t>(curH_));
        ar.check(curAH_ >= 0 && activeX_ >= 0 && activeY_ >= 0 &&
                 activeY_ + curAH_ <= curH_ && activeX_ + activeWidth() <= curW_);
        ar.check(!bordered() || !(steBorderFlag_ || newSteBorderFlag_) || activeX_ >= 16,
                 "Shifter::bordure gauche trop courte pour 336 px");
        // vcLineY_ est le curseur de ligne active, rattrapé par des boucles
        // « while (vcLineY_ < cible) endVideoLine(); » (videoCounter, endFrame).
        // Une valeur très négative forgée les rend praticables mais interminables
        // (~2³¹ tours à ~200 accès bus chacun) : gel à 100 % de CPU, SIGKILL seul
        // recours — même classe que les gels refermés sur frameStart_/lineCarry_.
        // C'est la borne BASSE qui ferme le gel ; l'indexation de lineSnap_ par
        // commitAnchor_ + vcLineY_ est déjà gardée sur place (endVideoLine).
        // ⚠ La borne haute n'est PAS curAH_ : bordure BASSE retirée, commitScanline
        // pousse volontairement vcLineY_ au-delà de curAH_ jusqu'à glueEndHBL_
        // (overscan_top.st le fait) — un plafond curAH_ refusait cet état LÉGITIME.
        // On plafonne donc au nombre de lignes de la trame, seule absurdité visée.
        ar.check(vcLineY_ >= 0
                 && vcLineY_ <= static_cast<int>(glueLines_.size()) + curAH_,
                 "Shifter::vcLineY_ hors bornes");
    }
};

