// =============================================================================
//  Mfp.hpp — MC68901 (Multi-Function Peripheral) de l'Atari ST.
//
//  Le MFP est le contrôleur d'interruptions vectorisées du ST (4 timers, GPIP,
//  USART). Toutes les IRQ "intelligentes" passent par lui et ressortent en
//  IPL 6 vers le 68000. Le modèle est quasi 1:1 avec `mfp.c` d'Hatari :
//    - les QUATRE timers A-D, en mode délai (datés par le Scheduler) comme en
//      event-count (Timer A sur XSINT, Timer B sur le Display-Enable vidéo) ;
//    - le GPIP complet avec sa machine de fronts AER/DDR (gpipSetLine /
//      gpipUpdateInterrupt = port de MFP_GPIP_Set_Line_Input) ;
//    - l'USART (RS-232 : débit, RxFull/TxEmpty, injection hôte modem) ;
//    - la logique d'interruption complète : IER/IPR/IMR/ISR + registre vecteur
//      (VR), modes auto / "software end-of-interrupt", et le cycle IACK.
//
//  Numéro de canal = numéro de source MFP (0..15). Vecteur = (VR & 0xF0) | canal.
//  Registre A = sources 8..15 (bits 0..7), registre B = sources 0..7 (bits 0..7).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <deque>

#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"

class Mfp {
public:
    static constexpr int SRC_DCD    = 1;   // RS232 DCD (GPIP1)
    static constexpr int SRC_CTS    = 2;   // RS232 CTS (GPIP2)
    static constexpr int SRC_GPU    = 3;   // Blitter GPU_DONE (GPIP3) — Mega ST/STE/Mega STE
    static constexpr int SRC_TIMERD = 4;   // Timer D (RS232 baud / délai)
    static constexpr int SRC_TIMERC = 5;   // tic système 200 Hz (délai)
    static constexpr int SRC_ACIA   = 6;   // clavier/MIDI (GPIP4)
    static constexpr int SRC_FDC    = 7;   // FDC/DMA disquette (GPIP5)
    static constexpr int SRC_TIMERB = 8;   // Timer B (synchro vidéo / event-count)
    static constexpr int SRC_TXERR  = 9;   // USART Transmit Error (underrun)
    static constexpr int SRC_TXEMPTY = 10; // USART Transmit Buffer Empty (RS232)
    static constexpr int SRC_RXERR  = 11;  // USART Receive Error (overrun, etc.)
    static constexpr int SRC_RXFULL = 12;  // USART Receive Buffer Full (RS232)
    static constexpr int SRC_TIMERA = 13;  // Timer A (souvent musique/délai)
    static constexpr int SRC_RI     = 14;  // RS232 Ring Indicator (GPIP6)
    static constexpr int SRC_GPIP7  = 15;  // GPIP7 : moniteur XOR XSINT son DMA (STE)

    // Branche l'ordonnanceur : le MFP y date lui-même ses timers en mode délai
    // (Timer A/C/D) à partir de leurs registres prescaler/données.
    void setScheduler(Scheduler* s) { sched_ = s; }
    // Liaison vidéo non persistée : appelée avant changement AER3, pour servir
    // l'ancien front DE puis repositionner le prochain (Hatari mfp.c2742+).
    void setTimerBEdgeSink(std::function<void(bool)> sink) { timerBEdgeSink_ = std::move(sink); }

    // RESET matériel du MFP (port de MFP_Reset, mfp.c:519-569). Remet à zéro les
    // registres d'interruption (GPIP/AER/DDR, IER/IPR/IMR/ISR, VR), les timers (mode,
    // recharge, compteurs, backing store) et annule les échéances en attente → plus
    // d'IRQ Timer A / GPIP7 « fantôme » survivant à un reset à chaud. NE TOUCHE PAS aux
    // propriétés machine/fixture (moniteur, son DMA, bouclage) ni aux lignes d'ENTRÉE
    // pilotées par les autres puces (reforcées à la lecture, puces resynchronisées).
    void reset();

    // RESET *partiel* du MFP — port STRICT de MFP_Reset (mfp.c:519-569), celui que
    // la broche /RESET du 68000 déclenche (customreset → MFP_Reset_All). Ne touche
    // QUE les registres MFP + les échéances de timers ; l'USART (RSR/UDR/UCR/débit),
    // la ligne XSINT et les lignes RS232 en entrée ne sont PAS remises (Hatari ne les
    // touche pas dans MFP_Reset). reset() (reset MACHINE) l'appelle puis élargit.
    void resetChip();

    // Partie entière de la période en CYCLES CPU d'un timer (0..3 = A/B/C/D) en
    // mode délai, ou 0 s'il est arrêté/event-count. La planification conserve en
    // interne les 8 bits fractionnaires (comme CYCINT_SHIFT d'Hatari), donc les
    // rechargements successifs ne dérivent pas de cette valeur arrondie.
    // `fromCounter` : période calculée depuis le COMPTEUR courant
    // (démarrage/continuation) plutôt que depuis la valeur de RECHARGE.
    int64_t timerPeriodCycles(int timer, bool fromCounter) const;
    void    scheduleTimer(int timer);
    // Programme l'échéance d'un timer (mode délai) à `anchor + période`. `anchor` =
    // horloge live pour une programmation fraîche, ou l'échéance servie pour une
    // replanification périodique anti-dérive (cf. onTimerExpire / PendingCyclesOver).
    void    scheduleTimerAt(int timer, int64_t anchor, bool fromCounter);

    // Contrôle BRUT d'un timer (TACR/TBCR 0-15, moitiés de TCDCR 0-7).
    int     timerCtrl(int timer) const;
    // Écriture d'un registre de contrôle (port des MFP_TimerXCtrl_WriteByte) :
    // valeur inchangée → aucun effet (ne redate PAS un timer qui court) ; arrêt
    // d'un délai → fige le compteur courant (relisible, continuation possible) ;
    // démarrage → échéance calculée depuis le COMPTEUR (pas la recharge).
    void    writeTimerCtrl(int timer, uint8_t newCtrl);
    // Fige le compteur vivant d'un délai qu'on arrête (port MFP_ReadTimerX(stopping)).
    void    storeStoppedCounter(int timer);
    // Référence du compteur (backing store) d'un timer.
    uint8_t& timerCounterRef(int timer);

    // Valeur lue dans le registre de données d'un timer (0..3 = A/B/C/D). En mode
    // DÉLAI actif, renvoie le COMPTEUR VIVANT (décompté depuis l'écriture, calculé
    // d'après les cycles restants avant l'IRQ) et non la valeur de recharge — port
    // de MFP_ReadTimer_AB/CD (Hatari). En event-count (A/B) ou à l'arrêt, le
    // compteur suivi (taCounter_/tbCounter_) ou la recharge convient déjà.
    uint8_t readTimerData(int timer) const;
    // Échéance atteinte : lève l'IRQ du timer et le replanifie (mode délai).
    void    onTimerExpire(int timer);

    // Événement HBLANK (une fois par ligne) : fait décompter Timer B en mode
    // event-count. TOS 1.x s'en sert pour se synchroniser à l'écran au boot.
    void hblank();
    void hblankAt(int64_t when);

    // Front compté par Timer B en event-count (AER bit3 de $FFFA03) : 0 = FINS de
    // ligne (défaut, DE_end+24), 1 = DÉBUTS de ligne (DE_start+24) — cf. Hatari
    // Video_TimerB_GetPosFromDE (« Seven Gates of Jambala »). Le compte (1/ligne
    // visible) est inchangé ; seule la POSITION du tic dans la ligne change.
    bool timerBStartOfLine() const { return (aer & 0x08) != 0; }

    // Ligne d'entrée TAI du Timer A (sur STE = ligne XSINT du son DMA, niveau HAUT
    // pendant une trame). Port de MFP_TimerA_Set_Line_Input : en event-count (TACR
    // bits0-3 == 0x08), on compte sur le FRONT sélectionné par l'AER GPIP4 (bit4) —
    // par défaut AER bit4=0 → on compte les transitions vers 0 (= fins de trame son
    // DMA). À 1, le compteur recharge (TADR) et lève l'IRQ Timer A (canal 13) ; sinon
    // il décrémente (data reg 0 = 256 via le wrap). Sert au double-buffering audio STE.
    void timerA_setLineInput(bool bit);

    uint8_t read8(uint32_t addr);
    void    write8(uint32_t addr, uint8_t v);

    // Sert les timers délai ÉCHUS avant l'accès registre en cours (≙ MFP_UpdateTimers,
    // mfp.c:681). Sans lui, un acquittement d'ISR peut travailler sur un ISR périmé —
    // le cas « Super Hang On » nommé par Hatari, cf. Scheduler::runMfpTimersTo.
    void updateTimers();

    // Élection GROUPÉE de l'IRQ (≙ MFP_UpdateNeeded / MFP_UpdateIRQ, mfp.c:1125 et
    // newcpu.c:3005,5509…). Une ENTRÉE d'interruption ne déclenche PAS l'élection : elle
    // pose seulement son bit pending et sa date, puis marque l'élection « à faire ».
    // C'est ce qui permet à la règle chronologique (`pendingTime_ <= pendingTimeMin_`)
    // de départager PLUSIEURS entrées d'une même instruction — Hatari :
    // « we must call MFP_UpdateIRQ() only later […] when all inputs were received, to
    // choose the oldest input's event time ». Une ÉCRITURE registre, elle, élit
    // immédiatement avec le cycle d'écriture (cf. write8).
    // NON SÉRIALISÉ À DESSEIN : transitoire à l'intérieur d'une instruction, et toujours
    // consommé par le calcul d'IPL qui suit tout dispatch. La sérialiser imposerait un
    // bump du format de save-state pour un drapeau qui vaut faux à toute frontière.
    void flushIrqUpdate();
    // Pendant un LOT DE DISPATCH, l'élection est SUSPENDUE : les entrées s'accumulent et
    // `flushIrqUpdate()` ne tranche pas. Machine l'arme sur les bornes du Scheduler.
    // Hors lot, le flush reste immédiat au calcul d'IPL — un raise venu d'un chemin qui
    // ne passe pas par le dispatch (écriture bus d'une autre puce) ne peut donc pas
    // rester non élu.
    void setInDispatch(bool b) { inDispatch_ = b; }

    // Fin d'accès d'une ÉCRITURE registre MFP (cf. Mfp.cpp) : c'est cet instant qui
    // date une IRQ née de l'écriture, pas le début de l'accès.
    int64_t writeEventTime() const;
    bool    irqUpdateNeeded_ = false;   // cf. flushIrqUpdate (non sérialisé)
    bool    inDispatch_ = false;        // cf. setInDispatch (non sérialisé, transitoire)
    // Décalage « début → fin » d'un accès MMIO, en cycles bus (cf. Cpu68k::cyclesIntoInstr :
    // Moira a déjà facturé le SYNC(2) de tête, la fin d'accès est donc à +2).
    // Verrou d'A/B : NEOST_MFP_WRITE_END (défaut 2 ; =0 restaure l'ancienne datation).
    static int64_t mmioWriteEnd();

    // Auto-test DÉTERMINISTE du MFP : (a) bits d'ENTRÉE GPIP forcés à la lecture
    // (moniteur bit7, FDC bit5, ACIA bit4 en wire-OR) ; (b) détection de FRONT
    // (gpipSetLine lève le canal seulement sur le front sélectionné par l'AER, ligne en
    // ENTRÉE) ; (c) Timer B event-count fin/début de ligne selon AER bit3. Sans boot ni
    // ordonnanceur temps réel. Renvoie true si OK ; détaille sur stderr. Appelé par
    // neost-headless --mfp-selftest.
    bool mfpSelfTest();

    // Sérialisation save-state SYMÉTRIQUE (save ET load). Défini INLINE pour l'accès
    // aux membres privés. Transfère TOUT l'état runtime : registres MMIO, état des
    // timers (compteurs vivants + recharges), USART/série, latches d'interruption
    // (IPR/ISR + chronologie pendingTime_), lignes d'ENTRÉE GPIP et champs dérivés
    // persistants. NE TOUCHE PAS aux liaisons moteur (sched_, serialSink_) ni aux
    // static/constexpr. Note : le MFP n'a PAS de FIFO série (deque) — buffer 1 octet.
    // Fin de caractère TX : sortie hôte et bouclage, puis transfert TDR→TSR.
    // Deux registres au plus, sans FIFO ; le TDR plein est écrasé par UDR.
    // Le transport sans Scheduler conserve le raccourci instantané des fixtures.
    void onSerialTxEvent();
    void serialize(StateArchive& ar) {
        // --- Registres d'interruption exposés au débogueur (publics) -----------------
        ar(gpip); ar(aer); ar(ddr);
        ar(iera); ar(ierb);
        ar(ipra); ar(iprb);
        ar(imra); ar(imrb);
        ar(isra); ar(isrb);
        ar(vr);

        // --- Lignes d'ENTRÉE GPIP + fixtures machine (persistent inter-trame) ---------
        ar(aciaLineKbd_); ar(aciaLineMidi_);
        ar(fdcLine_);
        ar(gpuLine_);
        ar(colorMonitor_);
        ar(hasDmaSound_);
        ar(xsint_);
        ar(busyLine_);
        ar(ctsLine_);
        ar(dcdLine_);
        ar(riLine_);

        // --- USART / réception série (buffer 1 octet, pas de FIFO) --------------------
        ar(rxByte_);
        ar(rxFull_);
        ar(rxOverrun_);
        ar(rxPendingOverrun_);
        ar.check(!rxPendingOverrun_ || (rxFull_ && !rxOverrun_), "Mfp::pending RX overrun");
        // loopback_ (bouchon RS-232) N'EST PLUS sérialisé depuis v51 : propriété de
        // banc posée par le frontal, comme les bouchons MIDI (MidiAcia) et SCC.

        // --- Timers B/C/D : mode, recharge et compteurs figés ------------------------
        ar(tbcr_); ar(tbReload_); ar(tbCounter_);
        ar(tcCounter_); ar(tdCounter_);

        // Échéances absolues en sous-cycles CPU (1/256) : la fraction MFP→CPU doit
        // survivre au save/load, sinon la grille du timer saute d'un cycle après la
        // reprise et peut déplacer une IRQ raster ou musicale.
        ar(timerDueSub_);
        for (int i = 0; i < 4; ++i)
            ar.check(timerDueSub_[i] >= 0 && timerDueSub_[i] <= INT64_MAX - 255,
                     "Mfp::timerDueSub_ hors bornes");

        // --- Timer A event-count (ligne TAI = XSINT son DMA STE) ---------------------
        ar(taReload_); ar(taCounter_);
        ar(tai_);

        // --- Backing store des registres timer/USART ($FF..) -------------------------
        ar(timer_);   // C-array uint8_t[0x40]

        // --- Signal IRQ daté (chronologie + délai de propagation) --------------------
        ar(irq_);
        ar(irqTime_);
        ar(currentInt_);
        ar(pendingTime_);      // C-array int64_t[16]
        ar(pendingTimeMin_);

        // --- Config USART effective (dérivée du Timer D / UCR) -----------------------
        ar(serialBaud_);
        ar(serialUcr_);
        ar(serialClockFactor_);
        ar(serialRxMask_);
        ar.check(serialRxMask_ == 0xFF || serialRxMask_ == 0x7F ||
                 serialRxMask_ == 0x3F || serialRxMask_ == 0x1F,
                 "Mfp::serialRxMask_");
        ar.check(serialClockFactor_ > 0 && serialClockFactor_ <= 819200,
                 "Mfp::serialClockFactor_");
        ar.check(serialBaud_ >= 0 && serialBaud_ <= 307200, "Mfp::serialBaud_");

        // --- File RX côté hôte (v10) : deque via vector, comme MidiAcia --------------
        {
            std::vector<uint8_t> q(hostRx_.begin(), hostRx_.end());
            ar.vec(q);
            if (ar.loading()) {
                ar.check(q.size() <= kHostRxMax, "Mfp::hostRx_ au-delà de kHostRxMax");
                hostRx_.assign(q.begin(), q.end());
            }
        }
        ar(serialTxBusy_); ar(serialTxFull_); ar(serialTxUnderrun_); ar(serialTxEnd_);
        ar(serialTxKind_);
        ar.check(serialTxKind_ <= 2, "Mfp::serialTxKind_");
        ar.check(serialTxKind_ == 0 || serialTxBusy_, "Mfp::serialTxKind_ sans échéance");
        ar(serialTxByte_); ar(serialTxHolding_);
        ar(serialTxCycle_); ar(serialTxRemainder_); ar(serialTxDivisor_);
        ar(serialTxFrameClearCycle_);
        ar.check(serialTxFrameClearCycle_ >= 0 &&
                 serialTxFrameClearCycle_ <= INT64_MAX - 1000000000,
                 "Mfp::serialTxFrameClearCycle_");
        ar(serialTxUnderrunClearCycle_);
        ar.check(serialTxUnderrunClearCycle_ >= 0 &&
                 serialTxUnderrunClearCycle_ <= INT64_MAX - 1000000000,
                 "Mfp::serialTxUnderrunClearCycle_");
        ar.check(serialTxCycle_ >= 0 && serialTxCycle_ <= INT64_MAX - 1000000000,
                 "Mfp::serialTxCycle_");
        ar.check(serialTxDivisor_ > 0 && serialTxDivisor_ <= 2457600,
                 "Mfp::serialTxDivisor_");
        ar.check(serialTxRemainder_ >= 0 && serialTxRemainder_ < serialTxDivisor_,
                 "Mfp::serialTxRemainder_");
        ar(serialRxArmed_);
        ar(serialRxCycle_);
        ar(serialRxRemainder_);
        ar(serialRxDivisor_);
        ar.check(serialRxCycle_ >= 0 && serialRxCycle_ <= INT64_MAX - 200000000,
                 "Mfp::serialRxCycle_");
        ar.check(serialRxDivisor_ > 0 && serialRxDivisor_ <= 2457600,
                 "Mfp::serialRxDivisor_");
        ar.check(serialRxRemainder_ >= 0 && serialRxRemainder_ < serialRxDivisor_,
                 "Mfp::serialRxRemainder_");
    }

    // Lignes d'interruption des DEUX ACIA (clavier ET MIDI), câblées en WIRE-OR
    // sur la MÊME entrée GPIP4 (active BAS). Cf. Hatari MFP_Main_Compute_GPIP_LINE_ACIA
    // (acia.c : « the 2 ACIA's IRQ pins are connected to the same MFP input »).
    // Chaque ACIA a sa propre ligne : GPIP4 reste asserté (bit4=0) tant que l'UNE
    // OU l'AUTRE a un octet en attente — sinon un MIDI inactif effaçait une IRQ
    // clavier pendante (clobber). Le handler _int_acia d'EmuTOS lit GPIP bit4 pour
    // savoir quand cesser de vider l'ACIA, AVANT d'effacer son bit in-service.
    // Chaque setter applique la règle de FRONT d'Hatari (MFP_GPIP_Set_Line_Input) :
    // le canal GPIP correspondant n'est levé QUE sur une transition du pin dont le
    // nouveau niveau égale le bit AER (DDR=entrée exigé) — les appelants ne doivent
    // PAS appeler raise() eux-mêmes (sinon IPR reposé sans front réel : une ligne
    // déjà basse — wire-OR ACIA, INTRQ maintenue — regénérerait des IRQ fantômes).
    void setAciaLineKbd (bool active) { gpipSetLine(aciaLineKbd_,  active); }
    void setAciaLineMidi(bool active) { gpipSetLine(aciaLineMidi_, active); }
    // Front série servi en retard : conserver son échéance sans court-circuiter
    // le détecteur AER/DDR ni le wire-OR des deux ACIA.
    void setAciaLineKbdAt(bool active, int64_t when) {
        if (aciaLineKbd_ == active) return;
        const uint8_t before = gpipInput();
        aciaLineKbd_ = active;
        gpipUpdateInterruptAt(before, gpipInput(), aer, aer, when);
    }
    void setAciaLineMidiAt(bool active, int64_t when) {
        if (aciaLineMidi_ == active) return;
        const uint8_t before = gpipInput();
        aciaLineMidi_ = active;
        gpipUpdateInterruptAt(before, gpipInput(), aer, aer, when);
    }
    // Alias de compatibilité (l'ancienne API ne pilotait qu'une seule ligne).
    void setAciaLine(bool active) { setAciaLineKbd(active); }

    // Ligne d'interruption du FDC sur GPIP5 (active BAS). EmuTOS attend la fin
    // d'une commande disque en pollant GPIP bit5 (timeout_gpip).
    void setFdcLine(bool active) { gpipSetLine(fdcLine_, active); }

    // Ligne GPU_DONE du blitter sur GPIP3 (active BAS). Le blitter la met HAUT au
    // démarrage puis BAS à la fin du transfert (cf. Hatari Blitter_Start). N'existe
    // que sur Mega ST/STE/Mega STE (le blitter n'est câblé au bus que sur ces modèles).
    // `done` est INVERSÉ vers le niveau de registre : gpuLine_ = 1 pendant le blit
    // (comme le bit 3 du GPIP d'Hatari), 0 au repos/fini — le front 1→0 de la fin
    // lève le canal 3 sous AER=0. Reset : 0 tant qu'aucun blit n'a tourné (INT du
    // blitter = copie du bit BUSY, au repos bas). Sans blitter : cf. hasBlitter_.
    void setBlitterLine(bool done) { gpipSetLine(gpuLine_, !done); }

    // Type de moniteur lu sur GPIP bit7 : couleur (basse rés) ou mono (haute rés).
    // À changer AVANT un reset pour que TOS détecte la bonne résolution au boot.
    void setColorMonitor(bool c) { colorMonitor_ = c; }

    // Présence du son DMA (STE / Mega STE) : seul ce drapeau autorise le XOR de la
    // ligne XSINT dans GPIP7 (cf. Hatari MFP_Main_Compute_GPIP7, réservé aux STE/TT).
    // Posé par DmaSound::setMfp ; sur un ST sans son DMA il reste faux → GPIP7 ne
    // dépend QUE du moniteur, exactement comme avant.
    void setHasDmaSound(bool h) { hasDmaSound_ = h; }
    // Présence du blitter (dérivée du modèle, cf. machineHasBlitter) : sans blitter,
    // GPIP3 n'est tirée que par la résistance de rappel R4 → lue à 1 (cf. gpipInput).
    void setHasBlitter(bool h) { hasBlitter_ = h; }
    // Ligne XSINT du son DMA STE (cf. Hatari DmaSnd_Update_XSINT_Line) : HAUT pendant
    // la lecture d'une trame, BAS sinon. Sur STE elle est XORée avec la détection
    // moniteur pour former GPIP7. Toute transition peut lever le canal GPIP7 (I7) si
    // armé (IERA bit7) et que le front correspond à l'AER — comme une entrée GPIP.
    void setXsintLine(bool a);

    // Récepteur du port série (RS-232) : chaque octet écrit dans l'UDR ($FFFA2F)
    // y est transmis. Les ROMs de diagnostic y impriment leur rapport quand la
    // vidéo n'est pas (encore) opérationnelle.
    void setSerialSink(std::function<void(uint8_t)> sink) { serialSink_ = std::move(sink); }
    // Injection RX série côté hôte (modem Hayes) : octets mis en
    // file et livrés au débit configuré via l'ordonnanceur (Scheduler::SERIAL_RX),
    // IRQ RxFull (canal 12) par octet. Cf. Mfp.cpp § Injection RX série.
    void receiveByte(uint8_t b);
    void onSerialRxEvent();                       // échéance SERIAL_RX (Machine)
    std::size_t hostRxPending() const { return hostRx_.size(); }
    bool colorMonitor() const { return colorMonitor_; }

    // Config EFFECTIVE de l'USART (port de rs232.c RS232_SetBaudRateFromTimerD +
    // RS232_HandleUCR) : bauds dérivés du Timer D (horloge de l'USART, prescaler
    // /16 de l'UCR) et format du mot depuis l'UCR. Comme chez Hatari c'est de la
    // pure CONFIGURATION (appliquée au tty hôte chez lui, état + journal ici) : le
    // débit d'émission émulé reste instantané (cf. RS232_TSR_ReadByte). Recalculée
    // à chaque écriture UCR ($FFFA29), TDDR ($FFFA25) ou TCDCR ($FFFA1D).
    int     serialBaud() const { return serialBaud_; }   // 0 = jamais configurée
    uint8_t serialUcr()  const { return serialUcr_; }

    // Lignes de contrôle RS232 en ENTRÉE (vues sur le GPIP, actives BAS) :
    //   CTS = GPIP2, DCD = GPIP1, RI = GPIP6.
    // Sur l'ST elles sont pilotées par un périphérique externe ; avec un connecteur
    // de BOUCLAGE elles recopient les sorties RTS/DTR du PSG (cf. Machine). On lève
    // aussi le canal MFP correspondant si la ligne s'active (le test série peut
    // utiliser l'IRQ). true = ligne assertée (→ bit GPIP à 0).
    // Le test série « lignes de contrôle » ENABLE le canal GPIP (RI/DCD/CTS) puis
    // toggle la sortie (DTR/RTS) : l'EDGE sur l'entrée doit lever l'IRQ. On lève donc
    // le canal sur tout changement d'état. C'est sûr : ces setters ne sont appelés que
    // quand le connecteur est branché (cf. Machine, gardé par loopback()), donc jamais
    // au boot où cela parasiterait le canal ACIA du clavier.
    // Ligne BUSY Centronics (GPIP0, active BAS). Sous fixture de bouclage, recopie
    // (inversée) le bit7 du port parallèle (cf. Machine, test « Printer/Joystick »).
    // Hatari psg.c : le strobe imprimante passe par le détecteur GPIP0,
    // avec qualification AER/DDR comme les autres broches (MC68HC901 §5.1).
    void setBusyLine(bool a) { gpipSetLine(busyLine_, a); }
    // gpipSetLine (et non raise() direct) : il n'y a de front actif QUE dans le sens
    // choisi par l'AER, et seulement si la ligne est en ENTRÉE. Lever le canal sur les
    // DEUX fronts, comme le faisait ce code, donnait une IRQ sur la mauvaise transition
    // à tout programme qui arme IERB bit1/2 puis bascule RTS/DTR sous --loopback.
    void setRs232Cts(bool a) { gpipSetLine(ctsLine_, a); }
    void setRs232Dcd(bool a) { gpipSetLine(dcdLine_, a); }
    void setRs232Ri (bool a) { gpipSetLine(riLine_, a); }   // cf. CTS/DCD : front selon l'AER
    // Bouton « freeze » du Multiface ST : tire la ligne moniteur (GPIP7) à 0 le temps
    // de l'appui, QUEL que soit le moniteur — c'est le câble qui intercepte la prise
    // moniteur (cf. io/PortDevices.hpp). Front selon l'AER, comme toute entrée GPIP.
    void setMonitorButton(bool pressed) { gpipSetLine(monButton_, pressed); }
    // Recouvrement à la LECTURE de $FFFA01 (bits d'entrée, après DDR) par un
    // adaptateur (B.A.T. II force CTS, Music Master retarde DCD — cf. PortDevices).
    // Pas d'effet sur la détection de fronts : ces clés sont sondées, pas armées.
    void setGpipReadHook(std::function<void(uint8_t&)> h) { gpipHook_ = std::move(h); }

    // Déclenche une source : positionne le bit IPR si le canal est activé (IER), sinon
    // l'EFFACE (port MFP_InputOnChannel). Date l'événement à l'horloge live ; raiseAt
    // permet d'ANTIDATER (timer servi en retard → l'IRQ est datée de l'échéance réelle,
    // port Interrupt_Delayed_Cycles). Met à jour le signal IRQ interne (updateIrq).
    void raise(int source);
    void raiseAt(int source, int64_t when);

    // Une interruption MFP doit-elle être présentée au CPU (IPL 6) ? Tient compte du
    // DÉLAI DE PROPAGATION de 4 cycles du 68901 (port MFP_IRQ_DELAY_TO_CPU / MFP_GetIRQ_CPU,
    // mfp.c:374,783) : le signal IRQ levé au cycle T n'est visible du CPU qu'à T+4.
    // Quand le délai court encore, updateIrq a armé Scheduler::MFP_IRQ à T+4 pour que
    // l'IPL soit recalculé pile à l'instant où l'IRQ devient visible.
    bool irqPending() const;

    // Acquittement (cycle IACK) : RÉ-ÉVALUE d'abord le signal IRQ (port MFP_ProcessIACK,
    // mfp.c:812-854 — une IRQ plus prioritaire survenue entre le début de l'exception et
    // la lecture du vecteur PEUT remplacer le vecteur ; sous Moira, cycle-exact, l'appel
    // arrive bien ~12 cycles après le début de l'exception), puis renvoie le vecteur de
    // currentInt_, met à jour IPR/ISR. -1 si plus rien en attente (spurious).
    int iack();

    // Registres exposés au débogueur.
    // GPIP : bit7 = détection moniteur (cf. EmuTOS shifter_get_monitor_type) :
    // bit7=1 → moniteur COULEUR (basse résolution, bureau couleur), bit7=0 → mono.
    uint8_t gpip = 0xFF, aer = 0, ddr = 0;
    uint8_t iera = 0, ierb = 0;   // enable
    uint8_t ipra = 0, iprb = 0;   // pending
    uint8_t imra = 0, imrb = 0;   // mask
    uint8_t isra = 0, isrb = 0;   // in-service
    uint8_t vr   = 0;             // vector register (bit3 = software EOI)
    bool    aciaLineKbd_  = false; // ligne ACIA clavier (true = octet dispo → GPIP4 bas)
    bool    aciaLineMidi_ = false; // ligne ACIA MIDI    (true = octet dispo → GPIP4 bas)
    bool    fdcLine_  = false;    // ligne FDC  (true = commande finie → GPIP5 bas)
    bool    gpuLine_  = false;    // ligne blitter GPU_DONE (true = blit fini → GPIP3 bas)
    bool    colorMonitor_ = true; // GPIP bit7 : true = couleur (basse rés)
    bool    hasDmaSound_  = false;// machine avec son DMA (STE/Mega STE) → XOR XSINT sur GPIP7
    bool    xsint_        = false;// ligne XSINT du son DMA STE (HAUT = trame en cours)
    bool    busyLine_ = false;    // Centronics BUSY (GPIP0, actif bas) — bouclage port parallèle
    // CTS et DCD sont ASSERTÉES au repos, donc GPIP bits 2 et 1 lus à 0 (« signal
    // actif ») : c'est la valeur par défaut d'Hatari quand aucun vrai port série n'est
    // branché (rs232.c:516-518 pose dcd = cts = 1, que mfp.c:1984-1993 traduit en bits
    // EFFACÉS). Les laisser désassertées faisait lire $FFFA01 = FF au lieu de A1, et un
    // logiciel qui attend CTS avant d'émettre aurait bouclé indéfiniment.
    bool    ctsLine_  = true;     // RS232 CTS (GPIP2, actif bas) — bouclage RTS
    bool    dcdLine_  = true;     // RS232 DCD (GPIP1, actif bas) — bouclage DTR
    // RI : ACTIVE BASSE comme CTS/DCD, mais repos DÉSASSERTÉ (bit6 = 1) — récepteur
    // MC1489 à entrée ouverte = sortie haute, cf. Mfp::gpipInput.
    bool    riLine_   = false;    // RS232 RI (GPIP6) : ACTIF BAS, repos DÉSASSERTÉ
    bool    hasBlitter_ = false;  // blitter câblé (Mega ST/STE/Mega STE) — sinon pull-up R4 sur GPIP3 (hors snapshot : dérivé du modèle)
    bool    monButton_ = false;   // Multiface : bouton freeze enfoncé → GPIP7 forcé à 0 (hors snapshot)
    std::function<void(uint8_t&)> gpipHook_;   // recouvrement lecture GPIP (PortDevices, hors snapshot)

    // USART (RS232) : tampon de réception 1 OCTET (le 68901 n'a PAS de FIFO ; un
    // nouvel octet est perdu si le précédent attend encore = overrun). rxFull_ = RSR bit7 (Buffer Full).
    // loopback_ = connecteur de bouclage BRANCHÉ (cf. setLoopback). Par défaut NON
    // branché : sinon l'écho du rapport série du diagnostic (imprimé en console)
    // reviendrait en réception et serait lu comme entrée terminal → le test clavier
    // échoue. Le câble n'est « branché » que pour tester le port série.
    uint8_t rxByte_  = 0;
    bool    rxFull_   = false;
    bool    rxOverrun_ = false;   // OE visible après lecture UDR, acquitté par RSR
    bool    rxPendingOverrun_ = false; // pertes retenues derrière un RDR encore plein
    // File RX côté hôte (cf. receiveByte) : bornée, livrée octet par octet au
    // débit série par l'événement SERIAL_RX. serialRxArmed_ = échéance en vol.
    static constexpr std::size_t kHostRxMax = 4096;
    std::deque<uint8_t> hostRx_;
    bool serialRxArmed_ = false;
    uint8_t serialRxMask_ = 0xFF; // largeur du caractère hôte déjà engagé
    int64_t serialRxCycle_ = 0;       // partie entière de la prochaine fin de caractère
    int64_t serialRxRemainder_ = 0;   // fraction exacte / serialRxDivisor_
    int serialRxDivisor_ = 1;
    bool internalSerialLoopback() const { return (timer_[0x2D] & 6) == 6; }
    void receiveSerialByte(uint8_t byte, int64_t when); // livraison commune hôte/bouclages
    void scheduleSerialRx(bool continuation = false);
    void startSerialTx(bool continuation);
    void scheduleSerialTxHalfBits(int halfBits, bool continuation);
    void advanceSerialTx(int64_t when);
    void finishSerialTx(int64_t when);
    bool serialTxBusy_ = false, serialTxFull_ = false;
    bool serialTxUnderrun_ = false, serialTxEnd_ = false;
    uint8_t serialTxByte_ = 0, serialTxHolding_ = 0;
    uint8_t serialTxKind_ = 0; // 0=donnée, 1=BREAK, 2=stop fin BREAK
    int64_t serialTxCycle_ = 0, serialTxRemainder_ = 0;
    int serialTxDivisor_ = 1;
    int64_t serialTxFrameClearCycle_ = 0; // seuil potentiel de la trame TSR courante
    int64_t serialTxUnderrunClearCycle_ = 0; // premier cycle autorisant clear UE
public:
    void setLoopback(bool plugged) { loopback_ = plugged; }
    bool loopback() const { return loopback_; }
private:
    bool    loopback_ = false;

    // Timer B (event-count sur HBLANK). tbCounter_ = compteur courant (lu en
    // $FFFA21), tbReload_ = valeur rechargée à 0, tbcr_ = mode ($FFFA1B).
    uint8_t tbcr_ = 0, tbReload_ = 0, tbCounter_ = 0;
    // Compteurs C/D figés (timer arrêté en plein décompte → relisible, et le
    // délai REPREND de là au prochain démarrage, cf. Hatari TC/TD_MAINCOUNTER).
    uint8_t tcCounter_ = 0, tdCounter_ = 0;

    // Timer A en event-count (TAI = ligne XSINT son DMA sur STE). Compteur courant
    // + valeur de recharge, chargés à l'écriture de TADR ($FFFA1F). tai_ = dernier
    // niveau de la ligne TAI (pour détecter les fronts, cf. timerA_setLineInput).
    uint8_t taReload_ = 0, taCounter_ = 0;
    bool    tai_ = false;

    // Grille absolue des timers délai A/B/C/D en unités de 1/256 cycle CPU.
    // 0 = pas de délai actif. Le Scheduler ne voit que ceil(timerDueSub_/256),
    // mais cette valeur exacte sert d'ancre au rechargement suivant : aucune
    // fraction MFP→CPU n'est perdue d'une période à l'autre.
    int64_t timerDueSub_[4] = {};

    // Backing store des autres registres timer/USART : TOS les écrit puis relit
    // pour vérifier la présence du MFP, donc ils doivent renvoyer ce qu'on y a mis.
    uint8_t timer_[0x40] = {};

private:
    int highestPending() const;     // n° de source prête la plus prioritaire, -1 sinon
    int highestInService() const;   // n° de source en cours de service, -1 sinon

    // --- Signal IRQ daté (port mfp.c : MFP_UpdateIRQ / MFP_InputOnChannel) ---------
    // Le 68901 ne présente pas « IPR & IMR » directement au CPU : il a un signal IRQ
    // interne, recalculé sur chaque changement de registre/entrée, avec :
    //   • CHRONOLOGIE : à priorités égales devant le câblage, les requêtes pendantes
    //     arrivées dans la même fenêtre sont servies dans l'ordre d'ARRIVÉE
    //     (pendingTime_/pendingTimeMin_, port Pending_Time[] mfp.c:963-1120) ;
    //   • DÉLAI : un front montant d'IRQ n'est visible du CPU que 4 cycles plus tard
    //     (irqTime_ + kIrqDelayToCpu ; la retombée, elle, est immédiate, comme Hatari).
    static constexpr int64_t kIrqDelayToCpu = 4;     // MFP_IRQ_DELAY_TO_CPU (mfp.c:374)
    static constexpr int64_t kNever = INT64_MAX;
    // Recalcule le signal IRQ (port MFP_UpdateIRQ). `eventTime` = cycle de l'événement
    // déclencheur (écriture registre, IACK) ; 0 = « venant d'un timer/entrée » → on
    // date le front montant de pendingTime_[canal élu] (antidatage des timers servis
    // en retard). Arme/annule Scheduler::MFP_IRQ pour la visibilité différée.
    void updateIrq(int64_t eventTime);
    // Source pendante éligible la plus prioritaire (port MFP_CheckPendingInterrupts) :
    // pendante ET non masquée ET la plus ANCIENNE de la fenêtre courante ET aucune
    // source de priorité ≥ en service. -1 si aucune.
    int  checkPendingInterrupts() const;

    bool    irq_        = false;     // signal IRQ interne du 68901
    int64_t irqTime_    = 0;         // cycle du dernier front montant d'IRQ
    int     currentInt_ = -1;        // canal élu (vecteur présenté à l'IACK)
    int64_t pendingTime_[16] = {};   // cycle d'arrivée de chaque requête (kNever au repos)
    int64_t pendingTimeMin_ = kNever;// plus ancienne requête non masquée de la fenêtre

    // Octet des 8 lignes d'ENTRÉE du GPIP (bit7 moniteur^XSINT … bit0 BUSY), tel que
    // le voit le détecteur de front — c'est la valeur calculée dans read8($FFFA01)
    // AVANT application du DDR. Centralisé ici pour servir aussi gpipUpdateInterrupt.
    uint8_t gpipInput() const;

    // Réévalue les IRQ GPIP front-déclenchées après un changement de GPIP/AER/DDR
    // (port MFP_GPIP_Update_Interrupt). État = GPIP ^ AER : sur une ligne en ENTRÉE
    // dont l'état bascule, on lève le canal si le front est ACTIF (AER == niveau GPIP).
    // Cas réel : une écriture AER (ex. bset/bclr #0,$FFFA03 des démos « M »/« Realtime »)
    // peut lever une IRQ alors même que la ligne d'entrée n'a pas bougé.
    void gpipUpdateInterrupt(uint8_t gpipOld, uint8_t gpipNew, uint8_t aerOld, uint8_t aerNew);
    void gpipUpdateInterruptAt(uint8_t gpipOld, uint8_t gpipNew, uint8_t aerOld, uint8_t aerNew, int64_t when);

    // Pose une ligne d'entrée GPIP avec détection de front (port MFP_GPIP_Set_Line_Input,
    // mfp.c:1180) : capture du GPIP avant/après le changement, puis gpipUpdateInterrupt
    // lève le canal seulement si la ligne est en ENTRÉE (DDR), a réellement basculé au
    // niveau du pin (le wire-OR ACIA absorbe une 2ᵉ ligne qui tombe) et que le front
    // correspond à l'AER. Sans transition : aucun effet.
    void gpipSetLine(bool& line, bool active) {
        if (line == active) return;
        const uint8_t before = gpipInput();
        line = active;
        gpipUpdateInterrupt(before, gpipInput(), aer, aer);
    }

    // Recalcule la config USART effective (cf. serialBaud) et la journalise au
    // premier réglage / à chaque CHANGEMENT (boîte à hack : on VOIT la négociation).
    void updateSerialConfig();

    // Période MFP convertie en unités internes de 1/256 cycle CPU, sans flottant.
    int64_t timerPeriodSubCycles(int timer, bool fromCounter) const;

    Scheduler* sched_ = nullptr;    // pour dater les timers (mode délai)
    std::function<void(uint8_t)> serialSink_;   // port série RS-232 (UDR $FFFA2F)
    int     serialClockFactor_ = 128; // prescaler × données × division UCR, défaut 9600
    int     serialBaud_ = 0;        // bauds informatifs tronqués (0 = USART jamais configurée)
    uint8_t serialUcr_  = 0;        // UCR au moment du dernier calcul (format du mot)
    std::function<void(bool)> timerBEdgeSink_; // lien moteur, non sérialisé
};
