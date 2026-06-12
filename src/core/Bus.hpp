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
#include "io/Fpu.hpp"
#include "io/Scu.hpp"
#include "io/StePads.hpp"
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

    // Accès mémoire des contrôleurs DMA (FDC, ACSI, son STE) — port de Hatari
    // stMemory.c STMemory_DMA_ReadByte/WriteByte : le DMA traverse le MÊME plan
    // mémoire que le CPU (traduction MMU / aliasing de banques inclus, ROM lisible)
    // mais ne déclenche JAMAIS de bus error — lire une zone fautive renvoie une
    // constante (0), y écrire est perdu. Pas de wait states ni de test superviseur :
    // ces protections sont propres aux accès CPU (BusMode d'Hatari).
    uint8_t  dmaRead8 (uint32_t addr);
    void     dmaWrite8(uint32_t addr, uint8_t v);

    // Fenêtre d'adresses décodée vers la ROM TOS (port memory.c map_banks ROMmem) :
    // une ROM à $E00000 répond sur TOUT $E00000-$EFFFFF (1 Mo, 16 banques), pas
    // seulement sur la taille du fichier — au-delà du TOS chargé on lit 0 (tampon
    // Hatari à zéro), jamais de bus error en LECTURE ; les ÉCRITURES fautent sur
    // toute la fenêtre. Une ROM historique à $FC0000 occupe 3 banques (192 Ko =
    // exactement le fichier).
    uint32_t romWindowSize() const {
        return romBase == stmap::ROM_E00000 ? 0x100000u : 0x30000u;
    }

    // Vrai si un accès OCTET à `addr` provoque une bus error sur le 68000 (aucun
    // circuit ne décode l'octet). Modèle porté de Hatari (ioMem.c + memory.c) :
    // tout l'espace $FF8000-$FFFFFF est bus error PAR DÉFAUT, puis on « whiteliste »
    // les registres réellement câblés selon le modèle (cf. ioFault_). Hors IO, les
    // trous $400000-$F9FFFF et $FF0000-$FF7FFF fautent aussi. Le matériel optionnel
    // (blitter, son DMA) est détecté par EmuTOS via ces bus errors.
    bool busFault(uint32_t addr) const;

    // Bus error pour un accès CPU de `n` octets (1/2/4) à partir de `addr`.
    // Trois étages, portés de Hatari :
    //  1. Écritures TOUJOURS fautives (ROMmem_put / SysMem_put) : ROM TOS, port
    //     cartouche, et les 8 premiers octets de RAM (miroir ROM des vecteurs reset).
    //  2. Mode UTILISATEUR (bit S du SR = 0) : $0-$7FF (variables système) et tout
    //     l'espace IO $FF8000-$FFFFFF sont réservés au superviseur (SysMem_get/put +
    //     is_super_access d'ioMem.c) — c'est la « protection mémoire » du GLUE/MMU.
    //     Seul le CPU est concerné : blitter/DMA passent par read8/write8 sans ce test
    //     (équivalent BusMode != BUS_MODE_CPU d'Hatari).
    //  3. Carte par octet (whitelist) : un accès word/long ne FAUTE que si TOUS ses
    //     octets tombent en zone bus error. Ainsi `move.w $FF8204` fonctionne (octet
    //     pair fautif + octet impair valide) alors que `move.b $FF8204` faute.
    bool busFaultN(uint32_t addr, unsigned n, bool write) const;

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
    // Fenêtre PRE_START du blitter non-hog (cycles bus ABSOLUS, -1 = inactive) :
    // un accès bus CPU daté dans [start, end) est signalé au blitter, qui le compte
    // à tort comme sien — bug « 63 accès » (cf. Blitter::notePreStartCpuAccess).
    // Champs ICI pour que le test du chemin chaud CPU soit un simple load (Cpu68k.cpp).
    int64_t blitterWinStart = -1;
    int64_t blitterWinEnd   = -1;
    Rtc*     rtc     = nullptr;   // horloge RP5C15 ($FFFC21) — Mega ST / Mega STE
    MidiAcia* midi   = nullptr;   // ACIA MIDI ($FFFC04) — bouclage OUT→IN
    Cpu68k*  cpu     = nullptr;   // pour rafraîchir l'IPL après un accès MMIO

    // Profil machine : décide quel matériel optionnel répond (son DMA STE, etc.)
    // et où une bus error se produit. Posé par Machine. Défaut : 1040 STE.
    MachineType machine = MachineType::Ste;

    // Dernier mot vu sur le bus de DONNÉES par le CPU (≈ regs.db du cœur UAE,
    // mis à jour par les overrides mémoire de NeostMoira : mot = valeur, octet =
    // valeur dupliquée sur les deux voies). Une lecture dans la zone RAM « void »
    // (banque absente / au-delà de la config MMU, < $400000) relit ce mot au lieu
    // de 0 : rien ne pilote le bus, il garde sa dernière valeur (cf. Hatari
    // VoidMem_bget/wget → regs.db).
    uint16_t cpuDb = 0;

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
    // IoMemTabMegaSTE_CacheCpuCtrl_WriteByte). L'écriture applique l'EFFET réel :
    // bascule du débit cycles via Cpu68k::setMegaSteSpeed + invalidation du cache
    // si bit0 retombe. Reset = 0 (8 MHz, sans cache) — cf. megaSteReset().
    uint8_t megaSteCacheCtrl = 0;

    // ---- Cache externe 16 Ko du Mega STE (port Hatari m68000.c MegaSTE_Cache_*) --
    // 8192 lignes × 1 mot (2 puces TAG RAM 35 ns + 2 RAM 85 ns sans wait state).
    // Adresse 24 bits → tag = bits 14-23, ligne = bits 1-13, bit 0 ignoré (mots).
    // Ici : les DONNÉES seulement (tags + valeurs) ; la facturation des cycles
    // (hit = 4 cycles CPU 16 MHz, miss = accès cadencé bus 8 MHz) est faite par le
    // cœur Moira dans Cpu68k.cpp. Cachable : RAM ST (< 4 Mo) toujours, ROM TOS en
    // lecture ; JAMAIS l'IO ni la cartouche. Invalidé par : bit0 de $FF8E21 → 0,
    // reset, bus error, et usage de BGACK (blitter / DMA disque) — cf. les appels
    // à megaSteCacheFlushIfEnabled().
    struct MegaSteCache {
        uint8_t  valid[8192];
        uint16_t tag[8192];
        uint16_t value[8192];
    };
    MegaSteCache megaSteCache = {};
    bool megaSteCacheEnabled() const { return (megaSteCacheCtrl & 0x01) != 0; }
    void megaSteCacheFlush();
    void megaSteCacheFlushIfEnabled() { if (megaSteCacheEnabled()) megaSteCacheFlush(); }
    // `super` = bit S du SR au moment de l'accès (un accès user < $800 fauterait,
    // donc n'est jamais caché — cf. MegaSTE_Cache_Addr_Cacheable).
    bool megaSteCacheable(uint32_t addr, int size, bool write, bool super) const;
    bool megaSteCacheRead(uint32_t addr, int size, uint16_t& val, bool super);
    bool megaSteCacheUpdate(uint32_t addr, int size, uint16_t val, bool write, bool super);
    // Reset matériel du couple cache/vitesse (MegaSTE_CPU_Cache_Reset) : $FF8E21=0.
    // L'appelant repasse aussi le CPU à 8 MHz (Cpu68k::setMegaSteSpeed(false)).
    void megaSteReset() { megaSteCacheCtrl = 0; megaSteCacheFlush(); }

    // Coprocesseur MC68881 OPTIONNEL du Mega STE, mappé $FFFA40-$FFFA5F (cf.
    // Fpu.hpp — niveau « sonde + trapping »). Défaut : absent → bus error, la
    // sonde TOS/diagnostic conclut « FPU not found » comme Hatari. Passer par
    // setFpuPresent (et non fpu.present directement) : la carte des bus errors
    // ($FFFA40 whitelisté ou non) doit être reconstruite.
    Fpu fpu;
    void setFpuPresent(bool present) { fpu.present = present; ioFaultBuilt_ = false; }

    // SCU MegaSTE ($FF8E01-$FF8E0F) : gate d'interruptions (cf. Scu.hpp), TOUJOURS actif
    // sur MegaSTE (comme `SCU_IsEnabled` d'Hatari). La livraison d'IPL le consulte dans
    // Cpu68k::neostUpdateIpl.
    Scu scu;

    // Joypads / paddles / lightpen STE et Mega STE ($FF9200-$FF9222). Alimenté par
    // le MÊME état joystick que l'IKBD (les frontends appellent setJoystick sur les
    // deux). Cf. StePads.hpp pour le multiplexage et les DIP switches Mega STE.
    StePads stePads;

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
