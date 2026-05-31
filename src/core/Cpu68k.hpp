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
class Tracer;

class Cpu68k {
public:
    explicit Cpu68k(Bus& bus);

    // Branche (ou détache avec nullptr) le traceur : journalise chaque
    // instruction et chaque interruption prise. Utilisé surtout en headless.
    void setTracer(Tracer* t);

    // Reset matériel : Musashi lit SSP ($0) et PC ($4) via le bus, puis on
    // referme l'overlay de boot de la ROM (cf. Bus::bootOverlay).
    void reset();

    // Exécute AU MOINS `cycles` cycles ; renvoie le nombre réellement consommé
    // (le 68000 termine toujours l'instruction en cours). La boucle d'horloge
    // s'en sert pour synchroniser le Shifter.
    int run(int cycles);

    // Recalcule l'IPL présenté au 68000 à partir de l'état des sources
    // (MFP niveau 6, VBL niveau 4). À appeler après tout changement d'IRQ.
    void updateIpl();

    // Marque une interruption verticale (VBL, niveau 4 auto-vectorisé) en
    // attente ; elle sera acquittée puis effacée au cycle IACK.
    void raiseVbl();

    // Marque une interruption horizontale (HBL, niveau 2 auto-vectorisé) — une
    // par ligne visible ; gatée par le masque du SR (utilisée par les jeux).
    void raiseHbl();

    // État exposé en lecture directe pour le visualiseur de registres ImGui.
    uint32_t pc()  const;          // compteur programme courant
    uint32_t reg(int idx) const;   // 0-7 = D0-D7, 8-15 = A0-A7
    uint16_t sr()  const;          // status register
};
