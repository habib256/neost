// =============================================================================
//  Audio.cpp — Implémentation miniaudio (l'unité qui DÉFINIT miniaudio).
//
//  Un seul .cpp doit poser MINIAUDIO_IMPLEMENTATION dans tout le projet.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio/Audio.hpp"
#include "core/YM2149.hpp"

#include <cstdio>

namespace {
// Callback exécuté par le thread audio de miniaudio : tire les échantillons
// du PSG. pUserData pointe sur l'instance Audio.
void dataCallback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frames) {
    auto* psg = static_cast<YM2149*>(dev->pUserData);
    psg->synthesize(static_cast<float*>(output), frames, dev->sampleRate);
}
} // namespace

Audio::Audio(YM2149& psg) : psg_(psg) {}

Audio::~Audio() { stop(); }

bool Audio::start() {
    auto* dev = new ma_device();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;   // on synthétise en float
    cfg.playback.channels = 1;               // PSG mono
    cfg.sampleRate        = 48000;
    cfg.dataCallback      = dataCallback;
    cfg.pUserData         = &psg_;            // le callback reçoit le YM2149

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
    std::printf("[Audio] miniaudio démarré : %u Hz mono\n", cfg.sampleRate);
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
