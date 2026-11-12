// =============================================================================
//  Scc.cpp — SCC Z85C30 du Mega STE (port de Hatari scc.c).
//
//  Port fonctionnel du cœur registre/IRQ de scc.c (cf. Scc.hpp pour le périmètre,
//  les grades et les empreintes des références UM0109, PS0117, Hatari @ f0736b24).
//  Les commentaires renvoient aux fonctions SCC_* d'Hatari.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Scc.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
constexpr int SCC_IRQ_ON = 0, SCC_IRQ_OFF = 1;

// [B] UM0109 WR0 p.140-142 : commandes WR0 (bits 3-5).
constexpr uint8_t WR0_CMD_RESET_EXT_STATUS = 0x02, WR0_CMD_RESET_TX_IP = 0x05,
    WR0_CMD_ERROR_RESET = 0x06, WR0_CMD_RESET_HIGHEST_IUS = 0x07;
// [B] UM0109 WR1 p.143.
constexpr uint8_t WR1_EXT_INT_ENABLE = 0x01, WR1_TX_INT_ENABLE = 0x02, WR1_PARITY_SPECIAL = 0x04;
constexpr uint8_t WR1_RX_MODE_FIRST_SPECIAL = 0x01, WR1_RX_MODE_ALL_SPECIAL = 0x02;
// [B] UM0109 WR3 p.148, WR5 p.152.
constexpr uint8_t WR3_RX_ENABLE = 0x01, WR3_SYNC_LOAD_INHIBIT = 0x02, WR3_AUTO_ENABLES = 0x20;
[[maybe_unused]] constexpr uint8_t WR5_RTS = 0x02, WR5_TX_ENABLE = 0x08, WR5_SEND_BREAK = 0x10, WR5_DTR = 0x80;
// [B] UM0109 WR4 p.150 : bits de stop.
constexpr uint8_t WR4_PARITY_ENABLE = 0x01, WR4_STOP_SYNC = 0x00;
// [B] UM0109 WR9 p.160.
constexpr uint8_t WR9_VIS = 0x01, WR9_NV = 0x02, WR9_MIE = 0x08, WR9_STATUS_HIGH_LOW = 0x10, WR9_SOFT_INTACK = 0x20;
constexpr uint8_t WR9_CMD_RESET_B = 0x01, WR9_CMD_RESET_A = 0x02, WR9_CMD_RESET_FORCE_HW = 0x03;
// [B] UM0109 WR15 p.173.
[[maybe_unused]] constexpr uint8_t WR15_WR7_PRIME = 0x01, WR15_ZERO_COUNT_IE = 0x02, WR15_STATUS_FIFO = 0x04,
    WR15_DCD_IE = 0x08, WR15_SYNC_HUNT_IE = 0x10, WR15_CTS_IE = 0x20,
    WR15_TX_UNDERRUN_IE = 0x40, WR15_BREAK_ABORT_IE = 0x80;
// [B] UM0109 RR0 p.175.
constexpr uint8_t RR0_RX_CHAR_AVAIL = 0x01, RR0_ZERO_COUNT = 0x02, RR0_TX_BUFFER_EMPTY = 0x04,
    RR0_DCD = 0x08, RR0_SYNC_HUNT = 0x10, RR0_CTS = 0x20, RR0_TX_UNDERRUN_EOM = 0x40, RR0_BREAK_ABORT = 0x80;
// [B] UM0109 RR1 p.178.
constexpr uint8_t RR1_ALL_SENT = 0x01, RR1_PARITY_ERROR = 0x10, RR1_RX_OVERRUN = 0x20,
    RR1_CRC_FRAMING = 0x40, RR1_EOF_SDLC = 0x80;
// [B] UM0109 RR3 p.181 : IP des deux canaux, lisibles sur le canal A seul.
constexpr uint8_t RR3_EXT_IP_B = 0x01, RR3_TX_IP_B = 0x02, RR3_RX_IP_B = 0x04,
    RR3_EXT_IP_A = 0x08, RR3_TX_IP_A = 0x10, RR3_RX_IP_A = 0x20;
// Sources d'interruption (bitmask interne)
constexpr uint32_t INT_RX_OVERRUN = 1u << 1, INT_RX_FRAMING = 1u << 2,
    INT_RX_PARITY = 1u << 4, INT_TX_BUFFER_EMPTY = 1u << 5;

Scheduler::Source brgSource(int channel) {
    return channel == 0 ? Scheduler::SCC_BRG_A : Scheduler::SCC_BRG_B;
}
Scheduler::Source serialSource(int c, bool tx) {
    return tx ? (c ? Scheduler::SCC_TX_B : Scheduler::SCC_TX_A)
              : (c ? Scheduler::SCC_RX_B : Scheduler::SCC_RX_A);
}
} // namespace

// -----------------------------------------------------------------------------
//  Reset
// -----------------------------------------------------------------------------
void Scc::resetChannel(int c, bool hw) {
    Chn& ch = chn_[c];
    // [B] UM0109 « Z85X30 Register Reset Value » p.35 : masques WR/RR de cette fonction
    // comparés bit à bit aux colonnes Hardware et Channel, sans écart.
    // [D] Hatari scc.c:590-634 @ f0736b24 — mêmes masques.
    ch.WR[0] = 0x00;
    activeReg_ = 0;
    ch.WR[1] &= 0x24; ch.WR[3] &= 0xfe; ch.WR[4] |= 0x04; ch.WR[5] &= 0x61;
    ch.WR[15] = 0xf8; ch.WR7p = 0x20;
    if (hw) {
        // [B] UM0109 WR14 D0 p.172 : le reset matériel coupe le BRG, donc son événement.
        // [D] Hatari scc.c:590-634 @ f0736b24 — ne retire pas le timer ici ;
        //     reset.c:76 @ f0736b24 — son reset machine réinitialise CycInt.
        ch.brgFraction = ch.brgSource = 0; ch.brgPulseStart = ch.brgPulseEnd = -1;
        brgPeriod_[c] = 0;
        if (sched_) sched_->cancel(brgSource(c));
        chn_[0].WR[9] &= 0x03; chn_[0].WR[9] |= 0xC0; ius_ = 0;
        ch.WR[10] = 0x00; ch.WR[11] = 0x08; ch.WR[14] &= 0xC0; ch.WR[14] |= 0x30;
    } else {
        chn_[0].WR[9] &= 0xdf;
        ch.WR[10] &= 0x60; ch.WR[14] &= 0xC3; ch.WR[14] |= 0x20;
    }
    ch.RR[0] &= 0xb8; ch.RR[0] |= 0x44;
    ch.rr0NoLatch = ch.RR[0]; ch.rr0Latched = false;
    ch.breakRaw = (ch.rr0NoLatch & RR0_BREAK_ABORT) != 0; ch.breakPending = 0;
    ch.RR[1] &= 0x01; ch.RR[1] |= 0x06;
    ch.RR[10] &= 0x40;
    ch.serialFraction[0] = ch.serialFraction[1] = ch.serialDivisor[0] = ch.serialDivisor[1] = 0;
    ch.txRemaining = 0; ch.txInterrupted = false;
    ch.tsrBits = 8; ch.tsrParity = 2;
    ch.txWritten = false; ch.tsrFull = false; ch.txActive = false; ch.rtsAsserted = false;
    ch.rxEchoStarted = ch.rxEchoEligible = ch.rxReceiveEligible = false;
    ch.rxHead = ch.rxCount = 0;
    ch.rxFifoHead = ch.rxFifoCount = 0;
    std::memset(ch.rxErrors, 0, sizeof(ch.rxErrors));
    ch.rxFirstArmed = true; ch.rxFirstPending = ch.rxLocked = false;
    ch.intSources = 0;
    // [B] UM0109 WR9 p.160 : seuls les IP/IUS du canal réinitialisé sont effacés.
    // RR3A contient aussi les IP de B : ne pas remettre tout ce registre à zéro.
    // Le tableau p.35 note RR3=$00 en colonne Channel ; le texte p.160 est le plus précis.
    const uint8_t interruptMask = c ? (RR3_RX_IP_B | RR3_TX_IP_B | RR3_EXT_IP_B)
                                    : (RR3_RX_IP_A | RR3_TX_IP_A | RR3_EXT_IP_A);
    updateRR3Bit(false, interruptMask);
    ius_ &= ~interruptMask;
    static constexpr uint8_t dataBits[] = {5,7,6,8};
    ch.txBits = dataBits[(ch.WR[5] >> 5) & 3];
    ch.rxBits = dataBits[ch.WR[3] >> 6];
    ch.parityBits = ch.WR[4] & 1;
    if (sched_) { sched_->cancel(serialSource(c, true)); sched_->cancel(serialSource(c, false)); }
}

void Scc::resetFull(bool hw) {
    uint8_t wr9old = chn_[0].WR[9];
    resetChannel(0, true);
    resetChannel(1, true);
    // [B] UM0109 WR9 p.160 : Force Hardware Reset garde MIE, Status High/Low et DLC écrits
    // avec la commande.
    if (!hw) { chn_[0].WR[9] &= ~0x1c; chn_[0].WR[9] |= (wr9old & 0x1c); }
    chn_[0].intSources = 0; chn_[1].intSources = 0;
    setLineIRQ(SCC_IRQ_OFF);
}

void Scc::reset() {
    trace_ = getenv("NEOST_SCC_TRACE") != nullptr;
    // Le canal est sauvé en bloc : effacer aussi son bourrage pour un état déterministe.
    std::memset(chn_, 0, sizeof(chn_));
    resetFull(true);
}

// -----------------------------------------------------------------------------
// BRG : port de SCC_Compute_BaudRate et SCC_InterruptHandler_BRG (scc.c).
// [B] UM0109 WR11 p.166, WR14 D0 p.172 : WR11 ne fait que multiplexer les horloges,
// il n'empêche PAS le BRG de tourner. Seul le débit BRG
// est porté ici ; les transferts utilisent des événements caractère séparés.
// -----------------------------------------------------------------------------
int64_t Scc::nextBrgPeriod(int c) {
    auto& ch = chn_[c];
    // [B] UM0109 p.72-73, p.168 : le compteur repasse à zéro toutes les TC+2 horloges.
    const int tc = (int(ch.WR[13]) << 8) | ch.WR[12];
    const int64_t total = neost::pacing::kCpuHzInt * (tc + 2) + ch.brgFraction;
    const uint32_t fraction = uint32_t(total % ch.brgSource);
    const int64_t period = total / ch.brgSource + (fraction != 0) - (ch.brgFraction != 0);
    ch.brgFraction = fraction;
    return period;
}
void Scc::updateBrg(int c) {
    auto& ch = chn_[c];
    if (!(ch.WR[14] & 1)) {
        brgPeriod_[c] = 0; ch.brgFraction = ch.brgSource = 0;
        ch.brgPulseStart = ch.brgPulseEnd = -1;
        if (sched_) sched_->cancel(brgSource(c));
        return;
    }
    // [B] UM0109 WR14 D1 p.172 : source du BRG = PCLK si 1, sinon /RTxC.
    // [D] Hatari scc.c:40-41,108 @ f0736b24 — sur Mega STE, PCLK = CLK8 et /RTxC = 3,672 MHz.
    //     Tranché par : le schéma du Mega STE.
    const uint32_t source = (ch.WR[14] & 2) ? uint32_t(neost::pacing::kCpuHzInt) : 3672000;
    // [B] UM0109 figure p.72 : ZC précède le diviseur /2 et le multiplicateur WR4.
    // [B] UM0109 p.72, WR12 p.167 : une nouvelle constante n'entre au compteur qu'au
    // prochain rechargement.
    // [B] UM0109 WR14 D0 p.172 : l'activation « is not reflected by the output of the BRG for
    // two counts of the counter ». [F] Deux lectures : départ du compteur retardé de deux coups,
    // ou sortie seule masquée deux coups. Ici le premier ZC tombe après TC+2, sans délai.
    // Tranché par : la date du premier ZC après WR14 D0 sur un vrai 85C30.
    if (ch.brgSource == source && brgPeriod_[c]) return;
    ch.brgSource = source; ch.brgFraction = 0;
    ch.brgPulseStart = ch.brgPulseEnd = -1;
    brgPeriod_[c] = nextBrgPeriod(c);
    if (sched_) sched_->schedule(brgSource(c), sched_->liveNow() + brgPeriod_[c]);
}

void Scc::onBrgEvent(int c) {
    if (c < 0 || c > 1 || !sched_ || !brgPeriod_[c] || !chn_[c].brgSource || !(chn_[c].WR[14] & 1)) return;
    auto& ch = chn_[c];
    const int64_t due = sched_->firingDue();
    const int64_t anchor = due >= 0 ? due : sched_->liveNow();
    // [B] UM0109 p.59/p.177, figure p.72 : RR0 ZC reste haut un cycle source, jusqu'au rechargement.
    const int64_t pulseTotal = neost::pacing::kCpuHzInt + ch.brgFraction;
    ch.brgPulseStart = anchor;
    ch.brgPulseEnd = anchor + pulseTotal / ch.brgSource
        + (pulseTotal % ch.brgSource != 0) - (ch.brgFraction != 0);
    brgPeriod_[c] = nextBrgPeriod(c);
    sched_->schedule(brgSource(c), anchor + brgPeriod_[c]);
    // [B] UM0109 RR0 D1 p.177 : ne pas retenir le front haut précédent, ce bit n'est pas latché.
    ch.RR[0] &= ~RR0_ZERO_COUNT;
    // [B] UM0109 RR0 D1 p.177 : le front montant de ZC ferme les latches ; un dispatch
    // CPU qui dépasse la pulse n'y change rien.
    if (!ch.rr0Latched && (ch.WR[15] & WR15_ZERO_COUNT_IE)) {
        ch.rr0Latched = true;
        if (ch.WR[1] & WR1_EXT_INT_ENABLE)
            updateRR3Bit(true, c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
    }
    updateRR0(c); updateIRQ();
}

// -----------------------------------------------------------------------------
//  RR0 / RR2 / RR3
// -----------------------------------------------------------------------------
// Lignes de modem en ENTRÉE. Sans rien de branché : au repos = assertées.
// [D] Hatari scc.c:811,837 @ f0736b24 — cts = dcd = 1 sans TTY.
// [F] Inconnu : repos réel de ces entrées, port débranché, sur Mega STE. Tranché par :
//     le schéma des récepteurs RS-232/RS-422, ou RR0 lu sur la machine sans câble.
// [F] Prise de bouclage Field Service branchée (setLoopback) : CTS
// suit la sortie /RTS réelle et DCD suit DTR (WR5 bit7) du MÊME canal — c'est ce que le
// test « I SCC » du diagnostic MegaSTE bascule pour détecter la prise. Cf. Scc.hpp.
uint16_t Scc::getCTS(int c) const { return extLoopback_ ? (chn_[c].rtsAsserted ? 1 : 0) : 1; }
uint16_t Scc::getDCD(int c) const { return extLoopback_ ? ((chn_[c].WR[5] & 0x80) ? 1 : 0) : 1; }

// Branche/débranche les prises de bouclage : synchronise /SYNC (DSR de la prise,
// suit DTR) puis recalcule RR0 des deux canaux (fronts CTS/DCD → ext-status IRQ
// si armés). [F] Prétendu équivalent à une vraie (dé)connexion, non mesuré ; cf. Scc.hpp.
void Scc::setLoopback(bool plugged) {
    extLoopback_ = plugged;
    for (int c = 0; c < 2; ++c) {
        if (plugged && (chn_[c].WR[5] & 0x80)) updateRR0Set(c, RR0_SYNC_HUNT);
        else                                   updateRR0Clear(c, RR0_SYNC_HUNT);
        updateRR0(c);
        updateSerial(c);
    }
    updateIRQ();
}

bool Scc::breakStatusAllowed(int c) const {
    const uint8_t mode = chn_[c].WR[4];
    return (mode & 0x0c) || (mode & 0x30) == 0x20;
}

void Scc::updateRR0LatchOff(int c) {
    auto& ch = chn_[c];
    if (ch.breakPending && breakStatusAllowed(c) && (ch.WR[15] & WR15_BREAK_ABORT_IE)) {
        // [B] UM0109 p.58 : les deux transitions de Break/Abort ferment les latches.
        // [F] Inconnu : l'ordre de service quand plusieurs sont en attente. Choix de NeoST,
        // servir d'abord l'état opposé à l'image verrouillée. Les transitions
        // alternent ; deux flags retiennent chaque polarité sans file illimitée.
        // Tranché par : un break court sous latch fermé, RR0 relu sur un vrai 85C30.
        const uint8_t next = (ch.RR[0] & RR0_BREAK_ABORT) ? 2 : 1;
        const uint8_t edge = (ch.breakPending & next) ? next : ch.breakPending;
        ch.breakPending &= ~edge;
        if (edge == 1) ch.RR[0] |= RR0_BREAK_ABORT;
        else ch.RR[0] &= ~RR0_BREAK_ABORT;
        ch.rr0Latched = true;
        if (ch.WR[1] & WR1_EXT_INT_ENABLE)
            updateRR3Bit(true, c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
    } else ch.rr0Latched = false;
    updateRR0(c);
}

void Scc::updateRR0(int c) {
    Chn& ch = chn_[c];
    // [B] UM0109 p.58 : les deux fronts BREAK ferment les latches même si une
    // autre cause est déjà en service. Détecter les fronts bruts une seule fois,
    // pas à chaque lecture RR0. Les cycles répétés coalescent par polarité.
    const bool breakAllowed = breakStatusAllowed(c);
    const bool externalSync = (ch.WR[4] & 0x0c) || (ch.WR[4] & 0x30) == 0x30;
    const bool syncSuppressed = externalSync && (ch.WR[11] & 0x80);
    // [B] UM0109 p.58–59, WR11 D7 p.166 : BREAK vaut zéro hors asynchrone/SDLC ; la broche
    // /SYNC est indisponible avec l'oscillateur cristal en async/external sync.
    // Garder les niveaux bruts : enlever l'option restitue la broche courante.
    if (!breakAllowed) ch.breakPending = 0;
    const bool rawBreak = breakAllowed && (ch.rr0NoLatch & RR0_BREAK_ABORT) != 0;
    if (breakAllowed && rawBreak != ch.breakRaw && ch.rr0Latched && (ch.WR[15] & WR15_BREAK_ABORT_IE))
        ch.breakPending |= rawBreak ? 1 : 2;
    ch.breakRaw = rawBreak;
    uint8_t rr0New;
    bool updCTS = false, updDCD = false;
    const int64_t now = sched_ ? sched_->liveNow() : -1;
    // [B] UM0109 p.59 : ZC reflète l'état courant, et vaut 0 tant que son IE WR15 est à 0.
    if ((ch.WR[15] & WR15_ZERO_COUNT_IE) && now >= ch.brgPulseStart && now < ch.brgPulseEnd)
        ch.rr0NoLatch |= RR0_ZERO_COUNT;
    else ch.rr0NoLatch &= ~RR0_ZERO_COUNT;
    // [B] UM0109 p.56 : RR0 rend l'état courant des bits sans IE, l'état latché des autres.
    // [B] UM0109 RR0 p.177 : D0 et D2 restent courants, latches fermés ou non.
    if (!ch.rr0Latched) {
        rr0New = ch.rr0NoLatch;
        updCTS = updDCD = true;
    } else {
        rr0New = ch.rr0NoLatch & (RR0_RX_CHAR_AVAIL | RR0_TX_BUFFER_EMPTY);
        rr0New |= ch.rr0NoLatch & RR0_ZERO_COUNT;
        if (ch.WR[15] & WR15_DCD_IE) rr0New |= ch.RR[0] & RR0_DCD; else updDCD = true;
        if (ch.WR[15] & WR15_SYNC_HUNT_IE) rr0New |= ch.RR[0] & RR0_SYNC_HUNT;
        else rr0New |= ch.rr0NoLatch & RR0_SYNC_HUNT;
        if (ch.WR[15] & WR15_CTS_IE) rr0New |= ch.RR[0] & RR0_CTS; else updCTS = true;
        if (ch.WR[15] & WR15_TX_UNDERRUN_IE) rr0New |= ch.RR[0] & RR0_TX_UNDERRUN_EOM;
        else rr0New |= ch.rr0NoLatch & RR0_TX_UNDERRUN_EOM;
        if (ch.WR[15] & WR15_BREAK_ABORT_IE) rr0New |= ch.RR[0] & RR0_BREAK_ABORT;
        else rr0New |= ch.rr0NoLatch & RR0_BREAK_ABORT;
    }
    if (updCTS) { rr0New &= ~RR0_CTS; if (getCTS(c)) rr0New |= RR0_CTS; }
    if (updDCD) { rr0New &= ~RR0_DCD; if (getDCD(c)) rr0New |= RR0_DCD; }

    if (syncSuppressed) rr0New &= ~RR0_SYNC_HUNT;
    if (!breakAllowed) rr0New &= ~RR0_BREAK_ABORT;
    uint8_t rr0Old = ch.RR[0];
    ch.RR[0] = rr0New;

    // [B] UM0109 p.56–57 : les latches WR15 fonctionnent même avec WR1 D0=0.
    // Ce dernier masque l'IP, pas la capture des conditions externes.
    {
        bool set = false;
        if (!ch.rr0Latched && ((rr0Old & RR0_ZERO_COUNT) == 0) && (rr0New & RR0_ZERO_COUNT) && (ch.WR[15] & WR15_ZERO_COUNT_IE)) set = true;
        else if (((rr0Old & RR0_DCD) != (rr0New & RR0_DCD)) && (ch.WR[15] & WR15_DCD_IE)) set = true;
        else if (!syncSuppressed && ((rr0Old & RR0_SYNC_HUNT) != (rr0New & RR0_SYNC_HUNT)) && (ch.WR[15] & WR15_SYNC_HUNT_IE)) set = true;
        else if (((rr0Old & RR0_CTS) != (rr0New & RR0_CTS)) && (ch.WR[15] & WR15_CTS_IE)) set = true;
        else if (((rr0Old & RR0_TX_UNDERRUN_EOM) == 0) && (rr0New & RR0_TX_UNDERRUN_EOM) && (ch.WR[15] & WR15_TX_UNDERRUN_IE)) set = true;
        else if (breakAllowed && ((rr0Old & RR0_BREAK_ABORT) != (rr0New & RR0_BREAK_ABORT)) && (ch.WR[15] & WR15_BREAK_ABORT_IE)) set = true;
        if (set) {
            ch.rr0Latched = true;
            if (ch.WR[1] & WR1_EXT_INT_ENABLE)
                updateRR3Bit(true, c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
        }
    }
}

uint8_t Scc::getVectorStatus() {
    // [B] UM0109 WR9 p.161 : codes de statut 0-7 ; p.41 : priorité A avant B, puis Rx, Tx, Ext.
    // [B] UM0109 p.44 : en mode No Vector, le vecteur ne change plus avant Reset IUS : le statut suit l'IUS.
    // ⚠ [B] UM0109 WR1 p.145 : RX désarmé, une condition spéciale donne encore son vecteur dans
    //    RR2. Ici sans IP RX, RR2B ne la montre pas. Écart non corrigé.
    uint8_t status, mask;
    // ius_ a l'ordre des bits de RR3 (cf. Scc.hpp).
    const uint8_t rr3 = ((chn_[0].WR[9] & WR9_NV) && ius_) ? ius_ : chn_[0].RR[3];
    if (rr3 & RR3_RX_IP_A) {
        mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC;
        if (chn_[0].WR[1] & WR1_PARITY_SPECIAL) mask |= RR1_PARITY_ERROR;
        // [B] UM0109 RR1 p.178-179 : les bits d'erreur RX vivent dans RR1 (tester RR0 prenait
        // CTS/underrun pour des erreurs → chaque RX vectorisé partait en « special condition »).
        // [D] Hatari scc.c:1224 @ f0736b24 — lit RR0 ici : ne pas s'aligner, le manuel prime.
        status = (chn_[0].RR[1] & mask) ? 7 : 6;
    } else if (rr3 & RR3_TX_IP_A) status = 4;
    else if (rr3 & RR3_EXT_IP_A) status = 5;
    else if (rr3 & RR3_RX_IP_B) {
        mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC;
        if (chn_[1].WR[1] & WR1_PARITY_SPECIAL) mask |= RR1_PARITY_ERROR;
        status = (chn_[1].RR[1] & mask) ? 3 : 2;    // cf. canal A : erreurs RX = RR1
    } else if (rr3 & RR3_TX_IP_B) status = 0;
    else if (rr3 & RR3_EXT_IP_B) status = 1;
    else status = 3;    // [B] UM0109 RR2 p.180 : aucun IP → V3,V2,V1 = 011 (V6,V5,V4 = 110).
    return status;
}

void Scc::updateRR2() {
    uint8_t vector = chn_[0].WR[2];
    chn_[0].RR[2] = vector;                       // [B] UM0109 RR2 p.180 : RR2A = WR2
    uint8_t status = getVectorStatus();
    if (chn_[0].WR[9] & WR9_STATUS_HIGH_LOW) {
        status = ((status & 1) << 2) + (status & 2) + ((status & 4) >> 2);
        vector &= 0x8f; vector |= (status << 4);
    } else {
        vector &= 0xf1; vector |= (status << 1);
    }
    chn_[1].RR[2] = vector;                       // [B] UM0109 RR2 p.180, WR9 D4 p.161 : RR2B = WR2 + statut, sans égard à VIS
}

void Scc::updateRR3Bit(bool set, uint8_t bit) {
    if (set) chn_[0].RR[3] |= bit; else chn_[0].RR[3] &= ~bit;
}

void Scc::updateRR3(int c) {
    Chn& ch = chn_[c];
    updateRR0(c);
    uint8_t rxMode = (ch.WR[1] >> 3) & 3;
    bool intRx = rxMode == WR1_RX_MODE_ALL_SPECIAL ||
        (rxMode == WR1_RX_MODE_FIRST_SPECIAL && ch.rxFirstPending);
    // [B] UM0109 p.47–48, WR1 p.146 : en mode All, retirer le dernier caractère efface IP,
    // même si RR1 conserve l'overrun/parité pour les prochains vecteurs.
    // Les modes First/Special conservent leur IP tant que la FIFO est verrouillée.
    bool set = (intRx && ch.rxFifoCount) || (rxMode && ch.rxLocked);
    updateRR3Bit(set, c ? RR3_RX_IP_B : RR3_RX_IP_A);

    // [B] UM0109 p.50 : TxIP suppose un premier caractère écrit ; WR0 p.142 : Reset Tx IP
    // l'interdit jusqu'au caractère suivant. p.40 : sans IE, l'IP n'est jamais posé.
    set = (ch.RR[0] & RR0_TX_BUFFER_EMPTY) && (ch.WR[1] & WR1_TX_INT_ENABLE) && ch.txWritten;
    updateRR3Bit(set, c ? RR3_TX_IP_B : RR3_TX_IP_A);
}

// -----------------------------------------------------------------------------
//  IRQ / IACK
// -----------------------------------------------------------------------------
void Scc::updateIRQ() {
    // [B] UM0109 p.40-42 : /INT exige MIE et un IP sans IUS de priorité supérieure ;
    // un IUS masque les sources inférieures, pas les supérieures.
    int irqNew = SCC_IRQ_OFF;
    if (chn_[0].WR[9] & WR9_MIE) {
        for (int i = 5; i >= 0; --i) {
            if (ius_ & (1 << i)) { irqNew = SCC_IRQ_OFF; break; }
            if (chn_[0].RR[3] & (1 << i)) { irqNew = SCC_IRQ_ON; break; }
        }
    }
    if (irqNew != irqLine_) setLineIRQ(irqNew);
}

void Scc::intSourcesChange(int c, uint32_t src, bool set) {
    if (set) { if ((chn_[c].intSources & src) == src) return; }
    else     { if ((chn_[c].intSources & src) == 0)   return; }
    updateRR3(c);
    if (set) chn_[c].intSources |= src; else chn_[c].intSources &= ~src;
    updateIRQ();
}

int Scc::doIACK(bool /*soft*/) {
    // [B] UM0109 p.44-45, WR9 D5 p.160 : l'acquittement, matériel ou logiciel, relève /INT.
    setLineIRQ(SCC_IRQ_OFF);
    // [B] UM0109 WR9 D3 p.161 : aucun nouvel IUS lorsque MIE est désarmé,
    // y compris lors d'un acquittement logiciel par lecture de RR2.
    // [D] Hatari scc.c:2405-2428 @ f0736b24 — ni test de MIE ni test des IUS : le manuel prime.
    if (chn_[0].WR[9] & WR9_MIE) {
        for (int i = 5; i >= 0; --i) {
            // [B] UM0109 p.41–42 : la chaîne des IUS masque aussi l'INTACK,
            // sans empêcher une source plus prioritaire de se faire acquitter.
            if (ius_ & (1 << i)) break;
            if (chn_[0].RR[3] & (1 << i)) { ius_ |= (1 << i); break; }
        }
    }
    updateRR2();
    // [B] UM0109 WR9 D0 p.161-162 : VIS choisit le vecteur modifié ou celui de WR2.
    return (chn_[0].WR[9] & WR9_VIS) ? chn_[1].RR[2] : chn_[0].RR[2];
}

int Scc::processIack() {
    int vector = doIACK(false);
    // [B] UM0109 WR9 D1 p.161 : No Vector laisse le bus flottant, -1 est la convention NeoST.
    // [B] UM0109 p.45 : VIS et NV sont ignorés quand WR9 D5=1, pour l'acquittement logiciel.
    // [F] Le manuel se tait sur un INTACK matériel reçu avec WR9 D5=1 ; ici VIS et NV
    //     restent appliqués. Tranché par : ce cas sur un Mega STE, WR9 D5=1 sous IRQ.
    if (chn_[0].WR[9] & WR9_NV) return -1;   // No Vector
    return vector;
}

// -----------------------------------------------------------------------------
//  TX / RX
// -----------------------------------------------------------------------------
void Scc::copyTdrTsr(int c, uint8_t tdr) {
    auto& ch = chn_[c];
    ch.txInterrupted = false; ch.txRemaining = 0;
    ch.tsrBits = ch.txBits;
    // [B] UM0109 p.94/p.153 : en mode « cinq ou moins », les bits hauts du
    // caractère encodent la longueur, puis seuls les bits bas sont émis.
    // [F] Le format est pris au chargement TSR, pas à sa sortie : le manuel ne le dit pas
    // (p.115 décrit le format, p.154 ne date au chargement que le CRC ; l'ancienne citation
    // p.114 parle du SDLC). Tranché par : WR5 D6-D5 changé en cours de caractère sur un 85C30.
    if (ch.txBits == 5) {
        if (!(tdr & 0x80)) ch.tsrBits = 5;
        else if (!(tdr & 0x40)) ch.tsrBits = 4;
        else if (!(tdr & 0x20)) ch.tsrBits = 3;
        else if (!(tdr & 0x10)) ch.tsrBits = 2;
        else ch.tsrBits = 1;
    }
    ch.tsr = tdr & uint8_t((1u << ch.tsrBits) - 1);
    ch.tsrParity = 2;
    if (ch.WR[4] & WR4_PARITY_ENABLE) {
        // [B] UM0109 WR4 D1 p.152 : 1 = parité paire, 0 = impaire.
        unsigned ones = 0;
        for (uint8_t b = ch.tsr; b; b >>= 1) ones ^= b & 1;
        ch.tsrParity = uint8_t(ones ^ ((ch.WR[4] & 2) ? 0 : 1));
    }
    // [D] Hatari scc.c:1660-1671 @ f0736b24 — le TDR passe au TSR presque aussitôt si le
    //     TSR est libre, et TBE remonte. Hatari l'attribue à la doc Zilog (« ~3 PCLKs ») :
    //     le manuel ne chiffre pas ce délai. [F] TBE avec TX coupé non plus : ce cas,
    //     lu sur un vrai 85C30, le tranche.
    chn_[c].tsrFull = true;
    chn_[c].RR[1] &= ~RR1_ALL_SENT;
    updateRR0Set(c, RR0_TX_BUFFER_EMPTY);
    intSourcesSet(c, INT_TX_BUFFER_EMPTY);
}

void Scc::serialWriteByte(int c, uint8_t v) {
    // [B] UM0109 p.69/p.172 : Auto Echo prime sur Local Loopback et déconnecte
    // complètement la sortie du TSR. Le pont RxD→TxD utilise seulement les
    // octets hôtes async huit bits sans parité dans onSerialEvent, sans
    // reconstruire les bits ni les intervalles du fil source.
    if (chn_[c].WR[14] & 0x08) return;
    if (trace_) std::fprintf(stderr, "[scc] TX %c: $%02X\n", 'A' + c, v);
    // [B] UM0109 WR14 D4 p.171, p.69 : en bouclage local, TxD est
    // relié à RxD à l'intérieur de la puce ; TxD reste aussi connecté au
    // transmetteur. La prise externe occupe le sink hôte, comme
    // hors bouclage local, mais ne doit pas doubler la réception interne.
    // [D] Hatari scc.c:610-611,619-620 @ f0736b24 — WR14 D4 posé, jamais lu : pas de bouclage local.
    // ⚠ [B] UM0109 p.35 (et p.26 Z80X30) : le reset matériel met WR14=$30 (bit4=1). Mais WR14 D4
    // p.171 dit « reset by a channel or hardware reset » : le manuel se contredit, et PS0117 ne
    // tranche pas. Le code suit le tableau. Un programme qui transmet SANS
    // reconfigurer WR14 boucle donc sur lui-même — NE PAS « corriger » sans preuve.
    // [F] « Inoffensif, tout pilote réécrit WR14 (BRG, bit4=0) avant d'émettre » n'est pas
    // vérifié. Tranché par : un octet émis sans écrire WR14 après reset, sur un vrai 85C30.
    // Parité connue seulement pour une boucle avec mêmes largeurs de données.
    // Les formats discordants nécessiteraient un véritable récepteur de bits.
    const auto& ch = chn_[c];
    const int parity = ch.tsrBits == ch.rxBits && ch.tsrParity < 2 ? ch.tsrParity : -1;
    if (ch.WR[14] & 0x10) deliverByte(c, v, parity);
    // [F] Prise de bouclage externe (Field Service) : TxD revient sur RxD du même canal. Cf. Scc.hpp.
    else if (extLoopback_) deliverByte(c, v, parity);
    if (sink_ && !extLoopback_) sink_(c, v);
}

void Scc::processTX(int c) {
    Chn& ch = chn_[c];
    if (ch.WR[5] & WR5_SEND_BREAK) return;
    if ((ch.RR[0] & RR0_TX_BUFFER_EMPTY) && !ch.tsrFull) return;   // underrun
    if (ch.tsrFull && (ch.txActive || transmitAllowed(c))) {
        if (!ch.txInterrupted) serialWriteByte(c, ch.tsr);
        ch.txInterrupted = false; ch.txRemaining = 0;
        ch.tsrFull = false; ch.txActive = false;
        // [B] UM0109 RR1 D0 p.180 : All Sent suit la fin de trame en asynchrone ; il se lit
        //    toujours à 1 en synchrone et SDLC (forcé à la lecture, cf. read8).
        ch.RR[1] |= RR1_ALL_SENT;
    }
    if ((ch.RR[0] & RR0_TX_BUFFER_EMPTY) == 0 && transmitAllowed(c))
        copyTdrTsr(c, ch.WR[8]);
    if (!(ch.RR[0] & RR0_TX_BUFFER_EMPTY)) ch.RR[1] &= ~RR1_ALL_SENT;
    updateRts(c);
    updateRR3(c); updateIRQ();
}

// [B] UM0109 p.45 : lire la donnée dépile données et erreurs ensemble ; RR1 D5 p.179 : seul
// le caractère écrasé porte l'overrun, latché à sa lecture.
// [D] L'overrun remplace le dernier emplacement, en conservant les deux plus anciens : le
// manuel ne nomme pas l'emplacement, et le registre à décalage en attente n'est pas modélisé.
// Vient de MAME z80scc.cpp:2674-2684 @ ce391499d37d (receive_data). Tranché par : un overrun provoqué sur un 85C30.
void Scc::refreshRx(int c) {
    auto& ch = chn_[c];
    // [B] UM0109 RR1 p.178-179 : framing/EOF suivent le caractère ; parité et overrun collent.
    ch.RR[1] &= ~(RR1_CRC_FRAMING | RR1_EOF_SDLC);
    if (ch.rxFifoCount) {
        ch.RR[8] = ch.rxFifo[ch.rxFifoHead];
        ch.RR[1] |= ch.rxErrors[ch.rxFifoHead];
        updateRR0Set(c, RR0_RX_CHAR_AVAIL);
    } else updateRR0Clear(c, RR0_RX_CHAR_AVAIL);
    updateRR3(c); updateIRQ();
}
void Scc::popRx(int c) {
    auto& ch = chn_[c];
    if (ch.rxFifoCount) {
        // [B] UM0109 p.47/p.142 : l'IP du premier caractère disparaît aussi
        // quand Error Reset retire ce caractère avant sa lecture.
        ch.rxFirstPending = false;
        ch.rxFifoHead = (ch.rxFifoHead + 1) % 3; --ch.rxFifoCount;
    }
}
void Scc::deliverByte(int c, uint8_t b, int parity) {
    auto& ch = chn_[c];
    if (!receiveAllowed(c)) return;
    // [B] UM0109 WR3 p.148 : en asynchrone, les bits RR8
    // inutilisés valent 1. Appliquer le format à l'entrée de la FIFO : une
    // reprogrammation ultérieure de WR3 ne modifie pas les octets déjà reçus.
    // En synchrone/SDLC, les huit bits de la tranche série sont conservés.
    if (ch.WR[4] & 0x0c) {
        unsigned used = ch.rxBits;
        // [B] UM0109 WR4 D0 p.152 : le bit de parité reçu est transmis au CPU
        // pour RX court. receiveByte ne fournit aucun bit physique : conserver
        // sa convention d'octet hôte, sans supposer une parité valide.
        if (used < 8 && (ch.WR[4] & WR4_PARITY_ENABLE) && parity >= 0) {
            b = uint8_t((b & ((1u << used)-1)) | (unsigned(parity) << used));
            ++used;
        }
        b |= uint8_t(0xffu << used);
    }
    // [B] UM0109 WR3 D1 p.149-150 : comparer WR6 à l'octet prêt pour la FIFO, même
    // en asynchrone (avertissement explicite du manuel). SDLC réemploie ce bit
    // pour l'adresse ; en Bisync 12 bits (WR10 D0 p.165), il est ignoré. Ceci ne
    // modélise ni la recherche de synchronisation ni l'assemblage des bits.
    const bool async = (ch.WR[4] & 0x0c) != 0;
    const unsigned syncMode = ch.WR[4] & 0x30;
    if ((ch.WR[3] & WR3_SYNC_LOAD_INHIBIT) && (async || syncMode != 0x20)
        && (async || syncMode != 0x10 || !(ch.WR[10] & 1)) && b == ch.WR[6]) return;
    if (ch.rxFifoCount == 3) {
        const int newest = (ch.rxFifoHead + 2) % 3;
        ch.rxFifo[newest] = b; ch.rxErrors[newest] |= RR1_RX_OVERRUN;
    } else {
        const int tail = (ch.rxFifoHead + ch.rxFifoCount) % 3;
        ch.rxFifo[tail] = b; ch.rxErrors[tail] = 0; ++ch.rxFifoCount;
    }
    // [B] UM0109 p.47 : en mode First, le premier caractère reçu ou déjà stocké pose l'IP.
    if (ch.rxFirstArmed && ((ch.WR[1] >> 3) & 3) == 1) {
        ch.rxFirstPending = true; ch.rxFirstArmed = false;
    }
    refreshRx(c);
}

// [D] Hatari scc.c:2139-2173 @ f0736b24 — cadence caractère, SCC_Start_InterruptHandler_TX_RX.
// Les horloges TX et RX sont indépendantes ; DPLL reste hors périmètre.
int64_t Scc::serialPeriod(int c, bool tx, bool advance) {
    auto& ch = chn_[c];
    // [B] UM0109 WR4 p.150-152 : multiplicateurs d'horloge ; stops 1, 1½ et 2 (en demi-bits).
    static constexpr int mults[] = {1,16,32,64};
    static constexpr int stops2[] = {0,2,3,4};
    // [B] UM0109 WR4 D7-D6 p.150 : le mode 1X est forcé en synchrone.
    // [B] UM0109 WR4 p.150-151 : en External Sync, D7-D6 ne choisissent plus un multiplicateur
    //    mais une variante de synchronisation (/SYNC seul, /SYNC ou WR7, SDLC), et doivent valoir
    //    00 en mode caractère (p.84). [F] L'horloge reste traitée en 1X ; aucune de ces variantes
    //    n'est modélisée. Tranché par : un 85C30 en External Sync avec D7-D6 ≠ 00.
    const int mult = (ch.WR[4] & 12) ? mults[ch.WR[4] >> 6] : 1;
    const int source = (ch.WR[11] >> (tx ? 3 : 5)) & 3;
    const int bits = tx ? (ch.tsrFull ? ch.tsrBits : ch.txBits) : ch.rxBits;
    const int selectedStops = (ch.WR[4] >> 2) & 3;
    // [B] UM0109 p.93/p.151 : WR4 ne sélectionne que les stops émis ;
    // le récepteur vérifie un seul stop, quel que soit ce sélecteur TX.
    const int halfBits = 2*(bits + 1 + ch.parityBits)
        + ((!tx && selectedStops) ? 2 : stops2[selectedStops]);
    if (source == 3 || (source == 2 && !(ch.WR[14] & 1))) return 0;
    // [B] UM0109 WR11 p.165-167, WR14 D1 p.172 : sélecteurs d'horloge.
    // [D] Hatari scc.c:40-43,108-109 @ f0736b24 — /RTxC = 3,672 MHz, /TRxCB = BCLK 2,4576 MHz.
    // [F] /TRxCA est LCLK, broche du connecteur, et non BCLK : Hatari scc.c:42 @ f0736b24,1001-1003
    //     @ f0736b24 marque ce repli d'un TODO, NeoST le reprend. Tranché par : le schéma.
    const int64_t freq = source == 2 ? ((ch.WR[14] & 2) ? neost::pacing::kCpuHzInt : 3'672'000)
                                     : (source == 0 ? 3'672'000 : 2'457'600);
    // [B] UM0109 WR12/13 p.168 : BRG = source / [2*(TC+2)]. La cadence des
    // caractères ne doit pas hériter de l'arrondi du timer Zero Count historique.
    const int64_t divider = source == 2 ? 2 * (((int(ch.WR[13]) << 8) | ch.WR[12]) + 2) : 1;
    {
        const uint32_t divisor = uint32_t(2 * freq);
        const uint32_t previous = ch.serialDivisor[tx] == divisor ? ch.serialFraction[tx] : 0;
        const int64_t total = neost::pacing::kCpuHzInt * mult * halfBits * divider + previous;
        const uint32_t fraction = uint32_t(total % divisor);
        // Échéances ceil(cumul) : ne jamais finir avant le dernier bit. La
        // différence des deux plafonds reste exacte lorsque le reste revient à 0.
        const int64_t delta = total / divisor + (fraction != 0) - (previous != 0);
        if (advance) { ch.serialFraction[tx] = fraction; ch.serialDivisor[tx] = divisor; }
        return std::max<int64_t>(1, delta);
    }
}

// [B] UM0109 WR3 D5 p.149, WR14 D4-D3 p.171-172 : qualifier les enables logiciels avec les broches réelles,
// jamais avec RR0 qui peut être verrouillé par une interruption External/Status.
bool Scc::receiveAllowed(int c) const {
    const auto& ch = chn_[c];
    return (ch.WR[3] & WR3_RX_ENABLE) && (!(ch.WR[3] & WR3_AUTO_ENABLES)
        || getDCD(c) || (ch.WR[14] & 0x10));
}
// Transport borné de caractères hôtes connus : pas de parité ni de bits
// reconstruits. L'écho est daté au stop RX abstrait, jamais par l'horloge TX.
bool Scc::localLoopbackOnly(int c) const {
    return (chn_[c].WR[14] & 0x18) == 0x10;
}
bool Scc::echoByteAllowed(int c) const {
    const auto& ch = chn_[c];
    return (ch.WR[14] & 8) && ch.rxBits == 8 && !(ch.WR[4] & WR4_PARITY_ENABLE)
        && (ch.WR[4] & 0x0c);
}
void Scc::startRxFrame(int c) {
    auto& ch = chn_[c];
    ch.rxEchoStarted = (ch.WR[14] & 8) != 0;
    ch.rxEchoEligible = echoByteAllowed(c);
    ch.rxReceiveEligible = receiveAllowed(c);
}
bool Scc::transmitAllowed(int c) const {
    const auto& ch = chn_[c];
    return (ch.WR[5] & WR5_TX_ENABLE) && (!(ch.WR[3] & WR3_AUTO_ENABLES)
        || getCTS(c) || (ch.WR[14] & 0x18));
}
// [B] UM0109 p.95 / p.154 : en asynchrone Auto Enables, attendre le dernier
// stop, TDR compris. La prise CTS doit suivre cette sortie, jamais WR5 directement.
void Scc::updateRts(int c) {
    auto& ch = chn_[c];
    const bool delayed = (ch.WR[3] & WR3_AUTO_ENABLES) && (ch.WR[4] & 0x0c);
    const bool busy = ch.tsrFull || !(ch.rr0NoLatch & RR0_TX_BUFFER_EMPTY);
    const bool asserted = (ch.WR[5] & WR5_RTS) || (delayed && busy && ch.rtsAsserted);
    if (asserted == ch.rtsAsserted) return;
    ch.rtsAsserted = asserted;
    updateRR0(c); updateIRQ();
}
void Scc::updateSerial(int c) {
    auto& ch = chn_[c];
    // [B] UM0109 WR5 D4 p.153 : BREAK force TxD à 0 au prochain coup d'horloge ; à sa fin,
    // TxD reprend le contenu du TSR.
    // [D] Que le TSR soit suspendu sans consommer de donnée vient de MAME z80scc.cpp:1203-1208
    // @ ce391499d37d (tra_callback) ; le manuel ne le dit pas. Tranché par : un BREAK en cours de caractère sur un 85C30.
    // Notre tranche au caractère
    // conserve les cycles restants, sans phase sous-bit ni erreurs RX inventées.
    // Un caractère coupé ne peut pas être livré comme un octet intact.
    if ((ch.WR[5] & WR5_SEND_BREAK) && sched_ && ch.txActive) {
        const int64_t remaining = sched_->rawCyclesUntil(serialSource(c, true));
        if (remaining > 0) {
            ch.txRemaining = remaining; ch.txInterrupted = true;
            sched_->cancel(serialSource(c, true));
        }
    }
    // [B] UM0109 p.109 (mode caractères ; même phrase p.125 en SDLC) : /DCD inactif arrête le
    // récepteur et perd le caractère en cours, pas la FIFO. Le chapitre async p.97 dit
    // seulement que le récepteur est coupé : la perte en async est une extrapolation.
    if (!receiveAllowed(c) && sched_ && ch.rxCount &&
        sched_->cyclesUntil(serialSource(c, false)) >= 0) {
        if (ch.rxEchoStarted) ch.rxReceiveEligible = false;
        else { ch.rxHead = (ch.rxHead + 1) & 255; --ch.rxCount; }
    }
    if (!ch.tsrFull && !(ch.rr0NoLatch & RR0_TX_BUFFER_EMPTY) && transmitAllowed(c) && !(ch.WR[5] & WR5_SEND_BREAK))
        copyTdrTsr(c, ch.WR[8]);
    if (!sched_) return;
    for (bool tx : {true, false}) {
        const auto source = serialSource(c, tx);
        // [B] UM0109 WR5 D3 p.153 : TX Enable bas termine la trame déjà partie.
        // [F] Comme /CTS, il ne retient que le prochain caractère dans le TDR : le manuel dit
        // seulement que /CTS haut coupe le transmetteur (p.95). Tranché par : /CTS en cours de trame.
        const bool pending = tx ? ch.tsrFull && !(ch.WR[5] & WR5_SEND_BREAK) && (ch.txActive || transmitAllowed(c))
                                : ch.rxCount && (receiveAllowed(c) || (ch.WR[14] & 8) || ch.rxEchoStarted);
        const int64_t period = serialPeriod(c, tx);
        if (!pending || !period) sched_->cancel(source);
        else if (sched_->cyclesUntil(source) < 0) {
            // Un nouveau flux repart du cycle LIVE ; une reprise BREAK garde
            // la fraction déjà consommée pour le caractère suspendu.
            if (!(tx && ch.txActive)) ch.serialFraction[tx] = ch.serialDivisor[tx] = 0;
            const int64_t nextPeriod = tx && ch.txRemaining ? ch.txRemaining : serialPeriod(c, tx, true);
            sched_->schedule(source, sched_->liveNow() + nextPeriod);
            if (tx) ch.txRemaining = 0;
            if (tx) ch.txActive = true;
            // Une reprise d'horloge n'est pas un nouveau caractère : conserver
            // l'invalidation d'un écho interrompu. Le prochain pop/start dans
            // onSerialEvent capturera une nouvelle éligibilité.
            else if (!ch.rxEchoStarted) startRxFrame(c);
        }
    }
}

void Scc::onSerialEvent(int c, bool tx) {
    if (c < 0 || c > 1 || !sched_) return;
    if (tx) processTX(c);
    else {
        auto& ch = chn_[c];
        if (ch.rxCount) {
            const uint8_t b = ch.rxQueue[ch.rxHead];
            ch.rxHead = (ch.rxHead + 1) & 255; --ch.rxCount;
            // [B] UM0109 p.69 : RxD→TxD est relié directement, sans enables ni TSR.
            // Une reconfiguration en cours de trame supprime cet octet écho :
            // le modèle n'invente pas un caractère intact à partir de fragments.
            if (ch.rxEchoStarted && ch.rxEchoEligible && sink_ && !extLoopback_) sink_(c, b);
            if (ch.rxReceiveEligible && !localLoopbackOnly(c)) deliverByte(c, b);
        }
        ch.rxEchoStarted = ch.rxEchoEligible = ch.rxReceiveEligible = false;
    }
    const int64_t period = serialPeriod(c, tx);
    auto& ch = chn_[c];
    const bool pending = tx ? ch.tsrFull && !(ch.WR[5] & WR5_SEND_BREAK) && transmitAllowed(c)
                            : ch.rxCount && (receiveAllowed(c) || (ch.WR[14] & 8));
    if (pending && period) {
        const auto due = sched_->firingDue();
        sched_->schedule(serialSource(c, tx), (due >= 0 ? due : sched_->liveNow()) + serialPeriod(c, tx, true));
        if (tx) ch.txActive = true;
        else startRxFrame(c);
    }
}

void Scc::receiveByte(int c, uint8_t b) {
    if (c < 0 || c > 1 || localLoopbackOnly(c)
        || (!receiveAllowed(c) && !(chn_[c].WR[14] & 8))) return;
    if (!sched_) {
        if (echoByteAllowed(c) && sink_ && !extLoopback_) sink_(c, b);
        deliverByte(c, b); return;
    }
    auto& ch = chn_[c];
    if (ch.rxCount == 256) return;
    ch.rxQueue[(ch.rxHead + ch.rxCount) & 255] = b; ++ch.rxCount;
    updateSerial(c);
}

uint8_t Scc::readDataReg(int c) {
    auto& ch = chn_[c];
    const uint8_t value = ch.RR[8];
    if (!ch.rxFifoCount) return value;
    const int mode = (ch.WR[1] >> 3) & 3;
    const uint8_t mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC |
        ((ch.WR[1] & WR1_PARITY_SPECIAL) ? RR1_PARITY_ERROR : 0);
    ch.rxFirstPending = false;
    if ((mode == 1 || mode == 3) && (ch.rxErrors[ch.rxFifoHead] & mask)) ch.rxLocked = true;
    else popRx(c);
    refreshRx(c);
    return value;
}

void Scc::writeDataReg(int c, uint8_t v) {
    chn_[c].WR[8] = v;
    chn_[c].txWritten = true;
    if ((!((chn_[c].WR[3] & WR3_AUTO_ENABLES)) || transmitAllowed(c)) &&
        !chn_[c].tsrFull) copyTdrTsr(c, v);
    else {
        chn_[c].RR[1] &= ~RR1_ALL_SENT;
        updateRR0Clear(c, RR0_TX_BUFFER_EMPTY);
        intSourcesClear(c, INT_TX_BUFFER_EMPTY);
    }
    updateRR3(c); updateIRQ();
    if (sched_) updateSerial(c);
    else processTX(c); // Puce isolée sans ordonnanceur : compatibilité du banc.

}

// -----------------------------------------------------------------------------
//  Lecture / écriture d'un registre de contrôle
// -----------------------------------------------------------------------------
uint8_t Scc::readControl(int c) {
    uint8_t value = 0;
    uint8_t reg = activeReg_;
    // [B] UM0109 p.157–158 : WR7' D6 active cinq images de WR.
    // [B] UM0109 WR15 D0 p.174 : il ne sélectionne que la destination des écritures WR7 ; les
    // fonctions WR7' restent actives jusqu'à désactivation ou reset.
    if (chn_[c].WR7p & 0x40) {
        switch (reg) {
        case 4: return chn_[c].WR[4];
        case 5: return chn_[c].WR[5];
        case 9: return chn_[c].WR[3];
        case 11: return chn_[c].WR[10];
        case 14: return chn_[c].WR7p;
        default: break;
        }
    }
    // [B] UM0109 table Z85X30 p.144-145, RR6/RR7 p.182 : RR6B/RR7B sont les images RR2B/RR3B,
    // sélectionnées par WR15B (idem A).
    // [D] Hatari scc.c:1574,1581 @ f0736b24 — teste et lit ici A seul : le manuel prime.
    // [D] Le soft IACK (WR9 bit5) à la lecture de l'image RR6 : le manuel ne cite que RR2 (p.160).
    // Vient de MAME z80scc.cpp:1702-1706, 1527-1530 @ ce391499d37d (scc_register_read replie RR6 sur
    // do_sccreg_rr2 hors FIFO d'état). Tranché par : RR6 lu sur un 85C30, WR9 D5=1.
    if ((reg == 6 || reg == 7) && !(chn_[c].WR[15] & WR15_STATUS_FIFO)) reg -= 4;
    switch (reg) {
    // [B] UM0109 p.144-145, p.182-187 : RR4/5/9/11/14 sont les images de RR0/1/13/15/10.
    case 0: case 4: updateRR0(c); value = chn_[c].RR[0]; break;
    case 1: case 5:
        // [B] UM0109 RR1 D0 p.180 : « This bit is always set in synchronous and SDLC modes » ;
        // WR4 D3-D2 = 00 sélectionne ces modes (p.150). Le suivi interne reste celui de l'asynchrone.
        value = chn_[c].RR[1] | ((chn_[c].WR[4] & 0x0C) == 0 ? RR1_ALL_SENT : 0);
        break;
    case 2:
        updateRR2();
        if (chn_[0].WR[9] & WR9_SOFT_INTACK) doIACK(true);
        value = chn_[c].RR[2];
        break;
    case 3: value = c ? 0 : chn_[0].RR[3]; break;   // [B] UM0109 RR3 p.181 : tout à 0 sur B.
    case 6: case 7:
        value = chn_[c].RR[reg];
        break;
    case 8: value = readDataReg(c); break;
    case 10: case 14: value = chn_[c].RR[10]; break;
    case 12: value = chn_[c].WR[12]; break;
    case 13: case 9: value = chn_[c].WR[13]; break;
    // [B] UM0109 RR15 p.187 : D0 et D2 lus à 0, sans toucher WR15. Lecture SANS effet de bord :
    // « &= » effaçait pour de bon WR15 bit0 (accès WR7') et bit2 (FIFO de statut) à chaque lecture.
    case 15: case 11: value = chn_[c].WR[15] & 0xFA; break;
    default: value = 0; break;
    }
    return value;
}

void Scc::writeControl(int c, uint8_t value) {
    Chn& ch = chn_[c];
    if (activeReg_ == 0) {
        // [B] UM0109 WR0 p.139-142 : CRC reset, commande et sélection de registre
        // sont trois champs indépendants. RR0 D6 p.175 décrit explicitement le
        // reset EOM même en asynchrone (commande erronée mais effective).
        // [B] UM0109 WR0 p.141 : transmetteur coupé, cette commande ne remet pas le latch à zéro.
        if ((value & 0xc0) == 0xc0 && (chn_[c].WR[5] & WR5_TX_ENABLE)) {
            updateRR0Clear(c, RR0_TX_UNDERRUN_EOM);
            updateRR0(c); updateIRQ();
        }
        const uint8_t command = (value >> 3) & 7;
        // [B] UM0109 WR0 p.142, RR0 D6 p.175 : Send Abort (011) vide le tampon TX et remet Underrun/EOM à 1.
        if (command == 3) {
            updateRR0Set(c, RR0_TX_BUFFER_EMPTY | RR0_TX_UNDERRUN_EOM);
            updateRR0(c); updateIRQ();
        }
        {

            // [B] UM0109 WR0 p.141-142 : Reset Ext/Status, Reset Tx IP, Next Rx, Error Reset,
            // Reset Highest IUS, dans l'ordre de cette chaîne.
            if (command == WR0_CMD_RESET_EXT_STATUS) {
                updateRR3Bit(false, c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
                updateRR0LatchOff(c);
                updateRR3(c);
                updateIRQ();
            } else if (command == WR0_CMD_RESET_TX_IP) {
                ch.txWritten = false;
                chn_[0].RR[3] &= ~(c ? RR3_TX_IP_B : RR3_TX_IP_A);
                updateIRQ();
            } else if (command == 4) { // Enable Rx Interrupt on Next Character
                ch.rxFirstArmed = !ch.rxFifoCount;
                ch.rxFirstPending = ch.rxFifoCount != 0;
                updateRR3(c); updateIRQ();
            } else if (command == WR0_CMD_ERROR_RESET) {
                const int mode = (ch.WR[1] >> 3) & 3;
                const uint8_t mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC |
                    ((ch.WR[1] & WR1_PARITY_SPECIAL) ? RR1_PARITY_ERROR : 0);
                // [B] UM0109 WR0 p.142 : même avant lecture, l'erreur du caractère de
                // sortie entraîne sa perte. RR1 est collant : consulter le slot.
                const bool discard = ch.rxFifoCount && (mode == 1 || mode == 3) &&
                    (ch.rxErrors[ch.rxFifoHead] & mask);
                if (ch.rxFifoCount) ch.rxErrors[ch.rxFifoHead] = 0;
                if (discard) popRx(c);
                ch.rxLocked = false;
                ch.RR[1] &= ~(RR1_PARITY_ERROR | RR1_RX_OVERRUN | RR1_CRC_FRAMING);
                intSourcesClear(c, INT_RX_PARITY | INT_RX_OVERRUN | INT_RX_FRAMING);
                refreshRx(c);
            } else if (command == WR0_CMD_RESET_HIGHEST_IUS) {
                for (int i = 5; i >= 0; --i) if (ius_ & (1 << i)) { ius_ &= ~(1 << i); break; }
                updateIRQ();
            }
        }
        // [B] UM0109 WR0 p.141, p.30 : Point High ajoute 8 ; un seul jeu de bits de pointeur.
        activeReg_ = (value & 7) | (command == 1 ? 8 : 0);
        return;
    }

    // Le MMIO peut arriver après l'échéance LIVE, avant son dispatch. Ce
    // caractère est déjà fini : le livrer avec l'ancien WR5 avant BREAK.
    if (activeReg_ == 5 && (value & WR5_SEND_BREAK) && !(ch.WR[5] & WR5_SEND_BREAK)
        && sched_ && ch.txActive) {
        const auto source = serialSource(c, true);
        for (;;) {
            const int64_t remaining = sched_->rawCyclesUntil(source);
            if (remaining == INT64_MIN || remaining > 0) break;
            const int64_t deadline = sched_->liveNow() + remaining;
            sched_->cancel(source); processTX(c);
            // Le TDR suivant a commencé à l'ancienne échéance, pas au MMIO.
            // Rattraper également sa fin si elle précède déjà le BREAK.
            const int64_t period = serialPeriod(c, true);
            if (!ch.tsrFull || !transmitAllowed(c) || !period) break;
            sched_->schedule(source, deadline + serialPeriod(c, true, true));
            ch.txActive = true;
        }
    }
    if (sched_ && sched_->cyclesUntil(serialSource(c, false)) >= 0
        && value != ch.WR[activeReg_]) {
        const bool rxFormat = activeReg_ == 3 && ((value ^ ch.WR[3]) & 0xc0);
        const bool txStopsOnly = activeReg_ == 4 && (value & 0x0c) && (ch.WR[4] & 0x0c)
            && !((value ^ ch.WR[4]) & ~0x0c);
        // [B] UM0109 p.166 : WR11 D4..3 agit sur le seul multiplexeur TX.
        // Aucun bit du chemin RX ne change : conserver l'écho déjà engagé.
        const bool txClockOnly = activeReg_ == 11 && !((value ^ ch.WR[11]) & ~0x18);
        // WR12/13 règlent le BRG, pas les entrées RTxC/TRxC directes
        // sélectionnées par le multiplexeur RX ([B] UM0109 p.166–168).
        const bool directRxClock = ((ch.WR[11] >> 5) & 3) < 2;
        const bool brgTimeConstant = activeReg_ == 12 || activeReg_ == 13;
        const bool brgControlOnly = activeReg_ == 14 && !((value ^ ch.WR[14]) & ~0x03);
        const bool rxClockOrMode = (activeReg_ == 4 && !txStopsOnly)
            || (activeReg_ == 11 && !txClockOnly)
            || (brgTimeConstant && !directRxClock)
            || (activeReg_ == 14 && !(brgControlOnly && directRxClock));
        if (rxFormat || rxClockOrMode) ch.rxEchoEligible = false;
        // [B] UM0109 p.69 : le multiplexeur Local Loopback coupe l'entrée RxD externe. Même si
        // l'on revient avant le stop, ce fragment n'est plus un octet intact.
        if (activeReg_ == 14 && (value & 0x18) == 0x10) ch.rxReceiveEligible = false;
    }
    const uint8_t oldWr1 = ch.WR[1], oldWr4 = ch.WR[4], oldWr5 = ch.WR[5], oldWr11 = ch.WR[11];
    // [B] UM0109 WR15 D0 p.174, p.157 : WR7' = Active_Reg==7 et WR15 bit0=1
    if (activeReg_ == 7 && (ch.WR[15] & 1)) ch.WR7p = value;
    else ch.WR[activeReg_] = value;

    switch (activeReg_) {
    case 1:
        // [B] UM0109 p.56-57 : masquer l'IP conserve les latches.
        // [D] Le réarmement rend visible la condition mémorisée, sans nouveau front : déduit du
        // manuel (IP = latches fermés) et de MAME z80scc.cpp:1964-1967 @ ce391499d37d (do_sccreg_wr1). Tranché par :
        // WR1 D0 remis à 1 sous latch fermé sur un 85C30, puis RR3 lu.
        updateRR3Bit((value & WR1_EXT_INT_ENABLE) && ch.rr0Latched,
                     c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
        if ((value & 0x18) != (oldWr1 & 0x18)) {
            const int oldMode = (oldWr1 >> 3) & 3, newMode = (value >> 3) & 3;
            // [B] UM0109 p.146 : changer entre First/Special et Special Only
            // n'est ni un Error Reset ni une désactivation des interruptions.
            // Conserver le verrou spécial déjà acquis dans ces deux modes.
            if (!((oldMode == 1 || oldMode == 3) && (newMode == 1 || newMode == 3)))
                ch.rxLocked = false;
            ch.rxFirstArmed = true; ch.rxFirstPending = false;
            if ((value & 0x18) == 8 && ch.rxFifoCount) {
                ch.rxFirstArmed = false; ch.rxFirstPending = true;
            }
        }
        updateRR3(c); updateIRQ(); break;
    case 2: chn_[0].WR[2] = value; break;                 // [B] UM0109 WR2 p.147 : WR2 commun
    case 3: {
        static constexpr uint8_t bits[] = {5,7,6,8};
        ch.rxBits = bits[value >> 6]; updateRts(c); updateSerial(c); break;
    }
    case 4: {
        ch.parityBits = (value & WR4_PARITY_ENABLE) ? 1 : 0;
        // [B] UM0109 p.58, RR0 D6 p.175 : Underrun/EOM à 1 en asynchrone.
        // [D] Hatari scc.c:1816-1817 @ f0736b24 — le repose à l'écriture de WR4.
        if (((value >> 2) & 3) != WR4_STOP_SYNC) updateRR0Set(c, RR0_TX_UNDERRUN_EOM);
        updateBrg(c); updateRts(c); updateSerial(c);
        if ((oldWr4 ^ value) & 0x3c) { updateRR0(c); updateIRQ(); }
        break;
    }
    case 5:                                               // RTS/DTR/break : lignes non câblées…
        { static constexpr uint8_t bits[] = {5,7,6,8}; ch.txBits = bits[(value >> 5) & 3]; }
        updateRts(c); updateSerial(c);
        // …sauf prise de bouclage branchée : RTS→CTS, DTR→DCD et DTR→/SYNC (DSR)
        // suivent les sorties physiques, avec fronts ext-status comme sur la vraie prise.
        // [F] Câblage et mécanisme déduits du test « I SCC », cf. Scc.hpp.
        // [B] UM0109 WR5 D4 p.153 : SEND BREAK (bit4) force TxD à 0. Bouclé (prise externe ou WR14
        // local), RxD le voit et RR0 bit7 (Break/Abort) suit. C'est LE mécanisme de
        // détection des prises du test « I SCC » du diagnostic MegaSTE (le break
        // émis doit revenir). [B] UM0109 p.58 : fin de break = second front ext-status.
        // ⚠ [B] UM0109 p.58, RR0 D7 p.175 : Break suppose un caractère nul avec erreur de framing,
        //    et sa fin laisse un caractère nul dans la FIFO. Ici RR0 D7 suit l'écriture de WR5, sans
        //    délai et sans WR3 Rx Enable (écart non corrigé : le test « I SCC » s'en sert).
        // [B] UM0109 p.58 (RR0 D7) et p.98 (Asynchronous Mode) : « A single null character is left in
        //    the Receive FIFO each time that the break condition is terminated », sans framing error.
        //    Livré ici à la fin du BREAK bouclé, en asynchrone et récepteur validé.
        //    [F] Le Parity Error que la p.98 annonce en parité impaire n'est pas posé : la FIFO
        //    NeoST ne calcule aucune parité RX. Tranché par la même page une fois la parité RX modélisée.
        if (extLoopback_ || (ch.WR[14] & 0x10)) {
            if (value & 0x10) updateRR0Set(c, RR0_BREAK_ABORT);
            else {
                updateRR0Clear(c, RR0_BREAK_ABORT);
                if ((oldWr5 & WR5_SEND_BREAK) && (ch.WR[4] & 0x0c)) deliverByte(c, 0x00);
            }
        }
        if (extLoopback_) {
            if (value & 0x80) updateRR0Set(c, RR0_SYNC_HUNT);
            else              updateRR0Clear(c, RR0_SYNC_HUNT);
        }
        if (extLoopback_ || (ch.WR[14] & 0x10)) {
            updateRR0(c);
            updateIRQ();
        }
        break;
    case 8: writeDataReg(c, value); break;
    case 11:
        updateBrg(c); updateSerial(c);
        if ((oldWr11 ^ value) & 0x80) { updateRR0(c); updateIRQ(); }
        break;
    case 12: case 13: case 14:
        updateBrg(c); updateSerial(c);
        break;
    case 9: {
        chn_[0].WR[9] = value;   // [B] UM0109 WR9 p.159 : un seul WR9, accessible des deux canaux.
        // [B] UM0109 p.44/p.161 : MIE masque la demande ; seuls les resets ou Reset Highest IUS
        // effacent les IUS déjà acquis.
        uint8_t command = (value >> 6) & 3;
        if (command == WR9_CMD_RESET_FORCE_HW) resetFull(false);
        else if (command == WR9_CMD_RESET_A) resetChannel(0, false);
        else if (command == WR9_CMD_RESET_B) resetChannel(1, false);
        updateIRQ();
        break;
    }
    case 15:
        if (!(value & WR15_BREAK_ABORT_IE)) ch.breakPending = 0;
        // [B] UM0109 p.59 : IE à 0, le bit ZC est forcé à 0.
        if ((value & WR15_ZERO_COUNT_IE) == 0) updateRR0Clear(c, RR0_ZERO_COUNT);
        updateRR3(c); updateIRQ();
        break;
    default: break;
    }
    // [B] UM0109 p.28, p.30 : le pointeur revient à 0 après l'accès au registre choisi.
    activeReg_ = 0;
}

// -----------------------------------------------------------------------------
//  Décodage MMIO $FF8C80-$FF8C87 (port de SCC_handleRead/Write + IoMem)
//  [D] Hatari scc.c:1630-1647,1939-1951,2458-2482 @ f0736b24 — bit2 = canal, bit1 = donnée.
// -----------------------------------------------------------------------------
uint8_t Scc::handleRead(uint32_t addr) {
    int channel = (addr >> 2) & 1;               // bit2 : 0 = A, 1 = B
    int reg = activeReg_;
    uint8_t v = (addr & 2) ? readDataReg(channel) : readControl(channel);
    if (trace_) std::fprintf(stderr, "[scc] rd %c %s%d=$%02X\n", 'A' + channel,
                             (addr & 2) ? "RRdata" : "RR", reg, v);
    // [B] UM0109 p.28, p.30 : une lecture D//C haut ne touche pas le pointeur, seule D//C bas le remet à 0.
    // [D] Hatari scc.c:1644 @ f0736b24 le remet à toute lecture ; le manuel prime.
    if (!(addr & 2)) activeReg_ = 0;
    return v;
}
void Scc::handleWrite(uint32_t addr, uint8_t v) {
    int channel = (addr >> 2) & 1;
    if (trace_) std::fprintf(stderr, "[scc] wr %c %s%d=$%02X\n", 'A' + channel,
                             (addr & 2) ? "WRdata" : "WR", activeReg_, v);
    // [B] UM0109 p.28, p.30 : l'écriture D//C haut laisse le pointeur en l'état.
    if (addr & 2) writeDataReg(channel, v);
    else          writeControl(channel, v);
}

uint8_t Scc::read8(uint32_t addr) {
    return (addr & 1) ? handleRead(addr) : 0xFF;   // octet pair = $FF ([D] Hatari scc.c:2465-2468 @ f0736b24)
}
void Scc::write8(uint32_t addr, uint8_t v) {
    if (addr & 1) handleWrite(addr, v);            // octet pair ignoré
}
