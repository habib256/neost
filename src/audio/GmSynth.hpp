// =============================================================================
//  GmSynth.hpp — Synthé General MIDI intégré (TinySoundFont, MIT, vendorisé)
//  branché sur le MIDI OUT de l'ACIA et mixé dans la sortie audio de NeoST.
//
//  C'est l'équivalent Linux/Windows du DLSMusicDevice d'Apple : du son General
//  MIDI sans rien installer. Apple embarque sa banque GS dans l'OS ; ici la banque
//  est une SoundFont (.sf2) — NeoST livre TimGM6mb (roms/gm/, 5,7 Mo, GPL-2) et
//  open() accepte aussi bien un fichier .sf2 qu'un dossier à fouiller, avec repli
//  sur les banques système (/usr/share/soundfonts, /usr/share/sounds/sf2).
//
//  Même schéma que Mt32Synth (l'horloge = l'AUDIO) : chaque octet MIDI est daté du
//  cycle 68000 où l'ACIA l'a émis, et le rendu de la trame découpe ses échantillons
//  aux dates des messages — précision à l'échantillon, aucune gigue d'hôte. C'est
//  MIEUX que le chemin macOS (DLSMusicDevice temps réel) sur ce point.
//
//  Sur macOS le GUI préfère le DLSMusicDevice (App::midiOutApply) ; la classe
//  compile partout — TSF est un header vendorisé, sans dépendance système.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "audio/MidiMessageParser.hpp"
#include "core/Pacing.hpp"
#include <cstdint>
#include <string>
#include <vector>

class GmSynth {
public:
    GmSynth();
    ~GmSynth();
    static bool available();                 // TSF vendorisé : vrai partout

    // Ouvre le synthé. `sf` = un fichier .sf2, ou un dossier où en chercher un
    // (premier *.sf2 par ordre de nom) ; à défaut, banques système. `outputRate` =
    // fréquence de la sortie audio.
    bool open(const std::string& sf, uint32_t outputRate);
    void close();
    bool isOpen() const { return synth_ != nullptr; }
    const std::string& soundFont() const { return sfName_; }   // fichier chargé
    const std::string& lastError() const { return error_; }

    // Octet MIDI OUT daté (thread d'émulation — le même que render).
    void byteAt(uint8_t b, int64_t cycle);
    // Jette les événements en attente SANS les jouer (pas de périphérique audio) —
    // même contrat que Mt32Synth::clearEvents, mêmes raisons (cf. Audio::produceFrame).
    void clearEvents();
    // « MIDI panic » de l'interface : All Sound Off, Reset All Controllers et All Notes
    // Off sur les 16 canaux, appliqués DIRECTEMENT au moteur TSF. Surtout pas par
    // byteAt : le parseur est celui du flux de l'ACIA, et y injecter « Bn 78 00 »
    // laissait son running status sur $BF — la note suivante du programme ST
    // (« 3E 40 » après un « 90 3C 40 ») devenait un contrôleur — et coupait un SysEx
    // en cours. Thread d'émulation (le même que render).
    void panic();

    // Rend `frames` échantillons stéréo entrelacés couvrant la trame émulée
    // [frameStartCycle, frameStartCycle + frameCycles) et les AJOUTE à `lr`.
    void render(float* lr, int frames, int64_t frameStartCycle, int64_t frameCycles);

    void setGain(float g) { gain_ = g; }

private:
    // A28 : UNE seule définition de l'horloge CPU/bus, dans core/Pacing.hpp.
    static constexpr double kCpuHz = neost::pacing::kCpuHz;
    // Messages de VOIE seulement (3 octets max) : un synthé GM n'a rien à faire des
    // SysEx (pas de patchs à charger, contrairement au MT-32) ni du temps réel.
    struct Event { int64_t cycle; uint8_t msg[3]; int len; };

    void* synth_ = nullptr;                  // tsf*
    std::string sfName_, error_;
    float gain_ = 0.9f;
    uint32_t rate_ = 48000;
    std::vector<float> scratch_;
    std::vector<Event> pending_;

    // Parseur MIDI partagé (running status, SysEx, temps réel) ; curCycle_ date le
    // message complet au cycle de son DERNIER octet.
    neost::midi::Parser parser_;
    int64_t curCycle_ = 0;

    void apply(const uint8_t* msg, int len); // message complet → TSF
};
