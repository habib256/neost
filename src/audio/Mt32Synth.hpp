// =============================================================================
//  Mt32Synth.hpp — Roland MT-32 / CM-32L émulé (libmt32emu, projet Munt, LGPL 2.1)
//  branché sur le MIDI OUT de l'ACIA et mixé dans la sortie audio de NeoST.
//
//  C'est l'expandeur que visaient les morceaux Cubase d'avant General MIDI (timbres
//  LA, rythmique « Rhythm Part ») : les numéros de programme d'un `.ALL` de 1991
//  retrouvent leur sens. Munt a besoin des ROM Roland (MT32_CONTROL/MT32_PCM ou
//  CM32L_CONTROL/CM32L_PCM), sous copyright : l'utilisateur les dépose dans
//  roms/mt32/ (cf. README), NeoST ne les fournit pas.
//
//  Horloge = l'AUDIO : chaque octet MIDI est daté du cycle 68000 où l'ACIA l'a émis,
//  puis converti en horodatage « échantillons rendus » de Munt au moment du rendu de
//  la trame (Synth::playMsg(msg, timestamp)) — précision à l'échantillon, aucune
//  gigue d'hôte (contrairement à une sortie MIDI temps réel).
//
//  Sans NEOST_WITH_MT32 (libmt32emu absente au configure), coquille vide.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "core/Pacing.hpp"
#include <cstdint>
#include <string>
#include <vector>

class Mt32Synth {
public:
    Mt32Synth();
    ~Mt32Synth();
    static bool available();                 // compilé avec libmt32emu ?

    // Ouvre le synthé avec les ROM trouvées dans `romDir` (CM-32L préféré au MT-32
    // quand les deux jeux sont là). `outputRate` = fréquence de la sortie audio.
    // `model` : "auto" (CM-32L si présent, sinon MT-32), "mt32" ou "cm32l" (strict).
    bool open(const std::string& romDir, uint32_t outputRate, const std::string& model = "auto");
    void close();
    bool isOpen() const { return synth_ != nullptr; }
    const std::string& model() const { return model_; }     // « MT-32 » / « CM-32L »
    const std::string& lastError() const { return error_; }

    // Octet MIDI OUT daté (thread d'émulation — le même que render).
    void byteAt(uint8_t b, int64_t cycle);
    // Jette les événements en attente SANS les jouer. À appeler quand la trame ne sera
    // pas rendue (pas de périphérique audio) : sans ça la file grossit sans fin, cf.
    // Audio::produceFrame. Le parseur n'est PAS réinitialisé, pour qu'un SysEx à cheval
    // sur plusieurs trames (un dump de patch MT-32 dure ~80 ms, soit 4 trames) survive.
    void clearEvents();
    // « MIDI panic » de l'interface : Reset All Controllers puis All Notes Off sur les
    // 16 canaux, appliqués DIRECTEMENT à Munt (Synth::playMsgNow), hors du parseur du
    // flux de l'ACIA — même raison que GmSynth::panic (running status et SysEx en cours
    // de l'invité préservés). Thread d'émulation (le même que render).
    void panic();

    // Rend `frames` échantillons stéréo entrelacés couvrant la trame émulée
    // [frameStartCycle, frameStartCycle + frameCycles) et les AJOUTE à `lr`.
    void render(float* lr, int frames, int64_t frameStartCycle, int64_t frameCycles);

    void setGain(float g) { gain_ = g; }

private:
    struct Event { int64_t cycle; uint32_t msg; std::vector<uint8_t> sysex; };

    void* synth_ = nullptr;                  // MT32Emu::Synth
    void* src_ = nullptr;                    // MT32Emu::SampleRateConverter
    const void* romControl_ = nullptr;       // MT32Emu::ROMImage
    const void* romPcm_ = nullptr;
    std::string model_, error_;
    float gain_ = 0.9f;
    uint32_t outputRate_ = 48000;
    std::vector<float> scratch_;
    std::vector<Event> pending_;

    // Parseur MIDI (running status, SysEx, temps réel).
    uint8_t status_ = 0; int needed_ = 0; uint8_t data_[2] = {0, 0}; int got_ = 0;
    bool inSysex_ = false; std::vector<uint8_t> sysex_;
    void parse(uint8_t b, int64_t cycle);
};
