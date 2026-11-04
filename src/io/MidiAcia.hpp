// =============================================================================
//  MidiAcia.hpp — ACIA 6850 MIDI de l'Atari ST ($FFFC04 contrôle/statut,
//  $FFFC06 données), avec BOUCLAGE interne (connecteur MIDI OUT→IN).
//
//  Références de ce fichier, de MidiAcia.cpp et d'Ikbd.{hpp,cpp} :
//    « MC6850 § x p. n » : MC6850 ACIA (Motorola, DS9493R4, 1985), extern/datasheets/MC6850.pdf
//      (sha256 294a8b9f557d03a2a7f4d736d060244f39216dad2d5479d2e47954feb4f34655).
//      Page du PDF = page imprimée.
//    « acia.c » est le cœur générique d'Hatari. Son MIDI ne s'en sert pas : [D]
//    midi.c:10,13-16 @ f0736b24 traite l'octet entier et ignore la plupart des bits. Une citation d'acia.c
//    pour cette ACIA vaut donc par analogie avec le clavier d'Hatari.
//  [B] MC6850 § Register Select p. 6, Table 1 p. 7 : RS bas = contrôle/statut, RS haut = données.
//  [D] Hatari acia.c:72-73 @ f0736b24 : ces deux adresses sur le ST.
//
//  [D] Hatari acia.c:70-73 @ f0736b24 : le port MIDI ST est une seconde ACIA 6850, distincte
//  du clavier. Le diagnostic
//  « M MIDI » émet un octet sur MIDI OUT, active l'interruption de réception (RIE)
//  et attend l'IRQ ACIA ([D] GPIP4 : Hatari acia.c:369-370 @ f0736b24 ; [B] canal 6 : MC68901
//  Table 3-1 p. 3-1) prouvant qu'il a RELU l'octet
//  sur MIDI IN — ce qui suppose un câble de bouclage OUT→IN branché. NeoST émule
//  ce câble : tout octet achevé sur TX est ré-injecté dans le registre de
//  réception et, si RIE est armé, lève le canal 6 du MFP (comme l'ACIA clavier).
//
//  Émission MC6850 :
//  [B] MC6850 § Transmit p. 5-6, § Transmit Data Register p. 7 : TDR (une case) → TSR ; au repos,
//      transfert à moins d'un temps de bit de l'écriture, sinon à la fin du caractère en cours ;
//      TDRE remonte à ce transfert.
//  [B] MC6850 § Control Register p. 8 : formats 7/8 bits, parité et 1/2 stops ; diviseurs /1 /16 /64.
//  [D] Hatari acia.c:40-41,465 @ f0736b24 : horloge 500 kHz, 16 cycles CPU par tic, d'où
//      16/256/1024 cycles par bit. Aucun schéma de carte ST dans extern/datasheets.
//  Sortie par octet au dernier stop : choix d'implémentation, la ligne n'est pas modélisée.
//  [D] Hatari acia.c:917-918 @ f0736b24 : format capturé au chargement TSR.
//  ⚠ [B] MC6850 § Word Select Bits p. 8 : CR2-4 « not buffered and therefore become effective
//      immediately ». Un changement pendant un caractère le modifierait ; ici non : écart non corrigé.
//  [B] MC6850 § Transmitter Control Bits p. 8 : parmi les effets de CR5-6, TIE ne commande que
//      l'IRQ. [D] RTS n'est pas câblé sur ST : Hatari acia.c:123,436 @ f0736b24.
//  [D] Hatari acia.c:893-899 @ f0736b24 : les écritures prématurées écrasent TDR. La fiche (§ Transmit
//      p. 5) demande seulement d'attendre TDRE et ne décrit pas ce cas.
//  [B] MC6850 § Transmitter Control Bits p. 8 : CR6=CR5=1 émet un Break, IRQ d'émission coupée.
//  [D] Hatari acia.c:965-970 @ f0736b24 : Break n'agit qu'au repos, laisse finir TSR et retient TDR.
//  [F] Fiche muette sur le caractère en cours, et MiSTery gen_uart.v:296 @ a00deb5 force la ligne à 0
//      aussitôt. Tranché par : une capture de Tx Data sur un vrai 6850 pendant un caractère.
//  Sa libération reprend au prochain front du diviseur choisi, sans modéliser la ligne physique.
//
//  ⚠ Le câble est DÉBRANCHÉ par défaut (2026-08-21) : Cubase Lite (MROS) avec son
//  MIDI Thru ré-émet tout ce qu'il reçoit — bouclé, chaque octet sorti revenait et
//  repartait, larsen MIDI infini, « gel » au chargement d'un morceau. Sur un vrai ST
//  rien n'est branché par défaut ; la fiche de bouclage se pose comme celle du
//  RS-232 : setLoopback(true) (--loopback en headless, menu Machine dans le GUI).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>

#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"

class Mfp;

class MidiAcia {
public:
    explicit MidiAcia(Mfp& mfp) : mfp_(mfp) {}

    // Ordonnanceur : date les transferts TDR→TSR et la sortie au stop.
    void setScheduler(Scheduler* s) { sched_ = s; }

    // Pont MIDI RÉSEAU (MIDIMaze en ligne — extension NeoST). Sans sink, l'ACIA
    // boucle OUT→IN si la fiche de diagnostic est branchée.
    // Avec un sink posé, les octets MIDI OUT partent vers le RÉSEAU (l'anneau
    // MIDI) au lieu de reboucler, et les octets de l'anneau reviennent par
    // receiveExternal — exactement le câblage d'un anneau MIDI physique.
    void setMidiSink(std::function<void(uint8_t)> fn) { midiSink_ = std::move(fn); }
    // Variante DATÉE : reçoit aussi le cycle CPU d'émission — pour une sortie hôte qui
    // replace chaque octet à son instant ST réel (cf. audio/MidiOutHost, gigue de trame).
    void setMidiSinkTimed(std::function<void(uint8_t, int64_t)> fn) { midiSinkTimed_ = std::move(fn); }
    // Câble de bouclage MIDI OUT→IN (diagnostics). Config, hors save-state.
    void setLoopback(bool plugged) { loopback_ = plugged; }
    bool loopback() const { return loopback_; }
    bool midiNetworked() const { return static_cast<bool>(midiSink_) || static_cast<bool>(midiSinkTimed_); }
    // Injecte un octet reçu du réseau dans MIDI IN (comme le bouclage, mais depuis
    // l'extérieur). [B] MC6850 § Receive Interrupt Enable Bit p. 8 : lève l'IRQ ACIA si RIE est armé.
    void receiveExternal(uint8_t b);

    // --- Source hôte CADENCÉE (appareil MIDI branché sur la machine) --------------
    // `fn(b)` rend true et remplit b s'il y a un octet à faire entrer. L'ACIA
    // l'interroge sur l'horloge SÉRIE ([D] Hatari midi.c:44-48 @ f0736b24 : 2560 cycles =
    // 10 bits à 31250 bauds), pas une
    // fois par trame : c'est la différence entre 3125 o/s (un vrai câble) et
    // 2 octets/trame (~143 o/s en mono), plafond mesuré de l'ancienne injection.
    // [E] Cette source reste un câble MIDI physique 31250 bauds 8N1 (spécification MIDI 1.0, absente
    // d'extern/datasheets, non relue) : les formats
    // TX ne changent ni sa phase ni son débit ; décodage RX bit/parité hors modèle.
    // ⚠ [B] MC6850 § Counter Divide Select Bits et § Word Select Bits p. 8 : CR0-4 régissent aussi le
    // récepteur. Sous un autre réglage, un vrai 6850 décoderait autre chose ou lèverait FE/PE : écart
    // non corrigé.
    // [B] MC6850 § Receive Data Register p. 7 : RDR plein, le transfert depuis le registre à décalage
    // est inhibé. Le débordement redevient alors celui du matériel : si le ST ne lit pas assez
    // vite, c'est le 6850 qui perd l'octet neuf (pushRx), pas l'hôte qui retient.
    void setRxSource(std::function<bool(uint8_t&)> fn);
    bool rxSourced() const { return static_cast<bool>(rxSource_); }
    void onRxPace();                         // échéance MIDI_RX : un octet a fini d'entrer
    // [B] MC6850 § Receive p. 6, § Receive Data Register p. 7, § Receiver Overrun p. 8 :
    // receiveExternal livre un octet ACHEVÉ : seul RDR peut le retenir ; le
    // registre à décalage n'est pas une seconde case d'octets déjà reçus.
    // Le transport réseau peut retenir ses octets en amont tant que RDR est plein.
    bool rxCanAccept() const { return !rdrf_; }

    uint8_t read8(uint32_t addr);            // $FFFC04 statut / $FFFC06 données
    void    write8(uint32_t addr, uint8_t v);

    // Reset matériel — appelé par les resets CHAUD et FROID, comme chez Hatari
    // ([D] reset.c:111 @ f0736b24 ACIA_Reset + :124 Midi_Reset, tous deux dans Reset_ST()). Sans
    // lui, un séquenceur qui avait armé RIE/TIE laissait après un Ctrl-Alt-Del un
    // statut périmé sur $FFFC04 et un octet fantôme livrable sur $FFFC06.
    // ⚠ [B] MC6850 Pin Assignment p. 1, § Counter Divide Select Bits p. 8 : pas de broche de reset, seul
    // CR0=CR1=1 remet la puce à zéro. Un vrai ST garde donc ces registres jusqu'au master reset du
    // logiciel : ce « statut périmé » est le comportement de la fiche, et Ikbd::resetHw le conserve
    // déjà ([D] Hatari acia.c:330-347 @ f0736b24). Ici tout est effacé, héritage de [D] Hatari
    // midi.c:110-122 @ f0736b24 : écart non corrigé.
    void reset() {
        rxOverrun_ = ovrn_ = srRead_ = false;
        // [D] Hatari midi.c:116 @ f0736b24 : amorçage à 1. [F] Fiche muette sur RDR à la mise sous
        // tension. Tranché par : une lecture de $FFFC06 au démarrage d'un vrai ST.
        rdr_ = 1;
        rdrf_ = false;
        control_ = 0;
        txEnableInt_ = false;
        tdre_ = true;                        // [D] Hatari midi.c:114 @ f0736b24 : ACIA_SR_TX_EMPTY
        tdr_ = tsr_ = 0;
        tsrBusy_ = false;
        txLoadDue_ = txCompleteDue_ = -1;
        txClockPhase_ = sched_ ? sched_->liveNow() % txBitCycles() : 0;
        if (sched_) sched_->cancel(Scheduler::MIDI_TX);
        // L'horloge de réception, elle, ne s'arrête pas au reset : le câble continue
        // de porter des octets pendant qu'on remet la puce à zéro. Une échéance
        // existante garde sa phase ; sinon on la réarme depuis l'horloge vive.
        armRxPace();
        raiseIfReady();
    }

    // Échéance MIDI_TX : copie TDR→TSR ou livraison au stop, avec rattrapage daté.
    void    onTxEmpty();

    // Save-state : registre reçu et acquittement différé de l'overrun.
    void serialize(StateArchive& ar) {
        ar(rxOverrun_); ar(ovrn_); ar(srRead_);
        ar(rdr_);
        ar(rdrf_);
        ar(control_);
        ar(txEnableInt_);
        ar(tdre_);
        ar(tdr_); ar(tsr_); ar(tsrBusy_);
        ar(txClockPhase_);
        ar.check(txClockPhase_ >= 0 && txClockPhase_ < txBitCycles(), "MIDI TX divider phase");
        ar(txLoadDue_); ar(txCompleteDue_);
        ar.check(txLoadDue_ >= -1 && txCompleteDue_ >= -1
                 && txLoadDue_ <= INT64_MAX - 11264
                 && txCompleteDue_ <= INT64_MAX - 11264, "MIDI TX deadlines");
        ar.check(tsrBusy_ == (txCompleteDue_ >= 0), "MIDI TSR busy");
        ar.check(!tsrBusy_ || txLoadDue_ == -1, "MIDI TX phase");
        // L'horloge de réception est un ÉTAT DU CÂBLAGE, pas de la machine : la
        // source hôte (rxSource_) survit au chargement, mais le Scheduler vient de
        // restaurer SES échéances — celles de l'état sauvé. Un état sauvé SANS
        // appareil MIDI IN, rechargé pendant qu'un clavier est branché, laissait donc
        // MIDI_RX éteint pour toujours : plus rien ne tirait, entrée morte en silence
        // (et la reconnexion à 1 Hz ne la ranimait pas — l'appareil est toujours
        // « ouvert » côté hôte). On réarme si la source existe et que l'échéance ne
        // s'est pas restaurée ; si elle s'est restaurée, on lui laisse sa phase.
        if (ar.loading() && rxSource_ && sched_
            && sched_->cyclesUntil(Scheduler::MIDI_RX) < 0)
            armRxPace();
    }

private:
    int64_t txBitCycles() const;
    int64_t nextTxEdge(int64_t now) const;
    uint8_t txData(uint8_t data) const;
    void advanceTx(int64_t cycle);
    void armTx();
    void emitTx(uint8_t v, int64_t cycle);
    uint8_t tdr_ = 0, tsr_ = 0;
    bool tsrBusy_ = false;
    int64_t txLoadDue_ = -1, txCompleteDue_ = -1;
    // Phase du diviseur pour la reprise BREAK ; TSR actif est recadencé par CR0..1.
    // [D] Hatari acia.c:801-805 @ f0736b24 : changer CR0-1 redémarre le timer de bit.
    // [F] Fiche muette sur son compteur : il pourrait tourner librement. Tranché par : une capture
    // de Tx Data autour d'une écriture de CR sur un vrai 6850.
    int64_t txClockPhase_ = 0;
    // [B] MC6850 § Interrupt Request p. 6 : l'IRQ suit la cause et son autorisation.
    void raiseIfReady(int64_t when = -1);                     // lève le canal 6 du MFP si cause RX ou TX

    Mfp&    mfp_;
    Scheduler* sched_ = nullptr;
    // Écarts à la fiche non corrigés, code dans MidiAcia.cpp ; le clavier a les mêmes (Ikbd.cpp) :
    // ⚠ [B] MC6850 § Receiver Overrun p. 8-9 : « The RDRF bit remains set until the Overrun is reset ».
    //   read8 efface RDRF dès la lecture du caractère valide, comme [D] Hatari acia.c:858 @ f0736b24 ;
    //   l'IRQ d'overrun retombe avec lui, comme [D] acia.c:721-724,869-873 @ f0736b24 : écart non corrigé.
    // ⚠ [B] MC6850 § Interrupt Request p. 6 : TDRE inhibé « being maintained in the Reset condition ». write8 pose
    //   tdre_, comme [D] acia.c:681 @ f0736b24 et gen_uart.v:273,301 @ a00deb5b : écart non corrigé.
    // ⚠ [B] MC6850 § Counter Divide Select Bits p. 8 : le master reset initialise le récepteur.
    //   onRxPace livre l'octet déjà en vol, y compris sous CR0-1=11 : écart non corrigé.
    // ⚠ [B] MC6850 § Receive p. 6, § Word Select Bits p. 8, Table 1 p. 7 : en 7 bits + parité, D7 reçu
    //   vaut 0 ; PE et FE signalent les erreurs. pushRx stocke l'octet entier, bits 4 et 6 toujours à 0 :
    //   écart non corrigé.
    // [B] MC6850 § Receive Data Register p. 7 : la lecture de RDR n'est pas destructive.
    uint8_t rdr_ = 1;                        // RDR persistant ; [D] amorçage midi.c:116 @ f0736b24
    bool rdrf_ = false;
    // [D] Hatari acia.c:1103-1123 @ f0736b24 : perte constatée au stop, OVRN différé.
    // [B] MC6850 § Receiver Overrun p. 8 : la condition naît au milieu du dernier bit du second caractère.
    // [F] Hatari lit « dernier bit » comme le stop (acia.c:1071-1075 @ f0736b24) ; NeoST date la fin de
    // trame. Tranché par : un overrun provoqué sur un vrai 6850, IRQ à l'oscilloscope.
    bool rxOverrun_ = false;
    // [B] MC6850 § Interrupt Request p. 6 : acquitté par SR puis RDR ou master reset. § Receiver Overrun
    // p. 9 ne cite que la lecture de RDR. [D] Hatari acia.c:861-864 @ f0736b24 exige SR puis RDR.
    // [F] Tranché par : deux lectures de RDR sans lecture de SR après un overrun sur un vrai 6850.
    bool ovrn_ = false;
    bool srRead_ = false;                     // [D] Hatari acia.c:768 @ f0736b24
    uint8_t control_ = 0;                    // [B] MC6850 Table 1 p. 7 : bit7 = RX int enable
    bool    txEnableInt_ = false;            // [B] MC6850 § Transmitter Control Bits p. 8 : CR6=0, CR5=1
    bool    tdre_ = true;                    // [B] MC6850 § Status Register p. 8 : TDR libre
    // Pont MIDI réseau (cf. setMidiSink). Non sérialisé (liaison frontend).
    std::function<void(uint8_t)> midiSink_;
    std::function<void(uint8_t, int64_t)> midiSinkTimed_;
    bool loopback_ = false;                  // fiche de bouclage OUT→IN branchée ?
    // Source hôte cadencée (cf. setRxSource). Non sérialisée (liaison frontend).
    std::function<bool(uint8_t&)> rxSource_;
    void armRxPace();                        // (ré)arme l'échéance MIDI_RX si source
    void pushRx(uint8_t v, int64_t when = -1);                  // livre un octet achevé dans RDR
};
