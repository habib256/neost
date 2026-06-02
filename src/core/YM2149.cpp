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

// Fait avancer l'enveloppe d'un cran. À la fin d'une rampe (niveau hors [0,15]),
// les 4 bits de R13 décident de la suite (cf. les 8 formes utiles de l'AY/YM) :
//   Continue=0           → une seule rampe puis niveau 0 figé (formes $0-$7) ;
//   Continue=1, Hold=1   → une rampe puis figé (à l'extrémité atteinte, ou à
//                          l'extrémité opposée si Alternate) ;
//   Continue=1, Hold=0   → répétition : dent de scie (Alternate=0) ou
//                          triangle (Alternate=1, on inverse le sens).
void YM2149::clockEnvelope(uint8_t shape) {
    if (envHold_) return;
    const bool cont = shape & 0x08, attack = shape & 0x04;
    const bool alt  = shape & 0x02, hold   = shape & 0x01;
    (void)attack;

    envLevel_ += envDir_;
    if (envLevel_ >= 0 && envLevel_ <= 15) return;          // toujours dans la rampe

    if (!cont) {                                            // $0-$7 : une rampe puis 0
        envLevel_ = 0; envHold_ = true;
    } else if (hold) {                                      // une rampe puis figé
        envLevel_ = alt ? (envDir_ > 0 ? 0 : 15)            // Alternate : extrémité opposée
                        : (envDir_ > 0 ? 15 : 0);           // sinon : extrémité atteinte
        envHold_ = true;
    } else if (alt) {                                       // triangle : on inverse le sens
        envDir_ = -envDir_;
        envLevel_ = (envDir_ > 0) ? 0 : 15;
    } else {                                                // dent de scie : on repart
        envLevel_ = (envDir_ > 0) ? 0 : 15;
    }
}

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

    // Enveloppe : période 16 bits (R11/R12), un pas de niveau à fclock/16/période.
    // Comme le compteur d'enveloppe de Hatari (YM2149_DoSamples_250) tick au même
    // rythme que les compteurs de ton (un cran par cycle YM, comparé à Env_per), le
    // diviseur est 16.0 comme pour le ton — la rampe complète de 16 crans dure alors
    // 256*envPer/CLOCK (l'ancien 256.0 la rendait 16× trop lente).
    const uint8_t shape   = regs_[13] & 0x0F;
    const int     envPer  = (regs_[11] | (regs_[12] << 8)) ? (regs_[11] | (regs_[12] << 8)) : 1;
    const double  envStepFreq = CLOCK_HZ / (16.0 * envPer);
    const double  incE = envStepFreq / sampleRate;
    if (envReload_) {                               // R13 écrit → réinitialise l'enveloppe
        envReload_ = false;
        envDir_   = (shape & 0x04) ? +1 : -1;       // Attack : monte, sinon descend
        envLevel_ = (shape & 0x04) ? 0 : 15;
        envHold_  = false;
        envPhase_ = 0.0;
    }

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

        // Avance l'enveloppe ; son niveau sert de volume aux voies en mode enveloppe.
        envPhase_ += incE;
        while (envPhase_ >= 1.0) { envPhase_ -= 1.0; clockEnvelope(shape); }
        const float envVol = kVolume[envLevel_ & 0x0F];

        float sample = 0.0f;
        for (int ch = 0; ch < 3; ++ch) {
            phase_[ch] += freq[ch] / sampleRate;
            if (phase_[ch] >= 1.0) phase_[ch] -= 1.0;
            const bool toneOn  = !((mix >> ch)        & 1);   // bit à 0 = activé
            const bool noiseOn = !((mix >> (ch + 3))  & 1);
            const float square = (phase_[ch] < 0.5) ? 1.0f : -1.0f;
            // Volume voie : bit 4 du registre (8/9/10) → suit l'enveloppe, sinon fixe.
            const uint8_t vreg = regs_[8 + ch];
            const float vol = (vreg & 0x10) ? envVol : kVolume[vreg & 0x0F];
            float v = 0.0f;
            if (toneOn)  v += square;
            if (noiseOn) v += noiseLevel;
            if (toneOn && noiseOn) v *= 0.5f;                  // moyenne ton+bruit
            sample += v * vol;
        }
        out[i] = sample * (1.0f / 3.0f);                       // mixe les 3 voies
    }
}
