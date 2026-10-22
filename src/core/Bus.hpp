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
// Glue.hpp est inclus (et non seulement déclaré) pour que le CHEMIN CHAUD du bus
// puisse lire `glue->memConfig_` en ligne — cf. mmuFastLimit(). C'est un en-tête
// autonome de 30 lignes sans dépendance : aucun cycle d'inclusion possible.
#include "core/MmuGlue.hpp"
#include "io/Fpu.hpp"
#include "io/Scu.hpp"
#include "io/StePads.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

class Shifter;
class YM2149;
class Mfp;
class Ikbd;
class Fdc;
class Cpu68k;
class DmaSound;
class Blitter;
class Rtc;
class MidiAcia;
class GemdosHd;
class Scc;
class Ne2000;
class Isp1160;
class Scheduler;
#include "core/CartDevice.hpp"
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

class StateArchive;

class Bus {
public:
    explicit Bus(std::size_t ramBytes = 512u * 1024u);

    // Save-state : pour l'instant la RAM (le gros bloc). ROM/cartouche NON sérialisées
    // (rechargées depuis leurs fichiers). L'état MMIO complet du Bus viendra ensuite.
    void serialize(StateArchive& ar);

    // Charge une image TOS ; positionne romBase selon la taille (192/256 Ko).
    bool loadTos(const std::string& path);

    // Charge une image de cartouche (port $FA0000, 128 Ko max) : ROM de
    // diagnostic (Test Kit) ou cartouche applicative. Le TOS détecte le magic
    // et amorce. Renvoie false si le fichier est introuvable ou trop gros.
    bool loadCart(const std::string& path);
    void ejectCart();
    const std::string& mountedCartPath() const { return cartPath_; }
    // La cartouche SYSTÈME GEMDOS est posée en mémoire sans fichier : son chemin doit
    // rester vide pour que GemdosHd::unmount sache qu'elle lui appartient.
    void clearCartPath() { cartPath_.clear(); }

    // -------------------------------------------------------------------------
    //  Accès vus par le CPU. Le 68000 est BIG-ENDIAN ; l'hôte (x86/arm64) est
    //  little-endian. On assemble donc les mots octet par octet : la RAM/ROM
    //  est stockée dans l'ordre natif ST, sans surprise pour le débogueur hexa.
    // -------------------------------------------------------------------------
    //
    //  CHEMIN CHAUD. Le décodage complet (overlay de boot, banques MMU, ROM,
    //  cartouche, MMIO) vit dans les variantes *Slow, hors ligne. Ici on ne traite
    //  que le cas ultra-majoritaire — RAM ordinaire en traduction identité — pour
    //  qu'il s'inline chez l'appelant (Moira, blitter, DMA). Cf. mmuFastLimit().
    uint8_t read8(uint32_t addr) {
        addr &= stmap::ADDR_MASK;
        // L'overlay de boot ne concerne QUE $0-$7 et ne vit que le temps de la
        // lecture SSP/PC au reset : il est exclu du chemin rapide, pas ignoré.
        if (addr < mmuFastLimit() && !(bootOverlay && addr < 8)) return ram[addr];
        // ROM TOS : deuxième source d'accès en volume — le code du TOS (et du GEM)
        // s'EXÉCUTE depuis la ROM, chaque mot d'opcode passe donc ici. `addr-romBase`
        // en arithmétique non signée : sous romBase le décalage déborde et le test
        // échoue, pas besoin d'une seconde comparaison.
        const uint32_t off = addr - romBase;
        if (off < rom.size()) return rom[off];
        return read8Slow(addr);
    }
    void write8(uint32_t addr, uint8_t v) {
        addr &= stmap::ADDR_MASK;
        // (pas de garde overlay : write8 n'en a jamais eu — l'overlay est une
        //  redirection de LECTURE, l'écriture va bien en RAM.)
        // DEBUG (NEOST_WATCH=hex, cf. Machine.cpp) : trace datée des écritures dans
        // [watchBase_, watchBase_+0x300) — l'équivalent du breakpoint tracking
        // Hatari « b ($addr).w ! ($addr).w :trace ». Désarmé : un test de bool.
        if (watchHook && addr - watchBase_ < 0x300u) watchHook(addr, v);
        if (addr < mmuFastLimit()) { ram[addr] = v; return; }
        write8Slow(addr, v);
    }
    // read16 a son propre chemin rapide plutôt que deux read8 : c'est l'accès le
    // plus fréquent de tous (chaque mot de code lu par le 68000 y passe), et la
    // revalidation du cache MMU n'est ainsi payée qu'une fois au lieu de deux.
    // ⚠ ioAccessWidth_ n'est PAS posé ici : il n'est consulté que par les
    // gestionnaires MMIO et par la zone RAM « void », deux chemins que ce raccourci
    // ne peut pas atteindre par construction.
    uint16_t read16(uint32_t addr) {
        addr &= stmap::ADDR_MASK;
        if (addr + 1 < mmuFastLimit() && !(bootOverlay && addr < 8))
            return uint16_t((ram[addr] << 8) | ram[addr + 1]);
        const uint32_t off = addr - romBase;             // cf. read8 : non signé
        // ⚠ `off + 1` seul déborderait à 0 quand off == 0xFFFFFFFF (addr == romBase-1,
        // p.ex. $DFFFFF sous un TOS 256 Ko) → `0 < rom.size()` vrai → rom[0xFFFFFFFF]
        // (lecture hôte hors bornes). Le premier test `off < rom.size()` coupe ce cas
        // AVANT l'addition ; le second gère le mot à cheval sur la fin de ROM.
        if (off < rom.size() && off + 1 < rom.size())
            return uint16_t((rom[off] << 8) | rom[off + 1]);
        return read16Slow(addr);
    }
    uint32_t read32(uint32_t addr);
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

    // Lecture SANS effet de bord pour le débogueur/désassembleur (équivalent du
    // get_iword_debug d'Hatari) : RAM/ROM/cartouche lues telles quelles, MMIO et
    // trous → 0xFF SANS dispatcher vers les puces (une lecture $FFFC02 ou $FF8604
    // depuis le débogueur ne doit ni consommer l'état ACIA/FDC ni facturer de
    // wait states — cf. Cpu68k::read16Dasm).
    uint8_t  peek8 (uint32_t addr) const;
    uint16_t peek16(uint32_t addr) const { return uint16_t((peek8(addr) << 8) | peek8(addr + 1)); }

    // Recopie des vecteurs reset SSP/PC ($0-$7) depuis la ROM en RAM — port de
    // STMemory_SetDefaultConfig (stMemory.c:251) : sur le vrai ST le GLUE mappe
    // ces 8 octets sur la ROM en permanence ; sans cette copie, l'idiome de
    // reboot à chaud « move.l $4.w,a0 ; jmp (a0) » lirait 0. À appeler à chaque
    // reset machine et après chargement d'une ROM.
    void     seedResetVectors();

    // Fenêtre d'adresses décodée vers la ROM TOS (port memory.c map_banks ROMmem) :
    // une ROM à $E00000 répond sur TOUT $E00000-$EFFFFF (1 Mo, 16 banques), pas
    // seulement sur la taille du fichier — au-delà du TOS chargé on lit 0 (tampon
    // Hatari à zéro), jamais de bus error en LECTURE ; les ÉCRITURES fautent sur
    // toute la fenêtre. Une ROM historique à $FC0000 occupe 3 banques (192 Ko =
    // exactement le fichier).
    // STE : le GST MCU (netlist Atari du 4081, archive de C. Zietz) ne décode que
    // /ROM2-/ROM1-/ROM0 = $E00000-$EBFFFF (768 Ko) ; $EC0000-$EFFFFF n'a pas de
    // DTACK → bus error. Écart assumé avec Hatari (1 Mo), aligné sur le matériel —
    // cf. docs/HATARI_DIVERGENCES.md. Mega STE inchangé (pas de netlist).
    uint32_t romWindowSize() const {
        if (romBase != stmap::ROM_E00000) return 0x30000u;
        return machine == MachineType::Ste ? 0xC0000u : 0x100000u;
    }

    // STE : sélections ROM du GST MCU décodées (DTACK) sans puce derrière, hors
    // fenêtre TOS — /ROM6 $D00000-$D3FFFF, /ROM5 $D40000-$D7FFFF et /ROMP
    // $FE0000-$FE1FFF. Source : netlist Atari du 4081 (archive ASIC de C. Zietz,
    // 4081P2, porte AF8 « ROMXB » = DTACK ROM : IROM0-4, /ROM5, /ROM6, /ROMP) ; le
    // « irom6 | irom6 » de gstmcu.v (MiSTer) est une coquille. En LECTURE, pas de
    // bus error : bus ouvert. Les écritures fautent toujours (lecture seule).
    // Bit « gamecart » posé ($FF9000) : /ROM4 et /ROM3 passent en $D80000-$DBFFFF
    // et $DC0000-$DFFFFF (cf. steCartRelocated).
    bool steRomSelectVoid(uint32_t addr) const {
        return machine == MachineType::Ste &&
               ((addr >= 0xD00000u && addr < 0xD80000u) ||
                (addr >= 0xFE0000u && addr < 0xFE2000u));
    }
    // Registre « gamecart » du GST MCU ($FF9000, bit 0 de l'octet pair ; netlist
    // 4081P10 : bascule GAMECART, D = ID8, effacée au reset RESB/PORB). À 1, le
    // port cartouche quitte $FA0000-$FBFFFF (bus error) pour $D80000-$DFFFFF.
    bool gameCart = false;
    // La cartouche SYSTÈME du HD GEMDOS (invention de NeoST, absente d'un vrai STE)
    // ne déménage pas : son trap #1 vit en $FA002A et doit rester joignable.
    bool steCartRelocated() const { return machine == MachineType::Ste && gameCart && !gemdos; }
    bool steCartWindow(uint32_t addr) const {
        return steCartRelocated() && addr >= 0xD80000u && addr < 0xE00000u;
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
    //  CHEMIN CHAUD : le CPU appelle busFaultN à CHAQUE accès (~5,5 % des
    //  instructions au profil callgrind, presque entièrement pour rien). La RAM
    //  ordinaire — au-dessus des variables système, sous les 4 Mo — ne faute JAMAIS,
    //  ni en lecture ni en écriture, ni en mode utilisateur : aucun des trois étages
    //  ci-dessous ne la concerne (étage 1 = ROM/cartouche/$0-$7, étage 2 = $0-$7FF et
    //  espace IO, étage 3 = busFault(), dont la toute première ligne renvoie false
    //  pour tout `addr < $400000`). On court-circuite donc l'appel hors ligne.
    bool busFaultN(uint32_t addr, unsigned n, bool write) const {
        addr &= stmap::ADDR_MASK;
        if (addr >= stmap::VECTORS_END && addr < 0x400000) return false;
        // LECTURE en ROM TOS : jamais fautive sur toute la fenêtre décodée (étage 3
        // → busFault). L'ÉCRITURE, elle, faute toujours (étage 1) → chemin complet.
        if (!write && addr >= romBase && addr < romBase + romWindowSize()) return false;
        return busFaultNSlow(addr, n, write);
    }
    bool busFaultNSlow(uint32_t addr, unsigned n, bool write) const;

    // Auto-test DÉTERMINISTE du modèle de bus error (whitelist par octet) : vérifie
    // que RAM/ROM/cartouche ne fautent pas, que $FF0000-$FF7FFF et les trous fautent,
    // et l'INVARIANT clé (un word ne faute que si TOUS ses octets fautent) sur une
    // frontière trouvée dynamiquement dans l'espace IO. Force le superviseur (la branche
    // user-mode ferait tout fauter). Renvoie true si OK ; détaille sur stderr. Appelé par
    // neost-headless --bus-selftest.
    bool busSelfTest();

    // Largeur de l'accès MMIO en cours (1/2/4 octets). Les registres FDC $FF8604/06
    // ne tolèrent que les mots (Hatari nIoMemAccessSize) ; read16/write16 posent 2.
    uint8_t ioAccessWidth() const { return ioAccessWidth_; }

    // Composants branchés sur le bus (injectés par main.cpp, pas de propriété).
    // Broche /RESET du 68000 (instruction RESET, $4E70) : réinitialise les PÉRIPHÉRIQUES
    // sans toucher au CPU ni à l'ordonnanceur — port de customreset() d'Hatari
    // (cpu/hatari-glue.c:54). Un loader de jeu/démo s'en sert pour faire taire la machine
    // héritée du TOS ; sans elle, le YM continuait de jouer, les timers MFP du TOS
    // tiraient des IRQ dans le code du jeu, et le moteur disquette restait en rotation.
    void peripheralReset();

    Shifter* shifter = nullptr;
    YM2149*  psg     = nullptr;
    MmuGlue* glue    = nullptr;
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
    // Fenêtre CPU du blitter non-hog (port BLITTER_PHASE_COUNT_CPU_BUS, modèle CE
    // d'Hatari) : true = le blitter attend que le CPU consomme ses 64 accès bus ;
    // chaque accès CPU est signalé (Blitter::noteCpuBusAccess), le 64ᵉ relance le
    // blitter. Champ ICI pour la même raison que blitterWinStart/End.
    bool    blitterCountCpu = false;
    Rtc*     rtc     = nullptr;   // horloge RP5C15 ($FFFC21) — Mega ST / Mega STE
    MidiAcia* midi   = nullptr;   // ACIA MIDI ($FFFC04) — bouclage OUT→IN
    Cpu68k*  cpu     = nullptr;   // pour rafraîchir l'IPL après un accès MMIO
    // Vol de cycles bus par un maître DMA (blitter, DMA disquette…) — A23 : la
    // primitive UNIQUE. Elle stalle le CPU (addBusWaitCycles) et, HORS quantum
    // seulement (callback d'échéance, discriminé par Cpu68k::inRun), crédite
    // l'ordonnanceur (addStolenCycles) — dans le quantum, `ran` capte déjà l'avance
    // et créditer la compterait deux fois. Ce couple invariant vivait recopié dans
    // Blitter::billCycles ET Fdc::billDmaCycles (« MÊME INVARIANT QUE BL3 ») ; le
    // troisième maître de bus l'aurait recopié une troisième fois.
    void stealBusCycles(Scheduler* sched, int n);
    // Watch d'écriture (debug, cf. write8) : hook posé par Machine si NEOST_WATCH.
    uint32_t watchBase_ = 0xFF000000u;
    std::function<void(uint32_t, uint8_t)> watchHook;
    // Émulation disque dur GEMDOS (redirection des appels GEMDOS vers un dossier
    // hôte, port de Hatari gemdos.c). Non nul = HD GEMDOS actif : le CPU intercepte
    // alors les opcodes magiques de la cartouche système (cf. Cpu68k::run). Posé par
    // le frontend via GemdosHd::setDirectory. Cf. io/GemdosHd.hpp.
    GemdosHd* gemdos = nullptr;
    // Contrôleur série SCC Z85C30 ($FF8C80-$FF8C87) — Mega STE uniquement. IRQ niv5
    // vectorisée gatée par le SCU. Posé par Machine (Mega STE). Cf. io/Scc.hpp.
    Scc*     scc    = nullptr;
    // Carte réseau NE2000 sur le port cartouche (EtherNEC — extension NeoST).
    // Non nul + activée = les lectures dans $FA0000-$FBFFFF sont décodées comme
    // accès EtherNEC AVANT la ROM cartouche. Posé par Machine. Cf. io/Ne2000.hpp.
    Ne2000*  ne2000 = nullptr;
    // Contrôleur hôte USB ISP1160 du NetUSBee (extension NeoST) : fenêtres
    // $FA0000/$FA8000/$FB8000/$FBC000 décodées AVANT la NE2000 — qui peut voir le
    // même accès (latch LSB = registre CR, cf. io/Isp1160.hpp). Posé par Machine.
    Isp1160* isp1160 = nullptr;
    // Périphériques du port cartouche abonnés aux signaux /ROM3, /ROM4, /UDS
    // (clés de protection…) : consultés dans l'ordre, avant la ROM cartouche et le
    // GEMDOS HD. Cf. core/CartDevice.hpp. Posés par Machine (addCartDevice).
    std::vector<CartDevice*> cartDevices;
    bool udsObserved = false;   // au moins un abonné veut /UDS (évite la boucle par cycle)
    void addCartDevice(CartDevice* d) { removeCartDevice(d); cartDevices.push_back(d); refreshUds(); }
    void removeCartDevice(CartDevice* d) {
        cartDevices.erase(std::remove(cartDevices.begin(), cartDevices.end(), d), cartDevices.end());
        refreshUds();
    }
    void refreshUds() {
        udsObserved = false;
        for (const CartDevice* d : cartDevices) udsObserved = udsObserved || d->wantsUds();
    }
    void udsCycle(uint32_t addr) { for (CartDevice* d : cartDevices) d->udsCycle(addr); }

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

    // Version du TOS chargé (mot big-endian de l'en-tête ROM, offset 2 ; 0x0104,
    // 0x0206…). Lue par loadTos, consultée par l'émulation GEMDOS HD (certains
    // comportements diffèrent selon la version, cf. GemdosHd).
    uint16_t tosVersion = 0;

    // Pointeur DIRECT dans ram[] pour un accès hôte contigu de `len` octets à partir
    // de l'adresse ST `addr` (traduction MMU incluse). Renvoie nullptr si la plage
    // n'est pas entièrement en RAM peuplée et contiguë. Utilisé par l'émulation
    // GEMDOS HD pour copier des données fichier en bloc (port de
    // STMemory_STAddrToPointer + STMemory_CheckAreaType). NE PAS utiliser pour la
    // MMIO ni au-delà de la RAM.
    uint8_t* hostRamPtr(uint32_t addr, uint32_t len);

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
    // Fpu.hpp — émulation fonctionnelle). Défaut : absent → bus error, la
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
    uint8_t  read8Slow (uint32_t addr);          // décodage complet (cf. read8)
    uint16_t read16Slow(uint32_t addr);
    void     write8Slow(uint32_t addr, uint8_t v);

    uint8_t  mmioRead8 (uint32_t addr);
    void     mmioWrite8(uint32_t addr, uint8_t v);

    // =========================================================================
    //  A31 — TABLE DE PLAGES MMIO (2026-08-28)
    //
    //  Avant : deux chaînes de `if` de ~110 lignes chacune (mmioRead8 /
    //  mmioWrite8), à ORDRE SÉMANTIQUE IMPLICITE. Ajouter une puce demandait de
    //  toucher les deux, au bon endroit, sans que rien ne dise où était « le bon
    //  endroit ». La table le dit : une ligne par puce, plage INCLUSIVE, porte
    //  explicite, et les deux dispatches se réduisent à une recherche.
    //
    //  ⚠ Ce que la table N'A PAS absorbé, et pourquoi : le port cartouche
    //  ($FA0000-$FBFFFF, read8Slow/write8Slow). L'ISP1160 et la NE2000 y voient
    //  TOUS DEUX le même accès — ce n'est pas un « premier qui répond gagne »,
    //  c'est un bus partagé sans schéma connu. Le forcer dans une table de
    //  premier-match serait le décrire faux.
    // =========================================================================
    struct MmioSlot {
        uint32_t lo, hi;                          // plage INCLUSIVE
        // Porte : puce câblée + modèle de machine + parité d'adresse quand elle
        // compte (le RTC ne décode que les adresses IMPAIRES). Rendre false =
        // l'accès retombe sur la GLUE, exactement comme avant.
        bool    (Bus::*claims)(uint32_t) const;
        uint8_t (Bus::*read)(uint32_t);
        void    (Bus::*write)(uint32_t, uint8_t);
        const char* name;
    };
    static const MmioSlot* mmioTable(std::size_t& count);
    const MmioSlot* mmioSlotFor(uint32_t addr) const;

    // Portes (cf. MmioSlot::claims).
    bool claimsShifter (uint32_t) const { return shifter != nullptr; }
    bool claimsPsg     (uint32_t) const { return psg != nullptr; }
    bool claimsFdc     (uint32_t) const { return fdc != nullptr; }
    bool claimsDmaSnd  (uint32_t) const { return dmasnd && machineHasDmaSound(machine); }
    bool claimsBlitter (uint32_t) const { return blitter && machineHasBlitter(machine); }
    bool claimsMfp     (uint32_t) const { return mfp != nullptr; }
    bool claimsIkbd    (uint32_t) const { return ikbd != nullptr; }
    bool claimsMidi    (uint32_t) const { return midi != nullptr; }
    // RTC RP5C15 : registres sur adresses IMPAIRES uniquement — une adresse paire
    // de la plage n'appartient PAS au RTC et doit retomber sur la glue.
    bool claimsRtc     (uint32_t a) const { return (a & 1) && rtc && machineIsMega(machine); }
    bool claimsStePads (uint32_t) const { return machineIsSte(machine); }
    bool claimsMsteCtrl(uint32_t) const { return machine == MachineType::MegaSte; }
    bool claimsFpu     (uint32_t) const { return machine == MachineType::MegaSte && fpu.present; }
    bool claimsScu     (uint32_t) const { return machine == MachineType::MegaSte; }
    bool claimsScc     (uint32_t) const { return machine == MachineType::MegaSte && scc; }

    // Handlers : les CORPS des anciennes branches, déplacés tels quels.
    uint8_t rdShifter(uint32_t a);  void wrShifter(uint32_t a, uint8_t v);
    uint8_t rdPsg    (uint32_t a);  void wrPsg    (uint32_t a, uint8_t v);
    uint8_t rdFdc    (uint32_t a);  void wrFdc    (uint32_t a, uint8_t v);
    uint8_t rdDmaSnd (uint32_t a);  void wrDmaSnd (uint32_t a, uint8_t v);
    uint8_t rdBlitter(uint32_t a);  void wrBlitter(uint32_t a, uint8_t v);
    uint8_t rdMfp    (uint32_t a);  void wrMfp    (uint32_t a, uint8_t v);
    uint8_t rdIkbd   (uint32_t a);  void wrIkbd   (uint32_t a, uint8_t v);
    uint8_t rdMidi   (uint32_t a);  void wrMidi   (uint32_t a, uint8_t v);
    uint8_t rdRtc    (uint32_t a);  void wrRtc    (uint32_t a, uint8_t v);
    uint8_t rdStePads(uint32_t a);  void wrStePads(uint32_t a, uint8_t v);
    uint8_t rdMsteCtl(uint32_t a);  void wrMsteCtl(uint32_t a, uint8_t v);
    uint8_t rdFpu    (uint32_t a);  void wrFpu    (uint32_t a, uint8_t v);
    uint8_t rdScu    (uint32_t a);  void wrScu    (uint32_t a, uint8_t v);
    uint8_t rdScc    (uint32_t a);  void wrScc    (uint32_t a, uint8_t v);

public:
    // Contrôle de COHÉRENCE de la table, appelable sans machine (cf.
    // tests/selftest_logic.cpp). Deux plages qui se recouvrent rendraient l'ordre
    // des lignes significatif — c'est-à-dire le défaut qu'A31 corrige. Renvoie
    // false et NOMME les deux coupables.
    static bool mmioTableDisjoint(const char** a, const char** b);
    // Nombre de plages décrites — pour qu'un test puisse dire « la table n'est pas
    // vide » plutôt que de valider le vide.
    static std::size_t mmioTableSize();
private:

    // Décodage de banques MMU ($FF8001) : adresse logique RAM (<4Mo) → index
    // physique dans ram[], ou -1 si la banque visée n'est pas peuplée (void).
    // Port fidèle de Hatari stMemory.c (RAS/CAS + aliasing). Cf. Bus.cpp.
    int64_t  mmuTranslate(uint32_t addr) const;

    // -------------------------------------------------------------------------
    //  Cache de décodage MMU — le gain le plus gros du chemin chaud.
    //
    //  mmuTranslate() refaisait le décodage COMPLET de $FF8001 à chaque octet :
    //  lecture de la config, deux `switch` de taille de banque, une division pour
    //  la taille de RAM posée, puis le remappage RAS/CAS. Le profil callgrind d'un
    //  boot TOS le mesurait à ~7 % des instructions du programme, pour 12,8 millions
    //  d'appels en 300 trames — alors que le résultat ne dépend QUE de deux
    //  entrées : l'octet de config et la taille de ram[].
    //
    //  On mémorise donc le décodage et on le REVALIDE par comparaison de ces deux
    //  entrées, plutôt que par une invalidation explicite posée aux sites d'écriture :
    //  impossible d'en oublier un (écriture $FF8001, changement de taille RAM,
    //  reset, chargement de save-state…), au prix de deux comparaisons.
    //
    //  mmuFastLimit_ = longueur du préfixe [0, limite) sur lequel la traduction est
    //  l'IDENTITÉ. C'est le cas dès qu'une banque est annoncée à sa taille réelle
    //  (ce que fait tout TOS après son sizing mémoire) : les remappages RAS/CAS se
    //  réduisent alors à `addr & (taille-1)` recollé au début de banque, soit `addr`.
    //  Config exotique (banque sur-déclarée pour un test de RAM, banque vide) →
    //  limite 0 ou réduite à la banque 0, et le décodage complet reprend la main.
    // -------------------------------------------------------------------------
    mutable uint32_t    mmuFastLimit_ = 0;
    mutable uint8_t     mmuCacheConf_ = 0xFF;               // 0xFF = jamais bâti
    mutable std::size_t mmuCacheRam_  = static_cast<std::size_t>(-1);
    // rebuildMmuCache dépend AUSSI de `machine` (règle banque 1 : ST/Mega ST lisent
    // les bits 0-1 de $FF8001, STE/Mega STE calquent la banque 1 sur la banque 0,
    // cf. Bus.cpp:245). `machine` doit donc faire partie de la clé de revalidation,
    // sinon une bascule ST↔STE à chaud (Machine::reconfigure, Machine.hpp:136) qui
    // ne change ni conf ni taille RAM laisserait le cache rance. (Aucune config
    // atteignable ne produit d'octet faux aujourd'hui — les limites ST/STE coïncident
    // sur les tailles standard — mais on ne laisse pas le piège armé.)
    mutable MachineType mmuCacheMachine_ = MachineType::Ste;
    void rebuildMmuCache(uint8_t conf) const;

    // Config mémoire effective. Sans Glue branchée (tests unitaires du Bus seul),
    // on la dérive de la RAM posée — exactement ce que faisait mmuTranslate.
    uint8_t mmuConfNow() const {
        return glue ? glue->memConfig_ : memConfigForBytes(ram.size());
    }
    uint32_t mmuFastLimit() const {
        const uint8_t conf = mmuConfNow();
        if (conf != mmuCacheConf_ || ram.size() != mmuCacheRam_ || machine != mmuCacheMachine_)
            rebuildMmuCache(conf);
        return mmuFastLimit_;
    }

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
