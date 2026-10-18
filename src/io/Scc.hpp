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
//  IACK vectorisé. TX immédiat (puits série, ou bouclage local interne si WR14 bit4=1,
//  cf. note dans serialWriteByte) ; RX par injection externe.
//  BRG cadencé et interruption Zero Count portés de Hatari. Restent : débit
//  temporisé des octets TX/RX et série hôte réelle.
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
    // Injection d'un octet reçu sur le canal `ch` (RX) — bouclage / source externe.
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
        for (auto& ch : chn_) ar.fixBools(ch.rr0Latched, ch.txWritten, ch.tsrFull);
        ar(irqLine_); ar(ius_); ar(activeReg_);
        for (int64_t& period : brgPeriod_) {
            ar(period);
            // SCC_Compute_BaudRate écrête le débit à 1 Hz : au plus une seconde.
            ar.check((period == 0 || period >= 4) && period <= neost::pacing::kCpuHzInt,
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
        uint8_t  rr0NoLatch = 0;
        bool     txWritten  = false;
        uint8_t  tsr        = 0;
        bool     tsrFull    = false;
        uint32_t intSources = 0;
        uint8_t  txBits = 8, rxBits = 8, parityBits = 0;
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
    void    updateRR0Clear(int chn, int bits) { chn_[chn].rr0NoLatch &= ~bits; }
    void    updateRR0Set(int chn, int bits)   { chn_[chn].rr0NoLatch |= bits; }
    void    updateRR0LatchOff(int chn) { chn_[chn].rr0Latched = false; updateRR0(chn); }
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

    uint16_t getCTS(int chn) const;
    uint16_t getDCD(int chn) const;
    void    serialWriteByte(int chn, uint8_t v);
};
