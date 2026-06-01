// =============================================================================
//  Audio.hpp — Backend de sortie son (miniaudio) : YM2149 + bruits de lecteur.
//
//  miniaudio ouvre UN périphérique de lecture et appelle, depuis un thread
//  dédié, un callback qui : (1) synthétise le PSG, puis (2) additionne la sortie
//  de DriveSound (ronron/clics/seek/index). Un seul flux mono float sort donc —
//  c'est le point de mixage du YM2149 et des bruits mécaniques du lecteur.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class YM2149;
class DriveSound;
class DmaSound;

class Audio {
public:
    // drive / dma peuvent être nuls — seul le PSG sort alors.
    explicit Audio(YM2149& psg, DriveSound* drive = nullptr, DmaSound* dma = nullptr);
    ~Audio();

    bool start();              // ouvre et démarre le périphérique
    void stop();
    bool ok() const { return started_; }

    // Remplit `out` (mono float) : PSG synthétisé puis bruits lecteur mixés.
    // Appelé par le callback miniaudio (thread audio) — public pour cet usage.
    void render(float* out, uint32_t frames, uint32_t sampleRate);

private:
    YM2149&     psg_;
    DriveSound* drive_   = nullptr;
    DmaSound*   dma_     = nullptr;
    bool        started_ = false;
    void*       device_  = nullptr;   // ma_device opaque (évite d'inclure miniaudio ici)
};
