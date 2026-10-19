// =============================================================================
//  MidiAcia.cpp — ACIA 6850 MIDI avec bouclage OUT→IN (cf. MidiAcia.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/MidiAcia.hpp"

#include <cstdio>
#include <cstdlib>
#include "io/Mfp.hpp"

// Bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_OVRN = 0x20,   // Receiver Overrun, visible après lecture de RDR
    ACIA_IRQ  = 0x80,   // ligne d'interruption (vers GPIP4 du MFP)
};

// Temps série d'un octet MIDI (trame de 10 bits à 31250 bauds) en cycles 68000 à
// 8 MHz : 8 000 000 × 10 / 31250 = 2560 (horloge /16 du quartz 500 kHz).
static constexpr int64_t kMidiRxByteCycles = 2560;

// acia.c ACIA_Serial_Params : start + données + parité éventuelle + stop(s).
static constexpr uint8_t kTxFrameBits[8] = {11,11,10,10,11,10,11,11};

int64_t MidiAcia::txBitCycles() const {
    // Le master reset conserve une base sûre /1 ; il n'émet aucun octet engagé.
    static constexpr int64_t bits[4] = {16,256,1024,16};
    return bits[control_ & 3];
}

uint8_t MidiAcia::txData(uint8_t data) const {
    return ((control_ >> 2) & 7) < 4 ? data & 0x7f : data;
}

int64_t MidiAcia::nextTxEdge(int64_t now) const {
    const int64_t bit = txBitCycles();
    const int64_t elapsed = (now % bit - txClockPhase_ + bit) % bit;
    return now + bit - elapsed;
}

uint8_t MidiAcia::read8(uint32_t addr) {
    advanceTx(sched_ ? sched_->liveNow() : 0);
    if ((addr & 2) == 0) {
        // $FFFC04 : statut. TDRE indique la case TDR, indépendamment de TIE ;
        // RX plein si le bouclage a livré un octet ; IRQ si une cause RX ou TX
        // est active (cf. acia.c ACIA_UpdateIRQ).
        uint8_t s = tdre_ ? ACIA_TDRE : 0;
        if (rdrf_) s |= ACIA_RDRF;
        if (ovrn_) s |= ACIA_OVRN;
        if (((rdrf_ || rxOverrun_) && (control_ & 0x80)) || (txEnableInt_ && tdre_))
            s |= ACIA_IRQ;
        srRead_ = true;
        return s;
    }
    // acia.c Read_RDR : acquitte l'ancien OVRN avant de publier la perte
    // pendante. Une lecture à vide conserve RDR mais effectue aussi ce protocole.
    rdrf_ = false;
    if (srRead_) { srRead_ = false; ovrn_ = false; }
    if (rxOverrun_) { ovrn_ = true; rxOverrun_ = false; }
    raiseIfReady();
    return rdr_;
}

void MidiAcia::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC04 : contrôle. Bits 5-6 = contrôle émetteur (01 → TIE, cf. ACIA_Write_CR) ;
        // bits 0-1 = 11 → master reset.
        advanceTx(sched_ ? sched_->liveNow() : 0);
        const bool wasBreak = (control_ & 0x60) == 0x60;
        const uint8_t oldDivider = control_ & 3;
        const int64_t oldBitCycles = txBitCycles();
        control_ = v;
        if (sched_ && tsrBusy_ && (v & 3) != 3 && oldDivider != (v & 3)) {
            // ACIA_Write_CR redémarre le timer de bit sans vider TSR. Le
            // dernier stop reste un front : compter ceux encore à émettre,
            // puis les recadencer depuis le nouveau timer (now + période).
            // advanceTx a déjà consommé les fronts arrivés sous l'ancien CR.
            const int64_t now = sched_->liveNow();
            const int64_t remaining = 1 + (txCompleteDue_ - now - 1) / oldBitCycles;
            // Au plus dix fronts après le start (trame 11 bits). Borner
            // aussi les échéances incohérentes d'un snapshot avant multiplication.
            const int64_t edges = remaining > 10 ? 10 : remaining;
            txCompleteDue_ = now + edges * txBitCycles();
        }
        // ACIA_Write_CR redémarre le timer seulement si CR0..1 changent.
        // BREAK/TIE/RTS seuls conservent les fronts ancrés à ce changement.
        if ((v & 3) != 3 && oldDivider != (v & 3))
            txClockPhase_ = sched_ ? sched_->liveNow() % txBitCycles() : 0;
        else txClockPhase_ %= txBitCycles(); // base sûre pendant master reset
        txEnableInt_ = ((v & 0x60) == 0x20);
        // ⚠ Changer les bits d'émission ne remet PAS l'émetteur au repos : seul le
        // master reset le fait. L'ancien « TIE coupé → tdre_ = true » rendait
        // l'émetteur instantanément prêt à chaque écriture du CR, ce qui annulait
        // le temps de transmission pour tout pilote retouchant le CR entre octets.
        // acia.c MasterReset : efface RDRF/OVRN et la perte pendante,
        // abandonne le registre à décalage, mais conserve RDR et SR_Read.
        if ((v & 0x03) == 0x03) {
            rdrf_ = false;
            rxOverrun_ = ovrn_ = false;
            tdre_ = true;
            tsrBusy_ = false;
            txLoadDue_ = txCompleteDue_ = -1;
            if (sched_) sched_->cancel(Scheduler::MIDI_TX);
        }
        // acia.c Clock_TX : break ne bloque que les nouveaux départs ; le
        // TSR engagé finit au stop. Relâcher break reprend au front de bit suivant.
        if (!tsrBusy_) {
            if ((control_ & 0x60) == 0x60) txLoadDue_ = -1;
            else if (wasBreak && !tdre_) {
                if (sched_) {
                    txLoadDue_ = nextTxEdge(sched_->liveNow());
                }
                else { tdre_ = true; emitTx(txData(tdr_), 0); }
            }
        }
        if (sched_ && !tsrBusy_ && !tdre_ && !wasBreak
            && (control_ & 0x60) != 0x60 && oldBitCycles != txBitCycles())
            txLoadDue_ = nextTxEdge(sched_->liveNow());
        armTx();
        raiseIfReady();
        return;
    }
    // acia.c ACIA_Write_TDR : une seule case d'attente, écrasable même pleine.
    // Le temps MMIO est l'horloge CPU vive, pas la fin du dernier quantum.
    if (!sched_) {
        // Banc sans horloge : repli synchrone historique, sans état bloqué.
        if ((control_ & 0x60) == 0x60) { tdr_ = v; tdre_ = false; }
        else emitTx(txData(v), 0);
        raiseIfReady();
        return;
    }
    const int64_t now = sched_->liveNow();
    advanceTx(now);
    tdr_ = v;
    tdre_ = false;
    if (!tsrBusy_ && txLoadDue_ < 0 && (control_ & 0x60) != 0x60)
        // Clock_TX IDLE tourne même sans donnée : écrire TDR ne redémarre
        // pas le diviseur. Le transfert attend son prochain front déjà phasé.
        txLoadDue_ = nextTxEdge(now);
    armTx();
    raiseIfReady();
}

void MidiAcia::emitTx(uint8_t v, int64_t cycle) {
    static const bool traceOut = std::getenv("NEOST_MIDI_TRACE") != nullptr;
    if (traceOut) std::fprintf(stderr, "[midi] %lld %02X\n",
                               static_cast<long long>(cycle), v);
    if (midiSinkTimed_) { midiSinkTimed_(v, cycle); return; }
    if (midiSink_) { midiSink_(v); return; }
    if (loopback_) pushRx(v, sched_ ? cycle : -1);
}

void MidiAcia::armTx() {
    if (!sched_) return;
    const int64_t due = tsrBusy_ ? txCompleteDue_ : txLoadDue_;
    if (due >= 0) sched_->schedule(Scheduler::MIDI_TX, due);
    else sched_->cancel(Scheduler::MIDI_TX);
}

void MidiAcia::advanceTx(int64_t cycle) {
    // Le TSR capture le format au chargement ; un changement de diviseur
    // recadence ses fronts restants dans write8 sans changer ses données.
    // Break inhibe les départs, sans ligne basse ni erreur de trame synthétique.
    for (;;) {
        if (tsrBusy_) {
            if (txCompleteDue_ > cycle) break;
            const int64_t done = txCompleteDue_;
            const uint8_t byte = tsr_;
            tsrBusy_ = false;
            txCompleteDue_ = -1;
            txLoadDue_ = (tdre_ || (control_ & 0x60) == 0x60) ? -1 : nextTxEdge(done);
            emitTx(byte, done);
        } else {
            if (txLoadDue_ < 0 || txLoadDue_ > cycle) break;
            const int64_t load = txLoadDue_;
            tsr_ = txData(tdr_);
            tsrBusy_ = true;
            tdre_ = true;
            txCompleteDue_ = txLoadDue_ + (kTxFrameBits[(control_ >> 2) & 7] - 1) * txBitCycles();
            txLoadDue_ = -1;
            raiseIfReady(load);
        }
    }
    armTx();
}

void MidiAcia::pushRx(uint8_t v, int64_t when) {
    // acia.c RX STOP_BIT : si RDR est encore plein, le nouvel octet est
    // perdu. Le registre à décalage ne stocke pas une seconde trame achevée.
    if (rdrf_) rxOverrun_ = true;
    else { rdr_ = v; rdrf_ = true; }
    raiseIfReady(when);
}

void MidiAcia::receiveExternal(uint8_t b) {
    pushRx(b);                       // octet de l'anneau MIDI → MIDI IN
}

// -----------------------------------------------------------------------------
//  Source hôte cadencée sur l'horloge série (appareil MIDI branché sur la machine)
// -----------------------------------------------------------------------------
void MidiAcia::setRxSource(std::function<bool(uint8_t&)> fn) {
    rxSource_ = std::move(fn);
    if (rxSource_) armRxPace();
    else if (sched_) sched_->cancel(Scheduler::MIDI_RX);
}

void MidiAcia::armRxPace() {
    // L'horloge tourne en PERMANENCE tant qu'un appareil est branché, même sans
    // octet à faire entrer : c'est ce que fait un vrai récepteur série, et ça évite
    // d'avoir à réveiller l'ordonnanceur depuis le thread de l'hôte. Reset et
    // remplacement de source préservent une trame déjà en vol ; seule une
    // connexion sans échéance démarre une période depuis le temps CPU vivant.
    if (rxSource_ && sched_ && sched_->cyclesUntil(Scheduler::MIDI_RX) < 0)
        sched_->schedule(Scheduler::MIDI_RX, sched_->liveNow() + kMidiRxByteCycles);
}

void MidiAcia::onRxPace() {
    if (!rxSource_) return;          // débranché entre l'armement et l'échéance
    uint8_t b = 0;
    // pushRx applique la règle du 6850 : récepteur plein → c'est le NOUVEL octet qui
    // tombe. C'est l'overrun du MATÉRIEL, et il est désormais au bon endroit — l'hôte
    // ne retient plus rien pour l'éviter, il livre au débit du câble.
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    if (rxSource_(b)) pushRx(b, due >= 0 ? due : (sched_ ? sched_->liveNow() : 0));
    // CycInt_AddRelativeInterrupt conserve PendingCyclesOver : le retard du
    // dispatch ne décale pas le débit du câble. Le Scheduler porte déjà la phase.
    if (rxSource_ && sched_) {
        const int64_t due = sched_->firingDue();
        sched_->schedule(Scheduler::MIDI_RX,
                         (due >= 0 ? due : sched_->liveNow()) + kMidiRxByteCycles);
    }
}

void MidiAcia::onTxEmpty() {
    advanceTx(sched_ ? sched_->now() : 0);
}

void MidiAcia::raiseIfReady(int64_t when) {
    // L'ACIA active sa ligne d'IRQ dès qu'une cause RX (octet dispo + RIE) ou TX
    // (TDRE + TIE) est active → canal 6 du MFP via GPIP4 (cf. ACIA_UpdateIRQ).
    const bool active = ((rdrf_ || rxOverrun_) && (control_ & 0x80)) || (txEnableInt_ && tdre_);
    // Le canal 6 est levé par le détecteur de FRONT du setter (wire-OR avec le
    // clavier : si GPIP4 est déjà bas, pas de nouveau front → pas d'IRQ, comme
    // sur le vrai MFP). Pas de raise() manuel.
    if (when >= 0) mfp_.setAciaLineMidiAt(active, when);
    else mfp_.setAciaLineMidi(active);
}
