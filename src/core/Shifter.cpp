// =============================================================================
//  Shifter.cpp — Décodage planaire ST → texture OpenGL (immediate mode).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Shifter.hpp"

#if defined(__APPLE__)
#include <OpenGL/gl.h>      // macOS Silicon : framework OpenGL (legacy 2.1)
#else
#include <GL/gl.h>          // Linux : libGL (symboles legacy directs)
#endif

Shifter::Shifter(Bus& bus) : bus_(bus) {
    frame_.assign(WIDTH * HEIGHT, 0xFF000000u);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    // NEAREST : on veut des pixels ST nets, pas de filtrage.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, frame_.data());
}

Shifter::~Shifter() {
    if (texture_) glDeleteTextures(1, &texture_);
}

uint32_t Shifter::stColorToArgb(uint16_t c) {
    // Format ST : %0000 0RRR 0GGG 0BBB (3 bits par canal). On étire 3→8 bits.
    const uint8_t r = (c >> 8) & 0x7;
    const uint8_t g = (c >> 4) & 0x7;
    const uint8_t b = (c >> 0) & 0x7;
    auto ex = [](uint8_t v) -> uint32_t { return (v * 255u) / 7u; };
    return 0xFF000000u | (ex(r) << 16) | (ex(g) << 8) | ex(b);
}

void Shifter::decodeLineLow(int line) {
    // Basse résolution : 4 bitplans entrelacés. Chaque groupe de 16 pixels =
    // 4 mots de 16 bits (un par plan). Le bit n de chaque plan forme l'index
    // de palette du pixel n. 320 px / 16 = 20 groupes, soit 160 octets/ligne.
    const uint32_t lineAddr = videoBase + static_cast<uint32_t>(line) * 160u;
    uint32_t* dst = &frame_[static_cast<std::size_t>(line) * WIDTH];

    for (int group = 0; group < WIDTH / 16; ++group) {
        const uint32_t a = lineAddr + static_cast<uint32_t>(group) * 8u;
        const uint16_t p0 = bus_.read16(a + 0);
        const uint16_t p1 = bus_.read16(a + 2);
        const uint16_t p2 = bus_.read16(a + 4);
        const uint16_t p3 = bus_.read16(a + 6);

        for (int bit = 15; bit >= 0; --bit) {
            const uint16_t m = static_cast<uint16_t>(1u << bit);
            const int idx = ((p0 & m) ? 1 : 0) | ((p1 & m) ? 2 : 0)
                          | ((p2 & m) ? 4 : 0) | ((p3 & m) ? 8 : 0);
            *dst++ = stColorToArgb(palette[idx]);
        }
    }
}

void Shifter::renderScanline(int line) {
    if (line < 0 || line >= HEIGHT) return;   // hors zone visible (bordure/VBlank)
    // Pour l'instant seul le mode basse résolution est décodé (cas pédagogique).
    if (mode == Mode::Low) decodeLineLow(line);
}

void Shifter::present() {
    // 1) Téléversement du framebuffer ST dans la texture.
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT,
                    GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, frame_.data());

    // 2) Quad plein écran en immediate mode (repère NDC -1..+1, V inversé pour
    //    que la ligne 0 du ST soit en haut de l'écran).
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
        glTexCoord2f(0.f, 1.f); glVertex2f(-1.f, -1.f);
        glTexCoord2f(1.f, 1.f); glVertex2f( 1.f, -1.f);
        glTexCoord2f(1.f, 0.f); glVertex2f( 1.f,  1.f);
        glTexCoord2f(0.f, 0.f); glVertex2f(-1.f,  1.f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
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
