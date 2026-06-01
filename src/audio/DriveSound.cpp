// =============================================================================
//  DriveSound.cpp — Implémentation des bruits de lecteur (miniaudio ma_engine).
//
//  N'inclut PAS MINIAUDIO_IMPLEMENTATION (c'est Audio.cpp qui la pose) : on se
//  contente d'appeler l'API haut niveau ma_engine_* / ma_sound_*, liée depuis
//  Audio.cpp. Les WAV proviennent de rom/drivesound/ (cf. son README).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "miniaudio.h"

#include "audio/DriveSound.hpp"

#include <cstdio>

using namespace std::chrono_literals;

// Délai d'arrêt du moteur après la dernière activité disque (un vrai lecteur
// laisse tourner le moteur ~quelques rotations avant de le couper).
static constexpr auto kMotorLinger = 1500ms;

bool DriveSound::init(const std::string& dir) {
    auto* eng = new ma_engine();
    if (ma_engine_init(nullptr, eng) != MA_SUCCESS) {
        std::fprintf(stderr, "[DriveSound] ma_engine_init a échoué — pas de bruits lecteur\n");
        delete eng;
        return false;
    }
    engine_      = eng;
    clickPath_   = dir + "/drive_click.wav";
    seekPath_    = dir + "/drive_seek.wav";
    startupPath_ = dir + "/drive_startup.wav";

    // Le ronron moteur est une boucle : on le décode une fois et on le démarre /
    // arrête à la demande (les one-shots passent par ma_engine_play_sound).
    auto* m = new ma_sound();
    const std::string spin = dir + "/drive_spin.wav";
    if (ma_sound_init_from_file(eng, spin.c_str(), MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, m) != MA_SUCCESS) {
        std::fprintf(stderr, "[DriveSound] %s introuvable — moteur muet\n", spin.c_str());
        delete m;                         // les clics/seek restent jouables
    } else {
        ma_sound_set_looping(m, MA_TRUE);
        motor_ = m;
    }
    ok_ = true;
    std::printf("[DriveSound] prêt (%s)\n", dir.c_str());
    return true;
}

void DriveSound::shutdown() {
    if (motor_) { ma_sound_uninit(static_cast<ma_sound*>(motor_)); delete static_cast<ma_sound*>(motor_); motor_ = nullptr; }
    if (engine_) { ma_engine_uninit(static_cast<ma_engine*>(engine_)); delete static_cast<ma_engine*>(engine_); engine_ = nullptr; }
    ok_ = false;
}

void DriveSound::motorStop() {
    if (motor_ && motorPlaying_) ma_sound_stop(static_cast<ma_sound*>(motor_));
    motorPlaying_ = false;
}

void DriveSound::onEvent(FdcSound e) {
    if (!ok_ || !enabled_) return;
    auto* eng = static_cast<ma_engine*>(engine_);
    switch (e) {
        case FdcSound::MotorOn:
            if (!motorPlaying_) {                       // transition arrêt → rotation
                motorPlaying_ = true;
                if (!startupPath_.empty()) ma_engine_play_sound(eng, startupPath_.c_str(), nullptr);
                if (motor_) {
                    ma_sound_seek_to_pcm_frame(static_cast<ma_sound*>(motor_), 0);
                    ma_sound_start(static_cast<ma_sound*>(motor_));
                }
            }
            motorDeadline_ = std::chrono::steady_clock::now() + kMotorLinger;  // ré-arme
            break;
        case FdcSound::Step:
            ma_engine_play_sound(eng, clickPath_.c_str(), nullptr);
            break;
        case FdcSound::Seek:
            ma_engine_play_sound(eng, seekPath_.c_str(), nullptr);
            break;
    }
}

void DriveSound::update() {
    if (!ok_ || !motorPlaying_) return;
    if (std::chrono::steady_clock::now() >= motorDeadline_) motorStop();
}

void DriveSound::setEnabled(bool on) {
    enabled_ = on;
    if (!on) motorStop();
}
