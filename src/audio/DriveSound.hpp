// =============================================================================
//  DriveSound.hpp — Bruits mécaniques du lecteur de disquette (GUI natif).
//
//  Rejoue les échantillons WAV de rom/drivesound/ (ronron du moteur en boucle,
//  « clic » de pas, bruit de seek, léger « tic » d'index) en réaction aux
//  événements FdcSound émis par le cœur (cf. Fdc::setSoundSink).
//
//  Mixage : DriveSound s'appuie sur un moteur miniaudio EN MODE « sans
//  périphérique » (ma_engine noDevice) ; ce n'est PAS lui qui ouvre la carte son.
//  C'est Audio (le périphérique unique) qui, dans son callback, additionne la
//  sortie du YM2149 ET celle de DriveSound (mix()). Un seul flux sort donc.
//
//  Découplage : le cœur SIGNALE (FdcSound), ce frontend JOUE. Le moteur est
//  démarré/arrêté par le cœur (MotorOn/MotorOff), plus de minuterie ici.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>

#include "io/Fdc.hpp"   // FdcSound

class DriveSound {
public:
    ~DriveSound() { shutdown(); }

    // Initialise un moteur miniaudio SANS périphérique (mixé par Audio) au format
    // demandé (mono, sampleRate Hz) et repère les WAV dans `dir` (drive_*.wav).
    bool init(const std::string& dir, uint32_t sampleRate);
    void shutdown();
    bool ok() const { return ok_; }

    // Réagit à un événement du FDC (appelé depuis le thread d'émulation).
    void onEvent(FdcSound e);
    // Additionne la sortie courante (moteur + one-shots) dans `out` (mono float) —
    // appelé depuis le thread audio par Audio. Silencieux si désactivé/échec.
    void mix(float* out, uint32_t frames);

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

private:
    void motorStop();

    void* engine_ = nullptr;     // ma_engine opaque (mode sans périphérique)
    void* motor_  = nullptr;     // ma_sound (boucle ronron) opaque
    void* tick_   = nullptr;     // ma_sound (tic d'index, volume bas) opaque
    bool  ok_      = false;
    bool  enabled_ = true;
    bool  motorPlaying_ = false;

    std::string clickPath_, seekPath_, startupPath_;
};
