// =============================================================================
//  Blitter.cpp — Implémentation du Blitter ST (port fonctionnel de Hatari).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Blitter.hpp"
#include "core/Bus.hpp"
#include "io/Mfp.hpp"

namespace {
    // LOP : l'opérateur logique référence-t-il HOP (source/halftone) et/ou la
    // destination ? (cf. Hatari Blitter_LOP_Table). HOP n'est lu que pour ces LOP,
    // dst seulement pour celles-là.
    constexpr bool LOP_USES_HOP[16] = {0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0};   // sauf 0,5,A,F
    constexpr bool LOP_USES_DST[16] = {0,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0};   // sauf 0,3,C,F
}

void Blitter::reset() { for (auto& b : reg_) b = 0; buffer_ = 0; busWord_ = 0; }

uint16_t Blitter::readWord(uint32_t addr) { return bus_.read16(addr & 0xFFFFFE); }
void     Blitter::writeWord(uint32_t addr, uint16_t v) { bus_.write16(addr & 0xFFFFFE, v); }

uint8_t Blitter::read8(uint32_t addr) { return reg_[addr & 0x3F]; }

void Blitter::write8(uint32_t addr, uint8_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off] = v;
    // Écriture du registre contrôle ($FF8A3C) avec le bit BUSY (bit7) → démarre.
    if (off == 0x3C && (v & 0x80)) run();
}

// Écritures mot/long ATOMIQUES : on pose TOUS les octets, PUIS on démarre si le
// registre contrôle ($FF8A3C, offset 0x3C) faisait partie de l'écriture et porte le
// bit BUSY. Garantit que le skew ($FF8A3D) est en place avant run() (cf. write16/32
// déclarés dans Blitter.hpp). Sinon « move.w …,$FF8A3C » lancerait le blit avec
// l'ancien skew → plan 0 décalé par rapport aux plans 1-3 (franges de couleur).
void Blitter::write16(uint32_t addr, uint16_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off]            = uint8_t(v >> 8);
    reg_[(off + 1) & 0x3F] = uint8_t(v);
    if ((off <= 0x3C && off + 1 >= 0x3C) && (reg_[0x3C] & 0x80)) run();
}

void Blitter::write32(uint32_t addr, uint32_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off]              = uint8_t(v >> 24);
    reg_[(off + 1) & 0x3F] = uint8_t(v >> 16);
    reg_[(off + 2) & 0x3F] = uint8_t(v >> 8);
    reg_[(off + 3) & 0x3F] = uint8_t(v);
    if ((off <= 0x3C && off + 3 >= 0x3C) && (reg_[0x3C] & 0x80)) run();
}

// -----------------------------------------------------------------------------
//  Transfert complet (mode HOG). Port de Hatari Blitter_Step/ProcessWord, mais
//  synchrone (sans comptage d'accès bus ni partage CPU). Le résultat de données
//  est identique au vrai matériel.
// -----------------------------------------------------------------------------
void Blitter::run() {
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
    const uint16_t xReset  = rd16(0x36);
    uint16_t       yCount  = rd16(0x38);
    int            htLine  = ctrl & 0x0F;             // ligne halftone courante

    // Démarrage dégénéré (xReset==0 ou yCount==0) : transfert déjà « complet » →
    // efface BUSY (bit7) ET HOG (bit6), comme Hatari Blitter_Control_WriteByte
    // (BlitterRegs.ctrl &= ~(0x80|0x40)). Pas d'IRQ GPIP3 ici (Hatari ne baisse pas
    // la ligne GPU_DONE sur ce chemin, seulement à la vraie fin de transfert).
    if (xReset == 0 || yCount == 0) { reg_[0x3C] &= ~0xC0; return; }   // rien à faire

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
    uint16_t xCount = xReset;
    bool     haveFxsr = false;
    bool     nfsrInt  = false;

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

    buffer_ = buffer; busWord_ = busWord;   // persistance Hatari du registre à décalage

    // Recopie l'état final dans les registres relisibles, efface BUSY/HOG.
    wr16(0x24, uint16_t(srcAddr >> 16)); wr16(0x26, uint16_t(srcAddr));
    wr16(0x32, uint16_t(dstAddr >> 16)); wr16(0x34, uint16_t(dstAddr));
    wr16(0x36, xCount);
    wr16(0x38, yCount);
    reg_[0x3C] = uint8_t((ctrl & 0x30) | (htLine & 0x0F));   // BUSY(7)+HOG(6) effacés

    // Fin de transfert (yCount==0) : le blitter abaisse la ligne GPU_DONE (GPIP3,
    // active bas) et demande l'IRQ canal 3 (cf. Hatari Blitter_Start ligne 916).
    // raise() respecte IERB → si le canal 3 n'est pas activé, aucun bit IPR n'est
    // posé (inoffensif). updateIpl() après l'écriture MMIO de $FF8A3C présentera
    // l'IRQ pendante. N'est atteint que sur Mega ST/STE/Mega STE (le blitter n'est
    // câblé au bus que sur ces modèles → auto-gaté).
    if (bus_.mfp) { bus_.mfp->setBlitterLine(true); bus_.mfp->raise(Mfp::SRC_GPU); }
}
