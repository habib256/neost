// =============================================================================
//  DmaSound.hpp — Son DMA de l'Atari STE ($FF8900-$FF8925).
//
//  Le STE ajoute un canal de son NUMÉRIQUE : un DMA lit des échantillons signés
//  8 bits en RAM, d'une adresse de début à une adresse de fin, à une fréquence
//  choisie (6.25 / 12.5 / 25 / 50 kHz), en mono ou stéréo entrelacé. À la fin de
//  la trame : arrêt, ou rebouclage si le bit « repeat » est posé.
//
//  Modèle (comme le YM2149) : la lecture des échantillons se fait sur le thread
//  audio (mix), par rééchantillonnage bloquant-zéro vers la fréquence de sortie.
//  Le compteur d'adresse courant est exposé (lecture CPU). Le mixage final
//  (YM2149 + ce canal) passe par AudioMix sur tous les frontends livrés.
//
//  Réf. : Hatari dmaSnd.c, doc registres STE. (c) 2026 VERHILLE Arnaud — NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "core/StateArchive.hpp"

class Bus;
class Mfp;
class Scheduler;

class DmaSound {
public:
    explicit DmaSound(Bus& bus) : bus_(bus) {}

    // L'ordonnanceur fournit l'horloge émulée (liveNow) qui date la consommation
    // DAC et donc la fin de trame (→ event-count Timer A du MFP), sur le thread
    // d'émulation. Branché par Machine.
    void setScheduler(Scheduler* s) { sched_ = s; }
    // Câble le MFP et lui SIGNALE la présence du son DMA (modèle STE/Mega STE) :
    // le MFP n'XOR la ligne XSINT dans GPIP7 que si ce flag est posé (cf. Hatari
    // MFP_Main_Compute_GPIP7 : XOR réservé à Config_IsMachineSTE()/TT()).
    void setMfp(Mfp* m);

    // Tic HBL (thread émulation) — port de DmaSnd_STE_HBL_Update (dmaSnd.c:727-741) :
    // remplit la FIFO 8 octets (fetch DMA au faisceau), consomme au rythme DAC (les
    // octets tirés sont CAPTURÉS pour le rendu audio), re-remplit. C'est ICI que la
    // fin de trame est détectée (au FETCH, en avance sur la lecture DAC, comme le
    // vrai matériel) → XSINT/Timer A. Appelé par Machine::onHbl (STE/Mega STE).
    void onHbl();

    // MMIO $FF8900-$FF8925 (octets ; le 68000 y fait des mots big-endian).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // API mono de compatibilité (`out` contient le YM). Aucun frontend livré ne
    // l'appelle : GUI/headless/WASM/Android passent par AudioMix + mixStereo.
    void    mix(float* out, uint32_t frames, uint32_t sampleRate);

    // Version STÉRÉO (chemin natif GUI) : écrit `st` ENTRELACÉ L/R (2×frames floats)
    // à partir de la voie YM mono `ym` (frames échantillons). Le DMA STE porte une
    // vraie image L/R (octets pairs/impairs en stéréo) → la séparation est PRÉSERVÉE
    // jusqu'à la sortie au lieu d'être moyennée. Applique le routage de mixage LMC1992
    // (mixing==1 → YM+DMA ; sinon DMA seul écrase YM) comme `mix`. DMA à l'arrêt → YM
    // recopié sur les deux canaux (choix NeoST : pas de mute du YM hors lecture DMA).
    //
    // Modèle « push » horodaté (comme le YM2149) : si une horloge frame-relative est
    // câblée (setCycleClock), les transitions PLAY/STOP de la trame sont rejouées à leur
    // cycle EXACT — un bruitage one-shot très court (démarré ET fini dans la même trame
    // émulée) n'est plus avalé, et la queue d'un sample n'est plus écrêtée par l'effacement
    // CPU du bit PLAY. `frameCycles` = durée de la trame en cycles CPU (mappe cycle→échantillon).
    // `dmaGain` : fader utilisateur sur la SEULE voie DMA (1 = neutre) ; le YM et le bit de
    // mixage LMC (YM+DMA / DMA seul) gardent leur sémantique matérielle.
    void    mixStereo(float* st, const float* ym, uint32_t frames, uint32_t sampleRate, int64_t frameCycles, float dmaGain = 1.0f);

    // Branche l'horloge frame-relative (cycles CPU depuis le début de la trame), posée
    // par le frontend audio « push » (comme YM2149::setCycleClock). Tant qu'elle n'est
    // pas posée, AUCUN événement n'est enregistré (pas de fuite mémoire) et mixStereo
    // retombe sur le rendu live (état CPU courant).
    // ⚠ Les QUATRE frontends livrés la posent (GUI, headless, WASM, Android) — le
    // « (headless/WASM) » d'avant était faux, cf. la même note sur YM2149::setCycleClock.
    void    setCycleClock(std::function<int64_t()> c) { cycleClock_ = std::move(c); events_.reserve(256); }

    // Jette les événements de la trame sans rendre (frontend audio non démarré / trame
    // sautée) : borne la mémoire ET draine le flux capturé (sinon un retard s'accumule
    // et rejouerait en différé au démarrage de l'audio). Appelé par Audio::produceFrame
    // sur les chemins early-out.
    // ⚠ RE-SYNCHRONISE aussi le miroir de rendu sur l'état CPU (même geste que
    // YM2149::clearEvents et son « audioRegs_ = regs_ ») : les événements jetés
    // portaient les transitions PLAY/STOP/MODE — sans recopie, un jeu qui démarre
    // sa musique DMA en boucle AVANT le 1er rendu (autoplay web : l'audio n'existe
    // qu'au 1er geste utilisateur) laissait aPlaying_/aMode_ aux défauts du reset :
    // flux 50 kHz consommé à 6258 Hz mono, définitivement (le mode repeat ne
    // regénère aucun événement).
    void    clearEvents() {
        events_.clear(); capR_ = capW_;
        aPlaying_ = playing_; aMode_ = mode_;
        aPhase_ = 0.0; aHaveCur_ = false;
    }

    // Gain de sortie linéaire (LMC1992 : volume maître + gauche/droite, en mono).
    // S'applique à TOUT le son STE (YM2149 + DMA), comme la puce réelle. 1.0 par
    // défaut (0 dB) → aucun effet tant que le microwire n'est pas programmé.
    float   masterGain() const;

    // Gains LINÉAIRES par canal du LMC1992 (volume maître × gauche, × droite). C'est
    // ce qui réalise le PANORAMIQUE STE : un jeu qui programme leftVolume≠rightVolume
    // pousse le son à gauche/droite. 1.0 par défaut (0 dB). Réf. Hatari lmc1992.left/right_gain.
    float   gainLeft() const;
    float   gainRight() const;

    // Correcteur de tonalité du LMC1992 (basses/aigus, ±12 dB) appliqué au mix
    // complet via deux filtres en plateau (shelving). Bypass total au défaut
    // (0/0 dB) → aucun coût ni risque tant que la tonalité n'est pas programmée.
    void    applyHpfStereo(float* st, uint32_t frames);   // HPF sous-sonique du MIX (STE)
    void    applyHpfMono(float* out, uint32_t frames);    // idem, chemin mono WASM
    void    applyTone(float* out, uint32_t frames, uint32_t sampleRate);
    // Idem en stéréo entrelacé (état de filtre L/R indépendant).
    void    applyToneStereo(float* st, uint32_t frames, uint32_t sampleRate);

    // RESET machine : coupe la lecture. `cold` = reset à FROID (power-cycle) → ré-init
    // du LMC1992 ; à chaud (Ctrl+reset) le Microwire n'a PAS de signal de reset et
    // conserve ses volumes/mixage (cf. Hatari DmaSnd_Reset, bloc `if (bCold)`).
    void    reset(bool cold = false);
    bool    playing() const { return playing_; }

    // ---- Save-state : transfère TOUT l'état runtime (save ET load, symétrique) -----
    // Un seul passage, ordre fixe. Les références (bus_), pointeurs moteur (mfp_,
    // sched_) et std::function (cycleClock_) NE sont PAS sérialisés (recâblés au load).
    void serialize(StateArchive& ar) {
        // Registres / adresses latchées.
        ar(ctrl_); ar(mode_); ar(pad20_);
        ar(startAddr_); ar(endAddr_); ar(curAddr_);
        ar(frameStartAddr_); ar(frameEndAddr_);
        // FIFO 8 octets + consommation DAC.
        ar(fifo_); ar(fifoPos_); ar(fifoNb_);
        ar.check(fifoPos_ < 8 && fifoNb_ <= 8);   // fifoPull lit fifo_[fifoPos_] sans masque
        ar(dacLast_); ar(dacRem_);
        // Anneau de capture (cœur → rendu audio). Taille fixe (kCapSize) → podVec
        // re-dimensionne à l'identique au load, indices monotones repris tels quels.
        // Invariant : l'indexation se fait au masque FIXE (capW_ & (kCapSize-1)) —
        // un anneau restauré d'une autre taille serait lu/écrit hors bornes.
        ar.podVec(cap_);
        ar.check(cap_.size() == kCapSize);
        ar(capW_); ar(capR_);
        // Microwire / LMC1992.
        ar(mwData_); ar(mwMask_); ar(mwShift_); ar(mwSteps_);
        ar.check(mwSteps_ >= 0 && mwSteps_ <= 16);   // sert de compteur de décalage (16 - mwSteps_)
        ar(mwMaster_); ar(mwLeft_); ar(mwRight_); ar(mwBass_); ar(mwTreble_); ar(mwMixing_);
        // État de lecture (thread audio).
        ar(playing_); ar(phase_);
        ar(dmaCur_); ar(haveCur_); ar(lpW0_); ar(lpW1_);
        ar(dmaCurL_); ar(dmaCurR_);
        ar(lpW0L_); ar(lpW1L_); ar(lpW0R_); ar(lpW1R_);
        // État côté AUDIO (modèle push horodaté).
        ar(aPlaying_); ar(aMode_); ar(aPhase_); ar(aHaveCur_);
        // Événements de trame horodatés — DmaEvent a du padding interne → champ par
        // champ (objVec), cf. StateArchive.
        ar.objVec(events_, 23, [](StateArchive& a, DmaEvent& e) {
            a(e.cycle); a(e.kind); a(e.start); a(e.end); a(e.mode); a(e.repeat); a(e.capW);
        });
        // Ligne XSINT.
        ar(xsint_);
        // Filtres de tonalité (biquads).
        ar(bx1_); ar(bx2_); ar(by1_); ar(by2_);
        ar(tx1_); ar(tx2_); ar(ty1_); ar(ty2_);
        ar(bx1R_); ar(bx2R_); ar(by1R_); ar(by2R_);
        ar(tx1R_); ar(tx2R_); ar(ty1R_); ar(ty2R_);
    }

private:
    // ---- FIFO 8 octets + capture (port dmaSnd.c, thread émulation) -----------------
    void    fifoRefill();            // fetch DMA par MOTS tant qu'il y a ≥ 2 places (≙ DmaSnd_FIFO_Refill)
    int8_t  fifoPull();              // tire l'octet le plus ancien (refill si vide, ≙ DmaSnd_FIFO_PullByte)
    void    fifoSetStereo();         // réaligne la FIFO sur frontière paire (≙ DmaSnd_FIFO_SetStereo)
    void    updateDac();             // consomme au rythme DAC jusqu'à liveNow → capture_ (≙ Sound_Update)
    // Anneau de CAPTURE des octets tirés par le DAC : produit par le cœur (updateDac),
    // consommé par le rendu audio (mixSegment/mix). Taille FIXE (jamais de realloc :
    // le chemin WASM lit depuis un autre thread), les plus vieux octets sont jetés si
    // personne ne consomme. Indices monotones (modulo à l'accès).
    size_t  capAvail() const { return capW_ - capR_; }
    // Octets que le RENDU peut consommer : bornés au repère du prochain événement de
    // la trame (capLimit_, posé par mixStereo segment par segment). capLimit_ ≥ capW_
    // (valeur par défaut, hors rendu segmenté) = aucune borne.
    size_t  capRenderAvail() const {
        const uint64_t w = capLimit_ < capW_ ? capLimit_ : uint64_t(capW_);
        return w > capR_ ? size_t(w - capR_) : 0;
    }
    void    capPush(int8_t b);
    // Tire le prochain échantillon L/R du flux capturé (mono → L=R). false = flux vide.
    bool    capPullLR(bool stereo, int& l, int& r);
    // Rend `count` échantillons stéréo dans `st` (entrelacé) à partir de l'état audio
    // (aPlaying_/aMode_…) en consommant le flux capturé ; `ym` aligné sur le 1er échantillon.
    void    mixSegment(float* st, const float* ym, uint32_t count, uint32_t sampleRate, float dmaGain);
    // Enregistre un événement de trame DMA daté (si l'horloge push est câblée).
    void    recordEvent(uint8_t kind);
    // Synchronise l'état audio (aXxx_) sur l'état CPU live (repli sans horloge push).
    void    syncAudioFromCpu();
    void    decodeMicrowire();                            // décode la commande LMC1992
    // Valeur LUE en $FF8924 : masque en ROTATION pendant le shift (revient
    // identique après les 16 pas), valeur écrite hors transfert — port de Hatari
    // DmaSnd_InterruptHandler_Microwire (dmaSnd.c:1063-1064).
    uint16_t mwMaskRead() const;
    void catchUpMicrowire(); // MMIO : consomme seulement les fronts Microwire échus
    void advanceMicrowire(int64_t nextEdge, int64_t now); // au plus 16 fronts

    void    startNewFrame();                              // (re)démarre une trame (gère start==end)
    void    setXsint(bool level);                         // pilote la ligne XSINT (→ MFP GPIP7)
    // Compteur de trame $FF8909/0B/0D : synchronise la consommation DAC + le fetch
    // FIFO jusqu'à l'instant émulé courant puis rend l'adresse de FETCH (port exact
    // de DmaSnd_GetFrameCount : Sound_Update en tête, puis dma.frameCounterAddr —
    // le compteur matériel montre où le DMA LIT, en avance de la FIFO sur le DAC).
    uint32_t liveCounter();

public:
    // Une étape du shift série Microwire (datée par le Scheduler, source MICROWIRE) :
    // décale $FF8922 vers 0 (16 étapes) puis, à 0, décode la commande LMC1992. Sans
    // ce shift, les diagnostics qui pollent $FF8922 jusqu'à 0 (STE_Test) bouclent.
    void    onMicrowireShift();
private:

    Bus&        bus_;
    Mfp*        mfp_   = nullptr;
    Scheduler*  sched_ = nullptr;

    // Registres. Adresses sur 24 bits (octets haut/moyen/bas, paires forcées).
    uint8_t  ctrl_ = 0;              // $FF8901 : bit0 = play, bit1 = repeat (loop), bits 2-3 relus seulement
public:
    // Posé par le Bus à chaque accès : vrai sur STE, dont on a la netlist du GST MCU
    // (relecture de $FF8901 sur 4 bits, fin relue figée par trame). Dérivé, non sérialisé.
    bool     steNetlist = true;
private:
    uint8_t  mode_ = 0;              // $FF8921 : bits0-1 fréquence, bit7 = mono
    uint8_t  pad20_ = 0;             // $FF8920 : octet scratch non intercepté, état MMIO sauvegardé
    uint32_t startAddr_ = 0;         // $FF8903/05/07
    uint32_t endAddr_   = 0;         // $FF890F/11/13
    uint32_t curAddr_   = 0;         // adresse de FETCH du DMA (avance par MOTS, cf. fifoRefill)
    // Trame DMA en cours, LATCHÉE au démarrage (port DmaSnd_StartNewFrame : le HW
    // fige début/fin à l'ouverture de la trame ; réécrire $FF8903+ pendant la
    // lecture ne vaut que pour la trame SUIVANTE). frameEndAddr_ = comparateur
    // d'ÉGALITÉ du fetch (début == fin, repeat ON → trame 2^24 octets, le fetch
    // traverse tout l'espace d'adressage — Hatari dmaSnd.c:336-341, démo « A
    // Little Bit Insane » de Lazer).
    uint32_t frameStartAddr_  = 0;   // adresse de début latchée
    uint32_t frameEndAddr_    = 0;   // adresse de fin latchée

    // ---- FIFO matérielle 8 octets + consommation DAC (thread émulation) -----------
    // Port du modèle Hatari (dmaSnd.c:117-147) : le DMA fetche des MOTS en RAM dans
    // une FIFO anneau de 8 octets remplie à chaque HBL ; le DAC la vide à la cadence
    // programmée. Conséquences fidèles : les octets sont figés AU FETCH (un programme
    // qui modifie le tampon pendant la lecture — Mental Hangover, Power Up Plus —
    // n'affecte pas les octets déjà fetchés) et la fin de trame tombe au FETCH du
    // dernier mot (XSINT/Timer A en avance d'au plus 8 octets sur le DAC).
    int8_t   fifo_[8] = {};
    uint16_t fifoPos_ = 0;           // prochain octet à tirer (0-7)
    uint16_t fifoNb_  = 0;           // octets présents (0-8)
    int64_t  dacLast_ = 0;           // cycle live de la dernière consommation DAC
    int64_t  dacRem_  = 0;           // reste fractionnaire (numérateur mod CPU_HZ, ≙ frameCounter_float)

    // Anneau de capture des octets consommés par le DAC (cœur → rendu audio). Taille
    // fixe, indices monotones ; débordement = octets les plus vieux jetés (aucun
    // consommateur branché : headless sans --sound-dump).
    static constexpr size_t kCapSize = 1u << 16;
    std::vector<int8_t> cap_ = std::vector<int8_t>(kCapSize);
    size_t   capW_ = 0, capR_ = 0;
    uint16_t mwData_ = 0, mwMask_ = 0;  // microwire $FF8922/$FF8924 (mots 16 bits)
    uint16_t mwShift_ = 0;              // valeur LUE en $FF8922 pendant le shift (→ 0)
    int      mwSteps_ = 0;              // décalages restants (16 au départ, 0 = fini)

    // LMC1992 décodé (volumes en pas de 2 dB). Défauts = 0 dB (aucune atténuation).
    int      mwMaster_ = 40;         // 0..40 → -80..0 dB (volume maître)
    int      mwLeft_   = 20;         // 0..20 → -40..0 dB
    int      mwRight_  = 20;
    int      mwBass_   = 6;          // stockés ; filtrage tonalité = TODO
    int      mwTreble_ = 6;
    int      mwMixing_ = 0;

    // État de lecture (thread audio).
    bool     playing_ = false;
    double   phase_   = 0.0;         // accumulateur de rééchantillonnage
    // Échantillon DMA COURANT, filtré à la cadence DMA (cf. lowPassPull) — tenu
    // entre deux octets (zéro-bloquant côté sortie, FIR côté entrée).
    float    lowPassPull(float in, bool enabled);
    float    dmaCur_  = 0.0f;
    bool     haveCur_ = false;
    float    lpW0_ = 0.0f, lpW1_ = 0.0f;   // retards du FIR (1,2,1)/4 anti-repliement (chemin mono)
    // Chemin STÉRÉO : échantillon courant et retards FIR séparés par canal.
    float    dmaCurL_ = 0.0f, dmaCurR_ = 0.0f;
    float    lpW0L_ = 0.0f, lpW1L_ = 0.0f, lpW0R_ = 0.0f, lpW1R_ = 0.0f;

    // --- État de lecture côté AUDIO (modèle push horodaté) -------------------------
    // Miroir de la trame DMA vu par le RENDU audio, distinct de l'état CPU (playing_,
    // curAddr_…) qui pilote la logique du jeu (compteur $FF8909, Timer A). Les DONNÉES
    // viennent du flux capturé (cap_) — l'audio ne relit plus la RAM ; les événements
    // rejoués posent la cadence (aMode_) et l'activité (aPlaying_). Après un STOP, le
    // flux résiduel est DRAINÉ avant de rendre la main au YM (≙ Hatari : la FIFO finit
    // de se vider après l'effacement du bit PLAY, dmaSnd.c:548).
    bool     aPlaying_ = false;
    uint8_t  aMode_ = 0;
    double   aPhase_ = 0.0;
    bool     aHaveCur_ = false;

    // Événement de trame DMA horodaté (cycle CPU frame-relatif). kind : 0 = PLAY start
    // (0→1 du bit PLAY, ≙ DmaInitSample), 1 = STOP (bit PLAY effacé par le CPU),
    // 2 = MODE (changement de $FF8921 : cadence/mono-stéréo du rendu).
    // `capW` = index d'écriture de l'anneau de capture AU MOMENT de l'événement : les
    // octets du flux n'étant pas datés, c'est lui qui borne ce qu'un segment de rendu
    // a le droit de consommer (cf. capLimit_) — sinon un PLAY en milieu de trame
    // (DMA au repos) faisait rendre dès l'échantillon 0 de la trame les octets
    // capturés APRÈS l'écriture PLAY, à l'ancienne cadence (bruitage avancé ≤ 20 ms).
    struct DmaEvent { uint32_t cycle; uint8_t kind; uint32_t start, end; uint8_t mode; bool repeat; uint64_t capW; };
    std::vector<DmaEvent>    events_;            // transitions de la trame courante
    std::function<int64_t()> cycleClock_;        // cycle CPU frame-relatif (posé par le frontend push)
    // Borne de consommation du flux capturé pour le segment de rendu en cours (index
    // monotone comme capW_). Transitoire du rendu (mixStereo) — NON sérialisé.
    uint64_t capLimit_ = UINT64_MAX;

    // Ligne XSINT (External Sound INTerrupt) du son DMA STE : HAUT pendant qu'une
    // trame joue, BAS à l'arrêt / fin de trame. Câblée à TAI (Timer A event-count,
    // fin de trame détectée au fetch, cf. fifoRefill) ET à GPIP7 du MFP (XOR moniteur).
    // Réf. Hatari DmaSnd_Update_XSINT_Line / DmaSnd_Get_XSINT_Line.
    bool     xsint_ = false;

    // État des filtres de tonalité (biquads Direct Form I), thread audio. Le jeu
    // « sans suffixe » sert le canal GAUCHE (et le chemin mono WASM) ; le jeu *R_ le
    // canal DROIT en stéréo. Les deux ne tournent jamais ensemble dans un même binaire.
    double   bx1_ = 0, bx2_ = 0, by1_ = 0, by2_ = 0;   // plateau basses (G / mono)
    double   tx1_ = 0, tx2_ = 0, ty1_ = 0, ty2_ = 0;   // plateau aigus  (G / mono)
    double   bx1R_ = 0, bx2R_ = 0, by1R_ = 0, by2R_ = 0;   // plateau basses (D)
    double   tx1R_ = 0, tx2R_ = 0, ty1R_ = 0, ty2R_ = 0;   // plateau aigus  (D)
    // HPF sous-sonique du MIX STE (cf. applyHpfStereo) — NON sérialisé (transitoire).
    double   hpfX1L_ = 0, hpfY0L_ = 0, hpfX1R_ = 0, hpfY0R_ = 0;
};
