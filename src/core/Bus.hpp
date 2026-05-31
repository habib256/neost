// =============================================================================
//  Bus.hpp — Le bus de données / Memory Map de l'Atari ST
//
//  Philosophie NeoST : ce fichier EST la carte mère vue par le 68000. Toute
//  lecture/écriture du CPU transite ici puis est aiguillée vers la RAM, la ROM
//  (TOS) ou un composant MMIO (Shifter, PSG, GLUE...). Rien n'est caché.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class Shifter;
class YM2149;
class Glue;
class Mfp;
class Ikbd;
class Fdc;
class Cpu68k;

// -----------------------------------------------------------------------------
//  Plan mémoire de l'Atari ST (bus d'adresses 24 bits → 16 Mo adressables).
//  Les constantes documentent le POURQUOI de chaque zone.
// -----------------------------------------------------------------------------
namespace stmap {
    // $000000-$0007FF : variables système + table des vecteurs d'exception.
    // C'est de la RAM, mais protégée en écriture en mode superviseur par le MMU
    // une fois le boot terminé (on ne modélise pas la protection ici).
    constexpr uint32_t VECTORS_END   = 0x000800;

    // La RAM ST commence à $000000. Taille selon la config (512 Ko à 4 Mo).
    constexpr uint32_t RAM_BASE       = 0x000000;

    // ROM TOS. Deux emplacements historiques selon la version :
    //  - $FC0000 : TOS 1.00-1.02, 192 Ko répartis sur 6 puces (ST d'origine).
    //  - $E00000 : TOS >= 1.04 ("Rainbow"), 256 Ko (STE, Mega STE...).
    // Le 68000 démarre en lisant SSP/PC à $0 ; voir bootOverlay ci-dessous.
    constexpr uint32_t ROM_FC0000     = 0xFC0000;
    constexpr uint32_t ROM_E00000     = 0xE00000;

    // $FF8000-$FFFFFF : espace des registres matériels (MMIO).
    constexpr uint32_t MMIO_BASE      = 0xFF8000;

    // Décodage MMIO par puce (bornes basses, cf. dispatch dans Bus.cpp) :
    constexpr uint32_t MMU_CONFIG     = 0xFF8001; // config mémoire (banques)
    constexpr uint32_t SHIFTER_BASE   = 0xFF8200; // vidéo : base, palette, rés.
    constexpr uint32_t SHIFTER_END    = 0xFF8260;
    constexpr uint32_t DMA_FDC_BASE   = 0xFF8600; // disquette / DMA
    constexpr uint32_t PSG_BASE       = 0xFF8800; // YM2149 (son)
    constexpr uint32_t MFP_BASE       = 0xFFFA00; // MFP 68901 (timers/IRQ)
    constexpr uint32_t ACIA_BASE      = 0xFFFC00; // clavier (IKBD) / MIDI

    constexpr uint32_t ADDR_MASK      = 0x00FFFFFF; // 24 bits utiles
}

class Bus {
public:
    explicit Bus(std::size_t ramBytes = 512u * 1024u);

    // Charge une image TOS ; positionne romBase selon la taille (192/256 Ko).
    bool loadTos(const std::string& path);

    // -------------------------------------------------------------------------
    //  Accès vus par le CPU. Le 68000 est BIG-ENDIAN ; l'hôte (x86/arm64) est
    //  little-endian. On assemble donc les mots octet par octet : la RAM/ROM
    //  est stockée dans l'ordre natif ST, sans surprise pour le débogueur hexa.
    // -------------------------------------------------------------------------
    uint8_t  read8 (uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint32_t read32(uint32_t addr);
    void     write8 (uint32_t addr, uint8_t  v);
    void     write16(uint32_t addr, uint16_t v);
    void     write32(uint32_t addr, uint32_t v);

    // Vrai si l'adresse n'est décodée par AUCUN circuit → bus error sur le 68000.
    // Le matériel optionnel (blitter, etc.) est détecté par EmuTOS justement en
    // provoquant ces bus errors : sans elles, EmuTOS croit le matériel présent.
    bool busFault(uint32_t addr) const;

    // Composants branchés sur le bus (injectés par main.cpp, pas de propriété).
    Shifter* shifter = nullptr;
    YM2149*  psg     = nullptr;
    Glue*    glue    = nullptr;
    Mfp*     mfp     = nullptr;   // contrôleur d'interruptions 68901
    Ikbd*    ikbd    = nullptr;   // ACIA clavier
    Fdc*     fdc     = nullptr;   // contrôleur disquette + DMA
    Cpu68k*  cpu     = nullptr;   // pour rafraîchir l'IPL après un accès MMIO

    // Données brutes exposées au débogueur (visualiseur hexa ImGui). Pas de
    // getters : l'accès direct est l'objet même de la "boîte à hack".
    std::vector<uint8_t> ram;
    std::vector<uint8_t> rom;
    uint32_t romBase = stmap::ROM_E00000;

    // Overlay de boot : au reset, le 68000 lit SSP ($0) et PC ($4). Le GLUE
    // route ces tout premiers accès vers la ROM. On désactive l'overlay dès
    // que les vecteurs ont été lus (cf. Cpu68k::reset), pour rendre ensuite
    // la vraie table des vecteurs en RAM accessible aux exceptions.
    bool bootOverlay = true;

private:
    uint8_t  mmioRead8 (uint32_t addr);
    void     mmioWrite8(uint32_t addr, uint8_t v);
};
