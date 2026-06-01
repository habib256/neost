// =============================================================================
//  YM2149.hpp — PSG (Programmable Sound Generator) de l'Atari ST.
//
//  Le YM2149 (clone du AY-3-8910) : 3 voies carrées + bruit + enveloppe, piloté
//  par 16 registres. L'accès CPU se fait en 2 temps via $FF8800 (sélection
//  registre) puis $FF8802 (donnée). Sur ST le PSG est cadencé à 2 MHz.
//
//  Synthèse : la classe produit directement des échantillons (synthesize) que
//  le backend miniaudio tire depuis le thread audio. Enveloppe non modélisée
//  pour l'instant (TODO) — tonalités + bruit + volume fixe suffisent à sonner.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <array>

class YM2149 {
public:
    // Horloge du PSG sur Atari ST : 2 MHz. La fréquence d'une voie vaut
    // fclock / (16 * période), d'où le diviseur 16 ci-dessous.
    static constexpr double CLOCK_HZ = 2'000'000.0;

    // --- Interface MMIO (appelée par le Bus) --------------------------------
    uint8_t read8(uint32_t addr) {
        if ((addr & 3) == 0) return regs_[selected_];   // $FF8800 : registre courant
        return 0xFF;
    }
    void write8(uint32_t addr, uint8_t v) {
        switch (addr & 3) {
            case 0: selected_ = v & 0x0F;  break;        // $FF8800 : choix du registre
            case 2:                                       // $FF8802 : écriture donnée
                regs_[selected_] = v;
                // Écrire R13 (forme d'enveloppe) RÉARME l'enveloppe, même valeur
                // identique (comportement matériel). Drapeau consommé par le thread
                // audio (cf. synthesize) pour éviter de toucher son état ici.
                if (selected_ == 13) envReload_ = true;
                break;
            default: break;
        }
    }

    // --- Synthèse (appelée par le thread audio miniaudio) -------------------
    // Remplit `out` (mono, float -1..+1) à la fréquence sampleRate.
    void synthesize(float* out, uint32_t frames, uint32_t sampleRate);

    // Avance l'enveloppe d'un pas (niveau ±1) selon la forme R13 (bits Continue/
    // Attack/Alternate/Hold) ; gère la fin de rampe (sawtooth / triangle / hold).
    void clockEnvelope(uint8_t shape);

    // Registres bruts exposés au débogueur.
    std::array<uint8_t, 16> regs_{};
    uint8_t selected_ = 0;

private:
    // Table de volume logarithmique 16 niveaux (normalisée 0..1), typique AY/YM.
    static const std::array<float, 16> kVolume;

    // État de synthèse (phase par voie + LFSR de bruit), thread audio.
    std::array<double, 3> phase_{};   // accumulateurs de phase des voies A/B/C
    uint32_t noiseLfsr_ = 1;          // registre à décalage du générateur de bruit
    double   noisePhase_ = 0.0;

    // État de l'enveloppe (générateur de volume 0..15), thread audio.
    double envPhase_  = 0.0;          // accumulateur de phase de l'enveloppe
    int    envLevel_  = 15;           // niveau courant (0..15)
    int    envDir_    = -1;           // sens : +1 montée, -1 descente
    bool   envHold_   = false;        // enveloppe figée (fin de cycle non répété)
    bool   envReload_ = false;        // R13 écrit → réinitialiser (posé par le CPU)
};
