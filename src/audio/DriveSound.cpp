// =============================================================================
//  DriveSound.cpp — Implémentation des bruits de lecteur (miniaudio ma_engine).
//
//  N'inclut PAS MINIAUDIO_IMPLEMENTATION (c'est Audio.cpp qui la pose) : on se
//  contente d'appeler l'API haut niveau ma_engine_* / ma_sound_*, liée depuis
//  Audio.cpp. Le moteur tourne en mode « sans périphérique » : sa sortie est
//  tirée par Audio (mix) puis additionnée au YM2149. Les WAV proviennent de
//  roms/drivesound/ (cf. son README).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "miniaudio.h"
#include "util/Log.hpp"

#include "audio/DriveSound.hpp"

#include <algorithm>
#include <cstdio>

bool DriveSound::init(const std::string& dir, uint32_t sampleRate) {
    // RÉ-initialisation : libérer d'abord. `init()` est appelé DEUX fois sur le même
    // objet — une fois à 48 kHz, puis de nouveau si le périphérique a négocié une
    // autre fréquence (AppInit.cpp) — et le second appel écrasait `engine_`, `motor_`
    // et `tick_` sans rien désinitialiser. Ce n'était pas qu'un bloc mémoire perdu :
    // `ma_engine_init` sans resource manager fourni en crée un, avec son THREAD de
    // travail (jobThreadCount = 1 par défaut), plus deux `ma_sound` aux tampons PCM
    // entièrement décodés (MA_SOUND_FLAG_DECODE). Le tout restait vivant et
    // inaccessible jusqu'à la fin du processus. `shutdown()` est idempotent — sur un
    // objet neuf les trois poignées sont nulles et il ne fait rien.
    shutdown();

    // Moteur SANS périphérique : on ne tire pas la carte son nous-mêmes, c'est
    // Audio qui lira nos frames (ma_engine_read_pcm_frames) et les mixera.
    ma_engine_config cfg = ma_engine_config_init();
    cfg.noDevice   = MA_TRUE;
    cfg.channels   = 1;                 // mono, comme le YM2149
    cfg.sampleRate = sampleRate;

    auto* eng = new ma_engine();
    if (ma_engine_init(&cfg, eng) != MA_SUCCESS) {
        std::fprintf(stderr, "[DriveSound] ma_engine_init failed — no drive noises\n");
        delete eng;
        return false;
    }
    engine_      = eng;
    clickPath_   = dir + "/drive_click.wav";
    seekPath_    = dir + "/drive_seek.wav";
    startupPath_ = dir + "/drive_startup.wav";

    // Le ronron moteur est une boucle décodée une fois, démarrée/arrêtée à la
    // demande (les clics/seek passent par ma_engine_play_sound, fire-and-forget).
    auto* m = new ma_sound();
    const std::string spin = dir + "/drive_spin.wav";
    if (ma_sound_init_from_file(eng, spin.c_str(), MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, m) != MA_SUCCESS) {
        std::fprintf(stderr, "[DriveSound] %s not found — motor silent\n", spin.c_str());
        delete m;
    } else {
        ma_sound_set_looping(m, MA_TRUE);
        motor_ = m;
    }

    // Le « tic » d'index réutilise le clic mais à volume bas (1/tour, discret).
    auto* t = new ma_sound();
    if (ma_sound_init_from_file(eng, clickPath_.c_str(), MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, t) != MA_SUCCESS) {
        delete t;
    } else {
        ma_sound_set_volume(t, 0.18f);
        tick_ = t;
    }

    ok_ = true;
    if (neost::log::verbose()) std::printf("[DriveSound] ready (%s, mixed into the YM2149)\n", dir.c_str());
    return true;
}

void DriveSound::shutdown() {
    if (tick_)   { ma_sound_uninit(static_cast<ma_sound*>(tick_));   delete static_cast<ma_sound*>(tick_);   tick_   = nullptr; }
    if (motor_)  { ma_sound_uninit(static_cast<ma_sound*>(motor_));  delete static_cast<ma_sound*>(motor_);  motor_  = nullptr; }
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
        case FdcSound::MotorOn:                         // le cœur a énergisé le moteur
            if (!motorPlaying_) {
                motorPlaying_ = true;
                if (!startupPath_.empty()) ma_engine_play_sound(eng, startupPath_.c_str(), nullptr);
                if (motor_) {
                    ma_sound_seek_to_pcm_frame(static_cast<ma_sound*>(motor_), 0);
                    ma_sound_start(static_cast<ma_sound*>(motor_));
                }
            }
            break;
        case FdcSound::MotorOff:                        // le cœur a coupé le moteur
            motorStop();
            break;
        case FdcSound::Step:
            ma_engine_play_sound(eng, clickPath_.c_str(), nullptr);
            break;
        case FdcSound::Seek:
            ma_engine_play_sound(eng, seekPath_.c_str(), nullptr);
            break;
        case FdcSound::Index:                           // léger « tic » 1/tour
            if (tick_) {
                ma_sound_seek_to_pcm_frame(static_cast<ma_sound*>(tick_), 0);
                ma_sound_start(static_cast<ma_sound*>(tick_));
            }
            break;
    }
}

void DriveSound::mix(float* out, uint32_t frames) {
    if (!ok_ || !enabled_) return;
    auto* eng = static_cast<ma_engine*>(engine_);
    float tmp[1024];
    uint32_t done = 0;
    while (done < frames) {
        const ma_uint64 want = std::min<uint32_t>(frames - done, 1024);
        ma_uint64 got = 0;
        if (ma_engine_read_pcm_frames(eng, tmp, want, &got) != MA_SUCCESS) break;
        for (ma_uint64 i = 0; i < got; ++i) out[done + i] += tmp[i];   // mono : somme directe
        done += static_cast<uint32_t>(got);   // got, PAS want : sinon une lecture courte décalerait out[] hors tampon
        if (got < want) break;
    }
}

void DriveSound::setEnabled(bool on) {
    enabled_ = on;
    if (!on) motorStop();
}
