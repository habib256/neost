// =============================================================================
//  YM2149.cpp — Synthèse des 3 voies + bruit du PSG.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/YM2149.hpp"

// Volume AY/YM : ~3 dB par pas. Valeurs normalisées (niveau 15 = pleine échelle).
const std::array<float, 16> YM2149::kVolume = {
    0.0000f, 0.0056f, 0.0079f, 0.0112f, 0.0158f, 0.0224f, 0.0316f, 0.0447f,
    0.0631f, 0.0891f, 0.1259f, 0.1778f, 0.2512f, 0.3548f, 0.5012f, 1.0000f
};

void YM2149::synthesize(float* out, uint32_t frames, uint32_t sampleRate) {
    // Lecture directe des registres (écrits par le thread émulation) : course
    // bénigne ici, ce sont des octets. Un vrai modèle passerait par un anneau.
    const auto period = [&](int lo, int hi) -> int {
        const int p = (regs_[lo] | ((regs_[hi] & 0x0F) << 8));
        return p < 1 ? 1 : p;                       // période 0 ≡ 1 (évite /0)
    };
    const double freqA = CLOCK_HZ / (16.0 * period(0, 1));
    const double freqB = CLOCK_HZ / (16.0 * period(2, 3));
    const double freqC = CLOCK_HZ / (16.0 * period(4, 5));
    const double freq[3] = { freqA, freqB, freqC };

    const int noisePer = (regs_[6] & 0x1F) ? (regs_[6] & 0x1F) : 1;
    const double noiseFreq = CLOCK_HZ / (16.0 * noisePer);

    const uint8_t mix = regs_[7];                   // bits 0-2 tons, 3-5 bruit (actifs à 0)
    const float volA = kVolume[regs_[8]  & 0x0F];
    const float volB = kVolume[regs_[9]  & 0x0F];
    const float volC = kVolume[regs_[10] & 0x0F];
    const float vol[3] = { volA, volB, volC };

    const double incN = noiseFreq / sampleRate;

    for (uint32_t i = 0; i < frames; ++i) {
        // Avance le bruit : à chaque période, on cadence le LFSR 17 bits.
        noisePhase_ += incN;
        while (noisePhase_ >= 1.0) {
            noisePhase_ -= 1.0;
            const uint32_t bit = ((noiseLfsr_ ^ (noiseLfsr_ >> 3)) & 1);
            noiseLfsr_ = (noiseLfsr_ >> 1) | (bit << 16);
        }
        const float noiseLevel = (noiseLfsr_ & 1) ? 1.0f : -1.0f;

        float sample = 0.0f;
        for (int ch = 0; ch < 3; ++ch) {
            phase_[ch] += freq[ch] / sampleRate;
            if (phase_[ch] >= 1.0) phase_[ch] -= 1.0;
            const bool toneOn  = !((mix >> ch)        & 1);   // bit à 0 = activé
            const bool noiseOn = !((mix >> (ch + 3))  & 1);
            const float square = (phase_[ch] < 0.5) ? 1.0f : -1.0f;
            float v = 0.0f;
            if (toneOn)  v += square;
            if (noiseOn) v += noiseLevel;
            if (toneOn && noiseOn) v *= 0.5f;                  // moyenne ton+bruit
            sample += v * vol[ch];
        }
        out[i] = sample * (1.0f / 3.0f);                       // mixe les 3 voies
    }
}
