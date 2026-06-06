// =============================================================================
//  Audio.cpp — Implémentation miniaudio (l'unité qui DÉFINIT miniaudio).
//
//  Un seul .cpp doit poser MINIAUDIO_IMPLEMENTATION dans tout le projet ; il
//  fournit aussi l'API haut niveau ma_engine_* utilisée par DriveSound.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "core/YM2149.hpp"
#include "core/DmaSound.hpp"

#include <algorithm>
#include <cstdio>

namespace {
// Callback du thread audio : pUserData pointe sur l'instance Audio, qui mixe le
// PSG et les bruits de lecteur dans le buffer de sortie.
void dataCallback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frames) {
    static_cast<Audio*>(dev->pUserData)->render(static_cast<float*>(output), frames, dev->sampleRate);
}
} // namespace

Audio::Audio(YM2149& psg, DriveSound* drive, DmaSound* dma)
    : psg_(psg), drive_(drive), dma_(dma) {}

Audio::~Audio() { stop(); }

// CONSOMMATEUR (thread audio) : recopie l'anneau rempli par produceFrame. AMORÇAGE :
// tant que l'anneau n'a pas atteint le coussin cible (~85 ms), on sort du SILENCE sans
// drainer — sinon on jouerait un anneau quasi-vide en underrun permanent, où seules les
// transitoires (drums) passent et la musique continue se perd ; l'anneau mettrait des
// dizaines de secondes à se remplir. Après tout underrun, on RÉ-AMORCE le coussin.
void Audio::render(float* out, uint32_t frames, uint32_t /*sampleRate*/) {
    if (!primed_) {
        if (ring_.available() < primeSamples_) { std::fill(out, out + frames, 0.0f); return; }
        primed_ = true;                                   // coussin atteint → on démarre la lecture
    }
    if (ring_.pull(out, frames) < frames) primed_ = false;  // underrun → on reconstitue le coussin
}

// PRODUCTEUR (thread d'émulation, après runFrame) : génère le son de la trame et le
// pousse dans l'anneau. Le PSG est synthétisé en REJOUANT ses écritures horodatées
// (synthesizeFrame → modulations sous-buffer : digidrums, sync-buzzer), puis on mixe le
// son DMA STE, le volume/tonalité LMC1992 et les bruits de lecteur, avant clamp.
void Audio::produceFrame(int64_t frameCycles) {
    if (!started_) { psg_.clearEvents(); return; }    // pas de périphérique : on draine juste les événements

    // Nombre d'échantillons pour cette trame = durée émulée × fréquence de sortie, avec
    // report fractionnaire (le débit moyen colle EXACTEMENT au temps émulé). Puis
    // ASSERVISSEMENT PROPORTIONNEL : on ajuste de quelques échantillons pour ramener l'anneau
    // vers le coussin cible (remplissage rapide à l'amorçage, recalage anti-dérive ensuite).
    // |adj| ≤ 8 sur ~960 → ≤ 0,8 % de variation de hauteur, inaudible. Sans toucher au
    // bridage vidéo 50 fps.
    static constexpr double CPU_HZ = 8021248.0;
    sampleCarry_ += double(frameCycles) * rate_ / CPU_HZ;
    int n = int(sampleCarry_);
    sampleCarry_ -= n;
    int adj = (int(primeSamples_) - int(ring_.available())) / 256;   // P : erreur vers la cible
    if      (adj >  8) adj =  8;
    else if (adj < -8) adj = -8;
    n += adj;
    if (n <= 0) return;

    if (int(scratch_.size()) < n) scratch_.assign(n, 0.0f);
    float* out = scratch_.data();

    psg_.synthesizeFrame(out, uint32_t(n), rate_, frameCycles);   // (1) PSG horodaté → out (écrase)
    if (dma_) {
        dma_->mix(out, uint32_t(n), rate_);               // (2) son DMA STE → additionné (mixing LMC géré dedans)
        const float g = dma_->masterGain();               // (3) volume maître LMC1992
        if (g != 1.0f) for (int i = 0; i < n; ++i) out[i] *= g;
        dma_->applyTone(out, uint32_t(n), rate_);          // (4) basses/aigus LMC1992
    }
    if (drive_) drive_->mix(out, uint32_t(n));            // (5) bruits lecteur (hors LMC1992)
    for (int i = 0; i < n; ++i)                            // garde-fou anti-saturation
        out[i] = std::max(-1.0f, std::min(1.0f, out[i]));

    ring_.push(out, size_t(n));                           // → thread audio (render). Surplus jeté si plein.
}

bool Audio::start() {
    auto* dev = new ma_device();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;   // on synthétise en float
    cfg.playback.channels = 1;               // PSG mono (+ bruits lecteur mono)
    cfg.sampleRate        = 48000;
    cfg.dataCallback      = dataCallback;
    cfg.pUserData         = this;            // le callback reçoit l'instance Audio

    if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] ma_device_init a échoué\n");
        delete dev;
        return false;
    }
    if (ma_device_start(dev) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] ma_device_start a échoué\n");
        ma_device_uninit(dev);
        delete dev;
        return false;
    }
    device_  = dev;
    started_ = true;
    rate_    = dev->sampleRate ? dev->sampleRate : cfg.sampleRate;  // fréquence réelle négociée
    primeSamples_ = rate_ * 85 / 1000;        // coussin ≈ 85 ms (latence visée) à la fréquence réelle
    ring_.clear();
    primed_      = false;                     // ré-amorçage propre
    sampleCarry_ = 0.0;
    std::printf("[Audio] miniaudio démarré : %u Hz mono, latence ~%u ms (modèle push : PSG horodaté + DMA + lecteur)\n",
                rate_, primeSamples_ * 1000 / rate_);
    return true;
}

void Audio::stop() {
    if (!device_) return;
    auto* dev = static_cast<ma_device*>(device_);
    ma_device_uninit(dev);
    delete dev;
    device_  = nullptr;
    started_ = false;
}
