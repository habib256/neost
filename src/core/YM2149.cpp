// =============================================================================
//  YM2149.cpp — Synthèse des 3 voies + bruit du PSG.
//
//  Moteur interne 250 kHz (port Hatari YM2149_DoSamples_250) + rééchantillonnage
//  pondéré N vers la fréquence de sortie — précision cycle pour sync-buzzer/syncsquare.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/YM2149.hpp"

#include <cmath>
#include <cstdint>

// Conversion volume fixe 4 bits → index 5 bits dans le DAC (Hatari YmVolume4to5) :
// volume5 = volume4*2+1, sauf 0 et 1 qui restent 0 et 1 → [0,15] mappé sur [0,31].
const std::array<uint8_t, 16> YM2149::kVolume4to5 = {
    0, 1, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31
};

// Coefficient du passe-haut sous-sonique (DC blocker 1er ordre). Mêmes valeurs que
// Hatari (Subsonic_IIR_HPF, sound.c:382-394) : pôle = 1 - 64/32768 ≈ 0.99805, soit
// fc ≈ 15 Hz à 48 kHz. Recentre le signal DAC unipolaire (couplage AC du vrai HW).
static constexpr double kHpfPole = 1.0 - 64.0 / 32768.0;

namespace {
constexpr uint16_t kMaskVoice[3] = { 0x001f, 0x03e0, 0x7c00 };

enum : int { ENV_GODOWN = 0, ENV_GOUP = 1, ENV_DOWN = 2, ENV_UP = 3 };

// 16 formes d'enveloppe × 3 blocs de 32 volumes (YmEnvDef + YM2149_EnvBuild, sound.c).
constexpr int kYmEnvDef[16][3] = {
    { ENV_GODOWN, ENV_DOWN, ENV_DOWN }, { ENV_GODOWN, ENV_DOWN, ENV_DOWN },
    { ENV_GODOWN, ENV_DOWN, ENV_DOWN }, { ENV_GODOWN, ENV_DOWN, ENV_DOWN },
    { ENV_GOUP,   ENV_DOWN, ENV_DOWN }, { ENV_GOUP,   ENV_DOWN, ENV_DOWN },
    { ENV_GOUP,   ENV_DOWN, ENV_DOWN }, { ENV_GOUP,   ENV_DOWN, ENV_DOWN },
    { ENV_GODOWN, ENV_GODOWN, ENV_GODOWN }, { ENV_GODOWN, ENV_DOWN, ENV_DOWN },
    { ENV_GODOWN, ENV_GOUP, ENV_GODOWN }, { ENV_GODOWN, ENV_UP, ENV_UP },
    { ENV_GOUP,   ENV_GOUP, ENV_GOUP }, { ENV_GOUP,   ENV_UP, ENV_UP },
    { ENV_GOUP,   ENV_GODOWN, ENV_GOUP }, { ENV_GOUP,   ENV_DOWN, ENV_DOWN },
};

uint16_t mergeVoice(int c, int b, int a) {
    return uint16_t((c << 10) | (b << 5) | a);
}

uint16_t tonePeriod(uint8_t hi, uint8_t lo) {
    return uint16_t(((hi & 0x0f) << 8) | lo);
}

// Bruit blanc : LFSR 17 étages, 2 taps (17,14) — YM2149_RndCompute, sound.c:969.
uint32_t rndCompute(uint32_t& rack) {
    if (rack & 1u) {
        rack = (rack >> 1) ^ 0x12000u;
        return 0xffffu;
    }
    rack >>= 1;
    return 0u;
}

int32_t sampleToFixed(float s) {
    return int32_t(std::lround(double(s) * 32768.0));
}

float fixedToSample(int64_t v) {
    return float(double(v) / 32768.0);
}
} // namespace

// -----------------------------------------------------------------------------
//  Table de conversion DAC 32×32×32 → échantillon (modèle de circuit Hatari).
// -----------------------------------------------------------------------------
const std::array<float, 32768>& YM2149::dacTable() {
    static const std::array<float, 32768> table = [] {
        constexpr double MaxVol = 65535.0, FOURTH2 = 1.19, WARP = 1.6666666666666667;
        constexpr float  kDacMakeup = 1.0f;

        double cond = 2.0 / 3.0 / (1.0 - 1.0 / WARP) - 2.0 / 3.0;
        double c[32];
        for (int i = 31; i >= 1; --i) {
            c[i] = cond / 2.0;
            cond = 1.0 / (1.0 - 1.0 / FOURTH2 / (1.0 / cond + 1.0)) - 1.0;
        }
        c[0] = 1.0e-8;

        const double max = (MaxVol * WARP) / (1.0 + 1.0 / (c[31] + c[31] + c[31]));
        std::array<float, 32768> t{};
        for (int idx = 0; idx < 32768; ++idx) {
            const int a = idx & 31, b = (idx >> 5) & 31, cc = (idx >> 10) & 31;
            const double v = (MaxVol * WARP) / (1.0 + 1.0 / (c[a] + c[b] + c[cc]));
            t[idx] = float(v / max) * kDacMakeup;
        }
        return t;
    }();
    return table;
}

const std::array<std::array<uint16_t, 96>, 16>& YM2149::envWaves() {
    static const std::array<std::array<uint16_t, 96>, 16> waves = [] {
        std::array<std::array<uint16_t, 96>, 16> w{};
        for (int env = 0; env < 16; ++env)
            for (int block = 0; block < 3; ++block) {
                int vol = 0, inc = 0;
                switch (kYmEnvDef[env][block]) {
                    case ENV_GODOWN: vol = 31; inc = -1; break;
                    case ENV_GOUP:   vol = 0;  inc = 1;  break;
                    case ENV_DOWN:   vol = 0;  inc = 0;  break;
                    case ENV_UP:     vol = 31; inc = 0;  break;
                }
                for (int i = 0; i < 32; ++i) {
                    w[env][block * 32 + i] = mergeVoice(vol, vol, vol);
                    vol += inc;
                }
            }
        return w;
    }();
    return waves;
}

void YM2149::updateFromRegs(const uint8_t* r) {
    tonePer_[0] = tonePeriod(r[1], r[0]);
    tonePer_[1] = tonePeriod(r[3], r[2]);
    tonePer_[2] = tonePeriod(r[5], r[4]);
    noisePer_   = std::max(1u, uint32_t(r[6] & 0x1f));
    uint32_t ep = (r[12] << 8) | r[11];
    envPer_     = uint16_t(std::max(1u, ep));
    envShape_   = r[13] & 0x0f;

    const uint8_t mix = r[7];
    for (int ch = 0; ch < 3; ++ch) {
        mixerT_[ch] = (mix & (1u << ch))       ? 0xffffu : 0u;
        mixerN_[ch] = (mix & (1u << (ch + 3))) ? 0xffffu : 0u;
    }

    envMask3_ = 0;
    vol3_     = 0;
    for (int ch = 0; ch < 3; ++ch) {
        const uint8_t vreg = r[8 + ch];
        if (vreg & 0x10) {
            envMask3_ |= kMaskVoice[ch];
        } else {
            vol3_ &= ~kMaskVoice[ch];
            vol3_ |= uint16_t(kVolume4to5[vreg & 0x0f]) << (5 * ch);
        }
    }
}

float YM2149::applyPwm250(float x0) {
    float y;
    if (x0 >= lpf250Y0_) {
        y = x0;
    } else {
        y = (3.0f * (x0 + lpf250X1_) + 2.0f * lpf250Y0_) * 0.125f;
    }
    lpf250X1_ = x0;
    lpf250Y0_ = y;
    return y;
}

void YM2149::doSamples250(int n) {
    const auto& dac  = dacTable();
    const auto& envW = envWaves();
    int pos = buf250Wr_;

    for (int i = 0; i < n; ++i) {
        // Bruit à 125 kHz (moitié de la cadence interne).
        freqDiv2_ ^= 1;
        if (freqDiv2_ == 0) {
            noiseCnt_++;
            if (noiseCnt_ >= noisePer_) {
                noiseCnt_ = 0;
                noiseVal_ = rndCompute(rndLfsr_);
            }
        }

        for (int ch = 0; ch < 3; ++ch) {
            toneCnt_[ch]++;
            if (toneCnt_[ch] >= tonePer_[ch]) {
                toneCnt_[ch] = 0;
                toneVal_[ch] ^= YM_SQUARE_UP;
            }
        }

        envCnt_++;
        if (envCnt_ >= envPer_) {
            envCnt_ = 0;
            envPos_++;
            if (envPos_ >= 3u * 32u)
                envPos_ -= 2u * 32u;
        }

        uint16_t env3 = uint16_t(envW[envShape_][envPos_] & envMask3_);
        uint16_t tone3 = 0;
        for (int ch = 0; ch < 3; ++ch) {
            const uint32_t bt = (toneVal_[ch] | mixerT_[ch]) & (noiseVal_ | mixerN_[ch]);
            tone3 |= uint16_t(bt & 0x1f) << (5 * ch);
        }
        tone3 &= uint16_t(env3 | vol3_);

        float s = applyPwm250(dac[tone3]);
        buf250_[pos] = s;
        pos = (pos + 1) & YM_BUF_250_MASK;
    }
    buf250Wr_ = pos;
}

void YM2149::ensureMargin(uint32_t sampleRate) {
    if (sampleRate == 0) return;
    const int margin = int(std::ceil(double(YM_250_HZ) / sampleRate)) + 2;
    while (((buf250Wr_ - buf250Rd_) & YM_BUF_250_MASK) < margin)
        doSamples250(margin);
}

float YM2149::nextResampleWeightedN(uint32_t sampleRate) {
    const uint32_t intervalFract = uint32_t((uint64_t(YM_250_HZ) * 0x10000ULL) / sampleRate);
    int64_t total = 0;

    if (resampleFracN_) {
        total += int64_t(sampleToFixed(buf250_[buf250Rd_])) * (0x10000 - resampleFracN_);
        buf250Rd_ = (buf250Rd_ + 1) & YM_BUF_250_MASK;
        resampleFracN_ -= 0x10000;
    }
    resampleFracN_ += intervalFract;

    while (resampleFracN_ & 0xffff0000u) {
        total += int64_t(sampleToFixed(buf250_[buf250Rd_])) * 0x10000;
        buf250Rd_ = (buf250Rd_ + 1) & YM_BUF_250_MASK;
        resampleFracN_ -= 0x10000;
    }
    if (resampleFracN_)
        total += int64_t(sampleToFixed(buf250_[buf250Rd_])) * resampleFracN_;

    return fixedToSample(total / int64_t(intervalFract));
}

void YM2149::synthBlock(const uint8_t* r, float* out, uint32_t frames, uint32_t sampleRate) {
    updateFromRegs(r);
    if (envReload_) {
        envReload_ = false;
        envPos_    = 0;
        envCnt_    = 0;
        envShape_  = r[13] & 0x0f;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        ensureMargin(sampleRate);
        const float s = nextResampleWeightedN(sampleRate);

        const double hp = double(s) - hpfX1_ + kHpfPole * hpfY0_;
        hpfX1_ = s;
        hpfY0_ = hp;
        out[i] = float(hp) * outScale_;
    }
}

void YM2149::synthesize(float* out, uint32_t frames, uint32_t sampleRate) {
    synthBlock(regs_.data(), out, frames, sampleRate);
}

void YM2149::synthesizeFrame(float* out, uint32_t frames, uint32_t sampleRate, int64_t frameCycles) {
    if (frameCycles <= 0) frameCycles = 1;
    uint32_t pos = 0;
    for (const RegEvent& e : events_) {
        uint32_t off = uint32_t(int64_t(e.cycle) * frames / frameCycles);
        if (off > frames) off = frames;
        if (off > pos) { synthBlock(audioRegs_.data(), out + pos, off - pos, sampleRate); pos = off; }
        audioRegs_[e.reg] = e.val;
        if (e.reg == 13) envReload_ = true;
    }
    if (pos < frames) synthBlock(audioRegs_.data(), out + pos, frames - pos, sampleRate);
    events_.clear();
}
