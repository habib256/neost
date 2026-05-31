// =============================================================================
//  Cpu68k.hpp — Wrapper C++ autour du core Musashi (Motorola 68000).
//
//  On NE réimplémente PAS le 68000 : Musashi est intégré en sous-module et
//  exposé via cette façade. Le wrapper relie les callbacks mémoire C de Musashi
//  à notre Bus, et expose juste ce qu'il faut au débogueur.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class Bus;

class Cpu68k {
public:
    explicit Cpu68k(Bus& bus);

    // Reset matériel : Musashi lit SSP ($0) et PC ($4) via le bus, puis on
    // referme l'overlay de boot de la ROM (cf. Bus::bootOverlay).
    void reset();

    // Exécute AU MOINS `cycles` cycles ; renvoie le nombre réellement consommé
    // (le 68000 termine toujours l'instruction en cours). La boucle d'horloge
    // s'en sert pour synchroniser le Shifter.
    int run(int cycles);

    // Pose une interruption auto-vectorisée (HBL=2, VBL=4 sur ST).
    void setIrq(int level);

    // État exposé en lecture directe pour le visualiseur de registres ImGui.
    uint32_t pc()  const;          // compteur programme courant
    uint32_t reg(int idx) const;   // 0-7 = D0-D7, 8-15 = A0-A7
    uint16_t sr()  const;          // status register
};
