// Comparaison indépendante au vrai C Hatari, compilé par test_fpu_decimal_oracle.py.
#include "io/SoftFloatDecimal.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
extern "C" {
#include "softfloat/softfloat.h"
floatx80 oracle_from_packed(const uint32_t*, float_status*);
void oracle_to_packed(floatx80, int, uint32_t*, float_status*);
}

namespace {
uint64_t rng = 0x9ABC1234DEF56780ull;
uint64_t random64() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
unsigned count = 0, failures = 0;
// NeoST réserve 0x02 à SNAN, Hatari à denormal ; les noms font foi.
unsigned flags(unsigned f) {
    return (f & 0x3D) | ((f & float_flag_signaling) ? sf::flag_signaling : 0)
                      | ((f & float_flag_decimal) ? sf::flag_decimal : 0);
}
void check(bool good, const char* what, sf::f80 value, int k, int mode) {
    ++count;
    if (!good && ++failures < 16)
        std::printf("FAIL %s value=%04x:%016llx k=%d mode=%d\n", what,
                    value.high, (unsigned long long)value.low, k, mode);
}
float_status oracleStatus(int mode) {
    float_status st{};
    st.float_rounding_mode = mode;
    st.floatx80_rounding_precision = 80;
    return st;
}
void raw(sf::f80 value, int k, int mode, bool output) {
    auto hs = oracleStatus(mode);
    sf::Status ns; ns.roundingMode = mode;
    int hk = k, nk = k;
    const floatx80 a{value.high, value.low};
    const auto expected = output ? floatx80_to_floatdecimal(a, &hk, &hs)
                                 : floatdecimal_to_floatx80(a, &hs);
    const auto actual = output ? sf::toDecimal(value, nk, ns) : sf::fromDecimal(value, ns);
    check(actual.high == expected.high && actual.low == expected.low
          && ns.exceptionFlags == flags(hs.float_exception_flags) && (!output || nk == hk),
          output ? "toDecimal" : "fromDecimal", value, k, mode);
}
void packedInput(const uint32_t* words, int mode) {
    auto hs = oracleStatus(mode);
    sf::Status ns; ns.roundingMode = mode;
    const auto expected = oracle_from_packed(words, &hs);
    const auto actual = sf::fromPacked(words, ns);
    check(actual.high == expected.high && actual.low == expected.low
          && ns.exceptionFlags == flags(hs.float_exception_flags), "fromPacked", actual, 0, mode);
}
void packedOutput(sf::f80 value, int k, int mode) {
    auto hs = oracleStatus(mode);
    sf::Status ns; ns.roundingMode = mode;
    uint32_t actual[3], expected[3];
    oracle_to_packed({value.high, value.low}, k, expected, &hs);
    sf::toPacked(value, k, actual, ns);
    check(std::memcmp(actual, expected, sizeof actual) == 0
          && ns.exceptionFlags == flags(hs.float_exception_flags), "toPacked", value, k, mode);
}
}

int main(int argc, char** argv) {
    const int cases = argc > 1 ? std::atoi(argv[1]) : 10000;
    const sf::f80 binaryEdges[] = {
        {0, 0}, {0x8000, 0}, {0, 1}, {0, 0x7FFFFFFFFFFFFFFFull},
        {1, 0x8000000000000000ull}, {0x3FFF, 0x8000000000000000ull},
        {0x3FFF, 0xA000000000000000ull}, {0xBFFF, 0xA000000000000000ull},
        {0x3FFD, 0xAAAAAAAAAAAAAAABull}, {0x7FFE, ~uint64_t(0)},
        {0x7FFF, 0}, {0xFFFF, 0}, {0x7FFF, 0xC000000000012345ull},
        {0xFFFF, 0x0000000000012345ull}
    };
    const uint32_t packedEdges[][3] = {
        {0x00000001, 0x23456789, 0x01234567}, {0x40010001, 0, 0},
        {0x80000001, 0x25000000, 0}, {0x09990001, 0, 0}, {0x49990001, 0, 0},
        {0x99990000, 0, 0}, {0x7FFF0000, 0, 0}, {0xFFFF0000, 0, 0},
        {0x7FFF0000, 0xC0000000, 0x12345}, {0xFFFF0000, 0, 0x12345},
        {0x0FFF000F, 0xFFFFFFFF, 0xFFFFFFFF}
    };
    const int ks[] = {-64, -17, -2, -1, 0, 1, 2, 16, 17, 18, 63};
    for (int mode = 0; mode < 4; ++mode) {
        for (const auto& words : packedEdges) packedInput(words, mode);
        for (auto value : binaryEdges) for (int k : ks) {
            raw(value, k, mode, true);
            // Le wrapper C amont décale hors limites pour zéro et k>17.
            // Comparer ces cas à la sémantique signée zéro explicitement ci-dessous.
            if (value.low || k <= 17) packedOutput(value, k, mode);
        }
        for (int i = 0; i < cases; ++i) {
            const int exponent = int(random64() % 2032) - 1016;
            const sf::f80 decimal{uint16_t((random64() & 1 ? 0x8000 : 0)
                | (exponent < 0 ? 0x4000 : 0) | (exponent < 0 ? -exponent : exponent)),
                1 + random64() % 100000000000000000ull};
            raw(decimal, 0, mode, false);
            const int exp = int(random64() % 0x7FFF);
            const sf::f80 value{uint16_t(exp | (random64() & 1 ? 0x8000 : 0)),
                random64() | (exp ? 0x8000000000000000ull : 1)};
            const int k = int(random64() % 128) - 64;
            raw(value, k, mode, true);
            packedOutput(value, k, mode);
            uint32_t words[3] = {};
            words[0] = uint32_t(random64() & 0xC0000000u);
            for (int j = 0; j < 3; ++j) words[0] |= (random64() % 10) << (16 + j * 4);
            words[0] |= 1 + random64() % 9;
            for (int j = 0; j < 16; ++j)
                words[1 + j / 8] |= (random64() % 10) << (28 - 4 * (j % 8));
            packedInput(words, mode);
        }
        // Durcissements explicitement distincts de l'oracle C dont ces entrées
        // provoquent des décalages indéfinis : ils doivent aussi passer UBSan.
        for (uint16_t sign : {uint16_t(0), uint16_t(0x8000)}) {
            sf::Status st; st.roundingMode = mode;
            const auto zero = sf::fromDecimal({uint16_t(sign | 999), 0}, st);
            check(zero.high == sign && zero.low == 0 && !st.exceptionFlags,
                  "decimal zero", zero, 0, mode);
            for (int k : ks) {
                uint32_t words[3];
                sf::toPacked({sign, 0}, k, words, st);
                check(words[0] == uint32_t(sign) << 16 && !words[1] && !words[2]
                      && !st.exceptionFlags, "packed zero", {sign, 0}, k, mode);
            }
        }
        for (uint16_t exp : {uint16_t(0x43FF), uint16_t(0x7525)}) {
            sf::Status st; st.roundingMode = mode;
            uint32_t words[3]; sf::toPacked({exp, 0}, 17, words, st);
            check(words[0] == 0x7FFF0000 && words[1] == 0xFFFFFFFF && words[2] == 0xFFFFFFFF
                  && st.exceptionFlags == sf::flag_invalid, "unnormal", {exp, 0}, 17, mode);
        }
    }
    std::printf("FPU decimal oracle: %u checks, %u failures\n", count, failures);
    return failures ? 1 : 0;
}
