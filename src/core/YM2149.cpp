// =============================================================================
//  YM2149.cpp — Synthèse des 3 voies + bruit du PSG.
//
//  Moteur interne 250 kHz (port Hatari YM2149_DoSamples_250, [D] sound.c:1024-1135 @ f0736b24)
//  + rééchantillonnage pondéré N vers la fréquence de sortie — précision cycle pour
//  sync-buzzer/syncsquare.
//
//  RÉFÉRENCES. « YM2149 » dans un crochet désigne la fiche YM2149 (Yamaha, sept. 1992),
//  extern/datasheets/YM2149_199209.pdf, sha256
//  a4b946bf525fc1a7c6dec9e7662ce2342d3a3c627f980c7c3b1a396df7e91d20 ; notation des sections : cf.
//  l'en-tête de YM2149.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include "core/YM2149.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// Conversion volume fixe 4 bits → index 5 bits dans le DAC ([D] sound.c:205-208 @ f0736b24) :
// volume5 = volume4*2+1, sauf 0 et 1 qui restent 0 et 1 → [0,15] mappé sur [0,31].
// ⚠ [B] YM2149 § 2 Fig. 1 p. 8 : deux pas d'enveloppe par niveau fixe, L15 face à E31, L14 face à
// E29 ; prolongée, la règle place L1 face à E3 et L0 face à E1 (étiquettes du bas illisibles sur
// le scan). Ici L1 tient l'index 1 et L0 l'index 0 : écart non corrigé. Les niveaux fixes n'en
// souffrent pas, la table mesurée rangeant L1 à l'index 1 (cf. dacTable). Ce sont les pas bas de
// l'enveloppe qui sortent trop forts (E1 = L1 au lieu de L0, E3 entre L1 et L2 au lieu de L1), vers
// -50 dB du plein niveau.
const std::array<uint8_t, 16> YM2149::kVolume4to5 = {
    0, 1, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31
};

// Coefficient du passe-haut sous-sonique (DC blocker 1er ordre). Mêmes valeurs que
// Hatari (Subsonic_IIR_HPF, [D] sound.c:379-396 @ f0736b24) : pôle = 1 - 64/32768 ≈ 0.99805, soit
// fc ≈ 15 Hz à 48 kHz. Recentre le signal DAC unipolaire.
// [B] YM2149 § 2 p. 8 : la sortie porte une composante continue d'environ 2 V.
// [F] Le couplage AC du ST et sa coupure réelle ne sont pas établis ici. Tranché par : le schéma
// de l'étage audio du ST, absent de extern/datasheets.
static constexpr double kHpfPole = 1.0 - 64.0 / 32768.0;

namespace {
constexpr uint16_t kMaskVoice[3] = { 0x001f, 0x03e0, 0x7c00 };

enum : int { ENV_GODOWN = 0, ENV_GOUP = 1, ENV_DOWN = 2, ENV_UP = 3 };

// 16 formes d'enveloppe × 3 blocs de 32 volumes (YmEnvDef + YM2149_EnvBuild, [D] sound.c:163-181,735 @ f0736b24).
// [B] YM2149 Table 4 p. 8 : les 16 lignes confrontées une à une, CONT/ATT/ALT/HOLD concordent.
// [B] YM2149 § (6) p. 7 : 32 pas par motif, niveau sur les 5 bits E4-E0.
constexpr int kYmEnvDef[16][3] = {
    { ENV_GODOWN, ENV_DOWN, ENV_DOWN }, { ENV_GODOWN, ENV_DOWN, ENV_DOWN },
    { ENV_GODOWN, ENV_DOWN, ENV_DOWN }, { ENV_GODOWN, ENV_DOWN, ENV_DOWN },
    { ENV_GOUP,   ENV_DOWN, ENV_DOWN }, { ENV_GOUP,   ENV_DOWN, ENV_DOWN },
    { ENV_GOUP,   ENV_DOWN, ENV_DOWN }, { ENV_GOUP,   ENV_DOWN, ENV_DOWN },
    { ENV_GODOWN, ENV_GODOWN, ENV_GODOWN }, { ENV_GODOWN, ENV_DOWN, ENV_DOWN },
    { ENV_GODOWN, ENV_GOUP, ENV_GODOWN }, { ENV_GODOWN, ENV_UP, ENV_UP },
    { ENV_GOUP,   ENV_GOUP, ENV_GOUP }, { ENV_GOUP,   ENV_UP, ENV_UP },
    { ENV_GOUP,   ENV_GODOWN, ENV_GOUP }, { ENV_GOUP,   ENV_DOWN, ENV_DOWN },
};

uint16_t mergeVoice(int c, int b, int a) {
    return uint16_t((c << 10) | (b << 5) | a);
}

uint16_t tonePeriod(uint8_t hi, uint8_t lo) {
    return uint16_t(((hi & 0x0f) << 8) | lo);
}

// Bruit blanc : LFSR 17 étages, 2 taps (17,14) — YM2149_RndCompute, [D] sound.c:965-980 @ f0736b24,
// qui les donne pour mesurés par D. Savinkoff. [B] YM2149 p. 4 dit seulement « pseudo-random ».
uint32_t rndCompute(uint32_t& rack) {
    if (rack & 1u) {
        rack = (rack >> 1) ^ 0x12000u;
        return 0xffffu;
    }
    rack >>= 1;
    return 0u;
}

int32_t sampleToFixed(float s) {
    return int32_t(std::lround(double(s) * 32768.0));
}

float fixedToSample(int64_t v) {
    return float(double(v) / 32768.0);
}
} // namespace

// -----------------------------------------------------------------------------
//  Table de conversion DAC 32×32×32 → échantillon. DÉFAUT = table MESURÉE sur un
//  vrai ST ([D] sound.c:210-212 @ f0736b24 : relevé de tiers, Paulo Simoes, machine et protocole
//  non documentés ; 16³ interpolée en 32³ par moyennes géométriques) — le
//  YM_TABLE_MIXING par défaut d'Hatari ([D] configuration.c:807 @ f0736b24) : l'interaction
//  NON-LINÉAIRE des 3 voies mesurée, pas modélisée. NEOST_YM_MIXING=model rebranche
//  l'ancien modèle de circuit (YM2149_BuildModelVolumeTable) pour A/B à l'oreille.
//  [B] YM2149 § 2 Fig. 1 p. 8 : pour une voie seule, loi logarithmique (.841 par pas de 5 bits,
//  .707 par niveau fixe) ; rien sur le mélange des voies, que seul le câblage du ST détermine.
//  Indexation identique des deux côtés : idx = A | B<<5 | C<<10 (≙ ymout5[Tone3Voices]),
//  la table d'Hatari étant volumetable[C][B][A] → recopie plate 1:1.
// -----------------------------------------------------------------------------
namespace {
// Mesures 4 bits × 3 voies ([D] ym2149_fixed_vol.h:4 @ f0736b24, vendorisé — GPL v2, © 2012 P. Simoes).
const uint16_t kVolumeTableOriginal[16][16][16] =
#include "core/ym2149_fixed_vol.h"
;
} // namespace

const std::array<float, 32768>& YM2149::dacTable() {
    static const std::array<float, 32768> table = [] {
        const char* mixEnv = std::getenv("NEOST_YM_MIXING");
        std::array<float, 32768> t{};

        if (mixEnv && !std::strcmp(mixEnv, "model")) {
            // Modèle de circuit (Hatari YM2149_BuildModelVolumeTable, [D] sound.c:567-680 @ f0736b24).
            constexpr double MaxVol = 65535.0, FOURTH2 = 1.19, WARP = 1.6666666666666667;
            double cond = 2.0 / 3.0 / (1.0 - 1.0 / WARP) - 2.0 / 3.0;
            double c[32];
            for (int i = 31; i >= 1; --i) {
                c[i] = cond / 2.0;
                cond = 1.0 / (1.0 - 1.0 / FOURTH2 / (1.0 / cond + 1.0)) - 1.0;
            }
            c[0] = 1.0e-8;
            const double max = (MaxVol * WARP) / (1.0 + 1.0 / (c[31] + c[31] + c[31]));
            for (int idx = 0; idx < 32768; ++idx) {
                const int a = idx & 31, b = (idx >> 5) & 31, cc = (idx >> 10) & 31;
                const double v = (MaxVol * WARP) / (1.0 + 1.0 / (c[a] + c[b] + c[cc]));
                t[idx] = float(v / max);
            }
            return t;
        }

        // Table mesurée : port EXACT de interpolate_volumetable ([D] sound.c:505-542 @ f0736b24).
        // vt(i,j,k) plat = i*1024 + j*32 + k, i=C (panel), j=B (row), k=A (element).
        std::vector<uint16_t> v(32768, 0);
        auto vt = [&](int i, int j, int k) -> uint16_t& { return v[(i << 10) | (j << 5) | k]; };
        auto geo = [](uint16_t x, uint16_t y) -> uint16_t {
            return uint16_t(0.5 + std::sqrt(double(x) * double(y)));
        };
        for (int i = 1; i < 32; i += 2) {              // 16 panels → un bloc
            for (int j = 1; j < 32; j += 2) {          // 16 rows → un panel
                for (int k = 1; k < 32; k += 2)        // 16 elements → une row
                    vt(i, j, k) = kVolumeTableOriginal[(i - 1) / 2][(j - 1) / 2][(k - 1) / 2];
                vt(i, j, 0) = vt(i, j, 1);             // déplace le 0ᵉ élément
                vt(i, j, 1) = vt(i, j, 3);             // déplace le 1ᵉʳ élément
                vt(i, j, 3) = geo(vt(i, j, 1), vt(i, j, 5));    // interpole le 3ᵉ
                for (int k = 2; k < 32; k += 2)        // interpole les éléments pairs
                    vt(i, j, k) = geo(vt(i, j, k - 1), vt(i, j, k + 1));
            }
            for (int k = 0; k < 32; ++k) {
                vt(i, 0, k) = vt(i, 1, k);             // déplace la 0ᵉ row
                vt(i, 1, k) = vt(i, 3, k);             // déplace la 1ʳᵉ row
                vt(i, 3, k) = geo(vt(i, 1, k), vt(i, 5, k));    // interpole la 3ᵉ
            }
            for (int j = 2; j < 32; j += 2)            // interpole les rows paires
                for (int k = 0; k < 32; ++k)
                    vt(i, j, k) = geo(vt(i, j - 1, k), vt(i, j + 1, k));
        }
        for (int j = 0; j < 32; ++j)
            for (int k = 0; k < 32; ++k) {
                vt(0, j, k) = vt(1, j, k);             // déplace le 0ᵉ panel
                vt(1, j, k) = vt(3, j, k);             // déplace le 1ᵉʳ panel
                vt(3, j, k) = geo(vt(1, j, k), vt(5, j, k));    // interpole le 3ᵉ
            }
        for (int i = 2; i < 32; i += 2)                // interpole les panels pairs
            for (int j = 0; j < 32; ++j)
                for (int k = 0; k < 32; ++k)
                    vt(i, j, k) = geo(vt(i - 1, j, k), vt(i + 1, j, k));

        // Normalisation ≙ YM2149_Normalise_5bit_Table ([D] sound.c:700-724 @ f0736b24) : [0,max] →
        // [0,1] flottant, max = entrée « 3 voies à fond » (0x7fff). Le niveau (Level /
        // Level>>1 STE d'Hatari) est porté par outScale_ ; pas de centrage
        // (YM_OUTPUT_CENTERED=false, [D] sound.c:247 @ f0736b24), le HPF sous-sonique recentre comme chez Hatari.
        const float max = float(v[0x7fff]);
        for (int idx = 0; idx < 32768; ++idx) t[idx] = float(v[idx]) / max;
        return t;
    }();
    return table;
}

const std::array<std::array<uint16_t, 96>, 16>& YM2149::envWaves() {
    static const std::array<std::array<uint16_t, 96>, 16> waves = [] {
        std::array<std::array<uint16_t, 96>, 16> w{};
        for (int env = 0; env < 16; ++env)
            for (int block = 0; block < 3; ++block) {
                int vol = 0, inc = 0;
                switch (kYmEnvDef[env][block]) {
                    case ENV_GODOWN: vol = 31; inc = -1; break;
                    case ENV_GOUP:   vol = 0;  inc = 1;  break;
                    case ENV_DOWN:   vol = 0;  inc = 0;  break;
                    case ENV_UP:     vol = 31; inc = 0;  break;
                }
                for (int i = 0; i < 32; ++i) {
                    w[env][block * 32 + i] = mergeVoice(vol, vol, vol);
                    vol += inc;
                }
            }
        return w;
    }();
    return waves;
}

void YM2149::updateFromRegs(const uint8_t* r) {
    tonePer_[0] = tonePeriod(r[1], r[0]);
    tonePer_[1] = tonePeriod(r[3], r[2]);
    tonePer_[2] = tonePeriod(r[5], r[4]);
    // [D] sound.c:994-1000 @ f0736b24 garde 0 tel quel, sans l'effet mesuré que dit sound.c:1046 @ f0736b24.
    noisePer_   = std::max<uint16_t>(1, uint16_t(r[6] & 0x1f));   // NP=0 vaut NP=1, cf. doSamples250
    // [B] YM2149 § (5) p. 7 : EP sur 16 bits, fE = fMaster / (256·EP) ; EP = 0 n'y est pas défini.
    // Le plancher à 1 : [A] N. Pomarède, mesure sur son STF (programme calé sur la vidéo, sortie
    // enregistrée en WAV), « per=0 gives the same result as per=1 … it's the case for noise and env »
    // (atari-forum.com/viewtopic.php?t=31610&start=25, 9 juin 2017, relu le 2026-11-11 par web.archive.org/web/20240505013950). Hatari le code ainsi ([D] sound.c:1045-1048 @ f0736b24).
    uint32_t ep = (r[12] << 8) | r[11];
    envPer_     = uint16_t(std::max(1u, ep));
    envShape_   = r[13] & 0x0f;

    // [B] YM2149 § (3) p. 6 : R7 B2-B0 = tons C,B,A, B5-B3 = bruits C,B,A, voie validée par un 0.
    const uint8_t mix = r[7];
    for (int ch = 0; ch < 3; ++ch) {
        mixerT_[ch] = (mix & (1u << ch))       ? 0xffffu : 0u;
        mixerN_[ch] = (mix & (1u << (ch + 3))) ? 0xffffu : 0u;
    }

    // [B] YM2149 § (4) p. 7 : M (bit 4) = 1 → niveau tiré de l'enveloppe, M = 0 → niveau fixe L3-L0.
    envMask3_ = 0;
    vol3_     = 0;
    for (int ch = 0; ch < 3; ++ch) {
        const uint8_t vreg = r[8 + ch];
        if (vreg & 0x10) {
            envMask3_ |= kMaskVoice[ch];
        } else {
            vol3_ &= ~kMaskVoice[ch];
            vol3_ |= uint16_t(kVolume4to5[vreg & 0x0f]) << (5 * ch);
        }
    }
}

// PWMaliasFilter ([D] sound.c:468-494 @ f0736b24) : anti-repliement d'émulateur, signé D. Savinkoff,
// sans composant du ST qui lui corresponde.
float YM2149::applyPwm250(float x0) {
    float y;
    if (x0 >= lpf250Y0_) {
        y = x0;
    } else {
        y = (3.0f * (x0 + lpf250X1_) + 2.0f * lpf250Y0_) * 0.125f;
    }
    lpf250X1_ = x0;
    lpf250Y0_ = y;
    return y;
}

// Passe-bas du condensateur C10 du STF. Il lisse les deux fronts, avec une coupure pour chacun : le YM tire
// vers le haut à travers 207 ohms, R8 vers le bas à travers 759 ohms.
// [D] sound.c:439-440 @ f0736b24 : C10 = 0,1 µF, 7693,7 Hz et 2096,6 Hz, calculés sur le schéma STF de déc. 1986.
// [D] sound.c:458,461 @ f0736b24 écrit ces coupures en coefficients pour Fs = 44,1 kHz, et sound.c:1117 @ f0736b24 les
// applique à 250 kHz : ils y coupent vers 43 et 11 kHz. Ne pas réaligner : on garde le circuit, pas ses coefficients.
// Ici, transposition bilinéaire à 250 kHz : K = tan(π·fc/Fs), b = K/(1+K), a = (1-K)/(1+K).
// [F] Schéma lu de seconde main, et Hatari juge « a slightly lower frequency » plausible. Tranché par : le
// spectre d'un créneau en sortie d'un vrai STF.
float YM2149::applyLpfStf250(float x0) {
    constexpr float kUpB = 0.08840989f, kUpA = 0.82318022f;       // 7693,7 Hz
    constexpr float kDownB = 0.02567611f, kDownA = 0.94864777f;   // 2096,6 Hz
    const float y = x0 >= lpf250Y0_ ? kUpB * (x0 + lpf250X1_) + kUpA * lpf250Y0_
                                    : kDownB * (x0 + lpf250X1_) + kDownA * lpf250Y0_;
    lpf250X1_ = x0;
    lpf250Y0_ = y;
    return y;
}

void YM2149::doSamples250(int n) {
    const auto& dac  = dacTable();
    const auto& envW = envWaves();
    int pos = buf250Wr_;

    for (int i = 0; i < n; ++i) {
        // Bruit : compteur incrémenté à 125 kHz (moitié de la cadence interne) mais
        // COMPARAISON à 250 kHz, HORS du bloc 125 kHz — port exact de Hatari
        // ([D] sound.c:1050-1058 @ f0736b24).
        // [B] YM2149 § (2) p. 6 : fN = fMaster / (16·NP), soit un pas de LFSR tous les 2·NP pas 250 kHz.
        // [A] N. Pomarède, STF, même relevé (cf. updateFromRegs) : « noise counter is incremented twice slower
        // than tone counter », et per=0 vaut per=1 pour le bruit. Ici, NP=0 est ramené à 1 et le LFSR avance
        // tous les 2·NP pas 250 kHz. [D] sound.c:1046 @ f0736b24 le note sans l'appliquer au bruit.
        freqDiv2_ ^= 1;
        if (freqDiv2_ == 0)
            noiseCnt_++;
        if (noiseCnt_ >= noisePer_) {
            noiseCnt_ = 0;
            noiseVal_ = rndCompute(rndLfsr_);
        }

        // [B] YM2149 § (1) p. 5 : fT = fMaster / (16·TP), soit une bascule tous les TP pas 250 kHz ; TP = 0
        // n'y est pas défini. [D] sound.c:1045-1048 @ f0736b24 : per=0 donné pour mesuré égal à per=1.
        for (int ch = 0; ch < 3; ++ch) {
            toneCnt_[ch]++;
            if (toneCnt_[ch] >= tonePer_[ch]) {
                toneCnt_[ch] = 0;
                toneVal_[ch] ^= YM_SQUARE_UP;
            }
        }

        // [B] YM2149 § (5) p. 7 : fEA = 32·fE, soit un pas tous les EP pas 250 kHz.
        // [D] sound.c:1089-1090 @ f0736b24 : le bloc 0 une fois, puis les blocs 1 et 2 en boucle.
        envCnt_++;
        if (envCnt_ >= envPer_) {
            envCnt_ = 0;
            envPos_++;
            if (envPos_ >= 3u * 32u)
                envPos_ -= 2u * 32u;
        }

        // [D] sound.c:1100-1111 @ f0736b24 : ton ET bruit, terme masqué toujours haut, puis ET avec le niveau.
        // ⚠ [B] YM2149 § (3) p. 6 : « Nothing is output » quand ton et bruit sont à 1. Ici la voie sort
        // alors son niveau en continu : écart non corrigé. Les digidrums, joués en écrivant le volume
        // voie masquée, n'existent qu'à ce prix ; § 2 p. 8 raisonne d'ailleurs hors composante continue.
        uint16_t env3 = uint16_t(envW[envShape_][envPos_] & envMask3_);
        uint16_t tone3 = 0;
        for (int ch = 0; ch < 3; ++ch) {
            const uint32_t bt = (toneVal_[ch] | mixerT_[ch]) & (noiseVal_ | mixerN_[ch]);
            tone3 |= uint16_t(bt & 0x1f) << (5 * ch);
        }
        tone3 &= uint16_t(env3 | vol3_);

        float s = useStfLpf_ ? applyLpfStf250(dac[tone3]) : applyPwm250(dac[tone3]);
        buf250_[pos] = s;
        pos = (pos + 1) & YM_BUF_250_MASK;
    }
    buf250Wr_ = pos;
}

void YM2149::ensureMargin(uint32_t sampleRate) {
    if (sampleRate == 0) return;
    const int margin = int(std::ceil(double(YM_250_HZ) / sampleRate)) + 2;
    while (((buf250Wr_ - buf250Rd_) & YM_BUF_250_MASK) < margin)
        doSamples250(margin);
}

float YM2149::nextResampleWeightedN(uint32_t sampleRate) {
    const uint32_t intervalFract = uint32_t((uint64_t(YM_250_HZ) * 0x10000ULL) / sampleRate);
    int64_t total = 0;

    if (resampleFracN_) {
        total += int64_t(sampleToFixed(buf250_[buf250Rd_])) * (0x10000 - resampleFracN_);
        buf250Rd_ = (buf250Rd_ + 1) & YM_BUF_250_MASK;
        resampleFracN_ -= 0x10000;
    }
    resampleFracN_ += intervalFract;

    while (resampleFracN_ & 0xffff0000u) {
        total += int64_t(sampleToFixed(buf250_[buf250Rd_])) * 0x10000;
        buf250Rd_ = (buf250Rd_ + 1) & YM_BUF_250_MASK;
        resampleFracN_ -= 0x10000;
    }
    if (resampleFracN_)
        total += int64_t(sampleToFixed(buf250_[buf250Rd_])) * resampleFracN_;

    return fixedToSample(total / int64_t(intervalFract));
}

// Pose l'état dérivé des registres (et consomme un rechargement d'enveloppe).
// Extrait de synthBlock pour pouvoir être appelé À CHAQUE ÉVÉNEMENT REGISTRE, et non
// une fois par bloc d'échantillons hôte — cf. synthesizeFrame.
void YM2149::applyRegs(const uint8_t* r) {
    updateFromRegs(r);
    // Écrire R13 relance la forme ET une nouvelle phase : [A] N. Pomarède, STF, « writing to envwave
    // register does both a restart of the wave pattern and a new phase » (atari-forum.com/viewtopic.php?t=31610&start=25, 9 juin 2017, relu le 2026-11-11 par web.archive.org/web/20240505013950).
    if (envReload_) {
        envReload_ = false;
        envPos_    = 0;
        envCnt_    = 0;
        envShape_  = r[13] & 0x0f;
    }
}

void YM2149::synthBlock(const uint8_t* r, float* out, uint32_t frames, uint32_t sampleRate) {
    applyRegs(r);
    renderHost(out, frames, sampleRate);
}

// Rend `frames` échantillons HÔTE depuis le tampon 250 kHz (rééchantillonnage pondéré).
// Ne touche PAS aux registres : l'état a déjà été posé par applyRegs.
void YM2149::renderHost(float* out, uint32_t frames, uint32_t sampleRate) {
    for (uint32_t i = 0; i < frames; ++i) {
        ensureMargin(sampleRate);
        float s = nextResampleWeightedN(sampleRate);
        // DAC du port parallèle (Pro Sound Designer) : son propre bloc DC (même pôle
        // que le HPF YM), amorcé au branchement — pas d'échelon —, puis le fader.
        if (portBDac_) {
            const float hp = dacLevel_ - dacHpX1_ + float(kHpfPole) * dacHpY0_;
            dacHpX1_ = dacLevel_; dacHpY0_ = hp;
            s += hp * dacGain_;
        }

        // STE (hpfBypass_) : YM BRUT — le HPF sous-sonique s'applique en aval au MIX
        // YM+DMA (DmaSound::applyHpfStereo ≙ [D] dmaSnd.c:699,706 @ f0736b24), comme [D] sound.c:1730-1735 @ f0736b24
        // qui n'appelle PAS Subsonic_IIR_HPF sur le chemin STE/TT.
        if (hpfBypass_) { out[i] = s * outScale_; continue; }
        const double hp = double(s) - hpfX1_ + kHpfPole * hpfY0_;
        hpfX1_ = s;
        hpfY0_ = hp;
        out[i] = float(hp) * outScale_;
    }
}

void YM2149::synthesize(float* out, uint32_t frames, uint32_t sampleRate) {
    synthBlock(regs_.data(), out, frames, sampleRate);
}

void YM2149::synthesizeFrame(float* out, uint32_t frames, uint32_t sampleRate, int64_t frameCycles) {
    if (frameCycles <= 0) frameCycles = 1;
    // Pro Sound Designer : niveau du DAC parallèle (R15) ajouté à chaque segment.
    auto dacSync = [&] { dacLevel_ = portBDac_ ? dacRaw(audioRegs_[15]) : 0.0f; };
    dacSync();

    // ⚠ LES ÉVÉNEMENTS SONT DATÉS SUR LA GRILLE 250 kHz, PAS SUR L'ÉCHANTILLON HÔTE.
    // C'était le défaut (corrigé le 2026-09-02) : l'ancienne version calculait l'offset
    // en échantillons HÔTE (`e.cycle * frames / frameCycles`) et ne synthétisait qu'un
    // bloc par offset — toutes les écritures tombant dans le même échantillon hôte
    // étaient donc ÉCRASÉES, et une seule survivait. Sur un flux normal c'est un jitter
    // de 21 µs sans conséquence, ce que l'inventaire des divergences en disait. Mais un
    // DIGIDRUM joue un échantillon numérisé en martelant les registres de VOLUME : sur
    // Super Hang-On, 897 écritures par trame (~45 kHz) pour 350 offsets hôte distincts —
    // 61 % du flux perdu, c'est-à-dire une DÉCIMATION, donc du repliement : le
    // grésillement rapporté. Sur la grille 250 kHz il y a ~5,6 pas internes par écriture,
    // le rééchantillonneur pondéré (nextResampleWeightedN) fait ensuite sa moyenne, et le
    // flux est FILTRÉ au lieu d'être décimé. C'est aussi ce que fait Hatari, qui applique
    // les écritures à la frontière 250 kHz (Sound_Update avant Sound_WriteReg, [D] psg.c:342,367 @ f0736b24).
    const uint32_t n250 = sampleRate ? uint32_t(uint64_t(frames) * YM_250_HZ / sampleRate) : 0;
    applyRegs(audioRegs_.data());
    uint32_t gen = 0;
    for (const RegEvent& e : events_) {
        uint32_t off = uint32_t(int64_t(e.cycle) * n250 / frameCycles);
        if (off > n250) off = n250;
        if (off > gen) { doSamples250(int(off - gen)); gen = off; }
        const int reg = e.reg & 15;
        audioRegs_[reg] = e.val;
        if (reg == 13) envReload_ = true;
        if (reg == 15) dacSync();
        applyRegs(audioRegs_.data());     // l'écriture prend effet ICI, au pas 250 kHz
    }
    // NEOST_YMEV_DIAG=1 : par trame, le NOMBRE d'écritures registre, le nombre
    // d'INSTANTS sonores distincts (cycles CPU distincts) et combien de ces instants
    // chaque grille sait résoudre — l'ancienne (échantillon hôte) et celle-ci
    // (250 kHz). C'est l'instrument qui a chiffré le défaut : sur Super Hang-On,
    // 897 instants par trame (44,9 kHz, un digidrum), 350 résolus par la grille hôte
    // contre 561 par la grille 250 kHz. Trace pure, sans effet sur l'émulation.
    { static const bool dz = std::getenv("NEOST_YMEV_DIAG") != nullptr;
      if (dz && !events_.empty()) {
        static long fr = 0; ++fr;
        int64_t lastCyc = -1; uint32_t lastH = 0xFFFFFFFFu, lastQ = 0xFFFFFFFFu;
        int inst = 0, dh = 0, dq = 0;
        for (const RegEvent& e : events_) {
            if (int64_t(e.cycle) != lastCyc) { ++inst; lastCyc = int64_t(e.cycle); }
            const uint32_t oh = uint32_t(int64_t(e.cycle) * frames / frameCycles);
            const uint32_t oq = uint32_t(int64_t(e.cycle) * n250 / frameCycles);
            if (oh != lastH) { ++dh; lastH = oh; }
            if (oq != lastQ) { ++dq; lastQ = oq; }
        }
        // Histogramme des ÉCARTS entre instants consécutifs, en cycles CPU. Un pas
        // 250 kHz vaut ~32 cycles à 8 MHz : tout écart inférieur tombe dans le même pas.
        // Écarts entre écritures consécutives, en cycles CPU. Un pas 250 kHz vaut
        // ~32 cycles à 8 MHz. La distribution est le vrai discriminant : un flux
        // d'instants sonores distincts donne des écarts étalés, tandis qu'un mixeur
        // qui pose R8/R9/R10 pour UN MÊME instant donne un histogramme bimodal
        // (2 écarts courts pour 1 long) — cf. Super Hang-On, 66,7 % / 33,3 %.
        int lt32 = 0, lt64 = 0, ge64 = 0; int64_t pc = -1;
        for (const RegEvent& e : events_) {
            if (pc >= 0) { const int64_t g = int64_t(e.cycle) - pc;
                           if (g < 32) ++lt32; else if (g < 64) ++lt64; else ++ge64; }
            pc = int64_t(e.cycle);
        }
        std::fprintf(stderr, "[ymev] f=%ld ecritures=%zu instants=%d resolus_hote=%d resolus_250k=%d ecarts <32=%d 32-63=%d >=64=%d\n",
                     fr, events_.size(), inst, dh, dq, lt32, lt64, ge64);
      } }
    if (gen < n250) doSamples250(int(n250 - gen));
    events_.clear();
    renderHost(out, frames, sampleRate);
}
