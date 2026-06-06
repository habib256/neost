// =============================================================================
//  YM2149.cpp — Synthèse des 3 voies + bruit du PSG.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/YM2149.hpp"

// Conversion volume fixe 4 bits → index 5 bits dans le DAC (Hatari YmVolume4to5) :
// volume5 = volume4*2+1, sauf 0 et 1 qui restent 0 et 1 → [0,15] mappé sur [0,31].
const std::array<uint8_t, 16> YM2149::kVolume4to5 = {
    0, 1, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31
};

// Coefficient du passe-haut sous-sonique (DC blocker 1er ordre). Mêmes valeurs que
// Hatari (Subsonic_IIR_HPF, sound.c:382-394) : pôle = 1 - 64/32768 ≈ 0.99805, soit
// fc ≈ 15 Hz à 48 kHz. Recentre le signal DAC unipolaire (couplage AC du vrai HW).
static constexpr double kHpfPole = 1.0 - 64.0 / 32768.0;

// -----------------------------------------------------------------------------
//  Table de conversion DAC 32×32×32 → échantillon (modèle de circuit Hatari).
//
//  Le DAC du YM2149 n'est PAS linéaire : les 3 sorties de voie débitent dans une
//  résistance de charge commune (R8 = 1 kΩ). La tension de sortie est donc une
//  fonction non linéaire de la SOMME des 3 conductances (loi (2^-1/4)^(n-31)) :
//  empiler des voies n'additionne PAS les amplitudes (3 voies pleines ≈ ×1, pas ×3).
//  On précalcule UNE fois la table modélisée de Hatari (YM2149_BuildModelVolumeTable,
//  sound.c:615-678), normalisée en float : l'index (idxC<<10)|(idxB<<5)|idxA donne
//  directement l'échantillon. La table est UNIPOLAIRE (offset DC) → le passe-haut en
//  sortie de synthesize la recentre.
//
//  kDacMakeup : facteur d'échelle de sortie. 1.0 = niveau FIDÈLE à Hatari (table
//  ramenée à [0,1], équivalent de YM_OUTPUT_LEVEL=0x7fff puis ÷32768). À ce niveau un
//  carré 3 voies pleines crête à ±0.5 après le passe-haut (transitoire d'attaque ≤1.0,
//  jamais clampé), 1 voie à ±0.28 — soit ~6 dB sous l'ancien modèle linéaire, mais
//  c'est le vrai niveau du DAC ST. NE PAS monter au-dessus de ~1.0 : le mixage
//  (son DMA STE + clamp [-1,1] de Audio::render) saturerait sur les transitoires.
//  L'équilibre fin YM/DMA sur STE relève d'une étape ultérieure (docs/SOUND_HATARI_DIFF.md).
// -----------------------------------------------------------------------------
const std::array<float, 32768>& YM2149::dacTable() {
    static const std::array<float, 32768> table = [] {
        constexpr double MaxVol = 65535.0, FOURTH2 = 1.19, WARP = 1.6666666666666667;
        constexpr float  kDacMakeup = 1.0f;        // 1.0 = niveau fidèle Hatari (cf. en-tête)

        // Conductances par niveau 5 bits (port direct du modèle Hatari).
        double cond = 2.0 / 3.0 / (1.0 - 1.0 / WARP) - 2.0 / 3.0;   // = 1.0
        double c[32];
        for (int i = 31; i >= 1; --i) {
            c[i] = cond / 2.0;
            cond = 1.0 / (1.0 - 1.0 / FOURTH2 / (1.0 / cond + 1.0)) - 1.0;
        }
        c[0] = 1.0e-8;                                              // évite la division par 0

        // Diviseur de tension : Vout = (MaxVol*WARP)/(1 + 1/Σconductances). max = 3 voies pleines.
        const double max = (MaxVol * WARP) / (1.0 + 1.0 / (c[31] + c[31] + c[31]));
        std::array<float, 32768> t{};
        for (int idx = 0; idx < 32768; ++idx) {
            const int a = idx & 31, b = (idx >> 5) & 31, cc = (idx >> 10) & 31;
            const double v = (MaxVol * WARP) / (1.0 + 1.0 / (c[a] + c[b] + c[cc]));
            t[idx] = float(v / max) * kDacMakeup;
        }
        return t;
    }();
    return table;
}

// Fait avancer l'enveloppe d'un cran. À la fin d'une rampe (niveau hors [0,31]),
// les 4 bits de R13 décident de la suite (cf. les 8 formes utiles de l'AY/YM) :
//   Continue=0           → une seule rampe puis niveau 0 figé (formes $0-$7) ;
//   Continue=1, Hold=1   → une rampe puis figé (à l'extrémité atteinte, ou à
//                          l'extrémité opposée si Alternate) ;
//   Continue=1, Hold=0   → répétition : dent de scie (Alternate=0) ou
//                          triangle (Alternate=1, on inverse le sens).
void YM2149::clockEnvelope(uint8_t shape) {
    if (envHold_) return;
    const bool cont = shape & 0x08, attack = shape & 0x04;
    const bool alt  = shape & 0x02, hold   = shape & 0x01;
    (void)attack;

    envLevel_ += envDir_;
    if (envLevel_ >= 0 && envLevel_ <= 31) return;          // toujours dans la rampe

    if (!cont) {                                            // $0-$7 : une rampe puis 0
        envLevel_ = 0; envHold_ = true;
    } else if (hold) {                                      // une rampe puis figé
        envLevel_ = alt ? (envDir_ > 0 ? 0 : 31)            // Alternate : extrémité opposée
                        : (envDir_ > 0 ? 31 : 0);           // sinon : extrémité atteinte
        envHold_ = true;
    } else if (alt) {                                       // triangle : on inverse le sens
        envDir_ = -envDir_;
        envLevel_ = (envDir_ > 0) ? 0 : 31;
    } else {                                                // dent de scie : on repart
        envLevel_ = (envDir_ > 0) ? 0 : 31;
    }
}

void YM2149::synthBlock(const uint8_t* r, float* out, uint32_t frames, uint32_t sampleRate) {
    const auto& dac = dacTable();

    // Lit la source de registres `r` (regs_ en mode legacy, audioRegs_ avancé par le
    // rejeu en mode push). Period/mix/forme sont relus à CHAQUE bloc → un segment qui
    // suit une écriture rejouée voit bien la nouvelle valeur.
    const auto period = [&](int lo, int hi) -> int {
        const int p = (r[lo] | ((r[hi] & 0x0F) << 8));
        return p < 1 ? 1 : p;                       // période 0 ≡ 1 (évite /0)
    };
    const double freqA = CLOCK_HZ / (16.0 * period(0, 1));
    const double freqB = CLOCK_HZ / (16.0 * period(2, 3));
    const double freqC = CLOCK_HZ / (16.0 * period(4, 5));
    const double freq[3] = { freqA, freqB, freqC };

    const int noisePer = (r[6] & 0x1F) ? (r[6] & 0x1F) : 1;
    const double noiseFreq = CLOCK_HZ / (16.0 * noisePer);

    const uint8_t mix = r[7];                   // bits 0-2 tons, 3-5 bruit (actifs à 0)

    // Enveloppe : période 16 bits (R11/R12), un pas de niveau à fclock/8/période.
    // La rampe compte 32 crans (niveaux D/A 5 bits) ; pour qu'une rampe complète dure
    // toujours 256*envPer/CLOCK comme sur Hatari, le diviseur est 8.0 (32*8 = 256).
    const uint8_t shape   = r[13] & 0x0F;
    const int     envPer  = (r[11] | (r[12] << 8)) ? (r[11] | (r[12] << 8)) : 1;
    const double  envStepFreq = CLOCK_HZ / (8.0 * envPer);
    const double  incE = envStepFreq / sampleRate;
    if (envReload_) {                               // R13 écrit → réinitialise l'enveloppe
        envReload_ = false;
        envDir_   = (shape & 0x04) ? +1 : -1;       // Attack : monte, sinon descend
        envLevel_ = (shape & 0x04) ? 0 : 31;
        envHold_  = false;
        envPhase_ = 0.0;
    }

    const double incN = noiseFreq / sampleRate;

    for (uint32_t i = 0; i < frames; ++i) {
        // Avance le bruit : à chaque période, on cadence le LFSR 17 bits.
        noisePhase_ += incN;
        while (noisePhase_ >= 1.0) {
            noisePhase_ -= 1.0;
            const uint32_t bit = ((noiseLfsr_ ^ (noiseLfsr_ >> 3)) & 1);
            noiseLfsr_ = (noiseLfsr_ >> 1) | (bit << 16);
        }
        const int noiseBit = noiseLfsr_ & 1;        // état binaire du bruit (0/1)

        // Avance l'enveloppe ; son niveau sert d'index volume aux voies en mode enveloppe.
        envPhase_ += incE;
        while (envPhase_ >= 1.0) { envPhase_ -= 1.0; clockEnvelope(shape); }
        const int envIdx = envLevel_ & 0x1F;

        // Construit l'index du DAC : 5 bits de volume par voie (A=bits 0-4, B=5-9, C=10-14).
        int dacIdx = 0;
        for (int ch = 0; ch < 3; ++ch) {
            phase_[ch] += freq[ch] / sampleRate;
            if (phase_[ch] >= 1.0) phase_[ch] -= 1.0;
            const bool toneOn  = !((mix >> ch)        & 1);   // bit à 0 = activé
            const bool noiseOn = !((mix >> (ch + 3))  & 1);
            // État binaire de la voie : ET LOGIQUE (porte) entre ton et bruit, voie
            // désactivée ⇒ terme toujours haut. Exactement comme Hatari (sound.c:1098) :
            //   bt = (Tone | ~Tmask) & (Noise | ~Nmask)  → la porteuse hache le bruit.
            const int toneBit = toneOn ? (phase_[ch] < 0.5 ? 1 : 0) : 1;
            const int gateBit = (noiseOn ? noiseBit : 1) & toneBit;
            // Volume 5 bits : suit l'enveloppe (bit 4 du registre 8/9/10) ou volume fixe.
            const uint8_t vreg = r[8 + ch];
            const int volIdx = (vreg & 0x10) ? envIdx : kVolume4to5[vreg & 0x0F];
            // Voie « haute » ⇒ son volume ; « basse » ⇒ 0 (cette voie est muette).
            dacIdx |= (gateBit ? volIdx : 0) << (5 * ch);
        }

        // Conversion non linéaire des 3 volumes en un échantillon (DAC à charge commune).
        const float s = dac[dacIdx];

        // (1) Passe-haut sous-sonique ~15 Hz : retire la composante continue du DAC
        //     unipolaire (couplage AC du vrai HW). y = x - x1 + pôle*y1 (Hatari Subsonic_IIR_HPF).
        const double hp = double(s) - hpfX1_ + kHpfPole * hpfY0_;
        hpfX1_ = s; hpfY0_ = hp;

        // (2) Passe-bas PWM (filtre par DÉFAUT de Hatari, PWMaliasFilter, sound.c:479-492) :
        //     adoucit sélectivement le front descendant → réduit l'aliasing des notes aiguës
        //     sans tuer les harmoniques (front montant laissé passer tel quel).
        const float x0 = float(hp);
        float y;
        if (x0 >= lpfY0_) y = x0;                                  // front montant : passe
        else              y = (3.0f * (x0 + lpfX1_) + 2.0f * lpfY0_) * (1.0f / 8.0f);
        lpfX1_ = x0; lpfY0_ = y;

        out[i] = y * outScale_;                                    // ½ amplitude sur STE (cf. setOutputScale)
    }
}

// Synthèse LEGACY (WASM / appel direct sans horloge câblée) : lit les registres CPU
// en direct. Ne capture PAS les modulations sous-buffer — utiliser synthesizeFrame
// (modèle push) pour cela.
void YM2149::synthesize(float* out, uint32_t frames, uint32_t sampleRate) {
    synthBlock(regs_.data(), out, frames, sampleRate);
}

// Synthèse d'UNE trame en mode « push » : rejoue les écritures de registres horodatées
// à leur position exacte dans la trame, en synthétisant par segments. C'est ce qui
// rend audibles les digidrums / sync-buzzer / arpèges très rapides (modulations qui
// changent les registres plusieurs fois par buffer audio).
void YM2149::synthesizeFrame(float* out, uint32_t frames, uint32_t sampleRate, int64_t frameCycles) {
    if (frameCycles <= 0) frameCycles = 1;
    uint32_t pos = 0;
    for (const RegEvent& e : events_) {
        // Position échantillon de l'écriture dans la trame (cycle CPU → échantillon).
        uint32_t off = uint32_t(int64_t(e.cycle) * frames / frameCycles);
        if (off > frames) off = frames;
        if (off > pos) { synthBlock(audioRegs_.data(), out + pos, off - pos, sampleRate); pos = off; }
        audioRegs_[e.reg] = e.val;                  // applique l'écriture À sa position
        if (e.reg == 13) envReload_ = true;         // R13 → réarme l'enveloppe au bon instant
    }
    if (pos < frames) synthBlock(audioRegs_.data(), out + pos, frames - pos, sampleRate);
    events_.clear();
}
