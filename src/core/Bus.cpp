// =============================================================================
//  Bus.cpp — Implémentation du Memory Map et du dispatch MMIO.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
#include "core/Shifter.hpp"
#include "core/YM2149.hpp"
#include "core/MmuGlue.hpp"
#include "core/Cpu68k.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"
#include "io/Fdc.hpp"
#include "core/DmaSound.hpp"
#include "core/Blitter.hpp"
#include "io/Rtc.hpp"
#include "io/MidiAcia.hpp"
#include "io/Scc.hpp"
#include "io/Ne2000.hpp"
#include "io/Isp1160.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>

Bus::Bus(std::size_t ramBytes) {
    ram.assign(ramBytes, 0);
}

// Save-state : la RAM (gros bloc, taille préfixée par vec → le load restaure aussi la
// bonne taille) PUIS l'état MMIO/config RUNTIME du Bus. NE SONT PAS sérialisés :
//  - rom        : image ROM, rechargée depuis son fichier (pas du state).
//  - cartPath_  : chemin fichier (rechargé par le frontend, pas de l'état machine).
//  - ioFault_ / ioFaultMachine_ / ioFaultBuilt_ : carte de bus error PUREMENT DÉRIVÉE
//    (reconstruite à la demande depuis `machine` + `fpu.present` via buildIoFault) → on
//    laisse ioFaultBuilt_ à sa valeur ; le premier accès MMIO après load la régénère.
// `cart`, EN REVANCHE, EST sérialisée depuis la v7 : ce n'est pas qu'une image immuable,
// le HD GEMDOS y écrit l'ANCIEN vecteur GEMDOS (GemdosHd::sysInit, CART_OLDGEMDOS) —
// donc de l'état machine runtime, sans lequel la RAM restaurée pointe sur un port
// cartouche dépeuplé. L'empreinte d'en-tête refuse de toute façon un rechargement croisé.
void Bus::serialize(StateArchive& ar) {
    ar.vec(ram);
    ar.vec(cart);   // cf. en-tête : porte le vecteur GEMDOS sauvegardé, pas qu'une image
    // La cartouche est décodée par read8Slow AVANT le MMIO, pour tout
    // addr < CART_BASE + cart.size() (Bus.cpp:369 → 373). loadCart plafonne l'image à
    // la fenêtre matérielle 128 Ko ($FA0000-$FC0000) mais ar.vec ne borne que par les
    // octets restants du buffer : un .state forgé (CRC recalculé) portant un cart plus
    // grand masquerait les registres $FF8xxx avec des octets de cartouche. On réapplique
    // le même plafond ici — l'échec rejoue le backup (Machine::loadState).
    ar.check(cart.size() <= (stmap::CART_END - stmap::CART_BASE),
             "Bus::cart.size() dépasse la fenêtre cartouche 128 Ko");
    // Taille RAM restreinte aux configs réelles (celles que ramBanks découpe en deux
    // banques 128K/512K/2M) : la ligne `default` de ramBanks
    // (b0 = taille brute) suppose une puissance de deux — un .state forgé portant
    // p.ex. 2,5 Mo rendrait tous les masques `r & (ramSz-1)` du remappage RAS/CAS
    // faux (aliasing corrompu), sans jamais réactiver le raccourci mmuFastLimit_.
    {
        const std::size_t kb = ram.size() / 1024;
        ar.check((ram.size() % 1024) == 0
                 && (kb == 128 || kb == 256 || kb == 512 || kb == 640
                     || kb == 1024 || kb == 2048 || kb == 2176 || kb == 2560
                     || kb == 4096),
                 "Bus::ram.size() hors des configs ST supportées");
    }

    // Profil machine + config ROM/TOS chargée.
    ar(machine);
    ar(romBase);
    ar(tosVersion);

    // Overlay de boot (route $0-$7 vers la ROM tant qu'actif).
    ar(bootOverlay);
    ar(gameCart);                          // v52 : registre « gamecart » STE ($FF9000)

    // Latch du bus de données (relu par la zone RAM « void » — cf. cpuDb).
    ar(cpuDb);

    // Fenêtres de comptage bus du blitter non-hog (bookkeeping possédé par le Bus).
    ar(blitterWinStart);
    ar(blitterWinEnd);
    ar(blitterCountCpu);

    // Registre Cache/CPU MegaSTE ($FF8E21) + contenu du cache externe 16 Ko.
    ar(megaSteCacheCtrl);
    ar(megaSteCache);       // struct POD (valid/tag/value)

    // Coprocesseurs / gate / pads : classes triviales à état interne.
    ar(fpu);                // inclut fpu.present (la carte ioFault_ se rebâtit au besoin)
    fpu.fixSerializedBools(ar);   // copie EN BLOC → booléens hors de portée d'operator()
    ar.check(fpu.stateValid());   // index du tampon CIR : cf. Fpu::stateValid
    ar(scu);
    ar(stePads);
    stePads.fixSerializedBools(ar);   // idem

    // Largeur d'accès MMIO en cours (transitoire, inoffensif).
    ar(ioAccessWidth_);

    // ar(fpu) a restauré fpu.present en CONTOURNANT setFpuPresent (seul poseur de
    // ioFaultBuilt_=false) : si la session avait déjà bâti la carte de bus-errors
    // avec l'autre valeur, $FFFA40-$FFFA5F garderait le mauvais statut → rebâtir.
    if (ar.loading()) ioFaultBuilt_ = false;
}

// Broche /RESET du 68000 (instruction RESET, $4E70). Port de customreset()
// (cpu/hatari-glue.c:54) : IKBD, Glue vidéo, PSG et FDC repartent à zéro, le CPU
// et l'ordonnanceur NON (seule la ligne /RESET des périphériques est assertée).
// Sans cela, un loader de jeu/démo qui fait « reset » pour faire taire la machine
// héritée du TOS gardait le YM en train de jouer, les timers MFP du TOS armés (IRQ
// parasites dans son propre code), le moteur disquette en rotation et la Glue sur la
// fréquence/résolution précédente.
void Bus::peripheralReset() {
    gameCart = false;                      // GAMECART effacé par RESB (netlist 4081P10)
    stePads.resetSelect();                 // JOYWE remis à 1 par le reset (gstmcu.v:352)
    if (ikbd)    ikbd->resetHw();          // IKBD_Reset(false) : SCI + boot ROM
    // Fréquence + résolution posées EN DIRECT, sans repasser par le bus. C'est bien ce
    // que fait Hatari : Video_Reset_Glue appelle IoMem_WriteByte, documenté « write 8-bit
    // byte into IO memory space WITHOUT interception » (includes/ioMem.h:86) — un simple
    // IoMem[addr] = v, qui n'appelle jamais Video_Sync_WriteByte. Router par Bus::write8
    // injectait au contraire un événement freq DATÉ dans la machine d'états de la Glue
    // (celle qui pilote la détection des retraits de bordure) à chaque « reset », plus
    // des wait-states de bus qu'Hatari n'ajoute pas.
    if (shifter) shifter->resetGlue(mfp && !mfp->colorMonitor());   // $FF820A = 0 + $FF8260
    if (psg)     psg->reset();             // PSG_Reset
    if (mfp)     mfp->resetChip();         // MFP_Reset_All
    if (fdc)     fdc->reset(/*cold=*/false);   // FDC_Reset(false)
}

bool Bus::loadTos(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[Bus] TOS not found: %s\n", path.c_str());
        return false;
    }
    const std::streamsize n = f.tellg();
    // tellg() peut renvoyer -1 (taille indéterminable) OU 2^63-1 (répertoire sous
    // Linux) → resize géant. Borne haute : la fenêtre ROM à $E00000 fait 1 Mo.
    constexpr std::streamsize kMaxTos = 1024 * 1024;
    if (n <= 0 || n > kMaxTos) {
        std::fprintf(stderr, "[Bus] invalid TOS (%lld B, max %lld B): %s\n",
                     static_cast<long long>(n), static_cast<long long>(kMaxTos), path.c_str());
        return false;
    }
    f.seekg(0);
    rom.resize(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(rom.data()), n);

    // L'emplacement de la ROM dépend de la version de TOS (cf. stmap) : un TOS
    // de 192 Ko vit à $FC0000, sinon (224/256 Ko) à $E00000.
    romBase = (rom.size() <= 192u * 1024u) ? stmap::ROM_FC0000 : stmap::ROM_E00000;
    // Version du TOS : mot big-endian de l'en-tête à l'offset 2 (cf. adjustMachineForTos).
    tosVersion = rom.size() >= 4 ? uint16_t((rom[2] << 8) | rom[3]) : 0;
    // Vecteurs reset $0-$7 : le GLUE les mappe sur la ROM en permanence — on les
    // recopie en RAM dès le chargement (et à chaque reset, cf. seedResetVectors).
    seedResetVectors();
    std::fprintf(stderr, "[Bus] TOS loaded: %s (%zu KB @ $%06X, version $%04X)\n",
                 path.c_str(), rom.size() / 1024, romBase, tosVersion);
    return true;
}

bool Bus::loadCart(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[Bus] cartridge not found: %s\n", path.c_str());
        return false;
    }
    const std::streamsize n = f.tellg();
    const std::size_t maxSize = stmap::CART_END - stmap::CART_BASE;   // 128 Ko
    if (n <= 0 || static_cast<std::size_t>(n) > maxSize) {
        std::fprintf(stderr, "[Bus] invalid cartridge (%lld B, max %zu B): %s\n",
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
    const char* kind = magic == 0xFA52235F ? "diagnostic (jump to $FA0004 at reset)"
                     : magic == 0xABCDEF42 ? "application (launched by TOS)"
                     : "unknown (no magic)";
    cartPath_ = path;
    std::fprintf(stderr, "[Bus] cartridge loaded: %s (%zu KB @ $FA0000, magic $%08X, %s)\n",
                 path.c_str(), cart.size() / 1024, magic, kind);
    return true;
}

void Bus::ejectCart() {
    if (!cart.empty())
        std::fprintf(stderr, "[Bus] cartridge ejected: %s\n", cartPath_.c_str());
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
    // Taille de la banque 0 (bits 2-3 de $FF8001). STE : la sélection RAM du GST
    // MCU ne regarde que RS3 puis RS2 (netlist 4081P18, multiplexeurs SAB sur RS3B) —
    // RS3 = 1 prend le chemin 2 Mo quel que soit RS2, donc « 11 » vaut 2 Mo ; sur
    // ST, « 11 » reste invalide (banque vide, modèle Hatari).
    uint32_t mmuBank0Size(uint8_t conf, MachineType m) {
        const uint8_t c = (conf >> 2) & 3;
        return (m == MachineType::Ste && c == 3) ? BANK_2M : mmuConfSize(c);
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
            case 2176: b0 = BANK_2M;  b1 = BANK_128; break;   // 2M + 128K
            case 2560: b0 = BANK_2M;  b1 = BANK_512; break;   // 2M + 512K
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
// la banque visée n'est pas peuplée (→ zone « void » : on relit le dernier mot du
// bus de données [cpuDb], l'écriture est perdue — cf. Hatari VoidMem_*).
int64_t Bus::mmuTranslate(uint32_t addr) const {
    const uint8_t conf = glue ? glue->memConfig_ : memConfigForBytes(ram.size());
    const uint32_t mmuB0 = mmuBank0Size(conf, machine);
    // STMemory_MMU_ConfToBank (Hatari) : seuls le ST/Mega ST (Config_IsMachineST,
    // MMU non-IMP) utilisent les bits 0-1 pour la banque 1 ; STE/Mega STE (IMP)
    // ignorent ces bits et calquent la banque 1 sur la banque 0.
    //
    // Banques MIXTES sur STE (2176/2560 Ko = 2 Mo + 128/512 Ko), arbitré le 2026-10-20.
    // Ce que le matériel réel dit :
    //  · FAQ ST comp.sys.atari.st § 3.1.1 (atari.org/hosted/quickfaq/stfaq_3.htm) :
    //    « Because of a bug in TOS, STEs will not recognize a 2.5Mb configuration
    //    (2x256Kb + 2x1Mb) without a small bootup program like silkboot2e or simmfix ».
    //  · Usenet 1991 (CFN 13/0243, « Re: STe simms ») : « STe's cannot normally have
    //    1 bank of 512k and one bank of 2048k » — SIMMFIX2 (Hoggan/Lodge, 1991) existe
    //    précisément pour ça.
    // Ce que chaque modèle produit dans NeoST (phystop $42E, mesuré) :
    //  · banque 1 calquée sur la banque 0 (Hatari, retenu) : TOS 1.62/2.06 → $400000,
    //    donc la config N'EST PAS reconnue, comme sur le vrai STE ; EmuTOS (sondage
    //    par banque, bios/memory.S) → $220000/$280000. Et un patch type SIMMFIX qui
    //    ne ferait que corriger phystop marche : banque 1 déclarée 2 Mo + puce 512 Ko
    //    se décode LINÉAIREMENT sur ses 512 premiers Ko (RAS/CAS entrelacés STE,
    //    mmuXlatSTE) — la RAM $200000-$27FFFF est utilisable.
    //  · banques indépendantes (bits 0-1 honorés, modèle MAME stmmu.cpp, qui ne gère
    //    d'ailleurs pas 2176/2560) : TOS 1.62/2.06 reconnaîtraient 2,5 Mo d'emblée —
    //    contredit les deux sources ci-dessus (aucun besoin de SIMMFIX).
    // Le modèle Hatari est donc le seul cohérent avec les témoignages matériels. Reste
    // non prouvé faute de mesure directe : la lecture de $FF8001 bits 0-1 sur STE et le
    // décodage exact d'une banque 1 de 128 Ko (2176 Ko, config rare). À trancher sur
    // vrai STE (écrire $09, motifs en $200000/$280000) si un jour disponible.
    const uint32_t mmuB1 = (machine == MachineType::St || machine == MachineType::MegaSt)
                               ? mmuConfSize(static_cast<uint8_t>(conf & 3))
                               : mmuB0;
    uint32_t ramB0, ramB1; ramBanks(ram.size(), ramB0, ramB1);

    // Trou de décodage MMU (port de memory_map_Standard_RAM, cpu/memory.c) : la
    // combinaison bank0 = 128 Ko + bank1 = 2 Mo ne sélectionne AUCUNE puce sur
    // $40000-$7FFFF (mesuré sur STF) — lecture flottante, écriture perdue. Ne peut
    // se produire que sur ST/Mega ST (les STE calquent bank1 sur bank0).
    if (mmuB0 == BANK_128 && mmuB1 == BANK_2M
        && addr >= 0x40000 && addr < 0x80000) return -1;

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
//  Reconstruction du cache de décodage MMU (cf. Bus.hpp § Cache de décodage MMU).
//
//  POURQUOI la traduction est l'identité quand une banque est annoncée à sa taille
//  réelle — le raisonnement, pour que personne n'ait à le refaire :
//
//   · Banque 0, mmuB0 == ramB0 : bankStart = 0, et les deux remappages RAS/CAS se
//     réduisent au cas « ramSz == mmuSz », c'est-à-dire à `a & (ramSz-1)`. Comme
//     a < mmuB0 = ramSz, cela vaut `a`. Traduction = identité sur [0, ramB0).
//   · Banque 1, mmuB1 == ramB1 (et banque réellement peuplée) : même réduction,
//     `a & (ramB1-1)`, puis `+ bankStart` avec bankStart = ramB0 = mmuB0. Or
//     ramB1 <= ramB0 = mmuB0 et toutes les tailles sont des puissances de deux :
//     mmuB0 est donc un multiple de ramB1, et `a & (ramB1-1)` vaut `a - mmuB0`.
//     Le recollage redonne `a`. Identité étendue à [0, ramB0 + ramB1).
//
//  Dès qu'une banque est SUR-déclarée (ce que font les tests de RAM en réglant
//  $FF8001 au maximum) ou vide alors que la config l'annonce, l'égalité tombe et on
//  laisse mmuTranslate faire le décodage complet — la limite est alors réduite à la
//  banque 0, voire à zéro. Aucun comportement n'est perdu, seul le raccourci l'est.
// -----------------------------------------------------------------------------
void Bus::rebuildMmuCache(uint8_t conf) const {
    mmuCacheConf_    = conf;
    mmuCacheRam_     = ram.size();
    mmuCacheMachine_ = machine;         // cf. clé de revalidation (Bus.hpp)
    mmuFastLimit_ = 0;

    const uint32_t mmuB0 = mmuBank0Size(conf, machine);
    // Même règle que mmuTranslate : seuls ST / Mega ST utilisent les bits 0-1 pour
    // la banque 1 ; les STE (MMU « IMP ») calquent la banque 1 sur la banque 0.
    const uint32_t mmuB1 = (machine == MachineType::St || machine == MachineType::MegaSt)
                               ? mmuConfSize(static_cast<uint8_t>(conf & 3))
                               : mmuB0;
    uint32_t ramB0, ramB1; ramBanks(ram.size(), ramB0, ramB1);

    if (mmuB0 != ramB0 || ramB0 == 0) return;          // banque 0 non identitaire
    uint64_t lim = ramB0;
    if (mmuB1 == ramB1 && ramB1 != 0) lim += ramB1;     // banque 1 aussi
    lim = std::min<uint64_t>(lim, ram.size());
    mmuFastLimit_ = static_cast<uint32_t>(std::min<uint64_t>(lim, 0x400000u));

    // Garde-fou de développement : le raccourci DOIT rendre exactement ce que rend
    // le décodage complet, aux bornes comme au milieu. Une divergence ici serait une
    // corruption mémoire silencieuse — on la fait échouer bruyamment en debug.
#ifndef NDEBUG
    if (mmuFastLimit_ > 0) {
        const uint32_t probes[] = { 0u, ramB0 - 1u, ramB0, mmuFastLimit_ - 1u,
                                    mmuFastLimit_ / 2u };
        for (uint32_t p : probes)
            if (p < mmuFastLimit_) assert(mmuTranslate(p) == static_cast<int64_t>(p)
                                          && "cache MMU incohérent avec mmuTranslate");
    }
#endif
}

// Pointeur hôte contigu dans ram[] pour [addr, addr+len) — cf. Bus.hpp. On traduit
// le premier et le dernier octet : la plage n'est utilisable que si elle reste dans
// la MÊME puce sans aliasing (octets physiques consécutifs), ce qui est le cas des
// tampons normaux d'un programme. Tout le reste (banque vide, repli MMU, hors RAM)
// renvoie nullptr → l'appelant retombe sur un accès octet par octet / une erreur.
uint8_t* Bus::hostRamPtr(uint32_t addr, uint32_t len) {
    addr &= stmap::ADDR_MASK;
    if (len == 0) len = 1;
    if (addr >= 0x400000 || addr + len > 0x400000) return nullptr;
    const int64_t p0 = mmuTranslate(addr);
    const int64_t p1 = mmuTranslate(addr + len - 1);
    if (p0 < 0 || p1 < 0) return nullptr;
    if (p1 - p0 != int64_t(len) - 1) return nullptr;          // non contigu (aliasing)
    if (static_cast<std::size_t>(p0) + len > ram.size()) return nullptr;
    return &ram[static_cast<std::size_t>(p0)];
}

// -----------------------------------------------------------------------------
//  Lecture / écriture 8 bits — point d'aiguillage central du bus.
// -----------------------------------------------------------------------------
// Décodage COMPLET. Le cas ultra-majoritaire (RAM ordinaire) est traité en ligne
// par Bus::read8 dans l'en-tête ; on arrive ici pour tout le reste — et aussi pour
// la RAM quand la config MMU n'est pas en traduction identité.
uint8_t Bus::read8Slow(uint32_t addr) {
    addr &= stmap::ADDR_MASK;

    // Overlay de boot : les 8 premiers octets ($0-$7) proviennent de la ROM
    // tant que le 68000 n'a pas fini de lire SSP+PC au reset.
    if (bootOverlay && addr < 8 && addr < rom.size())
        return rom[addr];

    // Espace RAM ($0-$3FFFFF) : décodé par le MMU (banques + aliasing).
    if (addr < 0x400000) {
        const int64_t phys = mmuTranslate(addr);
        if (phys >= 0) return ram[static_cast<std::size_t>(phys)];
        // Banque vide / au-delà de la config MMU : rien ne pilote le bus de
        // données → on relit le dernier mot qui y a transité (cf. Hatari
        // VoidMem_bget/wget → regs.db). Pour un accès MOT (assemblé ici octet
        // par octet), l'adresse paire porte l'octet fort : le mot relu vaut
        // exactement cpuDb, comme VoidMem_wget.
        if (ioAccessWidth_ >= 2) return uint8_t((addr & 1) ? cpuDb : cpuDb >> 8);
        return uint8_t(cpuDb);                   // accès octet : VoidMem_bget
    }

    // ROM TOS. La fenêtre décodée dépasse le fichier (1 Mo à $E00000, cf.
    // romWindowSize) : au-delà du TOS chargé on lit 0, comme le tampon ROM
    // d'Hatari (memory.c) — PAS de bus error dans la fenêtre.
    if (addr >= romBase && addr < romBase + romWindowSize())
        return addr < romBase + rom.size() ? rom[addr - romBase] : 0x00;

    // Carte réseau NE2000 sur le port cartouche (EtherNEC — extension NeoST) :
    // les lectures $FA0000-$FBFFFF encodent les accès registre (cf. Ne2000.hpp).
    // Décodée AVANT la ROM cartouche ; les deux sont mutuellement exclusives
    // (Machine::enableEtherNec refuse si une cartouche est montée).
    // NetUSBee : l'ISP1160 partage la fenêtre. Il est décodé D'ABORD (effets de
    // bord une fois par accès CPU : octet pair d'un mot, ou accès octet), puis la
    // NE2000 voit l'accès à son tour — la fenêtre LSB ($FA0000-$FA01FF) est aussi
    // son registre CR, et sans schéma on ne suppose aucun décodage exclusif.
    bool ispHit = false;
    uint8_t ispVal = 0xFF;
    if (isp1160 && addr >= stmap::CART_BASE && addr < stmap::CART_END) {
        const bool first = ioAccessWidth_ < 2 || (addr & 1) == 0;
        ispHit = isp1160->cartRead(addr, ispVal, first);
    }
    if (ne2000 && addr >= stmap::CART_BASE && addr < stmap::CART_END) {
        uint8_t v;
        if (ne2000->cartRead(addr, v)) return ispHit ? ispVal : v;
    }
    if (ispHit) return ispVal;

    // Périphériques abonnés au port cartouche (clés de protection…) : /ROM3 pour
    // $FBxxxx, /ROM4 pour $FAxxxx. Le premier qui pilote D0-D15 répond ; les autres
    // voient tout de même le cycle (une clé Notator écoute /ROM4 sans y répondre).
    if (!cartDevices.empty() && addr >= stmap::CART_BASE && addr < stmap::CART_END) {
        const bool first = ioAccessWidth_ < 2 || (addr & 1) == 0;
        const bool rom3  = addr >= 0xFB0000u;
        bool hit = false; uint8_t out = 0xFF;
        for (CartDevice* d : cartDevices) {
            uint8_t v;
            const bool h = rom3 ? d->rom3Read(addr, first, v) : d->rom4Read(addr, first, v);
            if (h && !hit) { hit = true; out = v; }
        }
        if (hit) return out;
    }

    // Port cartouche ($FA0000-$FBFFFF) : si une cartouche est montée, on expose
    // sa ROM ; le TOS lit le magic à $FA0000 et amorce (diagnostic/applicative).
    // Hors cartouche, l'espace reste "ouvert" (octets hauts, cf. plus bas) et le
    // magic ne correspond pas → boot normal.
    if (!cart.empty() && addr >= stmap::CART_BASE && addr < stmap::CART_BASE + cart.size())
        return cart[addr - stmap::CART_BASE];

    // Espace matériel.
    if (addr >= stmap::MMIO_BASE)
        return mmioRead8(addr);

    // STE « gamecart » : /ROM4 en $D80000-$DBFFFF, /ROM3 en $DC0000-$DFFFFF (256 Ko
    // chacun). La cartouche n'a que A1-A15 : chaque fenêtre de 64 Ko y est répétée.
    if (steCartWindow(addr)) {
        const uint32_t off = ((addr >= 0xDC0000u) ? 0x10000u : 0u) | (addr & 0xFFFFu);
        return off < cart.size() ? cart[off] : 0xFF;
    }

    // STE /ROM5, /ROM6 et /ROMP : sélection décodée, DTACK, mais aucune puce ne pilote
    // le bus de données → bus ouvert, comme une banque RAM vide ci-dessus.
    if (steRomSelectVoid(addr)) {
        if (ioAccessWidth_ >= 2) return uint8_t((addr & 1) ? cpuDb : cpuDb >> 8);
        return uint8_t(cpuDb);
    }

    // Trou au-dessus de $400000 (sous la ROM/cartouche) → bus error sur vrai ST ;
    // ici on renvoie $FF.
    return 0xFF;
}

void Bus::write8Slow(uint32_t addr, uint8_t v) {
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
    // décodés). Le blitter ($FF8A00-$FF8A3F) est ABSENT de ces tables communes
    // parce qu'il dépend du MODÈLE : il n'est dé-fauté que si `machineHasBlitter`
    // (cf. plus bas, `clear(0xFF8A00, 0x40)`) — sur une machine sans blitter la
    // plage faute, et EmuTOS en conclut « pas de blitter » puis bascule sur le VDI
    // logiciel. Idem le son DMA, ajouté seulement si le modèle le possède.
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
        // SCU MegaSTE (comme TT) : registres aux adresses IMPAIRES uniquement
        // ($FF8E01/03/…/0F) — même câblage que le MFP plus bas ; les octets PAIRS
        // $FF8E02-$FF8E0E ne sont pas décodés → bus error (cf. Hatari ioMem.c).
        for (uint32_t a = 0xFF8E01; a <= 0xFF8E0F; a += 2) clear(a, 1);
        clear(0xFF8E20, 0x04);                   // cache/CPU control
        clear(0xFF8C80, 0x08);                   // SCC série Z85C30
        clear(0xFF860E, 0x02);                   // mode densité DD/HD
        // MC68881 optionnel : ses CIR ($FFFA40-$FFFA5F) ne répondent que si le
        // socket est peuplé — sinon bus error et la sonde conclut « not found ».
        if (fpu.present) clear(Fpu::BASE, Fpu::END - Fpu::BASE);
    }

    ioFaultMachine_ = machine;
    ioFaultBuilt_   = true;
}

bool Bus::busFault(uint32_t addr) const {
    addr &= stmap::ADDR_MASK;

    // RAM ($0-$3FFFFF) : décodée par le MMU ; jamais de bus error (banque vide → 0).
    if (addr < 0x400000) return false;

    // ROM TOS : jamais de bus error sur TOUTE la fenêtre décodée (1 Mo à $E00000,
    // 192 Ko à $FC0000 — cf. romWindowSize), pas seulement la taille du fichier.
    if (addr >= romBase && addr < romBase + romWindowSize()) return false;

    // Port cartouche ($FA0000-$FBFFFF) : banque ROM sur le vrai matériel ; lit $FF
    // si rien n'est branché → jamais de bus error (le TOS y lit le magic au reset).
    // STE avec « gamecart » : la fenêtre a déménagé en $D80000, ici rien ne répond.
    if (addr >= stmap::CART_BASE && addr < stmap::CART_END) return steCartRelocated();
    if (steCartWindow(addr)) return false;

    // Espace IO ($FF8000-$FFFFFF) : carte octet par octet (cf. buildIoFault).
    if (addr >= stmap::MMIO_BASE) {
        if (!ioFaultBuilt_ || ioFaultMachine_ != machine) buildIoFault();
        return ioFault_[addr - stmap::MMIO_BASE] != 0;
    }

    // STE : /ROM6 et /ROMP décodés sans puce (cf. steRomSelectVoid) → DTACK, pas
    // de bus error en lecture ; l'écriture faute à l'étage 1 de busFaultNSlow.
    if (steRomSelectVoid(addr)) return false;

    // Tout le reste — trous $400000-$F9FFFF (RAM absente, ROM cartouche basse,
    // IDE, VME...) et $FF0000-$FF7FFF (sous l'espace IO) — n'est décodé par aucun
    // circuit → bus error sur le vrai ST (Hatari BusErrMem_bank). C'est notamment
    // ce que sondent les cartouches de diagnostic pour valider la gestion d'erreur.
    return true;
}

bool Bus::busSelfTest() {
    int pass = 0, fail = 0;
    auto chk = [&](const char* n, long got, long want) {
        if (got == want) { ++pass; }
        else { ++fail; std::fprintf(stderr, "  FAIL %-30s got=%ld want=%ld\n", n, got, want); }
    };
    // Force le superviseur : sinon la branche user-mode de busFaultN fait fauter TOUT
    // l'espace IO ($0-$7FF + $FF8000+) et masque l'invariant whitelist qu'on teste.
    // SR restauré en sortie — le CPU continue de tourner après le self-test.
    const uint16_t savedSr = cpu ? cpu->sr() : 0;
    if (cpu) cpu->setSr(uint16_t(savedSr | 0x2000));

    // 1) RAM ($0-$3FFFFF) : jamais de faute (byte + long).
    chk("RAM byte", busFault(0x1000) ? 1 : 0, 0);
    chk("RAM long", busFaultN(0x1000, 4, false) ? 1 : 0, 0);
    // 2) Sous l'espace IO ($FF0000-$FF7FFF) : faute inconditionnelle (BusErrMem_bank).
    chk("sous-IO byte", busFault(0xFF7FFE) ? 1 : 0, 1);
    // 3) Un accès qui DÉMARRE hors IO ne se « sauve » pas en débordant dans l'IO
    //    (dispatch sur l'octet de départ) : long $FF7FFE faute malgré $FF8000-01 valides.
    chk("hors-IO ne se sauve pas", busFaultN(0xFF7FFE, 4, false) ? 1 : 0, 1);
    // 4) Palette $FF8240 whitelistée : lecture jamais fautive (byte + word).
    chk("palette byte", busFault(0xFF8240) ? 1 : 0, 0);
    chk("palette word", busFaultN(0xFF8240, 2, false) ? 1 : 0, 0);
    // 5) INVARIANT WHITELIST : chercher une frontière IO (octet fautif adjacent à un
    //    non-fautif) → un word qui la chevauche NE FAUTE PAS ; et un word tout-fautif FAUTE.
    bool mix = false, allf = false;
    for (uint32_t a = stmap::MMIO_BASE; a < 0xFFFFFE && !(mix && allf); a += 2) {
        const bool f0 = busFault(a), f1 = busFault(a + 1);
        if (f0 != f1 && !mix) { chk("word mixte ne faute pas", busFaultN(a, 2, false) ? 1 : 0, 0); mix = true; }
        if (f0 && f1 && !allf) { chk("word tout-fautif faute", busFaultN(a, 2, false) ? 1 : 0, 1); allf = true; }
    }
    chk("frontière whitelist trouvée", mix ? 1 : 0, 1);
    chk("word tout-fautif trouvé", allf ? 1 : 0, 1);
    // 6) Écritures TOUJOURS fautives : $0-$7 (miroir vecteurs) et port cartouche (ROM).
    chk("write $0 protégé", busFaultN(0, 2, true) ? 1 : 0, 1);
    chk("write cartouche protégé", busFaultN(stmap::CART_BASE, 2, true) ? 1 : 0, 1);
    // 7) STE, décodage ROM du GST MCU (ST4081S) : $FC0000 faute avec un TOS en
    //    $E00000 (cas IK+, fidèle), $EC0000 aussi ; /ROM6 et /ROMP répondent en
    //    lecture et fautent en écriture, /ROM5 aussi (netlist) ; gamecart déplace
    //    la cartouche.
    if (machine == MachineType::Ste && romBase == stmap::ROM_E00000) {
        chk("STE $FC000C faute", busFaultN(0xFC000C, 2, false) ? 1 : 0, 1);
        chk("STE $EC0000 faute", busFaultN(0xEC0000, 2, false) ? 1 : 0, 1);
        chk("STE $EBFFFE décodé", busFaultN(0xEBFFFE, 2, false) ? 1 : 0, 0);
        chk("STE /ROM6 lecture", busFaultN(0xD00000, 2, false) ? 1 : 0, 0);
        chk("STE /ROM6 écriture", busFaultN(0xD00000, 2, true) ? 1 : 0, 1);
        chk("STE /ROMP lecture", busFaultN(0xFE1FFE, 2, false) ? 1 : 0, 0);
        chk("STE fin /ROMP", busFaultN(0xFE2000, 2, false) ? 1 : 0, 1);
        chk("STE /ROM5 lecture", busFaultN(0xD7FFFE, 2, false) ? 1 : 0, 0);
        chk("STE fin /ROM5", busFaultN(0xD80000, 2, false) ? 1 : 0, 1);
        const bool savedGc = gameCart;
        gameCart = true;
        chk("gamecart : $FA0000 faute", busFaultN(0xFA0000, 2, false) ? 1 : 0, 1);
        chk("gamecart : /ROM4 en $D80000", busFaultN(0xD80000, 2, false) ? 1 : 0, 0);
        chk("gamecart : /ROM3 écriture", busFaultN(0xDC0000, 2, true) ? 1 : 0, 1);
        gameCart = savedGc;
    }

    if (cpu) cpu->setSr(savedSr);               // restaure le SR d'entrée
    std::fprintf(stderr, "[bus-selftest] %d OK, %d FAIL\n", pass, fail);
    return fail == 0;
}

bool Bus::busFaultNSlow(uint32_t addr, unsigned n, bool write) const {
    addr &= stmap::ADDR_MASK;

    // 1) Écritures TOUJOURS fautives, même en superviseur (cf. Bus.hpp) : la ROM
    //    TOS et le port cartouche sont physiquement en lecture seule (Hatari
    //    ROMmem_*put → bus error — certains chargeurs s'en servent), et les 8
    //    premiers octets de RAM sont un miroir ROM des vecteurs reset (SysMem_*put :
    //    « write protected »).
    if (write) {
        if (addr < 0x8) return true;
        if (addr >= romBase && addr < romBase + romWindowSize()) return true;
        if (addr >= stmap::CART_BASE && addr < stmap::CART_END) return true;
        if (steRomSelectVoid(addr) || steCartWindow(addr)) return true;
    }

    // 2) Protection superviseur (GLUE/MMU) : en mode utilisateur, $0-$7FF et tout
    //    l'espace IO ($FF8000-$FFFFFF) fautent — port de SysMem_*get/put (memory.c)
    //    et du test is_super_access d'ioMem.c. Le bit S n'est consulté QUE si
    //    l'adresse est concernée (l'immense majorité des accès l'évite).
    if (addr < 0x800 || addr >= stmap::MMIO_BASE) {
        if (cpu && !cpu->supervisor()) return true;
    }

    // 3) Banque sélectionnée par l'adresse de DÉPART (Hatari cpu/memory.c : le
    //    dispatch lget/wget choisit la banque sur le premier octet). Un accès qui
    //    démarre HORS de l'espace IO ne consulte jamais la whitelist ioMem, même
    //    s'il déborde dedans : `move.l $FF7FFE` faute inconditionnellement
    //    (BusErrMem_bank), il ne « sauve » pas ses octets $FF8000-01.
    if (addr < stmap::MMIO_BASE) return busFault(addr);

    // 4) Espace IO : carte par octet (whitelist ioMem) — un word/long ne faute
    //    que si TOUS ses octets fautent.
    for (unsigned i = 0; i < n; ++i)
        if (!busFault(addr + i)) return false;
    return true;
}

// -----------------------------------------------------------------------------
//  Cache externe 16 Ko du Mega STE — port fidèle de Hatari m68000.c
//  (MegaSTE_Cache_Addr_Cacheable / _Read / _Update / _Flush). Données pures :
//  la facturation des cycles est faite par le cœur Moira (cf. Cpu68k.cpp).
// -----------------------------------------------------------------------------
void Bus::megaSteCacheFlush() {
    std::memset(megaSteCache.valid, 0, sizeof megaSteCache.valid);
}

bool Bus::megaSteCacheable(uint32_t addr, int size, bool write, bool super) const {
    addr &= stmap::ADDR_MASK;                    // 68000 : 24 bits d'adresse
    // Un accès qui provoquerait une address/bus error n'est jamais caché.
    if (size == 2 && (addr & 1)) return false;   // mot sur adresse impaire
    if (addr < 0x4 && write) return false;       // écriture vecteurs reset (miroir ROM)
    if (addr < 0x800 && !super) return false;    // RAM système en mode utilisateur
    // RAM ST installée (< 4 Mo) : cachable en lecture comme en écriture.
    if (addr < ram.size() && addr < 0x400000) return true;
    // ROM TOS (fenêtre décodée complète) : cachable en LECTURE seulement
    // (écrire fauterait) — Hatari cache tout $E00000-$F00000.
    if (addr >= romBase && addr < romBase + romWindowSize() && !write) return true;
    // IO, cartouche, trous : jamais cachés.
    return false;
}

bool Bus::megaSteCacheRead(uint32_t addr, int size, uint16_t& val, bool super) {
    addr &= stmap::ADDR_MASK;
    if (!megaSteCacheable(addr, size, /*write=*/false, super)) return false;
    const uint16_t line = (addr >> 1) & 0x1FFF;          // bits 1-13
    const uint16_t tag  = (addr >> 14) & 0x3FF;          // bits 14-23
    if (!megaSteCache.valid[line] || megaSteCache.tag[line] != tag) return false;
    val = megaSteCache.value[line];                      // hit : le mot caché
    if (size == 1)                                       // accès octet : moitié voulue
        val = (addr & 1) ? (val & 0xFF) : ((val >> 8) & 0xFF);
    return true;
}

bool Bus::megaSteCacheUpdate(uint32_t addr, int size, uint16_t val, bool write, bool super) {
    addr &= stmap::ADDR_MASK;
    if (!megaSteCacheable(addr, size, write, super)) return false;
    const uint16_t line = (addr >> 1) & 0x1FFF;
    const uint16_t tag  = (addr >> 14) & 0x3FF;
    if (size == 2) {                                     // accès mot : remplace la ligne
        megaSteCache.valid[line] = 1;
        megaSteCache.tag[line]   = tag;
        megaSteCache.value[line] = val;
        return true;
    }
    // Écriture octet : ne met à jour la ligne QUE si elle cache déjà ce mot (le
    // bus ne porte qu'un octet, pas de quoi remplir l'autre moitié de la ligne).
    if (megaSteCache.valid[line] && megaSteCache.tag[line] == tag) {
        val &= 0xFF;
        megaSteCache.value[line] = (addr & 1)
            ? uint16_t((megaSteCache.value[line] & 0xFF00) | val)
            : uint16_t((megaSteCache.value[line] & 0x00FF) | (val << 8));
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
//  Accès DMA (FDC / ACSI / son STE) — port de STMemory_DMA_ReadByte/WriteByte :
//  même plan mémoire que le CPU (MMU, ROM…), mais une zone fautive lit 0 /
//  absorbe l'écriture au lieu de déclencher une bus error (le DMA n'a pas de
//  cycle d'exception). Cf. Bus.hpp.
// -----------------------------------------------------------------------------
uint8_t Bus::dmaRead8(uint32_t addr) {
    addr &= stmap::ADDR_MASK;
    if (busFault(addr)) return 0x00;         // DMA_READ_BYTE_BUS_ERR
    // Jamais de dispatch MMIO : certains registres whitelistés (pads STE
    // $FF9200, FDC $FF8604-07) déclenchent une bus error périphérique en accès
    // octet — levée ici hors du try/catch de Moira, elle terminerait le
    // processus. Chez Hatari le pointeur DMA masqué n'atteint jamais l'espace
    // IO ; on lit 0 comme pour une zone fautive (et sans effet de bord puce).
    if (addr >= stmap::MMIO_BASE) return 0x00;
    return read8(addr);
}

void Bus::dmaWrite8(uint32_t addr, uint8_t v) {
    addr &= stmap::ADDR_MASK;
    // $0-$7 (SSP + PC de reset) sont mappés sur la ROM par la GLUE : personne ne peut
    // les écrire, DMA compris. busFault() ne connaît pas cette protection en écriture,
    // d'où le test explicite — sans lui, un pointeur DMA disquette dégénéré (registres
    // $FF8609/0B/0D à 0) écrasait le miroir SSP/PC et faisait partir dans le décor
    // l'idiome de reboot à chaud « move.l $4.w,a0 ; jmp (a0) ». Chez Hatari l'écriture
    // est simplement perdue (cpu/memory.c:775, SysMem_bput : « if (addr < 0x8) »).
    if (addr < 0x8 || busFault(addr)) return;   // écriture en zone protégée/fautive perdue
    if (addr >= stmap::MMIO_BASE) return;       // pas de dispatch MMIO (cf. dmaRead8)
    write8(addr, v);
}

// Lecture débogueur : même plan mémoire que read8 mais SANS effet de bord (pas
// de dispatch MMIO, pas de wait state, pas de bus error). Cf. Bus.hpp.
uint8_t Bus::peek8(uint32_t addr) const {
    addr &= stmap::ADDR_MASK;
    if (addr < 0x400000) {
        const int64_t phys = mmuTranslate(addr);
        return phys >= 0 ? ram[static_cast<std::size_t>(phys)] : 0xFF;
    }
    if (addr >= romBase && addr < romBase + romWindowSize())
        return addr < romBase + rom.size() ? rom[addr - romBase] : 0x00;
    if (steCartWindow(addr)) {               // même règle que read8Slow (gamecart)
        const uint32_t off = ((addr >= 0xDC0000u) ? 0x10000u : 0u) | (addr & 0xFFFFu);
        return off < cart.size() ? cart[off] : 0xFF;
    }
    if (!steCartRelocated() && !cart.empty()
        && addr >= stmap::CART_BASE && addr < stmap::CART_BASE + cart.size())
        return cart[addr - stmap::CART_BASE];
    return 0xFF;                             // MMIO / trous : neutre, sans dispatch
}

// Vecteurs reset $0-$7 : recopie ROM → RAM (STMemory_SetDefaultConfig). Écrit la
// RAM physique directement — l'écriture CPU y fauterait (miroir ROM, cf. busFaultN).
void Bus::seedResetVectors() {
    const std::size_t n = std::min<std::size_t>(8, std::min(rom.size(), ram.size()));
    for (std::size_t i = 0; i < n; ++i) ram[i] = rom[i];
}

// --- Accès 16/32 bits : le 68000 est big-endian, on assemble octet par octet --
uint16_t Bus::read16Slow(uint32_t addr) {
    const uint8_t saved = ioAccessWidth_;
    ioAccessWidth_ = 2;
    // Séquencement EXPLICITE octet fort puis octet faible : les opérandes de « | »
    // ne sont pas ordonnés en C++, or la MMIO a des effets de bord — l'ordre des
    // dispatchs doit être reproductible quel que soit le compilateur (déterminisme
    // du headless clang/gcc).
    const uint16_t hi = read8(addr);
    const uint16_t v  = static_cast<uint16_t>((hi << 8) | read8(addr + 1));
    ioAccessWidth_ = saved;
    return v;
}
uint32_t Bus::read32(uint32_t addr) {
    const uint8_t saved = ioAccessWidth_;
    ioAccessWidth_ = 4;
    const uint32_t hi = read16(addr);          // séquencé : mot fort d'abord (cf. read16)
    const uint32_t v  = (hi << 16) | read16(addr + 2);
    ioAccessWidth_ = saved;
    return v;
}
void Bus::write16(uint32_t addr, uint16_t v) {
    // Le décodage 24 bits précède aussi le raccourci atomique du blitter.
    addr &= stmap::ADDR_MASK;
    const uint8_t saved = ioAccessWidth_;
    ioAccessWidth_ = 2;
    // Blitter ($FF8A00-$FF8A3F) : écriture MOT atomique — le registre contrôle
    // (BUSY, $FF8A3C) et le skew ($FF8A3D) tiennent dans un même mot ; il faut poser
    // les DEUX octets avant que BUSY ne démarre run() (sinon skew périmé → plans
    // d'icône désalignés). Cf. Blitter::write16.
    if (addr >= 0xFF8A00 && addr + 1 <= 0xFF8A3F && blitter && machineHasBlitter(machine)) {
        blitter->write16(addr, v);
        ioAccessWidth_ = saved;        // NE PAS fuiter la largeur (sinon les bus-errors octet restent désarmées)
        return;
    }
    write8(addr,     static_cast<uint8_t>(v >> 8));
    write8(addr + 1, static_cast<uint8_t>(v));
    ioAccessWidth_ = saved;
}
void Bus::write32(uint32_t addr, uint32_t v) {
    addr &= stmap::ADDR_MASK;
    const uint8_t saved = ioAccessWidth_;
    ioAccessWidth_ = 4;
    if (addr >= 0xFF8A00 && addr + 3 <= 0xFF8A3F && blitter && machineHasBlitter(machine)) {
        blitter->write32(addr, v);    // écriture LONG atomique (idem write16)
        ioAccessWidth_ = saved;        // restaure la largeur (cf. write16)
        return;
    }
    write16(addr,     static_cast<uint16_t>(v >> 16));
    write16(addr + 2, static_cast<uint16_t>(v));
    ioAccessWidth_ = saved;
}

// -----------------------------------------------------------------------------
//  Dispatch MMIO ($FF8000-$FFFFFF). Chaque puce expose read8/write8 ; le bus
//  ne fait QUE router, il ne connaît pas les détails internes des composants.
// -----------------------------------------------------------------------------
// =============================================================================
//  A31 — dispatch MMIO par TABLE (cf. Bus.hpp § TABLE DE PLAGES MMIO).
//
//  Les corps ci-dessous sont les anciennes branches des deux chaînes de `if`,
//  déplacées TELLES QUELLES : mêmes wait states, mêmes updateIpl(), mêmes octets
//  « void », même ordre d'effets. Le seul changement est QUI les appelle.
// =============================================================================
uint8_t Bus::rdShifter(uint32_t a) { return shifter->read8(a); }
void    Bus::wrShifter(uint32_t a, uint8_t v) { shifter->write8(a, v); }

// Le YM2149 (2 registres : sélecteur $FF8800, donnée $FF8802) est MIROIRÉ sur tout
// $FF8800-$FF88FF (le matériel ne décode que A1, cf. Hatari IoMem_Init shadow PSG) :
// psg->read8 décode par (addr & 3), donc le miroir est géré tel quel.
uint8_t Bus::rdPsg(uint32_t a) {
    if (cpu) cpu->addPsgWaitCycles();     // wait state YM2149 (4 cyc / 1er accès instr.)
    return psg->read8(a);
}
void Bus::wrPsg(uint32_t a, uint8_t v) {
    if (cpu) cpu->addPsgWaitCycles();
    // Écriture OCTET (ou movep) sur adresse IMPAIRE : ombre du registre pair — le YM
    // ne décode pas A0, $FF8801 agit comme $FF8800 (sélecteur) et $FF8803 comme
    // $FF8802 (donnée). Port de PSG_ff8801/ff8803_WriteByte (le fix « X-Out musique
    // muette » d'Hatari). L'octet impair d'un accès MOT reste ignoré (il est porté
    // par l'octet pair du même accès, comme chez Hatari).
    if (a & 1) {
        if (ioAccessWidth() == 1) psg->write8(a & ~1u, v);
        return;
    }
    psg->write8(a, v);
}

uint8_t Bus::rdFdc(uint32_t a) {
    // Le registre de densité est câblé seulement sur Mega STE (ioMem.c).
    // Ailleurs, les octets autorisés par la carte des bus errors sont void.
    if (a >= 0xFF860E && machine != MachineType::MegaSte) return 0xFF;
    return fdc->read8(a);
}
void    Bus::wrFdc(uint32_t a, uint8_t v) {
    if (a >= 0xFF860E && machine != MachineType::MegaSte) return;
    fdc->write8(a, v);
    if (cpu) cpu->updateIpl();            // l'INTRQ FDC (GPIP5) a pu changer
}

// Les relectures tirées de la netlist du GST MCU ne valent que pour le STE.
uint8_t Bus::rdDmaSnd(uint32_t a) { dmasnd->steNetlist = machine == MachineType::Ste; return dmasnd->read8(a); }
void    Bus::wrDmaSnd(uint32_t a, uint8_t v) { dmasnd->steNetlist = machine == MachineType::Ste; dmasnd->write8(a, v); }

uint8_t Bus::rdBlitter(uint32_t a) { return blitter->read8(a); }        // Mega ST/STE
void    Bus::wrBlitter(uint32_t a, uint8_t v) { blitter->write8(a, v); }

uint8_t Bus::rdMfp(uint32_t a) {
    // $FFFA31-$FFFA3F (impairs) : VOID chez Hatari (ioMemTabST.c:143-150,
    // IoMem_VoidRead) — lecture 0xFF, AUCUN wait-state (pas de handler). Le dernier
    // registre câblé est l'UDR USART à $FFFA2F.
    // Octets PAIRS : non décodés par le 68901 (registres sur adresses impaires).
    // Atteignables seulement par un accès MOT (la whitelist faute l'accès octet) :
    // chez Hatari l'octet pair passe alors par IoMem_BusErrorEvenReadAccess → 0xFF,
    // jamais par la puce. Sans ce filtre, les default du MFP en faisaient des
    // cellules RAM fantômes.
    if (!(a & 1) || (a & 0x3F) >= 0x31) return 0xFF;
    // Wait state MFP (4 cyc) facturé UNE fois par accès : seul l'octet IMPAIR porte
    // un registre câblé (Hatari : M68000_WaitState(4) dans le handler du registre ;
    // l'octet pair d'un accès mot n'ajoute rien).
    if (cpu) cpu->addMfpWaitCycles();
    const uint8_t v = mfp->read8(a);
    if (cpu) cpu->updateIpl();            // l'état d'IRQ a pu changer
    return v;
}
void Bus::wrMfp(uint32_t a, uint8_t v) {
    // $FFFA31-$FFFA3F : void — écriture ABSORBÉE sans wait-state (cf. rdMfp) ;
    // l'ancien chemin en faisait 8 octets de RAM relisible cachés dans le MFP.
    // Octets PAIRS : non décodés, écriture jetée (IoMem_BusErrorEvenWriteAccess).
    if (!(a & 1) || (a & 0x3F) >= 0x31) return;
    if (cpu) cpu->addMfpWaitCycles();
    mfp->write8(a, v);
    if (cpu) cpu->updateIpl();            // (dé)masquage, fin d'interruption...
}

uint8_t Bus::rdIkbd(uint32_t a) {
    // $FFFC01/$FFFC03 : octets impairs non décodés (« void », ioMemTabST.c) → 0xFF
    // sans effet de bord ACIA ni wait state. Ainsi un accès MOT à $FFFC00 ne touche
    // l'ACIA qu'une fois (et n'est facturé qu'une fois).
    if (a & 1) return 0xFF;
    if (cpu) cpu->addAciaWaitCycles();    // wait state ACIA (6 cyc + E-Clock)
    const uint8_t v = ikbd->read8(a);     // ACIA clavier $FFFC00/$FFFC02
    if (cpu) cpu->updateIpl();
    return v;
}
void Bus::wrIkbd(uint32_t a, uint8_t v) {
    if (a & 1) return;                    // void — écriture absorbée
    if (cpu) cpu->addAciaWaitCycles();
    ikbd->write8(a, v);
    if (cpu) cpu->updateIpl();
}

uint8_t Bus::rdMidi(uint32_t a) {
    if (a & 1) return 0xFF;               // $FFFC05/$FFFC07 : void (cf. ACIA clavier)
    if (cpu) cpu->addAciaWaitCycles();    // ACIA MIDI : même timing que l'ACIA clavier
    const uint8_t v = midi->read8(a);     // ACIA MIDI ($FFFC04/06) — bouclage OUT→IN
    if (cpu) cpu->updateIpl();            // une lecture peut effacer l'IRQ ACIA
    return v;
}
void Bus::wrMidi(uint32_t a, uint8_t v) {
    if (a & 1) return;                    // void — écriture absorbée
    if (cpu) cpu->addAciaWaitCycles();
    midi->write8(a, v);
    if (cpu) cpu->updateIpl();            // un octet bouclé peut lever l'IRQ ACIA
}

uint8_t Bus::rdRtc(uint32_t a) { return rtc->read8(a); }            // Mega ST / Mega STE
void    Bus::wrRtc(uint32_t a, uint8_t v) { rtc->write8(a, v); }

// STE / Mega STE : joypads / paddles / lightpen + DIP switches Mega STE
// ($FF9200-$FF9223). Port fidèle de Hatari joy.c via StePads (multiplexage par le
// masque écrit en $FF9202, mapping directions/feu des pads A/B, valeurs au repos).
// L'octet HAUT de $FF9200 = DIP Mega STE (0xBF par défaut ; logique inversée, cf.
// StePads / IoMemTabMegaSTE_DIPSwitches_Read). Les registres mots ($FF9200/02/20/22)
// renvoient l'octet voulu en big-endian (haut = adresse paire).
uint8_t Bus::rdStePads(uint32_t a) {
    // Accès OCTET interdits (port joy.c) : $FF9200 « n'aime pas être lu en octet »
    // à l'adresse PAIRE ($FF9201 reste lisible en octet), et les mots lightpen
    // $FF9220/22 ne se lisent qu'en mot → bus error déclenchée par le périphérique
    // (comme le FDC $FF8604 en mode octet).
    if (ioAccessWidth_ == 1 && cpu
        && (a == 0xFF9200 || (a >= 0xFF9220 && a <= 0xFF9223))) {
        cpu->triggerBusError(a, false);   // longjmp/throw, sauf double faute
        return 0xFF;                      // double faute → CPU halté
    }
    switch (a) {
        // $FF9200.w : boutons feu (octet bas) + DIP Mega STE (octet haut).
        case 0xFF9200: return uint8_t(stePads.readButtonsDip() >> 8);   // DIP
        case 0xFF9201: return uint8_t(stePads.readButtonsDip() & 0xFF); // boutons
        // $FF9202.w : directions + boutons (info utile dans l'octet HAUT $FF9202).
        case 0xFF9202: return uint8_t(stePads.readDirections() >> 8);
        case 0xFF9203: {                     // lire l'octet bas invalide la sélection (JOYWE)
            const uint8_t v = uint8_t(stePads.readDirections() & 0xFF);
            if (machine == MachineType::Ste) stePads.noteLowByteRead();
            return v;
        }
        // Paddle/analogique X/Y ($FF9211/13/15/17) : axes hôte ou repli numérique,
        // plage $04-$43 (cf. StePads::readAnalog).
        case 0xFF9211: case 0xFF9213:
        case 0xFF9215: case 0xFF9217: return stePads.readAnalog(a);
        // Lightpen X/Y ($FF9220-$FF9223) : non supporté → 0 (mots à $FF9220/22).
        case 0xFF9220: case 0xFF9221:
        case 0xFF9222: case 0xFF9223: return uint8_t(stePads.readLightpen() >> (a & 1 ? 0 : 8));
        // Octets non décodés de la plage ($FF9204-$FF9210, etc.) : au repos.
        default: return 0xFF;
    }
}
void Bus::wrStePads(uint32_t a, uint8_t v) {
    // $FF9202 = latch de sélection des lignes des joypads (cf.
    // Joy_StePadMulti_WriteWord). Les autres registres $FF92xx sont en lecture
    // seule / sans effet (joy.c : Joy_StePadButtons_DIPSwitches_WriteWord ne fait rien).
    // On latche les deux octets du mot $FF9202/$FF9203.
    if (a == 0xFF9202 || a == 0xFF9203) { stePads.writeSelectByte(a, v); return; }
    // STE : le GST MCU n'acquitte $FF9200-01, $FF9210-17 et $FF9220-23 qu'en
    // LECTURE (netlist 4081 : BUTR/PADR/PENR contiennent IRWZ) → toute écriture
    // fait bus error. Mega STE non touché ($FF9200 y porte les DIP, pas de netlist).
    if (machine == MachineType::Ste && cpu) { cpu->triggerBusError(a, true); return; }
    // Écriture OCTET sur $FF9200 (adresse paire) → bus error, comme en lecture
    // (Joy_StePadButtons_DIPSwitches_WriteWord). Les autres écritures de la plage
    // (lightpen incluse : IoMem_WriteWithoutInterception) sont ignorées sans faute.
    if (a == 0xFF9200 && ioAccessWidth_ == 1 && cpu) { cpu->triggerBusError(a, true); return; }
}

// Registre Cache/CPU MegaSTE $FF8E21, relisible (latch écrit par TOS 2.x). cf.
// Bus.hpp megaSteCacheCtrl. $FF8E20/22/23 restent « void » (→ glue → 0xFF).
uint8_t Bus::rdMsteCtl(uint32_t) { return megaSteCacheCtrl; }
void Bus::wrMsteCtl(uint32_t, uint8_t v) {
    // Contrainte matérielle « le cache ne peut être actif qu'à 16 MHz » — si bit0
    // (cache) est demandé alors que bit1 (vitesse) = 0 (8 MHz), le matériel force
    // bit0 à 0 (cf. Hatari IoMemTabMegaSTE_CacheCpuCtrl_WriteByte). EFFET réel (port
    // MegaSTE_CPU_Cache_Update) : cache désactivé → invalidation complète ; bit1 →
    // bascule du débit cycles 8/16 MHz du cœur CPU.
    if ((v & 0x02) == 0 && (v & 0x01)) v &= 0xFE;   // cache impossible à 8 MHz
    // L'ACSI compte son délai en cycles CPU, le Scheduler en cycles bus 8 MHz.
    // Conserver le nombre de cycles CPU restants lors d'une bascule 8/16 MHz.
    if (fdc) fdc->onCpuSpeedChange((megaSteCacheCtrl & 2) != 0, (v & 2) != 0);
    megaSteCacheCtrl = v;
    if ((v & 0x01) == 0) megaSteCacheFlush();
    if (cpu) cpu->setMegaSteSpeed((v & 0x02) != 0);
}

// Coprocesseur MC68881 optionnel ($FFFA40-$FFFA5F) — cf. Fpu.hpp. Quand il est
// absent (défaut), la zone n'est pas whitelistée → bus error avant d'arriver ici.
uint8_t Bus::rdFpu(uint32_t a) { return fpu.read8(a); }
void    Bus::wrFpu(uint32_t a, uint8_t v) { fpu.write8(a, v); }

// SCU MegaSTE : registres d'interruption ($FF8E01-$FF8E0F). cf. Scu.hpp.
uint8_t Bus::rdScu(uint32_t a) { return scu.read8(a); }
void    Bus::wrScu(uint32_t a, uint8_t v) {
    // Écrire un masque (dé)masque des IRQ → on recalcule l'IPL CPU (gating conditionnel).
    if (scu.write8(a, v) && cpu) cpu->updateIpl();
}

// SCC Z85C30 ($FF8C80-$FF8C87) — Mega STE. Lecture comme écriture peuvent (dé)masquer
// l'IRQ niveau 5.
uint8_t Bus::rdScc(uint32_t a) {
    const uint8_t v = scc->read8(a);
    if (cpu) cpu->updateIpl();
    return v;
}
void Bus::wrScc(uint32_t a, uint8_t v) {
    scc->write8(a, v);
    if (cpu) cpu->updateIpl();
}

// -----------------------------------------------------------------------------
//  LA TABLE. L'ordre des lignes suit l'ancienne chaîne de `if` — mais il n'est
//  plus SIGNIFIANT : les plages sont DISJOINTES, et c'est vérifié par
//  Bus::mmioTableDisjoint(), appelé depuis tests/selftest_logic.cpp. Une ligne
//  ajoutée qui chevaucherait une autre fait rougir le palier `fast` en nommant
//  les deux coupables — au lieu de dépendre en silence de sa position.
// -----------------------------------------------------------------------------
const Bus::MmioSlot* Bus::mmioTable(std::size_t& count) {
    static const MmioSlot kSlots[] = {
        { stmap::SHIFTER_BASE, stmap::SHIFTER_END,          &Bus::claimsShifter,  &Bus::rdShifter, &Bus::wrShifter, "shifter"   },
        { stmap::PSG_BASE,     stmap::PSG_BASE + 0xFF,      &Bus::claimsPsg,      &Bus::rdPsg,     &Bus::wrPsg,     "ym2149"    },
        { stmap::DMA_FDC_BASE, stmap::DMA_FDC_BASE + 0x0F,  &Bus::claimsFdc,      &Bus::rdFdc,     &Bus::wrFdc,     "fdc/dma"   },
        { stmap::DMASND_BASE,  stmap::DMASND_END - 1,       &Bus::claimsDmaSnd,   &Bus::rdDmaSnd,  &Bus::wrDmaSnd,  "dmasound"  },
        { 0xFF8A00,            0xFF8A3F,                    &Bus::claimsBlitter,  &Bus::rdBlitter, &Bus::wrBlitter, "blitter"   },
        { 0xFF8C80,            0xFF8C87,                    &Bus::claimsScc,      &Bus::rdScc,     &Bus::wrScc,     "scc"       },
        { 0xFF8E01,            0xFF8E0F,                    &Bus::claimsScu,      &Bus::rdScu,     &Bus::wrScu,     "scu"       },
        { 0xFF8E21,            0xFF8E21,                    &Bus::claimsMsteCtrl, &Bus::rdMsteCtl, &Bus::wrMsteCtl, "mste-cache"},
        { 0xFF9200,            0xFF9223,                    &Bus::claimsStePads,  &Bus::rdStePads, &Bus::wrStePads, "stepads"   },
        { stmap::MFP_BASE,     stmap::MFP_BASE + 0x3F,      &Bus::claimsMfp,      &Bus::rdMfp,     &Bus::wrMfp,     "mfp"       },
        { Fpu::BASE,           Fpu::END - 1,                &Bus::claimsFpu,      &Bus::rdFpu,     &Bus::wrFpu,     "fpu"       },
        { stmap::ACIA_BASE,    stmap::ACIA_BASE + 3,        &Bus::claimsIkbd,     &Bus::rdIkbd,    &Bus::wrIkbd,    "acia-ikbd" },
        { 0xFFFC04,            0xFFFC07,                    &Bus::claimsMidi,     &Bus::rdMidi,    &Bus::wrMidi,    "acia-midi" },
        { 0xFFFC21,            0xFFFC3F,                    &Bus::claimsRtc,      &Bus::rdRtc,     &Bus::wrRtc,     "rtc"       },
    };
    count = sizeof(kSlots) / sizeof(kSlots[0]);
    return kSlots;
}

std::size_t Bus::mmioTableSize() { std::size_t n = 0; mmioTable(n); return n; }

bool Bus::mmioTableDisjoint(const char** a, const char** b) {
    std::size_t n = 0;
    const MmioSlot* t = mmioTable(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (t[i].lo > t[i].hi) { if (a) *a = t[i].name; if (b) *b = t[i].name; return false; }
        for (std::size_t j = i + 1; j < n; ++j)
            if (t[i].lo <= t[j].hi && t[j].lo <= t[i].hi) {
                if (a) *a = t[i].name;
                if (b) *b = t[j].name;
                return false;
            }
    }
    return true;
}

const Bus::MmioSlot* Bus::mmioSlotFor(uint32_t addr) const {
    std::size_t n = 0;
    const MmioSlot* t = mmioTable(n);
    for (std::size_t i = 0; i < n; ++i)
        if (addr >= t[i].lo && addr <= t[i].hi && (this->*(t[i].claims))(addr)) return &t[i];
    return nullptr;
}

uint8_t Bus::mmioRead8(uint32_t addr) {
    if (const MmioSlot* s = mmioSlotFor(addr)) return (this->*(s->read))(addr);
    if (machine == MachineType::Ste) {
        // $FF8001 : quatre bascules ID0-3 relues sur IDB0-3, IDB4-7 à VSS (netlist
        // REGISTER.PIN du 4081) — le haut de l'octet relit 0.
        if (addr == 0xFF8001 && glue) return uint8_t(glue->read8(addr) & 0x0F);
        // $FF9000 : IDB8 = GAMECART, IDB9 = VSS ; IDB10-15 non pilotés (bus flottant,
        // rendu $FF comme le reste du vide MMIO).
        if (addr == 0xFF9000) return uint8_t(0xFC | (gameCart ? 1 : 0));
    }
    if (glue) return glue->read8(addr);   // MMU et reste du MMIO
    return 0xFF;
}

void Bus::mmioWrite8(uint32_t addr, uint8_t v) {
    if (const MmioSlot* s = mmioSlotFor(addr)) { (this->*(s->write))(addr, v); return; }
    if (addr == 0xFF9000 && machine == MachineType::Ste) { gameCart = v & 1; return; }
    if (glue) glue->write8(addr, v);
}

// A23 : primitive unique du vol de cycles bus — voir la déclaration (Bus.hpp) pour
// l'invariant (stall CPU toujours, crédit ordonnanceur HORS quantum seulement).
void Bus::stealBusCycles(Scheduler* sched, int n) {
    if (n <= 0 || !cpu) return;
    cpu->addBusWaitCycles(n);
    if (sched && !cpu->inRun()) sched->addStolenCycles(n);
}
