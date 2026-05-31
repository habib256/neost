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

    // Accès au buffer décodé (ARGB8888) pour le frontend ou un dump.
    const uint32_t* pixels() const { return frame_.data(); }
    int width()  const { return curW_; }
    int height() const { return curH_; }

    // Interface MMIO ($FF8200-$FF8260) appelée par le Bus.
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // --- État exposé au débogueur (lecture directe) -------------------------
    uint32_t videoBase = 0;                 // adresse RAM du framebuffer (registres haut/milieu/bas)
    std::array<uint16_t, 16> palette{};     // 16 registres couleur $FF8240 ($0RGB, 3 bits/canal)
    Mode mode = Mode::Low;                  // moniteur couleur → basse résolution par défaut

private:
    static uint32_t stColorToArgb(uint16_t c);   // $0RGB → ARGB8888
    void resizeFor(Mode m);                       // ajuste le buffer si la rés. change

    Bus&          bus_;
    int           curW_ = 0, curH_ = 0;     // résolution décodée courante
    std::vector<uint32_t> frame_;           // curW_*curH_ pixels ARGB
};
