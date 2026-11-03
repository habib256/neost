// =============================================================================
//  VideoGlue.hpp — LE VRAI GLUE vidéo : masques de bordure, table de timings,
//  wakeup state. (Chantier A32, 2026-08-28.)
//
//  POURQUOI CE FICHIER EXISTE. Le nom « Glue » désignait dans l'arbre un stub de
//  31 lignes qui ne fait que porter la config mémoire du MMU ($FF8001) — pendant
//  que le VRAI GLUE, celui qui décide des retraits de bordure et des positions
//  DE/HBL, vivait anonymement au milieu de Shifter.cpp, dans un `namespace glue`
//  local à l'unité de compilation. Un lecteur qui cherchait la machine à états
//  des bordures ouvrait Glue.hpp et n'y trouvait rien.
//
//  Le stub MMU s'appelle désormais MmuGlue (MmuGlue.hpp) ; ce fichier-ci porte le
//  GLUE vidéo, et c'est lui qu'on ouvre pour comprendre une bordure retirée.
//
//  Port fidèle de Hatari video.c — les constantes gardent leurs noms d'amont
//  (BORDERMASK_*, HDE_On_*, VDE_Off_*) pour que le diff avec la source de vérité
//  reste immédiat.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstdlib>   // getenv / atoi (verrou NEOST_WS)
#include "core/MachineType.hpp"


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
constexpr uint32_t LEFT_OFF_2_STE  = 0x0200;   // retrait gauche COURT STE (hi→lo pile à cyc 4) → +20 o, shift −8
// V2 — tricks par changement de RÉSOLUTION (Video_WriteToGlueRes, video.c:1618-1789) :
constexpr uint32_t OVERSCAN_MED_RES = 0x0040;  // bordures retirées ET ligne en MOYENNE résolution (No Cooper greetings)
constexpr uint32_t LEFT_OFF_MED     = 0x0100;  // retrait gauche par bascule hi→MED (scroll hard 4 px / stab med)
constexpr uint32_t LEFT_OFF_2_STE_MED = 1u << 16;   // variante courte STE du retrait gauche med
constexpr uint32_t MED_OFFSET_MASK  = 0xFu << 20;   // décalage SOURCE (octets) de la ligne med overscan (0=No Cooper, 2=PYM)
// Fenêtres de détection des tricks res (video.h:100-106) — FIXES, hors table wakestate.
constexpr int LINE_LEFT_STAB_LOW    = 16;      // retrait gauche + stab med (hi/med/lo)
constexpr int LINE_SCROLL_13_CYCLE  = 20;      // scrolls « hardware » droite (px)
constexpr int LINE_SCROLL_9_CYCLE   = 24;
constexpr int LINE_SCROLL_5_CYCLE   = 28;
constexpr int LINE_SCROLL_1_CYCLE   = 32;
constexpr int LINE_LEFT_MED_CYCLE_1 = 20;      // med res overscan, décalage source 0 octet (No Cooper)
constexpr int LINE_LEFT_MED_CYCLE_2 = 28;      // med res overscan, décalage source 2 octets (PYM)
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
// Wakeup state STF — TRANCHÉ : WS3 (2026-07-08, fin de l'« hybride WS1/WS3 » de
// docs/HATARI_DIVERGENCES.md). Sur STF réel, la synchro MMU/GLUE au power-on tire
// un des 4 états qui décale les positions HORIZONTALES de la Glue
// (Video_InitTimings_Copy : WS1 = base, WS3 = +1, WS4 = +2, WS2 = +3), la
// position de l'IRQ HBL (WS1 : cpl−4 ; WS2/3/4 : cpl) et la VBL (WS1 : 60 ;
// autres : 64). L'oracle Hatari tourne au défaut VIDEO_TIMING_DEFAULT = WS3
// (video.c:624) et le STE (sans wakestate) a AUSSI l'HBL à cpl → NeoST adopte
// WS3. ⚠ Les ancres de RENDU / compteur vidéo / spec512 restent les constantes
// FIXES 56/376 : Hatari utilise LINE_START/END_CYCLE_* (video.h:91-95) HORS
// table wakestate pour Video_CalculateAddress, spec512.c et la copie écran — les
// DE stockés par la Glue (table WS-décalée, ≙ ShifterLines) sont re-normalisés
// de −kWsInc au rendu (cf. renderGlueFrame). Idem Timer B : la position par
// défaut vient des constantes fixes (Video_TimerB_GetDefaultPos), cf.
// timerBLinePos. NEOST_WS=1..4 pour A/B (défaut 3).
// A32 : `inline` — la table est passée d'une unité de compilation à un en-tête
// partagé par trois. Sans lui, chaque .cpp en emportait sa copie et le lien
// refusait le doublon.
inline int wakestate() {
    static const int v = [] {
        const char* s = std::getenv("NEOST_WS");
        const int n = s ? std::atoi(s) : 3;
        return (n >= 1 && n <= 4) ? n : 3;
    }();
    return v;
}
inline const int kWsInc    = (wakestate() == 1) ? 0 : (wakestate() == 3) ? 1
                           : (wakestate() == 4) ? 2 : 3;   // WS2 = +3
inline const int kWsHblAdj = (wakestate() == 1) ? -4 : 0;  // IRQ HBL : cpl−4 en WS1, cpl sinon
constexpr int LINE_END_FULL      = 512;        // FIXE (LINE_END_CYCLE_FULL, hors table wakestate)

// Table de timings de la Glue PAR MACHINE — port de VideoTimings[] (video.c:927-1057).
// STF : base WS1 + kWsInc (wakestate tranché WS3) ; STE : le GST MCU (MMU+GLUE
// fusionnés) n'a PAS de wakestate — valeurs PROPRES : preload MMU (le shifter
// commence à charger 16 cyc avant DE, positions Preload_Start_*), Line_Set_Pal 58,
// HSync −52/−12, RemoveBorder 500. `inc` = décalage des positions STOCKÉES vs les
// ancres FIXES du rendu (re-normalisation, cf. renderGlueFrame) : kWsInc en STF,
// 0 en STE. Hbl_Pos_* : canal HBL_Pos/nCyclesPerLine (video.c 977-979) — chaque
// « Freq_match » de phase 1 fixe la POSITION de l'IRQ HBL de la ligne courante et
// sa LONGUEUR (une ligne 71/60/50 Hz fait 224/508/512 cycles).
struct Timing {
    int inc;                                   // décalage wakestate des positions stockées
    int Preload_Start_Hi;                      //   0 (STE)
    int HDE_On_Hi;                             //   4
    int HBlank_Off_Low_60, HBlank_Off_Low_50;  //  24 / 28
    int Preload_Start_Low_60;                  //  36 (STE)
    int HDE_On_Low_60;                         //  52
    int Line_Set_Pal;                          //  54 STF / 58 STE
    int Preload_Start_Low_50;                  //  40 (STE)
    int HDE_On_Low_50;                         //  56
    int HDE_Off_Hi;                            // 164
    int HDE_Off_Low_60, HDE_Off_Low_50;        // 372 / 376
    int HSync_On_Off_Low, HSync_Off_Off_Low;   // −50/−10 STF, −52/−12 STE (relatifs à cpl)
    int RemoveTopBorder_Pos, RemoveBottomBorder_Pos;   // 502 STF / 500 STE
    int Hbl_Pos_Hi, Hbl_Pos_Low_60, Hbl_Pos_Low_50;    // 224/508/512 (−4 en WS1)
};
inline const Timing kStf = {
    kWsInc,
    0,                    // Preload_Start_Hi (inutilisé en STF)
    4 + kWsInc,
    24 + kWsInc, 28 + kWsInc,
    0,                    // Preload_Start_Low_60 (inutilisé en STF)
    52 + kWsInc,
    54 + kWsInc,
    0,                    // Preload_Start_Low_50 (inutilisé en STF)
    56 + kWsInc,
    164 + kWsInc,
    372 + kWsInc, 376 + kWsInc,
    -50 + kWsInc, -10 + kWsInc,
    502 + kWsInc, 502 + kWsInc,
    224 + kWsHblAdj, 508 + kWsHblAdj, 512 + kWsHblAdj,
};
// [D] Valeurs STE recoupées par simulation de gstmcu @ 4a6db7cc (tools/run_gstmcu_bench.py) :
//     HSync, fin de HBlank, HDE On/Off et longueurs de ligne, à 50 et 60 Hz.
//     Détail : docs/REFERENCE_LADDER.md.
inline const Timing kSte = {
    0,
    0,                    // Preload_Start_Hi
    4,
    24, 28,
    36,                   // Preload_Start_Low_60
    52,
    58,                   // Line_Set_Pal (≠ STF 54). [D] gstmcu @ 4a6db7cc, tools/run_glue_sweep.py : toute
                          // la table 60 Hz concorde à 58, pas à 56 (Hatari video.c @ f0736b24).
    40,                   // Preload_Start_Low_50
    56,
    164,
    372, 376,
    -52, -12,             // HSync (≠ STF −50/−10)
    500, 500,             // RemoveBorder (≠ STF 502)
    224, 508, 512,        // HBL à cpl (pas de wakestate)
};

// Table de la machine COURANTE — choisie à CHAQUE usage (la machine peut changer
// à chaud via Machine::reconfigure). Mega ST = STF, Mega STE = STE, comme Hatari
// Config_IsMachineST()/STE().
// A32 : prend le MODÈLE, plus un `Bus&`. Le GLUE vidéo n'a que faire du bus —
// et cette dépendance-là interdisait de sortir la table de Shifter.cpp.
inline const Timing& timing(MachineType m) { return machineIsSte(m) ? kSte : kStf; }
constexpr int CyclesLine_Hi  = 224;
constexpr int CyclesLine_60  = 508;
constexpr int CyclesLine_50  = 512;
// Positions verticales (lignes) : VDE_On/Off par fréquence.
constexpr int VDE_On_50 = 63, VDE_On_60 = 34, VDE_On_Hi = 34;
constexpr int VDE_Off_50 = 263, VDE_Off_60 = 234, VDE_Off_Hi = 434;
constexpr int VDE_Off_NoBottom_50 = 310;   // 263 + 47 (VIDEO_HEIGHT_BOTTOM_50HZ)
constexpr int VDE_Off_NoBottom_60 = 260;   // 234 + 26 (VIDEO_HEIGHT_BOTTOM_60HZ)
}  // namespace glue
