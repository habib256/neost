// =============================================================================
//  BitmapView.cpp — cf. BitmapView.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/BitmapView.hpp"

#include "core/Shifter.hpp"

namespace neost {

int bitmapPlanes(int planes) {
    return (planes == 1 || planes == 2) ? planes : 4;
}

int bitmapWidthPx(const BitmapLayout& l) {
    const int groupBytes = 2 * bitmapPlanes(l.planes);
    if (l.strideBytes <= 0) return 0;
    return (l.strideBytes / groupBytes) * 16;
}

std::size_t bitmapBytes(const BitmapLayout& l) {
    if (l.strideBytes <= 0 || l.height <= 0) return 0;
    return static_cast<std::size_t>(l.strideBytes) * static_cast<std::size_t>(l.height);
}

void bitmapPalette(const uint16_t stPal[16], int planes, bool mono, bool gray,
                   uint32_t out[16]) {
    planes = bitmapPlanes(planes);
    const int colors = 1 << planes;
    if (gray) {
        // Rampe régulière de l'index 0 (noir) au dernier index (blanc).
        for (int i = 0; i < 16; ++i) {
            const int k = (i < colors) ? i : colors - 1;
            const uint32_t v = static_cast<uint32_t>((k * 255) / (colors - 1));
            out[i] = 0xFF000000u | (v << 16) | (v << 8) | v;
        }
        return;
    }
    if (mono && planes == 1) {
        // Moniteur monochrome : seule la polarité vient du registre 0, comme
        // Shifter::renderLine (« inv = (palette[0] & 0x777) ? 0 : 1 »).
        const bool inv = (stPal[0] & 0x777) == 0;
        const uint32_t white = 0xFFFFFFFFu, black = 0xFF000000u;
        for (int i = 0; i < 16; ++i) out[i] = ((i & 1) ^ (inv ? 1 : 0)) ? black : white;
        return;
    }
    // Couleur : les registres 0..2^plans-1, comme le Shifter les applique (moyenne
    // résolution = registres 0-3, haute sur moniteur couleur = registres 0-1).
    for (int i = 0; i < 16; ++i) out[i] = Shifter::stColorToArgb(stPal[i]);
}

uint8_t bitmapPixelIndex(const uint8_t* row, int planes, int x) {
    planes = bitmapPlanes(planes);
    const uint8_t* g = row + (x >> 4) * 2 * planes;
    const int bit = 15 - (x & 15);
    uint8_t idx = 0;
    for (int p = 0; p < planes; ++p) {
        const unsigned w = (unsigned(g[2 * p]) << 8) | g[2 * p + 1];   // big-endian
        idx |= uint8_t(((w >> bit) & 1u) << p);
    }
    return idx;
}

void decodeBitmap(const BitmapLayout& l, const uint8_t* src, const uint8_t* valid,
                  const uint32_t pal[16], uint32_t voidColor, uint32_t* out) {
    const int planes = bitmapPlanes(l.planes);
    const int w = bitmapWidthPx(l);
    const int groupBytes = 2 * planes;
    for (int y = 0; y < l.height; ++y) {
        const std::size_t rowOff = static_cast<std::size_t>(y) * static_cast<std::size_t>(l.strideBytes);
        const uint8_t* row = src + rowOff;
        uint32_t* dst = out + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
        for (int g = 0; g < w / 16; ++g) {
            bool ok = true;
            if (valid) {
                const uint8_t* v = valid + rowOff + static_cast<std::size_t>(g) * groupBytes;
                for (int b = 0; b < groupBytes; ++b) ok = ok && v[b];
            }
            for (int px = 0; px < 16; ++px) {
                const int x = g * 16 + px;
                dst[x] = ok ? pal[bitmapPixelIndex(row, planes, x)] : voidColor;
            }
        }
    }
}

}  // namespace neost
