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
    };
    // Géométrie de la trame VERROUILLÉE (cf. frameMode_/frameSync_, posés par beginFrame).
    Geometry geometry() const { return geometryFor(frameMode_, frameSync_); }

    // Accès au buffer décodé (ARGB8888) pour le frontend ou un dump.
    const uint32_t* pixels() const { return frame_.data(); }
    int width()  const { return curW_; }
    int height() const { return curH_; }

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
    // (HWScrollCount/HWScrollPrefetch). La distinction prefetch/no-prefetch fine
    // (bord gauche, dérive de compteur) relève de la cycle-accuracy, non modélisée.
    uint8_t hwScrollCount = 0;              // 4 bits de scroll fin ($FF8264/65 & 0x0F)
    bool    hwScrollPrefetch = false;       // écriture via $FF8265 → prefetch
    // Largeur de ligne STE $FF820F (line-offset, en MOTS, ajoutés au stride en fin
    // de ligne) — CÂBLÉE dans renderLine et videoCounter. Cf. Video_LineWidth_WriteByte.
    uint8_t lineWidth = 0;

private:
    static uint32_t stColorToArgb(uint16_t c);   // $0RGB → ARGB8888
    void resizeFor(Mode m);                       // ajuste le buffer si la rés. change
    uint32_t videoCounter() const;                // adresse vidéo courante ($FF8205/07/09)

    // Géométrie (cycles/ligne, lignes/trame, DE) pour une résolution + fréquence
    // données. Statique : ne dépend que de (mode, sync) → réutilisée pour la trame
    // verrouillée (geometry()) comme pour un calcul ponctuel.
    static Geometry geometryFor(Mode m, uint8_t syncReg) {
        if (m == Mode::High)      return {224, 501, 400, 0, 160};   // 71 Hz monochrome
        if (syncReg & 0x02)       return {512, 313, 200, 56, 376};  // 50 Hz PAL (défaut)
        return                           {508, 263, 200, 52, 372};  // 60 Hz NTSC
    }

    Bus&          bus_;
    int           curW_ = 0, curH_ = 0;     // résolution décodée courante
    Mode          frameMode_ = Mode::Low;   // résolution verrouillée pour la trame
    uint8_t       frameSync_ = 0x02;        // fréquence ($FF820A) verrouillée pour la trame
    std::vector<uint32_t> frame_;           // curW_*curH_ pixels ARGB
    std::function<int64_t()> beamClock_;    // cycles dans la trame (cf. setBeamClock)
};
