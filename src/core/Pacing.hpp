// =============================================================================
//  Pacing.hpp — cadence des trames et asservissement audio, PARTAGÉS (chantier A28).
//
//  POURQUOI. Le servo audio (filtre proportionnel /256, clamp ±8, rampe anti-clic)
//  existait en TROIS copies — GUI natif (audio/Audio.cpp), web (main_web.cpp),
//  Android (main_android.cpp) — et la boucle de rattrapage de cadence en deux. La
//  constante `kCpuHz` était déclarée QUATRE fois. Le précédent est connu : la
//  chaîne de mixage vivait elle aussi en clair dans le GUI, recopiée ailleurs, et
//  la copie web avait DÉRIVÉ sur l'ancienne API mono non horodatée — des samples
//  inaudibles dans le navigateur, sans que personne ne le voie. `AudioMix` a réglé
//  ce cas-là ; ceci règle les deux qui restaient.
//
//  Ce fichier ne contient QUE de la logique pure : pas de miniaudio, pas de SDL,
//  pas d'Emscripten. Il est donc testable sans machine ni ROM — cf. la table de
//  vérité « cadence & servo audio » de tests/selftest_logic.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <algorithm>
#include <cstdint>

namespace neost::pacing {

// Horloge CPU/bus du ST (PAL). LA référence : tout ce qui convertit des cycles
// émulés en temps réel passe par ici.
// [D] Hatari clocks_timings.c @ f0736b24 : quartz 32 084 988 Hz / 4 = 8 021 247 Hz.
// [A] Relevés HWTST de tiers (atari-forum t=24862, 2013-2016), horloge CPU mesurée contre
//     le quartz MFP de 2,4576 MHz : STE et Mega STE PAL 8 020 7xx-8 021 2xx Hz (Jungsi,
//     Cyprian, AtariZoll) — concordant. Les STF PAL varient : Troed et exxos ont des STFM à
//     32,084988 MHz (8,02 MHz), mais un Mega ST4, un 520 STF FR et un 1040 STF tournent à
//     8,0097-8,0101 MHz (quartz 32,0424 MHz) et un 1040 STF UK à 8,0064 MHz. 8,02 MHz est
//     donc un cas réel du ST ; les autres quartz ne sont pas modélisés, ni l'horloge NTSC
//     (Mega STE NTSC mesuré à 8,0540 MHz) : NeoST, comme Hatari, garde l'horloge PAL.
inline constexpr double kCpuHz = 8021248.0;
// Même horloge, en ENTIER : les puces qui comptent des cycles émulés (RTC,
// son DMA, UltraSatan) travaillent en int64 et n'ont que faire d'un double.
// RÈGLE : c'est le seul littéral 8021248 du code C++ — tout le reste pointe ici.
// Elle a déjà cédé une fois sans que personne ne le voie (Mfp.cpp calculait son
// débit série sur un `int64_t(8021248)` recopié, et l'en-tête du dump MIDI du
// headless ANNONÇAIT une horloge en dur — recomptés et corrigés le 2026-08-30).
// Une seule exception assumée : tools/midi_compare.py, qui est du Python et ne
// peut pas inclure ceci — il LIT donc « cpu_hz= » dans l'en-tête du dump.
inline constexpr int64_t kCpuHzInt = 8021248;

// Durée RÉELLE d'une trame émulée. La cadence suit la GÉOMÉTRIE, jamais un 20 ms
// figé : 313×512 ≈ 19,97 ms (PAL 50 Hz), 263×508 ≈ 16,65 ms (NTSC 60 Hz),
// 501×224 ≈ 13,99 ms (mono 71 Hz). Un bridage fixe à 20 ms ralentissait un écran
// 60 Hz de ~17 % — musique traînante et anneau audio affamé.
inline double  frameMillis(int64_t frameCycles) { return double(frameCycles) * 1000.0 / kCpuHz; }
inline int64_t frameNanos (int64_t frameCycles) {
    return static_cast<int64_t>(double(frameCycles) * 1e9 / kCpuHz);
}

// -----------------------------------------------------------------------------
//  Servo audio : combien d'échantillons produire pour la trame qui vient.
// -----------------------------------------------------------------------------
class AudioPacer {
public:
    // Correction maximale par trame, en échantillons. ≤ 8 sur ~960 → ≤ 0,8 % de
    // variation de hauteur : inaudible, mais suffisant pour absorber la dérive
    // entre l'horloge du périphérique audio et celle de la machine (elles ne sont
    // PAS les mêmes, et rien ne les synchronise).
    static constexpr int kMaxAdjust = 8;
    // Diviseur du terme proportionnel. Volontairement mou : le servo corrige une
    // DÉRIVE lente, il ne doit pas réagir au bruit de mesure de la file.
    static constexpr int kServoDivisor = 256;

    // `cushionFrames` = remplissage VISÉ de la file de sortie, `queuedFrames` =
    // remplissage observé — les deux en TRAMES audio (pas en floats : l'anneau du
    // GUI compte des floats entrelacés, d'où le ÷2 chez l'appelant ; s'être trompé
    // là aurait doublé le gain du servo).
    int samplesForFrame(int64_t frameCycles, uint32_t rate,
                        int cushionFrames, int queuedFrames) {
        carry_ += double(frameCycles) * double(rate) / kCpuHz;
        int n = int(carry_);
        carry_ -= n;                     // report FRACTIONNAIRE : le débit moyen
                                         // colle exactement au temps émulé
        int adj = (cushionFrames - queuedFrames) / kServoDivisor;
        adj = std::max(-kMaxAdjust, std::min(kMaxAdjust, adj));
        return n + adj;
    }

    // Volume maître en RAMPE linéaire sur le bloc, depuis la valeur effective du
    // bloc précédent. Un saut instantané (mute 1→0 en plein signal, curseur qu'on
    // glisse) posait une marche d'amplitude par bloc de ~20 ms : clic audible, et
    // « zipper » au glissement. `stereo` est ENTRELACÉ (2×n floats).
    void applyMasterVolume(float* stereo, int n, float masterVol) {
        if (n <= 0) return;
        if (masterVol == volSmooth_ && masterVol == 1.0f) return;   // rien à faire
        const float v0 = volSmooth_, vt = masterVol;
        for (int i = 0; i < n; ++i) {
            const float v = v0 + (vt - v0) * (float(i + 1) / float(n));
            stereo[2 * i] *= v; stereo[2 * i + 1] *= v;
        }
        volSmooth_ = vt;
    }

    // Garde-fou anti-saturation, en sortie de chaîne.
    static void clampStereo(float* stereo, int n) {
        for (int i = 0; i < 2 * n; ++i)
            stereo[i] = std::max(-1.0f, std::min(1.0f, stereo[i]));
    }

    void reset() { carry_ = 0.0; volSmooth_ = 1.0f; }
    double carry() const { return carry_; }
    float  volumeSmoothed() const { return volSmooth_; }

private:
    double carry_     = 0.0;
    float  volSmooth_ = 1.0f;
};

// -----------------------------------------------------------------------------
//  Cadence des trames : combien de trames émulées sont DUES à l'instant présent.
// -----------------------------------------------------------------------------
class FramePacer {
public:
    // Plafond de rattrapage. Sans lui, un onglet remis au premier plan (rAF
    // suspendu) ou une application Android réveillée déclencherait une spirale :
    // des centaines de trames dues d'un coup, donc une boucle qui ne rend jamais
    // la main.
    static constexpr int kMaxCatchUp = 4;

    // Repart de `nowMs` : à appeler quand la machine a été EN PAUSE (menu borne
    // ouvert, application en arrière-plan). Sans ça la reprise croit devoir
    // rattraper tout le temps passé en pause.
    void resync(double nowMs) { nextMs_ = nowMs; armed_ = true; }

    // Joue les trames dues. `runOne` exécute UNE trame et renvoie son nombre de
    // cycles (la géométrie peut changer d'une trame à l'autre : 50↔60 Hz, mono).
    // Renvoie le nombre de trames jouées — 0 = l'écran va plus vite que la machine,
    // l'appelant garde son image précédente.
    template <class RunOneFrame>
    int runDue(double nowMs, RunOneFrame&& runOne) {
        if (!armed_) resync(nowMs);                 // 1re trame : on part d'ici
        int ran = 0;
        while (nowMs >= nextMs_ && ran < kMaxCatchUp) {
            nextMs_ += frameMillis(runOne());
            ++ran;
        }
        // Plafond atteint et toujours en retard : c'est une longue pause, pas un
        // retard rattrapable. On abandonne le retard au lieu de le traîner.
        if (ran == kMaxCatchUp && nowMs > nextMs_) nextMs_ = nowMs;
        return ran;
    }

    double nextDueMs() const { return nextMs_; }

    // Cadence par créneaux, pour un hôte qui ne peut pas appeler runDue : le web
    // pendant un blit HOG, où la trame est livrée par la pile suspendue (JSPI) et
    // reprise par la page. due() dit si la trame suivante est échue (un retard de
    // plus de kMaxCatchUp trames est abandonné, comme dans runDue) ; advance()
    // compte la trame livrée.
    bool due(double nowMs, int64_t frameCycles) {
        if (!armed_) resync(nowMs);
        if (nowMs - nextMs_ > kMaxCatchUp * frameMillis(frameCycles)) nextMs_ = nowMs;
        return nowMs >= nextMs_;
    }
    void advance(int64_t frameCycles) { nextMs_ += frameMillis(frameCycles); }

private:
    double nextMs_ = 0.0;
    bool   armed_  = false;
};

}  // namespace neost::pacing
