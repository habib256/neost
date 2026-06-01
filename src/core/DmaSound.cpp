// =============================================================================
//  DmaSound.cpp — Implémentation du son DMA STE.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/DmaSound.hpp"
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"

#include <cmath>

// Fréquences d'échantillonnage STE (cristal 25.175 MHz / diviseurs), bits 0-1 de
// $FF8921. Réf. doc matérielle / Hatari.
static const double kRate[4] = { 6258.0, 12517.0, 25033.0, 50066.0 };

// Gain du canal numérique dans le mix (pleine échelle 8 bits ramenée sous le 0 dB
// pour laisser de la marge au YM2149 ; le volume fin relève du LMC1992, cf. TODO).
static constexpr float kDmaGain = 0.7f;

// Horloge CPU (Hz) pour dater la fin de trame en cycles (cf. Mfp / Scheduler).
static constexpr int64_t CPU_HZ = 8021248;

// Programme l'échéance « fin de trame » sur l'ordonnanceur (thread émulation) :
// durée = nombre d'échantillons de la trame × cycles CPU par échantillon.
void DmaSound::scheduleFrameEnd() {
    if (!sched_) return;
    if (endAddr_ <= startAddr_) { sched_->cancel(Scheduler::DMASND); return; }
    const uint32_t step    = (mode_ & 0x80) ? 1u : 2u;        // mono 1 octet, stéréo 2
    const int64_t  samples = (endAddr_ - startAddr_) / step;
    const int64_t  rate    = int64_t(kRate[mode_ & 0x03]);
    const int64_t  dur     = samples * CPU_HZ / rate;
    sched_->schedule(Scheduler::DMASND, sched_->now() + (dur > 0 ? dur : 1));
}

void DmaSound::onFrameEnd() {
    if (mfp_) mfp_->timerA_eventCount();          // impulsion TAI → event-count Timer A
    if (ctrl_ & 0x02) scheduleFrameEnd();         // repeat : prochaine trame
}

void DmaSound::reset() {
    playing_ = false;
    ctrl_ = 0;
    phase_ = 0.0;
    if (sched_) sched_->cancel(Scheduler::DMASND);
    mwMaster_ = 40; mwLeft_ = 20; mwRight_ = 20;   // LMC1992 à 0 dB (pas de mute au reset)
    mwBass_ = 6; mwTreble_ = 6; mwMixing_ = 0;
}

// Décode une commande LMC1992 reçue par microwire. Le mot 16 bits ($FF8922) est
// décalé en série, seuls les bits où le masque ($FF8924) vaut 1 sortent (MSB
// d'abord). La commande utile fait 11 bits : %10 (adresse puce) + 3 bits de
// registre + 6 bits de donnée. Registres : 3=volume maître, 4=droite, 5=gauche,
// 1=basses, 2=aigus, 0=mixage (ces trois derniers stockés, filtrage = TODO).
void DmaSound::decodeMicrowire() {
    uint16_t cmd = 0; int bits = 0;
    for (int i = 15; i >= 0; --i)
        if (mwMask_ & (1u << i)) { cmd = uint16_t((cmd << 1) | ((mwData_ >> i) & 1)); ++bits; }
    if (bits < 11) return;                         // commande incomplète
    const uint16_t c = cmd & 0x7FF;
    if ((c >> 9) != 0b10) return;                  // n'adresse pas le LMC1992
    const int reg = (c >> 6) & 0x07, data = c & 0x3F;
    switch (reg) {
        case 0: mwMixing_ = data; break;
        case 1: mwBass_   = data; break;
        case 2: mwTreble_ = data; break;
        case 3: mwMaster_ = data; break;           // 0..40 → -80..0 dB
        case 4: mwRight_  = data; break;           // 0..20 → -40..0 dB
        case 5: mwLeft_   = data; break;
        default: break;
    }
}

float DmaSound::masterGain() const {
    // Pas de 2 dB ; valeurs au-delà du max = 0 dB. Sortie mono → moyenne G/D.
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double ldB = (mwLeft_   >= 20 ? 0 : (mwLeft_   - 20) * 2);
    const double rdB = (mwRight_  >= 20 ? 0 : (mwRight_  - 20) * 2);
    const double gL = std::pow(10.0, (mdB + ldB) / 20.0);
    const double gR = std::pow(10.0, (mdB + rdB) / 20.0);
    return static_cast<float>((gL + gR) * 0.5);
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
        case 0x22: return uint8_t(mwData_ >> 8);      // microwire data (mot 16 bits)
        case 0x23: return uint8_t(mwData_);
        case 0x24: return uint8_t(mwMask_ >> 8);      // microwire mask
        case 0x25: return uint8_t(mwMask_);
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
                scheduleFrameEnd();                    // date la fin de trame (→ Timer A)
            } else if (!(ctrl_ & 0x01)) {              // bit play à 0 : arrêt
                playing_ = false;
                if (sched_) sched_->cancel(Scheduler::DMASND);
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
        // Microwire : mots 16 bits ($FF8922 data, $FF8924 mask). On décode la
        // commande LMC1992 quand l'octet bas de la donnée est écrit (mot complet).
        case 0x22: mwData_ = uint16_t((mwData_ & 0x00FF) | (v << 8)); break;
        case 0x23: mwData_ = uint16_t((mwData_ & 0xFF00) | v); decodeMicrowire(); break;
        case 0x24: mwMask_ = uint16_t((mwMask_ & 0x00FF) | (v << 8)); break;
        case 0x25: mwMask_ = uint16_t((mwMask_ & 0xFF00) | v); break;
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
