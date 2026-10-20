// =============================================================================
//  Ikbd.cpp — ACIA 6850 + file de scancodes IKBD.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <algorithm>

// Bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_OVRN = 0x20,   // Receiver Overrun : octet(s) perdu(s), posé à la lecture RDR
    ACIA_IRQ  = 0x80,   // ligne d'interruption (vers GPIP4 du MFP)
};
// (DCD/CTS sont à la masse sur l'ST → bits 2/3 toujours 0 ; FE/PE n'arrivent
// jamais : la liaison émulée ne produit ni erreur de trame ni erreur de parité.)

// Liaison CPU → IKBD : grille 7812,5 bauds, 8N1. acia.c ACIA_Write_TDR /
// ACIA_Clock_TX : TDRE remonte au transfert TDR→TSR ; ikbd.c SCI_Get_Line_RX
// ne livre la commande qu'au bit de stop. Format/diviseur restent bornés
// au protocole clavier ST usuel (CR=$96/$B6) ; break inhibe les départs.
static constexpr int64_t kAciaTxBitCycles = 1024;
// Milieux déterministes des fourchettes IKBD_Delay_Random d'Hatari. Le SCI
// quantifie ensuite par division entière en bits de 1024 cycles (ikbd.c:1035).
static constexpr int kResponseDelayCycles = 7250;   // [7000,7500[
static constexpr int kJoystickDelayCycles = 8750;   // [7500,10000[

// Empreinte « CRC32 » d'Hatari (utils.c, crc32_*). On reconnaît un programme 6301
// custom à cette empreinte plutôt que d'émuler un vrai HD6301 ; les valeurs connues
// viennent de la table d'Hatari, il faut donc SA fonction, défaut compris.
// ⚠ Ce n'est PAS un CRC32 : Hatari teste `(c & 0x80) ^ (crc & 0x80000000)`, deux bits
// de positions différentes, donc vrai dès que L'UN OU L'AUTRE est levé. Le vrai OU
// exclusif, en place jusqu'au 2026-10-20, ne reconnaissait aucun programme réel
// (Froggies : 0xed260822 au lieu de 0x2efb11b1) ; seul le test synthétique passait.
namespace {
bool kbdTraceEnabled() {
    static const bool enabled = std::getenv("NEOST_KBD_TRACE") != nullptr;
    return enabled;
}

void crc32Reset(uint32_t& crc) { crc = 0xFFFFFFFFu; }
void crc32AddByte(uint32_t& crc, uint8_t c) {
    for (int b = 0; b < 8; ++b) {
        const bool topC   = (c   & 0x80u)       != 0;
        const bool topCrc = (crc & 0x80000000u) != 0;
        crc = (topC || topCrc) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
        c = uint8_t(c << 1);
    }
}
} // namespace

uint32_t Ikbd::customCodeCrcForTest(const uint8_t* data, std::size_t len) {
    uint32_t crc;
    crc32Reset(crc);
    for (std::size_t i = 0; i < len; ++i) crc32AddByte(crc, data[i]);
    return crc;
}

void Ikbd::armFroggiesForTest() {
    customRead_ = CR_NONE;
    customWrite_ = CW_FROGGIES;
    exeMode_ = true;
}

void Ikbd::armCommonBootForTest() {
    // État après un boot-stub reconnu ($20) puis $22 : CommonBoot accumule.
    crc32Reset(memLoadCrc_);
    memExeNbBytes_ = 0;
    customRead_ = CR_NONE;
    customWrite_ = CW_BOOT;
    exeMode_ = true;
}

namespace {
// CRC des boot-stubs ($20) connus (tous gérés par CommonBoot). 0xbc0c206d sert aux
// deux variantes d'Audio Sculpture (départagées au CRC du prog principal en ExeMode).
const uint32_t kKnownLoadCrc[] = {
    0x2efb11b1u, 0xadb6b503u, 0x33c23cdfu, 0x9ad7fcdfu, 0xbc0c206du,
};
} // namespace

Ikbd::Ikbd(Mfp& mfp) : mfp_(mfp) {
    initClockFromHostTime();
}

void Ikbd::trace(const char* format, ...) const {
    if (!kbdTraceEnabled()) return;
    std::fprintf(stderr, "[kbd] cycle=%lld ", (long long)firmwareEventTime());
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
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
        // encore lu ; OVRN si un overrun a été constaté à la lecture de RDR ;
        // IRQ si une cause RX ou TX est active. Sous PAUSE OUTPUT ($13), la
        // livraison est gelée → RDRF ne remonte plus (octets en file). Lire SR
        // est mémorisé (srRead_) : la prochaine lecture de RDR effacera OVRN
        // (séquence SR→RDR du 6850, cf. acia.c pACIA->SR_Read).
        uint8_t s = tdre_ ? ACIA_TDRE : 0;
        if (rdrf_)       s |= ACIA_RDRF;
        if (ovrn_)       s |= ACIA_OVRN;
        if (irqActive()) s |= ACIA_IRQ;
        srRead_ = true;
        return s;
    }
    // $FFFC02 : lecture de la donnée → consomme l'octet livré (efface RDRF) et
    // gère l'overrun (port de Hatari ACIA_Read_RDR) : si SR a été lu avant, OVRN
    // retombe ; si un overrun est PENDANT (octet perdu pendant que RDR était
    // plein, cf. onRxDeliver), OVRN se pose MAINTENANT — pas au moment de la
    // perte. À vide, le RDR conserve le dernier octet reçu.
    // NEOST_DEBUG_ACIA=1 : trace chaque lecture du data register.
    static const bool dbgRdr = std::getenv("NEOST_DEBUG_ACIA") != nullptr;
    const uint8_t b = rdr_;          // RDR courant (conservé si relu à vide)
    const bool had = rdrf_;
    rdrf_ = false;
    if (srRead_) {                   // séquence SR puis RDR → OVRN acquitté
        srRead_ = false;
        ovrn_ = false;
    }
    if (rxOverrun_) {                // octet(s) perdu(s) depuis la dernière lecture
        ovrn_ = true;
        rxOverrun_ = false;
    }
    trace("acia-read data=$%02X full=%d overrun=%d queued=%zu", b, had, ovrn_, rx_.size());
    if (dbgRdr)
        std::fprintf(stderr, "[acia] rd %s$%02X%s reste=%zu cyc=%lld\n",
                     had ? "" : "EMPTY (", b, had ? "" : ")",
                     rx_.size(), sched_ ? (long long)sched_->now() : 0LL);
    armRx();                         // octet suivant éventuel → livraison datée
    raiseIfReady();                  // RDRF/overrun retombés → l'IRQ RX peut retomber
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
        case 0x18: return 1;   // SetJoystickFireDuration (Hatari) = « fire button
                               // monitoring » de la doc Atari — mono-octet
        // A39 (2026-08-29) : ce commentaire disait « SetJoystickFireDuration », le nom
        // que Hatari donne à $18. $19 est le mode KEYCODE manette
        // (IKBD_Cmd_SetCursorForJoystick) : 6 octets de paramètres, RX/RY/TX/TY/VX/VY.
        // Les longueurs étaient justes — seule l'étiquette trompait le lecteur.
        case 0x19: return 7;   // SetCursorForJoystick : mode keycode manette (6 param.)
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
    // Un accès CPU peut précéder le dispatch d'un front TX déjà passé sur le
    // fil. Transférer/terminer ces caractères avant de modifier CR ou TDR.
    if (sched_) catchUpCpuTx(sched_->liveNow());
    if ((addr & 2) == 0) {
        trace("acia-control old=$%02X new=$%02X", control_, v);
        // $FFFC00 : registre de contrôle (diviseur, format, bit7 = RX int enable).
        // Bits 5-6 = contrôle émetteur : 01 arme l'IRQ d'émission (TIE), cf. ACIA
        // 6850 et acia.c ACIA_Write_CR. TIE ne décide QUE de l'interruption : le
        // temps de transmission, lui, est modélisé dans tous les cas (cf. write
        // des données plus bas) — un pilote qui SCRUTE TDRE est freiné comme un
        // pilote qui interrompt (jeu armant CR=$b6 au lieu de $96, Hades Nebula).
        const bool wasBreak = (control_ & 0x60) == 0x60;
        // Hatari acia.c ACIA_Write_CR/ACIA_Set_Timers_IKBD : seul un
        // changement de CR0..1 non reset redémarre le timer série. La tranche
        // CPU→IKBD reste bornée à /64 8N1 ; mémoriser sa phase TX.
        const bool dividerChanged = (v & 3) != 3 && (v & 3) != (control_ & 3);
        if (dividerChanged) {
            // Hatari ACIA_Write_CR : seul un changement du diviseur hors
            // master reset initialise Clock_Divider. Le reset le conserve.
            aciaClockInitialized_ = true;
            txClockPhase_ = sched_ ? sched_->liveNow() % kAciaTxBitCycles : 0;
        }
        control_ = v;
        txEnableInt_ = ((v & 0x60) == 0x20);
        // ⚠ Changer les bits d'émission ne remet PAS l'émetteur au repos : seul le
        // master reset le fait (6850). L'ancien « TIE coupé → tdre_ = true » rendait
        // l'émetteur prêt à chaque écriture du CR, ce qui annulait le temps de
        // transmission pour un pilote retouchant le CR entre deux octets.
        // Bits 0-1 = 11 → master reset de l'ACIA (cf. acia.c ACIA_MasterReset) :
        // SR repart à TDRE seul → RDRF/OVRN effacés. La file IKBD et la livraison
        // en vol sont CONSERVÉES (elles vivent côté 6301, pas côté ACIA).
        if ((v & 0x03) == 0x03) {
            rdrf_ = false;
            rxOverrun_ = ovrn_ = srRead_ = false;
            tdre_ = true;
            txShifting_ = false; txBitIndex_ = 0;
        }
        // acia.c ACIA_Clock_TX : break n'agit qu'au repos. Le TSR engagé finit
        // normalement ; le TDR reste plein tant que CR demande le break.
        // Le niveau bas est consommé par le SCI ; ses erreurs ne sont pas exposées
        // comme un registre CPU (le 6301 reste un modèle de firmware).
        if (sched_) {
            if (!txShifting_ && (wasBreak || dividerChanged))
                txNextBitCycle_ = nextTxClock(sched_->liveNow());
            scheduleCpuTx();
        }
        raiseIfReady();
        return;
    }
    trace("acia-write data=$%02X tdr-full=%d", v, !tdre_);
    if (!sched_) {
        // Repli synchrone pour les tests nus du protocole uniquement.
        receiveSerialByte(v);
        return;
    }
    // Un TDR et un TSR, jamais une file illimitée : une écriture prématurée
    // remplace le TDR, sans toucher à l'octet déjà engagé sur la ligne.
    txData_ = v;
    tdre_ = false;
    if (txNextBitCycle_ == 0) txNextBitCycle_ = nextTxClock(sched_->liveNow());
    scheduleCpuTx();
    raiseIfReady();
}

void Ikbd::receiveSerialByte(uint8_t v) {
    trace("sci-receive data=$%02X", v);
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
    // Borne BASSE autant que haute : ceinture et bretelles derrière l'invariant de
    // save-state (cf. Ikbd::serialize) — cet index a déjà servi d'écriture arbitraire.
    if (inBufLen_ >= 0 && inBufLen_ < static_cast<int>(inBuf_.size()))
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
    // Une seule ligne par commande, paramètres inclus ; les anciens diagnostics
    // gardent leur format pour les scripts qui les exploitent déjà.
    if (kbdTraceEnabled()) {
        char command[3 * 8 + 1] = {};
        for (int i = 0; i < inBufLen_ && i < (int)inBuf_.size(); ++i)
            std::snprintf(command + 3 * i, sizeof(command) - 3 * i, "%02X ", inBuf_[i]);
        trace("command bytes=%s", command);
    }
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
    const bool resumed = cmdLength(opcode) != 0 && pauseOutput_;
    if (resumed) {
        pauseOutput_ = false;
    }
    switch (opcode) {
        case 0x80:
            // Reset 0x80,0x01 : warm-boot de la ROM IKBD (cf. Hatari
            // IKBD_Boot_ROM(false)) — remise aux défauts + réponse $F1 différée.
            if (inBuf_[1] == 0x01)
                bootRom();
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
            // paquet $F7 + boutons + X/Y. État de bouton = l'état VIVANT (Hatari lit
            // Keyboard.bL/RButtonDown à l'événement hôte) — le latch de trame
            // (prevL_/R_) rapportait « pas pressé » jusqu'au VBL suivant le clic.
            // Le feu du joystick 1 compte comme bouton DROIT quand la souris est
            // active (même ligne physique — bRButtonDown|BUTTON_JOYSTICK posé par
            // IKBD_DuplicateMouseFireButtons au VBL chez Hatari).
            sendAbsMousePos(mouseLeft_,
                            mouseRight_ || (mouseMode_ != OFF && (hostJoy_[1] & 0x80)),
                            18000 - 7200); // IKBD_Cmd_ReadAbsMousePos : 18000-ACIA_CYCLES
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
            // Octet BRUT, 0 permis (Hatari ikbd.c:2000 stocke tel quel) : avec pas=0
            // la boucle sendCursorKeys émet ses 10 paires/trame sans consommer le Δ,
            // comme le vrai 6301 — l'ancien « ?: 1 » se comportait en pas=1.
            keyCodeDeltaX_ = inBuf_[1];
            keyCodeDeltaY_ = inBuf_[2];
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
            // $13 = PAUSE OUTPUT (cf. Hatari IKBD_Cmd_StopKeyboardTransfer) :
            // bloque les nouveaux départs SCI ; la trame engagée se termine.
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
            // $FC + les 6 octets BCD de l'horloge. Garde « paquet entier ou
            // rien » (IKBD_OutputBuffer_CheckFreeCount(7)) : tampon plein → jeté.
            if (!rxFree(7)) break;
            pushRxDelayed(0xFC, kResponseDelayCycles);
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
            // octet 1 = taux en 1/100 s, zéro ramené à un ; même base fixe
            // 8021247 Hz que Hatari. Cette cadence reste commune à tous les
            // reports jusqu’au boot ROM, même après sortie du mode monitoring.
            joyMode_ = JOY_MONITOR;
            mouseMode_ = OFF;
            joystickMonitorPeriod_ = int64_t(8021247) * (inBuf_[1] ? inBuf_[1] : 1) / 100;
            if (sched_) sched_->cancel(Scheduler::IKBD_AUTOSEND);
            if (sched_) sched_->schedule(Scheduler::IKBD_MONITOR,
                                         firmwareEventTime() + joystickMonitorPeriod_);
            break;
        case 0x18:
            // $18 = SET FIRE BUTTON MONITORING : non implémenté (comme Hatari).
            break;
        case 0x16: {
            // $16 = « interroger les joysticks » : l'IKBD répond après traitement par
            // un paquet $FD + état joystick 0 + état joystick 1 (cf. Hatari ikbd.c
            // IKBD_Cmd_ReturnJoystick — qui lit les DEUX ports bruts, sans couper
            // le port 0 quand la souris est active). État neutre $00 par défaut
            // (suffit à éviter « J2 Joystick time-out ») ; sous fixture de bouclage
            // la sonde écrase l'état hôte pour refléter le port parallèle (cf.
            // Machine) — test « Printer/Joystick » complet.
            // Garde « paquet entier ou rien » (IKBD_OutputBuffer_CheckFreeCount(3)) :
            // des jeux (Downfall, Fokker) répètent $16 sans lire les réponses →
            // tampon plein → le paquet $FD est jeté en entier, jamais tronqué.
            uint8_t joy0 = hostJoy_[0], joy1 = hostJoy_[1];
            if (joyProbe_) joyProbe_(joy0, joy1);
            if (!rxFree(3)) break;
            pushRxDelayed(0xFD, kJoystickDelayCycles);
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
            // Paquet de 8 octets entier ou rien (CheckFreeCount(8), comme Hatari).
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles);
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
        // bloqué tant que ces commandes étaient des NOP. Chaque réponse (8 octets)
        // est gardée « paquet entier ou rien » (CheckFreeCount(8), comme Hatari).
        case 0x87: {                          // REPORT MOUSE BUTTON ACTION
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(0x07); pushRx(mouseAction_);
            for (int i = 0; i < 5; ++i) pushRx(0x00);
            break;
        }
        case 0x88: case 0x89: case 0x8A: {    // REPORT MOUSE MODE
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles);
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
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(0x0B);
            pushRx(uint8_t(xThreshold_)); pushRx(uint8_t(yThreshold_));
            for (int i = 0; i < 4; ++i) pushRx(0x00);
            break;
        case 0x8C:                            // REPORT MOUSE SCALE
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(0x0C);
            pushRx(uint8_t(xScale_)); pushRx(uint8_t(yScale_));
            for (int i = 0; i < 4; ++i) pushRx(0x00);
            break;
        case 0x8F: case 0x90:                 // REPORT MOUSE VERTICAL COORDINATES
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(yAxis_ == -1 ? 0x0F : 0x10);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x92:                            // REPORT MOUSE AVAILABILITY
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(mouseMode_ == OFF ? 0x12 : 0x00);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x94: case 0x95: case 0x99:      // REPORT JOYSTICK MODE
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(joyMode_ == JOY_AUTO ? 0x14 : 0x15);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        case 0x9A:                            // REPORT JOYSTICK AVAILABILITY
            if (!rxFree(8)) break;
            pushRxDelayed(0xF6, kResponseDelayCycles); pushRx(joyMode_ == JOY_OFF ? 0x1A : 0x00);
            for (int i = 0; i < 6; ++i) pushRx(0x00);
            break;
        default:
            // Opcodes restants : no-op — le parseur a déjà consommé les octets de
            // paramètre.
            break;
    }
    // Le délai d'une nouvelle réponse doit être installé avant de relancer une
    // ancienne file gelée ; Hatari ne recharge le TDR qu'au prochain bit SCI.
    if (resumed) armRx();
}

void Ikbd::resetHw() {
    // Les fronts déjà échus précèdent le reset du récepteur SCI.
    if (sched_) catchUpCpuTx(sched_->liveNow());
    trace("hardware-reset");
    sciRxState_ = sciRxByte_ = sciRxBitsLeft_ = 0;
    if (sched_ && aciaClockInitialized_) {
        // ACIA_Reset → Set_Timers(offset0) : prochain front un bit après reset.
        txClockPhase_ = sched_->liveNow() % kAciaTxBitCycles;
        txNextBitCycle_ = sched_->liveNow() + kAciaTxBitCycles;
        scheduleCpuTx();
    }
    // « Reset the SCI » (ikbd.c:503-513) : SCI_TX_State = IDLE, TSR = 0,
    // SCI_TX_Size/Delay = 0, SCI_RX_State = IDLE, RSR = 0, SCI_RX_Size = 0.
    // Côté NeoST le shift register de sortie du 6301 est matérialisé par
    // l'échéance IKBD_RX (octet en cours d'acheminement vers l'ACIA).
    rxPending_ = false;
    rxShiftData_ = 0;
    responseDelayBits_ = 0;
    rxWaitingDelay_ = false;
    if (sched_) sched_->cancel(Scheduler::IKBD_RX);
    // ACIA_Reset (Hatari acia.c:334-347) ne remet aucun registre ACIA :
    // CR/RDR/statut/erreurs, TDR/TSR et initialisation du diviseur persistent.
    // Le Scheduler NeoST n'est pas effacé par Machine::reset : conserver
    // l'échéance TX déjà armée, sans annuler ni recharger le TDR.
    // Le transit /64 8N1 est assemblé bit par bit, y compris après reset SCI.
    // Les autres formats/diviseurs gardent le repli historique 8N1/1024 cycles.
    bootRom();                       // IKBD_Boot_ROM(false) — reset à chaud
    raiseIfReady();                   // republie les causes ACIA après reset MFP
}

void Ikbd::bootRom() {
    trace("firmware-boot");
    // Warm-boot de la ROM IKBD ($F000) — port de Hatari IKBD_Boot_ROM(false),
    // commun au reset logiciel $80,$01 et à la sortie du mode Execute
    // (exitExeMode). Le reset à froid = état de construction (défauts du .hpp,
    // alignés sur IKBD_Boot_ROM) + horloge amorcée sur l'heure hôte.
    // L'IKBD fait son auto-test puis renvoie $F1 APRÈS ~502000 cycles (valeur
    // Hatari IKBD_RESET_CYCLES). Répondre INSTANTANÉMENT casse les diagnostics
    // qui arment l'IRQ ACIA puis attendent la réponse : l'IRQ serait levée
    // avant l'armement (donc perdue) → « Keyboard not responding ». On diffère
    // via l'ordonnanceur ; à défaut (pas de scheduler), repli immédiat.
    //
    // L'IKBD repart de ses défauts : souris RELATIVE, joystick AUTO, seuils 1,
    // pas d'échelle, axe Y vers le haut, état de bouton/joystick émis remis à zéro.
    // Le boot remet la période à 150000 sans déplacer une échéance active.
    if (sched_ && joystickMonitorPeriod_ > 0) {
        const auto remain = sched_->rawCyclesUntil(Scheduler::IKBD_MONITOR);
        if (remain != INT64_MIN)
            sched_->schedule(Scheduler::IKBD_AUTOSEND, sched_->liveNow() + remain);
        sched_->cancel(Scheduler::IKBD_MONITOR);
    }
    joystickMonitorPeriod_ = 0;
    startAutoSend();
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
    // Table d'état des touches effacée (ScanCodeState[] dans IKBD_Boot_ROM) :
    // toutes réputées relâchées après le boot.
    for (auto& s : scanState_) s = 0;
    // Commande multi-octets en cours abandonnée (nBytesInInputBuffer = 0).
    inBufLen_ = 0;
    cmdExpected_ = 0;
    // Drapeaux des quirks de la fenêtre de reset (cf. IKBD_Boot_ROM).
    mouseDisabled_ = joystickDisabled_ = false;
    mouseEnabledDuringReset_ = false;
    bothMouseAndJoy_ = false;
    pauseOutput_   = false;
    rx_.clear();                   // BufferHead=Tail=0 dans IKBD_Boot_ROM
    // ⚠ On ne purge QUE le côté 6301 : le RDR de l'ACIA (rdrf_), l'overrun et la
    // livraison en vol SURVIVENT au reset logiciel $80,$01 — Hatari (IKBD_Boot_ROM,
    // ikbd.c:578-581) ne touche que Keyboard.Buffer ; le SCI n'est resetté qu'au
    // reset MACHINE (IKBD_Reset). Le CPU peut encore lire le dernier octet reçu.
    duringResetCriticalTime_ = true;
    // Le boot annule tout code 6301 custom en cours (cf. Hatari
    // IKBD_Boot_ROM : LoadMemory/ExeMode coupés).
    exeMode_ = false; customRead_ = CR_NONE; customWrite_ = CW_NONE;
    memLoadLeft_ = 0;
    // L'horloge ($1B/$1C) est CONSERVÉE (reset à chaud, cf. Hatari).
    constexpr int64_t kIkbdResetCycles = 502000;
    if (sched_) sched_->schedule(Scheduler::IKBD, firmwareEventTime() + kIkbdResetCycles);
    else        onResetResponse();
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
        if (rxFree(2)) { pushRx(0xFE); pushRx(joy0); }   // paquet $FE entier ou rien
        prevJoy0_ = joy0;                     // PrevJoyData mis à jour quoi qu'il arrive (Hatari)
    }
    if (joy1 != prevJoy1_) {
        if (rxFree(2)) { pushRx(0xFF); pushRx(joy1); }   // paquet $FF entier ou rien
        prevJoy1_ = joy1;
    }
}

void Ikbd::sendAutoJoysticksMonitoring(uint8_t joy0, uint8_t joy1) {
    if (!rxFree(2)) return;                   // paquet monitoring (2 octets) entier ou rien
    pushRx(uint8_t(((joy0 & 0x80) >> 6) | ((joy1 & 0x80) >> 7)));
    pushRx(uint8_t(((joy0 & 0x0F) << 4) | (joy1 & 0x0F)));
}

void Ikbd::onJoystickMonitor() {
    if (!sched_ || joystickMonitorPeriod_ <= 0) return;
    const int64_t due = sched_->firingDue();
    sched_->schedule(Scheduler::IKBD_MONITOR,
                     (due >= 0 ? due : sched_->liveNow()) + joystickMonitorPeriod_);
    // $17 utilise le même traitement que l'auto-send : MouseAction et position
    // interne précèdent le report monitoring, même quand MouseMode vaut OFF.
    sendAutoInput();
}

void Ikbd::onVbl(int64_t vblMicro) {
    // 1) Avance l'horloge interne IKBD ($1B/$1C) du temps d'une trame.
    updateClock(vblMicro);
    ++vblCount_;
    if (!sched_) sendAutoInput();
}

void Ikbd::startAutoSend() {
    // IKBD_Boot_ROM n'écrase pas une échéance active : les RESET rapprochés
    // ne doivent pas repousser indéfiniment la scrutation (Hatari ikbd.c:620).
    if (sched_ && joystickMonitorPeriod_ == 0 &&
        sched_->cyclesUntil(Scheduler::IKBD_AUTOSEND) < 0)
        sched_->schedule(Scheduler::IKBD_AUTOSEND, firmwareEventTime() + 150000);
}

void Ikbd::onAutoSend() {
    if (joystickMonitorPeriod_ > 0) return;
    // Réarmement sur l'échéance dispatchée, pas sur l'horloge CPU live : une
    // instruction dépassant la frontière ne décale pas les scrutations suivantes.
    if (sched_) {
        const int64_t due = sched_->firingDue();
        sched_->schedule(Scheduler::IKBD_AUTOSEND,
                         (due >= 0 ? due : sched_->liveNow()) + 150000);
    }
    sendAutoInput();
}

void Ikbd::sendAutoInput() {
    if (duringResetCriticalTime_ || vblCount_ <= 20) return;
    // ExeMode : appel périodique du handler de lecture custom (cf. Hatari, qui
    // l'invoque dans le traitement clavier de trame), puis Δ souris consommé. Pas
    // de report joystick auto tant que le 6301 exécute du code custom.
    if (exeMode_) {
        // LATCH du Δ de la trame écoulée AVANT le dispatch : Hatari met à jour
        // DeltaX/DeltaY à chaque tick (IKBD_UpdateInternalMousePosition) et les
        // handlers custom (Froggies, Dragonnels) lisent un delta STABLE toute la
        // trame. Lire l'accumulation en cours puis l'effacer au VBL perdait le
        // mouvement injecté entre le poll du jeu et le clear (menu souris mou/mort
        // selon la phase).
        mLatchDX_ = mDeltaX_; mLatchDY_ = mDeltaY_;
        mDeltaX_ = mDeltaY_ = 0;
        customReadDispatch();
        return;
    }
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
    trace("host-key scan=$%02X pressed=%d mode=%s", scancode, pressed,
          joyMode_ == JOY_MONITOR ? "joystick-monitor" : exeMode_ ? "custom" : "normal");
    if (joyMode_ == JOY_MONITOR)
        return;
    // Suivi d'état (lu par les handlers 6301 custom) puis make/break.
    scanState_[scancode & 0x7F] = pressed ? 1 : 0;
    // En ExeMode, le scan clavier normal est supprimé (cf. Hatari IKBD_Cmd_Return_Byte) :
    // seul le handler custom peut émettre, via customReadDispatch().
    if (exeMode_) { customReadDispatch(); return; }
    if (!rxFree(1)) {
        trace("host-key-drop reason=queue-full");
        return;
    }
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
    // Garde 256 (Hatari boucle sans borne, l.1382) : 16 tronquait silencieusement un
    // Δ hôte > 2048/trame ; 256 couvre tout Δ 15 bits sans risque d'emballement.
    for (int guard = 0; guard < 256; ++guard) {
        int bx = dX; if (bx > 127) bx = 127; else if (bx < -128) bx = -128;
        int by = dY; if (by > 127) by = 127; else if (by < -128) by = -128;
        const bool overThr =
            (bx < 0 && bx <= -xThreshold_) || (bx > 0 && bx >= xThreshold_) ||
            (by < 0 && by <= -yThreshold_) || (by > 0 && by >= yThreshold_);
        const bool btnChanged = (bOldL_ != left) || (bOldR_ != right);
        if (!overThr && !btnChanged) break;
        // paquet $F8 (3 octets) entier ou rien ; tampon plein → ce paquet
        // est jeté mais le Δ reste décrémenté (comme Hatari).
        if (rxFree(3)) {
            pushRx(uint8_t(0xF8 | (right ? 0x01 : 0) | (left ? 0x02 : 0)));
            pushRx(static_cast<uint8_t>(static_cast<int8_t>(bx)));
            pushRx(static_cast<uint8_t>(static_cast<int8_t>(by * yAxis_)));
        }
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
        // place pour les DEUX scancodes possibles, sinon on jette le tout.
        if (!rxFree(2)) return;
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

void Ikbd::sendAbsMousePos(bool curL, bool curR, int delayCycles) {
    // Port de Hatari IKBD_Cmd_ReadAbsMousePos : nibble boutons (droite-bas=0x01 /
    // droite-haut=0x02 / gauche-bas=0x04 / gauche-haut=0x08), masqué par ce qui a
    // déjà été signalé (prevAbsButtons_), puis position X/Y sur 16 bits.
    uint8_t buttons = uint8_t((curR ? 0x01 : 0x02) | (curL ? 0x04 : 0x08));
    const uint8_t prev = prevAbsButtons_;
    prevAbsButtons_ = buttons;
    buttons &= uint8_t(~prev);
    if (!rxFree(6)) return;                   // paquet $F7 (6 octets) entier ou rien
    pushRxDelayed(0xF7, delayCycles);
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
        // chaque paire de flèches (2 octets) est gardée séparément ; tampon
        // plein → la paire est jetée mais le Δ est quand même décrémenté (Hatari).
        if (dX != 0) {
            if (dX <= -keyCodeDeltaX_) { if (rxFree(2)) { pushRx(75); pushRx(75 | 0x80); } dX += keyCodeDeltaX_; }  // gauche
            if (dX >=  keyCodeDeltaX_) { if (rxFree(2)) { pushRx(77); pushRx(77 | 0x80); } dX -= keyCodeDeltaX_; }  // droite
        }
        if (dY != 0) {
            if (dY <= -keyCodeDeltaY_) { if (rxFree(2)) { pushRx(72); pushRx(72 | 0x80); } dY += keyCodeDeltaY_; }  // haut
            if (dY >=  keyCodeDeltaY_) { if (rxFree(2)) { pushRx(80); pushRx(80 | 0x80); } dY -= keyCodeDeltaY_; }  // bas
        }
        if (rxFree(2)) {                      // paire de boutons gardée à part
            if (left && !bOldL_)       pushRx(0x74);
            else if (!left && bOldL_)  pushRx(0x74 | 0x80);
            if (right && !bOldR_)      pushRx(0x75);
            else if (!right && bOldR_) pushRx(0x75 | 0x80);
        }
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

bool Ikbd::rxFree(std::size_t n) const {
    // Place pour `n` octets dans le tampon de sortie de l'IKBD (port de Hatari
    // IKBD_OutputBuffer_CheckFreeCount). Capacité = SIZE_KEYBOARD_BUFFER = 1024 ;
    // seuls les octets EN ATTENTE comptent. Le SCI engagé et le RDR de l’ACIA
    // vivent hors de cette file (ikbd.c Check_New_TDR décrémente le compteur).
    // Un paquet teste sa taille totale avant le premier pushRx : entier ou rien.
    constexpr std::size_t kBufSize = 1024;
    return rx_.size() <= kBufSize && n <= kBufSize - rx_.size();
}

void Ikbd::pushRx(uint8_t b) {
    // IKBD_Send_Byte_Delay : avant l'initialisation ACIA, jeter les octets
    // firmware, sans les différer jusqu'à une future configuration.
    if (!aciaClockInitialized_) {
        trace("firmware-drop data=$%02X reason=clock-uninitialized", b);
        return;
    }
    if (duringResetCriticalTime_) {
        trace("firmware-drop data=$%02X reason=reset", b);
        return;
    }
    // File firmware bornée à 1024 octets (SIZE_KEYBOARD_BUFFER de Hatari) :
    // plein → l'octet est jeté. TOUS les émetteurs de paquets (événements ET
    // réponses de commande) testent rxFree() avant le 1er octet (jamais de
    // demi-paquet, cf. IKBD_OutputBuffer_CheckFreeCount) ; ce garde ne fait que
    // borner les pushRx isolés ($F1, handlers custom) — robustesse mémoire absolue.
    if (rx_.size() >= 1024) {
        trace("firmware-drop data=$%02X reason=queue-full", b);
        return;
    }
    rx_.push_back(b);
    trace("firmware-queue data=$%02X queued=%zu paused=%d", b, rx_.size(), pauseOutput_);
    armRx();
}

void Ikbd::pushRxDelayed(uint8_t b, int delayCycles) {
    if (!aciaClockInitialized_) return;
    // Le délai est global, pas associé à l'octet : une nouvelle réponse remplace
    // SCI_TX_Delay, même si une ancienne réponse attend déjà dans la file.
    if (duringResetCriticalTime_) return;
    if (sched_ && delayCycles > 0) {
        responseDelayBits_ = delayCycles / 1024;
        if (rxWaitingDelay_) {
            // Le SCI attend encore (pas de start bit) : remplacer le délai
            // restant, sans additionner l'attente de la commande précédente.
            sched_->schedule(Scheduler::IKBD_RX,
                             firmwareEventTime() + int64_t(responseDelayBits_) * 1024);
        }
    }
    pushRx(b);
    // Hatari pose SCI_TX_Delay avant la garde de capacité : les réponses custom
    // (Froggies) peuvent retarder la file même si leur en-tête est rejeté.
    armRx();
}

void Ikbd::armRx(int64_t baseCycle) {
    // Date la livraison du prochain octet de la file : ~1 octet série (10 bits à
    // 7812,5 bauds = 10240 cycles à 8 MHz, cadence du SCI d'Hatari). Le SCI de
    // l'IKBD shifte EN CONTINU, que le RDR soit lu ou non — c'est onRxDeliver qui
    // constate l'overrun si RDR est encore plein. PAUSE OUTPUT ($13) bloque
    // le prochain départ, sans interrompre une trame. Sans ordonnanceur (tests
    // unitaires), repli : livraison immédiate — qui ne peut PAS écraser un RDR
    // plein, sinon pushRx → perte immédiate (cf. garde rdrf_ dans onRxDeliver).
    if (rxPending_) return;
    constexpr int64_t kAciaRxByteCycles = 10240;
    // Le SCI décrémente son attente même sans octet ou sous PAUSE OUTPUT.
    // L'événement distinct préserve aussi l'octet déjà engagé sur la liaison.
    if (sched_ && responseDelayBits_ > 0) {
        rxPending_ = rxWaitingDelay_ = true;
        const int64_t anchor = baseCycle >= 0 ? baseCycle : firmwareEventTime();
        sched_->schedule(Scheduler::IKBD_RX, anchor + int64_t(responseDelayBits_) * 1024);
        return;
    }
    if (rx_.empty() || pauseOutput_) return;
    if (sched_) {
        // Check_New_TDR est appelé en SCI IDLE uniquement : un seul octet
        // sort de la file avant DATA/STOP, sans préchargement du suivant.
        rxShiftData_ = rx_.front();
        rx_.pop_front();
        rxPending_ = true;
        sched_->schedule(Scheduler::IKBD_RX,
                         (baseCycle >= 0 ? baseCycle : firmwareEventTime()) + kAciaRxByteCycles);
    } else if (!rdrf_) {
        onRxDeliver();
    }
}

void Ikbd::onRxDeliver() {
    const bool shifting = rxPending_ && !rxWaitingDelay_;
    rxPending_ = false;
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    if (rxWaitingDelay_) {
        rxWaitingDelay_ = false;
        responseDelayBits_ = 0;
        armRx(due);             // ancre théorique : le retard de dispatch ne s'ajoute pas
        return;
    }
    if (!shifting && (rx_.empty() || pauseOutput_)) {
        armRx(responseDelayBits_ > 0 ? due : -1);
        return;
    }
    // Le SCI est indépendant de PauseOutput et de la file firmware : même
    // après un boot ROM qui vide cette dernière, le stop bit arrive à l'ACIA.
    const uint8_t b = shifting ? rxShiftData_ : rx_.front();
    if (!shifting) rx_.pop_front();
    trace("acia-receive data=$%02X result=%s", b, rdrf_ ? "overrun-drop" : "rdr");
    if (rdrf_) {
        // RDR pas encore lu quand l'octet suivant finit d'arriver : OVERRUN. Le
        // nouvel octet est PERDU, RDR conserve l'ancien (cf. acia.c ACIA_Clock_RX,
        // état STOP_BIT). Le bit OVRN ne se posera qu'à la lecture de RDR ; en
        // attendant la cause d'IRQ RX reste active (RX_Overrun dans ACIA_UpdateIRQ).
        rxOverrun_ = true;
    } else {
        rdr_ = b;
        rdrf_ = true;
    }
    armRx(responseDelayBits_ > 0 ? due : -1); // attente globale au prochain SCI idle
    raiseIfReady(due);
}

bool Ikbd::irqActive() const {
    // Cause d'IRQ de l'ACIA 6850 (cf. acia.c ACIA_UpdateIRQ) : RX = octet LIVRÉ
    // (RDRF) OU overrun pendant, et RX int activé (RIE, bit7) ; TX = registre
    // d'émission vide et IRQ d'émission armée (TIE). CTS = GND sur l'ST →
    // toujours 0, omis (de même DCD).
    return ((rdrf_ || rxOverrun_) && (control_ & 0x80)) || (txEnableInt_ && tdre_);
}

void Ikbd::raiseIfReady(int64_t when) {
    // L'ACIA active sa ligne d'IRQ dès qu'une cause (RX ou TX) est active. On la
    // publie sur GPIP4 (lue par _int_acia pour vider l'ACIA) ; le canal 6 du MFP
    // est levé par le détecteur de FRONT du setter (règle AER, wire-OR avec le
    // MIDI) — pas de raise() manuel, sinon IPR reposé sans front réel (octet
    // doublé côté handlers qui ne testent pas le SR).
    if (when >= 0) mfp_.setAciaLineKbdAt(irqActive(), when);
    else mfp_.setAciaLineKbd(irqActive());
}

int64_t Ikbd::nextTxClock(int64_t at) const {
    // Hatari ACIA_Set_Timers_IKBD : la phase change avec le diviseur CR.
    // Port borné au format ST /64 8N1 ; les autres modes conservent le repli
    // historique, sans prétendre reconstruire la réception physique du 6301.
    const int64_t phase = (control_ & 0x1F) == 0x16 ? txClockPhase_ : 0;
    const int64_t elapsed = (at % kAciaTxBitCycles - phase + kAciaTxBitCycles)
                            % kAciaTxBitCycles;
    return at + kAciaTxBitCycles - elapsed;
}

void Ikbd::onTxEmpty() {
    if (!sched_) return;
    // Ancrage sur l'échéance servie, sans dérive liée au dépassement CPU.
    const int64_t due = sched_->firingDue();
    const int64_t at = due >= 0 ? due : sched_->liveNow();
    advanceTx(at);
}

// Récepteur 6301 : ikbd.c IKBD_SCI_Get_Line_RX. Tout zéro en IDLE est
// un start, huit bits LSB suivent ; un stop nul abandonne la trame.
void Ikbd::clockSciRx(bool bit) {
    if (sciRxState_ == 0) {
        if (!bit) { sciRxState_ = 1; sciRxByte_ = 0; sciRxBitsLeft_ = 8; }
    } else if (sciRxState_ == 1) {
        if (bit) sciRxByte_ |= 0x80;
        if (--sciRxBitsLeft_) sciRxByte_ >>= 1;
        else sciRxState_ = 2;
    } else {
        sciRxState_ = 0;
        if (bit) receiveSerialByte(sciRxByte_);
        else trace("sci-drop data=$%02X reason=invalid-stop", sciRxByte_);
    }
}

// Progresser aux fronts physiques sans installer un événement par bit. Au repos
// MARK, IDLE ne change plus ; sous BREAK, dix zéros ramènent IDLE (framing).
void Ikbd::catchUpCpuTx(int64_t at) {
    if (!sched_ || txNextBitCycle_ == 0) return;
    while (txNextBitCycle_ <= at) {
        const bool breaking = (control_ & 0x60) == 0x60;
        if (!txShifting_ && sciRxState_ == 0 && (tdre_ || breaking)) {
            const int64_t clocks = (at - txNextBitCycle_) / kAciaTxBitCycles + 1;
            const int64_t skip = breaking ? clocks / 10 * 10 : clocks;
            if (skip) {
                if (breaking) sciRxByte_ = 0; // un cycle complet de dix LOW abandonne RSR=0
                txNextBitCycle_ += skip * kAciaTxBitCycles;
                continue;
            }
        }
        bool bit = true;
        bool ended = false;
        if (txShifting_) {
            if (txBitIndex_ < 8) bit = (txShiftData_ >> txBitIndex_++) & 1;
            else { txShifting_ = false; txBitIndex_ = 0; ended = true; }
        } else if (breaking) bit = false;
        else if (!tdre_) {
            txShiftData_ = txData_; txShifting_ = true; txBitIndex_ = 0;
            tdre_ = true; bit = false; // start au transfert TDR→TSR
            // Publier dès le START, même si un accès CR/TDR rattrape ensuite
            // plusieurs bits ou caractères avant le dispatch ordonnanceur.
            raiseIfReady(txNextBitCycle_);
        }
        const int64_t front = txNextBitCycle_;
        txNextBitCycle_ = ended ? nextTxClock(txNextBitCycle_)
                                : txNextBitCycle_ + kAciaTxBitCycles;
        const int64_t previousContext = cpuSerialEventCycle_;
        cpuSerialEventCycle_ = front;
        clockSciRx(bit);
        cpuSerialEventCycle_ = previousContext;
        // Une commande reconstruite peut modifier les modes ou relancer le boot.
        // Les événements sont servis au plus tard au stop SCI (scheduleCpuTx).
    }
    scheduleCpuTx();
}

void Ikbd::scheduleCpuTx() {
    if (!sched_) return;
    sched_->cancel(Scheduler::IKBD_TX);
    if (!txNextBitCycle_) return;
    int64_t due = INT64_MAX;
    if (txShifting_) due = txNextBitCycle_ + (8 - txBitIndex_) * kAciaTxBitCycles;
    else if (!tdre_ && (control_ & 0x60) != 0x60) due = txNextBitCycle_;
    if (sciRxState_ && (txShifting_ || (control_ & 0x60) != 0x60)) {
        const int64_t stop = txNextBitCycle_ + sciRxBitsLeft_ * kAciaTxBitCycles;
        due = std::min(due, stop);
    }
    if (due != INT64_MAX) sched_->schedule(Scheduler::IKBD_TX, due);
}

void Ikbd::advanceTx(int64_t at) { catchUpCpuTx(at); }

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
        { 165, 0x5617c33cu, CR_TRANSB2,  CW_IGNORE     },   // Transbeauce 2 (écritures ignorées)
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
            // Comme bootRom : le flush ne touche QUE le côté 6301 (Hatari
            // ikbd.c:2980-2981 ne vide que Keyboard.Buffer) — RDR/RDRF/overrun de
            // l'ACIA survivent, la livraison en vol se no-op sur file vide.
            return;
        }
    }
    // Pas (encore) de correspondance : on continue d'accumuler.
}

void Ikbd::customWriteDispatch(uint8_t v) {
    switch (customWrite_) {
        case CW_BOOT:       commonBoot(v);          break;
        // Transbeauce 2 : le handler d'écriture IGNORE les octets (cf. Hatari
        // IKBD_CustomCodeHandler_Transbeauce2Menu_Write) — en ExeMode ils ne
        // doivent JAMAIS retomber dans le parseur de commandes standard
        // (IKBD_Process_RDR court-circuite IKBD_RunKeyboardCommand).
        case CW_IGNORE:                             break;
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
    // Sortie du mode Execute (le 6301 fait jmp $f000) : Hatari appelle
    // IKBD_Boot_ROM(false) — même warm-boot que le reset $80,$01 : remise aux
    // défauts des modes souris/joystick, vidage des tampons, effacement de
    // scanState_, fenêtre critique puis réponse $F1 différée (~502000 cycles).
    bootRom();
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
    if (mLatchDY_ < 0) res80 = 0x7A;               // Δ LATCHÉ au VBL (stable toute la trame)
    if (mLatchDY_ > 0) res80 = 0x06;
    if (mLatchDX_ < 0) res81 = 0x7A;
    if (mLatchDX_ > 0) res81 = 0x06;
    if (lmb_)         res82 |= 0x80;
    if (scanState_[0x48]) res80 |= 0x7A;           // flèche haut
    if (scanState_[0x50]) res80 |= 0x06;           // flèche bas
    if (scanState_[0x4B]) res81 |= 0x7A;           // flèche gauche
    if (scanState_[0x4D]) res81 |= 0x06;           // flèche droite
    if (scanState_[0x70]) res82 |= 0x80;           // pavé numérique 0
    res80 |= res82;                                // bit7 = bouton gauche
    res81 |= res82;
    if (v == 1)      pushRx(res80);                // demande 1 octet
    else if (v == 4) { pushRxDelayed(res83, 7000); pushRx(res82); pushRx(res81); pushRx(res80); }
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
    if (mLatchDY_ < 0) res = 0xFC;                 // Δ latché au VBL (cf. froggiesWrite)
    if (mLatchDY_ > 0) res = 0x04;
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
