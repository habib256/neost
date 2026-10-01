// =============================================================================
//  Cpu68k.cpp — Liaison Moira <-> Bus NeoST.
//
//  Moira (cœur 68000 cycle-exact, MIT) est intégré en sous-module. Cette façade
//  route ses accès mémoire vers un Bus unique pointé par g_cur->bus et reproduit le
//  vectoring ST (MFP vectorisé niveau 6, VBL/HBL auto-vectorisés). C'est le seul
//  couplage « global » du projet (les callbacks CPU n'ont qu'un Bus actif).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Cpu68k.hpp"
#include "core/Blitter.hpp"
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
#include "core/Tracer.hpp"
#include "io/Mfp.hpp"
#include "io/GemdosHd.hpp"
#include "io/Scc.hpp"

#include <cstdio>
#include <cstdlib>      // getenv/strtol (diags sous variable d'environnement)
#include <stdexcept>

#include "Moira.h"

class NeostMoira;   // défini plus bas (le cœur Moira dérivé, avec nos hooks)

// =============================================================================
//  A33 — ÉTAT PAR INSTANCE du CPU, regroupé (2026-08-28).
//
//  Ce fichier portait 48 globaux `g_*`. Les confondre était le vrai obstacle au
//  mono-instance : les uns sont de l'ÉTAT (une machine émulée en a un jeu), les
//  autres de la CONFIGURATION DE PROCESSUS lue une fois dans l'environnement (les
//  verrous d'IACK, d'E-Clock, d'IPL, de créneau RAM et toutes les traces — cf.
//  tools/env_locks.json) : celles-là DOIVENT rester globales, les rendre
//  par-instance serait faux.
//
//  Les 25 champs ci-dessous sont l'état. Ils vivent dans une structure pour que
//  « une deuxième machine » devienne un deuxième objet, et non 25 variables à
//  démêler. Étape suivante (cf. TODO A33) : cette structure devient membre de
//  Cpu68k et `g_st` un pointeur vers l'instance ACTIVE, posé à l'entrée de run()
//  — les callbacks Moira ne tournent que dedans.
// =============================================================================
struct CpuState {
    Bus*        bus          = nullptr;   // bus actif vu par les callbacks CPU
    Scheduler*  sched        = nullptr;   // ordonnanceur piloté par sync() (cf. NeostMoira::sync)
    Cpu68k*     cpuSelf      = nullptr;   // instance courante (rebase de quantum depuis les hooks)
    NeostMoira* moira        = nullptr;   // cœur Moira actif
    Tracer*     tracer       = nullptr;   // traceur optionnel (nullptr = aucun surcoût)
    bool        vblPending   = false;     // VBL (niveau 4) en attente d'acquittement
    bool        hblPending   = false;     // HBL (niveau 2) en attente d'acquittement
    bool        inBusError   = false;     // garde « double bus fault » (cf. son bandeau)
    int         grp0Vector   = 0;
    // ---- Dernière faute de GROUPE 0, pour NOMMER un halt (A42, 2026-08-30) -----
    // Équivalent des `last_*_for_exception_3` d'Hatari (newcpu.c:3086) : sans elles
    // un halt ne disait QUE son vecteur, et « la machine est gelée » sans dire OÙ
    // n'aide personne — c'est ce qui transformait un diagnostic d'une minute en
    // rapport « ça plante ».
    uint32_t    faultAddr    = 0;         // adresse fautive
    uint32_t    faultPc      = 0;         // PC de l'instruction fautive (getPC0)
    bool        faultWrite   = false;     // true = écriture, false = lecture
    bool        faultValid   = false;     // une faute a été enregistrée
    bool        inReset      = false;
    bool        endSlice     = false;
    bool        bpHit        = false;     // un breakpoint/watchpoint a stoppé le dernier run()
    uint32_t    bpAddr       = 0;         // adresse atteinte (PC du BP, ou adresse DONNÉE du WP)
    bool        bpWatch      = false;     // true = WATCHPOINT (accès mémoire), pas un breakpoint PC
    uint32_t    bpSkipPc     = 0xFFFFFFFFu;  // adresse à ignorer UNE fois (reprise propre)
    int         cpuMul       = 1;         // 1 = 8 MHz, 2 = 16 MHz
    int64_t     cpuBias      = 0;         // biais de conversion (0 tant qu'on reste à 8 MHz)
    int         desiredIpl   = 0;         // niveau IPL calculé (immédiat, broche « réelle »)
    int         appliedIpl   = 0;         // niveau dernier PROPAGÉ à la broche Moira (POLL_IPL)
    int64_t     iplChgClock  = -1000;     // horloge du dernier changement de desiredIpl
    int64_t     hblPinDue    = -1;        // cycle BUS de montée de la broche HBL (−1 = inactif)
    int64_t     vblPinDue    = -1;        // idem VBL
    int64_t     pinNextDue   = -1;        // min des deux (chemin chaud O(1) dans sync())
    bool        htArmed      = false;     // trace NEOST_HTRACE armée
    int64_t     htPrev       = 0;
};

namespace {
// Instance ACTIVE. Les callbacks de Moira et les fonctions libres de ce fichier
// n'ont pas de `this` : elles passent par ce pointeur, posé par le constructeur et
// re-posé à l'entrée de Cpu68k::run(). Deux CPU peuvent donc coexister et tourner
// À TOUR DE RÔLE (test unitaire d'une Machine, A/B en un processus, anneau MIDI à
// deux nœuds) ; deux CPU tournant SIMULTANÉMENT dans deux threads demanderaient
// davantage — ce n'est pas ce qu'A33 promettait.
CpuState* g_cur = nullptr;

    // Garde « double bus fault » : armée quand on déclenche une bus error, désarmée
    // au début de l'instruction SUIVANTE. Si une NOUVELLE bus error survient alors
    // qu'elle est armée, c'est qu'un accès a fauté PENDANT l'empilement de la trame
    // d'exception (SSP/PC corrompus, code parti en vrille) : sur un vrai 68000 cela
    // halte le CPU. On reproduit ce halt au lieu de récurser → l'hôte ne segfault
    // plus et le mode headless peut vider sa trace/série.
    // Vecteur de l'exception de GROUPE 0 en cours de prise (2 = bus error,
    // 3 = address error), 0 hors exception. Latché par willExecute(M68kException…)
    // et effacé par didExecute(M68kException…) : si l'exception se termine
    // normalement il retombe à 0 ; si elle lève DoubleFault (SSP impair, ou
    // nouvelle faute pendant l'empilement) il vaut ENCORE le vecteur fautif quand
    // cpuDidHalt() est notifié — c'est ce qui permet de NOMMER la cause du halt.
    // Pendant cpu.reset() (lecture SSP/PC) le cœur consomme ~40 cyc via sync(). On NE
    // dispatche PAS l'ordonnanceur alors : sinon sched.now() serait traîné à 40 et la 1ʳᵉ
    // trame s'ancrerait là (frameStart_=40) au lieu de 0 → grille faisceau décalée de 40 cyc
    // → calibrations raster cassées (Enchanted Land noir). L'ancien modèle n'avançait
    // l'ordonnanceur qu'aux frontières de bloc, jamais pendant le reset.
    // Préemption du timeslice : posé par endTimeslice() (depuis un callback de
    // l'ordonnanceur, en plein milieu d'une instruction), testé après chaque
    // instruction dans la boucle run() pour rendre la main à l'horloge.
    // ---- Débogueur : breakpoints PC (cf. Cpu68k § Débogueur) --------------------
    // ---- Bascule 8/16 MHz du Mega STE ($FF8E21 bit1, cf. Cpu68k::setMegaSteSpeed) --
    // L'ordonnanceur et toutes les puces vivent en cycles BUS (8 MHz) ; le cœur
    // CPU, lui, compte ses propres cycles. À 16 MHz : 1 cycle bus = 2 cycles CPU.
    // La conversion est : bus = (clock + g_cur->cpuBias) / g_cur->cpuMul, le biais étant rebasé
    // à chaque bascule pour que l'horloge bus reste CONTINUE (port de l'esprit de
    // Hatari cpucycleunit = CYCLE_UNIT/2 dans clocks_timings.c/newcpu).
    // Synchro E-clock à l'entrée des IRQ auto-vectorisées (cf. NeostMoira::willInterrupt).
    // OPT-IN (`NEOST_ECLOCK_ON`) : mécanisme FIDÈLE (Moira, 68000 générique, n'a pas la
    // synchro E-clock Atari de l'IACK auto-vecteur), mais NON validable en jeu headless
    // (écrans cassés EL/LX/Cuddly/SHO inatteignables sans navigation ; overscan_lr rendu
    // à plat). Étalons pixel-exact INCHANGÉS dans les deux états. Risque résiduel de
    // double-comptage avec les hacks de datation vidéo empiriques (mêmes que chipWait8) →
    // resté gated tant que l'oracle in-game ne le tranche pas. À activer/calibrer lors de
    // la refonte coordonnée (retrait des hacks + ajout des mécanismes fidèles ensemble).
    bool    g_eclockOn    = []{ const char* s = std::getenv("NEOST_ECLOCK_ON"); return s && std::atoi(s) != 0; }();
    int     g_eclockPhase = []{ const char* s = std::getenv("NEOST_ECLOCK_PHASE"); return s ? std::atoi(s) : 0; }();
    // DEEP convergence WinUAE (opt-in NEOST_IACK) : timing d'IACK FIDÈLE. Hatari applique
    // l'attente E-clock + un bloc « occupé » au CYCLE D'IACK (iack_cycle, newcpu.c:2958-3019),
    // PAS avant l'empilement de trame. Moira appelle readIrqUserVector EXACTEMENT à l'IACK
    // (MoiraExceptions_cpp.h:538, après SYNC(6)+write PClo+SYNC(4)) → on y déplace l'E-clock
    // (phase correcte, inclut la latence d'amorce) ET on ajoute le bloc occupé absent de Moira :
    // CPU_IACK_CYCLES_VIDEO_CE(10)+idle(4)=14 (auto-vecteur HBL/VBL), CPU_IACK_CYCLES_MFP_CE=12
    // (MFP niv6, sans E-clock). Magnitude calibrable à l'oracle (la comptabilité SYNC d'entrée
    // d'exception de Moira diffère un peu de WinUAE). Avec NEOST_IACK, willInterrupt ne fait plus
    // rien (E-clock relocalisé). Corrige le résidu de PHASE d'entrée d'IRQ (HBL period, poll-beat).
    // DÉFAUT ON (2026-06-17) : RAM_SLOT+IACK ENSEMBLE déclenchent l'overscan beam-sync (dérive
    // du faisceau +78/ligne = Hatari, cf. [[ramslot-iack-enable-overscan]]). NEOST_IACK=0 désactive.
    bool    g_iackOn      = []{ const char* s = std::getenv("NEOST_IACK"); return s ? std::atoi(s) != 0 : true; }();
    int     g_iackVideo   = []{ const char* s = std::getenv("NEOST_IACK_VIDEO"); return s ? std::atoi(s) : 14; }();
    // 16 et non 12 : CPU_IACK_CYCLES_MFP_CE (12, « not measured » chez Hatari) ne
    // couvre pas le cycle bus d'IACK lui-même. MESURÉ à l'oracle instrumenté
    // (2026-08-06, Super Hang-On in-game, [HEXC] hbl/lc) : chaîne FIXE « exception
    // Timer B → handler → stop → HBL pendante » = 144 cyc chez Hatari, 140 chez NeoST
    // avec 12 → +4 sur l'IACK verrouille les histogrammes d'écritures palette
    // (1re écriture de paire {104..128} à ±1 pt partout, réveil STOP 40/40/20 intact).
    int     g_iackMfp     = []{ const char* s = std::getenv("NEOST_IACK_MFP");   return s ? std::atoi(s) : 16; }();
    // NEOST_IACK_SYNC (défaut ON) : dispatch des événements échus AU point d'IACK,
    // port du « CycInt_Process() + MFP_UpdateIRQ_All juste avant la séquence d'IACK »
    // d'Hatari (newcpu.c:2938-2946 MFP, :2998-3003 vidéo). Mesuré : un événement est
    // réellement échu à 5,7-7 % des IACK (banc SHO in-game). =0 restaure l'ancien
    // modèle (élection du vecteur sur les seuls IPR posés à la frontière).
    bool    g_iackSync    = []{ const char* s = std::getenv("NEOST_IACK_SYNC"); return s ? std::atoi(s) != 0 : true; }();
    // Lead-in willInterrupt → IACK réel : SYNC(6)+write PClo(4)+SYNC(4)=14 cyc (MoiraExceptions
    // _cpp.h:533-537). On l'ajoute à la phase E-clock pour la calculer comme au cycle d'IACK,
    // bien que le setClock soit fait depuis willInterrupt (seul point fiable hors mid-accès).
    int     g_iackLead    = []{ const char* s = std::getenv("NEOST_IACK_LEAD"); return s ? std::atoi(s) : 14; }();
    // NEOST_IACK_AT (défaut ON) : E-clock + bloc IACK appliqués AU point d'IACK réel
    // (hooks iackSyncBefore/After dans execInterrupt<C68000>), en REMPLACEMENT des
    // SYNC(4)+SYNC(4) stock — fidèle Hatari iack_cycle. =0 → ancien modèle willInterrupt
    // (lead constant, sur-compte +8, phase E-clock détruite par l'alignement du push).
    bool    g_iackAt      = []{ const char* s = std::getenv("NEOST_IACK_AT"); return s ? std::atoi(s) != 0 : true; }();
    // DEEP (opt-in NEOST_IPLDELAY) : règle cpuipldelay de WinUAE (ipl_fetch_next, newcpu.c:4982).
    // Moira reconnaît l'IPL IMMÉDIATEMENT (POLL_IPL = reg.ipl = ipl) ; WinUAE DIFFÈRE la
    // reconnaissance d'une instruction si le pin IPL a changé < 4 cyc avant le point
    // d'échantillonnage de l'instruction. On le reproduit en RETARDANT la propagation du niveau
    // DÉSIRÉ vers la broche Moira de 4 cyc (via sync()) : le POLL_IPL voit l'ancien niveau jusqu'à
    // +4 cyc → reconnaissance reportée à la frontière suivante. Corrige le multiset du beat
    // (poll-oracle {2,4,6}→{0,2,4,6} = Hatari) et vise le deadlock EL ($EE = attente IRQ mal datée).
    bool    g_iplDelay    = []{ const char* s = std::getenv("NEOST_IPLDELAY"); return s && std::atoi(s) != 0; }();
    int     g_iplDelayCyc = []{ const char* s = std::getenv("NEOST_IPLDELAY_CYC"); return s ? std::atoi(s) : 4; }();
    // DEEP (opt-in NEOST_IPLFETCH) : port FIDÈLE de WinUAE ipl_fetch_next (≠ g_iplDelay crude
    // ci-dessus). La broche change immédiatement (setIPL = update_ipl) mais l'ÉCHANTILLON
    // (POLL_IPL = pollIpl) garde l'ANCIENNE valeur si la broche a changé < 4 cyc avant
    // (valeur précédente si 2-4 cyc, différée si <2). Implémenté DANS Moira (pollIpl) →
    // s'applique au point d'échantillon exact de chaque instruction, pas en retardant la
    // broche de l'extérieur. Magnitude (4/2) ajustable. Ne PAS combiner avec g_iplDelay.
    bool    g_iplFetch    = []{ const char* s = std::getenv("NEOST_IPLFETCH"); return s && std::atoi(s) != 0; }();
    int     g_iplFetch4   = []{ const char* s = std::getenv("NEOST_IPLFETCH4"); return s ? std::atoi(s) : 4; }();
    int     g_iplFetch2   = []{ const char* s = std::getenv("NEOST_IPLFETCH2"); return s ? std::atoi(s) : 2; }();
    // DIAG handler-trace (cf. site d'appel Cpu68k::run) : dump cycle-exact d'une itération
    // du handler HBL d'EL en jeu, pour le diff Moira↔WinUAE.
    bool     g_htraceOn = []{ const char* s = std::getenv("NEOST_HTRACE"); return s != nullptr; }();
    uint32_t g_htPc     = []{ const char* s = std::getenv("NEOST_HTRACE_PC");   return s ? (uint32_t)std::strtoul(s, nullptr, 16) : 0x3862u; }();
    int      g_htSkip   = []{ const char* s = std::getenv("NEOST_HTRACE_SKIP"); return s ? std::atoi(s) : 2000; }();
    int      g_htN      = []{ const char* s = std::getenv("NEOST_HTRACE_N");    return s ? std::atoi(s) : 400; }();
    // DEEP convergence WinUAE (opt-in NEOST_RAM_SLOT) : alignement créneau bus 4 cyc à
    // 8 MHz pour la RAM ST (CHIP16 < $400000), port FIDÈLE de wait_cpu_cycle_read/write
    // (custom.c:148-153). Avant un accès RAM, si l'horloge bus n'est pas sur la grille
    // de 4, attendre (4 - slot) cycles. ⚠ CONTRAIREMENT au chipWait16 (16 MHz), AUCUN +4
    // additionnel : à 8 MHz Moira facture DÉJÀ les 4 cyc de l'accès. L'ancienne tentative
    // « chipWait8 » (mémoire beamsync-busalign-falsified) ajoutait ce +4 parasite (miroir
    // erroné du 16 MHz) → sur-comptait (overscan_top 32→36). Align-only NE sur-compte pas
    // (boucle 4-phasée → +0 ; boucle non-phasée comme `bra.s self` → +2 = WinUAE). ROM/
    // cartouche/IO sont FAST (pas d'alignement, mesuré sur STF). NEOST_RAM_SLOT_PHASE
    // décale la phase de la grille (calibration oracle si offset constant Moira↔WinUAE).
    // DÉFAUT ON (2026-06-17) : convergence cycle d'instruction (créneau bus RAM) + base de la
    // dérive faisceau beam-sync (cf. [[ramslot-iack-enable-overscan]]). NEOST_RAM_SLOT=0 désactive.
    bool    g_ramSlot     = []{ const char* s = std::getenv("NEOST_RAM_SLOT"); return s ? std::atoi(s) != 0 : true; }();
    int     g_ramSlotPhase= []{ const char* s = std::getenv("NEOST_RAM_SLOT_PHASE"); return s ? std::atoi(s) : 0; }();
    // A34 (2026-08-28) — LE MODÈLE DE DISPATCH EST TRANCHÉ, il n'y en a plus qu'un.
    // Modèle BLOC : sync() n'avance QUE l'horloge ; le dispatch des événements se fait
    // par runTo à la frontière d'événement (cf. Machine::runFrame). PT=true (datation
    // sous-instruction) et RAM_SLOT sont CONSERVÉS — la convergence cycle d'instruction
    // est indépendante du modèle de dispatch.
    // Le concurrent (« sync-driven » : dispatch mid-instruction, modèle do_cycles de
    // WinUAE) vivait derrière NEOST_SYNC_DISPATCH. Il est SUPPRIMÉ : il deadlockait
    // Enchanted Land (boucle beam-sync jamais servie) sans corriger le jitter qu'il
    // promettait, et la mesure re-prise le 2026-08-28 sur l'arbre du jour le confirme —
    // palier `fast` ROUGE, blitter_timer à 245 px là où le modèle BLOC est à 0.
    // Pré-armement des broches IRQ vidéo (cf. Cpu68k::armHblPinAt/.hpp) : cycle BUS
    // auquel la broche doit monter, appliqué par sync() en cours d'instruction.
    // −1 = inactif. g_cur->pinNextDue = min des deux (chemin chaud O(1) dans sync()).
    // DIAG (NEOST_BUS_DIAG=<préfixe PC sur 8 bits, hexa>) — cf. busDiag. Drapeau
    // NAMESPACE et pas statique LOCAL : en statique local, chaque accès bus du CPU
    // franchissait la garde d'initialisation du singleton ET évaluait getClock() pour
    // l'argument, alors que le diagnostic est désactivé en exploitation. Ici, le test
    // se réduit à la lecture d'un global et les sites d'appel peuvent l'éviter en amont.
    long    g_busDiagPage = []{ const char* s = std::getenv("NEOST_BUS_DIAG");
                                return s ? std::strtol(s, nullptr, 16) : -1L; }();
    inline void recomputePinNextDue() {
        g_cur->pinNextDue = g_cur->hblPinDue;
        if (g_cur->vblPinDue >= 0 && (g_cur->pinNextDue < 0 || g_cur->vblPinDue < g_cur->pinNextDue))
            g_cur->pinNextDue = g_cur->vblPinDue;
    }
    inline int64_t busOfClock(int64_t c) {
        return g_cur->cpuMul == 1 ? c + g_cur->cpuBias : (c + g_cur->cpuBias) >> 1;
    }
    inline int64_t cpuClockForBus(int64_t b) {
        return g_cur->cpuMul == 1 ? b - g_cur->cpuBias : (b << 1) - g_cur->cpuBias;
    }
    void    neostUpdateIpl(bool commit = false);   // recalcule l'IPL présenté au cœur
    void    noteBlitterPreStart();   // accès CPU pendant la fenêtre PRE_START du blitter ?
}

// -----------------------------------------------------------------------------
//  Backend Moira (cœur 68000 cycle-exact, MIT) — sous-classe routant la mémoire
//  vers le Bus et reproduisant le vectoring ST (MFP vectorisé niveau 6,
//  VBL/HBL auto-vectorisés) via readIrqUserVector (irqMode USER).
// -----------------------------------------------------------------------------
// A33 : au scope GLOBAL (plus dans un namespace anonyme) — CpuState est déclaré
// dans Cpu68k.hpp et porte un NeostMoira*, donc les deux doivent désigner LE
// MÊME type ; un type à liaison interne n'aurait pas pu convenir.
class NeostMoira : public moira::Moira {
public:
    NeostMoira() {
        setModel(moira::Model::M68000);
        irqMode = moira::IrqMode::USER;
        // Syntaxe Musashi : conserve le format de trace historique (comparaison MAME).
        setDasmSyntax(moira::Syntax::MUSASHI);
        // Délai de reconnaissance IPL fidèle WinUAE (opt-in). Seuils en clock-units =
        // cyc × g_cur->cpuMul (1 au boot = 8 MHz ST). Réappliqué si la vitesse change.
        if (g_iplFetch) setIplDelay(static_cast<int64_t>(g_iplFetch4) * g_cur->cpuMul,
                                    static_cast<int64_t>(g_iplFetch2) * g_cur->cpuMul);
    }

    // Une adresse non décodée déclenche une bus error : on lève l'exception
    // moira::BusError (rattrapée par Moira::execute → execBusError) avec une trame
    // d'exception de groupe 0 identique à celle que construit Moira::makeFrame
    // (privée, donc reproduite ici). C'est ainsi qu'EmuTOS sonde le matériel
    // optionnel — sans ça, Moira lit l'adresse fantôme et la détection HW d'EmuTOS
    // part en vrille (bureau GEM sans menu ni curseur).
    // Instruction RESET ($4E70) : Moira ne fait qu'y brûler 132 cycles (execReset =
    // SYNC + prefetch). Le hook didExecute, activé par MOIRA_DID_EXECUTE dans
    // MoiraConfig.h, permet d'y brancher la broche /RESET des périphériques — ce que
    // fait customreset() chez Hatari (cpu/hatari-glue.c:54), appelé depuis cpureset().
    // On ne touche NI au CPU NI à l'ordonnanceur : seule la ligne /RESET est assertée.
    void didExecute(const char* /*func*/, moira::Instr I, moira::Mode, moira::Size,
                    moira::u16) override {
        if (I != moira::Instr::RESET || !g_cur->bus) return;
        g_cur->vblPending = g_cur->hblPending = false;   // ≙ pendingInterrupts = 0
        g_cur->bus->peripheralReset();
        neostUpdateIpl();
    }

    // ---- Opcodes magiques GEMDOS HD (port de OpCode_GemDos/Pexec/SysInit) ----
    // La cartouche système ($FA0000) place des opcodes « illégaux » magiques
    // (8=GEMDOS, 9=PEXEC, 10=SYSINIT). Quand le HD GEMDOS est actif et que le PC est
    // DANS la cartouche, on traite l'appel en C (lit/écrit les registres + pose les
    // codes condition du SR) et Moira consomme l'opcode comme un NOP — exactement
    // comme CpuDoNOP chez Hatari. Hors cartouche, un vrai $0008 reste illégal.
    //
    // ⚠ Comme Hatari, le traitement vit DANS le gestionnaire de l'instruction
    // (execIllegal), pas avant execute() : il n'a lieu que si l'instruction s'exécute
    // vraiment. L'ancienne interception, faite avant execute(), servait l'appel sur
    // l'hôte PUIS laissait execute() prendre une interruption en attente : le PC
    // empilé désignait encore l'opcode magique, rejoué au rte → un Fread servi deux
    // fois, un bloc de fichier sauté en silence (TOSFC, Verify sur C:).
    bool illegalOpcodeHook(moira::u16 opcode) override {
        if (opcode < 0x0008 || opcode > 0x000A || !g_cur->bus || !g_cur->bus->gemdos)
            return false;
        const moira::u32 pc0 = getPC0() & 0x00FFFFFF;
        if (pc0 < 0xFA0000 || pc0 >= 0xFC0000) return false;
        // Filet : les primitives de GemdosHd sont non-fautives (checkArea) ; une
        // BusError qui s'échapperait (régression future) abandonne l'appel au lieu
        // de devenir une bus error invitée sur un opcode de cartouche.
        try { return g_cur->bus->gemdos->handleOpcode(opcode); }
        catch (const moira::BusError&) { return true; }
    }

    // ---- Traçage des exceptions de groupe 0 (bus error / address error) --------
    // Moira notifie ces deux délégués À L'ENTRÉE (MoiraExceptions_cpp.h:268 et :305)
    // et À LA SORTIE (:296 et :333) de execAddressError/execBusError. On s'en sert
    // uniquement pour retenir le vecteur en cours de prise : c'est la seule donnée
    // FIABLE dont dispose cpuDidHalt() (cf. plus bas — reg.pc0 a déjà été avancé par
    // le prefetch de l'instruction fautive, il ne désigne PAS la faute).
    void willExecute(moira::M68kException exc, moira::u16 vector) override {
        if (exc == moira::M68kException::BUS_ERROR ||
            exc == moira::M68kException::ADDRESS_ERROR) g_cur->grp0Vector = int(vector);
    }
    void didExecute(moira::M68kException exc, moira::u16) override {
        if (exc == moira::M68kException::BUS_ERROR ||
            exc == moira::M68kException::ADDRESS_ERROR) g_cur->grp0Vector = 0;
    }

    // ---- Halt du 68000 -------------------------------------------------------
    // Moira notifie ce hook depuis halt() (Moira.cpp:511-518), appelé par les
    // catch DoubleFault/AddressError/BusError de processException (Moira.cpp:457-479)
    // et par le catch(...) de reset() (Moira.cpp:236-239). NeoST s'y arrêtait en
    // SILENCE : l'écran se fige définitivement et RIEN n'explique pourquoi. Hatari,
    // lui, l'annonce — gui-sdl/dlgHalt.c:66-71 journalise « Detected double
    // bus/address error => CPU halted! » (puis quitte sous --run-vbls, ou ouvre un
    // dialogue reset chaud/froid/débogueur/quitter). On porte l'INFORMATION, pas la
    // boîte de dialogue.
    //
    // Cas concret : Stardust (jeu STE) lancé sur ST lit $FFFF8900 (son DMA STE,
    // absent du ST) → bus error ; le handler du TOS recharge ensuite un A7 IMPAIR
    // depuis une pile utilisateur corrompue et le push suivant prend une erreur
    // d'adresse avec SSP impair = double faute (porte Hatari newcpu.c:3076-3079,
    // porte Moira MoiraExceptions_cpp.h:281-282). Le halt lui-même est FIDÈLE
    // (mesuré : même instruction que Hatari) — seule l'observabilité manquait.
    //
    // ⚠ On n'affiche PAS de PC : getPC0() vaut ici l'instruction SUIVANTE, le
    // prefetch de l'instruction fautive (MoiraDataflow_cpp.h:586 `reg.pc0 = reg.pc`)
    // ayant lieu AVANT le test d'alignement (MoiraExec_cpp.h:2874-2886). Hatari
    // n'en affiche pas non plus dans son message. Le vecteur et le SSP, eux, sont
    // exacts au moment de la faute.
    void cpuDidHalt() override {
        if (g_cur->inReset) {
            std::fprintf(stderr,
                "[cpu] 68000 halted: reset vector fetch failed (SSP/PC unreadable).\n");
            return;
        }
        // A42 : on NOMME la faute quand on l'a. `faultAddr/faultPc` sont ceux du
        // DERNIER accès fauté — celui qui a provoqué la double faute. Hatari, lui,
        // journalise la PREMIÈRE (celle dont l'exception a pu être prise) et se tait
        // sur la seconde, puisque son `cpu_halt` sort avant le Log_Printf
        // (newcpu.c:3076-3090). Les deux adresses peuvent donc différer : pour voir
        // toute la chaîne, armer NEOST_FAULT_TRACE.
        if (g_cur->faultValid)
            std::fprintf(stderr,
                "[cpu] 68000 halted: double bus/address error while taking exception "
                "vector %d (SSP=$%08X).\n"
                "      Last fault: %s at address $%08X, PC=$%06X.\n"
                "      The emulated machine is frozen until reset.\n",
                g_cur->grp0Vector, static_cast<unsigned>(getSP()),
                g_cur->faultWrite ? "writing" : "reading",
                static_cast<unsigned>(g_cur->faultAddr),
                static_cast<unsigned>(g_cur->faultPc & 0xFFFFFFu));
        else
            std::fprintf(stderr,
                "[cpu] 68000 halted: double bus/address error while taking exception "
                "vector %d (SSP=$%08X).\n"
                "      The emulated machine is frozen until reset.\n",
                g_cur->grp0Vector, static_cast<unsigned>(getSP()));
    }

    [[noreturn]] void raiseBusError(moira::u32 addr, bool write) const {
        // A42 : on RETIENT la faute (adresse, PC, sens) — c'est tout ce qui manquait
        // pour nommer un halt. Port de l'esprit d'Hatari, qui garde ses
        // `last_fault_for_exception_3` / `last_writeaccess_for_exception_3` et les
        // affiche à la prise de l'exception (newcpu.c:3086).
        g_cur->faultAddr  = addr;
        g_cur->faultPc    = getPC0();
        g_cur->faultWrite = write;
        g_cur->faultValid = true;
        // ⚠ Journal OPT-IN (`NEOST_FAULT_TRACE`), pas par défaut. Hatari, lui, affiche
        // chaque bus error en WARN et doit filtrer les SONDAGES matériels du TOS par une
        // liste blanche d'adresses (M68000_IsVerboseBusError, m68000.c:572-621) — la
        // détection de machine en produit des dizaines par boot. On choisit le silence
        // par défaut plutôt qu'une liste blanche à maintenir : la faute est de toute
        // façon nommée là où elle compte, dans le message de halt.
        static const bool trace = std::getenv("NEOST_FAULT_TRACE") != nullptr;
        if (trace)
            std::fprintf(stderr, "[cpu] bus error %s at address $%08X, PC=$%06X\n",
                         write ? "writing" : "reading",
                         static_cast<unsigned>(addr),
                         static_cast<unsigned>(getPC0() & 0xFFFFFFu));
        moira::StackFrame f{};
        const moira::u16 ird = getIRD();
        // code = IR(15..5) | function-code(2..0) | bit4 R/W (1 = lecture sur 68000).
        f.code = (ird & 0xFFE0) | readFC() | (write ? 0 : 0x10);
        f.addr = addr;
        f.ird  = ird;
        f.sr   = getSR();
        f.pc   = getPC();
        f.fc   = readFC();
        f.ssw  = f.fc;
        throw moira::BusError(f);
    }

    // Déclenche la bus error, SAUF si une faute est déjà en cours (double bus
    // fault pendant l'empilement de trame) → on halte le CPU comme le vrai 68000,
    // au lieu de relancer une exception (qui aborterait l'hôte). Renvoie true si
    // halté (l'appelant doit alors fournir une valeur neutre).
    //
    // ⚠ Il faut LEVER DoubleFault, pas seulement poser le drapeau HALTED : poser un
    // drapeau ne dérobine pas la séquence en cours. execBusError poursuivait ses 7
    // empilements (absorbés en silence), puis jumpToVector<C>(2) lisait RÉELLEMENT le
    // vecteur et préfetchait — soit 3 accès bus APRÈS la faute, là où le vrai 68000
    // assère /HALT au deuxième échec et ne fait plus un seul cycle. Le throw remonte
    // jusqu'au catch(DoubleFault) de Moira::processException, qui appelle le vrai
    // halt() — lequel restaure aussi reg.pc = reg.pc0 et notifie cpuDidHalt(), deux
    // choses que le drapeau posé à la main sautait.
    bool faultOrHalt(moira::u32 a, bool write) const {
        if (g_cur->inBusError) throw moira::DoubleFault();
        g_cur->bus->megaSteCacheFlushIfEnabled();   // une bus error invalide le cache Mega STE (Hatari)
        g_cur->inBusError = true;
        raiseBusError(a, write);            // [[noreturn]] : lève moira::BusError
        return true;                        // inatteignable
    }

    // ---- Mega STE 16 MHz : accès mémoire cadencés bus + cache 16 Ko ------------
    // Port des mem_access_delay_*_megaste_16 de Hatari. Moira facture déjà 4 cycles
    // CPU par accès bus ; à 16 MHz un accès RAM ST réel en coûte 8 (le bus reste à
    // 8 MHz) APRÈS attente du créneau CPU/Shifter (le GSTMCU partage la RAM par
    // créneaux de 4 cycles bus = 8 cycles CPU 16 MHz). D'où : attente d'alignement
    // + 4 cycles additionnels, sauf hit du cache 16 Ko (RAM rapide dédiée, 4 cycles
    // = rien à ajouter). ROM/cartouche/IO sont « FAST » (aucun wait state, mesuré
    // sur vrai matériel par Hatari) → plein débit 16 MHz.
    void chipWait16() const {
        auto* self = const_cast<NeostMoira*>(this);
        const moira::i64 c = self->getClock();
        const int slot = int((c + g_cur->cpuBias) & 7);       // position dans le créneau bus
        self->setClock(c + ((8 - slot) & 7) + 4);
    }
    bool superNow() const { return (getSR() & 0x2000) != 0; }

    moira::u16 readMste16Mhz(moira::u32 a, int size) const {
        a &= 0x00FFFFFF;
        uint16_t v;
        if (a >= 0x400000) {                 // ROM/cartouche/IO : « FAST », plein 16 MHz
            v = size == 2 ? g_cur->bus->read16(a) : g_cur->bus->read8(a);
            if (g_cur->bus->megaSteCacheEnabled())
                g_cur->bus->megaSteCacheUpdate(a, size, v, false, superNow());
            return v;
        }
        const bool super = superNow();       // RAM ST, partagée avec le Shifter
        if (g_cur->bus->megaSteCacheEnabled() && g_cur->bus->megaSteCacheRead(a, size, v, super))
            return v;                        // hit : 4 cycles CPU (déjà facturés par Moira)
        chipWait16();                        // miss / cache off : accès cadencé bus 8 MHz
        v = size == 2 ? g_cur->bus->read16(a) : g_cur->bus->read8(a);
        if (g_cur->bus->megaSteCacheEnabled()) {
            if (size == 2) g_cur->bus->megaSteCacheUpdate(a, 2, v, false, super);
            // Lecture octet : le bus porte le MOT entier à cette adresse → la ligne
            // est remplie avec le mot pair complet (si cachable sans bus error).
            else if (g_cur->bus->megaSteCacheable(a & ~1u, 2, false, super))
                g_cur->bus->megaSteCacheUpdate(a & ~1u, 2, g_cur->bus->read16(a & ~1u), false, super);
        }
        return v;
    }

    void writeMste16Mhz(moira::u32 a, int size, moira::u16 v) const {
        a &= 0x00FFFFFF;
        if (a < 0x400000) chipWait16();      // écriture RAM ST : toujours cadencée bus
        if (size == 2) g_cur->bus->write16(a, v); else g_cur->bus->write8(a, moira::u8(v));
        if (g_cur->bus->megaSteCacheEnabled())    // write-through : maj du mot déjà caché
            g_cur->bus->megaSteCacheUpdate(a, size, v, true, superNow());
    }

    // Chaque accès CPU abouti latche le « dernier mot du bus de données » dans
    // Bus::cpuDb (≈ regs.db du cœur UAE : mot = valeur, octet = dupliqué sur les
    // deux voies — cf. cpu_prefetch.h). Les lectures en RAM « void » le relisent.
    void latchDb(moira::u16 v) const { g_cur->bus->cpuDb = v; }
    void latchDb8(moira::u8 v) const { g_cur->bus->cpuDb = moira::u16((moira::u16(v) << 8) | v); }

    // Alignement créneau bus 4 cyc (8 MHz) sur la RAM ST avant un accès — cf. g_ramSlot.
    // Port fidèle de wait_cpu_cycle_read (custom.c) : la GLUE partage la RAM en créneaux
    // de 4 cyc bus, le CPU attend son tour. Pas de +4 (l'accès est déjà facturé par Moira).
    // ⚠ PHASE (2026-07-02) : WinUAE aligne le DÉBUT de l'accès sur la grille — l'accès
    // occupe [4k, 4k+4], fin ≡ 0 (mod 4). Ce callback est appelé au point-MILIEU de
    // l'accès Moira (read<>/write<> : SYNC(2) · callback · SYNC(2)), soit début+2 → on
    // aligne (c − 2). L'ancienne version alignait c lui-même (= le milieu) → fin d'accès
    // ≡ 2 (mod 4), skew structurel de +2 vs Hatari sur TOUTE la datation MMIO (mesuré au
    // banc respulse : line_cyc Hatari ≡ 0 mod 4 en fin d'accès, NeoST ≡ 2). Les offsets
    // de datation Shifter (write +2, read −6) supposent CE repère. NEOST_RAM_SLOT_PHASE=2
    // restaure l'ancien alignement (A/B).
    void chipWait8(moira::u32 a) const {
        if (!g_ramSlot || (a & 0x00FFFFFFu) >= 0x400000u) return;
        auto* self = const_cast<NeostMoira*>(this);
        const moira::i64 c = self->getClock();
        const int slot = int((c + g_cur->cpuBias + g_ramSlotPhase - 2) & 3);
        if (slot) self->setClock(c + (4 - slot));
    }

    // DIAG (NEOST_BUS_DIAG=<pc-hex-préfixe-8bits>) : séquence bus (addr, horloge mod 4)
    // de chaque accès CPU quand PC0 est dans la page donnée — traque de phase créneau.
    void busDiag(char k, moira::u32 a, moira::i64 c) const {
        if (g_busDiagPage < 0) return;
        const moira::u32 pc = getPC0() & 0xFFFFFFu;
        if ((pc >> 8) != (moira::u32)g_busDiagPage) return;
        std::fprintf(stderr, "[BUS] %c pc=%06x a=%06x c=%lld m4=%d\n",
                     k, pc, a & 0xFFFFFFu, (long long)c, (int)(c & 3));
    }
    // Fin de cycle bus : /UDS remonte pour tout accès mot, ou octet à adresse PAIRE.
    // C'est l'horloge de la clé Cubase 2 (PAL16R8), qui voit CHAQUE cycle du CPU —
    // fetchs compris. Un seul test de bool quand aucune clé noire n'est branchée.
    void udsDone(moira::u32 a, int size) const { if (g_cur->bus->udsObserved && (size == 2 || !(a & 1))) g_cur->bus->udsCycle(a); }
    moira::u8  read8 (moira::u32 a) const override { if (g_cur->bus->blitterWinEnd >= 0 || g_cur->bus->blitterCountCpu) noteBlitterPreStart(); if (g_cur->bus->busFaultN(a, 1, false) && faultOrHalt(a, false)) return 0; moira::u8 v; if (g_cur->cpuMul == 2) v = moira::u8(readMste16Mhz(a, 1)); else { chipWait8(a); if (g_busDiagPage >= 0) busDiag('r', a, getClock()); v = g_cur->bus->read8(a); } latchDb8(v); udsDone(a, 1); return v; }
    moira::u16 read16(moira::u32 a) const override { if (g_cur->bus->blitterWinEnd >= 0 || g_cur->bus->blitterCountCpu) noteBlitterPreStart(); if (g_cur->bus->busFaultN(a, 2, false) && faultOrHalt(a, false)) return 0; moira::u16 v; if (g_cur->cpuMul == 2) v = readMste16Mhz(a, 2); else { chipWait8(a); if (g_busDiagPage >= 0) busDiag('R', a, getClock()); v = g_cur->bus->read16(a); } latchDb(v); udsDone(a, 2); return v; }
    void write8 (moira::u32 a, moira::u8  v) const override { if (g_cur->bus->blitterWinEnd >= 0 || g_cur->bus->blitterCountCpu) noteBlitterPreStart(); if (g_cur->bus->busFaultN(a, 1, true)) { if (faultOrHalt(a, true)) return; } latchDb8(v); if (g_cur->cpuMul == 2) { writeMste16Mhz(a, 1, v); udsDone(a, 1); return; } chipWait8(a); g_cur->bus->write8(a, v); udsDone(a, 1); }
    void write16(moira::u32 a, moira::u16 v) const override { if (g_cur->bus->blitterWinEnd >= 0 || g_cur->bus->blitterCountCpu) noteBlitterPreStart(); if (g_cur->bus->busFaultN(a, 2, true)) { if (faultOrHalt(a, true)) return; } latchDb(v); if (g_cur->cpuMul == 2) { writeMste16Mhz(a, 2, v); udsDone(a, 2); return; } chipWait8(a); g_cur->bus->write16(a, v); udsDone(a, 2); }
    // Débogueur : watchpoint mémoire atteint (appelé par la couche dataflow de Moira
    // PENDANT l'accès, si CHECK_WP). Sémantique « break-after-access » : on note le hit
    // (adresse DONNÉE) et on préempte → le run() rend la main APRÈS l'instruction fautive.
    void didReachWatchpoint(moira::u32 addr) override {
        g_cur->bpHit = true; g_cur->bpAddr = addr & 0xFFFFFFu; g_cur->bpWatch = true; g_cur->endSlice = true;
    }

    // Save-state : tout l'état d'exécution du cœur (les membres protégés de Moira sont
    // accessibles ici). reg/queue/StatusRegister sont des PODs → transférés en bloc.
    void serializeState(StateArchive& ar) {
        ar(clock); ar(reg); ar(queue); ar(ipl); ar(iplPrev);
        // `reg` est copié EN BLOC, donc les NEUF booléens de son registre d'état
        // (moira::StatusRegister : t1 t0 s m x n z c v) échappent à la normalisation
        // de StateArchive::operator(), qui ne voit que le type de l'agrégat. Un octet
        // valant autre chose que 0 ou 1 dans un bool est un COMPORTEMENT INDÉFINI —
        // et `s` est le bit SUPERVISEUR : les deux branches d'un `if (sr.s)` peuvent
        // être prises. C'est le plus lourd des sites de cette famille, et il avait été
        // manqué le 2026-09-01 parce que seuls les trois sites NOMMÉS avaient été
        // corrigés au lieu d'auditer. Liste complète établie depuis : cf. le bandeau
        // de fixBools() dans StateArchive.hpp.
        ar.fixBools(reg.sr.t1, reg.sr.t0, reg.sr.s, reg.sr.m,
                    reg.sr.x, reg.sr.n, reg.sr.z, reg.sr.v, reg.sr.c);
        ar(iplChangeClock); ar(iplChangeClockPrev); ar(iplDelay4); ar(iplDelay2);
        ar(fcl); ar(readBuffer); ar(flags);
        // LOOPING (mode loop 68010) n'est JAMAIS posé sur un 68000 : forgé dans un
        // .state (CRC recalculé), il fait prendre à execute() la branche
        // `(this->*loop[queue.ird])` dont l'assert de garde saute en Release —
        // pointeur-membre NUL pour la quasi-totalité des opcodes → SIGSEGV.
        // Même classe de durcissement que g_cur->cpuMul / enums IKBD (passes 8-9).
        ar.check(!(flags & moira::State::LOOPING), "Moira::flags LOOPING forgé (68000)");
    }

    // Lecture du vecteur de reset (SSP/PC) via l'overlay ROM : jamais de bus error.
    moira::u16 read16OnReset(moira::u32 a) const override { const moira::u16 v = g_cur->bus->read16(a); latchDb(v); return v; }
    // Lecture pour le désassembleur : pas d'effet de bord MMIO ni de bus error
    // (équivaut aux anciens m68k_read_disassembler_* de Musashi). peek16 lit la
    // RAM/ROM sans dispatcher vers les puces ni avancer l'horloge (get_iword_debug).
    moira::u16 read16Dasm(moira::u32 a) const override { return g_cur->bus->peek16(a); }

    // Le 68000 est-il en attente (instruction STOP) ? Permet à la boucle d'horloge
    // de SAUTER l'attente au lieu de la simuler cycle par cycle (cf. run()).
    bool isStopped() const { return (flags & moira::State::STOPPED) != 0; }

    // ---- Ordonnanceur PILOTÉ PAR L'HORLOGE (modèle `do_cycles` WinUAE/Hatari) ----
    // Hook cycle de Moira. Moira l'appelle à chaque pas : en PRECISE_TIMING, AVANT
    // chaque accès mémoire (sous-instruction) ; sinon, en fin d'instruction (cf.
    // MoiraMacros SYNC/CYCLES). On avance l'horloge du cœur de `n`, PUIS on dispatche
    // les événements échus de l'ordonnanceur — EN COURS d'instruction. L'IPL est donc
    // posé au CYCLE EXACT de l'événement (Timer-B/VBL/HBL) et vu par le POLL_IPL de
    // l'instruction COURANTE, au lieu d'être posé ~1 instruction plus tard à la
    // frontière de bloc (cause racine du jitter beam-sync). syncTo est O(1) tant que
    // rien n'est dû (cache nextDue_) — sync() est appelé très souvent. Pas de
    // réentrance : les callbacks de l'ordonnanceur n'exécutent pas le CPU.
    void sync(int n) override {
        setClock(getClock() + n);             // défaut Moira (avance l'horloge du cœur)
        // Broches IRQ vidéo PRÉ-ARMÉES (HBL/VBL) : montée au cycle bus EXACT, en
        // cours d'instruction — l'instruction qui ENJAMBE l'événement la voit à son
        // POLL_IPL, comme WinUAE (pin posée dans do_cycles). Cf. Cpu68k::armHblPinAt.
        if (g_cur->pinNextDue >= 0 && busOfClock(static_cast<int64_t>(getClock())) >= g_cur->pinNextDue) {
            const int64_t busNow = busOfClock(static_cast<int64_t>(getClock()));
            if (g_cur->hblPinDue >= 0 && busNow >= g_cur->hblPinDue) { g_cur->hblPinDue = -1; g_cur->hblPending = true; }
            if (g_cur->vblPinDue >= 0 && busNow >= g_cur->vblPinDue) { g_cur->vblPinDue = -1; g_cur->vblPending = true; }
            recomputePinNextDue();
            neostUpdateIpl();
        }
        // DEEP cpuipldelay : propage le niveau IPL désiré vers la broche une fois le délai de
        // reconnaissance (4 cyc) écoulé depuis son changement → POLL_IPL le voit alors, comme
        // WinUAE ipl_fetch_next. Gated NEOST_IPLDELAY ; sinon setIPL est immédiat (cf. neostUpdateIpl).
        if (g_iplDelay && g_cur->desiredIpl != g_cur->appliedIpl
            && getClock() - g_cur->iplChgClock >= static_cast<int64_t>(g_iplDelayCyc) * g_cur->cpuMul) {
            setIPL(static_cast<moira::u8>(g_cur->desiredIpl));
            g_cur->appliedIpl = g_cur->desiredIpl;
        }
    }

    // Committe l'IPL : broche + registre échantillonné (reg.ipl). setIPL ne pose que
    // la broche, que Moira n'échantillonne qu'au point de poll de l'instruction
    // SUIVANTE (pipeline IPL fidèle au 68000) — correct pour un changement en plein
    // accès MMIO, mais à une frontière d'instruction (événement daté MFP_IRQ, délai
    // 4 cyc déjà écoulé) l'exception doit partir AVANT l'instruction suivante,
    // comme Hatari (MFP_ProcessIRQ au test de frontière). Cf. Cpu68k::updateIplNow.
    void commitIpl(moira::u8 lvl) {
        setIPL(lvl);                       // broche (+ CHECK_IRQ si changement)
        reg.ipl = lvl;                     // déjà échantillonné : visible immédiatement
        flags |= moira::State::CHECK_IRQ;  // force le re-test même si la broche n'a pas bougé
    }

    // Une IRQ est-elle DÉJÀ prenable (niveau échantillonné > masque SR) ? Sert au
    // saut d'attente STOP de Cpu68k::run : si oui, le prochain execute() sort du
    // STOP au cycle COURANT — il ne faut PAS téléporter l'horloge au prochain
    // événement (le vrai 68000, niveau-sensible, sert l'IRQ immédiatement ; cas
    // mesuré : « stop #$2100 » avec HBL pendante — raster Super Hang-On).
    bool irqDeliverable() const { return reg.ipl > reg.sr.ipl || reg.ipl == 7; }

    // CPU en attente STOP ? (fenêtre de commit HBL/VBL : le réveil STOP est
    // niveau-sensible et immédiat — la fenêtre d'échantillonnage ne s'y applique pas).
    bool stoppedState() const { return (flags & moira::State::STOPPED) != 0; }

    // Synchro E-clock à l'ENTRÉE d'exception (port de Hatari M68000_WaitEClock,
    // m68000.c:810 + iack_cycle newcpu.c:2971-2990) : les IRQ AUTO-VECTORISÉES HBL
    // (niv 2) et VBL (niv 4) attendent le prochain front de l'E-clock (bus/10 =
    // 0,8 MHz) avant de lire leur vecteur → 0..8 cyc selon la phase de l'horloge
    // BUS au moment de l'IACK (motif déterministe {0,8,6,4,2}). Les IRQ VECTORISÉES
    // MFP (niv 6) et SCC (niv 5) n'ont PAS d'attente E-clock (elles fournissent leur
    // vecteur). C'est CE jitter d'entrée qui manquait à NeoST et déphasait le CPU
    // vis-à-vis du faisceau trame à trame. willInterrupt est appelé AVANT
    // l'empilement de trame (MoiraExceptions_cpp.h:508) → latence d'entrée PURE,
    // le timing des instructions reste intact.
    void willInterrupt(moira::u8 level) override {
        if (g_iackOn) {                                          // IACK fidèle WinUAE (E-clock @ IACK + bloc occupé)
            if (g_iackAt) return;                                // ← nouveau modèle : tout se fait AU point d'IACK
                                                                 //   (iackSyncBefore/After), plus rien ici.
            // LEGACY (NEOST_IACK_AT=0) : E-clock + bloc appliqués AVANT l'entrée d'exception,
            // avec un lead-in constant. ⚠ Mesuré (2026-07-02) : SUR-COMPTE de +8 (les SYNC(4)+
            // SYNC(4) stock de Moira restent facturés en plus du bloc) et la phase E-clock est
            // fausse (l'alignement bus du push PClo re-quantifie l'horloge APRÈS ce calcul →
            // positions d'impulsion uniformes mod 4 au lieu du motif mod 20 de Hatari).
            if (level == 2 || level == 4) {                      // auto-vecteur HBL/VBL : E-clock + bloc vidéo
                const int64_t busClk = busOfClock(getClock() + g_iackLead) + g_eclockPhase;
                int wait = 10 - static_cast<int>(((busClk % 10) + 10) % 10);
                if (wait == 10) wait = 0;
                const int add = wait + g_iackVideo;
                if (add) setClock(getClock() + static_cast<int64_t>(add) * g_cur->cpuMul);
            } else if (level == 6) {                             // MFP vectorisé : bloc MFP, pas d'E-clock
                if (g_iackMfp) setClock(getClock() + static_cast<int64_t>(g_iackMfp) * g_cur->cpuMul);
            }
            return;
        }
        if (!g_eclockOn || (level != 2 && level != 4)) return;    // opt-in ; auto-vecteurs HBL/VBL seulement
        const int64_t busClk = busOfClock(static_cast<int64_t>(getClock())) + g_eclockPhase;
        int wait = 10 - static_cast<int>(busClk % 10);           // cycles BUS jusqu'au prochain front E
        if (wait == 10) wait = 0;                                // déjà aligné
        if (wait) setClock(getClock() + static_cast<int64_t>(wait) * g_cur->cpuMul);
    }

    // Port FIDÈLE de Hatari `iack_cycle` (newcpu.c:2958-3019) AU point d'IACK du 68000
    // (execInterrupt<C68000> : entre le push PClo et les pushes SR/PChi — cf. hooks
    // Moira.h). Remplace les SYNC(4)+SYNC(4) stock (total 8) par :
    //   HBL/VBL (niv 2/4) : E-clock wait (0..8, motif [0 8 6 4 2], calculé ICI — la
    //     phase d'horloge est déjà quantifiée par l'alignement bus du push PClo,
    //     comme chez Hatari où M68000_WaitEClock lit l'horloge APRÈS ce push) puis
    //     CPU_IACK_CYCLES_VIDEO_CE(10) + idle(4) = g_iackVideo (14).
    //   MFP (niv 6) : vecteur lu immédiatement (comme MFP_ProcessIACK), puis
    //     CPU_IACK_CYCLES_MFP_CE(12) + idle(4) = g_iackMfp + 4.
    //   Autres (dont SCC niv 5) : stock Moira 4/4 (inchangé).
    // C'est CE placement qui fait émerger le motif mod-20 (E-clock mod-10 × créneau
    // bus mod-4) des positions d'entrée d'IRQ mesuré chez Hatari — l'ancien modèle
    // willInterrupt+lead le détruisait (positions uniformes mod 4). Cf.
    // docs/MOIRA_WINUAE_CONVERGENCE.md §8. NEOST_IACK_AT=0 restaure l'ancien modèle.
    int iackSyncBefore(moira::u8 level) override {
        if (!g_iackOn || !g_iackAt) return 4;                    // stock Moira
        if (level == 2 || level == 4) {                          // E-clock wait au cycle d'IACK
            const int64_t busClk = busOfClock(static_cast<int64_t>(getClock())) + g_eclockPhase;
            int wait = 10 - static_cast<int>(((busClk % 10) + 10) % 10);
            if (wait == 10) wait = 0;
            return wait * g_cur->cpuMul;
        }
        if (level == 6) return 0;                                // MFP : vecteur immédiat
        return 4;
    }
    int iackSyncAfter(moira::u8 level) override {
        if (!g_iackOn || !g_iackAt) return 4;                    // stock Moira
        if (level == 2 || level == 4) return g_iackVideo * g_cur->cpuMul;   // 10 (IACK→DTACK) + 4 idle
        if (level == 6) return (g_iackMfp + 4) * g_cur->cpuMul;             // 12 (IACK→DTACK) + 4 idle
        return 4;
    }

    moira::u16 readIrqUserVector(moira::u8 level) const override {
        // Port de « CycInt_Process() + MFP_UpdateIRQ_All au point d'IACK » d'Hatari
        // (newcpu.c:2938-2946 branche MFP, :2998-3003 branche vidéo — dans les DEUX
        // cas juste avant l'élection/l'acquittement). En mode bloc, les SYNC de
        // l'entrée d'exception ne dispatchent rien : un timer MFP dont l'échéance
        // tombe dans la fenêtre « frontière d'instruction → IACK » (~10-26 cyc)
        // n'avait pas posé son bit IPR quand Mfp::iack élisait le vecteur (l'élection
        // ne regarde que les IPR posés) → servi un cran trop tard. On dispatche les
        // événements échus AU cycle d'IACK réel. Sans danger pendant l'exception :
        // reg.sr.ipl est déjà monté au niveau servi (execInterrupt), un commit d'IPL
        // par un callback ne déclenche aucune prise parasite ; et le prochain
        // événement HBL/VBL est à une ligne entière (jamais dans la fenêtre).
        // ⚠ REBASE DU QUANTUM AVANT le dispatch, comme le saut STOP (cf. Cpu68k::run) :
        // sans lui, syncTo avance sched.now() jusqu'ici alors que `ran` (mesuré depuis
        // quantumStartBus_) sera ENCORE facturé par le runTo(now+ran) de Machine → le
        // temps écoulé compté DEUX FOIS, et les callbacks lisent liveNow() gonflé.
        // Mesuré sur le banc SHO : sans rebase, tout le raster glisse de ~16 cycles
        // (écritures palette {104..128} → {120..156}, réveil STOP {68,72,76} → {80..96}).
        if (g_iackSync && g_cur->sched && !g_cur->inReset && g_cur->cpuSelf) g_cur->cpuSelf->rebaseQuantumAndSync();
        if (level == 6 && g_cur->bus->mfp) {                 // MFP : vecteur fourni par le 68901
            const int v = g_cur->bus->mfp->iack();
            if (g_cur->tracer) g_cur->tracer->onInterrupt(level, v);
            neostUpdateIpl();
            return (v >= 0) ? moira::u16(v) : moira::u16(24);
        }
        if (level == 5 && g_cur->bus->scc) {                 // SCC série : vecteur vectorisé (IACK)
            const int v = g_cur->bus->scc->processIack();
            if (g_cur->tracer) g_cur->tracer->onInterrupt(level, v);
            neostUpdateIpl();
            return (v >= 0) ? moira::u16(v) : moira::u16(24);  // NV armé → vecteur spurious 24 ($60),
                                                               // comme le MFP et Hatari (iack_cycle : vector<0 → 24)
        }
        if (g_cur->tracer) g_cur->tracer->onInterrupt(level, 24 + level);   // VBL/HBL auto-vectorisés
        if (level == 4) g_cur->vblPending = false; else if (level == 2) g_cur->hblPending = false;
        neostUpdateIpl();
        return moira::u16(24 + level);
    }
};

namespace {
// Accès bus CPU signalé au blitter non-hog (Moira seul). Deux rôles, port des
// hooks Blitter_HOG_CPU_mem_access_before/after d'Hatari :
//  - bug « 63 accès » (phase PRE_START) : si l'accès tombe dans la fenêtre de
//    4 cycles précédant la prise de bus, le blitter le compte à tort comme un de
//    SES accès (la tranche suivante n'en fera que 63) ;
//  - fenêtre CPU (phase COUNT_CPU_BUS) : le blitter attend 64 accès bus CPU réels
//    avant de reprendre le bus — le 64ᵉ date la tranche suivante (+4 cycles).
// Date de l'accès = horloge de l'ORDONNANCEUR (Scheduler::liveNow), et pas celle du
// cœur : la fenêtre PRE_START est armée et la tranche reprogrammée avec liveNow()
// (Blitter.cpp:233-234), or les deux horloges sont décalées d'environ 40 cycles — les
// ~40 cycles que Moira::reset() consomme avant que la 1ʳᵉ trame n'ancre frameStart_ sur
// sched.now(). Comparer un timestamp du cœur à une fenêtre du domaine ordonnanceur
// ratait donc la fenêtre, et réinjecter ce timestamp dans schedule() décalait la
// reprise du blitter. (L'écart se résorbait par accident au 1ᵉʳ saut STOP, qui
// resynchronise les deux horloges.) L'ancrage de la trame sur sched.now() est, lui,
// DÉLIBÉRÉ — cf. Machine.cpp : l'ancrer sur busClockNow décalerait la grille faisceau.
void noteBlitterPreStart() {
    if (!g_cur->moira || !g_cur->bus->blitter) return;
    const int64_t t = g_cur->sched ? g_cur->sched->liveNow()
                              : busOfClock(static_cast<int64_t>(g_cur->moira->getClock()));
    if (t >= g_cur->bus->blitterWinStart && t < g_cur->bus->blitterWinEnd)
        g_cur->bus->blitter->notePreStartCpuAccess();
    if (g_cur->bus->blitterCountCpu)
        g_cur->bus->blitter->noteCpuBusAccess(t);
}

// Recalcule l'IPL présenté au CPU : MFP (6) > VBL (4) > HBL (2).
// `commit` (frontière d'instruction UNIQUEMENT) : pose aussi reg.ipl (valeur déjà
// échantillonnée) pour que l'exception parte avant l'instruction suivante — cf.
// NeostMoira::commitIpl.
void neostUpdateIpl(bool commit) {
    // Élection GROUPÉE du MFP, juste avant de lire son signal : c'est l'équivalent NeoST
    // de la boucle CPU d'Hatari (« if (MFP_UpdateNeeded) MFP_UpdateIRQ_All(0) »,
    // newcpu.c:3005 et 5509). Toutes les entrées d'interruption d'une même instruction
    // ont posé leur bit et leur date sans élire ; l'élection a donc ici la vue complète
    // et peut appliquer la règle « seules les plus anciennes concourent »
    // (`pendingTime_ <= pendingTimeMin_`). Ce site est le bon parce que TOUT callback
    // d'ordonnanceur susceptible de lever une IRQ MFP est suivi d'un updateIpl()
    // (cf. Machine.cpp) : aucune entrée ne peut donc rester non élue.
    if (g_cur->bus && g_cur->bus->mfp) g_cur->bus->mfp->flushIrqUpdate();
    const bool mfp6 = g_cur->bus && g_cur->bus->mfp && g_cur->bus->mfp->irqPending();
    int lvl;
    // MegaSTE : TOUTES les IRQ sont GATÉES par le SCU (SysIntMask/VmeIntMask) avant
    // d'atteindre le CPU — toujours actif comme `SCU_IsEnabled()` d'Hatari (= MegaSTE/TT).
    // Tout OS MegaSTE programme le SCU tôt au boot (TOS 2.06, EmuTOS 256K, diagnostic).
    if (g_cur->bus && g_cur->bus->machine == MachineType::MegaSte) {
        const bool scc5 = g_cur->bus->scc && g_cur->bus->scc->irqActive();  // SCC série niveau 5
        g_cur->bus->scu.syncState(mfp6, scc5, g_cur->vblPending, g_cur->hblPending);  // état ← sources vivantes
        lvl = g_cur->bus->scu.gatedLevel();                            // plus haut niveau autorisé
    } else {
        lvl = mfp6 ? 6 : g_cur->vblPending ? 4 : g_cur->hblPending ? 2 : 0;
    }
    if (g_cur->moira) {
        if (commit) { g_cur->moira->commitIpl(static_cast<moira::u8>(lvl)); g_cur->desiredIpl = g_cur->appliedIpl = lvl; }
        else if (g_iplDelay) {
            // cpuipldelay : enregistre le niveau désiré + l'horloge de changement ; ne propage à la
            // broche (POLL_IPL) que si le délai de 4 cyc est écoulé, sinon sync() le fera plus tard.
            if (lvl != g_cur->desiredIpl) { g_cur->desiredIpl = lvl; g_cur->iplChgClock = g_cur->moira->getClock(); }
            if (g_cur->moira->getClock() - g_cur->iplChgClock >= static_cast<int64_t>(g_iplDelayCyc) * g_cur->cpuMul) {
                g_cur->moira->setIPL(static_cast<moira::u8>(lvl)); g_cur->appliedIpl = lvl;
            }
        }
        else { g_cur->moira->setIPL(static_cast<moira::u8>(lvl)); g_cur->appliedIpl = lvl; }
    }
}
}

CpuCore Cpu68k::parseCore(const std::string& s) {
    std::string l;
    for (char c : s) l += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    // L'ancien cœur Musashi a été retiré : on tolère la valeur historique pour ne pas
    // casser les vieux neost.cfg / scripts, mais on AVERTIT et on reste sur Moira.
    if (l == "musashi" || l == "uae")
        std::fprintf(stderr, "[cpu] core \"%s\" removed — NeoST uses Moira (cycle-exact).\n", s.c_str());
    return CpuCore::Moira;
}

const char* Cpu68k::coreName(CpuCore) {
    return "moira";
}

// A33 : chaque Cpu68k possède SON état (state_) ; g_cur désigne celui qui tourne.
// Le `throw std::logic_error("Cpu68k supports only one live instance")` qui vivait
// ici a disparu — c'était le plafond qui interdisait le test unitaire d'une
// Machine, l'A/B en un processus et l'anneau MIDI à deux nœuds.
Cpu68k::Cpu68k(Bus& bus, CpuCore core) : core_(core), state_(new CpuState) {
    activate();                 // les callbacks Moira d'initCore visent CET état
    state_->bus = &bus;
    state_->cpuSelf = this;     // cf. rebaseQuantumAndSync (hook d'IACK)
    try {
        initCore();
    } catch (...) {
        state_->cpuSelf = nullptr;
        state_->bus = nullptr;
        throw;
    }
}

Cpu68k::~Cpu68k() {
    delete state_->moira;
    state_->moira = nullptr;
    // Ne pas laisser g_cur pendre sur un état détruit. S'il désignait une AUTRE
    // instance, on n'y touche pas : elle est toujours vivante.
    if (g_cur == state_.get()) g_cur = nullptr;
}

// Rend CETTE instance active : les callbacks Moira et les fonctions libres de ce
// fichier — qui n'ont pas de `this` — passent par g_cur. Appelée au constructeur et
// à l'entrée de run(), donc alterner deux CPU se fait sans cérémonie.
void Cpu68k::activate() const { g_cur = state_.get(); }

// Transfère à l'ordonnanceur le temps couru depuis le début du quantum, PUIS
// dispatche les événements échus — appelé au point d'IACK réel (≙ le
// « CycInt_Process() juste avant la séquence d'IACK » de Hatari, newcpu.c:2938).
// Le rebase est indispensable et doit précéder le dispatch : sans lui, `ran` (le
// retour de run(), mesuré depuis quantumStartBus_) serait facturé une SECONDE fois
// par le runTo(now+ran) de Machine, et les callbacks liraient un liveNow() gonflé.
// Même raisonnement — et même ordre — que le saut d'attente STOP de run().
void Cpu68k::rebaseQuantumAndSync() {
    if (!state_->sched || !state_->moira) return;
    const int64_t busNow = busOfClock(static_cast<int64_t>(state_->moira->getClock()));
    quantumStartBus_   = busNow;
    quantumStartClock_ = static_cast<int64_t>(state_->moira->getClock());
    // DIAG (NEOST_IACK_DISP=1) : compte les IACK où un événement était RÉELLEMENT
    // échu dans la fenêtre frontière→IACK (le cas que ce dispatch corrige) —
    // sert à prouver que le chemin n'est pas mort et à mesurer sa fréquence.
    static const bool diag = std::getenv("NEOST_IACK_DISP") != nullptr;
    if (diag) {
        static long total = 0, hits = 0;
        const int64_t nd = state_->sched->peekNextDue();
        ++total;
        if (nd >= 0 && nd <= busNow) ++hits;
        if (total % 100000 == 0)
            std::fprintf(stderr, "[IACKDISP] %ld/%ld IACK with a due event (%.3f %%)\n",
                         hits, total, 100.0 * double(hits) / double(total));
    }
    state_->sched->syncTo(busNow);
}

// (Ré)initialise le cœur Moira. Appelé par le constructeur ET par setCore()
// (reconfigure à chaud). Suppose qu'un éventuel ancien cœur a déjà été libéré.
void Cpu68k::initCore() {
    state_->moira = new NeostMoira();         // backend cycle-exact (irqMode/USER, M68000)
}

// Conservé pour compat (reconfigure à chaud) : libère l'ancien cœur puis ré-init.
// Cœur INCHANGÉ (le cas de tous les reconfigure : Moira est le seul cœur) → no-op :
// recréer l'objet effacerait les breakpoints/watchpoints posés dans le débogueur
// (l'état CPU, lui, est réinitialisé par le reset() que l'appelant enchaîne).
void Cpu68k::setCore(CpuCore core) {
    if (state_->moira && core == core_) return;
    if (state_->moira) { delete state_->moira; state_->moira = nullptr; }
    core_ = core;
    initCore();
}

void Cpu68k::setTracer(Tracer* t) {
    state_->tracer = t;
    if (t) t->setCpu(this);    // le Tracer lit les registres via ce CPU
}

void Cpu68k::setScheduler(Scheduler* s) {
    state_->sched = s;               // piloté depuis NeostMoira::sync (cf. en-tête hpp)
}

// Horloge BUS absolue live du cœur (cf. hpp). busOfClock intègre la bascule 8/16 MHz
// du Mega STE. Valide hors run comme en plein milieu d'une instruction.
int64_t Cpu68k::busClockNow() const {
    return busOfClock(static_cast<int64_t>(state_->moira->getClock()));
}

void Cpu68k::reset() {
    activate();   // A33 : cf. run()
    state_->bus->bootOverlay = true;
    state_->inReset = true;                    // sync() n'avance pas l'ordonnanceur pendant le reset
    state_->moira->reset();                    // lit SSP/PC via read16OnReset (overlay ROM)
    state_->inReset = false;
    state_->bus->bootOverlay = false;
}

int Cpu68k::run(int cycles) {
    activate();   // A33 : les callbacks Moira qui vont suivre visent CET état
    inRun_ = true;
    struct RunGuard { bool& f; ~RunGuard() { f = false; } } guard{inRun_};   // hors run → delta intra-quantum = 0
    const moira::i64 c0 = state_->moira->getClock();
    quantumStartClock_ = static_cast<int64_t>(c0);   // pour cyclesRunInQuantum()
    // Cible et résultat en cycles BUS (8 MHz). Le point de départ est FIGÉ ici :
    // une écriture $FF8E21 en plein quantum rebase la conversion (state_->cpuBias),
    // mais celle-ci reste continue → les deltas restent exacts.
    quantumStartBus_ = busOfClock(c0);
    // DIAG (NEOST_QDELTA_DIAG=<seuil>, inerte si la variable n'est pas posée) — sonde
    // de non-régression du chantier BL3, dans l'esprit de NEOST_IACK_DISP ci-dessus.
    // Mesure, À L'ENTRÉE du quantum, l'écart entre les deux horloges :
    //     delta = busOfClock(horloge CPU) − sched.now()
    // Tout cycle facturé à l'horloge CPU hors quantum sans être crédité à
    // l'ordonnanceur creuse ce delta : il n'est ni dans `ran` (mesuré depuis
    // quantumStartBus_, réancré juste au-dessus) ni dans now_. C'était le cas des
    // tranches non-hog du blitter — escalier de 136 en 136 jusqu'à 1088 juste avant le
    // plantage de Lethal Xcess en megast ; depuis Blitter::billCycles →
    // Scheduler::addStolenCycles, il reste PLAT.
    // ⚠ delta n'est PAS nul au démarrage : il vaut 40 (Moira::reset lit SSP/PC avant que
    // l'ordonnanceur ne démarre). CONSTANT, absorbé au 1ᵉʳ rebase d'IACK, SANS RAPPORT
    // avec le blitter — ne pas chercher à le « corriger ».
    {
        static const long qdiag = []{ const char* s = std::getenv("NEOST_QDELTA_DIAG");
                                      return s ? std::strtol(s, nullptr, 0) : -1; }();
        if (qdiag >= 0 && state_->sched) {
            const int64_t delta = quantumStartBus_ - state_->sched->now();
            static long    runs = 0, nonZero = 0;
            static int64_t maxDelta = 0, sumDelta = 0;
            ++runs;
            if (delta != 0) { ++nonZero; sumDelta += delta; if (delta > maxDelta) maxDelta = delta; }
            if (delta >= qdiag)
                std::fprintf(stderr, "[QDELTA] run#%ld busNow=%lld schedNow=%lld delta=%lld\n",
                             runs, (long long)quantumStartBus_,
                             (long long)state_->sched->now(), (long long)delta);
            if (runs % 100000 == 0)
                std::fprintf(stderr, "[QDELTA] recap runs=%ld nonzero=%ld (%.3f %%) max=%lld sum=%lld\n",
                             runs, nonZero, 100.0 * double(nonZero) / double(runs),
                             (long long)maxDelta, (long long)sumDelta);
        }
    }
    state_->endSlice = false;                              // un éventuel résidu de préemption ne doit pas couper le 1er pas
    const int64_t targetBus = quantumStartBus_ + cycles;
    while (busOfClock(state_->moira->getClock()) < targetBus) {
        state_->inBusError = false;                        // nouvelle instruction → faute précédente retombée
        if (state_->moira->isHalted()) { state_->moira->setClock(cpuClockForBus(targetBus)); break; }  // double bus fault → CPU arrêté
        instrStartClock_ = static_cast<int64_t>(state_->moira->getClock());   // repère « 1er accès » des wait states
        // DIAG (gated NEOST_HTRACE) — trace cycle-exact d'UNE itération du handler en jeu
        // pour le diff Moira↔WinUAE (traque du +20). Capture le PC + désasm AVANT execute
        // (sinon getPC0() renvoie l'instruction SUIVANTE → désalignement off-by-one), et le
        // coût en cycles APRÈS (delta d'horloge bus). S'arme au PC d'entrée (NEOST_HTRACE_PC)
        // après NEOST_HTRACE_SKIP passages, dump NEOST_HTRACE_N instr sur stderr.
        char htDis[200]; uint32_t htPc = 0; bool htEmit = false;
        if (g_htraceOn) {
            htPc = state_->moira->getPC0() & 0xFFFFFFu;
            if (!state_->htArmed && htPc == g_htPc) { if (g_htSkip > 0) --g_htSkip; else { state_->htArmed = true; state_->htPrev = busClockNow(); } }
            if (state_->htArmed && g_htN > 0) { disassemble(htDis, htPc); htEmit = true; }
        }
        // Débogueur : breakpoint « break-before » — stoppe AVANT d'exécuter l'instruction
        // ciblée (le run() rend la main, PC dessus). Le skip-once évite de re-déclencher à
        // la reprise (on est encore SUR le breakpoint). elements()==0 → coût nul.
        if (state_->moira->debugger.breakpoints.elements() != 0) {
            const uint32_t pc = state_->moira->getPC() & 0xFFFFFFu;
            if (pc == state_->bpSkipPc) {
                state_->bpSkipPc = 0xFFFFFFFFu;                 // laissé passer une fois
            } else if (state_->moira->debugger.breakpoints.isSetAt(pc)) {
                state_->bpHit = true; state_->bpAddr = pc; state_->endSlice = true; break;
            }
        }
        state_->moira->execute();                          // une instruction (sync() dispatche les events échus)
        if (state_->tracer) state_->tracer->onInstruction(state_->moira->getPC0());
        if (htEmit) {
            const int64_t now = busClockNow();
            std::fprintf(stderr, "HT %06X d=%-3lld %s\n", htPc, static_cast<long long>(now - state_->htPrev), htDis);
            state_->htPrev = now; --g_htN;
        }
        // Préemption du bloc CPU : runFrame arme beginRun à chaque bloc, donc elle est
        // toujours active (A34 : le second modèle d'exécution a été supprimé).
        if (state_->endSlice) { state_->endSlice = false; break; }
        // STOP : aucune instruction ne tournera tant qu'un événement ne change pas
        // l'IPL. Au lieu de simuler l'attente cycle par cycle (≈25× plus lent), on
        // saute au PROCHAIN événement armé (borné par la cible du bloc) et on le
        // DISPATCHE via syncTo : il peut lever l'IPL → l'execute() suivant sortira du
        // STOP au bon cycle. Sans événement avant la cible, on saute droit à la cible.
        // (≠ ancien modèle : on saute à l'EVENT, pas à la cible, sinon on zapperait
        // tous les events de la trame — la cible du bloc est maintenant la fin de trame.)
        if (state_->moira->isStopped() && !state_->moira->irqDeliverable()) {
            const int64_t busNow = busOfClock(state_->moira->getClock());
            if (busNow >= targetBus) break;
            const int64_t nd = state_->sched ? state_->sched->peekNextDue() : -1;
            const int64_t jumpTo = (nd > busNow && nd < targetBus) ? nd : targetBus;
            state_->moira->setClock(cpuClockForBus(jumpTo));
            // REBASE **AVANT** le dispatch — l'ordre compte. syncTo() exécute les
            // callbacks, et ceux-ci lisent l'heure via Machine::liveNow() = sched.now() +
            // cpu.cyclesRunInQuantum(). Rebaser après laissait cyclesRunInQuantum() valoir
            // encore (jumpTo − quantumStart) pendant tout le dispatch : les callbacks
            // voyaient liveNow() = jumpTo + δ, le temps écoulé compté DEUX FOIS. Mesuré
            // sur un boot EmuTOS nu : 1208 dispatches HBL sur 15780 (7,7 %), δ jusqu'à
            // 112 cycles ; ramenés à 0 par ce rebase (repro : NEOST_HBL_DIAG=1, comparer
            // « sched= » et « live= » — ils doivent être ÉGAUX par construction, le CPU
            // étant garé en STOP au cycle dispatché). Ce que ça corrigeait concrètement :
            // Mfp::updateIrq comparait cet instant gonflé à irqTime_+4, ratait la fenêtre
            // et armait MFP_IRQ trop tard — un retard d'IRQ Timer B (rasters) qui
            // n'apparaissait QUE lorsque le CPU dormait en STOP, donc invisible des
            // étalons qui bouclent en polling.
            quantumStartBus_   = jumpTo;
            quantumStartClock_ = static_cast<int64_t>(state_->moira->getClock());
            if (state_->sched) state_->sched->syncTo(jumpTo);    // dispatche l'event au saut (peut lever l'IPL)
            // REBASE du quantum : syncTo vient d'avancer sched.now() jusqu'à jumpTo,
            // or `ran` (retour de run) et cyclesRunInQuantum() mesuraient encore depuis
            // l'ANCIEN début → le saut était compté DEUX fois (une par syncTo, une par
            // le runTo(now+ran) de Machine). Conséquence mesurée (EL in-game) :
            // sched.now()/liveNow prenait une avance δ = jumpTo − quantumStart (4..26 cyc)
            // sur l'horloge CPU pour tout le reste de la trame → TOUTE la datation vidéo
            // (beamClock, compteur $8209, écritures freq/res) décalée de δ vs les créneaux
            // bus — impossible sur le vrai matériel (même horloge) ; quand δ ≡ 2 (mod 4)
            // le calibrateur beam-sync d'Enchanted Land déverrouillait (lock 47 % vs 100 %
            // Hatari). Après rebase : liveNow == horloge CPU, ran n'inclut plus le saut.
            // (le rebase lui-même est fait AVANT le syncTo ci-dessus, cf. son commentaire)
        }
    }
    return static_cast<int>(busOfClock(state_->moira->getClock()) - quantumStartBus_);
}

// Wait states de bus (cf. en-tête / Hatari M68000_SyncCpuBus). Appelé par le Shifter
// PENDANT l'exécution d'une instruction (depuis read8/write8 d'un registre aligné 4
// cycles) : on avance l'horloge du cœur, ce qui rallonge l'instruction en cours et
// décale tous les accès suivants (la contention de bus du vrai matériel).
// ⚠ INVARIANT DE FACTURATION (chantier BL3) — cette primitive n'avance QUE l'horloge du
// cœur. Tant que l'appel a lieu DANS un run(), c'est suffisant : `ran` (retour de run,
// mesuré depuis quantumStartBus_) le capte et Machine::runFrame le reverse à
// sched.now(). Un appel HORS run(), lui, échapperait à l'ordonnanceur — c'était le bug
// BL3. Le SEUL facturateur hors quantum est Blitter::billCycles depuis Blitter::onSlice,
// et il crédite lui-même l'ordonnanceur (Scheduler::addStolenCycles) ; les cinq autres
// appelants (Shifter ×2, addPsgWaitCycles, addMfpWaitCycles, addAciaWaitCycles) sont
// tous sur un chemin d'accès mémoire, donc dans le quantum.
// On n'a délibérément PAS mis le crédit ici, ni d'assert « jamais hors run » : les
// helpers ci-dessous sont AUSSI atteignables par une lecture d'OBSERVATION hors machine
// — p.ex. serialLoopbackSelfTest (src/headless/main_headless.cpp) fait
// `machine.bus.read8(0xFFFA01)`, que Bus::read8 route vers addMfpWaitCycles(). Un hook
// générique ferait avancer l'horloge ÉMULÉE au gré d'une simple lecture de débogage.
void Cpu68k::addBusWaitCycles(int n) {
    if (n <= 0) return;
    // `n` est en cycles BUS (8 MHz) : à 16 MHz l'horloge du cœur compte des cycles
    // CPU, deux fois plus fins → ×state_->cpuMul.
    state_->moira->setClock(state_->moira->getClock() + n * state_->cpuMul);
}

// Wait states YM2149 PSG (port Hatari psg.c:PSG_WaitState). 4 cycles au PREMIER accès
// de l'instruction ; les accès suivants de la MÊME instruction n'ajoutent rien (le cas
// movem +4 cyc tous les 4 accès est omis : aucun logiciel réel n'accède au PSG via movem).
void Cpu68k::addPsgWaitCycles() {
    if (instrStartClock_ != psgPrevInstrClock_) {         // nouvelle instruction → 4 cyc
        psgPrevInstrClock_ = instrStartClock_;
        addBusWaitCycles(4);
    }
}

// Wait state MFP 68901 (port Hatari mfp.c : M68000_WaitState(4) sur CHAQUE handler de
// lecture/écriture de registre). 4 cycles à chaque accès, sans dédup par instruction.
void Cpu68k::addMfpWaitCycles() {
    addBusWaitCycles(4);
}

// Wait states ACIA 6850 (port Hatari acia.c:ACIA_AddWaitCycles). 6 cycles à chaque accès,
// plus la synchro sur l'E-Clock (1 MHz = CPU/10) UNIQUEMENT au premier accès de
// l'instruction : on patiente jusqu'au prochain multiple de 10 cycles (0..8 cyc, motif
// [0 8 6 4 2] ; port M68000_WaitEClock).
void Cpu68k::addAciaWaitCycles() {
    int cycles = 6;                                       // coût de base par accès
    if (instrStartClock_ != aciaPrevInstrClock_) {        // 1er accès ACIA de l'instruction
        aciaPrevInstrClock_ = instrStartClock_;
        // E-Clock = horloge BUS / 10 (1 MHz), indépendante du 8/16 MHz CPU MegaSTE
        // (Hatari M68000_WaitEClock travaille sur CyclesGlobalClockCounter).
        int toNextE = 10 - static_cast<int>(busOfClock(state_->moira->getClock()) % 10);
        if (toNextE == 10) toNextE = 0;                   // déjà aligné sur l'E-Clock
        cycles += toNextE;
    }
    addBusWaitCycles(cycles);
}

// Cycles écoulés depuis le début du quantum run() courant (cf. en-tête).
int64_t Cpu68k::cyclesRunInQuantum() const {
    if (!inRun_) return 0;     // hors run : l'horloge sched.now() est déjà à jour
    // En cycles BUS (8 MHz), domaine de l'ordonnanceur — d'où la conversion 16 MHz.
    return busOfClock(static_cast<int64_t>(state_->moira->getClock())) - quantumStartBus_;
}

void Cpu68k::endTimeslice() {
    state_->endSlice = true;   // testé après l'instruction courante (cf. run)
}

// --- Débogueur : breakpoints PC (délègue au conteneur Guards de Moira) ------------
void Cpu68k::setBreakpoint(uint32_t addr) {
    state_->moira->debugger.breakpoints.setAt(addr & 0xFFFFFFu);
}
void Cpu68k::clearBreakpoint(uint32_t addr) {
    state_->moira->debugger.breakpoints.removeAt(addr & 0xFFFFFFu);
}
void Cpu68k::clearAllBreakpoints() {
    state_->moira->debugger.breakpoints.removeAll();
}
bool Cpu68k::hasBreakpoint(uint32_t addr) const {
    return state_->moira->debugger.breakpoints.isSetAt(addr & 0xFFFFFFu);
}
int Cpu68k::breakpointCount() const {
    return static_cast<int>(state_->moira->debugger.breakpoints.elements());
}
bool Cpu68k::breakpointByIndex(int nr, uint32_t& outAddr) const {
    const auto a = state_->moira->debugger.breakpoints.guardAddr(nr);
    if (!a) return false;
    outAddr = *a & 0xFFFFFFu;
    return true;
}
bool     Cpu68k::breakpointHit() const       { return state_->bpHit; }
uint32_t Cpu68k::breakpointHitAddr() const   { return state_->bpAddr; }
bool     Cpu68k::breakpointHitIsWatch() const{ return state_->bpWatch; }
void Cpu68k::clearBreakpointHit() {
    // Skip-once UNIQUEMENT pour un breakpoint PC (le watchpoint est « break-after »,
    // le PC a déjà avancé au-delà de l'instruction fautive → rien à ignorer).
    if (!state_->bpWatch) state_->bpSkipPc = state_->bpAddr;
    state_->bpHit = false; state_->bpWatch = false;
}

// --- Débogueur : watchpoints mémoire (accès lecture/écriture d'une adresse) ------
// Délègue au conteneur Guards de Moira (debugger.watchpoints) ; la couche dataflow de
// Moira teste l'accès et appelle NeostMoira::didReachWatchpoint. setAt arme CHECK_WP.
void Cpu68k::setWatchpoint(uint32_t addr) {
    state_->moira->debugger.watchpoints.setAt(addr & 0xFFFFFFu);
}
void Cpu68k::clearWatchpoint(uint32_t addr) {
    state_->moira->debugger.watchpoints.removeAt(addr & 0xFFFFFFu);
}
void Cpu68k::clearAllWatchpoints() {
    state_->moira->debugger.watchpoints.removeAll();
}
bool Cpu68k::hasWatchpoint(uint32_t addr) const {
    return state_->moira->debugger.watchpoints.isSetAt(addr & 0xFFFFFFu);
}
int Cpu68k::watchpointCount() const {
    return static_cast<int>(state_->moira->debugger.watchpoints.elements());
}
bool Cpu68k::watchpointByIndex(int nr, uint32_t& outAddr) const {
    const auto a = state_->moira->debugger.watchpoints.guardAddr(nr);
    if (!a) return false;
    outAddr = *a & 0xFFFFFFu;
    return true;
}

// Save-state : état du cœur Moira + timing du wrapper + vitesse Mega STE (g_cur->cpuMul).
void Cpu68k::serialize(StateArchive& ar) {
    state_->moira->serializeState(ar);
    ar(state_->cpuMul);
    // state_->cpuMul ∈ {1 (8 MHz), 2 (16 MHz Mega STE)} : sélecteur de conversion horloge
    // bus↔CPU (busOfClock/cpuClockForBus) ET multiplicateur des wait-states
    // (addBusWaitCycles : n * state_->cpuMul, Cpu68k.cpp:736). Forgé énorme → débordement /
    // bond d'horloge Moira. On rejette toute autre valeur (rejoue le backup), comme
    // cpl_/lpf_/now_.
    ar.check(state_->cpuMul == 1 || state_->cpuMul == 2, "Cpu68k::state_->cpuMul hors {1,2}");
    // state_->cpuBias accompagne state_->cpuMul : rebasé à CHAQUE bascule 8/16 MHz et non nul
    // même après retour à 8 MHz — sans lui, busOfClock() rendrait des valeurs d'un
    // autre domaine que sched.now_ après load (fenêtres blitter, dispatch faux).
    ar(state_->cpuBias);
    // Broches IRQ vidéo pendantes (niveau-sensibles jusqu'à IACK) : un HBL tombé au
    // dernier runTo avant la frontière de save avec SR masqué serait perdu au load.
    ar(state_->vblPending); ar(state_->hblPending);
    // Modes opt-in NEOST_IPLDELAY / NEOST_PIN_ARM : latence IPL et broches armées.
    ar(state_->desiredIpl); ar(state_->appliedIpl); ar(state_->iplChgClock);
    ar(state_->hblPinDue); ar(state_->vblPinDue); ar(state_->pinNextDue);
    ar(quantumStartClock_); ar(quantumStartBus_);
    ar(instrStartClock_); ar(psgPrevInstrClock_); ar(aciaPrevInstrClock_);
}

// Cycles BUS écoulés depuis le début de l'instruction courante (cf. en-tête).
int64_t Cpu68k::cyclesIntoInstr() const {
    if (instrStartClock_ < 0) return 0;
    return busClockNow() - busOfClock(instrStartClock_);
}

// Bascule 8/16 MHz du Mega STE — cf. Cpu68k.hpp. Le rebasage de g_cur->cpuBias garde
// l'horloge bus CONTINUE au cycle courant (la bascule arrive en plein quantum,
// pendant l'écriture $FF8E21) : busOfClock(c) garde la même valeur avant/après.
void Cpu68k::setMegaSteSpeed(bool sixteenMhz) {
    const int mul = sixteenMhz ? 2 : 1;
    if (mul == state_->cpuMul) return;
    const int64_t c = static_cast<int64_t>(state_->moira->getClock());
    const int64_t b = busOfClock(c);
    state_->cpuMul  = mul;
    state_->cpuBias = (mul == 2) ? (2 * b - c) : (b - c);
    // Réajuste les seuils du délai IPL (en clock-units) à la nouvelle vitesse CPU.
    if (g_iplFetch) state_->moira->setIplDelay(static_cast<int64_t>(g_iplFetch4) * state_->cpuMul,
                                         static_cast<int64_t>(g_iplFetch2) * state_->cpuMul);
    std::fprintf(stderr, "[cpu] Mega STE: 68000 at %d MHz\n", sixteenMhz ? 16 : 8);
}

bool Cpu68k::megaSte16Mhz() const { return state_->cpuMul == 2; }

bool Cpu68k::supervisor() const {
    return (state_->moira->getSR() & 0x2000) != 0;
}

bool Cpu68k::halted() const { return state_->moira->isHalted(); }

// A42 — la dernière faute de groupe 0 (cf. Cpu68k.hpp § Fault). Le vecteur vient de
// `grp0Vector`, latché par willExecute et ENCORE posé quand la faute a doublé.
Cpu68k::Fault Cpu68k::lastFault() const {
    Fault f;
    f.valid  = state_->faultValid;
    f.addr   = state_->faultAddr;
    f.pc     = state_->faultPc & 0x00FFFFFFu;
    f.write  = state_->faultWrite;
    f.vector = state_->grp0Vector;
    return f;
}

void Cpu68k::updateIpl() {
    neostUpdateIpl();
}

namespace { bool raiseWindowDefers(); }   // définie plus bas (fenêtre d'échantillonnage IPL)

void Cpu68k::updateIplNow() {
    // Même fenêtre d'échantillonnage que raiseHbl/raiseVbl : un événement MFP dû
    // MOINS de K cycles avant la frontière n'a pas été vu par l'instruction en cours
    // (WinUAE ipl_fetch) → broche pollée, exception après l'instruction SUIVANTE.
    // Mesuré sur Super Hang-On in-game : l'exception Timer B (code varié interrompu)
    // partait ~4 cyc trop tôt en moyenne, décalant toute la chaîne « handler → stop →
    // HBL pendante » du raster (écritures palette à cyc 100 vs plancher oracle 104).
    neostUpdateIpl(/*commit=*/!raiseWindowDefers());
}

// NEOST_RAISE_COMMIT : bit0 = HBL, bit1 = VBL — commit IMMÉDIAT de l'IPL au
// dispatch de l'événement (≙ Hatari CE : CycInt traité à la frontière d'instruction
// + intlev_load/ipl_fetch_now sans délai → exc−broche = 0, mesuré à l'oracle
// instrumenté). DÉFAUT 3 = HBL + VBL (modèle fidèle complet, 2026-07-02) :
// l'ancien blocage « le commit VBL casse le loader d'Enchanted Land » était en
// réalité le DOUBLE COMPTAGE du saut STOP dans la comptabilité de quantum
// (cf. Cpu68k::run, rebase de quantumStartBus_) — corrigé, le commit VBL passe
// (loader OK, lock moteur 100 %, étalons TOUS OK). Sans commit (bit à 0) :
// broche pollée → reconnaissance UNE instruction plus tard (l'ancien modèle).
namespace { int g_raiseCommit = []{ const char* s = std::getenv("NEOST_RAISE_COMMIT");
                                    return s ? std::atoi(s) : 3; }();
// NEOST_RAISE_WINDOW (opt-in expérimental, défaut 0 = commit inconditionnel) :
// fenêtre d'échantillonnage IPL à la frontière (cyc bus). Modèle « WinUAE ipl_fetch » :
// un événement dû MOINS de K cycles avant la frontière serait différé d'une instruction.
// MESURÉ (2026-08-06, banc SHO in-game + oracle instrumenté) : les frontières de prise
// d'IRQ de Hatari CE correspondent au commit SANS différé (positions d'exception Timer B
// 404/408/412/416 quasi identiques des deux côtés) — l'écart résiduel était l'IACK MFP
// (cf. g_iackMfp = 16), PAS un différé. K>0 SUR-diffère (queue tardive excédentaire).
// Gardé en A/B pour de futures divergences de frontière.
int g_raiseWindow = []{ const char* s = std::getenv("NEOST_RAISE_WINDOW");
                        return s ? std::atoi(s) : 0; }();
// La fenêtre s'applique-t-elle à ce dispatch ? (hors STOP : réveil niveau-sensible
// immédiat, validé exact à l'oracle — cf. stoppedState).
bool raiseWindowDefers() {
    if (!g_cur->sched || !g_cur->moira) return false;
    const int64_t due  = g_cur->sched->firingDue();
    const bool stopped = g_cur->moira->stoppedState();
    const bool defer   = !stopped && g_raiseWindow > 0 && due >= 0
                      && g_cur->sched->now() - due < g_raiseWindow;
    // DIAG (NEOST_RAISE_DIAG=1) : marge frontière−échéance, état STOP et décision.
    static const bool diag = std::getenv("NEOST_RAISE_DIAG") != nullptr;
    if (diag) std::fprintf(stderr, "[RWD] due=%lld now=%lld stop=%d defer=%d\n",
                           (long long)due, (long long)(g_cur->sched->now()),
                           stopped ? 1 : 0, defer ? 1 : 0);
    return defer;
} }
void Cpu68k::raiseVbl() {
    state_->vblPending = true;
    // COMMIT IMMÉDIAT (2026-07-02, mesuré à l'oracle instrumenté [HPIN]/[HEXC] :
    // chez Hatari CE, les événements vidéo sont traités À LA FRONTIÈRE d'instruction
    // (CycInt_Process) et `intlev_load → ipl_fetch_now` pose regs.ipl[0] SANS délai →
    // l'exception démarre AU MÊME CYCLE que le changement de broche, exc−pin = 0 sur
    // 12709/12711 échantillons du banc poll-entry). L'ancien chemin broche-POLLée
    // (neostUpdateIpl sans commit) faisait reconnaître UNE instruction plus tard.
    // Le callback d'événement EST une frontière d'instruction (modèle bloc) → commit sûr.
    neostUpdateIpl(/*commit=*/(g_raiseCommit & 2) != 0 && !raiseWindowDefers());
}

void Cpu68k::raiseHbl() {
    state_->hblPending = true;
    // même modèle que raiseVbl + fenêtre d'échantillonnage (cf. g_raiseWindow)
    neostUpdateIpl(/*commit=*/(g_raiseCommit & 1) != 0 && !raiseWindowDefers());
}

// Pré-armement des broches IRQ vidéo — cf. .hpp. Appelées par Machine au moment
// où l'événement est PLANIFIÉ (l'instant exact est connu d'avance) ; sync() les
// applique au cycle bus près, en cours d'instruction (modèle bloc conservé).
// NEOST_PIN_ARM : masque A/B — bit0 = HBL, bit1 = VBL. DÉFAUT 0 = DÉSACTIVÉ
// (2026-07-02, 2ᵉ mesure à l'oracle instrumenté) : Hatari CE ne lève PAS la
// broche mi-instruction pour les INT vidéo — CycInt est traité À LA FRONTIÈRE
// d'instruction et `ipl_fetch_now` commit sans délai → exc−broche = 0. Le
// modèle fidèle est donc « raise+COMMIT au dispatch » (cf. raiseHbl/raiseVbl),
// PAS le pré-armement mi-instruction. Pire : pré-armement + callback = DOUBLE
// prise (l'exception part dans le bloc, l'IACK efface le pending, puis le
// callback du dispatch re-lève → 2ᵉ HBL, période libre ~427 cyc mesurée au banc
// poll-entry). Gardé en opt-in pour expériences uniquement.
namespace { int g_pinArmMask = []{ const char* s = std::getenv("NEOST_PIN_ARM");
                                   return s ? std::atoi(s) : 0; }(); }
void Cpu68k::armHblPinAt(int64_t busCycle) { if (g_pinArmMask & 1) { state_->hblPinDue = busCycle; recomputePinNextDue(); } }
void Cpu68k::armVblPinAt(int64_t busCycle) { if (g_pinArmMask & 2) { state_->vblPinDue = busCycle; recomputePinNextDue(); } }

uint32_t Cpu68k::pc() const {
    return state_->moira->getPC0();
}

uint32_t Cpu68k::reg(int idx) const {
    return idx < 8 ? state_->moira->getD(idx) : state_->moira->getA(idx - 8);
}

uint16_t Cpu68k::sr() const {
    return state_->moira->getSR();
}

void Cpu68k::setReg(int idx, uint32_t v) {
    if (idx < 8) state_->moira->setD(idx, v); else state_->moira->setA(idx - 8, v);
}

void Cpu68k::setSr(uint16_t v) {
    state_->moira->setSR(v);
}

uint32_t Cpu68k::usp() const {
    return state_->moira->getUSP();
}

bool Cpu68k::triggerBusError(uint32_t addr, bool write) {
    return state_->moira->faultOrHalt(addr, write);
}

int Cpu68k::disassemble(char* str, uint32_t addr) const {
    return state_->moira->disassemble(str, addr);
}
