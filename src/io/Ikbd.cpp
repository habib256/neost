// =============================================================================
//  Ikbd.cpp — ACIA 6850 + file de scancodes IKBD.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <cstdlib>
#include <ctime>

// Bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_IRQ  = 0x80,   // ligne d'interruption (vers GPIP4 du MFP)
};

// Temps « série » d'un octet émis CPU → IKBD (~7812,5 bauds, trame de 10 bits) en
// cycles 68000 à 8 MHz. Ordre de grandeur de l'ACIA_CYCLES d'Hatari : sert à dater
// le re-remplissage de TDRE sous TIE (cf. onTxEmpty) — assez long pour ne pas
// noyer le CPU d'IRQ d'émission, assez court pour ne pas freiner l'envoi.
static constexpr int64_t kAciaTxByteCycles = 7200;

// CRC32 (poly IEEE 802.3, MSB-first) — port exact de Hatari utils.c (crc32_*). On
// reconnaît un programme 6301 custom à son CRC plutôt que d'émuler un vrai HD6301.
namespace {
void crc32Reset(uint32_t& crc) { crc = 0xFFFFFFFFu; }
void crc32AddByte(uint32_t& crc, uint8_t c) {
    for (int b = 0; b < 8; ++b) {
        const bool topC   = (c   & 0x80u)       != 0;
        const bool topCrc = (crc & 0x80000000u) != 0;
        crc = (topC ^ topCrc) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
        c = uint8_t(c << 1);
    }
}
// CRC des boot-stubs ($20) connus (tous gérés par CommonBoot). 0xbc0c206d sert aux
// deux variantes d'Audio Sculpture (départagées au CRC du prog principal en ExeMode).
const uint32_t kKnownLoadCrc[] = {
    0x2efb11b1u, 0xadb6b503u, 0x33c23cdfu, 0x9ad7fcdfu, 0xbc0c206du,
};
} // namespace

Ikbd::Ikbd(Mfp& mfp) : mfp_(mfp) {
    initClockFromHostTime();
}

void Ikbd::onResetResponse() {
    duringResetCriticalTime_ = false;
    mouseEnabledDuringReset_ = false;        // fin de la fenêtre (IKBD_InterruptHandler_ResetTimer)
    pushRx(0xF1);
}

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

void Ikbd::initClockFromHostTime() {
    const std::time_t now = std::time(nullptr);
    const std::tm* tm = std::localtime(&now);
    if (!tm) return;                                      // garde la base de secours

    auto bcd = [](int v) -> uint8_t {
        if (v < 0) v = 0;
        return uint8_t(((v / 10) % 10) << 4 | (v % 10));
    };

    clock_[0] = bcd((tm->tm_year + 1900) % 100);          // YY MM DD hh mm ss
    clock_[1] = bcd(tm->tm_mon + 1);
    clock_[2] = bcd(tm->tm_mday);
    clock_[3] = bcd(tm->tm_hour);
    clock_[4] = bcd(tm->tm_min);
    clock_[5] = bcd(tm->tm_sec);
    clockMicro_ = 0;
}

uint8_t Ikbd::read8(uint32_t addr) {
    if ((addr & 2) == 0) {
        // $FFFC00 : statut. TDRE = 1 au repos (0 pendant l'émission sous TIE) ;
        // RDRF si un octet a été LIVRÉ (cadence série, cf. onRxDeliver) et pas
        // encore lu ; IRQ si une cause RX ou TX est active. Sous PAUSE OUTPUT
        // ($13), la livraison est gelée → RDRF ne remonte plus (octets en file).
        uint8_t s = tdre_ ? ACIA_TDRE : 0;
        if (rdrf_)       s |= ACIA_RDRF;
        if (irqActive()) s |= ACIA_IRQ;
        return s;
    }
    // $FFFC02 : lecture de la donnée → consomme l'octet livré (efface RDRF) et
    // date la livraison du suivant (~1 octet série). À vide, le RDR conserve le
    // dernier octet reçu (cf. Hatari acia.c ACIA_Read_RDR).
    // NEOST_DEBUG_ACIA=1 : trace chaque lecture du data register.
    static const bool dbgRdr = std::getenv("NEOST_DEBUG_ACIA") != nullptr;
    if (!rdrf_) {
        if (dbgRdr)
            std::fprintf(stderr, "[acia] rd VIDE (rdr=$%02X) cyc=%lld\n",
                         rdr_, sched_ ? (long long)sched_->now() : 0LL);
        return rdr_;
    }
    const uint8_t b = rdr_;          // valeur AVANT que armRx ne livre la suivante
    rdrf_ = false;
    if (dbgRdr)
        std::fprintf(stderr, "[acia] rd $%02X reste=%zu cyc=%lld\n",
                     b, rx_.size(), sched_ ? (long long)sched_->now() : 0LL);
    armRx();                         // octet suivant éventuel → livraison datée
    raiseIfReady();
    return b;
}

int Ikbd::cmdLength(uint8_t opcode) {
    // Table portée de Hatari ikbd.c KeyboardCommands[] : nombre TOTAL d'octets
    // (opcode inclus) que l'IKBD attend avant d'exécuter la commande. On ne
    // référence que les commandes que NeoST encadre ; les autres opcodes sont
    // traités comme des commandes mono-octet ignorées (NOP, cf. Hatari).
    switch (opcode) {
        case 0x07: return 2;   // MouseAction
        case 0x08: return 1;   // RelMouseMode
        case 0x09: return 5;   // AbsMouseMode
        case 0x0A: return 3;   // MouseCursorKeycodes
        case 0x0B: return 3;   // SetMouseThreshold
        case 0x0C: return 3;   // SetMouseScale
        case 0x0D: return 1;   // ReadAbsMousePos
        case 0x0E: return 6;   // SetInternalMousePos
        case 0x0F: return 1;   // SetYAxisDown
        case 0x10: return 1;   // SetYAxisUp
        case 0x11: return 1;   // StartKeyboardTransfer (RESUME)
        case 0x12: return 1;   // DisableMouse
        case 0x13: return 1;   // StopKeyboardTransfer (PAUSE OUTPUT)
        case 0x14: return 1;   // JoystickAuto
        case 0x15: return 1;   // StopJoystick
        case 0x16: return 1;   // InterrogateJoystick
        case 0x17: return 2;   // SetJoystickMonitoring
        case 0x18: return 1;   // FireButton
        case 0x19: return 7;   // SetJoystickFireDuration (anti-désync : 6 octets de param)
        case 0x1A: return 1;   // DisableJoysticks
        case 0x1B: return 7;   // SetClock
        case 0x1C: return 1;   // ReadClock
        case 0x20: return 4;   // LoadMemory (en-tête $20 ADRMSB ADRLSB NUM ; NUM octets suivent)
        case 0x21: return 3;   // ReadMemory (anti-désync : 2 octets d'adresse)
        case 0x22: return 3;   // Execute (adresse de sous-routine custom)
        case 0x80: return 2;   // Reset
        // Commandes de RAPPORT d'état (bit7 mis, cf. table Hatari) : réponse
        // $F6 + 7 octets décrivant le réglage interrogé.
        case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C:
        case 0x8F: case 0x90: case 0x92: case 0x94: case 0x95: case 0x99:
        case 0x9A: return 1;
        default:   return 0;   // inconnu → mono-octet ignoré
    }
}

void Ikbd::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC00 : registre de contrôle (diviseur, format, bit7 = RX int enable).
        // Bits 5-6 = contrôle émetteur : 01 arme l'IRQ d'émission (TIE), cf. ACIA
        // 6850 et acia.c ACIA_Write_CR. Hors TIE, TDRE reste câblé à 1 : le modèle
        // d'émission ne s'active QUE quand un logiciel pilote l'IKBD par IRQ TX
        // (ex. jeu armant CR=$b6 au lieu de $96, type Hades Nebula).
        control_ = v;
        txEnableInt_ = ((v & 0x60) == 0x20);
        if (!txEnableInt_) {                 // TIE coupé → registre d'émission réputé prêt
            tdre_ = true;
            if (sched_) sched_->cancel(Scheduler::IKBD_TX);
        }
        raiseIfReady();
        return;
    }
    // Émission CPU → IKBD : sous TIE, écrire la donnée vide TDRE (transmetteur
    // occupé) ; il se re-remplit ~1 octet série plus tard (Scheduler IKBD_TX →
    // onTxEmpty), ce qui re-lève l'IRQ « transmetteur prêt » et cadence l'envoi
    // des commandes piloté par interruption. Le parseur reçoit l'octet tout de
    // suite (NeoST ne perd jamais d'octet) : TDRE ne sert qu'au statut et à l'IRQ TX.
    if (txEnableInt_ && sched_) {
        tdre_ = false;
        sched_->schedule(Scheduler::IKBD_TX, sched_->now() + kAciaTxByteCycles);
        raiseIfReady();                      // TDRE tombé → désarme l'IRQ TX jusqu'au re-remplissage
    }

    // Code 6301 custom : en mode Execute, l'octet va au handler du programme ;
    // pendant un LoadMemory ($20), il alimente le code téléversé. Sinon : parseur.
    if (exeMode_ && customWrite_ != CW_NONE) { customWriteDispatch(v); return; }
    if (memLoadLeft_ > 0)                    { loadMemoryByte(v);      return; }

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
    // NEOST_DEBUG_IKBD=1 : trace des commandes reçues par l'IKBD (équivalent du
    // --trace ikbd_cmds d'Hatari) — indispensable pour comprendre quel protocole
    // un jeu utilise (souris relative/absolue, joystick auto/monitoring…).
    static const bool dbg = std::getenv("NEOST_DEBUG_IKBD") != nullptr;
    if (dbg) {
        std::fprintf(stderr, "[ikbd] cmd $%02X", opcode);
        for (int i = 1; i < inBufLen_ && i < (int)inBuf_.size(); ++i)
            std::fprintf(stderr, " $%02X", inBuf_[i]);
        std::fprintf(stderr, "\n");
    }
    // Toute commande VALIDE complète lève la pause de sortie ($13) — cf. Hatari
    // IKBD_RunKeyboardCommand. Les opcodes inconnus (NOP) ne la lèvent pas ;
    // $13 lui-même la re-pose dans son case.
    if (cmdLength(opcode) != 0 && pauseOutput_) {
        pauseOutput_ = false;
        armRx();                         // les octets gelés en file redeviennent livrables
    }
    switch (opcode) {
        case 0x80:
            // Reset 0x80,0x01 : l'IKBD fait son auto-test puis renvoie $F1 APRÈS
            // ~502000 cycles (valeur Hatari IKBD_RESET_CYCLES). Répondre
            // INSTANTANÉMENT casse les diagnostics qui arment l'IRQ ACIA puis
            // attendent la réponse : l'IRQ serait levée avant l'armement (donc
            // perdue) → « Keyboard not responding ». On diffère via
            // l'ordonnanceur ; à défaut (pas de scheduler), repli immédiat.
            if (inBuf_[1] == 0x01) {
                // L'IKBD repart de ses défauts (cf. Hatari IKBD_Boot_ROM) : souris
                // RELATIVE, joystick AUTO, seuils 1, pas d'échelle, axe Y vers le
                // haut, état de bouton/joystick émis remis à zéro.
                mouseMode_     = REL;
                joyMode_       = JOY_AUTO;
                prevJoy0_      = prevJoy1_ = 0;
                xThreshold_    = yThreshold_ = 1;
                xScale_        = yScale_ = 0;
                yAxis_         = 1;
                mouseAction_   = 0;
                keyCodeDeltaX_ = keyCodeDeltaY_ = 1;
                bOldL_         = bOldR_ = false;
                mouseDeltaX_   = mouseDeltaY_ = 0;
                mouseLeft_     = mouseRight_ = false;
                absX_ = absY_  = 0;            // ABS_X/Y_ONRESET
                absMaxX_       = 320;          // ABS_MAX_X_ONRESET
                absMaxY_       = 200;          // ABS_MAY_Y_ONRESET
                prevAbsButtons_ = 0x0A;        // ABS_PREVBUTTONS
                // Drapeaux des quirks de la fenêtre de reset (cf. IKBD_Boot_ROM).
                mouseDisabled_ = joystickDisabled_ = false;
                mouseEnabledDuringReset_ = false;
                bothMouseAndJoy_ = false;
                pauseOutput_   = false;
                rx_.clear();                   // BufferHead=Tail=0 dans IKBD_Boot_ROM
                rdrf_ = false;                 // RDR vidé + livraison en vol annulée
                rxPending_ = false;
                if (sched_) sched_->cancel(Scheduler::IKBD_RX);
                duringResetCriticalTime_ = true;
                // Le reset annule tout code 6301 custom en cours (cf. Hatari
                // IKBD_Boot_ROM : LoadMemory/ExeMode coupés).
                exeMode_ = false; customRead_ = CR_NONE; customWrite_ = CW_NONE;
                memLoadLeft_ = 0;
                // L'horloge ($1B/$1C) est CONSERVÉE (reset à chaud, cf. Hatari).
                constexpr int64_t kIkbdResetCycles = 502000;
                if (sched_) sched_->schedule(Scheduler::IKBD, sched_->now() + kIkbdResetCycles);
                else        onResetResponse();
            }
            break;
        case 0x08:
            // $08 = SET RELATIVE MOUSE POSITION REPORTING (cf. Hatari
            // IKBD_Cmd_RelMouseMode) : retour aux paquets relatifs $F8 (mode par
            // défaut). Sans ce case, un jeu repassant en relatif depuis ABS/CURSOR/
            // OFF gardait l'ancien mode → souris muette ou flèches parasites.
            mouseMode_ = REL;
            // Reçu pendant la fenêtre de reset : mémorisé pour le quirk $08+$14
            // « souris ET joystick reportés ensemble » (Barbarian, cf. Hatari).
            if (duringResetCriticalTime_)
                mouseEnabledDuringReset_ = true;
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
            // paquet souris émis jusqu'au prochain mode souris. Le feu joystick et
            // les boutons souris se rejoignent alors sur les mêmes lignes (cf.
            // duplication des boutons dans onVbl).
            mouseMode_ = OFF;
            mouseDisabled_ = true;
            checkResetDisableBug();
            break;
        case 0x11:
            // $11 = RESUME (cf. Hatari IKBD_Cmd_StartKeyboardTransfer) : lève la
            // pause de sortie — déjà fait par le « toute commande valide lève la
            // pause » ci-dessus ; case explicite pour la lisibilité.
            break;
        case 0x13:
            // $13 = PAUSE OUTPUT (cf. Hatari IKBD_Cmd_StopKeyboardTransfer) : gèle
            // la livraison IKBD → ACIA jusqu'à la prochaine commande valide.
            // Ignorée pendant le reset (loader de « Just Bugging » par ACF).
            if (!duringResetCriticalTime_)
                pauseOutput_ = true;
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
            // bascule en mode auto ET COUPE LA SOURIS (comportement du vrai IKBD —
            // c'est ce qui fait passer le feu du joystick dans le paquet $FF au
            // lieu du bouton droit souris). Exceptions pendant la fenêtre de
            // reset : $08+$14 (Barbarian) ou $12+$14 (Hammerfist) → souris ET
            // joystick reportés ensemble (bothMouseAndJoy_).
            joyMode_ = JOY_AUTO;
            mouseMode_ = OFF;
            if (duringResetCriticalTime_ && mouseEnabledDuringReset_) {
                mouseMode_ = REL;
                bothMouseAndJoy_ = true;
            } else if (duringResetCriticalTime_ && mouseDisabled_) {
                mouseMode_ = REL;
                bothMouseAndJoy_ = true;
            }
            // RAZ l'état joystick mémorisé, puis — hack des jeux Utopos/Double
            // Bubble — émet AUSSITÔT un paquet $FE/$FF si l'état courant diffère
            // (sans attendre la prochaine trame ni vérifier l'ACIA).
            prevJoy0_ = prevJoy1_ = 0;
            uint8_t joy0 = 0, joy1 = 0;
            readJoystickState(joy0, joy1);
            sendAutoJoysticks(joy0, joy1);
            break;
        }
        case 0x15:
            // $15 = SET JOYSTICK INTERROGATION MODE (cf. Hatari IKBD_Cmd_StopJoystick) :
            // retour au mode interrogation seule ($16). Plus de report spontané.
            joyMode_ = JOY_OFF;
            break;
        case 0x1A:
            // $1A = DISABLE JOYSTICKS (cf. Hatari IKBD_Cmd_DisableJoysticks) : coupe
            // tout report spontané ; retour à l'interrogation seule, comme $15. Sans
            // ce case, joyMode_ restait JOY_AUTO et des paquets $FE/$FF non sollicités
            // continuaient d'être émis.
            joyMode_ = JOY_OFF;
            joystickDisabled_ = true;
            checkResetDisableBug();
            break;
        case 0x17:
            // $17 = SET JOYSTICK MONITORING (cf. Hatari IKBD_Cmd_SetJoystickMonitoring) :
            // octet 1 = taux d'échantillonnage en 1/100 s. NeoST échantillonne au VBL
            // (plutôt qu'au timer exact), mais respecte le format et coupe la souris.
            joyMode_ = JOY_MONITOR;
            mouseMode_ = OFF;
            break;
        case 0x18:
            // $18 = SET FIRE BUTTON MONITORING : non implémenté (comme Hatari).
            break;
        case 0x16: {
            // $16 = « interroger les joysticks » : l'IKBD répond IMMÉDIATEMENT par
            // un paquet $FD + état joystick 0 + état joystick 1 (cf. Hatari ikbd.c
            // IKBD_Cmd_ReturnJoystick — qui lit les DEUX ports bruts, sans couper
            // le port 0 quand la souris est active). État neutre $00 par défaut
            // (suffit à éviter « J2 Joystick time-out ») ; sous fixture de bouclage
            // la sonde écrase l'état hôte pour refléter le port parallèle (cf.
            // Machine) — test « Printer/Joystick » complet.
            uint8_t joy0 = hostJoy_[0], joy1 = hostJoy_[1];
            if (joyProbe_) joyProbe_(joy0, joy1);
            pushRx(0xFD);
            pushRx(joy0);
            pushRx(joy1);
            break;
        }
        case 0x20:
            // $20 = LOAD MEMORY (cf. Hatari IKBD_Cmd_LoadMemory) : en-tête
            // $20 ADRMSB ADRLSB NUM ; les NUM octets suivants sont le programme
            // 6301 téléversé (avalés + CRC, cf. loadMemoryByte).
            memLoadTotal_ = inBuf_[3];
            memLoadLeft_  = inBuf_[3];
            crc32Reset(memLoadCrc_);
            break;
        case 0x21:
            // $21 = READ MEMORY (cf. Hatari IKBD_Cmd_ReadMemory) : en-tête $F6 $20
            // puis 6 octets nuls (NeoST n'émule pas la RAM interne du 6301).
            pushRx(0xF6);
            pushRx(0x20);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x22:
            // $22 = CONTROLLER EXECUTE (cf. Hatari IKBD_Cmd_Execute) : bascule le
            // 6301 en mode custom SI un programme connu a été reconnu au chargement.
            if (customWrite_ != CW_NONE) exeMode_ = true;
            break;
        // --- Commandes de RAPPORT d'état $87-$9A (cf. Hatari IKBD_Cmd_Report*) ---
        // Réponse : $F6 + opcode du réglage + 6 octets de paramètres (zéro-remplis).
        // Un programme qui interroge son réglage et attend la réponse restait
        // bloqué tant que ces commandes étaient des NOP.
        case 0x87: {                          // REPORT MOUSE BUTTON ACTION
            pushRx(0xF6); pushRx(0x07); pushRx(mouseAction_);
            for (int i = 0; i < 5; ++i) pushRx(0x00);
            break;
        }
        case 0x88: case 0x89: case 0x8A: {    // REPORT MOUSE MODE
            pushRx(0xF6);
            switch (mouseMode_) {
                case ABS:
                    pushRx(0x09);
                    pushRx(uint8_t(absMaxX_ >> 8)); pushRx(uint8_t(absMaxX_));
                    pushRx(uint8_t(absMaxY_ >> 8)); pushRx(uint8_t(absMaxY_));
                    pushRx(0x00); pushRx(0x00);
                    break;
                case CURSOR:
                    pushRx(0x0A);
                    pushRx(uint8_t(keyCodeDeltaX_)); pushRx(uint8_t(keyCodeDeltaY_));
                    for (int i = 0; i < 4; ++i) pushRx(0x00);
                    break;
                case REL:
                    pushRx(0x08);
                    for (int i = 0; i < 6; ++i) pushRx(0x00);
                    break;
                default:                      // OFF : Hatari n'émet que l'en-tête $F6
                    break;
            }
            break;
        }
        case 0x8B:                            // REPORT MOUSE THRESHOLD
            pushRx(0xF6); pushRx(0x0B);
            pushRx(uint8_t(xThreshold_)); pushRx(uint8_t(yThreshold_));
            for (int i = 0; i < 4; ++i) pushRx(0x00);
            break;
        case 0x8C:                            // REPORT MOUSE SCALE
            pushRx(0xF6); pushRx(0x0C);
            pushRx(uint8_t(xScale_)); pushRx(uint8_t(yScale_));
            for (int i = 0; i < 4; ++i) pushRx(0x00);
            break;
        case 0x8F: case 0x90:                 // REPORT MOUSE VERTICAL COORDINATES
            pushRx(0xF6); pushRx(yAxis_ == -1 ? 0x0F : 0x10);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x92:                            // REPORT MOUSE AVAILABILITY
            pushRx(0xF6); pushRx(mouseMode_ == OFF ? 0x12 : 0x00);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x94: case 0x95: case 0x99:      // REPORT JOYSTICK MODE
            pushRx(0xF6); pushRx(joyMode_ == JOY_AUTO ? 0x14 : 0x15);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x9A:                            // REPORT JOYSTICK AVAILABILITY
            pushRx(0xF6); pushRx(joyMode_ == JOY_OFF ? 0x1A : 0x00);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        default:
            // Opcodes restants : no-op — le parseur a déjà consommé les octets de
            // paramètre.
            break;
    }
}

void Ikbd::checkResetDisableBug() {
    // Quirk du vrai IKBD (cf. Hatari IKBD_CheckResetDisableBug) : désactiver
    // souris ($12) ET joystick ($1A) pendant la fenêtre de reset ne tient pas —
    // les deux reports sont ré-activés ensemble (des jeux comptent dessus pour
    // recevoir souris et joystick en même temps).
    if (mouseDisabled_ && joystickDisabled_ && duringResetCriticalTime_) {
        mouseMode_ = REL;
        joyMode_   = JOY_AUTO;
        bothMouseAndJoy_ = true;
    }
}

void Ikbd::readJoystickState(uint8_t& joy0, uint8_t& joy1) const {
    joy0 = hostJoy_[0];
    joy1 = hostJoy_[1];
    if (joyProbe_) joyProbe_(joy0, joy1);

    // Sur Atari ST, le port joystick 0 est partagé avec la souris : tant que la
    // souris est active, Hatari le force à zéro — SAUF en mode « souris + joystick
    // simultanés » obtenu pendant la fenêtre de reset (cf. IKBD_GetJoystickData).
    if (!(mouseMode_ == OFF || (bothMouseAndJoy_ && mouseMode_ == REL)))
        joy0 = 0x00;
}

void Ikbd::sendAutoJoysticks(uint8_t joy0, uint8_t joy1) {
    // Émet un paquet par manette dont l'état a changé depuis la dernière fois
    // (cf. Hatari IKBD_SendAutoJoysticks) : $FE = joystick 0 / souris, $FF =
    // joystick 1. L'état reçu a déjà subi la duplication feu/boutons (onVbl).
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

void Ikbd::sendAutoJoysticksMonitoring(uint8_t joy0, uint8_t joy1) {
    pushRx(uint8_t(((joy0 & 0x80) >> 6) | ((joy1 & 0x80) >> 7)));
    pushRx(uint8_t(((joy0 & 0x0F) << 4) | (joy1 & 0x0F)));
}

void Ikbd::onVbl(int64_t vblMicro) {
    // 1) Avance l'horloge interne IKBD ($1B/$1C) du temps d'une trame.
    updateClock(vblMicro);
    ++vblCount_;
    // ExeMode : appel périodique du handler de lecture custom (cf. Hatari, qui
    // l'invoque dans le traitement clavier de trame), puis Δ souris consommé. Pas
    // de report joystick auto tant que le 6301 exécute du code custom.
    if (exeMode_) {
        customReadDispatch();
        mDeltaX_ = mDeltaY_ = 0;
        return;
    }
    if (duringResetCriticalTime_ || vblCount_ <= 20)
        return;

    // --- Trame IKBD, ordre calqué sur Hatari IKBD_SendAutoKeyboardCommands ---
    // 2) Lecture des manettes pour cette trame (joy0 coupé si la souris occupe le
    //    port 0, sauf mode « souris + joystick simultanés »).
    uint8_t joy0 = 0, joy1 = 0;
    readJoystickState(joy0, joy1);

    // 3) Duplication feu/boutons (cf. IKBD_DuplicateMouseFireButtons) : sur le
    //    vrai matériel le feu du joystick 1 et le bouton DROIT de la souris sont
    //    LA MÊME ligne (idem feu joystick 0 / bouton gauche). Souris coupée →
    //    les boutons souris remontent comme feux joystick ; souris active → le
    //    feu joystick 1 est RETIRÉ du paquet joystick et remonte comme bouton
    //    droit dans le paquet souris (jeux Big Run, Magic Pockets…).
    bool left = mouseLeft_, right = mouseRight_;
    if (mouseMode_ == OFF) {
        if (right) joy1 |= 0x80;
        if (left)  joy0 |= 0x80;
    } else if (joy1 & 0x80) {
        joy1 = uint8_t(joy1 & ~0x80);
        right = true;
    }

    // 4) Reporting lié à MouseAction ($07), AVANT le paquet de mode (l'état
    //    bOldL_/bOldR_ n'est mis à jour qu'en fin de trame).
    sendOnMouseAction(left, right);

    // 5) Position absolue interne : mise à jour dans TOUS les modes souris
    //    (cf. IKBD_UpdateInternalMousePosition), en consommant le Δ de la trame.
    const int dx = mouseDeltaX_;
    const int dy = mouseDeltaY_;
    mouseDeltaX_ = mouseDeltaY_ = 0;
    updateInternalAbsPos(dx, dy);
    prevL_ = left; prevR_ = right;           // état bouton courant (interrogation $0D)

    // 6) Monitoring ($17) : seul report émis, le reste de la trame est coupé.
    if (joyMode_ == JOY_MONITOR) {
        sendAutoJoysticksMonitoring(joy0, joy1);
        return;
    }

    // 7) Report joystick auto, PUIS paquet souris selon le mode (ordre Hatari).
    if (joyMode_ == JOY_AUTO)
        sendAutoJoysticks(joy0, joy1);
    if (mouseMode_ == REL)
        sendRelMousePacket(dx, dy, left, right);
    else if (mouseMode_ == CURSOR)
        sendCursorKeys(dx, dy, left, right);

    // 8) Mémorise l'état bouton EFFECTIF (feu joystick inclus) pour la détection
    //    de front de la prochaine trame.
    bOldL_ = left;
    bOldR_ = right;
}

void Ikbd::updateInternalAbsPos(int dx, int dy) {
    // Port de Hatari IKBD_UpdateInternalMousePosition : la position absolue est
    // tenue à jour quel que soit le mode souris (échelle $0C si > 1, signe d'axe
    // Y $0F/$10, bornes inclusives posées par $09).
    const int sx = (xScale_ > 1) ? dx * xScale_ : dx;
    const int sy = (yScale_ > 1) ? dy * yAxis_ * yScale_ : dy * yAxis_;
    int x = static_cast<int>(absX_) + sx;
    int y = static_cast<int>(absY_) + sy;
    if (x < 0) x = 0; else if (x > absMaxX_) x = absMaxX_;
    if (y < 0) y = 0; else if (y > absMaxY_) y = absMaxY_;
    absX_ = static_cast<uint16_t>(x);
    absY_ = static_cast<uint16_t>(y);
}

void Ikbd::keyEvent(uint8_t scancode, bool pressed) {
    if (joyMode_ == JOY_MONITOR)
        return;
    // Suivi d'état (lu par les handlers 6301 custom) puis make/break.
    scanState_[scancode & 0x7F] = pressed ? 1 : 0;
    // En ExeMode, le scan clavier normal est supprimé (cf. Hatari IKBD_Cmd_Return_Byte) :
    // seul le handler custom peut émettre, via customReadDispatch().
    if (exeMode_) { customReadDispatch(); return; }
    pushRx(pressed ? scancode : uint8_t(scancode | 0x80));
}

void Ikbd::mouseEvent(int dx, int dy, bool left, bool right) {
    // En ExeMode, le 6301 ne génère pas de paquet souris standard : on accumule le
    // Δ et l'état du bouton, lus par les handlers custom (Froggies/Dragonnels…).
    if (exeMode_) {
        mDeltaX_ += dx; mDeltaY_ += dy; lmb_ = left;
        return;
    }
    mouseDeltaX_ += dx;
    mouseDeltaY_ += dy;
    mouseLeft_ = left;
    mouseRight_ = right;
}

void Ikbd::sendRelMousePacket(int dx, int dy, bool left, bool right) {
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
    if (duringResetCriticalTime_)
        return;
    rx_.push_back(b);
    armRx();
}

void Ikbd::armRx() {
    // Date la livraison du prochain octet de la file : ~1 octet série (10 bits à
    // 7812,5 bauds = 10240 cycles à 8 MHz, cadence du SCI d'Hatari). Pas de
    // livraison tant que le RDR courant n'est pas lu, ni sous PAUSE OUTPUT ($13).
    // Sans ordonnanceur (tests unitaires), repli : livraison immédiate.
    if (rdrf_ || rx_.empty() || pauseOutput_ || rxPending_)
        return;
    constexpr int64_t kAciaRxByteCycles = 10240;
    if (sched_) {
        rxPending_ = true;
        sched_->schedule(Scheduler::IKBD_RX, sched_->now() + kAciaRxByteCycles);
    } else {
        onRxDeliver();
    }
}

void Ikbd::onRxDeliver() {
    rxPending_ = false;
    if (rdrf_ || rx_.empty() || pauseOutput_)
        return;
    rdr_ = rx_.front();
    rx_.pop_front();
    rdrf_ = true;
    raiseIfReady();
}

bool Ikbd::irqActive() const {
    // Cause d'IRQ de l'ACIA 6850 (cf. acia.c ACIA_UpdateIRQ) : RX = octet LIVRÉ
    // (RDRF) et RX int activé (RIE, bit7) ; TX = registre d'émission vide et IRQ
    // d'émission armée (TIE). CTS = GND sur l'ST → toujours 0, omis.
    return (rdrf_ && (control_ & 0x80)) || (txEnableInt_ && tdre_);
}

void Ikbd::raiseIfReady() {
    // L'ACIA active sa ligne d'IRQ dès qu'une cause (RX ou TX) est active. On la
    // publie sur GPIP4 (lue par _int_acia pour vider l'ACIA) ET on déclenche le
    // canal 6 du MFP.
    const bool active = irqActive();
    mfp_.setAciaLineKbd(active);
    if (active)
        mfp_.raise(Mfp::SRC_ACIA);
}

void Ikbd::onTxEmpty() {
    // Le registre d'émission de l'ACIA s'est vidé (octet « transmis » à l'IKBD) :
    // TDRE repasse à 1 → re-lève l'IRQ « transmetteur prêt » tant que TIE est armé.
    tdre_ = true;
    raiseIfReady();
}

// ============================================================================
//  Code 6301 custom ($20 LoadMemory / $22 Execute) — port de Hatari ikbd.c.
//  Faute d'émuler un vrai HD6301, on reconnaît le programme téléversé à son CRC
//  et un handler reproduit son protocole. Programme inconnu (ex. Vroom) → ignoré,
//  exactement comme Hatari (le jeu retombe sur la souris/clavier normale).
// ============================================================================

void Ikbd::loadMemoryByte(uint8_t v) {
    // Octet du programme chargé via $20 : on cumule le CRC ; au dernier octet, si
    // le CRC correspond à un boot-stub connu, on arme CommonBoot pour la suite.
    crc32AddByte(memLoadCrc_, v);
    if (--memLoadLeft_ > 0) return;
    for (uint32_t crc : kKnownLoadCrc) {
        if (crc == memLoadCrc_) {
            crc32Reset(memLoadCrc_);
            memExeNbBytes_ = 0;
            customRead_  = CR_NONE;
            customWrite_ = CW_BOOT;          // ExeBootHandler = CommonBoot
            return;
        }
    }
    customRead_  = CR_NONE;                  // code inconnu → aucun handler
    customWrite_ = CW_NONE;
}

void Ikbd::commonBoot(uint8_t v) {
    // Boot-stub commun : en ExeMode, on accumule le programme principal et son CRC
    // jusqu'à reconnaître un programme connu, puis on installe ses handlers R/W.
    struct Def { int nb; uint32_t crc; CustomR r; CustomW w; };
    static const Def defs[] = {
        { 167, 0xe7110b6du, CR_NONE,     CW_FROGGIES   },   // Froggies Over The Fence
        { 165, 0x5617c33cu, CR_TRANSB2,  CW_NONE       },   // Transbeauce 2
        {  83, 0xdf3e5a88u, CR_NONE,     CW_DRAGONNELS },   // Dragonnels
        { 109, 0xa11d8be5u, CR_CHAOSAD,  CW_CHAOSAD    },   // Chaos A.D.
        {  91, 0x119b26edu, CR_AS_COLOR, CW_AS         },   // Audio Sculpture (couleur)
        {  91, 0x63b5f4dfu, CR_AS_MONO,  CW_AS         },   // Audio Sculpture (mono)
    };
    crc32AddByte(memLoadCrc_, v);
    ++memExeNbBytes_;
    for (const auto& d : defs) {
        if (d.nb == memExeNbBytes_ && d.crc == memLoadCrc_) {
            customRead_  = d.r;
            customWrite_ = d.w;
            if (d.w == CW_CHAOSAD) { chaosFirst_ = true; chaosIgnore_ = 8; chaosIndex_ = 0; chaosCount_ = 0; }
            if (d.w == CW_AS)      { asMagic_ = false; asReadCount_ = 0; }
            rx_.clear();                     // flush des octets en file (BufferHead=Tail=0)
            rdrf_ = false;
            rxPending_ = false;
            if (sched_) sched_->cancel(Scheduler::IKBD_RX);
            return;
        }
    }
    // Pas (encore) de correspondance : on continue d'accumuler.
}

void Ikbd::customWriteDispatch(uint8_t v) {
    switch (customWrite_) {
        case CW_BOOT:       commonBoot(v);          break;
        case CW_FROGGIES:   froggiesWrite(v);       break;
        case CW_DRAGONNELS: dragonnelsWrite(v);     break;
        case CW_CHAOSAD:    chaosWrite(v);          break;
        case CW_AS:         audioSculptureWrite(v); break;
        default: break;
    }
}

void Ikbd::customReadDispatch() {
    switch (customRead_) {
        case CR_TRANSB2:  transbeauce2Read();        break;
        case CR_CHAOSAD:  chaosRead();               break;
        case CR_AS_COLOR: audioSculptureRead(true);  break;
        case CR_AS_MONO:  audioSculptureRead(false); break;
        default: break;
    }
}

void Ikbd::exitExeMode() {
    // Sortie du mode Execute (le 6301 fait jmp $f000) : retour au comportement ROM.
    exeMode_     = false;
    customRead_  = CR_NONE;
    customWrite_ = CW_NONE;
    memLoadLeft_ = 0;
}

int Ikbd::checkPressedKey() const {
    for (int i = 0; i < 128; ++i) if (scanState_[i]) return i;
    return -1;
}

// --- Froggies Over The Fence : renvoie le Δ souris/clavier sur demande ----------
void Ikbd::froggiesWrite(uint8_t v) {
    if (v & 0x80) { exitExeMode(); return; }      // octet <0 → le 6301 quitte ExeMode
    uint8_t res80 = 0, res81 = 0, res82 = 0;
    const uint8_t res83 = 0xFC;                    // valeur fixe (inutilisée par la démo)
    if (mDeltaY_ < 0) res80 = 0x7A;
    if (mDeltaY_ > 0) res80 = 0x06;
    if (mDeltaX_ < 0) res81 = 0x7A;
    if (mDeltaX_ > 0) res81 = 0x06;
    if (lmb_)         res82 |= 0x80;
    if (scanState_[0x48]) res80 |= 0x7A;           // flèche haut
    if (scanState_[0x50]) res80 |= 0x06;           // flèche bas
    if (scanState_[0x4B]) res81 |= 0x7A;           // flèche gauche
    if (scanState_[0x4D]) res81 |= 0x06;           // flèche droite
    if (scanState_[0x70]) res82 |= 0x80;           // pavé numérique 0
    res80 |= res82;                                // bit7 = bouton gauche
    res81 |= res82;
    if (v == 1)      pushRx(res80);                // demande 1 octet
    else if (v == 4) { pushRx(res83); pushRx(res82); pushRx(res81); pushRx(res80); }  // 4 octets
}

// --- Transbeauce 2 : 1 octet d'état clavier/joystick ----------------------------
void Ikbd::transbeauce2Read() {
    uint8_t res = 0;
    if (scanState_[0x48]) res |= 0x01;             // haut
    if (scanState_[0x50]) res |= 0x02;             // bas
    if (scanState_[0x4B]) res |= 0x04;             // gauche
    if (scanState_[0x4D]) res |= 0x08;             // droite
    if (scanState_[0x62]) res |= 0x40;             // Help
    if (scanState_[0x39]) res |= 0x80;             // Espace
    res |= uint8_t(hostJoy_[1] & 0x8F);            // joystick (bits 0-3 + feu bit7)
    pushRx(res);
}

// --- Dragonnels : Y souris + bouton gauche --------------------------------------
void Ikbd::dragonnelsWrite(uint8_t /*v*/) {
    uint8_t res = 0;
    if (mDeltaY_ < 0) res = 0xFC;
    if (mDeltaY_ > 0) res = 0x04;
    if (lmb_)         res = 0x80;
    pushRx(res);
}

// --- Chaos A.D. : décodeur de protection (XOR avec une clé de 8 octets) ----------
void Ikbd::chaosRead() {
    if (chaosFirst_) pushRx(0xFE);                 // « prêt à recevoir »
    chaosFirst_ = false;
}
void Ikbd::chaosWrite(uint8_t v) {
    static const uint8_t key[8] = { 0xCA, 0x0A, 0xBC, 0x00, 0xDE, 0xDE, 0xFE, 0xCA };
    if (chaosIgnore_ > 0) { --chaosIgnore_; return; }   // 8 octets de clé déjà connus
    if (chaosCount_ <= 6080) {                          // 6081 octets à décoder
        ++chaosCount_;
        v = uint8_t(v ^ key[chaosIndex_]);
        chaosIndex_ = (chaosIndex_ + 1) & 0x07;
        pushRx(v);
    } else if (v == 0x08) {
        exitExeMode();
    }
}

// --- Audio Sculpture : déchiffrement (magic $42 → renvoie la clé $4B $13) --------
void Ikbd::audioSculptureRead(bool colorMode) {
    if (asMagic_) {
        if (++asReadCount_ == 2) { exitExeMode(); asMagic_ = false; asReadCount_ = 0; }
    } else if ((!colorMode && checkPressedKey() >= 0) || scanState_[0x39]) {
        pushRx(0x39);                              // scancode Espace
    }
}
void Ikbd::audioSculptureWrite(uint8_t v) {
    if (v == 0x42) { asMagic_ = true; pushRx(0x4B); pushRx(0x13); }
}
