// =============================================================================
// SoftFloatDecimal.hpp — conversions décimales empaquetées du MC68881.
// Travail dérivé de SoftFloat-2a, John R. Hauser, et de son extension décimale
// WinUAE/Previous lue dans Hatari, épingle f0736b24b32b0439300b52107ba6ab434469ec3c,
// src/cpu/softfloat/softfloat_decimal.c. Algorithmes et arrondis conservés ; les
// primitives multi-mots utilisent les entiers exacts de SoftFloatX80.hpp.
// Aucune conversion par la libc ni par un flottant hôte.
// =============================================================================
/*
===============================================================================
This C header file is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2a.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

#pragma once
#include "io/SoftFloatX80.hpp"

namespace sf {
namespace decimal_detail {
inline void setRoundingMode(int mode, Status* status) { status->roundingMode = mode; }

// Produit complet de deux significandes de 128 bits, en quatre mots de 64 bits.
inline void mul128To256(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1,
                        uint64_t& z0, uint64_t& z1, uint64_t& z2, uint64_t& z3) {
    const uint64_t a[2] = {a1, a0}, b[2] = {b1, b0};
    uint64_t z[4] = {};
    for (int i = 0; i < 2; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < 2; ++j) {
            const __uint128_t p = __uint128_t(a[i]) * b[j] + z[i + j] + carry;
            z[i + j] = uint64_t(p);
            carry = uint64_t(p >> 64);
        }
        z[i + 2] = carry;
    }
    z0 = z[3]; z1 = z[2]; z2 = z[1]; z3 = z[0];
}
inline void mul128By64To192(uint64_t a0, uint64_t a1, uint64_t b,
                           uint64_t& z0, uint64_t& z1, uint64_t& z2) {
    const __uint128_t lo = __uint128_t(a1) * b;
    const __uint128_t hi = __uint128_t(a0) * b + uint64_t(lo >> 64);
    z2 = uint64_t(lo); z1 = uint64_t(hi); z0 = uint64_t(hi >> 64);
}

inline void round128to64(bool aSign, int32_t *aExp, uint64_t *aSig0, uint64_t *aSig1, Status *status)
{
	bool increment;
	int32_t zExp;
	uint64_t zSig0, zSig1;

	zExp = *aExp;
	zSig0 = *aSig0;
	zSig1 = *aSig1;

	increment = ( (int64_t) zSig1 < 0 );
	if (status->roundingMode != round_nearest_even) {
		if (status->roundingMode == round_to_zero) {
			increment = 0;
		} else {
			if (aSign) {
				increment = (status->roundingMode == round_down) && zSig1;
			} else {
				increment = (status->roundingMode == round_up) && zSig1;
			}
		}
	}

	if (increment) {
		++zSig0;
		if (zSig0 == 0) {
			++zExp;
			zSig0 = 0x8000000000000000ull;
		} else {
			if ((zSig1 << 1) == 0 && status->roundingMode == round_nearest_even)
				zSig0 &= ~1;
		}
	} else {
		if ( zSig0 == 0 ) zExp = 0;
	}

	*aExp = zExp;
	*aSig0 = zSig0;
	*aSig1 = 0;
}

inline void mul128by128round(int32_t *aExp, uint64_t *aSig0, uint64_t *aSig1, int32_t bExp, uint64_t bSig0, uint64_t bSig1, Status *status)
{
	int32_t zExp;
	uint64_t zSig0, zSig1, zSig2, zSig3;

	zExp = *aExp;
	zSig0 = *aSig0;
	zSig1 = *aSig1;

	round128to64(0, &bExp, &bSig0, &bSig1, status);

	zExp += bExp - 0x3FFE;
	mul128To256(zSig0, zSig1, bSig0, bSig1, zSig0, zSig1, zSig2, zSig3);
	zSig1 |= (zSig2 | zSig3) != 0;
	if ( 0 < (int64_t) zSig0 ) {
		shortShift128Left( zSig0, zSig1, 1, zSig0, zSig1 );
		--zExp;
	}
	*aExp = zExp;
	*aSig0 = zSig0;
	*aSig1 = zSig1;

	round128to64(0, aExp, aSig0, aSig1, status);
}

inline void mul128by128(int32_t *aExp, uint64_t *aSig0, uint64_t *aSig1, int32_t bExp, uint64_t bSig0, uint64_t bSig1)
{
	int32_t zExp;
	uint64_t zSig0, zSig1, zSig2, zSig3;

	zExp = *aExp;
	zSig0 = *aSig0;
	zSig1 = *aSig1;

	zExp += bExp - 0x3FFE;
	mul128To256(zSig0, zSig1, bSig0, bSig1, zSig0, zSig1, zSig2, zSig3);
	zSig1 |= (zSig2 | zSig3) != 0;
	if ( 0 < (int64_t) zSig0 ) {
		shortShift128Left( zSig0, zSig1, 1, zSig0, zSig1 );
		--zExp;
	}
	*aExp = zExp;
	*aSig0 = zSig0;
	*aSig1 = zSig1;
}

inline void div128by128(int32_t *paExp, uint64_t *paSig0, uint64_t *paSig1, int32_t bExp, uint64_t bSig0, uint64_t bSig1)
{
	int32_t zExp, aExp;
	uint64_t zSig0, zSig1, aSig0, aSig1;
	uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

	aExp = *paExp;
	aSig0 = *paSig0;
	aSig1 = *paSig1;

	zExp = aExp - bExp + 0x3FFE;
	if ( le128( bSig0, bSig1, aSig0, aSig1 ) ) {
		shift128Right( aSig0, aSig1, 1, aSig0, aSig1 );
		++zExp;
	}
	zSig0 = estimateDiv128To64( aSig0, aSig1, bSig0 );
	mul128By64To192( bSig0, bSig1, zSig0, term0, term1, term2 );
	sub192( aSig0, aSig1, 0, term0, term1, term2, rem0, rem1, rem2 );
	while ( (int64_t) rem0 < 0 ) {
		--zSig0;
		add192( rem0, rem1, rem2, 0, bSig0, bSig1, rem0, rem1, rem2 );
	}
	zSig1 = estimateDiv128To64( rem1, rem2, bSig0 );
	if ( ( zSig1 & 0x3FFF ) <= 4 ) {
		mul128By64To192( bSig0, bSig1, zSig1, term1, term2, term3 );
		sub192( rem1, rem2, 0, term1, term2, term3, rem1, rem2, rem3 );
		while ( (int64_t) rem1 < 0 ) {
			--zSig1;
			add192( rem1, rem2, rem3, 0, bSig0, bSig1, rem1, rem2, rem3 );
		}
		zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
	}

	*paExp = zExp;
	*paSig0 = zSig0;
	*paSig1 = zSig1;
}


inline void tentoint128(bool mSign, bool eSign, int32_t *aExp, uint64_t *aSig0, uint64_t *aSig1, int32_t scale, Status *status)
 {
    int save_rounding_mode;
    int32_t mExp;
    uint64_t mSig0, mSig1;

    save_rounding_mode = status->roundingMode;
    switch (status->roundingMode) {
        case round_nearest_even:
            break;
        case round_down:
            if (mSign != eSign) {
                setRoundingMode(round_up, status);
            }
            break;
        case round_up:
            if (mSign != eSign) {
                setRoundingMode(round_down, status);
            }
            break;
        case round_to_zero:
            if (eSign == 0) {
                setRoundingMode(round_down, status);
            } else {
                setRoundingMode(round_up, status);
            }
            break;
        default:
            break;
    }

	*aExp = 0x3FFF;
	*aSig0 = 0x8000000000000000ull;
	*aSig1 = 0;

	mExp = 0x4002;
	mSig0 = 0xA000000000000000ull;
	mSig1 = 0;

	while (scale) {
		if (scale & 1) {
			mul128by128round(aExp, aSig0, aSig1, mExp, mSig0, mSig1, status);
		}
		mul128by128(&mExp, &mSig0, &mSig1, mExp, mSig0, mSig1);
		scale >>= 1;
	}

	setRoundingMode(save_rounding_mode, status);
}



inline int64_t tentointdec(int32_t scale)
{
	uint64_t decM, decX;

	decX = 1;
	decM = 10;

	while (scale) {
		if (scale & 1) {
			decX *= decM;
		}
		decM *= decM;
		scale >>= 1;
	 }

	return decX;
}


inline int64_t float128toint64(bool zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, Status *status)
{
	int8_t roundingMode;
	bool roundNearestEven, increment;
	int64_t z;

	shift128RightJamming(zSig0, zSig1, 0x403E - zExp, zSig0, zSig1);

	roundingMode = status->roundingMode;
	roundNearestEven = (roundingMode == round_nearest_even);
	increment = ((int64_t)zSig1 < 0);
	if (!roundNearestEven) {
		if (roundingMode == round_to_zero) {
			increment = 0;
		} else {
			if (zSign) {
				increment = (roundingMode == round_down ) && zSig1;
			} else {
				increment = (roundingMode == round_up ) && zSig1;
			}
		}
	}
	if (increment) {
		++zSig0;
		zSig0 &= ~uint64_t(((zSig1 << 1) == 0) && roundNearestEven);
	}
	z = zSig0;
	if (zSig1) raise(*status, flag_inexact);
	return z;
}

inline int32_t getDecimalExponent(int32_t aExp, uint64_t aSig)
{
	bool zSign;
	int32_t zExp, shiftCount;
	uint64_t zSig0, zSig1;

	if (aSig == 0 || aExp == 0x3FFF) {
		return 0;
	}
	if (aExp < 0) {
		return -4932;
	}

	aSig ^= 0x8000000000000000ull;
	aExp -= 0x3FFF;
	zSign = (aExp < 0);
	aExp = zSign ? -aExp : aExp;
	shiftCount = 31 - (aExp ? __builtin_clz(uint32_t(aExp)) : 32);
	zExp = 0x3FFF + shiftCount;

	if (shiftCount < 0) {
		shortShift128Left(aSig, 0, -shiftCount, zSig0, zSig1);
	} else {
		shift128Right(aSig, 0, shiftCount, zSig0, zSig1);
		aSig = (uint64_t)aExp << (63 - shiftCount);
		if (zSign) {
			sub128(aSig, 0, zSig0, zSig1, zSig0, zSig1);
		} else {
			add128(aSig, 0, zSig0, zSig1, zSig0, zSig1);
		}
	}

	shiftCount = clz64(zSig0);
	shortShift128Left(zSig0, zSig1, shiftCount, zSig0, zSig1);
	zExp -= shiftCount;
	mul128by128(&zExp, &zSig0, &zSig1, 0x3FFD, 0x9A209A84FBCFF798ull, 0x8F8959AC0B7C9178ull);

	shiftCount = 0x403E - zExp;
	shift128RightJamming(zSig0, zSig1, shiftCount, zSig0, zSig1);

	if ((int64_t)zSig1 < 0) {
		++zSig0;
		zSig0 &= ~(((int64_t)(zSig1<<1) == 0) & 1);
	}

	zExp = (int32_t)(zSign ? (0 - zSig0) : zSig0);

	return zExp;
}

// Conversion décimale vers étendu.

inline f80 fromDecimalRaw(f80 a, Status *status)
{
	bool decSign, zSign, decExpSign;
	int32_t decExp, zExp, xExp, shiftCount;
	uint64_t decSig, zSig0, zSig1, xSig0, xSig1;

	decSign = signOf(a);
	decExp = expOf(a);
	decSig = fracOf(a);

	if (decExp == 0x7FFF) return a;

	// Un zéro reste signé quel que soit son exposant décimal.
	// Le source amont ne protège que exp==0 et décale sinon de 64 bits.
	if (decSig == 0) return pack(decSign, 0, 0);

	decExpSign = (decExp >> 14) & 1;
	decExp &= 0x3FFF;

	shiftCount = clz64( decSig );
	zExp = 0x403E - shiftCount;
	zSig0 = decSig << shiftCount;
	zSig1 = 0;
	zSign = decSign;

	tentoint128(decSign, decExpSign, &xExp, &xSig0, &xSig1, decExp, status);

	if (decExpSign) {
		div128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
	} else {
		mul128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
	}

	if (zSig1) raise(*status, flag_decimal);
	round128to64(zSign, &zExp, &zSig0, &zSig1, status);

	return pack( zSign, zExp, zSig0 );

}

// Conversion étendue vers décimal.

inline f80 toDecimalRaw(f80 a, int32_t *k, Status *status)
{
    // Comme les opérations binaires, refuser un encodage non normalisé.
    // Le source décimal amont ne le garde pas : certains trous FMOVECR
    // (exp non nul, man==0) conduisent à des décalages négatifs indéfinis.
    if (invalidEncoding(a)) { raise(*status, flag_invalid); return defaultNaN(); }
	bool aSign, decSign;
	int32_t aExp, decExp, zExp, xExp;
	uint64_t aSig, decSig, decX, zSig0, zSig1, xSig0, xSig1;
	bool ictr, lambda;
	int32_t kfactor, ilog, iscale, len;

	aSign = signOf(a);
	aExp = expOf(a);
	aSig = fracOf(a);

	if (aExp == 0x7FFF) {
		if ((uint64_t) (aSig<<1)) return propagateNaN1(*status, a);
		return a;
	}

	if (aExp == 0) {
		if (aSig == 0) return pack(aSign, 0, 0);
		normalizeSubnormal(aSig, aExp, aSig);
	}

	kfactor = *k;

	ilog = getDecimalExponent(aExp, aSig);

	ictr = 0;

try_again:

	if (kfactor > 0) {
		if (kfactor > 17) {
			kfactor = 17;
			raise(*status, flag_invalid);
		}
		len = kfactor;
	} else {
		len = ilog + 1 - kfactor;
		if (len > 17) {
			len = 17;
		}
		if (len < 1) {
			len = 1;
		}
		if (kfactor > ilog) {
			ilog = kfactor;
		}
	}


	lambda = 0;
	iscale = ilog + 1 - len;

	if (iscale < 0) {
		lambda = 1;
		iscale = -iscale;
	}


	tentoint128(lambda, 0, &xExp, &xSig0, &xSig1, iscale, status);


	zExp = aExp;
	zSig0 = aSig;
	zSig1 = 0;

	if (lambda) {
		mul128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
	} else {
		div128by128(&zExp, &zSig0, &zSig1, xExp, xSig0, xSig1);
	}


	decSig = float128toint64(aSign, zExp, zSig0, zSig1, status);


	if (ictr == 0) {

		decX = tentointdec(len - 1);

		if (decSig < decX) {
			ilog -= 1;
			ictr = 1;
			goto try_again;
		}

		decX *= 10;

		if (decSig > decX) {
			ilog += 1;
			ictr = 1;
			goto try_again;
		}
	}

	decSign = aSign;
	decExp = (ilog < 0) ? -ilog : ilog;
	if (decExp > 999) {
		raise(*status, flag_invalid);
	}
	if (ilog < 0) decExp |= 0x4000;

	*k = len;

	return pack(decSign, decExp, decSig);
}

} // namespace decimal_detail

inline f80 fromDecimal(f80 value, Status& status) {
    return decimal_detail::fromDecimalRaw(value, &status);
}
inline f80 toDecimal(f80 value, int& k, Status& status) {
    return decimal_detail::toDecimalRaw(value, &k, &status);
}

// Emballage 96 bits : port de fp_to_pack / fp_from_pack (fpp_softfloat.c).
// Les trois mots sont déjà assemblés dans l'ordre big-endian de la mémoire ST.
inline f80 fromPacked(const uint32_t* words, Status& status) {
    if (((words[0] >> 16) & 0x7FFF) == 0x7FFF)
        return f80{uint16_t(words[0] >> 16), (uint64_t(words[1]) << 32) | words[2]};
    if (!(words[0] & 15) && !words[1] && !words[2])
        return pack(words[0] >> 31, 0, 0);
    int exponent = 0;
    for (int i = 0; i < 3; ++i)
        exponent = exponent * 10 + ((words[0] >> (24 - i * 4)) & 15);
    if (words[0] & 0x40000000u) exponent = -exponent;
    exponent -= 16;
    uint64_t significand = words[0] & 15;
    const uint64_t fraction = (uint64_t(words[1]) << 32) | words[2];
    for (int i = 0; i < 16; ++i)
        significand = significand * 10 + ((fraction >> (60 - i * 4)) & 15);
    const uint16_t exp = uint16_t(exponent < 0 ? -exponent : exponent)
                       | (exponent < 0 ? 0x4000 : 0);
    return fromDecimal(pack(words[0] >> 31, exp, significand), status);
}
inline void toPacked(f80 value, int k, uint32_t* words, Status& status) {
    const f80 decimal = toDecimal(value, k, status);
    if (expOf(decimal) == 0x7FFF) {
        words[0] = uint32_t(decimal.high) << 16;
        words[1] = uint32_t(decimal.low >> 32);
        words[2] = uint32_t(decimal.low);
        return;
    }
    // Le retour anticipé de toDecimal pour zéro ne change pas k. L'itérer
    // directement (k négatif ou >17) produirait des décalages hors limites.
    if (value.low == 0) {
        words[0] = uint32_t(signOf(decimal)) << 31;
        words[1] = words[2] = 0;
        return;
    }
    uint64_t significand = decimal.low, fraction = 0;
    uint32_t integer = 0;
    for (int len = k; len > 0; ) {
        --len;
        const uint64_t digit = significand % 10;
        significand /= 10;
        if (len == 0) integer = uint32_t(digit);
        else fraction |= digit << (64 - len * 4);
    }
    uint32_t exponent = decimal.high & 0x3FFF;
    uint32_t packedExp = 0, fourthExpDigit = 0;
    for (int len = 4; len > 0; ) {
        --len;
        const uint32_t digit = exponent % 10;
        exponent /= 10;
        if (len == 0) fourthExpDigit = digit;
        else packedExp |= digit << (12 - len * 4);
    }
    words[0] = (packedExp << 16) | (fourthExpDigit << 12) | integer
             | ((decimal.high & 0x4000) ? 0x40000000u : 0)
             | ((decimal.high & 0x8000) ? 0x80000000u : 0);
    words[1] = uint32_t(fraction >> 32);
    words[2] = uint32_t(fraction);
}

} // namespace sf
