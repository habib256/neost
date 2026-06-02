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

uint8_t MidiAcia::read8(uint32_t addr) {
    if ((addr & 2) == 0) {
        // $FFFC04 : statut. TX toujours prêt ; RX plein si le bouclage a livré un octet.
        uint8_t s = ACIA_TDRE;
        if (!rx_.empty()) {
            s |= ACIA_RDRF;
            if (control_ & 0x80) s |= ACIA_IRQ;   // IRQ visible si RX int activé (RIE)
        }
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
        // $FFFC04 : contrôle. Bits0-1 = 11 → master reset de l'ACIA (vide la file).
        control_ = v;
        if ((v & 0x03) == 0x03) rx_.clear();
        raiseIfReady();
        return;
    }
    // $FFFC06 : octet émis sur MIDI OUT → bouclé aussitôt sur MIDI IN (câble OUT→IN).
    rx_.push_back(v);
    raiseIfReady();
}

void MidiAcia::raiseIfReady() {
    // Comme l'ACIA clavier : la ligne d'IRQ est active si un octet est dispo et que
    // l'interruption de réception est armée (RIE) → on lève le canal 6 du MFP (GPIP4).
    const bool active = !rx_.empty() && (control_ & 0x80);
    mfp_.setAciaLine(active);
    if (active) mfp_.raise(Mfp::SRC_ACIA);
}
