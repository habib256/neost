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
    //  `beginFrame()` verrouille la résolution de la trame (la résolution ne peut
    //  pas changer en cours de décodage) ; `renderLine(y)` décode UNE ligne avec
    //  l'état COURANT des registres (palette/base vidéo) → les changements en
    //  cours de trame (rasters, scroll par base) s'appliquent ligne à ligne.
    void beginFrame();
    void renderLine(int y);

    // Accès au buffer décodé (ARGB8888) pour le frontend ou un dump.
    const uint32_t* pixels() const { return frame_.data(); }
    int width()  const { return curW_; }
    int height() const { return curH_; }

    // Interface MMIO ($FF8200-$FF8260) appelée par le Bus.
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

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
    // on ne stocke QUE l'état des registres (le décalage par pixel relève de la
    // cycle-accuracy, différé). Cf. Hatari Video_HorScroll_Write (HWScrollCount/
    // HWScrollPrefetch).
    uint8_t hwScrollCount = 0;              // 4 bits de scroll fin ($FF8264/65 & 0x0F)
    bool    hwScrollPrefetch = false;       // écriture via $FF8265 → prefetch
    // Largeur de ligne STE $FF820F (octets ajoutés au compteur shifter en fin de
    // ligne). On la lit/réécrit ; le câblage dans renderLine reste différé (TODO).
    uint8_t lineWidth = 0;                  // cf. Hatari Video_LineWidth_WriteByte

private:
    static uint32_t stColorToArgb(uint16_t c);   // $0RGB → ARGB8888
    void resizeFor(Mode m);                       // ajuste le buffer si la rés. change
    uint32_t videoCounter() const;                // adresse vidéo courante ($FF8205/07/09)

    Bus&          bus_;
    int           curW_ = 0, curH_ = 0;     // résolution décodée courante
    Mode          frameMode_ = Mode::Low;   // résolution verrouillée pour la trame
    std::vector<uint32_t> frame_;           // curW_*curH_ pixels ARGB
    std::function<int64_t()> beamClock_;    // cycles dans la trame (cf. setBeamClock)
};
