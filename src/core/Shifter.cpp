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

// Verrouille la résolution de la trame : le décodage ligne à ligne s'y tient
// (un changement de $FF8260 en cours de trame ne prend effet qu'à la suivante,
// comme l'ancien renderFrame qui figeait la rés. au moment du décodage).
void Shifter::beginFrame() {
    frameMode_ = mode;
    resizeFor(frameMode_);
}

// Décode UNE scanline avec l'état COURANT des registres (palette, base vidéo).
void Shifter::renderLine(int y) {
    if (y < 0 || y >= curH_) return;
    uint32_t* dst = frame_.data() + static_cast<std::size_t>(y) * curW_;

    if (frameMode_ == Mode::High) {
        // Haute résolution = moniteur MONOCHROME : blanc (0) / noir (1), sans
        // tenir compte de la palette couleur (sinon un palette[1] non noir
        // — ex. rouge sous TOS 1.02 — colore l'écran à tort).
        const uint32_t base = videoBase + static_cast<uint32_t>(y) * 80u;
        for (int g = 0; g < curW_ / 16; ++g) {
            const uint16_t p0 = bus_.read16(base + static_cast<uint32_t>(g) * 2u);
            for (int bit = 15; bit >= 0; --bit)
                *dst++ = ((p0 >> bit) & 1) ? 0xFF000000u : 0xFFFFFFFFu;
        }
    } else if (frameMode_ == Mode::Medium) {
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

// Décode toute la trame d'un coup (repli / appel direct hors ordonnanceur).
void Shifter::renderFrame() {
    beginFrame();
    for (int y = 0; y < curH_; ++y) renderLine(y);
}

uint8_t Shifter::read8(uint32_t addr) {
    // Palette $FF8240-$FF825F : 16 mots, big-endian.
    if (addr >= 0xFF8240 && addr < 0xFF8260) {
        const int i = (addr - 0xFF8240) / 2;
        return (addr & 1) ? static_cast<uint8_t>(palette[i])
                          : static_cast<uint8_t>(palette[i] >> 8);
    }
    // Compteur d'adresse vidéo (lecture seule) : position courante du balayage.
    // $FF8205 = bits 16-23, $FF8207 = 8-15, $FF8209 = 0-7 (cf. Hatari
    // Video_ScreenCounter_ReadByte). Certains diagnostics (Test Kit) attendent
    // que ce compteur reflète la base vidéo + l'avance du faisceau.
    if (addr == 0xFF8205) return static_cast<uint8_t>(videoCounter() >> 16);
    if (addr == 0xFF8207) return static_cast<uint8_t>(videoCounter() >> 8);
    if (addr == 0xFF8209) return static_cast<uint8_t>(videoCounter());
    if (addr == 0xFF8260) return static_cast<uint8_t>(mode);
    return 0x00;
}

// Reconstruit l'adresse vidéo courante (Hatari Video_CalculateAddress, simplifié) :
// au sommet de la trame le compteur vaut videoBase ; il avance de `bpl` octets par
// ligne affichée (fenêtre Display-Enable), puis reste figé pendant le VBlank, et se
// recharge à videoBase à la trame suivante (remise à zéro de l'horloge faisceau).
uint32_t Shifter::videoCounter() const {
    if (!beamClock_) return videoBase & 0xFFFFFF;       // pas d'horloge → base brute
    const int64_t fc = beamClock_();                    // cycles dans la trame
    constexpr int kCyclesPerLine = 512;
    const int  bpl  = (frameMode_ == Mode::High) ? 80 : 160;   // octets/ligne affichée
    const int  disp = (frameMode_ == Mode::High) ? 400 : 200;  // lignes affichées
    const int  deEnd = 376;                             // fin Display-Enable (cf. Machine)
    const int  line = static_cast<int>(fc / kCyclesPerLine);
    uint32_t addr = videoBase;
    if (line >= disp) {
        addr += static_cast<uint32_t>(disp) * bpl;      // écran entièrement lu (avant VBL)
    } else if (line > 0 || (fc % kCyclesPerLine) > (deEnd - bpl)) {
        addr += static_cast<uint32_t>(line) * bpl;
        int into = static_cast<int>(fc % kCyclesPerLine) - (deEnd - bpl);  // ~1 octet/cycle
        if (into < 0) into = 0; else if (into > bpl) into = bpl;
        addr += static_cast<uint32_t>(into);
    }
    return addr & 0xFFFFFF;
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
