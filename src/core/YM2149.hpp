// =============================================================================
//  YM2149.hpp — PSG (Programmable Sound Generator) de l'Atari ST.
//
//  Le YM2149 (clone du AY-3-8910) : 3 voies carrées + bruit + enveloppe, piloté
//  par 16 registres. L'accès CPU se fait en 2 temps via $FF8800 (sélection
//  registre) puis $FF8802 (donnée). Sur ST le PSG est cadencé à 2 MHz.
//
//  Synthèse : la classe produit directement des échantillons (synthesize) que
//  le backend miniaudio tire depuis le thread audio. Le mixage des 3 voies passe
//  par la table DAC non linéaire à charge commune du YM2149 (table modélisée de
//  Hatari), suivie des filtres de sortie analogiques du ST (passe-haut anti-DC +
//  passe-bas PWM). Ton et bruit sont combinés par ET logique (porte), pas en somme.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <vector>

class YM2149 {
public:
    // Horloge du PSG sur Atari ST : 2 MHz. La fréquence d'une voie vaut
    // fclock / (16 * période), d'où le diviseur 16 ci-dessous.
    static constexpr double CLOCK_HZ = 2'000'000.0;

    // --- Interface MMIO (appelée par le Bus) --------------------------------
    uint8_t read8(uint32_t addr) {
        // Décodage partiel du PSG sur l'ST (A0/A1) : $FF8801/03 sont des lectures
        // « void » → 0xFF (psg.c:PSG_ff880x_ReadByte). $FF8800/02 renvoient le
        // registre sélectionné ; $FF8802 reste relisible pour les RMW diagnostics
        // (bclr/bset port A R14) — choix délibéré NeoST, cf. CHANGELOG. Sélecteur ≥16
        // → 0xFF (psg.c:283).
        switch (addr & 3) {
            case 1: case 3: return 0xFF;
            default:
                return (selected_ < 16) ? regs_[selected_] : uint8_t(0xFF);
        }
    }
    void write8(uint32_t addr, uint8_t v) {
        switch (addr & 3) {
            case 0: selected_ = v;  break;                // $FF8800 : choix du registre (8 bits NON masqués, psg.c:258)
            case 2:                                       // $FF8802 : écriture donnée
                if (selected_ >= 16) break;               // registre invalide (≥16) → écriture ignorée (psg.c:335)
                // Masque les bits inutilisés À L'ÉCRITURE (psg.c:351-358) → la relecture
                // renvoie la valeur masquée, comme le YM2149 : tons grossiers A/B/C (R1/3/5)
                // et forme d'enveloppe (R13) sur 4 bits ; ampli A/B/C (R8/9/10) et bruit (R6)
                // sur 5 bits. Ports A/B (R14/15, I/O) et autres registres : 8 bits intacts.
                if      (selected_ == 1 || selected_ == 3 || selected_ == 5 || selected_ == 13) v &= 0x0F;
                else if (selected_ == 6 || selected_ == 8 || selected_ == 9 || selected_ == 10) v &= 0x1F;
                regs_[selected_] = v;                     // valeur visible CPU (relue par read8)
                // Mode PUSH (horloge câblée par le frontend) : on HORODATE l'écriture des
                // registres sonores (0-13) au cycle CPU dans la trame, pour la rejouer au
                // bon instant lors de la synthèse (cf. synthesizeFrame). C'est ce qui capture
                // les modulations sous-buffer (digidrums, sync-buzzer). Le réarmement R13 est
                // alors géré par le rejeu, PAS ici. Mode LEGACY (WASM/direct, pas d'horloge) :
                // on réarme l'enveloppe tout de suite et synthesize lit regs_ en direct.
                if (cycleClock_) {
                    if (selected_ < 14)
                        events_.push_back({ uint32_t(cycleClock_()), selected_, v });
                } else if (selected_ == 13) {
                    envReload_ = true;
                }
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

    // Reset matériel du PSG : remet tous les registres à 0 → volumes 0 = SILENCE
    // immédiat (et tonalités/bruit/enveloppe coupés), réarme l'état de synthèse.
    // Indispensable pour que le son ne PERSISTE PAS après un reset (soft/hard) : un
    // YM2149 laissé en tonalité continue de « biper » sinon (cf. retour utilisateur).
    // regs_ est lu par le thread audio ; le mettre à 0 le rend silencieux aussitôt.
    void reset() {
        regs_.fill(0);
        regs_[7]    = 0xFF;     // mixeur : tons/bruit désactivés au reset (cf. Sound_Reset → R7=0xff)
        regs_[14]   = 0xFF;     // port A au repos : lignes I/O (actives bas) toutes inactives — cf. psg.c:223
        selected_   = 0;
        tonePer_.fill(0); toneCnt_.fill(0); toneVal_.fill(0);
        noisePer_ = noiseCnt_ = 0; noiseVal_ = 0;
        envPer_ = envCnt_ = 0; envPos_ = 0; envShape_ = 0;
        mixerT_.fill(0); mixerN_.fill(0);
        envMask3_ = vol3_ = 0;
        rndLfsr_ = 1; freqDiv2_ = 0;
        buf250_.fill(0.0f); buf250Wr_ = buf250Rd_ = 0;
        resampleFracN_ = 0;
        envReload_  = false;
        lpf250X1_ = lpf250Y0_ = 0.0f;
        hpfX1_ = hpfY0_ = 0.0;
        updateFromRegs(regs_.data());
        audioRegs_ = regs_;               // resynchronise l'ombre audio (rejeu) sur les registres
        events_.clear();                  // jette les écritures horodatées en attente
    }

    // Branche l'horloge frame-relative (cycles CPU depuis le début de la trame), posée
    // par le frontend qui utilise le modèle « push » (cf. synthesizeFrame). Tant qu'elle
    // n'est PAS posée (headless, WASM), aucun événement n'est enregistré et la synthèse
    // reste l'ancienne (synthesize, lecture directe des registres).
    void setCycleClock(std::function<int64_t()> c) {
        cycleClock_ = std::move(c);
        events_.reserve(8192);            // évite les réallocations dans write8 (chemin chaud)
    }

    // Jette les événements de la trame sans synthétiser (frontend audio non démarré) :
    // resynchronise l'ombre audio pour ne pas dériver, et borne la mémoire.
    void clearEvents() { audioRegs_ = regs_; events_.clear(); }

    // Synthèse d'UNE trame en mode « push » : rejoue les écritures de registres
    // horodatées à leur position exacte (cycle → échantillon via frameCycles), en
    // synthétisant par segments entre deux écritures. `frameCycles` = durée de la
    // trame en cycles CPU. Vide les événements à la fin.
    void synthesizeFrame(float* out, uint32_t frames, uint32_t sampleRate, int64_t frameCycles);

    // Échelle de sortie selon la machine : 0.5 sur STE/Mega STE (le mixeur STE met le
    // YM à DEMI-amplitude pour laisser de la marge au son DMA et éviter la saturation
    // quand les deux jouent fort — port de Hatari `YM_OUTPUT_LEVEL>>1`, sound.c:780-784),
    // 1.0 sur ST/Mega ST (pas de son DMA). Posée par `Machine` selon le type machine ;
    // NON remise à zéro par reset() (c'est une propriété figée du matériel, pas de l'état).
    void setOutputScale(float s) { outScale_ = s; }

    // --- Synthèse (appelée par le thread audio miniaudio) -------------------
    // Remplit `out` (mono, float -1..+1) à la fréquence sampleRate.
    void synthesize(float* out, uint32_t frames, uint32_t sampleRate);

    // Registres bruts exposés au débogueur.
    std::array<uint8_t, 16> regs_{};
    uint8_t selected_ = 0;

    // Écriture de registre horodatée (cycle CPU dans la trame). Rejouée par
    // synthesizeFrame pour appliquer la modulation au bon instant (digidrums…).
    struct RegEvent { uint32_t cycle; uint8_t reg; uint8_t val; };

private:
    // Synthétise un BLOC de `frames` échantillons depuis la source de registres `r`
    // (16 octets). Mutualisé entre synthesize (legacy, r=regs_) et synthesizeFrame
    // (push, r=audioRegs_, appelé par segments entre deux écritures rejouées).
    void synthBlock(const uint8_t* r, float* out, uint32_t frames, uint32_t sampleRate);

    // Met à jour périodes/mixeurs/volumes depuis les registres (port Sound_WriteReg).
    void updateFromRegs(const uint8_t* r);
    // Génère `n` échantillons internes à 250 kHz (YM2149_DoSamples_250).
    void doSamples250(int n);
    // Assure assez d'échantillons 250 kHz pour le prochain resample (marge Hatari).
    void ensureMargin(uint32_t sampleRate);
    // Rééchantillonnage pondéré N (YM2149_Next_Resample_Weighted_Average_N).
    float nextResampleWeightedN(uint32_t sampleRate);
    // Passe-bas PWM appliqué à 250 kHz (PWMaliasFilter, sound.c).
    float applyPwm250(float x0);
    // Table des 16 formes d'enveloppe (YmEnvWaves, construite une fois).
    static const std::array<std::array<uint16_t, 96>, 16>& envWaves();

    // Table de conversion DAC 32×32×32 → échantillon float (modèle de circuit Hatari,
    // YM2149_BuildModelVolumeTable). Index = (idxC<<10)|(idxB<<5)|idxA, valeurs déjà
    // normalisées (+ gain de compensation). Construite une seule fois (cf. .cpp).
    static const std::array<float, 32768>& dacTable();

    // Conversion volume fixe 4 bits → index 5 bits dans le DAC (Hatari YmVolume4to5) :
    // volume5 = volume4*2+1, sauf 0 et 1 qui restent 0 et 1 → [0,15] mappé sur [0,31].
    static const std::array<uint8_t, 16> kVolume4to5;

    // Moteur interne 250 kHz (YM2149_DoSamples_250, port sound.c).
    static constexpr int   YM_BUF_250_SIZE = 32768;
    static constexpr int   YM_BUF_250_MASK = YM_BUF_250_SIZE - 1;
    static constexpr int   YM_250_HZ       = 250'000;
    static constexpr uint32_t YM_SQUARE_UP = 0x1f;

    std::array<uint16_t, 3> tonePer_{}, toneCnt_{};
    std::array<uint16_t, 3> toneVal_{};
    uint16_t noisePer_ = 0, noiseCnt_ = 0;
    uint32_t noiseVal_ = 0;
    uint16_t envPer_ = 0, envCnt_ = 0;
    uint32_t envPos_ = 0;
    int      envShape_ = 0;
    std::array<uint32_t, 3> mixerT_{}, mixerN_{};
    uint16_t envMask3_ = 0, vol3_ = 0;
    uint32_t rndLfsr_ = 1;            // LFSR 17 bits Hatari (taps 17,14)
    uint16_t freqDiv2_ = 0;           // bruit à 125 kHz (moitié de 250 kHz)

    std::array<float, YM_BUF_250_SIZE> buf250_{};
    int      buf250Wr_ = 0, buf250Rd_ = 0;
    uint32_t resampleFracN_ = 0;      // position fractionnelle (16.16) pour Weighted_Average_N

    bool envReload_ = false;          // R13 écrit → réinitialiser Env_pos/Env_count

    // Filtres de sortie : PWM à 250 kHz, HPF à la fréquence de sortie (comme Hatari).
    float  lpf250X1_ = 0.0f, lpf250Y0_ = 0.0f;
    double hpfX1_ = 0.0, hpfY0_ = 0.0;

    // Échelle de sortie (1.0 ST, 0.5 STE) — propriété machine, voir setOutputScale().
    float  outScale_ = 1.0f;

    // --- Modèle « push » horodaté (Phase C) ---------------------------------
    // Ombre des registres vue par la SYNTHÈSE (avancée par le rejeu des événements),
    // distincte de regs_ (vue CPU, mise à jour immédiatement par write8 pour read8).
    std::array<uint8_t, 16> audioRegs_{};
    std::vector<RegEvent>   events_;            // écritures horodatées de la trame courante
    std::function<int64_t()> cycleClock_;       // cycle CPU frame-relatif (posé par le frontend push)

    std::function<void(uint8_t)> portAsink_;  // abonné aux écritures du port A (R14)
    std::function<void(uint8_t)> portBsink_;  // abonné aux écritures du port B (R15)
};
