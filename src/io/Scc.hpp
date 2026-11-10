// =============================================================================
//  Scc.hpp — Contrôleur série SCC Z85C30 du Mega STE (port de Hatari scc.c).
//
//  Références citées ici et dans Scc.cpp (grades : CLAUDE.md, « Les grades de preuve ») :
//   UM0109 = Zilog, SCC/ESCC User Manual, édition UM010902-0609 ; pages IMPRIMÉES
//            (page du PDF = imprimée + 7). extern/datasheets/Zilog_SCC_ESCC_UM.pdf,
//            sha256 2a13683ee0e423ee3cdf2781a4ce6b4df65c1e069a269e4234fa6964b4a3bea0.
//   PS0117 = Zilog, Z80C30/Z85C30 CMOS SCC Product Specification, PS011708-0115.
//            extern/datasheets/Zilog_PS0117.pdf,
//            sha256 feb4b46341bf20cab3ce5e555b02b7c13e1c2422cb12a9ba2dbfa121d0a34faa.
//   Hatari = src/scc.c (sauf mention) @ f0736b24 ; lignes vérifiées à cette révision.
//   MAME   = src/devices/machine/z80scc.cpp @ ce391499d37d (extern/mame, épinglé par
//            tools/setup_silicon_refs.sh) ; lignes vérifiées à cette révision.
//
//  [D] Hatari scc.c:31 @ f0736b24 — le Mega STE porte ce Zilog 85C30 à deux canaux.
//  [D] Hatari ioMem.c:225 @ f0736b24 — plage $FF8C80-$FF8C87.
//  [D] Hatari scc.c:36-39 @ f0736b24 — canal A = port « LAN » (ou « Serial 2 » par câble
//      interne vers le slot VME) ; canal B = « Modem 2 » (RS-232).
//  [D] Hatari scc.c:2278, scu_vme.c:58,156-161 @ f0736b24 — IRQ de NIVEAU 5, gatée par
//      le SCU (VmeIntMask). [D] Hatari scc.c:2444-2453 @ f0736b24 — IACK vectorisé.
//  [D] Hatari scc.c:1635,1639,2465-2468,2479-2480 @ f0736b24 — décodage, octets impairs
//      seulement : $..81 = ctrl A, $..83 = data A, $..85 = ctrl B, $..87 = data B.
//  [F] Inconnu : aucune de ces affectations de carte n'a de source primaire ici.
//      Tranché par : le schéma du Mega STE ou un relevé sur la machine.
//
//  Modèle fonctionnel (périmètre, pas preuve de fidélité) : jeu complet WR0-15 / RR0-15 par canal (pointeur de
//  registre via WR0), commandes WR0/WR9, reset matériel/canal, RR0 (statut TX/RX +
//  lignes), RR2 (vecteur + statut), RR3 (IP), sources d'interruption, IRQ niv5 +
//  IACK vectorisé. TX cadencé (puits série, ou bouclage local interne si WR14 bit4=1,
//  cf. note dans serialWriteByte) ; RX par injection externe mise en file et cadencée.
//  BRG cadencé et interruption Zero Count portés de Hatari. FIFO RX matérielle
//  de trois octets distincte de la file hôte ([B] UM0109 p.97 : trois octets sur
//  NMOS/CMOS). Restent : série hôte réelle, DPLL, modes synchrones et SDLC (chasse,
//  CRC, drapeaux, FIFO de statut), détection de parité et framing au récepteur.
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
    // [B] UM0109 p.34 : le reset matériel de la puce est /WR et /RD bas ensemble.
    // [D] Hatari scc.c:638-639 @ f0736b24 — non câblé sur Mega STE/TT/Falcon ; le reset
    //     machine qui équivaut à un reset matériel est une convention d'émulation.
    // [F] Inconnu : état réel du SCC après un reset machine. Tranché par : lecture des
    //     registres sur un Mega STE après RESET, avant que le TOS force WR9=$C0.
    void reset();

    // Horloge bus du Mega STE : callbacks SCC_BRG_A/B branchés par Machine.
    void setScheduler(Scheduler* scheduler) { sched_ = scheduler; }
    void onBrgEvent(int channel);
    void onSerialEvent(int channel, bool tx);

    // Accès MMIO $FF8C80-$FF8C87 (le 68000 y fait des mots ; seuls les octets
    // IMPAIRS portent un registre, les pairs lisent $FF / ignorent l'écriture).
    // [D] Hatari scc.c:2465-2468,2479-2480 @ f0736b24 — idem. Tranché par : lecture
    //     d'un octet pair sur un Mega STE ; le manuel Zilog ignore ce câblage de bus.
    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // IRQ niveau 5 : actif quand la ligne /INT est tirée bas (IRQ_Line == 0).
    // [B] UM0109 p.40 : le SCC demande en tirant /INT bas depuis son état drain ouvert.
    // Consulté par Cpu68k::neostUpdateIpl (via le SCU) sur Mega STE.
    bool irqActive() const { return irqLine_ == 0; }
    // Cycle IACK niveau 5 : pose l'IUS, renvoie le vecteur (ou -1 si NV/No Vector).
    // [B] UM0109 p.44 (IUS, puis vecteur) ; WR9 D1 p.161 (NV : bus en haute impédance).
    int  processIack();

    // Puits série en sortie (TX) — optionnel ; ch = 0 (A) / 1 (B). Comme le MFP.
    void setSerialSink(std::function<void(int ch, uint8_t b)> fn) { sink_ = std::move(fn); }
    // Injection d'un octet hôte de données (RX), sans bit de parité physique.
    // Seuls les bouclages du TSR possèdent les métadonnées de parité émises.
    void receiveByte(int ch, uint8_t b);
    // [F] Connecteurs de bouclage EXTERNES du kit Field Service (test « I SCC » : prises
    // Port A / Port B / LAN) : TxD→RxD, RTS→CTS, DTR→DCD et DTR→DSR (/SYNC) sur
    // CHAQUE canal. Câblage déduit de ce que le diagnostic Mega STE attend pour rendre
    // Pass, sans adresse de ROM citée. Tranché par : le schéma des prises du kit, ou un
    // désassemblage daté de la ROM du diagnostic (grade C).
    // Débranché par défaut, comme le MFP (--loopback en headless).
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
            // [B] UM0109 p.72-73 : un chargement de plus, d'où l'intervalle ZC de TC+2.
            // La borne « au moins deux cycles, au plus une seconde » est une garde de NeoST.
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
        bool     txActive   = false; // [B] UM0109 WR5 D3 p.153 : trame partie, à terminer même si TX Enable tombe.
        bool     txInterrupted = false; // Un BREAK a coupé ce caractère : aucun octet intact à livrer.
        int64_t  txRemaining = 0; // Cycles TSR suspendus par BREAK (approximation au caractère).
        bool     rtsAsserted = false; // [B] UM0109 WR5 D1 p.154 : état réel /RTS, distinct du bit WR5.
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
    uint8_t ius_      = 0;     // [B] UM0109 p.40 : IUS invisibles du CPU. Ici, mêmes bits que RR3 0-5.
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
