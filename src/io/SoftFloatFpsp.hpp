// =============================================================================
//  SoftFloatFpsp.hpp — Fonctions TRANSCENDANTES du MC68881 en étendu 80 bits
//  (mantisse 64 bits réelle), bit à bit identiques à Hatari.
//
//  Portage de `extern/hatari/src/cpu/softfloat/softfloat_fpsp.c` et de ses
//  tables `softfloat_fpsp_tables.h` : algorithmes de la bibliothèque FPSP de
//  Motorola (paquetage logiciel du 68040), transcrits en SoftFloat par Andreas
//  Grabher pour l'émulateur Previous, repris par WinUAE puis Hatari
//  (`fpp_softfloat.c` les appelle directement avec le `float_status` du FPCR).
//
//  Règle de portage : les CORPS sont recopiés tels quels — mêmes constantes,
//  même ordre des opérations, mêmes seuils, mêmes `goto` — car le moindre
//  réarrangement change un bit d'arrondi. Seule l'enveloppe change :
//    • `float_status *` devient `sf::Status *` (champs renommés) ;
//    • `LIT64(x)` devient `xull`, les tables deviennent `constexpr` ;
//    • SET_PREC / RESET_PREC deviennent setPrec / resetPrec (même effet :
//      calculs intermédiaires en étendu au plus près, dernière opération dans
//      la précision et le mode du FPCR) ;
//    • les primitives SoftFloat appelées (floatx80_add/sub/mul/div/sqrt,
//      roundAndPackFloatx80…) délèguent à SoftFloatX80.hpp, déjà comparé à
//      Hatari ; celles qui manquaient (int32/float32/float64 → floatx80,
//      floatx80_move/abs/to_int32) sont portées ci-dessous depuis softfloat.c.
//
//  Fidélité : c'est l'égalité avec HATARI qui est garantie (banc
//  tools/test_fpu_transcendental_oracle.py). Le 68881 réel calcule ces
//  fonctions par son microcode (CORDIC), pas par les polynômes du FPSP 68040 :
//  un écart d'un ulp avec le silicium reste possible et n'est pas documenté
//  par Motorola pour le 6888x.
//
//  En-tête d'origine du code porté (conservé tel quel) :
//
/*============================================================================

 This C source file is an extension to the SoftFloat IEC/IEEE Floating-point
 Arithmetic Package, Release 2a.

 Written by Andreas Grabher for Previous, NeXT Computer Emulator.

=============================================================================*/
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST (portage ; algorithmes Motorola
//  FPSP, transcription SoftFloat Andreas Grabher, SoftFloat-2a John R. Hauser).
// =============================================================================
#pragma once
#include <cstdint>
#include "SoftFloatX80.hpp"

namespace sf {
namespace fpsp {

// --- Enveloppe : noms SoftFloat d'origine → primitives de SoftFloatX80.hpp ------
// Garder les noms d'Hatari permet de relire les corps ligne à ligne contre
// softfloat_fpsp.c ; tout reste confiné à l'espace de noms sf::fpsp.
using floatx80     = f80;
using float_status = Status;
using float32      = uint32_t;
using float64      = uint64_t;
using flag         = uint8_t;

enum : uint8_t {
    float_flag_invalid   = flag_invalid,
    float_flag_divbyzero = flag_divzero,
    float_flag_overflow  = flag_overflow,
    float_flag_underflow = flag_underflow,
    float_flag_inexact   = flag_inexact,
    float_flag_signaling = flag_signaling,
};
enum { float_round_nearest_even = round_nearest_even };
constexpr uint64_t floatx80_default_infinity_low = INF_SIG;   // softfloat.h:348

inline uint64_t extractFloatx80Frac(floatx80 a) { return fracOf(a); }
inline int32_t  extractFloatx80Exp (floatx80 a) { return expOf(a); }
inline flag     extractFloatx80Sign(floatx80 a) { return flag(signOf(a)); }
inline floatx80 packFloatx80(flag zSign, int32_t zExp, uint64_t zSig) { return pack(zSign, zExp, zSig); }
inline void     float_raise(uint8_t flags, float_status* status) { raise(*status, flags); }
inline floatx80 floatx80_default_nan(float_status*) { return defaultNaN(); }
inline floatx80 propagateFloatx80NaNOneArg(floatx80 a, float_status* status) { return propagateNaN1(*status, a); }
inline void     normalizeFloatx80Subnormal(uint64_t aSig, int32_t* zExpPtr, uint64_t* zSigPtr) {
    normalizeSubnormal(aSig, *zExpPtr, *zSigPtr);
}
inline floatx80 roundAndPackFloatx80(int roundingPrecision, flag zSign, int32_t zExp,
                                     uint64_t zSig0, uint64_t zSig1, float_status* status) {
    return roundAndPack(roundingPrecision, zSign, zExp, zSig0, zSig1, *status);
}
inline floatx80 floatx80_add(floatx80 a, floatx80 b, float_status* s) { return add(a, b, *s); }
inline floatx80 floatx80_sub(floatx80 a, floatx80 b, float_status* s) { return sub(a, b, *s); }
inline floatx80 floatx80_mul(floatx80 a, floatx80 b, float_status* s) { return mul(a, b, *s); }
inline floatx80 floatx80_div(floatx80 a, floatx80 b, float_status* s) { return div(a, b, *s); }
inline floatx80 floatx80_sqrt(floatx80 a, float_status* s)            { return sqrt_(a, *s); }

// floatx80_to_int32 (softfloat.c:1764, branche SOFTFLOAT_68K) : sf::toInt en est
// le portage commun aux largeurs 32/16/8.
inline int32_t floatx80_to_int32(floatx80 a, float_status* status) {
    return int32_t(toInt(a, 32, *status));
}

// --- Primitives manquantes, portées de softfloat.c / softfloat-specialize.h ------
// int32_to_floatx80 (softfloat.c:1632) : conversion exacte, sans drapeau.
inline floatx80 int32_to_floatx80(int32_t a) {
    if (a == 0) return packFloatx80(0, 0, 0);
    const flag zSign = (a < 0);
    const uint32_t absA = zSign ? uint32_t(0) - uint32_t(a) : uint32_t(a);
    const int shiftCount = __builtin_clz(absA) + 32;
    const uint64_t zSig = absA;
    return packFloatx80(zSign, 0x403E - shiftCount, zSig << shiftCount);
}
// commonNaNToFloatx80 (branche SOFTFLOAT_68K : bit de quiétude seul, sans bit entier).
inline floatx80 commonNaNToFloatx80(flag sign, uint64_t high) {
    return floatx80{ uint16_t((uint16_t(sign) << 15) | 0x7FFF), 0x4000000000000000ull | (high >> 1) };
}
// float32_to_floatx80 (softfloat.c:1655). Les constantes du FPSP ne sont jamais
// des NaN ; la branche est portée pour que la primitive reste complète.
inline floatx80 float32_to_floatx80(float32 a, float_status* status) {
    uint32_t aSig = a & 0x007FFFFF;
    int aExp = int((a >> 23) & 0xFF);
    const flag aSign = flag(a >> 31);
    if (aExp == 0xFF) {
        if (aSig) {
            // float32ToCommonNaN : signalant si bit de quiétude (22) nul.
            if (((a >> 22) & 0x1FF) == 0x1FE && (a & 0x003FFFFF)) float_raise(float_flag_signaling, status);
            return commonNaNToFloatx80(aSign, uint64_t(a) << 41);
        }
        return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        const int shiftCount = __builtin_clz(aSig) - 8;          // normalizeFloat32Subnormal
        aSig <<= shiftCount; aExp = 1 - shiftCount;
    }
    aSig |= 0x00800000;
    return packFloatx80(aSign, aExp + 0x3F80, uint64_t(aSig) << 40);
}
// float64_to_floatx80 (softfloat.c:1707).
inline floatx80 float64_to_floatx80(float64 a, float_status* status) {
    uint64_t aSig = a & 0x000FFFFFFFFFFFFFull;
    int aExp = int((a >> 52) & 0x7FF);
    const flag aSign = flag(a >> 63);
    if (aExp == 0x7FF) {
        if (aSig) {
            // float64ToCommonNaN lève float_flag_invalid (et non signaling) chez Hatari.
            if (((a >> 51) & 0xFFF) == 0xFFE && (a & 0x0007FFFFFFFFFFFFull)) float_raise(float_flag_invalid, status);
            return commonNaNToFloatx80(aSign, a << 12);
        }
        return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        const int shiftCount = clz64(aSig) - 11;                 // normalizeFloat64Subnormal
        aSig <<= shiftCount; aExp = 1 - shiftCount;
    }
    return packFloatx80(aSign, aExp + 0x3C00, (aSig | 0x0010000000000000ull) << 11);
}
// floatx80_move (softfloat.c:3399) et floatx80_abs (softfloat.c:3279), modèle
// 6888x : inf_clear_intbit est faux, un infini garde son bit entier.
inline floatx80 floatx80_move(floatx80 a, float_status* status) {
    const uint64_t aSig = extractFloatx80Frac(a);
    const int32_t aExp = extractFloatx80Exp(a);
    const flag aSign = extractFloatx80Sign(a);
    if (aExp == 0x7FFF) {
        if (uint64_t(aSig << 1)) return propagateFloatx80NaNOneArg(a, status);
        return a;
    }
    if (aExp == 0) {
        if (aSig == 0) return a;
        return normalizeRoundAndPack(status->roundingPrecision, aSign, aExp, aSig, 0, *status);
    }
    return roundAndPackFloatx80(status->roundingPrecision, aSign, aExp, aSig, 0, status);
}
inline floatx80 floatx80_abs(floatx80 a, float_status* status) {
    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    if (aExp == 0x7FFF) {
        if (uint64_t(aSig << 1)) return propagateFloatx80NaNOneArg(a, status);
        return packFloatx80(0, aExp, aSig);
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(0, 0, 0);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    return roundAndPackFloatx80(status->roundingPrecision, 0, aExp, aSig, 0, status);
}

// --- Constantes et précision de travail (macros d'origine) ------------------------
constexpr uint64_t pi_sig    = 0xc90fdaa22168c235ull;
constexpr int32_t  pi_exp    = 0x4000;
constexpr int32_t  piby2_exp = 0x3FFF;
constexpr int32_t  one_exp   = 0x3FFF;
constexpr uint64_t one_sig   = 0x8000000000000000ull;

// SET_PREC : sauve mode + précision de l'utilisateur, passe au plus près en étendu.
// RESET_PREC : les restaure avant la DERNIÈRE opération (arrondi visible).
struct PrecSave { int mode; int prec; };
inline PrecSave setPrec(float_status* status) {
    const PrecSave s{ status->roundingMode, status->roundingPrecision };
    status->roundingMode = float_round_nearest_even;
    status->roundingPrecision = 80;
    return s;
}
inline void resetPrec(float_status* status, PrecSave s) {
    status->roundingMode = s.mode;
    status->roundingPrecision = s.prec;
}

// Déclarations anticipées (prototypes de softfloat.h) : acos/asin → atan,
// atanh → lognp1, cosh/sinh/tanh → etox/etoxm1, log10/log2 → logn.
inline floatx80 floatx80_atan(floatx80 a, float_status* status);
inline floatx80 floatx80_etox(floatx80 a, float_status* status);
inline floatx80 floatx80_etoxm1(floatx80 a, float_status* status);
inline floatx80 floatx80_logn(floatx80 a, float_status* status);
inline floatx80 floatx80_lognp1(floatx80 a, float_status* status);

// --- Tables (softfloat_fpsp_tables.h, recopiées à l'identique) --------------------

inline constexpr f80 atan_tbl[128] = {
	{0x3FFB, 0x83D152C5060B7A51ull},
	{0x3FFB, 0x8BC8544565498B8Bull},
	{0x3FFB, 0x93BE406017626B0Dull},
	{0x3FFB, 0x9BB3078D35AEC202ull},
	{0x3FFB, 0xA3A69A525DDCE7DEull},
	{0x3FFB, 0xAB98E94362765619ull},
	{0x3FFB, 0xB389E502F9C59862ull},
	{0x3FFB, 0xBB797E436B09E6FBull},
	{0x3FFB, 0xC367A5C739E5F446ull},
	{0x3FFB, 0xCB544C61CFF7D5C6ull},
	{0x3FFB, 0xD33F62F82488533Eull},
	{0x3FFB, 0xDB28DA8162404C77ull},
	{0x3FFB, 0xE310A4078AD34F18ull},
	{0x3FFB, 0xEAF6B0A8188EE1EBull},
	{0x3FFB, 0xF2DAF1949DBE79D5ull},
	{0x3FFB, 0xFABD581361D47E3Eull},
	{0x3FFC, 0x8346AC210959ECC4ull},
	{0x3FFC, 0x8B232A08304282D8ull},
	{0x3FFC, 0x92FB70B8D29AE2F9ull},
	{0x3FFC, 0x9ACF476F5CCD1CB4ull},
	{0x3FFC, 0xA29E76304954F23Full},
	{0x3FFC, 0xAA68C5D08AB85230ull},
	{0x3FFC, 0xB22DFFFD9D539F83ull},
	{0x3FFC, 0xB9EDEF453E900EA5ull},
	{0x3FFC, 0xC1A85F1CC75E3EA5ull},
	{0x3FFC, 0xC95D1BE828138DE6ull},
	{0x3FFC, 0xD10BF300840D2DE4ull},
	{0x3FFC, 0xD8B4B2BA6BC05E7Aull},
	{0x3FFC, 0xE0572A6BB42335F6ull},
	{0x3FFC, 0xE7F32A70EA9CAA8Full},
	{0x3FFC, 0xEF88843264ECEFAAull},
	{0x3FFC, 0xF7170A28ECC06666ull},
	{0x3FFD, 0x812FD288332DAD32ull},
	{0x3FFD, 0x88A8D1B1218E4D64ull},
	{0x3FFD, 0x9012AB3F23E4AEE8ull},
	{0x3FFD, 0x976CC3D411E7F1B9ull},
	{0x3FFD, 0x9EB689493889A227ull},
	{0x3FFD, 0xA5EF72C34487361Bull},
	{0x3FFD, 0xAD1700BAF07A7227ull},
	{0x3FFD, 0xB42CBCFAFD37EFB7ull},
	{0x3FFD, 0xBB303A940BA80F89ull},
	{0x3FFD, 0xC22115C6FCAEBBAFull},
	{0x3FFD, 0xC8FEF3E686331221ull},
	{0x3FFD, 0xCFC98330B4000C70ull},
	{0x3FFD, 0xD6807AA1102C5BF9ull},
	{0x3FFD, 0xDD2399BC31252AA3ull},
	{0x3FFD, 0xE3B2A8556B8FC517ull},
	{0x3FFD, 0xEA2D764F64315989ull},
	{0x3FFD, 0xF3BF5BF8BAD1A21Dull},
	{0x3FFE, 0x801CE39E0D205C9Aull},
	{0x3FFE, 0x8630A2DADA1ED066ull},
	{0x3FFE, 0x8C1AD445F3E09B8Cull},
	{0x3FFE, 0x91DB8F1664F350E2ull},
	{0x3FFE, 0x97731420365E538Cull},
	{0x3FFE, 0x9CE1C8E6A0B8CDBAull},
	{0x3FFE, 0xA22832DBCADAAE09ull},
	{0x3FFE, 0xA746F2DDB7602294ull},
	{0x3FFE, 0xAC3EC0FB997DD6A2ull},
	{0x3FFE, 0xB110688AEBDC6F6Aull},
	{0x3FFE, 0xB5BCC49059ECC4B0ull},
	{0x3FFE, 0xBA44BC7DD470782Full},
	{0x3FFE, 0xBEA94144FD049AACull},
	{0x3FFE, 0xC2EB4ABB661628B6ull},
	{0x3FFE, 0xC70BD54CE602EE14ull},
	{0x3FFE, 0xCD000549ADEC7159ull},
	{0x3FFE, 0xD48457D2D8EA4EA3ull},
	{0x3FFE, 0xDB948DA712DECE3Bull},
	{0x3FFE, 0xE23855F969E8096Aull},
	{0x3FFE, 0xE8771129C4353259ull},
	{0x3FFE, 0xEE57C16E0D379C0Dull},
	{0x3FFE, 0xF3E10211A87C3779ull},
	{0x3FFE, 0xF919039D758B8D41ull},
	{0x3FFE, 0xFE058B8F64935FB3ull},
	{0x3FFF, 0x8155FB497B685D04ull},
	{0x3FFF, 0x83889E3549D108E1ull},
	{0x3FFF, 0x859CFA76511D724Bull},
	{0x3FFF, 0x87952ECFFF8131E7ull},
	{0x3FFF, 0x89732FD19557641Bull},
	{0x3FFF, 0x8B38CAD101932A35ull},
	{0x3FFF, 0x8CE7A8D8301EE6B5ull},
	{0x3FFF, 0x8F46A39E2EAE5281ull},
	{0x3FFF, 0x922DA7D791888487ull},
	{0x3FFF, 0x94D19FCBDEDF5241ull},
	{0x3FFF, 0x973AB94419D2A08Bull},
	{0x3FFF, 0x996FF00E08E10B96ull},
	{0x3FFF, 0x9B773F9512321DA7ull},
	{0x3FFF, 0x9D55CC320F935624ull},
	{0x3FFF, 0x9F100575006CC571ull},
	{0x3FFF, 0xA0A9C290D97CC06Cull},
	{0x3FFF, 0xA22659EBEBC0630Aull},
	{0x3FFF, 0xA388B4AFF6EF0EC9ull},
	{0x3FFF, 0xA4D35F1061D292C4ull},
	{0x3FFF, 0xA60895DCFBE3187Eull},
	{0x3FFF, 0xA72A51DC7367BEACull},
	{0x3FFF, 0xA83A51530956168Full},
	{0x3FFF, 0xA93A20077539546Eull},
	{0x3FFF, 0xAA9E7245023B2605ull},
	{0x3FFF, 0xAC4C84BA6FE4D58Full},
	{0x3FFF, 0xADCE4A4A606B9712ull},
	{0x3FFF, 0xAF2A2DCD8D263C9Cull},
	{0x3FFF, 0xB0656F81F22265C7ull},
	{0x3FFF, 0xB18465150F71496Aull},
	{0x3FFF, 0xB28AAA156F9ADA35ull},
	{0x3FFF, 0xB37B44FF3766B895ull},
	{0x3FFF, 0xB458C3DCE9630433ull},
	{0x3FFF, 0xB525529D562246BDull},
	{0x3FFF, 0xB5E2CCA95F9D88CCull},
	{0x3FFF, 0xB692CADA7ACA1ADAull},
	{0x3FFF, 0xB736AEA7A6925838ull},
	{0x3FFF, 0xB7CFAB287E9F7B36ull},
	{0x3FFF, 0xB85ECC66CB219835ull},
	{0x3FFF, 0xB8E4FD5A20A593DAull},
	{0x3FFF, 0xB99F41F64AFF9BB5ull},
	{0x3FFF, 0xBA7F1E17842BBE7Bull},
	{0x3FFF, 0xBB4712857637E17Dull},
	{0x3FFF, 0xBBFABE8A4788DF6Full},
	{0x3FFF, 0xBC9D0FAD2B689D79ull},
	{0x3FFF, 0xBD306A39471ECD86ull},
	{0x3FFF, 0xBDB6C731856AF18Aull},
	{0x3FFF, 0xBE31CAC502E80D70ull},
	{0x3FFF, 0xBEA2D55CE33194E2ull},
	{0x3FFF, 0xBF0B10B7C03128F0ull},
	{0x3FFF, 0xBF6B7A18DACB778Dull},
	{0x3FFF, 0xBFC4EA4663FA18F6ull},
	{0x3FFF, 0xC0181BDE8B89A454ull},
	{0x3FFF, 0xC065B066CFBF6439ull},
	{0x3FFF, 0xC0AE345F56340AE6ull},
	{0x3FFF, 0xC0F222919CB9E6A7ull}
};


inline constexpr f80 exp_tbl[64] = {
	{0x3FFF, 0x8000000000000000ull},
	{0x3FFF, 0x8164D1F3BC030774ull},
	{0x3FFF, 0x82CD8698AC2BA1D8ull},
	{0x3FFF, 0x843A28C3ACDE4048ull},
	{0x3FFF, 0x85AAC367CC487B14ull},
	{0x3FFF, 0x871F61969E8D1010ull},
	{0x3FFF, 0x88980E8092DA8528ull},
	{0x3FFF, 0x8A14D575496EFD9Cull},
	{0x3FFF, 0x8B95C1E3EA8BD6E8ull},
	{0x3FFF, 0x8D1ADF5B7E5BA9E4ull},
	{0x3FFF, 0x8EA4398B45CD53C0ull},
	{0x3FFF, 0x9031DC431466B1DCull},
	{0x3FFF, 0x91C3D373AB11C338ull},
	{0x3FFF, 0x935A2B2F13E6E92Cull},
	{0x3FFF, 0x94F4EFA8FEF70960ull},
	{0x3FFF, 0x96942D3720185A00ull},
	{0x3FFF, 0x9837F0518DB8A970ull},
	{0x3FFF, 0x99E0459320B7FA64ull},
	{0x3FFF, 0x9B8D39B9D54E5538ull},
	{0x3FFF, 0x9D3ED9A72CFFB750ull},
	{0x3FFF, 0x9EF5326091A111ACull},
	{0x3FFF, 0xA0B0510FB9714FC4ull},
	{0x3FFF, 0xA27043030C496818ull},
	{0x3FFF, 0xA43515AE09E680A0ull},
	{0x3FFF, 0xA5FED6A9B15138ECull},
	{0x3FFF, 0xA7CD93B4E9653568ull},
	{0x3FFF, 0xA9A15AB4EA7C0EF8ull},
	{0x3FFF, 0xAB7A39B5A93ED338ull},
	{0x3FFF, 0xAD583EEA42A14AC8ull},
	{0x3FFF, 0xAF3B78AD690A4374ull},
	{0x3FFF, 0xB123F581D2AC2590ull},
	{0x3FFF, 0xB311C412A9112488ull},
	{0x3FFF, 0xB504F333F9DE6484ull},
	{0x3FFF, 0xB6FD91E328D17790ull},
	{0x3FFF, 0xB8FBAF4762FB9EE8ull},
	{0x3FFF, 0xBAFF5AB2133E45FCull},
	{0x3FFF, 0xBD08A39F580C36C0ull},
	{0x3FFF, 0xBF1799B67A731084ull},
	{0x3FFF, 0xC12C4CCA66709458ull},
	{0x3FFF, 0xC346CCDA24976408ull},
	{0x3FFF, 0xC5672A115506DADCull},
	{0x3FFF, 0xC78D74C8ABB9B15Cull},
	{0x3FFF, 0xC9B9BD866E2F27A4ull},
	{0x3FFF, 0xCBEC14FEF2727C5Cull},
	{0x3FFF, 0xCE248C151F8480E4ull},
	{0x3FFF, 0xD06333DAEF2B2594ull},
	{0x3FFF, 0xD2A81D91F12AE45Cull},
	{0x3FFF, 0xD4F35AABCFEDFA20ull},
	{0x3FFF, 0xD744FCCAD69D6AF4ull},
	{0x3FFF, 0xD99D15C278AFD7B4ull},
	{0x3FFF, 0xDBFBB797DAF23754ull},
	{0x3FFF, 0xDE60F4825E0E9124ull},
	{0x3FFF, 0xE0CCDEEC2A94E110ull},
	{0x3FFF, 0xE33F8972BE8A5A50ull},
	{0x3FFF, 0xE5B906E77C8348A8ull},
	{0x3FFF, 0xE8396A503C4BDC68ull},
	{0x3FFF, 0xEAC0C6E7DD243930ull},
	{0x3FFF, 0xED4F301ED9942B84ull},
	{0x3FFF, 0xEFE4B99BDCDAF5CCull},
	{0x3FFF, 0xF281773C59FFB138ull},
	{0x3FFF, 0xF5257D152486CC2Cull},
	{0x3FFF, 0xF7D0DF730AD13BB8ull},
	{0x3FFF, 0xFA83B2DB722A033Cull},
	{0x3FFF, 0xFD3E0C0CF486C174ull}
};

inline constexpr uint32_t exp_tbl2[64] = {
	0x00000000, 0x9F841A9B, 0x9FC1D5B9, 0xA0728369,
	0x1FC5C95C, 0x1EE85C9F, 0x9FA20729, 0xA07BF9AF,
	0xA0020DCF, 0x205A63DA, 0x1EB70051, 0x1F6EB029,
	0xA0781494, 0x9EB319B0, 0x2017457D, 0x1F11D537,
	0x9FB952DD, 0x1FE43087, 0x1FA2A818, 0x1FDE494D,
	0x20504890, 0xA073691C, 0x1F9B7A05, 0xA0797126,
	0xA071A140, 0x204F62DA, 0x1F283C4A, 0x9F9A7FDC,
	0xA05B3FAC, 0x1FDF2610, 0x9F705F90, 0x201F678A,
	0x1F32FB13, 0x20038B30, 0x200DC3CC, 0x9F8B2AE6,
	0xA02BBF70, 0xA00BF518, 0xA041DD41, 0x9FDF137B,
	0x201F1568, 0x1FC13A2E, 0xA03F8F03, 0x1FF4907D,
	0x9E6E53E4, 0x1FD6D45C, 0xA076EDB9, 0x9FA6DE21,
	0x1EE69A2F, 0x207F439F, 0x201EC207, 0x9E8BE175,
	0x20032C4B, 0x2004DFF5, 0x1E72F47A, 0x1F722F22,
	0xA017E945, 0x1F401A5B, 0x9FB9A9E3, 0x20744C05,
	0x1F773A19, 0x1FFE90D5, 0xA041ED22, 0x1F853F3A
};


inline constexpr f80 exp2_tbl[64] = {
	{0x3FFF, 0x8000000000000000ull},
	{0x3FFF, 0x8164D1F3BC030773ull},
	{0x3FFF, 0x82CD8698AC2BA1D7ull},
	{0x3FFF, 0x843A28C3ACDE4046ull},
	{0x3FFF, 0x85AAC367CC487B15ull},
	{0x3FFF, 0x871F61969E8D1010ull},
	{0x3FFF, 0x88980E8092DA8527ull},
	{0x3FFF, 0x8A14D575496EFD9Aull},
	{0x3FFF, 0x8B95C1E3EA8BD6E7ull},
	{0x3FFF, 0x8D1ADF5B7E5BA9E6ull},
	{0x3FFF, 0x8EA4398B45CD53C0ull},
	{0x3FFF, 0x9031DC431466B1DCull},
	{0x3FFF, 0x91C3D373AB11C336ull},
	{0x3FFF, 0x935A2B2F13E6E92Cull},
	{0x3FFF, 0x94F4EFA8FEF70961ull},
	{0x3FFF, 0x96942D3720185A00ull},
	{0x3FFF, 0x9837F0518DB8A96Full},
	{0x3FFF, 0x99E0459320B7FA65ull},
	{0x3FFF, 0x9B8D39B9D54E5539ull},
	{0x3FFF, 0x9D3ED9A72CFFB751ull},
	{0x3FFF, 0x9EF5326091A111AEull},
	{0x3FFF, 0xA0B0510FB9714FC2ull},
	{0x3FFF, 0xA27043030C496819ull},
	{0x3FFF, 0xA43515AE09E6809Eull},
	{0x3FFF, 0xA5FED6A9B15138EAull},
	{0x3FFF, 0xA7CD93B4E965356Aull},
	{0x3FFF, 0xA9A15AB4EA7C0EF8ull},
	{0x3FFF, 0xAB7A39B5A93ED337ull},
	{0x3FFF, 0xAD583EEA42A14AC6ull},
	{0x3FFF, 0xAF3B78AD690A4375ull},
	{0x3FFF, 0xB123F581D2AC2590ull},
	{0x3FFF, 0xB311C412A9112489ull},
	{0x3FFF, 0xB504F333F9DE6484ull},
	{0x3FFF, 0xB6FD91E328D17791ull},
	{0x3FFF, 0xB8FBAF4762FB9EE9ull},
	{0x3FFF, 0xBAFF5AB2133E45FBull},
	{0x3FFF, 0xBD08A39F580C36BFull},
	{0x3FFF, 0xBF1799B67A731083ull},
	{0x3FFF, 0xC12C4CCA66709456ull},
	{0x3FFF, 0xC346CCDA24976407ull},
	{0x3FFF, 0xC5672A115506DADDull},
	{0x3FFF, 0xC78D74C8ABB9B15Dull},
	{0x3FFF, 0xC9B9BD866E2F27A3ull},
	{0x3FFF, 0xCBEC14FEF2727C5Dull},
	{0x3FFF, 0xCE248C151F8480E4ull},
	{0x3FFF, 0xD06333DAEF2B2595ull},
	{0x3FFF, 0xD2A81D91F12AE45Aull},
	{0x3FFF, 0xD4F35AABCFEDFA1Full},
	{0x3FFF, 0xD744FCCAD69D6AF4ull},
	{0x3FFF, 0xD99D15C278AFD7B6ull},
	{0x3FFF, 0xDBFBB797DAF23755ull},
	{0x3FFF, 0xDE60F4825E0E9124ull},
	{0x3FFF, 0xE0CCDEEC2A94E111ull},
	{0x3FFF, 0xE33F8972BE8A5A51ull},
	{0x3FFF, 0xE5B906E77C8348A8ull},
	{0x3FFF, 0xE8396A503C4BDC68ull},
	{0x3FFF, 0xEAC0C6E7DD24392Full},
	{0x3FFF, 0xED4F301ED9942B84ull},
	{0x3FFF, 0xEFE4B99BDCDAF5CBull},
	{0x3FFF, 0xF281773C59FFB13Aull},
	{0x3FFF, 0xF5257D152486CC2Cull},
	{0x3FFF, 0xF7D0DF730AD13BB9ull},
	{0x3FFF, 0xFA83B2DB722A033Aull},
	{0x3FFF, 0xFD3E0C0CF486C175ull}
};


inline constexpr uint32_t exp2_tbl2[64] = {
	0x3F738000, 0x3FBEF7CA, 0x3FBDF8A9, 0x3FBCD7C9,
	0xBFBDE8DA, 0x3FBDE85C, 0x3FBEBBF1, 0x3FBB80CA,
	0xBFBA8373, 0xBFBE9670, 0x3FBDB700, 0x3FBEEEB0,
	0x3FBBFD6D, 0xBFBDB319, 0x3FBDBA2B, 0x3FBE91D5,
	0x3FBE8D5A, 0xBFBCDE7B, 0xBFBEBAAF, 0xBFBD86DA,
	0xBFBEBEDD, 0x3FBCC96E, 0xBFBEC90B, 0x3FBBD1DB,
	0x3FBCE5EB, 0xBFBEC274, 0x3FBEA83C, 0x3FBECB00,
	0x3FBE9301, 0xBFBD8367, 0xBFBEF05F, 0x3FBDFB3C,
	0x3FBEB2FB, 0x3FBAE2CB, 0x3FBCDC3C, 0x3FBEE9AA,
	0xBFBEAEFD, 0xBFBCBF51, 0x3FBEF88A, 0x3FBD83B2,
	0x3FBDF8AB, 0xBFBDFB17, 0xBFBEFE3C, 0xBFBBB6F8,
	0xBFBCEE53, 0xBFBDA4AE, 0x3FBC9124, 0x3FBEB243,
	0x3FBDE69A, 0xBFB8BC61, 0x3FBDF610, 0xBFBD8BE1,
	0x3FBACB12, 0x3FBB9BFE, 0x3FBCF2F4, 0x3FBEF22F,
	0xBFBDBF4A, 0x3FBEC01A, 0x3FBE8CAC, 0xBFBCBB3F,
	0x3FBEF73A, 0xBFB8B795, 0x3FBEF84B, 0xBFBEF581
};


inline constexpr f80 log_tbl[128] = {
	{0x3FFE, 0xFE03F80FE03F80FEull},
	{0x3FF7, 0xFF015358833C47E2ull},
	{0x3FFE, 0xFA232CF252138AC0ull},
	{0x3FF9, 0xBDC8D83EAD88D549ull},
	{0x3FFE, 0xF6603D980F6603DAull},
	{0x3FFA, 0x9CF43DCFF5EAFD48ull},
	{0x3FFE, 0xF2B9D6480F2B9D65ull},
	{0x3FFA, 0xDA16EB88CB8DF614ull},
	{0x3FFE, 0xEF2EB71FC4345238ull},
	{0x3FFB, 0x8B29B7751BD70743ull},
	{0x3FFE, 0xEBBDB2A5C1619C8Cull},
	{0x3FFB, 0xA8D839F830C1FB49ull},
	{0x3FFE, 0xE865AC7B7603A197ull},
	{0x3FFB, 0xC61A2EB18CD907ADull},
	{0x3FFE, 0xE525982AF70C880Eull},
	{0x3FFB, 0xE2F2A47ADE3A18AFull},
	{0x3FFE, 0xE1FC780E1FC780E2ull},
	{0x3FFB, 0xFF64898EDF55D551ull},
	{0x3FFE, 0xDEE95C4CA037BA57ull},
	{0x3FFC, 0x8DB956A97B3D0148ull},
	{0x3FFE, 0xDBEB61EED19C5958ull},
	{0x3FFC, 0x9B8FE100F47BA1DEull},
	{0x3FFE, 0xD901B2036406C80Eull},
	{0x3FFC, 0xA9372F1D0DA1BD17ull},
	{0x3FFE, 0xD62B80D62B80D62Cull},
	{0x3FFC, 0xB6B07F38CE90E46Bull},
	{0x3FFE, 0xD3680D3680D3680Dull},
	{0x3FFC, 0xC3FD032906488481ull},
	{0x3FFE, 0xD0B69FCBD2580D0Bull},
	{0x3FFC, 0xD11DE0FF15AB18CAull},
	{0x3FFE, 0xCE168A7725080CE1ull},
	{0x3FFC, 0xDE1433A16C66B150ull},
	{0x3FFE, 0xCB8727C065C393E0ull},
	{0x3FFC, 0xEAE10B5A7DDC8ADDull},
	{0x3FFE, 0xC907DA4E871146ADull},
	{0x3FFC, 0xF7856E5EE2C9B291ull},
	{0x3FFE, 0xC6980C6980C6980Cull},
	{0x3FFD, 0x82012CA5A68206D7ull},
	{0x3FFE, 0xC4372F855D824CA6ull},
	{0x3FFD, 0x882C5FCD7256A8C5ull},
	{0x3FFE, 0xC1E4BBD595F6E947ull},
	{0x3FFD, 0x8E44C60B4CCFD7DEull},
	{0x3FFE, 0xBFA02FE80BFA02FFull},
	{0x3FFD, 0x944AD09EF4351AF6ull},
	{0x3FFE, 0xBD69104707661AA3ull},
	{0x3FFD, 0x9A3EECD4C3EAA6B2ull},
	{0x3FFE, 0xBB3EE721A54D880Cull},
	{0x3FFD, 0xA0218434353F1DE8ull},
	{0x3FFE, 0xB92143FA36F5E02Eull},
	{0x3FFD, 0xA5F2FCABBBC506DAull},
	{0x3FFE, 0xB70FBB5A19BE3659ull},
	{0x3FFD, 0xABB3B8BA2AD362A5ull},
	{0x3FFE, 0xB509E68A9B94821Full},
	{0x3FFD, 0xB1641795CE3CA97Bull},
	{0x3FFE, 0xB30F63528917C80Bull},
	{0x3FFD, 0xB70475515D0F1C61ull},
	{0x3FFE, 0xB11FD3B80B11FD3Cull},
	{0x3FFD, 0xBC952AFEEA3D13E1ull},
	{0x3FFE, 0xAF3ADDC680AF3ADEull},
	{0x3FFD, 0xC2168ED0F458BA4Aull},
	{0x3FFE, 0xAD602B580AD602B6ull},
	{0x3FFD, 0xC788F439B3163BF1ull},
	{0x3FFE, 0xAB8F69E28359CD11ull},
	{0x3FFD, 0xCCECAC08BF04565Dull},
	{0x3FFE, 0xA9C84A47A07F5638ull},
	{0x3FFD, 0xD24204872DD85160ull},
	{0x3FFE, 0xA80A80A80A80A80Bull},
	{0x3FFD, 0xD78949923BC3588Aull},
	{0x3FFE, 0xA655C4392D7B73A8ull},
	{0x3FFD, 0xDCC2C4B49887DACCull},
	{0x3FFE, 0xA4A9CF1D96833751ull},
	{0x3FFD, 0xE1EEBD3E6D6A6B9Eull},
	{0x3FFE, 0xA3065E3FAE7CD0E0ull},
	{0x3FFD, 0xE70D785C2F9F5BDCull},
	{0x3FFE, 0xA16B312EA8FC377Dull},
	{0x3FFD, 0xEC1F392C5179F283ull},
	{0x3FFE, 0x9FD809FD809FD80Aull},
	{0x3FFD, 0xF12440D3E36130E6ull},
	{0x3FFE, 0x9E4CAD23DD5F3A20ull},
	{0x3FFD, 0xF61CCE92346600BBull},
	{0x3FFE, 0x9CC8E160C3FB19B9ull},
	{0x3FFD, 0xFB091FD38145630Aull},
	{0x3FFE, 0x9B4C6F9EF03A3CAAull},
	{0x3FFD, 0xFFE97042BFA4C2ADull},
	{0x3FFE, 0x99D722DABDE58F06ull},
	{0x3FFE, 0x825EFCED49369330ull},
	{0x3FFE, 0x9868C809868C8098ull},
	{0x3FFE, 0x84C37A7AB9A905C9ull},
	{0x3FFE, 0x97012E025C04B809ull},
	{0x3FFE, 0x87224C2E8E645FB7ull},
	{0x3FFE, 0x95A02568095A0257ull},
	{0x3FFE, 0x897B8CAC9F7DE298ull},
	{0x3FFE, 0x9445809445809446ull},
	{0x3FFE, 0x8BCF55DEC4CD05FEull},
	{0x3FFE, 0x92F113840497889Cull},
	{0x3FFE, 0x8E1DC0FB89E125E5ull},
	{0x3FFE, 0x91A2B3C4D5E6F809ull},
	{0x3FFE, 0x9066E68C955B6C9Bull},
	{0x3FFE, 0x905A38633E06C43Bull},
	{0x3FFE, 0x92AADE74C7BE59E0ull},
	{0x3FFE, 0x8F1779D9FDC3A219ull},
	{0x3FFE, 0x94E9BFF615845643ull},
	{0x3FFE, 0x8DDA520237694809ull},
	{0x3FFE, 0x9723A1B720134203ull},
	{0x3FFE, 0x8CA29C046514E023ull},
	{0x3FFE, 0x995899C890EB8990ull},
	{0x3FFE, 0x8B70344A139BC75Aull},
	{0x3FFE, 0x9B88BDAA3A3DAE2Full},
	{0x3FFE, 0x8A42F8705669DB46ull},
	{0x3FFE, 0x9DB4224FFFE1157Cull},
	{0x3FFE, 0x891AC73AE9819B50ull},
	{0x3FFE, 0x9FDADC268B7A12DAull},
	{0x3FFE, 0x87F78087F78087F8ull},
	{0x3FFE, 0xA1FCFF17CE733BD4ull},
	{0x3FFE, 0x86D905447A34ACC6ull},
	{0x3FFE, 0xA41A9E8F5446FB9Full},
	{0x3FFE, 0x85BF37612CEE3C9Bull},
	{0x3FFE, 0xA633CD7E6771CD8Bull},
	{0x3FFE, 0x84A9F9C8084A9F9Dull},
	{0x3FFE, 0xA8489E600B435A5Eull},
	{0x3FFE, 0x839930523FBE3368ull},
	{0x3FFE, 0xAA59233CCCA4BD49ull},
	{0x3FFE, 0x828CBFBEB9A020A3ull},
	{0x3FFE, 0xAC656DAE6BCC4985ull},
	{0x3FFE, 0x81848DA8FAF0D277ull},
	{0x3FFE, 0xAE6D8EE360BB2468ull},
	{0x3FFE, 0x8080808080808081ull},
	{0x3FFE, 0xB07197A23C46C654ull}
};


inline constexpr f80 pi_tbl[65] = {
	{0xC004, 0xC90FDAA22168C235ull},
	{0xC004, 0xC2C75BCD105D7C23ull},
	{0xC004, 0xBC7EDCF7FF523611ull},
	{0xC004, 0xB6365E22EE46F000ull},
	{0xC004, 0xAFEDDF4DDD3BA9EEull},
	{0xC004, 0xA9A56078CC3063DDull},
	{0xC004, 0xA35CE1A3BB251DCBull},
	{0xC004, 0x9D1462CEAA19D7B9ull},
	{0xC004, 0x96CBE3F9990E91A8ull},
	{0xC004, 0x9083652488034B96ull},
	{0xC004, 0x8A3AE64F76F80584ull},
	{0xC004, 0x83F2677A65ECBF73ull},
	{0xC003, 0xFB53D14AA9C2F2C2ull},
	{0xC003, 0xEEC2D3A087AC669Full},
	{0xC003, 0xE231D5F66595DA7Bull},
	{0xC003, 0xD5A0D84C437F4E58ull},
	{0xC003, 0xC90FDAA22168C235ull},
	{0xC003, 0xBC7EDCF7FF523611ull},
	{0xC003, 0xAFEDDF4DDD3BA9EEull},
	{0xC003, 0xA35CE1A3BB251DCBull},
	{0xC003, 0x96CBE3F9990E91A8ull},
	{0xC003, 0x8A3AE64F76F80584ull},
	{0xC002, 0xFB53D14AA9C2F2C2ull},
	{0xC002, 0xE231D5F66595DA7Bull},
	{0xC002, 0xC90FDAA22168C235ull},
	{0xC002, 0xAFEDDF4DDD3BA9EEull},
	{0xC002, 0x96CBE3F9990E91A8ull},
	{0xC001, 0xFB53D14AA9C2F2C2ull},
	{0xC001, 0xC90FDAA22168C235ull},
	{0xC001, 0x96CBE3F9990E91A8ull},
	{0xC000, 0xC90FDAA22168C235ull},
	{0xBFFF, 0xC90FDAA22168C235ull},
	{0x0000, 0x0000000000000000ull},
	{0x3FFF, 0xC90FDAA22168C235ull},
	{0x4000, 0xC90FDAA22168C235ull},
	{0x4001, 0x96CBE3F9990E91A8ull},
	{0x4001, 0xC90FDAA22168C235ull},
	{0x4001, 0xFB53D14AA9C2F2C2ull},
	{0x4002, 0x96CBE3F9990E91A8ull},
	{0x4002, 0xAFEDDF4DDD3BA9EEull},
	{0x4002, 0xC90FDAA22168C235ull},
	{0x4002, 0xE231D5F66595DA7Bull},
	{0x4002, 0xFB53D14AA9C2F2C2ull},
	{0x4003, 0x8A3AE64F76F80584ull},
	{0x4003, 0x96CBE3F9990E91A8ull},
	{0x4003, 0xA35CE1A3BB251DCBull},
	{0x4003, 0xAFEDDF4DDD3BA9EEull},
	{0x4003, 0xBC7EDCF7FF523611ull},
	{0x4003, 0xC90FDAA22168C235ull},
	{0x4003, 0xD5A0D84C437F4E58ull},
	{0x4003, 0xE231D5F66595DA7Bull},
	{0x4003, 0xEEC2D3A087AC669Full},
	{0x4003, 0xFB53D14AA9C2F2C2ull},
	{0x4004, 0x83F2677A65ECBF73ull},
	{0x4004, 0x8A3AE64F76F80584ull},
	{0x4004, 0x9083652488034B96ull},
	{0x4004, 0x96CBE3F9990E91A8ull},
	{0x4004, 0x9D1462CEAA19D7B9ull},
	{0x4004, 0xA35CE1A3BB251DCBull},
	{0x4004, 0xA9A56078CC3063DDull},
	{0x4004, 0xAFEDDF4DDD3BA9EEull},
	{0x4004, 0xB6365E22EE46F000ull},
	{0x4004, 0xBC7EDCF7FF523611ull},
	{0x4004, 0xC2C75BCD105D7C23ull},
	{0x4004, 0xC90FDAA22168C235ull}
};


inline constexpr uint32_t pi_tbl2[65] = {
	0x21800000, 0xA0D00000, 0xA1E80000, 0x21480000,
	0xA1200000, 0x21FC0000, 0x21100000, 0xA1580000,
	0x21E00000, 0x20B00000, 0xA1880000, 0x21C40000,
	0x20000000, 0x21380000, 0xA1300000, 0x9FC00000,
	0x21000000, 0xA1680000, 0xA0A00000, 0x20900000,
	0x21600000, 0xA1080000, 0x1F800000, 0xA0B00000,
	0x20800000, 0xA0200000, 0x20E00000, 0x1F000000,
	0x20000000, 0x20600000, 0x1F800000, 0x1F000000,
	0x00000000,
	0x9F000000, 0x9F800000, 0xA0600000, 0xA0000000,
	0x9F000000, 0xA0E00000, 0x20200000, 0xA0800000,
	0x20B00000, 0x9F800000, 0x21080000, 0xA1600000,
	0xA0900000, 0x20A00000, 0x21680000, 0xA1000000,
	0x1FC00000, 0x21300000, 0xA1380000, 0xA0000000,
	0xA1C40000, 0x21880000, 0xA0B00000, 0xA1E00000,
	0x21580000, 0xA1100000, 0xA1FC0000, 0x21200000,
	0xA1480000, 0x21E80000, 0x20D00000, 0xA1800000
};

// --- Corps (softfloat_fpsp.c, recopiés à l'identique) ---------------------------

/*----------------------------------------------------------------------------
| Algorithms for transcendental functions supported by MC68881 and MC68882 
| mathematical coprocessors. The functions are derived from FPSP library.
*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 | Function for compactifying extended double-precision floating point values.
 *----------------------------------------------------------------------------*/

inline int32_t floatx80_make_compact(int32_t aExp, uint64_t aSig)
{
	return (aExp<<16)|(aSig>>48);
}


/*----------------------------------------------------------------------------
 | Arc cosine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_acos(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	
	int32_t compact;
	floatx80 fp0, fp1, one;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
		return propagateFloatx80NaNOneArg(a, status);
	}
	if (aExp == 0 && aSig == 0) {
		float_raise(float_flag_inexact, status);
		return roundAndPackFloatx80(status->roundingPrecision, 0, piby2_exp, pi_sig, 0, status);
	}

	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact >= 0x3FFF8000) { // |X| >= 1
		if (aExp == one_exp && aSig == one_sig) { // |X| == 1
			if (aSign) { // X == -1
				a = packFloatx80(0, pi_exp, pi_sig);
				float_raise(float_flag_inexact, status);
				return floatx80_move(a, status);
			} else { // X == +1
				return packFloatx80(0, 0, 0);
			}
		} else { // |X| > 1
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}
	} // |X| < 1
	
	const PrecSave user_prec = setPrec(status);
	
	one = packFloatx80(0, one_exp, one_sig);
	fp0 = a;
	
	fp1 = floatx80_add(one, fp0, status);   // 1 + X
	fp0 = floatx80_sub(one, fp0, status);   // 1 - X
	fp0 = floatx80_div(fp0, fp1, status);   // (1-X)/(1+X)
	fp0 = floatx80_sqrt(fp0, status);       // SQRT((1-X)/(1+X))
	fp0 = floatx80_atan(fp0, status);       // ATAN(SQRT((1-X)/(1+X)))
	
	resetPrec(status, user_prec);
	
	a = floatx80_add(fp0, fp0, status);     // 2 * ATAN(SQRT((1-X)/(1+X)))
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Arc sine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_asin(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1, fp2, one;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
		return propagateFloatx80NaNOneArg(a, status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}

	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact >= 0x3FFF8000) { // |X| >= 1
		if (aExp == one_exp && aSig == one_sig) { // |X| == 1
			float_raise(float_flag_inexact, status);
			a = packFloatx80(aSign, piby2_exp, pi_sig);
			return floatx80_move(a, status);
		} else { // |X| > 1
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}

	} // |X| < 1
	
	const PrecSave user_prec = setPrec(status);
	
	one = packFloatx80(0, one_exp, one_sig);
	fp0 = a;

	fp1 = floatx80_sub(one, fp0, status);   // 1 - X
	fp2 = floatx80_add(one, fp0, status);   // 1 + X
	fp1 = floatx80_mul(fp2, fp1, status);   // (1+X)*(1-X)
	fp1 = floatx80_sqrt(fp1, status);       // SQRT((1+X)*(1-X))
	fp0 = floatx80_div(fp0, fp1, status);   // X/SQRT((1+X)*(1-X))

	resetPrec(status, user_prec);

	a = floatx80_atan(fp0, status);         // ATAN(X/SQRT((1+X)*(1-X)))
	
	float_raise(float_flag_inexact, status);

	return a;
}

/*----------------------------------------------------------------------------
 | Arc tangent
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_atan(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, tbl_index;
	floatx80 fp0, fp1, fp2, fp3, xsave;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		a = packFloatx80(aSign, piby2_exp, pi_sig);
		float_raise(float_flag_inexact, status);
		return floatx80_move(a, status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	compact = floatx80_make_compact(aExp, aSig);
	
	const PrecSave user_prec = setPrec(status);

	if (compact < 0x3FFB8000 || compact > 0x4002FFFF) { // |X| >= 16 or |X| < 1/16
		if (compact > 0x3FFF8000) { // |X| >= 16
			if (compact > 0x40638000) { // |X| > 2^(100)
				fp0 = packFloatx80(aSign, piby2_exp, pi_sig);
				fp1 = packFloatx80(aSign, 0x0001, one_sig);
				
				resetPrec(status, user_prec);
				
				a = floatx80_sub(fp0, fp1, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = a;
				fp1 = packFloatx80(1, one_exp, one_sig); // -1
				fp1 = floatx80_div(fp1, fp0, status); // X' = -1/X
				xsave = fp1;
				fp0 = floatx80_mul(fp1, fp1, status); // Y = X'*X'
				fp1 = floatx80_mul(fp0, fp0, status); // Z = Y*Y
				fp3 = float64_to_floatx80(0xBFB70BF398539E6Aull, status); // C5
				fp2 = float64_to_floatx80(0x3FBC7187962D1D7Dull, status); // C4
				fp3 = floatx80_mul(fp3, fp1, status); // Z*C5
				fp2 = floatx80_mul(fp2, fp1, status); // Z*C4
				fp3 = floatx80_add(fp3, float64_to_floatx80(0xBFC24924827107B8ull, status), status); // C3+Z*C5
				fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FC999999996263Eull, status), status); // C2+Z*C4
				fp1 = floatx80_mul(fp1, fp3, status); // Z*(C3+Z*C5)
				fp2 = floatx80_mul(fp2, fp0, status); // Y*(C2+Z*C4)
				fp1 = floatx80_add(fp1, float64_to_floatx80(0xBFD5555555555536ull, status), status); // C1+Z*(C3+Z*C5)
				fp0 = floatx80_mul(fp0, xsave, status); // X'*Y
				fp1 = floatx80_add(fp1, fp2, status); // [Y*(C2+Z*C4)]+[C1+Z*(C3+Z*C5)]
				fp0 = floatx80_mul(fp0, fp1, status); // X'*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]) ??
				fp0 = floatx80_add(fp0, xsave, status);
				fp1 = packFloatx80(aSign, piby2_exp, pi_sig);
				
				resetPrec(status, user_prec);

				a = floatx80_add(fp0, fp1, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			}
		} else { // |X| < 1/16
			if (compact < 0x3FD78000) { // |X| < 2^(-40)
				resetPrec(status, user_prec);
				
				a = floatx80_move(a, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = a;
				xsave = a;
				fp0 = floatx80_mul(fp0, fp0, status); // Y = X*X
				fp1 = floatx80_mul(fp0, fp0, status); // Z = Y*Y
				fp2 = float64_to_floatx80(0x3FB344447F876989ull, status); // B6
				fp3 = float64_to_floatx80(0xBFB744EE7FAF45DBull, status); // B5
				fp2 = floatx80_mul(fp2, fp1, status); // Z*B6
				fp3 = floatx80_mul(fp3, fp1, status); // Z*B5
				fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FBC71C646940220ull, status), status); // B4+Z*B6
				fp3 = floatx80_add(fp3, float64_to_floatx80(0xBFC24924921872F9ull, status), status); // B3+Z*B5
				fp2 = floatx80_mul(fp2, fp1, status); // Z*(B4+Z*B6)
				fp1 = floatx80_mul(fp1, fp3, status); // Z*(B3+Z*B5)
				fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FC9999999998FA9ull, status), status); // B2+Z*(B4+Z*B6)
				fp1 = floatx80_add(fp1, float64_to_floatx80(0xBFD5555555555555ull, status), status); // B1+Z*(B3+Z*B5)
				fp2 = floatx80_mul(fp2, fp0, status); // Y*(B2+Z*(B4+Z*B6))
				fp0 = floatx80_mul(fp0, xsave, status); // X*Y
				fp1 = floatx80_add(fp1, fp2, status); // [B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]
				fp0 = floatx80_mul(fp0, fp1, status); // X*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))])
				
				resetPrec(status, user_prec);
				
				a = floatx80_add(fp0, xsave, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			}
		}
	} else {
		aSig &= 0xF800000000000000ull;
		aSig |= 0x0400000000000000ull;
		xsave = packFloatx80(aSign, aExp, aSig); // F
		fp0 = a;
		fp1 = a; // X
		fp2 = packFloatx80(0, one_exp, one_sig); // 1
		fp1 = floatx80_mul(fp1, xsave, status); // X*F
		fp0 = floatx80_sub(fp0, xsave, status); // X-F
		fp1 = floatx80_add(fp1, fp2, status); // 1 + X*F
		fp0 = floatx80_div(fp0, fp1, status); // U = (X-F)/(1+X*F)
		
		tbl_index = compact;
		
		tbl_index &= 0x7FFF0000;
		tbl_index -= 0x3FFB0000;
		tbl_index >>= 1;
		tbl_index += compact&0x00007800;
		tbl_index >>= 11;
		
		fp3 = atan_tbl[tbl_index];
		
		fp3.high |= aSign ? 0x8000 : 0; // ATAN(F)
		
		fp1 = floatx80_mul(fp0, fp0, status); // V = U*U
		fp2 = float64_to_floatx80(0xBFF6687E314987D8ull, status); // A3
		fp2 = floatx80_add(fp2, fp1, status); // A3+V
		fp2 = floatx80_mul(fp2, fp1, status); // V*(A3+V)
		fp1 = floatx80_mul(fp1, fp0, status); // U*V
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x4002AC6934A26DB3ull, status), status); // A2+V*(A3+V)
		fp1 = floatx80_mul(fp1, float64_to_floatx80(0xBFC2476F4E1DA28Eull, status), status); // A1+U*V
		fp1 = floatx80_mul(fp1, fp2, status); // A1*U*V*(A2+V*(A3+V))
		fp0 = floatx80_add(fp0, fp1, status); // ATAN(U)
		
		resetPrec(status, user_prec);
		
		a = floatx80_add(fp0, fp3, status); // ATAN(X)
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic arc tangent
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_atanh(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1, fp2, one;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF && (uint64_t) (aSig<<1)) {
		return propagateFloatx80NaNOneArg(a, status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact >= 0x3FFF8000) { // |X| >= 1
		if (aExp == one_exp && aSig == one_sig) { // |X| == 1
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
		} else { // |X| > 1
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}
	} // |X| < 1
	
	const PrecSave user_prec = setPrec(status);
	
	one = packFloatx80(0, one_exp, one_sig);
	fp2 = packFloatx80(aSign, 0x3FFE, one_sig); // SIGN(X) * (1/2)
	fp0 = packFloatx80(0, aExp, aSig); // Y = |X|
	fp1 = packFloatx80(1, aExp, aSig); // -Y
	fp0 = floatx80_add(fp0, fp0, status); // 2Y
	fp1 = floatx80_add(fp1, one, status); // 1-Y
	fp0 = floatx80_div(fp0, fp1, status); // Z = 2Y/(1-Y)
	fp0 = floatx80_lognp1(fp0, status); // LOG1P(Z)
	
	resetPrec(status, user_prec);
	
	a = floatx80_mul(fp0, fp2, status); // ATANH(X) = SIGN(X) * (1/2) * LOG1P(Z)
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Cosine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_cos(floatx80 a, float_status *status)
{
	flag aSign, xSign;
	int32_t aExp, xExp;
	uint64_t aSig, xSig;
	
	int32_t compact, l, n, j;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
	float32 posneg1, twoto63;
	flag adjn, endflag;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	adjn = 1;
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, 0xC90FDAA200000000ull);
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, 0x85A308D300000000ull);
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
			}
		loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, 0xA2F9836E4E44152Aull); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, 0xC90FDAA200000000ull);
			twopi2 = packFloatx80(0, 0x3FDD + l, 0x85A308D300000000ull);
			
			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL
			
			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto sincont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			// SINSM
			fp0 = float32_to_floatx80(0x3F800000, status); // 1
			
			resetPrec(status, user_prec);
			
			if (adjn) {
				// COSTINY
				a = floatx80_sub(fp0, float32_to_floatx80(0x00800000, status), status);
			} else {
				// SINTINY
				a = floatx80_move(a, status);
			}
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(0x3FE45F306DC9C883ull, status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		j = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[j], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status), status); // FP0 IS R = (X-Y1)-Y2
		
	sincont:
		if ((n + adjn) & 1) {
			// COSPOLY
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp2 = float64_to_floatx80(0x3D2AC4D0D6011EE3ull, status); // B8
			fp3 = float64_to_floatx80(0xBDA9396F9F45AC19ull, status); // B7
			
			xSign = extractFloatx80Sign(fp0); // X IS S
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			if (((n + adjn) >> 1) & 1) {
				xSign ^= 1;
				posneg1 = 0xBF800000; // -1
			} else {
				xSign ^= 0;
				posneg1 = 0x3F800000; // 1
			} // X IS NOW R'= SGN*R
			
			fp2 = floatx80_mul(fp2, fp1, status); // TB8
			fp3 = floatx80_mul(fp3, fp1, status); // TB7
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3E21EED90612C972ull, status), status); // B6+TB8
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBE927E4FB79D9FCFull, status), status); // B5+TB7
			fp2 = floatx80_mul(fp2, fp1, status); // T(B6+TB8)
			fp3 = floatx80_mul(fp3, fp1, status); // T(B5+TB7)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EFA01A01A01D423ull, status), status); // B4+T(B6+TB8)
			fp4 = packFloatx80(1, 0x3FF5, 0xB60B60B60B61D438ull);
			fp3 = floatx80_add(fp3, fp4, status); // B3+T(B5+TB7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(B4+T(B6+TB8))
			fp1 = floatx80_mul(fp1, fp3, status); // T(B3+T(B5+TB7))
			fp4 = packFloatx80(0, 0x3FFA, 0xAAAAAAAAAAAAAB5Eull);
			fp2 = floatx80_add(fp2, fp4, status); // B2+T(B4+T(B6+TB8))
			fp1 = floatx80_add(fp1, float32_to_floatx80(0xBF000000, status), status); // B1+T(B3+T(B5+TB7))
			fp0 = floatx80_mul(fp0, fp2, status); // S(B2+T(B4+T(B6+TB8)))
			fp0 = floatx80_add(fp0, fp1, status); // [B1+T(B3+T(B5+TB7))]+[S(B2+T(B4+T(B6+TB8)))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status);
			
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			// SINPOLY
			xSign = extractFloatx80Sign(fp0); // X IS R
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			xSign ^= ((n + adjn) >> 1) & 1; // X IS NOW R'= SGN*R
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp3 = float64_to_floatx80(0xBD6AAA77CCC994F5ull, status); // A7
			fp2 = float64_to_floatx80(0x3DE612097AAE8DA1ull, status); // A6
			fp3 = floatx80_mul(fp3, fp1, status); // T*A7
			fp2 = floatx80_mul(fp2, fp1, status); // T*A6
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBE5AE6452A118AE4ull, status), status); // A5+T*A7
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EC71DE3A5341531ull, status), status); // A4+T*A6
			fp3 = floatx80_mul(fp3, fp1, status); // T(A5+TA7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(A4+TA6)
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBF2A01A01A018B59ull, status), status); // A3+T(A5+TA7)
			fp4 = packFloatx80(0, 0x3FF8, 0x88888888888859AFull);
			fp2 = floatx80_add(fp2, fp4, status); // A2+T(A4+TA6)
			fp1 = floatx80_mul(fp1, fp3, status); // T(A3+T(A5+TA7))
			fp2 = floatx80_mul(fp2, fp0, status); // S(A2+T(A4+TA6))
			fp4 = packFloatx80(1, 0x3FFC, 0xAAAAAAAAAAAAAA99ull);
			fp1 = floatx80_add(fp1, fp4, status); // A1+T(A3+T(A5+TA7))
			fp1 = floatx80_add(fp1, fp2, status); // [A1+T(A3+T(A5+TA7))]+[S(A2+T(A4+TA6))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status); // R'*S
			fp0 = floatx80_mul(fp0, fp1, status); // SIN(R')-R'
			
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, x, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic cosine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_cosh(floatx80 a, float_status *status)
{
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		return packFloatx80(0, aExp, aSig);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact > 0x400CB167) {
		if (compact > 0x400CB2B3) {
			resetPrec(status, user_prec);
			a =  roundAndPackFloatx80(status->roundingPrecision, 0, 0x8000, one_sig, 0, status);
			float_raise(float_flag_inexact, status);
			return a;
		} else {
			fp0 = packFloatx80(0, aExp, aSig);
			fp0 = floatx80_sub(fp0, float64_to_floatx80(0x40C62D38D3D64634ull, status), status);
			fp0 = floatx80_sub(fp0, float64_to_floatx80(0x3D6F90AEB1E75CC7ull, status), status);
			fp0 = floatx80_etox(fp0, status);
			fp1 = packFloatx80(0, 0x7FFB, one_sig);
			
			resetPrec(status, user_prec);

			a = floatx80_mul(fp0, fp1, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
	
	fp0 = packFloatx80(0, aExp, aSig); // |X|
	fp0 = floatx80_etox(fp0, status); // EXP(|X|)
	fp0 = floatx80_mul(fp0, float32_to_floatx80(0x3F000000, status), status); // (1/2)*EXP(|X|)
	fp1 = float32_to_floatx80(0x3E800000, status); // 1/4
	fp1 = floatx80_div(fp1, fp0, status); // 1/(2*EXP(|X|))
	
	resetPrec(status, user_prec);
	
	a = floatx80_add(fp0, fp1, status);
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | e to x
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_etox(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, k, m, m1;
	floatx80 fp0, fp1, fp2, fp3, l2, scale, adjscale;
	flag adjflag;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(0, 0, 0);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	if (aExp >= 0x3FBE) { // |X| >= 2^(-65)
		compact = floatx80_make_compact(aExp, aSig);
		
		if (compact < 0x400CB167) { // |X| < 16380 log2
			fp0 = a;
			fp1 = a;
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0x42B8AA3B, status), status); // 64/log2 * X
			adjflag = 0;
			n = floatx80_to_int32(fp0, status); // int(64/log2*X)
			fp0 = int32_to_floatx80(n);
			
			j = n & 0x3F; // J = N mod 64
			m = n / 64; // NOTE: this is really arithmetic right shift by 6
			if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
				m--;
			}
			m += 0x3FFF; // biased exponent of 2^(M)
			
		expcont1:
			fp2 = fp0; // N
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0xBC317218, status), status); // N * L1, L1 = lead(-log2/64)
			l2 = packFloatx80(0, 0x3FDC, 0x82E308654361C4C6ull);
			fp2 = floatx80_mul(fp2, l2, status); // N * L2, L1+L2 = -log2/64
			fp0 = floatx80_add(fp0, fp1, status); // X + N*L1
			fp0 = floatx80_add(fp0, fp2, status); // R
			
			fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp2 = float32_to_floatx80(0x3AB60B70, status); // A5
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 is S*A5
			fp3 = floatx80_mul(float32_to_floatx80(0x3C088895, status), fp1, status); // fp3 is S*A4
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FA5555555554431ull, status), status); // fp2 is A3+S*A5
			fp3 = floatx80_add(fp3, float64_to_floatx80(0x3FC5555555554018ull, status), status); // fp3 is A2+S*A4
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 is S*(A3+S*A5)
			fp3 = floatx80_mul(fp3, fp1, status); // fp3 is S*(A2+S*A4)
			fp2 = floatx80_add(fp2, float32_to_floatx80(0x3F000000, status), status); // fp2 is A1+S*(A3+S*A5)
			fp3 = floatx80_mul(fp3, fp0, status); // fp3 IS R*S*(A2+S*A4)
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 IS S*(A1+S*(A3+S*A5))
			fp0 = floatx80_add(fp0, fp3, status); // fp0 IS R+R*S*(A2+S*A4)
			fp0 = floatx80_add(fp0, fp2, status); // fp0 IS EXP(R) - 1
			
			fp1 = exp_tbl[j];
			fp0 = floatx80_mul(fp0, fp1, status); // 2^(J/64)*(Exp(R)-1)
			fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status), status); // accurate 2^(J/64)
			fp0 = floatx80_add(fp0, fp1, status); // 2^(J/64) + 2^(J/64)*(Exp(R)-1)
			
			scale = packFloatx80(0, m, one_sig);
			if (adjflag) {
				adjscale = packFloatx80(0, m1, one_sig);
				fp0 = floatx80_mul(fp0, adjscale, status);
			}
			
			resetPrec(status, user_prec);
			
			a = floatx80_mul(fp0, scale, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else { // |X| >= 16380 log2
			if (compact > 0x400CB27C) { // |X| >= 16480 log2
				resetPrec(status, user_prec);
				if (aSign) {
					a = roundAndPackFloatx80(status->roundingPrecision, 0, -0x1000, aSig, 0, status);
				} else {
					a = roundAndPackFloatx80(status->roundingPrecision, 0, 0x8000, aSig, 0, status);
				}
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = a;
				fp1 = a;
				fp0 = floatx80_mul(fp0, float32_to_floatx80(0x42B8AA3B, status), status); // 64/log2 * X
				adjflag = 1;
				n = floatx80_to_int32(fp0, status); // int(64/log2*X)
				fp0 = int32_to_floatx80(n);
				
				j = n & 0x3F; // J = N mod 64
				k = n / 64; // NOTE: this is really arithmetic right shift by 6
				if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
					k--;
				}
				m1 = k / 2; // NOTE: this is really arithmetic right shift by 1
				if (k < 0 && (k & 1)) { // arithmetic right shift is division and round towards minus infinity
					m1--;
				}
				m = k - m1;
				m1 += 0x3FFF; // biased exponent of 2^(M1)
				m += 0x3FFF; // biased exponent of 2^(M)
				
				goto expcont1;
			}
		}
	} else { // |X| < 2^(-65)
		resetPrec(status, user_prec);
		
		a = floatx80_add(a, float32_to_floatx80(0x3F800000, status), status); // 1 + X
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | e to x minus 1
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_etoxm1(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, m, m1;
	floatx80 fp0, fp1, fp2, fp3, l2, sc, onebysc;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(aSign, one_exp, one_sig);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	if (aExp >= 0x3FFD) { // |X| >= 1/4
		compact = floatx80_make_compact(aExp, aSig);
		
		if (compact <= 0x4004C215) { // |X| <= 70 log2
			fp0 = a;
			fp1 = a;
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0x42B8AA3B, status), status); // 64/log2 * X
			n = floatx80_to_int32(fp0, status); // int(64/log2*X)
			fp0 = int32_to_floatx80(n);
			
			j = n & 0x3F; // J = N mod 64
			m = n / 64; // NOTE: this is really arithmetic right shift by 6
			if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
				m--;
			}
			m1 = -m;
			//m += 0x3FFF; // biased exponent of 2^(M)
			//m1 += 0x3FFF; // biased exponent of -2^(-M)
			
			fp2 = fp0; // N
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0xBC317218, status), status); // N * L1, L1 = lead(-log2/64)
			l2 = packFloatx80(0, 0x3FDC, 0x82E308654361C4C6ull);
			fp2 = floatx80_mul(fp2, l2, status); // N * L2, L1+L2 = -log2/64
			fp0 = floatx80_add(fp0, fp1, status); // X + N*L1
			fp0 = floatx80_add(fp0, fp2, status); // R
			
			fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp2 = float32_to_floatx80(0x3950097B, status); // A6
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 is S*A6
			fp3 = floatx80_mul(float32_to_floatx80(0x3AB60B6A, status), fp1, status); // fp3 is S*A5
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3F81111111174385ull, status), status); // fp2 IS A4+S*A6
			fp3 = floatx80_add(fp3, float64_to_floatx80(0x3FA5555555554F5Aull, status), status); // fp3 is A3+S*A5
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 IS S*(A4+S*A6)
			fp3 = floatx80_mul(fp3, fp1, status); // fp3 IS S*(A3+S*A5)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FC5555555555555ull, status), status); // fp2 IS A2+S*(A4+S*A6)
			fp3 = floatx80_add(fp3, float32_to_floatx80(0x3F000000, status), status); // fp3 IS A1+S*(A3+S*A5)
			fp2 = floatx80_mul(fp2, fp1, status); // fp2 IS S*(A2+S*(A4+S*A6))
			fp1 = floatx80_mul(fp1, fp3, status); // fp1 IS S*(A1+S*(A3+S*A5))
			fp2 = floatx80_mul(fp2, fp0, status); // fp2 IS R*S*(A2+S*(A4+S*A6))
			fp0 = floatx80_add(fp0, fp1, status); // fp0 IS R+S*(A1+S*(A3+S*A5))
			fp0 = floatx80_add(fp0, fp2, status); // fp0 IS EXP(R) - 1
			
			fp0 = floatx80_mul(fp0, exp_tbl[j], status); // 2^(J/64)*(Exp(R)-1)
			
			if (m >= 64) {
				fp1 = float32_to_floatx80(exp_tbl2[j], status);
				onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); // -2^(-M)
				fp1 = floatx80_add(fp1, onebysc, status);
				fp0 = floatx80_add(fp0, fp1, status);
				fp0 = floatx80_add(fp0, exp_tbl[j], status);
			} else if (m < -3) {
				fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status), status);
				fp0 = floatx80_add(fp0, exp_tbl[j], status);
				onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); // -2^(-M)
				fp0 = floatx80_add(fp0, onebysc, status);
			} else { // -3 <= m <= 63
				fp1 = exp_tbl[j];
				fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status), status);
				onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); // -2^(-M)
				fp1 = floatx80_add(fp1, onebysc, status);
				fp0 = floatx80_add(fp0, fp1, status);
			}
			
			sc = packFloatx80(0, m + 0x3FFF, one_sig);
			
			resetPrec(status, user_prec);
			
			a = floatx80_mul(fp0, sc, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else { // |X| > 70 log2
			if (aSign) {
				fp0 = float32_to_floatx80(0xBF800000, status); // -1
				
				resetPrec(status, user_prec);
				
				a = floatx80_add(fp0, float32_to_floatx80(0x00800000, status), status); // -1 + 2^(-126)
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				resetPrec(status, user_prec);
				
				return floatx80_etox(a, status);
			}
		}
	} else { // |X| < 1/4
		if (aExp >= 0x3FBE) {
			fp0 = a;
			fp0 = floatx80_mul(fp0, fp0, status); // S = X*X
			fp1 = float32_to_floatx80(0x2F30CAA8, status); // B12
			fp1 = floatx80_mul(fp1, fp0, status); // S * B12
			fp2 = float32_to_floatx80(0x310F8290, status); // B11
			fp1 = floatx80_add(fp1, float32_to_floatx80(0x32D73220, status), status); // B10
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(0x3493F281, status), status); // B9
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3EC71DE3A5774682ull, status), status); // B8
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EFA01A019D7CB68ull, status), status); // B7
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3F2A01A01A019DF3ull, status), status); // B6
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3F56C16C16C170E2ull, status), status); // B5
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3F81111111111111ull, status), status); // B4
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FA5555555555555ull, status), status); // B3
			fp3 = packFloatx80(0, 0x3FFC, 0xAAAAAAAAAAAAAAABull);
			fp1 = floatx80_add(fp1, fp3, status); // B2
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, fp0, status);
			
			fp2 = floatx80_mul(fp2, fp0, status);
			fp1 = floatx80_mul(fp1, a, status);
			
			fp0 = floatx80_mul(fp0, float32_to_floatx80(0x3F000000, status), status); // S*B1
			fp1 = floatx80_add(fp1, fp2, status); // Q
			fp0 = floatx80_add(fp0, fp1, status); // S*B1+Q
			
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else { // |X| < 2^(-65)
			sc = packFloatx80(1, 1, one_sig);
			fp0 = a;

			if (aExp < 0x0033) { // |X| < 2^(-16382)
				fp0 = floatx80_mul(fp0, float64_to_floatx80(0x48B0000000000000ull, status), status);
				fp0 = floatx80_add(fp0, sc, status);
				
				resetPrec(status, user_prec);

				a = floatx80_mul(fp0, float64_to_floatx80(0x3730000000000000ull, status), status);
			} else {
				resetPrec(status, user_prec);
				
				a = floatx80_add(fp0, sc, status);
			}
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Log base 10
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_log10(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	floatx80 fp0, fp1;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign == 0)
			return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		float_raise(float_flag_divbyzero, status);
		return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
	}
	
	if (aSign) {
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	fp0 = floatx80_logn(a, status);
	fp1 = packFloatx80(0, 0x3FFD, 0xDE5BD8A937287195ull); // INV_L10
	
	resetPrec(status, user_prec);
	
	a = floatx80_mul(fp0, fp1, status); // LOGN(X)*INV_L10
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Log base 2
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_log2(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	floatx80 fp0, fp1;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign == 0)
			return a;
	}
	
	if (aExp == 0) {
		if (aSig == 0) {
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
		}
		normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
	}
	
	if (aSign) {
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	if (aSig == one_sig) { // X is 2^k
		resetPrec(status, user_prec);
		
		a = int32_to_floatx80(aExp-0x3FFF);
	} else {
		fp0 = floatx80_logn(a, status);
		fp1 = packFloatx80(0, 0x3FFF, 0xB8AA3B295C17F0BCull); // INV_L2
		
		resetPrec(status, user_prec);
		
		a = floatx80_mul(fp0, fp1, status); // LOGN(X)*INV_L2
	}
	
	float_raise(float_flag_inexact, status);
	
	return a;
}

/*----------------------------------------------------------------------------
 | Log base e
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_logn(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig, fSig;
	
	int32_t compact, j, k, adjk;
	floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign == 0)
			return a;
	}
	
	adjk = 0;
	
	if (aExp == 0) {
		if (aSig == 0) { // zero
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(1, 0x7FFF, floatx80_default_infinity_low);
		}
		// Branche « #if 1 » d'origine (l'alternative #else était morte).
		if ((aSig & one_sig) == 0) { // denormal
			normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
			adjk = -100;
			aExp += 100;
			a = packFloatx80(aSign, aExp, aSig);
		}
	}
	
	if (aSign) {
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FFEF07D || compact > 0x3FFF8841) { // |X| < 15/16 or |X| > 17/16
		k = aExp - 0x3FFF;
		k += adjk;
		fp1 = int32_to_floatx80(k);
		
		fSig = (aSig & 0xFE00000000000000ull) | 0x0100000000000000ull;
		j = (fSig >> 56) & 0x7E; // DISPLACEMENT FOR 1/F
		
		f = packFloatx80(0, 0x3FFF, fSig); // F
		fp0 = packFloatx80(0, 0x3FFF, aSig); // Y
		
		fp0 = floatx80_sub(fp0, f, status); // Y-F
		
		// LP1CONT1
		fp0 = floatx80_mul(fp0, log_tbl[j], status); // FP0 IS U = (Y-F)/F
		logof2 = packFloatx80(0, 0x3FFE, 0xB17217F7D1CF79ACull);
		klog2 = floatx80_mul(fp1, logof2, status); // FP1 IS K*LOG2
		fp2 = floatx80_mul(fp0, fp0, status); // FP2 IS V=U*U
		
		fp3 = fp2;
		fp1 = fp2;
		
		fp1 = floatx80_mul(fp1, float64_to_floatx80(0x3FC2499AB5E4040Bull, status), status); // V*A6
		fp2 = floatx80_mul(fp2, float64_to_floatx80(0xBFC555B5848CB7DBull, status), status); // V*A5
		fp1 = floatx80_add(fp1, float64_to_floatx80(0x3FC99999987D8730ull, status), status); // A4+V*A6
		fp2 = floatx80_add(fp2, float64_to_floatx80(0xBFCFFFFFFF6F7E97ull, status), status); // A3+V*A5
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A4+V*A6)
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A3+V*A5)
		fp1 = floatx80_add(fp1, float64_to_floatx80(0x3FD55555555555A4ull, status), status); // A2+V*(A4+V*A6)
		fp2 = floatx80_add(fp2, float64_to_floatx80(0xBFE0000000000008ull, status), status); // A1+V*(A3+V*A5)
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A2+V*(A4+V*A6))
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A1+V*(A3+V*A5))
		fp1 = floatx80_mul(fp1, fp0, status); // U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp2, status); // U+V*(A1+V*(A3+V*A5))
		
		fp1 = floatx80_add(fp1, log_tbl[j+1], status); // LOG(F)+U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp1, status); // FP0 IS LOG(F) + LOG(1+U)
		
		resetPrec(status, user_prec);
		
		a = floatx80_add(fp0, klog2, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	} else { // |X-1| >= 1/16
		fp0 = a;
		fp1 = a;
		fp1 = floatx80_sub(fp1, float32_to_floatx80(0x3F800000, status), status); // FP1 IS X-1
		fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // FP0 IS X+1
		fp1 = floatx80_add(fp1, fp1, status); // FP1 IS 2(X-1)
		
		// LP1CONT2
		fp1 = floatx80_div(fp1, fp0, status); // U
		saveu = fp1;
		fp0 = floatx80_mul(fp1, fp1, status); // FP0 IS V = U*U
		fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS W = V*V
		
		fp3 = float64_to_floatx80(0x3F175496ADD7DAD6ull, status); // B5
		fp2 = float64_to_floatx80(0x3F3C71C2FE80C7E0ull, status); // B4
		fp3 = floatx80_mul(fp3, fp1, status); // W*B5
		fp2 = floatx80_mul(fp2, fp1, status); // W*B4
		fp3 = floatx80_add(fp3, float64_to_floatx80(0x3F624924928BCCFFull, status), status); // B3+W*B5
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x3F899999999995ECull, status), status); // B2+W*B4
		fp1 = floatx80_mul(fp1, fp3, status); // W*(B3+W*B5)
		fp2 = floatx80_mul(fp2, fp0, status); // V*(B2+W*B4)
		fp1 = floatx80_add(fp1, float64_to_floatx80(0x3FB5555555555555ull, status), status); // B1+W*(B3+W*B5)
		
		fp0 = floatx80_mul(fp0, saveu, status); // FP0 IS U*V
		fp1 = floatx80_add(fp1, fp2, status); // B1+W*(B3+W*B5) + V*(B2+W*B4)
		fp0 = floatx80_mul(fp0, fp1, status); // U*V*( [B1+W*(B3+W*B5)] + [V*(B2+W*B4)] )
		
		resetPrec(status, user_prec);
		
		a = floatx80_add(fp0, saveu, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Log base e of x plus 1
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_lognp1(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig, fSig;
	
	int32_t compact, j, k;
	floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) {
			float_raise(float_flag_invalid, status);
			return floatx80_default_nan(status);
		}
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	if (aSign && aExp >= one_exp) {
		if (aExp == one_exp && aSig == one_sig) {
			float_raise(float_flag_divbyzero, status);
			return packFloatx80(aSign, 0x7FFF, floatx80_default_infinity_low);
		}
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp < 0x3f99 || (aExp == 0x3f99 && aSig == one_sig)) { // <= min threshold
		float_raise(float_flag_inexact, status);
		return floatx80_move(a, status);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	fp0 = a; // Z
	fp1 = a;
	
	fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // X = (1+Z)
	
	aExp = extractFloatx80Exp(fp0);
	aSig = extractFloatx80Frac(fp0);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FFE8000 || compact > 0x3FFFC000) { // |X| < 1/2 or |X| > 3/2
		k = aExp - 0x3FFF;
		fp1 = int32_to_floatx80(k);
		
		fSig = (aSig & 0xFE00000000000000ull) | 0x0100000000000000ull;
		j = (fSig >> 56) & 0x7E; // DISPLACEMENT FOR 1/F
		
		f = packFloatx80(0, 0x3FFF, fSig); // F
		fp0 = packFloatx80(0, 0x3FFF, aSig); // Y
		
		fp0 = floatx80_sub(fp0, f, status); // Y-F
		
	lp1cont1:
		// LP1CONT1
		fp0 = floatx80_mul(fp0, log_tbl[j], status); // FP0 IS U = (Y-F)/F
		logof2 = packFloatx80(0, 0x3FFE, 0xB17217F7D1CF79ACull);
		klog2 = floatx80_mul(fp1, logof2, status); // FP1 IS K*LOG2
		fp2 = floatx80_mul(fp0, fp0, status); // FP2 IS V=U*U
		
		fp3 = fp2;
		fp1 = fp2;
		
		fp1 = floatx80_mul(fp1, float64_to_floatx80(0x3FC2499AB5E4040Bull, status), status); // V*A6
		fp2 = floatx80_mul(fp2, float64_to_floatx80(0xBFC555B5848CB7DBull, status), status); // V*A5
		fp1 = floatx80_add(fp1, float64_to_floatx80(0x3FC99999987D8730ull, status), status); // A4+V*A6
		fp2 = floatx80_add(fp2, float64_to_floatx80(0xBFCFFFFFFF6F7E97ull, status), status); // A3+V*A5
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A4+V*A6)
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A3+V*A5)
		fp1 = floatx80_add(fp1, float64_to_floatx80(0x3FD55555555555A4ull, status), status); // A2+V*(A4+V*A6)
		fp2 = floatx80_add(fp2, float64_to_floatx80(0xBFE0000000000008ull, status), status); // A1+V*(A3+V*A5)
		fp1 = floatx80_mul(fp1, fp3, status); // V*(A2+V*(A4+V*A6))
		fp2 = floatx80_mul(fp2, fp3, status); // V*(A1+V*(A3+V*A5))
		fp1 = floatx80_mul(fp1, fp0, status); // U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp2, status); // U+V*(A1+V*(A3+V*A5))
		
		fp1 = floatx80_add(fp1, log_tbl[j+1], status); // LOG(F)+U*V*(A2+V*(A4+V*A6))
		fp0 = floatx80_add(fp0, fp1, status); // FP0 IS LOG(F) + LOG(1+U)
		
		resetPrec(status, user_prec);
		
		a = floatx80_add(fp0, klog2, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	} else if (compact < 0x3FFEF07D || compact > 0x3FFF8841) { // |X| < 1/16 or |X| > -1/16
		// LP1CARE
		fSig = (aSig & 0xFE00000000000000ull) | 0x0100000000000000ull;
		f = packFloatx80(0, 0x3FFF, fSig); // F
		j = (fSig >> 56) & 0x7E; // DISPLACEMENT FOR 1/F

		if (compact >= 0x3FFF8000) { // 1+Z >= 1
			// KISZERO
			fp0 = floatx80_sub(float32_to_floatx80(0x3F800000, status), f, status); // 1-F
			fp0 = floatx80_add(fp0, fp1, status); // FP0 IS Y-F = (1-F)+Z
			fp1 = packFloatx80(0, 0, 0); // K = 0
		} else {
			// KISNEG
			fp0 = floatx80_sub(float32_to_floatx80(0x40000000, status), f, status); // 2-F
			fp1 = floatx80_add(fp1, fp1, status); // 2Z
			fp0 = floatx80_add(fp0, fp1, status); // FP0 IS Y-F = (2-F)+2Z
			fp1 = packFloatx80(1, one_exp, one_sig); // K = -1
		}
		goto lp1cont1;
	} else {
		// LP1ONE16
		fp1 = floatx80_add(fp1, fp1, status); // FP1 IS 2Z
		fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // FP0 IS 1+X
		
		// LP1CONT2
		fp1 = floatx80_div(fp1, fp0, status); // U
		saveu = fp1;
		fp0 = floatx80_mul(fp1, fp1, status); // FP0 IS V = U*U
		fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS W = V*V
		
		fp3 = float64_to_floatx80(0x3F175496ADD7DAD6ull, status); // B5
		fp2 = float64_to_floatx80(0x3F3C71C2FE80C7E0ull, status); // B4
		fp3 = floatx80_mul(fp3, fp1, status); // W*B5
		fp2 = floatx80_mul(fp2, fp1, status); // W*B4
		fp3 = floatx80_add(fp3, float64_to_floatx80(0x3F624924928BCCFFull, status), status); // B3+W*B5
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x3F899999999995ECull, status), status); // B2+W*B4
		fp1 = floatx80_mul(fp1, fp3, status); // W*(B3+W*B5)
		fp2 = floatx80_mul(fp2, fp0, status); // V*(B2+W*B4)
		fp1 = floatx80_add(fp1, float64_to_floatx80(0x3FB5555555555555ull, status), status); // B1+W*(B3+W*B5)
		
		fp0 = floatx80_mul(fp0, saveu, status); // FP0 IS U*V
		fp1 = floatx80_add(fp1, fp2, status); // B1+W*(B3+W*B5) + V*(B2+W*B4)
		fp0 = floatx80_mul(fp0, fp1, status); // U*V*( [B1+W*(B3+W*B5)] + [V*(B2+W*B4)] )
		
		resetPrec(status, user_prec);
		
		a = floatx80_add(fp0, saveu, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Sine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_sin(floatx80 a, float_status *status)
{
	flag aSign, xSign;
	int32_t aExp, xExp;
	uint64_t aSig, xSig;
	
	int32_t compact, l, n, j;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
	float32 posneg1, twoto63;
	flag adjn, endflag;

	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	adjn = 0;
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, 0xC90FDAA200000000ull);
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, 0x85A308D300000000ull);
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
			}
		loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, 0xA2F9836E4E44152Aull); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, 0xC90FDAA200000000ull);
			twopi2 = packFloatx80(0, 0x3FDD + l, 0x85A308D300000000ull);
			
			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL
			
			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto sincont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			// SINSM
			fp0 = float32_to_floatx80(0x3F800000, status); // 1
			
			resetPrec(status, user_prec);
			
			if (adjn) {
				// COSTINY
				a = floatx80_sub(fp0, float32_to_floatx80(0x00800000, status), status);
			} else {
				// SINTINY
				a = floatx80_move(a, status);
			}
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(0x3FE45F306DC9C883ull, status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		j = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[j], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status), status); // FP0 IS R = (X-Y1)-Y2
		
	sincont:
		if ((n + adjn) & 1) {
			// COSPOLY
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp2 = float64_to_floatx80(0x3D2AC4D0D6011EE3ull, status); // B8
			fp3 = float64_to_floatx80(0xBDA9396F9F45AC19ull, status); // B7
			
			xSign = extractFloatx80Sign(fp0); // X IS S
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			if (((n + adjn) >> 1) & 1) {
				xSign ^= 1;
				posneg1 = 0xBF800000; // -1
			} else {
				xSign ^= 0;
				posneg1 = 0x3F800000; // 1
			} // X IS NOW R'= SGN*R
			
			fp2 = floatx80_mul(fp2, fp1, status); // TB8
			fp3 = floatx80_mul(fp3, fp1, status); // TB7
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3E21EED90612C972ull, status), status); // B6+TB8
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBE927E4FB79D9FCFull, status), status); // B5+TB7
			fp2 = floatx80_mul(fp2, fp1, status); // T(B6+TB8)
			fp3 = floatx80_mul(fp3, fp1, status); // T(B5+TB7)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EFA01A01A01D423ull, status), status); // B4+T(B6+TB8)
			fp4 = packFloatx80(1, 0x3FF5, 0xB60B60B60B61D438ull);
			fp3 = floatx80_add(fp3, fp4, status); // B3+T(B5+TB7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(B4+T(B6+TB8))
			fp1 = floatx80_mul(fp1, fp3, status); // T(B3+T(B5+TB7))
			fp4 = packFloatx80(0, 0x3FFA, 0xAAAAAAAAAAAAAB5Eull);
			fp2 = floatx80_add(fp2, fp4, status); // B2+T(B4+T(B6+TB8))
			fp1 = floatx80_add(fp1, float32_to_floatx80(0xBF000000, status), status); // B1+T(B3+T(B5+TB7))
			fp0 = floatx80_mul(fp0, fp2, status); // S(B2+T(B4+T(B6+TB8)))
			fp0 = floatx80_add(fp0, fp1, status); // [B1+T(B3+T(B5+TB7))]+[S(B2+T(B4+T(B6+TB8)))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status);
			
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			// SINPOLY
			xSign = extractFloatx80Sign(fp0); // X IS R
			xExp = extractFloatx80Exp(fp0);
			xSig = extractFloatx80Frac(fp0);
			
			xSign ^= ((n + adjn) >> 1) & 1; // X IS NOW R'= SGN*R
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S
			fp1 = floatx80_mul(fp0, fp0, status); // FP1 IS T
			fp3 = float64_to_floatx80(0xBD6AAA77CCC994F5ull, status); // A7
			fp2 = float64_to_floatx80(0x3DE612097AAE8DA1ull, status); // A6
			fp3 = floatx80_mul(fp3, fp1, status); // T*A7
			fp2 = floatx80_mul(fp2, fp1, status); // T*A6
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBE5AE6452A118AE4ull, status), status); // A5+T*A7
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EC71DE3A5341531ull, status), status); // A4+T*A6
			fp3 = floatx80_mul(fp3, fp1, status); // T(A5+TA7)
			fp2 = floatx80_mul(fp2, fp1, status); // T(A4+TA6)
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBF2A01A01A018B59ull, status), status); // A3+T(A5+TA7)
			fp4 = packFloatx80(0, 0x3FF8, 0x88888888888859AFull);
			fp2 = floatx80_add(fp2, fp4, status); // A2+T(A4+TA6)
			fp1 = floatx80_mul(fp1, fp3, status); // T(A3+T(A5+TA7))
			fp2 = floatx80_mul(fp2, fp0, status); // S(A2+T(A4+TA6))
			fp4 = packFloatx80(1, 0x3FFC, 0xAAAAAAAAAAAAAA99ull);
			fp1 = floatx80_add(fp1, fp4, status); // A1+T(A3+T(A5+TA7))
			fp1 = floatx80_add(fp1, fp2, status); // [A1+T(A3+T(A5+TA7))]+[S(A2+T(A4+TA6))]
			
			x = packFloatx80(xSign, xExp, xSig);
			fp0 = floatx80_mul(fp0, x, status); // R'*S
			fp0 = floatx80_mul(fp0, fp1, status); // SIN(R')-R'
			
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, x, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Sine and cosine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_sincos(floatx80 a, floatx80 *c, float_status *status)
{
	flag aSign, xSign, rSign, sSign;
	int32_t aExp, xExp, rExp, sExp;
	uint64_t aSig, rSig, sSig;
	
	int32_t compact, l, n, i, j1, j2;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, r, s, invtwopi, twopi1, twopi2;
	float32 posneg1, twoto63;
	flag endflag;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t)(aSig << 1)) {
			*c = propagateFloatx80NaNOneArg(a, status);
			return *c;
			
		}
		float_raise(float_flag_invalid, status);
		*c = floatx80_default_nan(status);
		return *c;
		
	}
	
	if (aExp == 0 && aSig == 0) {
		*c = packFloatx80(0, one_exp, one_sig);
		return packFloatx80(aSign, 0, 0);
		
	}
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, 0xC90FDAA200000000ull);
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, 0x85A308D300000000ull);
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
				
			}
			loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, 0xA2F9836E4E44152Aull); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, 0xC90FDAA200000000ull);
			twopi2 = packFloatx80(0, 0x3FDD + l, 0x85A308D300000000ull);

			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL

			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto sccont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			// SCSM
			fp0 = float32_to_floatx80(0x3F800000, status); // 1
			
			resetPrec(status, user_prec);
			
			// COSTINY
			*c = floatx80_sub(fp0, float32_to_floatx80(0x00800000, status), status);
			// SINTINY
			a = floatx80_move(a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(0x3FE45F306DC9C883ull, status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		i = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[i], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[i], status), status); // FP0 IS R = (X-Y1)-Y2
		
		sccont:
		n &= 3; // k = N mod 4
		if (n & 1) {
			// NODD
			j1 = n >> 1; // j1 = (k-1)/2
			j2 = j1 ^ (n & 1); // j2 = j1 EOR (k mod 2)
			
			rSign = extractFloatx80Sign(fp0); // R
			rExp = extractFloatx80Exp(fp0);
			rSig = extractFloatx80Frac(fp0);
			rSign ^= j2;
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S = R*R
			fp1 = float64_to_floatx80(0xBD6AAA77CCC994F5ull, status); // A7
			fp2 = float64_to_floatx80(0x3D2AC4D0D6011EE3ull, status); // B8
			fp1 = floatx80_mul(fp1, fp0, status); // FP1 IS SA7
			fp2 = floatx80_mul(fp2, fp0, status); // FP2 IS SB8
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3DE612097AAE8DA1ull, status), status); // A6+SA7
			fp2 = floatx80_add(fp2, float64_to_floatx80(0xBDA9396F9F45AC19ull, status), status); // B7+SB8
			fp1 = floatx80_mul(fp1, fp0, status); // S(A6+SA7)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B7+SB8)
			fp1 = floatx80_add(fp1, float64_to_floatx80(0xBE5AE6452A118AE4ull, status), status); // A5+S(A6+SA7)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3E21EED90612C972ull, status), status); // B6+S(B7+SB8)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A5+S(A6+SA7))
			fp2 = floatx80_mul(fp2, fp0, status); // S(B6+S(B7+SB8))
			
			sSign = extractFloatx80Sign(fp0); // S
			sExp = extractFloatx80Exp(fp0);
			sSig = extractFloatx80Frac(fp0);
			sSign ^= j1;
			posneg1 = 0x3F800000;
			posneg1 |= j1 ? 0x80000000 : 0;
			
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3EC71DE3A5341531ull, status), status); // A4+S(A5+S(A6+SA7))
			fp2 = floatx80_add(fp2, float64_to_floatx80(0xBE927E4FB79D9FCFull, status), status); // B5+S(B6+S(B7+SB8))
			fp1 = floatx80_mul(fp1, fp0, status); // S(A4+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B5+...)
			fp1 = floatx80_add(fp1, float64_to_floatx80(0xBF2A01A01A018B59ull, status), status); // A3+S(A4+...)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EFA01A01A01D423ull, status), status); // B4+S(B5+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A3+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B4+...)
			fp3 = packFloatx80(0, 0x3FF8, 0x88888888888859AFull);
			fp4 = packFloatx80(1, 0x3FF5, 0xB60B60B60B61D438ull);
			fp1 = floatx80_add(fp1, fp3, status); // A2+S(A3+...)
			fp2 = floatx80_add(fp2, fp4, status); // B3+S(B4+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A2+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(B3+...)
			fp3 = packFloatx80(1, 0x3FFC, 0xAAAAAAAAAAAAAA99ull);
			fp4 = packFloatx80(0, 0x3FFA, 0xAAAAAAAAAAAAAB5Eull);
			fp1 = floatx80_add(fp1, fp3, status); // A1+S(A2+...)
			fp2 = floatx80_add(fp2, fp4, status); // B2+S(B3+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(A1+...)
			fp0 = floatx80_mul(fp0, fp2, status); // S(B2+...)
			
			r = packFloatx80(rSign, rExp, rSig);
			fp1 = floatx80_mul(fp1, r, status); // R'S(A1+...)
			fp0 = floatx80_add(fp0, float32_to_floatx80(0xBF000000, status), status); // B1+S(B2...)
			
			s = packFloatx80(sSign, sExp, sSig);
			fp0 = floatx80_mul(fp0, s, status); // S'(B1+S(B2+...))
			
			resetPrec(status, user_prec);
			
			*c = floatx80_add(fp1, r, status); // COS(X)
			
			a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status); // SIN(X)
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			// NEVEN
			j1 = n >> 1; // j1 = k/2
			
			rSign = extractFloatx80Sign(fp0); // R
			rExp = extractFloatx80Exp(fp0);
			rSig = extractFloatx80Frac(fp0);
			rSign ^= j1;
			
			fp0 = floatx80_mul(fp0, fp0, status); // FP0 IS S = R*R
			fp1 = float64_to_floatx80(0x3D2AC4D0D6011EE3ull, status); // B8
			fp2 = float64_to_floatx80(0xBD6AAA77CCC994F5ull, status); // A7
			fp1 = floatx80_mul(fp1, fp0, status); // SB8
			fp2 = floatx80_mul(fp2, fp0, status); // SA7
			
			sSign = extractFloatx80Sign(fp0); // S
			sExp = extractFloatx80Exp(fp0);
			sSig = extractFloatx80Frac(fp0);
			sSign ^= j1;
			posneg1 = 0x3F800000;
			posneg1 |= j1 ? 0x80000000 : 0;
			
			fp1 = floatx80_add(fp1, float64_to_floatx80(0xBDA9396F9F45AC19ull, status), status); // B7+SB8
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3DE612097AAE8DA1ull, status), status); // A6+SA7
			fp1 = floatx80_mul(fp1, fp0, status); // S(B7+SB8)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A6+SA7)
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3E21EED90612C972ull, status), status); // B6+S(B7+SB8)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0xBE5AE6452A118AE4ull, status), status); // A5+S(A6+SA7)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B6+S(B7+SB8))
			fp2 = floatx80_mul(fp2, fp0, status); // S(A5+S(A6+SA7))
			fp1 = floatx80_add(fp1, float64_to_floatx80(0xBE927E4FB79D9FCFull, status), status); // B5+S(B6+S(B7+SB8))
			fp2 = floatx80_add(fp2, float64_to_floatx80(0x3EC71DE3A5341531ull, status), status); // A4+S(A5+S(A6+SA7))
			fp1 = floatx80_mul(fp1, fp0, status); // S(B5+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A4+...)
			fp1 = floatx80_add(fp1, float64_to_floatx80(0x3EFA01A01A01D423ull, status), status); // B4+S(B5+...)
			fp2 = floatx80_add(fp2, float64_to_floatx80(0xBF2A01A01A018B59ull, status), status); // A3+S(A4+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B4+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A3+...)
			fp3 = packFloatx80(1, 0x3FF5, 0xB60B60B60B61D438ull);
			fp4 = packFloatx80(0, 0x3FF8, 0x88888888888859AFull);
			fp1 = floatx80_add(fp1, fp3, status); // B3+S(B4+...)
			fp2 = floatx80_add(fp2, fp4, status); // A2+S(A3+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B3+...)
			fp2 = floatx80_mul(fp2, fp0, status); // S(A2+...)
			fp3 = packFloatx80(0, 0x3FFA, 0xAAAAAAAAAAAAAB5Eull);
			fp4 = packFloatx80(1, 0x3FFC, 0xAAAAAAAAAAAAAA99ull);
			fp1 = floatx80_add(fp1, fp3, status); // B2+S(B3+...)
			fp2 = floatx80_add(fp2, fp4, status); // A1+S(A2+...)
			fp1 = floatx80_mul(fp1, fp0, status); // S(B2+...)
			fp0 = floatx80_mul(fp0, fp2, status); // S(A1+...)
			fp1 = floatx80_add(fp1, float32_to_floatx80(0xBF000000, status), status); // B1+S(B2...)
			
			r = packFloatx80(rSign, rExp, rSig);
			fp0 = floatx80_mul(fp0, r, status); // R'S(A1+...)
			
			s = packFloatx80(sSign, sExp, sSig);
			fp1 = floatx80_mul(fp1, s, status); // S'(B1+S(B2+...))
			
			resetPrec(status, user_prec);
			
			*c = floatx80_add(fp1, float32_to_floatx80(posneg1, status), status); // COS(X)
			
			a = floatx80_add(fp0, r, status); // SIN(X)
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic sine
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_sinh(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact;
	floatx80 fp0, fp1, fp2;
	float32 fact;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);

	if (compact > 0x400CB167) {
		// SINHBIG
		if (compact > 0x400CB2B3) {
			resetPrec(status, user_prec);
			
			a = roundAndPackFloatx80(status->roundingPrecision, aSign, 0x8000, aSig, 0, status);
			float_raise(float_flag_inexact, status);
			return a;
		} else {
			fp0 = floatx80_abs(a, status); // Y = |X|
			fp0 = floatx80_sub(fp0, float64_to_floatx80(0x40C62D38D3D64634ull, status), status); // (|X|-16381LOG2_LEAD)
			fp0 = floatx80_sub(fp0, float64_to_floatx80(0x3D6F90AEB1E75CC7ull, status), status); // |X| - 16381 LOG2, ACCURATE
			fp0 = floatx80_etox(fp0, status);
			fp2 = packFloatx80(aSign, 0x7FFB, one_sig);
			
			resetPrec(status, user_prec);
			
			a = floatx80_mul(fp0, fp2, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else { // |X| < 16380 LOG2
		fp0 = floatx80_abs(a, status); // Y = |X|
		fp0 = floatx80_etoxm1(fp0, status); // FP0 IS Z = EXPM1(Y)
		fp1 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1+Z
		fp2 = fp0;
		fp0 = floatx80_div(fp0, fp1, status); // Z/(1+Z)
		fp0 = floatx80_add(fp0, fp2, status);
		
		fact = 0x3F000000;
		fact |= aSign ? 0x80000000 : 0x00000000;
		
		resetPrec(status, user_prec);
		
		a = floatx80_mul(fp0, float32_to_floatx80(fact, status), status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | Tangent
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_tan(floatx80 a, float_status *status)
{
	flag aSign, xSign;
	int32_t aExp, xExp;
	uint64_t aSig, xSig;
	
	int32_t compact, l, n, j;
	floatx80 fp0, fp1, fp2, fp3, fp4, fp5, invtwopi, twopi1, twopi2;
	float32 twoto63;
	flag endflag;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		float_raise(float_flag_invalid, status);
		return floatx80_default_nan(status);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	fp0 = a;
	
	if (compact < 0x3FD78000 || compact > 0x4004BC7E) { // 2^(-40) > |X| > 15 PI
		if (compact > 0x3FFF8000) { // |X| >= 15 PI
			// REDUCEX
			fp1 = packFloatx80(0, 0, 0);
			if (compact == 0x7FFEFFFF) {
				twopi1 = packFloatx80(aSign ^ 1, 0x7FFE, 0xC90FDAA200000000ull);
				twopi2 = packFloatx80(aSign ^ 1, 0x7FDC, 0x85A308D300000000ull);
				fp0 = floatx80_add(fp0, twopi1, status);
				fp1 = fp0;
				fp0 = floatx80_add(fp0, twopi2, status);
				fp1 = floatx80_sub(fp1, fp0, status);
				fp1 = floatx80_add(fp1, twopi2, status);
			}
		loop:
			xSign = extractFloatx80Sign(fp0);
			xExp = extractFloatx80Exp(fp0);
			xExp -= 0x3FFF;
			if (xExp <= 28) {
				l = 0;
				endflag = 1;
			} else {
				l = xExp - 27;
				endflag = 0;
			}
			invtwopi = packFloatx80(0, 0x3FFE - l, 0xA2F9836E4E44152Aull); // INVTWOPI
			twopi1 = packFloatx80(0, 0x3FFF + l, 0xC90FDAA200000000ull);
			twopi2 = packFloatx80(0, 0x3FDD + l, 0x85A308D300000000ull);
			
			twoto63 = 0x5F000000;
			twoto63 |= xSign ? 0x80000000 : 0x00000000; // SIGN(INARG)*2^63 IN SGL
			
			fp2 = floatx80_mul(fp0, invtwopi, status);
			fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status), status); // THE FRACTIONAL PART OF FP2 IS ROUNDED
			fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status), status); // FP2 is N
			fp4 = floatx80_mul(twopi1, fp2, status); // W = N*P1
			fp5 = floatx80_mul(twopi2, fp2, status); // w = N*P2
			fp3 = floatx80_add(fp4, fp5, status); // FP3 is P
			fp4 = floatx80_sub(fp4, fp3, status); // W-P
			fp0 = floatx80_sub(fp0, fp3, status); // FP0 is A := R - P
			fp4 = floatx80_add(fp4, fp5, status); // FP4 is p = (W-P)+w
			fp3 = fp0; // FP3 is A
			fp1 = floatx80_sub(fp1, fp4, status); // FP1 is a := r - p
			fp0 = floatx80_add(fp0, fp1, status); // FP0 is R := A+a
			
			if (endflag > 0) {
				n = floatx80_to_int32(fp2, status);
				goto tancont;
			}
			fp3 = floatx80_sub(fp3, fp0, status); // A-R
			fp1 = floatx80_add(fp1, fp3, status); // FP1 is r := (A-R)+a
			goto loop;
		} else {
			resetPrec(status, user_prec);
			
			a = floatx80_move(a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else {
		fp1 = floatx80_mul(fp0, float64_to_floatx80(0x3FE45F306DC9C883ull, status), status); // X*2/PI
		
		n = floatx80_to_int32(fp1, status);
		j = 32 + n;
		
		fp0 = floatx80_sub(fp0, pi_tbl[j], status); // X-Y1
		fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status), status); // FP0 IS R = (X-Y1)-Y2
		
	tancont:
		if (n & 1) {
			// NODD
			fp1 = fp0; // R
			fp0 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp3 = float64_to_floatx80(0x3EA0B759F50F8688ull, status); // Q4
			fp2 = float64_to_floatx80(0xBEF2BAA5A8924F04ull, status); // P3
			fp3 = floatx80_mul(fp3, fp0, status); // SQ4
			fp2 = floatx80_mul(fp2, fp0, status); // SP3
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBF346F59B39BA65Full, status), status); // Q3+SQ4
			fp4 = packFloatx80(0, 0x3FF6, 0xE073D3FC199C4A00ull);
			fp2 = floatx80_add(fp2, fp4, status); // P2+SP3
			fp3 = floatx80_mul(fp3, fp0, status); // S(Q3+SQ4)
			fp2 = floatx80_mul(fp2, fp0, status); // S(P2+SP3)
			fp4 = packFloatx80(0, 0x3FF9, 0xD23CD68415D95FA1ull);
			fp3 = floatx80_add(fp3, fp4, status); // Q2+S(Q3+SQ4)
			fp4 = packFloatx80(1, 0x3FFC, 0x8895A6C5FB423BCAull);
			fp2 = floatx80_add(fp2, fp4, status); // P1+S(P2+SP3)
			fp3 = floatx80_mul(fp3, fp0, status); // S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp0, status); // S(P1+S(P2+SP3))
			fp4 = packFloatx80(1, 0x3FFD, 0xEEF57E0DA84BC8CEull);
			fp3 = floatx80_add(fp3, fp4, status); // Q1+S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp1, status); // RS(P1+S(P2+SP3))
			fp0 = floatx80_mul(fp0, fp3, status); // S(Q1+S(Q2+S(Q3+SQ4)))
			fp1 = floatx80_add(fp1, fp2, status); // R+RS(P1+S(P2+SP3))
			fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1+S(Q1+S(Q2+S(Q3+SQ4)))
			
			xSign = extractFloatx80Sign(fp1);
			xExp = extractFloatx80Exp(fp1);
			xSig = extractFloatx80Frac(fp1);
			xSign ^= 1;
			fp1 = packFloatx80(xSign, xExp, xSig);

			resetPrec(status, user_prec);
			
			a = floatx80_div(fp0, fp1, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
			fp3 = float64_to_floatx80(0x3EA0B759F50F8688ull, status); // Q4
			fp2 = float64_to_floatx80(0xBEF2BAA5A8924F04ull, status); // P3
			fp3 = floatx80_mul(fp3, fp1, status); // SQ4
			fp2 = floatx80_mul(fp2, fp1, status); // SP3
			fp3 = floatx80_add(fp3, float64_to_floatx80(0xBF346F59B39BA65Full, status), status); // Q3+SQ4
			fp4 = packFloatx80(0, 0x3FF6, 0xE073D3FC199C4A00ull);
			fp2 = floatx80_add(fp2, fp4, status); // P2+SP3
			fp3 = floatx80_mul(fp3, fp1, status); // S(Q3+SQ4)
			fp2 = floatx80_mul(fp2, fp1, status); // S(P2+SP3)
			fp4 = packFloatx80(0, 0x3FF9, 0xD23CD68415D95FA1ull);
			fp3 = floatx80_add(fp3, fp4, status); // Q2+S(Q3+SQ4)
			fp4 = packFloatx80(1, 0x3FFC, 0x8895A6C5FB423BCAull);
			fp2 = floatx80_add(fp2, fp4, status); // P1+S(P2+SP3)
			fp3 = floatx80_mul(fp3, fp1, status); // S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp1, status); // S(P1+S(P2+SP3))
			fp4 = packFloatx80(1, 0x3FFD, 0xEEF57E0DA84BC8CEull);
			fp3 = floatx80_add(fp3, fp4, status); // Q1+S(Q2+S(Q3+SQ4))
			fp2 = floatx80_mul(fp2, fp0, status); // RS(P1+S(P2+SP3))
			fp1 = floatx80_mul(fp1, fp3, status); // S(Q1+S(Q2+S(Q3+SQ4)))
			fp0 = floatx80_add(fp0, fp2, status); // R+RS(P1+S(P2+SP3))
			fp1 = floatx80_add(fp1, float32_to_floatx80(0x3F800000, status), status); // 1+S(Q1+S(Q2+S(Q3+SQ4)))
			
			resetPrec(status, user_prec);
			
			a = floatx80_div(fp0, fp1, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	}
}

/*----------------------------------------------------------------------------
 | Hyperbolic tangent
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_tanh(floatx80 a, float_status *status)
{
	flag aSign, vSign;
	int32_t aExp, vExp;
	uint64_t aSig, vSig;
	
	int32_t compact;
	floatx80 fp0, fp1;
	float32 sign;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		return packFloatx80(aSign, one_exp, one_sig);
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(aSign, 0, 0);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FD78000 || compact > 0x3FFFDDCE) {
		// TANHBORS
		if (compact < 0x3FFF8000) {
			// TANHSM
			resetPrec(status, user_prec);
			
			a = floatx80_move(a, status);
			
			float_raise(float_flag_inexact, status);
			
			return a;
		} else {
			if (compact > 0x40048AA1) {
				// TANHHUGE
				sign = 0x3F800000;
				sign |= aSign ? 0x80000000 : 0x00000000;
				fp0 = float32_to_floatx80(sign, status);
				sign &= 0x80000000;
				sign ^= 0x80800000; // -SIGN(X)*EPS
				
				resetPrec(status, user_prec);
				
				a = floatx80_add(fp0, float32_to_floatx80(sign, status), status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			} else {
				fp0 = packFloatx80(0, aExp+1, aSig); // Y = 2|X|
				fp0 = floatx80_etox(fp0, status); // FP0 IS EXP(Y)
				fp0 = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // EXP(Y)+1
				sign = aSign ? 0x80000000 : 0x00000000;
				fp1 = floatx80_div(float32_to_floatx80(sign^0xC0000000, status), fp0, status); // -SIGN(X)*2 / [EXP(Y)+1]
				fp0 = float32_to_floatx80(sign | 0x3F800000, status); // SIGN
				
				resetPrec(status, user_prec);
				
				a = floatx80_add(fp1, fp0, status);
				
				float_raise(float_flag_inexact, status);
				
				return a;
			}
		}
	} else { // 2**(-40) < |X| < (5/2)LOG2
		fp0 = packFloatx80(0, aExp+1, aSig); // Y = 2|X|
		fp0 = floatx80_etoxm1(fp0, status); // FP0 IS Z = EXPM1(Y)
		fp1 = floatx80_add(fp0, float32_to_floatx80(0x40000000, status), status); // Z+2
		
		vSign = extractFloatx80Sign(fp1);
		vExp = extractFloatx80Exp(fp1);
		vSig = extractFloatx80Frac(fp1);
		
		fp1 = packFloatx80(vSign ^ aSign, vExp, vSig);
		
		resetPrec(status, user_prec);
		
		a = floatx80_div(fp0, fp1, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | 10 to x
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_tentox(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, l, m, m1;
	floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(0, 0, 0);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	fp0 = a;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FB98000 || compact > 0x400B9B07) { // |X| > 16480 LOG2/LOG10 or |X| < 2^(-70)
		if (compact > 0x3FFF8000) { // |X| > 16480
			resetPrec(status, user_prec);
			
			if (aSign) {
				return roundAndPackFloatx80(status->roundingPrecision, 0, -0x1000, aSig, 0, status);
			} else {
				return roundAndPackFloatx80(status->roundingPrecision, 0, 0x8000, aSig, 0, status);
			}
		} else { // |X| < 2^(-70)
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1 + X
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else { // 2^(-70) <= |X| <= 16480 LOG 2 / LOG 10
		fp1 = fp0; // X
		fp1 = floatx80_mul(fp1, float64_to_floatx80(0x406A934F0979A371ull, status), status); // X*64*LOG10/LOG2
		n = floatx80_to_int32(fp1, status); // N=INT(X*64*LOG10/LOG2)
		fp1 = int32_to_floatx80(n);

		j = n & 0x3F;
		l = n / 64; // NOTE: this is really arithmetic right shift by 6
		if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
			l--;
		}
		m = l / 2; // NOTE: this is really arithmetic right shift by 1
		if (l < 0 && (l & 1)) { // arithmetic right shift is division and round towards minus infinity
			m--;
		}
		m1 = l - m;
		m1 += 0x3FFF; // ADJFACT IS 2^(M')

		adjfact = packFloatx80(0, m1, one_sig);
		fact1 = exp2_tbl[j];
		fact1.high += m;
		fact2.high = exp2_tbl2[j]>>16;
		fact2.high += m;
		fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
		fact2.low <<= 48;
		
		fp2 = fp1; // N
		fp1 = floatx80_mul(fp1, float64_to_floatx80(0x3F734413509F8000ull, status), status); // N*(LOG2/64LOG10)_LEAD
		fp3 = packFloatx80(1, 0x3FCD, 0xC0219DC1DA994FD2ull);
		fp2 = floatx80_mul(fp2, fp3, status); // N*(LOG2/64LOG10)_TRAIL
		fp0 = floatx80_sub(fp0, fp1, status); // X - N L_LEAD
		fp0 = floatx80_sub(fp0, fp2, status); // X - N L_TRAIL
		fp2 = packFloatx80(0, 0x4000, 0x935D8DDDAAA8AC17ull); // LOG10
		fp0 = floatx80_mul(fp0, fp2, status); // R
		
		// EXPR
		fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
		fp2 = float64_to_floatx80(0x3F56C16D6F7BD0B2ull, status); // A5
		fp3 = float64_to_floatx80(0x3F811112302C712Cull, status); // A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*A5
		fp3 = floatx80_mul(fp3, fp1, status); // S*A4
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FA5555555554CC1ull, status), status); // A3+S*A5
		fp3 = floatx80_add(fp3, float64_to_floatx80(0x3FC5555555554A54ull, status), status); // A2+S*A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp1, status); // S*(A2+S*A4)
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FE0000000000000ull, status), status); // A1+S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp0, status); // R*S*(A2+S*A4)
		
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A1+S*(A3+S*A5))
		fp0 = floatx80_add(fp0, fp3, status); // R+R*S*(A2+S*A4)
		fp0 = floatx80_add(fp0, fp2, status); // EXP(R) - 1
		
		fp0 = floatx80_mul(fp0, fact1, status);
		fp0 = floatx80_add(fp0, fact2, status);
		fp0 = floatx80_add(fp0, fact1, status);
		
		resetPrec(status, user_prec);

		a = floatx80_mul(fp0, adjfact, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

/*----------------------------------------------------------------------------
 | 2 to x
 *----------------------------------------------------------------------------*/

inline floatx80 floatx80_twotox(floatx80 a, float_status *status)
{
	flag aSign;
	int32_t aExp;
	uint64_t aSig;
	
	int32_t compact, n, j, l, m, m1;
	floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;
	
	aSig = extractFloatx80Frac(a);
	aExp = extractFloatx80Exp(a);
	aSign = extractFloatx80Sign(a);
	
	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
		if (aSign) return packFloatx80(0, 0, 0);
		return a;
	}
	
	if (aExp == 0 && aSig == 0) {
		return packFloatx80(0, one_exp, one_sig);
	}
	
	const PrecSave user_prec = setPrec(status);
	
	fp0 = a;
	
	compact = floatx80_make_compact(aExp, aSig);
	
	if (compact < 0x3FB98000 || compact > 0x400D80C0) { // |X| > 16480 or |X| < 2^(-70)
		if (compact > 0x3FFF8000) { // |X| > 16480
			resetPrec(status, user_prec);
			
			if (aSign) {
				return roundAndPackFloatx80(status->roundingPrecision, 0, -0x1000, aSig, 0, status);
			} else {
				return roundAndPackFloatx80(status->roundingPrecision, 0, 0x8000, aSig, 0, status);
			}
		} else { // |X| < 2^(-70)
			resetPrec(status, user_prec);
			
			a = floatx80_add(fp0, float32_to_floatx80(0x3F800000, status), status); // 1 + X
			
			float_raise(float_flag_inexact, status);
			
			return a;
		}
	} else { // 2^(-70) <= |X| <= 16480
		fp1 = fp0; // X
		fp1 = floatx80_mul(fp1, float32_to_floatx80(0x42800000, status), status); // X * 64
		n = floatx80_to_int32(fp1, status);
		fp1 = int32_to_floatx80(n);
		j = n & 0x3F;
		l = n / 64; // NOTE: this is really arithmetic right shift by 6
		if (n < 0 && j) { // arithmetic right shift is division and round towards minus infinity
			l--;
		}
		m = l / 2; // NOTE: this is really arithmetic right shift by 1
		if (l < 0 && (l & 1)) { // arithmetic right shift is division and round towards minus infinity
			m--;
		}
		m1 = l - m;
		m1 += 0x3FFF; // ADJFACT IS 2^(M')
		
		adjfact = packFloatx80(0, m1, one_sig);
		fact1 = exp2_tbl[j];
		fact1.high += m;
		fact2.high = exp2_tbl2[j]>>16;
		fact2.high += m;
		fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
		fact2.low <<= 48;
		
		fp1 = floatx80_mul(fp1, float32_to_floatx80(0x3C800000, status), status); // (1/64)*N
		fp0 = floatx80_sub(fp0, fp1, status); // X - (1/64)*INT(64 X)
		fp2 = packFloatx80(0, 0x3FFE, 0xB17217F7D1CF79ACull); // LOG2
		fp0 = floatx80_mul(fp0, fp2, status); // R
		
		// EXPR
		fp1 = floatx80_mul(fp0, fp0, status); // S = R*R
		fp2 = float64_to_floatx80(0x3F56C16D6F7BD0B2ull, status); // A5
		fp3 = float64_to_floatx80(0x3F811112302C712Cull, status); // A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*A5
		fp3 = floatx80_mul(fp3, fp1, status); // S*A4
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FA5555555554CC1ull, status), status); // A3+S*A5
		fp3 = floatx80_add(fp3, float64_to_floatx80(0x3FC5555555554A54ull, status), status); // A2+S*A4
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp1, status); // S*(A2+S*A4)
		fp2 = floatx80_add(fp2, float64_to_floatx80(0x3FE0000000000000ull, status), status); // A1+S*(A3+S*A5)
		fp3 = floatx80_mul(fp3, fp0, status); // R*S*(A2+S*A4)
		
		fp2 = floatx80_mul(fp2, fp1, status); // S*(A1+S*(A3+S*A5))
		fp0 = floatx80_add(fp0, fp3, status); // R+R*S*(A2+S*A4)
		fp0 = floatx80_add(fp0, fp2, status); // EXP(R) - 1
		
		fp0 = floatx80_mul(fp0, fact1, status);
		fp0 = floatx80_add(fp0, fact2, status);
		fp0 = floatx80_add(fp0, fact1, status);
		
		resetPrec(status, user_prec);
		
		a = floatx80_mul(fp0, adjfact, status);
		
		float_raise(float_flag_inexact, status);
		
		return a;
	}
}

} // namespace fpsp
} // namespace sf
