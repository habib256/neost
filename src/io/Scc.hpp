// =============================================================================
//  Scc.hpp — Contrôleur série SCC Z85C30 du Mega STE (port de Hatari scc.c).
//
//  Le Mega STE (comme le TT) ajoute un Zilog 85C30 SCC à deux canaux série au
//  $FF8C80-$FF8C87. Sur Mega STE : canal A = port « LAN », canal B = « Serial 2 »
//  (Modem 2, RS-232). L'IRQ du SCC est de NIVEAU 5, VECTORISÉE, gatée par le SCU
//  (VmeIntMask). Décodage des accès (octets impairs uniquement, port des décodes
//  d'Hatari) :  $..81 = ctrl A, $..83 = data A, $..85 = ctrl B, $..87 = data B.
//
//  Modèle fonctionnel fidèle : jeu complet WR0-15 / RR0-15 par canal (pointeur de
//  registre via WR0), commandes WR0/WR9, reset matériel/canal, RR0 (statut TX/RX +
//  lignes), RR2 (vecteur + statut), RR3 (IP), sources d'interruption, IRQ niv5 +
//  IACK vectorisé. TX cadencé (puits série, ou bouclage local interne si WR14 bit4=1,
//  cf. note dans serialWriteByte) ; RX par injection externe mise en file et cadencée.
//  BRG cadencé et interruption Zero Count portés de Hatari. FIFO RX matérielle
//  de trois octets distincte de la file hôte. Restent : série hôte réelle et DPLL.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>

#include "core/StateArchive.hpp"
#include "core/Scheduler.hpp"
#include "core/Pacing.hpp"

class Scc {
public:
    Scc() { reset(); }

    // Reset matériel complet (SCC_Reset). Appelé au reset machine.
    void reset();

    // Horloge bus du Mega STE : callbacks SCC_BRG_A/B branchés par Machine.
    void setScheduler(Scheduler* scheduler) { sched_ = scheduler; }
    void onBrgEvent(int channel);
    void onSerialEvent(int channel, bool tx);

    // Accès MMIO $FF8C80-$FF8C87 (le 68000 y fait des mots ; seuls les octets
    // IMPAIRS portent un registre, les pairs lisent $FF / ignorent l'écriture).
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // IRQ niveau 5 : actif quand la ligne /INT est tirée bas (IRQ_Line == 0).
    // Consulté par Cpu68k::neostUpdateIpl (via le SCU) sur Mega STE.
    bool irqActive() const { return irqLine_ == 0; }
    // Cycle IACK niveau 5 : pose l'IUS, renvoie le vecteur (ou -1 si NV/No Vector).
    int  processIack();

    // Puits série en sortie (TX) — optionnel ; ch = 0 (A) / 1 (B). Comme le MFP.
    void setSerialSink(std::function<void(int ch, uint8_t b)> fn) { sink_ = std::move(fn); }
    // Injection d'un octet hôte de données (RX), sans bit de parité physique.
    // Seuls les bouclages du TSR possèdent les métadonnées de parité émises.
    void receiveByte(int ch, uint8_t b);
    // Connecteurs de bouclage EXTERNES du kit Field Service (test « I SCC » : prises
    // Port A / Port B / LAN) : TxD→RxD, RTS→CTS, DTR→DCD et DTR→DSR (/SYNC) sur
    // CHAQUE canal. Débranché par défaut, comme le MFP (--loopback en headless).
    // Propriété de banc (pas de l'état machine) : non sérialisée, comme Mfp::loopback_.
    void setLoopback(bool plugged);

    // Sérialisation save-state (SYMÉTRIQUE). `Chn` est POD (trivially-copyable) →
    // copie brute. On saute `sink_` (callback re-lié à la construction) et `trace_`
    // (drapeau de débogage, pas de l'état machine).
    void serialize(StateArchive& ar) {
        ar(chn_[0]); ar(chn_[1]);
        // `Chn` est copié EN BLOC : ses booléens échappent à la normalisation de
        // `operator()`, qui ne voit que le type de la struct. Relus dans les DEUX
        // polarités (`if (!ch.rr0Latched)`, `if (!ch.tsrFull)` puis `if (ch.tsrFull)`),
        // un octet valant 63 ferait prendre les deux branches — l'UB même que la
        // normalisation existe pour fermer.
        for (auto& ch : chn_) {
            ar.fixBools(ch.rr0Latched, ch.breakRaw, ch.txWritten, ch.tsrFull, ch.txActive, ch.txInterrupted, ch.rtsAsserted, ch.rxFirstArmed, ch.rxFirstPending, ch.rxLocked, ch.rxEchoStarted, ch.rxEchoEligible, ch.rxReceiveEligible);
            ar.check(ch.txRemaining >= 0 && ch.txRemaining <= 64 * neost::pacing::kCpuHzInt,
                     "Scc::BREAK TX remainder out of range");
            ar.check(!ch.txInterrupted || ch.txActive, "Scc::interrupted TX without active frame");
            ar.check(!ch.txRemaining || (ch.txInterrupted && ch.txActive), "Scc::BREAK remainder without active frame");
            ar.check(ch.breakPending <= 3, "Scc::invalid BREAK edge mask");
            ar.check(!ch.txActive || ch.tsrFull, "Scc::active TX without TSR");
            ar.check(ch.brgSource == 0 || ch.brgSource == 3672000 || ch.brgSource == neost::pacing::kCpuHzInt,
                     "Scc::invalid BRG source");
            ar.check(ch.brgSource ? ch.brgFraction < ch.brgSource : ch.brgFraction == 0,
                     "Scc::invalid BRG fraction");
            ar.check((ch.brgPulseStart == -1 && ch.brgPulseEnd == -1)
                     || (ch.brgPulseStart >= 0 && ch.brgPulseEnd > ch.brgPulseStart
                         && ch.brgPulseEnd - ch.brgPulseStart <= 3), "Scc::invalid BRG pulse");
            for (int i = 0; i < 2; ++i) {
                ar.check(ch.serialDivisor[i] == 0 || ch.serialDivisor[i] == 2*3672000
                         || ch.serialDivisor[i] == 2*2457600
                         || ch.serialDivisor[i] == 2*neost::pacing::kCpuHzInt, "Scc::invalid serial clock divisor");
                ar.check(ch.serialDivisor[i] ? ch.serialFraction[i] < ch.serialDivisor[i]
                         : ch.serialFraction[i] == 0, "Scc::invalid serial clock fraction");
            }
            ar.check(!ch.rxEchoEligible || ch.rxEchoStarted, "Scc::echo eligible without RX frame");
            ar.check(ch.rxFifoHead < 3 && ch.rxFifoCount <= 3);
            ar.check(ch.rxHead < 256 && ch.rxCount <= 256);
            ar.check(ch.txBits >= 5 && ch.txBits <= 8 && ch.rxBits >= 5 && ch.rxBits <= 8
                     && ch.tsrParity <= 2 && ch.tsrBits >= 1 && ch.tsrBits <= 8 && ch.parityBits <= 1, "Scc::invalid character format");
        }
        ar(irqLine_); ar(ius_); ar(activeReg_);
        for (int64_t& period : brgPeriod_) {
            ar(period);
            // Intervalle ZC (TC+2) : au moins deux cycles, au plus une seconde.
            ar.check((period == 0 || period >= 2) && period <= neost::pacing::kCpuHzInt,
                     "Scc::BRG period out of range");
        }
        // writeControl fait « ch.WR[activeReg_] = value » : un état forgé passant le CRC
        // écrirait hors du tableau WR[16] (même convention de garde qu'Acsi::serialize).
        ar.check(activeReg_ >= 0 && activeReg_ <= 15);
    }

private:
    struct Chn {
        uint8_t  WR[16] = {0};
        uint8_t  WR7p   = 0;
        uint8_t  RR[16] = {0};
        bool     rr0Latched = false;
        bool     breakRaw = false;
        uint8_t  breakPending = 0; // D0=front montant, D1=descendant ; répétitions coalescées.
        uint8_t  rr0NoLatch = 0;
        bool     txWritten  = false;
        uint8_t  tsr        = 0;
        uint8_t  tsrParity  = 2; // 0/1 = bit émis connu ; 2 = parité absente.
        uint8_t  tsrBits    = 8; // Format du caractère chargé, distinct du WR5 courant.
        bool     tsrFull    = false;
        bool     txActive   = false; // Trame partie, à terminer même si WR5 TX Enable tombe.
        bool     txInterrupted = false; // Un BREAK a coupé ce caractère : aucun octet intact à livrer.
        int64_t  txRemaining = 0; // Cycles TSR suspendus par BREAK (approximation au caractère).
        bool     rtsAsserted = false; // État réel /RTS, distinct du bit WR5.
        uint32_t intSources = 0;
        uint32_t brgFraction = 0, brgSource = 0;
        int64_t brgPulseStart = -1, brgPulseEnd = -1;
        uint32_t serialFraction[2] = {}; // Restes RX/TX des horloges directes.
        uint32_t serialDivisor[2] = {}; // Dénominateurs associés, zéro = nouveau flux.
        uint8_t  txBits = 8, rxBits = 8, parityBits = 0;
        uint8_t rxFifo[3] = {}, rxErrors[3] = {};
        uint8_t rxFifoHead = 0, rxFifoCount = 0;
        bool rxEchoStarted = false, rxEchoEligible = false, rxReceiveEligible = false;
        bool rxFirstArmed = true, rxFirstPending = false, rxLocked = false;
        uint8_t rxQueue[256] = {};
        uint16_t rxHead = 0, rxCount = 0;
    };
    Chn     chn_[2];
    uint8_t irqLine_  = 1;     // 1 = OFF (haut), 0 = ON (bas)
    uint8_t ius_      = 0;     // Interrupt Under Service (mêmes bits que RR3 0-5)
    int     activeReg_ = 0;
    int64_t brgPeriod_[2] = {0, 0};
    Scheduler* sched_ = nullptr;  // câblage hôte, non sérialisé
    std::function<void(int, uint8_t)> sink_;
    bool    trace_ = false;
    bool    extLoopback_ = false;   // prises de bouclage Field Service branchées (cf. setLoopback)

    // --- Cœur (port des SCC_*) -------------------------------------------------
    uint8_t handleRead(uint32_t addr);
    void    handleWrite(uint32_t addr, uint8_t v);
    uint8_t readControl(int chn);
    void    writeControl(int chn, uint8_t v);
    uint8_t readDataReg(int chn);
    void    writeDataReg(int chn, uint8_t v);

    void    resetChannel(int chn, bool hwReset);
    void    resetFull(bool hwReset);

    uint8_t getVectorStatus();
    void    updateRR0(int chn);
    bool    breakStatusAllowed(int chn) const;
    void    updateRR0Clear(int chn, int bits) { chn_[chn].rr0NoLatch &= ~bits; }
    void    updateRR0Set(int chn, int bits)   { chn_[chn].rr0NoLatch |= bits; }
    void    updateRR0LatchOff(int chn);
    void    updateRR2();
    void    updateRR3Bit(bool set, uint8_t bit);
    void    updateRR3(int chn);

    void    setLineIRQ(int bit) { irqLine_ = uint8_t(bit); }
    void    updateIRQ();
    void    intSourcesChange(int chn, uint32_t src, bool set);
    void    intSourcesSet(int chn, uint32_t src)   { intSourcesChange(chn, src, true); }
    void    intSourcesClear(int chn, uint32_t src) { intSourcesChange(chn, src, false); }

    int     doIACK(bool soft);

    void    copyTdrTsr(int chn, uint8_t tdr);
    void    processTX(int chn);
    void    updateBrg(int channel);
    int64_t nextBrgPeriod(int channel);
    int64_t serialPeriod(int channel, bool tx, bool advance = false);
    void updateSerial(int channel);
    bool receiveAllowed(int channel) const;
    bool localLoopbackOnly(int channel) const;
    bool echoByteAllowed(int channel) const;
    void startRxFrame(int channel);
    bool transmitAllowed(int channel) const;
    void updateRts(int channel);
    void deliverByte(int channel, uint8_t byte, int parity = -1);
    void refreshRx(int channel);
    void popRx(int channel);

    uint16_t getCTS(int chn) const;
    uint16_t getDCD(int chn) const;
    void    serialWriteByte(int chn, uint8_t v);
};
