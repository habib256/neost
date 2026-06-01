// =============================================================================
//  DmaSound.hpp — Son DMA de l'Atari STE ($FF8900-$FF8925).
//
//  Le STE ajoute un canal de son NUMÉRIQUE : un DMA lit des échantillons signés
//  8 bits en RAM, d'une adresse de début à une adresse de fin, à une fréquence
//  choisie (6.25 / 12.5 / 25 / 50 kHz), en mono ou stéréo entrelacé. À la fin de
//  la trame : arrêt, ou rebouclage si le bit « repeat » est posé.
//
//  Modèle (comme le YM2149) : la lecture des échantillons se fait sur le thread
//  audio (mix), par rééchantillonnage bloquant-zéro vers la fréquence de sortie.
//  Le compteur d'adresse courant est exposé (lecture CPU). Le mixage final
//  (YM2149 + ce canal) est fait par Audio (GUI) / neost_audio_render (WASM).
//
//  Réf. : Hatari dmaSnd.c, doc registres STE. (c) 2026 VERHILLE Arnaud — NeoST.
// =============================================================================
#pragma once
#include <cstdint>

class Bus;

class DmaSound {
public:
    explicit DmaSound(Bus& bus) : bus_(bus) {}

    // MMIO $FF8900-$FF8925 (octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Additionne le canal numérique dans `out` (mono float) — thread audio.
    void    mix(float* out, uint32_t frames, uint32_t sampleRate);

    // Gain de sortie linéaire (LMC1992 : volume maître + gauche/droite, en mono).
    // S'applique à TOUT le son STE (YM2149 + DMA), comme la puce réelle. 1.0 par
    // défaut (0 dB) → aucun effet tant que le microwire n'est pas programmé.
    float   masterGain() const;

    void    reset();                 // coupe la lecture (RESET machine)
    bool    playing() const { return playing_; }

private:
    int     sampleAt(uint32_t addr, bool stereo) const;   // octet(s) RAM → -128..127 mono
    void    decodeMicrowire();                            // décode la commande LMC1992

    Bus&     bus_;

    // Registres. Adresses sur 24 bits (octets haut/moyen/bas, paires forcées).
    uint8_t  ctrl_ = 0;              // $FF8901 : bit0 = play, bit1 = repeat (loop)
    uint8_t  mode_ = 0;              // $FF8921 : bits0-1 fréquence, bit7 = mono
    uint32_t startAddr_ = 0;         // $FF8903/05/07
    uint32_t endAddr_   = 0;         // $FF890F/11/13
    uint32_t curAddr_   = 0;         // $FF8909/0B/0D (compteur courant)
    uint16_t mwData_ = 0, mwMask_ = 0;  // microwire $FF8922/$FF8924 (mots 16 bits)

    // LMC1992 décodé (volumes en pas de 2 dB). Défauts = 0 dB (aucune atténuation).
    int      mwMaster_ = 40;         // 0..40 → -80..0 dB (volume maître)
    int      mwLeft_   = 20;         // 0..20 → -40..0 dB
    int      mwRight_  = 20;
    int      mwBass_   = 6;          // stockés ; filtrage tonalité = TODO
    int      mwTreble_ = 6;
    int      mwMixing_ = 0;

    // État de lecture (thread audio).
    bool     playing_ = false;
    double   phase_   = 0.0;         // accumulateur de rééchantillonnage
};
