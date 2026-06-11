// =============================================================================
//  Shifter.cpp — Décodage planaire ST (basse/moyenne/haute) → buffer ARGB.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Shifter.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Décalage d'alignement pixel↔couleur du re-rendu spec512, en cycles (8 MHz).
// Port de l'alignement d'Hatari (spec512.c Spec512_StartScanLine) : avant de tracer
// le 1ᵉʳ pixel affiché, Hatari fait avancer ScanLineCycleCount de (LineStartCycle/4 + 7)
// périodes de 4 cycles, soit LineStartCycle + 28 — le « +7 » étant le décalage
// pipeline du shifter documenté (« [NP] '7' is required to align pixels and colors »).
// Une écriture à la position-ligne L apparaît donc au pixel (L − LineStartCycle − 28).
// Côté NeoST, Moira date l'écriture au DÉBUT du cycle bus (~4 cyc avant la convention
// Hatari instr_end−8), d'où un net de −28 + 4 = −24, AFFINÉ à −23 (1 cyc) une fois le
// flicker spec512 corrigé (cf. kVideoCounterReadOffsetCyc) : la correction du compteur
// vidéo a VERROUILLÉ l'état des écritures (qui oscillait ±4 cyc une trame sur deux), si
// bien que l'alignement rendu optimal s'est figé à −23. Indissociable de
// applyShifterBusAlignment (sans le recalage des wait states bus, la position dérive de
// −2 cyc/ligne). VALIDÉ : diff pixel vs oracle Hatari = **0 px** sur LES 4 images du
// slideshow Spectrum 512 — BEE512 (honeycomb), sun (dégradé), PLANET (sci-fi), cougar
// (photo) — soit 100 % pixel-identique. À −24 il restait 122/54/210/319 px (frontières
// décalées d'1 px sur les images à arêtes nettes). Sweep en V confirmant −23 sur chaque.
static constexpr int kSpec512AlignCyc = -23;

// Seuil de détection « image spec512 » : nombre d'écritures palette MOT par trame
// au-delà duquel on bascule sur le re-rendu intra-ligne. Bien au-dessus d'un usage
// normal (16 couleurs posées une fois = 16, ou raster-bars ~ quelques centaines) pour
// ne JAMAIS toucher les trames ordinaires. Une image Spectrum 512 réécrit ~48 couleurs/
// ligne sur 200 lignes (~9600 mots/trame). Depuis la fusion octet→mot de recordColorWrite
// (un move.w = 1 écriture, comme Hatari), on compte les MOTS : 512 ⇔ l'ancien seuil de
// 1024 (qui comptait 2 octets par mot), frontière de détection inchangée.
static constexpr int kSpec512Threshold = 512;

// Correction de datation de la LECTURE du compteur vidéo $FF8205/07/09, en cycles.
// Pendant côté lecture de kSpec512AlignCyc (qui aligne les ÉCRITURES). Port du modèle
// Hatari Video_CalculateAddress : Hatari date la lecture PLUS TÔT que le cycle de bus
// brut — FrameCycles = Video_GetCyclesSinceVbl_OnReadAccess() − 8 (le « magic 8 »),
// plus l'offset « read effective N cyc avant la fin de l'instruction » (cycles.c,
// Cycles_GetInternalCycleOnReadAccess). NeoST échantillonne au cycle de lecture brut de
// Moira (liveNow) → 2 cyc trop tard. Sans correction, la valeur tombe PILE sur la
// frontière de cellule-mot de la quantification (X−lineStart)>>1 &~1 (granularité 4 cyc) :
// les démos spec512 à auto-synchro (BEE512…) qui lisent $FF8209 puis sautent dans un
// nop-slide calculé atterrissent ±4 cyc une trame sur deux → image STATIQUE qui clignote
// à 25 Hz (~1418 px/trame). −2 cyc recentre la lecture dans la cellule. Valeur EXACTE
// (pas un simple ≡2 mod 4 anti-flicker) calée sur l'oracle Hatari (TRACE_VIDEO_COLOR) :
// 1ʳᵉ écriture palette ligne 64 datée cyc=80 stable côté Hatari ; NeoST sans correction
// oscille 76↔80, avec −2 se verrouille sur 80 (= Hatari). Flicker plein-diaporama
// (BEE512/sun/PLANET/ANIMAL, fenêtre 540..1010) : 111 paires → 0.
static constexpr int kVideoCounterReadOffsetCyc = -2;

// =============================================================================
//  Machine GLUE — retrait de bordures (port fidèle de Hatari video.c :
//  Video_Update_Glue_State + Video_StartHBL + section verticale Video_EndHBL).
//  Rejouée HORS-LIGNE en fin de trame sur les écritures freq/res datées
//  (syncWrites_) → la timeline live est INCHANGÉE (zéro régression).
// =============================================================================
namespace glue {
// BORDERMASK_* (Hatari video.c)
constexpr uint32_t LEFT_OFF        = 0x0001;   // retrait bordure gauche (hi/lo) → +26 o
constexpr uint32_t LEFT_PLUS_2     = 0x0002;   // ligne 60 Hz commence 2 o plus tôt
constexpr uint32_t STOP_MIDDLE     = 0x0004;   // fin en hi-res au cycle 160 → -106 o
constexpr uint32_t RIGHT_MINUS_2   = 0x0008;   // ligne 60 Hz finit 2 o plus tôt
constexpr uint32_t RIGHT_OFF       = 0x0010;   // retrait bordure droite → +44 o
constexpr uint32_t RIGHT_OFF_FULL  = 0x0020;   // retrait droite + gauche ligne suivante
constexpr uint32_t NO_DE           = 0x0800;   // vertical DE off pour cette ligne
constexpr uint32_t BLANK           = 0x1000;   // ligne blanche (50/60 Hz)
constexpr uint32_t NO_COUNT        = 0x2000;   // compteur ligne non incrémenté
constexpr uint32_t NO_SYNC         = 0x4000;   // pas de HSYNC (ligne vide)
constexpr uint32_t SYNC_HIGH       = 0x8000;
// V_OVERSCAN_* (Hatari includes/video.h)
constexpr uint32_t VO_NO_TOP       = 0x01;
constexpr uint32_t VO_NO_BOTTOM_50 = 0x02;
constexpr uint32_t VO_NO_BOTTOM_60 = 0x04;
constexpr uint32_t VO_BOTTOM_SHORT_50 = 0x08;
constexpr uint32_t VO_NO_DE        = 0x10;
// Timing STF nominal (ancres 56/376 cohérentes avec la géométrie/videoCounter
// validés de NeoST ; la variante "wakeup state" WS3 +1 cyc relève du sous-pixel,
// cf. TODO wait states). Cf. Hatari VideoTimings[VIDEO_TIMING_STF_WS1].
constexpr int HDE_On_Hi          =   4;
constexpr int HBlank_Off_Low_60  =  24;
constexpr int HBlank_Off_Low_50  =  28;
constexpr int HDE_On_Low_60      =  52;
constexpr int Line_Set_Pal       =  54;
constexpr int HDE_On_Low_50      =  56;
constexpr int HDE_Off_Hi         = 164;
constexpr int HDE_Off_Low_60     = 372;
constexpr int HDE_Off_Low_50     = 376;
constexpr int HSync_On_Off_Low   = -50;        // HSync_On_Offset_Low (DE_end droite = cpl-50)
constexpr int HSync_Off_Off_Low  = -10;        // HSync_Off_Offset_Low
constexpr int RemoveTopBorder_Pos    = 502;
constexpr int RemoveBottomBorder_Pos = 502;
constexpr int LINE_END_FULL      = 512;
// Positions verticales (lignes) : VDE_On/Off par fréquence.
constexpr int VDE_On_50 = 63, VDE_On_60 = 34, VDE_On_Hi = 34;
constexpr int VDE_Off_50 = 263, VDE_Off_60 = 234, VDE_Off_Hi = 434;
constexpr int VDE_Off_NoBottom_50 = 310;   // 263 + 47 (VIDEO_HEIGHT_BOTTOM_50HZ)
constexpr int VDE_Off_NoBottom_60 = 260;   // 234 + 26 (VIDEO_HEIGHT_BOTTOM_60HZ)
}  // namespace glue

Shifter::Shifter(Bus& bus) : bus_(bus) {
    resizeFor(mode);
}

uint32_t Shifter::stColorToArgb(uint16_t c) {
    // Format STE : %0000 RRRR GGGG BBBB (4 bits par canal). Le ST n'utilise que
    // les 3 bits bas de chaque nibble ; le STE ajoute un 4e bit (bit3) qui pèse
    // une DEMI-marche, d'où des teintes intermédiaires. Port fidèle de Hatari
    // conv_st.c (ConvST_SetupRGBTable) : v8 = ((c4&0x7)<<1)|((c4&0x8)>>3) puis
    // v8 |= v8<<4 (réplique le nibble pour étirer 4→8 bits). Pour un coloris ST
    // (bit3=0) cela donne la même nuance qu'avant (ex. 7 → 0xEE).
    const uint8_t r4 = (c >> 8) & 0xF;
    const uint8_t g4 = (c >> 4) & 0xF;
    const uint8_t b4 = (c >> 0) & 0xF;
    auto ex = [](uint8_t c4) -> uint32_t {
        uint32_t v = ((c4 & 0x7u) << 1) | ((c4 & 0x8u) >> 3);
        v |= v << 4;
        return v;
    };
    return 0xFF000000u | (ex(r4) << 16) | (ex(g4) << 8) | ex(b4);
}

void Shifter::resizeFor(Mode m) {
    // Lignes ACTIVES (display-enable) décodées et offset de l'écran actif dans le
    // buffer. En basse rés bordée, le buffer overscan ajoute des bordures autour
    // de l'écran 320×200 (cf. kBorder*). Sinon le buffer = l'écran actif (offset 0).
    int aw = 320, ah = 200;                       // dimensions de l'écran ACTIF
    switch (m) {
        case Mode::Low:    aw = 320; ah = 200; break;   // 16 couleurs
        case Mode::Medium: aw = 640; ah = 200; break;   // 4 couleurs
        case Mode::High:   aw = 640; ah = 400; break;   // monochrome
    }
    const bool border = (m == Mode::Low) && kBordersEnabled;
    activeX_ = border ? kBorderLeftPx   : 0;
    activeY_ = border ? kBorderTopLines : 0;
    const int w = border ? (kBorderLeftPx + aw + kBorderRightPx) : aw;
    const int h = border ? (kBorderTopLines + ah + kBorderBotLines) : ah;
    curAH_ = ah;
    if (w == curW_ && h == curH_) return;
    curW_ = w; curH_ = h;
    frame_.assign(static_cast<std::size_t>(w) * h, 0xFF000000u);
}

// Verrouille la résolution ET la fréquence de la trame : le décodage ligne à
// ligne s'y tient (un changement de $FF8260 ou $FF820A en cours de trame ne prend
// effet qu'à la suivante, comme l'ancien renderFrame qui figeait la rés. au moment
// du décodage). La géométrie de la trame (cycles/ligne, lignes/trame) découle de
// ce couple — cf. geometry() — et est lue par Machine juste après cet appel.
void Shifter::beginFrame() {
    frameMode_ = mode;
    frameSync_ = sync;
    resizeFor(frameMode_);
    // Réinitialise l'enregistrement spec512 de la trame qui commence. La palette
    // courante devient la base du replay (= état de fin de trame précédente ; les
    // écritures palette de CETTE trame seront rejouées par-dessus, datées).
    colorWrites_.clear();
    paletteAccesses_ = 0;
    spec512Active_   = false;
    frameStartPalette_ = palette;
    syncWrites_.clear();
    bordersTrick_ = false;
    // VDE_On live du compteur vidéo : valeur nominale selon la fréquence verrouillée
    // (50 Hz → 63, 60 Hz → 34). Les bascules freq de la trame peuvent l'avancer
    // (retrait bordure haute) via updateLiveStartHBL.
    liveStartHBL_ = (frameSync_ & 0x02) ? 63 : 34;
    // Compteur vidéo matérialisé : LATCH de la base au début de trame (port
    // Video_ClearOnVBL → Video_RestartVideoCounter : pVideoRaster = &STRam[VideoBase]).
    // Une écriture $FF8201/03 en cours de trame ne réapparaîtra qu'ici, à la trame
    // suivante — comme sur le vrai matériel. Les écritures différées (NewHWScrollCount,
    // NewLineWidth, offset compteur) ne sont PAS effacées : Hatari ne les purge qu'au
    // reset vidéo, elles s'appliquent à la prochaine fin de ligne active.
    vcFrameBase_ = videoBase & 0xFFFFFFu;
    vcLineBase_  = vcFrameBase_;
    vcLineY_     = 0;
    // Fond bordure : remplit tout le buffer overscan avec la couleur de bordure
    // (registre 0) au début de trame. Les lignes actives écrasent leur zone ; les
    // bordures haut/bas et les côtés non réécrits restent à cette couleur. (Phase 1 :
    // couleur de bordure figée à la trame ; les barres raster en bordure haut/bas
    // viendront avec le retrait de bordures et le suivi du registre 0 par ligne.)
    if (bordered()) {
        const uint32_t bg = stColorToArgb(palette[0]);
        std::fill(frame_.begin(), frame_.end(), bg);
    }
    // Machine Glue LIVE : prépare l'état par-ligne de la nouvelle trame. Mêmes
    // structures que replayGlue — qui ré-écrase TOUT en fin de trame à partir des
    // mêmes syncWrites_, donc live et replay donnent le même résultat ; entre-temps
    // videoCounter() peut consulter la fenêtre DE réelle de la ligne courante.
    if (frameMode_ != Mode::High) {
        const Geometry g = geometry();
        glueLines_.assign(static_cast<std::size_t>(g.linesPerFrame) + 2, GlueLine{ -1, 0, 0, 0 });
        glueStartHBL_   = g.dispStartLine;
        glueEndHBL_     = g.dispStartLine + g.displayLines;
        glueVOverscan_  = 0;
        glueBlankLines_ = 0;
        nScreenRefreshRate_ = (frameSync_ & 0x02) ? 50 : 60;
    }
    liveGlueLine_   = -1;
    liveGlueWi_     = 0;
    liveGlueRes_    = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
    liveGlueFreq50_ = (frameSync_ & 0x02) ? 1 : 0;
}

// Avance la machine Glue LIVE jusqu'à la ligne `targetLine` incluse : startHBL sur
// les lignes nouvellement atteintes + consommation des écritures freq/res déjà
// enregistrées, en ordre chronologique — exactement la boucle de replayGlue, mais
// au fil de la trame (les écritures arrivent triées : recordSyncWrite est daté live).
void Shifter::liveGlueCatchUp(int targetLine) {
    if (frameMode_ == Mode::High || glueLines_.size() < 2) return;
    const int maxLine = static_cast<int>(glueLines_.size()) - 2;
    if (targetLine > maxLine) targetLine = maxLine;
    const int cpl = geometry().cyclesPerLine;
    for (;;) {
        // Écriture en attente sur une ligne déjà initialisée → consommer AVANT
        // d'avancer (elle conditionne res/freq des lignes suivantes).
        if (liveGlueWi_ < syncWrites_.size()) {
            const SyncWrite& w = syncWrites_[liveGlueWi_];
            int wl = w.frameCycle / cpl;
            if (wl > maxLine) wl = maxLine;
            if (wl <= liveGlueLine_) {
                if (w.isRes) liveGlueRes_    = w.val & 0x03;
                else         liveGlueFreq50_ = (w.val & 0x02) ? 1 : 0;
                const int freqHz = (liveGlueRes_ == 2) ? 71 : (liveGlueFreq50_ ? 50 : 60);
                updateGlueState(wl, w.frameCycle % cpl, w.isRes, freqHz);
                ++liveGlueWi_;
                continue;
            }
        }
        if (liveGlueLine_ >= targetLine) break;
        ++liveGlueLine_;
        const int freqHz = (liveGlueRes_ == 2) ? 71 : (liveGlueFreq50_ ? 50 : 60);
        startHBL(liveGlueLine_, liveGlueRes_, freqHz);
    }
}

// Décode les index de palette (ou bit mono) d'UNE scanline dans `idx`, selon la
// résolution VERROUILLÉE de la trame. Renvoie le décalage scroll fin STE.
int Shifter::decodeLineIndices(int y, uint8_t* idx) const {
    const int  W      = activeWidth();             // pixels de l'écran actif (hors bordures)
    const bool hi     = (frameMode_ == Mode::High);
    const int  planes = hi ? 1 : (frameMode_ == Mode::Medium ? 2 : 4);  // plans entrelacés
    const int  bpl    = hi ? 80 : 160;                  // octets/ligne AFFICHÉE
    const int  groupB = 2 * planes;                     // octets pour 16 px (1 mot/plan)
    const int  groups = W / 16;                         // groupes de 16 px affichés
    const int  scroll = hwScrollCount;                  // 0 hors STE scrollé ($FF8264/65)

    // Line-offset STE ($FF820F) : le shifter saute `lineWidth` MOTS en fin de ligne
    // → la ligne suivante démarre `bpl + lineWidth*2` octets plus loin (stride). Sur
    // ST/STF lineWidth=0 → stride = bpl (rendu strictement inchangé). Le scroll fin
    // avec prefetch ajoute 1 mot par plan (cf. scrollCounterAdvance).
    const uint32_t stride = static_cast<uint32_t>(bpl) + static_cast<uint32_t>(lineWidth) * 2u
                          + static_cast<uint32_t>(scrollCounterAdvance());
    // Adresse de la ligne : le compteur MATÉRIALISÉ (vcLineBase_, ≙ pVideoRaster) pour
    // le rendu live séquentiel — il accumule les strides RÉELS (lineWidth variable,
    // écritures du compteur $FF8205/07/09, scroll) — sinon repli analytique depuis la
    // base latchée de la trame (re-rendus spec512/renderFrame, où ces effets STE
    // dynamiques ne sont pas rejoués).
    const uint32_t base = (y == vcLineY_) ? vcLineBase_
                                          : vcFrameBase_ + static_cast<uint32_t>(y) * stride;

    // Deux variantes matérielles (port Video_CopyScreenLineColor) :
    //  • $FF8265 (PREFETCH) : le shifter lit un groupe de 16 px DE PLUS (1 mot par
    //    plan, juste après la ligne) pour fournir les `scroll` pixels qui entrent
    //    par la droite — la ligne entière est décalée À GAUCHE de `scroll` px.
    //  • $FF8264 (SANS prefetch) : aucun mot supplémentaire — l'affichage démarre
    //    16 px plus tard (premiers 16 px = couleur 0) et s'arrête au point normal :
    //    dst[c] = source[c-16+scroll]. On pré-transforme idx pour que l'émetteur
    //    (`idx[c + scroll]`) produise ce résultat : données décodées à partir de
    //    idx[16], puis idx[0..16+scroll) mis à l'index 0 (memmove+memset d'Hatari).
    //    En MONO, Hatari ne distingue pas le prefetch (Video_CopyScreenLineMono) →
    //    toujours le modèle prefetch.
    const bool prefetch = hwScrollPrefetch || hi;
    const int decodeGroups = (scroll && prefetch) ? groups + 1 : groups;
    int px = (scroll && !prefetch) ? 16 : 0;
    for (int g = 0; g < decodeGroups; ++g) {
        const uint32_t a  = base + static_cast<uint32_t>(g) * groupB;
        const uint16_t p0 = bus_.read16(a);
        const uint16_t p1 = planes > 1 ? bus_.read16(a + 2) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16(a + 4) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16(a + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));   // bord gauche couleur 0
    return scroll;
}

// Avance SUPPLÉMENTAIRE du compteur vidéo par ligne due au scroll fin (port des
// `pVideoRaster += n*2` de Video_CopyScreenLine*) : avec PREFETCH ($FF8265) le
// shifter a consommé 1 mot PAR PLAN de plus (8 octets en basse rés, 4 en moyenne) ;
// sans prefetch ($FF8264) : rien. En mono, Hatari avance toujours d'1 mot.
int Shifter::scrollCounterAdvance() const {
    if (!hwScrollCount) return 0;
    if (frameMode_ == Mode::High) return 2;
    if (!hwScrollPrefetch) return 0;
    return frameMode_ == Mode::Medium ? 4 : 8;
}

// Décode UNE scanline active (display-enable) avec l'état COURANT des registres
// (palette, base vidéo) et la place à l'offset bordure dans le buffer overscan.
void Shifter::renderLine(int y) {
    if (y < 0 || y >= curAH_) return;
    const int  W   = activeWidth();
    const bool hi  = (frameMode_ == Mode::High);

    uint8_t idx[660];                                   // max (640/16 + 1) * 16 = 656
    const int scroll = decodeLineIndices(y, idx);

    // Début de la zone ACTIVE dans le buffer (décalée des bordures gauche/haut).
    uint32_t* dst = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_ + activeX_;

    // Émet W pixels à partir de l'offset `scroll`. En haute résolution = moniteur
    // MONOCHROME : blanc (0) / noir (1), sans la palette couleur (sinon un
    // palette[1] non noir — ex. rouge sous TOS 1.02 — colore l'écran à tort).
    if (hi) {
        for (int c = 0; c < W; ++c)
            dst[c] = (idx[c + scroll] & 1) ? 0xFF000000u : 0xFFFFFFFFu;
    } else {
        for (int c = 0; c < W; ++c)
            dst[c] = stColorToArgb(palette[idx[c + scroll]]);
    }

    // Bordures latérales de CETTE ligne = couleur registre 0 courante (Phase 1).
    if (bordered()) {
        const uint32_t bg = stColorToArgb(palette[0]);
        uint32_t* row = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_;
        for (int x = 0; x < activeX_; ++x)         row[x] = bg;
        for (int x = activeX_ + W; x < curW_; ++x) row[x] = bg;
    }

    // Fin de la ligne active : le compteur matérialisé avance et les écritures STE
    // différées s'appliquent (uniquement sur le rendu live séquentiel — un re-rendu
    // hors séquence, spec512/renderFrame, ne doit pas re-avancer le compteur).
    if (y == vcLineY_) endVideoLine();
}

// Fin de Video_CopyScreenLine (video.c:3833-3872), adaptée au modèle NeoST : la
// ligne active vient d'être décodée → le compteur vidéo avance de son stride réel,
// puis les modifications STE différées pendant la ligne s'appliquent, dans l'ordre
// d'Hatari : scroll-prefetch (+1 mot), line-offset, offset compteur ($FF8205/07/09
// écrits pendant le DE), nouveau scroll fin, nouvelle largeur de ligne.
void Shifter::endVideoLine() {
    const int bpl = (frameMode_ == Mode::High) ? 80 : 160;
    vcLineBase_ += static_cast<uint32_t>(bpl);
    vcLineBase_ += static_cast<uint32_t>(scrollCounterAdvance());   // prefetch : +1 mot PAR PLAN
    vcLineBase_ += static_cast<uint32_t>(lineWidth) * 2u;  // line-offset STE (mots sautés)
    if (vcDelayedOffset_ != 0) {                           // écriture compteur pendant le DE
        vcLineBase_ += static_cast<uint32_t>(vcDelayedOffset_ & ~1);
        vcDelayedOffset_ = 0;
    }
    if (newHwScrollCount_ >= 0) {                          // HSCROLL différé (NewHWScrollCount)
        hwScrollCount    = static_cast<uint8_t>(newHwScrollCount_);
        hwScrollPrefetch = newHwScrollPrefetch_;
        newHwScrollCount_ = -1;
    }
    if (newLineWidth_ >= 0) {                              // LINEWIDTH différé (NewLineWidth)
        lineWidth = static_cast<uint8_t>(newLineWidth_);
        newLineWidth_ = -1;
    }
    vcLineBase_ &= 0xFFFFFFu;
    ++vcLineY_;
}

// Position du faisceau (ligne absolue + cycle dans la ligne) depuis l'horloge de
// trame. Sert aux décisions immédiat/différé des écritures STE (port
// Video_GetPosition_OnWriteAccess). false si aucune horloge n'est branchée (tests).
bool Shifter::beamPos(int& line, int& lineCyc) const {
    if (!beamClock_) return false;
    const Geometry g = geometry();
    const int64_t fc = beamClock_();
    if (fc < 0) return false;
    line    = static_cast<int>(fc / g.cyclesPerLine);
    lineCyc = static_cast<int>(fc % g.cyclesPerLine);
    return true;
}

// Fin de trame : re-rendu spec512 (palette intra-ligne) si détecté. Port du
// modèle Hatari spec512.c — au lieu de mémoriser une palette par ligne (figée à
// DE_END), on rejoue les écritures palette datées et on met à jour une palette
// « roulante » au CYCLE où chaque pixel est balayé. Le 68000 ne peut écrire la
// palette qu'une fois tous les 4 cycles (bus 16 bits), donc au plus ~1 changement
// tous les 4 pixels en basse résolution → jusqu'à 512 couleurs à l'écran.
void Shifter::finishFrame() {
    if (frameMode_ == Mode::High) return;                      // mono : ni spec512 ni bordures

    // Rejoue la machine Glue sur les écritures freq/res datées → état d'affichage
    // par scanline + bordures haut/bas (détection de retrait). Bon marché si aucun
    // switch (cas normal → bordersTrick_ reste faux).
    replayGlue();

    // Si un retrait de bordure est détecté, le rendu fenêtré (palette roulante)
    // gère TOUT : bordures ouvertes + raster par ligne + spec512 intra-ligne.
    if (bordersTrick_) { renderGlueFrame(); return; }

    if (!spec512Active_) return;                               // trame normale : rendu ligne-à-ligne conservé

    // Tri stable par cycle : les écritures d'un même move.l / movem partagent un
    // cycle ; l'ordre d'insertion (= ordre des registres) doit être conservé pour
    // que la dernière l'emporte, exactement comme sur le bus.
    std::stable_sort(colorWrites_.begin(), colorWrites_.end(),
                     [](const ColorWrite& a, const ColorWrite& b) {
                         return a.frameCycle < b.frameCycle;
                     });

    // Wait states d'alignement bus 4 cycles du shifter (cf. applyShifterBusAlignment) :
    // recale les positions intra-ligne sur le vrai HW (sans ça, dérive -2 cyc/ligne).
    applyShifterBusAlignment();

    // DEBUG (NEOST_SPEC512_TRACE) : dump des écritures palette converties en
    // (ligne, position dans la ligne) — format comparable au trace `video_color`
    // d'Hatari (« spec store col line N cyc=H idx=I col=RGB »). Diff oracle.
    static const char* spcTrace = std::getenv("NEOST_SPEC512_TRACE");
    if (spcTrace) {
        const Geometry gg = geometry();
        FILE* tf = std::fopen(spcTrace, "w");
        if (tf) {
            for (const auto& w : colorWrites_) {
                const int line = static_cast<int>(w.frameCycle / gg.cyclesPerLine);
                const int hpos = static_cast<int>(w.frameCycle % gg.cyclesPerLine);
                std::fprintf(tf, "line %d cyc=%d idx=%d col=%03x pc=%06x\n",
                             line, hpos, w.index, w.colour & 0xFFF, w.pc);
            }
            std::fclose(tf);
            std::fprintf(stderr, "[spec512] %zu écritures palette → %s\n",
                         colorWrites_.size(), spcTrace);
        }
    }

    const Geometry g   = geometry();
    const int W        = activeWidth();            // pixels de l'écran actif (hors bordures)
    const int cpl      = g.cyclesPerLine;
    const int lineStart= g.lineStartCycle;
    const int dispStart= g.dispStartLine;          // VDE_On : l'affichage actif commence à cette ligne
    const int span     = g.lineEndCycle - g.lineStartCycle;    // cycles couvrant les W pixels affichés

    // Palette roulante : démarre à l'état de début de trame puis absorbe chaque
    // écriture au cycle voulu, en avançant un curseur unique sur toute la trame
    // (les cycles-pixel sont monotones croissants ligne après ligne).
    std::array<uint16_t, 16> pal = frameStartPalette_;
    const std::size_t n = colorWrites_.size();
    std::size_t cur = 0;

    static const char* alo = std::getenv("NEOST_ALIGN_OFF");   // DEBUG : offset ADDITIONNEL (relatif à kSpec512AlignCyc) pour re-calibrer le rendu vs oracle
    const int alignOff = alo ? std::atoi(alo) : 0;

    uint8_t idx[660];
    for (int y = 0; y < curAH_; ++y) {
        const int scroll = decodeLineIndices(y, idx);
        // Zone active dans le buffer overscan (décalée des bordures gauche/haut).
        uint32_t* dst = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_ + activeX_;
        for (int c = 0; c < W; ++c) {
            // Cycle (dans la trame) où le pixel c de la ligne y sort du shifter
            // (1 cycle/pixel en basse résolution, 0,5 en moyenne). La ligne active y
            // est balayée à la scanline (dispStart + y) — l'offset VDE_On aligne les
            // écritures (datées au cycle live, ~(63+y)*cpl) sur les pixels. Le décalage
            // kSpec512AlignCyc cale le front couleur sur le front pixel.
            const int64_t pixCyc = static_cast<int64_t>(dispStart + y) * cpl + lineStart
                                 + static_cast<int64_t>(c) * span / W;
            const int64_t limit = pixCyc - kSpec512AlignCyc + alignOff;
            while (cur < n && colorWrites_[cur].frameCycle <= limit) {
                pal[colorWrites_[cur].index] = colorWrites_[cur].colour;
                ++cur;
            }
            dst[c] = stColorToArgb(pal[idx[c + scroll]]);
        }
    }
}

// Rejoue HORS-LIGNE les wait states d'alignement bus du shifter (port fidèle de
// Hatari M68000_SyncCpuBus, video.c:5382). Les registres couleur ($FF824x) ne
// s'accèdent que sur une frontière de 4 cycles : une écriture mot qui arrive à un
// cycle non multiple de 4 fait patienter le CPU jusqu'à la frontière, soit
// (4 - cyc%4) cycles de gel. Le CPU étant gelé, CE wait DÉCALE D'AUTANT toutes les
// écritures suivantes → on accumule le décalage et on le propage. Moira (68000 pur)
// ne modélise pas ces wait states ; la boucle spec512 (24 move.l (a3)+,(ax)+ + dbra
// = 510 cyc/ligne sous Moira) dérive alors de -2 cyc/ligne au lieu de +4 sur vrai HW.
// En recalant chaque écriture sur sa frontière de 4 cycles ON RECONSTRUIT le timing
// matériel, sans toucher la timeline live (zéro régression). colorWrites_ est déjà
// trié par cycle d'exécution (croissant) en entrée.
void Shifter::applyShifterBusAlignment() {
    int64_t accumWait = 0;                 // total des wait states injectés jusqu'ici
    for (auto& w : colorWrites_) {
        const int64_t arrival = static_cast<int64_t>(w.frameCycle) + accumWait;
        const int64_t wait = (4 - (arrival & 3)) & 3;     // 0..3 jusqu'à la frontière de 4
        accumWait += wait;
        w.frameCycle = static_cast<int32_t>(arrival + wait);
    }
}

// Enregistre l'écriture palette du registre `index` (valeur déjà posée dans
// palette[index]) avec son cycle live dans la trame, pour le re-rendu spec512.
void Shifter::recordColorWrite(int index) {
    if (!liveFrameClock_) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;                              // hors trame courante

    // Une écriture MOT ($FF824x) passe par le bus en DEUX write8 (gros-boutiste,
    // même cycle) : le 1ᵉʳ pose l'octet haut (valeur transitoire haut-neuf/bas-vieux),
    // le 2ᵉ l'octet bas (valeur finale). Sur le vrai 68000 c'est UN seul accès mot.
    // On fusionne donc les deux : si la dernière écriture vise le MÊME registre au
    // MÊME cycle, on met simplement à jour sa couleur (valeur finale) — un seul
    // ColorWrite par mot, comme Hatari (CyclePalettes[] = 1 entrée/écriture mot).
    if (!colorWrites_.empty()) {
        ColorWrite& last = colorWrites_.back();
        if (last.frameCycle == static_cast<int32_t>(fc) &&
            last.index == static_cast<uint8_t>(index)) {
            last.colour = palette[index];
            return;
        }
    }
    const uint32_t wpc = bus_.cpu ? bus_.cpu->pc() : 0;
    colorWrites_.push_back({ static_cast<int32_t>(fc), palette[index],
                             static_cast<uint8_t>(index), wpc });
    if (++paletteAccesses_ >= kSpec512Threshold) spec512Active_ = true;
}

// Wait state de bus 4 cycles (port LIVE de Hatari M68000_SyncCpuBus, cf. .hpp). Le
// cycle de référence est le cycle LIVE dans la trame (même horloge que recordColorWrite) ;
// on aligne sur la frontière de 4 en faisant patienter le CPU d'autant.
void Shifter::syncCpuBus() {
    if (!liveFrameClock_ || !bus_.cpu) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;                                  // hors trame courante
    const int wait = static_cast<int>((4 - (fc & 3)) & 3);   // 0..3 jusqu'à la frontière
    if (wait) bus_.cpu->addBusWaitCycles(wait);
}

void Shifter::recordSyncWrite(bool isRes, uint8_t val) {
    if (!liveFrameClock_) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;
    syncWrites_.push_back({ static_cast<int32_t>(fc), val, isRes });
    updateLiveStartHBL(static_cast<int32_t>(fc), isRes, val);   // VDE_On live (retrait haut)
    // Machine Glue LIVE : consomme l'écriture immédiatement (fenêtre DE de la ligne
    // courante à jour pour les lectures $FF8209 qui suivent).
    liveGlueCatchUp(static_cast<int>(fc / geometry().cyclesPerLine));
}

// Met à jour le VDE_On LIVE du compteur vidéo sur une écriture freq — détection du
// RETRAIT de bordure HAUTE (port du comportement de Hatari Video_Update_Glue_State /
// nStartHBL, video.c ~2895). Sur le vrai matériel, une bascule 60 Hz pendant la
// bordure haute (avant VDE_On 50 Hz = ligne 63) ouvre le haut de l'écran : la 1ʳᵉ
// ligne affichée passe à 34 (VDE_On 60 Hz) et le compteur d'adresse vidéo commence
// donc à monter dès la ligne 34 au lieu de 63. C'est exactement ce dont dépendent les
// boucles d'auto-synchro fullscreen (Cuddly Demo) qui sondent $FF8209 pour se caler.
//
// Modèle (approximation fidèle au RÉSULTAT) : toute bascule 60 Hz dans la bordure haute
// VERROUILLE VDE_On=34 pour la trame — on ne fait que BAISSER (jamais remonter). En
// effet la boucle d'auto-synchro toggle 60→50 Hz à chaque itération ; sur le matériel
// la décision est latchée au passage de la ligne et le 50 Hz qui suit ne la ré-ferme
// pas. Un retrait gauche/droite (sur les lignes AFFICHÉES ≥ 63) ou bas (ligne 262)
// n'entre pas dans la fenêtre [0,63) → non concerné. Un écran 50 Hz ordinaire ne fait
// AUCUNE bascule freq → liveStartHBL_ reste 63 (zéro régression). On NE touche QUE
// liveStartHBL_ (lu par videoCounter) ; la géométrie de rendu (replayGlue) est inchangée.
void Shifter::updateLiveStartHBL(int32_t frameCycle, bool isRes, uint8_t val) {
    if (frameMode_ == Mode::High) return;            // mono : pas concerné
    if (isRes) return;                               // seules les bascules freq bougent VDE_On ici
    const Geometry g = geometry();
    const int line = frameCycle / g.cyclesPerLine;
    constexpr int VDE_On_50 = 63, VDE_On_60 = 34;
    const bool freq60 = !(val & 0x02);               // bit1=0 → 60 Hz
    if (freq60 && line < VDE_On_50 && VDE_On_60 < liveStartHBL_)
        liveStartHBL_ = VDE_On_60;                   // retrait haut verrouillé (sticky)
}

// Valeurs par défaut d'une scanline selon res/freq COURANTS au début de la ligne
// (port de Hatari Video_StartHBL). DisplayStartCycle n'est posé que s'il vaut -1
// (une écriture de la ligne précédente a pu le pré-positionner : right-off full).
void Shifter::startHBL(int line, int curRes, int freqHz) {
    GlueLine& L = glueLines_[line];
    if (curRes == 2) {                                   // haute résolution (71 Hz)
        if (L.displayStartCycle == -1) L.displayStartCycle = glue::HDE_On_Hi;   // 4
        L.displayEndCycle = glue::HDE_Off_Hi;            // 164
        if (nScreenRefreshRate_ != 71) {                 // ligne hi dans écran non-71 → retrait gauche par défaut
            L.borderMask |= glue::LEFT_OFF;
            L.displayPixelShift = -4;
        }
    } else if (freqHz == 50) {
        if (L.displayStartCycle == -1) L.displayStartCycle = glue::HDE_On_Low_50;  // 56
        L.displayEndCycle = glue::HDE_Off_Low_50;        // 376
    } else {                                             // 60 Hz
        if (L.displayStartCycle == -1) L.displayStartCycle = glue::HDE_On_Low_60;  // 52
        L.displayEndCycle = glue::HDE_Off_Low_60;        // 372
        if (nScreenRefreshRate_ == 50)                   // ligne 60 Hz dans écran 50 Hz → left+2/right-2
            L.borderMask |= (glue::LEFT_PLUS_2 | glue::RIGHT_MINUS_2);
    }
}

// Port FIDÈLE de Video_Update_Glue_State (chemin STF) : applique une écriture
// freq/res au cycle `lineCycles` de la scanline `line`. Met à jour la GlueLine
// (DE start/end, BorderMask, PixelShift), les lignes voisines (right-off full), et
// les bordures haut/bas (glueStartHBL_/glueEndHBL_ + glueVOverscan_).
void Shifter::updateGlueState(int line, int lineCycles, bool writeToRes, int freqHz) {
    using namespace glue;
    if (writeToRes) lineCycles--;                        // GLUE latche la res 1 cyc avant la freq (STF)

    GlueLine& GL = glueLines_[line];
    int DE_start = GL.displayStartCycle;
    int DE_end   = GL.displayEndCycle;
    uint32_t BorderMask = GL.borderMask;
    bool Freq_match_found = false;
    const int cpl = geometry().cyclesPerLine;

    // ===== Phase 1 : valeur de Freq AVANT DE_start (STF, video.c 2244-2438) =====
    if (freqHz == 71 && lineCycles <= HDE_On_Hi) {
        Freq_match_found = true;
        if (!(BorderMask & NO_DE)) {
            DE_start = HDE_On_Hi; DE_end = HDE_Off_Hi;
            BorderMask |= LEFT_OFF; GL.displayPixelShift = -4;
            BorderMask &= ~LEFT_PLUS_2;
        }
    } else if (freqHz == 71 && lineCycles <= HBlank_Off_Low_50) {
        Freq_match_found = true;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= (BLANK | NO_DE); }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz == 71 && lineCycles <= HDE_On_Low_50) {
        Freq_match_found = true;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Hi; BorderMask |= NO_DE; }
        BorderMask &= ~LEFT_PLUS_2;
    } else if (freqHz != 71) {
        if (lineCycles <= HDE_On_Hi && (BorderMask & LEFT_OFF)) {
            if (freqHz == 50) DE_start = HDE_On_Low_50;
            else { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
            BorderMask &= ~LEFT_OFF; GL.displayPixelShift = 0;
        }
        if (lineCycles <= HBlank_Off_Low_50 && (BorderMask & (BLANK | NO_DE)) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~(BLANK | NO_DE);
        } else if (lineCycles <= HDE_On_Low_50 && (BorderMask & NO_DE) && !(BorderMask & BLANK) && !(BorderMask & NO_COUNT)) {
            BorderMask &= ~NO_DE;
        }
    }

    // Ligne 50 Hz qui continue en 60 Hz (et réciproque) — video.c 2342-2421
    if (freqHz == 60 && lineCycles < Line_Set_Pal) {
        Freq_match_found = true;
        if (!(BorderMask & NO_DE)) {
            if (DE_start > 0) { DE_end = HDE_Off_Low_60; BorderMask |= RIGHT_MINUS_2; }
            if (lineCycles > HBlank_Off_Low_60 && lineCycles <= HBlank_Off_Low_50) BorderMask |= BLANK;
            if (DE_start == HDE_On_Low_50) { DE_start = HDE_On_Low_60; BorderMask |= LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= HDE_On_Low_60) {
        Freq_match_found = true;
        if (!(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            BorderMask &= ~RIGHT_MINUS_2;
            if (DE_start == HDE_On_Low_60) { DE_start = HDE_On_Low_50; BorderMask &= ~LEFT_PLUS_2; }
        }
    } else if (freqHz == 50 && lineCycles <= Line_Set_Pal) {
        Freq_match_found = true;
        if (!(BorderMask & NO_DE)) { DE_end = HDE_Off_Low_50; BorderMask &= ~RIGHT_MINUS_2; }
    }

    if (freqHz == 60 && lineCycles > HDE_On_Low_60 && lineCycles <= HDE_On_Low_50 && !(BorderMask & NO_DE)) {
        Freq_match_found = true;
        if (DE_start == HDE_On_Low_50) { DE_start = 0; DE_end = 0; BorderMask |= NO_DE; }
    }

    // ===== Phase 2 : valeur de Freq ENTRE DE_start et DE_end (video.c 2667-2841) =====
    if (!Freq_match_found) {
        GlueLine& NX = glueLines_[line + 1];             // ligne suivante (right-off full, no-sync)
        if (freqHz == 71 && lineCycles <= DE_end && lineCycles <= HDE_Off_Hi && !(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Hi; BorderMask |= STOP_MIDDLE; BorderMask &= ~RIGHT_MINUS_2;
        } else if (freqHz == 71 && lineCycles <= DE_end && !(BorderMask & NO_DE)) {
            DE_end = LINE_END_FULL;
            BorderMask |= (RIGHT_OFF | RIGHT_OFF_FULL);
            NX.borderMask |= LEFT_OFF; NX.displayStartCycle = HDE_On_Hi;
        } else if (freqHz == 71 && lineCycles <= cpl + HSync_On_Off_Low) {
            BorderMask |= NO_SYNC;
            NX.borderMask |= (BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = 0; NX.displayEndCycle = 0;
            glueBlankLines_++;
        } else if (freqHz == 71 && lineCycles <= cpl + HSync_Off_Off_Low) {
            BorderMask |= SYNC_HIGH;
            NX.borderMask |= (BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = 0; NX.displayEndCycle = 0;
            glueBlankLines_++;
        } else if (freqHz == 71) {
            NX.borderMask |= LEFT_OFF;
            NX.displayStartCycle = HDE_On_Hi; NX.displayEndCycle = HDE_Off_Hi; NX.displayPixelShift = -4;
        }

        if (freqHz == 60 && lineCycles <= DE_end && lineCycles <= HDE_Off_Low_60 && !(BorderMask & NO_DE)) {
            if (DE_end == HDE_Off_Low_50) BorderMask |= RIGHT_MINUS_2;
            DE_end = HDE_Off_Low_60;
            if (BorderMask & STOP_MIDDLE) BorderMask &= ~STOP_MIDDLE;
            else if (BorderMask & (RIGHT_OFF | RIGHT_OFF_FULL)) {
                BorderMask &= ~(RIGHT_OFF | RIGHT_OFF_FULL);
                NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
            }
        } else if (freqHz == 50 && lineCycles <= DE_end && lineCycles <= HDE_Off_Low_50 && !(BorderMask & NO_DE)) {
            DE_end = HDE_Off_Low_50;
            if (BorderMask & RIGHT_MINUS_2) BorderMask &= ~RIGHT_MINUS_2;
            else if (BorderMask & STOP_MIDDLE) BorderMask &= ~STOP_MIDDLE;
            else if (BorderMask & (RIGHT_OFF | RIGHT_OFF_FULL)) {
                BorderMask &= ~(RIGHT_OFF | RIGHT_OFF_FULL);
                NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
            }
        } else if (freqHz == 60 && lineCycles <= DE_end && lineCycles > HDE_Off_Low_60
                   && lineCycles <= HDE_Off_Low_50 && !(BorderMask & NO_DE)) {
            if (DE_end == HDE_Off_Low_50) {              // retrait bordure DROITE
                DE_end = cpl + HSync_On_Off_Low;        // 462 (50 Hz) — l'affichage va jusqu'au HSYNC
                BorderMask |= RIGHT_OFF;
                BorderMask &= ~RIGHT_MINUS_2;
            }
        } else if (freqHz != 71 && lineCycles <= cpl + HSync_On_Off_Low) {
            if (lineCycles <= DE_end) {
                DE_end = cpl + HSync_On_Off_Low;
                if (BorderMask & RIGHT_OFF_FULL) {
                    BorderMask &= ~RIGHT_OFF_FULL;
                    NX.borderMask &= ~LEFT_OFF; NX.displayStartCycle = -1;
                }
            } else if (BorderMask & NO_SYNC) {
                BorderMask &= ~NO_SYNC;
                NX.borderMask &= ~(BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = -1; glueBlankLines_--;
            }
        } else if (freqHz != 71 && lineCycles <= cpl + HSync_Off_Off_Low) {
            if (BorderMask & SYNC_HIGH) {
                BorderMask &= ~SYNC_HIGH;
                NX.borderMask &= ~(BLANK | NO_DE | NO_COUNT); NX.displayStartCycle = -1; glueBlankLines_--;
            }
        }
    }

    // ===== Bordures HAUT/BAS (video.c 2896-2991) =====
    // Top : tant que la 1ʳᵉ ligne affichée n'est pas atteinte, on peut la déplacer.
    if (line < glueStartHBL_ - 1
        || (line == glueStartHBL_ - 1 && lineCycles <= RemoveTopBorder_Pos)) {
        int Top_Pos = (freqHz == 71) ? VDE_On_Hi : (freqHz == 60 ? VDE_On_60 : VDE_On_50);
        if (Top_Pos != glueStartHBL_
            && (line < Top_Pos - 1 || (line == Top_Pos - 1 && lineCycles <= RemoveTopBorder_Pos))) {
            glueStartHBL_ = Top_Pos;
            if (nScreenRefreshRate_ == 50 && glueStartHBL_ < VDE_On_50) glueVOverscan_ |= VO_NO_TOP;
            else glueVOverscan_ &= ~VO_NO_TOP;
            glueVOverscan_ &= ~VO_NO_DE;
        } else {
            if (nScreenRefreshRate_ == 50 && freqHz != 50) glueVOverscan_ |= VO_NO_DE;
            else glueVOverscan_ &= ~VO_NO_DE;
        }
    }
    // Bottom : tant que la dernière ligne affichée n'est pas atteinte.
    if (line < glueEndHBL_ - 1
        || (line == glueEndHBL_ - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
        int Bottom_Pos = (freqHz == 71) ? VDE_Off_Hi : (freqHz == 60 ? VDE_Off_60 : VDE_Off_50);
        if (line < VDE_Off_60 - 1
            || (line == VDE_Off_60 - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (nScreenRefreshRate_ == 60 && freqHz != 60) { glueEndHBL_ = VDE_Off_NoBottom_60; glueVOverscan_ |= VO_NO_BOTTOM_60; }
            else if (nScreenRefreshRate_ == 50 && freqHz == 60) { glueEndHBL_ = VDE_Off_60; glueVOverscan_ |= VO_BOTTOM_SHORT_50; }
            else { glueEndHBL_ = Bottom_Pos; glueVOverscan_ &= ~(VO_NO_BOTTOM_60 | VO_BOTTOM_SHORT_50); }
        } else if (line < VDE_Off_50 - 1
                   || (line == VDE_Off_50 - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (glueVOverscan_ & VO_NO_BOTTOM_60) { /* déjà retiré, inchangeable */ }
            else if (nScreenRefreshRate_ == 50 && freqHz != 50) { glueEndHBL_ = VDE_Off_NoBottom_50; glueVOverscan_ |= VO_NO_BOTTOM_50; }
            else { glueEndHBL_ = Bottom_Pos; glueVOverscan_ &= ~VO_NO_BOTTOM_50; }
        } else if (line < VDE_Off_Hi - 1
                   || (line == VDE_Off_Hi - 1 && lineCycles <= RemoveBottomBorder_Pos)) {
            if (glueVOverscan_ & VO_NO_BOTTOM_50) { /* déjà retiré */ }
            else { glueEndHBL_ = Bottom_Pos; }
        }
    }

    GL.displayStartCycle = static_cast<int16_t>(DE_start);
    GL.displayEndCycle   = static_cast<int16_t>(DE_end);
    GL.borderMask        = BorderMask;
}

// Rejoue la machine Glue sur les écritures freq/res datées de la trame, ligne par
// ligne (StartHBL defaults → écritures via updateGlueState). Remplit glueLines_,
// glueStartHBL_/glueEndHBL_ et arme bordersTrick_ si une bordure est retirée.
void Shifter::replayGlue() {
    bordersTrick_ = false;
    if (frameMode_ == Mode::High) return;                // mono : pas de bordures modélisées

    const Geometry g = geometry();
    const int lpf = g.linesPerFrame;
    const int cpl = g.cyclesPerLine;
    nScreenRefreshRate_ = (frameSync_ & 0x02) ? 50 : 60;
    const int baseStart = g.dispStartLine;               // VDE_On de l'écran (63/34)
    const int baseEnd   = baseStart + g.displayLines;    // VDE_Off (263/234)

    glueLines_.assign(static_cast<std::size_t>(lpf) + 2, GlueLine{ -1, 0, 0, 0 });
    glueStartHBL_   = baseStart;
    glueEndHBL_     = baseEnd;
    glueVOverscan_  = 0;
    glueBlankLines_ = 0;

    std::stable_sort(syncWrites_.begin(), syncWrites_.end(),
                     [](const SyncWrite& a, const SyncWrite& b){ return a.frameCycle < b.frameCycle; });

    // État res/freq COURANT (début de trame = valeurs verrouillées). res : 0=low,
    // 1=med, 2=hi ; freq : bit1 de $FF820A (1=50 Hz).
    int curRes  = (frameMode_ == Mode::Medium) ? 1 : (frameMode_ == Mode::High ? 2 : 0);
    int curFreq50 = (frameSync_ & 0x02) ? 1 : 0;

    std::size_t wi = 0;
    const std::size_t nw = syncWrites_.size();
    for (int line = 0; line < lpf; ++line) {
        int freqHz = (curRes == 2) ? 71 : (curFreq50 ? 50 : 60);
        startHBL(line, curRes, freqHz);
        // Applique les écritures de CETTE ligne (cycle croissant).
        while (wi < nw && (syncWrites_[wi].frameCycle / cpl) == line) {
            const SyncWrite& w = syncWrites_[wi++];
            const int lc = w.frameCycle % cpl;
            if (w.isRes) curRes    = w.val & 0x03;
            else         curFreq50 = (w.val & 0x02) ? 1 : 0;
            freqHz = (curRes == 2) ? 71 : (curFreq50 ? 50 : 60);
            updateGlueState(line, lc, w.isRes, freqHz);
        }
    }

    // Détection : une bordure est-elle retirée ? (haut/bas déplacés, ou une ligne
    // affichée a un DE élargi gauche/droite).
    if (glueStartHBL_ != baseStart || glueEndHBL_ != baseEnd) bordersTrick_ = true;
    if (!bordersTrick_) {
        for (int sl = glueStartHBL_; sl < glueEndHBL_; ++sl) {
            const GlueLine& L = glueLines_[sl];
            if (L.displayStartCycle >= 0
                && (L.displayStartCycle < g.lineStartCycle || L.displayEndCycle > g.lineEndCycle)) {
                bordersTrick_ = true; break;
            }
        }
    }

    // Stat Glue (gated NEOST_GLUE_STAT) : liste les écritures freq/res datées de la
    // trame (ligne, cycle, registre, valeur) — diagnostic « pourquoi pas de trick ».
    if (!syncWrites_.empty() && std::getenv("NEOST_GLUE_STAT")) {
        std::fprintf(stderr, "[gluestat] %zu écritures :", syncWrites_.size());
        for (std::size_t i = 0; i < syncWrites_.size() && i < 24; ++i) {
            const SyncWrite& w = syncWrites_[i];
            std::fprintf(stderr, " %s=%02X@%lld+%d", w.isRes ? "res" : "frq", w.val,
                         (long long)(w.frameCycle / cpl), (int)(w.frameCycle % cpl));
        }
        std::fprintf(stderr, " | trick=%d start=%d end=%d\n", bordersTrick_ ? 1 : 0,
                     glueStartHBL_, glueEndHBL_);
    }

    // Trace bordure (gated NEOST_BORDER_TRACE) : pour le diff oracle Hatari
    // (video_border_h/v). Émet les retraits détectés cette trame, format comparable.
    if (bordersTrick_ && std::getenv("NEOST_BORDER_TRACE")) {
        if (glueStartHBL_ < baseStart)
            std::fprintf(stderr, "detect remove top (nStartHBL=%d)\n", glueStartHBL_);
        if (glueEndHBL_ > baseEnd)
            std::fprintf(stderr, "detect remove bottom (nEndHBL=%d)\n", glueEndHBL_);
        int nLeft = 0, nRight = 0;
        for (int sl = glueStartHBL_; sl < glueEndHBL_ && sl < (int)glueLines_.size(); ++sl) {
            const GlueLine& L = glueLines_[sl];
            if (L.displayStartCycle >= 0 && L.displayStartCycle < g.lineStartCycle) ++nLeft;
            if (L.displayEndCycle > g.lineEndCycle) ++nRight;
        }
        if (nLeft)  std::fprintf(stderr, "detect remove left x%d\n", nLeft);
        if (nRight) std::fprintf(stderr, "detect remove right x%d\n", nRight);
    }
}

// Décode `nPix` index planaires à partir de l'adresse vidéo `base` (rendu fenêtré
// des bordures : largeur explicite, base fournie). `idx` doit tenir nPix+16 octets.
int Shifter::decodeWindowIndices(uint32_t base, int nPix, uint8_t* idx) const {
    const int planes = (frameMode_ == Mode::Medium) ? 2 : 4;   // low=4, med=2
    const int groupB = 2 * planes;                             // octets pour 16 px
    const int groups = (nPix + 15) / 16;
    int px = 0;
    for (int gI = 0; gI < groups; ++gI) {
        const uint32_t a  = base + static_cast<uint32_t>(gI) * groupB;
        const uint16_t p0 = bus_.read16(a);
        const uint16_t p1 = planes > 1 ? bus_.read16(a + 2) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16(a + 4) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16(a + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    return px;
}

// Re-rendu de la trame AVEC retrait de bordures : pour chaque LIGNE du buffer
// overscan, on calcule la scanline correspondante (sl = baseStart + row - activeY_),
// et si elle est affichée [glueStartHBL_, glueEndHBL_) on décode sa fenêtre
// [DisplayStartCycle, DisplayEndCycle) depuis l'ADRESSE VIDÉO ACCUMULÉE (port de
// Video_CalculateAddress : une ligne plus large lit plus d'octets → décale les
// suivantes). Palette ROULANTE (gère raster par ligne ET spec512 intra-ligne).
// Hors fenêtre (et lignes de bordure) = couleur registre 0 au cycle courant.
void Shifter::renderGlueFrame() {
    const Geometry g    = geometry();
    const int W         = curW_;                           // largeur buffer (overscan)
    const int cpl       = g.cyclesPerLine;
    const int baseStart = g.dispStartLine;                 // scanline du haut de l'actif (buffer row activeY_)
    const int visFirst  = g.lineStartCycle - kBorderLeftPx;   // cycle au buffer x=0 (56-48=8)
    const int bytePerPix = (frameMode_ == Mode::Medium) ? 4 : 2;  // px par octet (low=2, med=4)

    std::stable_sort(colorWrites_.begin(), colorWrites_.end(),
                     [](const ColorWrite& a, const ColorWrite& b){ return a.frameCycle < b.frameCycle; });
    // Recalage spec512 (wait states bus + offset pixel↔couleur) UNIQUEMENT pour une
    // vraie image spec512 (palette réécrite intra-ligne). Pour un écran glue ordinaire
    // (desktop 60 Hz, barres raster), la palette est posée une fois hors-affichage :
    // on garde l'ancien chemin (offset 0, pas de wait states) → rendu byte-identique,
    // zéro régression. Les démos overscan spec512 (palette intra-ligne + bordures)
    // bénéficient du même recalage que le chemin sans bordure.
    const int glueAlignCyc = spec512Active_ ? kSpec512AlignCyc : 0;
    if (spec512Active_) applyShifterBusAlignment();
    std::array<uint16_t, 16> pal = frameStartPalette_;
    const std::size_t n = colorWrites_.size();
    std::size_t cur = 0;
    uint32_t addr = vcFrameBase_ & 0xFFFFFFu;              // compteur vidéo latché au VBL (≙ Video_ClearOnVBL)
    const int nLines = static_cast<int>(glueLines_.size());

    uint8_t idx[700];                                      // max DE (462-4) + marge
    for (int row = 0; row < curH_; ++row) {
        const int sl = baseStart + (row - activeY_);       // scanline de cette ligne buffer
        const bool displayed = (sl >= glueStartHBL_ && sl < glueEndHBL_ && sl >= 0 && sl < nLines);
        int ds = 0, de = 0, shift = 0; uint32_t bm = 0;
        if (displayed) { const GlueLine& L = glueLines_[sl]; ds = L.displayStartCycle; de = L.displayEndCycle; bm = L.borderMask; shift = L.displayPixelShift; }
        const bool lineHasDE = displayed && !(bm & glue::NO_DE) && de > ds;
        const int  nPix = lineHasDE ? (de - ds) : 0;
        // decodeWindowIndices décode des GROUPES de 16 px : la plage valide de idx est
        // [0, nDec) avec nDec arrondi au groupe supérieur → marge pour le DisplayPixelShift.
        const int  nDec = lineHasDE ? ((nPix + 15) / 16) * 16 : 0;
        if (nPix > 0) decodeWindowIndices(addr, nPix, idx);

        uint32_t* dst = frame_.data() + static_cast<std::size_t>(row) * W;
        for (int x = 0; x < W; ++x) {
            const int cyc = visFirst + x;                  // cycle du pixel balayé à cette colonne
            const int64_t limit = static_cast<int64_t>(sl) * cpl + cyc - glueAlignCyc;
            while (cur < n && colorWrites_[cur].frameCycle <= limit) {
                pal[colorWrites_[cur].index] = colorWrites_[cur].colour;
                ++cur;
            }
            if (lineHasDE && cyc >= ds && cyc < de) {
                // Décalage pixel de la ligne (Hatari DisplayPixelShift : <0 = vers la
                // gauche, p.ex. -4 sur le retrait gauche hi/lo). 0 pour les lignes
                // normales → aucun effet (top/bottom/écran standard inchangés).
                int s = (cyc - ds) - shift;
                if (s < 0) s = 0; else if (s >= nDec) s = nDec - 1;
                dst[x] = stColorToArgb(pal[idx[s]]);       // dans la fenêtre → contenu
            } else {
                dst[x] = stColorToArgb(pal[0]);            // hors fenêtre / bordure → registre 0
            }
        }
        if (nPix > 0) addr += static_cast<uint32_t>(nPix / bytePerPix);  // adresse vidéo accumulée
    }
}

// Auto-test déterministe de la machine Glue (cf. déclaration). Chaque scénario
// injecte des écritures freq/res à des cycles exacts puis vérifie l'état calculé
// contre les valeurs Hatari. Affiche un récap sur stderr ; renvoie true si tout OK.
bool Shifter::glueSelfTest() {
    frameMode_ = Mode::Low;                       // STF 50 Hz basse rés
    frameSync_ = 0x02;
    resizeFor(frameMode_);
    const int cpl = geometry().cyclesPerLine;     // 512
    int pass = 0, fail = 0;

    // Rejoue une liste d'écritures {line, cycle, isRes(0=freq/1=res), val}.
    auto run = [&](std::vector<std::array<int,4>> writes) {
        syncWrites_.clear();
        for (const auto& v : writes)
            syncWrites_.push_back({ v[0] * cpl + v[1],
                                    static_cast<uint8_t>(v[3]), v[2] != 0 });
        replayGlue();
    };
    auto chk = [&](const char* name, long got, long want) {
        if (got == want) { ++pass; }
        else { ++fail; std::fprintf(stderr, "  FAIL %-26s got=%ld want=%ld\n", name, got, want); }
    };

    // 1. Bordure DROITE : 60 Hz @ cyc 374 puis 50 Hz @ 380, ligne 100.
    run({ {100,374,0,0x00}, {100,380,0,0x02} });
    chk("right DE_start", glueLines_[100].displayStartCycle, 56);
    chk("right DE_end",   glueLines_[100].displayEndCycle,   462);   // cpl-50 (RIGHT_OFF)
    chk("right mask",     (glueLines_[100].borderMask & glue::RIGHT_OFF) ? 1 : 0, 1);

    // 2. Bordure GAUCHE : hi-rés @ 2 puis lo-rés @ 6, ligne 100.
    run({ {100,2,1,0x02}, {100,6,1,0x00} });
    chk("left DE_start",  glueLines_[100].displayStartCycle, 4);     // HDE_On_Hi
    chk("left DE_end",    glueLines_[100].displayEndCycle,   376);
    chk("left mask",      (glueLines_[100].borderMask & glue::LEFT_OFF) ? 1 : 0, 1);

    // 3. RIGHT-2 (ligne 60 Hz) : 60 Hz @ 100 puis 50 Hz @ 400, ligne 100.
    run({ {100,100,0,0x00}, {100,400,0,0x02} });
    chk("right-2 DE_end", glueLines_[100].displayEndCycle,   372);   // HDE_Off_Low_60
    chk("right-2 mask",   (glueLines_[100].borderMask & glue::RIGHT_MINUS_2) ? 1 : 0, 1);

    // 4. Retrait HAUT : 60 Hz @ ligne 10, 50 Hz @ ligne 40.
    run({ {10,100,0,0x00}, {40,100,0,0x02} });
    chk("top nStartHBL",  glueStartHBL_, 34);                        // VDE_On_60
    chk("top NO_TOP",     (glueVOverscan_ & glue::VO_NO_TOP) ? 1 : 0, 1);
    chk("top nEndHBL",    glueEndHBL_, 263);                         // bas inchangé

    // 5. Retrait BAS : 60 Hz @ ligne 261.
    run({ {261,100,0,0x00} });
    chk("bottom nEndHBL", glueEndHBL_, 310);                         // VDE_Off_NoBottom_50

    // 6. Écran NORMAL (aucune écriture) : aucune bordure retirée.
    run({});
    chk("normal nStartHBL", glueStartHBL_, 63);
    chk("normal nEndHBL",   glueEndHBL_, 263);
    chk("normal DE_start",  glueLines_[100].displayStartCycle, 56);
    chk("normal DE_end",    glueLines_[100].displayEndCycle, 376);
    chk("normal trick",     bordersTrick_ ? 1 : 0, 0);

    // 7. STOP_MIDDLE : hi-rés @ cyc 100 (entre DE, ≤160), ligne 100.
    run({ {100,100,1,0x02}, {100,500,1,0x00} });
    chk("stopmid DE_end", glueLines_[100].displayEndCycle, 164);     // HDE_Off_Hi
    chk("stopmid mask",   (glueLines_[100].borderMask & glue::STOP_MIDDLE) ? 1 : 0, 1);

    std::fprintf(stderr, "[glue-selftest] %d OK, %d FAIL\n", pass, fail);
    return fail == 0;
}

// Décode toute la trame d'un coup (repli / appel direct hors ordonnanceur).
void Shifter::renderFrame() {
    beginFrame();
    for (int y = 0; y < curAH_; ++y) renderLine(y);   // lignes ACTIVES (bordures déjà posées)
}

uint8_t Shifter::read8(uint32_t addr) {
    // Palette $FF8240-$FF825F : 16 mots, big-endian.
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        syncCpuBus();          // wait state bus 4 cycles (lecture registre couleur)
        const int i = (addr - 0xFF8240) / 2;
        return (addr & 1) ? static_cast<uint8_t>(palette[i])
                          : static_cast<uint8_t>(palette[i] >> 8);
    }
    // Compteur d'adresse vidéo (lecture seule) : position courante du balayage.
    // $FF8205 = bits 16-23, $FF8207 = 8-15, $FF8209 = 0-7 (cf. Hatari
    // Video_ScreenCounter_ReadByte). Certains diagnostics (Test Kit) attendent
    // que ce compteur reflète la base vidéo + l'avance du faisceau.
    // Adresse de base vidéo ($FF8201/03, + octet bas STE $FF820D) : RELISIBLE —
    // le ST renvoie la dernière valeur écrite (Hatari : IoMem_ReadWithoutInterception).
    // Indispensable : les diagnostics RÉCUPÈRENT la base écran en relisant ces
    // registres pour calculer leur framebuffer (sans ça → base 0 → ils dessinent
    // sur la table des vecteurs et plantent).
    if (addr == 0xFF8201) return static_cast<uint8_t>(videoBase >> 16);
    if (addr == 0xFF8203) return static_cast<uint8_t>(videoBase >> 8);
    // Octet bas de la base vidéo $FF820D : STE seulement (sur ST il vaut toujours
    // 0, cf. Hatari Video_BaseLow_ReadByte).
    if (addr == 0xFF820D) return machineIsSte(bus_.machine) ? static_cast<uint8_t>(videoBase) : 0;
    // Une écriture du compteur pendant le DE est en attente (vcDelayedOffset_) : la
    // relecture doit déjà la refléter (port Video_ScreenCounter_ReadByte qui ajoute
    // VideoCounterDelayedOffset & ~1 à l'adresse calculée).
    if (addr == 0xFF8205) return static_cast<uint8_t>((videoCounter() + (vcDelayedOffset_ & ~1)) >> 16);
    if (addr == 0xFF8207) return static_cast<uint8_t>((videoCounter() + (vcDelayedOffset_ & ~1)) >> 8);
    if (addr == 0xFF8209) return static_cast<uint8_t>(videoCounter() + (vcDelayedOffset_ & ~1));
    // Synchro $FF820A : bits inutilisés 2-7 forcés à 1 (ST et STE), cf. Hatari
    // Video_Sync_ReadByte (IoMem[0xff820a] |= 0xfc). On NE masque PAS le champ
    // stocké `sync` : videoCounter() s'en sert toujours via `sync & 2`.
    if (addr == 0xFF820A) return static_cast<uint8_t>((sync & 0x03) | 0xFC);
    // Largeur de ligne STE $FF820F : 0 sur ST (cf. Hatari Video_LineWidth_ReadByte).
    if (addr == 0xFF820F) return machineIsSte(bus_.machine) ? lineWidth : 0;
    if (addr == 0xFF8260) { syncCpuBus(); return static_cast<uint8_t>(mode); }
    // Scroll fin : Hatari n'expose QUE $FF8265 en lecture (Video_HorScroll_Read).
    if (addr == 0xFF8265) { syncCpuBus(); return hwScrollCount; }
    // Tout autre registre routé mais non géré : zones « void » du shifter.
    // Port fidèle Hatari (ioMem.c IoMem_VoidRead/IoMem_VoidRead_00) :
    //  - STE/MegaSTE : $FF820B, $FF8262-63 et $FF8266-7F lisent 0x00
    //    (ioMemTabSTE.c IoMem_VoidRead_00) ; le reste (dont $FF820C/$FF820E)
    //    lit 0xFF (IoMem_VoidRead).
    //  - ST/MegaST : TOUTES les zones void lisent 0xFF (ioMemTabST.c).
    if (machineIsSte(bus_.machine) &&
        (addr == 0xFF820B || addr == 0xFF8262 || addr == 0xFF8263 ||
         (addr >= 0xFF8266 && addr <= 0xFF827F)))
        return 0x00;
    return 0xFF;
}

// Reconstruit l'adresse vidéo courante — port fidèle de Hatari Video_CalculateAddress :
//   addr = videoBase + ligne*bpl + NbBytes, avec NbBytes = ((X - LineStartCycle) >> 1) & ~1
// où X = cycles DANS la ligne et le shifter lit 2 cycles/octet entre LineStartCycle
// (56 en 50 Hz, 52 en 60 Hz ; 0 en haute rés) et LineEndCycle (376). Après la dernière
// ligne affichée, le compteur reste figé jusqu'au rechargement VBL. (L'ancienne version
// supposait 1 octet/cycle depuis le cycle 216 — faux en milieu de ligne, d'où l'échec
// du test « T0 » des diagnostics qui relisent $FF8205/07/09 au cycle près.)
uint32_t Shifter::videoCounter() const {
    if (!beamClock_) return videoBase & 0xFFFFFF;       // pas d'horloge → base brute
    int64_t fc = beamClock_();                          // cycles dans la trame
    fc += kVideoCounterReadOffsetCyc;                   // datation lecture façon Hatari (anti-flicker spec512)
    static const char* vco = std::getenv("NEOST_VC_OFF");   // DEBUG : offset ADDITIONNEL (relatif à la correction)
    if (vco) fc += std::atoi(vco);
    // Géométrie VERROUILLÉE de la trame (cycles/ligne et début DE dépendent de la
    // fréquence 50/60/71 Hz : avant, 512 et 56/52 étaient figés → compteur faux en
    // 60 Hz). frameSync_ est posé par beginFrame, comme frameMode_.
    const Geometry g = geometry();
    const int  kCyclesPerLine = g.cyclesPerLine;
    const bool hi   = (frameMode_ == Mode::High);
    const int  bpl  = hi ? 80 : 160;                    // octets/ligne affichée
    const int  disp = g.displayLines;                   // lignes affichées
    const int  lineStart = g.lineStartCycle;            // début Display-Enable (50/60/71 Hz)
    // Stride réel d'une ligne = octets affichés + line-offset STE ($FF820F, en mots)
    // + 1 mot PAR PLAN si scroll fin avec prefetch (cf. scrollCounterAdvance).
    // lineWidth=0 et scroll=0 sur ST/STF → bpl.
    const int  stride = bpl + static_cast<int>(lineWidth) * 2 + scrollCounterAdvance();
    // Compteur MATÉRIALISÉ (≙ pVideoRaster) : vcLineBase_ = début de la ligne active
    // vcLineY_ (les lignes déjà rendues ont déjà accumulé leur stride réel — lineWidth
    // variable, écritures du compteur, scroll). L'affichage couvre les lignes
    // [dispStart, dispStart+disp) (VDE_On..VDE_Off) ; avant (bordure HAUTE) le
    // compteur reste à la base latchée ; pendant, il vaut base de ligne + offset
    // intra-ligne ; après (bordure BASSE), il reste figé sur l'écran entièrement lu.
    // VDE_On LIVE (cf. liveStartHBL_) : reflète un retrait de bordure HAUTE en cours
    // (bascule 60 Hz dans la bordure haute → 34) → le compteur monte plus tôt, comme
    // sur le vrai matériel (Hatari nStartHBL). Un écran 50 Hz normal garde 63.
    const int  dispStart = liveStartHBL_;
    const int  line = static_cast<int>(fc / kCyclesPerLine);
    const int  X    = static_cast<int>(fc % kCyclesPerLine);
    const int  la   = line - dispStart;                 // index de ligne active du faisceau
    uint32_t addr = vcLineBase_;
    if (la >= 0) {
        // Ligne au-delà de celle du compteur matérialisé (rendu pas encore passé) :
        // extrapole au stride courant. Bordure basse : figé à l'écran entièrement lu.
        const int laEff = la < disp ? la : disp;
        if (laEff > vcLineY_)
            addr += static_cast<uint32_t>(laEff - vcLineY_) * static_cast<uint32_t>(stride);
        // Offset intra-ligne UNIQUEMENT si la ligne courante n'a pas déjà été rendue
        // (la < vcLineY_ = bordure droite : le stride de la ligne est déjà accumulé).
        if (la < disp && laEff >= vcLineY_) {
            // Fenêtre DE RÉELLE de la ligne courante (machine Glue LIVE) : une bascule
            // 60/50 Hz mi-ligne déplace la fin du DE (right-2, stop-middle, retraits) et
            // le compteur doit le refléter EN DIRECT — port de Video_CalculateAddress,
            // qui lit ShifterLines[HBL].DisplayStartCycle/EndCycle. C'est ce que mesure
            // la calibration fullscreen d'Enchanted Land sur $FF8209 (impulsion 60→50
            // enjambant le comparateur 372 → ligne -2 octets). Trame SANS écriture
            // freq/res → chemin historique inchangé (zéro régression).
            int ds = lineStart, lineBytes = bpl;
            if (frameMode_ != Mode::High && !syncWrites_.empty()
                && static_cast<std::size_t>(line) + 1 < glueLines_.size()) {
                const_cast<Shifter*>(this)->liveGlueCatchUp(line);
                const GlueLine& L = glueLines_[static_cast<std::size_t>(line)];
                if (L.displayStartCycle >= 0) {
                    ds = L.displayStartCycle;
                    lineBytes = (L.displayEndCycle - L.displayStartCycle) >> 1;
                    if (lineBytes < 0) lineBytes = 0;
                }
            }
            int nb = (X - ds) >> 1;                     // 2 cycles par octet
            nb &= ~1;                                   // le shifter lit par MOTS
            if (nb < 0) nb = 0; else if (nb > lineBytes) nb = lineBytes;
            addr += static_cast<uint32_t>(nb);
        }
    }
    // ⚠ RESTART du compteur en fin de trame (Video_RestartVideoCounter : rechargement
    // depuis $FF8201/03 au HBL 310/260, cycle 56/60, AVANT le VBL — ULM DSOTS) :
    // VOLONTAIREMENT NON PORTÉ. Tenté puis retiré : les logiciels qui se synchronisent
    // en pollant « compteur revenu à la base » (ex. make_overscan_test) sortent alors
    // de leur poll à la ligne 310 au lieu du VBL et posent leur bascule 60/50 Hz PILE
    // à la frontière de trame — or beginFrame VERROUILLE la géométrie par trame : un
    // registre sync à 60 Hz à cet instant bascule TOUTE la trame en 263 lignes
    // (étalon overscan_top : 0 détection de bordure, 8 % de diff). Hatari n'a pas ce
    // problème (machine d'état continue, ligne à ligne). À reporter avec le chantier
    // « géométrie par ligne / bascule 50-60 Hz en cours de trame » (cf. inventaire).
    return addr & 0xFFFFFF;
}

// Écriture du compteur vidéo $FF8205/07/09 (STE/TT) — port fidèle de
// Video_ScreenCounter_WriteByte (video.c:5145-5250). On reconstruit la nouvelle
// adresse en remplaçant UN octet de la valeur courante (corrigée d'une éventuelle
// modification déjà différée), puis :
//   • affichage pas commencé sur la ligne (cycle ≤ MMUStart = HDE_On − 16 si scroll),
//     ligne déjà rendue (bordure droite — notre rendu à DE_end est déjà passé, ce qui
//     équivaut au pVideoRasterDelayed d'Hatari appliqué en fin de ligne), ou faisceau
//     hors zone affichée → application IMMÉDIATE au compteur matérialisé ;
//   • écriture PENDANT le DE → on mémorise l'ÉCART (vcDelayedOffset_), appliqué à la
//     fin de la ligne (endVideoLine) — sur un vrai STE cela produit des artefacts,
//     l'adresse de fin de ligne est en revanche exacte. Étalons : Stardust Tunnel STE,
//     Braindamage End Part.
void Shifter::writeVideoCounterByte(uint32_t addr, uint8_t v) {
    // Octet haut limité comme la base/DMA : adresses vidéo ≤ $3FFFFF sur les machines
    // ≤ 4 Mo (port DMA_MaskAddressHigh — NeoST plafonne la ST-RAM à 4 Mo).
    if (addr == 0xFF8205) v &= 0x3F;
    const uint32_t cur = videoCounter();                       // adresse courante (brute)
    uint32_t an = (cur + static_cast<uint32_t>(vcDelayedOffset_)) & 0xFFFFFFu;
    if (addr == 0xFF8205)      an = (an & 0x00FFFFu) | (uint32_t(v) << 16);
    else if (addr == 0xFF8207) an = (an & 0xFF00FFu) | (uint32_t(v) << 8);
    else                       an = (an & 0xFFFF00u) | uint32_t(v);

    int line = 0, cyc = 0;
    const Geometry g  = geometry();
    const bool havePos = beamPos(line, cyc);
    const int  la      = havePos ? line - liveStartHBL_ : -1;
    const bool active  = havePos && la >= 0 && la < g.displayLines;
    // Le MMU commence à lire 16 cycles AVANT le HDE_On quand le scroll fin est armé
    // AVEC PREFETCH ($FF8265 ; rien via $FF8264) — port Video_GetMMUStartCycle.
    const int mmuStart = g.lineStartCycle - ((hwScrollCount && hwScrollPrefetch) ? 16 : 0);
    if (!havePos || !active || cyc <= mmuStart || la < vcLineY_) {
        vcLineBase_ = an;                                      // application immédiate
        vcDelayedOffset_ = 0;
    } else {
        vcDelayedOffset_ = static_cast<int>(an) - static_cast<int>(cur);  // pendant le DE → fin de ligne
    }
}

void Shifter::write8(uint32_t addr, uint8_t v) {
    // Adresse de base vidéo : octets haut ($FF8201) et milieu ($FF8203). Le bit
    // bas est fixé à 0 (le ST aligne le framebuffer sur 256 octets).
    const bool ste = machineIsSte(bus_.machine);
    switch (addr) {
        case 0xFF8201:
            videoBase = (videoBase & 0x00FF00) | (uint32_t(v) << 16);
            // STE/TT : écrire l'octet haut/milieu remet à 0 l'octet bas $FF820D
            // (cf. Hatari Video_ScreenBase_WriteByte).
            if (ste) videoBase &= 0xFFFF00;
            return;
        case 0xFF8203:
            videoBase = (videoBase & 0xFF0000) | (uint32_t(v) << 8);
            if (ste) videoBase &= 0xFFFF00;
            return;
        case 0xFF820A: recordSyncWrite(false, v); sync = v; return;   // synchro 50/60 Hz (+ détection bordures)
        // Compteur vidéo $FF8205/07/09 : INSCRIPTIBLE sur STE/TT seulement (port
        // Video_ScreenCounter_WriteByte) — immédiat hors affichage, différé sinon.
        case 0xFF8205: case 0xFF8207: case 0xFF8209:
            if (ste) writeVideoCounterByte(addr, v);
            return;
        // Octet bas de la base vidéo $FF820D (STE) : bit0 ignoré (aligné pair).
        case 0xFF820D:
            if (ste) videoBase = (videoBase & 0xFFFF00) | (uint32_t(v) & ~1u);
            return;
        // Largeur de ligne STE $FF820F — port Video_LineWidth_WriteByte : applicable
        // IMMÉDIATEMENT si le Display-Enable de la ligne courante n'est pas terminé
        // (ou faisceau hors zone affichée) ; sinon DIFFÉRÉ à la fin de la ligne
        // (NewLineWidth, cf. endVideoLine). Étalon : Pacemaker (bump mapping).
        case 0xFF820F:
            if (ste) {
                int line = 0, cyc = 0;
                const Geometry g = geometry();
                const int la = beamPos(line, cyc) ? line - liveStartHBL_ : -1;
                const bool active = la >= 0 && la < g.displayLines;
                if (!active || cyc <= g.lineEndCycle) { lineWidth = v; newLineWidth_ = -1; }
                else                                  { newLineWidth_ = v; }
            }
            return;
        case 0xFF8260: syncCpuBus(); recordSyncWrite(true, v); mode = static_cast<Mode>(v & 0x3); return;  // résolution (+ bordures + wait state bus)
        // Scroll fin horizontal STE — port Video_HorScroll_Write : $FF8264 sans
        // prefetch, $FF8265 avec. Applicable IMMÉDIATEMENT si l'affichage de la ligne
        // courante n'a pas commencé (cycle ≤ HDE_On, ou faisceau hors zone affichée) ;
        // sinon la nouvelle valeur est DIFFÉRÉE à la fin de la ligne (NewHWScrollCount,
        // cf. endVideoLine). Étalons : Mindrewind, Digiworld 2, cool_ste.
        case 0xFF8264: case 0xFF8265:
            syncCpuBus();
            if (ste) {
                const uint8_t sc       = v & 0x0F;
                const bool    prefetch = (addr == 0xFF8265);
                int line = 0, cyc = 0;
                const Geometry g = geometry();
                const int la = beamPos(line, cyc) ? line - liveStartHBL_ : -1;
                const bool active = la >= 0 && la < g.displayLines;
                if (!active || cyc <= g.lineStartCycle) {
                    hwScrollCount = sc; hwScrollPrefetch = prefetch; newHwScrollCount_ = -1;
                } else {
                    newHwScrollCount_ = sc; newHwScrollPrefetch_ = prefetch;
                }
            }
            return;
        default: break;
    }
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        syncCpuBus();          // wait state bus 4 cycles AVANT de dater l'écriture
        const int i = (addr - 0xFF8240) / 2;
        uint16_t col;
        if (bus_.ioAccessWidth() == 1) {
            // Quirk matériel (port Video_ColorReg_WriteWord) : sur une écriture
            // OCTET, le 68000 pose l'octet sur les DEUX moitiés du bus de données
            // et le Shifter latche le MOT entier → l'octet est dupliqué, que
            // l'adresse soit paire ou impaire (move.b #$07,$FF8240 → couleur $707).
            col = uint16_t((uint16_t(v) << 8) | v);
        } else {
            // Écriture mot/long : le bus la découpe en deux write8 (big-endian).
            col = (addr & 1) ? uint16_t((palette[i] & 0xFF00) | v)
                             : uint16_t((palette[i] & 0x00FF) | (uint16_t(v) << 8));
        }
        // La couleur est STOCKÉE masquée — palette ST 512 couleurs ($777) ou STE
        // 4096 ($FFF) : des jeux écrivent $FFFF et RELISENT pour détecter le STE.
        palette[i] = col & (ste ? 0x0FFF : 0x0777);
        recordColorWrite(i);   // spec512 : date l'écriture au cycle ALIGNÉ (palette intra-ligne)
    }
    // Tout autre registre nouvellement routé mais non géré ($FF8266-$FF827F) :
    // écriture sans effet (no-op), comme les zones « void » du shifter.
}
