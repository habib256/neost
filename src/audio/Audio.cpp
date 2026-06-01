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

#include <cstdio>

namespace {
// Callback du thread audio : pUserData pointe sur l'instance Audio, qui mixe le
// PSG et les bruits de lecteur dans le buffer de sortie.
void dataCallback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frames) {
    static_cast<Audio*>(dev->pUserData)->render(static_cast<float*>(output), frames, dev->sampleRate);
}
} // namespace

Audio::Audio(YM2149& psg, DriveSound* drive) : psg_(psg), drive_(drive) {}

Audio::~Audio() { stop(); }

void Audio::render(float* out, uint32_t frames, uint32_t sampleRate) {
    psg_.synthesize(out, frames, sampleRate);     // (1) PSG → out (écrase)
    if (drive_) drive_->mix(out, frames);         // (2) bruits lecteur → additionnés
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
    std::printf("[Audio] miniaudio démarré : %u Hz mono (YM2149 + lecteur)\n", cfg.sampleRate);
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
