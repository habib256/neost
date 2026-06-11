// =============================================================================
//  MidiAcia.cpp — ACIA 6850 MIDI avec bouclage OUT→IN (cf. MidiAcia.hpp).
//
//  Modèle aligné sur l'ACIA clavier (Ikbd.cpp) et midi.c/acia.c d'Hatari.
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

uint8_t MidiAcia::read8(uint32_t addr) {
    if ((addr & 2) == 0) {
        // $FFFC04 : statut. TDRE = 1 au repos ; RDRF si un octet a été livré au RDR
        // (bouclage MIDI IN) et pas encore lu ; IRQ si une cause RX ou TX est active.
        uint8_t s = tdre_ ? ACIA_TDRE : 0;
        if (rdrf_)       s |= ACIA_RDRF;
        if (irqActive()) s |= ACIA_IRQ;
        return s;
    }
    // $FFFC06 : donnée reçue. À vide, le RDR conserve le dernier octet reçu (RDR
    // persistant, cf. Hatari acia.c). Sinon : consomme l'octet livré (efface RDRF)
    // et livre aussitôt le suivant éventuel (le MIDI n'a pas besoin de cadence).
    if (!rdrf_)
        return rdr_;
    const uint8_t b = rdr_;
    rdrf_ = false;
    if (!rx_.empty()) {              // octet suivant du bouclage → devient le RDR courant
        rdr_  = rx_.front();
        rx_.pop_front();
        rdrf_ = true;
    }
    raiseIfReady();
    return b;
}

void MidiAcia::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC04 : registre de contrôle. Bits 5-6 = contrôle émetteur : 01 arme
        // l'IRQ d'émission (TIE) ; hors TIE, TDRE reste câblé à 1.
        control_ = v;
        txEnableInt_ = ((v & 0x60) == 0x20);
        if (!txEnableInt_)
            tdre_ = true;
        if ((v & 0x03) == 0x03) {
            // MASTER RESET 6850 (CR bits 0-1 = 11, cf. acia.c ACIA_MasterReset) :
            // le SR retombe à TDRE seul (RDRF effacé, l'octet du RDR est perdu) et la
            // ligne IRQ est relâchée. La file de réception n'est PAS purgée (les
            // octets « en transit » arrivent quand même après le reset).
            rdrf_ = false;
            tdre_ = true;
        }
        raiseIfReady();
        return;
    }
    // $FFFC06 : octet émis sur MIDI OUT → bouclé aussitôt sur MIDI IN (câble OUT→IN).
    // S'il n'y a pas d'octet déjà dans le RDR, il y est livré directement ; sinon il
    // attend dans la file (RDR persistant, lecture FIFO).
    if (!rdrf_) {
        rdr_  = v;
        rdrf_ = true;
    } else {
        rx_.push_back(v);
    }
    raiseIfReady();
}

bool MidiAcia::irqActive() const {
    // Cause d'IRQ de l'ACIA 6850 (cf. midi.c MIDI_UpdateIRQ) : RX = octet livré
    // (RDRF) et RX int activé (RIE, bit7) ; TX = registre d'émission vide (TDRE)
    // et IRQ d'émission armée (TIE, CR bits5-6 = 01).
    return (rdrf_ && (control_ & 0x80)) || (txEnableInt_ && tdre_);
}

void MidiAcia::raiseIfReady() {
    const bool active = irqActive();
    mfp_.setAciaLineMidi(active);
    if (active) mfp_.raise(Mfp::SRC_ACIA);
}
