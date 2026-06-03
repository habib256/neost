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

// Horloge interne IKBD : l'octet est-il un nombre BCD valide ? (cf. Hatari
// IKBD_BCD_Check) — SetClock ($1B) ignore les octets non BCD et garde les autres.
static bool bcdCheck(uint8_t v) {
    return ((v & 0x0f) <= 0x09) && ((v & 0xf0) <= 0x90);
}

// Ré-ajuste un octet en BCD après +1 (cf. Hatari IKBD_BCD_Adjust, instruction DAA
// du HD6301) : propage les quartets > 9 vers le quartet supérieur.
static uint8_t bcdAdjust(uint8_t v) {
    if ((v & 0x0f) > 0x09) v += 0x06;
    if ((v & 0xf0) > 0x90) v += 0x60;
    return v;
}

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
                // L'IKBD repart de ses défauts (cf. Hatari IKBD_Reset, l.565-569) :
                // souris RELATIVE, seuils 1, pas d'échelle, axe Y vers le haut,
                // état de bouton émis remis à zéro.
                mouseMode_     = REL;
                xThreshold_    = yThreshold_ = 1;
                xScale_        = yScale_ = 0;
                yAxis_         = 1;
                mouseAction_   = 0;
                keyCodeDeltaX_ = keyCodeDeltaY_ = 1;
                bOldL_         = bOldR_ = false;
                // L'horloge ($1B/$1C) est CONSERVÉE (reset à chaud, cf. Hatari).
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
        case 0x0D:
            // $0D = INTERROGATE MOUSE POSITION (cf. Hatari IKBD_Cmd_ReadAbsMousePos) :
            // paquet $F7 + boutons + X/Y. État de bouton = dernier connu (prevL_/R_).
            sendAbsMousePos(prevL_, prevR_);
            break;
        case 0x07:
            // $07 = SET MOUSE BUTTON ACTION (cf. Hatari IKBD_Cmd_MouseAction) :
            // bit0 = report position abs à l'appui, bit1 = au relâchement, bit2 =
            // boutons remontés comme scancodes. Remet le cache de boutons abs à la
            // valeur « rien à signaler » (ABS_PREVBUTTONS = 0x0A).
            mouseAction_   = inBuf_[1];
            prevAbsButtons_ = 0x0A;
            break;
        case 0x0A:
            // $0A = SET MOUSE KEYCODE MODE (cf. Hatari IKBD_Cmd_MouseCursorKeycodes) :
            // les mouvements deviennent des flèches clavier ; octets 1/2 = pas X/Y.
            mouseMode_     = CURSOR;
            keyCodeDeltaX_ = inBuf_[1] ? inBuf_[1] : 1;
            keyCodeDeltaY_ = inBuf_[2] ? inBuf_[2] : 1;
            break;
        case 0x12:
            // $12 = DISABLE MOUSE (cf. Hatari IKBD_Cmd_TurnMouseOff) : plus aucun
            // paquet souris émis jusqu'au prochain mode souris.
            mouseMode_ = OFF;
            break;
        case 0x1B:
            // $1B = SET CLOCK (cf. Hatari IKBD_Cmd_SetClock) : 6 octets BCD
            // (YY MM DD hh mm ss). Les octets non BCD sont ignorés, les autres pris.
            for (int i = 1; i <= 6; ++i)
                if (bcdCheck(inBuf_[i])) clock_[i - 1] = inBuf_[i];
            break;
        case 0x1C:
            // $1C = INTERROGATE CLOCK (cf. Hatari IKBD_Cmd_ReadClock) : paquet
            // $FC + les 6 octets BCD de l'horloge.
            pushRx(0xFC);
            for (int i = 0; i < 6; ++i) pushRx(clock_[i]);
            break;
        case 0x0E:
            // $0E = LOAD MOUSE POSITION (cf. Hatari IKBD_Cmd_SetInternalMousePos) :
            // octet 1 = filler ; X = octets 2/3 ; Y = octets 4/5 (système mis à
            // l'échelle ; aucun clamp ici, comme Hatari).
            absX_ = uint16_t((inBuf_[2] << 8) | inBuf_[3]);
            absY_ = uint16_t((inBuf_[4] << 8) | inBuf_[5]);
            break;
        case 0x0B:
            // $0B = SET MOUSE THRESHOLD (cf. Hatari IKBD_Cmd_SetMouseThreshold) :
            // seuils X/Y (octets 1/2) d'émission du paquet relatif. En deçà, le
            // mouvement n'est pas remonté → le jeu filtre les micro-déplacements.
            xThreshold_ = inBuf_[1];
            yThreshold_ = inBuf_[2];
            break;
        case 0x0C:
            // $0C = SET MOUSE SCALE (cf. Hatari IKBD_Cmd_SetMouseScale) : facteur
            // d'échelle X/Y (octets 1/2), appliqué à l'accumulation en mode ABSOLU
            // si > 1. Sans effet sur le paquet relatif, comme Hatari.
            xScale_ = inBuf_[1];
            yScale_ = inBuf_[2];
            break;
        case 0x0F:
            // $0F = SET Y AXIS DOWN (cf. Hatari IKBD_Cmd_SetYAxisDown) : origine Y
            // en bas → le signe du Δy émis (et l'accumulation absolue) est inversé.
            yAxis_ = -1;
            break;
        case 0x10:
            // $10 = SET Y AXIS UP (cf. Hatari IKBD_Cmd_SetYAxisUp) : origine Y en
            // haut (défaut de reset).
            yAxis_ = 1;
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
            // éviter « J2 Joystick time-out ») ; on amorce avec l'état hôte
            // (manette/clavier du frontend), puis sous fixture de bouclage la sonde
            // l'écrase pour refléter le port parallèle (cf. Machine) — test
            // « Printer/Joystick » complet.
            uint8_t joy0 = hostJoy_[0], joy1 = hostJoy_[1];
            if (joyProbe_) joyProbe_(joy0, joy1);
            pushRx(0xFD);
            pushRx(joy0);
            pushRx(joy1);
            break;
        }
        default:
            // Opcodes restants (pause/reprise transmission, chargement mémoire
            // contrôleur, exécution programme custom…) : no-op — le parseur a déjà
            // consommé les octets de paramètre.
            break;
    }
}

void Ikbd::sendAutoJoysticks() {
    // Émet un paquet par manette dont l'état a changé depuis la dernière fois
    // (cf. Hatari IKBD_SendAutoJoysticks) : $FE = joystick 0 / souris, $FF =
    // joystick 1. Amorcé avec l'état hôte ; la sonde l'écrase sous fixture de bouclage.
    uint8_t joy0 = hostJoy_[0], joy1 = hostJoy_[1];
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

void Ikbd::onVbl(int64_t vblMicro) {
    // 1) Avance l'horloge interne IKBD ($1B/$1C) du temps d'une trame.
    updateClock(vblMicro);
    // 2) Report joystick auto : à chaque trame, on émet spontanément l'état des
    //    manettes qui ont changé. No-op strict hors mode auto (JOY_OFF par défaut),
    //    donc aucun impact sur le boot EmuTOS ni les cartes de diagnostic (qui
    //    interrogent en polled via $16).
    if (joyMode_ == JOY_AUTO)
        sendAutoJoysticks();
}

void Ikbd::keyEvent(uint8_t scancode, bool pressed) {
    // Make à l'appui, break (make | 0x80) au relâchement.
    pushRx(pressed ? scancode : uint8_t(scancode | 0x80));
}

void Ikbd::mouseEvent(int dx, int dy, bool left, bool right) {
    // Ordre calqué sur Hatari IKBD_SendAutoKeyboardCommands : d'ABORD le reporting
    // lié à MouseAction ($07), qui compare l'état courant à l'ancien (bOldL_/bOldR_),
    // PUIS le traitement propre au mode souris. bOldL_/bOldR_ ne sont mis à jour
    // qu'après — ainsi un même front de bouton peut produire à la fois un scancode
    // d'action ET un paquet de mode (comme sur le vrai IKBD).
    sendOnMouseAction(left, right);

    if (mouseMode_ == OFF) {
        // Souris désactivée ($12) : aucun paquet, on retient juste l'état bouton.
        bOldL_ = left; bOldR_ = right;
        return;
    }

    if (mouseMode_ == ABS) {
        // Mode absolu (cf. Hatari IKBD_UpdateInternalMousePosition, AUTOMODE_MOUSEABS) :
        // on accumule Δ dans la position courante en la bornant à [0, Max], en
        // appliquant l'échelle ($0C, si > 1) et le signe d'axe Y ($0F/$10), et on
        // retient l'état des boutons. Aucun paquet $F8 n'est émis ; l'hôte lira la
        // position via $0D (ou via le report d'action ci-dessus).
        const int sx = (xScale_ > 1) ? dx * xScale_ : dx;
        const int sy = (yScale_ > 1) ? dy * yAxis_ * yScale_ : dy * yAxis_;
        int x = static_cast<int>(absX_) + sx;
        int y = static_cast<int>(absY_) + sy;
        if (x < 0) x = 0; else if (x > absMaxX_) x = absMaxX_;
        if (y < 0) y = 0; else if (y > absMaxY_) y = absMaxY_;
        absX_ = static_cast<uint16_t>(x);
        absY_ = static_cast<uint16_t>(y);
        prevL_ = left; prevR_ = right;
        bOldL_ = left; bOldR_ = right;
        return;
    }

    if (mouseMode_ == CURSOR) {
        // Mode curseur-clavier ($0A) : le Δ sort en pressions de flèches.
        sendCursorKeys(dx, dy, left, right);
        bOldL_ = left; bOldR_ = right;
        return;
    }

    // Mode relatif (port fidèle de Hatari IKBD_SendRelMousePacket, l.1382-1422) :
    // on draine le Δ de cette trame en paquets $F8 de 3 octets. Un paquet n'est
    // émis QUE si le Δ borné à [-128,127] atteint le seuil EN VALEUR ABSOLUE, OU
    // si un bouton a changé depuis le dernier paquet — cette seconde condition est
    // ce qui fait remonter un appui/relâchement SANS mouvement (boutons de Vroom).
    // Les gros Δ sortent en plusieurs paquets ; le signe de Δy suit yAxis_ ($0F/$10)
    // mais la soustraction de drain utilise le Δy non signé (comme Hatari). Le
    // reliquat sous le seuil est abandonné (Hatari écrase Δ à la trame suivante).
    int dX = dx, dY = dy;
    for (int guard = 0; guard < 16; ++guard) {
        int bx = dX; if (bx > 127) bx = 127; else if (bx < -128) bx = -128;
        int by = dY; if (by > 127) by = 127; else if (by < -128) by = -128;
        const bool overThr =
            (bx < 0 && bx <= -xThreshold_) || (bx > 0 && bx >= xThreshold_) ||
            (by < 0 && by <= -yThreshold_) || (by > 0 && by >= yThreshold_);
        const bool btnChanged = (bOldL_ != left) || (bOldR_ != right);
        if (!overThr && !btnChanged) break;
        pushRx(uint8_t(0xF8 | (right ? 0x01 : 0) | (left ? 0x02 : 0)));
        pushRx(static_cast<uint8_t>(static_cast<int8_t>(bx)));
        pushRx(static_cast<uint8_t>(static_cast<int8_t>(by * yAxis_)));
        dX -= bx;
        dY -= by;
        bOldL_ = left;
        bOldR_ = right;
    }
}

void Ikbd::sendOnMouseAction(bool left, bool right) {
    // Port de Hatari IKBD_SendOnMouseAction. Émis quel que soit le mode souris.
    if (mouseAction_ & 0x4) {
        // Boutons remontés comme scancodes touche (0x74 gauche / 0x75 droit ;
        // |0x80 au relâchement). Les bits 0/1 sont ignorés quand le bit2 est mis.
        if (left && !bOldL_)       pushRx(0x74);
        else if (!left && bOldL_)  pushRx(0x74 | 0x80);
        if (right && !bOldR_)      pushRx(0x75);
        else if (!right && bOldR_) pushRx(0x75 | 0x80);
        return;
    }
    if (mouseAction_ & 0x3) {
        // Report de la position absolue à l'appui (bit0) / au relâchement (bit1) ;
        // on pré-positionne le cache de boutons abs comme le fait Hatari, puis on
        // émet le paquet $F7 — uniquement en mode absolu.
        bool report = false;
        if (mouseAction_ & 0x1) {
            if (left  && !bOldL_) { report = true; prevAbsButtons_ = (prevAbsButtons_ & ~0x04) | 0x02; }
            if (right && !bOldR_) { report = true; prevAbsButtons_ = (prevAbsButtons_ & ~0x01) | 0x08; }
        }
        if (mouseAction_ & 0x2) {
            if (!left  && bOldL_) { report = true; prevAbsButtons_ = (prevAbsButtons_ & ~0x08) | 0x01; }
            if (!right && bOldR_) { report = true; prevAbsButtons_ = (prevAbsButtons_ & ~0x02) | 0x04; }
        }
        if (report && mouseMode_ == ABS)
            sendAbsMousePos(left, right);
    }
}

void Ikbd::sendAbsMousePos(bool curL, bool curR) {
    // Port de Hatari IKBD_Cmd_ReadAbsMousePos : nibble boutons (droite-bas=0x01 /
    // droite-haut=0x02 / gauche-bas=0x04 / gauche-haut=0x08), masqué par ce qui a
    // déjà été signalé (prevAbsButtons_), puis position X/Y sur 16 bits.
    uint8_t buttons = uint8_t((curR ? 0x01 : 0x02) | (curL ? 0x04 : 0x08));
    const uint8_t prev = prevAbsButtons_;
    prevAbsButtons_ = buttons;
    buttons &= uint8_t(~prev);
    pushRx(0xF7);
    pushRx(buttons);
    pushRx(uint8_t(absX_ >> 8));
    pushRx(uint8_t(absX_ & 0xFF));
    pushRx(uint8_t(absY_ >> 8));
    pushRx(uint8_t(absY_ & 0xFF));
}

void Ikbd::sendCursorKeys(int dx, int dy, bool left, bool right) {
    // Port de Hatari IKBD_SendCursorMousePacket : convertit le Δ en pressions de
    // flèches (make immédiatement suivi de break) par pas de keyCodeDelta, plus les
    // boutons comme touches (0x74/0x75). Borné à 10 itérations (le Δ hôte peut être
    // bien plus grossier qu'un déplacement ST ; le reliquat est abandonné).
    int dX = dx, dY = dy;
    for (int i = 0; i < 10 &&
                    (dX != 0 || dY != 0 || bOldL_ != left || bOldR_ != right); ++i) {
        if (dX != 0) {
            if (dX <= -keyCodeDeltaX_) { pushRx(75); pushRx(75 | 0x80); dX += keyCodeDeltaX_; }  // gauche
            if (dX >=  keyCodeDeltaX_) { pushRx(77); pushRx(77 | 0x80); dX -= keyCodeDeltaX_; }  // droite
        }
        if (dY != 0) {
            if (dY <= -keyCodeDeltaY_) { pushRx(72); pushRx(72 | 0x80); dY += keyCodeDeltaY_; }  // haut
            if (dY >=  keyCodeDeltaY_) { pushRx(80); pushRx(80 | 0x80); dY -= keyCodeDeltaY_; }  // bas
        }
        if (left && !bOldL_)       pushRx(0x74);
        else if (!left && bOldL_)  pushRx(0x74 | 0x80);
        if (right && !bOldR_)      pushRx(0x75);
        else if (!right && bOldR_) pushRx(0x75 | 0x80);
        bOldL_ = left;
        bOldR_ = right;
    }
}

void Ikbd::updateClock(int64_t vblMicro) {
    // Port de Hatari IKBD_UpdateClockOnVBL : on cumule la durée d'une trame ; chaque
    // seconde écoulée incrémente l'horloge BCD (YY MM DD hh mm ss) avec la même
    // logique de propagation/retenue (DAA) que la ROM du HD6301, bornée par le
    // nombre de jours du mois (et l'année bissextile pour février).
    clockMicro_ += vblMicro;
    if (clockMicro_ < 1000000) return;
    clockMicro_ -= 1000000;

    static const uint8_t valMax[6]  = {0xFF, 0x13, 0x00, 0x24, 0x60, 0x60};
    static const uint8_t dayMax[18] = {0x32,0x29,0x32,0x31,0x32,0x31,0x32,0x32,0x31,
                                       0,0,0,0,0,0, 0x32,0x31,0x32};
    for (int i = 5; i >= 0; --i) {
        const uint8_t val = bcdAdjust(uint8_t(clock_[i] + 1));
        uint8_t max;
        if (i != 2) {
            max = valMax[i];
        } else {                              // jour : dépend du mois (et bissextile)
            uint8_t month = clock_[1];
            if (month > 0x12) month = 0x12;   // garde-fou (cf. Hatari)
            if (month < 0x01) month = 0x01;   // NeoST : évite l'index -1 (mois non posé)
            max = dayMax[month - 1];
            if (clock_[1] == 2) {             // février : test bissextile (logique ROM)
                uint8_t year = clock_[0];
                if (year & 0x10) year += 0x0a;
                if ((year & 0x03) == 0) max = 0x30;
            }
        }
        if (val != max) { clock_[i] = val; break; }
        else if (i == 1 || i == 2) clock_[i] = 1;   // jour/mois démarrent à 1
        else                       clock_[i] = 0;   // heure/minute/seconde à 0
    }
}

void Ikbd::pushRx(uint8_t b) {
    rx_.push_back(b);
    raiseIfReady();
}

void Ikbd::raiseIfReady() {
    // L'ACIA active sa ligne d'IRQ (RDRF + RX int activé). On la publie sur GPIP4
    // (lue par _int_acia pour vider l'ACIA) ET on déclenche le canal 6 du MFP.
    const bool active = !rx_.empty() && (control_ & 0x80);
    mfp_.setAciaLineKbd(active);
    if (active)
        mfp_.raise(Mfp::SRC_ACIA);
}
