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
class Mfp;
class Scheduler;

class DmaSound {
public:
    explicit DmaSound(Bus& bus) : bus_(bus) {}

    // L'ordonnanceur date la fin de trame (→ event-count Timer A du MFP), sur le
    // thread d'émulation : c'est là que doit tomber l'interruption, pas sur le
    // thread audio (qui ne fait que générer le son). Branchés par Machine.
    void setScheduler(Scheduler* s) { sched_ = s; }
    // Câble le MFP et lui SIGNALE la présence du son DMA (modèle STE/Mega STE) :
    // le MFP n'XOR la ligne XSINT dans GPIP7 que si ce flag est posé (cf. Hatari
    // MFP_Main_Compute_GPIP7 : XOR réservé à Config_IsMachineSTE()/TT()).
    void setMfp(Mfp* m);

    // Échéance « fin de trame DMA » : pulse Timer A (TAI), reboucle si repeat.
    void onFrameEnd();

    // MMIO $FF8900-$FF8925 (octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Additionne le canal numérique dans `out` (mono float) — thread audio.
    void    mix(float* out, uint32_t frames, uint32_t sampleRate);

    // Gain de sortie linéaire (LMC1992 : volume maître + gauche/droite, en mono).
    // S'applique à TOUT le son STE (YM2149 + DMA), comme la puce réelle. 1.0 par
    // défaut (0 dB) → aucun effet tant que le microwire n'est pas programmé.
    float   masterGain() const;

    // Correcteur de tonalité du LMC1992 (basses/aigus, ±12 dB) appliqué au mix
    // complet via deux filtres en plateau (shelving). Bypass total au défaut
    // (0/0 dB) → aucun coût ni risque tant que la tonalité n'est pas programmée.
    void    applyTone(float* out, uint32_t frames, uint32_t sampleRate);

    // RESET machine : coupe la lecture. `cold` = reset à FROID (power-cycle) → ré-init
    // du LMC1992 ; à chaud (Ctrl+reset) le Microwire n'a PAS de signal de reset et
    // conserve ses volumes/mixage (cf. Hatari DmaSnd_Reset, bloc `if (bCold)`).
    void    reset(bool cold = false);
    bool    playing() const { return playing_; }

private:
    int     sampleAt(uint32_t addr, bool stereo) const;   // octet(s) RAM → -128..127 mono
    void    decodeMicrowire();                            // décode la commande LMC1992
    void    scheduleFrameEnd();                           // date la prochaine fin de trame
    void    startNewFrame();                              // (re)démarre une trame (gère start==end)
    void    setXsint(bool level);                         // pilote la ligne XSINT (→ MFP GPIP7)

public:
    // Une étape du shift série Microwire (datée par le Scheduler, source MICROWIRE) :
    // décale $FF8922 vers 0 (16 étapes) puis, à 0, décode la commande LMC1992. Sans
    // ce shift, les diagnostics qui pollent $FF8922 jusqu'à 0 (STE_Test) bouclent.
    void    onMicrowireShift();
private:

    Bus&        bus_;
    Mfp*        mfp_   = nullptr;
    Scheduler*  sched_ = nullptr;

    // Registres. Adresses sur 24 bits (octets haut/moyen/bas, paires forcées).
    uint8_t  ctrl_ = 0;              // $FF8901 : bit0 = play, bit1 = repeat (loop)
    uint8_t  mode_ = 0;              // $FF8921 : bits0-1 fréquence, bit7 = mono
    uint32_t startAddr_ = 0;         // $FF8903/05/07
    uint32_t endAddr_   = 0;         // $FF890F/11/13
    uint32_t curAddr_   = 0;         // $FF8909/0B/0D (compteur courant)
    uint16_t mwData_ = 0, mwMask_ = 0;  // microwire $FF8922/$FF8924 (mots 16 bits)
    uint16_t mwShift_ = 0;              // valeur LUE en $FF8922 pendant le shift (→ 0)
    int      mwSteps_ = 0;              // décalages restants (16 au départ, 0 = fini)

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

    // Ligne XSINT (External Sound INTerrupt) du son DMA STE : HAUT pendant qu'une
    // trame joue, BAS à l'arrêt / fin de trame. Câblée à TAI (Timer A event-count,
    // déjà géré via onFrameEnd) ET à GPIP7 du MFP (XOR avec la détection moniteur).
    // Réf. Hatari DmaSnd_Update_XSINT_Line / DmaSnd_Get_XSINT_Line.
    bool     xsint_ = false;

    // État des filtres de tonalité (biquads Direct Form I), thread audio.
    double   bx1_ = 0, bx2_ = 0, by1_ = 0, by2_ = 0;   // plateau basses
    double   tx1_ = 0, tx2_ = 0, ty1_ = 0, ty2_ = 0;   // plateau aigus
};
