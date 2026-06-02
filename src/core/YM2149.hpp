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
#include <functional>

class YM2149 {
public:
    // Horloge du PSG sur Atari ST : 2 MHz. La fréquence d'une voie vaut
    // fclock / (16 * période), d'où le diviseur 16 ci-dessous.
    static constexpr double CLOCK_HZ = 2'000'000.0;

    // --- Interface MMIO (appelée par le Bus) --------------------------------
    uint8_t read8(uint32_t /*addr*/) {
        // $FF8800 ET $FF8802 renvoient le registre sélectionné : le décodage du PSG
        // sur l'ST est partiel (seul A1 distingue select/data en écriture). Les
        // diagnostics font des read-modify-write (bclr/bset) sur la donnée $FF8802
        // du port A (R14) → il FAUT relire la valeur courante, pas 0xFF.
        return regs_[selected_];
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
                // R14 = port A (I/O) : pilote sélection lecteur/face, strobe Centronics,
                // et les sorties RS232 RTS (bit3)/DTR (bit4). Notifie l'abonné éventuel.
                if (selected_ == 14 && portAsink_) portAsink_(v);
                // R15 = port B = données du port parallèle (Centronics). Abonné éventuel
                // (fixture de bouclage parallèle→BUSY/joystick du diagnostic).
                if (selected_ == 15 && portBsink_) portBsink_(v);
                break;
            default: break;
        }
    }

    // Abonné aux écritures du port A (R14) : reçoit la valeur écrite. Sert à câbler
    // les sorties RS232 RTS (bit3)/DTR (bit4) sur les entrées de contrôle du MFP via
    // un connecteur de bouclage (cf. Machine).
    void setPortASink(std::function<void(uint8_t)> s) { portAsink_ = std::move(s); }
    void setPortBSink(std::function<void(uint8_t)> s) { portBsink_ = std::move(s); }

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
    // Table de volume D/A 5 bits (32 niveaux, normalisée 0..1), MESURÉE sur un vrai
    // ST (Hatari ymout1c5bit[32], chaque valeur ÷ 65535). Plus fidèle que l'ancienne
    // approximation log 16 niveaux.
    static const std::array<float, 32> kVolume;

    // Conversion volume fixe 4 bits → index 5 bits dans kVolume (Hatari YmVolume4to5) :
    // volume5 = volume4*2+1, sauf 0 et 1 qui restent 0 et 1 → [0,15] mappé sur [0,31].
    static const std::array<uint8_t, 16> kVolume4to5;

    // État de synthèse (phase par voie + LFSR de bruit), thread audio.
    std::array<double, 3> phase_{};   // accumulateurs de phase des voies A/B/C
    uint32_t noiseLfsr_ = 1;          // registre à décalage du générateur de bruit
    double   noisePhase_ = 0.0;

    // État de l'enveloppe (générateur de volume 0..31), thread audio.
    double envPhase_  = 0.0;          // accumulateur de phase de l'enveloppe
    int    envLevel_  = 31;           // niveau courant (0..31)
    int    envDir_    = -1;           // sens : +1 montée, -1 descente
    bool   envHold_   = false;        // enveloppe figée (fin de cycle non répété)
    bool   envReload_ = false;        // R13 écrit → réinitialiser (posé par le CPU)

    std::function<void(uint8_t)> portAsink_;  // abonné aux écritures du port A (R14)
    std::function<void(uint8_t)> portBsink_;  // abonné aux écritures du port B (R15)
};
