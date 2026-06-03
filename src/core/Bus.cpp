// =============================================================================
//  Bus.cpp — Implémentation du Memory Map et du dispatch MMIO.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Bus.hpp"
#include "core/Shifter.hpp"
#include "core/YM2149.hpp"
#include "core/Glue.hpp"
#include "core/Cpu68k.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"
#include "io/Fdc.hpp"
#include "core/DmaSound.hpp"
#include "core/Blitter.hpp"
#include "io/Rtc.hpp"
#include "io/MidiAcia.hpp"

#include <cstdio>
#include <fstream>

Bus::Bus(std::size_t ramBytes) {
    ram.assign(ramBytes, 0);
}

bool Bus::loadTos(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[Bus] TOS introuvable : %s\n", path.c_str());
        return false;
    }
    const std::streamsize n = f.tellg();
    f.seekg(0);
    rom.resize(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(rom.data()), n);

    // L'emplacement de la ROM dépend de la version de TOS (cf. stmap) : un TOS
    // de 192 Ko vit à $FC0000, sinon (224/256 Ko) à $E00000.
    romBase = (rom.size() <= 192u * 1024u) ? stmap::ROM_FC0000 : stmap::ROM_E00000;
    std::fprintf(stderr, "[Bus] TOS chargé : %s (%zu Ko @ $%06X)\n",
                 path.c_str(), rom.size() / 1024, romBase);
    return true;
}

bool Bus::loadCart(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[Bus] cartouche introuvable : %s\n", path.c_str());
        return false;
    }
    const std::streamsize n = f.tellg();
    const std::size_t maxSize = stmap::CART_END - stmap::CART_BASE;   // 128 Ko
    if (n <= 0 || static_cast<std::size_t>(n) > maxSize) {
        std::fprintf(stderr, "[Bus] cartouche invalide (%lld o, max %zu o) : %s\n",
                     static_cast<long long>(n), maxSize, path.c_str());
        return false;
    }
    f.seekg(0);
    cart.resize(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(cart.data()), n);

    // Le magic du long word de tête révèle le type de cartouche (cf. stmap).
    const uint32_t magic = cart.size() >= 4
        ? (uint32_t(cart[0]) << 24) | (uint32_t(cart[1]) << 16) |
          (uint32_t(cart[2]) << 8)  |  uint32_t(cart[3])
        : 0;
    const char* kind = magic == 0xFA52235F ? "diagnostic (saut $FA0004 au reset)"
                     : magic == 0xABCDEF42 ? "applicative (lancée par le TOS)"
                     : "inconnue (magic absent)";
    cartPath_ = path;
    std::fprintf(stderr, "[Bus] cartouche chargée : %s (%zu Ko @ $FA0000, magic $%08X, %s)\n",
                 path.c_str(), cart.size() / 1024, magic, kind);
    return true;
}

void Bus::ejectCart() {
    if (!cart.empty())
        std::fprintf(stderr, "[Bus] cartouche éjectée : %s\n", cartPath_.c_str());
    cart.clear();
    cartPath_.clear();
}

// -----------------------------------------------------------------------------
//  Décodage de banques MMU (port fidèle de Hatari stMemory.c).
//
//  Le MMU de l'ST traduit une adresse LOGIQUE (vue CPU/Shifter) en adresse
//  PHYSIQUE dans les puces RAM via les lignes RAS/CAS. Quand le registre de config
//  $FF8001 déclare une banque plus GRANDE que la puce réellement posée, les lignes
//  d'adresse hautes ne sont pas câblées → l'accès « aliase » dans la puce. C'est
//  exactement ce dont se servent les tests de RAM (Test Kit) pour mesurer la taille
//  installée : ils règlent $FF8001 au max puis écrivent/relisent en haut de chaque
//  banque. Sans ce décodage, NeoST renvoyait 0 et le sizing échouait.
// -----------------------------------------------------------------------------
namespace {
    constexpr uint32_t BANK_128 = 128u * 1024;
    constexpr uint32_t BANK_512 = 512u * 1024;
    constexpr uint32_t BANK_2M  = 2048u * 1024;

    // 2 bits de $FF8001 → taille d'une banque MMU (00=128K, 01=512K, 10=2M, 11=invalide).
    uint32_t mmuConfSize(uint8_t c) {
        switch (c & 3) { case 0: return BANK_128; case 1: return BANK_512; case 2: return BANK_2M; }
        return 0;
    }
    // RAM physiquement posée → taille des 2 banques (cf. STMemory_RAM_SetBankSize).
    void ramBanks(std::size_t bytes, uint32_t& b0, uint32_t& b1) {
        switch (bytes / 1024) {
            case 128:  b0 = BANK_128; b1 = 0;        break;
            case 256:  b0 = BANK_128; b1 = BANK_128; break;
            case 512:  b0 = BANK_512; b1 = 0;        break;
            case 640:  b0 = BANK_512; b1 = BANK_128; break;
            case 1024: b0 = BANK_512; b1 = BANK_512; break;
            case 2048: b0 = BANK_2M;  b1 = 0;        break;
            case 4096: b0 = BANK_2M;  b1 = BANK_2M;  break;
            default:   b0 = static_cast<uint32_t>(bytes); b1 = 0; break;
        }
    }
    // STF / Mega STF : remappage RAS/CAS (STMemory_MMU_Translate_Addr_STF).
    uint32_t mmuXlatSTF(uint32_t a, uint32_t ramSz, uint32_t mmuSz) {
        uint32_t r;
        if (ramSz == BANK_2M) {
            if      (mmuSz == BANK_2M)  r = a;
            else if (mmuSz == BANK_512) r = ((a & 0xffc00) << 1) | (a & 0x7ff);
            else                        r = ((a & 0x7fe00) << 2) | (a & 0x7ff);
        } else if (ramSz == BANK_512) {
            if      (mmuSz == BANK_2M)  r = ((a & 0xff800) >> 1) | (a & 0x3ff);
            else if (mmuSz == BANK_512) r = a;
            else                        r = ((a & 0x3fe00) << 1) | (a & 0x3ff);
        } else {  // ramSz == BANK_128
            if      (mmuSz == BANK_2M)  r = ((a & 0x7f800) >> 2) | (a & 0x1ff);
            else if (mmuSz == BANK_512) r = ((a & 0x3fc00) >> 1) | (a & 0x1ff);
            else                        r = a;
        }
        return r & (ramSz - 1);                 // contenu dans la puce (aliasing)
    }
    // STE / Mega STE : RAS/CAS entrelacés (STMemory_MMU_Translate_Addr_STE).
    uint32_t mmuXlatSTE(uint32_t a, uint32_t ramSz, uint32_t mmuSz) {
        uint32_t r;
        if (ramSz == BANK_2M)        r = a & (mmuSz == BANK_2M ? 0xffffffffu : 0x1fffff);
        else if (ramSz == BANK_512)  r = (mmuSz == BANK_512) ? a : (a & 0x7ffff);
        else                         r = (mmuSz == BANK_128) ? a : (a & 0x1ffff);
        return r & (ramSz - 1);
    }
}

// Traduit une adresse logique RAM (<4Mo) en index physique dans ram[], ou -1 si
// la banque visée n'est pas peuplée (→ zone « void » : lecture 0, écriture perdue).
int64_t Bus::mmuTranslate(uint32_t addr) const {
    const uint8_t conf = glue ? glue->memConfig_ : memConfigForBytes(ram.size());
    const uint32_t mmuB0 = mmuConfSize(static_cast<uint8_t>((conf >> 2) & 3));
    // STMemory_MMU_ConfToBank (Hatari) : seuls le ST/Mega ST (Config_IsMachineST,
    // MMU non-IMP) utilisent les bits 0-1 pour la banque 1 ; STE/Mega STE (IMP)
    // ignorent ces bits et calquent la banque 1 sur la banque 0.
    const uint32_t mmuB1 = (machine == MachineType::St || machine == MachineType::MegaSt)
                               ? mmuConfSize(static_cast<uint8_t>(conf & 3))
                               : mmuB0;
    uint32_t ramB0, ramB1; ramBanks(ram.size(), ramB0, ramB1);

    uint32_t bankStart, ramSz, mmuSz;
    if (addr < mmuB0)              { bankStart = 0;     ramSz = ramB0; mmuSz = mmuB0; }
    else if (addr < mmuB0 + mmuB1) { bankStart = ramB0; ramSz = ramB1; mmuSz = mmuB1; }
    else return -1;                              // au-delà de la config MMU → void
    if (ramSz == 0) return -1;                   // banque déclarée mais sans puce → void

    const uint32_t phys = (machineIsSte(machine) ? mmuXlatSTE(addr, ramSz, mmuSz)
                                                 : mmuXlatSTF(addr, ramSz, mmuSz)) + bankStart;
    return phys < ram.size() ? static_cast<int64_t>(phys) : -1;
}

// -----------------------------------------------------------------------------
//  Lecture / écriture 8 bits — point d'aiguillage central du bus.
// -----------------------------------------------------------------------------
uint8_t Bus::read8(uint32_t addr) {
    addr &= stmap::ADDR_MASK;

    // Overlay de boot : les 8 premiers octets ($0-$7) proviennent de la ROM
    // tant que le 68000 n'a pas fini de lire SSP+PC au reset.
    if (bootOverlay && addr < 8 && addr < rom.size())
        return rom[addr];

    // Espace RAM ($0-$3FFFFF) : décodé par le MMU (banques + aliasing).
    if (addr < 0x400000) {
        const int64_t phys = mmuTranslate(addr);
        return phys >= 0 ? ram[static_cast<std::size_t>(phys)] : 0x00;   // banque vide → 0
    }

    // ROM TOS.
    if (addr >= romBase && addr < romBase + rom.size())
        return rom[addr - romBase];

    // Port cartouche ($FA0000-$FBFFFF) : si une cartouche est montée, on expose
    // sa ROM ; le TOS lit le magic à $FA0000 et amorce (diagnostic/applicative).
    // Hors cartouche, l'espace reste "ouvert" (octets hauts, cf. plus bas) et le
    // magic ne correspond pas → boot normal.
    if (!cart.empty() && addr >= stmap::CART_BASE && addr < stmap::CART_BASE + cart.size())
        return cart[addr - stmap::CART_BASE];

    // Espace matériel.
    if (addr >= stmap::MMIO_BASE)
        return mmioRead8(addr);

    // Trou au-dessus de $400000 (sous la ROM/cartouche) → bus error sur vrai ST ;
    // ici on renvoie $FF.
    return 0xFF;
}

void Bus::write8(uint32_t addr, uint8_t v) {
    addr &= stmap::ADDR_MASK;

    // Espace RAM ($0-$3FFFFF) : décodé par le MMU (banques + aliasing). Une banque
    // déclarée mais sans puce absorbe l'écriture dans le vide.
    if (addr < 0x400000) {
        const int64_t phys = mmuTranslate(addr);
        if (phys >= 0) ram[static_cast<std::size_t>(phys)] = v;
        return;
    }
    if (addr >= stmap::MMIO_BASE) {
        mmioWrite8(addr, v);
        return;
    }
    // Écriture en ROM ou trou d'adressage : ignorée (lecture seule).
}

// -----------------------------------------------------------------------------
//  Carte de bus error de l'espace IO — port fidèle de Hatari (ioMem.c + tables
//  ioMemTabST.c / ioMemTabSTE.c). Principe : TOUT $FF8000-$FFFFFF faute par
//  défaut, puis on déclare « non fautifs » (registre câblé OU zone « void »
//  silencieuse) exactement les octets listés par la table du modèle. Un word/
//  long ne faute que si TOUS ses octets sont fautifs (cf. busFaultN), ce qui
//  reproduit le fait que `move.w $FF8204` marche mais `move.b $FF8204` faute.
// -----------------------------------------------------------------------------
namespace {
    struct IoSpan { uint32_t addr; uint32_t span; };

    // Octets NON fautifs communs à toutes les machines (registres réellement
    // décodés). Le blitter ($FF8A00-$FF8A3F) est volontairement ABSENT : NeoST
    // ne l'émule pas, donc il doit fauter (EmuTOS en conclut « pas de blitter »
    // et bascule sur le VDI logiciel). Idem le son DMA, ajouté seulement si le
    // modèle le possède.
    // -- Modèle ST / Mega ST (table IoMemTabST) ------------------------------
    const IoSpan ST_OK[] = {
        {0xFF8001,1},                                            // config MMU
        {0xFF8201,1},{0xFF8203,1},{0xFF8205,1},{0xFF8207,1},     // base/compteur vidéo
        {0xFF8209,1},{0xFF820A,1},{0xFF820B,1},{0xFF820D,1},     // sync + void
        {0xFF8240,32},                                           // palette 0-15 (16 mots)
        {0xFF8260,1},{0xFF8261,1},{0xFF8262,30},                 // résolution + void →$FF827F
        {0xFF8604,2},{0xFF8606,2},{0xFF8609,1},{0xFF860B,1},{0xFF860D,1}, // FDC/DMA
        {0xFF8800,4},                                            // PSG (mirroir ajouté plus bas)
    };
    // -- Modèle STE / Mega STE (table IoMemTabSTE) ---------------------------
    const IoSpan STE_OK[] = {
        {0xFF8000,16},                                           // config + void $FF8000-$FF800F
        {0xFF8200,16},                                           // base/compteur/sync vidéo (void inclus)
        {0xFF8240,64},                                           // palette + rés. + scroll fin →$FF827F
        {0xFF8604,12},                                           // FDC/DMA $FF8604-$FF860F (void inclus)
        {0xFF8800,4},                                            // PSG
        {0xFF9000,2},                                            // void
        {0xFF9200,4},{0xFF9211,1},{0xFF9213,1},{0xFF9215,1},     // joypad/lightpen STE
        {0xFF9217,1},{0xFF9220,4},
    };
}

void Bus::buildIoFault() const {
    ioFault_.assign(0x8000, 1);                  // défaut : tout faute (Hatari SetBusErrorRegion)
    auto clear = [&](uint32_t a, uint32_t span) {
        for (uint32_t i = 0; i < span; ++i) {
            const uint32_t addr = a + i;
            if (addr >= stmap::MMIO_BASE && addr <= 0xFFFFFF)
                ioFault_[addr - stmap::MMIO_BASE] = 0;
        }
    };
    const bool ste = machineIsSte(machine);
    if (ste) for (const auto& s : STE_OK) clear(s.addr, s.span);
    else     for (const auto& s : ST_OK)  clear(s.addr, s.span);

    // Son DMA STE ($FF8900-$FF893F) : présent uniquement STE / Mega STE.
    if (machineHasDmaSound(machine)) clear(0xFF8900, 0x40);

    // Blitter ($FF8A00-$FF8A3F) : présent sur Mega ST / STE / Mega STE → ses
    // registres répondent (pas de bus error). Sur STF d'origine, la zone reste
    // fautive (EmuTOS en conclut « pas de blitter » → VDI logiciel).
    if (machineHasBlitter(machine)) clear(0xFF8A00, 0x40);

    // MFP 68901 : registres aux adresses IMPAIRES uniquement ($FFFA01-$FFFA3F) ;
    // les octets pairs fautent. RS232 et octets « void » inclus (tous impairs).
    for (uint32_t a = 0xFFFA01; a <= 0xFFFA3F; a += 2) clear(a, 1);

    // ACIA clavier/MIDI + RTC + zone void contiguë : $FFFC00-$FFFDFF non fautif.
    clear(0xFFFC00, 0x200);

    // PSG : miroir matériel des 4 registres sur tout $FF8800-$FF88FF (Hatari).
    clear(0xFF8800, 0x100);

    // Différences de chipset selon le modèle (Hatari IoMem_FixVoidAccess*).
    if (machine == MachineType::St) {            // chipset Ricoh : 2 octets « void »
        clear(0xFF820F, 1); clear(0xFF860F, 1);
    } else if (machine == MachineType::MegaSt) { // chipset IMP : plus de zones void
        const uint32_t voidAddr[] = {0xFF8000,0xFF8200,0xFF8202,0xFF8204,0xFF8206,
                                     0xFF8208,0xFF820C,0xFF8608,0xFF860A,0xFF860C};
        for (uint32_t a : voidAddr) clear(a, 1);
        clear(0xFF8002, 0x0C);                   // $FF8002-$FF800D void
    } else if (machine == MachineType::MegaSte) {
        clear(0xFF8E01, 0x0F);                   // SCU (comme TT)
        clear(0xFF8E20, 0x04);                   // cache/CPU control
        clear(0xFF8C80, 0x08);                   // SCC série Z85C30
        clear(0xFF860E, 0x02);                   // mode densité DD/HD
    }

    ioFaultMachine_ = machine;
    ioFaultBuilt_   = true;
}

bool Bus::busFault(uint32_t addr) const {
    addr &= stmap::ADDR_MASK;

    // RAM ($0-$3FFFFF) : décodée par le MMU ; jamais de bus error (banque vide → 0).
    if (addr < 0x400000) return false;

    // ROM TOS : jamais de bus error.
    if (addr >= romBase && addr < romBase + rom.size()) return false;

    // Port cartouche ($FA0000-$FBFFFF) : banque ROM sur le vrai matériel ; lit $FF
    // si rien n'est branché → jamais de bus error (le TOS y lit le magic au reset).
    if (addr >= stmap::CART_BASE && addr < stmap::CART_END) return false;

    // Espace IO ($FF8000-$FFFFFF) : carte octet par octet (cf. buildIoFault).
    if (addr >= stmap::MMIO_BASE) {
        if (!ioFaultBuilt_ || ioFaultMachine_ != machine) buildIoFault();
        return ioFault_[addr - stmap::MMIO_BASE] != 0;
    }

    // Tout le reste — trous $400000-$F9FFFF (RAM absente, ROM cartouche basse,
    // IDE, VME...) et $FF0000-$FF7FFF (sous l'espace IO) — n'est décodé par aucun
    // circuit → bus error sur le vrai ST (Hatari BusErrMem_bank). C'est notamment
    // ce que sondent les cartouches de diagnostic pour valider la gestion d'erreur.
    return true;
}

bool Bus::busFaultN(uint32_t addr, unsigned n) const {
    // Règle Hatari : faute uniquement si TOUS les octets de l'accès fautent.
    for (unsigned i = 0; i < n; ++i)
        if (!busFault(addr + i)) return false;
    return true;
}

// --- Accès 16/32 bits : le 68000 est big-endian, on assemble octet par octet --
uint16_t Bus::read16(uint32_t addr) {
    return static_cast<uint16_t>((read8(addr) << 8) | read8(addr + 1));
}
uint32_t Bus::read32(uint32_t addr) {
    return (static_cast<uint32_t>(read16(addr)) << 16) | read16(addr + 2);
}
void Bus::write16(uint32_t addr, uint16_t v) {
    write8(addr,     static_cast<uint8_t>(v >> 8));
    write8(addr + 1, static_cast<uint8_t>(v));
}
void Bus::write32(uint32_t addr, uint32_t v) {
    write16(addr,     static_cast<uint16_t>(v >> 16));
    write16(addr + 2, static_cast<uint16_t>(v));
}

// -----------------------------------------------------------------------------
//  Dispatch MMIO ($FF8000-$FFFFFF). Chaque puce expose read8/write8 ; le bus
//  ne fait QUE router, il ne connaît pas les détails internes des composants.
// -----------------------------------------------------------------------------
uint8_t Bus::mmioRead8(uint32_t addr) {
    if (addr >= stmap::SHIFTER_BASE && addr <= stmap::SHIFTER_END && shifter)
        return shifter->read8(addr);
    if (addr >= stmap::PSG_BASE && addr < stmap::PSG_BASE + 4 && psg)
        return psg->read8(addr);
    if (addr >= stmap::DMA_FDC_BASE && addr < stmap::DMA_FDC_BASE + 0x10 && fdc)
        return fdc->read8(addr);          // contrôleur disquette + DMA ($FF8600)
    if (addr >= stmap::DMASND_BASE && addr < stmap::DMASND_END && dmasnd
        && machineHasDmaSound(machine))
        return dmasnd->read8(addr);       // son DMA STE ($FF8900) — STE/Mega STE
    if (addr >= 0xFF8A00 && addr <= 0xFF8A3F && blitter && machineHasBlitter(machine))
        return blitter->read8(addr);      // blitter ($FF8A00) — Mega ST/STE/Mega STE
    if (addr >= stmap::MFP_BASE && addr < stmap::MFP_BASE + 0x40 && mfp) {
        const uint8_t v = mfp->read8(addr);
        if (cpu) cpu->updateIpl();        // l'état d'IRQ a pu changer
        return v;
    }
    if (addr >= stmap::ACIA_BASE && addr < stmap::ACIA_BASE + 4 && ikbd) {
        const uint8_t v = ikbd->read8(addr);   // ACIA clavier $FFFC00/$FFFC02
        if (cpu) cpu->updateIpl();
        return v;
    }
    if (addr >= 0xFFFC04 && addr < 0xFFFC08 && midi) {
        const uint8_t v = midi->read8(addr);   // ACIA MIDI ($FFFC04/06) — bouclage OUT→IN
        if (cpu) cpu->updateIpl();             // une lecture peut effacer l'IRQ ACIA
        return v;
    }
    if (addr >= 0xFFFC21 && addr <= 0xFFFC3F && rtc && machineIsMega(machine))
        return rtc->read8(addr);          // RTC RP5C15 — Mega ST / Mega STE
    // STE / Mega STE : joypads / paddles / lightpen + DIP switches MegaSTE
    // ($FF9200-$FF9223). NeoST n'émule aucun de ces périphériques → on renvoie les
    // valeurs « au repos » d'Hatari (joy.c) au lieu d'un 0xFF générique. Le seul
    // registre non trivial est l'octet HAUT de $FF9200 = DIP MegaSTE (0xBF : lecteur
    // HD 1.44 Mo monté, son DMA actif ; logique inversée, cf. IoMemTabMegaSTE_DIPSwitches_Read).
    if (machineIsSte(machine) && addr >= 0xFF9200 && addr <= 0xFF9223) {
        switch (addr) {
            // $FF9200.w = boutons feu (octet bas, 0xFF relâché) | DIP (octet haut).
            case 0xFF9200: return machine == MachineType::MegaSte ? 0xBF : 0xFF;
            // Paddle/analogique X/Y ($FF9211/13/15/17) : axe au NEUTRE (mid-value 0x24).
            case 0xFF9211: case 0xFF9213:
            case 0xFF9215: case 0xFF9217: return 0x24;
            // Lightpen X/Y ($FF9220-$FF9223) : non supporté → 0 (mots à $FF9220/22).
            case 0xFF9220: case 0xFF9221:
            case 0xFF9222: case 0xFF9223: return 0x00;
            // $FF9201 (DIP bas / boutons) et $FF9202/03 (directions+sélection) au repos.
            default: return 0xFF;
        }
    }
    // Registre Cache/CPU MegaSTE $FF8E21, relisible (latch écrit par TOS 2.x). cf.
    // Bus.hpp megaSteCacheCtrl. $FF8E20/22/23 restent « void » (→ glue → 0xFF).
    if (machine == MachineType::MegaSte && addr == 0xFF8E21)
        return megaSteCacheCtrl;
    if (glue)
        return glue->read8(addr);         // MMU et reste du MMIO
    return 0xFF;
}

void Bus::mmioWrite8(uint32_t addr, uint8_t v) {
    if (addr >= stmap::SHIFTER_BASE && addr <= stmap::SHIFTER_END && shifter) {
        shifter->write8(addr, v);
        return;
    }
    if (addr >= stmap::PSG_BASE && addr < stmap::PSG_BASE + 4 && psg) {
        psg->write8(addr, v);
        return;
    }
    if (addr >= stmap::DMA_FDC_BASE && addr < stmap::DMA_FDC_BASE + 0x10 && fdc) {
        fdc->write8(addr, v);             // contrôleur disquette + DMA
        if (cpu) cpu->updateIpl();        // l'INTRQ FDC (GPIP5) a pu changer
        return;
    }
    if (addr >= stmap::DMASND_BASE && addr < stmap::DMASND_END && dmasnd
        && machineHasDmaSound(machine)) {
        dmasnd->write8(addr, v);          // son DMA STE ($FF8900) — STE/Mega STE
        return;
    }
    if (addr >= 0xFF8A00 && addr <= 0xFF8A3F && blitter && machineHasBlitter(machine)) {
        blitter->write8(addr, v);         // blitter ($FF8A00) — Mega ST/STE/Mega STE
        return;
    }
    if (addr >= stmap::MFP_BASE && addr < stmap::MFP_BASE + 0x40 && mfp) {
        mfp->write8(addr, v);
        if (cpu) cpu->updateIpl();        // (dé)masquage, fin d'interruption...
        return;
    }
    if (addr >= stmap::ACIA_BASE && addr < stmap::ACIA_BASE + 4 && ikbd) {
        ikbd->write8(addr, v);
        if (cpu) cpu->updateIpl();
        return;
    }
    if (addr >= 0xFFFC04 && addr < 0xFFFC08 && midi) {
        midi->write8(addr, v);            // ACIA MIDI ($FFFC04/06) — bouclage OUT→IN
        if (cpu) cpu->updateIpl();        // un octet bouclé peut lever l'IRQ ACIA
        return;
    }
    if (addr >= 0xFFFC21 && addr <= 0xFFFC3F && rtc && machineIsMega(machine)) {
        rtc->write8(addr, v);             // RTC RP5C15 — Mega ST / Mega STE
        return;
    }
    // Registre Cache/CPU MegaSTE $FF8E21 : latché + contrainte matérielle « le cache ne
    // peut être actif qu'à 16 MHz » — si bit0 (cache) est demandé alors que bit1 (vitesse)
    // = 0 (8 MHz), le matériel force bit0 à 0 (cf. Hatari IoMemTabMegaSTE_CacheCpuCtrl_WriteByte).
    // L'EFFET réel (débit cycles 8/16 MHz, cache 16 Ko) relève d'items « précision cycle ».
    if (machine == MachineType::MegaSte && addr == 0xFF8E21) {
        if ((v & 0x02) == 0 && (v & 0x01)) v &= 0xFE;   // cache impossible à 8 MHz
        megaSteCacheCtrl = v;
        return;
    }
    if (glue)
        glue->write8(addr, v);
}
