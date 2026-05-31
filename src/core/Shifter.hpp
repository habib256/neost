// =============================================================================
//  Shifter.hpp — Puce vidéo de l'Atari ST (extraction du framebuffer).
//
//  Le Shifter lit la RAM vidéo de façon planaire et "pousse" les pixels vers
//  le bus vidéo. Ici on le modélise comme un générateur de texture OpenGL : la
//  boucle d'horloge appelle renderScanline() ligne par ligne, puis present()
//  téléverse le buffer ARGB dans une texture GL affichée en quad plein écran
//  (immediate mode, façon POM1/POM2). Squelette compact, Data-Oriented : on
//  décode dans un unique buffer linéaire ARGB.
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
    //   0 = basse  (320x200, 16 couleurs)
    //   1 = moyenne(640x200,  4 couleurs)
    //   2 = haute  (640x400, monochrome)
    enum class Mode : uint8_t { Low = 0, Medium = 1, High = 2 };

    explicit Shifter(Bus& bus);   // un contexte OpenGL doit être courant ici
    ~Shifter();

    // Décode UNE ligne du framebuffer ST vers le buffer ARGB interne.
    void renderScanline(int line);

    // Téléverse le buffer ARGB dans la texture GL et le dessine plein écran.
    void present();

    // Identifiant de la texture GL (pour l'afficher via ImGui::Image au besoin).
    unsigned int textureId() const { return texture_; }

    // Interface MMIO ($FF8200-$FF8260) appelée par le Bus.
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // --- État exposé au débogueur (lecture directe) -------------------------
    uint32_t videoBase = 0;                 // adresse RAM du framebuffer (registres haut/milieu/bas)
    std::array<uint16_t, 16> palette{};     // 16 registres couleur $FF8240 ($0RGB, 3 bits/canal)
    Mode mode = Mode::Low;

    static constexpr int WIDTH  = 320;      // dimensions logiques de la texture (basse rés.)
    static constexpr int HEIGHT = 200;

private:
    // Convertit une couleur ST $0RGB (3 bits/canal) en ARGB8888 hôte.
    static uint32_t stColorToArgb(uint16_t c);
    void decodeLineLow(int line);           // décodage planaire 4 plans / 16 couleurs

    Bus&          bus_;
    unsigned int  texture_ = 0;             // GLuint : texture streaming
    std::vector<uint32_t> frame_;           // WIDTH*HEIGHT pixels ARGB, mis à jour ligne par ligne
};
