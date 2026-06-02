// =============================================================================
//  Ikbd.cpp — ACIA 6850 + file de scancodes IKBD.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>

// Bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_IRQ  = 0x80,   // ligne d'interruption (vers GPIP4 du MFP)
};

uint8_t Ikbd::read8(uint32_t addr) {
    if ((addr & 2) == 0) {
        // $FFFC00 : statut. TX toujours prêt ; RX plein si la file n'est pas vide.
        uint8_t s = ACIA_TDRE;
        if (!rx_.empty()) {
            s |= ACIA_RDRF;
            if (control_ & 0x80) s |= ACIA_IRQ;   // IRQ visible si RX int activé (RIE)
        }
        return s;
    }
    // $FFFC02 : lecture de la donnée → consomme un octet (efface RDRF).
    if (rx_.empty()) return 0x00;
    const uint8_t b = rx_.front();
    rx_.pop_front();
    raiseIfReady();                  // octet suivant éventuel → ré-arme l'IRQ
    return b;
}

void Ikbd::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC00 : registre de contrôle (diviseur, format, RX int enable bit7).
        control_ = v;
        raiseIfReady();
        return;
    }
    // $FFFC02 : octet de commande envoyé à l'IKBD. On gère le reset 0x80,0x01.
    if (cmd0_ == 0x80 && v == 0x01) {
        // L'IKBD fait son auto-test puis renvoie $F1 APRÈS ~502000 cycles (valeur
        // Hatari IKBD_RESET_CYCLES). Répondre INSTANTANÉMENT casse les diagnostics
        // qui arment l'IRQ ACIA puis attendent la réponse : l'IRQ serait levée
        // avant l'armement (donc perdue) → « Keyboard not responding ». On diffère
        // donc via l'ordonnanceur ; à défaut (pas de scheduler), repli immédiat.
        constexpr int64_t kIkbdResetCycles = 502000;
        if (sched_) sched_->schedule(Scheduler::IKBD, sched_->now() + kIkbdResetCycles);
        else        pushRx(0xF1);
        cmd0_ = 0;
    } else {
        cmd0_ = v;                   // autres commandes : ignorées (modes souris, etc.)
    }
}

void Ikbd::keyEvent(uint8_t scancode, bool pressed) {
    // Make à l'appui, break (make | 0x80) au relâchement.
    pushRx(pressed ? scancode : uint8_t(scancode | 0x80));
}

void Ikbd::mouseEvent(int dx, int dy, bool left, bool right) {
    // Paquet "position relative" : en-tête %11111000 + boutons (bit0=droit,
    // bit1=gauche), puis Δx et Δy signés sur 8 bits. Les en-têtes $F8-$FB ne
    // chevauchent aucun scancode (max ~$F2), d'où l'absence d'ambiguïté pour le
    // parseur IKBD d'EmuTOS qui lit ces flux entremêlés sur la même ACIA.
    auto clamp8 = [](int v) -> uint8_t {
        if (v < -128) v = -128; else if (v > 127) v = 127;
        return static_cast<uint8_t>(static_cast<int8_t>(v));
    };
    pushRx(uint8_t(0xF8 | (right ? 0x01 : 0) | (left ? 0x02 : 0)));
    pushRx(clamp8(dx));
    pushRx(clamp8(dy));
}

void Ikbd::pushRx(uint8_t b) {
    rx_.push_back(b);
    raiseIfReady();
}

void Ikbd::raiseIfReady() {
    // L'ACIA active sa ligne d'IRQ (RDRF + RX int activé). On la publie sur GPIP4
    // (lue par _int_acia pour vider l'ACIA) ET on déclenche le canal 6 du MFP.
    const bool active = !rx_.empty() && (control_ & 0x80);
    mfp_.setAciaLine(active);
    if (active)
        mfp_.raise(Mfp::SRC_ACIA);
}
