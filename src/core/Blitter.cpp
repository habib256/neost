// =============================================================================
//  Blitter.cpp — Implémentation du Blitter ST (port fonctionnel de Hatari).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Blitter.hpp"
#include "core/Bus.hpp"

namespace {
    // LOP : l'opérateur logique référence-t-il HOP (source/halftone) et/ou la
    // destination ? (cf. Hatari Blitter_LOP_Table). HOP n'est lu que pour ces LOP,
    // dst seulement pour celles-là.
    constexpr bool LOP_USES_HOP[16] = {0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0};   // sauf 0,5,A,F
    constexpr bool LOP_USES_DST[16] = {0,1,1,0,1,1,1,1,1,1,1,1,0,1,1,0};   // sauf 0,3,C,F
}

void Blitter::reset() { for (auto& b : reg_) b = 0; }

uint16_t Blitter::readWord(uint32_t addr) { return bus_.read16(addr & 0xFFFFFE); }
void     Blitter::writeWord(uint32_t addr, uint16_t v) { bus_.write16(addr & 0xFFFFFE, v); }

uint8_t Blitter::read8(uint32_t addr) { return reg_[addr & 0x3F]; }

void Blitter::write8(uint32_t addr, uint8_t v) {
    const uint32_t off = addr & 0x3F;
    reg_[off] = v;
    // Écriture du registre contrôle ($FF8A3C) avec le bit BUSY (bit7) → démarre.
    if (off == 0x3C && (v & 0x80)) run();
}

// -----------------------------------------------------------------------------
//  Transfert complet (mode HOG). Port de Hatari Blitter_Step/ProcessWord, mais
//  synchrone (sans comptage d'accès bus ni partage CPU). Le résultat de données
//  est identique au vrai matériel.
// -----------------------------------------------------------------------------
void Blitter::run() {
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

    if (xReset == 0 || yCount == 0) { reg_[0x3C] &= ~0x80; return; }   // rien à faire

    // Registre à décalage source (32 bits) + dernier mot lu (pour NFSR).
    uint32_t buffer = 0;
    uint16_t lastSrc = 0;
    uint16_t xCount = xReset;
    bool     haveFxsr = false;
    bool     nfsrInt  = false;

    auto srcShift = [&]() { if (srcXinc < 0) buffer >>= 16; else buffer <<= 16; };
    auto srcFetch = [&](bool nfsrOn) {
        const uint32_t w = nfsrOn ? lastSrc : (lastSrc = readWord(srcAddr));
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
        const uint16_t dstWord = needDst ? readWord(dstAddr) : 0;
        if (nfsr && xCount == 1) { srcShift(); srcFetch(true); }   // cas particulier NFSR

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

    // Recopie l'état final dans les registres relisibles, efface BUSY/HOG.
    wr16(0x24, uint16_t(srcAddr >> 16)); wr16(0x26, uint16_t(srcAddr));
    wr16(0x32, uint16_t(dstAddr >> 16)); wr16(0x34, uint16_t(dstAddr));
    wr16(0x36, xCount);
    wr16(0x38, yCount);
    reg_[0x3C] = uint8_t((ctrl & 0x30) | (htLine & 0x0F));   // BUSY(7)+HOG(6) effacés
}
