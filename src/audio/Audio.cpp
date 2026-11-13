// =============================================================================
//  Audio.cpp — Implémentation miniaudio (l'unité qui DÉFINIT miniaudio).
//
//  Un seul .cpp doit poser MINIAUDIO_IMPLEMENTATION dans tout le projet ; il
//  fournit aussi l'API haut niveau ma_engine_* utilisée par DriveSound.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "util/Log.hpp"

#include "audio/Audio.hpp"
#include "audio/DriveSound.hpp"
#include "audio/Mt32Synth.hpp"
#include "audio/GmSynth.hpp"
#include "core/AudioMix.hpp"
#include "core/YM2149.hpp"
#include "core/DmaSound.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace {
// Callback du thread audio : pUserData pointe sur l'instance Audio, qui mixe le
// PSG et les bruits de lecteur dans le buffer de sortie.
void dataCallback(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frames) {
    static_cast<Audio*>(dev->pUserData)->render(static_cast<float*>(output), frames, dev->sampleRate);
}
} // namespace

Audio::Audio(YM2149& psg, DriveSound* drive, DmaSound* dma)
    : psg_(psg), drive_(drive), dma_(dma) {}

Audio::~Audio() { stop(); }

// CONSOMMATEUR (thread audio) : recopie l'anneau rempli par produceFrame. AMORÇAGE :
// tant que l'anneau n'a pas atteint le coussin cible (~85 ms), on sort du SILENCE sans
// drainer — sinon on jouerait un anneau quasi-vide en underrun permanent, où seules les
// transitoires (drums) passent et la musique continue se perd ; l'anneau mettrait des
// dizaines de secondes à se remplir. Après un underrun, on ne refait en revanche qu'un
// petit coussin (~20 ms, au moins un bloc backend) : ré-attendre 85 ms avalait à elle
// seule une percussion/note entière alors que le producteur avait déjà repris.
void Audio::render(float* out, uint32_t frames, uint32_t /*sampleRate*/) {
    const uint32_t need = frames * 2;                       // sortie stéréo entrelacée
    // Diagnostic : taille réelle des blocs demandés par le backend (3 premiers
    // callbacks). Un bloc PLUS GRAND que le coussin (primeSamples_) rendrait
    // l'underrun STRUCTUREL : chaque callback viderait l'anneau tout juste amorcé.
    static int dbg = 0;
    if (dbg < 3 && neost::log::verbose()) { std::fprintf(stderr, "[Audio] callback: %u frames (ring %zu floats, cushion %u frames)\n",
                                frames, ring_.available(), primeSamples_); ++dbg; }
    bool fadeIn = false;
    if (!primed_) {
        // Démarrage : latence cible complète. Reprise : petit coussin après un
        // micro-décrochage, coussin complet après une vraie coupure, jamais moins que la
        // demande courante (cf. neost::pacing::refillThreshold).
        const uint32_t threshold = neost::pacing::refillThreshold(
            played_, dryFrames_, frames, primeSamples_, recoverSamples_, longGapFrames_);
        if (ring_.available() < size_t(threshold) * 2) {
            std::fill(out, out + need, 0.0f);
            if (played_ && dryFrames_ < 0x7FFFFFFFu - frames) dryFrames_ += frames;
            return;
        }
        fadeIn  = played_;                                // reprise après coupure : fondu
        primed_ = true;                                   // coussin atteint → on démarre la lecture
        played_ = true;
        dryFrames_ = 0;
    }
    const size_t got = ring_.pull(out, need);
    if (fadeIn) neost::pacing::fadeStereo(out, frames, std::min<uint32_t>(frames, rate_ / 500), true);
    if (got < need) {                                       // underrun → on reconstitue le coussin
        // Fondu de sortie sur la fin de ce qui restait (~2 ms) : la coupure franche cliquait.
        const uint32_t validFrames = uint32_t(got / 2);
        neost::pacing::fadeStereo(out, validFrames, std::min<uint32_t>(validFrames, rate_ / 500), false);
        primed_ = false;
        dryFrames_ = frames - validFrames;                  // à sec depuis ce bloc
        underruns_.fetch_add(1, std::memory_order_relaxed); // diagnostic « son haché »
    }
}

// PRODUCTEUR (thread d'émulation, après runFrame) : génère le son de la trame et le
// pousse dans l'anneau. Le PSG est synthétisé en REJOUANT ses écritures horodatées
// (synthesizeFrame → modulations sous-buffer : digidrums, sync-buzzer), puis on mixe le
// son DMA STE, le volume/tonalité LMC1992 et les bruits de lecteur, avant clamp.
void Audio::produceFrame(int64_t frameCycles, int64_t frameEndCycle) {
    if (!started_) {                                  // pas de périphérique : on draine juste les événements
        psg_.clearEvents();
        if (dma_) dma_->clearEvents();
        // Le MT-32 était OUBLIÉ ici : Mt32Synth::byteAt empile dès que le synthé est
        // ouvert, mais sa file n'est vidée que par render(), qui est justement ce
        // qu'on saute. Sans périphérique audio, elle grossissait donc sans fin — et
        // au retour du son (cf. la reprise automatique ci-dessous) tout le retard
        // partait d'un coup dans une seule trame. Devenu atteignable par tous depuis
        // que le MT-32 est actif par défaut.
        if (mt32_) mt32_->clearEvents();
        if (gm_) gm_->clearEvents();               // même file, même dérive sinon
        return;
    }
    // Périphérique hôte PERDU (débranché, suspend/resume, backend brut) : le thread audio
    // meurt en silence — plus d'underrun signalé, anneau saturé, son mort jusqu'au
    // redémarrage. On surveille l'état du device et on retente un start ~1×/s.
    if (device_) {
        auto* dev = static_cast<ma_device*>(device_);
        if (ma_device_get_state(dev) == ma_device_state_stopped) {
            if (devLostFrames_++ == 0)
                std::fprintf(stderr, "[Audio] audio device stopped — auto-resuming…\n");
            if (devLostFrames_ % 50 == 0 && ma_device_start(dev) == MA_SUCCESS) {
                std::fprintf(stderr, "[Audio] audio device resumed\n");
                devLostFrames_ = 0;
            }
        } else devLostFrames_ = 0;
    }

    // Nombre d'échantillons pour cette trame = durée émulée × fréquence de sortie, avec
    // report fractionnaire (le débit moyen colle EXACTEMENT au temps émulé). Puis
    // ASSERVISSEMENT PROPORTIONNEL : on ajuste de quelques échantillons pour ramener l'anneau
    // vers le coussin cible (remplissage rapide à l'amorçage, recalage anti-dérive ensuite).
    // |adj| ≤ 8 sur ~960 → ≤ 0,8 % de variation de hauteur, inaudible. Sans toucher au
    // bridage vidéo 50 fps.
    // A28 : le calcul vit dans neost::pacing::AudioPacer (core/Pacing.hpp) — il
    // était recopié à l'identique ici, dans le frontend web et dans l'Android.
    // ring_.available() est en FLOATS (entrelacé) → ÷2 pour comparer aux FRAMES.
    const int n = pacer_.samplesForFrame(frameCycles, rate_,
                                         int(primeSamples_), int(ring_.available() / 2));
    // Anneau saturé : on draine PSG/DMA (un échantillon perdu ne laisse pas de trace).
    // Le MT-32, lui, est DÉLIBÉRÉMENT conservé : jeter un Note-Off laisserait une note
    // bloquée. C'est transitoire, les événements repartent à la trame suivante, et
    // kMaxPending (Mt32Synth) borne le pire cas.
    if (n <= 0) { psg_.clearEvents(); if (dma_) dma_->clearEvents(); return; }

    // Chaîne YM horodaté + DMA STE + LMC1992 : PARTAGÉE avec le headless et le
    // frontend web (core/AudioMix.cpp). Elle vivait ici en clair, recopiée dans les
    // deux autres — et la copie web avait dérivé sur l'ancienne API mono non
    // horodatée (samples inaudibles dans le navigateur).
    // Branche DMA gatée par le MODÈLE COURANT (cf. setDmaGate) : sur ST/Mega ST le
    // gain de rattrapage LMC ×2 (compensation du ½-YM STE, outScale_ 0.5) doublerait
    // un YM à pleine échelle (outScale_ 1.0) → clipping ; et l'état microwire d'une
    // session STE (reconfigure à chaud) colorerait le ST.
    const bool dmaOn = dma_ && (!dmaGate_ || dmaGate_());
    float* st = neost::mixEmulatedFrame(psg_, dma_, dmaOn,
                                        uint32_t(n), rate_, frameCycles, mixBuf_, gainYm_, gainDma_);
    if (!st) return;
    if (drive_) {                                          // (5) bruits lecteur (mono, hors LMC1992) → centrés
        if (int(driveScratch_.size()) < n) driveScratch_.assign(n, 0.0f);
        float* dv = driveScratch_.data();
        std::fill(dv, dv + n, 0.0f);
        drive_->mix(dv, uint32_t(n));
        const float g = gainDrive_;
        for (int i = 0; i < n; ++i) { st[2 * i] += dv[i] * g; st[2 * i + 1] += dv[i] * g; }
    }
    // (6) Roland MT-32/CM-32L (Munt) : événements MIDI datés de la trame → rendu → mix.
    if (mt32_ && mt32_->isOpen()) {
        mt32_->setGain(0.9f * gainMt32_);
        mt32_->render(st, n, frameEndCycle >= 0 ? frameEndCycle - frameCycles : 0, frameCycles);
    }
    // (7) Synthé GM intégré (TinySoundFont) : même contrat que le MT-32.
    if (gm_ && gm_->isOpen()) {
        gm_->setGain(0.9f * gainGm_);
        gm_->render(st, n, frameEndCycle >= 0 ? frameEndCycle - frameCycles : 0, frameCycles);
    }
    // Volume maître utilisateur (menu), appliqué en RAMPE linéaire sur le bloc depuis la
    // valeur effective du bloc précédent : un saut instantané (mute 1→0 en plein signal,
    // drag du slider) posait une marche d'amplitude par bloc de ~20 ms (clic / zipper).
    pacer_.applyMasterVolume(st, n, masterVol_);
    neost::pacing::AudioPacer::clampStereo(st, n);         // garde-fou anti-saturation

    ring_.push(st, size_t(2 * n));                        // → thread audio (render). Surplus jeté si plein.

    // Diagnostic : un underrun isolé peut arriver (chargement, déplacement de fenêtre) ;
    // RÉPÉTÉ, c'est que la boucle d'émulation ne tient pas la cadence des trames (son
    // haché + temps émulé ralenti). On signale avec la CADENCE OBSERVÉE de la boucle
    // (appels produceFrame/s réels) pour rendre le déficit lisible — limité ~1 msg/5 s.
    using dclock = std::chrono::steady_clock;
    static dclock::time_point t0 = dclock::now();
    static long calls = 0;
    ++calls;
    if (underrunMuteFrames_ > 0) --underrunMuteFrames_;
    const uint32_t u = underruns_.load(std::memory_order_relaxed);
    // Terminal sobre (GUI) : un décrochage ISOLÉ — bascule borne/bureau, Apply, chargement
    // d'une disquette, l'émulation s'est arrêtée un instant — n'est pas un défaut. On ne
    // prévient que s'il se RÉPÈTE (3 depuis le dernier message), signe que l'hôte ne tient
    // pas la cadence. En --verbose, chaque décrochage est signalé comme avant.
    const bool report = neost::log::verbose() ? (u != underrunsSeen_) : (u >= underrunsSeen_ + 3);
    if (report && underrunMuteFrames_ <= 0) {
        const double secs = std::chrono::duration<double>(dclock::now() - t0).count();
        // ⚠ « 50/60 » était FAUX en haute résolution : le mode mono tourne à 71,2 Hz,
        // et l'on y lisait « 71.4 real frames/s (expected ~50/60) » comme un emballement
        // alors que la boucle était À L'HEURE. Les trois cadences nominales sont donc
        // citées ; ce qui signale un vrai problème, c'est l'underrun lui-même.
        std::fprintf(stderr, "[Audio] ring underrun (total %u) — emulation loop: %.1f real frames/s "
                             "(nominal 50 PAL / 60 NTSC / 71 mono), ring %zu\n",
                     u, secs > 0 ? calls / secs : 0.0, ring_.available());
        underrunsSeen_ = u;
        underrunMuteFrames_ = 250;                        // ≈ 5 s à 50 trames/s
        t0 = dclock::now(); calls = 0;                    // fenêtre de mesure suivante
    }
}

bool Audio::start() {
    if (device_) return true;
    auto* dev = new ma_device();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;   // on synthétise en float
    cfg.playback.channels = 2;               // STÉRÉO : son DMA STE L/R + panoramique LMC1992
    cfg.sampleRate        = 48000;
    cfg.dataCallback      = dataCallback;
    cfg.pUserData         = this;            // le callback reçoit l'instance Audio

    if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] ma_device_init failed\n");
        delete dev;
        return false;
    }
    // TOUT ce que le callback render() lit (rate_, coussin primeSamples_, anneau,
    // amorçage primed_) doit être posé AVANT ma_device_start : le thread audio
    // démarre DANS cet appel et appellerait sinon render() sur un état à moitié
    // initialisé (data race). La fréquence réelle négociée est connue dès
    // ma_device_init.
    rate_    = dev->sampleRate ? dev->sampleRate : cfg.sampleRate;  // fréquence réelle négociée
    primeSamples_ = rate_ * latencyMs_ / 1000; // coussin ≈ latencyMs_ (défaut 85 ms) à la fréquence réelle
    recoverSamples_ = std::max(1u, rate_ / 50); // ≈20 ms ; render() le monte à la taille du callback si besoin
    longGapFrames_  = std::max(1u, rate_ / 20); // ≈50 ms à sec = vraie coupure (cf. refillThreshold)
    dryFrames_      = 0;
    ring_.clear();
    primed_      = false;                     // ré-amorçage propre
    played_      = false;
    pacer_.reset();
    if (ma_device_start(dev) != MA_SUCCESS) {
        std::fprintf(stderr, "[Audio] ma_device_start failed\n");
        ma_device_uninit(dev);
        delete dev;
        return false;
    }
    device_  = dev;
    started_ = true;
    if (neost::log::verbose())
        std::printf("[Audio] miniaudio started: %u Hz STEREO, latency ~%u ms (push model: timestamped PSG + DMA L/R + drive)\n",
                    rate_, primeSamples_ * 1000 / rate_);
    std::fflush(stdout);   // visible même si l'appli est tuée (diagnostic)
    return true;
}

void Audio::stop() {
    if (!device_) return;
    auto* dev = static_cast<ma_device*>(device_);
    ma_device_uninit(dev);
    delete dev;
    device_  = nullptr;
    started_ = false;
}
