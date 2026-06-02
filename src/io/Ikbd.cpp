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
        case 0x09:
            // $09 = SET ABSOLUTE MOUSE POSITION REPORTING (cf. Hatari
            // IKBD_Cmd_AbsMouseMode) : bascule en mode absolu ; les bornes MaxX/
            // MaxY sont inclusives (octets MSB/LSB). Plus de paquet $F8 émis.
            mouseMode_ = ABS;
            absMaxX_ = uint16_t((inBuf_[1] << 8) | inBuf_[2]);
            absMaxY_ = uint16_t((inBuf_[3] << 8) | inBuf_[4]);
            break;
        case 0x0D: {
            // $0D = INTERROGATE MOUSE POSITION (cf. Hatari IKBD_Cmd_ReadAbsMousePos) :
            // l'IKBD renvoie un paquet $F7 + boutons + X(MSB,LSB) + Y(MSB,LSB).
            // Nibble boutons : droite-bas=0x01 / droite-haut=0x02 / gauche-bas=0x04 /
            // gauche-haut=0x08, relatif à la dernière interrogation (on masque les
            // bits déjà signalés via prevAbsButtons_).
            uint8_t buttons = 0;
            buttons |= prevR_ ? 0x01 : 0x02;
            buttons |= prevL_ ? 0x04 : 0x08;
            const uint8_t prev = prevAbsButtons_;
            prevAbsButtons_ = buttons;
            buttons &= uint8_t(~prev);
            pushRx(0xF7);
            pushRx(buttons);
            pushRx(uint8_t(absX_ >> 8));
            pushRx(uint8_t(absX_ & 0xFF));
            pushRx(uint8_t(absY_ >> 8));
            pushRx(uint8_t(absY_ & 0xFF));
            break;
        }
        case 0x0E:
            // $0E = LOAD MOUSE POSITION (cf. Hatari IKBD_Cmd_SetInternalMousePos) :
            // octet 1 = filler ; X = octets 2/3 ; Y = octets 4/5 (système mis à
            // l'échelle ; aucun clamp ici, comme Hatari).
            absX_ = uint16_t((inBuf_[2] << 8) | inBuf_[3]);
            absY_ = uint16_t((inBuf_[4] << 8) | inBuf_[5]);
            break;
        case 0x14: {
            // $14 = SET JOYSTICK EVENT REPORTING (cf. Hatari IKBD_Cmd_ReturnJoystickAuto) :
            // bascule en mode auto. Cette commande RAZ l'état joystick mémorisé,
            // puis — hack des jeux Utopos/Double Bubble — émet AUSSITÔT un paquet
            // $FE/$FF si l'état courant diffère de l'état remis à zéro (sans
            // attendre la prochaine trame ni vérifier l'ACIA).
            joyMode_ = JOY_AUTO;
            prevJoy0_ = prevJoy1_ = 0;
            sendAutoJoysticks();
            break;
        }
        case 0x15:
            // $15 = SET JOYSTICK INTERROGATION MODE (cf. Hatari IKBD_Cmd_StopJoystick) :
            // retour au mode interrogation seule ($16). Plus de report spontané.
            joyMode_ = JOY_OFF;
            break;
        case 0x17:
            // $17 = SET JOYSTICK MONITORING (cf. Hatari IKBD_Cmd_SetJoystickMonitoring) :
            // octet 1 = taux d'échantillonnage en 1/100 s. Implémentation minimale :
            // on retient le mode et émet des paires état/feu depuis onVbl().
            joyMode_ = JOY_MONITOR;
            break;
        case 0x18:
            // $18 = SET FIRE BUTTON MONITORING : non implémenté (comme Hatari).
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

void Ikbd::sendAutoJoysticks() {
    // Émet un paquet par manette dont l'état a changé depuis la dernière fois
    // (cf. Hatari IKBD_SendAutoJoysticks) : $FE = joystick 0 / souris, $FF =
    // joystick 1. La sonde reflète le port parallèle sous fixture de bouclage.
    uint8_t joy0 = 0, joy1 = 0;
    if (joyProbe_) joyProbe_(joy0, joy1);
    if (joy0 != prevJoy0_) {
        pushRx(0xFE);
        pushRx(joy0);
        prevJoy0_ = joy0;
    }
    if (joy1 != prevJoy1_) {
        pushRx(0xFF);
        pushRx(joy1);
        prevJoy1_ = joy1;
    }
}

void Ikbd::onVbl() {
    // Report joystick auto : à chaque trame, on émet spontanément l'état des
    // manettes qui ont changé. No-op strict hors mode auto (JOY_OFF par défaut),
    // donc aucun impact sur le boot EmuTOS ni les cartes de diagnostic (qui
    // interrogent en polled via $16).
    if (joyMode_ == JOY_AUTO)
        sendAutoJoysticks();
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
    if (mouseMode_ == ABS) {
        // Mode absolu (cf. Hatari ikbd.c, AUTOMODE_MOUSEABS) : on accumule Δ dans
        // la position courante en la bornant à [0, Max], et on retient l'état des
        // boutons. Aucun paquet $F8 n'est émis ; l'hôte lira la position via $0D.
        int x = static_cast<int>(absX_) + dx;
        int y = static_cast<int>(absY_) + dy;
        if (x < 0) x = 0; else if (x > absMaxX_) x = absMaxX_;
        if (y < 0) y = 0; else if (y > absMaxY_) y = absMaxY_;
        absX_ = static_cast<uint16_t>(x);
        absY_ = static_cast<uint16_t>(y);
        prevL_ = left;
        prevR_ = right;
        return;
    }
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
