// =============================================================================
//  Cpu68k.hpp — Wrapper C++ autour du core Moira (Motorola 68000, cycle-exact).
//
//  On NE réimplémente PAS le 68000 : Moira est intégré en sous-module et exposé
//  via cette façade. Le wrapper relie les accès mémoire de Moira à notre Bus et
//  expose juste ce qu'il faut au débogueur.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <functional>
#include <memory>
#include <cstdint>
#include <string>

class Bus;
class Tracer;
class Scheduler;
class StateArchive;

// NeoST n'a plus qu'UN SEUL cœur 68000 : Moira (cœur de vAmiga, MIT, cycle-exact,
// timing inter-instructions). L'ancien cœur Musashi — rapide mais NON cycle-exact —
// a été retiré : il n'apportait plus rien face à Moira. L'énum subsiste avec une
// seule valeur pour garder les signatures (Machine, headless, WASM, neost.cfg) et
// la rétro-compat de configuration (cf. parseCore).
enum class CpuCore { Moira };

class Cpu68k {
public:
    // core = cœur souhaité (toujours Moira ; paramètre conservé pour compat API).
    explicit Cpu68k(Bus& bus, CpuCore core = CpuCore::Moira);
    ~Cpu68k();

    Cpu68k(const Cpu68k&) = delete;
    Cpu68k& operator=(const Cpu68k&) = delete;
    Cpu68k(Cpu68k&&) = delete;
    Cpu68k& operator=(Cpu68k&&) = delete;

    // Tolère l'ancienne clé "musashi"/"uae" (insensible à la casse) : on AVERTIT et
    // on bascule sur Moira, seul cœur disponible. Toute autre valeur → Moira aussi.
    static CpuCore parseCore(const std::string& s);
    static const char* coreName(CpuCore c);

    // Cœur réellement actif (toujours Moira).
    CpuCore core() const { return core_; }

    // Conservé pour compat (reconfigure à chaud) : ré-initialise simplement le cœur.
    // L'appelant doit ensuite reset() (lecture SSP/PC).
    void setCore(CpuCore core);

    // Branche (ou détache avec nullptr) le traceur : journalise chaque
    // instruction et chaque interruption prise. Utilisé surtout en headless.
    void setTracer(Tracer* t);

    // Branche l'ordonnanceur que le cœur pilote DEPUIS son hook cycle (sync) : à
    // chaque pas, sync() avance l'horloge puis dispatche les événements échus, de
    // sorte que l'IPL soit posé au cycle exact (modèle `do_cycles` WinUAE/Hatari),
    // EN COURS d'instruction, et vu par le POLL_IPL de Moira (cf. Cpu68k.cpp).
    void setScheduler(Scheduler* s);

    // Horloge BUS (8 MHz) ABSOLUE live du cœur, valide à tout instant (même au
    // milieu d'une instruction en PRECISE_TIMING). C'est l'horloge maîtresse :
    // l'ordonnanceur, le faisceau vidéo et le RTC en dérivent. Remplace l'ancien
    // sched.now() + cyclesRunInQuantum() (le delta intra-quantum est désormais
    // intégré au cœur, dispatché par sync()).
    int64_t busClockNow() const;

    // Reset matériel : Moira lit SSP ($0) et PC ($4) via le bus (overlay ROM de
    // boot, cf. Bus::bootOverlay), puis on referme l'overlay.
    void reset();

    // Exécute AU MOINS `cycles` cycles BUS (horloge 8 MHz de l'ordonnanceur) ;
    // renvoie le nombre réellement consommé (le 68000 termine toujours
    // l'instruction en cours). La boucle d'horloge s'en sert pour synchroniser
    // le Shifter. En mode Mega STE 16 MHz, 1 cycle bus = 2 cycles CPU : la
    // conversion est interne (cf. setMegaSteSpeed), l'ordonnanceur et toutes les
    // puces restent cadencés à 8 MHz comme sur le vrai matériel.
    int run(int cycles);

    // Bascule 8/16 MHz du Mega STE ($FF8E21 bit1) — port de Hatari
    // MegaSTE_CPU_Cache_Update / MegaSTE_CPU_Set_16Mhz. Appelé par le Bus à
    // l'écriture du registre, et au reset (retour 8 MHz). L'horloge du cœur passe
    // en cycles CPU 16 MHz ; les accès RAM ST restent cadencés par le bus 8 MHz
    // (créneau de 8 cycles CPU + accès 8 cycles, cf. wait_cpu_cycle_read_megaste_16)
    // sauf hit du cache 16 Ko (4 cycles). ROM/cartouche/IO : « FAST », pas de wait
    // state (mesuré sur vrai STF par Hatari) → 2× plus rapides.
    void setMegaSteSpeed(bool sixteenMhz);
    bool megaSte16Mhz() const;

    // Bit S du SR : vrai si le CPU est en mode superviseur. Consulté par le Bus
    // pour la protection mémoire du GLUE ($0-$7FF et IO réservés superviseur).
    bool supervisor() const;

    // Le 68000 a-t-il assuré /HALT (double faute de bus/adresse) ? Il ne redémarre
    // que sur un reset machine : plus une seule instruction n'est exécutée, l'écran
    // reste figé. À consulter par un harnais (headless) pour distinguer « figé » de
    // « tourne encore » — Hatari, lui, quitte avec un code non nul sous --run-vbls
    // (gui-sdl/dlgHalt.c:66-71).
    bool halted() const;

    // A42 (2026-08-30) — CE QUI A GELÉ LA MACHINE. Un halt sans son adresse ne se
    // diagnostique pas : c'est ce qui a fait passer « No Cooper haltait en Mega ST »
    // (double faute de bus, FIDÈLE — cf. docs/CASE_STUDIES.md) pour un « ça plante »
    // pendant des semaines. `valid` est faux tant qu'aucune faute de groupe 0 n'a été
    // prise ; `addr`/`pc` portent alors la DERNIÈRE (celle qui a doublé la faute).
    // Équivalent des `last_*_for_exception_3` d'Hatari (newcpu.c:3086).
    struct Fault {
        bool     valid = false;
        uint32_t addr  = 0;      // adresse fautive
        uint32_t pc    = 0;      // PC de l'instruction fautive
        bool     write = false;  // true = écriture, false = lecture
        int      vector = 0;     // 2 = bus error, 3 = address error (0 = inconnu)
    };
    Fault lastFault() const;

    // Wait states de bus (port LIVE de Hatari M68000_SyncCpuBus) : sur le 68000, les
    // registres couleur ($FF8240-5F), résolution ($FF8260) et scroll fin ($FF8264/65)
    // du Shifter ne s'accèdent que sur une frontière de bus de 4 cycles ; un accès qui
    // tombe hors frontière fait PATIENTER le CPU jusqu'à la prochaine (0..3 cycles). Le
    // Shifter appelle ceci à chaque accès concerné ; le cœur AVANCE son horloge d'autant
    // → l'instruction consomme ces cycles et tous les accès suivants sont décalés (la
    // contention de bus du vrai matériel). Remplace EN LIVE l'ancien recalage hors-ligne
    // (applyShifterBusAlignment) : les écritures palette sont désormais datées au cycle
    // ALIGNÉ dès recordColorWrite.
    void addBusWaitCycles(int n);

    // Wait states d'accès aux périphériques 8 bits du bus, portés de Hatari (psg.c,
    // mfp.c, acia.c). Sur le vrai 68000 chaque lecture/écriture d'un de ces composants
    // « lents » coûte des cycles de bus supplémentaires ; le Bus appelle l'un de ces
    // helpers AVANT de router vers la puce (le cœur avance son horloge, comme
    // addBusWaitCycles).
    //
    //  - PSG YM2149  : 4 cyc au PREMIER accès de l'instruction (port PSG_WaitState ;
    //    les accès suivants de la même instruction n'ajoutent rien — le cas movem
    //    +4/4e accès, inexistant dans le logiciel réel, est volontairement omis).
    //  - MFP 68901   : 4 cyc à CHAQUE accès registre (port M68000_WaitState(4)).
    //  - ACIA 6850   : 6 cyc à chaque accès + synchro E-Clock (0..8 cyc, port
    //    ACIA_AddWaitCycles) au PREMIER accès de l'instruction seulement.
    void addPsgWaitCycles();
    void addMfpWaitCycles();
    void addAciaWaitCycles();

    // Cycles consommés depuis le DÉBUT du quantum courant (l'appel run() en cours).
    // L'ordonnanceur ne met `sched.now()` à jour qu'aux frontières de quantum ; une
    // lecture MMIO en plein milieu (p.ex. le RTC) verrait donc un cycle périmé. Ce
    // delta permet de reconstituer le cycle ABSOLU exact = sched.now() + ce delta.
    int64_t cyclesRunInQuantum() const;
    // Horodatage de trace dans le domaine de l'ordonnanceur. Le compteur brut
    // Moira inclut les 40 cycles de reset non facturés au Scheduler ; ce biais
    // disparaît au premier rebase IACK et faussait les mesures à cheval dessus.
    int64_t traceClockNow() const;
    // Vrai pendant un appel run(), faux à une frontière d'événement (donc dans un
    // callback de Scheduler::runTo). DISCRIMINANT DE FACTURATION : un stall facturé
    // DANS le quantum est capté par `ran` puis reversé à l'ordonnanceur par le
    // runTo(now + ran) de Machine::runFrame ; un stall facturé HORS quantum ne l'est
    // par personne et doit être crédité explicitement à l'ordonnanceur (cf.
    // Blitter::billCycles → Scheduler::addStolenCycles). Créditer les DEUX
    // double-compterait le mode HOG du blitter.
    bool inRun() const { return inRun_; }
    // Rebase du quantum + dispatch des événements échus, au point d'IACK réel
    // (≙ CycInt_Process d'Hatari avant l'élection du vecteur). Cf. .cpp.
    void rebaseQuantumAndSync();

    // Cycles BUS écoulés depuis le DÉBUT de l'instruction COURANTE, à l'instant exact
    // de l'appel (en plein accès mémoire en PRECISE_TIMING). = busClockNow() -
    // busOf(instrStartClock_). Pour un accès mémoire, Moira a déjà facturé le SYNC(2)
    // de tête de l'accès → cette valeur ≈ (cycles avant l'accès) + 2. Utilisé pour
    // dater une écriture MMIO comme Hatari mode CE (Cycles_GetInternalCycleOnWriteAccess
    // = currcycle + 4) : le cycle de FIN d'accès = busClockNow() + 2.
    int64_t cyclesIntoInstr() const;

    // Coupe le bloc d'exécution en cours : le CPU termine son instruction courante
    // puis rend la main (run() retourne le nombre RÉEL de cycles consommés). Appelé
    // par l'ordonnanceur quand un événement est armé avant la cible du bloc, pour
    // que la boucle d'horloge le serve à temps (latence IRQ ~1 instruction). Sous
    // Moira : drapeau testé après chaque instruction.
    void endTimeslice();

    // Recalcule l'IPL présenté au 68000 à partir de l'état des sources
    // (MFP niveau 6, VBL niveau 4). À appeler après tout changement d'IRQ.
    void updateIpl();

    // Comme updateIpl(), mais l'IPL est COMMITTÉ : la valeur est posée à la fois sur
    // la broche ET dans le registre échantillonné (reg.ipl), comme si le poll IPL de
    // l'instruction précédente l'avait déjà vue → l'exception part AVANT l'instruction
    // suivante. À n'appeler qu'à une FRONTIÈRE d'instruction (callback de
    // l'ordonnanceur), jamais en plein accès MMIO. C'est l'équivalent du chemin Hatari
    // MFP_ProcessIRQ : au test de frontière, si clock-IRQ_Time ≥ 4, l'exception est
    // déclenchée immédiatement (pas un poll d'instruction plus tard). Sans ça, le délai
    // 4 cyc du MFP s'ADDITIONNERAIT au pipeline IPL de Moira (~1 instruction de trop →
    // le test « T4 Video Counter » des diagnostics échoue).
    void updateIplNow();

    // Marque une interruption verticale (VBL, niveau 4 auto-vectorisé) en
    // attente ; elle sera acquittée puis effacée au cycle IACK.
    void raiseVbl();

    // Marque une interruption horizontale (HBL, niveau 2 auto-vectorisé) — une
    // par ligne visible ; gatée par le masque du SR (utilisée par les jeux).
    void raiseHbl();

    // PRÉ-ARMEMENT de la broche IPL pour les IRQ vidéo PLANIFIÉES (HBL niv 2,
    // VBL niv 4) : la Machine connaît d'avance le cycle bus EXACT de l'événement
    // (grille frameStart + k·cpl) — la broche est alors levée PAR LE HOOK sync()
    // du cœur, EN COURS d'instruction, au cycle près (comme Hatari où CycInt tire
    // dans do_cycles). Sans ça (modèle bloc), la broche ne montait qu'au dispatch
    // de l'événement = frontière de bloc, 0..24 cyc APRÈS l'instant vrai (le
    // dépassement de la dernière instruction) → l'instruction enjambant l'événement
    // ne la voyait pas à son POLL_IPL → reconnaissance ~1 instruction trop tard,
    // avec un jitter = dépassement de bloc (cause mesurée au banc poll-entry :
    // entrées de handler NeoST ≈ Hatari +8..12). Le callback d'événement reste au
    // bloc (dispatch BLOC conservé) et re-pose la broche (idempotent, filet).
    void armHblPinAt(int64_t busCycle);
    void armVblPinAt(int64_t busCycle);

    // Bus error déclenchée par un périphérique (ex. FDC $FF8604/06 en mode octet).
    // Renvoie true si l'accès doit être abandonné — CPU halté (double faute), ou accès
    // HORS exécution d'instruction (hôte : aucune faute levée ni enregistrée) —
    // l'appelant fournit alors une valeur neutre.
    bool triggerBusError(uint32_t addr, bool write);

    // État exposé en lecture directe pour le visualiseur de registres ImGui.
    uint32_t pc()  const;          // compteur programme courant
    uint32_t reg(int idx) const;   // 0-7 = D0-D7, 8-15 = A0-A7
    uint16_t sr()  const;          // status register

    // --- Accès écriture aux registres pour l'émulation GEMDOS HD --------------------
    // L'interception d'un appel GEMDOS (trap #1 redirigé via la cartouche système)
    // lit/modifie les registres et le SR du 68000 directement, comme le fait Hatari
    // dans gemdos.c (Regs[REG_Dx], M68000_SetSR…). Index : 0-7 = D0-D7, 8-15 = A0-A7.
    void     setReg(int idx, uint32_t v);
    void     setSr(uint16_t v);
    uint32_t usp() const;          // pointeur de pile UTILISATEUR (regs.usp)

    // Observateur des TRAP #n, appelé À L'ENTRÉE de l'exception avec le vecteur
    // (32 + n) : SR et pile de l'appelant encore en place, A7 pointe donc sur ses
    // arguments. Lecture seule (relevé de console de --console-log) ; non sérialisé.
    void setTrapObserver(std::function<void(uint16_t vector)> observer);

    // Désassemble l'instruction à `addr` (lecture sans effet de bord via le bus) :
    // écrit le texte dans `str` (≥256 octets recommandés) et renvoie sa longueur en
    // octets. Utilisé par le Tracer et le mode --disasm du headless. Le désassembleur
    // de Moira reproduit la syntaxe Musashi (cf. setDasmSyntax) → format inchangé.
    int disassemble(char* str, uint32_t addr) const;

    // Save-state : état interne du cœur Moira (registres, prefetch, horloge, IPL, flags)
    // + les compteurs de timing du wrapper. Symétrique save/load (cf. StateArchive).
    void serialize(StateArchive& ar);

    // --- Débogueur : breakpoints PC (réutilise le conteneur Guards de Moira) --------
    // Sémantique « break-before » : l'exécution s'arrête AVANT d'exécuter l'instruction
    // à l'adresse pointée (le run() en cours rend la main, PC positionné dessus). À la
    // REPRISE, l'adresse courante est ignorée UNE fois (clearBreakpointHit) pour ne pas
    // re-déclencher sur place. Adresses masquées sur 24 bits (bus ST).
    void setBreakpoint(uint32_t addr);      // ajoute (idempotent)
    void clearBreakpoint(uint32_t addr);    // retire s'il existe
    void clearAllBreakpoints();
    bool hasBreakpoint(uint32_t addr) const;
    int  breakpointCount() const;
    // Récupère l'adresse du nᵉ breakpoint (0..count-1) → true si valide.
    bool breakpointByIndex(int nr, uint32_t& outAddr) const;

    // --- Débogueur : watchpoints mémoire (arrêt à l'accès lecture OU écriture) -------
    // Sémantique « break-after-access » : l'exécution s'arrête APRÈS l'instruction qui a
    // accédé à l'adresse. Testés par la couche dataflow de Moira (pas de coût côté Bus).
    void setWatchpoint(uint32_t addr);
    void clearWatchpoint(uint32_t addr);
    void clearAllWatchpoints();
    bool hasWatchpoint(uint32_t addr) const;
    int  watchpointCount() const;
    bool watchpointByIndex(int nr, uint32_t& outAddr) const;

    // Un breakpoint OU watchpoint a-t-il stoppé le dernier run() ? (à consulter par la
    // boucle de trame et le frontend pour passer en pause.) breakpointHitAddr() = adresse
    // atteinte (PC du breakpoint, ou adresse DONNÉE du watchpoint) ; breakpointHitIsWatch()
    // distingue les deux.
    bool     breakpointHit() const;
    uint32_t breakpointHitAddr() const;
    bool     breakpointHitIsWatch() const;
    // Efface l'état « hit » ET arme le skip-once (reprise propre, breakpoints PC only).
    void     clearBreakpointHit();

private:
    void initCore();   // (ré)initialise le cœur Moira

    // A33 — ÉTAT PAR INSTANCE. Défini dans le .cpp (il référence des types internes
    // au cœur) ; possédé ici pour qu'une deuxième machine soit un deuxième objet et
    // non 25 variables globales à démêler. `activate()` désigne l'instance dont les
    // callbacks Moira — qui n'ont pas de `this` — doivent voir l'état.
    std::unique_ptr<struct CpuState> state_;
    void activate() const;

    CpuCore core_ = CpuCore::Moira;   // cœur actif (toujours Moira)

    // Horloge Moira au début du quantum courant (cf. cyclesRunInQuantum).
    int64_t quantumStartClock_ = 0;
    // Équivalent BUS (8 MHz) de quantumStartClock_, figé au début du quantum : la
    // bascule 8/16 MHz peut survenir EN PLEIN quantum (écriture $FF8E21), le point
    // de départ doit donc être mémorisé sous l'ancienne conversion (cf. run()).
    int64_t quantumStartBus_ = 0;

    // Détection « premier accès de l'instruction courante » pour les wait states
    // PSG/ACIA (cf. add*WaitCycles). `instrStartClock_` est l'horloge Moira figée
    // AVANT chaque execute() : constante durant l'instruction, distincte d'une
    // instruction à l'autre (toute instr. consomme ≥4 cyc). Un helper compare cette
    // valeur à la dernière mémorisée pour savoir s'il s'agit du 1er accès de l'instr.
    // (équivaut au test `PrevClock != CyclesGlobalClockCounter` de Hatari).
    int64_t instrStartClock_   = -1;   // horloge au début de l'instruction en cours
    int64_t psgPrevInstrClock_ = -1;   // instr. du dernier accès PSG (wait 4 cyc)
    int64_t aciaPrevInstrClock_ = -1;  // instr. du dernier accès ACIA (synchro E-Clock)

    // Vrai UNIQUEMENT pendant un appel run() : hors run (ex. handlers d'événements
    // appelés par Scheduler::runTo), le compteur intra-quantum est périmé →
    // cyclesRunInQuantum() doit alors valoir 0 pour que liveNow() == now() (l'horloge
    // a déjà été avancée par l'ordonnanceur).
    bool inRun_ = false;
};
