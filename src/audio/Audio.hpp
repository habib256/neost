// =============================================================================
//  Audio.hpp — Backend de sortie son (miniaudio) : YM2149 + bruits de lecteur.
//
//  miniaudio ouvre UN périphérique de lecture et appelle, depuis un thread
//  dédié, un callback qui : (1) synthétise le PSG, puis (2) additionne la sortie
//  de DriveSound (ronron/clics/seek/index). Un seul flux mono float sort donc —
//  c'est le point de mixage du YM2149 et des bruits mécaniques du lecteur.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "core/Pacing.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "audio/SampleRing.hpp"
#include "core/AudioMix.hpp"   // chaîne de mixage partagée (GUI / headless / web)

class YM2149;
class DriveSound;
class DmaSound;

class Mt32Synth;
class GmSynth;

class Audio {
public:
    // drive / dma peuvent être nuls — seul le PSG sort alors.
    explicit Audio(YM2149& psg, DriveSound* drive = nullptr, DmaSound* dma = nullptr);
    ~Audio();

    bool start();              // ouvre et démarre le périphérique
    void stop();
    bool ok() const { return started_; }
    // Fréquence RÉELLEMENT négociée avec le périphérique (cf. rate_) : elle peut
    // différer des 48 kHz demandés. Tout ce qui synthétise à part — MT-32, bruits de
    // lecteur — doit s'aligner dessus, sinon eux seuls sortent désaccordés.
    uint32_t rate() const { return rate_; }

    // Coussin d'amorçage visé, en MILLISECONDES (défaut 85). À appeler AVANT start() :
    // c'est start() qui convertit en échantillons une fois la fréquence réelle négociée.
    // Monter cette valeur absorbe la gigue d'ordonnancement des machines lentes (borne
    // Raspberry Pi) au prix d'une latence perçue ; la descendre rapproche le son de
    // l'image mais rend l'underrun probable dès le moindre à-coup. Borné à [20, 250] ms :
    // au-delà, le coussin s'approcherait de la capacité de l'anneau (341 ms à 48 kHz
    // stéréo) et le producteur jetterait des échantillons à chaque trame.
    void setLatencyMs(uint32_t ms) { latencyMs_ = ms < 20 ? 20 : (ms > 250 ? 250 : ms); }
    uint32_t latencyMs() const { return latencyMs_; }

    // Consommateur (thread audio miniaudio) : recopie l'anneau dans `out`, silence si
    // underrun. Ne synthétise plus rien — toute la génération est faite en amont par
    // produceFrame sur le thread d'émulation (modèle « push » de la Phase C).
    void render(float* out, uint32_t frames, uint32_t sampleRate);

    // Producteur (thread d'émulation) : génère le son d'UNE trame (PSG horodaté + son DMA
    // + LMC1992 + bruits lecteur, clampé) et le pousse dans l'anneau. `frameCycles` = durée
    // de la trame en cycles CPU (pour dater les écritures PSG et calibrer le nombre
    // d'échantillons). À appeler APRÈS Machine::runFrame.
    // `frameEndCycle` (cycle CPU à la fin de la trame) date les événements MIDI du MT-32.
    void produceFrame(int64_t frameCycles, int64_t frameEndCycle = -1);

    // Synthé MT-32/CM-32L (Munt) mixé dans la sortie — nul = absent (cf. audio/Mt32Synth).
    void setMt32(Mt32Synth* s) { mt32_ = s; }
    // Synthé GM intégré (TinySoundFont) mixé dans la sortie — nul = absent (cf. audio/GmSynth).
    void setGm(GmSynth* s) { gm_ = s; }

    // Mixeur utilisateur (page Sound) : gains par source, 0..2 (1 = neutre). YM et DMA
    // s'appliquent en amont du LMC1992 ; lecteur, MT-32 et GM à leur entrée dans le mix.
    void setMixGains(float ym, float dma, float drive, float mt32, float gm = 1.0f) {
        gainYm_ = ym; gainDma_ = dma; gainDrive_ = drive; gainMt32_ = mt32; gainGm_ = gm;
    }

    // Volume maître de la SORTIE (0..1) — réglage utilisateur (barre de menu, persisté
    // dans neost.cfg), appliqué au mix final avant clamp. Indépendant du LMC1992 ÉMULÉ
    // (qui, lui, appartient à la machine). Lu/écrit sur le seul thread d'émulation.
    void  setMasterVolume(float v) { masterVol_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    float masterVolume() const { return masterVol_; }

    // Prédicat « la machine courante a le son DMA » (évalué à CHAQUE trame — suit les
    // reconfigure à chaud) : gate la branche DMA/LMC1992 de produceFrame. Sans gate,
    // le gain de rattrapage LMC (×2) s'appliquait au YM des machines ST. Nul = legacy
    // (branche DMA active dès que dma_ existe).
    void setDmaGate(std::function<bool()> f) { dmaGate_ = std::move(f); }

private:
    YM2149&     psg_;
    DriveSound* drive_   = nullptr;
    DmaSound*   dma_     = nullptr;
    Mt32Synth*  mt32_    = nullptr;
    GmSynth*    gm_      = nullptr;
    float gainYm_ = 1.0f, gainDma_ = 1.0f, gainDrive_ = 1.0f, gainMt32_ = 1.0f, gainGm_ = 1.0f;   // cf. setMixGains
    bool        started_ = false;
    void*       device_  = nullptr;   // ma_device opaque (évite d'inclure miniaudio ici)

    // --- Modèle « push » (Phase C) : anneau émulation → audio --------------------
    // L'anneau stocke des échantillons ENTRELACÉS L/R (2 floats par frame). `primeSamples_`,
    // `n` et le report du pacer sont comptés en FRAMES (par canal) ; ring_.available()/space()
    // comptent en floats → toujours convertir (×2 / ÷2) au passage de frontière.
    SampleRing         ring_{32768};     // SPSC entrelacé : ~340 ms de marge à 48 kHz stéréo
    neost::FrameMixBuffers mixBuf_;      // tampons de la chaîne partagée (YM mono + sortie L/R)
    std::vector<float> driveScratch_;    // bruits lecteur mono intermédiaires (frames)
    uint32_t           rate_ = 48000;    // fréquence de sortie réelle du périphérique (frames/s)
    float              masterVol_ = 1.0f; // volume maître utilisateur (cf. setMasterVolume)
    // A28 : report fractionnaire + servo + rampe anti-clic vivent désormais dans
    // neost::pacing::AudioPacer (core/Pacing.hpp), partagé avec le web et l'Android.
    neost::pacing::AudioPacer pacer_;
    std::function<bool()> dmaGate_;      // cf. setDmaGate (nul = branche DMA dès que dma_ existe)
    int                devLostFrames_ = 0; // trames depuis la détection « périphérique arrêté »
    uint32_t           latencyMs_ = 85;      // coussin visé en ms (cf. setLatencyMs) — lu par start()
    uint32_t           primeSamples_ = 4000; // coussin cible (≈ latence visée, ~85 ms) — amorçage + asservissement
    uint32_t           recoverSamples_ = 960;// ré-amorce courte après underrun (≈20 ms, au moins 1 callback)
    bool               primed_ = false;  // (thread audio) : l'anneau a-t-il atteint le coussin ? sinon → silence
    bool               played_ = false;  // premier amorçage terminé ; ensuite recoverSamples_ suffit
    uint32_t           dryFrames_ = 0;   // (thread audio) frames jouées à sec depuis l'underrun
    uint32_t           longGapFrames_ = 2400; // à sec au-delà (≈50 ms) → reprise sur coussin complet

    // Diagnostic « son haché » : nombre d'underruns de l'anneau (le thread audio a
    // voulu drainer plus que produit → courte ré-amorce ~20 ms avant la reprise).
    // Incrémenté par render() (thread audio), surveillé par produceFrame() (thread
    // émulation) qui avertit sur stderr — un underrun RÉPÉTÉ signifie que la boucle
    // d'émulation ne tient pas la cadence des trames (cf. bridage dans main.cpp).
    std::atomic<uint32_t> underruns_{0};
    uint32_t              underrunsSeen_ = 0;   // (thread émulation) dernier total signalé
    int64_t               underrunMuteFrames_ = 0; // anti-spam : trames restantes avant re-signalement
};
