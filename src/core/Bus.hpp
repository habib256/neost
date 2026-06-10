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
#include "core/MachineType.hpp"
#include "io/Scu.hpp"
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
class DmaSound;
class Blitter;
class Rtc;
class MidiAcia;
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

    // $FA0000-$FBFFFF : port cartouche (128 Ko max). Au reset, TOS/EmuTOS y lit
    // un long word à $FA0000 : magic $FA52235F → cartouche de DIAGNOSTIC (le CPU
    // saute aussitôt en $FA0004, avant même l'init RAM) ; magic $ABCDEF42 →
    // cartouche applicative classique (lancée après l'init du TOS). NeoST n'a qu'à
    // exposer la ROM cartouche ici : c'est le TOS qui détecte le magic et amorce.
    constexpr uint32_t CART_BASE      = 0xFA0000;
    constexpr uint32_t CART_END       = 0xFC0000; // exclu : $FA0000..$FBFFFF (128 Ko)

    // $FF8000-$FFFFFF : espace des registres matériels (MMIO).
    constexpr uint32_t MMIO_BASE      = 0xFF8000;

    // Décodage MMIO par puce (bornes basses, cf. dispatch dans Bus.cpp) :
    constexpr uint32_t MMU_CONFIG     = 0xFF8001; // config mémoire (banques)
    constexpr uint32_t SHIFTER_BASE   = 0xFF8200; // vidéo : base, palette, rés.
    constexpr uint32_t SHIFTER_END    = 0xFF827F; // inclut scroll fin STE ($FF8264/65), linewidth ($FF820F)…
    constexpr uint32_t DMA_FDC_BASE   = 0xFF8600; // disquette / DMA
    constexpr uint32_t PSG_BASE       = 0xFF8800; // YM2149 (son)
    constexpr uint32_t DMASND_BASE    = 0xFF8900; // son DMA STE ($FF8900-$FF8925)
    constexpr uint32_t DMASND_END     = 0xFF8940;
    constexpr uint32_t MFP_BASE       = 0xFFFA00; // MFP 68901 (timers/IRQ)
    constexpr uint32_t ACIA_BASE      = 0xFFFC00; // clavier (IKBD) / MIDI

    constexpr uint32_t ADDR_MASK      = 0x00FFFFFF; // 24 bits utiles
}

class Bus {
public:
    explicit Bus(std::size_t ramBytes = 512u * 1024u);

    // Charge une image TOS ; positionne romBase selon la taille (192/256 Ko).
    bool loadTos(const std::string& path);

    // Charge une image de cartouche (port $FA0000, 128 Ko max) : ROM de
    // diagnostic (Test Kit) ou cartouche applicative. Le TOS détecte le magic
    // et amorce. Renvoie false si le fichier est introuvable ou trop gros.
    bool loadCart(const std::string& path);
    void ejectCart();
    const std::string& mountedCartPath() const { return cartPath_; }

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

    // Vrai si un accès OCTET à `addr` provoque une bus error sur le 68000 (aucun
    // circuit ne décode l'octet). Modèle porté de Hatari (ioMem.c + memory.c) :
    // tout l'espace $FF8000-$FFFFFF est bus error PAR DÉFAUT, puis on « whiteliste »
    // les registres réellement câblés selon le modèle (cf. ioFault_). Hors IO, les
    // trous $400000-$F9FFFF et $FF0000-$FF7FFF fautent aussi. Le matériel optionnel
    // (blitter, son DMA) est détecté par EmuTOS via ces bus errors.
    bool busFault(uint32_t addr) const;

    // Bus error pour un accès de `n` octets (1/2/4) à partir de `addr`. Règle
    // matérielle (Hatari) : un accès word/long ne FAUTE que si TOUS ses octets
    // tombent en zone bus error. Ainsi `move.w $FF8204` fonctionne (octet pair
    // fautif + octet impair valide) alors que `move.b $FF8204` faute.
    bool busFaultN(uint32_t addr, unsigned n) const;

    // Largeur de l'accès MMIO en cours (1/2/4 octets). Les registres FDC $FF8604/06
    // ne tolèrent que les mots (Hatari nIoMemAccessSize) ; read16/write16 posent 2.
    uint8_t ioAccessWidth() const { return ioAccessWidth_; }

    // Composants branchés sur le bus (injectés par main.cpp, pas de propriété).
    Shifter* shifter = nullptr;
    YM2149*  psg     = nullptr;
    Glue*    glue    = nullptr;
    Mfp*     mfp     = nullptr;   // contrôleur d'interruptions 68901
    Ikbd*    ikbd    = nullptr;   // ACIA clavier
    Fdc*     fdc     = nullptr;   // contrôleur disquette + DMA
    DmaSound* dmasnd = nullptr;   // son DMA STE ($FF8900) — optionnel
    Blitter* blitter = nullptr;   // blitter ($FF8A00) — Mega ST / STE / Mega STE
    Rtc*     rtc     = nullptr;   // horloge RP5C15 ($FFFC21) — Mega ST / Mega STE
    MidiAcia* midi   = nullptr;   // ACIA MIDI ($FFFC04) — bouclage OUT→IN
    Cpu68k*  cpu     = nullptr;   // pour rafraîchir l'IPL après un accès MMIO

    // Profil machine : décide quel matériel optionnel répond (son DMA STE, etc.)
    // et où une bus error se produit. Posé par Machine. Défaut : 1040 STE.
    MachineType machine = MachineType::Ste;

    // Données brutes exposées au débogueur (visualiseur hexa ImGui). Pas de
    // getters : l'accès direct est l'objet même de la "boîte à hack".
    std::vector<uint8_t> ram;
    std::vector<uint8_t> rom;
    std::vector<uint8_t> cart;        // ROM cartouche ($FA0000) — vide si absente
    uint32_t romBase = stmap::ROM_E00000;

    // Overlay de boot : au reset, le 68000 lit SSP ($0) et PC ($4). Le GLUE
    // route ces tout premiers accès vers la ROM. On désactive l'overlay dès
    // que les vecteurs ont été lus (cf. Cpu68k::reset), pour rendre ensuite
    // la vraie table des vecteurs en RAM accessible aux exceptions.
    bool bootOverlay = true;

    // Registre Cache/CPU MegaSTE $FF8E21 (octet) : bit0 = cache 16 Ko (0=off),
    // bit1 = vitesse CPU (0=8 MHz, 1=16 MHz). Latché et relisible (cf. Hatari
    // IoMemTabMegaSTE_CacheCpuCtrl_WriteByte) — l'EFFET réel (débit cycles, cache)
    // relève d'items « précision cycle » séparés. Reset = 0 (8 MHz, sans cache).
    uint8_t megaSteCacheCtrl = 0;

    // SCU MegaSTE ($FF8E01-$FF8E0F) : gate d'interruptions (cf. Scu.hpp), TOUJOURS actif
    // sur MegaSTE (comme `SCU_IsEnabled` d'Hatari). La livraison d'IPL le consulte dans
    // Cpu68k::neostUpdateIpl.
    Scu scu;

private:
    uint8_t  mmioRead8 (uint32_t addr);
    void     mmioWrite8(uint32_t addr, uint8_t v);
    // Décodage de banques MMU ($FF8001) : adresse logique RAM (<4Mo) → index
    // physique dans ram[], ou -1 si la banque visée n'est pas peuplée (void).
    // Port fidèle de Hatari stMemory.c (RAS/CAS + aliasing). Cf. Bus.cpp.
    int64_t  mmuTranslate(uint32_t addr) const;

    // Carte de bus error de l'espace IO ($FF8000-$FFFFFF), 1 octet par adresse
    // (1 = bus error, 0 = registre câblé ou « void »). Construite à la demande
    // depuis les tables Hatari (ioMemTabST/STE) selon `machine`. Cf. buildIoFault.
    mutable std::vector<uint8_t> ioFault_;
    mutable MachineType          ioFaultMachine_ = MachineType::St;
    mutable bool                 ioFaultBuilt_   = false;
    void buildIoFault() const;       // (re)construit ioFault_ pour `machine`

    mutable uint8_t ioAccessWidth_ = 1;   // largeur accès CPU courant (cf. ioAccessWidth)

    std::string cartPath_;           // chemin de l'image cartouche montée
};
