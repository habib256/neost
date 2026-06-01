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
    std::fprintf(stderr, "[Bus] cartouche chargée : %s (%zu Ko @ $FA0000, magic $%08X, %s)\n",
                 path.c_str(), cart.size() / 1024, magic, kind);
    return true;
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
    const uint32_t mmuB1 = mmuConfSize(static_cast<uint8_t>(conf & 3));
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

bool Bus::busFault(uint32_t addr) const {
    addr &= stmap::ADDR_MASK;
    // Blitter ($FF8A00-$FF8A3F) : absent sur ST de base → la lecture provoque
    // une bus error. EmuTOS s'en sert pour conclure "pas de blitter" ; sinon il
    // route ses tracés VDI (barre de menu, curseur souris) vers un blitter
    // fantôme et ils n'apparaissent pas.
    if (addr >= 0xFF8A00 && addr <= 0xFF8A3F) return true;
    // Son DMA STE ($FF8900-$FF893F) : absent sur ST / Mega ST → bus error, comme
    // sur le vrai matériel (c'est ainsi qu'EmuTOS conclut « pas de son DMA »).
    if (addr >= stmap::DMASND_BASE && addr < stmap::DMASND_END && !machineHasDmaSound(machine))
        return true;
    // Zone réservée non décodée entre la config mémoire ($FF8000-01) et le shifter
    // ($FF8200) : aucun périphérique → bus error sur vrai ST. EmuTOS y sonde le
    // matériel au boot (FC007C : tst.w $FF8006, vecteur bus error armé juste avant)
    // pour distinguer les modèles. Confirmé vérité Hatari. Cf. [[busfault-ff80xx]].
    if (addr >= stmap::MMIO_BASE + 2 && addr < stmap::SHIFTER_BASE) {
        // Sur Mega ST/STE (chipset IMP), $FF8002-$FF800D est « void » (lecture
        // sans bus error) au lieu de fauter : c'est la différence que sonde
        // EmuTOS pour distinguer ST/Mega ST. Vérité Hatari : `tst.w $FF8006`
        // faute sur ST mais pas sur Mega ST. Réf. IoMem_FixVoidAccessForMegaST.
        if (machineIsMega(machine) && addr <= 0xFF800D) return false;
        return true;
    }
    return false;
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
    if (addr >= 0xFFFC04 && addr < 0xFFFC08)   // ACIA MIDI : statut "prêt, rien à lire"
        return (addr & 2) ? 0x00 : 0x02;
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
    if (addr >= 0xFFFC04 && addr < 0xFFFC08)   // ACIA MIDI : écritures ignorées
        return;
    if (glue)
        glue->write8(addr, v);
}
