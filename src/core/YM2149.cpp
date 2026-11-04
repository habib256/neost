// =============================================================================
//  YM2149.cpp — Synthèse des 3 voies + bruit du PSG.
//
//  Moteur interne 250 kHz (port Hatari YM2149_DoSamples_250) + rééchantillonnage
//  pondéré N vers la fréquence de sortie — précision cycle pour sync-buzzer/syncsquare.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <cstdio>
#include "core/YM2149.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// Conversion volume fixe 4 bits → index 5 bits dans le DAC (Hatari YmVolume4to5) :
// volume5 = volume4*2+1, sauf 0 et 1 qui restent 0 et 1 → [0,15] mappé sur [0,31].
const std::array<uint8_t, 16> YM2149::kVolume4to5 = {
    0, 1, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31
};

// Coefficient du passe-haut sous-sonique (DC blocker 1er ordre). Mêmes valeurs que
// Hatari (Subsonic_IIR_HPF, [D] sound.c:382-394 @ f0736b24) : pôle = 1 - 64/32768 ≈ 0.99805, soit
// fc ≈ 15 Hz à 48 kHz. Recentre le signal DAC unipolaire (couplage AC du vrai HW).
static constexpr double kHpfPole = 1.0 - 64.0 / 32768.0;

namespace {
constexpr uint16_t kMaskVoice[3] = { 0x001f, 0x03e0, 0x7c00 };

enum : int { ENV_GODOWN = 0, ENV_GOUP = 1, ENV_DOWN = 2, ENV_UP = 3 };

// 16 formes d'enveloppe × 3 blocs de 32 volumes (YmEnvDef + YM2149_EnvBuild, sound.c).
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

// Bruit blanc : LFSR 17 étages, 2 taps (17,14) — YM2149_RndCompute, [D] sound.c:969 @ f0736b24.
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
//  vrai ST (Paulo Simoes, 16³ interpolée en 32³ par moyennes géométriques) — le
//  YM_TABLE_MIXING par défaut d'Hatari ([D] configuration.c:807 @ f0736b24) : l'interaction
//  NON-LINÉAIRE des 3 voies mesurée, pas modélisée. NEOST_YM_MIXING=model rebranche
//  l'ancien modèle de circuit (YM2149_BuildModelVolumeTable) pour A/B à l'oreille.
//  Indexation identique des deux côtés : idx = A | B<<5 | C<<10 (≙ ymout5[Tone3Voices]),
//  la table d'Hatari étant volumetable[C][B][A] → recopie plate 1:1.
// -----------------------------------------------------------------------------
namespace {
// Mesures 4 bits × 3 voies (ym2149_fixed_vol.h, vendorisé — GPL v2, © 2012 P. Simoes).
const uint16_t kVolumeTableOriginal[16][16][16] =
#include "core/ym2149_fixed_vol.h"
;
} // namespace

const std::array<float, 32768>& YM2149::dacTable() {
    static const std::array<float, 32768> table = [] {
        const char* mixEnv = std::getenv("NEOST_YM_MIXING");
        std::array<float, 32768> t{};

        if (mixEnv && !std::strcmp(mixEnv, "model")) {
            // Modèle de circuit (Hatari YM2149_BuildModelVolumeTable, [D] sound.c:617-680 @ f0736b24).
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

        // Table mesurée : port EXACT de interpolate_volumetable ([D] sound.c:505-543 @ f0736b24).
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
        // (YM_OUTPUT_CENTERED=false), le HPF sous-sonique recentre comme chez Hatari.
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
    // Période de bruit BRUTE, sans plancher à 1 : Hatari (YM2149_NoisePer) garde 0
    // tel quel — le « per=0 ⇒ per=1 » est réalisé par la structure incrémente-puis-
    // compare de doSamples250 (comparaison à 250 kHz, cf. [D] sound.c:1050-1058 @ f0736b24).
    noisePer_   = uint16_t(r[6] & 0x1f);
    uint32_t ep = (r[12] << 8) | r[11];
    envPer_     = uint16_t(std::max(1u, ep));
    envShape_   = r[13] & 0x0f;

    const uint8_t mix = r[7];
    for (int ch = 0; ch < 3; ++ch) {
        mixerT_[ch] = (mix & (1u << ch))       ? 0xffffu : 0u;
        mixerN_[ch] = (mix & (1u << (ch + 3))) ? 0xffffu : 0u;
    }

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

// Passe-bas du condensateur C10 du STF (port fidèle de LowPassFilter, [D] sound.c:453-466 @ f0736b24) :
// contrairement au PWMaliasFilter (front montant passe-tout), il lisse LES DEUX fronts
//  • montant  (x0 ≥ y0) : y = (3·(x0+x1) + 2·y0) / 8   (fc ≈ 7,6 kHz)
//  • descendant         : y = (   (x0+x1) + 6·y0) / 8   (fc ≈ 2,0 kHz)
// Supprime le contenu HF des ondes carrées qui, sinon, se replie au rééchantillonnage.
float YM2149::applyLpfStf250(float x0) {
    float y;
    if (x0 >= lpf250Y0_)
        y = (3.0f * (x0 + lpf250X1_) + 2.0f * lpf250Y0_) * 0.125f;
    else
        y = ((x0 + lpf250X1_) + 6.0f * lpf250Y0_) * 0.125f;
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
        // ([D] sound.c:1050-1058 @ f0736b24). Pour per=0 le LFSR est ainsi retiré à CHAQUE cycle
        // 250 kHz (2× plus vite que per=1), comme mesuré sur le vrai YM2149.
        freqDiv2_ ^= 1;
        if (freqDiv2_ == 0)
            noiseCnt_++;
        if (noiseCnt_ >= noisePer_) {
            noiseCnt_ = 0;
            noiseVal_ = rndCompute(rndLfsr_);
        }

        for (int ch = 0; ch < 3; ++ch) {
            toneCnt_[ch]++;
            if (toneCnt_[ch] >= tonePer_[ch]) {
                toneCnt_[ch] = 0;
                toneVal_[ch] ^= YM_SQUARE_UP;
            }
        }

        envCnt_++;
        if (envCnt_ >= envPer_) {
            envCnt_ = 0;
            envPos_++;
            if (envPos_ >= 3u * 32u)
                envPos_ -= 2u * 32u;
        }

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
    // les écritures à la frontière 250 kHz (Sound_Update avant Sound_WriteReg, [D] psg.c:346 @ f0736b24).
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
