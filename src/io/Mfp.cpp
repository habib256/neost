// =============================================================================
//  Mfp.cpp — Logique d'interruption du MC68901.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Mfp.hpp"
#include "core/Pacing.hpp"   // kCpuHzInt — UNE horloge CPU pour tout l'arbre
#include <cstdio>
#include <cstdlib>
#include <string>

// NEOST_MFP_EXACT : broche IRQ MFP au cycle exact (port fidèle du couple
// MFP_TimerB_EventCount + MFP_ProcessIRQ de Hatari). bit0 = ANTI-DATATION du tic
// Timer B event-count (l'IRQ est datée à l'échéance TIMER_B servie — ≙
// Delayed_Cycles=PendingCycles — et non à la frontière de bloc, en retard de
// 0..24 cyc) ; bit1 = PRISE À LA FRONTIÈRE courante quand le délai de 4 cyc est
// déjà écoulé (l'événement MFP_IRQ est armé à l'horloge live → commit de l'IPL à
// cette frontière, ≙ MFP_ProcessIRQ « clock − IRQ_Time ≥ 4 → exception » ; l'ancien
// modèle n'armait RIEN dans ce cas → broche seule, reconnue UNE instruction plus
// tard). Défaut 3 (les deux) ; =0 restaure l'ancien comportement.
namespace { const int g_mfpExact = []{ const char* s = std::getenv("NEOST_MFP_EXACT");
                                       return s ? std::atoi(s) : 3; }(); }

// NEOST_MFP_UPDTIMERS : dispatch des timers MFP échus AVANT tout accès registre du MFP
// — port de `MFP_UpdateTimers` (mfp.c:681). Défaut ON ; =0 restaure le comportement
// « frontière de bloc » pour que l'A/B reste exécutable. Récit et cas Super Hang-On
// dans `Scheduler::runMfpTimersTo`.
namespace { const bool g_mfpUpdTimers = []{ const char* s = std::getenv("NEOST_MFP_UPDTIMERS");
                                            return s ? std::atoi(s) != 0 : true; }(); }

// Instant d'un ÉVÉNEMENT DÉCLENCHÉ PAR UNE ÉCRITURE registre du MFP : le cycle où
// l'écriture est TERMINÉE, pas celui où elle commence.
//
// Port de `Cycles_GetInternalCycleOnWriteAccess` (cycles.c) : en mode cycle-exact
// Hatari date une écriture MMIO à `currcycle + 4` — « the number of cycles when the
// write will be completed », un accès mémoire du 68000 durant 4 cycles. Côté NeoST,
// `liveNow()` vaut déjà « cycles avant l'accès + 2 » (Moira a facturé le SYNC(2) de
// tête), donc la fin d'accès est à +2 — la convention est écrite dans
// `Cpu68k::cyclesIntoInstr`, et le Shifter l'applique déjà à ses écritures palette.
// Le MFP, lui, ne l'appliquait PAS : ses IRQ nées d'une écriture registre étaient
// antidatées de 2 cycles, et comme la visibilité CPU court depuis cet instant
// (irqTime_ + 4), l'exception partait 2 cycles trop tôt.
//
// Hatari nomme le titre que ça corrige (mfp.c:113, entrée du 2013/03/14) : « When
// writing to the MFP's registers, take the write cycles into account when updating
// MFP_IRQ_Time (properly fix Super Hang On) ».
int64_t Mfp::mmioWriteEnd() {
    static const int64_t v = []{ const char* s = std::getenv("NEOST_MFP_WRITE_END");
                                 return s ? int64_t(std::atoi(s)) : int64_t(2); }();
    return v;
}

int64_t Mfp::writeEventTime() const {
    return sched_ ? sched_->liveNow() + mmioWriteEnd() : 0;
}

// NEOST_MFP_BATCH : élection de l'IRQ GROUPÉE après toutes les entrées d'une même
// instruction (défaut ON, ≙ MFP_UpdateNeeded). =0 restaure l'élection immédiate à
// chaque entrée — l'ancien comportement, où la règle chronologique était inopérante.
namespace { const bool g_mfpBatch = []{ const char* s = std::getenv("NEOST_MFP_BATCH");
                                        return s ? std::atoi(s) != 0 : true; }(); }

void Mfp::updateTimers() {
    if (!(g_mfpUpdTimers && sched_)) return;
    sched_->runMfpTimersTo(sched_->liveNow());
    // ≙ mfp.c:689-690 : après avoir servi les timers, si des entrées ont eu lieu, on
    // élit MAINTENANT — et avec l'horloge courante comme date de front, comme Hatari
    // (`MFP_UpdateIRQ(pMFP, Clock)`) et non avec pendingTime_.
    if (irqUpdateNeeded_) { irqUpdateNeeded_ = false; updateIrq(sched_->liveNow()); }
}

// RESET matériel du MC68901 (port de MFP_Reset, mfp.c:519-569). Le vrai MFP n'a PAS
// de signal de reset dédié pour le GPIP/USART, mais sur l'ST la broche /RESET du 68000
// le réinitialise (cf. reset.c:74, AVANT M68000_Reset). NeoST l'omettait → des IRQ
// Timer A / GPIP7 pouvaient survivre à un reset à chaud (musique chip qui s'emballe ou
// notes parasites après Ctrl+reset). On remet ici tout l'état d'interruption et les
// timers à zéro. PRÉSERVÉ : colorMonitor_/hasDmaSound_/loopback_ (propriétés posées
// AVANT le reset) et les lignes d'ENTRÉE des autres puces (FDC/ACIA/RS232), reforcées
// à la lecture du GPIP et resynchronisées par leurs puces respectives.
void Mfp::resetChip() {
    // GPIP au reset : **0x00, comme Hatari** (mfp.c:523 `pMFP->GPIP = 0`) — les
    // lignes démarrent BASSES et ce sont les périphériques qui les assertent
    // (moniteur bit7, FDC bit5, ACIA bit4 : forcés en lecture par readGpip).
    // L'ancien 0xFF (« entrées non assertées = 1 ») rendait bits 6 (RI) et 3 à 1 :
    // l'inventaire matériel de CLOSURE (Sync) lit $FA01 dans sa table d'identité
    // ($2E22F) et la machine générée divergeait de l'oracle dès cet octet
    // (NeoST $F9 vs Hatari $B1, cf. docs/CLOSURE_CHANTIER.md).
    gpip = 0x00; aer = 0; ddr = 0;
    iera = ierb = 0;                      // enable
    ipra = iprb = 0;                      // pending
    imra = imrb = 0;                      // mask
    isra = isrb = 0;                      // in-service
    vr   = 0;                             // registre vecteur (mode auto, base 0)
    // Timers : mode + recharge + compteurs vivants + backing store des registres de
    // contrôle/données — ET EUX SEULS (MFP_Reset ne touche PAS SCR/UCR/RSR/TSR/UDR).
    tbcr_ = tbReload_ = tbCounter_ = 0;
    taReload_ = taCounter_ = 0;
    tcCounter_ = tdCounter_ = 0;
    for (int64_t& t : timerDueSub_) t = 0;
    tai_ = false;                         // TAI/TBI = 0 (entrées event-count)
    timer_[0x19] = timer_[0x1B] = timer_[0x1D] = 0;                 // TACR / TBCR / TCDCR
    timer_[0x1F] = timer_[0x21] = timer_[0x23] = timer_[0x25] = 0;  // TADR / TBDR / TCDR / TDDR
    // Signal IRQ daté : tout retombe (Current_Interrupt/IRQ/IRQ_Time/Pending_Time).
    irq_ = false; irqTime_ = 0; currentInt_ = -1;
    for (int64_t& t : pendingTime_) t = kNever;
    pendingTimeMin_ = kNever;
    // ⚠ AUCUNE annulation d'échéance ici : customreset() n'appelle PAS CycInt_Reset
    // (hatari-glue.c:54). Une échéance de timer déjà datée SURVIT au /RESET et se
    // périme d'elle-même quand elle arrive (onTimerExpire voit ctrl=0 → ni IRQ ni
    // replanification, ≙ MFP_InterruptHandler_TimerA + MFP_StartTimer_AB(ctrl=0)).
    // Les annuler décale l'étalon nocooper d'une trame entière (mesuré).
}

void Mfp::reset() {
    resetChip();                          // registres + timers (MFP_Reset strict)
    for (uint8_t& b : timer_) b = 0;      // + USART (SCR/UCR/RSR/TSR/UDR) au reset MACHINE
    xsint_ = false;                       // ligne XSINT son DMA (re-synchronisée ensuite par DmaSound::reset)
    // CTS/DCD REVIENNENT à leur valeur de repos ACTIVE : ce sont des entrées, pas de
    // l'état machine. Chez Hatari elles sont recalculées à CHAQUE lecture GPIP
    // (RS232_Get_DCD_CTS) et ne peuvent donc pas rester coincées ; ici, sans cette
    // remise, une désassertion sous --loopback survivait à tous les resets.
    ctsLine_ = dcdLine_ = true;
    // Même logique pour Centronics BUSY (GPIP0) : entrée recalculée seulement quand
    // le sink PSG tire — psg.reset() (R15=0) ne le fait pas, un BUSY asserté par le
    // bouclage ou la capture imprimante restait donc coincé après reset.
    busyLine_ = false;
    // RI (GPIP6) : MÊME raison que CTS/DCD et BUSY ci-dessus — une entrée, jamais
    // recalculée à la lecture (repos HAUT, cf. gpipInput). Deux sources la tirent et
    // aucune ne la relâchait après un reset machine :
    //  · bouclage RS-232 (DTR→RI), comme la désassertion CTS/DCD décrite plus haut ;
    //  · bouton Ultimate Ripper, RELÂCHÉ à la VBL par PortDevices::onVbl — or
    //    Machine::reset appelle ports.reset(), qui remet pressed_ à faux : la VBL
    //    sortait aussitôt et la ligne restait assertée POUR TOUJOURS.
    riLine_ = false;             // repos DÉSASSERTÉ (bit6 = 1, MC1489 entrée ouverte)
    monButton_ = false;                   // bouton Multiface relâché (PortDevices::reset l'oublie aussi)
    rxByte_ = 0; rxFull_ = false; rxOverrun_ = rxPendingOverrun_ = false;   // USART : tampon vidé (pas de RXFULL fantôme)
    serialClockFactor_ = 128;
    serialBaud_ = 0; serialUcr_ = 0;      // suivi débit série remis (sinon serialBaud() rapporte l'avant-reset)
    if (sched_) {   // reset MACHINE : Hatari appelle CycInt_Reset() (reset.c:76) avant MFP_Reset_All
        sched_->cancel(Scheduler::TIMER_A);
        sched_->cancel(Scheduler::TIMER_B);
        sched_->cancel(Scheduler::TIMER_B_DELAY);
        sched_->cancel(Scheduler::TIMER_C);
        sched_->cancel(Scheduler::TIMER_D);
        sched_->cancel(Scheduler::MFP_IRQ);
        sched_->cancel(Scheduler::SERIAL_RX);
        sched_->cancel(Scheduler::SERIAL_TX);
    }
    serialTxBusy_ = serialTxFull_ = serialTxUnderrun_ = serialTxEnd_ = false;
    serialTxByte_ = serialTxHolding_ = serialTxKind_ = 0;
    serialTxCycle_ = serialTxRemainder_ = 0; serialTxDivisor_ = 1;
    serialTxUnderrunClearCycle_ = serialTxFrameClearCycle_ = 0;
    hostRx_.clear();
    serialRxArmed_ = false; serialRxMask_ = 0xFF;
    serialRxCycle_ = 0; serialRxRemainder_ = 0; serialRxDivisor_ = 1;
}

// -----------------------------------------------------------------------------
//  Injection RX série côté hôte (modem Hayes). Les octets sont
//  mis en file puis livrés UN PAR UN au débit et au format série configurés,
//  avec IRQ RxFull à chaque livraison — un pilote d'époque qui compte sur le
//  rythme du fil (STiK/STinG en SLIP) ne perd ainsi aucun octet. La file est
//  bornée : l'appelant re-pompe quand hostRxPending() redescend.
// -----------------------------------------------------------------------------
void Mfp::receiveByte(uint8_t b) {
    if (hostRx_.size() >= kHostRxMax) return;    // pleine : l'octet attend côté hôte
    hostRx_.push_back(b);
    scheduleSerialRx();
}

void Mfp::scheduleSerialRx(bool continuation) {
    if (!sched_ || serialRxArmed_ || hostRx_.empty()) return;
    // Hatari rs232.c: RS232_HandleUCR ; MAME mc68901.cpp REGISTER_UCR :
    // un start, 5..8 données, parité et stops 1/1,5/2.
    // Mode synchrone non modélisé : conserver le transport hôte 8N1 historique.
    const uint8_t ucr = timer_[0x29];
    const int stops = (ucr >> 3) & 3;
    // MAME rx_frame_start/rx_clock : compteur de longueur au start, données
    // courtes alignées à droite et complétées par des zéros. Le transport par
    // octets fige le format au caractère ; pas de reconstruction des fronts.
    serialRxMask_ = stops ? uint8_t(0xFF >> ((ucr >> 5) & 3)) : 0xFF;
    const int halfBits = stops ? 2 * (1 + 8 - ((ucr >> 5) & 3) + ((ucr & 4) ? 1 : 0))
                                   + (stops == 1 ? 2 : stops == 2 ? ((ucr & 0x80) ? 3 : 2) : 4) : 20;
    const int divisor = 2457600;
    if (!continuation) {
        serialRxCycle_ = sched_->liveNow();
        serialRxRemainder_ = 0;
    }
    // Une reconfiguration prend effet au caractère suivant, sans déplacer celui
    // déjà en vol. La base commune 2 457 600 conserve aussi la fraction lors
    // des changements de débit, sans timestamp Q32 borné.
    if (serialRxDivisor_ != divisor) serialRxRemainder_ = 0;
    serialRxDivisor_ = divisor;
    const int64_t numerator = neost::pacing::kCpuHzInt * halfBits * serialClockFactor_;
    const int64_t total = numerator + serialRxRemainder_;
    serialRxCycle_ += total / divisor;
    serialRxRemainder_ = total % divisor;
    sched_->schedule(Scheduler::SERIAL_RX, serialRxCycle_ + (serialRxRemainder_ != 0));
    serialRxArmed_ = true;
}

void Mfp::onSerialRxEvent() {
    serialRxArmed_ = false;
    if (hostRx_.empty()) return;
    const uint8_t b = hostRx_.front() & serialRxMask_;
    hostRx_.pop_front();
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    // MC68HC901 §2.10.1/.3 et MAME rc_w : SI/RC externes sont déconnectés
    // par H/L=11 ; les octets du fil hôte continuent à passer sans remplir RDR.
    if (!internalSerialLoopback())
        receiveSerialByte(b, due >= 0 ? due : (sched_ ? sched_->liveNow() : 0));
    scheduleSerialRx(true);
}

// MC68HC901 §7.2.2 OE et MAME rx_async_frame_complete / REGISTER_UDR :
// la perte reste invisible tant que RDR n'est pas lu. OE visible bloque ensuite
// l'assemblage jusqu'à la lecture RSR ; aucun framing physique n'est synthétisé.
void Mfp::receiveSerialByte(uint8_t byte, int64_t when) {
    if (!(timer_[0x2B] & 1) || rxOverrun_) return;
    if (rxFull_) { rxPendingOverrun_ = true; return; }
    rxByte_ = byte;
    rxFull_ = true;
    raiseAt(SRC_RXFULL, when);
}

// Transport TX asynchrone à deux registres : TDR écrasable, TSR en vol.
// Le chargement initial est immédiat (phase du front TC non modélisée), puis les
// fins de caractères gardent une phase rationnelle indépendante du dispatch CPU.
// Le mode synchrone utilise le repli 8N1 /16 ; BREAK cadence TXERR et bloque
// le rechargement, sans produire de forme d'onde ni d'erreur RX synthétique.
void Mfp::startSerialTx(bool continuation) {
    if (!sched_ || serialTxBusy_ || !(timer_[0x2D] & 1)) return;
    const uint8_t ucr = timer_[0x29];
    const int stops = (ucr >> 3) & 3;
    const int bits = 8 - ((ucr >> 5) & 3);
    const int halfBits = stops ? 2*(1+bits+((ucr & 4) ? 1 : 0)) +
        (stops == 1 ? 2 : stops == 2 ? ((ucr & 0x80) ? 3 : 2) : 4) : 20;
    if ((timer_[0x2D] & 8) && stops) {
        serialTxKind_ = 1; // intervalle BREAK, aucun transfert du TDR
        scheduleSerialTxHalfBits(halfBits, continuation);
        return;
    }
    if (!serialTxFull_) return;
    serialTxKind_ = 0;
    serialTxByte_ = serialTxHolding_;
    if (stops) serialTxByte_ &= uint8_t((1u << bits) - 1);
    const int64_t when = continuation ? serialTxCycle_ + (serialTxRemainder_ != 0) : writeEventTime();
    serialTxFull_ = false;
    scheduleSerialTxHalfBits(halfBits, continuation);
    raiseAt(SRC_TXEMPTY, when);
}

void Mfp::scheduleSerialTxHalfBits(int halfBits, bool continuation) {
    const int divisor = 2457600;
    if (!continuation) { serialTxCycle_ = sched_->liveNow(); serialTxRemainder_ = 0; }
    if (serialTxDivisor_ != divisor) serialTxRemainder_ = 0;
    serialTxDivisor_ = divisor;
    const int64_t total = neost::pacing::kCpuHzInt * halfBits * serialClockFactor_ + serialTxRemainder_;
    serialTxCycle_ += total / divisor;
    serialTxRemainder_ = total % divisor;
    const int64_t bit = neost::pacing::kCpuHzInt * 2 * serialClockFactor_;
    serialTxFrameClearCycle_ = serialTxCycle_ + (serialTxRemainder_ + bit + divisor - 1) / divisor;
    serialTxBusy_ = true;
    sched_->schedule(Scheduler::SERIAL_TX, serialTxCycle_ + (serialTxRemainder_ != 0));
}

// MC68901UM §7.3.2 : END immédiat si aucun caractère ne reste en vol.
// MAME tx_clock : END déclenche TXERR, AT active RE puis se désarme.
void Mfp::finishSerialTx(int64_t when) {
    if (!serialTxEnd_) {
        serialTxEnd_ = true;
        raiseAt(SRC_TXERR, when);
    }
    if (timer_[0x2D] & 0x20) {
        timer_[0x2B] |= 1;
        timer_[0x2D] &= ~0x20;
    }
}

void Mfp::onSerialTxEvent() {
    if (!serialTxBusy_) return;
    advanceSerialTx(sched_->firingDue());
}

void Mfp::advanceSerialTx(int64_t when) {
    serialTxBusy_ = false;
    if (serialTxKind_ != 0) {
        const uint8_t kind = serialTxKind_;
        serialTxKind_ = 0;
        if (!(timer_[0x2D] & 1)) { finishSerialTx(when); return; }
        const bool sendBreak = (timer_[0x2D] & 8) && (timer_[0x29] & 0x18);
        if (kind == 1 && sendBreak) {
            // MC68HC901 p7-8 (famille68901), MAME mc68901 : TSR inchangé ;
            // UE et IPR doivent être libres
            // pour observer chaque frontière normale pendant le BREAK.
            if (!serialTxUnderrun_ && !(ipra & (1u << (SRC_TXERR & 7)))) raiseAt(SRC_TXERR, when);
            startSerialTx(true);
        } else if (serialTxFull_ || sendBreak) startSerialTx(true);
        else if (!serialTxUnderrun_) {
            serialTxUnderrunClearCycle_ = serialTxFrameClearCycle_;
            serialTxUnderrun_ = true; raiseAt(SRC_TXERR, when);
        }
        return;
    }
    if (serialSink_) serialSink_(serialTxByte_);
    // MAME tc_w sélectionne le mode H/L courant à chaque front, sans latch au
    // départ. Ici il est évalué à la livraison de l'octet : une commutation au
    // milieu d'une trame ne reconstruit pas les bits partiels ni leurs erreurs.
    if (loopback_ || internalSerialLoopback()) receiveSerialByte(serialTxByte_, when);
    if (!(timer_[0x2D] & 1)) {
        finishSerialTx(when);
    } else if (!serialTxFull_ && !serialTxUnderrun_ && !((timer_[0x2D] & 8) && (timer_[0x29] & 0x18))) {
        // MAME tx_clock : UE est mémorisé jusqu'à lecture TSR ou TE=0.
        // Écrire/recharger TDR ne l'efface pas et ne crée pas un nouveau front.
        serialTxUnderrunClearCycle_ = serialTxFrameClearCycle_;
        serialTxUnderrun_ = true; raiseAt(SRC_TXERR, when);
    }
    startSerialTx(true);
}

// Les registres MFP sont sur les adresses IMPAIRES à partir de $FFFA00.
// On indexe par l'offset bas (addr & 0x3F).
uint8_t Mfp::read8(uint32_t addr) {
    updateTimers();          // ≙ MFP_UpdateTimers en tête de MFP_*_ReadByte
    switch (addr & 0x3F) {
        case 0x01: {                // GPIP : lignes d'ENTRÉE matérielles (les écritures
                                    // CPU sur $FFFA01 ne doivent pas les écraser).
            // Lignes en SORTIE (DDR=1) → on relit le verrou écrit par le CPU ; lignes
            // en ENTRÉE (DDR=0) → la valeur calculée par gpipInput() (cf. Hatari
            // MFP_GPIP_ReadByte_Main : GPIP = (GPIP & DDR) | (entrées & ~DDR)).
            // ddr vaut 0 par défaut (tout en entrée) → le résultat reste exactement les entrées.
            const uint8_t v = gpipInput();
            uint8_t r = uint8_t((gpip & ddr) | (v & ~ddr));
            if (gpipHook_) gpipHook_(r);     // adaptateur sur le port série (PortDevices)
            return r;
        }
        case 0x03: return aer;
        case 0x05: return ddr;
        case 0x07: return iera;
        case 0x09: return ierb;
        case 0x0B: return ipra;
        case 0x0D: return iprb;
        case 0x0F: return isra;
        case 0x11: return isrb;
        case 0x13: return imra;
        case 0x15: return imrb;
        // (diag lectures TBDR : cf. case 0x21 ci-dessous)
        // MC68HC901 §4.1.3 et MAME REGISTER_VR : bits2..0 inutilisés,
        // toujours lus à zéro, y compris depuis un ancien état brut sauvegardé.
        case 0x17: return uint8_t(vr & 0xF8);
        case 0x1B: return tbcr_;     // Timer B control
        case 0x1F: {                          // TADR : compteur VIVANT de Timer A
            const uint8_t r = readTimerData(0);
            // DIAG (NEOST_MFPRD_DIAG=1) : lectures TADR/TBDR datées — à diff'er
            // contre Hatari --trace mfp_read (chantier Closure : la démo mesure
            // par les compteurs de timers, cf. docs/CLOSURE_CHANTIER.md).
            static const bool d = std::getenv("NEOST_MFPRD_DIAG") != nullptr;
            if (d) std::fprintf(stderr, "[MFPRD] TADR=%02X\n", r);
            return r;
        }
        case 0x21: {                          // TBDR : compteur VIVANT de Timer B
            const uint8_t r = readTimerData(1);
            static const bool d = std::getenv("NEOST_MFPRD_DIAG") != nullptr;
            if (d) std::fprintf(stderr, "[MFPRD] TBDR=%02X\n", r);
            return r;
        }
        case 0x23: return readTimerData(2);  // TCDR : compteur VIVANT de Timer C
        case 0x25: return readTimerData(3);  // TDDR : compteur VIVANT de Timer D
        case 0x2B: {                 // RSR : bit7 = Buffer Full ; bit6 = Overrun Error
            const uint8_t v = uint8_t((timer_[0x2B] & 0x3F) | (rxFull_ ? 0x80 : 0) | (rxOverrun_ ? 0x40 : 0));
            rxOverrun_ = false;      // les bits d'erreur du RSR se vident à la LECTURE (pas l'UDR)
            return v;
        }
        case 0x2D:
            if (sched_) {
                const uint8_t status = uint8_t((timer_[0x2D] & 0x2F) |
                    (!serialTxFull_ ? 0x80 : 0) | (serialTxUnderrun_ ? 0x40 : 0) |
                    (serialTxEnd_ ? 0x10 : 0));
                if (sched_->liveNow() >= serialTxUnderrunClearCycle_)
                    serialTxUnderrun_ = false;
                return status;
            }
                                    // TSR : bit7 = Buffer Empty (TX instantané, cf. Hatari
                                     // RS232_TSR_ReadByte) ; bit6 = Underrun Error. Notre
                                     // transmetteur « instantané » tourne à vide dès qu'il est
                                     // inactif → underrun (le test série attend cette erreur).
            return uint8_t(timer_[0x2D] | 0x80 | (loopback_ ? 0x40 : 0));
        case 0x2F:                   // UDR : lecture → consomme l'octet reçu (l'overrun, lui,
                                     // ne se vide qu'à la lecture du RSR → le handler RxErr le voit)
            // MAME REGISTER_UDR retourne toujours le registre RX, même BF=0.
            // Une lecture vide ne révèle pas le registre TX distinct.
            rxFull_ = false;
            if (rxPendingOverrun_) {
                rxPendingOverrun_ = false;
                rxOverrun_ = true;
                // OE devient visible et RXERR naît à cette lecture, pas au
                // caractère perdu : elle ouvre le buffer encore occupé jusque-là.
                raiseAt(SRC_RXERR, sched_ ? sched_->liveNow() : 0);
            }
            return rxByte_;
        default:   return timer_[addr & 0x3F];   // autres timers/USART : relisables
    }
}

void Mfp::write8(uint32_t addr, uint8_t v) {
    // Les fins TX/BREAK déjà échues précèdent la modification MMIO.
    bool caughtUpTx = false;
    if (sched_) while (serialTxBusy_) {
        const int64_t remaining = sched_->rawCyclesUntil(Scheduler::SERIAL_TX);
        if (remaining == INT64_MIN || remaining > 0) break;
        const int64_t when = sched_->liveNow() + remaining;
        sched_->cancel(Scheduler::SERIAL_TX); advanceSerialTx(when);
        caughtUpTx = true;
    }
    // Élire ces fronts à leur échéance, avant que updateTimers ne traite la
    // frontière MMIO courante ; TXEMPTY reste daté même hors dispatch.
    if (caughtUpTx) flushIrqUpdate();
    updateTimers();          // ≙ MFP_UpdateTimers en tête de MFP_*_WriteByte
    switch (addr & 0x3F) {
        case 0x01: gpip = v; break;   // latch des bits de SORTIE (les entrées sont calculées)
        // Écriture AER : un changement du front actif peut DÉCLENCHER une IRQ GPIP même
        // sans transition de la ligne d'entrée (cf. gpipUpdateInterrupt / démos « M »).
        // GPIP (0x01) et DDR (0x05) ne peuvent PAS lever de front ici : les bits d'entrée
        // sont calculés (gpipInput, inchangé par ces écritures) et les bits de sortie sont
        // exclus (DDR=1). On ne réévalue donc que sur AER.
        case 0x03: { const uint8_t aerOld = aer;
                     if (((aerOld ^ v) & 8) && timerBEdgeSink_) timerBEdgeSink_((v & 8) != 0);
                     aer = v;
                     gpipUpdateInterrupt(gpipInput(), gpipInput(), aerOld, aer); break; }
        case 0x05: ddr  = v; break;
        // Tout changement de IER/IPR/IMR/ISR/VR RÉ-ÉVALUE le signal IRQ (port Hatari :
        // MFP_UpdateIRQ_All après chaque écriture de ces registres) — daté du cycle
        // d'écriture : démasquer une requête déjà pendante fait monter IRQ MAINTENANT
        // (visible du CPU 4 cycles plus tard), pas à la date d'arrivée de la requête.
        // Désactiver un canal (IER=0) efface aussi son interruption pendante.
        case 0x07: iera = v; ipra &= iera; updateIrq(writeEventTime()); break;
        case 0x09: ierb = v; iprb &= ierb; updateIrq(writeEventTime()); break;
        // IPR/ISR : on n'EFFACE que les bits écrits à 0 (les 1 laissent inchangé).
        case 0x0B: ipra &= v; updateIrq(writeEventTime()); break;
        case 0x0D: iprb &= v; updateIrq(writeEventTime()); break;
        case 0x0F: isra &= v; updateIrq(writeEventTime()); break;
        case 0x11: isrb &= v; updateIrq(writeEventTime()); break;
        case 0x13: imra = v; updateIrq(writeEventTime()); break;
        case 0x15: imrb = v; updateIrq(writeEventTime()); break;
        case 0x17: {
            // VR : vecteur d'interruption + bit3 = mode EOI (1 = software, 0 =
            // automatique). Le passage software→automatique (1→0) VIDE les bits
            // in-service ISRA/ISRB (cf. Hatari MFP_VectorReg_WriteByte) — sinon un
            // in-service resté posé bloque les IRQ de priorité inférieure pour
            // toujours.
            const uint8_t oldVr = vr;
            vr = v & 0xF8;
            if ((oldVr & 0x08) && !(v & 0x08)) { isra = 0; isrb = 0; }
            updateIrq(writeEventTime());
            break;
        }
        // TxCR : changement de mode (port des MFP_TimerXCtrl_WriteByte) — une valeur
        // INCHANGÉE ne redate pas le timer ; un arrêt fige le compteur courant ; un
        // démarrage part du compteur (continuation), pas de la recharge.
        case 0x19: writeTimerCtrl(0, uint8_t(v & 0x0F)); break;           // TACR (bit4 reset ignoré, cf. Hatari)
        case 0x1B: writeTimerCtrl(1, uint8_t(v & 0x0F)); break;           // TBCR (0x08 = event-count ; 1-7 = délai)
        case 0x1D: writeTimerCtrl(2, uint8_t((v >> 4) & 0x07));           // TCDCR : C bits 4-6, D bits 0-2
                   writeTimerCtrl(3, uint8_t(v & 0x07));
                   updateSerialConfig(); break;   // Timer D = horloge USART (cf. mfp.c:3474)
        // TxDR : SEULE la valeur de recharge change. Un timer en délai qui court n'est
        // NI rechargé NI redaté (cf. MFP_TimerAData_WriteByte : « if timer is running
        // do not set ») — Captain Blood réécrit TADR en boucle et compare au compteur
        // VIVANT qui continue de décompter ; un timer ARRÊTÉ (ctrl=0) charge aussi le
        // compteur. Le nouveau délai s'applique au prochain rechargement.
        case 0x1F: timer_[0x1F] = v; taReload_ = v;                       // TADR
                   if (timerCtrl(0) == 0) taCounter_ = v;
                   break;
        case 0x21: tbReload_ = v;                                         // TBDR
                   if (timerCtrl(1) == 0) tbCounter_ = v;
                   break;
        case 0x23: timer_[0x23] = v;                                      // TCDR
                   if (timerCtrl(2) == 0) tcCounter_ = v;
                   break;
        case 0x25: timer_[0x25] = v;                                      // TDDR
                   if (timerCtrl(3) == 0) tdCounter_ = v;
                   updateSerialConfig(); break;   // nouveau diviseur → bauds USART (mfp.c:3311)
        case 0x29: timer_[0x29] = v;                                      // UCR
                   updateSerialConfig();
                   if (sched_ && serialTxKind_ == 1 && !(v & 0x18)) {
                       sched_->cancel(Scheduler::SERIAL_TX);
                       serialTxBusy_ = false; serialTxKind_ = 0;
                   }
                   startSerialTx(false); break;   // UCR synchrone rend B inopérant (MC68901UM §7.3.2)
        case 0x2B:
                   if (!(v & 1)) {
                       // MAME REGISTER_RSR : RE=0 remet le RSR entier à zéro,
                       // sans effacer les données RX ni les IRQ MFP déjà pendantes.
                       timer_[0x2B] = 0;
                       rxFull_ = rxOverrun_ = rxPendingOverrun_ = false;
                   } else {
                       // MAME REGISTER_RSR : seuls RE/SS sont contrôlables ;
                       // F/S l'est aussi en synchrone. BF/OE/PE/FE/M/CIP et
                       // le bit Break asynchrone appartiennent au récepteur.
                       const uint8_t writable = (timer_[0x29] & 0x18) ? 0x03 : 0x0B;
                       timer_[0x2B] = uint8_t((timer_[0x2B] & ~writable) | (v & writable));
                   }
                   // Le transport hôte garde sa cadence du fil ; réacquisition
                   // après réactivation au milieu d'un caractère non modélisée.
                   break;
        case 0x2D:
                   timer_[0x2D] = v & 0x2F;
                   if (!(v & 1)) {
                       serialTxUnderrun_ = false;
                       if (sched_ && !serialTxBusy_) finishSerialTx(writeEventTime());
                   }
                   else {
                       serialTxEnd_ = false;
                       if (sched_ && serialTxKind_ == 1 && !((v & 8) && (timer_[0x29] & 0x18))) {
                           sched_->cancel(Scheduler::SERIAL_TX);
                           serialTxBusy_ = false; serialTxKind_ = 2;
                           // Déduction du « end-of-break stop bit » (MC68HC901
                           // p7-8) : un bit de mark avant TDR/UE. Ancrage MMIO,
                           // phase TC initiale non modélisée, pas de mesure matérielle.
                           scheduleSerialTxHalfBits(2, false);
                       } else startSerialTx(false);
                   }
                   break;
        case 0x2F:
                   if (sched_) {
                       serialTxHolding_ = v; serialTxFull_ = true;
                       startSerialTx(false);
                       break;
                   }
                   timer_[0x2F] = v;                  // UDR : octet émis sur le port série
                   if (serialSink_) serialSink_(v);   // (RS-232). On le transmet aussitôt
                   // TX : le buffer d'émission se vide dès l'octet parti (TX instantané) —
                   // canaux 10 (buffer vide) et 9 (underrun) si le TRANSMETTEUR est activé
                   // (TSR bit0 TE), branché ou non : Hatari lève TRN_BUF_EMPTY à CHAQUE
                   // émission (rs232.c:569), sans condition de bouclage ni de récepteur.
                   // L'ancien gating « loopback ET RE » tuait la TX pilotée par IRQ : un
                   // terminal qui arme IERA bit2 et attend le canal 10 pour envoyer
                   // l'octet suivant (STiK, streaming modem) émettait UN octet puis
                   // attendait pour toujours. raise() ne pose l'IPR que si IER arme le
                   // canal → aucune IRQ parasite pour les impressions série normales.
                   if (timer_[0x2D] & 0x01) {          // TSR bit0 = Transmitter Enable
                       raise(SRC_TXERR);               // canal 9  : underrun (TX idle après envoi)
                       raise(SRC_TXEMPTY);             // canal 10 : buffer d'émission vidé
                   }
                   // Le repli instantané conserve les mêmes registres RX/OE.
                   if (loopback_ || internalSerialLoopback()) receiveSerialByte(v, writeEventTime());
                   break;
        default: timer_[addr & 0x3F] = v; break;      // autres timers/USART : mémorisés
    }
}

// -----------------------------------------------------------------------------
//  Timer D : 2,4576 MHz / prescaler / données ; sortie basculée à chaque
//  expiration (/2). MC68901UM §7.1.3 et MAME mc68901.cpp : UCR bit7 choisit
//  /16 ou /1. Mode synchrone non modélisé : repli de transport 8N1 /16.
//  Aucun arrondi tty hôte : le débit effectif appartient au matériel émulé.
// -----------------------------------------------------------------------------
void Mfp::updateSerialConfig() {
    const int ctrl = timer_[0x1D] & 0x07;          // prescaler Timer D (mode délai)
    if (!ctrl) return;                             // Timer D arrêté → config inchangée
    static constexpr int kDiv[8] = {0, 4, 10, 16, 50, 64, 100, 200};
    const int data = timer_[0x25] ? timer_[0x25] : 256;   // données 0 = 256
    const uint8_t ucr = timer_[0x29];
    const bool asynchronous = (ucr & 0x18) != 0;
    const int clockDivisor = asynchronous && !(ucr & 0x80) ? 1 : 16;
    // Le facteur est figé même si Timer D est arrêté ensuite. La division /2
    // de sa sortie s'annule avec les demi-bits du format dans les durées RX/TX.
    serialClockFactor_ = data * clockDivisor * kDiv[ctrl];
    const int baud = 2457600 / (2 * serialClockFactor_);
    if (baud == serialBaud_ && ucr == serialUcr_) return;   // rien de neuf → silence
    serialBaud_ = baud;
    serialUcr_  = ucr;
    // ⚠ ZONE CHAUDE — trace PLAFONNÉE. Le Timer D cadence l'USART, mais les jeux le
    // reprogramment en permanence pour tout autre chose (interruptions musique/raster) :
    // chaque écriture recalculait un « nouveau débit » et écrivait sur stderr. New Zealand
    // Story inondait ainsi le terminal de « USART : 3 bauds » à la trame, et l'écriture
    // synchrone sur un terminal attaché coûtait des trames (underruns audio observés).
    // Hatari ne journalise cela qu'en LOG_TRACE (rs232.c). On garde les premières lignes —
    // celles du boot TOS sont informatives — puis on se tait. NEOST_MFP_TRACE=1 rétablit tout.
    static const int  kMaxLogs = 8;
    static int        nLogs    = 0;
    static const bool traceAll = std::getenv("NEOST_MFP_TRACE") != nullptr;
    if (!traceAll && nLogs > kMaxLogs) return;
    if (!traceAll && nLogs == kMaxLogs) {
        ++nLogs;
        std::fprintf(stderr, "[mfp] USART: further reconfigurations are silent "
                             "(Timer D repurposed by the program; NEOST_MFP_TRACE=1 to see them all)\n");
        return;
    }
    ++nLogs;
    static const char* kStops[4] = {"sync", "1", "1.5", "2"};   // UCR bits 3-4
    std::fprintf(stderr, "[mfp] USART: %d baud, %d%c%s (UCR=$%02X, TDDR=%d, prescaler /%d)\n",
                 baud, 8 - ((ucr >> 5) & 3),                    // taille du mot (bits 5-6)
                 (ucr & 4) ? ((ucr & 2) ? 'E' : 'O') : 'N',     // parité (bits 1-2)
                 kStops[(ucr >> 3) & 3], ucr, data, kDiv[ctrl]);
}

// -----------------------------------------------------------------------------
//  Timers en mode DÉLAI (A/C/D) datés sur l'ordonnanceur (cf. docs/CYCLE_ACCURACY).
//  Le MFP tourne à 2457600 Hz, le CPU à 8021248 Hz : ratio EXACT 31333/9600
//  (même conversion entière qu'Hatari cycInt.c, sans flottant).
// -----------------------------------------------------------------------------
// Prescalers MFP et utilitaires communs aux timers. Les modes « pulse » des
// timers A/B (bit3 + bits 0-2 ≠ 0, soit ctrl 9-15) se comportent comme le mode
// délai correspondant (cf. Hatari MFP_StartTimer_AB : « clear bit 3, pulse
// width mode -> delay mode ») ; 8 = event-count, géré par hblank()/TAI.
static constexpr int kMfpDiv[8] = {0, 4, 10, 16, 50, 64, 100, 200};
static inline int delayCtrl(int ctrl) { return (ctrl > 8) ? (ctrl & 0x07) : ctrl; }
static constexpr Scheduler::Source kTimerSrc[4] = {
    Scheduler::TIMER_A, Scheduler::TIMER_B_DELAY, Scheduler::TIMER_C, Scheduler::TIMER_D };

int Mfp::timerCtrl(int timer) const {
    switch (timer) {
        case 0:  return timer_[0x19] & 0x0F;          // TACR
        case 1:  return tbcr_ & 0x0F;                 // TBCR
        case 2:  return (timer_[0x1D] >> 4) & 0x07;   // TCDCR moitié C
        default: return timer_[0x1D] & 0x07;          // TCDCR moitié D
    }
}

uint8_t& Mfp::timerCounterRef(int timer) {
    switch (timer) {
        case 0:  return taCounter_;
        case 1:  return tbCounter_;
        case 2:  return tcCounter_;
        default: return tdCounter_;
    }
}

int64_t Mfp::timerPeriodSubCycles(int timer, bool fromCounter) const {
    const int ctrl = delayCtrl(timerCtrl(timer));
    if (ctrl < 1 || ctrl > 7) return 0;       // 0 = arrêté ; 8 = event-count (pas délai)
    int data;
    switch (timer) {                          // recharge, ou compteur courant (continuation)
        case 0:  data = fromCounter ? taCounter_ : timer_[0x1F]; break;  // A
        case 1:  data = fromCounter ? tbCounter_ : tbReload_;    break;  // B
        case 2:  data = fromCounter ? tcCounter_ : timer_[0x23]; break;  // C
        default: data = fromCounter ? tdCounter_ : timer_[0x25]; break;  // D
    }
    const int count = data ? data : 256;      // données = 0 → 256
    const int64_t mfpCycles = static_cast<int64_t>(kMfpDiv[ctrl]) * count;
    // Même unité interne qu'Hatari (CYCINT_SHIFT=8) : la conversion est tronquée
    // UNE FOIS à 1/256 cycle, puis cette fraction est conservée entre les périodes.
    // L'ancien calcul tronquait à chaque recharge en cycles entiers (ex. Timer C
    // 200 Hz : 40106 au lieu de 40106,238), d'où une dérive monotone timer/faisceau.
    return ((mfpCycles << 8) * 31333) / 9600;
}

int64_t Mfp::timerPeriodCycles(int timer, bool fromCounter) const {
    return timerPeriodSubCycles(timer, fromCounter) >> 8;
}

// Port des MFP_TimerXCtrl_WriteByte (Hatari). Trois règles importantes :
//  - réécrire la MÊME valeur de contrôle ne touche à rien (pas de redatage) ;
//  - arrêter un délai en cours (1-7 → 0) fige le compteur courant, qui reste
//    relisible et sert de point de REPRISE si on redémarre sans réécrire TxDR ;
//  - démarrer programme l'échéance depuis le COMPTEUR (MFP_StartTimer_AB part
//    de TA_MAINCOUNTER), la recharge ne servant qu'aux rechargements suivants.
void Mfp::writeTimerCtrl(int timer, uint8_t newCtrl) {
    const int old = timerCtrl(timer);
    if (old == newCtrl) return;                       // valeur inchangée → aucun effet
    const int oldDelay = delayCtrl(old);
    if (newCtrl == 0 && oldDelay >= 1 && oldDelay <= 7)
        storeStoppedCounter(timer);                   // arrêt : fige le compteur courant
    switch (timer) {                                  // mémorise le nouveau contrôle
        case 0: timer_[0x19] = newCtrl; break;
        case 1: tbcr_ = newCtrl; break;
        case 2: timer_[0x1D] = uint8_t((timer_[0x1D] & 0x0F) | (newCtrl << 4)); break;
        case 3: timer_[0x1D] = uint8_t((timer_[0x1D] & 0xF0) | newCtrl); break;
    }
    // Délai (1-7 ou pulse) → échéance fraîche depuis le compteur ; 0/8 → annule
    // l'échéance délai (scheduleTimerAt cancel via période nulle).
    scheduleTimer(timer);
}

// Port de MFP_ReadTimerX(…, TimerIsStopping=true) : à l'arrêt d'un délai, le
// compteur vivant est figé dans le backing store. Cas limite Hatari : s'il
// reste moins d'une unité de prescaler (compteur interne < 1), le data reg
// vaudra la RECHARGE au prochain redémarrage.
void Mfp::storeStoppedCounter(int timer) {
    const int ctrl = delayCtrl(timerCtrl(timer));     // contrôle AVANT l'arrêt
    uint8_t reload;
    switch (timer) {
        case 0:  reload = timer_[0x1F]; break;
        case 1:  reload = tbReload_;    break;
        case 2:  reload = timer_[0x23]; break;
        default: reload = timer_[0x25]; break;
    }
    uint8_t count = reload;
    if (sched_ && ctrl >= 1 && ctrl <= 7) {
        // MÊME échéance sous-cyclique que readTimerData, et pour la MÊME raison (le
        // plafond entier du Scheduler surestime le reste de presque un cycle) : chez
        // Hatari les deux chemins passent par le SEUL MFP_ReadTimer_AB/CD, celui-ci
        // avec TimerIsStopping — il n'y a donc pas deux arrondis à faire diverger.
        const int64_t dueSub = timerDueSub_[timer];
        if (dueSub > 0 && sched_->rawCyclesUntil(kTimerSrc[timer]) != INT64_MIN) {
            const int64_t remSub = dueSub - sched_->liveNow() * 256;
            const int64_t remMfp = remSub > 0 ? (remSub * 9600 / 31333) >> 8 : 0;
            const int     div    = kMfpDiv[ctrl];
            if (remMfp >= div)                                     // sinon : règle « < 1 » → recharge
                count = static_cast<uint8_t>(((remMfp + div - 1) / div) & 0xFF);
        }
    }
    timerCounterRef(timer) = count;
}

// Port de MFP_ReadTimer_AB/CD (Hatari) : en mode délai actif, le registre de
// données reflète le COMPTEUR qui décompte (data → 1 → recharge), pas la valeur
// de recharge figée. On le reconstruit depuis les cycles CPU restants avant
// l'IRQ programmée : count = ceil(cyclesMfpRestants / prescaler), avec la
// conversion CPU→MFP inverse de timerPeriodCycles (× 9600 / 31333).
uint8_t Mfp::readTimerData(int timer) const {
    const int ctrl = delayCtrl(timerCtrl(timer));
    // Mode délai (ctrl 1-7) ET échéance armée → compteur vivant (MFP_CYCLE_TO_REG).
    if (sched_ && ctrl >= 1 && ctrl <= 7) {
        // ⚠ L'échéance de référence est la SOUS-CYCLIQUE (timerDueSub_, 8 bits de
        // fraction), PAS celle que voit le Scheduler — qui en est le PLAFOND entier
        // (`next = (nextSub + 255) >> 8`, cf. scheduleTimerAt) et surestime donc le
        // reste de presque un cycle CPU. Comme le compte est un ceil (MFP_CYCLE_TO_REG),
        // cette fraction perdue faisait lire UN CRAN DE TROP chaque fois que le reste
        // tombait pile sur un multiple du prescaler. Mesuré à l'oracle Hatari sur
        // l'étalon `mfp_poll` (2026-09-02) : 6 lignes sur 100, toutes NeoST = Hatari+1,
        // 80 px — closes par ce seul changement. Hatari ne peut pas avoir le défaut :
        // son `InterruptHandlers[].Cycles` EST la valeur fractionnaire (unités internes
        // CPU<<8, CYCINT_SHIFT), et `CycInt_FindCyclesRemaining` la soustrait telle
        // quelle de l'horloge live (cycInt.c) — c'est exactement ce qu'on fait ici.
        const int64_t dueSub = timerDueSub_[timer];
        if (dueSub > 0 && sched_->rawCyclesUntil(kTimerSrc[timer]) != INT64_MIN) {
            int64_t remSub = dueSub - sched_->liveNow() * 256;
            // Échéance passée mais pas encore dispatchée (lecture sous-instruction
            // entre l'expiration et la fin du bloc CPU) : le matériel a DÉJÀ
            // rechargé → repli modulo la période de recharge. Hatari obtient le
            // même effet en avançant les timers (MFP_UpdateTimers) avant la lecture.
            // Sans ce repli, l'écrêtage à 0 rend la valeur de recharge ILLISIBLE
            // (Captain Blood compare TADR au compteur vivant et ne sort jamais).
            if (remSub <= 0) {
                const int64_t periodSub = timerPeriodSubCycles(timer, /*fromCounter=*/false);
                if (periodSub > 0) remSub = periodSub - ((-remSub) % periodSub);
                else               remSub = 0;
            }
            // Sous-cycles CPU → cycles MFP : inverse EXACTE de timerPeriodSubCycles
            // (× 31333/9600 avec 8 bits de fraction), la fraction n'étant lâchée qu'ici.
            const int64_t remMfp = (remSub * 9600 / 31333) >> 8;
            const int     div    = kMfpDiv[ctrl];
            const int64_t count  = (remMfp + div - 1) / div;       // ceil (round vers le haut)
            return static_cast<uint8_t>(count & 0xFF);             // 256 → 0
        }
    }
    // event-count (A/B, ctrl=8) → compteur suivi par hblank()/timerA_setLineInput() ;
    // timer à l'arrêt → compteur figé par storeStoppedCounter()/écriture TxDR.
    switch (timer) {
        case 0:  return taCounter_;
        case 1:  return tbCounter_;
        case 2:  return tcCounter_;
        default: return tdCounter_;
    }
}

void Mfp::scheduleTimer(int timer) {
    // Programmation FRAÎCHE (démarrage via TxCR) : ancrée sur l'horloge live, le
    // cycle absolu EXACT de l'écriture (et non le début du quantum) — un timer
    // programmé en plein bloc CPU démarre à l'instant réel, comme Hatari
    // (CycInt_AddRelativeInterrupt depuis l'horloge immédiate). La préemption du
    // Scheduler coupe alors le bloc pour servir l'IRQ à temps. La première
    // échéance part du COMPTEUR courant (continuation après un arrêt) ; les
    // suivantes (onTimerExpire) repartiront de la recharge.
    scheduleTimerAt(timer, sched_ ? sched_->liveNow() : 0, /*fromCounter=*/true);
}

void Mfp::scheduleTimerAt(int timer, int64_t anchor, bool fromCounter) {
    if (!sched_) return;
    // Timer B (timer==1) : seul le mode DÉLAI (TBCR 1-7) est daté ici ; en event-count
    // (TBCR=8) timerPeriodSubCycles renvoie 0 → on annule la source délai (le tic est
    // alors piloté par Machine via mfp.hblank()).
    const Scheduler::Source src = kTimerSrc[timer];
    const int64_t periodSub = timerPeriodSubCycles(timer, fromCounter);
    if (periodSub <= 0) {
        timerDueSub_[timer] = 0;
        sched_->cancel(src);
        return;
    }

    // Une programmation fraîche repart de l'horloge CPU entière de l'écriture.
    // Un rechargement repart de l'échéance FRACTIONNAIRE précédente et non de son
    // ceil entier exposé au Scheduler : c'est le reste accumulé de cycInt/Hatari.
    const int64_t anchorSub = (!fromCounter && timerDueSub_[timer] > 0)
                            ? timerDueSub_[timer] : anchor * 256;
    int64_t nextSub = anchorSub + periodSub;
    const int64_t nowSub = sched_->liveNow() * 256;
    if (nextSub <= nowSub) {
        // Retard ≥ une période entière (cas rare : on a sauté des échéances) : on
        // réaligne sur la grille d'origine sans tirer une rafale d'IRQ en retard —
        // équivalent du modulo sur PendingCyclesOver d'Hatari (≤ une période).
        const int64_t skipped = (nowSub - nextSub) / periodSub + 1;
        nextSub += skipped * periodSub;
    }
    timerDueSub_[timer] = nextSub;
    // CycInt_Process déclenche quand deadline_sub <= clock_cpu<<8 : le premier
    // cycle CPU qui atteint l'échéance est donc son plafond, pas sa troncature.
    const int64_t next = (nextSub + 255) >> 8;
    sched_->schedule(src, next);
}

void Mfp::onTimerExpire(int timer) {
    static constexpr int kSrc[4] = {SRC_TIMERA, SRC_TIMERB, SRC_TIMERC, SRC_TIMERD};
    // Garde « timer arrêté » (Hatari MFP_InterruptHandler_TimerA mfp.c:1741 :
    // « if ( ( pMFP->TACR & 0xf ) != 0 ) » avant MFP_InputOnChannel, puis
    // MFP_StartTimer_* avec ctrl=0 → TimerClockCycles=0 → pas de replanification).
    if ((timerCtrl(timer) & 0x0F) == 0) return;
    // L'IRQ est ANTIDATÉE de l'échéance réelle du timer (et non de l'horloge live,
    // en retard de la latence de dispatch) — port d'Interrupt_Delayed_Cycles
    // (mfp.c:1741+) : le délai de visibilité de 4 cycles court depuis l'expiration
    // matérielle du timer, pas depuis le moment où l'émulateur a servi l'événement.
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    const int64_t when = due >= 0 ? due : (sched_ ? sched_->liveNow() : 0);
    raiseAt(kSrc[timer], when);               // lève l'IRQ (si le canal est activé)
    // À l'expiration, le compteur RECHARGE depuis le data reg (data → 1 → reload).
    switch (timer) {
        case 0:  taCounter_ = taReload_;    break;
        case 1:  tbCounter_ = tbReload_;    break;
        case 2:  tcCounter_ = timer_[0x23]; break;
        default: tdCounter_ = timer_[0x25]; break;
    }
    // Relance la période ANCRÉE sur l'échéance qui vient d'expirer (port
    // PendingCyclesOver) : le prochain tic tombe à échéance+période, gommant la
    // latence d'IRQ. Repli sur l'horloge live si l'échéance n'est pas disponible.
    scheduleTimerAt(timer, when, /*fromCounter=*/false);
}

void Mfp::hblank() {
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    hblankAt(due >= 0 ? due : (sched_ ? sched_->liveNow() : 0));
}

void Mfp::hblankAt(int64_t when) {
    // En mode event-count (TBCR bits 0-3 == 0x08), Timer B décompte d'une unité
    // par ligne ; à 0 il recharge et lève l'IRQ Timer B (canal 8) si armée.
    // Pas de garde sur compteur==0 : comme pour le Timer A, data reg 0 vaut 256
    // — le décrément 0→255 est le wrap voulu (MFP_TimerB_EventCount, mfp.c).
    if ((tbcr_ & 0x0F) != 0x08) return;
    if (--tbCounter_ == 0) {
        tbCounter_ = tbReload_;
        // Anti-datation du tic (bit0 NEOST_MFP_EXACT, ≙ MFP_TimerB_EventCount qui
        // passe Delayed_Cycles=PendingCycles) : l'IRQ est datée à l'échéance
        // TIMER_B servie (firingDue), pas à la frontière de bloc — le délai de
        // 4 cyc vers le CPU court depuis l'événement DE réel, comme les timers
        // délai (cf. onTimerExpire).
        if (g_mfpExact & 1) raiseAt(SRC_TIMERB, when);
        else                raise(SRC_TIMERB);
    }
}

void Mfp::timerA_setLineInput(bool bit) {
    // Port de MFP_TimerA_Set_Line_Input. La ligne TAI (XSINT du son DMA sur STE) est
    // associée à l'AER GPIP4. On ne compte que sur le FRONT actif : transition vers le
    // niveau égal au bit4 de l'AER (par défaut AER bit4=0, partagé avec l'ACIA active
    // bas → on compte les passages à 0 = fins de trame son DMA).
    if (tai_ == bit) return;                       // pas de transition
    tai_ = bit;
    if ((timer_[0x19] & 0x0F) != 0x08) return;     // pas en event-count → rien
    if (bit != ((aer >> 4) & 1)) return;           // front non sélectionné par l'AER GPIP4
    // À 1, le compteur expire : recharge depuis TADR puis IRQ. Sinon décrément ; comme
    // taCounter_ est un uint8_t, 0 décrémente vers 255 → data reg 0 vaut bien 256.
    if (taCounter_ == 1) {
        taCounter_ = taReload_;
        raise(SRC_TIMERA);
    } else {
        taCounter_ = uint8_t(taCounter_ - 1);
    }
}

// Ligne XSINT du son DMA STE → GPIP7. La valeur du pin GPIP7 est (moniteur XOR XSINT)
// ; on applique la règle de front d'Hatari (MFP_GPIP_Update_Interrupt) : un canal GPIP
// se lève sur une transition de la ligne quand sa nouvelle valeur égale le bit AER
// correspondant (AER=0 → front 1→0 ; AER=1 → front 0→1). raise() ne posera l'IPR que
// si le canal 15 est activé (IERA bit7). Sur une machine sans son DMA, hasDmaSound_ est
// faux et ce setter n'est jamais appelé avec une ligne active.
void Mfp::setXsintLine(bool a) {
    // Recomposer la pin entière avant de détecter le front, comme
    // MFP_GPIP_Set_Line_Input (Hatari mfp.c:1180-1219). Le bouton Multiface
    // peut maintenir GPIP7 bas malgré une transition du XOR moniteur/XSINT.
    gpipSetLine(xsint_, a);
}

// Octet des 8 lignes d'ENTRÉE du GPIP (actives BAS), tel que le voit le détecteur de
// front. Identique au calcul de read8($FFFA01) avant application du DDR.
bool Mfp::mfpSelfTest() {
    int pass = 0, fail = 0;
    auto chk = [&](const char* n, long got, long want) {
        if (got == want) { ++pass; }
        else { ++fail; std::fprintf(stderr, "  FAIL %-30s got=%ld want=%ld\n", n, got, want); }
    };

    // --- (a) Bits d'ENTRÉE GPIP forcés à la lecture (ddr=0 → read8 $01 = gpipInput) --
    hasDmaSound_ = false;                 // isole bit7 = colorMonitor_ (pas de XOR XSINT)
    ddr = 0;                              // toutes les lignes en ENTRÉE
    fdcLine_ = aciaLineKbd_ = aciaLineMidi_ = false;
    riLine_ = false;             // RI : repos DÉSASSERTÉ (MC1489 entrée ouverte)
    gpuLine_ = busyLine_ = false;        // désassertées
    hasBlitter_ = true;                  // bit3 piloté par le blitter (cf. (a') sans blitter)
    ctsLine_ = dcdLine_  = true;         // CTS/DCD actives au repos (cf. Mfp.hpp)
    colorMonitor_ = true;
    chk("bit7 couleur (gpipInput)", (gpipInput() & 0x80) ? 1 : 0, 1);
    chk("bit7 couleur (read8 $01)", (read8(0x01) & 0x80) ? 1 : 0, 1);
    colorMonitor_ = false;
    chk("bit7 mono", (gpipInput() & 0x80) ? 1 : 0, 0);
    colorMonitor_ = true;
    fdcLine_ = true;  chk("bit5 FDC asserté→0", (gpipInput() & 0x20) ? 1 : 0, 0);
    fdcLine_ = false; chk("bit5 FDC repos→1",   (gpipInput() & 0x20) ? 1 : 0, 1);
    aciaLineKbd_ = true;  chk("bit4 ACIA kbd→0", (gpipInput() & 0x10) ? 1 : 0, 0);
    aciaLineKbd_ = false; aciaLineMidi_ = true;
    chk("bit4 ACIA midi→0 (wire-OR)", (gpipInput() & 0x10) ? 1 : 0, 0);
    aciaLineMidi_ = false; chk("bit4 ACIA repos→1", (gpipInput() & 0x10) ? 1 : 0, 1);
    // Bits 6 (RI) et 3 (blitter) : SUIVENT leur ligne —
    // figés à 0 ils courts-circuiteraient le détecteur de front (bug 2026-08-13 :
    // IRQ fin de blit et RI mortes, gpipInput invariant sous ces lignes).
    gpuLine_ = true;  chk("bit3 blit en cours→1", (gpipInput() & 0x08) ? 1 : 0, 1);
    gpuLine_ = false; chk("bit3 repos→0",         (gpipInput() & 0x08) ? 1 : 0, 0);
    riLine_ = true;   chk("bit6 RI assertée→0",    (gpipInput() & 0x40) ? 1 : 0, 0);
    riLine_ = false;  chk("bit6 RI désassertée→1", (gpipInput() & 0x40) ? 1 : 0, 1);
    // (a') ST SANS blitter : GPIP3 tirée haut par R4, insensible à gpuLine_.
    hasBlitter_ = false;
    chk("bit3 sans blitter : pull-up→1", (gpipInput() & 0x08) ? 1 : 0, 1);
    hasBlitter_ = true;
    // Front de FIN DE BLIT (canal 3 = GPIP3, IERB bit3, AER=0 → front 1→0) : le
    // chemin complet setBlitterLine(start)→(done) doit lever IPRB bit3.
    ierb = 0xFF; aer = 0x00; iprb = 0; gpuLine_ = false;
    setBlitterLine(false);                // démarrage : ligne haute (blit en cours)
    chk("blit start : pas d'IRQ", (iprb & 0x08) ? 1 : 0, 0);
    setBlitterLine(true);                 // fin : front 1→0 → canal 3
    chk("blit done : IPRB bit3", (iprb & 0x08) ? 1 : 0, 1);
    iprb = 0;
    // Une ligne en SORTIE (ddr=1) renvoie le latch gpip, PAS l'entrée calculée.
    ddr = 0x20; gpip = 0x20; fdcLine_ = true;   // fdc asserté, mais bit5 en sortie=1
    chk("bit5 en sortie = latch", (read8(0x01) & 0x20) ? 1 : 0, 1);
    ddr = 0; fdcLine_ = false;

    // --- (b) Détection de FRONT GPIP (canal 7 = FDC, bit5 ; IERB l'active) -----------
    ierb = 0xFF;
    aer = 0x00;                           // AER bit5=0 → front ACTIF = 1→0
    iprb = 0; fdcLine_ = false;
    gpipSetLine(fdcLine_, true);          // 1→0 : actif → lève IPRB bit7
    chk("AER0 front 1→0 actif", (iprb & 0x80) ? 1 : 0, 1);
    iprb = 0; gpipSetLine(fdcLine_, false); // 0→1 : inactif
    chk("AER0 front 0→1 inactif", (iprb & 0x80) ? 1 : 0, 0);
    aer = 0x20;                           // AER bit5=1 → front ACTIF = 0→1
    iprb = 0; fdcLine_ = false;
    gpipSetLine(fdcLine_, true);          // 1→0 : inactif
    chk("AER1 front 1→0 inactif", (iprb & 0x80) ? 1 : 0, 0);
    iprb = 0; gpipSetLine(fdcLine_, false); // 0→1 : actif
    chk("AER1 front 0→1 actif", (iprb & 0x80) ? 1 : 0, 1);
    // Ligne en SORTIE (ddr bit5=1) : aucun front ne lève le canal.
    ddr = 0x20; aer = 0x00; iprb = 0; fdcLine_ = false;
    gpipSetLine(fdcLine_, true);
    chk("ligne en sortie : pas de front", (iprb & 0x80) ? 1 : 0, 0);
    ddr = 0;

    // --- (c) Timer B event-count : fin/début de ligne selon AER bit3 ----------------
    aer = 0x00; chk("TimerB fin de ligne (AER3=0)",   timerBStartOfLine() ? 1 : 0, 0);
    aer = 0x08; chk("TimerB début de ligne (AER3=1)", timerBStartOfLine() ? 1 : 0, 1);

    // --- (d) Conversion MFP→CPU fractionnaire : aucune dérive à chaque recharge ----
    // Timer C TCDR=192, prescaler /64 = 12288 cycles MFP = 40106,23828125 cycles
    // CPU. Avec l'ancien arrondi indépendant, 25 périodes finissaient à 1002650 ;
    // la grille Hatari ×256 finit à ceil(25*10267197/256) = 1002656.
    Scheduler timerSched;
    Mfp timerProbe;
    timerProbe.setScheduler(&timerSched);
    timerProbe.timer_[0x1D] = 0x50;       // Timer C /64
    timerProbe.timer_[0x23] = 192;
    timerProbe.tcCounter_ = 192;
    timerProbe.scheduleTimerAt(2, 0, /*fromCounter=*/true);
    chk("TimerC 1re échéance ceil", timerSched.rawCyclesUntil(Scheduler::TIMER_C), 40107);
    chk("TimerC période sub-cycle", timerProbe.timerDueSub_[2], 10267197);
    for (int i = 1; i < 25; ++i)
        timerProbe.scheduleTimerAt(2, 0, /*fromCounter=*/false);
    chk("TimerC reste accumulé x25", timerSched.rawCyclesUntil(Scheduler::TIMER_C), 1002656);

    // Les trois autres timers, chacun sur un prescaler distinct, et la donnée 0 qui
    // vaut 256. Attendus calculés hors du code : floor(p×d×256×31333/9600), puis
    // plafond à l'échéance (CycInt d'Hatari).
    struct DelayCase { const char* name; int timer; int ctrl; int data; int64_t sub, first, x25; };
    static constexpr DelayCase kDelay[] = {
        {"TimerA /4 d192",   0, 1, 192,  641699,  2507,  62666},
        {"TimerB /16 d83",   1, 3,  83, 1109605,  4335, 108360},
        {"TimerD /200 d17",  3, 7,  17, 2840858, 11098, 277428},
        {"TimerA /4 d0=256", 0, 1,   0,  855599,  3343,  83555},
    };
    for (const DelayCase& c : kDelay) {
        Scheduler s;
        Mfp m;
        m.setScheduler(&s);
        const uint8_t d = uint8_t(c.data);
        switch (c.timer) {
            case 0:  m.timer_[0x19] = uint8_t(c.ctrl); m.timer_[0x1F] = d; m.taCounter_ = d; break;
            case 1:  m.tbcr_ = uint8_t(c.ctrl); m.tbReload_ = d; m.tbCounter_ = d; break;
            default: m.timer_[0x1D] = uint8_t(c.ctrl); m.timer_[0x25] = d; m.tdCounter_ = d; break;
        }
        const Scheduler::Source src = kTimerSrc[c.timer];
        m.scheduleTimerAt(c.timer, 0, /*fromCounter=*/true);
        const std::string n = c.name;
        chk((n + " 1re échéance").c_str(), s.rawCyclesUntil(src), c.first);
        chk((n + " période sub-cycle").c_str(), m.timerDueSub_[c.timer], c.sub);
        for (int i = 1; i < 25; ++i) m.scheduleTimerAt(c.timer, 0, /*fromCounter=*/false);
        chk((n + " reste accumulé x25").c_str(), s.rawCyclesUntil(src), c.x25);
    }

    // --- (e) reset() RELÂCHE toutes les lignes d'ENTRÉE ------------------------------
    // Ce sont des entrées, jamais recalculées à la lecture : celle qu'un reset oublie
    // reste coincée POUR TOUJOURS. RI l'était (bouclage DTR→RI, et bouton Ultimate
    // Ripper dont le relâchement à la VBL ne vient jamais — Machine::reset remet
    // PortDevices::pressed_ à faux avant). En DERNIER : reset() touche tout l'état.
    {
        Mfp probe;
        probe.ddr = 0; probe.hasDmaSound_ = false; probe.colorMonitor_ = true;
        probe.riLine_ = true; probe.busyLine_ = true; probe.monButton_ = true;  // RI hors repos (assertée)
        probe.hasBlitter_ = true;
        probe.ctsLine_ = probe.dcdLine_ = false;   // désassertées (repos = ACTIVES)
        probe.reset();
        chk("reset : RI (bit6) au repos",   (probe.gpipInput() & 0x40) ? 1 : 0, 1);
        chk("reset : BUSY (bit0) relâchée", (probe.gpipInput() & 0x01) ? 1 : 0, 1);
        chk("reset : CTS (bit2) au repos",  (probe.gpipInput() & 0x04) ? 1 : 0, 0);
        chk("reset : DCD (bit1) au repos",  (probe.gpipInput() & 0x02) ? 1 : 0, 0);
        chk("reset : bouton Multiface (bit7) relâché", (probe.gpipInput() & 0x80) ? 1 : 0, 1);
        chk("reset : GPIP pristine (blitter)", probe.gpipInput(), 0xF1);
        probe.hasBlitter_ = false;
        chk("reset : GPIP pristine (ST nu)", probe.gpipInput(), 0xF9);
    }

    // --- (f) ÉLECTION DE L'EXCEPTION : la PLUS ANCIENNE l'emporte ------------------
    // Table de vérité du chantier A42. La règle du 68901, portée d'Hatari
    // (MFP_InterruptRequest, mfp.c:1005 : `Pending_Time[Int] <= Pending_Time_Min`), est
    // que seules les requêtes les PLUS ANCIENNES concourent — la priorité ne départage
    // qu'entre elles. Elle n'a de sens que si l'élection voit PLUSIEURS entrées à la
    // fois, d'où l'élection groupée (flushIrqUpdate).
    // ⚠ C'est l'exhibiteur que le TODO exigeait avant de toucher à ce chemin : sans lui,
    // la règle était inobservable (aucun étalon pixel ne bouge, et `mfp_poll` masque les
    // IRQ, donc il est aveugle à toute l'élection).
    {
        auto elect = [&](int firstSrc, int64_t firstT, int secondSrc, int64_t secondT) {
            Scheduler s;
            Mfp p;
            p.setScheduler(&s);
            p.iera = p.ierb = 0xFF;      // tous les canaux activés…
            p.imra = p.imrb = 0xFF;      // …et non masqués
            p.raiseAt(firstSrc, firstT);
            p.raiseAt(secondSrc, secondT);   // AUCUNE élection entre les deux
            p.flushIrqUpdate();
            return p.currentInt_;
        };
        // Timer D (source 4, priorité BASSE) arrive AVANT Timer A (13, HAUTE) :
        // c'est D qui doit être servi. Sans élection groupée, A l'emportait.
        chk("élection : la PLUS ANCIENNE gagne (D avant A)",
            elect(SRC_TIMERD, 100, SRC_TIMERA, 140), SRC_TIMERD);
        // L'ordre des raise ne doit rien changer : c'est la DATE qui tranche.
        chk("élection : ordre des entrées indifférent",
            elect(SRC_TIMERA, 140, SRC_TIMERD, 100), SRC_TIMERD);
        // Dates ÉGALES → la priorité reprend ses droits (la plus haute gagne).
        chk("élection : à date égale, la plus PRIORITAIRE",
            elect(SRC_TIMERD, 100, SRC_TIMERA, 100), SRC_TIMERA);
        // La plus ancienne EST la plus prioritaire : pas de conflit.
        chk("élection : plus ancienne ET prioritaire",
            elect(SRC_TIMERA, 100, SRC_TIMERD, 140), SRC_TIMERA);
    }

    std::fprintf(stderr, "[mfp-selftest] %d OK, %d FAIL\n", pass, fail);
    return fail == 0;
}

uint8_t Mfp::gpipInput() const {
    // Repos des lignes, d'après le MATÉRIEL (et non plus d'après Hatari) :
    //
    //  · bit6 = RS-232 RI. Atari STe Field Service Manual (C302481-001, §2.12.5) :
    //    CTS, DCD et RI entrent au MFP via des récepteurs 1489 (U207 « 1489A RS-232
    //    receiver »). Le MC1489 INVERSE et, entrée ouverte, sort HAUT (fiche TI
    //    SLLS095, VOH « Input open » ≥ 2,6 V) ; un RI désasserté (−12 V, marque) sort
    //    haut aussi. Sans câble, ou avec tout câble dont RI ne sonne pas : bit6 = 1.
    //    Concordants : MAME (rs232_port_device : RI = 1 sans appareil, relié à i6
    //    sans inversion) et une lecture sur STFM réel (exxos, exxosforum t=3992
    //    p.49, 2022 : $FFFA01 = 233 = $E9, bits 6 et 3 à 1). Hatari rend 0 par pur
    //    artefact : MFP_Reset pose GPIP = 0 (mfp.c:523) et rien ne pilote jamais le
    //    bit 6. RI reste ACTIVE BASSE (assertion → 0, front descendant sous AER=0) :
    //    la cartouche Field Service teste « S9 RI-DTR » comme « SA DCD-DTR ».
    //  · bit3 = GPU_DONE. SANS blitter (ST) : seule la résistance de rappel R4 tire
    //    la ligne (4K7 sur ST, 1K sur STFM/Mega/STE — agranlund/STBlitter_RevA,
    //    README) → 1, conforme à la lecture $E9 ci-dessus. AVEC blitter : l'INT
    //    recopie BUSY (Atari Compendium) → 0 au repos, 1 pendant le blit — inchangé.
    //    ⚠ Incertitude : aucune mesure publiée sur Mega ST/STE au repos trouvée.
    //
    // Conséquence (moniteur couleur, CTS/DCD au repos actif à la Hatari) : $FFFA01
    // au repos vaut $F9 sur ST et $F1 sur Mega ST/STE/Mega STE, contre $B1 chez
    // Hatari — un diff de trace sur ce registre est ATTENDU, pas une régression.
    // L'inventaire de CLOSURE stocke cet octet (docs/CLOSURE_CHANTIER.md) mais la
    // convergence vers $B1 n'avait PAS corrigé sa mort : ce n'en était pas la cause.
    // ⚠ Les bits 6 et 3 SUIVENT leur ligne (riLine_/gpuLine_) : les figer court-
    // circuiterait le détecteur de front de gpipSetLine (IRQ fin de blit, canal 3, et
    // IRQ RI du bouclage, canal 14, mortes — bug du 2026-08-13).
    uint8_t v = 0xFF;
    if (hasBlitter_ && !gpuLine_) v &= ~0x08;   // bit3 : blitter au repos → BAS
    bool bit7 = colorMonitor_;                   // moniteur : couleur=1, mono=0
    if (hasDmaSound_) bit7 ^= xsint_;            // STE/Mega STE : XOR ligne XSINT son DMA
    if (!bit7)          v &= ~0x80;              // bit7 = moniteur^XSINT
    if (monButton_)     v &= ~0x80;              // bouton freeze Multiface (PortDevices)
    if (riLine_)        v &= uint8_t(~0x40);     // bit6 = RS232 RI assertée → 0
    if (fdcLine_)       v &= ~0x20;              // bit5 = FDC
    if (aciaLineKbd_ || aciaLineMidi_) v &= ~0x10;  // bit4 = ACIA clavier OU MIDI (wire-OR)
    if (ctsLine_)       v &= ~0x04;              // bit2 = RS232 CTS
    if (dcdLine_)       v &= ~0x02;              // bit1 = RS232 DCD
    if (busyLine_)      v &= ~0x01;              // bit0 = Centronics BUSY
    return v;
}

// Port de MFP_GPIP_Update_Interrupt : sur un changement de GPIP/AER/DDR, on lève les
// canaux GPIP dont le FRONT actif vient de se produire. État = GPIP ^ AER ; pour une
// ligne en ENTRÉE (DDR=0) dont l'état bascule, le front est actif quand AER == niveau
// GPIP (AER=0 → front 1→0, AER=1 → front 0→1). raise() ne pose l'IPR que si le canal
// est activé (IER), comme MFP_InputOnChannel.
void Mfp::gpipUpdateInterrupt(uint8_t gpipOld, uint8_t gpipNew, uint8_t aerOld, uint8_t aerNew) {
    gpipUpdateInterruptAt(gpipOld, gpipNew, aerOld, aerNew, sched_ ? sched_->liveNow() : 0);
}

void Mfp::gpipUpdateInterruptAt(uint8_t gpipOld, uint8_t gpipNew, uint8_t aerOld, uint8_t aerNew, int64_t when) {
    static constexpr int kChan[8] = {0, 1, 2, 3, 6, 7, 14, 15};   // bit GPIP → canal MFP
    const uint8_t stateOld = gpipOld ^ aerOld;
    const uint8_t stateNew = gpipNew ^ aerNew;
    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t m = uint8_t(1u << bit);
        if ((ddr & m) == 0                          // ligne configurée en ENTRÉE
         && (stateOld & m) != (stateNew & m)        // l'état (GPIP^AER) a basculé
         && (gpipNew & m) == (aerNew & m))          // front ACTIF (AER == niveau GPIP)
            raiseAt(kChan[bit], when);
    }
}

// Port de MFP_InputOnChannel (mfp.c:1088-1131) : une requête sur un canal ACTIVÉ
// (IER=1) pose le bit pendant et DATE son arrivée (pendingTime_) ; sur un canal
// désactivé elle l'EFFACE. La plus ancienne requête non masquée de la fenêtre est
// suivie (pendingTimeMin_) pour servir les requêtes simultanées dans l'ordre
// chronologique. `when` peut être ANTÉRIEUR à l'horloge (timer servi en retard).
void Mfp::raise(int source) {
    raiseAt(source, sched_ ? sched_->liveNow() : 0);
}

void Mfp::raiseAt(int source, int64_t when) {
    const uint8_t bit = uint8_t(1u << (source & 7));
    uint8_t& ier = source >= 8 ? iera : ierb;
    uint8_t& ipr = source >= 8 ? ipra : iprb;
    const uint8_t imr = source >= 8 ? imra : imrb;
    if (ier & bit) {
        ipr |= bit;
        pendingTime_[source] = when;
        if ((imr & bit) && when < pendingTimeMin_) pendingTimeMin_ = when;
    } else {
        ipr &= ~bit;                      // canal désactivé : la requête est perdue
    }
    // ⚠ PAS D'ÉLECTION ICI (port de MFP_InputOnChannel, mfp.c:1125 : « MFP_UpdateNeeded
    // = true ; Tell main CPU loop to call MFP_UpdateIRQ() »). Élire tout de suite
    // consommerait `pendingTimeMin_` avant l'arrivée des autres entrées de la MÊME
    // instruction, et la règle chronologique ne pourrait jamais les départager : une
    // interruption PLUS RÉCENTE mais plus prioritaire l'emporterait, là où le 68901 sert
    // la PLUS ANCIENNE. Deux entrées consécutives existent dans ce fichier même
    // (TXERR→TXEMPTY, RXERR→RXFULL). L'élection est faite par flushIrqUpdate(), appelé
    // au calcul d'IPL — l'équivalent NeoST de la boucle CPU d'Hatari.
    if (g_mfpBatch) irqUpdateNeeded_ = true;
    else            updateIrq(0);         // A/B : ancien comportement, élection immédiate
}

// Élection différée (cf. Mfp.hpp). Event_Time = 0 → le front est daté de
// `pendingTime_[canal élu]`, comme MFP_UpdateIRQ appelé depuis la boucle CPU.
void Mfp::flushIrqUpdate() {
    if (inDispatch_ || !irqUpdateNeeded_) return;   // lot en cours : on accumule encore
    irqUpdateNeeded_ = false;
    updateIrq(0);
}

// Port de MFP_UpdateIRQ (mfp.c:946-985) : recalcule le signal IRQ du 68901. Sur un
// front MONTANT, l'instant du front (irqTime_) = eventTime (écriture registre/IACK)
// ou, à 0, la date d'arrivée de la requête élue — c'est ce qui antidate correctement
// un timer servi avec quelques cycles de latence. La visibilité CPU est différée de
// kIrqDelayToCpu : on arme Scheduler::MFP_IRQ pour recalculer l'IPL pile à temps
// (le callback Machine appelle cpu.updateIpl()). La retombée est immédiate.
void Mfp::updateIrq(int64_t eventTime) {
    int newInt = -1;
    if ((ipra & imra) | (iprb & imrb)) newInt = checkPendingInterrupts();
    if (newInt >= 0) {
        if (!irq_) irqTime_ = eventTime != 0 ? eventTime : pendingTime_[newInt];
        irq_ = true;
        currentInt_ = newInt;
    } else {
        irq_ = false;                     // pendantes bloquées par une in-service, ou rien
    }
    pendingTimeMin_ = kNever;             // la fenêtre chronologique est consommée
    if (!sched_) return;
    if (irq_) {
        const int64_t visibleAt = irqTime_ + kIrqDelayToCpu;
        const int64_t now = sched_->liveNow();
        if (now < visibleAt) sched_->schedule(Scheduler::MFP_IRQ, visibleAt);
        // Délai déjà écoulé (IRQ anti-datée servie en retard) : la prise doit se
        // faire À CETTE frontière d'instruction, pas une instruction plus tard
        // (bit1 NEOST_MFP_EXACT, ≙ MFP_ProcessIRQ : « clock − IRQ_Time ≥ 4 →
        // exception » testé à chaque frontière). On arme MFP_IRQ à l'horloge
        // live : depuis un callback du dispatch (TIMER_B < MFP_IRQ dans l'énum),
        // il part dans le MÊME runTo ; depuis un accès MMIO mid-instruction, la
        // préemption coupe le bloc et il part à la frontière suivante — dans les
        // deux cas le commit de l'IPL a lieu à la première frontière éligible.
        else if (g_mfpExact & 2) sched_->schedule(Scheduler::MFP_IRQ, now);
    } else {
        sched_->cancel(Scheduler::MFP_IRQ);
    }
}

// Port de MFP_CheckPendingInterrupts + MFP_InterruptRequest (mfp.c:993-1071) :
// balayage par priorité décroissante (sources 15..8 puis 7..0) ; une source n'est
// éligible que si (1) pendante et non masquée, (2) la plus ANCIENNE de la fenêtre
// courante (pendingTime_ ≤ pendingTimeMin_ : deux requêtes dans la même instruction
// sont servies dans l'ordre d'arrivée, pas de priorité), (3) aucune source de
// priorité ≥ n'est en service (l'ISR n'est non nul qu'en mode software-EOI, le test
// inconditionnel est donc équivalent au câblage réel).
int Mfp::checkPendingInterrupts() const {
    const uint8_t pa = ipra & imra;
    const uint8_t pb = iprb & imrb;
    const int hi = highestInService();
    for (int b = 7; b >= 0; --b) {
        const int s = 8 + b;
        if ((pa & (1u << b)) && pendingTime_[s] <= pendingTimeMin_ && hi < s) return s;
    }
    for (int b = 7; b >= 0; --b) {
        const int s = b;
        if ((pb & (1u << b)) && pendingTime_[s] <= pendingTimeMin_ && hi < s) return s;
    }
    return -1;
}

int Mfp::highestPending() const {
    const uint8_t pa = ipra & imra;      // pendant ET non masqué
    const uint8_t pb = iprb & imrb;
    for (int b = 7; b >= 0; --b) if (pa & (1u << b)) return 8 + b;   // sources 15..8
    for (int b = 7; b >= 0; --b) if (pb & (1u << b)) return b;       // sources 7..0
    return -1;
}

int Mfp::highestInService() const {
    for (int b = 7; b >= 0; --b) if (isra & (1u << b)) return 8 + b;
    for (int b = 7; b >= 0; --b) if (isrb & (1u << b)) return b;
    return -1;
}

// Signal IRQ tel que VU DU CPU (port MFP_GetIRQ_CPU + MFP_ProcessIRQ, mfp.c:737/899) :
// le front montant n'est visible qu'après kIrqDelayToCpu cycles. Pendant la fenêtre,
// Scheduler::MFP_IRQ garantit qu'un updateIpl() sera rejoué à irqTime_+4.
bool Mfp::irqPending() const {
    if (!irq_) return false;
    if (!sched_) return true;             // pas d'ordonnanceur (tests unitaires) → immédiat
    return sched_->liveNow() - irqTime_ >= kIrqDelayToCpu;
}

// Port de MFP_ProcessIACK (mfp.c:812-854). Appelé par le cœur CPU au cycle de lecture
// du vecteur (sous Moira, cycle-exact, ~12 cycles après le début de l'exception) : on
// RÉ-ÉVALUE d'abord le signal (une IRQ plus prioritaire — ou un pending reposé —
// survenu entre-temps peut changer le vecteur), puis on sert currentInt_.
int Mfp::iack() {
    const int64_t now = sched_ ? sched_->liveNow() : 0;
    updateIrq(now != 0 ? now : 0);
    if (!irq_ || currentInt_ < 0) return -1;          // plus rien → spurious interrupt
    const int s = currentInt_;
    const uint8_t bit = uint8_t(1u << (s & 7));
    if (s >= 8) { ipra &= ~bit; if (vr & 0x08) isra |= bit; else isra &= ~bit; }
    else        { iprb &= ~bit; if (vr & 0x08) isrb |= bit; else isrb &= ~bit; }
    updateIrq(now != 0 ? now : 0);                    // le signal retombe (ou re-monte)
    return (vr & 0xF0) | s;               // vecteur MFP
}
