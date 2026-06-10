// =============================================================================
//  Cpu68k.hpp — Wrapper C++ autour du core Musashi (Motorola 68000).
//
//  On NE réimplémente PAS le 68000 : Musashi est intégré en sous-module et
//  exposé via cette façade. Le wrapper relie les callbacks mémoire C de Musashi
//  à notre Bus, et expose juste ce qu'il faut au débogueur.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>

class Bus;
class Tracer;

// Cœur d'exécution 68000 sélectionnable AU DÉMARRAGE :
//   - Musashi : cœur MAME (MIT), rapide, coût total par instruction.
//   - Moira   : cœur de vAmiga (MIT, cycle-exact, timing inter-instructions).
// Choisi via --cpu / neost.cfg / l'UI WASM (cf. docs/CYCLE_ACCURACY.md).
enum class CpuCore { Musashi, Moira };

class Cpu68k {
public:
    // core = cœur souhaité (choisi avant le reset, p. ex. via --cpu / config / UI).
    explicit Cpu68k(Bus& bus, CpuCore core = CpuCore::Musashi);

    // "musashi"/"uae" (insensible à la casse) → CpuCore ; défaut Musashi sinon.
    static CpuCore parseCore(const std::string& s);
    static const char* coreName(CpuCore c);

    // Cœur réellement actif (peut différer du demandé si UAE pas dispo → repli).
    CpuCore core() const { return core_; }

    // Bascule le cœur 68000 à CHAUD (Musashi ↔ Moira) sans recréer le Cpu68k :
    // libère l'ancien cœur, ré-initialise le nouveau. L'appelant doit ensuite
    // reset() (lecture SSP/PC). Permet de changer de cœur sans relancer l'appli.
    void setCore(CpuCore core);

    // Branche (ou détache avec nullptr) le traceur : journalise chaque
    // instruction et chaque interruption prise. Utilisé surtout en headless.
    void setTracer(Tracer* t);

    // Reset matériel : Musashi lit SSP ($0) et PC ($4) via le bus, puis on
    // referme l'overlay de boot de la ROM (cf. Bus::bootOverlay).
    void reset();

    // Exécute AU MOINS `cycles` cycles ; renvoie le nombre réellement consommé
    // (le 68000 termine toujours l'instruction en cours). La boucle d'horloge
    // s'en sert pour synchroniser le Shifter.
    int run(int cycles);

    // Wait states de bus (port LIVE de Hatari M68000_SyncCpuBus) : sur le 68000, les
    // registres couleur ($FF8240-5F), résolution ($FF8260) et scroll fin ($FF8264/65)
    // du Shifter ne s'accèdent que sur une frontière de bus de 4 cycles ; un accès qui
    // tombe hors frontière fait PATIENTER le CPU jusqu'à la prochaine (0..3 cycles). Le
    // Shifter appelle ceci à chaque accès concerné ; le cœur AVANCE son horloge d'autant
    // → l'instruction consomme ces cycles et tous les accès suivants sont décalés (la
    // contention de bus du vrai matériel). Remplace EN LIVE l'ancien recalage hors-ligne
    // (applyShifterBusAlignment) : les écritures palette sont désormais datées au cycle
    // ALIGNÉ dès recordColorWrite. Moira (cycle-exact) avance son clock ; Musashi (cœur
    // « rapide », non cycle-exact) ne modélise pas la contention → no-op.
    void addBusWaitCycles(int n);

    // Wait states d'accès aux périphériques 8 bits du bus, portés de Hatari (psg.c,
    // mfp.c, acia.c). Sur le vrai 68000 chaque lecture/écriture d'un de ces composants
    // « lents » coûte des cycles de bus supplémentaires ; le Bus appelle l'un de ces
    // helpers AVANT de router vers la puce (le cœur avance son horloge, comme
    // addBusWaitCycles). Moira (cycle-exact) seul ; Musashi → no-op.
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

    // Coupe le bloc d'exécution en cours : le CPU termine son instruction courante
    // puis rend la main (run() retourne le nombre RÉEL de cycles consommés). Appelé
    // par l'ordonnanceur quand un événement est armé avant la cible du bloc, pour
    // que la boucle d'horloge le serve à temps (latence IRQ ~1 instruction).
    // Musashi : m68k_end_timeslice() ; Moira : drapeau testé après chaque instruction.
    void endTimeslice();

    // Recalcule l'IPL présenté au 68000 à partir de l'état des sources
    // (MFP niveau 6, VBL niveau 4). À appeler après tout changement d'IRQ.
    void updateIpl();

    // Comme updateIpl(), mais l'IPL est COMMITTÉ : sous Moira, la valeur est posée
    // à la fois sur la broche ET dans le registre échantillonné (reg.ipl), comme si
    // le poll IPL de l'instruction précédente l'avait déjà vue → l'exception part
    // AVANT l'instruction suivante. À n'appeler qu'à une FRONTIÈRE d'instruction
    // (callback de l'ordonnanceur), jamais en plein accès MMIO. C'est l'équivalent
    // du chemin Hatari MFP_ProcessIRQ : au test de frontière, si clock-IRQ_Time ≥ 4,
    // l'exception est déclenchée immédiatement (pas un poll d'instruction plus tard).
    // Sans ça, le délai 4 cyc du MFP s'ADDITIONNERAIT au pipeline IPL de Moira
    // (~1 instruction de trop → le test « T4 Video Counter » des diagnostics échoue).
    void updateIplNow();

    // Marque une interruption verticale (VBL, niveau 4 auto-vectorisé) en
    // attente ; elle sera acquittée puis effacée au cycle IACK.
    void raiseVbl();

    // Marque une interruption horizontale (HBL, niveau 2 auto-vectorisé) — une
    // par ligne visible ; gatée par le masque du SR (utilisée par les jeux).
    void raiseHbl();

    // Bus error déclenchée par un périphérique (ex. FDC $FF8604/06 en mode octet).
    // Renvoie true si le CPU est halté (double faute) — l'appelant fournit alors 0.
    bool triggerBusError(uint32_t addr, bool write);

    // État exposé en lecture directe pour le visualiseur de registres ImGui.
    uint32_t pc()  const;          // compteur programme courant
    uint32_t reg(int idx) const;   // 0-7 = D0-D7, 8-15 = A0-A7
    uint16_t sr()  const;          // status register

private:
    void initCore();   // (ré)initialise le cœur actif selon core_ (Musashi/Moira)

    CpuCore core_ = CpuCore::Musashi;   // cœur actif (après repli éventuel)

    // Horloge Moira au début du quantum courant (cf. cyclesRunInQuantum). Pour
    // Musashi on utilise directement m68k_cycles_run().
    int64_t quantumStartClock_ = 0;

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
    // appelés par Scheduler::runTo), le compteur intra-quantum est périmé (Musashi
    // garde les cycles du dernier bloc) → cyclesRunInQuantum() doit alors valoir 0
    // pour que liveNow() == now() (l'horloge a déjà été avancée par l'ordonnanceur).
    bool inRun_ = false;
};
