// =============================================================================
//  MidiTimecode.hpp — MIDI Time Code (MTC) tiré de l'horloge MIDI du ST.
//
//  Le cas : le ST mène la séance (Cubase, Notator) et un Mac porte les instruments.
//  L'horloge MIDI ($F8, start/stop, Song Position Pointer) traverse déjà vers l'hôte,
//  mais un logiciel qui ne se cale QUE sur un code temporel (Logic Pro, entre autres,
//  ne suit pas l'horloge MIDI en esclave) n'en fait rien. Sur un vrai studio, c'était
//  le travail d'un convertisseur — l'Unitor de C-Lab ou le Midex de Steinberg lisaient
//  et écrivaient du SMPTE. Ces boîtiers ne sont PAS émulés (protocole non documenté,
//  cf. docs/EXTENSIONS.md) : ceci est un accessoire HÔTE, qui suit le transport du
//  ST et émet du MTC sur les sorties MIDI de l'hôte. Rien n'entre dans la machine.
//
//  ── Le format ──────────────────────────────────────────────────────────────
//  [B] MMA, « MIDI Time Code » (1987), repris dans MIDI 1.0 Detailed Specification
//      4.2 — lu à travers deux transcriptions concordantes (Wikipedia « MIDI
//      timecode » ; techref « MIDI Time Code », pic.hallikainen.org) : l'original
//      n'est pas dans extern/datasheets.
//    · Quarter Frame : F1 0nnn dddd. nnn = pièce 0-7 ; pièces 0/1 = images (bas, haut),
//      2/3 = secondes, 4/5 = minutes, 6/7 = heures ; la pièce 7 porte aussi le code de
//      cadence (bits 2-1) : 0 = 24, 1 = 25, 2 = 29,97 drop-frame, 3 = 30 i/s.
//    · Quatre Quarter Frames par image ; les huit pièces couvrent DEUX images, et le
//      temps codé est celui où la pièce 0 a été émise (le récepteur ajoute 2 images).
//    · Full Frame (après un saut) : F0 7F 7F 01 01 hh mm ss ff F7, hh = cadence<<5 | h.
//
//  ── Le transport du ST → un temps ─────────────────────────────────────────
//  Choix de conception (pas un comportement matériel) :
//    · Le temps de la chanson est le temps ÉMULÉ écoulé depuis le départ : exact
//      quel que soit le tempo et ses changements, puisqu'un code temporel est un
//      temps absolu.
//    · [B] MIDI 1.0 Detailed Specification 4.2, System Real Time (même réserve de
//      lecture) : après Start ($FA) ou Continue ($FB), l'esclave part à l'horloge
//      SUIVANTE. Le temps 0 est donc daté par le premier $F8 qui suit $FA.
//    · Un Song Position Pointer ($F2, en doubles-croches) se convertit en temps par
//      l'HISTORIQUE des doubles-croches déjà jouées ; au-delà de ce qui a été joué, par
//      extrapolation au dernier tempo mesuré (120 BPM faute de mesure). Exact pour un
//      retour en arrière ; approché pour un saut en avant avec changement de tempo.
//    · Un Full Frame est émis à chaque repositionnement (SPP, Start, Continue), puis
//      les Quarter Frames à partir de la frontière d'image suivante.
//    · Origine 00:00:00:00 : pas de décalage de départ (le 01:00:00:00 de la
//      post-production reste à régler côté récepteur).
//
//  Fonctions et classe PURES (aucun appel système) : neost-selftest les éprouve.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neost::midi::mtc {

// Valeurs de configuration (midi_mtc=) : 0 = coupé, 24, 25, 29 (= 29,97 drop-frame), 30.
inline bool validRate(int r) { return r == 0 || r == 24 || r == 25 || r == 29 || r == 30; }

// Code de cadence des pièces 7 et du Full Frame.
inline int rateCode(int r) {
    switch (r) { case 24: return 0; case 25: return 1; case 29: return 2; default: return 3; }
}

// Cadence RÉELLE (images par seconde) : 29,97 = 30000/1001.
inline double realFps(int r) {
    switch (r) { case 24: return 24.0; case 25: return 25.0; case 29: return 30000.0 / 1001.0;
                 default: return 30.0; }
}

struct Timecode {
    int h = 0, m = 0, s = 0, f = 0;
    bool operator==(const Timecode& o) const { return h == o.h && m == o.m && s == o.s && f == o.f; }
};

// Nombre d'images écoulées depuis 00:00:00:00 → étiquette. En 29,97 drop-frame, les
// numéros d'image 0 et 1 sont SAUTÉS au début de chaque minute, sauf les minutes
// multiples de 10 — 17 982 images par tranche de dix minutes, 1 798 par minute après
// la première (règle SMPTE 12M du drop-frame, telle qu'appliquée par le MTC).
inline Timecode fromFrames(int64_t n, int rate) {
    if (n < 0) n = 0;
    int fps = rate == 29 ? 30 : rate;
    if (fps <= 0) fps = 30;
    if (rate == 29) {
        const int64_t d = n / 17982, m = n % 17982;
        n += 18 * d + (m >= 2 ? 2 * ((m - 2) / 1798) : 0);
    }
    Timecode tc;
    tc.f = int(n % fps);
    tc.s = int((n / fps) % 60);
    tc.m = int((n / (int64_t(fps) * 60)) % 60);
    tc.h = int((n / (int64_t(fps) * 3600)) % 24);
    return tc;
}

// Octet de données d'un Quarter Frame (pièce 0-7).
inline uint8_t quarterFrameData(const Timecode& tc, int rate, int piece) {
    int nib = 0;
    switch (piece & 7) {
    case 0: nib = tc.f & 0x0F; break;
    case 1: nib = (tc.f >> 4) & 0x01; break;
    case 2: nib = tc.s & 0x0F; break;
    case 3: nib = (tc.s >> 4) & 0x03; break;
    case 4: nib = tc.m & 0x0F; break;
    case 5: nib = (tc.m >> 4) & 0x03; break;
    case 6: nib = tc.h & 0x0F; break;
    default: nib = ((tc.h >> 4) & 0x01) | (rateCode(rate) << 1); break;
    }
    return uint8_t(((piece & 7) << 4) | nib);
}

// Full Frame : 10 octets écrits dans `out`.
inline void fullFrame(const Timecode& tc, int rate, uint8_t out[10]) {
    const uint8_t msg[10] = {0xF0, 0x7F, 0x7F, 0x01, 0x01,
                             uint8_t((rateCode(rate) << 5) | (tc.h & 0x1F)),
                             uint8_t(tc.m), uint8_t(tc.s), uint8_t(tc.f), 0xF7};
    for (int i = 0; i < 10; ++i) out[i] = msg[i];
}

// Suit le flux MIDI OUT du ST (octet par octet, avec son cycle émulé) et produit le
// MTC correspondant. `emit(const uint8_t* msg, int len, int64_t cycle)` reçoit des
// messages COMPLETS datés en cycles — à l'appelant de les intercaler entre les
// messages du ST (ils ne doivent jamais couper un message en cours).
class Generator {
public:
    explicit Generator(double cpuHz) : hz_(cpuHz) {}

    void setRate(int r) {
        if (!validRate(r)) r = 0;
        if (r == rate_) return;
        rate_ = r;
        if (running_) startSchedule(lastSeenCycle_);   // nouvelle cadence, même temps
    }
    int rate() const { return rate_; }
    bool running() const { return running_; }

    // Un octet émis par le ST, au cycle `cycle` (croissant). Peut émettre un Full Frame.
    template <typename Emit>
    void observe(uint8_t b, int64_t cycle, const Emit& emit) {
        lastSeenCycle_ = cycle;
        if (b >= 0xF8) { realtime(b, cycle, emit); return; }
        // Song Position Pointer : F2 lsb msb. Tout autre statut l'interrompt ; les
        // données des autres messages ne le concernent pas.
        if (b & 0x80) { sppNeed_ = (b == 0xF2) ? 2 : 0; return; }
        if (sppNeed_ == 2) { sppLo_ = b; sppNeed_ = 1; return; }
        if (sppNeed_ == 1) {
            sppNeed_ = 0;
            // Un maître ne repositionne qu'à l'arrêt ; un SPP en pleine lecture
            // désynchroniserait la grille des Quarter Frames — ignoré.
            if (running_ || armed_) return;
            const int p = sppLo_ | (int(b) << 7);           // en doubles-croches
            nextClock_ = int64_t(p) * 6;
            locate(songTimeForClock(nextClock_), cycle, emit);
        }
    }

    // Émet les Quarter Frames dus jusqu'à `cycle` inclus.
    template <typename Emit>
    void advance(int64_t cycle, const Emit& emit) {
        if (!running_ || rate_ == 0) return;
        for (int guard = 0; guard < 4096; ++guard) {
            const int64_t due = qfCycle(qfIndex_);
            if (due > cycle) break;
            const int piece = int(qfIndex_ & 7);
            const Timecode tc = fromFrames(qfBaseFrame_ + 2 * (qfIndex_ >> 3), rate_);
            const uint8_t msg[2] = {0xF1, quarterFrameData(tc, rate_, piece)};
            emit(msg, 2, due);
            ++qfIndex_;
        }
    }

    void reset() {
        running_ = armed_ = false; clocks_ = -1; nextClock_ = 0; hist_.clear();
        songAtStop_ = 0.0; lastClockCycle_ = -1; clockPeriod_ = 0; sppNeed_ = 0;
    }

private:
    template <typename Emit>
    void realtime(uint8_t b, int64_t cycle, const Emit& emit) {
        switch (b) {
        case 0xFA:                                          // Start : position 0
            hist_.clear(); nextClock_ = 0; armed_ = true; running_ = false;
            locate(0.0, cycle, emit);
            break;
        case 0xFB:                                          // Continue
            armed_ = true; running_ = false;
            locate(songTimeForClock(nextClock_), cycle, emit);
            break;
        case 0xFC:                                          // Stop
            if (running_) {
                // La prochaine horloge sera la position suivante de la dernière reçue.
                nextClock_ = clocks_ + 1;
                songAtStop_ = songTimeForClock(nextClock_);
            }
            running_ = armed_ = false;
            break;
        case 0xF8:                                          // Timing Clock
            if (lastClockCycle_ >= 0 && cycle > lastClockCycle_)
                clockPeriod_ = cycle - lastClockCycle_;
            lastClockCycle_ = cycle;
            if (armed_) {
                // Le départ est À CETTE HORLOGE (cf. l'en-tête).
                armed_ = false; running_ = true;
                clocks_ = nextClock_;
                refSong_ = songTimeForClock(clocks_);
                refCycle_ = cycle;
                startSchedule(cycle);
            } else if (running_) {
                ++clocks_;
            } else {
                return;
            }
            if (clocks_ % 6 == 0) record(std::size_t(clocks_ / 6), songAt(cycle));
            break;
        default: break;
        }
    }

    double songAt(int64_t cycle) const { return refSong_ + double(cycle - refCycle_) / hz_; }

    void record(std::size_t sixteenth, double t) {
        if (sixteenth >= hist_.size()) hist_.resize(sixteenth + 1, NAN);
        hist_[sixteenth] = t;
    }

    double clockSeconds() const {
        // 120 BPM faute de mesure : 48 horloges par seconde.
        return clockPeriod_ > 0 ? double(clockPeriod_) / hz_ : 1.0 / 48.0;
    }

    // Temps de la chanson à l'horloge n : la double-croche connue la plus proche en
    // dessous, plus les horloges restantes au dernier tempo mesuré.
    double songTimeForClock(int64_t n) const {
        if (n <= 0) return 0.0;
        std::size_t h = std::size_t(n / 6);
        if (h >= hist_.size()) h = hist_.empty() ? 0 : hist_.size() - 1;
        while (h > 0 && std::isnan(hist_[h])) --h;
        const double base = (h < hist_.size() && !std::isnan(hist_[h])) ? hist_[h] : 0.0;
        const int64_t baseClock = (h < hist_.size() && !std::isnan(hist_[h])) ? int64_t(h) * 6 : 0;
        return base + double(n - baseClock) * clockSeconds();
    }

    template <typename Emit>
    void locate(double song, int64_t cycle, const Emit& emit) {
        songAtStop_ = song;
        if (rate_ == 0) return;
        uint8_t ff[10];
        fullFrame(fromFrames(int64_t(std::floor(song * realFps(rate_) + kFrameSlack)), rate_), rate_, ff);
        emit(ff, 10, cycle);
    }

    // Grille des Quarter Frames : la pièce 0 tombe sur la frontière d'image qui suit
    // le temps courant ; le Quarter Frame k est dû à (base + k/4) images.
    void startSchedule(int64_t cycle) {
        if (rate_ == 0) return;
        const double fps = realFps(rate_);
        const double now = running_ ? songAt(cycle) : songAtStop_;
        qfBaseFrame_ = int64_t(std::ceil(now * fps - kFrameSlack));
        qfIndex_ = 0;
    }

    int64_t qfCycle(int64_t k) const {
        const double t = (double(qfBaseFrame_) + double(k) / 4.0) / realFps(rate_);
        return refCycle_ + int64_t(std::llround((t - refSong_) * hz_));
    }

    // Tolérance d'arrondi, en images (40 µs à 25 i/s). Le temps est mesuré en cycles
    // ENTIERS : une période d'horloge tronquée au cycle fait tomber « 1 s » à
    // 0,999998 s, et un plancher strict étiquetterait 00:00:00:24 au lieu de 01:00.
    static constexpr double kFrameSlack = 1e-3;

    double hz_;
    int rate_ = 0;
    bool running_ = false, armed_ = false;
    int64_t clocks_ = -1;          // position (en horloges) de la dernière horloge reçue
    int64_t nextClock_ = 0;        // position de la prochaine horloge (Start, SPP, Stop)
    double refSong_ = 0.0;         // temps de la chanson au cycle refCycle_
    int64_t refCycle_ = 0;
    double songAtStop_ = 0.0;
    std::vector<double> hist_;     // temps de chaque double-croche jouée (NaN = inconnue)
    int64_t lastClockCycle_ = -1, clockPeriod_ = 0, lastSeenCycle_ = 0;
    int64_t qfIndex_ = 0, qfBaseFrame_ = 0;
    int sppNeed_ = 0;
    uint8_t sppLo_ = 0;
};

} // namespace neost::midi::mtc
