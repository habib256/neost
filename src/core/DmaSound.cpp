// =============================================================================
//  DmaSound.cpp — Implémentation du son DMA STE.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/DmaSound.hpp"
#include "core/Bus.hpp"

// Fréquences d'échantillonnage STE (cristal 25.175 MHz / diviseurs), bits 0-1 de
// $FF8921. Réf. doc matérielle / Hatari.
static const double kRate[4] = { 6258.0, 12517.0, 25033.0, 50066.0 };

// Gain du canal numérique dans le mix (pleine échelle 8 bits ramenée sous le 0 dB
// pour laisser de la marge au YM2149 ; le volume fin relève du LMC1992, cf. TODO).
static constexpr float kDmaGain = 0.7f;

void DmaSound::reset() {
    playing_ = false;
    ctrl_ = 0;
    phase_ = 0.0;
}

// Lecture d'un échantillon (mono -128..127). En stéréo, moyenne L+R (sortie mono).
int DmaSound::sampleAt(uint32_t addr, bool stereo) const {
    const auto rd = [&](uint32_t a) -> int {
        a &= stmap::ADDR_MASK;
        return (a < bus_.ram.size()) ? static_cast<int8_t>(bus_.ram[a]) : 0;
    };
    return stereo ? (rd(addr) + rd(addr + 1)) / 2 : rd(addr);
}

uint8_t DmaSound::read8(uint32_t addr) {
    switch (addr & 0xFF) {
        case 0x01: return ctrl_;
        case 0x03: return uint8_t(startAddr_ >> 16);
        case 0x05: return uint8_t(startAddr_ >> 8);
        case 0x07: return uint8_t(startAddr_);
        case 0x09: return uint8_t(curAddr_ >> 16);    // compteur courant (position de lecture)
        case 0x0B: return uint8_t(curAddr_ >> 8);
        case 0x0D: return uint8_t(curAddr_);
        case 0x0F: return uint8_t(endAddr_ >> 16);
        case 0x11: return uint8_t(endAddr_ >> 8);
        case 0x13: return uint8_t(endAddr_);
        case 0x21: return mode_;
        case 0x22: return uint8_t(mwData_);           // microwire (LMC1992)
        case 0x24: return uint8_t(mwMask_);
        default:   return 0xFF;
    }
}

void DmaSound::write8(uint32_t addr, uint8_t v) {
    switch (addr & 0xFF) {
        case 0x01: {                                  // contrôle : play / repeat
            const bool wasPlaying = ctrl_ & 0x01;
            ctrl_ = v & 0x03;
            if ((ctrl_ & 0x01) && !wasPlaying) {       // 0→1 : (re)démarre la trame
                curAddr_ = startAddr_;
                phase_   = 0.0;
                playing_ = true;
            } else if (!(ctrl_ & 0x01)) {              // bit play à 0 : arrêt
                playing_ = false;
            }
            break;
        }
        // Adresses 24 bits (paires forcées : bit0 de l'octet bas ignoré).
        case 0x03: startAddr_ = (startAddr_ & 0x00FFFF) | (uint32_t(v) << 16); break;
        case 0x05: startAddr_ = (startAddr_ & 0xFF00FF) | (uint32_t(v) << 8);  break;
        case 0x07: startAddr_ = (startAddr_ & 0xFFFF00) | (uint32_t(v) & 0xFE); break;
        case 0x0F: endAddr_   = (endAddr_   & 0x00FFFF) | (uint32_t(v) << 16); break;
        case 0x11: endAddr_   = (endAddr_   & 0xFF00FF) | (uint32_t(v) << 8);  break;
        case 0x13: endAddr_   = (endAddr_   & 0xFFFF00) | (uint32_t(v) & 0xFE); break;
        case 0x21: mode_ = v; break;                   // fréquence + mono/stéréo
        case 0x22: mwData_ = v; break;                 // microwire data
        case 0x24: mwMask_ = v; break;                 // microwire mask
        default: break;                                // compteur courant : lecture seule
    }
}

void DmaSound::mix(float* out, uint32_t frames, uint32_t sampleRate) {
    if (!playing_ || sampleRate == 0) return;
    const double inc    = kRate[mode_ & 0x03] / sampleRate;   // pas de rééchantillonnage
    const bool   stereo = !(mode_ & 0x80);                    // bit7=0 → stéréo entrelacé
    const uint32_t step = stereo ? 2u : 1u;                   // octets par trame DMA

    for (uint32_t i = 0; i < frames; ++i) {
        out[i] += (sampleAt(curAddr_, stereo) / 128.0f) * kDmaGain;
        phase_ += inc;
        while (phase_ >= 1.0) {                                // avance dans la RAM
            phase_ -= 1.0;
            curAddr_ += step;
            if (curAddr_ >= endAddr_) {                        // fin de trame
                if (ctrl_ & 0x02) {                            // repeat : reboucle
                    curAddr_ = startAddr_;
                } else {                                       // sinon : arrêt
                    playing_ = false;
                    ctrl_ &= ~0x01;
                    return;                                    // reste de `out` = silence
                }
            }
        }
    }
}
