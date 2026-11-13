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
#include <atomic>
#include <cstdint>
#include <cstring>

namespace neost::pacing {

// Horloge CPU/bus du ST (PAL), VALEUR PAR DÉFAUT. Le quartz réellement en service se
// lit par cpuHz()/cpuHzInt() (plus bas) : il peut être choisi en option (setQuartz).
// LA référence : tout ce qui convertit des cycles émulés en temps réel passe par ici.
// [D] Hatari clocks_timings.c @ f0736b24 : quartz 32 084 988 Hz / 4 = 8 021 247 Hz.
// [A] Relevés HWTST de tiers (atari-forum t=24862, 2013-2016), horloge CPU mesurée contre
//     le quartz MFP de 2,4576 MHz : STE et Mega STE PAL 8 020 7xx-8 021 2xx Hz (Jungsi,
//     Cyprian, AtariZoll) — concordant. Les STF PAL varient : Troed et exxos ont des STFM à
//     32,084988 MHz (8,02 MHz), mais un Mega ST4, un 520 STF FR et un 1040 STF tournent à
//     8,0097-8,0101 MHz (quartz 32,0424 MHz) et un 1040 STF UK à 8,0064 MHz. 8,02 MHz est
//     donc un cas réel du ST ; c'est le DÉFAUT. Les autres quartz (et l'horloge NTSC du
//     Mega STE, mesurée à 8,0540 MHz) se choisissent en option : cf. Quartz ci-dessous.
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

// -----------------------------------------------------------------------------
//  Quartz maître choisi EN OPTION (TODO « Horloges NTSC et quartz STF à 32,0424 MHz »).
//
//  Le ST tire CPU, bus, GLUE/MMU, Shifter, blitter et (sur ST) FDC et YM du MÊME quartz
//  maître, divisé par 4 pour le CPU. Tout ce qui se compte en cycles émulés suit donc le
//  quartz sans rien changer : géométrie vidéo, DMA disque, accès bus. Ce qui en DÉPEND
//  vraiment, ce sont les horloges INDÉPENDANTES converties en cycles : quartz du MFP
//  (2,4576 MHz : timers et USART), oscillateur SCLK du STE (son DMA, FDC), quartz de
//  l'IKBD, RTxC du SCC, ISP1160, et le temps réel de l'hôte (cadence des trames, audio,
//  MIDI). Ceux-là lisent cpuHzInt()/busHzExact() ; aucun autre point du code.
//
//  [A] Relevés HWTST de tiers (atari-forum t=24862, cf. kCpuHz) : Mega STE NTSC à
//      8,0540 MHz ; Mega ST4, 520 STF FR et 1040 STF à 8,0097-8,0101 MHz ; STE/Mega STE PAL
//      et STFM de Troed et exxos à 8,021 MHz.
//  [D] Hatari clocks_timings.c:137-151 @ f0736b24 : quartz 32 084 988 (PAL), 32 042 400
//      (« STF NTSC ») et 32 215 905 Hz (« STE NTSC », 9 × 3,579545 MHz). Les relevés A
//      tombent sur ces trois quartz à la tolérance près (le relevé est fait contre le quartz
//      du MFP, lui-même à quelques dizaines de ppm) — sauf le 1040 STF UK à 8,0064 MHz, quartz
//      inconnu, non modélisé.
//
//  Le DÉFAUT reste le PAL historique : aucune référence ne bouge. Il garde son écart
//  interne d'1 Hz (cpuHz 8 021 248 pour le rapport MFP exact 31 333/9 600, busHzExact
//  8 021 247 = 32 084 988/4 pour FDC STE, rotation disque et grille 250 kHz du YM), tel
//  qu'il existait avant l'option ; les autres quartz n'ont qu'une valeur, mclk/4.
//
//  Process-global : une seule Machine par processus (GUI, headless, web, Android). La
//  valeur change hors émulation (Machine::setQuartz, avant le boot ou à la reconfiguration)
//  ; atomique pour qu'un fil audio ne lise jamais une valeur déchirée.
// -----------------------------------------------------------------------------
enum class Quartz : uint8_t { Pal = 0, Q32042 = 1, Q32216 = 2 };
inline constexpr int kQuartzCount = 3;

struct QuartzInfo {
    const char* name;     // nom CLI / neost.cfg (en anglais : interface)
    int64_t mclkHz;       // quartz maître
    int64_t cpuHz;        // horloge CPU pour MFP, temps réel, IKBD, SCC, USB
    int64_t busHz;        // horloge bus exacte (mclk/4) : FDC STE, rotation, YM 250 kHz
    const char* label;    // libellé GUI
};
inline const QuartzInfo& quartzInfo(Quartz q) {
    static const QuartzInfo k[kQuartzCount] = {
        {"pal",   32084988, kCpuHzInt, 8021247, "32.084988 MHz (PAL, default)"},
        {"32042", 32042400, 8010600,   8010600, "32.0424 MHz (STF NTSC, some PAL STF/Mega ST)"},
        {"32216", 32215905, 8053976,   8053976, "32.215905 MHz (STE/Mega STE NTSC)"},
    };
    const int i = int(q);
    return k[(i >= 0 && i < kQuartzCount) ? i : 0];
}
// « pal », « 32042 », « 32216 » ; alias « ntsc-stf », « ntsc-ste ». Faux si inconnu.
inline bool parseQuartz(const char* s, Quartz& out) {
    if (!s) return false;
    for (int i = 0; i < kQuartzCount; ++i)
        if (!std::strcmp(s, quartzInfo(Quartz(i)).name)) { out = Quartz(i); return true; }
    if (!std::strcmp(s, "ntsc-stf")) { out = Quartz::Q32042; return true; }
    if (!std::strcmp(s, "ntsc-ste")) { out = Quartz::Q32216; return true; }
    return false;
}

namespace detail {
inline std::atomic<int64_t> gCpuHz{kCpuHzInt};
inline std::atomic<int64_t> gBusHz{8021247};
inline std::atomic<uint8_t> gQuartz{0};
}
inline int64_t cpuHzInt()   { return detail::gCpuHz.load(std::memory_order_relaxed); }
inline double  cpuHz()      { return double(cpuHzInt()); }
inline int64_t busHzExact() { return detail::gBusHz.load(std::memory_order_relaxed); }
inline Quartz  quartz()     { return Quartz(detail::gQuartz.load(std::memory_order_relaxed)); }
// Bas niveau : ne touche QUE les valeurs globales. Une machine en service passe par
// Machine::setQuartz, qui recale aussi le rapport du MFP.
inline void setQuartzGlobal(Quartz q) {
    const QuartzInfo& i = quartzInfo(q);
    detail::gCpuHz.store(i.cpuHz, std::memory_order_relaxed);
    detail::gBusHz.store(i.busHz, std::memory_order_relaxed);
    detail::gQuartz.store(uint8_t(q), std::memory_order_relaxed);
}

// Durée RÉELLE d'une trame émulée. La cadence suit la GÉOMÉTRIE, jamais un 20 ms
// figé : 313×512 ≈ 19,97 ms (PAL 50 Hz), 263×508 ≈ 16,65 ms (NTSC 60 Hz),
// 501×224 ≈ 13,99 ms (mono 71 Hz). Un bridage fixe à 20 ms ralentissait un écran
// 60 Hz de ~17 % — musique traînante et anneau audio affamé. Et elle suit le QUARTZ :
// à 8,054 MHz la même trame dure 0,41 % de moins.
inline double  frameMillis(int64_t frameCycles) { return double(frameCycles) * 1000.0 / cpuHz(); }
inline int64_t frameNanos (int64_t frameCycles) {
    return static_cast<int64_t>(double(frameCycles) * 1e9 / cpuHz());
}

// -----------------------------------------------------------------------------
//  Reprise de la lecture après un underrun : quel remplissage attendre (en FRAMES).
// -----------------------------------------------------------------------------
// Avant la première lecture : le coussin complet `prime`. Après un MICRO-décrochage
// (l'anneau à sec moins de `longGap`) : un petit coussin `recover`, pour ne pas avaler
// une note de plus. Après une VRAIE coupure (à sec ≥ `longGap` : l'émulation s'est
// arrêtée — bascule borne/bureau, Apply, chargement), le coussin COMPLET. Jusqu'au
// 2026-11-13 on reprenait toujours sur ~20 ms ; l'asservissement (≤ 0,8 % par trame)
// mettait ~8 s à regarnir les 85 ms, et pendant ce temps le moindre à-coup de l'hôte
// recoupait le son — les « ring underrun » s'enchaînaient dans la trace de l'auteur.
// L'attente ajoutée (≤ 85 ms) se fond dans une coupure déjà audible. Jamais moins
// qu'un bloc du backend `block`, sinon le callback suivant recouperait aussitôt.
inline uint32_t refillThreshold(bool played, uint32_t dryFrames, uint32_t block,
                                uint32_t prime, uint32_t recover, uint32_t longGap) {
    if (!played) return prime;
    const uint32_t want = dryFrames >= longGap ? prime : recover;
    return want > block ? want : block;
}

// Fondu linéaire de `n` frames stéréo entrelacées (n ≤ frames du bloc) : `up` = entrée
// (0 → 1) au début de `buf`, sinon sortie (1 → 0) sur les `n` DERNIÈRES frames de `buf`
// (`frames` frames valides). Évite le clic d'une coupure ou d'une reprise franche.
inline void fadeStereo(float* buf, uint32_t frames, uint32_t n, bool up) {
    if (n > frames) n = frames;
    if (n == 0) return;
    for (uint32_t i = 0; i < n; ++i) {
        const float g = up ? float(i) / float(n) : float(n - i) / float(n + 1);
        const uint32_t f = up ? i : frames - n + i;
        buf[2 * f] *= g; buf[2 * f + 1] *= g;
    }
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
        carry_ += double(frameCycles) * double(rate) / cpuHz();
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
    // compte la trame livrée. L'hôte borne lui-même à kMaxCatchUp trames par tour
    // (la page : neostParkBudget).
    // Le retard au-delà de kMaxCatchUp trames est abandonné, mais on garde les
    // kMaxCatchUp trames que runDue jouerait encore dans ce tour : recaler sur
    // `nowMs` AVANT de jouer ne laissait qu'une trame par tour dès que l'écran tombait
    // sous le cinquième de la fréquence de la machine (rAF à 10 Hz : 10 trames/s au
    // lieu de 40, mesuré dans Chrome, tools/check_web_jspi.mjs).
    bool due(double nowMs, int64_t frameCycles) {
        if (!armed_) resync(nowMs);
        const double cap = kMaxCatchUp * frameMillis(frameCycles);
        if (nowMs - nextMs_ > cap) nextMs_ = nowMs - cap;
        return nowMs >= nextMs_;
    }
    void advance(int64_t frameCycles) { nextMs_ += frameMillis(frameCycles); }

private:
    double nextMs_ = 0.0;
    bool   armed_  = false;
};

}  // namespace neost::pacing
