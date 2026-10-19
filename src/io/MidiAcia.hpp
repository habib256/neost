// =============================================================================
//  MidiAcia.hpp — ACIA 6850 MIDI de l'Atari ST ($FFFC04 contrôle/statut,
//  $FFFC06 données), avec BOUCLAGE interne (connecteur MIDI OUT→IN).
//
//  Le port MIDI ST est une seconde ACIA 6850, distincte du clavier. Le diagnostic
//  « M MIDI » émet un octet sur MIDI OUT, active l'interruption de réception (RIE)
//  et attend l'IRQ ACIA (canal 6 du MFP via GPIP4) prouvant qu'il a RELU l'octet
//  sur MIDI IN — ce qui suppose un câble de bouclage OUT→IN branché. NeoST émule
//  ce câble : tout octet achevé sur TX est ré-injecté dans le registre de
//  réception et, si RIE est armé, lève le canal 6 du MFP (comme l'ACIA clavier).
//
//  Émission MC6850 : TDR (une case) → TSR, formats 7/8 bits, parité et
//  1/2 stops, diviseurs /1 /16 /64 (16/256/1024 cycles par bit).
//  Sortie par octet au dernier stop ; format capturé au chargement TSR. TIE ne commande
//  que l'IRQ ; les écritures prématurées écrasent TDR (acia.c Write_TDR).
//  Break (CR bits5-6=11) laisse finir TSR et retient TDR ; sa libération
//  reprend au prochain front du diviseur choisi, sans modéliser la ligne physique.
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
    // l'extérieur). Lève l'IRQ ACIA si RIE est armé.
    void receiveExternal(uint8_t b);

    // --- Source hôte CADENCÉE (appareil MIDI branché sur la machine) --------------
    // `fn(b)` rend true et remplit b s'il y a un octet à faire entrer. L'ACIA
    // l'interroge sur l'horloge SÉRIE (2560 cycles = 10 bits à 31250 bauds), pas une
    // fois par trame : c'est la différence entre 3125 o/s (un vrai câble) et
    // 2 octets/trame (~143 o/s en mono), plafond mesuré de l'ancienne injection.
    // Cette source reste un câble MIDI physique 31250 bauds 8N1 : les formats
    // TX ne changent ni sa phase ni son débit ; décodage RX bit/parité hors modèle.
    // Le débordement redevient alors celui du matériel : si le ST ne lit pas assez
    // vite, c'est le 6850 qui perd l'octet neuf (pushRx), pas l'hôte qui retient.
    void setRxSource(std::function<bool(uint8_t&)> fn);
    bool rxSourced() const { return static_cast<bool>(rxSource_); }
    void onRxPace();                         // échéance MIDI_RX : un octet a fini d'entrer
    // receiveExternal livre un octet ACHEVÉ : seul RDR peut le retenir ; le
    // registre à décalage n'est pas une seconde case d'octets déjà reçus.
    // Le transport réseau peut retenir ses octets en amont tant que RDR est plein.
    bool rxCanAccept() const { return !rdrf_; }

    uint8_t read8(uint32_t addr);            // $FFFC04 statut / $FFFC06 données
    void    write8(uint32_t addr, uint8_t v);

    // Reset matériel — appelé par les resets CHAUD et FROID, comme chez Hatari
    // (reset.c:111 ACIA_Reset + :124 Midi_Reset, tous deux dans Reset_ST()). Sans
    // lui, un séquenceur qui avait armé RIE/TIE laissait après un Ctrl-Alt-Del un
    // statut périmé sur $FFFC04 et un octet fantôme livrable sur $FFFC06.
    void reset() {
        rxOverrun_ = ovrn_ = srRead_ = false;
        rdr_ = 1;                            // amorçage 1 (midi.c:110)
        rdrf_ = false;
        control_ = 0;
        txEnableInt_ = false;
        tdre_ = true;                        // ACIA_SR_TX_EMPTY
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
    int64_t txClockPhase_ = 0;
    void raiseIfReady(int64_t when = -1);                     // lève le canal 6 du MFP si cause RX ou TX

    Mfp&    mfp_;
    Scheduler* sched_ = nullptr;
    uint8_t rdr_ = 1;                        // RDR persistant ; amorçage midi.c Midi_Reset
    bool rdrf_ = false;
    bool rxOverrun_ = false;                  // perte constatée au stop, OVRN différé
    bool ovrn_ = false;                       // bit SR, acquitté par SR puis RDR
    bool srRead_ = false;
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    bool    txEnableInt_ = false;            // IRQ d'émission armée : CR bits5-6 = 01
    bool    tdre_ = true;                    // Transmit Data Register Empty (TDR libre)
    // Pont MIDI réseau (cf. setMidiSink). Non sérialisé (liaison frontend).
    std::function<void(uint8_t)> midiSink_;
    std::function<void(uint8_t, int64_t)> midiSinkTimed_;
    bool loopback_ = false;                  // fiche de bouclage OUT→IN branchée ?
    // Source hôte cadencée (cf. setRxSource). Non sérialisée (liaison frontend).
    std::function<bool(uint8_t&)> rxSource_;
    void armRxPace();                        // (ré)arme l'échéance MIDI_RX si source
    void pushRx(uint8_t v, int64_t when = -1);                  // livre un octet achevé dans RDR
};
