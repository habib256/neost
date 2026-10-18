// =============================================================================
//  SoftFloatX80.hpp — Arithmétique flottante ÉTENDUE 80 bits (mantisse 64 bits
//  RÉELLE) pour le MC68881. Portage propre de l'algorithme SoftFloat-2a de
//  John R. Hauser (licence permissive « SoftFloat-2a », travaux dérivés
//  autorisés avec mention), tel qu'étendu pour le 680x0 par WinUAE/Previous et
//  lu dans `extern/hatari/src/cpu/softfloat` (source de vérité, cf. CLAUDE.md).
//
//  Pourquoi : le 68881 calcule sur 64 bits de mantisse (format étendu), là où un
//  `double` hôte n'en a que 53. Ce module fournit add/sub/mul/div/sqrt/rem au
//  cycle bit près du silicium, avec les 4 modes d'arrondi du FPCR, le contrôle de
//  précision (étendu/double/simple) et les drapeaux d'exception IEEE. Les leaf
//  multi-mots (mul64→128, div128→64…) utilisent `__uint128_t` (dispo clang/gcc
//  sur les deux cibles) — structurellement plus simple et plus sûr que le calcul
//  64 bits manuel d'origine, mais sémantiquement identique.
//
//  Les opérandes arrivent déjà EXACTS dans le format étendu (cf. Fpu::decodeFmt :
//  X bit-exact ; S/D/L/W/B = élargissements exacts vers `double` puis étendu) →
//  toute l'arithmétique se fait donc bien sur 64 bits de mantisse.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST (portage SoftFloat-2a, Hauser et al.).
// =============================================================================
#pragma once
#include <cstdint>

namespace sf {

// Valeur au format étendu 80 bits : high = signe(bit15)+exposant biaisé $3FFF
// (bits14-0), low = mantisse 64 bits à bit entier EXPLICITE (bit63). Disposition
// identique à Fpu::Ext → conversion triviale.
struct f80 { uint16_t high; uint64_t low; };

// Modes d'arrondi (valeurs SoftFloat ; le FPCR utilise un autre ordre, mappé côté Fpu).
enum { round_nearest_even = 0, round_down = 1, round_up = 2, round_to_zero = 3 };
// Drapeaux d'exception IEEE (valeurs SoftFloat). flag_signaling (extension 680x0) =
// entrée SNaN → distinct de flag_invalid pour mapper sur FPSR.SNAN (et non OPERR).
enum { flag_invalid = 0x01, flag_signaling = 0x02, flag_divzero = 0x04, flag_overflow = 0x08,
       flag_underflow = 0x10, flag_inexact = 0x20, flag_decimal = 0x40 };
// Précision d'arrondi (bits 7-6 du FPCR → 80 étendu / 64 double / 32 simple).
enum { prec_extended = 80, prec_double = 64, prec_single = 32 };

struct Status {
    int     roundingMode      = round_nearest_even;
    int     roundingPrecision = prec_extended;
    uint8_t exceptionFlags    = 0;
};
inline void raise(Status& s, uint8_t f) { s.exceptionFlags |= f; }

// --- Champs --------------------------------------------------------------------
inline uint64_t fracOf(f80 a) { return a.low; }
inline int32_t  expOf (f80 a) { return a.high & 0x7FFF; }
inline int      signOf(f80 a) { return a.high >> 15; }
inline f80      pack(int sign, int32_t exp, uint64_t sig) {
    return f80{ uint16_t((uint16_t(sign & 1) << 15) | (uint16_t(exp) & 0x7FFF)), sig };
}
inline bool isNaN(f80 a) { return (a.high & 0x7FFF) == 0x7FFF && (a.low << 1) != 0; }
inline bool isSNaN(f80 a){ return isNaN(a) && !(a.low & 0x4000000000000000ull); }
inline f80  defaultNaN() { return f80{ 0x7FFF, 0xFFFFFFFFFFFFFFFFull }; }     // QNaN 68881
// Encodage INVALIDE (« unnormal ») : exposant ni 0 ni $7FFF alors que le bit entier
// explicite du significande est à 0. Le 68881 refuse ces motifs — Hatari les garde en
// tête de CHAQUE opération (floatx80_invalid_encoding, softfloat.c:1862, 1912, 2475,
// 2500, 2527, 2645, 2806, 3097) et rend un NaN par défaut avec OPERR.
// ⚠ Sans cette garde, sqrt_ GELAIT l'émulateur : estimateSqrt32 recevait une mantisse
// dont le bit 63 est nul, l'estimation initiale partait à côté, et la boucle de
// correction décrémentait le significande UN PAR UN — jusqu'à 1,7 MILLIARD d'itérations
// mesurées, avec un pire cas théorique de 2^63. Atteignable depuis le code invité :
// FMOVE.X <ea>,FP0 accepte 12 octets arbitraires, puis FSQRT.
inline bool invalidEncoding(f80 a) {
    const int32_t e = a.high & 0x7FFF;
    return e != 0 && e != 0x7FFF && !(a.low & 0x8000000000000000ull);
}
constexpr uint64_t INF_LOW = 0x8000000000000000ull;   // MSB du SIGNIFICANDE
// Significande canonique d'un infini GÉNÉRÉ par l'arithmétique 68881 : bit entier
// explicite j = 0, fraction = 0 (≙ floatx80_default_infinity_low, softfloat.h:348).
// À ne pas confondre avec INF_LOW ci-dessus : Fpu::decodeFmt rend déjà cette forme
// pour un ±∞ chargé depuis la mémoire ; empaqueter INF_LOW ici produirait DEUX
// motifs binaires différents pour +∞ selon son origine, et FMOVE.X ≠ oracle.
constexpr uint64_t INF_SIG = 0x0000000000000000ull;

// Propagation des NaN (cf. Hatari propagateFloatx80NaN, chemin SOFTFLOAT_68K) :
// renvoie l'opérande NaN RÉEL (signe + payload) quiété (bit 62 = 1), au lieu d'un
// default-NaN ; une entrée SNaN lève flag_signaling (→ FPSR.SNAN, pas OPERR).
inline f80 propagateNaN(Status& s, f80 a, f80 b) {
    if (isSNaN(a) || isSNaN(b)) raise(s, flag_signaling);
    f80 r = isNaN(a) ? a : b;
    r.low |= 0x4000000000000000ull;        // SNaN → QNaN (quiet)
    return r;
}
inline f80 propagateNaN1(Status& s, f80 a) {
    if (isSNaN(a)) raise(s, flag_signaling);
    f80 r = a;
    r.low |= 0x4000000000000000ull;
    return r;
}

// --- Leaf multi-mots (via __uint128_t) -----------------------------------------
inline int clz64(uint64_t a) { return a ? __builtin_clzll(a) : 64; }

inline void shift64RightJamming(uint64_t a, int count, uint64_t& z) {
    if (count == 0)        z = a;
    else if (count < 64)   z = (a >> count) | ((a << ((-count) & 63)) != 0);
    else                   z = (a != 0);
}
inline void shift64ExtraRightJamming(uint64_t a0, uint64_t a1, int count,
                                     uint64_t& z0, uint64_t& z1) {
    int neg = (-count) & 63;
    if (count == 0)      { z1 = a1; z0 = a0; }
    else if (count < 64) { z1 = (a0 << neg) | (a1 != 0); z0 = a0 >> count; }
    else { z1 = (count == 64) ? (a0 | (a1 != 0)) : (((a0 | a1) != 0)); z0 = 0; }
}
inline void shift128Right(uint64_t a0, uint64_t a1, int count, uint64_t& z0, uint64_t& z1) {
    int neg = (-count) & 63;
    if (count == 0)      { z1 = a1; z0 = a0; }
    else if (count < 64) { z1 = (a0 << neg) | (a1 >> count); z0 = a0 >> count; }
    else { z1 = (count < 128) ? (a0 >> (count & 63)) : 0; z0 = 0; }
}
inline void shift128RightJamming(uint64_t a0, uint64_t a1, int count, uint64_t& z0, uint64_t& z1) {
    int neg = (-count) & 63;
    if (count == 0)      { z1 = a1; z0 = a0; }
    else if (count < 64) { z1 = (a0 << neg) | (a1 >> count) | ((a1 << neg) != 0); z0 = a0 >> count; }
    else {
        if (count == 64)       z1 = a0 | (a1 != 0);
        else if (count < 128)  z1 = (a0 >> (count & 63)) | ((((a0 << neg) | a1)) != 0);
        else                   z1 = ((a0 | a1) != 0);
        z0 = 0;
    }
}
inline void shortShift128Left(uint64_t a0, uint64_t a1, int count, uint64_t& z0, uint64_t& z1) {
    z1 = a1 << count;
    z0 = (count == 0) ? a0 : (a0 << count) | (a1 >> ((-count) & 63));
}
inline void add128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1, uint64_t& z0, uint64_t& z1) {
    __uint128_t z = ((__uint128_t(a0) << 64) | a1) + ((__uint128_t(b0) << 64) | b1);
    z0 = uint64_t(z >> 64); z1 = uint64_t(z);
}
inline void sub128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1, uint64_t& z0, uint64_t& z1) {
    __uint128_t z = ((__uint128_t(a0) << 64) | a1) - ((__uint128_t(b0) << 64) | b1);
    z0 = uint64_t(z >> 64); z1 = uint64_t(z);
}
inline void mul64To128(uint64_t a, uint64_t b, uint64_t& z0, uint64_t& z1) {
    __uint128_t z = __uint128_t(a) * b;
    z0 = uint64_t(z >> 64); z1 = uint64_t(z);
}
// Division 128/64 exacte (cap à 2^64-1 si débordement) : meilleure que l'estimation
// d'origine (la boucle de correction des appelants devient un no-op).
inline uint64_t estimateDiv128To64(uint64_t a0, uint64_t a1, uint64_t b) {
    if (b <= a0) return 0xFFFFFFFFFFFFFFFFull;
    return uint64_t((((__uint128_t(a0) << 64) | a1)) / b);
}
inline bool le128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {
    return (a0 < b0) || ((a0 == b0) && (a1 <= b1));
}
inline bool lt128(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {
    return (a0 < b0) || ((a0 == b0) && (a1 < b1));
}
inline void add192(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t b0,uint64_t b1,uint64_t b2,
                   uint64_t& z0,uint64_t& z1,uint64_t& z2) {
    // La retenue sortante du 128 bits BAS doit remonter dans z0. L'ancien
    // « add128(a0,c1,b0,0,z0,c0) » la déposait dans c0 — puis la JETAIT : z0 valait
    // a0+b0 tout court. Seul appelant : sqrt_, où b0 == 0, si bien que le reste ne
    // bougeait plus et que la boucle de raffinement tournait à l'infini — FSQRT gelait
    // l'émulateur sur toute racine exactement représentable (1.0, 9.0, 100.0, 0.25…).
    // Hatari propage en trois temps (softfloat-macros.h:433-440).
    uint64_t c1=0; add128(a1,a2,b1,b2,z1,z2); if(lt128(z1,z2,b1,b2)) c1=1;
    z0 = a0 + b0 + c1;
}
inline void sub192(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t b0,uint64_t b1,uint64_t b2,
                   uint64_t& z0,uint64_t& z1,uint64_t& z2) {
    uint64_t borrow1 = (a1 < b1) || ((a1==b1) && (a2<b2));
    sub128(a1,a2,b1,b2,z1,z2);
    z0 = a0 - b0 - borrow1;
}
inline uint32_t estimateSqrt32(int aExp, uint32_t a) {
    static const uint16_t odd[]  = { 0x0004,0x0022,0x005D,0x00B1,0x011D,0x019F,0x0236,0x02E0,
                                     0x039C,0x0468,0x0545,0x0631,0x072B,0x0832,0x0946,0x0A67 };
    static const uint16_t even[] = { 0x0A2D,0x08AF,0x075A,0x0629,0x051A,0x0429,0x0356,0x029E,
                                     0x0200,0x0179,0x0109,0x00AF,0x0068,0x0034,0x0012,0x0002 };
    int index = (a >> 27) & 15; uint32_t z;
    if (aExp & 1) {
        z = 0x4000 + (a >> 17) - odd[index];
        z = ((a / z) << 14) + (z << 15); a >>= 1;
    } else {
        z = 0x8000 + (a >> 17) - even[index];
        z = a / z + z; z = (0x20000 <= z) ? 0xFFFF8000u : (z << 15);
        if (z <= a) return uint32_t(int32_t(a) >> 1);
    }
    return uint32_t((uint64_t(a) << 31) / z) + (z >> 1);
}

inline void normalizeSubnormal(uint64_t aSig, int32_t& zExp, uint64_t& zSig) {
    // « -sc » et NON « 1 - sc » : le format étendu du 68881 a un bit entier EXPLICITE,
    // là où le x87/IEEE l'a implicite. softfloat.c porte les deux formes derrière un
    // #ifdef SOFTFLOAT_68K (softfloat.c:1061-1065) — défini en tête de softfloat.h, donc
    // TOUJOURS actif chez Hatari — et c'est la branche x87 qui avait été portée ici.
    // Effet : TOUT opérande dénormal ressortait exactement ×2 (et FGETEXP décalé de 1),
    // sur FADD/FSUB/FMUL/FDIV/FSQRT/FREM/FMOD/FGETEXP/FGETMAN/FSCALE.
    int sc = clz64(aSig); zSig = aSig << sc; zExp = -sc;
}

// Déclaration anticipée : roundSigAndPack délègue à roundAndPack pour prec == 80.
inline f80 roundAndPack(int prec, int sign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, Status& st);

// --- Cœur : arrondi + emballage (port de roundAndPackFloatx80) ------------------
inline f80 roundAndPack(int prec, int sign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, Status& st) {
    int rm = st.roundingMode;
    bool rne = (rm == round_nearest_even);
    uint64_t roundIncr, roundMask, roundBits;
    int32_t expOffset;
    if (prec == 80) goto precision80;
    if (prec == 64)      { roundIncr = 0x0000000000000400ull; roundMask = 0x00000000000007FFull; expOffset = 0x3C00; }
    else if (prec == 32) { roundIncr = 0x0000008000000000ull; roundMask = 0x000000FFFFFFFFFFull; expOffset = 0x3F80; }
    else goto precision80;
    zSig0 |= (zSig1 != 0);
    if (!rne) {
        if (rm == round_to_zero) roundIncr = 0;
        else { roundIncr = roundMask;
               if (sign) { if (rm == round_up)   roundIncr = 0; }
               else      { if (rm == round_down) roundIncr = 0; } }
    }
    roundBits = zSig0 & roundMask;
    if (((0x7FFE - expOffset) < zExp) ||
        ((zExp == (0x7FFE - expOffset)) && (zSig0 + roundIncr < zSig0))) {
        raise(st, flag_overflow);
        if (zSig0 & roundMask) raise(st, flag_inexact);
        if (rm == round_to_zero || (sign && rm == round_up) || (!sign && rm == round_down))
            return pack(sign, 0x7FFE - expOffset, ~roundMask);
        return pack(sign, 0x7FFF, INF_SIG);
    }
    if (zExp < (expOffset + 1)) {
        raise(st, flag_underflow);
        shift64RightJamming(zSig0, -(zExp - (expOffset + 1)), zSig0);
        zExp = expOffset + 1; roundBits = zSig0 & roundMask;
        if (roundBits) raise(st, flag_inexact);
        zSig0 += roundIncr; roundIncr = roundMask + 1;
        if (rne && ((roundBits << 1) == roundIncr)) roundMask |= roundIncr;
        zSig0 &= ~roundMask;
        return pack(sign, zExp, zSig0);
    }
    if (roundBits) raise(st, flag_inexact);
    zSig0 += roundIncr;
    if (zSig0 < roundIncr) { ++zExp; zSig0 = INF_LOW; }
    roundIncr = roundMask + 1;
    if (rne && ((roundBits << 1) == roundIncr)) roundMask |= roundIncr;
    zSig0 &= ~roundMask;
    if (zSig0 == 0) zExp = 0;
    return pack(sign, zExp, zSig0);
precision80:
    bool increment = (int64_t(zSig1) < 0);
    if (!rne) {
        if (rm == round_to_zero) increment = 0;
        else if (sign) increment = (rm == round_down) && zSig1;
        else           increment = (rm == round_up)   && zSig1;
    }
    if (0x7FFE <= uint32_t(zExp)) {
        if ((0x7FFE < zExp) || ((zExp == 0x7FFE) && (zSig0 == 0xFFFFFFFFFFFFFFFFull) && increment)) {
            raise(st, flag_overflow);
            if (zSig1) raise(st, flag_inexact);
            if (rm == round_to_zero || (sign && rm == round_up) || (!sign && rm == round_down))
                return pack(sign, 0x7FFE, ~uint64_t(0));
            return pack(sign, 0x7FFF, INF_SIG);
        }
        if (zExp < 0) {
            raise(st, flag_underflow);
            shift64ExtraRightJamming(zSig0, zSig1, -zExp, zSig0, zSig1);
            zExp = 0;
            if (zSig1) raise(st, flag_inexact);
            if (rne) increment = (int64_t(zSig1) < 0);
            else if (sign) increment = (rm == round_down) && zSig1;
            else           increment = (rm == round_up)   && zSig1;
            if (increment) {
                ++zSig0;
                zSig0 &= ~uint64_t((uint64_t(zSig1 << 1) == 0) & rne);
            }
            return pack(sign, zExp, zSig0);
        }
    }
    if (zSig1) raise(st, flag_inexact);
    if (increment) {
        ++zSig0;
        if (zSig0 == 0) { ++zExp; zSig0 = INF_LOW; }
        else zSig0 &= ~uint64_t((uint64_t(zSig1 << 1) == 0) & rne);
    } else if (zSig0 == 0) zExp = 0;
    return pack(sign, zExp, zSig0);
}
inline f80 normalizeRoundAndPack(int prec, int sign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, Status& st) {
    if (zSig0 == 0) { zSig0 = zSig1; zSig1 = 0; zExp -= 64; }
    int sc = clz64(zSig0);
    shortShift128Left(zSig0, zSig1, sc, zSig0, zSig1);
    zExp -= sc;
    return roundAndPack(prec, sign, zExp, zSig0, zSig1, st);
}

// --- Addition / soustraction des magnitudes ------------------------------------
inline f80 addSigs(f80 a, f80 b, int zSign, Status& st) {
    int32_t aExp = expOf(a), bExp = expOf(b), zExp; uint64_t aSig = fracOf(a), bSig = fracOf(b), zSig0, zSig1;
    if (aExp == 0) { if (aSig == 0) aExp = -64; else normalizeSubnormal(aSig, aExp, aSig); }
    if (bExp == 0) { if (bSig == 0) bExp = -64; else normalizeSubnormal(bSig, bExp, bSig); }
    int32_t expDiff = aExp - bExp;
    if (0 < expDiff) {
        if (aExp == 0x7FFF) { if (aSig << 1) return propagateNaN(st, a, b); return a; }
        shift64ExtraRightJamming(bSig, 0, expDiff, bSig, zSig1); zExp = aExp;
    } else if (expDiff < 0) {
        if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); return pack(zSign, bExp, bSig); }
        shift64ExtraRightJamming(aSig, 0, -expDiff, aSig, zSig1); zExp = bExp;
    } else {
        if (aExp == 0x7FFF) { if ((aSig | bSig) << 1) return propagateNaN(st, a, b); return a; }
        zSig1 = 0; zSig0 = aSig + bSig; zExp = aExp;
        if (aSig == 0 && bSig == 0) return pack(zSign, 0, 0);
        if (aSig == 0 || bSig == 0) return roundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
        goto shiftRight1;
    }
    zSig0 = aSig + bSig;
    if (int64_t(zSig0) < 0) return roundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
shiftRight1:
    shift64ExtraRightJamming(zSig0, zSig1, 1, zSig0, zSig1);
    zSig0 |= INF_LOW; ++zExp;
    return roundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
}
inline f80 subSigs(f80 a, f80 b, int zSign, Status& st) {
    int32_t aExp = expOf(a), bExp = expOf(b), zExp; uint64_t aSig = fracOf(a), bSig = fracOf(b), zSig0, zSig1;
    int32_t expDiff = aExp - bExp;
    if (0 < expDiff) goto aExpBigger;
    if (expDiff < 0) goto bExpBigger;
    if (aExp == 0x7FFF) { if ((aSig | bSig) << 1) return propagateNaN(st, a, b); raise(st, flag_invalid); return defaultNaN(); }
    zSig1 = 0;
    if (bSig < aSig) goto aBigger;
    if (aSig < bSig) goto bBigger;
    return pack(st.roundingMode == round_down, 0, 0);
bExpBigger:
    if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); return pack(zSign ^ 1, bExp, bSig); }
    shift128RightJamming(aSig, 0, -expDiff, aSig, zSig1);
bBigger:
    sub128(bSig, 0, aSig, zSig1, zSig0, zSig1); zExp = bExp; zSign ^= 1;
    return normalizeRoundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
aExpBigger:
    if (aExp == 0x7FFF) { if (aSig << 1) return propagateNaN(st, a, b); return a; }
    shift128RightJamming(bSig, 0, expDiff, bSig, zSig1);
aBigger:
    sub128(aSig, 0, bSig, zSig1, zSig0, zSig1); zExp = aExp;
    return normalizeRoundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
}
inline f80 add(f80 a, f80 b, Status& st) {
    if (invalidEncoding(a) || invalidEncoding(b)) { raise(st, flag_invalid); return defaultNaN(); }
    int aS = signOf(a), bS = signOf(b);
    return (aS == bS) ? addSigs(a, b, aS, st) : subSigs(a, b, aS, st);
}
inline f80 sub(f80 a, f80 b, Status& st) {
    if (invalidEncoding(a) || invalidEncoding(b)) { raise(st, flag_invalid); return defaultNaN(); }
    int aS = signOf(a), bS = signOf(b);
    return (aS == bS) ? subSigs(a, b, aS, st) : addSigs(a, b, aS, st);
}

// --- Arrondi à la précision SANS réduction de plage d'exposant -------------------
// Port de roundSigAndPackFloatx80 (softfloat.c:1503), utilisé par FSGLMUL / FSGLDIV
// et FMOVECR (fpp_round32/64). Différence avec roundAndPack : la mantisse est arrondie à 24 ou
// 53 bits, mais l'exposant garde la plage ÉTENDUE (seuils 0x7FFE / 0), là où roundAndPack
// applique en plus un expOffset qui rabat la plage sur celle du simple/double précision.
// Passer par roundAndPack faisait donc déborder FSGLMUL/FSGLDIV vers 2^±126 au lieu de
// 2^±16383 — un résultat parfaitement représentable rendu ±∞.
inline f80 roundSigAndPack(int prec, int sign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, Status& st) {
    const int rm = st.roundingMode;
    const bool rne = (rm == round_nearest_even);
    uint64_t roundIncr, roundMask;
    if (prec == 32)      { roundIncr = 0x0000008000000000ull; roundMask = 0x000000FFFFFFFFFFull; }
    else if (prec == 64) { roundIncr = 0x0000000000000400ull; roundMask = 0x00000000000007FFull; }
    else return roundAndPack(80, sign, zExp, zSig0, zSig1, st);
    zSig0 |= (zSig1 != 0);
    if (!rne) {
        if (rm == round_to_zero) roundIncr = 0;
        else { roundIncr = roundMask;
               if (sign) { if (rm == round_up)   roundIncr = 0; }
               else      { if (rm == round_down) roundIncr = 0; } }
    }
    uint64_t roundBits = zSig0 & roundMask;
    if (0x7FFE <= uint32_t(zExp)) {
        if ((0x7FFE < zExp) || ((zExp == 0x7FFE) && (zSig0 + roundIncr < zSig0))) {
            raise(st, flag_overflow);
            if (zSig0 & roundMask) raise(st, flag_inexact);
            if (rm == round_to_zero || (sign && rm == round_up) || (!sign && rm == round_down))
                return pack(sign, 0x7FFE, ~uint64_t(0));
            return pack(sign, 0x7FFF, INF_SIG);
        }
        if (zExp < 0) {
            raise(st, flag_underflow);
            shift64RightJamming(zSig0, -zExp, zSig0);
            zExp = 0;
            roundBits = zSig0 & roundMask;
            if (roundBits) raise(st, flag_inexact);
            zSig0 += roundIncr;
            if (rne && (roundBits == roundIncr)) roundMask |= roundIncr << 1;
            zSig0 &= ~roundMask;
            return pack(sign, zExp, zSig0);
        }
    }
    if (roundBits) raise(st, flag_inexact);
    zSig0 += roundIncr;
    if (zSig0 < roundIncr) { ++zExp; zSig0 = INF_LOW; }
    roundIncr = roundMask + 1;
    if (rne && ((roundBits << 1) == roundIncr)) roundMask |= roundIncr;
    zSig0 &= ~roundMask;
    if (zSig0 == 0) zExp = 0;
    return pack(sign, zExp, zSig0);
}

inline f80 mul(f80 a, f80 b, Status& st) {
    if (invalidEncoding(a) || invalidEncoding(b)) { raise(st, flag_invalid); return defaultNaN(); }
    int aSign = signOf(a), bSign = signOf(b), zSign = aSign ^ bSign;
    int32_t aExp = expOf(a), bExp = expOf(b), zExp; uint64_t aSig = fracOf(a), bSig = fracOf(b), zSig0, zSig1;
    if (aExp == 0x7FFF) {
        if ((aSig << 1) || ((bExp == 0x7FFF) && (bSig << 1))) return propagateNaN(st, a, b);
        if ((bExp | bSig) == 0) { raise(st, flag_invalid); return defaultNaN(); }
        return pack(zSign, aExp, aSig);
    }
    if (bExp == 0x7FFF) {
        if (bSig << 1) return propagateNaN(st, a, b);
        if ((aExp | aSig) == 0) { raise(st, flag_invalid); return defaultNaN(); }
        return pack(zSign, bExp, bSig);
    }
    if (aExp == 0) { if (aSig == 0) return pack(zSign, 0, 0); normalizeSubnormal(aSig, aExp, aSig); }
    if (bExp == 0) { if (bSig == 0) return pack(zSign, 0, 0); normalizeSubnormal(bSig, bExp, bSig); }
    zExp = aExp + bExp - 0x3FFE;
    mul64To128(aSig, bSig, zSig0, zSig1);
    if (0 < int64_t(zSig0)) { shortShift128Left(zSig0, zSig1, 1, zSig0, zSig1); --zExp; }
    return roundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
}

inline f80 div(f80 a, f80 b, Status& st) {
    if (invalidEncoding(a) || invalidEncoding(b)) { raise(st, flag_invalid); return defaultNaN(); }
    int aSign = signOf(a), bSign = signOf(b), zSign = aSign ^ bSign;
    int32_t aExp = expOf(a), bExp = expOf(b), zExp; uint64_t aSig = fracOf(a), bSig = fracOf(b), zSig0, zSig1;
    uint64_t rem0, rem1, rem2, term0, term1, term2;
    if (aExp == 0x7FFF) {
        if (aSig << 1) return propagateNaN(st, a, b);
        if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); raise(st, flag_invalid); return defaultNaN(); }
        return pack(zSign, aExp, aSig);
    }
    if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); return pack(zSign, 0, 0); }
    if (bExp == 0) {
        if (bSig == 0) {
            if ((aExp | aSig) == 0) { raise(st, flag_invalid); return defaultNaN(); }
            raise(st, flag_divzero); return pack(zSign, 0x7FFF, INF_SIG);
        }
        normalizeSubnormal(bSig, bExp, bSig);
    }
    if (aExp == 0) { if (aSig == 0) return pack(zSign, 0, 0); normalizeSubnormal(aSig, aExp, aSig); }
    zExp = aExp - bExp + 0x3FFE;
    rem1 = 0;
    if (bSig <= aSig) { shift128Right(aSig, 0, 1, aSig, rem1); ++zExp; }
    zSig0 = estimateDiv128To64(aSig, rem1, bSig);
    mul64To128(bSig, zSig0, term0, term1);
    sub128(aSig, rem1, term0, term1, rem0, rem1);
    while (int64_t(rem0) < 0) { --zSig0; add128(rem0, rem1, 0, bSig, rem0, rem1); }
    zSig1 = estimateDiv128To64(rem1, 0, bSig);
    if (uint64_t(zSig1 << 1) <= 8) {
        mul64To128(bSig, zSig1, term1, term2);
        sub128(rem1, 0, term1, term2, rem1, rem2);
        while (int64_t(rem1) < 0) { --zSig1; add128(rem1, rem2, 0, bSig, rem1, rem2); }
        zSig1 |= ((rem1 | rem2) != 0);
    }
    return roundAndPack(st.roundingPrecision, zSign, zExp, zSig0, zSig1, st);
}

// --- FSGLMUL / FSGLDIV (ports de floatx80_sglmul / floatx80_sgldiv) --------------
// Mantisses tronquées à 24 bits, mais résultat arrondi par roundSigAndPack (plage
// d'exposant ÉTENDUE). La troncature n'intervient qu'APRÈS le traitement des NaN/∞/zéro
// ET après normalisation des dénormaux : la faire sur les opérandes bruts écrasait le
// payload des NaN propagés et réduisait un dénormal à zéro.
inline f80 sglmul(f80 a, f80 b, Status& st) {
    int aSign = signOf(a), bSign = signOf(b), zSign = aSign ^ bSign;
    int32_t aExp = expOf(a), bExp = expOf(b), zExp; uint64_t aSig = fracOf(a), bSig = fracOf(b), zSig0, zSig1;
    if (aExp == 0x7FFF) {
        if ((aSig << 1) || ((bExp == 0x7FFF) && (bSig << 1))) return propagateNaN(st, a, b);
        if ((bExp | bSig) == 0) { raise(st, flag_invalid); return defaultNaN(); }
        return pack(zSign, aExp, aSig);
    }
    if (bExp == 0x7FFF) {
        if (bSig << 1) return propagateNaN(st, a, b);
        if ((aExp | aSig) == 0) { raise(st, flag_invalid); return defaultNaN(); }
        return pack(zSign, bExp, bSig);
    }
    if (aExp == 0) { if (aSig == 0) return pack(zSign, 0, 0); normalizeSubnormal(aSig, aExp, aSig); }
    if (bExp == 0) { if (bSig == 0) return pack(zSign, 0, 0); normalizeSubnormal(bSig, bExp, bSig); }
    aSig &= 0xFFFFFF0000000000ull;      // 24 bits — APRÈS les cas spéciaux (cf. en-tête)
    bSig &= 0xFFFFFF0000000000ull;
    zExp = aExp + bExp - 0x3FFE;
    mul64To128(aSig, bSig, zSig0, zSig1);
    if (0 < int64_t(zSig0)) { shortShift128Left(zSig0, zSig1, 1, zSig0, zSig1); --zExp; }
    return roundSigAndPack(32, zSign, zExp, zSig0, zSig1, st);
}

inline f80 sgldiv(f80 a, f80 b, Status& st) {
    int aSign = signOf(a), bSign = signOf(b), zSign = aSign ^ bSign;
    int32_t aExp = expOf(a), bExp = expOf(b), zExp; uint64_t aSig = fracOf(a), bSig = fracOf(b), zSig0, zSig1;
    uint64_t rem0, rem1, rem2, term0, term1, term2;
    if (aExp == 0x7FFF) {
        if (aSig << 1) return propagateNaN(st, a, b);
        if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); raise(st, flag_invalid); return defaultNaN(); }
        return pack(zSign, aExp, aSig);
    }
    if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); return pack(zSign, 0, 0); }
    if (bExp == 0) {
        if (bSig == 0) {
            if ((aExp | aSig) == 0) { raise(st, flag_invalid); return defaultNaN(); }
            raise(st, flag_divzero); return pack(zSign, 0x7FFF, INF_SIG);
        }
        normalizeSubnormal(bSig, bExp, bSig);
    }
    if (aExp == 0) { if (aSig == 0) return pack(zSign, 0, 0); normalizeSubnormal(aSig, aExp, aSig); }
    zExp = aExp - bExp + 0x3FFE;
    rem1 = 0;
    if (bSig <= aSig) { shift128Right(aSig, 0, 1, aSig, rem1); ++zExp; }
    zSig0 = estimateDiv128To64(aSig, rem1, bSig);
    mul64To128(bSig, zSig0, term0, term1);
    sub128(aSig, rem1, term0, term1, rem0, rem1);
    while (int64_t(rem0) < 0) { --zSig0; add128(rem0, rem1, 0, bSig, rem0, rem1); }
    zSig1 = estimateDiv128To64(rem1, 0, bSig);
    if (uint64_t(zSig1 << 1) <= 8) {
        mul64To128(bSig, zSig1, term1, term2);
        sub128(rem1, 0, term1, term2, rem1, rem2);
        while (int64_t(rem1) < 0) { --zSig1; add128(rem1, rem2, 0, bSig, rem1, rem2); }
        zSig1 |= ((rem1 | rem2) != 0);
    }
    return roundSigAndPack(32, zSign, zExp, zSig0, zSig1, st);
}

inline f80 sqrt_(f80 a, Status& st) {
    if (invalidEncoding(a)) { raise(st, flag_invalid); return defaultNaN(); }
    int aSign = signOf(a); int32_t aExp = expOf(a), zExp;
    uint64_t aSig0 = fracOf(a), aSig1, zSig0, zSig1, doubleZSig0;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;
    if (aExp == 0x7FFF) { if (aSig0 << 1) return propagateNaN1(st, a); if (!aSign) return a; raise(st, flag_invalid); return defaultNaN(); }
    if (aSign) { if ((aExp | aSig0) == 0) return a; raise(st, flag_invalid); return defaultNaN(); }
    if (aExp == 0) { if (aSig0 == 0) return pack(0, 0, 0); normalizeSubnormal(aSig0, aExp, aSig0); }
    zExp = ((aExp - 0x3FFF) >> 1) + 0x3FFF;
    zSig0 = estimateSqrt32(aExp, uint32_t(aSig0 >> 32));
    shift128Right(aSig0, 0, 2 + (aExp & 1), aSig0, aSig1);
    zSig0 = estimateDiv128To64(aSig0, aSig1, zSig0 << 32) + (zSig0 << 30);
    doubleZSig0 = zSig0 << 1;
    mul64To128(zSig0, zSig0, term0, term1);
    sub128(aSig0, aSig1, term0, term1, rem0, rem1);
    while (int64_t(rem0) < 0) { --zSig0; doubleZSig0 -= 2; add128(rem0, rem1, zSig0 >> 63, doubleZSig0 | 1, rem0, rem1); }
    zSig1 = estimateDiv128To64(rem1, 0, doubleZSig0);
    if ((zSig1 & 0x3FFFFFFFFFFFFFFFull) <= 5) {
        if (zSig1 == 0) zSig1 = 1;
        mul64To128(doubleZSig0, zSig1, term1, term2);
        sub128(rem1, 0, term1, term2, rem1, rem2);
        mul64To128(zSig1, zSig1, term2, term3);
        sub192(rem1, rem2, 0, 0, term2, term3, rem1, rem2, rem3);
        while (int64_t(rem1) < 0) {
            --zSig1; shortShift128Left(0, zSig1, 1, term2, term3);
            term3 |= 1; term2 |= doubleZSig0;
            add192(rem1, rem2, rem3, 0, term2, term3, rem1, rem2, rem3);
        }
        zSig1 |= ((rem1 | rem2 | rem3) != 0);
    }
    shortShift128Left(0, zSig1, 1, zSig0, zSig1);
    zSig0 |= doubleZSig0;
    return roundAndPack(st.roundingPrecision, 0, zExp, zSig0, zSig1, st);
}

// Reste IEEE (FREM, mod=false) ou modulo tronqué (FMOD, mod=true). `q` reçoit les 7
// bits de poids faible du quotient + le signe (octet quotient FPSR), `s` son signe.
inline f80 rem(f80 a, f80 b, uint64_t& q, int& s, bool mod, Status& st) {
    if (invalidEncoding(a) || invalidEncoding(b)) { raise(st, flag_invalid); return defaultNaN(); }
    int aSign = signOf(a), bSign = signOf(b), zSign;
    int32_t aExp = expOf(a), bExp = expOf(b), expDiff;
    uint64_t aSig0 = fracOf(a), aSig1, bSig = fracOf(b), qTemp, term0, term1, altA0, altA1;
    s = 0; q = 0;
    if (aExp == 0x7FFF) { if ((aSig0 << 1) || ((bExp == 0x7FFF) && (bSig << 1))) return propagateNaN(st, a, b); raise(st, flag_invalid); return defaultNaN(); }
    if (bExp == 0x7FFF) { if (bSig << 1) return propagateNaN(st, a, b); s = (aSign != bSign); return normalizeRoundAndPack(st.roundingPrecision, aSign, aExp, aSig0, 0, st); }
    if (bExp == 0) { if (bSig == 0) { raise(st, flag_invalid); return defaultNaN(); } normalizeSubnormal(bSig, bExp, bSig); }
    if (aExp == 0) { if (aSig0 == 0) { s = (aSign != bSign); return a; } normalizeSubnormal(aSig0, aExp, aSig0); }
    bSig |= INF_LOW; zSign = aSign; expDiff = aExp - bExp; s = (aSign != bSign); aSig1 = 0;
    if (expDiff < 0) {
        // floatx80_mod (softfloat.c:3048) passe TOUT expDiff négatif par roundAndPack
        // — d'où l'arrondi à la précision du FPCR et UNFL sur un dénormal ; seul
        // floatx80_rem (softfloat.c:2941) court-circuite par « return a ».
        if (mod) return roundAndPack(st.roundingPrecision, aSign, aExp, aSig0, 0, st);
        if (expDiff < -1) return a;
        shift128Right(aSig0, 0, 1, aSig0, aSig1); expDiff = 0;
    }
    qTemp = (bSig <= aSig0); if (qTemp) aSig0 -= bSig;
    q = (expDiff > 63) ? 0 : (qTemp << expDiff); expDiff -= 64;
    while (0 < expDiff) {
        qTemp = estimateDiv128To64(aSig0, aSig1, bSig); qTemp = (2 < qTemp) ? qTemp - 2 : 0;
        mul64To128(bSig, qTemp, term0, term1); sub128(aSig0, aSig1, term0, term1, aSig0, aSig1);
        shortShift128Left(aSig0, aSig1, 62, aSig0, aSig1);
        q = (expDiff > 63) ? 0 : (qTemp << expDiff); expDiff -= 62;
    }
    expDiff += 64;
    if (0 < expDiff) {
        qTemp = estimateDiv128To64(aSig0, aSig1, bSig); qTemp = (2 < qTemp) ? qTemp - 2 : 0;
        qTemp >>= 64 - expDiff;
        mul64To128(bSig, qTemp << (64 - expDiff), term0, term1); sub128(aSig0, aSig1, term0, term1, aSig0, aSig1);
        shortShift128Left(0, bSig, 64 - expDiff, term0, term1);
        while (le128(term0, term1, aSig0, aSig1)) { ++qTemp; sub128(aSig0, aSig1, term0, term1, aSig0, aSig1); }
        q += qTemp;
    } else { term1 = 0; term0 = bSig; }
    if (!mod) {                                    // FREM : ajuste le quotient au plus proche
        sub128(term0, term1, aSig0, aSig1, altA0, altA1);
        if (lt128(altA0, altA1, aSig0, aSig1) || (altA0 == aSig0 && altA1 == aSig1 && (qTemp & 1))) {
            aSig0 = altA0; aSig1 = altA1; zSign = !zSign; ++q;
        }
    }
    return normalizeRoundAndPack(st.roundingPrecision, zSign, bExp + expDiff, aSig0, aSig1, st);
}

// Arrondi à l'entier selon le mode courant (FINT) ; round_to_zero pour FINTRZ.
inline f80 roundToInt(f80 a, Status& st) {
    int aSign = signOf(a); int32_t aExp = expOf(a);
    uint64_t lastBit, roundMask; f80 z;
    if (0x403E <= aExp) {
        if (aExp == 0x7FFF) { if (fracOf(a) << 1) return propagateNaN1(st, a); return a; }
        return a;
    }
    if (aExp < 0x3FFF) {
        if (aExp == 0 && fracOf(a) == 0) return a;
        raise(st, flag_inexact);
        switch (st.roundingMode) {
            case round_nearest_even:
                if ((aExp == 0x3FFE) && (fracOf(a) << 1)) return pack(aSign, 0x3FFF, INF_LOW);
                break;
            case round_down: return aSign ? pack(1, 0x3FFF, INF_LOW) : pack(0, 0, 0);
            case round_up:   return aSign ? pack(1, 0, 0) : pack(0, 0x3FFF, INF_LOW);
        }
        return pack(aSign, 0, 0);
    }
    lastBit = uint64_t(1) << (0x403E - aExp); roundMask = lastBit - 1; z = a;
    switch (st.roundingMode) {
        case round_nearest_even: z.low += lastBit >> 1; if ((z.low & roundMask) == 0) z.low &= ~lastBit; break;
        case round_to_zero: break;
        case round_up:   if (!signOf(z)) z.low += roundMask; break;
        case round_down: if ( signOf(z)) z.low += roundMask; break;
    }
    z.low &= ~roundMask;
    if (z.low == 0) { z.high++; z.low = INF_LOW; }
    if (z.low != a.low) raise(st, flag_inexact);
    return z;
}

// FGETEXP : exposant non biaisé en flottant. FGETMAN : mantisse dans [1,2).
inline int32_t getExpUnbiased(f80 a, bool& special) {
    int32_t aExp = expOf(a); special = false;
    if (aExp == 0x7FFF || ((aExp == 0) && fracOf(a) == 0)) { special = true; return 0; }
    if (aExp == 0) { uint64_t s; normalizeSubnormal(fracOf(a), aExp, s); }
    return aExp - 0x3FFF;
}
inline f80 getMan(f80 a, Status& st) {
    int aSign = signOf(a); int32_t aExp = expOf(a); uint64_t aSig = fracOf(a);
    if (aExp == 0x7FFF) { if (aSig << 1) return propagateNaN1(st, a); raise(st, flag_invalid); return defaultNaN(); }
    if (aExp == 0) { if (aSig == 0) return pack(aSign, 0, 0); normalizeSubnormal(aSig, aExp, aSig); }
    return pack(aSign, 0x3FFF, aSig);
}

// --- Conversion étendu → ENTIER signé (L/W/B) ---------------------------------
// Port de `roundAndPackInt32/16/8` + `floatx80_to_int32/16/8`
// (hatari/src/cpu/softfloat/softfloat.c). `bits` ∈ {32,16,8}.
//
// ⚠ POURQUOI CE CHEMIN EXISTE, alors que `Fpu::encodeFmt` savait déjà « arrondir ».
// Il arrondissait un `double` obtenu par `extToD` — donc APRÈS une première perte de
// 64 → 53 bits de mantisse. Deux arrondis successifs ne valent pas un seul : l'étendu
// juste au-dessus de 0,5 ($3FFE 80000000_00000001) retombe EXACTEMENT sur 0,5 en
// double, puis la règle du pair le plus proche l'envoie sur 0 — là où le 68881 rend 1,
// la valeur étant strictement supérieure à 0,5. Exhibé par le test 10 de
// `tools/make_fpu_testrom.py` (2026-09-02). On convertit donc depuis la mantisse
// 64 bits, sans jamais passer par un flottant hôte.
//
// Les 7 bits de poids faible servent de bits d'arrondi (d'où le décalage à
// `0x4037 = 16383 + 63 - 7`) ; `shift64RightJamming` conserve la « collante » qui
// distingue une demie EXACTE d'une demie APPROCHÉE — c'est elle qui manquait.
inline int64_t roundAndPackInt(int bits, int sign, uint64_t absZ, Status& st) {
    const int  rm  = st.roundingMode;
    const bool rne = (rm == round_nearest_even);
    int roundIncrement;
    switch (rm) {
        case round_to_zero: roundIncrement = 0;                 break;
        case round_up:      roundIncrement = sign ? 0    : 0x7F; break;
        case round_down:    roundIncrement = sign ? 0x7F : 0;    break;
        default:            roundIncrement = 0x40;               break;   // au plus près
    }
    const int roundBits = int(absZ & 0x7F);
    absZ = (absZ + uint64_t(roundIncrement)) >> 7;
    // Égalité EXACTE au demi sous « au plus près » → on force le bit 0 à 0 (pair).
    absZ &= ~uint64_t((roundBits ^ 0x40) == 0 && rne ? 1u : 0u);
    int64_t z = int64_t(absZ);
    if (sign) z = -z;
    const uint64_t over = absZ >> bits;                     // >>32, >>16 ou >>8
    if (over || (z != 0 && ((z < 0) != (sign != 0)))) {
        raise(st, flag_invalid);                            // → OPERR, valeur saturée
        const int64_t maxv = (int64_t(1) << (bits - 1)) - 1;
        return sign ? -maxv - 1 : maxv;
    }
    if (roundBits) raise(st, flag_inexact);                 // → INEX2
    return z;
}

inline int64_t toInt(f80 a, int bits, Status& st) {
    const uint64_t aSig  = fracOf(a);
    const int32_t  aExp  = expOf(a);
    const int      aSign = signOf(a);
    if (aExp == 0x7FFF) {
        if (uint64_t(aSig << 1)) {                          // NaN → PAYLOAD, pas 0
            const f80 n = propagateNaN1(st, a);
            if (n.low == aSig) raise(st, flag_invalid);
            // int32 → low>>32, int16 → low>>48, int8 → low>>56 (softfloat.c).
            const uint64_t raw = n.low >> (64 - bits);
            const uint64_t sgn = uint64_t(1) << (bits - 1);
            return int64_t((raw ^ sgn) - sgn);              // extension de signe
        }
        raise(st, flag_invalid);                            // ±inf → saturation
        const int64_t maxv = (int64_t(1) << (bits - 1)) - 1;
        return aSign ? -maxv - 1 : maxv;
    }
    int shiftCount = 0x4037 - aExp;
    if (shiftCount <= 0) shiftCount = 1;
    uint64_t sig;
    shift64RightJamming(aSig, shiftCount, sig);
    return roundAndPackInt(bits, aSign, sig, st);
}

// --- Conversion étendu → SIMPLE / DOUBLE (FMOVE.S / FMOVE.D) -------------------
// Port de `roundAndPackFloat32/64` + `floatx80_to_float32/64` (softfloat.c).
// Mêmes raisons que `toInt` : `Fpu::encodeFmt` fabriquait un `double` hôte avec
// l'arrondi HÔTE (toujours au plus près), ignorant le mode du FPCR, et ne levait
// ni INEX2 ni UNFL ; en D il ne signalait même pas l'OVFL. Ici l'arrondi se fait UNE
// fois, depuis la mantisse 64 bits, dans le mode demandé, avec les drapeaux.
inline void shift32RightJamming(uint32_t a, int count, uint32_t& z) {
    if (count == 0)      z = a;
    else if (count < 32) z = (a >> count) | ((a << ((-count) & 31)) != 0);
    else                 z = (a != 0);
}

inline uint32_t roundAndPackF32(int sign, int32_t zExp, uint32_t zSig, Status& st) {
    const int  rm  = st.roundingMode;
    const bool rne = (rm == round_nearest_even);
    int roundIncrement;
    switch (rm) {
        case round_to_zero: roundIncrement = 0;                  break;
        case round_up:      roundIncrement = sign ? 0    : 0x7F; break;
        case round_down:    roundIncrement = sign ? 0x7F : 0;    break;
        default:            roundIncrement = 0x40;               break;
    }
    int roundBits = int(zSig & 0x7F);
    if (uint16_t(zExp) >= 0xFD) {
        if (zExp > 0xFD || (zExp == 0xFD && int32_t(zSig + uint32_t(roundIncrement)) < 0)) {
            raise(st, flag_overflow);
            if (roundBits) raise(st, flag_inexact);
            const uint32_t inf = (uint32_t(sign) << 31) | (0xFFu << 23);
            return roundIncrement == 0 ? (inf - 1) : inf;      // RZ → plus grand fini
        }
        if (zExp < 0) {                                        // dénormalisation
            const bool tiny = (zExp < -1) || (zSig + uint32_t(roundIncrement) < 0x80000000u);
            if (tiny) raise(st, flag_underflow);
            shift32RightJamming(zSig, -zExp, zSig);
            zExp = 0;
            roundBits = int(zSig & 0x7F);
        }
    }
    if (roundBits) raise(st, flag_inexact);
    zSig = (zSig + uint32_t(roundIncrement)) >> 7;
    zSig &= ~uint32_t((roundBits ^ 0x40) == 0 && rne ? 1u : 0u);
    if (zSig == 0) zExp = 0;
    return (uint32_t(sign) << 31) + (uint32_t(zExp) << 23) + zSig;
}

inline uint64_t roundAndPackF64(int sign, int32_t zExp, uint64_t zSig, Status& st) {
    const int  rm  = st.roundingMode;
    const bool rne = (rm == round_nearest_even);
    int roundIncrement;
    switch (rm) {
        case round_to_zero: roundIncrement = 0;                   break;
        case round_up:      roundIncrement = sign ? 0     : 0x3FF; break;
        case round_down:    roundIncrement = sign ? 0x3FF : 0;     break;
        default:            roundIncrement = 0x200;                break;
    }
    int roundBits = int(zSig & 0x3FF);
    if (uint16_t(zExp) >= 0x7FD) {
        if (zExp > 0x7FD || (zExp == 0x7FD && int64_t(zSig + uint64_t(roundIncrement)) < 0)) {
            raise(st, flag_overflow);
            if (roundBits) raise(st, flag_inexact);
            const uint64_t inf = (uint64_t(sign) << 63) | (uint64_t(0x7FF) << 52);
            return roundIncrement == 0 ? (inf - 1) : inf;
        }
        if (zExp < 0) {
            const bool tiny = (zExp < -1)
                           || (zSig + uint64_t(roundIncrement) < 0x8000000000000000ull);
            if (tiny) raise(st, flag_underflow);
            uint64_t s; shift64RightJamming(zSig, -zExp, s); zSig = s;
            zExp = 0;
            roundBits = int(zSig & 0x3FF);
        }
    }
    if (roundBits) raise(st, flag_inexact);
    zSig = (zSig + uint64_t(roundIncrement)) >> 10;
    zSig &= ~uint64_t((roundBits ^ 0x200) == 0 && rne ? 1u : 0u);
    if (zSig == 0) zExp = 0;
    return (uint64_t(sign) << 63) + (uint64_t(uint32_t(zExp)) << 52) + zSig;
}

inline uint32_t toFloat32(f80 a, Status& st) {
    uint64_t aSig = fracOf(a); int32_t aExp = expOf(a); const int aSign = signOf(a);
    if (aExp == 0x7FFF) {
        if (uint64_t(aSig << 1)) {                             // NaN → payload tronqué
            const f80 n = propagateNaN1(st, a);
            return (uint32_t(signOf(n)) << 31) | (0xFFu << 23)
                 | uint32_t((n.low >> 40) & 0x7FFFFFu);
        }
        return (uint32_t(aSign) << 31) | (0xFFu << 23);        // ±inf
    }
    if (aExp == 0) {
        if (aSig == 0) return uint32_t(aSign) << 31;           // ±0
        normalizeSubnormal(aSig, aExp, aSig);
    }
    uint64_t s; shift64RightJamming(aSig, 33, s);
    return roundAndPackF32(aSign, aExp - 0x3F81, uint32_t(s), st);
}

inline uint64_t toFloat64(f80 a, Status& st) {
    uint64_t aSig = fracOf(a); int32_t aExp = expOf(a); const int aSign = signOf(a);
    if (aExp == 0x7FFF) {
        if (uint64_t(aSig << 1)) {
            const f80 n = propagateNaN1(st, a);
            return (uint64_t(signOf(n)) << 63) | (uint64_t(0x7FF) << 52)
                 | ((n.low >> 11) & 0xFFFFFFFFFFFFFull);
        }
        return (uint64_t(aSign) << 63) | (uint64_t(0x7FF) << 52);
    }
    if (aExp == 0) {
        if (aSig == 0) return uint64_t(aSign) << 63;
        normalizeSubnormal(aSig, aExp, aSig);
    }
    uint64_t s; shift64RightJamming(aSig, 1, s);
    return roundAndPackF64(aSign, aExp - 0x3C01, s, st);
}

// Comparaison ordonnée : -1 (a<b), 0 (a==b), +1 (a>b), 2 (non ordonné = NaN).
inline int compare(f80 a, f80 b) {
    if (isNaN(a) || isNaN(b)) return 2;
    int aSign = signOf(a), bSign = signOf(b);
    bool aZero = (expOf(a) == 0 && fracOf(a) == 0), bZero = (expOf(b) == 0 && fracOf(b) == 0);
    if (aZero && bZero) return 0;
    if (aSign != bSign) return aSign ? -1 : 1;
    // mêmes signes : compare (exp,man) puis inverse si négatif
    int c;
    if (expOf(a) != expOf(b)) c = (expOf(a) < expOf(b)) ? -1 : 1;
    else if (fracOf(a) != fracOf(b)) c = (fracOf(a) < fracOf(b)) ? -1 : 1;
    else c = 0;
    return aSign ? -c : c;
}

} // namespace sf
