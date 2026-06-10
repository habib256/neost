// =============================================================================
//  Mfp.cpp — Logique d'interruption du MC68901.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Mfp.hpp"
#include <cstdio>

// RESET matériel du MC68901 (port de MFP_Reset, mfp.c:519-569). Le vrai MFP n'a PAS
// de signal de reset dédié pour le GPIP/USART, mais sur l'ST la broche /RESET du 68000
// le réinitialise (cf. reset.c:74, AVANT M68000_Reset). NeoST l'omettait → des IRQ
// Timer A / GPIP7 pouvaient survivre à un reset à chaud (musique chip qui s'emballe ou
// notes parasites après Ctrl+reset). On remet ici tout l'état d'interruption et les
// timers à zéro. PRÉSERVÉ : colorMonitor_/hasDmaSound_/loopback_ (propriétés posées
// AVANT le reset) et les lignes d'ENTRÉE des autres puces (FDC/ACIA/RS232), reforcées
// à la lecture du GPIP et resynchronisées par leurs puces respectives.
void Mfp::reset() {
    gpip = 0xFF; aer = 0; ddr = 0;        // GPIP au repos (entrées non assertées), AER/DDR neutres
    iera = ierb = 0;                      // enable
    ipra = iprb = 0;                      // pending
    imra = imrb = 0;                      // mask
    isra = isrb = 0;                      // in-service
    vr   = 0;                             // registre vecteur (mode auto, base 0)
    // Timers : mode + recharge + compteurs vivants + backing store des données/contrôle
    // (TACR $FFFA19, TBCR $FFFA1B, TCDCR $FFFA1D, TADR/TBDR/TCDR/TDDR…) tous remis à 0.
    tbcr_ = tbReload_ = tbCounter_ = 0;
    taReload_ = taCounter_ = 0;
    tai_ = false;                         // ligne d'entrée Timer A (XSINT) au repos
    for (uint8_t& b : timer_) b = 0;
    xsint_ = false;                       // ligne XSINT son DMA (re-synchronisée ensuite par DmaSound::reset)
    rxByte_ = 0; rxFull_ = false; rxOverrun_ = false;   // USART : tampon vidé (pas de RXFULL fantôme)
    // Signal IRQ daté : tout retombe (port MFP_Reset — IRQ/IRQ_Time/Pending_Time).
    irq_ = false; irqTime_ = 0; currentInt_ = -1;
    for (int64_t& t : pendingTime_) t = kNever;
    pendingTimeMin_ = kNever;
    if (sched_) {                         // annule toute échéance de timer en attente
        sched_->cancel(Scheduler::TIMER_A);
        sched_->cancel(Scheduler::TIMER_B);
        sched_->cancel(Scheduler::TIMER_B_DELAY);
        sched_->cancel(Scheduler::TIMER_C);
        sched_->cancel(Scheduler::TIMER_D);
        sched_->cancel(Scheduler::MFP_IRQ);
    }
}

// Les registres MFP sont sur les adresses IMPAIRES à partir de $FFFA00.
// On indexe par l'offset bas (addr & 0x3F).
uint8_t Mfp::read8(uint32_t addr) {
    switch (addr & 0x3F) {
        case 0x01: {                // GPIP : lignes d'ENTRÉE matérielles (les écritures
                                    // CPU sur $FFFA01 ne doivent pas les écraser).
            // Lignes en SORTIE (DDR=1) → on relit le verrou écrit par le CPU ; lignes
            // en ENTRÉE (DDR=0) → la valeur calculée par gpipInput() (cf. Hatari
            // MFP_GPIP_ReadByte_Main : GPIP = (GPIP & DDR) | (entrées & ~DDR)).
            // ddr vaut 0 par défaut (tout en entrée) → le résultat reste exactement les entrées.
            const uint8_t v = gpipInput();
            return uint8_t((gpip & ddr) | (v & ~ddr));
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
        case 0x17: return vr;
        case 0x1B: return tbcr_;     // Timer B control
        case 0x1F: return readTimerData(0);  // TADR : compteur VIVANT de Timer A
        case 0x21: return readTimerData(1);  // TBDR : compteur VIVANT de Timer B
        case 0x23: return readTimerData(2);  // TCDR : compteur VIVANT de Timer C
        case 0x25: return readTimerData(3);  // TDDR : compteur VIVANT de Timer D
        case 0x2B: {                 // RSR : bit7 = Buffer Full ; bit6 = Overrun Error
            const uint8_t v = uint8_t((timer_[0x2B] & 0x3F) | (rxFull_ ? 0x80 : 0) | (rxOverrun_ ? 0x40 : 0));
            rxOverrun_ = false;      // les bits d'erreur du RSR se vident à la LECTURE (pas l'UDR)
            return v;
        }
        case 0x2D:                   // TSR : bit7 = Buffer Empty (TX instantané, cf. Hatari
                                     // RS232_TSR_ReadByte) ; bit6 = Underrun Error. Notre
                                     // transmetteur « instantané » tourne à vide dès qu'il est
                                     // inactif → underrun (le test série attend cette erreur).
            return uint8_t(timer_[0x2D] | 0x80 | (loopback_ ? 0x40 : 0));
        case 0x2F:                   // UDR : lecture → consomme l'octet reçu (l'overrun, lui,
                                     // ne se vide qu'à la lecture du RSR → le handler RxErr le voit)
            if (rxFull_) { rxFull_ = false; return rxByte_; }
            return timer_[0x2F];
        default:   return timer_[addr & 0x3F];   // autres timers/USART : relisables
    }
}

void Mfp::write8(uint32_t addr, uint8_t v) {
    switch (addr & 0x3F) {
        case 0x01: gpip = v; break;   // latch des bits de SORTIE (les entrées sont calculées)
        // Écriture AER : un changement du front actif peut DÉCLENCHER une IRQ GPIP même
        // sans transition de la ligne d'entrée (cf. gpipUpdateInterrupt / démos « M »).
        // GPIP (0x01) et DDR (0x05) ne peuvent PAS lever de front ici : les bits d'entrée
        // sont calculés (gpipInput, inchangé par ces écritures) et les bits de sortie sont
        // exclus (DDR=1). On ne réévalue donc que sur AER.
        case 0x03: { const uint8_t aerOld = aer; aer = v;
                     gpipUpdateInterrupt(gpipInput(), gpipInput(), aerOld, aer); break; }
        case 0x05: ddr  = v; break;
        // Tout changement de IER/IPR/IMR/ISR/VR RÉ-ÉVALUE le signal IRQ (port Hatari :
        // MFP_UpdateIRQ_All après chaque écriture de ces registres) — daté du cycle
        // d'écriture : démasquer une requête déjà pendante fait monter IRQ MAINTENANT
        // (visible du CPU 4 cycles plus tard), pas à la date d'arrivée de la requête.
        // Désactiver un canal (IER=0) efface aussi son interruption pendante.
        case 0x07: iera = v; ipra &= iera; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x09: ierb = v; iprb &= ierb; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        // IPR/ISR : on n'EFFACE que les bits écrits à 0 (les 1 laissent inchangé).
        case 0x0B: ipra &= v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x0D: iprb &= v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x0F: isra &= v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x11: isrb &= v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x13: imra = v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x15: imrb = v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x17: vr   = v; updateIrq(sched_ ? sched_->liveNow() : 0); break;
        case 0x1B: tbcr_ = v; scheduleTimer(1); break;   // TBCR (0x08 = event-count ; 1-7 = délai)
        case 0x21: tbReload_ = v; tbCounter_ = v; scheduleTimer(1); break;  // TBDR → recharge + (re)date le délai
        // Timers A/C/D : on mémorise le registre PUIS on (re)programme l'échéance.
        case 0x19: timer_[0x19] = v; scheduleTimer(0); break;             // TACR
        case 0x1D: timer_[0x1D] = v; scheduleTimer(2); scheduleTimer(3); break; // TCDCR (C+D)
        case 0x1F: timer_[0x1F] = v; taReload_ = taCounter_ = v;          // TADR (+ event-count)
                   scheduleTimer(0); break;
        case 0x23: timer_[0x23] = v; scheduleTimer(2); break;             // TCDR
        case 0x25: timer_[0x25] = v; scheduleTimer(3); break;             // TDDR
        case 0x2F: timer_[0x2F] = v;                  // UDR : octet émis sur le port série
                   if (serialSink_) serialSink_(v);   // (RS-232). On le transmet aussitôt
                   // Connecteur de bouclage TxD→RxD : l'octet émis revient en réception
                   // (RSR Buffer Full + canal 12). Sans bouclage rien ne le lirait : le
                   // diagnostic « S RS232 » en a besoin pour valider la boucle locale.
                   // Le récepteur USART ne capte (et ne lève le canal 12) que s'il est
                   // ACTIVÉ : RSR bit0 = Receiver Enable. Le diagnostic ne l'arme que
                   // pendant le test « S RS232 » ; les impressions série normales (RE=0)
                   // ne doivent donc PAS générer d'IRQ parasite (sinon le test clavier,
                   // au boot, échoue). C'est le comportement matériel du MFP 68901.
                   if (loopback_) {              // connecteur branché : TxD→RxD (buffer 1 octet)
                       if (rxFull_) { rxOverrun_ = true; raise(SRC_RXERR); }  // octet sur buffer plein → overrun (canal 11)
                       rxByte_ = v;
                       rxFull_ = true;
                       raise(SRC_TXERR);         // canal 9  : underrun (transmetteur idle après envoi)
                       raise(SRC_TXEMPTY);       // canal 10 : buffer d'émission vidé (TX instantané)
                       raise(SRC_RXFULL);        // canal 12 : octet reçu par le bouclage
                   }
                   break;
        default: timer_[addr & 0x3F] = v; break;      // autres timers/USART : mémorisés
    }
}

// -----------------------------------------------------------------------------
//  Timers en mode DÉLAI (A/C/D) datés sur l'ordonnanceur (cf. docs/CYCLE_ACCURACY).
//  Le MFP tourne à 2457600 Hz, le CPU à 8021248 Hz : ratio EXACT 31333/9600
//  (même conversion entière qu'Hatari cycInt.c, sans flottant).
// -----------------------------------------------------------------------------
int64_t Mfp::timerPeriodCycles(int timer) const {
    static constexpr int kDiv[8] = {0, 4, 10, 16, 50, 64, 100, 200};   // prescalers MFP
    int ctrl, data;
    switch (timer) {
        case 0: ctrl =  timer_[0x19] & 0x0F;       data = timer_[0x1F]; break;  // A
        case 1: ctrl =  tbcr_ & 0x0F;              data = tbReload_;    break;  // B
        case 2: ctrl = (timer_[0x1D] >> 4) & 0x07; data = timer_[0x23]; break;  // C
        case 3: ctrl =  timer_[0x1D] & 0x07;       data = timer_[0x25]; break;  // D
        default: return 0;
    }
    if (ctrl < 1 || ctrl > 7) return 0;       // 0 = arrêté ; 8+ = event-count/pulse (pas délai)
    const int count = data ? data : 256;      // données = 0 → 256
    const int64_t mfpCycles = static_cast<int64_t>(kDiv[ctrl]) * count;
    return mfpCycles * 31333 / 9600;          // MFP → cycles CPU
}

// Port de MFP_ReadTimer_AB/CD (Hatari) : en mode délai actif, le registre de
// données reflète le COMPTEUR qui décompte (data → 1 → recharge), pas la valeur
// de recharge figée. On le reconstruit depuis les cycles CPU restants avant
// l'IRQ programmée : count = ceil(cyclesMfpRestants / prescaler), avec la
// conversion CPU→MFP inverse de timerPeriodCycles (× 9600 / 31333).
uint8_t Mfp::readTimerData(int timer) const {
    static constexpr int kDiv[8] = {0, 4, 10, 16, 50, 64, 100, 200};   // prescalers MFP
    int ctrl;
    Scheduler::Source src;
    switch (timer) {
        case 0: ctrl =  timer_[0x19] & 0x0F;       src = Scheduler::TIMER_A;       break;  // TACR
        case 1: ctrl =  tbcr_ & 0x0F;              src = Scheduler::TIMER_B_DELAY; break;  // TBCR
        case 2: ctrl = (timer_[0x1D] >> 4) & 0x07; src = Scheduler::TIMER_C;       break;  // TCDCR(C)
        case 3: ctrl =  timer_[0x1D] & 0x07;       src = Scheduler::TIMER_D;       break;  // TCDCR(D)
        default: return 0;
    }
    // Mode délai (ctrl 1-7) ET échéance armée → compteur vivant (MFP_CYCLE_TO_REG).
    if (sched_ && ctrl >= 1 && ctrl <= 7) {
        const int64_t remCpu = sched_->cyclesUntil(src);
        if (remCpu >= 0) {
            const int64_t remMfp = remCpu * 9600 / 31333;          // cycles CPU → MFP
            const int     div    = kDiv[ctrl];
            const int64_t count  = (remMfp + div - 1) / div;       // ceil (round vers le haut)
            return static_cast<uint8_t>(count & 0xFF);             // 256 → 0
        }
    }
    // event-count (A/B, ctrl=8) → compteur suivi par hblank()/timerA_setLineInput() ;
    // timer à l'arrêt → la recharge (== compteur courant) dans le backing store.
    switch (timer) {
        case 0: return taCounter_;
        case 1: return tbCounter_;
        case 2: return timer_[0x23];
        default:return timer_[0x25];
    }
}

void Mfp::scheduleTimer(int timer) {
    // Programmation FRAÎCHE (écriture TxCR/TxDR) : ancrée sur l'horloge live, le
    // cycle absolu EXACT de l'écriture (et non le début du quantum) — un timer
    // programmé en plein bloc CPU démarre à l'instant réel, comme Hatari
    // (CycInt_AddRelativeInterrupt depuis l'horloge immédiate). La préemption du
    // Scheduler coupe alors le bloc pour servir l'IRQ à temps.
    scheduleTimerAt(timer, sched_ ? sched_->liveNow() : 0);
}

void Mfp::scheduleTimerAt(int timer, int64_t anchor) {
    if (!sched_) return;
    // Timer B (timer==1) : seul le mode DÉLAI (TBCR 1-7) est daté ici ; en event-count
    // (TBCR=8) timerPeriodCycles renvoie 0 → on annule la source délai (le tic est
    // alors piloté par Machine via mfp.hblank()).
    const Scheduler::Source src = timer == 0 ? Scheduler::TIMER_A
                                : timer == 1 ? Scheduler::TIMER_B_DELAY
                                : timer == 2 ? Scheduler::TIMER_C
                                :              Scheduler::TIMER_D;
    const int64_t period = timerPeriodCycles(timer);
    if (period <= 0) { sched_->cancel(src); return; }   // arrêté / event-count
    // Échéance = ancre + période. Pour une programmation fraîche, ancre = maintenant
    // → maintenant + période. Pour une replanification périodique, ancre = échéance
    // servie → échéance + période, ce qui ABSORBE le dépassement de latence d'IRQ
    // (overshoot) au lieu de l'ajouter à chaque tour : pas de dérive.
    int64_t next = anchor + period;
    const int64_t now = sched_->liveNow();
    if (next <= now) {
        // Retard ≥ une période entière (cas rare : on a sauté des échéances) : on
        // réaligne sur la grille d'origine sans tirer une rafale d'IRQ en retard —
        // équivalent du modulo sur PendingCyclesOver d'Hatari (≤ une période).
        const int64_t over = (now - anchor) % period;
        next = now + period - over;
    }
    sched_->schedule(src, next);
}

void Mfp::onTimerExpire(int timer) {
    static constexpr int kSrc[4] = {SRC_TIMERA, SRC_TIMERB, SRC_TIMERC, SRC_TIMERD};
    // L'IRQ est ANTIDATÉE de l'échéance réelle du timer (et non de l'horloge live,
    // en retard de la latence de dispatch) — port d'Interrupt_Delayed_Cycles
    // (mfp.c:1741+) : le délai de visibilité de 4 cycles court depuis l'expiration
    // matérielle du timer, pas depuis le moment où l'émulateur a servi l'événement.
    const int64_t due = sched_ ? sched_->firingDue() : -1;
    const int64_t when = due >= 0 ? due : (sched_ ? sched_->liveNow() : 0);
    raiseAt(kSrc[timer], when);               // lève l'IRQ (si le canal est activé)
    // Relance la période ANCRÉE sur l'échéance qui vient d'expirer (port
    // PendingCyclesOver) : le prochain tic tombe à échéance+période, gommant la
    // latence d'IRQ. Repli sur l'horloge live si l'échéance n'est pas disponible.
    scheduleTimerAt(timer, when);
}

void Mfp::hblank() {
    // En mode event-count (TBCR bits 0-3 == 0x08), Timer B décompte d'une unité
    // par ligne ; à 0 il recharge et lève l'IRQ Timer B (canal 8) si armée.
    if ((tbcr_ & 0x0F) != 0x08 || tbCounter_ == 0) return;
    if (--tbCounter_ == 0) {
        tbCounter_ = tbReload_;
        raise(SRC_TIMERB);
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
    if (a == xsint_) return;                       // pas de transition
    if (hasDmaSound_) {
        const bool pinOld = colorMonitor_ ^ xsint_;    // niveau GPIP7 avant
        const bool pinNew = colorMonitor_ ^ a;         // niveau GPIP7 après
        const bool aerBit = (aer & 0x80) != 0;
        if (pinOld != pinNew && pinNew == aerBit)      // front actif (cf. Hatari)
            raise(SRC_GPIP7);                          // canal 15 (IERA bit7) si armé
    }
    xsint_ = a;
}

// Octet des 8 lignes d'ENTRÉE du GPIP (actives BAS), tel que le voit le détecteur de
// front. Identique au calcul de read8($FFFA01) avant application du DDR.
uint8_t Mfp::gpipInput() const {
    uint8_t v = 0xFF;                            // bits au repos (haut)
    bool bit7 = colorMonitor_;                   // moniteur : couleur=1, mono=0
    if (hasDmaSound_) bit7 ^= xsint_;            // STE/Mega STE : XOR ligne XSINT son DMA
    if (!bit7)          v &= ~0x80;              // bit7 = moniteur^XSINT
    if (riLine_)        v &= ~0x40;              // bit6 = RS232 RI
    if (fdcLine_)       v &= ~0x20;              // bit5 = FDC
    if (aciaLineKbd_ || aciaLineMidi_) v &= ~0x10;  // bit4 = ACIA clavier OU MIDI (wire-OR)
    if (gpuLine_)       v &= ~0x08;              // bit3 = blitter GPU_DONE
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
    static constexpr int kChan[8] = {0, 1, 2, 3, 6, 7, 14, 15};   // bit GPIP → canal MFP
    const uint8_t stateOld = gpipOld ^ aerOld;
    const uint8_t stateNew = gpipNew ^ aerNew;
    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t m = uint8_t(1u << bit);
        if ((ddr & m) == 0                          // ligne configurée en ENTRÉE
         && (stateOld & m) != (stateNew & m)        // l'état (GPIP^AER) a basculé
         && (gpipNew & m) == (aerNew & m))          // front ACTIF (AER == niveau GPIP)
            raise(kChan[bit]);
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
    updateIrq(0);                         // 0 → front daté de pendingTime_[canal élu]
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
        if (sched_->liveNow() < visibleAt) sched_->schedule(Scheduler::MFP_IRQ, visibleAt);
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
