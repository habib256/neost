// =============================================================================
//  Mfp.cpp — Logique d'interruption du MC68901.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Mfp.hpp"
#include <cstdio>

// Les registres MFP sont sur les adresses IMPAIRES à partir de $FFFA00.
// On indexe par l'offset bas (addr & 0x3F).
uint8_t Mfp::read8(uint32_t addr) {
    switch (addr & 0x3F) {
        case 0x01: {                // GPIP : lignes d'ENTRÉE matérielles (les écritures
                                    // CPU sur $FFFA01 ne doivent pas les écraser).
            uint8_t v = 0xFF;            // bits au repos
            if (!colorMonitor_) v &= ~0x80;  // bit7 = 0 → moniteur MONO (haute rés)
            if (riLine_)        v &= ~0x40;  // bit6 = RS232 RI (actif bas)
            if (fdcLine_)       v &= ~0x20;  // bit5 = FDC (actif bas)
            if (aciaLine_)      v &= ~0x10;  // bit4 = ACIA clavier (actif bas)
            if (ctsLine_)       v &= ~0x04;  // bit2 = RS232 CTS (actif bas)
            if (dcdLine_)       v &= ~0x02;  // bit1 = RS232 DCD (actif bas)
            if (busyLine_)      v &= ~0x01;  // bit0 = Centronics BUSY (actif bas)
            return v;
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
        case 0x21: return tbCounter_; // Timer B data (compteur courant)
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
        case 0x01: gpip = v; break;
        case 0x03: aer  = v; break;
        case 0x05: ddr  = v; break;
        // Désactiver un canal (IER=0) efface aussi son interruption pendante.
        case 0x07: iera = v; ipra &= iera; break;
        case 0x09: ierb = v; iprb &= ierb; break;
        // IPR/ISR : on n'EFFACE que les bits écrits à 0 (les 1 laissent inchangé).
        case 0x0B: ipra &= v; break;
        case 0x0D: iprb &= v; break;
        case 0x0F: isra &= v; break;
        case 0x11: isrb &= v; break;
        case 0x13: imra = v; break;
        case 0x15: imrb = v; break;
        case 0x17: vr   = v; break;
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

void Mfp::scheduleTimer(int timer) {
    if (!sched_) return;
    // Timer B (timer==1) : seul le mode DÉLAI (TBCR 1-7) est daté ici ; en event-count
    // (TBCR=8) timerPeriodCycles renvoie 0 → on annule la source délai (le tic est
    // alors piloté par Machine via mfp.hblank()).
    const Scheduler::Source src = timer == 0 ? Scheduler::TIMER_A
                                : timer == 1 ? Scheduler::TIMER_B_DELAY
                                : timer == 2 ? Scheduler::TIMER_C
                                :              Scheduler::TIMER_D;
    const int64_t period = timerPeriodCycles(timer);
    if (period > 0) sched_->schedule(src, sched_->now() + period);
    else            sched_->cancel(src);      // arrêté / event-count → plus d'échéance délai
}

void Mfp::onTimerExpire(int timer) {
    static constexpr int kSrc[4] = {SRC_TIMERA, SRC_TIMERB, SRC_TIMERC, SRC_TIMERD};
    raise(kSrc[timer]);                       // lève l'IRQ (si le canal est activé)
    scheduleTimer(timer);                     // relance la période (mode délai)
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

void Mfp::timerA_eventCount() {
    // Idem pour Timer A en event-count (TACR bits0-3 == 0x08) : une impulsion sur
    // TAI (fin de trame son DMA STE) décompte ; à 0, recharge et lève l'IRQ.
    if ((timer_[0x19] & 0x0F) != 0x08 || taCounter_ == 0) return;
    if (--taCounter_ == 0) {
        taCounter_ = taReload_;
        raise(SRC_TIMERA);
    }
}

void Mfp::raise(int source) {
    if (source >= 8) {
        const uint8_t bit = uint8_t(1u << (source - 8));
        if (iera & bit) ipra |= bit;     // l'IRQ ne devient pendante que si activée
    } else {
        const uint8_t bit = uint8_t(1u << source);
        if (ierb & bit) iprb |= bit;
    }
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

bool Mfp::irqPending() const {
    const int s = highestPending();
    if (s < 0) return false;
    // En mode "software EOI" (VR bit3), une source en service bloque les sources
    // de priorité ≤ tant que son bit ISR n'est pas effacé par le handler.
    if (vr & 0x08) return s > highestInService();
    return true;
}

int Mfp::iack() {
    const int s = highestPending();
    if (s < 0) return -1;
    const uint8_t bit = uint8_t(1u << (s & 7));
    if (s >= 8) { ipra &= ~bit; if (vr & 0x08) isra |= bit; }
    else        { iprb &= ~bit; if (vr & 0x08) isrb |= bit; }
    return (vr & 0xF0) | s;               // vecteur MFP
}
