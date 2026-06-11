// =============================================================================
//  Blitter.cpp — Implémentation du Blitter ST (port de Hatari, données + bus).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Blitter.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "io/Mfp.hpp"

namespace {
    // LOP : l'opérateur logique référence-t-il HOP (source/halftone) et/ou la
    // destination ? (cf. Hatari Blitter_LOP_Table). HOP n'est lu que pour ces LOP,
    // dst seulement pour celles-là.
    constexpr bool LOP_USES_HOP[16] = {0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0};   // sauf 0,5,A,F
    constexpr bool LOP_USES_DST[16] = {0,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0};   // sauf 0,3,C,F

    // Partage de bus non-hog (blitter.c BLITTER_NONHOG_BUS_*) : 64 accès pour le
    // blitter, puis 64 accès (= 256 cycles) pour le CPU. Un accès bus = 4 cycles.
    constexpr int kNonHogBusBlitter = 64;
    constexpr int kNonHogCpuCycles  = 64 * 4;
    // Latence avant la prise de bus (phase PRE_START, blitter.c « t+0..t+4 ») et
    // arbitration de restitution blitter → CPU (Blitter_BusArbitration).
    constexpr int kPreStartCycles   = 4;
    constexpr int kArbOut           = 4;
}

void Blitter::reset() {
    for (auto& b : reg_) b = 0;
    buffer_ = 0; busWord_ = 0;
    midBlit_ = false; haveFxsr_ = false; nfsrInt_ = false;
    busCountError_ = false;
    clearPreStartWindow();
    if (sched_) sched_->cancel(Scheduler::BLITTER);
}

uint16_t Blitter::readWord(uint32_t addr) { ++sliceBus_; return bus_.read16(addr & 0xFFFFFE); }
void     Blitter::writeWord(uint32_t addr, uint16_t v) { ++sliceBus_; bus_.write16(addr & 0xFFFFFE, v); }

// Facture le temps de bus du blitter au CPU : 4 cycles par accès + les cycles
// d'arbitration (prise 4/8, restitution 4). Moira avance son horloge (le CPU
// « attend » que le blitter rende le bus, cf. addBusWaitCycles).
void Blitter::stallCpu(int busAccesses, int arbCycles) {
    const int cycles = busAccesses * 4 + arbCycles;
    if (busAccesses > 0 && bus_.cpu) bus_.cpu->addBusWaitCycles(cycles);
}

uint8_t Blitter::read8(uint32_t addr) { return reg_[addr & 0x3F]; }

void Blitter::write8(uint32_t addr, uint8_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off] = v;
    if (off == 0x3C) {
        // Écriture du registre contrôle ($FF8A3C) : BUSY (bit7) à 1 → démarre ou
        // REPREND ; à 0 pendant un transfert → PAUSE (le CPU peut arrêter le
        // blitter en non-hog, cf. blitter.c:88 — état conservé, reprise au
        // prochain BUSY=1).
        if (v & 0x80) start();
        else if (midBlit_) pauseTransfer();
    }
}

// Écritures mot/long ATOMIQUES : on pose TOUS les octets, PUIS on démarre si le
// registre contrôle ($FF8A3C, offset 0x3C) faisait partie de l'écriture et porte le
// bit BUSY. Garantit que le skew ($FF8A3D) est en place avant le départ (cf.
// Blitter.hpp). Sinon « move.w …,$FF8A3C » lancerait le blit avec l'ancien skew
// → plan 0 décalé par rapport aux plans 1-3 (franges de couleur).
void Blitter::write16(uint32_t addr, uint16_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off]              = uint8_t(v >> 8);
    reg_[(off + 1) & 0x3F] = uint8_t(v);
    if (off <= 0x3C && off + 1 >= 0x3C) {
        if (reg_[0x3C] & 0x80) start();
        else if (midBlit_) pauseTransfer();
    }
}

void Blitter::write32(uint32_t addr, uint32_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off]              = uint8_t(v >> 24);
    reg_[(off + 1) & 0x3F] = uint8_t(v >> 16);
    reg_[(off + 2) & 0x3F] = uint8_t(v >> 8);
    reg_[(off + 3) & 0x3F] = uint8_t(v);
    if (off <= 0x3C && off + 3 >= 0x3C) {
        if (reg_[0x3C] & 0x80) start();
        else if (midBlit_) pauseTransfer();
    }
}

// -----------------------------------------------------------------------------
//  Démarrage / reprise (BUSY écrit à 1). Mode HOG : tout le transfert d'un coup,
//  CPU arrêté pour la durée totale. Non-hog : 1re tranche de 64 accès tout de
//  suite (le blitter prend le bus en premier), puis alternance via l'ordonnanceur.
// -----------------------------------------------------------------------------
void Blitter::start() {
    // Transfert dégénéré (xCount==0 ou yCount==0) : « déjà complet » → efface
    // BUSY (bit7) ET HOG (bit6), comme Hatari Blitter_Control_WriteByte. Pas
    // d'IRQ GPIP3 ici (la ligne GPU_DONE ne bouge qu'à une vraie fin de blit).
    const uint16_t xc = uint16_t((reg_[0x36] << 8) | reg_[0x37]);
    const uint16_t yc = uint16_t((reg_[0x38] << 8) | reg_[0x39]);
    if (xc == 0 || yc == 0) { reg_[0x3C] &= ~0xC0; midBlit_ = false; return; }

    if (!midBlit_) {                       // vrai départ (pas une reprise de pause)
        xReset_   = xc;                    // latch de la recharge X (Hatari x_count_reset)
        haveFxsr_ = false;
        nfsrInt_  = false;
        midBlit_  = true;
    }

    // Cycles d'arbitration de bus (port Blitter_BusArbitration) : prendre le bus
    // coûte 4 cycles (8 sur Mega STE), le rendre au CPU 4 cycles.
    const int arbIn = (bus_.machine == MachineType::MegaSte) ? 8 : 4;

    if (reg_[0x3C] & 0x40) {               // mode HOG : bus gardé jusqu'à y_count=0
        sliceBus_ = 0;
        runSlice(-1);
        stallCpu(sliceBus_, arbIn + kArbOut);   // CPU stallé : arbitration + tout le blit
        return;
    }
    // Non-hog : sur le vrai matériel, le blitter ne prend pas le bus tout de
    // suite — phase PRE_START de 4 cycles (le CPU tourne encore) puis arbitration.
    // On date la 1re tranche à +4 et on ARME la fenêtre PRE_START : un accès bus
    // CPU dans [maintenant, +4) sera compté à tort par le blitter (bug « 63 accès »,
    // cf. notePreStartCpuAccess). Sans ordonnanceur : tranche immédiate.
    if (sched_) {
        armPreStartWindow(sched_->liveNow());
        sched_->schedule(Scheduler::BLITTER, sched_->liveNow() + kPreStartCycles);
    } else {
        onSlice();
    }
}

// Tranche non-hog (échéance Scheduler::BLITTER) : jusqu'à 64 accès bus — 63 si un
// accès CPU est tombé dans la fenêtre PRE_START (le blitter le compte à tort comme
// sien, cf. blitter.c:69-79). Ici on est à une frontière d'événement : avancer
// l'horloge CPU (stallCpu) retarde d'autant le prochain bloc d'exécution — le CPU
// « perd » arbitration + part du blitter, puis garde le bus 256 cycles (64 accès)
// avant la tranche suivante (précédée de sa propre fenêtre PRE_START).
void Blitter::onSlice() {
    if (!(reg_[0x3C] & 0x80)) { clearPreStartWindow(); return; }   // pause/reset
    const int arbIn  = (bus_.machine == MachineType::MegaSte) ? 8 : 4;
    const int budget = kNonHogBusBlitter - (busCountError_ ? 1 : 0);
    busCountError_ = false;
    clearPreStartWindow();
    sliceBus_ = 0;
    const bool done = runSlice(budget);
    stallCpu(sliceBus_, arbIn + kArbOut);
    if (!done && sched_) {
        // Prochaine prise de bus : part CPU (256 cyc) + latence PRE_START (4 cyc),
        // après le stall qu'on vient de facturer. Fenêtre 63/64 armée juste avant.
        const int64_t stall = int64_t(sliceBus_) * 4 + arbIn + kArbOut;
        const int64_t next  = sched_->now() + stall + kNonHogCpuCycles + kPreStartCycles;
        armPreStartWindow(next - kPreStartCycles);
        sched_->schedule(Scheduler::BLITTER, next);
    }
}

// Fenêtre PRE_START [t, t+4) : pendant ces 4 cycles le bit BUSY est posé mais le
// blitter n'a pas encore le bus — il compte pourtant déjà les accès. Un accès bus
// CPU dans la fenêtre (signalé par les accès mémoire de Moira via Bus) lui
// vole un accès : la tranche suivante n'en fera que 63.
void Blitter::armPreStartWindow(int64_t now) {
    bus_.blitterWinStart = now;
    bus_.blitterWinEnd   = now + kPreStartCycles;
}

void Blitter::clearPreStartWindow() {
    bus_.blitterWinStart = bus_.blitterWinEnd = -1;
}

void Blitter::notePreStartCpuAccess() {
    busCountError_ = true;                 // port Blitter_HOG_CPU_BusCountError = 1
}

// PAUSE du transfert (BUSY effacé par le CPU pendant un blit non-hog) : l'état
// reste en place (reprise au prochain BUSY=1), la tranche datée est annulée.
void Blitter::pauseTransfer() {
    clearPreStartWindow();
    busCountError_ = false;
    if (sched_) sched_->cancel(Scheduler::BLITTER);
}

// Fin de transfert (yCount==0) : le blitter abaisse la ligne GPU_DONE (GPIP3,
// active bas) et demande l'IRQ canal 3 (cf. Hatari Blitter_Start ligne 916).
// raise() respecte IERB → si le canal 3 n'est pas activé, aucun bit IPR n'est
// posé (inoffensif). N'est atteint que sur Mega ST/STE/Mega STE (le blitter
// n'est câblé au bus que sur ces modèles → auto-gaté).
void Blitter::finishTransfer() {
    midBlit_ = false;
    if (bus_.mfp) { bus_.mfp->setBlitterLine(true); bus_.mfp->raise(Mfp::SRC_GPU); }
}

// -----------------------------------------------------------------------------
//  Transfert par tranche — port de Hatari Blitter_Step/ProcessWord. L'état de
//  reprise vit dans les registres relisibles (adresses, X/Y count, ligne
//  halftone) + les membres haveFxsr_/nfsrInt_/buffer_/busWord_ : une tranche
//  reprend exactement où la précédente s'est arrêtée. `maxBusAccesses` < 0 =
//  transfert complet (HOG) ; la découpe se fait à la frontière de MOT.
// -----------------------------------------------------------------------------
bool Blitter::runSlice(int maxBusAccesses) {
    // Le blitter prend le bus via BGACK → le cache 16 Ko du Mega STE est invalidé
    // (Hatari MegaSTE_Cache_Flush) : ses écritures RAM ne passent pas par le cache.
    bus_.megaSteCacheFlushIfEnabled();
    auto rd16 = [&](uint32_t o) -> uint16_t { return uint16_t((reg_[o] << 8) | reg_[o + 1]); };
    auto wr16 = [&](uint32_t o, uint16_t w) { reg_[o] = uint8_t(w >> 8); reg_[o + 1] = uint8_t(w); };
    auto rd32 = [&](uint32_t o) -> uint32_t { return (uint32_t(rd16(o)) << 16) | rd16(o + 2); };
    auto s16  = [&](uint32_t o) -> int32_t  { return int16_t(rd16(o)); };

    const int      hop     = reg_[0x3A] & 3;
    const int      lop     = reg_[0x3B] & 0xF;
    const uint8_t  ctrl    = reg_[0x3C];
    const bool     smudge  = (ctrl & 0x20) != 0;
    const uint8_t  skewReg = reg_[0x3D];
    const int      skew    = skewReg & 0x0F;
    const bool     nfsr    = (skewReg & 0x40) != 0;   // NFSR = bit6 ($40)
    const bool     fxsr    = (skewReg & 0x80) != 0;   // FXSR = bit7 ($80)

    const int32_t  srcXinc = s16(0x20), srcYinc = s16(0x22);
    uint32_t       srcAddr = rd32(0x24) & 0xFFFFFE;
    const uint16_t em1 = rd16(0x28), em2 = rd16(0x2A), em3 = rd16(0x2C);
    const int32_t  dstXinc = s16(0x2E), dstYinc = s16(0x30);
    uint32_t       dstAddr = rd32(0x32) & 0xFFFFFE;
    const uint16_t xReset  = xReset_;
    uint16_t       xCount  = rd16(0x36);              // compteur VIVANT (relisible)
    uint16_t       yCount  = rd16(0x38);
    int            htLine  = ctrl & 0x0F;             // ligne halftone courante

    // Registre à décalage source (32 bits) + dernier mot ayant transité sur le BUS.
    // Hatari : BlitterState.bus_word est mis à jour à CHAQUE accès bus du blitter —
    // lecture source, lecture destination ET écriture destination (cf. blitter.c
    // Blitter_ReadWord l.440 / Blitter_WriteWord l.446). Le cas particulier NFSR
    // (Blitter_SourceFetch(true)) réinjecte ce bus_word dans le registre à décalage.
    // Au dernier mot d'une ligne NFSR la lecture source normale est SAUTÉE, donc le
    // dernier accès bus est la lecture (ou l'écriture) de la destination : c'est bien
    // CE mot qui est réinjecté, pas la dernière source. (Bug corrigé : on suivait
    // « lastSrc » = dernière source, d'où des pixels parasites sur les icônes GEM.)
    uint32_t buffer  = buffer_;     // persistance Hatari (pas de remise à 0 par blit)
    uint16_t busWord = busWord_;
    bool     haveFxsr = haveFxsr_;
    bool     nfsrInt  = nfsrInt_;

    auto srcShift = [&]() { if (srcXinc < 0) buffer >>= 16; else buffer <<= 16; };
    auto srcFetch = [&](bool nfsrOn) {
        const uint32_t w = nfsrOn ? busWord : (busWord = readWord(srcAddr));
        if (srcXinc < 0) buffer |= w << 16; else buffer |= w;
    };
    auto srcRead = [&]() -> uint16_t { return uint16_t(buffer >> skew); };
    auto halftoneWord = [&]() -> uint16_t {
        return smudge ? rd16(0x00 + (srcRead() & 15) * 2) : rd16(0x00 + htLine * 2);
    };
    auto computeHOP = [&]() -> uint16_t {
        switch (hop) {
            case 0: return 0xFFFF;
            case 1: return halftoneWord();
            case 2: return srcRead();
            default: return uint16_t(srcRead() & halftoneWord());
        }
    };

    while (yCount > 0) {
        // Budget de bus de la tranche épuisé → on rend la main (frontière de mot).
        if (maxBusAccesses >= 0 && sliceBus_ >= maxBusAccesses) break;

        const bool firstWord = (xCount == xReset);
        const uint16_t endMask = (firstWord || xReset == 1) ? em1
                               : (xCount == 1)              ? em3 : em2;
        if (firstWord) nfsrInt = false;

        const bool needSrc = LOP_USES_HOP[lop] && ((hop & 2) || (hop == 1 && smudge));
        const bool needDst = LOP_USES_DST[lop] || (endMask != 0xFFFF);

        // --- ProcessWord ---
        bool fetchSrc = false;
        if (firstWord && fxsr && needSrc && !haveFxsr) {       // FXSR : lecture source extra
            srcShift(); srcFetch(false); srcAddr += srcXinc; haveFxsr = true;
        }
        if (needSrc && !nfsrInt) {                              // lecture source normale
            srcShift(); srcFetch(false); fetchSrc = true;
        }
        // Lecture destination : met aussi à jour busWord (Blitter_ReadWord).
        const uint16_t dstWord = needDst ? (busWord = readWord(dstAddr)) : busWord;
        // Cas particulier NFSR (1/2) : AVANT le LOP. Réinjecte busWord (= la dst qu'on
        // vient de lire, la source normale ayant été sautée) dans le registre source.
        if (nfsr && xCount == 1) { srcShift(); srcFetch(true); }

        const uint16_t hopv = computeHOP();
        uint16_t lopv;
        switch (lop) {
            case 0x0: lopv = 0;                          break;
            case 0x1: lopv = uint16_t( hopv &  dstWord); break;
            case 0x2: lopv = uint16_t( hopv & ~dstWord); break;
            case 0x3: lopv = hopv;                       break;
            case 0x4: lopv = uint16_t(~hopv &  dstWord); break;
            case 0x5: lopv = dstWord;                    break;
            case 0x6: lopv = uint16_t( hopv ^  dstWord); break;
            case 0x7: lopv = uint16_t( hopv |  dstWord); break;
            case 0x8: lopv = uint16_t(~hopv & ~dstWord); break;
            case 0x9: lopv = uint16_t(~(hopv ^ dstWord));break;
            case 0xA: lopv = uint16_t(~dstWord);         break;
            case 0xB: lopv = uint16_t( hopv | ~dstWord); break;
            case 0xC: lopv = uint16_t(~hopv);            break;
            case 0xD: lopv = uint16_t(~hopv |  dstWord); break;
            case 0xE: lopv = uint16_t(~hopv | ~dstWord); break;
            default:  lopv = 0xFFFF;                     break;
        }
        const uint16_t out = (endMask != 0xFFFF)
            ? uint16_t((lopv & endMask) | (dstWord & ~endMask)) : lopv;
        writeWord(dstAddr, out);
        busWord = out;                         // l'écriture dst met aussi à jour bus_word

        // Cas particulier NFSR (2/2) : APRÈS l'écriture. Hatari répète le shift+fetch
        // (blitter.c l.738-743) ; réinjecte le mot qu'on vient d'écrire. Sans FXSR le
        // registre source est conservé d'une ligne à l'autre, donc cette 2ᵉ passe doit
        // être fidèlement reproduite pour l'alignement du registre à décalage.
        if (nfsr && xCount == 1) { srcShift(); srcFetch(true); }

        // --- mise à jour compteurs/adresses ---
        if (xCount == 2 && nfsr) nfsrInt = true;
        if (fetchSrc) srcAddr += (xCount == 1 || nfsrInt) ? srcYinc : srcXinc;
        if (xCount == 1) {                                 // fin de ligne
            haveFxsr = false;
            --yCount;
            xCount = xReset;
            dstAddr += dstYinc;
            htLine = (dstYinc >= 0) ? (htLine + 1) & 15 : (htLine - 1) & 15;
        } else {
            --xCount;
            dstAddr += dstXinc;
        }
    }

    // État de reprise : registres relisibles (progression visible du CPU) + membres.
    buffer_ = buffer; busWord_ = busWord;   // persistance Hatari du registre à décalage
    haveFxsr_ = haveFxsr; nfsrInt_ = nfsrInt;
    wr16(0x24, uint16_t(srcAddr >> 16)); wr16(0x26, uint16_t(srcAddr));
    wr16(0x32, uint16_t(dstAddr >> 16)); wr16(0x34, uint16_t(dstAddr));
    wr16(0x36, xCount);
    wr16(0x38, yCount);

    if (yCount > 0) {                       // tranche finie, transfert pas terminé
        reg_[0x3C] = uint8_t(0x80 | (ctrl & 0x70) | (htLine & 0x0F));   // BUSY maintenu
        return false;
    }
    reg_[0x3C] = uint8_t((ctrl & 0x30) | (htLine & 0x0F));   // BUSY(7)+HOG(6) effacés
    finishTransfer();
    return true;
}
