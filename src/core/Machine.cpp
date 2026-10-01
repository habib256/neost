// =============================================================================
//  Machine.cpp — Câblage des composants + boucle d'horloge d'une trame.
//
//  Depuis la Phase 1 de cycle-accuracy (cf. docs/CYCLE_ACCURACY.md), la trame est
//  pilotée par un ordonnanceur d'événements datés (`Scheduler`) au lieu d'une
//  boucle « 313 lignes × 512 cycles » avec des `if` en ligne. À ce stade le
//  timing produit reste STRICTEMENT IDENTIQUE (quantum CPU = la ligne) : c'est un
//  refactor de structure, validé par diff de trace. Les phases suivantes
//  resserreront le quantum et ajouteront des sources (Timers A/B/D, FDC, DMA…).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "core/Machine.hpp"
#include "core/StateArchive.hpp"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

// Modèle de dispatch BLOC = DÉFAUT (le sync-driven mid-instruction est RÉFUTÉ : il
// deadlockait Enchanted Land sans corriger le jitter). CPU borné à l'événement suivant
// + dispatch à la frontière via runTo, en GARDANT PT=true + RAM_SLOT (la convergence
// cycle d'instruction est indépendante du dispatch). VALIDÉ : EL remarche (jeu rendu),
// étalons 19/0, différentiel 14/14. Le sync-driven reste accessible en OPT-IN pour A/B.

// Port de Hatari TOS_CheckSysConfig (sous-ensemble utile à NeoST) : abaisse la
// machine si le TOS chargé ne la supporte pas. Seul le cas « TOS <= 1.04 → ST »
// est porté (les autres règles d'Hatari visent TT/Falcon, hors champ NeoST).
MachineType Machine::adjustMachineForTos(MachineType requested, const std::string& romPath) {
    std::ifstream f(romPath, std::ios::binary);
    if (!f) return requested;                 // introuvable → loadTos signalera l'erreur
    uint8_t b[2] = {0, 0};
    f.seekg(2);                               // version TOS : mot big-endian à l'offset 2
    f.read(reinterpret_cast<char*>(b), 2);
    if (f.gcount() != 2) return requested;    // fichier tronqué/illisible → loadTos signalera
    const uint16_t tosVer = uint16_t((b[0] << 8) | b[1]);
    // TOS <= 1.04 (TOS 1.0x ; EmuTOS 192 Ko se présente en « Atari ST » 1.4) ne gère ni
    // le STE ni le Mega STE → Hatari bascule en mode ST. machineIsSte() = STE || Mega STE
    // (le Mega ST tourne nativement sous TOS 1.0x, donc PAS de bascule).
    if (tosVer <= 0x0104 && machineIsSte(requested)) {
        std::fprintf(stderr,
            "[NeoST] TOS %X.%02X only runs in ST mode (68000) — switching %s -> ST.\n"
            "        For STE/Mega STE, use EmuTOS 256 KB (etos256*) or TOS 1.62/2.06.\n",
            tosVer >> 8, tosVer & 0xFF, machineName(requested));
        return MachineType::St;
    }
    // Symétrique (port de tos.c:844-855) : TOS 1.06 et 1.62 sont des ROM **STE
    // exclusivement**. Sur un ST/Mega ST, la routine de nettoyage RAM du TOS
    // ($E001AA-$E001B2) monte jusqu'à $00400000, prend une bus error, double-faute et
    // HALTE le CPU : NeoST restait sur un écran uniforme, définitivement, SANS le
    // moindre message — un utilisateur qui choisit « ST » avec un TOS 1.62 n'avait
    // aucun moyen de comprendre. Hatari bascule en STE et boote ; on fait pareil.
    if ((tosVer == 0x0106 || tosVer == 0x0162) && requested != MachineType::Ste) {
        std::fprintf(stderr,
            "[NeoST] TOS %X.%02X is an STE-only ROM — switching %s -> STE.\n"
            "        (on ST its RAM clear overruns and halts the CPU: frozen screen)\n",
            tosVer >> 8, tosVer & 0xFF, machineName(requested));
        return MachineType::Ste;
    }
    // Le Mega STE exige un TOS ≥ 2.0x : lui seul programme le cache 16 Ko, le SCU et
    // les 16 MHz. Un TOS 1.0x/1.6x (STE) ne connaît pas ce matériel et reste bloqué au
    // boot (combo inexistant sur vrai matériel — chaque machine a son ROM). On bascule
    // donc en STE, où ce TOS tourne normalement (cf. bug cube3d : Mega STE + TOS 1.62
    // = écran vide, alors que STE + 1.62 fonctionne).
    if (tosVer < 0x0200 && requested == MachineType::MegaSte) {
        std::fprintf(stderr,
            "[NeoST] TOS %X.%02X does not support the Mega STE (cache/SCU/16 MHz) — switching Mega STE -> STE.\n"
            "        For a real Mega STE, use TOS 2.06 (tos206*) or EmuTOS 256 KB (etos256*).\n",
            tosVer >> 8, tosVer & 0xFF);
        return MachineType::Ste;
    }
    return requested;
}

namespace { // Décalage de la position d'IRQ HBL dans la ligne, relatif à cpl.
// Hatari Hbl_Int_Pos = cpl−4 en WS1, cpl en WS2/3/4 ET sur STE (video.c:977-979,
// 1055-1057) — tranché WS3 (2026-07-08, cf. glue:: dans Shifter.cpp) → défaut 0
// (HBL à 508/512 selon la fréquence, à la FRONTIÈRE de ligne comme l'oracle).
// Le STE n'a PAS de wakestate : toujours 0, même en A/B NEOST_WS=1.
// NEOST_HBL_OFF garde la main pour l'A/B (ex. −4 = ancien hybride WS1).
// Historique : l'ancien défaut −4 citait la table WS1 ; l'A/B à 0 avait été
// écarté à l'époque car il compensait une broche IPL levée en retard au dispatch
// de bloc — corrigé depuis par le pré-armement (Cpu68k::armHblPinAt).
int kHblOff(bool isSte) {
    static const bool envSet = std::getenv("NEOST_HBL_OFF") != nullptr;
    static const int  envVal = envSet ? std::atoi(std::getenv("NEOST_HBL_OFF")) : 0;
    if (envSet) return envVal;                             // A/B explicite : prime partout
    return (!isSte && Shifter::wakestate() == 1) ? -4 : 0;
} }

Machine::Machine(std::size_t ramBytes, CpuCore cpuCore, MachineType machine)
    : bus(ramBytes), cpu(bus, cpuCore) {
    machineType_ = machine;
    bus.machine  = machine;         // profil matériel (gating MMIO / bus errors)
    glue.memConfig_ = memConfigForBytes(ramBytes);   // $FF8001 cohérent (EmuTOS recalcule)
    // Branchement des puces sur le bus (le bus ne possède pas les composants).
    bus.shifter = &shifter;
    bus.psg     = &psg;
    bus.glue    = &glue;
    bus.mfp     = &mfp;
    bus.ikbd    = &ikbd;
    bus.fdc     = &fdc;
    bus.dmasnd  = &dmasnd;
    bus.blitter = &blitter;
    bus.rtc     = &rtc;     // horloge RP5C15 (Mega ST / Mega STE)
    bus.midi    = &midi;    // ACIA MIDI ($FFFC04) — bouclage OUT→IN
    bus.cpu     = &cpu;     // pour rafraîchir l'IPL après chaque accès MMIO
    bus.scc     = (machine == MachineType::MegaSte) ? &scc : nullptr;  // SCC série (Mega STE)
    // Joypads STE ($FF9200/02) : seul le Mega STE expose les DIP switches motherboard
    // dans l'octet haut de $FF9200 (cf. StePads / IoMemTabMegaSTE_DIPSwitches_Read).
    bus.stePads.setMegaSte(machine == MachineType::MegaSte);
    // Horloge faisceau pour le compteur d'adresse vidéo $FF8205/07/09 : cycles
    // écoulés depuis le début de la trame courante (cf. Shifter::videoCounter).
    // Horloge LIVE (delta intra-quantum inclus) — INDISPENSABLE : `sched.now()` est
    // figé pendant un bloc CPU et ne bouge qu'aux frontières d'événement, donc un code
    // qui POLLE $FF8209 en boucle serrée (sync raster des démos spec512 : `tst.b (a5);
    // beq`) verrait un compteur GELÉ qui ne saute qu'aux events → la valeur lue ne
    // correspond pas au cycle réel et le stabilisateur (saut calculé dans un nop-slide)
    // se cale de travers → clignotement ±16 cyc. Avec liveNow le compteur suit le cycle
    // exact de l'accès (comme Hatari Video_GetCyclesSinceVbl_OnReadAccess).
    shifter.setBeamClock([this] { return sched.liveNow() - frameStart_; });
    // Horloge LIVE dans la trame (delta intra-quantum CPU inclus) : date au cycle
    // près chaque écriture palette pour le re-rendu spec512 (cf. Shifter::finishFrame).
    shifter.setLiveFrameClock([this] { return sched.liveNow() - frameStart_; });
    // DEBUG (NEOST_WATCH=hex) : trace datée [WATCH] des écritures RAM dans
    // [base, base+0x300) — pendant NeoST du tracking Hatari
    // « b ($addr).w ! ($addr).w :trace » (chantier Closure, remplisseur de listes).
    if (const char* w = std::getenv("NEOST_WATCH")) {
        bus.watchBase_ = static_cast<uint32_t>(std::strtoul(w, nullptr, 16));
        bus.watchHook = [this](uint32_t addr, uint8_t v) {
            const int64_t fc = sched.liveNow() - frameStart_;
            std::fprintf(stderr, "[WATCH] fc=%lld line=%lld cyc=%lld addr=%06x val=%02x pc=%06x\n",
                static_cast<long long>(fc), static_cast<long long>(fc / cpl_),
                static_cast<long long>(fc % cpl_), addr, v, cpu.pc());
        };
    }
    // Horloge RTC : cycle CPU ABSOLU exact, même au milieu d'une lecture MMIO.
    // L'horloge maîtresse est désormais le cœur (busClockNow), conduit par sync().
    rtc.setClock([this] { return sched.now() + cpu.cyclesRunInQuantum(); });
    // L'horloge de l'UltraSatan (pile propre) suit la même horloge émulée.
    usatan.setClock([this] { return sched.now() + cpu.cyclesRunInQuantum(); });
    // Horloge « live » de l'ordonnanceur = cycle bus absolu live du cœur. Les puces
    // qui datent un événement en plein milieu d'une instruction (MFP timers, compteur
    // vidéo…) s'en servent pour le caler sur l'instant RÉEL de l'accès. C'est l'horloge
    // que sync() avance et sur laquelle il dispatche (cf. NeostMoira::sync).
    sched.setLiveClock([this] { return sched.now() + cpu.cyclesRunInQuantum(); });
    // L'ordonnanceur est piloté DEPUIS le hook cycle du cœur (sync) : à chaque pas,
    // sync() avance l'horloge puis dispatche les échus → IPL posé au cycle exact.
    cpu.setScheduler(&sched);
    // Préemption du bloc CPU : runFrame/stepInstruction arment beginRun avant chaque
    // cpu.run — un événement planifié avant la cible coupe le bloc. Toujours active
    // depuis A34 (le second modèle d'exécution, où beginRun n'était jamais appelé, a
    // été supprimé).
    sched.setEndSlice([this] { cpu.endTimeslice(); });
    // V2 res-switch (opt-in NEOST_V2) : la Glue signale une impulsion hi-res PRÉCOCE
    // (cyc ≤ 56) sur la ligne courante → on raccourcit la ligne (HBL reprogrammé à la
    // position hi-res 220 au lieu de cpl-4) et on décale les lignes SUIVANTES de
    // (cpl-224) via lineCarry_. Port de Hatari HBL_Pos/nCyclesPerLine (video.c:2249,
    // Video_AddInterruptHBL 2849) : laisse dériver la phase du gestionnaire fullscreen.
    v2_ = std::getenv("NEOST_V2") != nullptr;
    // Longueurs de ligne PAR-LIGNE (port HBL_Pos/nCyclesPerLine de
    // Video_Update_Glue_State) — **ON par défaut depuis le tranchage WS3
    // (2026-07-08)** : validé étalons TOUS OK + Cuddly 190/250 vs oracle + A/B
    // interne EL/spec512 à 0 px (LX indisponible ici, à re-vérifier au premier
    // disque). NEOST_LINELEN=0 désactive (A/B). À chaque écriture freq/res dont
    // la branche « Freq_match » fixe la géométrie de la ligne HBL COURANTE :
    // l'IRQ HBL de la ligne est REPROGRAMMÉE à lineStart+hblPos (≙
    // Video_AddInterruptHBL) et la longueur (224/508/512) est retenue ; onHbl
    // cumulera le raccourcissement dans lineCarry_ (les événements des lignes
    // suivantes — HBL/Timer B/RENDER — suivent déjà −lineCarry_).
    // ⚠ A16 : lire le verrou par Shifter::lineLenEnv() et RIEN d'autre — cette
    // ligne lisait getenv elle-même, avec un défaut qui a divergé de celui des
    // sites Shifter pendant sept semaines (hybride non voulu, cf. Shifter.cpp).
    lineLenOn_ = Shifter::lineLenEnv();
    shifter.setLineGeom([this](int line, int hblPos, int cyclesLine) {
        if (!lineLenOn_ || line != hblLine_ || line >= lpf_) return;
        const int64_t lineStart = frameStart_ + static_cast<int64_t>(line) * cpl_ - lineCarry_;
        // Ne reprogramme que si l'IRQ HBL de la ligne n'a pas encore été servie
        // (l'événement est encore armé à une position ≥ maintenant).
        sched.schedule(Scheduler::HBL, lineStart + hblPos);
        cpu.armHblPinAt(lineStart + hblPos);
        curLineLen_ = cyclesLine;
        if (std::getenv("NEOST_LINELEN_TRACE")) std::fprintf(stderr,
            "[LLEN] line=%d hblPos=%d len=%d carry=%lld\n", line, hblPos, cyclesLine,
            (long long)lineCarry_);
    });
    shifter.setHblShorten([this] {
        if (!v2_ || hblLine_ == v2ShortLine_ || hblLine_ >= lpf_) return;   // déjà raccourcie / hors trame
        // Cycle DANS la ligne courante (grille décalée par lineCarry_). Seule une
        // impulsion hi-res PRÉCOCE (≤ HDE_On_Low_50=56) raccourcit la ligne — comme
        // les branches Hatari video.c:2246/2268/2288 (pas la branche right-off tardive).
        const int64_t lineStart = static_cast<int64_t>(hblLine_) * cpl_ - lineCarry_;
        const int64_t lineCyc   = (sched.liveNow() - frameStart_) - lineStart;
        if (lineCyc < 0 || lineCyc > 56) return;
        v2ShortLine_ = hblLine_;
        const int kHblPosHi = 224 + kHblOff(machineIsSte(machineType_));            // Hbl_Int_Pos_Hi = cpl_71 + offset HBL (224 WS3, 220 WS1)
        constexpr int kLineLenHi = 224;                   // CYCLES_PER_LINE_71HZ
        sched.schedule(Scheduler::HBL, frameStart_ + lineStart + kHblPosHi);
        lineCarry_ += cpl_ - kLineLenHi;                  // lignes suivantes décalées plus tôt
        if (std::getenv("NEOST_V2_TRACE")) { static long n=0; if (++n % 500 == 0)
            std::fprintf(stderr, "[v2] shorten #%ld line=%d cyc=%lld carry=%lld\n",
                         n, hblLine_, (long long)lineCyc, (long long)lineCarry_); }
    });
    // Connecteur de bouclage RS232 : les sorties RTS (port A bit3) et DTR (bit4) du
    // PSG recopient les entrées de contrôle du MFP — RTS→CTS (GPIP2), DTR→DCD (GPIP1)
    // ET DTR→RI (GPIP6) — comme le câble de test du diagnostic « S RS232 ». Le port A
    // est actif BAS (bit=0 → ligne assertée). On rafraîchit l'IPL (un canal a pu lever).
    psg.setPortASink([this](uint8_t a) {
        // SÉLECTION LECTEUR/FACE — port de psg.c:419-420, qui appelle FDC_SetDriveSide à
        // CHAQUE écriture du port A. C'est une POUSSÉE, et elle est indispensable : le FDC
        // de NeoST se contentait de RELIRE le PSG depuis ses propres accès registre
        // (Fdc::refreshDriveSide n'était appelé que par executeCommand et les accès MMIO),
        // si bien qu'un programme qui écrit sa commande FDC AVANT de sélectionner le lecteur
        // restait bloqué avec driveSel_ = -1 pour toujours : la commande partait sans
        // lecteur, indexTime_ restait à 0, et INTRQ/GPIP5 n'était jamais levé. Symptôme :
        // Stardust sur STE gelait en noir après son menu (le CPU tournait dans
        // « tst.b D5 / bne $261e » en attente d'une IRQ FDC qui ne venait plus), là où
        // Hatari atteint « INSERT DISK 2 IN ANY DRIVE ». Cf. TODO.md § D-PSG.
        // refreshDriveSide() est idempotent (il ne ré-ancre l'index QUE si le lecteur change,
        // Fdc.cpp) — l'appeler à chaque écriture du port A est donc sûr, et c'est
        // exactement ce que fait Hatari.
        fdc.refreshDriveSide();
        if (ports.hasSerial()) { ports.onPortA(a, sched.now(), mfp); cpu.updateIpl(); }
        if (!mfp.loopback()) return;        // connecteur non branché → lignes inertes
        // ⚠ Le port A du PSG est ACTIF BAS : bit=0 → ligne ASSERTÉE. Convention établie
        // sur CE registre chez Hatari — psg.c:223 le met à $FF au reset (« no drive
        // selected »), et la sélection lecteur s'y teste par « (PORTA & (1<<1)) == 0 »
        // (psg.c:400). Le code testait « != 0 » : au repos ($FF) le bouclage assertait
        // donc CTS/DCD/RI, et toute bascule les faisait bouger À L'ENVERS. Les deux
        // cartouches Field Service (test S) concluaient « No loopback connector ».
        const bool rts = (a & 0x08) == 0;   // bit3 = 0 → RTS assertée
        const bool dtr = (a & 0x10) == 0;   // bit4 = 0 → DTR assertée
        mfp.setRs232Cts(rts);
        mfp.setRs232Dcd(dtr);
        mfp.setRs232Ri(dtr);
        cpu.updateIpl();
    });
    mfp.setScheduler(&sched);   // le MFP date lui-même ses timers (A/C/D, mode délai)

    // A42 — L'ÉLECTION DE L'IRQ MFP EST SUSPENDUE PENDANT UN LOT DE DISPATCH, puis
    // tranchée UNE FOIS à la fin, avant que l'IPL ne soit relu. C'est l'ordre d'Hatari :
    // CycInt_Process sert tous les événements échus, PUIS la boucle CPU fait
    // `if (MFP_UpdateNeeded) MFP_UpdateIRQ_All(0)`, PUIS l'IPL est évalué
    // (newcpu.c:3005, 5509). Sans ça, chaque callback élisait pour son compte — les 18
    // `cpu.updateIpl()` de ce fichier — et la règle « seules les plus anciennes
    // concourent » (`pendingTime_ <= pendingTimeMin_`) ne voyait jamais plus d'un
    // candidat : deux timers échus dans le MÊME lot étaient élus séparément, et le plus
    // RÉCENT mais plus prioritaire l'emportait. Verrou d'A/B : NEOST_IPL_BATCH=0.
    // ⚠ Le CPU est ARRÊTÉ pendant le lot : les updateIpl() intermédiaires des callbacks
    // deviennent donc sans effet observable (Moira ne relit sa broche qu'à une frontière
    // d'instruction), et on les laisse en place — les retirer serait une seconde refonte.
    {
        static const bool iplBatch = []{ const char* e = std::getenv("NEOST_IPL_BATCH");
                                         return e ? std::atoi(e) != 0 : true; }();
        if (iplBatch)
            sched.setDispatchHooks(
                [this] { mfp.setInDispatch(true); },
                [this] { mfp.setInDispatch(false); mfp.flushIrqUpdate(); cpu.updateIpl(); });
    }
    ikbd.setScheduler(&sched);  // l'IKBD diffère sa réponse de reset ($F1)
    midi.setScheduler(&sched);  // l'ACIA MIDI date son TDRE sous TIE (cf. MIDI_TX)
    // Fixture de bouclage parallèle→joystick (test « Printer/Joystick », sous
    // --loopback) : le diagnostic écrit un motif sur le port parallèle (PSG port B,
    // R15) et attend de le relire sur les lignes joystick. Câblage (décodé du test) :
    //   • bits 0-2 de B → n : direction 1<<n (nibble bas → joy0, nibble haut → joy1)
    //   • bit7 de B → bouton (feu) joystick 0 ; bit6 de B → bouton joystick 1
    // Ligne BUSY Centronics (GPIP0) : sous fixture, le port parallèle (port B) bit7
    // pilote BUSY, inversé (bit7=1 → BUSY assertée → GPIP0=0). Le test « P1 Busy ».
    psg.setPortBSink([this](uint8_t b) {
        if (mfp.loopback()) { mfp.setBusyLine((b & 0x80) != 0); cpu.updateIpl(); }
    });
    // Imprimante Centronics : sur chaque FRONT de strobe (R14 bit5), l'octet du port B
    // est capturé dans le fichier imprimante (si activé via setPrinterFile) et la ligne
    // BUSY (GPIP0) est assertée bas — port fidèle de psg.c:388-390 (Printer_TransferByteTo
    // + MFP_GPIP_Set_Line_Input LINE0 LOW). Sans fichier imprimante : no-op (le défaut, donc
    // comportement inchangé tant que l'imprimante n'est pas explicitement activée).
    psg.setPrinterSink([this](uint8_t b) {
        if (!printerFile_) return;
        std::fputc(b, printerFile_);
        std::fflush(printerFile_);
        mfp.setBusyLine(true);
        cpu.updateIpl();
    });
    ikbd.setJoystickProbe([this](uint8_t& joy0, uint8_t& joy1) {
        // Clé sur un port joystick (Leader Board, Cricket Captain…) : recouvre les
        // directions, par-dessus l'état hôte.
        ports.onJoystick(joy0, joy1);
        // Hors fixture de bouclage : conserver l'état hôte déjà amorcé par l'IKBD
        // (manette USB / émulation clavier posée par le frontend via setJoystick).
        if (!mfp.loopback()) return;
        const uint8_t b = psg.regs_[15];
        const uint8_t dir = uint8_t(1u << (b & 7));          // direction encodée sur 8 bits
        joy0 = uint8_t((dir & 0x0F) | ((b & 0x80) ? 0x80 : 0));   // nibble bas + feu (bit7)
        joy1 = uint8_t(((dir >> 4) & 0x0F) | ((b & 0x40) ? 0x80 : 0)); // nibble haut + feu (bit6)
    });
    fdc.setScheduler(&sched);   // le FDC diffère la fin de commande (BUSY → INTRQ)
    blitter.setScheduler(&sched);  // tranches non-hog du blitter (cf. Scheduler::BLITTER)
    dmasnd.setScheduler(&sched);   // le son DMA date sa consommation DAC (FIFO → Timer A)
    dmasnd.setMfp(&mfp);
    // STE/Mega STE : le YM2149 est mixé à DEMI-amplitude (marge pour le son DMA, évite la
    // saturation) ; ST/Mega ST : pleine amplitude. Cf. YM2149::setOutputScale.
    psg.setOutputScale(machineIsSte(machineType_) ? 0.5f : 1.0f);
    // Filtre de sortie YM (cf. Hatari Sound_Update_Filters) : ST/Mega ST utilisent le
    // passe-bas analogique (C10, LPF_STF) ; STE/Mega STE le PWM (front montant passe-tout).
    psg.setStfLowPass(!machineIsSte(machineType_));
    // HPF sous-sonique : sur ST il est DANS la chaîne YM (sound.c:1744) ; sur STE le YM
    // entre BRUT dans le mix et c'est le MÉLANGE YM+DMA qui est filtré par la chaîne
    // LMC (dmaSnd.c:699,706 → DmaSound::applyHpfStereo, appelé par le rendu audio).
    psg.setHpfBypass(machineIsSte(machineType_));

    installSchedulerCallbacks();
}

// -----------------------------------------------------------------------------
//  Ordonnanceur : câblage des handlers et programmation d'une trame.
// -----------------------------------------------------------------------------
void Machine::installSchedulerCallbacks() {
    sched.setCallback(Scheduler::RENDER,  [this] { onRender(); });
    sched.setCallback(Scheduler::TIMER_B, [this] { onTimerB(); });
    sched.setCallback(Scheduler::HBL,     [this] { onHbl(); });
    sched.setCallback(Scheduler::VBL,     [this] { onVbl(); });
    // Timers MFP en mode délai : datés par le MFP, déclenchés ici (IRQ + IPL).
    sched.setCallback(Scheduler::TIMER_A, [this] { mfp.onTimerExpire(0); cpu.updateIpl(); });
    sched.setCallback(Scheduler::TIMER_C, [this] { mfp.onTimerExpire(2); cpu.updateIpl(); });
    sched.setCallback(Scheduler::TIMER_D, [this] { mfp.onTimerExpire(3); cpu.updateIpl(); });
    // Timer B en mode DÉLAI (≠ event-count) : daté par le MFP, déclenché ici.
    sched.setCallback(Scheduler::TIMER_B_DELAY, [this] { mfp.onTimerExpire(1); cpu.updateIpl(); });
    // Visibilité différée du signal IRQ MFP : le 68901 met 4 cycles à propager IRQ
    // vers le CPU (port MFP_IRQ_DELAY_TO_CPU). Le MFP arme cet événement à
    // irqTime+4 ; on recalcule alors l'IPL en mode COMMIT (frontière d'instruction,
    // délai écoulé → l'exception doit partir avant l'instruction suivante, comme
    // Hatari MFP_ProcessIRQ — sinon le délai s'additionnerait au pipeline IPL de
    // Moira et le test « T4 Video Counter » des diagnostics échoue).
    sched.setCallback(Scheduler::MFP_IRQ, [this] { cpu.updateIplNow(); });
    // Machine à états du FDC (port Hatari) : chaque phase (spin-up, head-load,
    // latence rotationnelle, transfert DMA octet par octet, INTRQ, arrêt moteur)
    // est datée et avancée ici. L'INTRQ (GPIP5 + canal 7) peut être levée/effacée.
    sched.setCallback(Scheduler::FDC,     [this] { fdc.onFdcEvent(); cpu.updateIpl(); });
    // (Scheduler::FDC_INDEX n'est plus utilisé : l'index est géré dans la machine
    // à états du FDC — comptage de tours pour spin-up / arrêt moteur, bit INDEX.)
    // (Scheduler::DMASND n'est plus armé : la fin de trame du son DMA STE est
    // détectée au FETCH de la FIFO 8 octets, cadencée par le HBL — DmaSound::onHbl.)
    // Réponse de reset du clavier ($F1) : l'IKBD l'a datée → on l'émet + IRQ ACIA.
    sched.setCallback(Scheduler::IKBD,   [this] { ikbd.onResetResponse(); cpu.updateIpl(); });
    // Livraison cadencée IKBD → ACIA (un octet série ≈ 10240 cycles) : l'octet en
    // tête de file devient visible (RDRF) et lève l'IRQ ACIA (cf. onRxDeliver).
    sched.setCallback(Scheduler::IKBD_RX, [this] { ikbd.onRxDeliver(); cpu.updateIpl(); });
    // Re-remplissage du registre d'émission ACIA (TDRE→1) ~1 octet série après une
    // écriture $FFFC02 sous TIE : ré-arme l'IRQ « transmetteur prêt » (cf. onTxEmpty).
    sched.setCallback(Scheduler::IKBD_TX, [this] { ikbd.onTxEmpty(); cpu.updateIpl(); });
    // Idem pour l'ACIA MIDI (~1 octet à 31250 bauds = 2560 cycles) : cadence l'IRQ
    // d'émission des séquenceurs MIDI (cf. MidiAcia::onTxEmpty).
    sched.setCallback(Scheduler::MIDI_TX, [this] { midi.onTxEmpty(); cpu.updateIpl(); });
    // MIDI_RX : horloge de réception d'un appareil hôte branché (cf. MidiAcia::
    // setRxSource). updateIpl car un octet entrant lève le canal 6 du MFP sous RIE.
    sched.setCallback(Scheduler::MIDI_RX, [this] { midi.onRxPace(); cpu.updateIpl(); });
    // Livraison cadencée d'un octet RX à l'USART MFP (injection hôte : modem
    // Hayes) : RxFull (canal 12) par octet, au débit configuré.
    sched.setCallback(Scheduler::SERIAL_RX, [this] { mfp.onSerialRxEvent(); cpu.updateIpl(); });
    // Étape de shift série Microwire ($FF8922 → 0) du son STE.
    sched.setCallback(Scheduler::MICROWIRE, [this] { dmasnd.onMicrowireShift(); });
    // Tranche non-hog du blitter (64 accès bus / 64 accès CPU) : la fin de blit
    // peut lever l'IRQ GPIP3 (canal 3 MFP) → IPL recalculé.
    sched.setCallback(Scheduler::BLITTER, [this] { blitter.onSlice(); cpu.updateIpl(); });
    // RESTART compteur vidéo fin de trame (Video_RestartVideoCounter) : la fréquence
    // du registre sync est relue À CET INSTANT (live, comme Hatari relit $FF820A) —
    // le restart n'a lieu que si elle correspond à la ligne (50 Hz↔310, 60 Hz↔260).
    sched.setCallback(Scheduler::VC_RESTART, [this] {
        const bool is50 = (shifter.sync & 0x02) != 0;
        if ((is50 && lpf_ == 313) || (!is50 && lpf_ == 263))
            shifter.restartVideoCounter(lpf_ - 3);
    });
}

// Arme les événements VIDÉO de la trame courante, à des cycles ABSOLUS (horloge
// continue) = frameStart_ + position dans la trame. Les Timers A/C/D persistent
// d'une trame à l'autre (datés par le MFP) et ne sont PAS réarmés ici.

void Machine::scheduleFrameEvents() {
    renderLine_ = 0;
    tbLine_     = 0;
    hblLine_    = 0;
    lineCarry_   = 0;     // V2/LINELEN : aucune ligne raccourcie au début de trame
    v2ShortLine_ = -1;
    shifter.beginFrame();                          // verrouille résolution + fréquence
    // Géométrie de la trame (50/60/71 Hz) figée pour toute la trame, lue ici.
    const Shifter::Geometry g = shifter.geometry();
    cpl_       = g.cyclesPerLine;
    curLineLen_ = cpl_;   // LINELEN : longueur nominale tant qu'aucun match freq
    lpf_       = g.linesPerFrame;
    disp_      = g.displayLines;
    deEnd_     = g.lineEndCycle;
    dispStart_ = g.dispStartLine;   // VDE_On : l'affichage actif commence à cette scanline

    // Premiers événements, à leur CYCLE EXACT dans la trame. Le RENDER et le Timer B
    // (event-count sur Display-Enable) ne se déclenchent QUE sur les lignes affichées,
    // donc d'abord à la scanline VDE_On (63 en 50 Hz) — l'affichage actif est centré
    // dans la trame, encadré des bordures haut/bas (port Hatari nStartHBL). Le HBL
    // niveau 2, lui, est émis à CHAQUE scanline (0..lpf-1) comme sur le vrai matériel
    // (Video_InterruptHandler_HBL) — il restera l'ancre de Video_EndHBL (bordures H/B).
    sched.schedule(Scheduler::RENDER,  frameStart_ + static_cast<int64_t>(dispStart_) * cpl_ + deEnd_);
    // Timer B : événement à CHAQUE scanline (comme le HBL) ; le tic n'est compté que
    // si la ligne est réellement AFFICHÉE d'après la machine Glue LIVE (cf. onTimerB) —
    // un retrait de bordure haut/bas en cours de trame déplace les tics, comme Hatari.
    tbScheduledAt_ = frameStart_ + timerBPos();
    sched.schedule(Scheduler::TIMER_B, tbScheduledAt_);
    // Position de l'IRQ HBL dans la ligne : Hatari HBL_VIDEO_CYCLE_OFFSET = 0 →
    // l'interruption tombe à la FRONTIÈRE de ligne (cycle 512 = 0 de la suivante).
    // L'ancien −4 était une calibration d'avant la refonte IACK (2026-07-02) ;
    // NEOST_HBL_OFF le rétablit pour A/B (valeur = décalage vs cpl, ex. -4).
    sched.schedule(Scheduler::HBL,     frameStart_ + (cpl_ + kHblOff(machineIsSte(machineType_))));   // HBL niveau 2 (frontière ligne 0)
    // Broche IPL pré-armée au cycle EXACT (montée mid-instruction via sync(), cf.
    // Cpu68k::armHblPinAt) — le dispatch de l'événement (frontière de bloc) arrive
    // 0..24 cyc plus tard et ne fait que re-poser la broche (idempotent).
    cpu.armHblPinAt(frameStart_ + (cpl_ + kHblOff(machineIsSte(machineType_))));
    // VBL niveau 4 — port fidèle de Hatari (Video_InterruptHandler_VBL) : l'IRQ VBL
    // est générée VBL_VIDEO_CYCLE_OFFSET cycles APRÈS la fin de la DERNIÈRE ligne de
    // la trame (313×512 + 64 en 50 Hz STF), donc au tout début du vblank = ~SOMMET de
    // la trame courante (la trame précédente vient de finir). On la cale à
    // frameStart_ + offset, et NON plus à la ligne 201 (~112 lignes / 57000 cyc trop
    // tôt) : le handler VBL du jeu (base écran, palette, sprites…) s'applique alors à
    // la trame qui VA s'afficher, comme sur le vrai matériel. Offset STF=64, STE=68.
    // VBL_VIDEO_CYCLE_OFFSET : STE = 68 ; STF = 64 en WS2/3/4 (tranché WS3), 60 en WS1.
    const int vblOffset = machineIsSte(machineType_) ? 68
                        : (Shifter::wakestate() == 1 ? 60 : 64);
    sched.schedule(Scheduler::VBL, frameStart_ + vblOffset);
    cpu.armVblPinAt(frameStart_ + vblOffset);          // broche niveau 4 au cycle exact (cf. armHblPinAt)
    // RESTART du compteur vidéo en FIN de trame (port Video_RestartVideoCounter,
    // ULM DSOTS) : ligne 310 (50 Hz, 313 lignes) / 260 (60 Hz, 263), cycle 56
    // (STF) / 60 (STE). La base $FF8201/03 est relue À CET INSTANT — après le
    // handler VBL du jeu — ce qui alimente correctement les moteurs double-buffer
    // beam-syncés (Enchanted Land en jeu). Le check de fréquence (registre sync
    // LIVE, comme Hatari qui relit $FF820A) se fait dans le callback. Pas de
    // restart en 71 Hz (Hatari : lignes définies pour 50/60 Hz seulement).
    if (lpf_ == 313 || lpf_ == 263) {
        const int restartLine = lpf_ - 3;
        const int restartPos  = machineIsSte(machineType_) ? 60 : 56;
        sched.schedule(Scheduler::VC_RESTART,
                       frameStart_ + static_cast<int64_t>(restartLine) * cpl_ + restartPos);
    }
}

void Machine::onRender() {
    // Décode la scanline à la fin de son Display-Enable (cycle 376), avec l'état
    // COURANT des registres (palette/base) — AVANT le tic Timer B (400) et le HBL
    // (508) de la même ligne, dont les handlers changeront les registres pour la
    // ligne SUIVANTE (rasters). Rendu purement « sortie » : n'altère ni CPU ni IRQ.
    const int h = shifter.activeHeight();          // lignes ACTIVES (≠ buffer overscan)
    if (renderLine_ < h) shifter.renderLine(renderLine_);
    ++renderLine_;
    // L'index actif renderLine_ correspond à la scanline (dispStart_ + renderLine_).
    if (renderLine_ < h && dispStart_ + renderLine_ < lpf_)
        sched.schedule(Scheduler::RENDER,
                       frameStart_ + static_cast<int64_t>(dispStart_ + renderLine_) * cpl_ + deEnd_ - lineCarry_);
}

void Machine::onTimerB() {
    // DIAG (NEOST_TB_TRACE=1) : chaque tic Timer B avec sa ligne, sa position DANS
    // la ligne et s'il a réellement pulsé (ligne affichée) — à diff'er contre les
    // « EndLine TB » de Hatari --trace video_hbl (chantier Closure : la démo
    // chronomètre les événements TB, cf. docs/CLOSURE_CHANTIER.md).
    // Timer B en event-count : décompte une fois par ligne AFFICHÉE (sur DE). La
    // fenêtre verticale est LIVE (machine Glue) : un retrait de bordure haut (60 Hz
    // vers la ligne 33 → VDE_On 34) ou bas en COURS de trame ajoute ses tics, comme
    // Hatari (Video_AddInterruptTimerB par ligne) — Enchanted Land VÉRIFIE l'effet
    // de ses impulsions en comptant les événements DE. tbLine_ = scanline ABSOLUE.
    // Position CIBLE au moment du callback : le DE de la ligne a pu être élargi par
    // une écriture POSTÉRIEURE à la planification (retrait de bordure droite : 60 Hz
    // à ~cyc 374 → DE_end 462 → tic à ~488). Hatari REPROGRAMME l'IRQ TB à chaque
    // écriture qui change le DE (Video_AddInterruptTimerB, video.c:2880-2891) ; ici
    // on re-vérifie à l'arrivée : cible plus loin → on se replanifie SANS tirer.
    // (Un DE raccourci après coup laisse le tic à l'ancienne position — même
    // approximation que la reprogrammation tardive d'Hatari quand le tic est passé.)
    {
        const int64_t real = shifter.timerBFrameCycleForLine(tbLine_, mfp.timerBStartOfLine());
        const int64_t target = (real >= 0)
            ? frameStart_ + real
            : frameStart_ + static_cast<int64_t>(tbLine_) * cpl_ + timerBPosLine(tbLine_) - lineCarry_;
        // Comparer à l'ÉCHÉANCE PLANIFIÉE, pas à l'heure de service : pendant un
        // STOP (la mesure de Closure vit sous stop #$2100), le réveil est
        // quantifié et le callback peut être servi des dizaines de cycles APRÈS
        // son échéance — « now ≥ target » concluait alors à tort que la cible
        // était passée, et le tic partait à la position par défaut (400) alors
        // que la Glue affichait 487 (mesuré : 310 tics sur 1344 chez Closure).
        if (tbScheduledAt_ < target) {
            tbScheduledAt_ = target;
            sched.schedule(Scheduler::TIMER_B, target);
            return;
        }
    }
    static const bool tbTrace = std::getenv("NEOST_TB_TRACE") != nullptr;
    if (tbTrace) {
        const int64_t pos = sched.now() - frameStart_
                          - static_cast<int64_t>(tbLine_) * cpl_ + lineCarry_;
        // posLine = ce que voit la Glue MAINTENANT (déclenche le catch-up) ; le
        // delta pos↔posLine au tir révèle les tics partis avant leur re-cible.
        const int pl = timerBPosLine(tbLine_);
        std::fprintf(stderr, "[TB] line=%d pos=%lld posLine=%d fired=%d\n",
                     tbLine_, (long long)pos, pl, shifter.liveLineDisplayed(tbLine_) ? 1 : 0);
    }
    if (shifter.liveLineDisplayed(tbLine_)) {
        mfp.hblank();
        cpu.updateIpl();                           // un underflow Timer B → IPL 6
    }
    ++tbLine_;
    if (tbLine_ < lpf_) {
        // Grille RÉELLE si disponible (cf. timerBFrameCycleForLine) ; sinon nominale.
        // La position sera de toute façon RE-VÉRIFIÉE au callback (bloc ci-dessus).
        const int64_t real = shifter.timerBFrameCycleForLine(tbLine_, mfp.timerBStartOfLine());
        tbScheduledAt_ = (real >= 0)
            ? frameStart_ + real
            : frameStart_ + static_cast<int64_t>(tbLine_) * cpl_ + timerBPosLine(tbLine_) - lineCarry_;
        sched.schedule(Scheduler::TIMER_B, tbScheduledAt_);
    }
}

void Machine::onHbl() {
    // DIAG (NEOST_HBL_DIAG=1) : timestamp ABSOLU de l'événement HBL (= la frontière
    // de ligne où la broche IRQ monte) — ancre le repère ligne dans les traces cyc=.
    static const long hblDiagN = []{ const char* s = std::getenv("NEOST_HBL_DIAG");
                                     return s ? std::atol(s) : 0L; }();
    if (hblDiagN) { static long n=0; if (++n % hblDiagN == 0)
        std::fprintf(stderr, "[HBLD] line=%d sched=%lld live=%lld\n",
                     hblLine_, (long long)sched.now(), (long long)sched.liveNow()); }
    cpu.raiseHbl();                                // HBL niveau 2 (gaté par le SR)
    // Son DMA STE : la FIFO 8 octets est entretenue à CHAQUE HBL (fetch au faisceau
    // + consommation DAC datée) — port de l'appel DmaSnd_STE_HBL_Update du handler
    // HBL d'Hatari (video.c:3322). Peut pulser Timer A (fin de trame) → IPL.
    if (machineHasDmaSound(machineType_)) { dmasnd.onHbl(); cpu.updateIpl(); }
    // Commit (compteur vidéo + capture lineSnap_) de la scanline qui SE TERMINE —
    // même ancre que le Video_EndHBL du handler HBL d'Hatari (video.c:3319).
    shifter.commitScanline(hblLine_);
    // Longueur RÉELLE de la ligne qui se termine (NEOST_LINELEN) : le cumul
    // lineCarry_ décale toutes les planifications des lignes suivantes (−carry),
    // comme la chaîne StartCycle+nCyclesPerLine de Hatari.
    if (lineLenOn_ && curLineLen_ != cpl_) {
        lineCarry_ += cpl_ - curLineLen_;
        curLineLen_ = cpl_;
    }
    ++hblLine_;
    // HBL émis à CHAQUE scanline (hblLine_ = numéro de ligne absolu 0..lpf-1), comme
    // sur le vrai matériel — y compris dans les bordures haut/bas (ancre Video_EndHBL).
    // V2 : −lineCarry_ décale la ligne suivante du cumul des raccourcissements (=0 hors V2).
    if (hblLine_ < lpf_) {
        const int64_t next = frameStart_ + static_cast<int64_t>(hblLine_) * cpl_
                           + (cpl_ + kHblOff(machineIsSte(machineType_))) - lineCarry_;
        sched.schedule(Scheduler::HBL, next);
        cpu.armHblPinAt(next);                 // broche au cycle exact (cf. armHblPinAt)
    }
}

void Machine::onVbl() {
    // DIAG beam-sync : dépassement (carry) du service VBL vs cycle théorique 64/68
    // = équivalent du pending_cyc d'Hatari (video_vbl). Compare la phase CPU↔faisceau.
    static const bool vblTrace = std::getenv("NEOST_VBL_TRACE") != nullptr;
    if (vblTrace) {
        const int64_t evt = frameStart_ + (machineIsSte(machineType_) ? 68 : 64);
        std::fprintf(stderr, "[vbl] over=%lld now=%lld\n",
                     static_cast<long long>(sched.now() - evt), static_cast<long long>(sched.now()));
    }
    cpu.raiseVbl();   // interruption trame (niveau 4) — une fois par trame
    // Tic VBL de l'IKBD (horloge interne $1B/$1C + report joystick auto). La durée
    // d'une trame en µs se déduit de la géométrie COURANTE : (lignes × cycles/ligne)
    // à 8 MHz (horloge bus du Shifter, indépendante du 8/16 MHz CPU MegaSTE).
    // ≈ 20032 µs (50 Hz) / 16700 µs (60 Hz) / 14028 µs (71 Hz mono).
    const int64_t kVblMicro = static_cast<int64_t>(lpf_) * cpl_ / 8;
    ikbd.onVbl(kVblMicro);
    fdc.setFrameCycles(static_cast<int64_t>(lpf_) * cpl_);   // fenêtre Mediach en VBL
    ports.onVbl(mfp);   // relâche le bouton Multiface/URC enfoncé pendant la trame
}

// -----------------------------------------------------------------------------
//  Une trame : horloge CONTINUE (les timers MFP la traversent). On exécute le CPU
//  d'événement en événement (carry du dépassement), puis on finit le décodage.
// -----------------------------------------------------------------------------
void Machine::runFrame() {
    // FIX1 (beam-sync) — ANCRE DE TRAME FIXE = VBL THÉORIQUE. Port de Hatari
    // VBL_ClockCounter = CyclesGlobalClockCounter − PendingCyclesOver (video.c:4964) qui
    // RETRANCHE le carry δ du dépassement de la dernière instruction enjambant frameEnd.
    // Au lieu de frameStart_ = sched.now() (qui CAPTE δ et le fait varier trame à trame),
    // on AVANCE frameStart_ de la longueur THÉORIQUE de la trame qui se termine (lpf_*cpl_
    // encore posés par le beginFrame précédent). δ ne vit plus que dans l'horloge CPU
    // (sched.now()) ; la grille d'events (scheduleFrameEvents) ET la datation
    // beamClock_/liveFrameClock_ dérivent toutes de frameStart_ → co-ancrées au théorique,
    // comme Hatari. Supprime le jitter de phase qui faisait sauter l'image (cf. workflow).
    // 1ʳᵉ trame : ancre sur sched.now() (= 0 après reset), comme l'ancien modèle, et NON
    // sur busClockNow() — le cœur a déjà avancé de ~40 cyc (lecture SSP/PC du reset) quand
    // la 1ʳᵉ trame démarre. Ancrer sur busClockNow décalerait la grille faisceau de ces 40
    // cyc → déphasage CPU↔faisceau qui casse les calibrations raster (Enchanted Land noir).
    // L'offset reset est ainsi absorbé dans la 1ʳᵉ trame, comme avant.
    // NEOST_LINELEN : la trame qui se termine était RACCOURCIE du cumul lineCarry_
    // (lignes 224/508) — l'ancre de la trame suivante avance de sa longueur RÉELLE
    // (≙ Hatari : la VBL est posée depuis le dernier HBL, la somme des longueurs
    // de lignes réelles fait la trame). Hors LINELEN, lineCarry_ vaut 0 ici (V2
    // seul le touche, opt-in) → comportement inchangé.
    // Trame RÉSUMABLE (débogueur) : l'ancre + la programmation des événements (le FIX1
    // ci-dessus) sont faites par beginFrame_(), UNIQUEMENT au début d'une trame. Après un
    // breakpoint (frameInProgress_ resté vrai) on SAUTE l'amorce et on REPREND la même
    // trame — pas de ré-ancrage → aucune dérive d'horloge CPU↔vidéo.
    if (!frameInProgress_) beginFrame_();
    const int64_t frameEnd = frameEnd_;
    // MODÈLE D'EXÉCUTION : BLOC. Le bloc CPU est borné au prochain événement,
    // dispatché à la frontière via runTo, avec préemption si un événement plus proche
    // est planifié en cours de bloc. sync() n'y avance QUE l'horloge.
    //
    // A34 (2026-08-28) : il y en avait DEUX. Le concurrent — « piloté par l'horloge »,
    // modèle `do_cycles` de WinUAE, où sync() dispatchait les événements au fil de
    // l'instruction — vivait derrière NEOST_SYNC_DISPATCH et n'a JAMAIS été validé par
    // les étalons. Supprimé : il deadlockait Enchanted Land (boucle beam-sync jamais
    // servie) sans corriger le jitter qu'il promettait, et la mesure re-prise sur
    // l'arbre du jour le confirme (palier `fast` rouge, blitter_timer 245 px contre 0).
    // Une branche morte-vivante dans la boucle d'exécution coûte plus qu'elle ne garde :
    // elle double le raisonnement de chaque lecteur et n'est exercée par rien.
    //
    // cpu.run() termine son instruction et peut dépasser frameEnd de quelques cycles
    // (carry, comme Hatari) ; la boucle reboucle si nécessaire.
    while (sched.now() < frameEnd) {
        int64_t next = sched.nextDue();
        if (next < 0 || next > frameEnd) next = frameEnd;
        const int64_t want = next - sched.now();
        sched.beginRun(next);
        const int ran = cpu.run(static_cast<int>(want > 0 ? want : 1));
        sched.endRun();
        sched.runTo(sched.now() + ran);
        if (cpu.breakpointHit()) return;  // débogueur : rend la main SANS finaliser (résumable)
    }
    // Fin de trame atteinte (pas de breakpoint) → on finalise et on rouvre une trame neuve.
    finalizeFrame_();
    frameInProgress_ = false;
}

// Amorce d'une trame (FIX1 beam-sync, cf. commentaire de runFrame) : ancre frameStart_
// au VBL THÉORIQUE, programme la grille d'événements (qui remet renderLine_=0), fixe
// frameEnd_. Appelée UNE fois par trame (runFrame et stepInstruction, guardés par
// frameInProgress_).
void Machine::beginFrame_() {
    if (frameStartInit_) frameStart_ += static_cast<int64_t>(lpf_) * cpl_ - (lineLenOn_ ? lineCarry_ : 0);
    else { frameStart_ = sched.now(); frameStartInit_ = true; }
    static const bool frameDiag = std::getenv("NEOST_FRAME_DIAG") != nullptr;
    if (frameDiag)
        std::fprintf(stderr, "[FRM] start=%lld mod4=%d lpf=%d cpl=%d\n",
                     (long long)frameStart_, (int)(frameStart_ & 3), lpf_, cpl_);
    scheduleFrameEvents();
    frameEnd_ = frameStart_ + static_cast<int64_t>(lpf_) * cpl_;
    // L'horloge temps réel compte les secondes en CYCLES : on lui donne la longueur
    // d'une seconde telle que la MACHINE la vit (trame verrouillée × Hz), et non une
    // constante. Sinon le RTC dérive contre la base de temps de tout le reste — et un
    // logiciel qui attend « une seconde » en comptant ses trames (la cartouche Atari
    // Field Service, test L) retombe systématiquement juste AVANT le tic.
    rtc.setSecondCycles(static_cast<int64_t>(lpf_) * cpl_ * shifter.refreshHz());
    frameInProgress_ = true;
}

// Finalisation d'une trame : rattrape les événements restants (CPU halté), décode les
// lignes non encore rendues, puis re-rend spec512 si détecté. Idempotence : appelée une
// fois quand frameEnd_ est atteint (ou à la fin d'un pas qui franchit la frontière).
void Machine::finalizeFrame_() {
    if (sched.now() < frameEnd_) sched.syncTo(frameEnd_);
    const int h = shifter.activeHeight();          // lignes ACTIVES (≠ buffer overscan)
    while (renderLine_ < h) shifter.renderLine(renderLine_++);
    shifter.finishFrame();
}

// Débogueur : pas-à-pas INSTRUCTION. Avance d'exactement UNE instruction 68000 en gardant
// l'ordonnanceur EN LOCKSTEP (sync() dispatche les événements au cycle pendant cpu.run) —
// pas de finalisation prématurée ni de dérive d'horloge. Si le pas franchit frameEnd_, la
// trame est finalisée ; la prochaine amorcera une trame neuve.
void Machine::stepInstruction() {
    if (!frameInProgress_) beginFrame_();
    cpu.clearBreakpointHit();   // arme le skip-once du PC courant → exécute même si BP ici
    // sync() n'avance QUE l'horloge — il faut dispatcher les événements échus ici,
    // comme la boucle de runFrame, sinon le pas-à-pas ne sert JAMAIS HBL/VBL/timers
    // (aucune IRQ) et tout se déverse d'un coup à la finalisation de trame —
    // comportement divergent de l'exécution continue.
    int64_t next = sched.nextDue();
    if (next < 0 || next > frameEnd_) next = frameEnd_;
    sched.beginRun(next);
    const int ran = cpu.run(1);   // run(1) = une instruction (toute instr ≥ 4 cyc > 1)
    sched.endRun();
    sched.runTo(sched.now() + ran);
    if (cpu.busClockNow() >= frameEnd_) { finalizeFrame_(); frameInProgress_ = false; }
}

static uint32_t stateCrc32(const uint8_t* p, std::size_t n);   // défini plus bas

// Empreinte de la cartouche montée, pour l'en-tête de save-state. Les 4 octets de
// CART_OLDGEMDOS ($FA0024) sont EXCLUS : GemdosHd::sysInit y écrit l'ancien vecteur GEMDOS
// au boot, si bien qu'un CRC brut comparait le contenu MUTÉ de la sauvegarde à la cartouche
// encore VIERGE de la session qui recharge — tout état pris avec --gemdos devenait
// définitivement irrechargeable, alors même que la v7 sérialise bus.cart pour le corriger.
static uint32_t cartFingerprint(const std::vector<uint8_t>& cart) {
    if (cart.empty()) return 0u;
    std::vector<uint8_t> tmp(cart);
    constexpr std::size_t kOldGemdosOff = GemdosHd::CART_OLDGEMDOS - 0xFA0000u;
    for (std::size_t i = 0; i < 4 && kOldGemdosOff + i < tmp.size(); ++i)
        tmp[kOldGemdosOff + i] = 0;
    return stateCrc32(tmp.data(), tmp.size());
}

// --- Save-states (increment 1) : CPU + RAM + ordonnanceur + état de trame ----------
// Méthode SYMÉTRIQUE (StateArchive gère save ET load) → l'ordre ne peut pas diverger.
// A17 (2026-08-27) : le numéro de version vivait EN DUR à deux endroits (écriture ici,
// contrôle dans loadStateFile) — et ils s'étaient déjà désynchronisés : le message de
// rejet annonçait « writes v16 » alors que le build écrivait v17. Constante unique.
static constexpr uint16_t kStateVersion = 19;
static uint32_t stateCrc32(const uint8_t* p, std::size_t n);   // défini plus bas
void Machine::serializeState(StateArchive& ar) {
    uint32_t magic   = 0x4E535453u;   // 'NSTS'
    uint16_t version = kStateVersion; // v19 : + EMPREINTE CRC32 DE LA ROM dans l'en-tête.
                                      // Les 2 octets de version TOS ne distinguaient pas
                                      // deux ROM localisées différentes (tos104us/uk/fr/de/es
                                      // portent toutes $0104, etos192us et etos192fr aussi) :
                                      // un état repris sous une AUTRE ROM de même version
                                      // était accepté, et la RAM restaurée — vecteurs
                                      // d'exception, piles, adresses de retour — pointait
                                      // dans une image différente. La ROM est hors-snapshot
                                      // par construction, donc seule une empreinte peut
                                      // garder l'appariement ;
                                      // v18 : + Scheduler::MIDI_RX (horloge de réception
                                      // de l'ACIA MIDI) — SRC_COUNT passe de 20 à 21,
                                      // donc le tableau due_ sérialisé change de taille ;
                                      // v17 : Rtc::mode_ bit3 = TIMER EN désormais HONORÉ
                                      // (un état v16 pouvait porter mode_=0, qui fige
                                      // maintenant le compteur) ;
                                      // v16 : + PortDevices (clés joystick/série, DAC, bouton) ;
                                      // v15 : + clé Notator dans CartridgeKey ;
                                      // v14 : + CartridgeKey (clé Steinberg /ROM3) ;
                                      // v13 : FujiNet RETIRÉ (matériel inexistant sur ST) —
                                      // FujiDevice, Acsi::fujiPending_ et le bit de drapeau
                                      // disparaissent, les bits suivants se décalent ;
                                      // v12 : + UltraSatan + Isp1160 (NetUSBee) + Acsi::
                                      // usatanPending_ ;
                                      // v11 : + Mfp::timerDueSub_ (phase MFP ×256) ;
                                      // v9 : + lineScrollSnap_ (scroll fin STE par
                                      // ligne, renderGlueFrame per-line) ; v8 : empreinte
                                      // cartouche INSENSIBLE aux octets mutés par le HD GEMDOS
                                      // + CTS/DCD actives au repos ; v7 : + empreinte GEMDOS/
                                      // cartouche et cart sérialisée ; v6 : + commitAnchor_
    ar(magic); ar(version);
    // Empreinte de configuration : un état n'est rechargeable QUE dans la même
    // config (loadState la vérifie AVANT de restaurer — sinon machine hybride :
    // bus.machine restauré mais machineType_/ROM de la session → timings/MMIO
    // incohérents). Sérialisée aussi ici pour rester dans le flux symétrique.
    uint8_t  mt    = static_cast<uint8_t>(machineType_);
    uint32_t ramSz = static_cast<uint32_t>(bus.ram.size());
    uint16_t tosV  = bus.tosVersion;
    ar(mt); ar(ramSz); ar(tosV);
    // Deux composantes d'empreinte ajoutées en v7, toutes deux nées d'un état ACCEPTÉ
    // à tort qui produisait une machine incohérente :
    //  · bit0 = HD GEMDOS actif. GemdosHd n'a AUCUN serialize (handles de fichiers
    //    hôte, DTA d'énumération en cours, lecteur courant, chemin courant) : recharger
    //    entre une session avec et une session sans laissait _drvbits annoncer C: et le
    //    vecteur trap #1 pointer dans le port cartouche vide → premier appel GEMDOS
    //    dans le décor. Tant que ce composant n'est pas sérialisable, on REFUSE.
    //  · empreinte de la cartouche : le port $FA0000 n'est peuplé que si une cartouche
    //    est montée, et la RAM restaurée peut y pointer.
    //  · bit1 = EtherNEC attaché (v13, ex-bit2). Le pointeur bus.ne2000 est réétabli
    //    par enableEtherNec avant un load, pas par la sérialisation.
    //  · bit2 = UltraSatan attaché, bit3 = NetUSBee (ISP1160) attaché (v13, ex-bit3/4) :
    //    mêmes raisons — pointeurs bus/ACSI réétablis par enable*, pas par la
    //    sérialisation.
    uint8_t flags = uint8_t((gemdos.active() ? 1u : 0u)
                            | (ne2000.enabled() ? 2u : 0u) | (usatanOn_ ? 4u : 0u)
                            | (isp1160.enabled() ? 8u : 0u)
                            | (dongle.attached() ? 16u : 0u));   // clé Cubase (v14)
    uint32_t cartFp = cartFingerprint(bus.cart);
    // Empreinte de l'IMAGE ROM elle-même. `tosV` ci-dessus ne porte que les 2 octets de
    // version lus à l'offset 2 : il ne sépare pas deux ROM de même version, alors que la
    // ROM est précisément le composant que le state NE contient PAS.
    uint32_t romFp = bus.rom.empty() ? 0u : stateCrc32(bus.rom.data(), bus.rom.size());
    ar(flags); ar(cartFp); ar(romFp);
    // CRC32 du payload (tout ce qui suit ce champ) : écrit par saveState (patch à
    // l'offset fixe 13), vérifié par loadState AVANT toute restauration. Dans le
    // flux symétrique il vaut 0 — seul le patch post-sérialisation le remplit.
    uint32_t crcField = 0;
    ar(crcField);
    if (ar.loading()) machineType_ = static_cast<MachineType>(mt);
    // Config banques RAM du GLUE ($FF8001) : lue en LIVE par mmuTranslate à chaque
    // accès RAM — sans elle, l'aliasing MMU d'une session différente relirait la
    // RAM restaurée de travers.
    ar(glue.memConfig_);
    // État de trame / géométrie (recalculable depuis les puces, mais on le fige pour un
    // save/load à une frontière de trame — les puces suivront à l'increment 2).
    ar(frameStart_); ar(frameStartInit_); ar(frameEnd_); ar(frameInProgress_);
    ar(renderLine_); ar(tbLine_); ar(hblLine_);
    ar(lineCarry_); ar(v2ShortLine_); ar(v2_);
    ar(lineLenOn_); ar(curLineLen_);
    ar(cpl_); ar(lpf_); ar(disp_); ar(deEnd_); ar(dispStart_);
    // ⚠ INVARIANTS DE GÉOMÉTRIE ET D'HORLOGE. runFrame boucle sur
    // `while (cpu.busClockNow() < frameEnd)` et finalizeFrame_ fait `sched.syncTo(frameEnd_)` :
    // une horloge maître incohérente rend ces boucles NON BORNÉES — gel définitif à 100 %
    // de CPU, terminable au seul SIGKILL (reproduit avec frameStart_, lineCarry_ ou
    // Scheduler::now_ forgés). Aucun `ar.check` ne couvrait cette section.
    // Bornes larges à dessein : elles ne visent que l'absurde, pas la validation fine.
    ar.check(cpl_ > 0 && cpl_ <= 4096 && lpf_ > 0 && lpf_ <= 4096);
    // Bornées EN HAUT aussi : ces trois compteurs de ligne servent de cible aux
    // boucles de rattrapage du Shifter (« while (vcLineY_ < y) endVideoLine(); »
    // dans commitScanline/videoCounter). Un hblLine_ forgé à ~2³¹ les rend
    // interminables — gel à 100 % de CPU, comme frameStart_/lineCarry_ ci-dessus.
    ar.check(renderLine_ >= 0 && tbLine_ >= 0 && hblLine_ >= 0
             && renderLine_ <= lpf_ && tbLine_ <= lpf_ && hblLine_ <= lpf_,
             "Machine::renderLine_/tbLine_/hblLine_ hors [0,lpf_]");
    ar.check(disp_ >= 0 && deEnd_ >= 0 && dispStart_ >= 0);
    ar.check(frameStart_ >= 0 && frameEnd_ >= frameStart_
             && frameEnd_ - frameStart_ <= 8 * int64_t(lpf_) * int64_t(cpl_));
    ar.check(lineCarry_ >= -int64_t(lpf_) * int64_t(cpl_)
             && lineCarry_ <=  int64_t(lpf_) * int64_t(cpl_));
    // curLineLen_ = longueur réelle en CYCLES de la ligne HBL courante (224..512 ;
    // peut dépasser cpl_ en overscan → lineCarry_ négatif). Consommé une fois par
    // advanceLine (lineCarry_ += cpl_ - curLineLen_, Machine.cpp:422) : forgé absurde
    // (~±2³¹), il désancre lineCarry_ APRÈS le check ci-dessus → planification HBL
    // aberrante. Borné comme cpl_/lpf_ (jamais atteint par une valeur réelle).
    ar.check(curLineLen_ > 0 && curLineLen_ <= 4096, "Machine::curLineLen_ hors ]0,4096]");
    // Composants. Ordre quelconque mais IDENTIQUE save/load (même méthode). Le SCC
    // (Mega STE) et l'état de commande ACSI (dans `fdc`) sont sérialisés ; seul le
    // CONTENU des images disque/disque dur reste hors-snapshot (il vit dans les
    // fichiers hôtes, `writeBack` les persiste au fil de l'eau).
    // NEOST_STATE_MAP=1 : trace l'offset de début de chaque composant (diagnostic
    // des divergences du test de déterminisme — l'offset fautif → la puce fautive).
    const bool mapDbg = !ar.loading() && std::getenv("NEOST_STATE_MAP");
    auto mapAt = [&](const char* who, size_t off) {
        if (mapDbg) std::fprintf(stderr, "[state-map] %-8s @ %zu\n", who, off);
    };
    bus.serialize(ar);      mapAt("cpu",     ar.saveSize());
    cpu.serialize(ar);      mapAt("sched",   ar.saveSize());
    sched.serialize(ar);    mapAt("shifter", ar.saveSize());
    // ⚠ ANCRAGE ABSOLU de la fenêtre de trame sur l'horloge maître — impossible plus
    // haut, `sched` n'étant restauré qu'ICI. Les gardes ci-dessus ne contraignent que
    // l'ÉCART entre frameStart_ et frameEnd_ : un couple décalé DE FAÇON COHÉRENTE
    // (les deux à 2^58, écart intact) les passait toutes, et `runFrame` bouclait
    // ensuite sur `while (busClockNow() < frameEnd)` pendant 2^58 cycles — le gel à
    // 100 % de CPU que le commentaire d'en haut annonce fermer, et qu'il ne fermait
    // que pour un champ bougé SEUL. `frameStart_ >= 0` n'avait aucune borne HAUTE.
    // Même slack que les autres gardes (8 trames) : on ne vise que l'absurde.
    {
        const int64_t slack = 8 * int64_t(lpf_) * int64_t(cpl_);
        const int64_t now = sched.now();
        ar.check(frameEnd_ - now <= slack && now - frameStart_ <= slack,
                 "Machine::frameStart_/frameEnd_ désancrés de l'horloge maître");
    }
    shifter.serialize(ar);  mapAt("mfp",     ar.saveSize());
    mfp.serialize(ar);      mapAt("psg",     ar.saveSize());
    psg.serialize(ar);      mapAt("dmasnd",  ar.saveSize());
    dmasnd.serialize(ar);   mapAt("blitter", ar.saveSize());
    blitter.serialize(ar);  mapAt("ikbd",    ar.saveSize());
    ikbd.serialize(ar);     mapAt("midi",    ar.saveSize());
    midi.serialize(ar);     mapAt("rtc",     ar.saveSize());
    rtc.serialize(ar);      mapAt("fdc",     ar.saveSize());
    fdc.serialize(ar);      mapAt("scc",     ar.saveSize());   // inclut l'ACSI
    scc.serialize(ar);      mapAt("ne2000",  ar.saveSize());   // SCC Z85C30 (Mega STE)
    ne2000.serialize(ar);   mapAt("usatan",  ar.saveSize());   // NE2000/EtherNEC (v10)
    usatan.serialize(ar);   mapAt("isp1160", ar.saveSize());   // UltraSatan (v12)
    isp1160.serialize(ar);  mapAt("dongle",  ar.saveSize());   // ISP1160/NetUSBee (v12)
    dongle.serialize(ar);   mapAt("ports",   ar.saveSize());   // clé cartouche (v14/v15)
    ports.serialize(ar);    mapAt("fin",     ar.saveSize());   // périphériques des ports (v16)
    if (ar.loading()) {
        // tbScheduledAt_ n'est pas dans le flux : chaque schedule(TIMER_B) lui est
        // apparié, donc il se re-dérive de l'échéance restaurée. Le laisser à la
        // valeur de la SESSION COURANTE faussait le test « tbScheduledAt_ < target »
        // d'onTimerB après chargement d'un état pris en cours de trame (tic Timer B
        // parti à la mauvaise position → une ligne raster décalée).
        const int64_t rem = sched.rawCyclesUntil(Scheduler::TIMER_B);
        tbScheduledAt_ = (rem == INT64_MIN) ? 0 : sched.now() + rem;
        bus.refreshUds();   // la clé noire a pu (dé)verrouiller son choix
        portsApply(/*fromLoad=*/true);   // re-câble hooks/DAC SANS toucher aux lignes GPIP restaurées
    }
}

// En-tête d'un .state v19 : magic(4) version(2) machine(1) ram(4) tos(2) flags(1)
// cartFp(4) romFp(4) crc32(4) — le CRC du payload reste le DERNIER champ de l'en-tête.
static constexpr std::size_t kStateHeaderSize = 26;
static constexpr std::size_t kStateCrcOffset  = 22;

// CRC32 (IEEE, réflexe, sans table) du payload : détecte un fichier corrompu de la
// bonne longueur AVANT de muter la machine — la seule troncature était couverte.
// Table du CRC-32 (polynôme réfléchi $EDB88320), construite une fois. La version
// bit à bit d'origine faisait 8 décalages PAR OCTET : sur un état de 1,4 Mo elle
// était le poste DOMINANT d'une reprise (mesuré : 21,6 ms par restauration, dont
// deux CRC complets et une sérialisation de sauvegarde, contre 1,6 ms pour émuler
// une trame). Valeurs produites IDENTIQUES — même polynôme, même initialisation,
// même inversion finale : le format de fichier ne bouge pas et les états déjà
// écrits restent lisibles (vérifié en relisant des états du binaire précédent).
static const uint32_t* stateCrcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            t[i] = c;
        }
        return t;
    }();
    return table.data();
}

static uint32_t stateCrc32(const uint8_t* p, std::size_t n) {
    const uint32_t* t = stateCrcTable();
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}

void Machine::saveState(std::vector<uint8_t>& out) {
    out.clear();
    StateArchive ar = StateArchive::saver(out);
    serializeState(ar);
    // Patch du CRC32 du payload à son offset fixe (le flux symétrique a écrit 0).
    if (out.size() >= kStateHeaderSize) {
        const uint32_t crc = stateCrc32(out.data() + kStateHeaderSize,
                                        out.size() - kStateHeaderSize);
        std::memcpy(out.data() + kStateCrcOffset, &crc, 4);
    }
}

bool Machine::loadState(const uint8_t* data, std::size_t n) {
    // Valide l'en-tête (magic + version + empreinte de config + CRC du payload)
    // AVANT de restaurer quoi que ce soit : un état d'une autre machine/RAM/TOS est
    // REFUSÉ (sinon machine hybride ST/STE ou RAM invitée incohérente avec la ROM
    // présente), un payload corrompu aussi.
    if (n < kStateHeaderSize) return false;
    uint32_t magic;   std::memcpy(&magic, data, 4);
    uint16_t version; std::memcpy(&version, data + 4, 2);
    if (magic != 0x4E535453u) return false;
    if (version != kStateVersion) {
        std::fprintf(stderr, "[state] rejected: unsupported format v%u (this build of "
                     "NeoST writes v%u) — older states are not compatible\n",
                     version, static_cast<unsigned>(kStateVersion));
        return false;
    }
    uint8_t  mt    = data[6];
    uint32_t ramSz; std::memcpy(&ramSz, data + 7, 4);
    uint16_t tosV;  std::memcpy(&tosV, data + 11, 2);
    if (mt != static_cast<uint8_t>(machineType_) || ramSz != bus.ram.size()
        || tosV != bus.tosVersion) {
        std::fprintf(stderr, "[state] rejected: the state was saved on a different config "
                     "(machine %u/RAM %u KB/TOS %03x vs session %u/%zu KB/%03x)\n",
                     mt, ramSz / 1024u, tosV, unsigned(machineType_),
                     bus.ram.size() / 1024u, bus.tosVersion);
        return false;
    }
    const uint8_t  flags  = data[13];
    uint32_t cartFp; std::memcpy(&cartFp, data + 14, 4);
    uint32_t romFp;  std::memcpy(&romFp,  data + 18, 4);
    const uint32_t curRomFp = bus.rom.empty() ? 0u
                                              : stateCrc32(bus.rom.data(), bus.rom.size());
    if (romFp != curRomFp) {
        // La version TOS contrôlée plus haut ne suffit PAS : tos104us/uk/fr/de/es portent
        // toutes $0104, tos106* toutes $0106, etos192us et etos192fr aussi $0104. Un état
        // repris sous une ROM DIFFÉRENTE de même version passait donc, et la RAM restaurée
        // — vecteurs d'exception, piles, adresses de retour, pointeurs système — désignait
        // des adresses d'une AUTRE image : CPU figé, sans un mot d'explication.
        std::fprintf(stderr, "[state] rejected: the state was saved with a DIFFERENT ROM "
                     "image (same TOS version %03x, but the bytes differ) — reload the "
                     "exact ROM used for the save\n", bus.tosVersion);
        return false;
    }
    const uint8_t  curFlags  = uint8_t((gemdos.active() ? 1u : 0u)
                                       | (ne2000.enabled() ? 2u : 0u) | (usatanOn_ ? 4u : 0u)
                                       | (isp1160.enabled() ? 8u : 0u)
                                       | (dongle.attached() ? 16u : 0u));
    const uint32_t curCartFp = cartFingerprint(bus.cart);
    if (flags != curFlags) {
        std::fprintf(stderr, "[state] rejected: peripheral config mismatch — GEMDOS HD "
                     "%s->%s, EtherNEC %s->%s, UltraSatan %s->%s, "
                     "NetUSBee %s->%s, Cubase dongle %s->%s (these must match the save)\n",
                     (flags & 1) ? "active" : "inactive", (curFlags & 1) ? "active" : "inactive",
                     (flags & 2) ? "active" : "inactive", (curFlags & 2) ? "active" : "inactive",
                     (flags & 4) ? "active" : "inactive", (curFlags & 4) ? "active" : "inactive",
                     (flags & 8) ? "active" : "inactive", (curFlags & 8) ? "active" : "inactive",
                     (flags & 16) ? "active" : "inactive", (curFlags & 16) ? "active" : "inactive");
        return false;
    }
    if (cartFp != curCartFp) {
        std::fprintf(stderr, "[state] rejected: the mounted cartridge is not the one from "
                     "the save (fingerprint %08x vs %08x)\n", cartFp, curCartFp);
        return false;
    }
    uint32_t crc; std::memcpy(&crc, data + kStateCrcOffset, 4);
    if (crc != stateCrc32(data + kStateHeaderSize, n - kStateHeaderSize)) {
        std::fprintf(stderr, "[state] rejected: invalid payload CRC (corrupt file)\n");
        return false;
    }
    // Filet de sécurité : un load qui échoue à MI-restauration (ok_=false — champ
    // hors bornes détecté par un ar.check(), taille incohérente — ou exception
    // d'allocation) laisserait la machine à moitié mutée. On fige l'état courant
    // et on le rejoue (le backup vient d'être produit par ce même code → valide).
    // Ce tampon ne quitte JAMAIS la mémoire et son rejeu passe par StateArchive sans
    // repasser par l'en-tête : lui calculer un CRC ne protégeait rien et doublait
    // presque le coût d'une reprise d'état. On sérialise donc à nu.
    std::vector<uint8_t> backup;
    { StateArchive bar = StateArchive::saver(backup); serializeState(bar); }
    bool ok = false;
    try {
        StateArchive ar = StateArchive::loader(data, n);
        serializeState(ar);   // relit l'en-tête (déjà validé) puis restaure le reste
        ok = ar.ok();
    } catch (...) { ok = false; }
    if (!ok) {
        StateArchive rb = StateArchive::loader(backup.data(), backup.size());
        serializeState(rb);
        std::fprintf(stderr, "[state] truncated/inconsistent file — session state restored\n");
        return false;
    }
    return true;
}

bool Machine::saveStateFile(const std::string& path) {
    std::vector<uint8_t> buf;
    saveState(buf);
    // Écriture ATOMIQUE (tmp + rename) : le slot est unique — un crash/disque plein
    // en cours d'écriture ne doit pas détruire le seul état existant.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
        if (!f) { std::remove(tmp.c_str()); return false; }
    }
    // fs::rename et non std::rename : la CRT Windows refuse d'écraser une
    // destination existante — le slot unique neost.state ne se réécrivait jamais.
    std::error_code rnec;
    std::filesystem::rename(tmp, path, rnec);
    if (rnec) { std::remove(tmp.c_str()); return false; }
    return true;
}

bool Machine::loadStateFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    // tellg() sur un répertoire renvoie 2^63-1 sous Linux (pas -1) → borne haute.
    // Un état légitime = RAM (≤ 4 Mo) + puces + marges ; 64 Mo est très large.
    if (n <= 0 || n > 64 * 1024 * 1024) return false;
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.read(reinterpret_cast<char*>(buf.data()), n);
    return loadState(buf.data(), buf.size());
}
