// =============================================================================
//  DmaSound.cpp — Implémentation du son DMA STE.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Pacing.hpp"
#include "core/DmaSound.hpp"
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// Fréquences d'échantillonnage STE (cristal 25.175 MHz / diviseurs), bits 0-1 de
// $FF8921. Réf. doc matérielle / Hatari.
static const double kRate[4] = { 6258.0, 12517.0, 25033.0, 50066.0 };

// Gain du canal numérique AVANT les gains LMC — port de la comptabilité Hatari
// (dmaSnd.c:1146-1158) : « DMA sound is 3/4 level of YM sound », rapporté à
// l'échelle DEMI du YM STE (outScale_ 0.5, marge anti-saturation) parce que les
// gains LMC sont DOUBLÉS en compensation (cf. gainLeft/gainRight). Objectif : un
// rapport DMA/YM de ¾ après ces gains, comme Hatari (module exact ci-dessous).
// NÉGATIF : le LMC1992 INVERSE le signal DMA (le YM ressort en +1 : −1 ampli-op ×
// −1 LMC). Port du « Multiply DMA sound by -1 » d'Hatari (dmaSnd.c:520-535, formule
// −((256×¾)/4)/4). Module 0.1875 = ¾ × ¼ : le ÷4 d'Hatari couvre À LA FOIS la table
// YM demi-amplitude STE (≙ outScale_ 0.5) ET la pré-compensation du ×2 kLmcMakeup
// des gains — gains appliqués ensuite au MIX ENTIER, DMA compris (dmaSnd.c:699/706).
// L'ancien −0.375 (= ¾ × ½) ne retranchait que la demi-table : le ×2 des gains
// s'appliquait une DEUXIÈME fois au canal DMA → +6 dB vs Hatari (à 0 dB LMC,
// s=127 : Hatari 0.372 fs, NeoST sortait 0.744 fs — bug hunt 10ᵉ passe).
static constexpr float kDmaGain = -0.1875f;

// Horloge CPU (Hz) pour dater la consommation DAC en cycles (cf. Mfp / Scheduler).
static constexpr int64_t CPU_HZ = neost::pacing::kCpuHzInt;   // A28 : core/Pacing.hpp

// Câble le MFP et lui annonce que cette machine possède le son DMA (STE/Mega STE) :
// seul ce signal autorise le MFP à XOR la ligne XSINT dans GPIP7 (cf. Hatari
// MFP_Main_Compute_GPIP7). Sur un ST sans son DMA, le composant n'est pas câblé ici.
void DmaSound::setMfp(Mfp* m) {
    mfp_ = m;
    if (mfp_) mfp_->setHasDmaSound(true);
}

// Met à jour la ligne XSINT et la propage au MFP. Réf. Hatari DmaSnd_Update_XSINT_Line :
// HAUT = trame en cours, BAS = son DMA inactif. La ligne est câblée à DEUX entrées du
// MFP — GPIP7 (XOR détection moniteur) ET TAI du Timer A (event-count) — donc chaque
// transition pilote les deux. C'est la transition vers 0 (fin de trame, AER GPIP4=0 par
// défaut) qui décompte le Timer A pour le double-buffering audio STE.
void DmaSound::setXsint(bool level) {
    if (level == xsint_) return;                  // pas de transition → rien à faire
    xsint_ = level;
    if (mfp_) {
        mfp_->setXsintLine(xsint_);               // → GPIP7
        mfp_->timerA_setLineInput(xsint_);        // → TAI (Timer A event-count, polarité AER)
    }
}

// (Re)démarre une trame DMA : recale le fetch sur l'adresse de début et latche
// début/fin. Port de Hatari DmaSnd_StartNewFrame (dmaSnd.c:456-479) : si début ==
// fin (égalité STRICTE, comparateur du HW) ET repeat OFF, la trame est VIDE → on
// coupe la lecture SANS lever XSINT (sinon GPIP7 resterait figé HAUT, faussant la
// détection moniteur ; bug des demos start==end comme l'Amberstar cracktro).
// Repeat ON, ou fin < début → on ne coupe pas : le fetch avance avec wrap 24 bits
// (début == fin, repeat ON → trame 2^24 octets, cf. fifoRefill).
void DmaSound::startNewFrame() {
    curAddr_ = startAddr_;
    // ⚠ NE PAS remettre phase_/haveCur_ ici : DmaSnd_StartNewFrame (dmaSnd.c:456-479)
    // ne touche PAS l'état de rééchantillonnage (DmaInitSample/frameCounter_float),
    // remis au SEUL front PLAY 0→1 (DmaSnd_SoundControl_WriteWord, dmaSnd.c:806-812).
    // startNewFrame tourne AUSSI à chaque relatch repeat (fifoRefill) : y forcer
    // haveCur_=false consommait un octet du flux mono hors comptabilité de phase et
    // effaçait le reste fractionnaire → click/dérive d'un octet par boucle de sample.
    if (endAddr_ == startAddr_ && !(ctrl_ & 0x02)) {   // trame vide, pas de repeat → arrêt sec
        playing_ = false;
        ctrl_   &= ~0x01;
        setXsint(false);                               // surtout PAS de XSINT HAUT
        return;
    }
    frameStartAddr_ = startAddr_;                      // latch de la trame (le HW fige début/fin)
    frameEndAddr_   = endAddr_;
    playing_ = true;
    setXsint(true);                                    // début de trame : XSINT → HAUT (→ GPIP7)
}

// Remplit la FIFO par MOTS tant qu'il y a au moins 2 places — port exact de
// DmaSnd_FIFO_Refill (dmaSnd.c:342-368). C'est ICI que la fin de trame est
// détectée : au FETCH du dernier mot (le vrai DMA fetche en avance de la FIFO
// sur le DAC) → XSINT bas (event Timer A), puis relatch si repeat, sinon le
// MATÉRIEL auto-efface le bit PLAY ($FF8901 relit 0 — démo STE « Faster »).
// Le fetch traverse le plan mémoire complet (traduction MMU / aliasing de
// banques) comme le DMA disque — port STMemory_DMA_ReadByte (Bus::dmaRead8).
void DmaSound::fifoRefill() {
    if (!playing_) return;                             // PLAY off : pas de fetch
    // Trace du fetch (NEOST_DMASND_TRACE=1) : même format que le LOG_TRACE
    // « DMA snd fifo refill » d'Hatari → diff direct des séquences adr/contenu.
    static const bool trace = std::getenv("NEOST_DMASND_TRACE") != nullptr;
    while (8 - fifoNb_ >= 2) {
        fifo_[(fifoPos_ + fifoNb_ + 0) & 7] = int8_t(bus_.dmaRead8(curAddr_));
        fifo_[(fifoPos_ + fifoNb_ + 1) & 7] = int8_t(bus_.dmaRead8(curAddr_ + 1));
        if (trace)
            std::fprintf(stderr, "DMA snd fifo refill adr=%x pos %d nb %d %x %x\n",
                         curAddr_, fifoPos_, fifoNb_,
                         uint8_t(fifo_[(fifoPos_ + fifoNb_ + 0) & 7]),
                         uint8_t(fifo_[(fifoPos_ + fifoNb_ + 1) & 7]));
        fifoNb_ += 2;
        curAddr_ = (curAddr_ + 2) & 0xFFFFFF;
        if (curAddr_ == frameEndAddr_) {               // fin de trame atteinte AU FETCH
            // Cas « début == fin, repeat ON » (trame 2^24 octets) : sur STE réel
            // AUCUNE interruption de fin de trame n'est générée (dmaSnd.c:336-341)
            // — le fetch vient de wrapper sur début, on continue SANS pulser XSINT.
            if ((ctrl_ & 0x02) && frameEndAddr_ == frameStartAddr_) continue;
            setXsint(false);                           // XSINT → BAS (compte Timer A si AER bit4=0)
            if (ctrl_ & 0x02) {
                startNewFrame();                       // repeat : relatch depuis les registres → XSINT HAUT
            } else {
                ctrl_   &= ~0x01;                      // one-shot : le HW auto-efface PLAY
                playing_ = false;
                recordEvent(1);                        // STOP daté, comme l'arrêt CPU — sinon
                                                       // le rendu push garde aPlaying_ vrai et
                                                       // tient la sortie DMA (YM muet) à jamais
                break;
            }
        }
    }
}

// Tire l'octet le plus ancien de la FIFO (refill à la volée si elle est vide et
// que le DMA joue encore) — port de DmaSnd_FIFO_PullByte (dmaSnd.c:387-409).
int8_t DmaSound::fifoPull() {
    if (fifoNb_ == 0) {
        fifoRefill();
        if (fifoNb_ == 0) return 0;                    // FIFO vide et DMA arrêté
    }
    const int8_t s = fifo_[fifoPos_ & 7];
    fifoPos_ = (fifoPos_ + 1) & 7;
    --fifoNb_;
    return s;
}

// Passage mono → stéréo : réaligne la position de lecture de la FIFO sur une
// frontière PAIRE (octet pair = gauche, impair = droite), en sautant un octet si
// besoin — port de DmaSnd_FIFO_SetStereo (dmaSnd.c:419-438).
void DmaSound::fifoSetStereo() {
    if (fifoPos_ & 1) {
        fifoPos_ = (fifoPos_ + 1) & 7;
        if (fifoNb_ > 0) --fifoNb_;
    }
}

// Pousse un octet consommé par le DAC dans l'anneau de capture (→ rendu audio).
// Anneau plein (aucun consommateur branché) : jette le plus vieux — taille fixe,
// jamais de réallocation (le chemin WASM consomme depuis le thread audio).
void DmaSound::capPush(int8_t b) {
    if (capAvail() >= kCapSize) ++capR_;
    cap_[capW_++ & (kCapSize - 1)] = b;
}

// Tire le prochain échantillon L/R du flux capturé (stéréo : 2 octets pair/impair,
// mono : 1 octet dupliqué). false = flux épuisé (rien n'est consommé).
// Lecture bornée par capRenderAvail (repère du prochain événement de trame).
bool DmaSound::capPullLR(bool stereo, int& l, int& r) {
    if (stereo) {
        if (capRenderAvail() < 2) return false;
        l = cap_[capR_++ & (kCapSize - 1)];
        r = cap_[capR_++ & (kCapSize - 1)];
    } else {
        if (capRenderAvail() < 1) return false;
        l = r = cap_[capR_++ & (kCapSize - 1)];
    }
    return true;
}

// Consomme la FIFO au rythme du DAC depuis la dernière mise à jour jusqu'à
// l'instant émulé courant (reste fractionnaire exact, ≙ frameCounter_float), et
// CAPTURE les octets tirés pour le rendu audio. Équivalent du Sound_Update
// d'Hatari : appelé à chaque HBL (onHbl) et avant chaque accès registre sensible
// (compteur $FF8909+, contrôle, mode) pour dater l'état au cycle près.
void DmaSound::updateDac() {
    if (!sched_) return;
    const int64_t now     = sched_->liveNow();
    const int64_t elapsed = now - dacLast_;
    if (elapsed <= 0) return;
    dacLast_ = now;
    if (!playing_ && fifoNb_ == 0) { dacRem_ = 0; return; }   // DAC inactif
    const int64_t  rate    = int64_t(kRate[mode_ & 0x03]);
    const int64_t  num     = elapsed * rate + dacRem_;
    const int64_t  samples = num / CPU_HZ;
    dacRem_ = num % CPU_HZ;
    const uint32_t step  = (mode_ & 0x80) ? 1u : 2u;          // octets par échantillon
    // Consommation par ÉCHANTILLON entier (paire L/R en stéréo), jamais par octet
    // isolé : s'arrêter entre le L et le R (fin de flux sur résidu impair) laissait
    // un nombre IMPAIR d'octets dans l'anneau de capture — tous les échantillons
    // suivants décalés d'un octet, canaux inversés jusqu'au drain complet. Hatari
    // ne peut pas le faire : sa boucle stéréo tire toujours les deux octets ensemble.
    for (int64_t s = 0; s < samples; ++s) {
        if (fifoNb_ == 0 && !playing_) break;
        if (step == 2 && fifoNb_ < 2 && !playing_) break;     // paire incomplète : on n'entame pas
        for (uint32_t b = 0; b < step; ++b)
            capPush(fifoPull());
    }
}

// Tic HBL — port de DmaSnd_STE_HBL_Update (dmaSnd.c:727-741) : le DMA garde la
// FIFO pleine à chaque ligne (refill), le DAC consomme ce qui est dû (update),
// puis la FIFO est re-remplie derrière. C'est ce grain « par ligne » qui rend
// fidèles les programmes modifiant le tampon PENDANT la lecture (Mental
// Hangover, Power Up Plus) : les octets sont figés au fetch, pas relus après.
void DmaSound::onHbl() {
    if (!playing_ && fifoNb_ == 0) return;             // son DMA totalement inactif
    fifoRefill();
    updateDac();
    fifoRefill();
}

void DmaSound::reset(bool cold) {
    playing_ = false;
    ctrl_  = 0;
    mode_  = 0;
    phase_ = 0.0;
    haveCur_ = false; dmaCur_ = 0.0f; lpW0_ = lpW1_ = 0.0f;   // FIR anti-repliement à zéro
    dmaCurL_ = dmaCurR_ = 0.0f; lpW0L_ = lpW1L_ = lpW0R_ = lpW1R_ = 0.0f;   // idem chemin stéréo
    bx1_  = bx2_  = by1_  = by2_  = 0; tx1_  = tx2_  = ty1_  = ty2_  = 0;   // filtres tonalité G/mono
    bx1R_ = bx2R_ = by1R_ = by2R_ = 0; tx1R_ = tx2R_ = ty1R_ = ty2R_ = 0;   // filtres tonalité D
    aPlaying_ = false; aMode_ = 0;                 // état audio « push »
    aPhase_ = 0.0; aHaveCur_ = false;
    events_.clear();
    fifoPos_ = fifoNb_ = 0;                        // FIFO vidée (≙ DmaSnd_Reset, dmaSnd.c:253-254)
    dacLast_ = sched_ ? sched_->liveNow() : 0;
    dacRem_  = 0;
    capR_ = capW_;                                 // flux capturé drainé
    startAddr_ = endAddr_ = curAddr_ = 0;          // Hatari met start/end à 0 même au warm reset (fix 'Brace')
    mwShift_ = 0; mwSteps_ = 0;                     // transfert série Microwire éventuel annulé
    setXsint(false);                               // son DMA inactif au reset → XSINT BAS
    // Le LMC1992/Microwire n'a PAS de broche de reset : ses registres (volumes, basses/
    // aigus, mixage) PERSISTENT au reset à chaud (cf. Hatari, bloc `if (bCold)`). On ne
    // les réinitialise qu'à FROID. NeoST garde des défauts à 0 dB (et non muets comme le
    // vrai HW/Hatari) pour rester audible si l'OS ne programme pas le LMC.
    if (cold) {
        mwMaster_ = 40; mwLeft_ = 20; mwRight_ = 20;   // LMC1992 à 0 dB
        mwBass_ = 6; mwTreble_ = 6; mwMixing_ = 0;
    }
}

// Décode une commande LMC1992 reçue par microwire (port dmaSnd.c:DmaSnd_InterruptHandler_Microwire).
// Une commande = un run CONTIGU de bits à 1 dans le masque ($FF8924) ; les bits à 0 du masque
// terminent la commande. Si le run ne forme pas une adresse LMC valide (%10 + ≥9 bits), on
// reprend la recherche au prochain '1' du masque (commandes invalides ignorées).
void DmaSound::decodeMicrowire() {
    uint16_t cmd = 0;
    int      cmdLen = 0;
    for (int i = 15; i >= 0; --i) {
        if (!(mwMask_ & (1u << i))) continue;
        do {
            cmd = uint16_t(cmd << 1);
            ++cmdLen;
            if (mwData_ & (1u << i)) cmd |= 1;
            --i;
        } while (i >= 0 && (mwMask_ & (1u << i)));

        if (cmdLen >= 11 && ((cmd >> (cmdLen - 2)) & 0x03) == 0x02)
            break;

        if (i < 0) return;
        cmd = 0;
        cmdLen = 0;
    }

    if (cmdLen < 11 || ((cmd >> (cmdLen - 2)) & 0x03) != 0x02)
        return;

    switch ((cmd >> 6) & 0x07) {
        case 0: mwMixing_ = cmd & 0x03; break;
        case 1: mwBass_   = cmd & 0x0F; break;
        case 2: mwTreble_ = cmd & 0x0F; break;
        case 3: mwMaster_ = cmd & 0x3F; break;   // 0..40 → -80..0 dB
        case 4: mwRight_  = cmd & 0x1F; break;   // 0..20 → -40..0 dB
        case 5: mwLeft_   = cmd & 0x1F; break;
        default: break;
    }
}

// Valeur relue en $FF8924 : pendant le transfert le masque TOURNE au rythme du
// shift — `(mask << (16 - étapes_restantes)) | (mask >> étapes_restantes)`, port
// exact de Hatari DmaSnd_InterruptHandler_Microwire (dmaSnd.c:1063-1064). Après
// les 16 pas la rotation est complète → le masque relu redevient la valeur écrite
// (c'est aussi pourquoi decodeMicrowire peut décoder sur mwMask_ intact).
uint16_t DmaSound::mwMaskRead() const {
    if (mwSteps_ <= 0) return mwMask_;
    return uint16_t((mwMask_ << (16 - mwSteps_)) | (mwMask_ >> mwSteps_));
}

// Une étape du transfert série Microwire (datée toutes les 8 cycles par le
// Scheduler). $FF8922 lit `data << (16 - étapes_restantes)` → atteint 0 après 16
// décalages (port de Hatari DmaSnd_InterruptHandler_Microwire). À 0, on décode la
// commande LMC1992. Le masque ($FF8924) tourne mais revient identique → decode OK.
void DmaSound::onMicrowireShift() {
    const int64_t now = sched_ ? sched_->liveNow() : 0;
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    advanceMicrowire(due >= 0 ? due : now, now);
}

void DmaSound::catchUpMicrowire() {
    if (!sched_ || mwSteps_ <= 0) return;
    const int64_t remaining = sched_->rawCyclesUntil(Scheduler::MICROWIRE);
    if (remaining == INT64_MIN || remaining > 0) return;
    const int64_t now = sched_->liveNow();
    sched_->cancel(Scheduler::MICROWIRE);
    advanceMicrowire(now + remaining, now);
}

// Hatari DmaSnd_InterruptHandler_Microwire conserve PendingCyclesOver : un
// callback tardif consomme tous les fronts à 8 cycles déjà passés, sans dériver.
// La même cascade bornée (16 pas au maximum) sert les accès Data/Mask avant MMIO.
void DmaSound::advanceMicrowire(int64_t nextEdge, int64_t now) {
    if (mwSteps_ <= 0) return;
    while (nextEdge <= now) {
        --mwSteps_;
        mwShift_ = uint16_t(uint32_t(mwData_) << (16 - mwSteps_));
        if (!mwSteps_) {
            if (sched_) sched_->cancel(Scheduler::MICROWIRE);
            decodeMicrowire(); // une seule application au seizième front
            return;
        }
        nextEdge += 8;
    }
    if (sched_) sched_->schedule(Scheduler::MICROWIRE, nextEdge);
}

// Plateaux (shelving) 1er ORDRE du correcteur LMC1992 — port EXACT de Hatari
// DmaSnd_Bass_Shelf / DmaSnd_Treble_Shelf (dmaSnd.c:1366-1404, méthode Savinkoff),
// coupures MESURÉES 118.2763 Hz (basses) / 8438.756 Hz (aigus). L'ancien modèle
// (RBJ 2e ordre 200/8000 Hz) donnait une réponse différente dès que basses/aigus
// quittaient 0 dB. À 0 dB (g = 1) : b0 = 1, b1 = a1 → transfert identité, d'où le
// bypass exact des appelants quand les deux codes valent 6.
static void shelf1(bool lowShelf, double dB, double f0, double fs,
                   double& b0, double& b1, double& a1) {
    // M_PI n'est PAS standard (POSIX seulement) : MinGW ne le définit pas sans
    // _USE_MATH_DEFINES, et le build Windows échouait ici. Constante locale plutôt
    // qu'une macro conditionnelle — même valeur, aucune dépendance de plateforme.
    constexpr double kPi = 3.14159265358979323846;
    const double g = std::pow(10.0, dB / 20.0);
    const double t = std::tan(kPi * f0 / fs);
    if (lowShelf) {
        a1 = (g < 1.0) ? (t - g) / (t + g) : (t - 1.0) / (t + 1.0);
        b0 = (1.0 + a1) * (g - 1.0) / 2.0 + 1.0;
        b1 = (1.0 + a1) * (g - 1.0) / 2.0 + a1;
    } else {
        a1 = (g < 1.0) ? (g * t - 1.0) / (g * t + 1.0) : (t - 1.0) / (t + 1.0);
        b0 = 1.0 + (1.0 - a1) * (g - 1.0) / 2.0;
        b1 = a1 + (a1 - 1.0) * (g - 1.0) / 2.0;
    }
}

void DmaSound::applyTone(float* out, uint32_t frames, uint32_t sampleRate) {
    if ((mwBass_ == 6 && mwTreble_ == 6) || sampleRate == 0) return;   // 0/0 dB → bypass
    // Codes 0-12 → -12..+12 dB (pas de 2 dB) ; codes 13-15 SATURENT à +12 dB, comme la
    // table LMC1992_Bass_Treble_Table de Hatari (dmaSnd.c:211-214) — sans ça les codes
    // 13-15 donnaient +14/+16/+18 dB.
    const int bassCode = mwBass_   < 12 ? mwBass_   : 12;
    const int trebCode = mwTreble_ < 12 ? mwTreble_ : 12;
    const double bassDb = (bassCode - 6) * 2.0;
    const double trebDb = (trebCode - 6) * 2.0;
    double bb0, bb1, ba1, tb0, tb1, ta1;
    shelf1(true,  bassDb, 118.2763,  sampleRate, bb0, bb1, ba1);   // basses (Savinkoff)
    shelf1(false, trebDb, 8438.756, sampleRate, tb0, tb1, ta1);    // aigus

    for (uint32_t i = 0; i < frames; ++i) {
        const double x = out[i];
        const double yb = bb0 * x + bb1 * bx1_ - ba1 * by1_;
        bx1_ = x;  by1_ = yb;
        const double yt = tb0 * yb + tb1 * tx1_ - ta1 * ty1_;
        tx1_ = yb; ty1_ = yt;
        out[i] = static_cast<float>(yt);
    }
}

// Idem que applyTone mais sur le tampon ENTRELACÉ L/R : les coefficients sont calculés
// une fois (basses+aigus identiques sur les deux canaux), chaque canal gardant son
// propre état de filtre (G = sans suffixe, D = *R_) pour ne pas mélanger les retards.
void DmaSound::applyToneStereo(float* st, uint32_t frames, uint32_t sampleRate) {
    if ((mwBass_ == 6 && mwTreble_ == 6) || sampleRate == 0) return;   // 0/0 dB → bypass
    const int bassCode = mwBass_   < 12 ? mwBass_   : 12;
    const int trebCode = mwTreble_ < 12 ? mwTreble_ : 12;
    const double bassDb = (bassCode - 6) * 2.0;
    const double trebDb = (trebCode - 6) * 2.0;
    double bb0, bb1, ba1, tb0, tb1, ta1;
    shelf1(true,  bassDb, 118.2763,  sampleRate, bb0, bb1, ba1);
    shelf1(false, trebDb, 8438.756, sampleRate, tb0, tb1, ta1);

    for (uint32_t i = 0; i < frames; ++i) {
        // Canal gauche (état sans suffixe).
        const double xl = st[2 * i];
        const double ybl = bb0 * xl + bb1 * bx1_ - ba1 * by1_;
        bx1_ = xl;  by1_ = ybl;
        const double ytl = tb0 * ybl + tb1 * tx1_ - ta1 * ty1_;
        tx1_ = ybl; ty1_ = ytl;
        st[2 * i] = static_cast<float>(ytl);
        // Canal droit (état *R_).
        const double xr = st[2 * i + 1];
        const double ybr = bb0 * xr + bb1 * bx1R_ - ba1 * by1R_;
        bx1R_ = xr;  by1R_ = ybr;
        const double ytr = tb0 * ybr + tb1 * tx1R_ - ta1 * ty1R_;
        tx1R_ = ybr; ty1R_ = ytr;
        st[2 * i + 1] = static_cast<float>(ytr);
    }
}

// HPF sous-sonique appliqué au MIX YM+DMA (STE) — port de Subsonic_IIR_HPF_Left/Right
// (sound.c:382-399, pôle 1 − 64/32768) tel qu'appelé par DmaSnd_Apply_LMC AVANT
// gains + tonalité (dmaSnd.c:699,706). Sur ST le HPF reste DANS la chaîne YM
// (sound.c:1744) ; sur STE le YM entre BRUT dans le mix (YM2149::setHpfBypass) et
// c'est le MÉLANGE — composante continue du DMA comprise — qui est filtré ici.
// États NON sérialisés : transitoire de rendu (<0,1 s), format save-state inchangé.
void DmaSound::applyHpfStereo(float* st, uint32_t frames) {
    constexpr double pole = 1.0 - 64.0 / 32768.0;
    for (uint32_t i = 0; i < frames; ++i) {
        const double xl = st[2 * i], xr = st[2 * i + 1];
        const double yl = xl - hpfX1L_ + pole * hpfY0L_;
        hpfX1L_ = xl; hpfY0L_ = yl; st[2 * i]     = static_cast<float>(yl);
        const double yr = xr - hpfX1R_ + pole * hpfY0R_;
        hpfX1R_ = xr; hpfY0R_ = yr; st[2 * i + 1] = static_cast<float>(yr);
    }
}

// Variante MONO (chemin WASM legacy) : même pôle, état gauche seul.
void DmaSound::applyHpfMono(float* out, uint32_t frames) {
    constexpr double pole = 1.0 - 64.0 / 32768.0;
    for (uint32_t i = 0; i < frames; ++i) {
        const double x = out[i];
        const double y = x - hpfX1L_ + pole * hpfY0L_;
        hpfX1L_ = x; hpfY0L_ = y; out[i] = static_cast<float>(y);
    }
}

// Facteur ×2 des gains LMC — port de Hatari dmaSnd.c:1152-1153/1460-1461 :
// « lmc1992.left_gain = leftVolume × masterVolume × 2/(65536²) ». Le ×2 compense
// la DEMI-amplitude du YM en STE (table `YM_OUTPUT_LEVEL>>1` chez Hatari ≙
// outScale_ 0.5 ici, marge anti-saturation pour le DMA) : à volume LMC plein
// (init TOS), le YM STE ressort à PLEINE amplitude, identique au YM ST. Sans ce
// ×2, le YM STE jouait 6 dB sous le ST (S3, cf. docs/HATARI_DIVERGENCES.md).
static constexpr double kLmcMakeup = 2.0;

float DmaSound::masterGain() const {
    // Pas de 2 dB ; valeurs au-delà du max = 0 dB. Sortie mono → moyenne G/D.
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double ldB = (mwLeft_   >= 20 ? 0 : (mwLeft_   - 20) * 2);
    const double rdB = (mwRight_  >= 20 ? 0 : (mwRight_  - 20) * 2);
    const double gL = std::pow(10.0, (mdB + ldB) / 20.0) * kLmcMakeup;
    const double gR = std::pow(10.0, (mdB + rdB) / 20.0) * kLmcMakeup;
    return static_cast<float>((gL + gR) * 0.5);
}

// Gains linéaires par canal (volume maître × gauche / × droite) — réalisent le
// panoramique STE. Réf. Hatari : lmc1992.left_gain = leftVolume × masterVolume × 2.
float DmaSound::gainLeft() const {
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double ldB = (mwLeft_   >= 20 ? 0 : (mwLeft_   - 20) * 2);
    return static_cast<float>(std::pow(10.0, (mdB + ldB) / 20.0) * kLmcMakeup);
}
float DmaSound::gainRight() const {
    const double mdB = (mwMaster_ >= 40 ? 0 : (mwMaster_ - 40) * 2);
    const double rdB = (mwRight_  >= 20 ? 0 : (mwRight_  - 20) * 2);
    return static_cast<float>(std::pow(10.0, (mdB + rdB) / 20.0) * kLmcMakeup);
}

// FIR anti-repliement (1,2,1)/4 à état EXTERNE (cf. lowPassPull) — partagé par les
// deux canaux du chemin stéréo, chacun avec ses propres retards w0/w1.
static inline float fir121(float& w0, float& w1, int in, bool enabled) {
    const float x = float(in);
    const float y = enabled ? (w0 + 2.0f * w1 + x) : (w1 * 4.0f);
    w0 = w1; w1 = x;
    return y;
}

// Enregistre une transition de trame DMA datée au cycle frame-relatif courant (modèle
// push, cf. mixStereo). PLAY start (kind 0) latche start/end/mode/repeat ; STOP (kind 1)
// = effacement CPU du bit PLAY. Sans horloge câblée (headless/WASM) : no-op.
void DmaSound::recordEvent(uint8_t kind) {
    if (!cycleClock_) return;
    int64_t c = cycleClock_();
    if (c < 0) c = 0;
    events_.push_back({ uint32_t(c), kind, startAddr_, endAddr_, mode_, bool(ctrl_ & 0x02), uint64_t(capW_) });
}

// Repli SANS horloge push : aligne l'état audio sur l'état CPU live au début de trame.
// Reproduit l'ancien rendu « échantillonné par trame » (le bit PLAY pilote tout).
void DmaSound::syncAudioFromCpu() {
    if (playing_ && !aPlaying_) {                  // démarrage vu côté CPU → (re)cale l'audio
        aMode_ = mode_; aPhase_ = 0.0; aHaveCur_ = false;
    }
    aPlaying_ = playing_;
}

// Compteur de trame $FF8909/0B/0D — port exact de DmaSnd_GetFrameCount : on
// synchronise d'abord la consommation DAC + le fetch à l'instant émulé courant
// (≙ le Sound_Update en tête), puis on rend l'adresse de FETCH (le compteur
// matériel montre où le DMA LIT, en avance de la FIFO — ≤ 8 octets — sur le
// DAC). À l'ARRÊT, le vrai HW renvoie l'adresse de DÉBUT (dmaSnd.c:756-759).
uint32_t DmaSound::liveCounter() {
    // Port EXACT de DmaSnd_GetFrameCount (dmaSnd.c:748-761) : il appelle SEULEMENT
    // Sound_Update (≙ updateDac), PAS de refill FIFO. Le refill ne se fait qu'au HBL
    // (onHbl) ou à la volée quand la FIFO se vide (fifoPull). Un `fifoRefill()` ajouté
    // ici re-topait curAddr_ à CHAQUE lecture du compteur → un poll serré de
    // $FF8909-0D à l'intérieur d'une ligne voyait l'adresse de fetch avancer en
    // continu (+2, +2…) au lieu de sauter par paquets au HBL comme sur le vrai STE.
    if (playing_ && sched_) updateDac();
    if (!playing_) return startAddr_;                  // à l'arrêt : adresse de DÉBUT
    return curAddr_;
}

uint8_t DmaSound::read8(uint32_t addr) {
    if ((addr & 0xFF) >= 0x22 && (addr & 0xFF) <= 0x25) catchUpMicrowire();
    switch (addr & 0xFF) {
        // $FF8900 : registre MOT chez Hatari (ioMemTabSTE.c:142) — la lecture pose
        // nDmaSoundControl en 16 bits, l'octet PAIR relit donc 0x00 (pas 0xFF) :
        // « move.w $FF8900,d0 » doit rendre $000x, pas $FF0x.
        case 0x00: return 0x00;
        case 0x01: return ctrl_;
        // $FF8920 : octet lu/écrit SANS interception chez Hatari (ioMemTabSTE.c:162)
        // — il conserve la valeur écrite (0 au boot), sauvegardée avec les
        // registres comme l'IoMem dans STMemory_MemorySnapShot_Capture d'Hatari.
        case 0x20: return pad20_;
        case 0x03: return uint8_t(startAddr_ >> 16);
        case 0x05: return uint8_t(startAddr_ >> 8);
        case 0x07: return uint8_t(startAddr_);
        // Compteur courant (position de lecture) : position LIVE cycle-exacte
        // dérivée de l'horloge émulée (cf. liveCounter — port DmaSnd_GetFrameCount
        // qui appelle Sound_Update en tête). À l'ARRÊT, le vrai HW renvoie
        // l'adresse de DÉBUT, pas la dernière position (dmaSnd.c:756-759).
        case 0x09: return uint8_t(liveCounter() >> 16);
        case 0x0B: return uint8_t(liveCounter() >> 8);
        case 0x0D: return uint8_t(liveCounter());
        // Fin : la relecture passe par le second étage SFT (netlist 4081P14), verrou
        // transparent tant que SFRAME est bas (son arrêté) et figé à chaque début de
        // trame — pendant la lecture, une nouvelle fin écrite n'est visible qu'à la
        // trame suivante.
        // STE seulement (netlist) ; Mega STE : valeur écrite, comme Hatari.
        case 0x0F: return uint8_t((playing_ && steNetlist ? frameEndAddr_ : endAddr_) >> 16);
        case 0x11: return uint8_t((playing_ && steNetlist ? frameEndAddr_ : endAddr_) >> 8);
        case 0x13: return uint8_t(playing_ && steNetlist ? frameEndAddr_ : endAddr_);
        case 0x21: return mode_;
        case 0x22: return uint8_t(mwShift_ >> 8);     // microwire data : valeur EN COURS de shift
        case 0x23: return uint8_t(mwShift_);          // (→ 0 quand le transfert est fini)
        // Masque microwire : en ROTATION pendant le shift (cf. mwMaskRead) — un
        // programme qui polle $FF8924 voit le masque tourner puis revenir.
        case 0x24: return uint8_t(mwMaskRead() >> 8);
        case 0x25: return uint8_t(mwMaskRead());
        default:   return 0xFF;
    }
}

void DmaSound::write8(uint32_t addr, uint8_t v) {
    if ((addr & 0xFF) >= 0x22 && (addr & 0xFF) <= 0x25) catchUpMicrowire();
    switch (addr & 0xFF) {
        case 0x01: {                                  // contrôle : play / repeat
            updateDac();                               // date l'état AVANT le changement (≙ Sound_Update
                                                       // en tête de DmaSnd_SoundControl_WriteWord)
            const bool wasPlaying = ctrl_ & 0x01;
            ctrl_ = v & (steNetlist ? 0x0F : 0x03);    // STE : 4 verrous ID0-3 relus (REGISTER.PIN) ;
                                                       // seuls play/repeat pilotent quelque chose
            if ((ctrl_ & 0x01) && !wasPlaying) {       // 0→1 : (re)démarre la trame
                dacLast_ = sched_ ? sched_->liveNow() : 0;
                dacRem_  = 0;                          // ≙ frameCounter_float = 0 au start
                phase_   = 0.0;                        // ≙ DmaInitSample : état de rééchantillonnage
                haveCur_ = false;                      // mono remis au SEUL front PLAY 0→1 (pas au relatch)
                startNewFrame();                       // latch + XSINT haut (gère start==end)
                if (playing_) recordEvent(0);          // PLAY daté (≙ DmaInitSample — pas au relatch repeat)
            } else if (!(ctrl_ & 0x01) && wasPlaying) { // bit play à 0 : arrêt
                playing_ = false;
                recordEvent(1);                        // STOP daté (modèle push audio)
                setXsint(false);                       // arrêt : XSINT → BAS (la FIFO finit de se vider)
            }
            break;
        }
        // Adresses 24 bits (paires forcées : bit0 de l'octet bas ignoré). Octet
        // haut masqué à $3F : sur ST/STE ≤ 4 Mo le compteur DMA n'a que 22 bits
        // (Hatari DMA_MaskAddressHigh, m68000.c) — le pointeur son ne sort
        // jamais de l'espace RAM, comme sur le vrai matériel.
        case 0x03: startAddr_ = (startAddr_ & 0x00FFFF) | (uint32_t(v & 0x3F) << 16); break;
        case 0x05: startAddr_ = (startAddr_ & 0xFF00FF) | (uint32_t(v) << 8);  break;
        case 0x07: startAddr_ = (startAddr_ & 0xFFFF00) | (uint32_t(v) & 0xFE); break;
        case 0x0F: endAddr_   = (endAddr_   & 0x00FFFF) | (uint32_t(v & 0x3F) << 16); break;
        case 0x11: endAddr_   = (endAddr_   & 0xFF00FF) | (uint32_t(v) << 8);  break;
        case 0x13: endAddr_   = (endAddr_   & 0xFFFF00) | (uint32_t(v) & 0xFE); break;
        // $FF8920 : octet scratch relisible (cf. read8 — Hatari ne l'intercepte pas).
        case 0x20: pad20_ = v; break;
        // Mode : seuls les bits existant sur un vrai STE sont câblés — masque 0x8F
        // (bit7 mono + bits0-1 fréquence), la relecture montre la valeur masquée
        // (Hatari DmaSnd_SoundModeCtrl_WriteByte, dmaSnd.c:1013-1027). La
        // consommation due est datée à l'ANCIENNE cadence avant le changement, et
        // le passage mono → stéréo réaligne la FIFO sur frontière paire.
        case 0x21: {
            updateDac();
            const uint8_t newMode = v & 0x8F;
            if ((mode_ & 0x80) && !(newMode & 0x80)) fifoSetStereo();
            if (newMode != mode_) { mode_ = newMode; recordEvent(2); }   // MODE daté (cadence du rendu)
            break;
        }
        // Microwire : mots 16 bits ($FF8922 data, $FF8924 mask). Pendant un
        // transfert EN COURS (shift), les écritures data/mask sont IGNORÉES —
        // « Only update, if no shift is in progress » (Hatari DmaSnd_MicrowireData/
        // Mask_WriteWord, dmaSnd.c:1195-1206 et 1238-1244).
        case 0x22:
            if (mwSteps_ == 0) {
                // L'autre octet vient du registre visible, déjà décalé à zéro
                // après la commande précédente (Hatari : IoMem_ReadWord).
                mwData_ = uint16_t((mwShift_ & 0x00FF) | (v << 8));
                // Accès OCTET isolé (move.b $FF8922) : Hatari intercepte la donnée
                // Microwire en SIZE_WORD (ioMemTabSTE.c:164) — TOUT accès écriture,
                // même octet, déclenche le transfert avec (octet haut neuf | octet
                // bas ancien) (dmaSnd.c:1195-1204). L'accès MOT (write16 → 2 write8)
                // ne démarre que sur l'octet BAS (case 0x23), mot complet posé.
                if (bus_.ioAccessWidth() == 1) {
                    mwShift_ = mwData_;
                    if (sched_) { mwSteps_ = 16; sched_->schedule(Scheduler::MICROWIRE, sched_->liveNow() + 8); }
                    else        { mwShift_ = 0; decodeMicrowire(); }
                }
            }
            break;
        case 0x23:
            // Écriture de l'octet bas de la donnée (mot complet) → démarre un
            // transfert série Microwire (16 décalages de 8 cycles, cf. Hatari
            // DmaSnd_MicrowireData_WriteWord). $FF8922 lira la valeur décalée
            // jusqu'à 0, puis decodeMicrowire(). Sans scheduler → décodage immédiat.
            if (mwSteps_ == 0) {
                // En accès mot, le haut vient d'être posé sans déclenchement.
                // En accès octet, reprendre le haut visible après les shifts.
                const uint16_t previous = bus_.ioAccessWidth() == 1 ? mwShift_ : mwData_;
                mwData_ = uint16_t((previous & 0xFF00) | v);
                mwShift_ = mwData_;
                if (sched_) { mwSteps_ = 16; sched_->schedule(Scheduler::MICROWIRE, sched_->liveNow() + 8); }
                else        { mwShift_ = 0; decodeMicrowire(); }
            }
            break;
        case 0x24: if (mwSteps_ == 0) mwMask_ = uint16_t((mwMask_ & 0x00FF) | (v << 8)); break;
        case 0x25: if (mwSteps_ == 0) mwMask_ = uint16_t((mwMask_ & 0xFF00) | v); break;
        default: break;                                // compteur courant : lecture seule
    }
}

// Filtre anti-repliement du canal DMA — port du DmaSnd_LowPassFilter d'Hatari
// (dmaSnd.c:1316-1349) : FIR 3 points (1,2,1)/4 appliqué à CHAQUE octet tiré à la
// cadence DMA quand elle dépasse la fréquence de sortie hôte (50066 Hz → 48 kHz :
// sans lui, le repliement siffle). Filtre coupé : simple retard d'un échantillon
// (×4), pour garder gain et latence constants — le /4 est appliqué au mixage,
// comme le « Divide by 4 to account for DmaSnd_LowPassFilter » d'Hatari.
float DmaSound::lowPassPull(float in, bool enabled) {
    const float x = in;
    const float y = enabled ? (lpW0_ + 2.0f * lpW1_ + x) : (lpW1_ * 4.0f);
    lpW0_ = lpW1_;
    lpW1_ = x;
    return y;
}

void DmaSound::mix(float* out, uint32_t frames, uint32_t sampleRate) {
    if ((!playing_ && capAvail() == 0) || sampleRate == 0) return;
    const double inc    = kRate[mode_ & 0x03] / sampleRate;   // pas de rééchantillonnage
    const bool   stereo = !(mode_ & 0x80);                    // bit7=0 → stéréo entrelacé
    const bool lowPass  = kRate[mode_ & 0x03] > double(sampleRate);   // anti-repliement

    // Registre de mixage du LMC1992 (commande 0) : seul mixing==1 mélange YM2149 + DMA ;
    // 0/2/3 routent le DMA SEUL → il écrase le YM (cf. Hatari DmaSnd_GenerateSamples,
    // dmaSnd.c:555-568). N'a d'effet qu'ici, trame en cours : DMA à l'arrêt → `mix` sort
    // plus haut et le YM passe intact (pas de mute du YM hors lecture DMA).
    const bool addYm = (mwMixing_ == 1);
    for (uint32_t i = 0; i < frames; ++i) {
        if (!haveCur_) {                                      // 1er échantillon de la trame
            int l, r;
            if (capPullLR(stereo, l, r)) {
                // Moyenne en flottant : une paire de somme impaire conserve son
                // demi-échantillon, comme la moyenne des deux voies FIR stéréo.
                dmaCur_  = lowPassPull((l + r) * 0.5f, lowPass);
                haveCur_ = true;
            }
        }
        const float dma = (dmaCur_ / 4.0f / 128.0f) * kDmaGain;   // /4 = gain du FIR
        if (addYm) out[i] += dma;                             // YM2149 + DMA
        else       out[i]  = dma;                             // DMA seul (écrase le YM)
        phase_ += inc;
        while (phase_ >= 1.0) {                                // avance dans le flux capturé
            int l, r;
            if (!capPullLR(stereo, l, r)) {
                if (!playing_) return;                         // flux drainé, DMA arrêté → reste = YM
                // Sous-alimenté transitoire : tient l'échantillon. La dette est
                // PLAFONNÉE à 1 octet — sinon le rattrapage SAUTERAIT des octets
                // en rafale au retour du flux (rendu déformé : octets « mangés »).
                phase_ = 1.0;
                break;
            }
            phase_ -= 1.0;
            // Octet suivant tiré à la cadence DMA → filtré ICI (pas à la cadence
            // hôte) : c'est ce qui fait l'anti-repliement.
            dmaCur_ = lowPassPull((l + r) * 0.5f, lowPass);
        }
    }
}

// Rend `count` échantillons stéréo (entrelacés dans `st`) depuis l'état AUDIO (aXxx_),
// qu'il avance en CONSOMMANT le flux capturé (cap_) — les octets ont été figés au
// fetch par le cœur (FIFO au faisceau), l'audio ne relit plus la RAM. `ym` est aligné
// sur le 1er échantillon du segment. DMA inactif ET flux drainé → YM recopié L=R.
void DmaSound::mixSegment(float* st, const float* ym, uint32_t count, uint32_t sampleRate, float dmaGain) {
    // DMA inactif ET plus rien à drainer AVANT le prochain événement → YM seul (L=R).
    // C'est capRenderAvail (borné) et non capAvail : avant un PLAY de milieu de trame,
    // l'anneau contient déjà les octets que le DAC tirera APRÈS l'écriture PLAY.
    if ((!aPlaying_ && capRenderAvail() == 0) || sampleRate == 0) {
        for (uint32_t i = 0; i < count; ++i) { st[2 * i] = ym[i]; st[2 * i + 1] = ym[i]; }
        return;
    }
    const double inc    = kRate[aMode_ & 0x03] / sampleRate;
    const bool   stereo = !(aMode_ & 0x80);
    const bool lowPass  = kRate[aMode_ & 0x03] > double(sampleRate);
    const bool addYm    = (mwMixing_ == 1);                   // 1 = YM+DMA, sinon DMA écrase YM (live)

    for (uint32_t i = 0; i < count; ++i) {
        if (!aHaveCur_) {                                     // 1er octet du flux (≙ DmaInitSample)
            int l, r;
            if (capPullLR(stereo, l, r)) {
                dmaCurL_ = fir121(lpW0L_, lpW1L_, l, lowPass);
                dmaCurR_ = fir121(lpW0R_, lpW1R_, r, lowPass);
                aHaveCur_ = true;
            }
        }
        const float dl = (dmaCurL_ / 4.0f / 128.0f) * kDmaGain * dmaGain;
        const float dr = (dmaCurR_ / 4.0f / 128.0f) * kDmaGain * dmaGain;
        if (addYm) { st[2 * i] = ym[i] + dl; st[2 * i + 1] = ym[i] + dr; }
        else       { st[2 * i] = dl;         st[2 * i + 1] = dr; }
        aPhase_ += inc;
        while (aPhase_ >= 1.0) {
            int l, r;
            if (!capPullLR(stereo, l, r)) {
                if (!aPlaying_) {                              // flux drainé, DMA arrêté → YM sur le reste
                    aHaveCur_ = false;
                    for (uint32_t j = i + 1; j < count; ++j) { st[2 * j] = ym[j]; st[2 * j + 1] = ym[j]; }
                    return;
                }
                // Sous-alimenté transitoire : tient l'échantillon. Dette PLAFONNÉE
                // à 1 octet — sans le plafond, le rattrapage sauterait des octets
                // en rafale au retour du flux (les fins de trame « mangeaient »
                // le début du lot suivant, mesuré sur l'étalon make_dmasnd_test).
                aPhase_ = 1.0;
                break;
            }
            aPhase_ -= 1.0;
            dmaCurL_ = fir121(lpW0L_, lpW1L_, l, lowPass);
            dmaCurR_ = fir121(lpW0R_, lpW1R_, r, lowPass);
        }
    }
}

// Version stéréo « push » : rejoue les transitions PLAY/STOP de la trame à leur cycle
// exact (segments entre événements), comme YM2149::synthesizeFrame. Sans horloge
// câblée : repli sur un segment unique aligné sur l'état CPU live (ancien comportement).
void DmaSound::mixStereo(float* st, const float* ym, uint32_t frames, uint32_t sampleRate, int64_t frameCycles, float dmaGain) {
    if (!cycleClock_) {                                       // repli legacy (pas de timeline)
        syncAudioFromCpu();
        mixSegment(st, ym, frames, sampleRate, dmaGain);
        return;
    }
    if (frameCycles <= 0) frameCycles = 1;
    uint32_t pos = 0;
    for (const DmaEvent& e : events_) {
        uint32_t off = uint32_t(int64_t(e.cycle) * frames / frameCycles);
        if (off > frames) off = frames;
        // Le segment qui PRÉCÈDE l'événement ne consomme que les octets capturés avant
        // lui (e.capW) : pas d'octets post-PLAY rendus en avance, pas d'octets du
        // nouveau MODE lus à l'ancien format. Un reliquat non consommé (rendu un peu
        // plus lent que le DAC) reste dans l'anneau et passe au segment suivant.
        capLimit_ = e.capW;
        if (off > pos) { mixSegment(st + 2 * pos, ym + pos, off - pos, sampleRate, dmaGain); pos = off; }
        if (e.kind == 0) {                                    // PLAY start (0→1) : recale la cadence
            aPlaying_ = true; aMode_ = e.mode;
            aPhase_ = 0.0; aHaveCur_ = false;                 // ≙ DmaInitSample : 1er octet frais
        } else if (e.kind == 1) {                             // STOP (effacement CPU du bit PLAY) :
            aPlaying_ = false;                                // le flux capturé résiduel se draine
        } else {                                              // MODE : nouvelle cadence / mono-stéréo
            aMode_ = e.mode;
        }
    }
    capLimit_ = UINT64_MAX;                                   // dernier segment : tout le flux
    if (pos < frames) mixSegment(st + 2 * pos, ym + pos, frames - pos, sampleRate, dmaGain);
    events_.clear();
}
