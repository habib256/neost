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

int Ikbd::cmdLength(uint8_t opcode) {
    // Table portée de Hatari ikbd.c KeyboardCommands[] : nombre TOTAL d'octets
    // (opcode inclus) que l'IKBD attend avant d'exécuter la commande. On ne
    // référence que les commandes que NeoST encadre ; les autres opcodes sont
    // traités comme des commandes mono-octet ignorées (NOP, cf. Hatari).
    switch (opcode) {
        case 0x07: return 2;   // MouseAction
        case 0x09: return 5;   // AbsMouseMode
        case 0x0A: return 3;   // MouseCursorKeycodes
        case 0x0B: return 3;   // SetMouseThreshold
        case 0x0C: return 3;   // SetMouseScale
        case 0x0E: return 6;   // SetInternalMousePos
        case 0x0F: return 1;   // SetYAxisDown
        case 0x10: return 1;   // SetYAxisUp
        case 0x12: return 1;   // DisableMouse
        case 0x14: return 1;   // JoystickAuto
        case 0x15: return 1;   // StopJoystick
        case 0x16: return 1;   // InterrogateJoystick
        case 0x17: return 2;   // SetJoystickMonitoring
        case 0x18: return 1;   // FireButton
        case 0x1A: return 1;   // DisableJoysticks
        case 0x1B: return 7;   // SetClock
        case 0x1C: return 1;   // ReadClock
        case 0x80: return 2;   // Reset
        default:   return 0;   // inconnu → mono-octet ignoré
    }
}

void Ikbd::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC00 : registre de contrôle (diviseur, format, RX int enable bit7).
        control_ = v;
        raiseIfReady();
        return;
    }
    // $FFFC02 : octet de commande envoyé à l'IKBD. Parseur multi-octets calqué
    // sur Hatari IKBD_RunKeyboardCommand : on accumule dans inBuf_ jusqu'à ce que
    // le nombre d'octets attendu (table KeyboardCommands[]) soit atteint, puis on
    // exécute la commande et on vide le tampon.
    if (cmdExpected_ == 0) {
        // Pas de commande en cours : ce premier octet est l'opcode.
        const int len = cmdLength(v);
        if (len <= 1) {
            // Mono-octet (ou opcode inconnu traité en NOP). On dispatche tout de suite.
            inBuf_[0] = v;
            inBufLen_ = 1;
            dispatchCommand();
            inBufLen_ = 0;
        } else {
            inBuf_[0] = v;
            inBufLen_ = 1;
            cmdExpected_ = len;
        }
        return;
    }
    // Commande en cours : on accumule les octets de paramètre.
    if (inBufLen_ < static_cast<int>(inBuf_.size()))
        inBuf_[inBufLen_] = v;
    ++inBufLen_;
    if (inBufLen_ >= cmdExpected_) {
        dispatchCommand();
        inBufLen_ = 0;
        cmdExpected_ = 0;
    }
}

void Ikbd::dispatchCommand() {
    const uint8_t opcode = inBuf_[0];
    switch (opcode) {
        case 0x80:
            // Reset 0x80,0x01 : l'IKBD fait son auto-test puis renvoie $F1 APRÈS
            // ~502000 cycles (valeur Hatari IKBD_RESET_CYCLES). Répondre
            // INSTANTANÉMENT casse les diagnostics qui arment l'IRQ ACIA puis
            // attendent la réponse : l'IRQ serait levée avant l'armement (donc
            // perdue) → « Keyboard not responding ». On diffère via
            // l'ordonnanceur ; à défaut (pas de scheduler), repli immédiat.
            if (inBuf_[1] == 0x01) {
                constexpr int64_t kIkbdResetCycles = 502000;
                if (sched_) sched_->schedule(Scheduler::IKBD, sched_->now() + kIkbdResetCycles);
                else        pushRx(0xF1);
            }
            break;
        case 0x16: {
            // $16 = « interroger les joysticks » : l'IKBD répond IMMÉDIATEMENT par
            // un paquet $FD + état joystick 0 + état joystick 1 (cf. Hatari ikbd.c
            // IKBD_Cmd_ReturnJoysticks). État neutre $00 par défaut (suffit à
            // éviter « J2 Joystick time-out ») ; sous fixture de bouclage, la sonde
            // reflète le port parallèle (cf. Machine) pour le test « Printer/
            // Joystick » complet.
            uint8_t joy0 = 0, joy1 = 0;
            if (joyProbe_) joyProbe_(joy0, joy1);
            pushRx(0xFD);
            pushRx(joy0);
            pushRx(joy1);
            break;
        }
        default:
            // Autres commandes (modes souris, axe Y, joystick auto, horloge…) :
            // simples no-op pour l'instant — le parseur a déjà consommé tous les
            // octets de paramètre. Implémentations dédiées dans des tâches suivantes.
            break;
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
