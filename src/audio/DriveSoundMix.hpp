// =============================================================================
//  DriveSoundMix.hpp — Bruits du lecteur de disquette rendus en PCM, sans dépendance.
//
//  Le GUI joue les échantillons de roms/drivesound/ par son propre moteur miniaudio
//  (audio/DriveSound). Un frontend à UN seul flux son (libretro, Android) doit les
//  mêler à la trame : c'est ce que fait ce mixeur, dans le cœur, sans bibliothèque.
//
//  · Le ronron SUIT l'état du moteur (setMotor, appelé à chaque trame entendue) plutôt
//    que les événements MotorOn/MotorOff : après une reprise d'état ou un retour
//    arrière, il est juste sans qu'aucun événement ne soit rejoué.
//  · Les pas et les déplacements de tête sont des événements (onEvent) : l'appelant
//    ne les transmet que pour les trames ENTENDUES, si bien que l'exécution anticipée
//    ne les fait pas sonner deux fois.
//
//  Cosmétique : rien de ceci n'entre dans l'état de la machine.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "io/Fdc.hpp"   // FdcSound

namespace neost {

class DriveSoundMix {
public:
    // Charge drive_spin, drive_startup, drive_seek et drive_click (.wav PCM 8 ou 16
    // bits, mono ou stéréo) de `dir`, rééchantillonnés à `rate`. Faux si aucun.
    bool load(const std::string& dir, uint32_t rate);
    bool loaded() const { return !spin_.empty() || !click_.empty(); }

    void onEvent(FdcSound e);
    void setMotor(bool on);
    // Ajoute `frames` échantillons stéréo entrelacés à `stereo`, au gain `gain`.
    void mix(float* stereo, int frames, float gain);
    void reset();

    // Lecture d'un WAV PCM en flottants mono au débit `rate` (autotests).
    static bool readWav(const std::string& path, uint32_t rate, std::vector<float>& out);

private:
    struct Voice { const std::vector<float>* pcm; std::size_t pos; float gain; };
    std::vector<float> spin_, startup_, seek_, click_;
    std::vector<Voice> voices_;
    bool motor_ = false;
    std::size_t spinPos_ = 0;
    float motorLevel_ = 0.0f;   // rampe d'entrée et de sortie du ronron (pas de clic)
};

}  // namespace neost
