// =============================================================================
//  MidiAcia.cpp — ACIA 6850 MIDI avec bouclage OUT→IN (cf. MidiAcia.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/MidiAcia.hpp"
#include "io/Mfp.hpp"

// Bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_IRQ  = 0x80,   // ligne d'interruption (vers GPIP4 du MFP)
};

// Temps série d'un octet MIDI (trame de 10 bits à 31250 bauds) en cycles 68000 à
// 8 MHz : 8 000 000 × 10 / 31250 = 2560. C'est la cadence du re-remplissage de
// TDRE sous TIE (l'horloge MIDI est /16 d'un quartz 500 kHz, cf. Hatari midi.c).
static constexpr int64_t kMidiTxByteCycles = 2560;

uint8_t MidiAcia::read8(uint32_t addr) {
    if ((addr & 2) == 0) {
        // $FFFC04 : statut. TDRE = 1 au repos (0 pendant l'émission sous TIE) ;
        // RX plein si le bouclage a livré un octet ; IRQ si une cause RX ou TX
        // est active (cf. acia.c ACIA_UpdateIRQ).
        uint8_t s = tdre_ ? ACIA_TDRE : 0;
        if (!rx_.empty()) s |= ACIA_RDRF;
        if ((!rx_.empty() && (control_ & 0x80)) || (txEnableInt_ && tdre_))
            s |= ACIA_IRQ;
        return s;
    }
    // $FFFC06 : donnée reçue → consomme un octet (efface RDRF).
    if (rx_.empty()) return 0x00;
    const uint8_t b = rx_.front();
    rx_.pop_front();
    raiseIfReady();                  // octet suivant éventuel → ré-arme l'IRQ
    return b;
}

void MidiAcia::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC04 : contrôle. Bits 5-6 = contrôle émetteur (01 → TIE, cf.
        // ACIA_Write_CR) ; bits 0-1 = 11 → master reset (vide la file, SR à TDRE).
        control_ = v;
        txEnableInt_ = ((v & 0x60) == 0x20);
        if (!txEnableInt_) {                 // TIE coupé → émetteur réputé prêt
            tdre_ = true;
            if (sched_) sched_->cancel(Scheduler::MIDI_TX);
        }
        if ((v & 0x03) == 0x03) rx_.clear();
        raiseIfReady();
        return;
    }
    // $FFFC06 : octet émis sur MIDI OUT. Sous TIE, TDRE tombe (transmetteur
    // occupé) et se re-remplit ~1 octet MIDI plus tard (Scheduler::MIDI_TX →
    // onTxEmpty) : c'est l'IRQ qui cadence la sortie des séquenceurs. L'octet
    // est bouclé aussitôt sur MIDI IN (câble OUT→IN) — NeoST ne perd jamais
    // l'octet émis, TDRE ne sert qu'au statut et à l'IRQ TX.
    if (txEnableInt_ && sched_) {
        tdre_ = false;
        sched_->schedule(Scheduler::MIDI_TX, sched_->now() + kMidiTxByteCycles);
    }
    rx_.push_back(v);
    raiseIfReady();
}

void MidiAcia::onTxEmpty() {
    // Le registre d'émission s'est vidé (octet « parti » sur MIDI OUT) : TDRE
    // repasse à 1 → re-lève l'IRQ « transmetteur prêt » tant que TIE est armé.
    tdre_ = true;
    raiseIfReady();
}

void MidiAcia::raiseIfReady() {
    // L'ACIA active sa ligne d'IRQ dès qu'une cause RX (octet dispo + RIE) ou TX
    // (TDRE + TIE) est active → canal 6 du MFP via GPIP4 (cf. ACIA_UpdateIRQ).
    const bool active = (!rx_.empty() && (control_ & 0x80)) || (txEnableInt_ && tdre_);
    mfp_.setAciaLineMidi(active);
    if (active) mfp_.raise(Mfp::SRC_ACIA);
}
