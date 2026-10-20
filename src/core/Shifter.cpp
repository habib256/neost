// =============================================================================
//  Shifter.cpp — Décodage planaire ST (basse/moyenne/haute) → buffer ARGB.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/ShifterInternal.hpp"
#include "core/VideoGlue.hpp"
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
// Hatari instr_end−8). Indissociable de applyShifterBusAlignment (sans le recalage des
// wait states bus, la position dérive de −2 cyc/ligne).
// **−25 (2026-07-03) = modèle spec512.c EXACT.** Hatari stocke la position d'écriture
// 4-alignée (bus) et l'applique par CELLULES de 4 cycles : premier pixel affecté =
// cyc_hatari − 84 (50 Hz : pré-spans (56−0)/4 + 7 = 21 → count 84 au 1ᵉʳ pixel visible,
// cf. Spec512_StartScanLine). Les écritures NeoST étant TOUTES ≡2 mod 4 (syncCpuBus,
// convention −2 vs Hatari), −25 place le front à c = w−82 = (w+2)−84 ≡ 0 mod 4 —
// quantification de cellule REPRODUITE par le seul offset, sans code de plus.
// VALIDÉ : diaporama étalon spectrum_512_auto_diapo — les **10** images du cycle à
// **0 px** vs oracle Hatari fastfdc aligné trame-à-trame (l'ancien −23 laissait
// 350/23/415/666/15/6 px sur 6 des 10 : marges d'index serrées autour des fronts ;
// les traces NEOST_SPEC512_TRACE ↔ TRACE_VIDEO_COLOR matchaient déjà à Δ=+2 constant
// sur les 9552 écritures/trame → seule la règle de quantification différait).
static constexpr int kSpec512AlignCyc = -25;

// Lecture d'un verrou d'environnement booléen, avec la MÊME règle partout :
// variable absente → défaut, sinon « 0 » (ou toute valeur nulle) = OFF. Les trois
// sites NEOST_LINELEN de ce fichier testaient la seule PRÉSENCE de la variable,
// alors que Machine.cpp lit sa valeur : NEOST_LINELEN=0, censé désactiver le
// mécanisme pour l'A/B, désactivait la moitié Machine et ACTIVAIT la moitié
// Shifter — l'A/B mesurait donc un hybride jamais validé.

// A16 (2026-08-27) — le tranchage du verrou NEOST_LINELEN, instruit par la mesure.
// Historique : jusqu'au 2026-07-08 tous les sites (Machine + Shifter) partageaient
// UNE variable, OFF par défaut. Le tranchage WS3 (45f9a65) a basculé le défaut à ON
// côté Machine SEULEMENT — les quatre sites de ce fichier sont restés OFF, et la
// production exécute depuis un couple Machine-ON/Shifter-OFF… qui est en réalité LA
// configuration validée par la suite (étalons TOUS OK, avant comme aujourd'hui).
// Tenté le 2026-08-27 : unifier le défaut à ON → le glue-selftest SEGFAULTE
// (SIGSEGV, reproduit aussi sur le code d'avant avec NEOST_LINELEN=1 — la recette
// d'A/B documentée crashait donc déjà). Les deux moitiés sont DEUX fonctionnalités :
//  · NEOST_LINELEN (défaut ON, =0 pour l'A/B) — le canal HBL_Pos/nCyclesPerLine
//    côté Machine, validé WS3. Lecteur unique : lineLenEnv(), partagé avec
//    Machine.cpp (lineLenOn_).
//  · NEOST_LINELEN_ATTR (défaut ON depuis le 2026-09-01) — l'ATTRIBUTION à la
//    grille réelle des débuts de ligne (glueLineStart_) côté Shifter : le chantier
//    V3, CLOS à l'oracle (cf. la fin de ce bandeau). =0 rétablit la grille fixe
//    frameCycle/512 pour un A/B — et fait rougir l'étalon freq_switch.
// Séparer les variables ferme le piège : poser NEOST_LINELEN=1 pour un A/B
// n'arme plus silencieusement un chemin expérimental non validé.
//
// A16b (2026-08-28) — le SEGFAULT est corrigé, et la cause n'était pas dans le
// selftest. L'hypothèse portée au TODO (« glueLineStart_ vraisemblablement vide ou
// désynchronisé ») est CONFIRMÉE à la ligne près par ASan+UBSan :
//     runtime error: reference binding to null pointer of type 'long long'
//     AddressSanitizer: SEGV on unknown address 0x000000000000 (READ)
//       #0 Shifter::liveGlueCatchUp(int)   Shifter.cpp:537
//       #1 Shifter::liveLineDisplayed(int) Shifter.cpp:1259
//       #2 Shifter::glueSelfTest()         Shifter.cpp:2219
// glueLineStart_ était VIDE — donc data() == nullptr — parce que seul beginFrame()
// le dimensionnait, et que replayGlue() redimensionnait glueLines_ SEUL. Le
// glue-selftest appelle replayGlue() sans beginFrame() : il tombait dessus à coup
// sûr. Mais le même trou existait EN PRODUCTION, silencieusement, pour une trame
// dont lpf change en cours de route — cas que le commentaire de Shifter::serialize
// admet explicitement. Le correctif tient l'invariant dans replayGlue (cf. la note
// A16b là-bas) ; l'auto-test `glue_selftest_attr` du manifeste le garde armé.
// V3 CLOS le 2026-09-01. Le « vert avec le verrou armé » ne prouvait rien, et pour
// une raison qu'on ne soupçonnait pas : le verrou NE FAISAIT RIEN. Sous
// NEOST_LINELEN_ATTR la longueur de ligne retombait à cpl à chaque ligne et n'était
// corrigée que par un Freq_match tombant SUR la ligne — 140 lignes de 60 Hz sans
// écriture restaient à 512, la grille réelle ne dérivait jamais. Exhibiteur construit
// exprès (tools/make_freqswitch_test.py, étalon `freq_switch`) : plage de 140 lignes
// 60 Hz (dérive 560 cyc) puis bascules alternées. Trace `video_sync` d'Hatari contre
// `[attr]` (NEOST_VARLINE_TRACE) : avec la longueur de base posée par l'état freq/res
// au début de ligne (glueLineLenFor, ≙ Video_StartHBL → nCyclesPerLine), NeoST
// attribue les 18 écritures de la trame à la MÊME ligne et au MÊME cycle qu'Hatari
// (169/336 174/268 … 242/508 247/468) ; la grille fixe en manque 17 sur 18, d'une à
// deux lignes. Palier pixel entier à 0 px avec le canal armé : rien d'existant ne
// bouge, seul l'exhibiteur le voit — c'est ce qu'il fallait prouver.
bool Shifter::lineLenEnv() {
    static const bool on = envFlag("NEOST_LINELEN", true);
    return on;
}


// Seuil de détection « image spec512 » : nombre d'écritures palette MOT par trame
// à partir duquel on bascule sur le re-rendu intra-ligne. **1 = le défaut Hatari**
// (configuration.c:769 nSpec512Threshold = 1 ; spec512.c:222 compare en >=) : la
// PREMIÈRE écriture palette de la trame suffit — toute trame qui touche la palette
// est rendue palette-par-pixel, datation −25/alignement bus compris. C'est ce qui
// rend l'écriture palette mid-ligne byte-exacte face à l'oracle même HORS image
// Spectrum 512 (banc poll : bascule de couleur à x=49..61 selon le cycle d'écriture,
// l'ancien seuil 512 laissait ces trames au rendu ligne-à-ligne → bascule figée au
// début d'aire active, ~1330 px/trame de résidu). L'ancien seuil élevé (512 mots,
// « ne jamais toucher les trames ordinaires ») divergeait de Hatari par principe.
static constexpr int kSpec512Threshold = 1;

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
// Datation des LECTURES du compteur vidéo $FF8205/07/09 — VALEUR FIDÈLE
// THÉORIQUE **−6** (2026-07-03) = la dérivation « fin d'accès − 8 » de
// Video_CalculateAddrees (doc convergence §8, cible #1). L'ancien −14
// « calibré à l'oracle » (2026-07-02, banc poll) était CO-CALIBRÉ avec
// l'ancienne datation write (−6) autour d'un résidu commun de +8 : les DEUX
// datations ont été ramenées ENSEMBLE (+8 chacune) à leurs valeurs fidèles
// de la table §8 (read −6, write +2). Mesure décisive (menu robot Cuddly,
// oracle video_addr + cpu_disasm) : chemin CPU inter-trame et ancre VBL
// IDENTIQUES à Hatari, mais valeur $8209 lue 4-6 octets PLUS PETITE au même
// instant → la sortie du synchroniseur (pc=f264, octet bas > $40 en SIGNÉ)
// glissait de L34 à L36 ~1 trame/10 → clignotement vertical bistable.
// Validé ENSEMBLE : Cuddly 250/250 verrouillées (225/250 avant), EL top-trick
// 40/40, LX titre propre, SHO byte-identique, étalons 19/19 + TOUS OK.
// NEOST_VC_OFF ajuste pour le diagnostic/A/B (−8 → ancien −14).

// La machine GLUE (masques de bordure, table de timings, wakeup state) vit
// désormais dans core/VideoGlue.hpp — cf. A32.

Shifter::Shifter(Bus& bus) : bus_(bus) {
    resizeFor(mode);
}

// Remise à zéro au RESET machine — port fidèle de Video_Reset (video.c:810).
// Hatari remet : la base vidéo (VideoBase = 0), les registres STE (LineWidth,
// HWScrollCount) et leurs modifications DIFFÉRÉES (NewLineWidth/NewHWScrollCount
// = −1, VideoCounterDelayedOffset = 0), l'état mémorisé de la Glue
// (ShifterFrame.Freq/Res = −1) et les lignes par-trame (Video_InitShifterLines).
// Video_Reset_Glue (video.c:904) remet en plus freq $FF820A = 0 et la résolution
// à basse (moniteur couleur — le mono est piloté ailleurs, bit7 GPIP).
// ⚠ Video_Reset ne touche PAS la palette : les registres couleur survivent au
// reset (vérifié : aucun accès à HBLPalettes/IoMem $FF8240 dans Video_Reset).
void Shifter::reset() {
    videoBase = 0;                       // VideoBase = 0L
    // Video_Reset_Glue : freq et résolution à 0 (le RESET 68000 remet la Glue à
    // 60 Hz / basse rés ; TOS reprogramme aussitôt).
    sync = 0;
    mode = Mode::Low;
    // Registres STE + modifications différées annulées (« cancel pending
    // modifications set before the reset », video.c:850-853).
    lineWidth            = 0;
    hwScrollCount        = 0;
    hwScrollPrefetch     = false;
    hwScrollReg8264_     = 0;
    steBorderFlag_       = false;
    newSteBorderFlag_    = false;
    lastScroll8265Cycle_ = -1;
    newLineWidth_        = -1;
    newHwScrollCount_    = -1;
    newHwScrollPrefetch_ = false;
    vcDelayedOffset_     = 0;
    // Résolution côté Shifter (ShifterFrame.Res_Shifter = −1, ResPosStop_Shifter
    // effacé, VideoRasterDelayedInc et DisplayPixelShift par ligne remis à 0).
    shifterRes_       = static_cast<uint8_t>(mode);
    lastShifterRes_   = -1;
    shifterResCyc_    = -1;
    shifterResLine_   = -1;
    stopFromFc_       = -1;
    lastStopFromFc_   = -1;
    lastStopToFc_     = -1;
    vcStopInc_        = 0;
    shifterStopLine_  = -1;
    shifterStopShift_ = 0;
    // ShifterFrame.Freq/Res = −1 (video.c:824-825) : le filtre « même valeur
    // ignorée » de recordSyncWrite repart neutre — la 1ʳᵉ écriture freq/res
    // après reset est TOUJOURS traitée.
    lastGlueFreq_ = -1;
    lastGlueRes_  = -1;
    // Compteur vidéo matérialisé (≙ pVideoRaster = &STRam[0]) + restart fin de trame.
    vcFrameBase_   = 0;
    vcLineBase_    = 0;
    vcLineY_       = 0;
    vcRestartBase_ = -1;
    vcRestartLine_ = 0;
    // État glue PAR-TRAME (≙ Video_InitShifterLines + Video_ClearOnVBL) : écritures
    // datées purgées, curseurs live remis, bordures nominales, spec512 désarmé.
    // beginFrame ré-écrase tout au prochain début de trame ; on garantit ici un
    // état cohérent pour les lectures $FF8205/07/09 d'ici là.
    syncWrites_.clear();
    colorWrites_.clear();
    paletteAccesses_ = 0;
    spec512Active_   = false;
    bordersTrick_    = false;
    frameMode_ = mode;
    frameSync_ = sync;
    resizeFor(frameMode_);
    const Geometry g = geometry();
    liveStartHBL_   = g.dispStartLine;
    glueStartHBL_   = g.dispStartLine;
    glueEndHBL_     = g.dispStartLine + g.displayLines;
    glueVOverscan_  = 0;
    glueBlankLines_ = 0;
    glueHblPos_     = -1;
    glueCyclesLine_ = -1;
    nScreenRefreshRate_ = (sync & 0x02) ? 50 : 60;
    liveGlueLine_   = -1;
    liveGlueWi_     = 0;
    liveGlueRes_    = 0;
    liveGlueFreq50_ = (sync & 0x02) ? 1 : 0;
    liveGlueLen_    = g.cyclesPerLine;
    std::fill(glueLines_.begin(), glueLines_.end(), GlueLine{ -1, 0, 0, 0 });
    std::fill(glueLineStart_.begin(), glueLineStart_.end(), int64_t{0});
    std::fill(lineSnapLen_.begin(), lineSnapLen_.end(), static_cast<uint16_t>(0));
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

Shifter::FrameGeometry Shifter::geometryFor(Mode m) {
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
    FrameGeometry g;
    g.x  = border ? kBorderLeftPx   : 0;
    g.y  = border ? kBorderTopLines : 0;
    g.w  = border ? (kBorderLeftPx + aw + kBorderRightPx) : aw;
    g.h  = border ? (kBorderTopLines + ah + kBorderBotLines) : ah;
    g.ah = ah;
    return g;
}

void Shifter::resizeFor(Mode m) {
    const FrameGeometry g = geometryFor(m);
    activeX_ = g.x;
    activeY_ = g.y;
    const int w = g.w;
    const int h = g.h;
    curAH_ = g.ah;
    // La taille du tampon fait partie du test : sans elle, une géométrie restaurée
    // d'un save-state qui coïncide avec celle du mode courant sauterait la
    // réallocation et laisserait frame_ sous-dimensionné (cf. l'invariant posé
    // dans Shifter::serialize).
    if (w == curW_ && h == curH_ &&
        frame_.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) return;
    curW_ = w; curH_ = h;
    frame_.assign(static_cast<std::size_t>(w) * h, 0xFF000000u);
}

// Verrouille la résolution ET la fréquence de la trame : le décodage ligne à
// ligne s'y tient (un changement de $FF8260 ou $FF820A en cours de trame ne prend
// effet qu'à la suivante, comme l'ancien renderFrame qui figeait la rés. au moment
// du décodage). La géométrie de la trame (cycles/ligne, lignes/trame) découle de
// ce couple — cf. geometry() — et est lue par Machine juste après cet appel.
void Shifter::beginFrame() {
    lastScroll8265Cycle_ = -1;   // la paire d'écritures doit appartenir à la même VBL
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
    sideTrick_    = false;
    sideTrickLines_ = 0;
    // VDE_On live du compteur vidéo : valeur nominale selon la fréquence verrouillée
    // (50 Hz → 63, 60 Hz → 34). En MONO (haute rés), Hatari force nStartHBL =
    // VIDEO_START_HBL_71HZ = 34 QUELLE QUE SOIT la fréquence du registre $FF820A
    // (Video_ResetShifterTimings, video.c:4498-4504 : haute rés ⇒ 71 Hz ⇒ 34) —
    // cohérent avec geometryFor(High) qui pose déjà dispStartLine = 34. Les
    // bascules freq de la trame peuvent l'avancer (retrait bordure haute) via
    // updateLiveStartHBL.
    liveStartHBL_ = (frameMode_ == Mode::High) ? 34
                                               : ((frameSync_ & 0x02) ? 63 : 34);
    // Compteur vidéo matérialisé : LATCH de la base au début de trame (port
    // Video_ClearOnVBL → Video_RestartVideoCounter : pVideoRaster = &STRam[VideoBase]).
    // Une écriture $FF8201/03 en cours de trame ne réapparaîtra qu'ici, à la trame
    // suivante — comme sur le vrai matériel. Les écritures différées (NewHWScrollCount,
    // NewLineWidth, offset compteur) ne sont PAS effacées : Hatari ne les purge qu'au
    // reset vidéo, elles s'appliquent à la prochaine fin de ligne active.
    // Base de trame : si le RESTART fin-de-trame (ligne 310/260, port
    // Video_RestartVideoCounter) a eu lieu, c'est SA base (relue à cet instant)
    // qui fait foi — une écriture $FF8201/03 entre la ligne 310 et la ligne 0
    // n'est PAS reprise (comme Hatari). Sinon (mono, ou freq inadaptée à la
    // ligne de restart), latch classique au début de trame.
    vcFrameBase_ = ((vcRestartBase_ >= 0) ? static_cast<uint32_t>(vcRestartBase_)
                                          : videoBase) & kVideoAddrMask;
    // Scroll hard « Shifter arrêté » : comme le DisplayPixelShift d'Hatari
    // (Video_InitShifterLines à chaque VBL), le décalage ne vaut que pour la trame
    // où le stop a eu lieu. ⚠ Hatari note lui-même (TODO video.c:1915) que sur le
    // vrai STF le décalage devrait PERSISTER aux trames suivantes ; faute de mesure
    // matérielle, on garde le comportement de la référence.
    shifterStopLine_  = -1;
    shifterStopShift_ = 0;
    // Les positions du dernier changement de résolution Shifter sont en cycles de
    // la trame PRÉCÉDENTE : la ligne n'a plus de sens (un stop à la ligne 100 de la
    // trame k ne se compare pas à la ligne 100 de la trame k+1), et un arrêt qui
    // chevauche le VBL repart du cycle 0 de la nouvelle trame.
    shifterResLine_  = -1;
    if (stopFromFc_ >= 0) stopFromFc_ = 0;
    lastStopFromFc_  = -1;
    lastStopToFc_    = -1;
    vcRestartBase_ = -1;
    vcLineBase_  = vcFrameBase_;
    vcLineY_     = 0;
    commitAnchor_ = -1;   // ancre verticale du commit : re-latchée au 1er commit
    // Fond bordure : remplit tout le buffer overscan avec la couleur de bordure
    // (registre 0) au début de trame. Les lignes actives écrasent leur zone ; les
    // bordures haut/bas et les côtés non réécrits restent à cette couleur. (Phase 1 :
    // couleur de bordure figée à la trame ; les barres raster en bordure haut/bas
    // viendront avec le retrait de bordures et le suivi du registre 0 par ligne.)
    if (bordered()) {
        const uint32_t bg = stColorToArgb(palette[0]);
        std::fill(frame_.begin(), frame_.end(), bg);
    }
    // Base du latch bord gauche : la 1ʳᵉ ligne active prend le registre 0 de début de
    // trame (aucune ligne active « précédente » encore). Réamorcé à chaque trame.
    leftBorderPal0_ = palette[0];
    // Machine Glue LIVE : prépare l'état par-ligne de la nouvelle trame. Mêmes
    // structures que replayGlue — qui ré-écrase TOUT en fin de trame à partir des
    // mêmes syncWrites_, donc live et replay donnent le même résultat ; entre-temps
    // videoCounter() peut consulter la fenêtre DE réelle de la ligne courante.
    if (frameMode_ != Mode::High) {
        const Geometry g = geometry();
        glueLines_.assign(static_cast<std::size_t>(g.linesPerFrame) + 2, GlueLine{ -1, 0, 0, 0 });
        // Capture par ligne (cf. lineSnap_) : remise à zéro des longueurs — le gros
        // tampon d'octets n'a pas besoin d'être effacé (len fait foi).
        lineSnapLen_.assign(glueLines_.size(), 0);
        lineSnap_.resize(glueLines_.size() * static_cast<std::size_t>(kLineSnapBytes));
        lineScrollSnap_.assign(glueLines_.size(), 0);   // scroll fin STE par ligne (cf. renderGlueFrame)
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
    // LINELEN : échelle des débuts de ligne réels (remplie au fil des avances).
    glueLineStart_.assign(glueLines_.size(), 0);
    liveGlueLen_ = geometry().cyclesPerLine;
}

// Décode les index de palette (ou bit mono) d'UNE scanline dans `idx`, selon la
// résolution VERROUILLÉE de la trame. Renvoie le décalage scroll fin STE.
int Shifter::decodeLineIndices(int y, uint8_t* idx) const {
    const int  W      = activeWidth() + ((steBorderFlag_ && bordered()) ? 16 : 0);
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
    uint32_t base = (y == vcLineY_) ? vcLineBase_
                                    : vcFrameBase_ + static_cast<uint32_t>(y) * stride;
    // En moyenne résolution, la fenêtre hôte commence au pixel actif : les
    // 8 octets supplémentaires à gauche restent hors du framebuffer.
    if (steBorderFlag_ && frameMode_ == Mode::Medium) base += 8;

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
        // Chaque mot est adressé modulo 4 Mo (compteur 22 bits, cf. kVideoAddrMask —
        // port video_memcpy_mmu) : une ligne qui chevauche $3FFFFF reprend en $000000.
        const uint32_t a  = base + static_cast<uint32_t>(g) * groupB;
        const uint16_t p0 = bus_.read16(a & kVideoAddrMask);
        const uint16_t p1 = planes > 1 ? bus_.read16((a + 2) & kVideoAddrMask) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16((a + 4) & kVideoAddrMask) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16((a + 6) & kVideoAddrMask) : 0;
        // Dé-entrelacement des plans par TABLE, 8 pixels d'un coup (cf. kSpread) :
        // la boucle bit à bit qu'il y avait ici pesait ~10 % des instructions du
        // programme au profil callgrind — c'est le point chaud n°1 de la vidéo, appelé
        // pour chaque groupe de 16 px de chaque ligne affichée de chaque trame.
        // Chaque octet de kSpread[b] vaut 0 ou 1 : les décalages de 1, 2 et 3 restent
        // donc CONFINÉS à leur octet (valeur max 8), sans retenue d'un octet sur
        // l'autre — c'est ce qui permet de composer les quatre plans en une seule
        // opération 64 bits, et ce qui la rend indépendante du boutisme de l'hôte.
        for (int half = 0; half < 2; ++half) {
            const int sh = half ? 0 : 8;               // octet fort d'abord (px croissants)
            const uint64_t v = kSpread[(p0 >> sh) & 0xFF]
                             | (kSpread[(p1 >> sh) & 0xFF] << 1)
                             | (kSpread[(p2 >> sh) & 0xFF] << 2)
                             | (kSpread[(p3 >> sh) & 0xFF] << 3);
            std::memcpy(idx + px, &v, 8);
            px += 8;
        }
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));   // bord gauche couleur 0
    return scroll;
}

// Avance SUPPLÉMENTAIRE du compteur vidéo par ligne due au scroll fin (port des
// `pVideoRaster += n*2` de Video_CopyScreenLine*) : avec PREFETCH ($FF8265) le
// shifter a consommé 1 mot PAR PLAN de plus (8 octets en basse rés, 4 en moyenne) ;
// sans prefetch ($FF8264) : rien. En mono, Hatari avance toujours d'1 mot.
int Shifter::steBorderBytes(uint32_t bm) {
    // Priorités des branches de recopie Hatari video.c:4020-4075. Le retrait
    // court STE LOW se cumule avec +8 octets, pas le retrait MED ni LEFT_PLUS_2.
    return (bm & (glue::LEFT_OFF | glue::LEFT_OFF_MED | glue::LEFT_OFF_2_STE_MED
                  | glue::LEFT_PLUS_2)) ? 0 : 8;
}

int Shifter::scrollCounterAdvance(int scanline) const {
    if (steBorderFlag_ && frameMode_ != Mode::High) {
        const uint32_t bm = scanline >= 0 && scanline < static_cast<int>(glueLines_.size())
            ? glueLines_[scanline].borderMask : 0;
        return steBorderBytes(bm);
    }
    if (!hwScrollCount) return 0;
    if (frameMode_ == Mode::High) return 2;
    if (!hwScrollPrefetch) return 0;
    return frameMode_ == Mode::Medium ? 4 : 8;
}

// Décode UNE scanline active (display-enable) avec l'état COURANT des registres
// (palette, base vidéo) et la place à l'offset bordure dans le buffer overscan.
void Shifter::renderLine(int y) {
    if (y < 0 || y >= curAH_) return;
    // Commit PARESSEUX du compteur matérialisé (2026-07-02, fidèle Video_EndHBL) :
    // les lignes < y sont committées ICI (au rendu de la ligne y = cycle
    // y·cpl+376), donc APRÈS toute écriture freq/res de FIN de ligne y−1 (ex.
    // impulsion 60→50 Hz à cyc 372-376 ouvrant la bordure droite : la calibration
    // du loader d'Enchanted Land lit $FF8209 8 cycles après l'impulsion et doit
    // voir le compteur ENCORE en marche jusqu'à 460, puis la ligne créditée de
    // 204 octets). L'ancien commit à DE_end (376) de la ligne COURANTE figeait
    // la ligne à 160 octets AVANT que l'écriture datée 376 ne soit consommée →
    // valeur figée (base+160) au lieu d'avancer (base+164) = delta nul → le
    // scan de calibration EL ne détectait jamais l'ouverture (loader bloqué).
    // Les lectures sur lignes non-committées passent par l'extrapolation
    // glueLineBytes (live) de videoCounter — mêmes valeurs, mais la fenêtre DE
    // de la ligne courante est consultée EN DIRECT. Le re-rendu hors séquence
    // (spec512/renderFrame) n'avance jamais le compteur (y ≤ vcLineY_).
    while (vcLineY_ < y) endVideoLine();
    const int lead = (steBorderFlag_ && bordered()) ? 16 : 0;
    const int  W   = activeWidth() + lead;
    const bool hi  = (frameMode_ == Mode::High);

    uint8_t idx[660];                                   // max (640/16 + 1) * 16 = 656
    const int scroll = decodeLineIndices(y, idx);

    // Début de la zone ACTIVE dans le buffer (décalée des bordures gauche/haut).
    uint32_t* dst = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_ + activeX_ - lead;

    // Shifter ARRÊTÉ (res 3 côté Shifter) : il n'absorbe plus les mots du MMU et ne
    // sort aucun pixel — l'aire active sort NOIRE (Hatari video.c:1912 « all pixels
    // will be black during the time when shifter is stopped », observation Troed/
    // Sync sur STF). ⚠ Incertain : noir franc vs couleur 0, et comportement STE non
    // documenté — on applique le modèle STF aux deux, comme Hatari. Les bordures,
    // produites hors DE, gardent leur couleur.
    const bool stopped = (shifterRes_ == 3);

    // Émet W pixels à partir de l'offset `scroll`. En haute résolution = moniteur
    // MONOCHROME : seule la polarité vient du registre 0 de la palette — reg0
    // sans bit couleur ($000) = vidéo INVERSE, blanc sur noir (Hatari conv_st.c,
    // « HBLPalettes[0] & 0x777 »). palette[1] reste ignoré (sinon un palette[1]
    // non noir — ex. rouge sous TOS 1.02 — colorerait l'écran à tort).
    if (stopped) {
        for (int c = 0; c < W; ++c) dst[c] = 0xFF000000u;
    } else if (hi) {
        const uint8_t inv = (palette[0] & 0x777) ? 0 : 1;
        for (int c = 0; c < W; ++c)
            dst[c] = ((idx[c + scroll] ^ inv) & 1) ? 0xFF000000u : 0xFFFFFFFFu;
    } else {
        // Conversion $0RGB → ARGB8888 SORTIE de la boucle : elle était refaite pour
        // chacun des 320 à 640 pixels de chaque ligne, alors que `palette` ne peut
        // pas changer pendant l'émission (aucune émulation ne tourne ici — les
        // changements en cours de ligne passent par le re-rendu spec512, qui rappelle
        // renderLine). 16 conversions par ligne au lieu de W.
        uint32_t argb[16];
        for (int i = 0; i < 16; ++i) argb[i] = stColorToArgb(palette[i]);
        const uint8_t* src = idx + scroll;
        for (int c = 0; c < W; ++c) dst[c] = argb[src[c]];
    }

    // Bordures latérales de CETTE ligne. Le registre 0 (couleur de bordure) est écrit
    // par le handler HBL vers cyc 508 (fin de ligne), pour la ligne SUIVANTE. Or les
    // pixels du bord GAUCHE sortent en tout début de ligne (cyc ~0-56), AVANT cette
    // écriture → ils gardent le registre 0 latché en FIN de ligne précédente (mesuré à
    // l'oracle Hatari : bord gauche[N] == active[N−1]). Le bord DROIT sort après l'aire
    // active (cyc ~376+) → registre 0 COURANT (bord droit[N] == active[N], déjà fidèle).
    if (bordered()) {
        const uint32_t bgLeft  = stColorToArgb(leftBorderPal0_);   // ligne précédente
        const uint32_t bgRight = stColorToArgb(palette[0]);        // courant
        uint32_t* row = frame_.data() + static_cast<std::size_t>(activeY_ + y) * curW_;
        for (int x = 0; x < activeX_ - lead; ++x) row[x] = bgLeft;
        for (int x = activeX_ + activeWidth(); x < curW_; ++x) row[x] = bgRight;
        leftBorderPal0_ = palette[0];   // pour le bord gauche de la ligne suivante
    }

}

// Fin de trame : re-rendu spec512 (palette intra-ligne) si détecté. Port du
// modèle Hatari spec512.c — au lieu de mémoriser une palette par ligne (figée à
// DE_END), on rejoue les écritures palette datées et on met à jour une palette
// « roulante » au CYCLE où chaque pixel est balayé. Le 68000 ne peut écrire la
// palette qu'une fois tous les 4 cycles (bus 16 bits), donc au plus ~1 changement
// tous les 4 pixels en basse résolution → jusqu'à 512 couleurs à l'écran.
void Shifter::finishFrame() {
    // Commit de la DERNIÈRE ligne active : le commit paresseux de renderLine(y) ne
    // pousse dans endVideoLine que les lignes < y — la ligne curAH_−1 n'était donc
    // JAMAIS committée (pas de capture lineSnap_ pour elle → l'artefact « sprite en
    // course avec le faisceau » pouvait réapparaître sur cette unique ligne). On la
    // committe ici, AVANT le replay/rendu glue. Pas de double-commit (vcLineY_ fait
    // foi) ni de décalage des autres lignes : les lectures $FF8205/07/09 en bordure
    // basse extrapolaient déjà exactement ce stride (mêmes valeurs qu'avant).
    while (vcLineY_ < curAH_) endVideoLine();

    if (frameMode_ == Mode::High) return;                      // mono : ni spec512 ni bordures

    // Rejoue la machine Glue sur les écritures freq/res datées → état d'affichage
    // par scanline + bordures haut/bas (détection de retrait). Bon marché si aucun
    // switch (cas normal → bordersTrick_ reste faux).
    replayGlue();

    // DEBUG (NEOST_PAL_TRACE) : dump PAR TRAME des écritures palette (ligne, cyc,
    // idx, couleur, PC), même hors spec512 — diagnostic des rasters « normaux »
    // (raster bars, splits Timer B/HBL). Le fichier est réécrit à chaque trame →
    // il contient la DERNIÈRE trame rendue. Format aligné sur NEOST_SPEC512_TRACE.
    static const char* palTrace = std::getenv("NEOST_PAL_TRACE");
    // NEOST_PAL_TRACE_ALL=1 : mode CUMULATIF (append + en-tête « frame N ») pour
    // diff multi-trames contre l'oracle Hatari (--trace video_color, continu).
    static const bool palTraceAll = std::getenv("NEOST_PAL_TRACE_ALL") != nullptr;
    static long palTraceFrame = -1;
    ++palTraceFrame;
    if (palTrace && (palTraceAll || !colorWrites_.empty())) {
        auto ws = colorWrites_;                    // copie : ne pas perturber spec512
        std::stable_sort(ws.begin(), ws.end(),
                         [](const ColorWrite& a, const ColorWrite& b) {
                             return a.frameCycle < b.frameCycle;
                         });
        // 1re ouverture du run en "w" : TRONQUE un fichier survivant d'un run
        // précédent (sinon les en-têtes « frame 0.. » de plusieurs sessions se
        // concaténaient et le diff multi-trames se synchronisait sur le run périmé).
        static bool palTraceFresh = true;
        if (FILE* tf = std::fopen(palTrace, (palTraceAll && !palTraceFresh) ? "a" : "w")) {
            palTraceFresh = false;
            if (palTraceAll) std::fprintf(tf, "frame %ld\n", palTraceFrame);
            const Geometry gg = geometry();
            for (const auto& w : ws)
                std::fprintf(tf, "line %d cyc=%d idx=%d col=%03x pc=%06x\n",
                             static_cast<int>(w.frameCycle / gg.cyclesPerLine),
                             static_cast<int>(w.frameCycle % gg.cyclesPerLine),
                             w.index, w.colour & 0xFFF, w.pc);
            std::fclose(tf);
        }
    }

    // Rendu fenêtré (palette roulante) si retrait de bordure détecté OU si la
    // palette a été écrite dans la trame (spec512Active_, seuil Hatari = 1) : il
    // gère TOUT — bordures ouvertes, bordures ROULANTES (registre 0 au cycle du
    // faisceau, flancs gauche/droit ET lignes haut/bas compris), raster par ligne,
    // spec512 intra-ligne. C'est le pendant de la conversion écran UNIQUE d'Hatari
    // (spec512.c + video.c) : une écriture palette mid-ligne bascule la couleur au
    // pixel exact, même dans la bordure — l'ancien chemin « actif seul » laissait
    // les bordures au latch par-ligne de renderLine (banc poll : bascules à
    // x=45..47 dans la bordure gauche et pal[0] roulant haut/bas manquants).
    // Sans écriture palette, masque ni retrait : rendu ligne-à-ligne conservé.
    // Deux checks OFF manqués peuvent garder VBlank actif jusqu'à l'aire normale,
    // sans retrait de bordure ni écriture palette : le replay doit aussi l'effacer.
    // NO_DE/BLANK/STOP_MIDDLE peuvent masquer une ligne SANS élargir de bordure.
    // Le rendu live nominal ne connaît pas ces masques ; rejouer même si aucune
    // palette n'a été écrite. Garder bordersTrick_ pour les véritables extensions
    // de fenêtre : le cadrage adaptatif s'appuie sur ce signal.
    const bool maskedLine = std::any_of(glueLines_.begin(), glueLines_.end(),
        [](const GlueLine& line) {
            return (line.borderMask & (glue::NO_DE | glue::BLANK | glue::STOP_MIDDLE)) != 0;
        });
    const Geometry vg = geometry();
    // Scroll hard « Shifter arrêté » (shifterStopLine_) ou ligne committée Shifter
    // arrêté (bit 7 de lineScrollSnap_) : seul le rendu fenêtré sait décaler/masquer
    // une ligne individuellement.
    const bool stoppedLine = std::any_of(lineScrollSnap_.begin(), lineScrollSnap_.end(),
        [](uint8_t m) { return (m & 0x80) != 0; });
    if (bordersTrick_ || spec512Active_ || maskedLine || shifterStopLine_ >= 0 || stoppedLine
        || vblankAtLine(vg.dispStartLine + vg.displayLines - 1))
        renderGlueFrame();

    // Snapshot Glue stable pour le zoom kiosk : capturé ICI, après replayGlue() /
    // renderGlueFrame(), AVANT que beginFrame_() du cycle suivant ne remette à zéro
    // glueStartHBL_/glueEndHBL_. Le rendu GL lit snapLiveTop()/snapLiveHeight().
    snapGlueStart_      = glueStartHBL_;
    snapGlueEnd_        = glueEndHBL_;
    snapGlueVOverscan_  = glueVOverscan_;
    snapGlueBlankLines_ = glueBlankLines_;
    snapBordersTrick_   = bordersTrick_;
    snapSideTrick_      = sideTrick_;
    snapSideTrickLines_ = sideTrickLines_;
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
        // Même convention que syncCpuBus : Hatari aligne la FIN de l'accès (= cycle
        // enregistré + 2, le cycle live NeoST datant le point-MILIEU de l'accès Moira).
        // syncCpuBus (LIVE) ayant déjà aligné, ceci reste un no-op de garde.
        const int64_t wait = (4 - ((arrival + 2) & 3)) & 3;   // 0..3 jusqu'à la frontière de 4
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
    // DEBUG (NEOST_COL_DIAG) : datation des écritures palette — à confronter aux
    // « write col ... line_cyc_w » de l'oracle (--trace video_color). Chantier
    // Closure : l'effet plein écran est un raster de palette beam-racé, toute
    // erreur de datation par opcode se voit (hachis par segments).
    if (std::getenv("NEOST_COL_DIAG")) {
        const int cpl = geometry().cyclesPerLine;
        const int64_t into = bus_.cpu ? bus_.cpu->cyclesIntoInstr() : -1;
        std::fprintf(stderr, "[COL] base=%06x line=%lld cyc=%lld into=%lld idx=%d col=%03x pc=%06x\n",
            vcFrameBase_ & 0xFFFFFF,
            static_cast<long long>(fc / cpl), static_cast<long long>(fc % cpl),
            static_cast<long long>(into), index, palette[index] & 0xFFF,
            bus_.cpu ? bus_.cpu->pc() : 0);
    }

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

// Wait state de bus 4 cycles (port LIVE de Hatari M68000_SyncCpuBus, cf. .hpp). Hatari
// aligne la FIN de l'accès (Cycles_GetClockCounterOn{Read,Write}Access = currcycle+4)
// sur la frontière de 4. Le cycle live NeoST date le point-MILIEU de l'accès Moira →
// fin d'accès = fc + 2 ; on aligne CETTE fin (cohérent avec chipWait8 qui aligne le
// début d'accès, et avec les offsets de datation write +2 / read −6).
void Shifter::syncCpuBus() {
    if (!liveFrameClock_ || !bus_.cpu) return;
    const int64_t fc = liveFrameClock_();
    if (fc < 0) return;                                  // hors trame courante
    const int wait = static_cast<int>((4 - ((fc + 2) & 3)) & 3);   // fin d'accès → frontière
    if (wait) bus_.cpu->addBusWaitCycles(wait);
}

// Datation des ÉCRITURES freq/res — VALEUR FIDÈLE THÉORIQUE **+2** (2026-07-03)
// = fin d'accès Moira (fc+2, ≙ WinUAE currcycle+4, callback au point-MILIEU),
// SANS décalage d'origine (doc convergence §8, cible #2). L'« origine −8 »
// mesurée le 2026-07-02 (write −6 / read −14) était un ARTEFACT co-calibré :
// la mesure croisée à l'oracle (menu Cuddly, ancre VBL f02e + chemin CPU
// identique) montre que l'origine d'horloge trame NeoST = Hatari, et que les
// DEUX datations devaient revenir ENSEMBLE (+8 chacune) aux valeurs fidèles
// de la table §8 (read −6, write +2). Historique : le +16 initial puis le −6
// étaient des rustines calibrées autour de biais CPU corrigés depuis (IACK
// sur-compté, alignement bus, double comptage STOP). Validé ENSEMBLE :
// Cuddly menu 250/250 verrouillées, EL loader + top-trick 40/40, LX titre
// propre, SHO byte-identique, étalons 19/19 + TOUS OK.
// NEOST_SYNC_OFF ajuste pour l'A/B (−8 → ancien −6).

// Décode `nPix` index planaires à partir de l'adresse vidéo `base` (rendu fenêtré
// des bordures : largeur explicite, base fournie). Applique le SCROLL FIN STE avec
// le même modèle matériel que decodeLineIndices (port Video_CopyScreenLineColor) :
//  • PREFETCH ($FF8265) : 1 groupe de 16 px lu EN PLUS, la ligne est décalée à
//    gauche de `scroll` px (l'appelant consomme idx[s + scroll]) ;
//  • SANS prefetch ($FF8264) : aucun octet en plus, l'affichage démarre 16 px plus
//    tard (bord gauche couleur 0) — données décodées à partir de idx[16] puis
//    idx[0..16+scroll) mis à 0.
// scroll = 0 (ST/STF) → décodage historique strictement inchangé. Renvoie le
// décalage scroll. `idx` doit tenir nPix arrondi au groupe + 16 + 16 octets.
int Shifter::decodeWindowIndices(uint32_t base, int nPix, uint8_t* idx, bool medLine,
                                 int scroll, bool prefetch) const {
    const int planes = (frameMode_ == Mode::Medium || medLine) ? 2 : 4;   // low=4, med=2
    const int groupB = 2 * planes;                             // octets pour 16 px
    const int groups = (nPix + 15) / 16;
    const int  decodeGroups = (scroll && prefetch) ? groups + 1 : groups;
    int px = (scroll && !prefetch) ? 16 : 0;
    for (int gI = 0; gI < decodeGroups; ++gI) {
        const uint32_t a  = base + static_cast<uint32_t>(gI) * groupB;   // repli 22 bits par mot
        const uint16_t p0 = bus_.read16(a & kVideoAddrMask);
        const uint16_t p1 = planes > 1 ? bus_.read16((a + 2) & kVideoAddrMask) : 0;
        const uint16_t p2 = planes > 2 ? bus_.read16((a + 4) & kVideoAddrMask) : 0;
        const uint16_t p3 = planes > 3 ? bus_.read16((a + 6) & kVideoAddrMask) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));   // bord gauche couleur 0
    return scroll;
}

int Shifter::decodeWindowIndicesFromBytes(const uint8_t* src, int srcLen, int nPix, uint8_t* idx, bool medLine,
                                          int scroll, bool prefetch) const {
    // Même décodage planaire (et même modèle de scroll fin STE) que
    // decodeWindowIndices, mais depuis la CAPTURE de la ligne (octets
    // échantillonnés au faisceau) au lieu du bus. Au-delà de srcLen (marge de
    // capture épuisée) : octets à 0, comme une RAM vierge.
    const int planes = (frameMode_ == Mode::Medium || medLine) ? 2 : 4;
    const int groupB = 2 * planes;
    const int groups = (nPix + 15) / 16;
    const int  decodeGroups = (scroll && prefetch) ? groups + 1 : groups;
    auto rd16 = [&](int off) -> uint16_t {
        const uint8_t hiB = (off     < srcLen) ? src[off]     : 0;
        const uint8_t loB = (off + 1 < srcLen) ? src[off + 1] : 0;
        return static_cast<uint16_t>((hiB << 8) | loB);
    };
    int px = (scroll && !prefetch) ? 16 : 0;
    for (int gI = 0; gI < decodeGroups; ++gI) {
        const int off     = gI * groupB;
        const uint16_t p0 = rd16(off);
        const uint16_t p1 = planes > 1 ? rd16(off + 2) : 0;
        const uint16_t p2 = planes > 2 ? rd16(off + 4) : 0;
        const uint16_t p3 = planes > 3 ? rd16(off + 6) : 0;
        for (int bit = 15; bit >= 0; --bit)
            idx[px++] = static_cast<uint8_t>(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                           | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3));
    }
    if (scroll && !prefetch)
        std::memset(idx, 0, static_cast<std::size_t>(16 + scroll));
    return scroll;
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
    // Moyenne résolution : le shifter sort 2 PIXELS PAR CYCLE (640 px sur la même
    // fenêtre DE de 320 cycles, 4 octets par groupe de 16 px au lieu de 8 — port du
    // LineRes par-ligne de Video_CopyScreenLineColor ; le chemin spec512 sans
    // bordure de finishFrame fait déjà ce mapping via c*span/W). Basse rés :
    // 1 px/cycle (chemin historique inchangé).
    const int ppc       = (frameMode_ == Mode::Medium) ? 2 : 1;
    // Cycle du pixel balayé à la colonne x=0 du buffer : bordure gauche incluse en
    // basse rés bordée (56−48=8) ; en moyenne rés le buffer n'a PAS de bordure
    // (bordered() = low seulement) → colonne 0 = début DE nominal.
    const int visFirst  = g.lineStartCycle - (bordered() ? kBorderLeftPx : 0);
    // Scroll fin STE : appliqué par decodeWindowIndices* (même modèle que
    // decodeLineIndices) ; l'émetteur lit idx[s + scroll]. 0 sur ST/STF.
    const int scroll    = hwScrollCount;

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

    // DEBUG (NEOST_SPEC512_TRACE) : dump des écritures palette converties en
    // (ligne, position dans la ligne) — format comparable au trace `video_color`
    // d'Hatari (« spec store col line N cyc=H idx=I col=RGB »). Diff oracle.
    // Après tri + alignement bus : datations telles que RENDUES.
    static const char* spcTrace = std::getenv("NEOST_SPEC512_TRACE");
    if (spcTrace && spec512Active_) {
        const Geometry gg = geometry();
        if (FILE* tf = std::fopen(spcTrace, "w")) {
            for (const auto& w : colorWrites_)
                std::fprintf(tf, "line %d cyc=%d idx=%d col=%03x pc=%06x\n",
                             static_cast<int>(w.frameCycle / gg.cyclesPerLine),
                             static_cast<int>(w.frameCycle % gg.cyclesPerLine),
                             w.index, w.colour & 0xFFF, w.pc);
            std::fclose(tf);
            std::fprintf(stderr, "[spec512] %zu palette writes → %s\n",
                         colorWrites_.size(), spcTrace);
        }
    }

    std::array<uint16_t, 16> pal = frameStartPalette_;
    const std::size_t n = colorWrites_.size();
    std::size_t cur = 0;
    uint32_t addr = vcFrameBase_ & kVideoAddrMask;         // compteur vidéo latché au VBL (≙ Video_ClearOnVBL)
    const int nLines = static_cast<int>(glueLines_.size());

    // DIAG (gated NEOST_RENDER_TRACE=<numéro de trame>) : état per-ligne au rendu glue.
    static int s_renderFrame = -1; ++s_renderFrame;
    static const int traceFrame = []{ const char* s = std::getenv("NEOST_RENDER_TRACE"); return s ? std::atoi(s) : -1; }();
    static const bool renderAll = std::getenv("NEOST_RENDER_ALL") != nullptr;   // toutes les lignes (≠ 12)
    const bool rtr = (traceFrame >= 0 && s_renderFrame >= traceFrame);
    if (rtr) std::fprintf(stderr, "[render f%d] base=%06x start=%d end=%d vover=%d\n",
                          s_renderFrame, vcFrameBase_ & 0xFFFFFF, glueStartHBL_, glueEndHBL_, glueVOverscan_);

    // Dimensionné pour le PIRE cas décodable : DE_end = LINE_END_FULL (512, hors
    // table — écriture 71 Hz mi-ligne) × 2 px/cycle (ligne med) = 1024 px, arrondi
    // au groupe de 16 + groupe scroll (nDec ≤ 1040). L'ancien [960] débordait la
    // pile sur ce cas pathologique (nPix clampé ci-dessous par ceinture ET bretelles).
    uint8_t idx[1072];
    const int wsInc = glue::timing(bus_.machine).inc;              // re-normalisation des DE stockés (cf. plus bas)
    for (int row = 0; row < curH_; ++row) {
        const int sl = baseStart + (row - activeY_);       // scanline de cette ligne buffer
        // Fenêtre verticale = [nStartHBL, nEndHBL + BlankLines) comme Hatari
        // (video.c:3985, :1568) : les lignes blanches no-sync insérées (glueBlank-
        // Lines_) repoussent la fin d'affichage d'autant. 0 sans trick no-sync.
        // VO_NO_DE : trame 50 Hz dont le DE vertical n'a JAMAIS été activé (bascule
        // de fréquence entre Top_Pos et nStartHBL) — chez Hatari, TOUTES les lignes
        // tombent dans le test « total blank line » (video.c:3988) et sortent à
        // l'index couleur 0. Le drapeau est figé dès nStartHBL, donc l'appliquer à
        // la trame entière au rendu équivaut au test par-ligne d'Hatari.
        const bool displayed = (sl >= glueStartHBL_ && sl < glueEndHBL_ + glueBlankLines_
                                && sl >= 0 && sl < nLines
                                && !(glueVOverscan_ & glue::VO_NO_DE));
        int ds = 0, de = 0, shift = 0; uint32_t bm = 0;
        if (displayed) { const GlueLine& L = glueLines_[sl]; ds = L.displayStartCycle; de = L.displayEndCycle; bm = L.borderMask; shift = L.displayPixelShift; }
        // Scroll hard « Shifter arrêté » (Troed/Sync, port video.c:1968-1972) : les
        // lignes APRÈS celle du stop prennent DisplayPixelShift = 0/−4/−8/−12. Chez
        // Hatari, une écriture Glue POSTÉRIEURE sur une de ces lignes (retrait gauche,
        // stab…) réécrit son DisplayPixelShift : on ne l'applique donc qu'aux lignes
        // dont la Glue n'a posé ni décalage ni retrait gauche (approximation : une
        // écriture Glue qui reposerait explicitement 0 n'est pas distinguée).
        const bool stopShifted = displayed && shifterStopLine_ >= 0 && sl > shifterStopLine_
            && shift == 0 && !(bm & (glue::LEFT_OFF | glue::LEFT_OFF_MED));
        if (stopShifted) shift = shifterStopShift_;
        // Re-normalisation wakestate : les DE stockés viennent de la table Glue
        // WS-décalée (≙ ShifterLines d'Hatari, 57/377 en WS3-STF ; inc = 0 sur STE)
        // mais le RENDU est ancré sur les constantes FIXES 56/376 (≙ copie écran
        // Hatari, byte-based, WS-indépendante) — on retire l'incrément, sauf 0
        // (pas de DE) et LINE_END_FULL (512, hors table).
        if (ds > 0) ds -= wsInc;
        if (de > 0 && de != glue::LINE_END_FULL) de -= wsInc;
        // Le latch 336 px suit CETTE scanline, comme le scroll fin (bit 5 du
        // snapshot). Une bascule mid-trame ne doit pas changer les lignes passées.
        const bool snapHere = sl >= 0 && sl < static_cast<int>(lineSnapLen_.size())
                              && lineSnapLen_[sl] > 0;
        const bool scrollSnapped = sl >= 0 && sl < static_cast<int>(lineScrollSnap_.size())
                                   && (lineScrollSnap_[sl] & 0x40);
        const bool lineBorder = scrollSnapped ? (lineScrollSnap_[sl] & 0x20) != 0 : steBorderFlag_;
        if (lineBorder && steBorderBytes(bm)) {
            if (bm & glue::LEFT_OFF_2_STE) ds = 0;
            else ds -= 16;
        }
        const bool lineHasDE = displayed && !(bm & glue::NO_DE) && de > ds;
        // Ligne BLANK (bascule 60/50 près du blanking) : le shifter LIT ses octets
        // (le compteur avance, cf. glueLineBytes) mais le signal vidéo est coupé —
        // Hatari efface la ligne à l'index couleur 0 APRÈS l'avoir copiée
        // (video.c:4081 memset → couleur 0 ; le « vrai noir » est un TODO Hatari,
        // video.c:3983). On rend donc pal[0] sur toute la ligne, sans décoder.
        const bool vblank = vblankAtLine(sl);
        const bool lineBlank = vblank || (lineHasDE && (bm & glue::BLANK) != 0);
        // Shifter arrêté (res 3) sur toute la fenêtre DE de CETTE ligne (bit 7 posé
        // au commit par lineStoppedOver) : aire active noire (cf. renderLine). La lecture RAM/compteur n'est pas affectée (le MMU continue).
        const bool lineStopped = scrollSnapped && (lineScrollSnap_[sl] & 0x80) != 0;
        // V2 — résolution PAR LIGNE (port Video_StoreResolution, video.c:3729-3733) :
        // une ligne marquée OVERSCAN_MED_RES / LEFT_OFF_2_STE_MED se décode en
        // MOYENNE résolution (2 plans, 2 px/cycle) au sein d'une trame basse rés.
        // Le buffer reste à 1 px/cycle → on émet le pixel med PAIR (convention
        // alignée sur la comparaison oracle : Hatari 832 px sous-échantillonné 2×).
        const bool lineMed = (frameMode_ == Mode::Low)
                           && (bm & (glue::OVERSCAN_MED_RES | glue::LEFT_OFF_2_STE_MED)) != 0;
        static const bool medDiag = std::getenv("NEOST_MED_DIAG") != nullptr;
        if (medDiag && displayed && sl == 120) {
            static long n = 0;
            if (++n % 100 == 0)
                std::fprintf(stderr, "[MED] sl=%d bm=%05x med=%d ds=%d de=%d snap=%d\n",
                             sl, bm, lineMed ? 1 : 0, ds, de,
                             (sl < (int)lineSnapLen_.size() && lineSnapLen_[sl] > 0) ? 1 : 0);
        }
        const int  lppc = lineMed ? 2 : ppc;               // px décodés par cycle de CETTE ligne
        int nPix = lineHasDE ? (de - ds) * lppc : 0;
        if (nPix > 1024) nPix = 1024;                      // garde idx : cf. dimensionnement ci-dessus
        if (rtr && displayed && (renderAll || sl < baseStart + 12))
            std::fprintf(stderr, "  sl%d ds=%d de=%d bm=%03x sh=%d nPix=%d addr=%06x snap=%d snapLen=%d gbytes=%d\n",
                         sl, ds, de, bm, shift, nPix, addr & 0xFFFFFF,
                         (sl >= 0 && sl < (int)lineSnapLen_.size() && lineSnapLen_[sl] > 0) ? 1 : 0,
                         (sl >= 0 && sl < (int)lineSnapLen_.size()) ? lineSnapLen_[sl] : -1,
                         glueLineBytes(sl));
        // Scroll fin STE PAR LIGNE : la valeur capturée au commit de CETTE scanline
        // (lineScrollSnap_, même datation que lineSnap_) — un split qui change
        // $FF8264/65 à mi-trame était re-rendu tout entier avec le scroll de FIN
        // de trame dès qu'une écriture palette déclenchait renderGlueFrame
        // (spec512Active_, seuil 1). Repli : valeur de fin de trame (lignes non
        // committées — même approximation que la relecture RAM ci-dessous).
        // Compteur (bits 0-3) ET mode prefetch (bit 4) de CETTE ligne : le
        // décodage (groupe en plus / départ à idx[16] / memset de tête) doit
        // suivre la même paire que l'émission — les membres vivants portent la
        // valeur de FIN de trame, fausse pour l'autre moitié d'un split.
        const int  lineScroll = scrollSnapped ? (lineScrollSnap_[sl] & 0x0F) : scroll;
        const bool linePref   = scrollSnapped ? (lineScrollSnap_[sl] & 0x10) != 0
                                              : hwScrollPrefetch;
        // decodeWindowIndices décode des GROUPES de 16 px (+1 groupe si scroll avec
        // prefetch, ou offset 16 sans prefetch) : la plage valide de idx est
        // [0, nDec) — marge pour le DisplayPixelShift et le scroll.
        const int  nDec = lineHasDE ? ((nPix + 15) / 16) * 16 + (lineScroll ? 16 : 0) : 0;
        // Décalage SOURCE de la ligne med overscan, en OCTETS sur la base de
        // décodage : Hatari VideoOffset = −champ MED_OFFSET (0 No Cooper, −2 PYM)
        // là où le chemin LOW validé (LEFT_OFF) vaut −2 → différentiel
        // (2 − champ) octets. ⚠ EN OCTETS et pas en pixels : le stride d'une
        // ligne overscan (186 o) n'est PAS multiple de 4 — l'appariement des
        // plans (mot p0, mot p1) dépend de l'origine octet, un décalage d'index
        // pixel ne réordonnerait pas les plans (logo « rayé » une ligne sur
        // deux, mesuré sur l'oracle greetings). kMedCal = ajustement d'ÉMISSION
        // en px med : **−4** (le 1er mot med sort 2 cycles après DE_start —
        // même famille que le « +7 » pipeline du spec512) — CALIBRÉ à 0 px
        // contre l'oracle Hatari sur l'écran greetings No Cooper (2026-07-08).
        // NEOST_MED_CAL pour l'A/B.
        static const int kMedCal = [] {
            const char* e = std::getenv("NEOST_MED_CAL");
            return e ? std::atoi(e) : -4;
        }();
        int medSrcBytes = lineMed
            ? 2 - static_cast<int>((bm & glue::MED_OFFSET_MASK) >> 20) : 0;
        const int medSrcPx = lineMed ? kMedCal : 0;
        // Scroll hardware STF 4 px / stab med (port video.c:3946-3990, « ST Cnx »
        // et « 'Closure' demo Troed/Sync ») : le retrait gauche par bascule
        // hi→med→lo déplace CHAQUE ligne selon le cycle de sa bascule retour
        // (displayPixelShift stocké 13/9/5/1, ou 0 = stab). Hatari applique un
        // OFFSET SOURCE en octets (VideoOffset {2,0,−2,−4}, −4 pour le stab :
        // « planes are shifted » — l'octet d'origine PERMUTE les plans, comme le
        // chemin med) PLUS « STF_PixelScroll −= 8 » (le retrait gauche med décale
        // l'affichage de 8 px). Exprimé RELATIVEMENT à notre repère calibré
        // (LEFT_OFF standard shift −4, offset 0, nocooper 0 px ↔ Hatari
        // VideoOffset −2) : srcOff = VideoOffset + 2. Sans ce port, les lignes
        // X-DISTING de Closure sortaient au shift brut sans permutation de
        // plans → damier à marches (+11 px / ~9 lignes) au lieu du slide lisse.
        //
        // A40 (2026-08-30) — LE REPÈRE, établi par le calcul PUIS mesuré. Hatari ne
        // rend pas ces lignes au faisceau : il RECOPIE des octets dans un tampon de
        // SCREENBYTES_LINE = 208 o (24 bordure G + 160 fenêtre + 24 bordure D = 416 px),
        // en partant de `raster + BORDERBYTES_LEFT − SCREENBYTES_LEFT + VideoOffset`
        // = `raster + 2 + VideoOffset` (video.c:4014-4019), PUIS décale tout le tampon
        // de STF_PixelScroll (video.c:4273+). Dans le repère du buffer NeoST
        // (x = cyc − 8, DE d'une ligne left-off à ds = 4), cela donne
        //     index source Hatari  s_H(x) = x + 4 + 2·VideoOffset − scrollFinal
        // et notre rendu, source décalée de medSrcBytes octets (1 o = 2 px),
        //     s_N(x) = x + 4 − shEff + 2·medSrcBytes.
        // Avec la convention DÉJÀ posée ici (medSrcBytes = VideoOffset + 2), les deux
        // coïncident si et seulement si **shEff = 4 + scrollFinal**.
        // Le stab med de Closure (VideoOffset −4, scrollFinal −8, video.c:3990-3993)
        // vaut donc shEff = −4, et PAS −8 : le −8 recopiait le scroll d'Hatari en
        // oubliant que son ancrage de recopie est 4 px à droite du faisceau. C'était
        // A40 : 4 px de décalage sur TOUTE l'image (oracle 64,08 % → 1,81 %, puis
        // 0,02 % avec la règle de queue ci-dessous). Balayage ±6 px et ±3 o autour :
        // aucun autre couple ne descend sous 53 %, l'optimum est isolé.
        // ⚠ Les cas 13/9/5/1 (scroll « hardware » ST Cnx) et le −4 du left-off
        // standard gardent leurs valeurs : la même algèbre les dit décalés de 4 px
        // (resp. 8), mais AUCUN étalon ne les exhibe — Closure ne produit ces
        // masques que sur des lignes BLANK (rendues à l'index 0, cf. lineBlank) et
        // jamais avec shift 13/9/5/1. Sans exhibiteur mesuré, on ne règle pas.
        int shEff = shift;
        // Queue de ligne SANS SOURCE. Le tampon d'Hatari fait SCREENBYTES_LINE, soit
        // exactement la largeur du buffer : décalé de `scrollFinal` pixels vers la
        // gauche, ses |scrollFinal| DERNIERS pixels n'ont plus de source et restent à
        // l'index couleur 0 (video.c:4295, « entering pixels to the extreme right
        // should be set to color 0 »). Exprimé en COLONNES du buffer, la règle vaut
        // quelle que soit la largeur rendue (bordures ou non). Sans elle il restait
        // 8 px par ligne, soit 2 083 px sur l'étalon closure.
        int blankTailFrom = W;
        if (!lineMed && (bm & (glue::LEFT_OFF | glue::LEFT_OFF_MED))) {
            switch (shift) {
                case 13: medSrcBytes += 4; shEff = 5;  break;
                case 9:  medSrcBytes += 2; shEff = 1;  break;
                case 5:  medSrcBytes += 0; shEff = -3; break;
                case 1:  medSrcBytes -= 2; shEff = -7; break;
                case 0:  if (bm & glue::LEFT_OFF_MED) {   // stab med (Closure STF)
                             medSrcBytes -= 2;            // VideoOffset −4 → srcOff −2
                             shEff = -4;                  // 4 + scrollFinal(−8)
                             blankTailFrom = W - 8;       // |scrollFinal| = 8
                         }
                         break;
                default: break;                    // −4 : left-off standard, non mesuré
            }
        }
        // Source des pixels : la CAPTURE datée au faisceau de cette scanline si elle
        // existe (cf. lineSnap_ — seul l'échantillon voit un sprite dessiné puis
        // effacé EN COURSE avec le faisceau, ex. robot du menu Cuddly), sinon repli
        // relecture RAM en fin de trame (lignes non committées : bordure haute
        // ouverte, bas de trame au-delà des lignes actives).
        if (nPix > 0 && !lineBlank && !lineStopped) {
            // Les slots de capture portent kSnapLead octets de garde en tête :
            // un offset source négatif (scroll hard / stab med, ≥ −kSnapLead)
            // reste DANS le slot — pas de repli RAM (qui ré-introduirait
            // l'artefact « dessin en course avec le faisceau », cf. kSnapLead).
            const bool haveSnap = snapHere;
            if (haveSnap)
                decodeWindowIndicesFromBytes(lineSnap_.data() + static_cast<std::size_t>(sl) * kLineSnapBytes
                                                 + kSnapLead + medSrcBytes,
                                             lineSnapLen_[sl] - medSrcBytes, nPix, idx, lineMed,
                                             lineScroll, linePref);
            else
                decodeWindowIndices(addr + static_cast<uint32_t>(static_cast<int32_t>(medSrcBytes)), nPix, idx, lineMed,
                                    lineScroll, linePref);
        }

        // Ligne basse rés décalée par Shifter arrêté : chez Hatari TOUTE la ligne de
        // 416 px (bordures comprises) glisse de |shift| px à gauche (video.c:4273+) —
        // les premiers pixels actifs débordent dans la bordure gauche et la queue de
        // l'aire active montre la bordure. On décale donc la FENÊTRE d'émission, la
        // source restant indexée depuis ds (s = 0 au premier pixel émis).
        const int winShift = (stopShifted && !lineMed && ppc == 1) ? shift : 0;
        if (winShift) shEff = winShift;
        const int wds = ds + winShift, wde = de + winShift;
        uint32_t* dst = frame_.data() + static_cast<std::size_t>(row) * W;
        for (int x = 0; x < W; ++x) {
            const int cyc = visFirst + x / ppc;            // cycle du pixel balayé à cette colonne
            const int64_t limit = static_cast<int64_t>(sl) * cpl + cyc - glueAlignCyc;
            while (cur < n && colorWrites_[cur].frameCycle <= limit) {
                // `index` est borné à la relecture d'un save-state (cf. serialize) ;
                // on le re-teste ici car c'est ICI que l'écriture a lieu, sur un
                // tableau de PILE de 16 entrées.
                const uint8_t ci = colorWrites_[cur].index;
                if (ci < pal.size()) pal[ci] = colorWrites_[cur].colour;
                ++cur;
            }
            if (lineHasDE && lineStopped && cyc >= ds && cyc < de) {
                dst[x] = 0xFF000000u;                      // Shifter arrêté : pas de pixel
            } else if (lineHasDE && !lineBlank && cyc >= wds && cyc < wde) {
                // Décalage pixel de la ligne (Hatari DisplayPixelShift : <0 = vers la
                // gauche, p.ex. -4 sur le retrait gauche hi/lo). 0 pour les lignes
                // normales → aucun effet (top/bottom/écran standard inchangés).
                // En moyenne rés, 2 pixels par cycle (x/ppc + sous-pixel x%ppc).
                // Le scroll fin STE décale la source (idx[s + scroll], cf.
                // decodeWindowIndices — modèle decodeLineIndices).
                // Ligne MED dans trame LOW : 2 px décodés par cycle, le buffer
                // (1 px/cycle) émet la MOYENNE des 2 px med de la colonne (même
                // réduction que l'oracle 2× → un étalon référencé sur l'oracle
                // vérifie les DEUX phases med). Le displayPixelShift du retrait
                // hi (−4) ne s'applique pas au chemin med (Hatari rend ces
                // lignes via VideoOffset seul, video.c:3932).
                if (x >= blankTailFrom) {                  // queue sans source (cf. plus haut)
                    dst[x] = stColorToArgb(pal[0]);
                    continue;
                }
                int s = lineMed ? ((cyc - ds) * 2 + medSrcPx)
                                : ((cyc - ds) * ppc + (x % ppc) - shEff + lineScroll);
                if (s < 0) s = 0; else if (s >= nDec) s = nDec - 1;
                if (lineMed) {
                    const int s2 = (s + 1 < nDec) ? s + 1 : s;
                    const uint32_t a = stColorToArgb(pal[idx[s]]);
                    const uint32_t b = stColorToArgb(pal[idx[s2]]);
                    dst[x] = 0xFF000000u | ((((a & 0x00FEFEFE) + (b & 0x00FEFEFE)) >> 1) & 0x00FFFFFF);
                } else {
                    dst[x] = stColorToArgb(pal[idx[s]]);   // dans la fenêtre → contenu
                }
            } else {
                dst[x] = stColorToArgb(pal[0]);            // hors fenêtre / bordure / blank → registre 0
            }
        }
        // Adresse vidéo ACCUMULÉE : avance par NOMBRES D'OCTETS FIXES selon les
        // drapeaux de bordure (port de Video_CopyScreenLineColor / BORDERBYTES_*,
        // video.h:111-115) et NON par (DE_end-DE_start)/2 : la fenêtre RIGHT_OFF
        // s'arrête à cpl-50=462 mais le shifter lit 160+44=204 octets (bord réel
        // 464) — l'ancien calcul perdait 1 octet PAR LIGNE et le décor dérivait
        // cumulativement vers le bas (loader TDA de Rick Dangerous : bandes de
        // garbage qui empirent ligne à ligne).
        // + line-offset STE ($FF820F, en mots) et avance de scroll fin (prefetch =
        //   1 mot PAR PLAN), comme l'accumulation par ligne de videoCounter — les
        //   `pVideoRaster += LineWidth*2` / `+= n*2` de Video_CopyScreenLineColor.
        //   0 sur ST/STF (chemin historique inchangé).
        if (lineHasDE && !vblank) {
            const int extra = lineBorder ? steBorderBytes(bm)
                : (lineScroll && linePref ? (frameMode_ == Mode::Medium ? 4 : 8) : 0);
            addr += static_cast<uint32_t>(glueLineBytes(sl) + static_cast<int>(lineWidth) * 2 + extra);
        }
        // Ajustement « Shifter arrêté » en fin de ligne du stop (≙ vcStopInc_ du
        // compteur live : AddressInc = shift/2, soit 0/−2/−4/−6 octets).
        if (sl == shifterStopLine_) addr += static_cast<uint32_t>(shifterStopShift_ / 2);
    }
}

// Auto-test déterministe du re-rendu Spectrum 512 (cf. header). On construit une
// trame STF basse rés 50 Hz où CHAQUE pixel décode l'index de palette 1, on injecte
// des écritures palette datées sur l'index 1, on force le rendu spec512 (finishFrame)
// et on vérifie octet-exact que la bascule de couleur tombe au pixel prédit par le
// modèle (position = f(kSpec512AlignCyc, géométrie de trame). Aucun boot ni oracle.
bool Shifter::spec512SelfTest() {
    int pass = 0, fail = 0;
    auto chk = [&](const char* name, long got, long want) {
        if (got == want) { ++pass; }
        else { ++fail; std::fprintf(stderr, "  FAIL %-24s got=%ld want=%ld\n", name, got, want); }
    };

    // 0. Garde-fou : la constante d'alignement calibrée à l'oracle Hatari (spec512.c).
    //    Toute modification accidentelle décale TOUTES les frontières palette → scramble.
    chk("kSpec512AlignCyc", kSpec512AlignCyc, -25);

    // 1. Trame STF basse rés 50 Hz (512 cyc/ligne, DE 56..376, VDE_On ligne 63).
    mode = Mode::Low; sync = 0x02;
    videoBase = 0x20000;                 // dans la RAM (≥ 256k) ; hors vecteurs/système
    beginFrame();                        // verrouille la géométrie + dimensionne le buffer
    const Geometry g = geometry();
    const int W   = activeWidth();       // 320
    const int cpl = g.cyclesPerLine;     // 512
    const int lst = g.lineStartCycle;    // 56
    const int ds  = g.dispStartLine;     // 63

    // 2. RAM vidéo synthétique : tous les pixels = index 1 (plan 0 = tous les bits à 1,
    //    plans 1..3 = 0). En basse rés : 4 mots entrelacés par groupe de 16 px.
    for (int y = 0; y < curAH_; ++y) {
        const uint32_t rowBase = videoBase + static_cast<uint32_t>(y) * 160u;
        for (int gx = 0; gx < W / 16; ++gx) {
            const uint32_t a = rowBase + static_cast<uint32_t>(gx) * 8u;
            bus_.write16(a,     0xFFFF);   // plan 0 → tous les pixels bit0=1
            bus_.write16(a + 2, 0x0000);   // plan 1
            bus_.write16(a + 4, 0x0000);   // plan 2
            bus_.write16(a + 6, 0x0000);   // plan 3
        }
    }
    // Compteur matérialisé : base latchée = videoBase, TOUTES les lignes committées
    // (vcLineY_ = curAH_) → decodeLineIndices prend la voie analytique base + y·160.
    vcFrameBase_ = videoBase; vcLineBase_ = videoBase; vcLineY_ = curAH_;

    // 3. Palette de départ : index 1 = C0. Écritures datées sur l'index 1.
    //    Cycle-pixel du pixel c de la ligne active y : (ds+y)·cpl + lst + c  (span/W=1).
    //    Une écriture de cycle F prend effet au pixel c dès que F ≤ pixCyc(c) + 25, soit
    //    au 1ᵉʳ pixel c ≥ F − (pixCyc(0)+25). On CHOISIT donc F pour viser un pixel exact.
    //    Contraintes : F ≡ 2 (mod 4) → applyShifterBusAlignment est un no-op (cf. syncCpuBus,
    //    toutes les écritures NeoST sont ≡2 mod 4), et pixCyc(0)+25 est impair → pixel ≡1 mod4.
    const uint16_t C0 = 0x111, C1 = 0x700, C2 = 0x070, C3 = 0x007, C4 = 0x777;
    frameStartPalette_.fill(0x000);
    frameStartPalette_[1] = C0;
    auto pixCyc = [&](int y, int c) {
        return static_cast<int64_t>(ds + y) * cpl + lst + c;
    };
    auto cycForPixel = [&](int y, int c) {   // cycle F qui bascule la couleur PILE au pixel c
        return static_cast<int32_t>(pixCyc(y, c) - kSpec512AlignCyc);
    };
    // Frontières visées (pixels ≡1 mod4 → F ≡2 mod4). Ligne 0 : C1@41, C2@101, C3@201.
    const int b1 = 41, b2 = 101, b3 = 201;
    // Ligne 1 : la palette roulante conserve C3 de la ligne 0 jusqu'à b4, puis C4.
    const int b4 = 53;
    colorWrites_.clear();
    colorWrites_.push_back({ cycForPixel(0, b1), C1, 1, 0 });
    colorWrites_.push_back({ cycForPixel(0, b2), C2, 1, 0 });
    colorWrites_.push_back({ cycForPixel(0, b3), C3, 1, 0 });
    colorWrites_.push_back({ cycForPixel(1, b4), C4, 1, 0 });
    paletteAccesses_ = static_cast<int>(colorWrites_.size());
    spec512Active_   = true;             // force la voie de re-rendu spec512

    // Vérifie que les cycles choisis sont bien ≡2 mod4 (sinon le modèle ci-dessous,
    // qui suppose applyShifterBusAlignment neutre, ne tiendrait pas).
    for (const auto& w : colorWrites_) chk("write ≡2 mod4", w.frameCycle & 3, 2);

    // 4. Rendu spec512 (palette roulante par cycle-pixel).
    finishFrame();

    // 5. Vérification octet-exact. Couleur attendue de l'index 1 par pixel, selon les
    //    frontières ; on la passe par stColorToArgb (même conversion que le rendu).
    auto pix = [&](int y, int c) -> uint32_t {
        return frame_[static_cast<std::size_t>(activeY_ + y) * curW_ + activeX_ + c];
    };
    auto expect = [&](int y, int c) -> uint16_t {
        if (y == 0) return c < b1 ? C0 : c < b2 ? C1 : c < b3 ? C2 : C3;
        return c < b4 ? C3 : C4;               // ligne 1 : C3 hérité puis C4
    };
    // Échantillonne largement + précisément AUTOUR de chaque frontière (±1 px), là où
    // un décalage d'alignement d'un seul cycle se voit.
    int mism = 0;
    for (int y = 0; y <= 1; ++y)
        for (int c = 0; c < W; ++c)
            if (pix(y, c) != stColorToArgb(expect(y, c))) ++mism;
    chk("pixels non conformes", mism, 0);
    // Frontières exactes (le pixel juste avant/après doit basculer).
    chk("L0 avant b1", pix(0, b1 - 1) == stColorToArgb(C0), 1);
    chk("L0 à b1",     pix(0, b1)     == stColorToArgb(C1), 1);
    chk("L0 à b2",     pix(0, b2)     == stColorToArgb(C2), 1);
    chk("L0 à b3",     pix(0, b3)     == stColorToArgb(C3), 1);
    chk("L1 hérite C3",pix(1, b4 - 1) == stColorToArgb(C3), 1);
    chk("L1 à b4",     pix(1, b4)     == stColorToArgb(C4), 1);

    // 6. Chemin FENÊTRÉ (spec512 BORDÉ) : ouvre les bordures G+D sur toutes les lignes
    //    actives et vérifie que le re-rendu fenêtré (renderGlueFrame) applique bien la
    //    palette roulante spec512 — c'est le chemin d'une image spec512 à bordures
    //    ouvertes (≠ chemin borderless testé ci-dessus). Smoke test : la voie est armée
    //    (bordersTrick_) et les couleurs injectées apparaissent (palette non figée).
    beginFrame();                         // réinitialise colorWrites_/syncWrites_/glue
    vcFrameBase_ = videoBase; vcLineBase_ = videoBase; vcLineY_ = curAH_;
    frameStartPalette_.fill(0x000); frameStartPalette_[1] = C0;
    colorWrites_.clear();
    colorWrites_.push_back({ cycForPixel(0, b2), C2, 1, 0 });
    colorWrites_.push_back({ cycForPixel(1, b4), C4, 1, 0 });
    paletteAccesses_ = 2; spec512Active_ = true;
    // Bordures G (hi@2 / lo@8) + D (60 Hz@374 / 50 Hz@380) sur chaque ligne active —
    // mêmes stimuli validés que glueSelfTest (tombent dans les fenêtres STF WS3 / STE).
    syncWrites_.clear();
    for (int L = ds; L < ds + curAH_; ++L) {
        const int32_t bc = static_cast<int32_t>(L) * cpl;
        syncWrites_.push_back({ bc + 2,   0x02, true  });   // hi-res → LEFT_OFF
        syncWrites_.push_back({ bc + 8,   0x00, true  });   // retour lo-res
        syncWrites_.push_back({ bc + 374, 0x00, false });   // 60 Hz → RIGHT_OFF
        syncWrites_.push_back({ bc + 380, 0x02, false });   // retour 50 Hz
    }
    finishFrame();                        // → replayGlue (bordersTrick_) → renderGlueFrame
    chk("bordé : voie fenêtrée armée", bordersTrick_ ? 1 : 0, 1);
    auto hasColor = [&](uint16_t c) {
        const uint32_t argb = stColorToArgb(c);
        for (uint32_t px : frame_) if (px == argb) return true;
        return false;
    };
    chk("bordé : C2 présent (palette roulante)",      hasColor(C2) ? 1 : 0, 1);
    chk("bordé : C4 présent (curseur inter-lignes)",  hasColor(C4) ? 1 : 0, 1);

    std::fprintf(stderr, "[spec512-selftest] %d OK, %d FAIL\n", pass, fail);
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
    // Octet bas de la base vidéo $FF820D : STE seulement. Sur ST/MegaST le registre
    // n'existe pas : zone VOID → 0xFF (ioMemTabST.c:53, IoMem_VoidRead — l'ancien 0
    // suivait Video_BaseLow_ReadByte, qui n'est PAS mappé sur la table ST).
    if (addr == 0xFF820D) return machineIsSte(bus_.machine) ? static_cast<uint8_t>(videoBase) : 0xFF;
    // Une écriture du compteur pendant le DE est en attente (vcDelayedOffset_) : la
    // relecture doit déjà la refléter (port Video_ScreenCounter_ReadByte qui ajoute
    // VideoCounterDelayedOffset & ~1 à l'adresse calculée).
    if (addr == 0xFF8205 || addr == 0xFF8207 || addr == 0xFF8209) {
        // Wait-state de la lecture du compteur vidéo $FF8205/07/09. La VALEUR est
        // échantillonnée au cycle d'ACCÈS (avant tout wait, façon Hatari
        // Cycles_GetCounterOnReadAccess), PUIS le CPU pourrait être retardé.
        // ⚠ DÉFAUT = 0 (2026-06-18). Mesuré à l'oracle Hatari sur EL EN JEU (poll fullscreen
        // $ee78 lecture simple + $3700 double lecture) AVEC `NEOST_RAM_SLOT` défaut-ON :
        //   $ee78 = 20 cyc/itér (NeoST VC_WAIT=0) = 20 (Hatari) ; VC_WAIT=2 → 24 (+4 FAUX).
        //   $3700 = 36 (NeoST VC_WAIT=0) = 36 (Hatari) ; VC_WAIT=2 → 40 (+4 FAUX).
        // RAM_SLOT (alignement créneau bus) fournit DÉJÀ le +2 que ce wait ajoutait jadis
        // → VC_WAIT=2 DOUBLE-COMPTE (+4) et fait DÉRIVER le poll → SCRAMBLE EL en jeu.
        // VC_WAIT=0 : EL en jeu propre (garbage→paysage net), étalons BYTE-IDENTIQUES
        // (spec512/overscan_top/scroll/glue 19-0). L'ancien commentaire « +2 requis sinon
        // LX/EL noir » datait d'AVANT RAM_SLOT défaut-ON → périmé. Override : NEOST_VC_WAIT.
        const uint32_t vc = (videoCounter() + (vcDelayedOffset_ & ~1)) & kVideoAddrMask;
        static const int vcWait = [] { const char* s = std::getenv("NEOST_VC_WAIT"); return s ? std::atoi(s) : 0; }();
        if (vcWait && bus_.cpu) bus_.cpu->addBusWaitCycles(vcWait);
        // DEBUG (oracle Hatari `--trace video_addr`) : trace chaque lecture du compteur
        // vidéo avec assez d'état pour diff'er au cycle. Gated NEOST_VC_TRACE.
        static const char* vctr = std::getenv("NEOST_VC_TRACE");
        if (vctr) {
            int64_t fc = beamClock_ ? beamClock_() : 0;
            fc += kVideoCounterReadOffsetCyc;
            const Geometry g = geometry();
            const int ln = g.cyclesPerLine ? static_cast<int>(fc / g.cyclesPerLine) : 0;
            const int X  = g.cyclesPerLine ? static_cast<int>(fc % g.cyclesPerLine) : 0;
            const uint32_t pc = bus_.cpu ? bus_.cpu->pc() : 0;
            const int into = bus_.cpu ? static_cast<int>(bus_.cpu->cyclesIntoInstr()) : -1;
            std::fprintf(stderr,
                "VC reg=%05x base=%06x addr=%06x fc=%lld line=%d X=%d start=%d cpl=%d "
                "liveStart=%d sync=%zu pc=%06x into=%d\n",
                addr, videoBase & 0xFFFFFFu, vc, static_cast<long long>(fc), ln, X,
                g.lineStartCycle, g.cyclesPerLine, liveStartHBL_,
                syncWrites_.size(), pc, into);
        }
        if (addr == 0xFF8205) return static_cast<uint8_t>(vc >> 16);
        if (addr == 0xFF8207) return static_cast<uint8_t>(vc >> 8);
        return static_cast<uint8_t>(vc);
    }
    // Synchro $FF820A : bits inutilisés 2-7 forcés à 1 (ST et STE), cf. Hatari
    // Video_Sync_ReadByte (IoMem[0xff820a] |= 0xfc). On NE masque PAS le champ
    // stocké `sync` : videoCounter() s'en sert toujours via `sync & 2`.
    if (addr == 0xFF820A) return static_cast<uint8_t>((sync & 0x03) | 0xFC);
    // Largeur de ligne STE $FF820F : sur ST/MegaST le registre n'existe pas —
    // zone void → 0xFF (absent de ioMemTable_ST ; Video_LineWidth_ReadByte n'y
    // est mappé que sur STE).
    if (addr == 0xFF820F) return machineIsSte(bus_.machine) ? lineWidth : 0xFF;
    // $FF8260 (résolution GLUE+Shifter) et son alias Shifter-seul $FF8261 partagent la
    // même lecture (Video_ResGlueShifter/ResShifter_ReadByte → Video_Res_ReadByte,
    // video.c:5281,5300-5308) : STF/Mega ST forcent les bits inutilisés 2-7 à 1, STE/Mega
    // STE les laissent à 0. (Le moniteur mono impose déjà mode = High = 2 en amont, comme
    // le `if (bUseHighRes) IoMem=2` de Hatari.) Avant : $8260 rendait bits 2-7=0 sur ST
    // (faux) et $8261 tombait en zone void → 0xFF (non géré).
    // La valeur relue est celle du SHIFTER (c'est lui qui pilote le bus de données,
    // commentaire Hatari video.c:5310 « the value is read from the SHIFTER ») : elle
    // ne diffère de la Glue qu'après une écriture $FF8261 seule. ⚠ Hatari relit
    // IoMem[adresse] (dernier octet écrit À CETTE adresse) ; le Shifter n'ayant que
    // D8-D9, ce qu'une lecture OCTET de $FF8261 (D0-D7) voit sur le vrai matériel
    // n'est pas documenté — on garde l'alias complet, comme avant.
    if (addr == 0xFF8260 || addr == 0xFF8261) {
        syncCpuBus();
        const uint8_t r = shifterRes_ & 0x03;
        return machineIsSte(bus_.machine) ? r : static_cast<uint8_t>(r | 0xFC);
    }
    // Scroll fin (STE seulement — sur ST/MegaST $FF8262-7F est une zone void → 0xFF,
    // ioMemTabST.c:72). $FF8264 : Hatari n'intercepte PAS la lecture (video.c:5813,
    // Video_HorScroll_Read_8264 = wait state seul) → on renvoie la dernière valeur
    // BRUTE écrite, comme IoMem. $FF8265 : valeur de scroll courante.
    if (addr == 0xFF8264 && machineIsSte(bus_.machine)) { syncCpuBus(); return hwScrollReg8264_; }
    if (addr == 0xFF8265) {
        if (!machineIsSte(bus_.machine)) return 0xFF;
        syncCpuBus();
        return hwScrollCount;
    }
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

void Shifter::write8(uint32_t addr, uint8_t v) {
    // Adresse de base vidéo : octets haut ($FF8201) et milieu ($FF8203). Le bit
    // bas est fixé à 0 (le ST aligne le framebuffer sur 256 octets).
    const bool ste = machineIsSte(bus_.machine);
    switch (addr) {
        case 0xFF8201:
            // Octet haut masqué comme le compteur $FF8205 (port Hatari video.c:5084
            // DMA_MaskAddressHigh) : sans lui, une base $FFxxxx ferait fetcher la
            // MMIO par le rendu (lectures à effets de bord — UDR MFP, STR FDC… —
            // voire bus error déclenchée HORS exécution CPU).
            v &= 0x3F;
            videoBase = (videoBase & 0x00FF00) | (uint32_t(v) << 16);
            // STE/TT : écrire l'octet haut/milieu remet à 0 l'octet bas $FF820D
            // (cf. Hatari Video_ScreenBase_WriteByte).
            if (ste) videoBase &= 0xFFFF00;
            return;
        case 0xFF8203:
            videoBase = (videoBase & 0xFF0000) | (uint32_t(v) << 8);
            if (ste) videoBase &= 0xFFFF00;
            // DIAG beam-sync (gated NEOST_BASE_TRACE) : ligne/cycle/valeur des écritures de
            // base vidéo. Une base jittery trame à trame (joueur immobile) = la boucle de
            // calibration fullscreen du jeu ne converge pas (résidu phase CPU↔faisceau),
            // PAS un trick glue res-switch — cf. EL en jeu (scramble).
            static const bool baseTrace_ = std::getenv("NEOST_BASE_TRACE") != nullptr;
            if (baseTrace_ && liveFrameClock_) {
                const int64_t fc = liveFrameClock_(); const int cpl = geometry().cyclesPerLine;
                std::fprintf(stderr, "[base] $8203=%02x line=%lld cyc=%lld\n", v,
                             (long long)(fc>=0?fc/cpl:-1), (long long)(fc>=0?fc%cpl:-1));
            }
            return;
        case 0xFF820A: syncCpuBus(); recordSyncWrite(false, v); sync = v; return;   // synchro 50/60 Hz (+ bordures + wait-state bus, FIX2 : aligne l'accès sur 4 cyc comme $FF8260/palette, port wait_cpu_cycle_write)
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
        case 0xFF8260: syncCpuBus(); recordSyncWrite(true, v); mode = static_cast<Mode>(v & 0x3);
            // V2 res-switch : une écriture hi-res ($8260 bit1) peut raccourcir la ligne
            // courante (la Machine vérifie qu'elle est précoce). Cf. setHblShorten.
            if ((v & 0x3) == 2 && hblShorten_) hblShorten_();
            // Le Shifter reçoit la MÊME valeur : en octet comme en mot, il lit les bits
            // 8-9 du bus, c.-à-d. l'octet adressé à $FF8260 (Video_Res_WriteByte).
            writeShifterRes(v & 0x3);
            return;  // résolution (+ bordures + wait state bus)
        // Alias $FF8261 : résolution du SHIFTER SEUL, la Glue n'est pas touchée (ni
        // timings ni bordures — ioMemTabST.c:71 / ioMemTabSTE.c:120). Seul un accès
        // OCTET compte : dans un move.w vers $FF8260, l'octet bas qui « tombe » ici
        // est ignoré par le Shifter (il lit D8-D9 = l'octet de $FF8260, déjà traité
        // ci-dessus). move.b #x,$FF8261 duplique x sur D8-D15 → Shifter = x.
        case 0xFF8261:
            if (bus_.ioAccessWidth() == 1) { syncCpuBus(); writeShifterRes(v & 0x3); }
            return;
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
                const int64_t fc = beamClock_ ? beamClock_() : 0;
                const bool add16 = !prefetch && sc == 0 && lastScroll8265Cycle_ >= 0
                    && fc >= lastScroll8265Cycle_ && fc - lastScroll8265Cycle_ <= 40;
                if (prefetch) lastScroll8265Cycle_ = sc ? static_cast<int32_t>(fc) : -1;
                // $FF8264 : mémorise l'octet BRUT pour la relecture (Hatari laisse
                // IoMem tel quel — la lecture n'est pas interceptée, video.c:5813).
                if (!prefetch) hwScrollReg8264_ = v;
                int line = 0, cyc = 0;
                const Geometry g = geometry();
                const int la = beamPos(line, cyc) ? line - liveStartHBL_ : -1;
                const bool active = la >= 0 && la < g.displayLines;
                if (!active || cyc <= g.lineStartCycle) {
                    hwScrollCount = sc; hwScrollPrefetch = prefetch; newHwScrollCount_ = -1;
                    steBorderFlag_ = add16;
                } else {
                    newHwScrollCount_ = sc; newHwScrollPrefetch_ = prefetch;
                    newSteBorderFlag_ = add16;
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
