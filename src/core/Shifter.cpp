// =============================================================================
//  Shifter.cpp — Décodage planaire ST (basse/moyenne/haute) → buffer ARGB.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Shifter.hpp"

Shifter::Shifter(Bus& bus) : bus_(bus) {
    resizeFor(mode);
}

uint32_t Shifter::stColorToArgb(uint16_t c) {
    // Format ST : %0000 0RRR 0GGG 0BBB (3 bits par canal). On étire 3→8 bits.
    const uint8_t r = (c >> 8) & 0x7;
    const uint8_t g = (c >> 4) & 0x7;
    const uint8_t b = (c >> 0) & 0x7;
    auto ex = [](uint8_t v) -> uint32_t { return (v * 255u) / 7u; };
    return 0xFF000000u | (ex(r) << 16) | (ex(g) << 8) | ex(b);
}

void Shifter::resizeFor(Mode m) {
    int w = 320, h = 200;
    switch (m) {
        case Mode::Low:    w = 320; h = 200; break;   // 16 couleurs
        case Mode::Medium: w = 640; h = 200; break;   // 4 couleurs
        case Mode::High:   w = 640; h = 400; break;   // monochrome
    }
    if (w == curW_ && h == curH_) return;
    curW_ = w; curH_ = h;
    frame_.assign(static_cast<std::size_t>(w) * h, 0xFF000000u);
}

void Shifter::renderFrame() {
    resizeFor(mode);                                  // suit un éventuel changement de rés.
    uint32_t* dst = frame_.data();

    for (int y = 0; y < curH_; ++y) {
        if (mode == Mode::High) {
            // Haute résolution = moniteur MONOCHROME : blanc (0) / noir (1), sans
            // tenir compte de la palette couleur (sinon un palette[1] non noir
            // — ex. rouge sous TOS 1.02 — colore l'écran à tort).
            const uint32_t base = videoBase + static_cast<uint32_t>(y) * 80u;
            for (int g = 0; g < curW_ / 16; ++g) {
                const uint16_t p0 = bus_.read16(base + static_cast<uint32_t>(g) * 2u);
                for (int bit = 15; bit >= 0; --bit)
                    *dst++ = ((p0 >> bit) & 1) ? 0xFF000000u : 0xFFFFFFFFu;
            }
        } else if (mode == Mode::Medium) {
            // 2 plans entrelacés : 2 mots = 16 pixels. Index 0..3.
            const uint32_t base = videoBase + static_cast<uint32_t>(y) * 160u;
            for (int g = 0; g < curW_ / 16; ++g) {
                const uint32_t a = base + static_cast<uint32_t>(g) * 4u;
                const uint16_t p0 = bus_.read16(a + 0);
                const uint16_t p1 = bus_.read16(a + 2);
                for (int bit = 15; bit >= 0; --bit) {
                    const int idx = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1);
                    *dst++ = stColorToArgb(palette[idx]);
                }
            }
        } else {
            // Basse rés. : 4 plans entrelacés, 4 mots = 16 pixels. Index 0..15.
            const uint32_t base = videoBase + static_cast<uint32_t>(y) * 160u;
            for (int g = 0; g < curW_ / 16; ++g) {
                const uint32_t a = base + static_cast<uint32_t>(g) * 8u;
                const uint16_t p0 = bus_.read16(a + 0);
                const uint16_t p1 = bus_.read16(a + 2);
                const uint16_t p2 = bus_.read16(a + 4);
                const uint16_t p3 = bus_.read16(a + 6);
                for (int bit = 15; bit >= 0; --bit) {
                    const int idx = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1)
                                  | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3);
                    *dst++ = stColorToArgb(palette[idx]);
                }
            }
        }
    }
}

uint8_t Shifter::read8(uint32_t addr) {
    // Palette $FF8240-$FF825F : 16 mots, big-endian.
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        const int i = (addr - 0xFF8240) / 2;
        return (addr & 1) ? static_cast<uint8_t>(palette[i])
                          : static_cast<uint8_t>(palette[i] >> 8);
    }
    if (addr == 0xFF8260) return static_cast<uint8_t>(mode);
    return 0x00;
}

void Shifter::write8(uint32_t addr, uint8_t v) {
    // Adresse de base vidéo : octets haut ($FF8201) et milieu ($FF8203). Le bit
    // bas est fixé à 0 (le ST aligne le framebuffer sur 256 octets).
    switch (addr) {
        case 0xFF8201: videoBase = (videoBase & 0x00FF00) | (uint32_t(v) << 16); return;
        case 0xFF8203: videoBase = (videoBase & 0xFF0000) | (uint32_t(v) << 8);  return;
        case 0xFF8260: mode = static_cast<Mode>(v & 0x3); return;
        default: break;
    }
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        const int i = (addr - 0xFF8240) / 2;
        if (addr & 1) palette[i] = (palette[i] & 0xFF00) | v;
        else          palette[i] = static_cast<uint16_t>((palette[i] & 0x00FF) | (uint16_t(v) << 8));
    }
}
