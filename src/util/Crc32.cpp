// =============================================================================
//  Crc32.cpp — cf. Crc32.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/Crc32.hpp"

#include <cstring>

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

namespace neost {

namespace {

// Tables du découpage par 8 : t[0] est la table classique octet par octet, t[k] fait
// avancer un octet de k positions supplémentaires.
struct Tables {
    uint32_t t[8][256];
};

const Tables& tables() {
    static const Tables k = [] {
        Tables x{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            x.t[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i)
            for (int s = 1; s < 8; ++s)
                x.t[s][i] = (x.t[s - 1][i] >> 8) ^ x.t[0][x.t[s - 1][i] & 0xFFu];
        return x;
    }();
    return k;
}

constexpr bool kLittleEndian =
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#else
    false;   // ordre inconnu : le chemin octet par octet, toujours juste
#endif

}  // namespace

uint32_t crc32Bytewise(const uint8_t* p, std::size_t n) {
    const auto& t = tables().t[0];
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

uint32_t crc32Sliced(const uint8_t* p, std::size_t n) {
    if (!kLittleEndian) return crc32Bytewise(p, n);
    const auto& t = tables().t;
    uint32_t c = 0xFFFFFFFFu;
    while (n >= 8) {
        uint32_t lo, hi;
        std::memcpy(&lo, p, 4);
        std::memcpy(&hi, p + 4, 4);
        lo ^= c;
        c = t[7][lo & 0xFFu] ^ t[6][(lo >> 8) & 0xFFu] ^ t[5][(lo >> 16) & 0xFFu] ^ t[4][lo >> 24]
          ^ t[3][hi & 0xFFu] ^ t[2][(hi >> 8) & 0xFFu] ^ t[1][(hi >> 16) & 0xFFu] ^ t[0][hi >> 24];
        p += 8; n -= 8;
    }
    while (n--) c = t[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

bool crc32Hardware() {
#if defined(__ARM_FEATURE_CRC32)
    return true;
#else
    return false;
#endif
}

uint32_t crc32(const uint8_t* p, std::size_t n) {
#if defined(__ARM_FEATURE_CRC32)
    // [B] Arm Architecture Reference Manual (DDI 0487), CRC32B/CRC32X : polynôme
    // $04C11DB7 en ordre réfléchi, sans inversion — l'appelant pose init et inversion,
    // comme pour la table. Mots lus en petit-boutiste (AArch64 sous macOS, Linux, Android).
    uint32_t c = 0xFFFFFFFFu;
    while (n >= 8) {
        uint64_t v;
        std::memcpy(&v, p, 8);
        c = __crc32d(c, v);
        p += 8; n -= 8;
    }
    while (n--) c = __crc32b(c, *p++);
    return ~c;
#else
    return crc32Sliced(p, n);
#endif
}

}  // namespace neost
