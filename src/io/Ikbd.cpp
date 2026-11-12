// =============================================================================
//  Ikbd.cpp — ACIA 6850 + file de scancodes IKBD.
//  Références de l'ACIA : voir l'en-tête d'Ikbd.hpp (MC6850.pdf, sha256 294a8b9f).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Ikbd.hpp"
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"   // kCpuHzInt — UNE horloge CPU pour tout l'arbre
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <algorithm>

// [B] MC6850 Table 1 p. 7 : bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_OVRN = 0x20,   // [B] MC6850 § Receiver Overrun p. 8-9 : octet(s) perdu(s), posé à la lecture RDR
    ACIA_IRQ  = 0x80,   // [B] MC6850 § Status Register p. 9 : état de la sortie IRQ (vers GPIP4 du MFP)
};
// [D] DCD/CTS à la masse sur le ST : Hatari acia.c:38-39 @ f0736b24, mais :123,408,422 disent « not
// connected ». Hatari se contredit. [B] MC6850 § Peripheral/Modem Control p. 7, § Status Register p. 8 : CTS
// haut inhiberait TDRE et DCD haut le récepteur ; or la liaison clavier fonctionne, donc les deux sont
// bas et les bits 2/3 toujours 0. [F] Le câblage exact : tranché par le schéma de la carte ST, absent
// d'extern/datasheets.
// [B] MC6850 § Status Register p. 8-9 : FE signale l'absence du premier stop, PE une parité fausse. Ils
// n'arrivent jamais ici : la liaison émulée ne produit ni erreur de trame ni erreur de parité.

// Liaison CPU → IKBD : grille 7812,5 bauds, 8N1. [D] Hatari acia.c:126,465 @ f0736b24 : 500 kHz/64, 16 cycles
// CPU par tic, soit 1024 cycles par bit. ACIA_Write_TDR /
// ACIA_Clock_TX ([B] MC6850 § Transmit Data Register p. 7) : TDRE remonte au transfert TDR→TSR ; ikbd.c
// SCI_Get_Line_RX ne livre la commande qu'au bit de stop.
// ⚠ [B] MC6850 § Counter Divide Select Bits, § Word Select Bits p. 8 : CR0-4 régissent l'émetteur ET le
// récepteur. Format/diviseur restent ici bornés au protocole clavier ST usuel (CR=$96/$B6, soit /64 8N1) :
// autre réglage, même débit et aucun effet sur RDR : écart non corrigé. Break inhibe les départs.
static constexpr int64_t kAciaTxBitCycles = 1024;
// Milieux déterministes des fourchettes IKBD_Delay_Random d'Hatari ([D] ikbd.c:971 @ f0736b24). Le SCI
// quantifie ensuite par division entière en bits de 1024 cycles ([D] ikbd.c:1035 @ f0736b24).
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

// Ordre de balayage de la matrice au reset (ROM $F086-$F0C5), extrait de la ROM :
// colonnes 1 à 15 ; colonnes 1-4 : rangée 0 ($F311[col], F1-F3) avant le
// modificateur de la colonne ($F206 : Ctrl, Shift G, Alt, Shift D, rangées 4-7) ;
// colonnes 5-15 : rangée la plus basse, $F311[(col-4)*8 + rangée].
static constexpr uint8_t kResetScanOrder[] = {
    0x1D, 0x3B, 0x2A, 0x3C, 0x38, 0x3D, 0x36, 0x3E, 0x01, 0x02, 0x0F, 0x10, 0x1E, 0x60,
    0x2C, 0x3F, 0x03, 0x04, 0x11, 0x12, 0x1F, 0x20, 0x2D, 0x40, 0x05, 0x06, 0x13, 0x14,
    0x21, 0x2E, 0x2F, 0x41, 0x07, 0x08, 0x15, 0x22, 0x23, 0x30, 0x31, 0x42, 0x09, 0x0A,
    0x16, 0x17, 0x24, 0x25, 0x32, 0x43, 0x0B, 0x0C, 0x18, 0x19, 0x26, 0x33, 0x39, 0x44,
    0x0D, 0x29, 0x1A, 0x1B, 0x27, 0x34, 0x3A, 0x62, 0x0E, 0x53, 0x52, 0x2B, 0x1C, 0x28,
    0x35, 0x61, 0x48, 0x47, 0x4B, 0x50, 0x4D, 0x6D, 0x70, 0x63, 0x64, 0x67, 0x68, 0x6A,
    0x6B, 0x6E, 0x71, 0x65, 0x66, 0x69, 0x4A, 0x6C, 0x4E, 0x6F, 0x72,
};

void Ikbd::onResetResponse() {
    duringResetCriticalTime_ = false;
    // ROM $F0A3-$F0CD : une touche tenue pendant l'auto-test → UN seul code de
    // relâchement (scancode | $80), celui de la première touche du balayage, envoyé
    // juste avant $F1.
    for (uint8_t sc : kResetScanOrder)
        if (hostKeys_[sc]) { pushRx(uint8_t(sc | 0x80)); break; }
    pushRx(0xF1);
    ocfOrigin_ = firmwareEventTime();         // le premier OCR est posé juste avant le cli
    // ROM $F139 (cli) : l'octet resté dans le RDR pendant l'init est traité ; s'il y
    // en a eu deux, la routine d'overrun $FF40 a tout jeté.
    const int held = heldOverrun_ ? -1 : heldByte_;
    heldByte_ = -1;
    heldOverrun_ = false;
    if (held >= 0) {
        trace("reset-window held=$%02X executed", held);
        receiveSerialByte(uint8_t(held));
    }
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
    // Comme write8 et MidiAcia::read8 : un front TX passé mais pas encore dispatché
    // (lecture en cours d'instruction) a déjà transféré TDR → TSR. Sans ce rattrapage,
    // le statut rendait un TDRE périmé à 0 ([B] MC6850 § Status Register p. 8 : TDRE
    // remonte au transfert).
    if (sched_) catchUpCpuTx(sched_->liveNow());
    if ((addr & 2) == 0) {
        // $FFFC00 : statut, [B] MC6850 § Status Register p. 8-9.
        // TDRE = 0 de l'écriture de TDR au transfert vers TSR, 1 sinon, que TIE soit armé ou non ;
        // RDRF si un octet a été LIVRÉ (cadence série, cf. onRxDeliver) et pas
        // encore lu ; OVRN si un overrun a été constaté à la lecture de RDR ;
        // IRQ si une cause RX ou TX est active. Sous PAUSE OUTPUT ($13), la
        // livraison est gelée → RDRF ne remonte plus (octets en file). Lire SR
        // est mémorisé (srRead_) : la prochaine lecture de RDR effacera OVRN
        // ([B] MC6850 § Interrupt Request p. 6 : séquence SR→RDR ; [D] Hatari acia.c:768 @ f0736b24).
        // [B] MC6850 § Interrupt Request p. 6 : TDRE est inhibé « being maintained in the Reset
        // condition » ; § Counter Divide Select Bits p. 8 : le master reset efface le SR. tdre_
        // reste vrai en interne : TDRE se relit à 1 dès la sortie du reset.
        uint8_t s = (tdre_ && (control_ & 0x03) != 0x03) ? ACIA_TDRE : 0;
        if (rdrf_)       s |= ACIA_RDRF;
        if (ovrn_)       s |= ACIA_OVRN;
        if (irqActive()) s |= ACIA_IRQ;
        srRead_ = true;
        return s;
    }
    // $FFFC02 : lecture de la donnée → consomme l'octet livré (efface RDRF) et
    // gère l'overrun (port de Hatari ACIA_Read_RDR, [D] acia.c:856-881 @ f0736b24) : si SR a été lu avant, OVRN
    // retombe ; si un overrun est PENDANT (octet perdu pendant que RDR était
    // plein, cf. onRxDeliver), OVRN se pose MAINTENANT — pas au moment de la
    // perte ([B] MC6850 § Receiver Overrun p. 8-9). À vide, le RDR conserve le dernier octet reçu
    // ([B] MC6850 § Receive Data Register p. 7).
    // ⚠ [B] MC6850 § Receiver Overrun p. 8-9 : « The RDRF bit remains set until the Overrun is reset ».
    // Effacer RDRF ici dès le caractère valide suit [D] Hatari acia.c:858 @ f0736b24. Essai du 2026-11-04 : RDRF
    // tenu jusqu'à SR puis RDR, Enchanted Land, Lemmings, Dynamite Dux, Lethal Xcess et Magic Pockets restent
    // bloqués sur leurs premiers écrans, l'IRQ clavier ne retombant plus. Ne pas appliquer sans relevé sur un 6850.
    // [F] § Receiver Overrun p. 9 ne cite que la lecture de RDR pour remettre OVRN, § Interrupt Request
    // p. 6 exige SR puis RDR. Tranché par : deux lectures de RDR sans SR après un overrun sur un vrai 6850.
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
    // ⚠ [B] MC6850 § Interrupt Request p. 6, § Receiver Overrun p. 8-9 : l'IRQ ne tomberait qu'à SR puis RDR. Écart gardé, cf. ci-dessus.
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
        // $FFFC00 : registre de contrôle ([B] MC6850 § Control Register p. 8 : diviseur, format, bit7 =
        // RX int enable).
        // [B] MC6850 § Transmitter Control Bits p. 8 : bits 5-6 = contrôle émetteur : 01 arme l'IRQ d'émission
        // (TIE), comme [D] Hatari acia.c:820-823 @ f0736b24. TIE ne décide QUE de l'interruption : le
        // temps de transmission, lui, est modélisé dans tous les cas (cf. write
        // des données plus bas) — un pilote qui SCRUTE TDRE est freiné comme un
        // pilote qui interrompt (jeu armant CR=$b6 au lieu de $96, Hades Nebula).
        const bool wasBreak = (control_ & 0x60) == 0x60;
        // [D] Hatari acia.c:795-805,452-455 @ f0736b24 ACIA_Write_CR/ACIA_Set_Timers_IKBD : seul un
        // changement de CR0..1 non reset redémarre le timer série. [F] Fiche muette sur ce compteur :
        // il pourrait tourner librement. Tranché par : une capture de Tx Data autour d'une écriture de CR
        // sur un vrai 6850. La tranche CPU→IKBD suit tous les formats du CR au /64 ;
        // mémoriser sa phase TX.
        const bool dividerChanged = (v & 3) != 3 && (v & 3) != (control_ & 3);
        if (dividerChanged) {
            // [D] Hatari acia.c:801-804 @ f0736b24 ACIA_Write_CR : seul un changement du diviseur hors
            // master reset initialise Clock_Divider. Le reset le conserve.
            aciaClockInitialized_ = true;
            txClockPhase_ = sched_ ? sched_->liveNow() % kAciaTxBitCycles : 0;
        }
        control_ = v;
        txEnableInt_ = ((v & 0x60) == 0x20);
        // ⚠ Changer les bits d'émission ne remet PAS l'émetteur au repos : seul le
        // master reset le fait ([B] MC6850 § Counter Divide Select Bits p. 8). L'ancien « TIE coupé → tdre_ = true » rendait
        // l'émetteur prêt à chaque écriture du CR, ce qui annulait le temps de
        // transmission pour un pilote retouchant le CR entre deux octets.
        // [B] MC6850 § Counter Divide Select Bits p. 8 : bits 0-1 = 11 → master reset de l'ACIA, qui efface le
        // SR hors CTS/DCD et initialise récepteur et émetteur.
        // [D] Hatari acia.c:669-705 @ f0736b24 ACIA_MasterReset : SR repart à TDRE seul → RDRF/OVRN effacés.
        // TDRE se lit à 0 tant que CR0-1=11 (cf. read8) ; divergence avec [D] Hatari acia.c:681 @ f0736b24.
        // [B] MC6850 § Master Reset p. 5 : IRQ tenue haute, cf. irqActive. [D] Hatari acia.c:727-730 @ f0736b24 la lève sous CR=%x01xxx11.
        // La file IKBD et la livraison
        // en vol sont CONSERVÉES (elles vivent côté 6301, pas côté ACIA).
        // srRead_ : [D] Hatari acia.c:669-705 @ f0736b24 n'y touche pas, MidiAcia.cpp non plus. Sans effet observable :
        // ovrn_ ne se pose qu'en lecture de RDR, qui vient de consommer srRead_.
        // ⚠ [B] MC6850 § Counter Divide Select Bits p. 8 : le récepteur est initialisé, et [D] Hatari acia.c:688-691
        // @ f0736b24 abandonne le caractère en cours. Ici l'octet en vol arrive entier : le SCI du 6301 continue d'émettre,
        // et ne pas décaler l'octet suivant demande un champ d'état de plus. Aucun titre de la logithèque n'est touché.
        if ((v & 0x03) == 0x03) {
            rdrf_ = false;
            rxOverrun_ = ovrn_ = srRead_ = false;
            tdre_ = true;
            txShifting_ = false; txBitIndex_ = 0;
        }
        // [B] MC6850 § Transmitter Control Bits p. 8 : CR6=CR5=1 émet un Break.
        // [D] Hatari acia.c:965-970 @ f0736b24 ACIA_Clock_TX : break n'agit qu'au repos. Le TSR engagé finit
        // normalement ; le TDR reste plein tant que CR demande le break.
        // [F] Fiche muette sur le caractère en cours, et MiSTery gen_uart.v:296 @ a00deb5 force la ligne à 0
        // aussitôt. Tranché par : une capture de Tx Data sur un vrai 6850 pendant un caractère.
        // Le niveau bas est vu par le SCI : trame nulle, erreur de trame et gestionnaire
        // ORFE de la ROM ($FF40), puis reset si le BREAK dure (cf. sciWakeCleared).
        if (sched_) {
            if (!txShifting_ && (wasBreak || dividerChanged))
                txNextBitCycle_ = nextTxClock(sched_->liveNow());
            scheduleCpuTx();
        }
        raiseIfReady();
        return;
    }
    // Master reset MAINTENU (CR1-0 = 11) : l'émetteur est tenu initialisé, l'octet est JETÉ
    // (ni TDR plein, ni départ à la sortie du reset), comme dans MidiAcia::write8.
    // [B] Motorola MC6850 § Counter Divide Select Bits p. 8 (« initializes both the receiver and
    //     transmitter ») et § Interrupt Request p. 6 (« maintained in the Reset condition ») : TDRE
    //     se relit à 1, TDR vide, dès la sortie — garder l'octet ferait partir un TDR déclaré vide.
    // [D] MiSTery gen_uart.v:299-318 @ a00deb5 : reset logiciel en impulsion, l'écriture y partirait.
    //     Grade inférieur, non retenu. Sans cette garde, le 6301 recevait ET exécutait la commande.
    if ((control_ & 0x03) == 0x03) {
        trace("acia-write data=$%02X result=reset-drop", v);
        return;
    }
    trace("acia-write data=$%02X tdr-full=%d", v, !tdre_);
    if (!sched_) {
        // Repli synchrone pour les tests nus du protocole uniquement.
        receiveSerialByte(v);
        return;
    }
    // [B] MC6850 § Transmit p. 5-6 : un TDR et un TSR, jamais une file illimitée.
    // [D] Hatari acia.c:893-899 @ f0736b24 : une écriture prématurée
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
    // Initialisation de la ROM (sei $F010 … cli $F139) : la liaison n'est pas lue.
    // Un octet attend dans le RDR ; le suivant déborde et tout est perdu.
    if (duringResetCriticalTime_) {
        if (heldByte_ < 0 && !heldOverrun_) heldByte_ = v;
        else { heldByte_ = -1; heldOverrun_ = true; }
        trace("reset-window data=$%02X %s", v, heldOverrun_ ? "overrun" : "held");
        return;
    }
    // [C] ROM $FF13, $FF33-$FF3D : paramètres restants d'une commande avortée par une
    // erreur de trame, jetés un à un.
    if (sciDiscard_ > 0) {
        --sciDiscard_;
        trace("sci-discard data=$%02X left=%d", v, int(sciDiscard_));
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
    // Levée de la pause ($13) : seules les commandes qui sortent par $F91B de la
    // ROM la lèvent. Les rapports d'état, $13, un $0D hors mode absolu, un $16
    // refusé, $80 xx (xx ≠ $01) et les opcodes inconnus sortent par $F929 : la
    // pause reste.
    bool resumes = cmdLength(opcode) != 0 && opcode != 0x13 && opcode < 0x80;
    switch (opcode) {
        case 0x80:
            // Reset 0x80,0x01 : warm-boot de la ROM IKBD — remise aux défauts +
            // réponse $F1 différée. Tout autre paramètre est ignoré ($F929).
            if (inBuf_[1] == 0x01)
                bootRom();
            break;
        case 0x08:
            // $08 = SET RELATIVE MOUSE POSITION REPORTING (cf. Hatari
            // IKBD_Cmd_RelMouseMode) : retour aux paquets relatifs $F8 (mode par
            // défaut). Sans ce case, un jeu repassant en relatif depuis ABS/CURSOR/
            // OFF gardait l'ancien mode → souris muette ou flèches parasites.
            selectMouseMode(REL);
            break;
        case 0x09:
            // $09 = SET ABSOLUTE MOUSE POSITION REPORTING (cf. Hatari
            // IKBD_Cmd_AbsMouseMode) : bascule en mode absolu ; les bornes MaxX/
            // MaxY sont inclusives (octets MSB/LSB). Plus de paquet $F8 émis.
            selectMouseMode(ABS);
            // ROM $FACB : seules les bornes ($AA-$AD) sont copiées. La position
            // ($B6-$B9) n'est PAS remise à zéro, contrairement à ce que dit la doc
            // du protocole (Hatari avait raison) ; un $09 qui ne change que les
            // bornes ne ramène pas le curseur à l'origine.
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
            // ROM $FB39 : répond seulement en mode absolu, souris active et port 0
            // à la souris. Les boutons sont les ÉVÉNEMENTS cumulés depuis le
            // dernier $0D (registre $C2), remis à zéro par la lecture.
            if (mouseMode_ != ABS) { resumes = false; break; }
            sendAbsMousePos(prevAbsButtons_, 18000 - 7200); // délai : IKBD_Cmd_ReadAbsMousePos
            prevAbsButtons_ = 0;
            break;
        case 0x07:
            // $07 = SET MOUSE BUTTON ACTION (cf. Hatari IKBD_Cmd_MouseAction) :
            // bit0 = report position abs à l'appui, bit1 = au relâchement, bit2 =
            // boutons remontés comme scancodes. Remet le cache de boutons abs à la
            // valeur « rien à signaler » (ABS_PREVBUTTONS = 0x0A).
            mouseAction_   = inBuf_[1];
            break;
        case 0x0A:
            // $0A = SET MOUSE KEYCODE MODE (cf. Hatari IKBD_Cmd_MouseCursorKeycodes) :
            // les mouvements deviennent des flèches clavier ; octets 1/2 = pas X/Y.
            selectMouseMode(CURSOR);
            // Octet BRUT, 0 permis : avec un pas de 0, la ROM émet des flèches en
            // continu ($F5D2, cf. sendCursorKeys).
            keyCodeDeltaX_ = inBuf_[1];
            keyCodeDeltaY_ = inBuf_[2];
            break;
        case 0x12:
            // $12 = DISABLE MOUSE (cf. Hatari IKBD_Cmd_TurnMouseOff) : plus aucun
            // paquet souris émis jusqu'au prochain mode souris. Le feu joystick et
            // les boutons souris se rejoignent alors sur les mêmes lignes (cf.
            // duplication des boutons dans onVbl).
            mouseOn_ = false;                 // ROM $FB88 : efface le bit 7 de $C9
            syncMouseMode();
            break;
        case 0x11:
            // $11 = RESUME (cf. Hatari IKBD_Cmd_StartKeyboardTransfer) : lève la
            // pause de sortie — déjà fait par le « toute commande valide lève la
            // pause » ci-dessus ; case explicite pour la lisibilité.
            break;
        case 0x13:
            // $13 = PAUSE OUTPUT (ROM $F9AF) : mémorise l'index d'écriture du
            // tampon — les octets DÉJÀ en file sortent encore, les suivants
            // attendent. Pendant $17, le décompte de surveillance est gelé
            // ($FEAC). Le cas « Just Buggin' » ($13 pendant le reset) est couvert
            // par la fenêtre de reset (octets perdus par overrun).
            if (!pauseOutput_) {
                pauseOutput_ = true;
                pauseKeep_ = uint32_t(rx_.size());
                if (joyMode_ == JOY_MONITOR && sched_) {
                    const auto remain = sched_->rawCyclesUntil(Scheduler::IKBD_MONITOR);
                    if (remain != INT64_MIN) {
                        monitorFrozen_ = std::max<int64_t>(remain, 0);
                        sched_->cancel(Scheduler::IKBD_MONITOR);
                    }
                }
            }
            break;
        case 0x1B:
            // $1B = SET CLOCK (cf. Hatari IKBD_Cmd_SetClock) : 6 octets BCD
            // (YY MM DD hh mm ss). Les octets non BCD sont ignorés, les autres pris.
            for (int i = 1; i <= 6; ++i)
                if (bcdCheck(inBuf_[i])) clock_[i - 1] = inBuf_[i];
            clockMicro_ = 0;                  // ROM $FA88 : compteur de ms rechargé
            clockSet_ = true;                 // $83 ≠ 0 : l'interruption 1 ms tient l'horloge
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
            // ROM $FB5F : efface les événements boutons et passe en mode ABSOLU,
            // souris active ($C9 = $C9 & $0F | $A0) — le bit 1 (port 0) est gardé.
            prevAbsButtons_ = 0;
            lastMouseMode_ = ABS;
            mouseOn_ = true;
            syncMouseMode();
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
            // $14 = SET JOYSTICK EVENT REPORTING ($CA = $28) : mode événement, et
            // le port 0 passe au joystick ($FA47) — la souris se tait, le feu du
            // joystick passe dans le paquet $FF au lieu du bouton droit souris.
            stopJoystickSampling();
            joyMode_ = JOY_AUTO;
            joysticksEnabled_ = true;         // toute commande de mode joystick les rallume
            port0ToJoystick();
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
            // retour au mode interrogation seule ($16). Plus de report spontané,
            // mais les joysticks restent DISPONIBLES ($9A rapporte $00, pas $1A).
            // ROM 6301 : $15 passe par le même $FA47 que $14 et rend donc aussi
            // le port 0 au joystick — la souris se tait (Hatari la laisse active).
            stopJoystickSampling();
            joyMode_ = JOY_OFF;
            joysticksEnabled_ = true;
            port0ToJoystick();
            break;
        case 0x1A:
            // $1A = DISABLE JOYSTICKS (ROM $FA41) : efface le seul bit 5 de $CA.
            // Plus aucun report joystick, mais le MODE est gardé ($94 répond
            // toujours $14 après $14 puis $1A).
            joysticksEnabled_ = false;
            break;
        case 0x17:
            // $17 = SET JOYSTICK MONITORING (cf. Hatari IKBD_Cmd_SetJoystickMonitoring) :
            // octet 1 = taux en 1/100 s, zéro ramené à un ; même base fixe
            // 8021247 Hz que Hatari. Cette cadence reste commune à tous les
            // reports jusqu’au boot ROM, même après sortie du mode monitoring.
            // Taux 0 : la ROM ($F778) envoie en continu, soit au débit de la ligne
            // (2 octets par échantillon).
            joyMode_ = JOY_MONITOR;
            joysticksEnabled_ = true;
            port0ToJoystick();
            monitorFrozen_ = -1;
            if (sched_) sched_->cancel(Scheduler::IKBD_AUTOSEND);
            if (inBuf_[1]) {
                monitorContinuous_ = false;
                joystickMonitorPeriod_ = int64_t(8021247) * inBuf_[1] / 100;
                if (sched_) sched_->schedule(Scheduler::IKBD_MONITOR,
                                             firmwareEventTime() + joystickMonitorPeriod_);
            } else {
                // Taux 0 : un échantillon à chaque ligne libre (cf. onLineIdle).
                monitorContinuous_ = true;
                joystickMonitorPeriod_ = 0;
                if (sched_) sched_->cancel(Scheduler::IKBD_MONITOR);
            }
            break;
        case 0x18:
            // $18 = SET FIRE BUTTON MONITORING (ROM $FA12, $CA = $21) : le feu du
            // joystick 1 est échantillonné en continu, 8 échantillons par octet
            // (le plus ancien en bit 7, 1 = enfoncé), écrits directement dans le
            // TDR quand la ligne est libre. Port 0 au joystick, souris muette.
            stopJoystickSampling();
            joyMode_ = JOY_FIRE;
            joysticksEnabled_ = true;
            port0ToJoystick();
            fireShift_ = 0;                   // ROM $FA19 : $A4 = 0, $A5 = 8
            fireCount_ = 8;
            joystickMonitorPeriod_ = kFireByteCycles;   // drapeau « échantillonnage propre »
            if (sched_) sched_->cancel(Scheduler::IKBD_AUTOSEND);
            if (sched_) sched_->schedule(Scheduler::IKBD_MONITOR, firmwareEventTime());
            break;
        case 0x19:
            // $19 = SET JOYSTICK KEYCODE MODE (ROM $FA21, $CA = $24) : le joystick 0
            // produit des flèches (cf. sendKeycodeJoystick). Paramètres RX RY TX TY
            // VX VY en dixièmes de seconde.
            stopJoystickSampling();
            // ROM $FA2E-$FA34 : $94, $A1 et les prédiviseurs $A2/$A3 remis à zéro.
            for (int i = 0; i < 6; ++i) kjParam_[i] = inBuf_[1 + i];
            kjKey_[0] = kjKey_[1] = 0;
            kjStart_[0] = kjStart_[1] = kjLast_[0] = kjLast_[1] = 0;
            kjFire_ = false;
            {
                // $9B (feux) est tenu à jour dans tous les modes ($F186) : l'état courant
                // est déjà accepté. $9F n'est suivi que lorsque le port 0 sert un
                // joystick ($F681). [F] Quartet du joystick 0 pris au repos à l'entrée :
                // un manche déjà tenu passe par l'anti-rebond de 4 ms. Tranché par la
                // valeur de $9F laissée par le mode précédent, relevée sur un IKBD.
                uint8_t j0 = 0, j1 = 0;
                const bool wasJoy = port0Joy_;
                port0Joy_ = true;
                readJoystickState(j0, j1);
                port0Joy_ = wasJoy;
                kjFireStable_ = kjFireCand_ = (j0 & 0x80) != 0;
                kjDirStable_ = kjDirCand_ = 0;
                kjFireSince_ = kjDirSince_ = -1;
            }
            joyMode_ = JOY_KEYCODE;
            joysticksEnabled_ = true;
            port0ToJoystick();
            joystickMonitorPeriod_ = kKeycodePollCycles;   // drapeau « scrutation propre »
            if (sched_) sched_->cancel(Scheduler::IKBD_AUTOSEND);
            if (sched_) sched_->schedule(Scheduler::IKBD_MONITOR, firmwareEventTime());
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
            // ROM $F9CC-$F9DE : refusé si les joysticks sont désactivés ou hors des
            // modes événement/interrogation ; joystick 0 lu à 0 si le port 0 sert
            // la souris.
            if (!joysticksEnabled_ || (joyMode_ != JOY_AUTO && joyMode_ != JOY_OFF)) {
                resumes = false;
                break;
            }
            uint8_t joy0 = hostJoy_[0], joy1 = hostJoy_[1];
            if (joyProbe_) joyProbe_(joy0, joy1);
            if (!port0Joy_) joy0 = 0;
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
        // --- Commandes de RAPPORT d'état $87-$9A (ROM $FC59-$FCFB) ---
        // Réponse : $F6 + octet du réglage + paramètres, bourrée à 8 octets. Ignorées
        // (sans réponse, $F90A/$F90F) pendant la pause et pendant les surveillances
        // $17/$18 ($CA bits 0-1).
        case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C:
        case 0x8F: case 0x90: case 0x92: case 0x94: case 0x95: case 0x99:
        case 0x9A: {
            if (pauseOutput_ || joyMode_ == JOY_MONITOR || joyMode_ == JOY_FIRE) break;
            switch (opcode) {
                case 0x87:                    // REPORT MOUSE BUTTON ACTION
                    statusReply(0x07, &mouseAction_, 1);
                    break;
                case 0x88: case 0x89: case 0x8A: {    // REPORT MOUSE MODE
                    // ROM $FC62 : seuls les bits 6 (keycode) et 5 (absolu) de $C9
                    // comptent ; souris coupée, le dernier mode choisi est rapporté.
                    if (lastMouseMode_ == ABS) {
                        const uint8_t b[4] = { uint8_t(absMaxX_ >> 8), uint8_t(absMaxX_),
                                               uint8_t(absMaxY_ >> 8), uint8_t(absMaxY_) };
                        statusReply(0x09, b, 4);
                    } else if (lastMouseMode_ == CURSOR) {
                        const uint8_t b[2] = { uint8_t(keyCodeDeltaX_), uint8_t(keyCodeDeltaY_) };
                        statusReply(0x0A, b, 2);
                    } else {
                        statusReply(0x08);
                    }
                    break;
                }
                case 0x8B: {                  // REPORT MOUSE THRESHOLD
                    const uint8_t b[2] = { uint8_t(xThreshold_), uint8_t(yThreshold_) };
                    statusReply(0x0B, b, 2);
                    break;
                }
                case 0x8C: {                  // REPORT MOUSE SCALE
                    const uint8_t b[2] = { uint8_t(xScale_), uint8_t(yScale_) };
                    statusReply(0x0C, b, 2);
                    break;
                }
                case 0x8F: case 0x90:         // REPORT MOUSE VERTICAL COORDINATES
                    statusReply(yAxis_ == -1 ? 0x0F : 0x10);
                    break;
                case 0x92:                    // REPORT MOUSE AVAILABILITY : bit 7 de $C9 ($FCA2)
                    statusReply(mouseOn_ ? 0x00 : 0x12);
                    break;
                case 0x94: case 0x95: case 0x99:      // REPORT JOYSTICK MODE ($FCAE)
                    if (joyMode_ == JOY_KEYCODE) statusReply(0x19, kjParam_, 6);
                    else statusReply(joyMode_ == JOY_AUTO ? 0x14 : 0x15);
                    break;
                default:                      // $9A REPORT JOYSTICK AVAILABILITY : bit 5 de $CA
                    statusReply(joysticksEnabled_ ? 0x00 : 0x1A);
                    break;
            }
            break;
        }
        default:
            // Opcodes restants : no-op — le parseur a déjà consommé les octets de
            // paramètre.
            break;
    }
    // Le délai d'une nouvelle réponse doit être installé avant de relancer une
    // ancienne file gelée ; Hatari ne recharge le TDR qu'au prochain bit SCI.
    if (resumes && pauseOutput_) liftPause();
    onLineIdle();
}

void Ikbd::liftPause() {
    pauseOutput_ = false;
    pauseKeep_ = 0;
    if (monitorFrozen_ >= 0 && sched_ && joyMode_ == JOY_MONITOR)
        sched_->schedule(Scheduler::IKBD_MONITOR, firmwareEventTime() + monitorFrozen_);
    monitorFrozen_ = -1;
    armRx();
    onLineIdle();
}

void Ikbd::statusReply(uint8_t first, const uint8_t* data, int n) {
    // ROM $FCD7-$FCF8 : $F6 seul, puis le premier octet et ses paramètres en un
    // appel, puis un appel par octet de bourrage — chacun admis ou jeté selon la
    // place dans le tampon de 21 octets ($FD34).
    if (rxFree(1)) pushRxDelayed(0xF6, kResponseDelayCycles);
    if (rxFree(std::size_t(1 + n))) {
        pushRx(first);
        for (int i = 0; i < n; ++i) pushRx(data[i]);
    }
    for (int k = 1 + n; k < 7; ++k)
        if (rxFree(1)) pushRx(0x00);
}

void Ikbd::resetHw() {
    // Les fronts déjà échus précèdent le reset du récepteur SCI.
    if (sched_) catchUpCpuTx(sched_->liveNow());
    trace("hardware-reset");
    sciRxState_ = sciRxByte_ = sciRxBitsLeft_ = 0;
    if (sched_ && aciaClockInitialized_) {
        // [D] Hatari acia.c:343-344,452-455 @ f0736b24 : ACIA_Reset → Set_Timers(offset0) : prochain front un
        // bit après reset. Hatari le fait parce que son reset coupe ses interruptions internes ([D] acia.c:330-332 @ f0736b24).
        // ⚠ [B] MC6850 Pin Assignment p. 1 : pas de broche de reset, le reset de la machine n'atteint pas ce
        // diviseur. Réancrer la phase ici : écart non corrigé.
        txClockPhase_ = sched_->liveNow() % kAciaTxBitCycles;
        txNextBitCycle_ = sched_->liveNow() + kAciaTxBitCycles;
        scheduleCpuTx();
    }
    // « Reset the SCI » ([D] ikbd.c:503-513 @ f0736b24) : SCI_TX_State = IDLE, TSR = 0,
    // SCI_TX_Size/Delay = 0, SCI_RX_State = IDLE, RSR = 0, SCI_RX_Size = 0.
    // Côté NeoST le shift register de sortie du 6301 est matérialisé par
    // l'échéance IKBD_RX (octet en cours d'acheminement vers l'ACIA).
    rxPending_ = false;
    rxShiftData_ = 0;
    responseDelayBits_ = 0;
    rxWaitingDelay_ = false;
    if (sched_) sched_->cancel(Scheduler::IKBD_RX);
    // ACIA_Reset (Hatari [D] acia.c:334-347 @ f0736b24) ne remet aucun registre ACIA :
    // CR/RDR/statut/erreurs, TDR/TSR et initialisation du diviseur persistent.
    // [B] MC6850 Pin Assignment p. 1, § Counter Divide Select Bits p. 8 : seul CR0=CR1=1 remet la puce à zéro.
    // Le Scheduler NeoST n'est pas effacé par Machine::reset : conserver
    // l'échéance TX déjà armée, sans annuler ni recharger le TDR.
    // Le transit /64 est assemblé bit par bit dans tous les formats du CR, y compris
    // après reset SCI. Les diviseurs /16 et /1 gardent le repli de 1024 cycles par bit.
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
    lastMouseMode_ = REL;
    mouseOn_       = true;                    // ROM $F10F : $C9 = $98
    port0Joy_      = false;
    joyMode_       = JOY_AUTO;                // ROM $F113 : $CA = $28
    kjKey_[0] = kjKey_[1] = 0;
    kjFire_        = false;
    kjDirSince_ = kjFireSince_ = -1;
    // ROM $F02A : TRCSR = $1A efface WU ; $D4 et $CB sont dans la RAM effacée.
    sciWake_ = false; sciOnes_ = 0; sciErrorAt_ = sciResumeAt_ = -1;
    sciDropOutput_ = false; sciDiscard_ = 0;
    fireShift_     = 0;
    fireCount_     = 8;
    monitorContinuous_ = false;
    joysticksEnabled_ = true;
    prevJoy0_      = prevJoy1_ = 0;
    xThreshold_    = yThreshold_ = 1;
    xScale_        = yScale_ = 1;            // ROM $F106
    yAxis_         = 1;
    mouseAction_   = 0;
    keyCodeDeltaX_ = keyCodeDeltaY_ = 1;
    bOldL_         = bOldR_ = false;
    mouseDeltaX_   = mouseDeltaY_ = 0;
    mouseLeft_     = mouseRight_ = false;
    // ROM : RAM $89-$FF effacée — position, bornes et événements à 0 (Hatari
    // pose des bornes 320/200, visibles seulement via $88).
    absX_ = absY_  = 0;
    absMaxX_       = 0;
    absMaxY_       = 0;
    prevAbsButtons_ = 0;
    // Table d'état des touches effacée (ScanCodeState[] dans IKBD_Boot_ROM) :
    // toutes réputées relâchées après le boot.
    for (auto& s : scanState_) s = 0;
    // Commande multi-octets en cours abandonnée (nBytesInInputBuffer = 0).
    inBufLen_ = 0;
    cmdExpected_ = 0;
    // Fenêtre de reset : le RDR du 6301 repart vide.
    heldByte_      = -1;
    heldOverrun_   = false;
    pauseOutput_   = false;
    pauseKeep_     = 0;
    monitorFrozen_ = -1;
    rx_.clear();                   // BufferHead=Tail=0 dans IKBD_Boot_ROM
    // ⚠ On ne purge QUE le côté 6301 : le RDR de l'ACIA (rdrf_), l'overrun et la
    // livraison en vol SURVIVENT au reset logiciel $80,$01 — Hatari (IKBD_Boot_ROM,
    // [D] ikbd.c:532,579 @ f0736b24) ne touche que Keyboard.Buffer ; le SCI n'est resetté qu'au
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

void Ikbd::selectMouseMode(MouseMode m) {
    // ROM 6301 $FBAF : bit 7 posé, bit 1 effacé (port 0 rendu à la souris) ; si le
    // joystick était en surveillance, tir ou keycode ($CA bits 0-2), retour au mode
    // événement ($CA = $28). NeoST n'a que la surveillance ($17) à défaire.
    lastMouseMode_ = m;
    mouseOn_ = true;
    port0Joy_ = false;
    syncMouseMode();
    if (joyMode_ == JOY_MONITOR || joyMode_ == JOY_FIRE || joyMode_ == JOY_KEYCODE) {
        stopJoystickSampling();
        joyMode_ = JOY_AUTO;
        joysticksEnabled_ = true;
    }
}

void Ikbd::stopJoystickSampling() {
    // Sortie de $17/$18 : la scrutation normale reprend sur la grille de l'échantillon
    // en cours (même règle qu'au boot).
    if (sched_ && joystickMonitorPeriod_ > 0) {
        const auto remain = sched_->rawCyclesUntil(Scheduler::IKBD_MONITOR);
        if (remain != INT64_MIN)
            sched_->schedule(Scheduler::IKBD_AUTOSEND, sched_->liveNow() + remain);
        sched_->cancel(Scheduler::IKBD_MONITOR);
    }
    joystickMonitorPeriod_ = 0;
    monitorContinuous_ = false;
    monitorFrozen_ = -1;
    startAutoSend();
}

void Ikbd::readJoystickState(uint8_t& joy0, uint8_t& joy1) const {
    joy0 = hostJoy_[0];
    joy1 = hostJoy_[1];
    if (joyProbe_) joyProbe_(joy0, joy1);

    // Port 0 partagé : le joystick 0 n'est lu que si le port lui est attribué
    // (bit 1 de $C9 ; ROM $F141-$F168 ne lit sinon que le quartet du joystick 1).
    if (!port0Joy_)
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
    if (exeMode_) {                           // code 6301 custom : il remplace la ROM
        sendAutoInput();
        return;
    }
    if (joyMode_ == JOY_FIRE) {
        // ROM $F8A2 : un échantillon par tour de boucle. Sous pause ($F17B) ou ligne
        // occupée ($D7 bit 7), le tour se fait sans échantillon. P22 (feu du joystick
        // 1, actif bas) entre par la droite de $A4 ; au 8ᵉ, $A4 part dans le TDR.
        auto us = [](int64_t v) { return (v * neost::pacing::kCpuHzInt + 500000) / 1000000; };
        const int64_t base = due >= 0 ? due : sched_->liveNow();
        if (pauseOutput_ || duringResetCriticalTime_) {
            sched_->schedule(Scheduler::IKBD_MONITOR, addOcfDelay(base, us(34)));
            return;
        }
        if (!rx_.empty() || rxPending_) {
            sched_->schedule(Scheduler::IKBD_MONITOR, addOcfDelay(base, us(57)));
            return;
        }
        uint8_t joy0 = 0, joy1 = 0;
        readJoystickState(joy0, joy1);
        const bool fire = (joy1 & 0x80) || mouseRight_;
        fireShift_ = uint8_t((fireShift_ << 1) | (fire ? 1 : 0));
        int64_t next = fire ? 177 : 180;
        if (--fireCount_ == 0) {
            fireCount_ = 8;
            next += 15;
            pushRx(fireShift_);
        }
        sched_->schedule(Scheduler::IKBD_MONITOR, addOcfDelay(base, us(next)));
        return;
    }
    if (joyMode_ == JOY_KEYCODE) {
        // Prochaine évaluation au tic 1 ms suivant (grille OCF, pas la date de $19).
        scheduleKeycodePoll(due >= 0 ? due : sched_->liveNow());
        uint8_t joy0 = 0, joy1 = 0;
        readJoystickState(joy0, joy1);
        if (joysticksEnabled_ && !duringResetCriticalTime_) sendKeycodeJoystick(joy0);
        return;
    }
    // $17 utilise le même traitement que l'auto-send : MouseAction et position
    // interne précèdent le report monitoring, même quand MouseMode vaut OFF.
    sendAutoInput();
}

int64_t Ikbd::addOcfDelay(int64_t start, int64_t cycles) const {
    // Étend [start, start + cycles] de la durée de chaque interruption 1 ms qui y tombe
    // (une interruption allonge la fenêtre et peut en faire tomber une autre dedans).
    // 1 ms = 8 021,248 cycles CPU (8 021 248 Hz) : le k-ième tic est calculé EXACTEMENT
    // depuis l'origine. Arrondi à 8 021, la phase dérivait de 0,248 cycle par ms et
    // perdait une période entière en ~4 s de jeu.
    constexpr int64_t kHz = neost::pacing::kCpuHzInt;
    auto us = [](int64_t v) { return (v * kHz + 500000) / 1000000; };
    auto tickAt = [this](int64_t k) { return ocfOrigin_ + (k * kHz + 500) / 1000; };
    int64_t end = start + cycles;
    int64_t k = (start >= ocfOrigin_) ? (start - ocfOrigin_) * 1000 / kHz : 0;
    while (k > 0 && tickAt(k) > start) --k;   // premier tic strictement après start
    for (int guard = 0; guard < 64; ++guard, ++k) {
        const int64_t tick = tickAt(k);
        if (tick > end) break;
        if (tick <= start) continue;
        end += us(((k & 1) ? 215 : 128) + (clockSet_ ? 12 : 0));
    }
    return end;
}

void Ikbd::onVbl(int64_t vblMicro) {
    // 1) Avance l'horloge interne IKBD ($1B/$1C) du temps d'une trame.
    updateClock(vblMicro);
    ++vblCount_;
    if (!sched_) sendAutoInput();
}

void Ikbd::startAutoSend() {
    // IKBD_Boot_ROM n'écrase pas une échéance active : les RESET rapprochés
    // ne doivent pas repousser indéfiniment la scrutation (Hatari [D] ikbd.c:620 @ f0736b24).
    if (sched_ && joystickMonitorPeriod_ == 0 &&
        sched_->cyclesUntil(Scheduler::IKBD_AUTOSEND) < 0)
        sched_->schedule(Scheduler::IKBD_AUTOSEND, firmwareEventTime() + 150000);
}

void Ikbd::onAutoSend() {
    if (joystickMonitorPeriod_ > 0 || monitorContinuous_) return;
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
        if (left && port0Joy_) joy0 |= 0x80;
    } else if (joy1 & 0x80) {
        joy1 = uint8_t(joy1 & ~0x80);
        right = true;
    }

    // 4) Événements boutons du passage ($C1 de la ROM : bit0 droit enfoncé, bit1
    //    droit relâché, bit2 gauche enfoncé, bit3 gauche relâché), cumulés en mode
    //    absolu dans le registre lu par $0D ($C2).
    uint8_t events = 0;
    if (right && !prevR_) events |= 0x01;
    if (!right && prevR_) events |= 0x02;
    if (left && !prevL_)  events |= 0x04;
    if (!left && prevL_)  events |= 0x08;
    if (mouseMode_ == ABS) prevAbsButtons_ |= events;

    // 5) Reporting lié à MouseAction ($07), AVANT le paquet de mode (l'état
    //    bOldL_/bOldR_ n'est mis à jour qu'en fin de trame).
    //    Ces actions vivent dans le traitement souris de la ROM ($F371) : sans
    //    souris active (port 0 au joystick, $17…), elles ne s'exécutent pas.
    if (mouseMode_ != OFF) sendOnMouseAction(left, right, events);

    // 6) Position absolue : ROM $F3D4 → $F439, seulement en mode absolu.
    //    En relatif et en flèches, le cumul reste : les paquets le consomment.
    if (mouseMode_ == ABS || mouseMode_ == OFF) {
        const int dx = mouseDeltaX_;
        const int dy = mouseDeltaY_;
        mouseDeltaX_ = mouseDeltaY_ = 0;
        if (mouseMode_ == ABS) updateInternalAbsPos(dx, dy);
    }
    prevL_ = left; prevR_ = right;

    // 6) Monitoring ($17) : seul report émis, le reste de la trame est coupé.
    // Sous PAUSE OUTPUT ($13), le protocole IKBD suspend l'échantillonnage :
    // « the PAUSE OUTPUT command also temporarily stops the monitoring process
    // (i.e. the samples are not enqueued for transmission) ». La cadence continue,
    // rien n'est mis en file ; à la reprise, l'échantillon suivant est l'état
    // courant. Hatari empile au contraire les échantillons : défaut non reproduit.
    if (joyMode_ == JOY_MONITOR) {
        // $1A (bit 5 de $CA) : la ROM ne permet pas de trancher ; on suit la doc du
        // protocole (« disable … scanning of the joystick port ») — plus d'échantillon,
        // la cadence continue.
        if (!pauseOutput_ && joysticksEnabled_)
            sendAutoJoysticksMonitoring(joy0, joy1);
        return;
    }

    // 7) Report joystick auto, PUIS paquet souris selon le mode (ordre Hatari).
    //    $1A (bit 5 de $CA) coupe tout report joystick.
    if (joyMode_ == JOY_AUTO && joysticksEnabled_)
        sendAutoJoysticks(joy0, joy1);
    // Souris relative et flèches : la ROM émet dès que la ligne est libre (cf.
    // onLineIdle) ; la scrutation ne sert ici qu'aux changements de bouton.
    if (mouseMode_ == REL)
        sendRelMousePacket(left, right);
    else if (mouseMode_ == CURSOR)
        sendCursorKeys(left, right);

    // 8) Mémorise l'état bouton EFFECTIF (feu joystick inclus) pour la détection
    //    de front de la prochaine trame.
    bOldL_ = left;
    bOldR_ = right;
}

// Grille de l'interruption 1 ms du 6301 (OCF, ROM $FD9D) : k-ième tic depuis
// ocfOrigin_, exact au cycle près (même calcul que addOcfDelay).
int64_t Ikbd::ocfTick(int64_t k) const {
    return ocfOrigin_ + (k * neost::pacing::kCpuHzInt + 500) / 1000;
}

// Indice du dernier tic 1 ms <= t (-1 avant le premier).
int64_t Ikbd::ocfIndexAtOrBefore(int64_t t) const {
    if (t < ocfOrigin_) return -1;
    int64_t k = (t - ocfOrigin_) * 1000 / neost::pacing::kCpuHzInt;
    while (ocfTick(k + 1) <= t) ++k;
    while (k >= 0 && ocfTick(k) > t) --k;
    return k;
}

// [C] ROM 6301 $F7E3 et $FE7D-$FEA6 (MiSTery ikbd/rom/IKBD.ASM) : à l'appui, le
// prédiviseur de l'axe ($A2/$A3) est chargé à 100 ; l'interruption 1 ms le décrémente
// et, quand il est DÉJÀ à zéro, le recharge à 100 et décompte d'un dixième $A4..$A9.
// Le premier dixième tombe donc au 101ᵉ tic après l'appui, les suivants tous les
// 101 tics : un « dixième » de $19 dure 101 ms, pas 100.
int64_t Ikbd::keycodeTenths(int64_t since, int64_t t) const {
    const int64_t k0 = ocfIndexAtOrBefore(since) + 1;   // premier tic après l'appui
    const int64_t kt = ocfIndexAtOrBefore(t);
    if (kt < k0 + 100) return 0;
    return (kt - k0 - 100) / 101 + 1;
}

void Ikbd::sendKeycodeJoystick(uint8_t joy0) {
    // Joystick 0 en touches ($19), tour de boucle de la ROM : anti-rebond du feu
    // ($F186) et des directions ($F681), puis axe X, axe Y ($F7A6-$F87C) et feu
    // ($F87E). Évalué à chaque tic 1 ms (les compteurs ne changent qu'aux
    // interruptions) et à chaque changement de l'état hôte (cf. setJoystick) :
    // l'entrée ne change qu'à ces instants, la ROM ne verrait rien de plus en la
    // relisant à chaque tour.
    // [F] Latence du tour de boucle après l'interruption ou le changement (quelques
    // dizaines de µs, plus 128/215 µs de gestionnaire) non comptée. Tranché par le
    // décompte des cycles de la boucle $F13A -> $F681 -> $F7A6 sur la ROM.
    if (!sched_) return;
    const int64_t now = firmwareEventTime();
    const int64_t kNow = ocfIndexAtOrBefore(now);

    // [C] ROM $F186-$F1AD : anti-rebond du feu. $9D posé à 1 à la détection, +1 à chaque
    // tic ($FE6E) ; à 10, le candidat $9C est accepté dans $9B s'il tient toujours.
    const bool fireRaw = joy0 & 0x80;
    if (kjFireSince_ >= 0 && kNow - ocfIndexAtOrBefore(kjFireSince_) >= 9) {
        if (fireRaw == kjFireCand_) kjFireStable_ = kjFireCand_;
        kjFireSince_ = -1;
    }
    if (kjFireSince_ < 0 && fireRaw != kjFireStable_) { kjFireCand_ = fireRaw; kjFireSince_ = now; }

    // [C] ROM $F681-$F6A1 : même mécanisme pour les directions ($9E, seuil 5 ; candidat
    // $A0, état accepté $9F), bit par bit : un bit qui a rebougé garde son ancien état.
    const uint8_t dirRaw = joy0 & 0x0F;
    if (kjDirSince_ >= 0 && kNow - ocfIndexAtOrBefore(kjDirSince_) >= 4) {
        kjDirStable_ = uint8_t(((dirRaw ^ kjDirCand_) & kjDirStable_) | (dirRaw & kjDirCand_));
        kjDirSince_ = -1;
    }
    if (kjDirSince_ < 0 && dirRaw != kjDirStable_) { kjDirCand_ = dirRaw; kjDirSince_ = now; }

    // [C] ROM $F7B0-$F86C, par axe (X puis Y). $94 = direction tenue (kjKey_),
    // $A4/$A5 = RX restant, $A6/$A7 = TX restant, $A8/$A9 = VX restant, $A1 bits 5/6 =
    // phase VX. Les compteurs se déduisent des dixièmes écoulés depuis l'appui
    // (kjStart_) et depuis le dernier rechargement (kjLast_).
    struct Axis { uint8_t mask, neg, keyNeg, keyPos; };
    static constexpr Axis kAxes[2] = { {0x0C, 0x04, 0x4B, 0x4D},    // X : gauche/droite
                                       {0x03, 0x01, 0x48, 0x50} };  // Y : haut/bas
    for (int a = 0; a < 2; ++a) {
        const uint8_t dir = kjDirStable_ & kAxes[a].mask;
        if (!dir) { kjKey_[a] = 0; continue; }           // $F7BC : compteurs effacés, rien n'est émis
        if (dir == kAxes[a].mask) continue;              // $F7CD : directions opposées, rien ne change
        const uint8_t key = (dir & kAxes[a].neg) ? kAxes[a].keyNeg : kAxes[a].keyPos;
        // $F84E : la paire make+break part d'un bloc ; $FD34 la jette si le tampon est plein.
        auto emit = [&] { kjLast_[a] = now; if (rxFree(2)) { pushRx(key); pushRx(uint8_t(key | 0x80)); } };
        if (key != kjKey_[a]) {                          // $F7D4 : nouvel appui
            kjKey_[a] = key;
            kjStart_[a] = now;
            emit();
            continue;
        }
        const int rx = kjParam_[a], tx = kjParam_[2 + a], vx = kjParam_[4 + a];
        const int64_t tenths = keycodeTenths(kjStart_[a], now);
        const int64_t atReload = keycodeTenths(kjStart_[a], kjLast_[a]);
        const int64_t sinceReload = tenths - atReload;
        if (rx == 0 || atReload >= rx) {                 // $F80D : phase VX, une paire tous les VX
            if (vx && sinceReload >= vx) emit();
        } else if (tx && sinceReload >= tx) {            // $F82B : $A6 échu, répétition
            emit();                                      // ($F835 : bascule en VX si $A4 l'est aussi)
        } else if (tenths >= rx) {                       // $F81F : $A4 échu, bascule sans émettre
            kjLast_[a] = now;
        }
    }
    // [C] ROM $F87E-$F89E : $74 à l'appui, $F4 au relâchement ; $A1 bit 1 est mis à
    // jour avant l'envoi, que $FD34 jette si le tampon est plein.
    if (kjFireStable_ != kjFire_) {
        kjFire_ = kjFireStable_;
        if (rxFree(1)) pushRx(kjFire_ ? 0x74 : 0xF4);
    }
}

void Ikbd::scheduleKeycodePoll(int64_t after) {
    if (sched_) sched_->schedule(Scheduler::IKBD_MONITOR, ocfTick(ocfIndexAtOrBefore(after) + 1));
}

void Ikbd::setJoystick(uint8_t joy0, uint8_t joy1) {
    const bool changed = hostJoy_[0] != joy0 || hostJoy_[1] != joy1;
    hostJoy_[0] = joy0; hostJoy_[1] = joy1;
    // $19 : la ROM relit le port à chaque tour de boucle ; un changement de l'hôte est
    // vu tout de suite, sans attendre le prochain tic 1 ms.
    if (changed && sched_ && joyMode_ == JOY_KEYCODE && joystickMonitorPeriod_ > 0)
        sched_->schedule(Scheduler::IKBD_MONITOR, sched_->liveNow());
}

void Ikbd::updateInternalAbsPos(int dx, int dy) {
    // Position absolue (ROM $F439, mode absolu seulement) : échelle $0C, signe
    // d'axe Y $0F/$10, bornes inclusives posées par $09.
    // ROM $F488 : l'échelle est le pas par impulsion (1 par défaut ; 0 fige l'axe).
    const int sx = dx * xScale_;
    const int sy = dy * yAxis_ * yScale_;
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
    // $17 et $18 : la boucle principale de la ROM ne scrute plus le clavier
    // ($F171 → $F8A2 pour $18).
    // Suivi d'état (lu par les handlers 6301 custom) puis make/break.
    hostKeys_[scancode & 0x7F] = pressed ? 1 : 0;
    scanState_[scancode & 0x7F] = pressed ? 1 : 0;
    if ((joyMode_ == JOY_MONITOR || joyMode_ == JOY_FIRE) && !exeMode_)
        return;
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
    mouseLeft_ = left;
    mouseRight_ = right;
    // Le port 0 ne compte des impulsions souris que s'il sert la souris : souris
    // coupée ou port au joystick ($14/$15/$17/$18/$19), rien ne s'accumule. Cumul
    // borné (une ligne bloquée sous pause ne doit pas le faire déborder).
    if (mouseMode_ != OFF) {
        constexpr int kMax = 1 << 20;
        mouseDeltaX_ = std::clamp(mouseDeltaX_ + dx, -kMax, kMax);
        mouseDeltaY_ = std::clamp(mouseDeltaY_ + dy, -kMax, kMax);
    }
    // Le 6301 compte les impulsions en continu et émet sans attendre la scrutation.
    if (mouseMode_ == REL && !duringResetCriticalTime_ && vblCount_ > 20) {
        bool l, r; effectiveButtons(l, r);
        sendRelMousePacket(l, r);
    } else {
        onLineIdle();
    }
}

bool Ikbd::lineFree() const {
    return aciaClockInitialized_ && rx_.empty() && !rxPending_ && !outputBlocked()
        && !duringResetCriticalTime_;
}

void Ikbd::effectiveButtons(bool& left, bool& right) const {
    // Feu du joystick 1 = bouton droit tant que la souris est active (même ligne).
    left = mouseLeft_;
    right = mouseRight_ || (mouseMode_ != OFF && (hostJoy_[1] & 0x80));
}

void Ikbd::onLineIdle() {
    if (inLineIdle_ || exeMode_ || duringResetCriticalTime_ || vblCount_ <= 20) return;
    if (!lineFree()) return;
    inLineIdle_ = true;
    if (joyMode_ == JOY_MONITOR && monitorContinuous_) {
        sendAutoInput();                     // un échantillon $17 par ligne libre
    } else if (mouseMode_ == REL) {
        bool l, r; effectiveButtons(l, r);
        sendRelMousePacket(l, r);
    } else if (mouseMode_ == CURSOR) {
        bool l, r; effectiveButtons(l, r);
        sendCursorKeys(l, r);
    }
    inLineIdle_ = false;
}

void Ikbd::sendRelMousePacket(bool left, bool right) {
    // Mode relatif d'après la ROM ($F4E5, $F54A-$F556) : le 6301 cumule les
    // impulsions et n'émet un paquet $F8 que si (a) la ligne est libre et le cumul
    // atteint le seuil ($0B, 0 = toujours : flux continu de $F8 0 0), ou (b) un
    // bouton a changé, ou (c) le cumul dépasse ±127. Le reliquat sous le seuil reste
    // cumulé (Hatari l'abandonne). Le signe de Δy suit $0F/$10.
    if (inMouseEmit_) return;                 // sans ordonnanceur, pushRx livre en direct
    inMouseEmit_ = true;
    struct Release { bool& f; ~Release() { f = false; } } release{inMouseEmit_};
    for (int guard = 0; guard < 256; ++guard) {
        const int dX = mouseDeltaX_, dY = mouseDeltaY_;
        int bx = dX; if (bx > 127) bx = 127; else if (bx < -128) bx = -128;
        int by = dY; if (by > 127) by = 127; else if (by < -128) by = -128;
        auto reaches = [](int v, int thr) { return thr == 0 || (v < 0 && v <= -thr) || (v > 0 && v >= thr); };
        const bool overThr = reaches(bx, xThreshold_) || reaches(by, yThreshold_);
        const bool overflow = dX != bx || dY != by;
        // $07 bit 2 : les boutons partent en touches $74/$75 (ROM $F560 → $F62B),
        // un changement de bouton seul ne produit pas de paquet $F8.
        const bool btnChanged = !(mouseAction_ & 0x4) && ((bOldL_ != left) || (bOldR_ != right));
        if (!btnChanged && !overflow && !(overThr && lineFree())) return;
        if (rxFree(3)) {
            pushRx(uint8_t(0xF8 | (right ? 0x01 : 0) | (left ? 0x02 : 0)));
            pushRx(static_cast<uint8_t>(static_cast<int8_t>(bx)));
            pushRx(static_cast<uint8_t>(static_cast<int8_t>(by * yAxis_)));
        }
        mouseDeltaX_ -= bx;
        mouseDeltaY_ -= by;
        // $07 bit 2 : après le $F8, la ROM saute en $F62B et émet les touches
        // boutons dans le même passage ($F59D-$F5A2).
        if (mouseAction_ & 0x4) emitButtonKeys(left, right);
        else { bOldL_ = left; bOldR_ = right; }
        if (xThreshold_ == 0 || yThreshold_ == 0) return;   // flux continu : un paquet par ligne libre
    }
}

void Ikbd::emitButtonKeys(bool left, bool right) {
    // Boutons en touches ($07 bit 2, et mode flèches) : $74 gauche / $75 droit,
    // |$80 au relâchement — ROM $F62B. Un seul émetteur, qui consomme le front
    // (bOld*), pour qu'aucun chemin ne le réémette ni ne le perde.
    if (left == bOldL_ && right == bOldR_) return;
    if (rxFree(2)) {
        if (left && !bOldL_)       pushRx(0x74);
        else if (!left && bOldL_)  pushRx(0x74 | 0x80);
        if (right && !bOldR_)      pushRx(0x75);
        else if (!right && bOldR_) pushRx(0x75 | 0x80);
    }
    bOldL_ = left;
    bOldR_ = right;
}

void Ikbd::sendOnMouseAction(bool left, bool right, uint8_t events) {
    // Port de Hatari IKBD_SendOnMouseAction. Émis quel que soit le mode souris.
    if (mouseAction_ & 0x4) {
        // Boutons remontés comme scancodes touche ; les bits 0/1 sont ignorés.
        emitButtonKeys(left, right);
        return;
    }
    if (mouseAction_ & 0x3) {
        // Report de la position absolue à l'appui (bit0) / au relâchement (bit1),
        // en mode absolu : le paquet $F7 porte les événements DU PASSAGE ($C1 de
        // la ROM), sans vider le registre cumulé que lit $0D.
        const bool report = ((mouseAction_ & 0x1) && (events & 0x05))
                         || ((mouseAction_ & 0x2) && (events & 0x0A));
        if (report && mouseMode_ == ABS)
            sendAbsMousePos(events);
    }
}

void Ikbd::sendAbsMousePos(uint8_t buttons, int delayCycles) {
    // Paquet $F7 : octet d'événements boutons (droit enfoncé 0x01 / relâché 0x02,
    // gauche enfoncé 0x04 / relâché 0x08), puis position X/Y sur 16 bits.
    if (!rxFree(6)) return;                   // paquet $F7 (6 octets) entier ou rien
    pushRxDelayed(0xF7, delayCycles);
    pushRx(buttons);
    pushRx(uint8_t(absX_ >> 8));
    pushRx(uint8_t(absX_ & 0xFF));
    pushRx(uint8_t(absY_ >> 8));
    pushRx(uint8_t(absY_ & 0xFF));
}

void Ikbd::sendCursorKeys(bool left, bool right) {
    // Mode flèches ($0A) d'après la ROM ($F5D2, $F62B) : boutons en touches $74/$75
    // au changement ; sur chaque axe, dès que la ligne est libre et que le cumul
    // atteint le pas, UNE paire make+break part et le cumul est EFFACÉ. Pas 0 : la
    // condition est toujours vraie — flux continu de « droite » et « haut ». L'axe Y
    // n'est pas inversé par $0F ici.
    if (inMouseEmit_) return;
    inMouseEmit_ = true;
    struct Release { bool& f; ~Release() { f = false; } } release{inMouseEmit_};
    emitButtonKeys(left, right);
    if (!lineFree()) return;
    const int dX = mouseDeltaX_, dY = mouseDeltaY_;
    if (std::abs(dX) >= keyCodeDeltaX_) {
        const uint8_t k = dX < 0 ? 75 : 77;
        if (rxFree(2)) { pushRx(k); pushRx(uint8_t(k | 0x80)); }
        mouseDeltaX_ = 0;
    }
    if (std::abs(dY) >= keyCodeDeltaY_) {
        const uint8_t k = dY > 0 ? 80 : 72;
        if (rxFree(2)) { pushRx(k); pushRx(uint8_t(k | 0x80)); }
        mouseDeltaY_ = 0;
    }
}

void Ikbd::updateClock(int64_t vblMicro) {
    // Port de Hatari IKBD_UpdateClockOnVBL : on cumule la durée d'une trame ; chaque
    // seconde écoulée incrémente l'horloge BCD (YY MM DD hh mm ss) avec la même
    // logique de propagation/retenue (DAA) que la ROM du HD6301, bornée par le
    // nombre de jours du mois (et l'année bissextile pour février).
    // Mois 0 (horloge jamais réglée) : la ROM ($FDB6) ne fait plus avancer l'horloge.
    if (clock_[1] == 0) return;
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
    // Place pour `n` octets dans le tampon de sortie du 6301 : $D9-$ED, 21 octets
    // (ROM $F11B : $D7 = $95, émetteur au repos + 21 places ; Hatari en prévoit
    // 1024). Seuls les octets EN ATTENTE comptent ; l'octet engagé dans le SCI et le
    // RDR de l'ACIA vivent hors du tampon. Un appel de $FD34 est admis entier ou
    // jeté : un paquet teste sa taille totale avant son premier pushRx.
    constexpr std::size_t kBufSize = 21;
    // ROM $FD34 : $CB bit 4 (posé par le gestionnaire ORFE) jette l'envoi entier jusqu'au
    // tic de reprise ($FE44).
    if (sciDropOutput_ && (sciErrorAt_ >= 0 || firmwareEventTime() < sciResumeAt_)) return false;
    return rx_.size() <= kBufSize && n <= kBufSize - rx_.size();
}

void Ikbd::pushRx(uint8_t b) {
    // [D] Hatari ikbd.c:1027-1032 @ f0736b24 IKBD_Send_Byte_Delay : avant l'initialisation ACIA, jeter les octets
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
    // 7812,5 bauds = 10240 cycles à 8 MHz, cadence du SCI d'Hatari, [D] ikbd.c:1035 @ f0736b24). Le SCI de
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
    if (rx_.empty() || outputBlocked()) return;
    // Après une erreur de trame, TIE n'est rétabli qu'au tic de reprise ($FE47).
    if (baseCycle < sciResumeAt_ && (baseCycle >= 0 || firmwareEventTime() < sciResumeAt_))
        baseCycle = sciResumeAt_;
    if (sched_) {
        // Check_New_TDR est appelé en SCI IDLE uniquement : un seul octet
        // sort de la file avant DATA/STOP, sans préchargement du suivant.
        rxShiftData_ = rx_.front();
        rx_.pop_front();
        if (pauseOutput_ && pauseKeep_ > 0) --pauseKeep_;   // octet admis avant la pause
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
    if (!shifting && (rx_.empty() || outputBlocked())) {
        armRx(responseDelayBits_ > 0 ? due : -1);
        return;
    }
    // Le SCI est indépendant de PauseOutput et de la file firmware : même
    // après un boot ROM qui vide cette dernière, le stop bit arrive à l'ACIA.
    const uint8_t b = shifting ? rxShiftData_ : rx_.front();
    if (!shifting) {
        rx_.pop_front();
        if (pauseOutput_ && pauseKeep_ > 0) --pauseKeep_;
    }
    trace("acia-receive data=$%02X result=%s", b,
          (control_ & 0x03) == 0x03 ? "reset-drop" : rdrf_ ? "overrun-drop" : "rdr");
    if ((control_ & 0x03) == 0x03) {
        // Master reset MAINTENU (CR1-0 = 11) : le récepteur est tenu en reset et
        // n'assemble aucun caractère — l'octet est perdu, le 6301 continue d'émettre.
        // [B] Motorola MC6850 (extern/datasheets/MC6850.pdf) § Counter Divide Select
        //     Bits p. 8 (« master reset which clears the Status Register […] and
        //     initializes both the receiver and transmitter ») et § Interrupt Request
        //     p. 6 (ACIA « maintained in the Reset condition »).
        // [D] Hatari acia.c:1038 @ f0736b24 — ACIA_Clock_RX tourne pendant le reset et
        //     pose RDRF : défaut d'Hatari, NON porté. Sans cette garde, une touche pressée
        //     puis relâchée pendant un reset tenu donnait RDRF puis overrun à la sortie,
        //     relâchement perdu, touche collée en auto-répétition.
    } else if (rdrf_) {
        // RDR pas encore lu quand l'octet suivant finit d'arriver : OVERRUN. Le
        // nouvel octet est PERDU, RDR conserve l'ancien ([B] MC6850 § Receive Data Register p. 7 ;
        // [D] Hatari acia.c:1103-1123 @ f0736b24 ACIA_Clock_RX, état STOP_BIT). Le bit OVRN ne se posera
        // qu'à la lecture de RDR ([B] MC6850 § Receiver Overrun p. 8-9) ; en
        // attendant la cause d'IRQ RX reste active ([D] Hatari acia.c:721-724 @ f0736b24, RX_Overrun dans
        // ACIA_UpdateIRQ).
        rxOverrun_ = true;
    } else {
        rdr_ = b;
        rdrf_ = true;
    }
    armRx(responseDelayBits_ > 0 ? due : -1); // attente globale au prochain SCI idle
    raiseIfReady(due);
    if (sched_) onLineIdle();                 // ligne libérée : flux « dès que possible »
}

bool Ikbd::irqActive() const {
    // Cause d'IRQ de l'ACIA 6850 ([B] MC6850 § Interrupt Request p. 6, § Receive Interrupt Enable Bit p. 8 ;
    // [D] Hatari acia.c:715-730 @ f0736b24 ACIA_UpdateIRQ) : RX = octet LIVRÉ
    // (RDRF) OU overrun pendant, et RX int activé (RIE, bit7) ; TX = registre
    // d'émission vide et IRQ d'émission armée (TIE). CTS et DCD bas sur le ST (cf. tête de fichier) →
    // omis.
    // [B] MC6850 § Master Reset p. 5 : IRQ tenue haute en master reset ; [D] gen_uart.v:347-348 @ a00deb5b aussi.
    if ((control_ & 0x03) == 0x03) return false;
    return ((rdrf_ || rxOverrun_) && (control_ & 0x80)) || (txEnableInt_ && tdre_);
}

void Ikbd::raiseIfReady(int64_t when) {
    // [B] MC6850 § Interrupt Request p. 6 : l'ACIA active sa ligne d'IRQ dès qu'une cause (RX ou TX) est
    // active, en drain ouvert, d'où le wire-OR avec le MIDI. La retombée prend au plus 1,2 µs
    // (§ Serial Data Timing p. 3, tIR), non modélisé : elle est immédiate ici, dans la borne.
    // [D] Hatari acia.c:369-370 @ f0736b24 : les deux ACIA partagent GPIP4. On la
    // publie sur GPIP4 (lue par _int_acia pour vider l'ACIA) ; le canal 6 du MFP
    // est levé par le détecteur de FRONT du setter (règle AER, wire-OR avec le
    // MIDI) — pas de raise() manuel, sinon IPR reposé sans front réel (octet
    // doublé côté handlers qui ne testent pas le SR).
    if (when >= 0) mfp_.setAciaLineKbdAt(irqActive(), when);
    else mfp_.setAciaLineKbd(irqActive());
}

int64_t Ikbd::nextTxClock(int64_t at) const {
    // [D] Hatari acia.c:452-455,801-805 @ f0736b24 ACIA_Set_Timers_IKBD : la phase change avec le diviseur CR.
    // [B] MC6850 § Control Register p. 8 : (CR & $1F) == $16 est /64 8N1.
    // Phase gardée pour tout format au /64. [F] Aux /16 et /1, l'ACIA émet 4 ou 64 fois
    // plus vite que le SCI n'échantillonne (8 × 7812,5 bauds, HD6301 Handbook p. 1247) :
    // repli d'un bit de 1024 cycles, sans forme d'onde. Tranché par l'échantillonnage du
    // SCI sur la ligne réelle, à modéliser.
    const int64_t phase = (control_ & 0x03) == 0x02 ? txClockPhase_ : 0;
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

// [B] MC6850 § Word Select Bits p. 8 : 000 7E2, 001 7O2, 010 7E1, 011 7O1, 100 8N2, 101 8N1,
// 110 8E1, 111 8O1. Nombre de bits du caractère après le start.
int Ikbd::aciaFrameBits() const {
    const int format = (control_ >> 2) & 7;
    const int data = (format & 4) ? 8 : 7;
    const int parity = (format == 4 || format == 5) ? 0 : 1;
    const int stops = (format == 0 || format == 1 || format == 4) ? 2 : 1;
    return data + parity + stops;
}

// Niveau du bit `index` (0 = premier bit de donnée) : données, parité, stops à 1.
// [B] MC6850 § Parity Error p. 9 : parité impaire = nombre total de « 1 » impair ; CR2 = 1 → impaire.
bool Ikbd::aciaFrameBit(int index) const {
    const int format = (control_ >> 2) & 7;
    const int data = (format & 4) ? 8 : 7;
    if (index < data) return (txShiftData_ >> index) & 1;
    if (index == data && format != 4 && format != 5) {
        int ones = 0;
        for (int i = 0; i < data; ++i) ones += (txShiftData_ >> i) & 1;
        return ((ones & 1) != 0) != ((format & 1) != 0);
    }
    return true;
}

// Récepteur 6301 : ikbd.c IKBD_SCI_Get_Line_RX. Tout zéro en IDLE est
// un start, huit bits LSB suivent ; un stop nul est une erreur de trame.
// [B] HD6301 Handbook p. 155 : la trame du SCI fait toujours 10 bits (8N1) ; un caractère
// 7 bits + parité y arrive avec la parité en bit 7, un 8 bits + parité y perd sa parité
// comme bit de stop.
void Ikbd::clockSciRx(bool bit) {
    if (sciWake_) {
        // [B] HD6301 Handbook TRCSR bit 0 p. 152 : sous WU, rien n'est assemblé ; dix « 1 »
        // consécutifs effacent WU.
        if (!bit) { sciOnes_ = 0; return; }
        if (++sciOnes_ >= 10) {
            sciWake_ = false;
            sciOnes_ = 0;
            sciWakeCleared(firmwareEventTime());
        }
        return;
    }
    if (sciRxState_ == 0) {
        if (!bit) { sciRxState_ = 1; sciRxByte_ = 0; sciRxBitsLeft_ = 8; }
    } else if (sciRxState_ == 1) {
        if (bit) sciRxByte_ |= 0x80;
        if (--sciRxBitsLeft_) sciRxByte_ >>= 1;
        else sciRxState_ = 2;
    } else {
        sciRxState_ = 0;
        if (bit) receiveSerialByte(sciRxByte_);
        else {
            trace("sci-drop data=$%02X reason=invalid-stop", sciRxByte_);
            sciFramingError();
        }
    }
}

// [C] ROM 6301 $FF40-$FF6D, appelé par l'interruption SCI sur ORFE ($FEE6).
void Ikbd::sciFramingError() {
    // Pendant l'initialisation (sei $F010 … cli $F139), l'interruption n'est pas servie.
    // [F] ORFE resterait posé jusqu'au cli ; le caractère est simplement perdu ici.
    // Tranché par : une erreur de trame provoquée pendant le reset d'un IKBD.
    if (duringResetCriticalTime_ || exeMode_ || memLoadLeft_ > 0) return;
    sciWake_ = true;                       // $FF42 : WU
    sciOnes_ = 0;
    sciErrorAt_ = firmwareEventTime();     // $FF4F : $D4 = 1 (WU ne peut pas s'effacer en 128 µs)
    sciResumeAt_ = -1;
    sciDropOutput_ = true;                 // $FF52 : $CB bit 4 ; $FF55 : TIE coupé
    // $FF58-$FF6B : une commande en cours perd le caractère fautif comme paramètre, les
    // suivants sont jetés ; sinon $CB est remis à zéro, PAUSE (bit 3) excepté, bit 4 compris.
    int discard = 0;
    if (cmdExpected_ > 0 && inBufLen_ > 0) discard = cmdExpected_ - inBufLen_ - 1;
    inBufLen_ = 0;
    cmdExpected_ = 0;
    sciDiscard_ = uint8_t(discard > 0 ? discard : 0);
    if (!sciDiscard_) sciDropOutput_ = false;
    trace("sci-framing-error discard=%d", int(sciDiscard_));
}

// WU effacé à `at` : reprise au tic 1 ms suivant ($FE3E-$FE4C), ou reset si $D4 a
// atteint $CD pendant que la ligne restait basse ($FE2D-$FE3B, $F00B).
void Ikbd::sciWakeCleared(int64_t at) {
    if (sciErrorAt_ < 0) return;
    const int64_t kError = ocfIndexAtOrBefore(sciErrorAt_);
    const int64_t ticksLow = ocfIndexAtOrBefore(at - 1) - kError;   // tics vus avec WU posé
    sciErrorAt_ = -1;
    if (ticksLow >= 0xCD - 1) {
        // $D4 = 1 + tics : $CD au 204ᵉ, saut au 205ᵉ ; $F00B attend ensuite WU effacé.
        const int64_t rebootAt = std::max(at, ocfTick(kError + 0xCD));
        trace("sci-break-reset ms=%lld", (long long)ticksLow);
        const int64_t previous = cpuSerialEventCycle_;
        cpuSerialEventCycle_ = rebootAt;
        bootRom();
        cpuSerialEventCycle_ = previous;
        return;
    }
    sciResumeAt_ = ocfTick(ocfIndexAtOrBefore(at) + 1);
    trace("sci-wake resume=%lld", (long long)sciResumeAt_);
    armRx(sciResumeAt_);
}

// Progresser aux fronts physiques sans installer un événement par bit. Au repos
// MARK, IDLE ne change plus ; sous BREAK, dix zéros ramènent IDLE (framing).
void Ikbd::catchUpCpuTx(int64_t at) {
    if (!sched_ || txNextBitCycle_ == 0) return;
    while (txNextBitCycle_ <= at) {
        const bool breaking = (control_ & 0x60) == 0x60;
        if (!txShifting_ && sciRxState_ == 0 && (tdre_ || breaking)) {
            const int64_t clocks = (at - txNextBitCycle_) / kAciaTxBitCycles + 1;
            int64_t skip = 0;
            if (sciWake_) {
                // Sous WU : la ligne basse remet le compte à zéro ; au repos, les « 1 »
                // s'accumulent, le dixième passe par clockSciRx (reprise datée).
                if (breaking) {
                    sciOnes_ = 0;
                    skip = clocks;
                } else {
                    skip = std::min<int64_t>(clocks, 9 - sciOnes_);
                    sciOnes_ = uint8_t(sciOnes_ + skip);
                }
            } else if (!breaking) skip = clocks;   // le BREAK passe bit à bit jusqu'à l'erreur de trame
            if (skip) {
                txNextBitCycle_ += skip * kAciaTxBitCycles;
                continue;
            }
        }
        bool bit = true;
        bool ended = false;
        if (txShifting_) {
            if (txBitIndex_ + 1 < aciaFrameBits()) bit = aciaFrameBit(txBitIndex_++);
            else { txShifting_ = false; txBitIndex_ = 0; ended = true; }   // dernier stop
        } else if (breaking) bit = false;
        else if (!tdre_) {
            txShiftData_ = txData_; txShifting_ = true; txBitIndex_ = 0;
            // [B] MC6850 § Transmit Data Register p. 7 : TDRE remonte au transfert.
            // [D] Hatari acia.c:974-983 @ f0736b24 : start au même front que le transfert TDR→TSR.
            tdre_ = true; bit = false;
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
    const bool breaking = (control_ & 0x60) == 0x60;
    if (txShifting_)
        due = txNextBitCycle_ + std::max(0, aciaFrameBits() - 1 - int(txBitIndex_)) * kAciaTxBitCycles;
    else if (!tdre_ && !breaking) due = txNextBitCycle_;
    else if (breaking && !sciWake_) due = txNextBitCycle_;   // BREAK : trame nulle → erreur de trame datée
    else if (sciWake_ && !breaking)                          // dixième « 1 » au repos : WU s'efface
        due = txNextBitCycle_ + (9 - sciOnes_) * kAciaTxBitCycles;
    if (sciRxState_) {
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
            // [D] ikbd.c:2980-2981 @ f0736b24 ne vide que Keyboard.Buffer) — RDR/RDRF/overrun de
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
