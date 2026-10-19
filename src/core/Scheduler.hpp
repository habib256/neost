// =============================================================================
//  Scheduler.hpp — Ordonnanceur d'événements datés (horloge en cycles).
//
//  Calqué sur l'idée d'Hatari (`cycInt.c`) : plutôt que de tester chaque source
//  d'interruption à chaque ligne/trame, on garde, par source, le CYCLE auquel son
//  prochain événement est dû. La boucle d'horloge exécute le CPU jusqu'au plus
//  proche événement, puis déclenche les callbacks échus (qui peuvent se
//  replanifier). Voir docs/CYCLE_ACCURACY.md.
//
//  Les phases annoncées à la création de ce fichier sont FAITES : l'énumération
//  `Source` compte une vingtaine de sources (vidéo, timers MFP A-D, FDC, son DMA,
//  IKBD/MIDI/série, Microwire, blitter, VC_RESTART, HBL, VBL) et le quantum n'est
//  plus la ligne mais l'ÉVÉNEMENT — le bloc CPU est borné par `nextDue()` et
//  préempté (`beginRun`/`endSlice`) dès qu'un événement est planifié plus tôt.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include "core/StateArchive.hpp"

class Scheduler {
public:
    // Sources ordonnancées. L'ORDRE de l'énum = priorité à cycle égal (cf. runTo).
    // RENDER ≈ fin Display-Enable (376) ; TIMER_B = event-count sur DE (400, piloté
    // par Machine) ; HBL niveau 2 = 508 ; VBL. Les Timers A/C/D (mode délai) sont
    // datés par le MFP lui-même (période calculée des registres TxCR/TxDR).
    // FDC = fin de commande (BUSY→INTRQ) ; FDC_INDEX = impulsion d'index du
    // lecteur (1/tour, ~200 ms à 300 tr/min) tant que le moteur tourne ;
    // DMASND = fin de trame du son DMA STE (→ event-count Timer A du MFP).
    // IKBD = réponse différée du clavier (ex. $F1 ~502000 cycles après le reset
    // $80,$01, comme Hatari) : l'IRQ ACIA doit arriver PENDANT que le code attend,
    // pas instantanément (sinon elle est levée avant l'armement et perdue).
    // IKBD_TX = transfert TDR→TSR sur la grille bit de 1024 cycles (TDRE remonte),
    // puis réception au bit de stop côté IKBD. Un TDR peut attendre pendant
    // l'émission du TSR ; TIE ne conditionne que l'IRQ « transmetteur prêt ».
    // MICROWIRE = shift série du registre Microwire ($FF8922) du son STE : 16
    // décalages (8 cycles chacun, cf. Hatari) avant que la commande LMC1992 soit
    // décodée et que $FF8922 retombe à 0. Des diagnostics POLLENT $FF8922 jusqu'à 0.
    // TIMER_B = tic event-count de Timer B (une fois/ligne affichée, piloté par Machine).
    // TIMER_B_DELAY = Timer B en mode DÉLAI (prescaler/données comme A/C/D), daté par le
    // MFP. Les deux modes de Timer B sont exclusifs (TBCR = 8 → event-count, 1-7 → délai),
    // d'où deux sources distinctes sans conflit.
    // MFP_IRQ = instant où le signal IRQ du MFP devient VISIBLE du CPU : le 68901 met
    // 4 cycles à propager IRQ vers le 68000 (port MFP_IRQ_DELAY_TO_CPU, mfp.c:374).
    // Le MFP l'arme à irqTime+4 quand son IRQ monte ; le callback recalcule l'IPL.
    // Placé APRÈS les timers (à cycle égal, l'IPL est recalculé une fois les IRQ levées).
    // IKBD_RX = livraison cadencée IKBD → ACIA : un octet de la file ne devient
    // visible (RDRF) qu'après ~1 octet série (10 bits à 7812,5 bauds = 10240
    // cycles), comme le SCI d'Hatari. Des jeux (Vroom) identifient les octets du
    // paquet souris à leur CADENCE d'arrivée : une livraison instantanée des 3
    // octets fait perdre la synchro de leur parseur (axes souris « tournés »).
    // MIDI_TX = chargement TDR→TSR (TDRE) puis fin de transmission sur MIDI OUT.
    // Un caractère /16 8N1 dure 2560 cycles ; l’IRQ TX suit le TDR, pas le câble.
    // MIDI_RX = livraison cadencée d'un octet du MONDE EXTÉRIEUR à l'ACIA MIDI :
    // l'horloge de réception tourne en permanence à 31250 bauds (10 bits = 2560
    // cycles) tant qu'une source hôte est branchée, et fait entrer AU PLUS un octet
    // par période — comme le registre à décalage d'un 6850 sur un vrai câble.
    // Auparavant l'injection se faisait une fois par TRAME, ce qui plafonnait
    // l'entrée à 2 octets/trame (~143 o/s en mono) contre 3125 o/s sur un câble :
    // un accord un peu dense entrait au ralenti. Symétrique de MIDI_TX.
    // BLITTER = tranche non-hog suivante du blitter : 64 accès bus blitter (256
    // cycles, CPU arrêté) alternés avec 64 accès CPU (cf. Blitter::onSlice).
    // VC_RESTART = rechargement du compteur vidéo en fin de trame (port
    // Video_RestartVideoCounter : ligne 310/260, cycle 56 [+4 STE]) — cf. Machine.
    // SERIAL_RX = livraison cadencée d'un octet RX à l'USART MFP (injection côté
    // hôte : modem Hayes — cf. Mfp::receiveByte). Un octet toutes
    // les ~10 périodes bit au débit configuré, IRQ RxFull (canal 12) par octet.
    // ACSI_IRQ = fin DMA du disque dur après le délai minimal de 1000 cycles CPU
    // (500 cycles bus lorsque le Mega STE tourne à 16 MHz),
    // (Hatari HDC_ACSI_InterruptHandler_Update), distincte des acquittements octet.
    // SCC_BRG_A/B = passages à zéro des générateurs de débit SCC (Mega STE).
    // SERIAL_TX : fin de caractère émis par l’USART MFP.
    // IKBD_MONITOR : échantillonnage joystick périodique demandé par $17.
    // SCC_TX/RX : fin d'un caractère série, distincte du pulse Zero Count du BRG.
    enum Source { RENDER, TIMER_A, TIMER_B, TIMER_B_DELAY, TIMER_C, TIMER_D, MFP_IRQ, FDC, FDC_INDEX, DMASND, IKBD, IKBD_RX, IKBD_TX, MIDI_TX, MIDI_RX, SERIAL_RX, MICROWIRE, BLITTER, VC_RESTART, HBL, VBL, ACSI_IRQ, SCC_BRG_A, SCC_BRG_B, IKBD_MONITOR, SCC_TX_A, SCC_TX_B, SCC_RX_A, SCC_RX_B, IKBD_AUTOSEND, SERIAL_TX, SRC_COUNT };
    static_assert(SRC_COUNT <= 32, "Scheduler source mask is 32 bits");

    using Callback = std::function<void()>;

    Scheduler() { due_.fill(kInactive); }

    void setCallback(Source s, Callback cb) { cb_[s] = std::move(cb); }

    // Horloge « live » = cycle CPU absolu EXACT, même au milieu d'une instruction
    // (now_ + cycles déjà consommés dans le quantum run() en cours). Indispensable
    // pour dater un timer programmé en plein milieu d'un bloc CPU : sans ça il
    // serait calé sur le DÉBUT du quantum (jusqu'à ~380 cycles trop tôt). C'est le
    // `Cycles_GetClockCounterImmediate()` d'Hatari (cf. cycInt.c). Sous Moira
    // (cœur cycle-exact) cette horloge est précise à la SOUS-instruction.
    void setLiveClock(std::function<int64_t()> fn) { liveClock_ = std::move(fn); }
    int64_t liveNow() const { return liveClock_ ? liveClock_() : now_; }

    // Préemption du timeslice CPU : quand un événement est posé AVANT la cible du
    // bloc CPU en cours, on coupe le bloc (le CPU finit son instruction puis rend
    // la main) pour que la boucle d'horloge re-planifie et s'arrête PRÈS de cet
    // instant → latence IRQ ~1 instruction, comme Hatari (qui re-teste
    // PendingInterruptCount à chaque instruction). Sans ça, un timer court armé en
    // plein bloc ne se déclenche qu'à la fin du bloc (jusqu'à ~380 cycles de retard).
    void setEndSlice(std::function<void()> fn) { endSlice_ = std::move(fn); }

    // Bornes d'un LOT DE DISPATCH (A42) : appelées au début et à la fin de runTo, une
    // seule fois même en ré-entrance (compteur de profondeur). Machine s'en sert pour
    // que l'élection de l'IRQ MFP voie TOUTES les entrées du lot avant de trancher —
    // c'est l'ordre d'Hatari : CycInt_Process sert tous les événements échus, PUIS la
    // boucle CPU fait `if (MFP_UpdateNeeded) MFP_UpdateIRQ_All(0)`, PUIS l'IPL est lu.
    // Sans ça, chaque callback élisait pour son compte et la règle « la plus ancienne
    // l'emporte » ne voyait jamais plus d'un candidat.
    void setDispatchHooks(std::function<void()> begin, std::function<void()> end) {
        dispatchBegin_ = std::move(begin); dispatchEnd_ = std::move(end);
    }
    void beginRun(int64_t target) { runTarget_ = target; }
    void endRun() { runTarget_ = kInactive; }

    // Réarme l'horloge à 0 et désactive tous les événements (début de trame).
    void reset() {
        now_ = 0;
        for (auto& d : due_) d = kInactive;
        armed_     = 0;
        runTarget_ = kInactive;
        nextDue_   = kInactive;
    }

    // Programme (ou reprogramme) l'événement `s` au cycle absolu `atCycle`.
    void schedule(Source s, int64_t atCycle) {
        const int64_t old = due_[s];
        due_[s] = atCycle;
        // Miroir binaire (cf. armed_). Le test explicite contre kInactive garde
        // l'invariant vrai même si un appelant planifie « à l'inactif » — l'ancien
        // code, qui relisait due_ à chaque balayage, y était insensible.
        if (atCycle != kInactive) armed_ |= 1u << s; else armed_ &= ~(1u << s);
        // Cache du plus proche événement dû (cf. nextDue_). Il est tenu EXACT, et pas
        // seulement minorant : un événement plus tôt l'avance ; un événement REPOUSSÉ
        // alors qu'il PORTAIT le minimum le fait reculer, et il faut alors rebalayer.
        // Ce dernier cas est rare — une source périodique qui se replanifie depuis son
        // propre callback a déjà été remise à kInactive par runTo, donc `old` y vaut
        // kInactive et aucun balayage n'a lieu. L'exactitude est ce qui permet à
        // nextDue() de répondre en O(1) : c'est lui que Machine::runFrame appelle pour
        // borner chaque bloc CPU, et il pesait à lui seul ~5 % des instructions.
        //
        // ⚠ « Planifier à l'inactif » (atCycle == kInactive) équivaut à un cancel :
        // sans ce cas particulier, `atCycle < nextDue_` serait VRAI (-1 < tout) et
        // poserait nextDue_ = kInactive alors que d'autres sources restent armées —
        // cache faussé, assert de nextDue() en debug, dispatch gelé en mode sync.
        // Aucun appelant actuel ne le fait, mais on ne laisse pas l'invariant fragile.
        if (atCycle == kInactive) {
            if (old != kInactive && old == nextDue_) nextDue_ = scanNextDue();
        }
        else if (nextDue_ == kInactive || atCycle < nextDue_) nextDue_ = atCycle;
        else if (old != kInactive && old == nextDue_)          nextDue_ = scanNextDue();
        // Si on est en plein bloc CPU (runTarget_ armé) et que cet événement tombe
        // AVANT la cible du bloc, on préempte : le CPU rend la main à la prochaine
        // frontière d'instruction et la boucle d'horloge ré-évaluera nextDue().
        // (A34 : il n'y a plus qu'un modèle d'exécution — le BLOC — donc beginRun est
        //  TOUJOURS armé et cette préemption est toujours active.)
        if (atCycle != kInactive && runTarget_ != kInactive && atCycle < runTarget_ && endSlice_) {
            runTarget_ = atCycle;   // nouvelle cible effective (évite des coupes redondantes)
            ++preemptions;
            endSlice_();
        }
    }
    void cancel(Source s) {
        const bool wasMin = (due_[s] != kInactive && due_[s] == nextDue_);
        due_[s] = kInactive;
        armed_ &= ~(1u << s);
        if (wasMin) nextDue_ = scanNextDue();   // l'échéance minimale disparaît → recalcul
    }

    int64_t now() const { return now_; }

    // Échéance du plus proche événement dû, en O(1) (cache). -1 si aucun. C'est le
    // pendant de PendingInterruptCount d'Hatari : Cpu68k::sync() le compare à
    // l'horloge live à CHAQUE pas pour ne dispatcher que si nécessaire.
    int64_t peekNextDue() const { return nextDue_; }

    // Chemin piloté par Cpu68k::sync() (modèle `do_cycles` WinUAE) : avance l'horloge
    // dispatchée à `cycle` (cycle bus absolu, EN COURS d'instruction) et déclenche les
    // échus. O(1) si rien n'est dû — sync() est appelé à chaque instruction/accès.
    // Les callbacks posent l'IPL au cycle exact → vu par le POLL_IPL de Moira.
    void syncTo(int64_t cycle) {
        if (nextDue_ != kInactive && cycle >= nextDue_) runTo(cycle);
        else now_ = cycle;
    }

    // Dispatch CIBLÉ des timers MFP DATÉS (TIMER_A/B_DELAY/C/D) échus à `cycle` — port
    // RESTREINT de `MFP_UpdateTimers` (hatari/src/mfp.c:681), qu'Hatari appelle avant
    // CHAQUE accès registre du MFP « to ensure MFP timers are updated in chronological
    // order ».
    //
    // POURQUOI C'EST NÉCESSAIRE, et le cas est nommé par Hatari lui-même (mfp.c:135,
    // entrée du 2022/01/27) : « fix the game Super Hang On, where 'bclr #0,$fffffa0f'
    // to clear Timer B ISR sometimes happens at the same time that Timer C expires,
    // which used the wrong ISR value and gave FLICKERING RASTER COLORS ». Sans ce
    // dispatch, l'écriture qui acquitte l'ISR de Timer B travaille sur un ISR qui
    // n'inclut pas encore l'expiration de Timer C : l'acquittement porte alors sur la
    // mauvaise valeur, une interruption raster est perdue ou rejouée, et la ligne sort
    // avec la palette de sa voisine — des bandes de couleur pleine largeur, à hauteur
    // arbitraire, par intermittence.
    //
    // ⚠ POURQUOI « CIBLÉ », ET NON LE `syncTo` NU QUE LA LETTRE D'HATARI SUGGÈRE.
    // Hatari appelle `CycInt_Process_Clock`, qui sert TOUTES les sources échues. Le
    // porter tel quel réactiverait le modèle SYNC-DRIVEN RÉFUTÉ par NeoST (deadlock
    // Enchanted Land, ~1590 dispatches mid-instruction par trame — cf.
    // `docs/HATARI_DIVERGENCES.md`) : servir HBL/VBL/RENDER au milieu d'une instruction
    // déplace le faisceau vidéo sous les pieds du rendu. Les quatre timers DÉLAI du MFP,
    // eux, ne touchent QUE l'état du MFP (compteur rechargé, IRQ antidatée à l'échéance
    // via `firingDue()`), donc les anticiper ne peut réordonner aucune autre puce.
    // TIMER_B (event-count) est délibérément EXCLU : il n'est pas daté par une période
    // calculée mais posé par `Machine` sur le Display-Enable, et l'avancer déplacerait
    // un tic vidéo.
    //
    // `now_` n'est PAS avancé : il reste l'horloge DISPATCHÉE du modèle bloc. On ne fait
    // qu'anticiper des événements que le prochain `runTo` aurait servis de toute façon,
    // à la date où le matériel les a réellement produits.
    void runMfpTimersTo(int64_t cycle) {
        if (!(armed_ & kMfpTimerMask)) return;   // O(1) : aucun timer daté armé
        struct FiringGuard {                     // même garde RAII que runTo
            int64_t& f; int64_t prev;
            explicit FiringGuard(int64_t& b) : f(b), prev(b) {}
            ~FiringGuard() { f = prev; }
        } firingGuard{firingDue_};
        uint32_t fired = 0;                      // anti-livelock : 1 tir max par source
        bool any = false;
        for (;;) {
            int best = -1;
            for (uint32_t m = armed_ & kMfpTimerMask; m; m &= m - 1) {
                const int s = ctz32(m);
                if (!((fired >> s) & 1u) && due_[s] <= cycle
                    && (best < 0 || due_[s] < due_[best])) best = s;
            }
            if (best < 0) break;
            const int64_t late = cycle - due_[best];
            if (late > timerMaxLate) timerMaxLate = late;
            fired |= 1u << best;
            firingDue_ = due_[best];
            due_[best] = kInactive;
            armed_ &= ~(1u << best);
            any = true;
            if (cb_[best]) cb_[best]();
        }
        if (any) nextDue_ = scanNextDue();
    }

    // PRE_START du blitter : reprise du bus pendant une instruction CPU,
    // comme Blitter_HOG_CPU_do_cycles_after. Ne sert pas les événements vidéo
    // du modèle bloc et ne déplace pas now_ : le quantum CPU facture le stall.
    bool runBlitterTo(int64_t cycle) {
        if (due_[BLITTER] == kInactive || due_[BLITTER] > cycle) return false;
        struct Guard {
            int64_t& value; int64_t old;
            ~Guard() { value = old; }
        } guard{firingDue_, firingDue_};
        firingDue_ = due_[BLITTER];
        cancel(BLITTER);
        if (cb_[BLITTER]) cb_[BLITTER]();
        return true;
    }

    // Cycles de bus VOLÉS au CPU par un AUTRE maître de bus (le blitter) — port de
    // `Blitter_AddCycles` (hatari/src/blitter.c:342-354, symbole `all_cycles`).
    //
    // Chez Hatari il n'y a qu'UNE base de temps : le blitter fait
    //     nCyclesMainCounter       += all_cycles;      // blitter.c:351
    //     CyclesGlobalClockCounter += all_cycles;      // blitter.c:352
    // c'est-à-dire EXACTEMENT les deux compteurs que le CPU incrémente lui aussi
    // (m68000.h, M68000_AddCycles*) et que CycInt lit pour dater ET servir ses
    // échéances (cycInt.h, `CycInt_Process` boucle sur CyclesGlobalClockCounter). Un
    // cycle volé par le blitter avance donc la base de temps des timers.
    //
    // NeoST avait DEUX bases de temps dès que la facturation avait lieu hors quantum
    // CPU : `Cpu68k::addBusWaitCycles` n'avance que l'horloge du cœur Moira, et `ran`
    // (retour de `Cpu68k::run`, mesuré depuis un `quantumStartBus_` RÉANCRÉ sur
    // l'horloge CPU à CHAQUE entrée) ne peut pas rattraper ce qui s'est passé AVANT
    // l'entrée du quantum. Ces cycles n'étaient donc dans NI `ran` NI `now_` : perdus
    // pour l'ordonnanceur, qui prenait un retard cumulé blit après blit, puis rattrapé
    // D'UN SEUL COUP par le `syncTo` du hook d'IACK (`Cpu68k::rebaseQuantumAndSync`).
    // Ce saut mangeait des tics de prescaler MFP — cf. `Mfp::readTimerData`, qui
    // reconstruit TADR depuis `due_ - liveNow()`.
    //
    // DISPATCH IMMÉDIAT (port de `Blitter_FlushCycles`, blitter.c:356-375) — chantier
    // BL4. Hatari ne se contente pas d'avancer ses compteurs : il appelle
    // `CycInt_Process()` après CHAQUE accès de 4 cycles, depuis le handler CycInt du
    // blitter lui-même (`INTERRUPT_BLITTER`). Sa boucle de dispatch est donc
    // RÉ-ENTRANTE par construction — `while (ActiveInt <= now) CycInt_CallActiveHandler()`
    // (cycInt.h:85-88), sans masque anti-relance. On porte ce comportement tel quel :
    // `syncTo` sert ICI MÊME les échéances franchies par les cycles volés, au cycle où
    // elles tombent, au lieu d'attendre la fin de la tranche.
    //
    // La ré-entrance de `runTo` est assurée par son `DispatchGuard` : `fired` et
    // `minAll` sont déjà des LOCALES (chaque trame a les siennes), `firingDue_` est
    // sauvegardé/restauré, et `nextDue_` est recalculé par un balayage complet à la
    // sortie de chaque trame. Le seul appelant réel (`Blitter::billCycles` depuis
    // `Blitter::readWord`/`writeWord`) ne peut pas rentrer dans `Blitter::onSlice` :
    // la source BLITTER a été consommée avant l'appel du callback et n'est replanifiée
    // que par `noteCpuBusAccess` (accès bus CPU, impossible pendant la tranche), et
    // `onSlice` porte de toute façon la garde `inSlice_`.
    //
    // AUCUN ÉTAT PERSISTANT n'est introduit : la dette n'est jamais différée, elle est
    // consommée dans l'appel, et `now_` est déjà sérialisé.
    void addStolenCycles(int64_t cycles) {
        if (cycles <= 0) return;
        syncTo(now_ + cycles);   // O(1) tant que rien n'est dû (cache nextDue_)
    }

    // Cycles CPU restants avant l'échéance de la source `s`, mesurés depuis
    // l'horloge LIVE (sous-instruction). Renvoie -1 si la source n'est pas armée.
    // Équivalent de `CycInt_FindCyclesRemaining` d'Hatari : permet à une puce (le
    // MFP) de lire le COMPTEUR VIVANT d'un timer en mode délai (cf. MFP_ReadTimer_AB/CD).
    int64_t cyclesUntil(Source s) const {
        if (due_[s] == kInactive) return -1;
        const int64_t rem = due_[s] - liveNow();
        return rem > 0 ? rem : 0;
    }

    // Variante NON écrêtée : peut renvoyer un reste NÉGATIF si l'échéance est
    // passée mais pas encore dispatchée (lecture sous-instruction entre le cycle
    // d'expiration et la fin du bloc CPU). Le MFP s'en sert pour replier le
    // compteur vivant modulo la période de RECHARGE (le matériel a déjà rechargé).
    // INT64_MIN si la source n'est pas armée.
    int64_t rawCyclesUntil(Source s) const {
        if (due_[s] == kInactive) return INT64_MIN;
        return due_[s] - liveNow();
    }

    // Cycle du prochain événement dû (>= now), ou -1 si aucun n'est armé. O(1) : le
    // cache nextDue_ est tenu EXACT par schedule/cancel/runTo (cf. schedule).
    int64_t nextDue() const {
        assert(nextDue_ == scanNextDue() && "cache nextDue_ désynchronisé");
        return nextDue_;
    }

    // Avance l'horloge jusqu'à `cycle` puis déclenche les événements échus (due <=
    // cycle) DANS L'ORDRE CHRONOLOGIQUE de leurs échéances (comme la liste triée
    // cycInt d'Hatari) — l'ordre de l'énum ne sert que de tie-break à cycle égal.
    // (L'ancien scan par index déclenchait TIMER_A avant un HBL dû 200 cycles plus
    // tôt quand l'overshoot d'un quantum franchissait plusieurs échéances.)
    // Chaque source tire AU PLUS une fois par runTo (garde anti-livelock) ; un
    // callback peut replanifier (échéance > now : re-déclenchée au tour suivant).
    void runTo(int64_t cycle) {
        now_ = cycle;
        assert(armedInvariant() && "armed_ désynchronisé de due_");
        // RÉ-ENTRANCE (chantier BL4) — `runTo` peut être appelé DEPUIS un callback, via
        // Scheduler::addStolenCycles (accès bus du blitter) ou via un syncTo. C'est le
        // modèle d'Hatari, dont `CycInt_Process` est ré-entré depuis le handler du
        // blitter (cycInt.h:85-88 + blitter.c:366). `fired` et `minAll` sont des
        // LOCALES, donc propres à chaque trame ; `nextDue_` est recalculé par un
        // balayage complet à la sortie de chaque trame. Restait `firingDue_`, ancre
        // anti-dérive de l'événement EN COURS (cf. firingDue()) : une trame imbriquée
        // l'écrasait puis le remettait à kInactive, faisant perdre son ancre au
        // callback extérieur — d'où cette sauvegarde/restauration RAII, qui tient aussi
        // si un callback sort par exception (bus error).
        struct FiringGuard {
            int64_t& f; int64_t prev;
            explicit FiringGuard(int64_t& b) : f(b), prev(b) {}
            ~FiringGuard() { f = prev; }
        } firingGuard{firingDue_};
        uint32_t fired = 0;
        static_assert(SRC_COUNT <= 32, "masque fired sur 32 bits");
        // Bornes du lot (cf. setDispatchHooks). RAII : le lot se ferme même si un
        // callback sort par exception (bus error), et la profondeur rend l'appel unique
        // sous ré-entrance (addStolenCycles du blitter re-rentre dans runTo).
        struct DispatchGuard {
            Scheduler& s;
            explicit DispatchGuard(Scheduler& sc) : s(sc) {
                if (s.dispatchDepth_++ == 0 && s.dispatchBegin_) s.dispatchBegin_();
            }
            ~DispatchGuard() {
                if (--s.dispatchDepth_ == 0 && s.dispatchEnd_) s.dispatchEnd_();
            }
        } dispatchGuard{*this};
        // Minimum de TOUTES les échéances armées, tenu à jour par la passe ci-dessous.
        // À la passe qui ne trouve plus rien à déclencher — donc après que tous les
        // callbacks ont replanifié — il vaut exactement ce que rendrait scanNextDue().
        // On économise ainsi un balayage complet par appel à runTo (1,8 M par 300
        // trames au profil), qui pesait à lui seul ~7 % des instructions du programme.
        int64_t minAll = kNever;
        for (;;) {
            int best = -1;
            minAll = kNever;
            // Balayage des seules sources ARMÉES (cf. armed_) : le profil callgrind
            // montrait ce triple test répété sur les 19 sources — dont plusieurs
            // inactives — à chaque dispatch. L'ordre de visite reste croissant en `s`
            // (ctz sur le bit de poids faible) : le tie-break à échéance ÉGALE
            // (comparaison STRICTE ci-dessous) désigne donc la même source qu'avant.
            // ⚠ Le minimum se calcule sur armed_ ENTIER, pas sur `armed_ & ~fired` :
            // une source déjà tirée que son callback a replanifiée est bien la
            // prochaine échéance, même si elle ne peut plus tirer dans ce runTo.
            for (uint32_t m = armed_; m; m &= m - 1) {
                const int s = ctz32(m);
                const int64_t d = due_[s];
                if (d < minAll) minAll = d;
                if (!((fired >> s) & 1u) && d <= now_
                    && (best < 0 || d < due_[best])) best = s;
            }
            if (best < 0) break;
            // Métrique cycle-accuracy : retard d'un timer MFP daté (now - échéance).
            // Piloté par sync(), il reste ~1 accès (sous-instruction en PRECISE_TIMING).
            if (isMfpTimer(best)) {
                const int64_t late = now_ - due_[best];
                if (late > timerMaxLate) timerMaxLate = late;
            }
            fired |= 1u << best;
            firingDue_ = due_[best];             // échéance servie (pour replanif. anti-dérive)
            due_[best] = kInactive;              // consommé avant l'appel…
            armed_ &= ~(1u << best);
            if (cb_[best]) cb_[best]();           // …le callback peut replanifier
        }
        // (firingDue_ est restauré par firingGuard à la sortie : kInactive pour une
        //  trame de premier niveau, l'ancre de l'appelant pour une trame imbriquée.)
        // Les callbacks ont pu (re)planifier : minAll, calculé par la DERNIÈRE passe,
        // porte déjà le résultat du balayage complet (cf. sa déclaration).
        nextDue_ = (minAll == kNever) ? kInactive : minAll;
        assert(nextDue_ == scanNextDue() && "minAll incohérent avec scanNextDue");
    }

    // Échéance de l'événement en cours de dispatch (valide PENDANT son callback),
    // -1 sinon. Permet à une source périodique de se replanifier ancrée sur SON
    // échéance servie (et non l'horloge courante), ne perdant pas le dépassement de
    // latence d'IRQ — port de PendingCyclesOver d'Hatari (cf. Mfp::onTimerExpire).
    int64_t firingDue() const { return firingDue_; }

    // Diagnostics (chantier précision cycle) : pire retard d'un timer MFP daté, et
    // nombre de préemptions du timeslice CPU déclenchées. Lus par le headless.
    int64_t timerMaxLate = 0;
    long    preemptions  = 0;

    // Save-state : échéances par source + horloges. Les callbacks (cb_), la liveClock_
    // et l'endSlice_ sont re-liés à la construction de Machine → PAS sérialisés.
    void serialize(StateArchive& ar) {
        ar(due_); ar(now_); ar(nextDue_); ar(runTarget_);
        // armed_ est PUREMENT DÉRIVÉ de due_ : on le reconstruit au chargement plutôt
        // que de le sérialiser (un état ancien ou forgé ne peut donc pas désynchroniser
        // l'invariant, et le format de save-state ne change pas).
        if (ar.loading()) {
            armed_ = 0;
            for (int s = 0; s < SRC_COUNT; ++s)
                if (due_[s] != kInactive) armed_ |= 1u << s;
            // nextDue_ est lui aussi PUREMENT DÉRIVÉ de due_ : on le recalcule au lieu
            // de faire confiance à la valeur sérialisée. Le nouveau code (cache tenu
            // EXACT, cf. schedule) l'exige — un état sauvé par un build plus ancien, ou
            // forgé, pouvait porter un nextDue_ minorant/faux qui faisait avorter
            // nextDue() en debug et retardait/gelait le dispatch en release.
            nextDue_ = scanNextDue();
        }
        // ⚠ L'horloge du scheduler pilote des boucles de rattrapage (syncTo, et côté FDC
        // le rattrapage d'impulsion index) : forgée absurde, elle les rend non bornées —
        // gel définitif reproduit. Une échéance doit être soit inactive, soit dans une
        // fenêtre plausible autour de `now_` (le passé récent existe : une échéance déjà
        // due mais pas encore servie).
        ar.check(now_ >= 0);
        for (int64_t d : due_)
            ar.check(d == kInactive || (d >= now_ - (1LL << 32) && d <= now_ + (1LL << 40)));
        ar.check(nextDue_   == kInactive || (nextDue_   >= now_ - (1LL << 32) && nextDue_   <= now_ + (1LL << 40)));
        ar.check(runTarget_ == kInactive || (runTarget_ >= now_ - (1LL << 32) && runTarget_ <= now_ + (1LL << 40)));
    }

private:
    static bool isMfpTimer(int s) {              // sources dont le retard dépend de la préemption
        return s == TIMER_A || s == TIMER_B_DELAY || s == TIMER_C || s == TIMER_D;
    }
    // Les MÊMES quatre sources en masque, pour runMfpTimersTo (TIMER_B event-count exclu).
    static constexpr uint32_t kMfpTimerMask =
        (1u << TIMER_A) | (1u << TIMER_B_DELAY) | (1u << TIMER_C) | (1u << TIMER_D);
    // Scan complet du plus proche événement dû (-1 si aucun). N'est plus sur le chemin
    // chaud : runTo calcule désormais ce minimum au passage (cf. minAll). Il ne reste
    // appelé que par cancel() de l'échéance minimale et par le nextDue() public.
    int64_t scanNextDue() const {
        int64_t best = kInactive;
        for (uint32_t m = armed_; m; m &= m - 1) {
            const int64_t d = due_[ctz32(m)];
            if (best < 0 || d < best) best = d;
        }
        return best;
    }
    // Index du bit de poids faible. GCC/Clang uniquement (les deux seuls compilateurs
    // ciblés par NeoST) ; `m` est TOUJOURS non nul aux points d'appel.
    static int ctz32(uint32_t m) { return __builtin_ctz(m); }

    // Contrôle de l'invariant armed_ ⟺ due_ (compilé hors NDEBUG uniquement) : c'est
    // le seul risque introduit par le masque — un site d'écriture de due_ ajouté plus
    // tard sans mettre armed_ à jour rendrait un événement invisible du dispatch.
    bool armedInvariant() const {
        for (int s = 0; s < SRC_COUNT; ++s) {
            if (((armed_ >> s) & 1u) != (due_[s] != kInactive ? 1u : 0u)) return false;
        }
        return true;
    }

    static constexpr int64_t kInactive = -1;
    // Sentinelle « aucune échéance » du minimum courant de runTo (cf. minAll) : une
    // valeur plus grande que toute échéance plausible, pour n'avoir aucun cas
    // particulier dans la boucle.
    static constexpr int64_t kNever = INT64_MAX;
    std::array<int64_t, SRC_COUNT>  due_{};      // (rempli à kInactive au ctor)
    // Miroir binaire de « due_[s] != kInactive », maintenu par schedule/cancel/runTo/
    // reset/serialize. Raison d'être : le dispatch et le recalcul du plus proche dû
    // pesaient ~18 % des instructions du cœur (callgrind, boot TOS comme en jeu) en
    // balayant les 19 sources ; ici on ne visite que celles réellement armées (~6).
    // INVARIANT : (armed_ >> s) & 1  ⟺  due_[s] != kInactive. Vérifié en debug par
    // armedInvariant().
    uint32_t armed_ = 0;
    std::array<Callback, SRC_COUNT> cb_{};
    int64_t now_ = 0;
    int64_t nextDue_ = kInactive;                // cache O(1) du plus proche dû (cf. syncTo)
    int64_t firingDue_ = kInactive;              // échéance de l'événement en cours de dispatch
    int64_t runTarget_ = kInactive;              // cible du bloc CPU courant (-1 = hors run)
    std::function<int64_t()> liveClock_{};       // horloge sous-quantum (cf. liveNow)
    std::function<void()> dispatchBegin_{}, dispatchEnd_{};   // bornes du lot (cf. setDispatchHooks)
    int dispatchDepth_ = 0;                     // profondeur de ré-entrance de runTo
    std::function<void()>    endSlice_{};         // coupe le timeslice CPU (préemption)
};
