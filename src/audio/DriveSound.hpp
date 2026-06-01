// =============================================================================
//  DriveSound.hpp — Bruits mécaniques du lecteur de disquette (GUI natif).
//
//  Rejoue les échantillons WAV de rom/drivesound/ (ronron du moteur en boucle,
//  « clic » de pas de tête, bruit de seek) en réaction aux événements FdcSound
//  émis par le cœur (cf. Fdc::setSoundSink). S'appuie sur le moteur haut niveau
//  de miniaudio (ma_engine) ; l'implémentation miniaudio vit dans Audio.cpp.
//
//  Découplage : le cœur SIGNALE (FdcSound), ce frontend JOUE. Le moteur s'arrête
//  tout seul après un court délai d'inactivité (comme un vrai lecteur).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <chrono>
#include <string>

#include "io/Fdc.hpp"   // FdcSound

class DriveSound {
public:
    ~DriveSound() { shutdown(); }

    // Initialise le moteur audio et repère les WAV dans `dir` (drive_*.wav).
    bool init(const std::string& dir);
    void shutdown();
    bool ok() const { return ok_; }

    // Réagit à un événement du FDC (appelé depuis le thread d'émulation).
    void onEvent(FdcSound e);
    // À appeler une fois par trame : coupe la boucle moteur après inactivité.
    void update();

    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

private:
    void motorStop();

    void* engine_ = nullptr;     // ma_engine opaque
    void* motor_  = nullptr;     // ma_sound (boucle ronron) opaque
    bool  ok_      = false;
    bool  enabled_ = true;
    bool  motorPlaying_ = false;

    std::string clickPath_, seekPath_, startupPath_;
    std::chrono::steady_clock::time_point motorDeadline_{};
};
