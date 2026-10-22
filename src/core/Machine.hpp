// =============================================================================
//  Machine.hpp — La carte mère Atari ST assemblée + la boucle d'horloge.
//
//  Regroupe tous les composants (Bus, CPU, Shifter, PSG, MFP, IKBD, GLUE) et
//  encapsule le timing d'une trame. AUCUNE dépendance GUI : c'est ce qui permet
//  d'exécuter exactement la même machine en mode fenêtré (neost) ou en headless
//  (neost-headless), garantissant des traces reproductibles.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Shifter.hpp"
#include "core/YM2149.hpp"
#include "core/DmaSound.hpp"
#include "core/Blitter.hpp"
#include "core/MmuGlue.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"
#include "io/Ikbd.hpp"
#include "io/Fdc.hpp"
#include "io/Rtc.hpp"
#include "io/MidiAcia.hpp"
#include "io/GemdosHd.hpp"
#include "io/Scc.hpp"
#include "io/Ne2000.hpp"
#include "io/UltraSatan.hpp"
#include "io/Isp1160.hpp"
#include "io/CartridgeKey.hpp"
#include "io/PortDevices.hpp"

class Machine {
public:
    // Timing PAL basse résolution 50 Hz = valeurs de RÉFÉRENCE (et défaut). La
    // géométrie RÉELLE d'une trame est désormais dynamique (50/60/71 Hz), dérivée
    // de la résolution + $FF820A et verrouillée à beginFrame — cf. Shifter::Geometry
    // et les membres cpl_/lpf_/disp_ ci-dessous.
    static constexpr int CYCLES_PER_LINE = 512;
    static constexpr int LINES_PER_FRAME = 313;
    static constexpr int VISIBLE_LINES   = 200;

    // cpuCore : cœur 68000 (toujours Moira) ; machine : profil matériel (ST/STE/…).
    // Le profil machine est choisi AVANT le démarrage (figé à la construction).
    explicit Machine(std::size_t ramBytes = 512u * 1024u,
                     CpuCore cpuCore = CpuCore::Moira,
                     MachineType machine = MachineType::Ste);
    ~Machine() {
        if (printerFile_) std::fclose(printerFile_);
    }

    MachineType machineType() const { return machineType_; }

    // --- Audio « push » (Phase C) : horloge pour dater les écritures registres -----
    // Durée de la trame courante en cycles CPU (lignes × cycles/ligne). Sert à calibrer
    // le nombre d'échantillons produits et à mapper les écritures horodatées.
    int64_t frameCycles() const { return static_cast<int64_t>(lpf_) * cpl_; }
    // Cycle CPU écoulé depuis le début de la trame (horloge LIVE, delta intra-quantum
    // inclus) : c'est l'estampille d'une écriture PSG faite en plein bloc CPU. À câbler
    // sur YM2149::setCycleClock côté frontend audio (cf. main.cpp).
    int64_t frameRelCycle() const { return sched.liveNow() - frameStart_; }

    // Abaisse le type machine si le TOS de `romPath` ne le supporte pas — port de
    // Hatari `TOS_CheckSysConfig` : un TOS <= 1.04 (TOS 1.0x, EmuTOS 192 Ko qui se
    // présente en « Atari ST » 1.4) ne tourne qu'en mode ST/68000 → sur STE/Mega STE
    // on bascule en ST (avertissement sur stderr). Le Mega ST, lui, tourne nativement
    // sous TOS 1.0x → conservé. À appeler AVANT de construire la Machine (lit la
    // version dans l'en-tête ROM, mot big-endian à l'offset 2).
    static MachineType adjustMachineForTos(MachineType requested, const std::string& romPath);

    bool loadTos(const std::string& path)  { return bus.loadTos(path); }
    bool loadCart(const std::string& path) { return bus.loadCart(path); }
    void ejectCart() { bus.ejectCart(); }
    bool loadDisk(const std::string& path)  { return fdc.loadImage(path, 0); }   // lecteur A
    bool loadDiskB(const std::string& path) { return fdc.loadImage(path, 1); }   // lecteur B (optionnel)
    // Imprimante Centronics : capture les octets du port parallèle (PSG port B, émis sur
    // FRONT de strobe) dans un fichier hôte (ajout binaire). Chemin vide = désactive.
    // Active aussi le handshake BUSY (GPIP0) à la Hatari (cf. ctor). Renvoie false si l'ouverture échoue.
    bool setPrinterFile(const std::string& path) {
        if (printerFile_) { std::fclose(printerFile_); printerFile_ = nullptr; }
        if (path.empty()) return true;
        printerFile_ = std::fopen(path.c_str(), "ab");
        return printerFile_ != nullptr;
    }
    // À chaud : LMC1992 préservé. Mega STE : $FF8E21 → 0 (8 MHz, cache invalidé,
    // port de Hatari MegaSTE_CPU_Cache_Reset) + FPU au repos. Couverture calquée
    // sur Hatari reset.c : Video_Reset, FDC_Reset, Blitter_Reset (annule un blit
    // en vol — sinon il continuerait de corrompre la RAM PENDANT le boot),
    // IKBD_Reset (→ $F1 différé) et recopie des vecteurs reset $0-$7 en RAM.
    void reset() {
        desktopLineA_ = 0;
        psg.reset(); dmasnd.reset(/*cold=*/false); mfp.reset();
        shifter.reset();           // Video_Reset : scroll/linewidth/offset différé STE, glue
        fdc.reset(/*cold=*/false); // FDC_Reset : commande/DMA en vol annulés
        blitter.reset();           // Blitter_Reset : blit en vol annulé, BUSY retombe
        ikbd.resetHw();            // IKBD_Reset (chaud) : SCI + modes aux défauts, $F1 différé
        bus.megaSteReset(); cpu.setMegaSteSpeed(false); bus.fpu.reset(); bus.scu.reset(/*cold=*/false);
        gemdos.reset();    // ferme les fichiers HD GEMDOS ouverts (no-op si inactif)
        scc.reset();       // SCC série (Mega STE) au repos
        midi.reset();      // ACIA MIDI (reset.c:111 ACIA_Reset + :124 Midi_Reset)
        dongle.reset(); bus.refreshUds();   // clé cartouche : registres à 0
        ports.reset();
        bus.seedResetVectors();    // vecteurs SSP/PC $0-$7 : miroir ROM en RAM (stMemory.c)
        cpu.reset();
        frameStartInit_ = false;   // FIX1 : ré-ancre frameStart_ sur sched.now() à la 1re trame post-reset
        frameInProgress_ = false;  // débogueur : un reset repart sur une trame neuve
    }
    // Reset à FROID (power-cycle) : efface toute la ST-RAM, ce qui invalide le
    // « memvalid » de TOS — il refait alors un boot COMPLET (re-détection mémoire,
    // re-init OS) au lieu du boot à chaud d'un simple reset. Puis reset matériel.
    void hardReset() {
        desktopLineA_ = 0;
        bus.ram.assign(bus.ram.size(), 0);
        psg.reset(); dmasnd.reset(/*cold=*/true); mfp.reset();
        shifter.reset();           // cf. reset() : couverture Hatari reset.c
        fdc.reset(/*cold=*/true);
        blitter.reset(/*cold=*/true); // miroir MMIO brut effacé à la mise sous tension
        ikbd.resetHw();
        bus.megaSteReset(); cpu.setMegaSteSpeed(false); bus.fpu.reset(); bus.scu.reset(/*cold=*/true);
        gemdos.reset();    // ferme les fichiers HD GEMDOS ouverts (no-op si inactif)
        scc.reset();       // SCC série (Mega STE) au repos
        midi.reset();      // ACIA MIDI (reset.c:111 ACIA_Reset + :124 Midi_Reset)
        dongle.reset(); bus.refreshUds();   // clé cartouche : registres à 0
        ports.reset();
        // $FF8001 remis à 0 au FROID (stMemory.c:93, bCold). ⚠ DIVERGENCE ASSUMÉE : à la
        // CONFIGURATION (constructeur / reconfigure), NeoST le pré-remplit au contraire
        // avec memConfigForBytes() alors qu'Hatari laisse 0 et compte sur le test mémoire
        // du TOS pour le programmer. Visible uniquement pour du code exécuté AVANT le TOS
        // (sonde MMIO : NeoST 0x04 vs Hatari 0x00 sur 1 Mo) ; après boot le TOS l'écrase.
        if (bus.glue) bus.glue->memConfig_ = 0;
        bus.seedResetVectors();    // vecteurs SSP/PC $0-$7 (après l'effacement RAM !)
        cpu.reset();
        frameStartInit_ = false;   // FIX1 : ré-ancre frameStart_ sur sched.now() à la 1re trame post-reset
        frameInProgress_ = false;  // débogueur : un reset repart sur une trame neuve
    }

    // Reconfigure la machine À CHAUD sans recréer l'objet (son adresse reste
    // stable → les références externes, p.ex. Audio→psg/dmasnd, restent valides) :
    // change la taille de ST-RAM, le modèle matériel et le cœur 68000. Efface la
    // RAM (boot à froid). Les composants déjà câblés au bus sont conservés.
    // L'appelant recharge la ROM si besoin, repose le moniteur, puis reset().
    void reconfigure(std::size_t ramBytes, CpuCore cpuCore, MachineType machine) {
        bus.ram.assign(ramBytes, 0);
        bus.machine     = machine;
        machineType_    = machine;
        mfp.setHasBlitter(machineHasBlitter(machine));   // GPIP3 : pull-up R4 sans blitter
        bus.scc         = (machine == MachineType::MegaSte) ? &scc : nullptr;  // SCC (Mega STE)
        glue.memConfig_ = memConfigForBytes(ramBytes);
        bus.megaSteReset();                // $FF8E21 → 0 (8 MHz, cache invalidé)
        cpu.setMegaSteSpeed(false);
        cpu.setCore(cpuCore);              // bascule de cœur 68000 si nécessaire
        // DIP switches $FF9200 : l'octet haut dépend du modèle (0xBF MegaSTE,
        // 0xFF sinon) — sans cette mise à jour, une bascule STE↔MegaSTE à chaud
        // exposait les DIP de l'ancienne machine (cf. ctor).
        bus.stePads.setMegaSte(machine == MachineType::MegaSte);
        psg.setOutputScale(machineIsSte(machine) ? 0.5f : 1.0f);   // ½ ampli YM sur STE (cf. ctor)
        psg.setStfLowPass(!machineIsSte(machine));                 // LPF_STF sur ST/Mega ST, PWM sur STE
        // HPF sous-sonique : suit la machine comme au ctor — oublié, une bascule
        // STE→ST à chaud laissait le YM SANS recentrage DC (hpfBypass_ resté vrai),
        // et ST→STE filtrait le YM DEUX fois (chaîne YM + HPF du mix).
        psg.setHpfBypass(machineIsSte(machine));
        // Reconfigure = boot à FROID (la RAM est effacée) : le LMC1992/Microwire repart
        // aux défauts — le reset() chaud que l'appelant enchaîne les PRÉSERVE, et sans
        // ça l'état microwire d'une session STE (mixing/tonalité) colorait la suivante.
        dmasnd.reset(/*cold=*/true);
        blitter.reset(/*cold=*/true);
    }

    // Exécute UNE trame complète : 313 lignes de cycles CPU, 4 tics Timer C
    // (≈200 Hz) et un VBL niveau 4. Décode l'image en fin de trame.
    //
    //  Depuis la Phase 1 de cycle-accuracy (cf. docs/CYCLE_ACCURACY.md), la trame
    //  est pilotée par `sched` : on exécute le CPU jusqu'au prochain événement
    //  daté (HBL/Timer C/VBL) puis on déclenche son handler. Le quantum CPU reste
    //  la ligne (512 cycles) → timing IDENTIQUE au modèle « par blocs » d'avant.
    void runFrame();

    // Débogueur : avance d'UNE instruction 68000 en gardant l'ordonnanceur EN LOCKSTEP
    // (sync() dispatche les événements au cycle) — pas de ré-ancrage de trame. Si le pas
    // franchit la fin de trame, la trame est finalisée et une nouvelle démarre au pas
    // suivant. S'appuie sur la trame RÉSUMABLE (cf. frameInProgress_).
    void stepInstruction();

    // Save-states CPU/RAM/puces et contexte GEMDOS. saveState échoue sans produire
    // de buffer si un fichier hôte n'est plus accessible. loadState refuse les
    // fichiers modifiés, sans toucher à la session courante.
    bool saveState(std::vector<uint8_t>& out);
    bool desktopMousePosition(int& x, int& y, int& width, int& height) const;
    bool desktopMouseMove(int x, int y, bool left, bool right);
    bool loadState(const uint8_t* data, std::size_t n);
    // Variantes fichier (wrappers I/O). saveStateFile écrit le buffer ; loadStateFile lit
    // le fichier puis loadState. Renvoient false sur erreur d'I/O ou en-tête invalide.
    bool saveStateFile(const std::string& path);
    bool loadStateFile(const std::string& path);

    // Accès direct aux composants (frontend, débogueur, headless).
    Bus       bus;
    Shifter   shifter{bus};
    YM2149    psg;
    DmaSound  dmasnd{bus};
    Blitter   blitter{bus};
    MmuGlue   glue;
    Mfp       mfp;
    Ikbd      ikbd{mfp};
    Fdc       fdc{bus, psg, mfp};
    Rtc       rtc;
    MidiAcia  midi{mfp};
    Cpu68k    cpu{bus};
    // Émulation disque dur GEMDOS (redirection vers un dossier hôte). Inactive tant
    // que le frontend n'a pas appelé gemdos.setDirectory(...) — cf. io/GemdosHd.hpp.
    GemdosHd  gemdos{bus, cpu};
    Scc       scc;     // SCC série Z85C30 ($FF8C80) — Mega STE uniquement (cf. ctor/reconfigure)
    // Carte réseau NE2000 sur le port cartouche (EtherNEC, extension NeoST,
    // INACTIVE par défaut). Le backend physique (NetBackend) est posé par le
    // frontend. Exclusive d'une cartouche montée (mêmes adresses $FA0000).
    Ne2000    ne2000;
    // Interface SD UltraSatan sur le bus ACSI (extension NeoST, INACTIVE par
    // défaut) : 2 slots = 2 cibles, horloge propre, paquets ICD 'US…'. Cf. io/UltraSatan.hpp.
    UltraSatan usatan;
    // Contrôleur hôte USB ISP1160 du NetUSBee (extension NeoST, INACTIF par défaut).
    // NetUSBee = ne2000 (RTL8019AS, câblage EtherNEC) + isp1160, même port cartouche.
    Isp1160   isp1160;
    // Clé Steinberg (Cubase 2/3) sur /ROM3 — OFF par défaut. Cf. io/CartridgeKey.hpp.
    CartridgeKey dongle;
    // Périphériques des ports joystick 0/1, RS-232, imprimante, bouton de cartouche
    // (Leader Board, B.A.T. II, Music Master, Pro Sound Designer, Multiface…) — OFF par
    // défaut, un par port, coexistants. Cf. io/PortDevices.hpp.
    PortDevices ports;
    Scheduler sched;

    // Active la NE2000/EtherNEC. Refuse si une cartouche est montée (conflit de
    // fenêtre $FA0000) — renvoie false et n'active rien. N'est PAS remise à zéro
    // par reset() (une carte réseau survit au reboot de l'ST) ; hardReset() la
    // reset comme un power-cycle de la carte.
    bool enableEtherNec() {
        if (!bus.cart.empty()) return false;   // cartouche présente : conflit
        // Clé Steinberg déjà branchée : MÊME exclusivité que dans setDongle, mais dans
        // l'autre sens. Ne la tester que là-bas rendait l'interdit dépendant de l'ORDRE
        // (clé d'abord → les deux cohabitaient sur le port cartouche, alors que réseau
        // d'abord était refusé). L'EtherNEC décode toute la fenêtre : c'est exclusif.
        if (dongle.attached()) return false;
        ne2000.setEnabled(true);
        ne2000.reset();
        bus.ne2000 = &ne2000;
        return true;
    }
    void disableEtherNec() { bus.ne2000 = nullptr; ne2000.setEnabled(false); }

    // Active le NetUSBee : la NE2000 (exactement l'EtherNEC) + l'ISP1160 USB, sur
    // le port cartouche. Même exclusivité qu'EtherNEC vis-à-vis d'une cartouche.
    bool enableNetUsbee() {
        if (!enableEtherNec()) return false;
        isp1160.setEnabled(true);
        isp1160.reset();
        bus.isp1160 = &isp1160;
        return true;
    }
    void disableNetUsbee() { bus.isp1160 = nullptr; isp1160.setEnabled(false); disableEtherNec(); }

    // Branche une clé Steinberg sur /ROM3 (None = débranche). Pas d'exclusivité avec
    // le GEMDOS HD ($FA0000 = /ROM4) ; incompatible avec EtherNEC/NetUSBee qui
    // décodent toute la fenêtre — refusé dans ce cas.
    bool setDongle(CartridgeKey::Model m) {
        if (m != CartridgeKey::Model::None && (ne2000.enabled() || isp1160.enabled())) return false;
        dongle.setModel(m);
        if (dongle.attached()) bus.addCartDevice(&dongle); else bus.removeCartDevice(&dongle);
        return true;
    }
    bool netUsbeeEnabled() const { return isp1160.enabled(); }

    // Branche `d` sur le port `p` (None = débranche). Faux si le connecteur ne convient
    // pas. Pas de conflit possible avec le reste : rien ici ne décode d'adresse.
    bool plugPort(PortDevices::Port p, PortDevices::Device d) {
        if (!ports.plug(p, d)) return false;
        portsApply();
        return true;
    }
    // Re-câble les crochets selon les périphériques branchés (après plug, load d'état).
    // `fromLoad` : au PLUG, on remet au repos les lignes qu'un périphérique précédent
    // a pu laisser hors repos ; au LOAD, ces lignes (cts/dcd/riLine_) viennent d'être
    // RESTAURÉES par mfp.serialize — les forcer ici écraserait l'état restauré (Jeanne
    // d'Arc mi-protection, bouclage DTR) et gpipSetLine pourrait lever une IRQ GPIP
    // fantôme au chargement. Seule GPIP7 (monButton_, hors snapshot) est re-dérivée.
    void portsApply(bool fromLoad = false) {
        psg.setPortBDac(ports.usesPortBDac());
        if (fromLoad) {
            mfp.setMonitorButton(ports.buttonPressed() &&
                ports.at(PortDevices::Port::CartButton) == PortDevices::Device::Multiface);
        } else {
            // Lignes qu'un périphérique précédent a pu laisser hors repos : bouton (GPIP7),
            // RI (GPIP6), DCD (Jeanne d'Arc — repos ACTIF, cf. Mfp::reset).
            mfp.setMonitorButton(false); mfp.setRs232Ri(false); mfp.setRs232Dcd(true);
        }
        if (ports.hasSerial())
            mfp.setGpipReadHook([this](uint8_t& v) { ports.gpipRead(v, sched.now()); });
        else
            mfp.setGpipReadHook(nullptr);
    }
    // Bouton du Multiface / Ultimate Ripper (sans effet si aucun n'est branché).
    void pressPortButton() { ports.pressButton(mfp); cpu.updateIpl(); }

    // Active l'UltraSatan sur les cibles ACSI `firstTarget` et `firstTarget+1`
    // (défaut 0-1 : c'est l'ID d'usine, le TOS y boote). Les images SD se montent
    // ensuite avec fdc.mountAcsi(path, firstTarget + slot). Survit au reset/hardReset.
    void enableUltraSatan(int firstTarget = 0) {
        fdc.attachUltraSatan(&usatan, firstTarget);
        usatanOn_ = true;
    }
    void disableUltraSatan() { fdc.detachUltraSatan(); usatanOn_ = false; }
    bool ultraSatanEnabled() const { return usatanOn_; }

private:
    bool usatanOn_ = false;    // UltraSatan attaché (config, hors snapshot — cf. flags d'état)
    // Câble les callbacks de l'ordonnanceur (appelé une fois, au constructeur).
    void installSchedulerCallbacks();
    // Arme le premier événement de chaque source pour la trame courante.
    void scheduleFrameEvents(bool scheduleVbl = true);
    void beginFrame_();     // amorce une trame (ancre + événements) — cf. runFrame/stepInstruction
    void finalizeFrame_();  // finalise une trame (rattrapage + décodage lignes + spec512)
    void serializeState(StateArchive& ar, bool hostFiles = true);
    mutable uint32_t desktopLineA_ = 0; // cache hôte dérivé, validé avant chaque utilisation
    // Handlers des événements datés vidéo (positions au cycle dans la ligne).
    // Les Timers A/C/D (mode délai) sont datés par le MFP lui-même.
    void onRender();        // décode la scanline (≈ fin Display-Enable, cycle 376)
    void retargetTimerB(bool startOfLine); // AER3 : ancien front servi avant mutation
    void onTimerB();        // Timer B event-count sur DE (position DE-dépendante)
    void onHbl();           // HBL niveau 2 (cycle 508)
    void onVbl();

    // Position (cycle DANS la ligne) du tic Timer B event-count, dérivée du
    // Display-Enable (résolution + 50/60 Hz du Shifter, front début/fin via l'AER du
    // MFP) — cf. Shifter::timerBLinePos / Hatari Video_TimerB_GetDefaultPos.
    int timerBPos() const { return shifter.timerBLinePos(mfp.timerBStartOfLine()); }
    // Position PAR LIGNE (DE réel de la machine Glue — tricks compris), pour le
    // Timer B event-count : cf. Shifter::timerBPosForLine et Machine::onTimerB.
    int timerBPosLine(int line) { return shifter.timerBPosForLine(line, mfp.timerBStartOfLine()); }
    int64_t tbScheduledAt_ = 0;   // échéance PLANIFIÉE du tic Timer B courant (cf. onTimerB)

    MachineType machineType_ = MachineType::Ste;   // profil matériel (figé au boot)

    int64_t frameStart_ = 0;  // cycle (horloge continue) du début de la trame courante
    bool    frameStartInit_ = false;  // FIX1 : frameStart_ ancré au VBL THÉORIQUE (avance de
                                      // lpf_*cpl_) après la 1re trame, au lieu de sched.now()
    // Trame RÉSUMABLE (débogueur) : au breakpoint on rend la main SANS finaliser la trame
    // (frameInProgress_ reste vrai) → la reprise/le pas continue la MÊME trame, sans
    // ré-ancrer ni dériver l'horloge. frameEnd_ = cible de fin de trame courante.
    int64_t frameEnd_        = 0;
    bool    frameInProgress_ = false;
    int renderLine_  = 0;     // prochaine scanline à décoder
    int tbLine_      = 0;     // prochaine ligne pour le tic Timer B
    int hblLine_     = 0;     // prochaine ligne pour le HBL niveau 2
    // V2 res-switch (opt-in NEOST_V2) : longueur de ligne VARIABLE. Une impulsion hi-res
    // PRÉCOCE (cyc ≤ 56) raccourcit la ligne à 224 cyc (port Hatari HBL_Pos/nCyclesPerLine,
    // video.c:2249) : le HBL est reprogrammé tôt et les lignes SUIVANTES sont décalées de
    // (cpl-224) = lineCarry_. Le HW dérive ainsi la phase du gestionnaire fullscreen.
    int64_t lineCarry_ = 0;   // décalage cumulé (cyc) dû aux lignes raccourcies cette trame
    int     v2ShortLine_ = -1; // dernière ligne déjà raccourcie (évite double-comptage)
    bool    v2_ = false;      // NEOST_V2 actif ?
    // Chantier longueurs de ligne PAR-LIGNE (gated NEOST_LINELEN, port complet
    // HBL_Pos/nCyclesPerLine — cf. setLineGeom dans le ctor + doc maître).
    bool    lineLenOn_  = false;
    int     curLineLen_ = CYCLES_PER_LINE;   // longueur retenue pour la ligne HBL courante
    // frameEnd_ sur la grille RÉELLE : lignes terminées (lineCarry_) + écart de la ligne
    // EN COURS (cpl_ − curLineLen_). À rappeler dès que curLineLen_ change (cf. onHbl).
    void syncFrameEnd_() {
        frameEnd_ = frameStart_ + static_cast<int64_t>(lpf_) * cpl_ - lineCarry_
                  - (cpl_ - curLineLen_);
    }
    // (Le RTC avance en paresseux à la lecture, cf. Rtc::catchUp — plus de compteur ici.)

    // Géométrie de la trame COURANTE (50/60/71 Hz), verrouillée par scheduleFrameEvents
    // depuis Shifter::geometry() juste après beginFrame. Défauts = 50 Hz PAL.
    int cpl_   = CYCLES_PER_LINE;   // cycles par ligne (512/508/224)
    int lpf_   = LINES_PER_FRAME;   // lignes par trame (313/263/501)
    int disp_  = VISIBLE_LINES;     // scanlines affichées = Timer B + rendu (200/400)
    int deEnd_ = DE_END_CYCLE;      // fin Display-Enable dans la ligne (376/372/160)
    // Numéro de la PREMIÈRE scanline affichée (VDE_On : 63/34/34, cf. Shifter::Geometry).
    // L'affichage actif occupe les scanlines [dispStart_, dispStart_+disp_) ; les lignes
    // 0..dispStart_-1 sont la bordure HAUTE, dispStart_+disp_..lpf_-1 la bordure BASSE.
    // Avant, l'affichage commençait à la ligne 0 (pas de bordure haute dans la timeline).
    int dispStart_ = 63;            // VDE_On de la trame courante (verrouillé par scheduleFrameEvents)

    // Positions au cycle DANS la ligne (STF PAL 50 Hz, cf. Hatari video.h).
    static constexpr int DE_END_CYCLE   = 376;   // fin Display-Enable → rendu ligne
    // (Timer B : position dérivée du Display-Enable, cf. timerBPos / Shifter::timerBLinePos.)
    static constexpr int HBL_CYCLE      = 508;   // 512 - 4 (Hbl_Int_Pos_Low_50)

    std::FILE* printerFile_ = nullptr;   // imprimante Centronics : fichier de capture (nul = désactivée)
};
