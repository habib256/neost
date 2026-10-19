// =============================================================================
//  Scc.cpp — SCC Z85C30 du Mega STE (port de Hatari scc.c).
//
//  Port fonctionnel fidèle du cœur registre/IRQ de scc.c (cf. Scc.hpp pour le
//  périmètre). Les commentaires renvoient aux fonctions SCC_* d'Hatari.
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

// Commandes WR0 (bits 3-5)
constexpr uint8_t WR0_CMD_RESET_EXT_STATUS = 0x02, WR0_CMD_RESET_TX_IP = 0x05,
    WR0_CMD_ERROR_RESET = 0x06, WR0_CMD_RESET_HIGHEST_IUS = 0x07;
// WR1
constexpr uint8_t WR1_EXT_INT_ENABLE = 0x01, WR1_TX_INT_ENABLE = 0x02, WR1_PARITY_SPECIAL = 0x04;
constexpr uint8_t WR1_RX_MODE_FIRST_SPECIAL = 0x01, WR1_RX_MODE_ALL_SPECIAL = 0x02;
// WR3 / WR5
constexpr uint8_t WR3_RX_ENABLE = 0x01, WR3_SYNC_LOAD_INHIBIT = 0x02, WR3_AUTO_ENABLES = 0x20;
[[maybe_unused]] constexpr uint8_t WR5_RTS = 0x02, WR5_TX_ENABLE = 0x08, WR5_SEND_BREAK = 0x10, WR5_DTR = 0x80;
// WR4 stop bits
constexpr uint8_t WR4_PARITY_ENABLE = 0x01, WR4_STOP_SYNC = 0x00;
// WR9
constexpr uint8_t WR9_VIS = 0x01, WR9_NV = 0x02, WR9_MIE = 0x08, WR9_STATUS_HIGH_LOW = 0x10, WR9_SOFT_INTACK = 0x20;
constexpr uint8_t WR9_CMD_RESET_B = 0x01, WR9_CMD_RESET_A = 0x02, WR9_CMD_RESET_FORCE_HW = 0x03;
// WR15
[[maybe_unused]] constexpr uint8_t WR15_WR7_PRIME = 0x01, WR15_ZERO_COUNT_IE = 0x02, WR15_STATUS_FIFO = 0x04,
    WR15_DCD_IE = 0x08, WR15_SYNC_HUNT_IE = 0x10, WR15_CTS_IE = 0x20,
    WR15_TX_UNDERRUN_IE = 0x40, WR15_BREAK_ABORT_IE = 0x80;
// RR0
constexpr uint8_t RR0_RX_CHAR_AVAIL = 0x01, RR0_ZERO_COUNT = 0x02, RR0_TX_BUFFER_EMPTY = 0x04,
    RR0_DCD = 0x08, RR0_SYNC_HUNT = 0x10, RR0_CTS = 0x20, RR0_TX_UNDERRUN_EOM = 0x40, RR0_BREAK_ABORT = 0x80;
// RR1
constexpr uint8_t RR1_ALL_SENT = 0x01, RR1_PARITY_ERROR = 0x10, RR1_RX_OVERRUN = 0x20,
    RR1_CRC_FRAMING = 0x40, RR1_EOF_SDLC = 0x80;
// RR3 (interrupt pending, channel A holds all)
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
    ch.WR[0] = 0x00;
    activeReg_ = 0;
    ch.WR[1] &= 0x24; ch.WR[3] &= 0xfe; ch.WR[4] |= 0x04; ch.WR[5] &= 0x61;
    ch.WR[15] = 0xf8; ch.WR7p = 0x20;
    if (hw) {
        // WR9 force-hardware-reset désactive WR14 : annuler aussi son événement.
        // Hatari laisse ici un timer fantôme (ResetChannel ne le retire pas),
        // contrairement à son reset machine global qui réinitialise CycInt.
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
    // UM0109 WR9 p.160 : seuls les IP/IUS du canal réinitialisé sont effacés.
    // RR3A contient aussi les IP de B : ne pas remettre tout ce registre à zéro.
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
// Les modes TX/RX de WR11 n'empêchent PAS le BRG de tourner. Seul le débit BRG
// est porté ici ; les transferts utilisent des événements caractère séparés.
// -----------------------------------------------------------------------------
int64_t Scc::nextBrgPeriod(int c) {
    auto& ch = chn_[c];
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
    const uint32_t source = (ch.WR[14] & 2) ? uint32_t(neost::pacing::kCpuHzInt) : 3672000;
    // UM0109 p.71 : ZC précède le diviseur /2 et le multiplicateur WR4.
    // Une nouvelle constante n'entre au compteur qu'au prochain rechargement.
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
    // RR0 ZC reste haut un cycle source, jusqu'au rechargement (p.57/p.177).
    const int64_t pulseTotal = neost::pacing::kCpuHzInt + ch.brgFraction;
    ch.brgPulseStart = anchor;
    ch.brgPulseEnd = anchor + pulseTotal / ch.brgSource
        + (pulseTotal % ch.brgSource != 0) - (ch.brgFraction != 0);
    brgPeriod_[c] = nextBrgPeriod(c);
    sched_->schedule(brgSource(c), anchor + brgPeriod_[c]);
    // Ne pas retenir le front haut précédent dans RR0 : ce bit n'est pas latché.
    ch.RR[0] &= ~RR0_ZERO_COUNT;
    // Même si le dispatch CPU dépasse la pulse, son front a fermé le latch.
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
// Lignes de modem en ENTRÉE. Sans rien de branché : au repos = assertées, comme
// Hatari sans TTY. Prise de bouclage Field Service branchée (setLoopback) : CTS
// suit la sortie /RTS réelle et DCD suit DTR (WR5 bit7) du MÊME canal — c'est ce que le
// test « I SCC » du diagnostic MegaSTE bascule pour détecter la prise.
uint16_t Scc::getCTS(int c) const { return extLoopback_ ? (chn_[c].rtsAsserted ? 1 : 0) : 1; }
uint16_t Scc::getDCD(int c) const { return extLoopback_ ? ((chn_[c].WR[5] & 0x80) ? 1 : 0) : 1; }

// Branche/débranche les prises de bouclage : synchronise /SYNC (DSR de la prise,
// suit DTR) puis recalcule RR0 des deux canaux (fronts CTS/DCD → ext-status IRQ
// si armés, exactement comme une vraie (dé)connexion).
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
        // Servir d'abord l'état opposé à l'image verrouillée. Les transitions
        // alternent ; deux flags retiennent chaque polarité sans file illimitée.
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
    // UM0109 p.58 : les deux fronts BREAK ferment les latches même si une
    // autre cause est déjà en service. Détecter les fronts bruts une seule fois,
    // pas à chaque lecture RR0. Les cycles répétés coalescent par polarité.
    const bool breakAllowed = breakStatusAllowed(c);
    const bool externalSync = (ch.WR[4] & 0x0c) || (ch.WR[4] & 0x30) == 0x30;
    const bool syncSuppressed = externalSync && (ch.WR[11] & 0x80);
    // UM0109 p.58–59 : BREAK vaut zéro hors asynchrone/SDLC ; la broche
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
    if ((ch.WR[15] & WR15_ZERO_COUNT_IE) && now >= ch.brgPulseStart && now < ch.brgPulseEnd)
        ch.rr0NoLatch |= RR0_ZERO_COUNT;
    else ch.rr0NoLatch &= ~RR0_ZERO_COUNT;
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

    // UM0109 p.56–57 : les latches WR15 fonctionnent même avec WR1 D0=0.
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
    uint8_t status, mask;
    uint8_t rr3 = chn_[0].RR[3];
    if (rr3 & RR3_RX_IP_A) {
        mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC;
        if (chn_[0].WR[1] & WR1_PARITY_SPECIAL) mask |= RR1_PARITY_ERROR;
        // Bits d'erreur RX : ils vivent dans RR1 (tester RR0 prenait CTS/underrun
        // pour des erreurs → chaque RX vectorisé partait en « special condition »).
        status = (chn_[0].RR[1] & mask) ? 7 : 6;
    } else if (rr3 & RR3_TX_IP_A) status = 4;
    else if (rr3 & RR3_EXT_IP_A) status = 5;
    else if (rr3 & RR3_RX_IP_B) {
        mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC;
        if (chn_[1].WR[1] & WR1_PARITY_SPECIAL) mask |= RR1_PARITY_ERROR;
        status = (chn_[1].RR[1] & mask) ? 3 : 2;    // cf. canal A : erreurs RX = RR1
    } else if (rr3 & RR3_TX_IP_B) status = 0;
    else if (rr3 & RR3_EXT_IP_B) status = 1;
    else status = 3;
    return status;
}

void Scc::updateRR2() {
    uint8_t vector = chn_[0].WR[2];
    chn_[0].RR[2] = vector;                       // RR2A = WR2
    uint8_t status = getVectorStatus();
    if (chn_[0].WR[9] & WR9_STATUS_HIGH_LOW) {
        status = ((status & 1) << 2) + (status & 2) + ((status & 4) >> 2);
        vector &= 0x8f; vector |= (status << 4);
    } else {
        vector &= 0xf1; vector |= (status << 1);
    }
    chn_[1].RR[2] = vector;                       // RR2B = WR2 + statut
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
    // UM0109 p.47–48 : en mode All, retirer le dernier caractère efface IP,
    // même si RR1 conserve l'overrun/parité pour les prochains vecteurs.
    // Les modes First/Special conservent leur IP tant que la FIFO est verrouillée.
    bool set = (intRx && ch.rxFifoCount) || (rxMode && ch.rxLocked);
    updateRR3Bit(set, c ? RR3_RX_IP_B : RR3_RX_IP_A);

    set = (ch.RR[0] & RR0_TX_BUFFER_EMPTY) && (ch.WR[1] & WR1_TX_INT_ENABLE) && ch.txWritten;
    updateRR3Bit(set, c ? RR3_TX_IP_B : RR3_TX_IP_A);
}

// -----------------------------------------------------------------------------
//  IRQ / IACK
// -----------------------------------------------------------------------------
void Scc::updateIRQ() {
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
    setLineIRQ(SCC_IRQ_OFF);
    // UM0109 WR9 D3 p.161 : aucun nouvel IUS lorsque MIE est désarmé,
    // y compris lors d'un acquittement logiciel par lecture de RR2.
    if (chn_[0].WR[9] & WR9_MIE) {
        for (int i = 5; i >= 0; --i) {
            // UM0109 p.41–42 : la chaîne des IUS masque aussi l'INTACK,
            // sans empêcher une source plus prioritaire de se faire acquitter.
            if (ius_ & (1 << i)) break;
            if (chn_[0].RR[3] & (1 << i)) { ius_ |= (1 << i); break; }
        }
    }
    updateRR2();
    return (chn_[0].WR[9] & WR9_VIS) ? chn_[1].RR[2] : chn_[0].RR[2];
}

int Scc::processIack() {
    int vector = doIACK(false);
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
    // UM0109 p.94/p.153 : en mode « cinq ou moins », les bits hauts du
    // caractère encodent la longueur, puis seuls les bits bas sont émis.
    // Le format est pris au chargement TSR (UM0109 p.114), pas à sa sortie.
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
        unsigned ones = 0;
        for (uint8_t b = ch.tsr; b; b >>= 1) ones ^= b & 1;
        ch.tsrParity = uint8_t(ones ^ ((ch.WR[4] & 2) ? 0 : 1));
    }
    chn_[c].tsrFull = true;
    chn_[c].RR[1] &= ~RR1_ALL_SENT;
    updateRR0Set(c, RR0_TX_BUFFER_EMPTY);
    intSourcesSet(c, INT_TX_BUFFER_EMPTY);
}

void Scc::serialWriteByte(int c, uint8_t v) {
    // UM0109 p.69/p.172 : Auto Echo prime sur Local Loopback et déconnecte
    // complètement la sortie du TSR. Le pont RxD→TxD utilise seulement les
    // octets hôtes async huit bits sans parité dans onSerialEvent, sans
    // reconstruire les bits ni les intervalles du fil source.
    if (chn_[c].WR[14] & 0x08) return;
    if (trace_) std::fprintf(stderr, "[scc] TX %c: $%02X\n", 'A' + c, v);
    // Bouclage local interne (WR14 bit4 = Local Loopback, Zilog Z85C30 §WR14) : TxD est
    // relié à RxD à l'intérieur de la puce ; TxD reste aussi connecté au
    // transmetteur (UM0109 p.69). La prise externe occupe le sink hôte, comme
    // hors bouclage local, mais ne doit pas doubler la réception interne.
    // Hatari ne modélise pas le bouclage local. ⚠ Le reset matériel met WR14=$30 (bit4=1) : un programme qui transmet SANS
    // reconfigurer WR14 boucle donc sur lui-même — inoffensif en pratique car tout pilote
    // série/LAN réécrit WR14 (réglage du BRG, bit4=0) avant d'émettre. NE PAS « corriger ».
    // Parité connue seulement pour une boucle avec mêmes largeurs de données.
    // Les formats discordants nécessiteraient un véritable récepteur de bits.
    const auto& ch = chn_[c];
    const int parity = ch.tsrBits == ch.rxBits && ch.tsrParity < 2 ? ch.tsrParity : -1;
    if (ch.WR[14] & 0x10) deliverByte(c, v, parity);
    // Prise de bouclage externe (Field Service) : TxD revient sur RxD du même canal.
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
        ch.RR[1] |= RR1_ALL_SENT;
    }
    if ((ch.RR[0] & RR0_TX_BUFFER_EMPTY) == 0 && transmitAllowed(c))
        copyTdrTsr(c, ch.WR[8]);
    if (!(ch.RR[0] & RR0_TX_BUFFER_EMPTY)) ch.RR[1] &= ~RR1_ALL_SENT;
    updateRts(c);
    updateRR3(c); updateIRQ();
}

// UM0109 : données et erreurs progressent ensemble ; l'overrun remplace
// le dernier emplacement (MAME receive_data), en conservant les deux plus anciens.
void Scc::refreshRx(int c) {
    auto& ch = chn_[c];
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
        // UM0109 p.47/p.142 : l'IP du premier caractère disparaît aussi
        // quand Error Reset retire ce caractère avant sa lecture.
        ch.rxFirstPending = false;
        ch.rxFifoHead = (ch.rxFifoHead + 1) % 3; --ch.rxFifoCount;
    }
}
void Scc::deliverByte(int c, uint8_t b, int parity) {
    auto& ch = chn_[c];
    if (!receiveAllowed(c)) return;
    // Zilog UM010904, WR3 (p.148) : en asynchrone, les bits RR8
    // inutilisés valent 1. Appliquer le format à l'entrée de la FIFO : une
    // reprogrammation ultérieure de WR3 ne modifie pas les octets déjà reçus.
    // En synchrone/SDLC, les huit bits de la tranche série sont conservés.
    if (ch.WR[4] & 0x0c) {
        unsigned used = ch.rxBits;
        // UM0109 WR4 p.152 : le bit de parité reçu est transmis au CPU
        // pour RX court. receiveByte ne fournit aucun bit physique : conserver
        // sa convention d'octet hôte, sans supposer une parité valide.
        if (used < 8 && (ch.WR[4] & WR4_PARITY_ENABLE) && parity >= 0) {
            b = uint8_t((b & ((1u << used)-1)) | (unsigned(parity) << used));
            ++used;
        }
        b |= uint8_t(0xffu << used);
    }
    // UM0109 WR3 p.149 : comparer WR6 à l'octet prêt pour la FIFO, même
    // en asynchrone (avertissement explicite du manuel). SDLC réemploie ce bit
    // pour l'adresse ; en Bisync 12 bits (WR10 D0), il est ignoré. Ceci ne
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
    if (ch.rxFirstArmed && ((ch.WR[1] >> 3) & 3) == 1) {
        ch.rxFirstPending = true; ch.rxFirstArmed = false;
    }
    refreshRx(c);
}

// Cadence caractère : scc.c SCC_Start_InterruptHandler_TX_RX (2139-2173).
// Les horloges TX et RX sont indépendantes ; DPLL reste hors périmètre.
int64_t Scc::serialPeriod(int c, bool tx, bool advance) {
    auto& ch = chn_[c];
    static constexpr int mults[] = {1,16,32,64};
    static constexpr int stops2[] = {0,2,3,4};
    const int mult = (ch.WR[4] & 12) ? mults[ch.WR[4] >> 6] : 1;
    const int source = (ch.WR[11] >> (tx ? 3 : 5)) & 3;
    const int bits = tx ? (ch.tsrFull ? ch.tsrBits : ch.txBits) : ch.rxBits;
    const int selectedStops = (ch.WR[4] >> 2) & 3;
    // UM0109 p.93/p.151 : WR4 ne sélectionne que les stops émis ;
    // le récepteur vérifie un seul stop, quel que soit ce sélecteur TX.
    const int halfBits = 2*(bits + 1 + ch.parityBits)
        + ((!tx && selectedStops) ? 2 : stops2[selectedStops]);
    if (source == 3 || (source == 2 && !(ch.WR[14] & 1))) return 0;
    const int64_t freq = source == 2 ? ((ch.WR[14] & 2) ? neost::pacing::kCpuHzInt : 3'672'000)
                                     : (source == 0 ? 3'672'000 : 2'457'600);
    // UM0109 WR12/13 : BRG = source / [2*(TC+2)]. La cadence des
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

// UM0109 WR3 p.149 : qualifier les enables logiciels avec les broches réelles,
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
// UM0109 p.95 / p.154 : en asynchrone Auto Enables, attendre le dernier
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
    // UM0109 p.153 et MAME tra_callback : BREAK suspend le TSR ; il force
    // TxD au prochain bit sans consommer de donnée. Notre tranche au caractère
    // conserve les cycles restants, sans phase sous-bit ni erreurs RX inventées.
    // Un caractère coupé ne peut pas être livré comme un octet intact.
    if ((ch.WR[5] & WR5_SEND_BREAK) && sched_ && ch.txActive) {
        const int64_t remaining = sched_->rawCyclesUntil(serialSource(c, true));
        if (remaining > 0) {
            ch.txRemaining = remaining; ch.txInterrupted = true;
            sched_->cancel(serialSource(c, true));
        }
    }
    // UM0109 p.106 : /DCD inactif perd le caractère en cours, pas la FIFO.
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
        // UM0109 WR5 p.153 : TX Enable bas termine la trame déjà partie.
        // Comme /CTS, il ne retient que le prochain caractère dans le TDR.
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
            // RxD→TxD est indépendant des enables du récepteur et du TSR.
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
    // Z85C30, UM0109 p.157–158 : WR7' D6 active cinq images de WR.
    // WR15 D0 ne sélectionne que la destination des écritures WR7 ; les
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
    // Zilog UM0109, table Z85X30 : RR6B/RR7B sont les images RR2B/RR3B,
    // sélectionnées par WR15B (idem A). Hatari scc.c teste et lit ici A seul.
    // MAME scc_register_read remappe aussi avant do_sccreg_rr2 : réutiliser la
    // lecture réelle garde le vecteur courant et son soft IACK (WR9 bit5).
    if ((reg == 6 || reg == 7) && !(chn_[c].WR[15] & WR15_STATUS_FIFO)) reg -= 4;
    switch (reg) {
    case 0: case 4: updateRR0(c); value = chn_[c].RR[0]; break;
    case 1: case 5: value = chn_[c].RR[1]; break;
    case 2:
        updateRR2();
        if (chn_[0].WR[9] & WR9_SOFT_INTACK) doIACK(true);
        value = chn_[c].RR[2];
        break;
    case 3: value = c ? 0 : chn_[0].RR[3]; break;
    case 6: case 7:
        value = chn_[c].RR[reg];
        break;
    case 8: value = readDataReg(c); break;
    case 10: case 14: value = chn_[c].RR[10]; break;
    case 12: value = chn_[c].WR[12]; break;
    case 13: case 9: value = chn_[c].WR[13]; break;
    // Lecture SANS effet de bord : « &= » effaçait pour de bon WR15 bit0 (accès
    // WR7') et bit2 (FIFO de statut) à chaque lecture de RR15.
    case 15: case 11: value = chn_[c].WR[15] & 0xFA; break;
    default: value = 0; break;
    }
    return value;
}

void Scc::writeControl(int c, uint8_t value) {
    Chn& ch = chn_[c];
    if (activeReg_ == 0) {
        // UM0109 WR0 p.139 : CRC reset, commande et sélection de registre
        // sont trois champs indépendants. RR0 p.176 décrit explicitement le
        // reset EOM même en asynchrone (commande erronée mais effective).
        if ((value & 0xc0) == 0xc0) {
            updateRR0Clear(c, RR0_TX_UNDERRUN_EOM);
            updateRR0(c); updateIRQ();
        }
        const uint8_t command = (value >> 3) & 7;
        {

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
                // UM0109 WR0 : même avant lecture, l'erreur du caractère de
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
        // UM0109 p.166 : WR11 D4..3 agit sur le seul multiplexeur TX.
        // Aucun bit du chemin RX ne change : conserver l'écho déjà engagé.
        const bool txClockOnly = activeReg_ == 11 && !((value ^ ch.WR[11]) & ~0x18);
        // WR12/13 règlent le BRG, pas les entrées RTxC/TRxC directes
        // sélectionnées par le multiplexeur RX (UM0109 p.166–168).
        const bool directRxClock = ((ch.WR[11] >> 5) & 3) < 2;
        const bool brgTimeConstant = activeReg_ == 12 || activeReg_ == 13;
        const bool brgControlOnly = activeReg_ == 14 && !((value ^ ch.WR[14]) & ~0x03);
        const bool rxClockOrMode = (activeReg_ == 4 && !txStopsOnly)
            || (activeReg_ == 11 && !txClockOnly)
            || (brgTimeConstant && !directRxClock)
            || (activeReg_ == 14 && !(brgControlOnly && directRxClock));
        if (rxFormat || rxClockOrMode) ch.rxEchoEligible = false;
        // Le multiplexeur Local Loopback coupe l'entrée RxD externe. Même si
        // l'on revient avant le stop, ce fragment n'est plus un octet intact.
        if (activeReg_ == 14 && (value & 0x18) == 0x10) ch.rxReceiveEligible = false;
    }
    const uint8_t oldWr1 = ch.WR[1], oldWr4 = ch.WR[4], oldWr11 = ch.WR[11];
    // WR7' : Active_Reg==7 et WR15 bit0=1
    if (activeReg_ == 7 && (ch.WR[15] & 1)) ch.WR7p = value;
    else ch.WR[activeReg_] = value;

    switch (activeReg_) {
    case 1:
        // MAME do_sccreg_wr1 : masquer l'IP conserve les latches ; le
        // réarmement rend visible la condition mémorisée, sans nouveau front.
        updateRR3Bit((value & WR1_EXT_INT_ENABLE) && ch.rr0Latched,
                     c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
        if ((value & 0x18) != (oldWr1 & 0x18)) {
            const int oldMode = (oldWr1 >> 3) & 3, newMode = (value >> 3) & 3;
            // UM0109 p.146 : changer entre First/Special et Special Only
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
    case 2: chn_[0].WR[2] = value; break;                 // WR2 commun
    case 3: {
        static constexpr uint8_t bits[] = {5,7,6,8};
        ch.rxBits = bits[value >> 6]; updateRts(c); updateSerial(c); break;
    }
    case 4: {
        ch.parityBits = (value & WR4_PARITY_ENABLE) ? 1 : 0;
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
        // SEND BREAK (bit4) : TxD forcé à l'espace — bouclé (prise externe ou WR14
        // local), RxD le voit et RR0 bit7 (Break/Abort) suit. C'est LE mécanisme de
        // détection des prises du test « I SCC » du diagnostic MegaSTE (le break
        // émis doit revenir). Fin de break = second front ext-status, comme la puce.
        if (extLoopback_ || (ch.WR[14] & 0x10)) {
            if (value & 0x10) updateRR0Set(c, RR0_BREAK_ABORT);
            else              updateRR0Clear(c, RR0_BREAK_ABORT);
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
        chn_[0].WR[9] = value;
        // MIE masque la demande ; seuls les resets ou Reset Highest IUS
        // effacent les IUS déjà acquis (UM0109 p.44/p.161).
        uint8_t command = (value >> 6) & 3;
        if (command == WR9_CMD_RESET_FORCE_HW) resetFull(false);
        else if (command == WR9_CMD_RESET_A) resetChannel(0, false);
        else if (command == WR9_CMD_RESET_B) resetChannel(1, false);
        updateIRQ();
        break;
    }
    case 15:
        if (!(value & WR15_BREAK_ABORT_IE)) ch.breakPending = 0;
        if ((value & WR15_ZERO_COUNT_IE) == 0) updateRR0Clear(c, RR0_ZERO_COUNT);
        updateRR3(c); updateIRQ();
        break;
    default: break;
    }
    activeReg_ = 0;
}

// -----------------------------------------------------------------------------
//  Décodage MMIO $FF8C80-$FF8C87 (port de SCC_handleRead/Write + IoMem)
// -----------------------------------------------------------------------------
uint8_t Scc::handleRead(uint32_t addr) {
    int channel = (addr >> 2) & 1;               // bit2 : 0 = A, 1 = B
    int reg = activeReg_;
    uint8_t v = (addr & 2) ? readDataReg(channel) : readControl(channel);
    if (trace_) std::fprintf(stderr, "[scc] rd %c %s%d=$%02X\n", 'A' + channel,
                             (addr & 2) ? "RRdata" : "RR", reg, v);
    activeReg_ = 0;
    return v;
}
void Scc::handleWrite(uint32_t addr, uint8_t v) {
    int channel = (addr >> 2) & 1;
    if (trace_) std::fprintf(stderr, "[scc] wr %c %s%d=$%02X\n", 'A' + channel,
                             (addr & 2) ? "WRdata" : "WR", activeReg_, v);
    if (addr & 2) writeDataReg(channel, v);
    else          writeControl(channel, v);
}

uint8_t Scc::read8(uint32_t addr) {
    return (addr & 1) ? handleRead(addr) : 0xFF;   // octet pair = $FF
}
void Scc::write8(uint32_t addr, uint8_t v) {
    if (addr & 1) handleWrite(addr, v);            // octet pair ignoré
}
