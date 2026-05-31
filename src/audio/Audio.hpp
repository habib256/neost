// =============================================================================
//  Audio.hpp — Backend de sortie son (miniaudio) branché sur le YM2149.
//
//  miniaudio ouvre un périphérique de lecture et appelle, depuis un thread
//  dédié, un callback qui tire les échantillons du PSG. On reste mono float
//  (suffisant pour le PSG ST) ; miniaudio convertit vers le format matériel.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once

class YM2149;

class Audio {
public:
    explicit Audio(YM2149& psg);
    ~Audio();

    bool start();              // ouvre et démarre le périphérique
    void stop();
    bool ok() const { return started_; }

private:
    YM2149& psg_;
    bool    started_ = false;
    void*   device_  = nullptr;   // ma_device opaque (évite d'inclure miniaudio ici)
};
