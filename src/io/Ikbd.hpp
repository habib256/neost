// =============================================================================
//  Ikbd.hpp — ACIA 6850 clavier + contrôleur IKBD (HD6301) de l'Atari ST.
//
//  Le clavier ST est un micro-contrôleur intelligent (IKBD) relié au 68000 par
//  une liaison série à travers une ACIA 6850 ($FFFC00 contrôle/statut,
//  $FFFC02 données). L'IKBD envoie des scancodes : "make" à l'appui, make|0x80
//  au relâchement. Quand un octet est reçu, l'ACIA tire la ligne GPIP4 du MFP
//  (canal 6) → interruption niveau 6.
//
//  NeoST modélise : la file de réception, les bits de statut ACIA, et juste ce
//  qu'il faut de l'IKBD (réponse 0xF1 au reset) pour qu'EmuTOS soit content.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <type_traits>

#include "core/Scheduler.hpp"
#include "core/Pacing.hpp"
#include "core/StateArchive.hpp"

class Mfp;

class Ikbd {
public:
    explicit Ikbd(Mfp& mfp);

    // Ordonnanceur : l'IKBD y date sa réponse de reset (l'IRQ ACIA doit arriver
    // APRÈS coup, pas pendant l'instruction qui envoie la commande).
    void setScheduler(Scheduler* s) { sched_ = s; }

    uint8_t read8(uint32_t addr);            // $FFFC00 statut / $FFFC02 données
    void    write8(uint32_t addr, uint8_t v);

    // Échéance : l'IKBD a fini son auto-test → envoie $F1 (réponse de reset).
    void    onResetResponse();

    // Warm-boot de la ROM IKBD (port d'IKBD_Boot_ROM(false) d'Hatari) : remise
    // aux défauts, tampons vidés, scanState_ effacé, puis fenêtre critique et
    // réponse $F1 différée. Commun au reset $80,$01, à exitExeMode et au reset
    // MACHINE (Machine::reset — Hatari reset.c appelle IKBD_Reset).
    void    bootRom();

    // RESET MATÉRIEL de l'IKBD (port d'IKBD_Reset, ikbd.c:499-522) : remet d'abord
    // le SCI (la liaison série 6301 → ACIA) — l'octet EN VOL sur la ligne est perdu,
    // son échéance de livraison annulée — PUIS boote la ROM. bootRom() seul (≙
    // IKBD_Boot_ROM) ne vide que le tampon de sortie du firmware : un octet déjà
    // engagé dans le registre à décalage arrivait quand même APRÈS le reset.
    void    resetHw();

    // Échéance CPU→IKBD : transfert TDR→TSR ou réception du bit de stop.
    void    onTxEmpty();

    // Entrée du firmware HD6301 : octet reçu après le bit de stop.
    // Les tests du protocole peuvent injecter ici sans traverser l'ACIA.
    void receiveSerialByte(uint8_t v);

    // Échéance : un octet série IKBD → ACIA est arrivé (~10240 cycles après son
    // départ de la file) → il devient le RDR courant (RDRF=1, IRQ ACIA). Cette
    // CADENCE est celle du SCI d'Hatari (1024 cycles/bit à 7812,5 bauds) ; des
    // jeux (Vroom) synchronisent leur parseur de paquets souris dessus — une
    // livraison instantanée des 3 octets leur fait confondre dx et dy.
    void    onRxDeliver();

    // Échantillon joystick $17 : timer indépendant du VBL, en cycles bus 8 MHz.
    void    onJoystickMonitor();

    // Scrutation firmware indépendante du VBL : Hatari IKBD_AUTOSEND, 150000 cycles.
    void    startAutoSend();
    void    onAutoSend();

    // Sonde joystick : peut RÉ-ÉCRIRE (joy0, joy1) à l'interrogation `$16`/au
    // report auto. Les valeurs sont d'abord amorcées avec l'état hôte courant
    // (cf. setJoystick) ; le diagnostic « Printer/Joystick » installe ici un
    // fixture de bouclage parallèle→joystick (Machine connecte le port B du PSG)
    // qui ÉCRASE cet état le temps du test. Hors fixture, la sonde laisse passer
    // l'état hôte intact.
    void    setJoystickProbe(std::function<void(uint8_t&, uint8_t&)> fn) { joyProbe_ = std::move(fn); }

    // État joystick venant de l'hôte (manette USB ou émulation clavier du
    // frontend). Index 0 = port ST 0 (partagé souris), 1 = port ST 1 (port
    // « jeux »). Bits : haut $01 / bas $02 / gauche $04 / droite $08 / feu $80
    // (cf. Hatari ATARIJOY_BITMASK_*, joy.h). Lu à chaque interrogation $16 et,
    // en mode auto ($14), à chaque trame via sendAutoJoysticks.
    void    setJoystick(uint8_t joy0, uint8_t joy1) { hostJoy_[0] = joy0; hostJoy_[1] = joy1; }
    // Relit l'état hôte TENU (brut, sans la sonde ni le masquage souris de
    // readJoystickState) : le mode serveur s'en sert pour reprendre l'entrée qu'un
    // --joy ou un état restauré a posée, au lieu de supposer 0.
    void    hostJoystick(uint8_t& joy0, uint8_t& joy1) const { joy0 = hostJoy_[0]; joy1 = hostJoy_[1]; }

    // Force l'horloge IKBD (YY MM DD hh mm ss, décimal). Utilisé par le headless
    // pour un boot DÉTERMINISTE : EmuTOS/TOS lit la date/heure du bureau via la
    // commande IKBD $1C — sans ça l'horloge hôte casse les diffs pixel à pixel.
    void    setClock(int yy, int mm, int dd, int hh, int mi, int ss) {
        auto bcd = [](int v) { return uint8_t(((v / 10) << 4) | (v % 10)); };
        clock_[0] = bcd(yy); clock_[1] = bcd(mm); clock_[2] = bcd(dd);
        clock_[3] = bcd(hh); clock_[4] = bcd(mi); clock_[5] = bcd(ss);
        clockMicro_ = 0;
    }

    // Événement clavier venant de l'hôte (scancode ST déjà traduit).
    void keyEvent(uint8_t scancode, bool pressed);

    // A39 (2026-08-29) — la table des longueurs de commande, exposée pour le seul
    // test de protocole (tests/selftest_logic.cpp). Une longueur fausse ne se voit
    // PAS à l'exécution : elle décale le flux de commandes du jeu qui l'utilise, et
    // de lui seul. La comparer ligne à ligne à Hatari KeyboardCommands[] est le seul
    // moyen de la garder.
    int commandLengthForTest(uint8_t opcode) { return cmdLength(opcode); }

    // Empreinte de reconnaissance des programmes 6301 custom (fonction d'Hatari,
    // défaut compris), et installation directe du handler Froggies : les vrais
    // programmes ne sont pas versionnés, et l'empreinte n'est pas inversible.
    static uint32_t customCodeCrcForTest(const uint8_t* data, std::size_t len);
    void armFroggiesForTest();
    void armCommonBootForTest();

    // Mouvement/boutons souris (cf. signe frontend : dx>0 = droite, dy>0 = bas).
    // En mode relatif, draine le Δ en paquets $F8 de 3 octets sous contrôle du
    // seuil ($0B), du signe d'axe Y ($0F/$10), et émet aussi sur changement de
    // bouton SANS mouvement. En mode absolu, accumule la position (échelle $0C).
    void mouseEvent(int dx, int dy, bool left, bool right);
    bool desktopMouseReady() const {
        return mouseMode_ == REL && !exeMode_ && !pauseOutput_ && !duringResetCriticalTime_
            && aciaClockInitialized_ && rx_.empty() && !rxPending_ && !rdrf_
            && !rxOverrun_ && xThreshold_ == 1 && yThreshold_ == 1 && yAxis_ == 1 && !mouseAction_;
    }

    // Tic VBL : horloge interne et garde des vingt premières trames.
    // Sans ordonnanceur seulement, sert aussi de repli pour la scrutation.
    void onVbl(int64_t vblMicro);

    // Sérialisation save-state SYMÉTRIQUE (save ET load) : transfère tout l'état
    // runtime de l'ACIA/IKBD. Ne touche PAS les liaisons (mfp_, sched_, joyProbe_)
    // ni la file rx_ (std::deque non sérialisable — voir note). Défini inline pour
    // l'accès aux membres privés.
    void serialize(StateArchive& ar) {
        // File de sortie IKBD→CPU (std::deque : pas d'aide StateArchive → longueur + octets).
        {
            uint32_t n = static_cast<uint32_t>(rx_.size());
            ar(n);
            ar.check(n <= 1024, "Ikbd::firmware output queue exceeds capacity");
            if (ar.loading()) {
                rx_.clear();
                for (uint32_t i = 0; i < n && ar.ok(); ++i) { uint8_t b = 0; ar(b); rx_.push_back(b); }
            } else {
                for (uint8_t b : rx_) ar(b);
            }
        }
        // --- ACIA / réception ---
        ar(rdr_);
        ar(rdrf_);
        ar(rxPending_);
        ar(rxShiftData_);
        ar(rxOverrun_);
        ar(ovrn_);
        ar(srRead_);
        ar(control_);
        ar(aciaClockInitialized_);
        ar(txEnableInt_);
        ar(tdre_);
        ar(txData_); ar(txShiftData_); ar(txShifting_);
        ar(txClockPhase_);
        ar(txNextBitCycle_); ar(txBitIndex_);
        ar(sciRxState_); ar(sciRxByte_); ar(sciRxBitsLeft_);
        ar.check(txNextBitCycle_ >= 0 && txBitIndex_ <= 8, "Ikbd::TX bit state");
        ar.check(sciRxState_ <= 2 && sciRxBitsLeft_ <= 8 &&
                 (sciRxState_ == 1 ? sciRxBitsLeft_ > 0 : sciRxBitsLeft_ == 0),
                 "Ikbd::SCI receive state");
        ar.check(txClockPhase_ >= 0 && txClockPhase_ < 1024, "Ikbd::txClockPhase_");

        // --- Tampon de commande multi-octets ---
        ar(inBuf_);        // std::array<uint8_t, 8>
        ar(inBufLen_);
        ar(cmdExpected_);
        // ⚠ INVARIANTS OBLIGATOIRES. write8 n'éprouve que la borne HAUTE avant
        // `inBuf_[inBufLen_] = v` : un save-state forgé avec un inBufLen_ NÉGATIF
        // donnait au programme émulé une écriture d'octet à un offset arbitraire
        // (±2 Go autour de inBuf_) — et comme l'état porte aussi la RAM invitée,
        // la primitive était complète. Les autres composants (YM, DmaSound, Scc,
        // Acsi, Shifter, Fdc) gardent déjà leurs index ; ces deux-ci avaient été oubliés.
        ar.check(inBufLen_    >= 0 && inBufLen_    <= int(inBuf_.size()));
        ar.check(cmdExpected_ >= 0 && cmdExpected_ <= int(inBuf_.size()));

        // --- Joystick hôte ---
        ar.arr(hostJoy_);  // uint8_t[2]

        // --- Mode souris ---
        // Transité par le TYPE SOUS-JACENT, jamais par l'énumération elle-même :
        // matérialiser dans une enum une valeur hors de son domaine est un
        // COMPORTEMENT INDÉFINI (UBSan sur un .state forgé : « load of value 173,
        // which is not a valid value for type MouseMode »), et mouseMode_ pilote
        // ensuite des comparaisons dans tout le traitement souris. Même taille que
        // l'enum → format de fichier INCHANGÉ (pas de bump de version).
        std::underlying_type_t<MouseMode> mm =
            static_cast<std::underlying_type_t<MouseMode>>(mouseMode_);
        ar(mm);
        const bool mmOk = (mm >= REL && mm <= CURSOR);
        ar.check(mmOk, "Ikbd::mouseMode_ hors domaine");
        if (ar.loading() && mmOk) mouseMode_ = static_cast<MouseMode>(mm);
        ar(absX_);
        ar(absY_);
        ar(absMaxX_);
        ar(absMaxY_);
        ar(prevAbsButtons_);
        ar(prevL_);
        ar(prevR_);

        // --- Paramètres paquet souris relatif ---
        ar(xThreshold_);
        ar(yThreshold_);
        ar(xScale_);
        ar(yScale_);
        ar(yAxis_);
        ar(bOldL_);
        ar(bOldR_);
        ar(mouseDeltaX_);
        ar(mouseDeltaY_);
        ar(mouseLeft_);
        ar(mouseRight_);

        // --- MouseAction ($07) + mode curseur ($0A) ---
        ar(mouseAction_);
        ar(keyCodeDeltaX_);
        ar(keyCodeDeltaY_);

        // --- Horloge interne IKBD ---
        ar.arr(clock_);    // uint8_t[6]
        ar(clockMicro_);

        // --- Mode joystick ---
        // Enum transité par le type sous-jacent (cf. mouseMode_ ci-dessus) : un .state
        // forgé posant joyMode_ hors {JOY_OFF..JOY_MONITOR} matérialiserait une valeur
        // hors domaine, ensuite lue par onVbl (comparaisons Ikbd.cpp) = UB (UBSan enum).
        std::underlying_type_t<JoystickMode> jm =
            static_cast<std::underlying_type_t<JoystickMode>>(joyMode_);
        ar(jm);
        const bool jmOk = (jm >= JOY_OFF && jm <= JOY_KEYCODE);
        ar.check(jmOk, "Ikbd::joyMode_ hors domaine");
        if (ar.loading() && jmOk) joyMode_ = static_cast<JoystickMode>(jm);
        ar(prevJoy0_);
        ar(prevJoy1_);
        ar(vblCount_);
        ar(duringResetCriticalTime_);

        // --- Fenêtre de reset : octet retenu dans le RDR du 6301 (ROM $F010-$F139) ---
        ar(heldByte_);
        ar(heldOverrun_);
        ar.arr(hostKeys_);                    // v54 : touches tenues (balayage du reset)
        ar(ocfOrigin_);                       // v54 : phase de l'interruption 1 ms du 6301
        ar(clockSet_);
        ar.check(heldByte_ >= -1 && heldByte_ <= 0xFF, "Ikbd::heldByte_ hors domaine");

        // --- Rapports d'état $88 / $9A (v51) ---
        std::underlying_type_t<MouseMode> lm =
            static_cast<std::underlying_type_t<MouseMode>>(lastMouseMode_);
        ar(lm);
        const bool lmOk = (lm == REL || lm == ABS || lm == CURSOR);
        ar.check(lmOk, "Ikbd::lastMouseMode_ hors domaine");
        if (ar.loading() && lmOk) lastMouseMode_ = static_cast<MouseMode>(lm);
        ar(joysticksEnabled_);
        ar(mouseOn_);                         // v52 : bit 7 de $C9 (rapport $92)
        ar(port0Joy_);                        // v52 : bit 1 de $C9 (port 0 au joystick)
        ar.arr(kjParam_);                     // v52 : joystick en keycode ($19)
        ar.arr(kjKey_);
        ar.arr(kjStart_);
        ar.arr(kjLast_);
        ar.check(kjStart_[0] >= 0 && kjStart_[1] >= 0 && kjLast_[0] >= kjStart_[0]
                 && kjLast_[1] >= kjStart_[1], "Ikbd::keycode joystick times hors domaine");
        ar(kjFire_);
        ar(fireShift_);                       // v52 : surveillance du tir ($18)
        ar(fireCount_);
        ar(monitorContinuous_);
        ar.check(fireCount_ >= 1 && fireCount_ <= 8, "Ikbd::fireCount_ hors domaine");

        // --- Pause output ($13) ---
        ar(pauseOutput_);
        ar(pauseKeep_);
        ar(monitorFrozen_);
        ar.check(pauseKeep_ <= rx_.size(), "Ikbd::pauseKeep_ hors domaine");
        ar.check(monitorFrozen_ >= -1 && monitorFrozen_ <= int64_t(8021247) * 255 / 100,
                 "Ikbd::monitorFrozen_ hors domaine");

        // --- État code 6301 custom ($20/$22) ---
        // Deux enums transités par le type sous-jacent (cf. mouseMode_) : sans ça, un
        // .state forgé les matérialise hors domaine (customWrite_ lu par les switch de
        // dispatch 6301, customRead_ par la lecture event-driven) = UB. L'ancien
        // ar.check(customWrite_...) plus bas LISAIT déjà l'enum hors-domaine avant de
        // rejeter — la validation doit porter sur l'entier sous-jacent, AVANT le cast.
        std::underlying_type_t<CustomW> cw =
            static_cast<std::underlying_type_t<CustomW>>(customWrite_);
        ar(cw);
        const bool cwOk = (cw >= CW_NONE && cw <= CW_AS);
        ar.check(cwOk, "Ikbd::customWrite_ hors domaine");
        if (ar.loading() && cwOk) customWrite_ = static_cast<CustomW>(cw);
        std::underlying_type_t<CustomR> cr =
            static_cast<std::underlying_type_t<CustomR>>(customRead_);
        ar(cr);
        const bool crOk = (cr >= CR_NONE && cr <= CR_AS_MONO);
        ar.check(crOk, "Ikbd::customRead_ hors domaine");
        if (ar.loading() && crOk) customRead_ = static_cast<CustomR>(cr);
        ar(exeMode_);
        ar(memLoadLeft_);
        ar(memLoadTotal_);
        ar(memExeNbBytes_);
        ar(memLoadCrc_);
        ar.arr(scanState_);   // uint8_t[128]
        ar(mDeltaX_);
        ar(mDeltaY_);
        ar(mLatchDX_);
        ar(mLatchDY_);
        ar(lmb_);
        ar(chaosFirst_);
        ar(chaosIgnore_);
        ar(chaosIndex_);
        ar(chaosCount_);
        // chaosWrite() indexe key[8] avec la valeur BRUTE : le masque « & 0x07 » n'est
        // appliqué qu'à l'INCRÉMENT, donc le premier octet décodé après un load lirait
        // hors du tableau si l'état a été forgé (même classe que les gardes YM/SCC/FPU).
        ar.check(chaosIndex_ >= 0 && chaosIndex_ < 8);
        ar.check(chaosIgnore_ >= 0 && chaosIgnore_ <= 8);
        ar.check(chaosCount_ >= 0);
        // (customWrite_ est désormais validé plus haut, sur l'entier sous-jacent AVANT
        // le cast dans l'enum — l'ancien ar.check ici lisait l'enum déjà hors-domaine.)
        ar(asMagic_);
        ar(asReadCount_);
        ar(joystickMonitorPeriod_);
        ar.check(joystickMonitorPeriod_ == 0 ||
                 (joystickMonitorPeriod_ >= kKeycodePollCycles &&
                  joystickMonitorPeriod_ <= int64_t(8021247) * 255 / 100),
                 "Ikbd::joystick monitor period out of range");
        ar(responseDelayBits_);
        ar(rxWaitingDelay_);
        ar.check(responseDelayBits_ >= 0 && responseDelayBits_ <= 10,
                 "Ikbd::response delay out of range");
        ar.check(!rxWaitingDelay_ || (rxPending_ && responseDelayBits_ > 0),
                 "Ikbd::response delay phase invalid");
    }

private:
    void trace(const char* format, ...) const; // NEOST_KBD_TRACE, stderr, aucun état émulé
    void pushRx(uint8_t b);                  // empile un octet IKBD → CPU (livraison cadencée)
    void pushRxDelayed(uint8_t b, int delayCycles); // attente firmware globale avant le SCI
    // Place pour `n` octets dans le tampon de sortie IKBD borné à 1024 (port de
    // Hatari IKBD_OutputBuffer_CheckFreeCount) : un émetteur de PAQUET teste la
    // taille TOTALE avant son 1er pushRx → paquet entier ou rien (jamais coupé).
    bool rxFree(std::size_t n) const;
    void armRx(int64_t baseCycle = -1);       // date attente firmware ou prochain octet
    void raiseIfReady(int64_t when = -1);     // GPIP4 daté ; -1 = accès courant
    bool irqActive() const;                  // cause d'IRQ : RX (RDRF & RIE) OU TX (TIE & TDRE)

    // Renvoie le nombre total d'octets (commande incluse) attendu pour `opcode`,
    // d'après la table KeyboardCommands[] de Hatari (ikbd.c). 0 = opcode inconnu
    // (traité comme une commande mono-octet ignorée).
    static int cmdLength(uint8_t opcode);

    // Exécute la commande IKBD complète accumulée dans inBuf_ (inBuf_[0] = opcode).
    void dispatchCommand();


    // --- Code 6301 custom ($20 LoadMemory / $22 Execute) -----------------------
    // Port du mécanisme d'Hatari (ikbd.c) : faute d'émuler un vrai HD6301, on
    // calcule le CRC32 du programme téléversé et, s'il correspond à un programme
    // connu (menus de démos / protections), on installe un handler qui reproduit
    // son protocole. Inconnu (ex. Vroom) → ignoré, comme Hatari.
    void loadMemoryByte(uint8_t v);          // octet du programme chargé via $20
    void commonBoot(uint8_t v);              // boot-stub : accumule le prog principal en ExeMode
    void customWriteDispatch(uint8_t v);     // écriture $FFFC02 → handler custom actif
    void customReadDispatch();               // event clavier/souris/VBL → handler custom actif
    void exitExeMode();                      // sortie du mode Execute (jmp $f000) → bootRom() (public)
    int  checkPressedKey() const;            // 1er scancode pressé dans scanState_, ou -1
    // Handlers de programmes connus (cf. CustomCodeDefinitions[] de Hatari).
    void froggiesWrite(uint8_t v);
    void transbeauce2Read();
    void dragonnelsWrite(uint8_t v);
    void chaosRead();
    void chaosWrite(uint8_t v);
    void audioSculptureRead(bool colorMode);
    void audioSculptureWrite(uint8_t v);

    // Sonde les manettes (joy0 lu seulement si le port 0 est attribué au joystick,
    // bit 1 de $C9) et émet $FE+joy0 / $FF+joy1 pour celles dont l'état a changé
    // (cf. Hatari IKBD_SendAutoJoysticks).
    void sendAutoInput();
    void readJoystickState(uint8_t& joy0, uint8_t& joy1) const;
    void sendAutoJoysticks(uint8_t joy0, uint8_t joy1);
    void sendAutoJoysticksMonitoring(uint8_t joy0, uint8_t joy1);

    // Réponse d'état $F6 (ROM $FCD7) : en-tête, puis le premier octet et ses
    // paramètres, puis le bourrage à zéro jusqu'à 8 octets — chaque appel admis
    // séparément dans le tampon de 21 octets, la réponse peut donc être tronquée.
    void statusReply(uint8_t first, const uint8_t* data = nullptr, int n = 0);
    // Joystick 0 en keycode ($19, ROM $FA21) et surveillance du tir ($18, $FA12).
    void sendKeycodeJoystick(uint8_t joy0);
    void stopJoystickSampling();             // quitte $17/$18 : retour à la scrutation normale
    bool outputBlocked() const { return pauseOutput_ && pauseKeep_ == 0; }
    void liftPause();

    // Mise à jour de la position absolue à partir du Δ de la trame, en mode absolu
    // seulement (ROM $F3D4 → $F439 ; Hatari la tient à jour dans tous les modes).
    void updateInternalAbsPos(int dx, int dy);
    // Paquet relatif $F8 d'après la ROM ($F54A-$F556) : émis dès que la ligne est
    // libre et que le mouvement accumulé atteint le seuil, ou sur changement de
    // bouton, ou quand le cumul dépasse ±127 ; le reste du cumul est gardé.
    void sendRelMousePacket(bool left, bool right);

    // Reporting lié à MouseAction ($07, cf. IKBD_SendOnMouseAction) : boutons
    // remontés comme scancodes (bit2), ou position absolue à l'appui/relâchement
    // (bits 0/1, en mode ABS seulement). Comparé à l'ancien état (bOldL_/bOldR_).
    void sendOnMouseAction(bool left, bool right, uint8_t events);

    // Émet le paquet $F7 « position absolue » (cf. IKBD_Cmd_ReadAbsMousePos) :
    // boutons (changements depuis la dernière interrogation) + X/Y sur 16 bits.
    void sendAbsMousePos(uint8_t buttons, int delayCycles = 0);

    // Émet le Δ souris comme pressions de flèches (cf. IKBD_SendCursorMousePacket,
    // mode $0A) : 72 haut / 80 bas / 75 gauche / 77 droite par pas de keyCodeDelta.
    void sendCursorKeys(bool left, bool right);
    // Ligne IKBD→ACIA libre (tampon vide, émetteur au repos, pas de pause) : la ROM
    // y émet ses flux « dès que la ligne est libre » (souris relative, flèches, taux 0).
    bool lineFree() const;
    void onLineIdle();
    void effectiveButtons(bool& left, bool& right) const;
    bool inLineIdle_ = false;               // garde de réentrance (transitoire, non sérialisé)
    bool inMouseEmit_ = false;              // idem pour les émetteurs souris (tests sans ordonnanceur)
    void emitButtonKeys(bool left, bool right);

    // Avance l'horloge interne BCD d'une seconde par 1e6 µs cumulés (cf.
    // IKBD_UpdateClockOnVBL) avec la propagation/retenue de la ROM HD6301.
    void initClockFromHostTime();
    void updateClock(int64_t vblMicro);

    Mfp& mfp_;
    Scheduler* sched_ = nullptr;             // pour différer la réponse de reset
    std::deque<uint8_t> rx_;                 // file IKBD → CPU (octets pas encore livrés)
    uint8_t rdr_ = 0;                        // Receive Data Register : dernier octet LIVRÉ
                                             // (relire à vide le renvoie, cf. acia.c)
    bool    rdrf_ = false;                   // RDR plein (octet livré non encore lu)
    uint8_t rxShiftData_ = 0;                // octet sorti de la file, engagé dans le SCI
    bool    rxPending_ = false;              // une livraison IKBD_RX est déjà datée
    int     responseDelayBits_ = 0;          // SCI_TX_Delay global, 1024 cycles/bit
    bool    rxWaitingDelay_ = false;         // IKBD_RX termine l'attente, pas un octet
    // Overrun récepteur (port acia.c) : le SCI livre EN CONTINU ; un octet qui
    // arrive RDR plein est PERDU (rxOverrun_ pendant, maintient l'IRQ RX). Le bit
    // OVRN du SR (ovrn_) n'est posé qu'à la lecture de RDR, et acquitté par la
    // séquence « lire SR (srRead_) puis RDR » — cf. ACIA_Read_RDR/SR_Read.
    bool    rxOverrun_ = false;              // octet(s) perdu(s) non encore signalé(s)
    bool    ovrn_ = false;                   // bit OVRN visible dans le SR
    bool    srRead_ = false;                 // SR lu depuis la dernière lecture RDR
    bool aciaClockInitialized_ = false; // Clock_Divider != 0 : conserve aux resets ACIA
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    bool    txEnableInt_ = false;            // IRQ d'émission armée : CR bits5-6 = 01 (ex. $b6, Hades Nebula)
    bool    tdre_ = true;                    // TDR libre, indépendant de TIE
    uint8_t txData_ = 0, txShiftData_ = 0;   // un octet en attente et un en vol
    int64_t nextTxClock(int64_t at) const;
    int64_t cpuSerialEventCycle_ = -1; // contexte transitoire du stop décodé, non sérialisé
    int64_t firmwareEventTime() const {
        if (cpuSerialEventCycle_ >= 0) return cpuSerialEventCycle_;
        if (!sched_) return 0;
        const int64_t due = sched_->firingDue();
        return due >= 0 ? due : sched_->liveNow();
    }
    void clockSciRx(bool bit);
    void catchUpCpuTx(int64_t at);
    void scheduleCpuTx();
    int64_t txNextBitCycle_ = 0;
    uint8_t txBitIndex_ = 0; // prochain bit TSR : 0..7 données, 8 stop
    uint8_t sciRxState_ = 0, sciRxByte_ = 0, sciRxBitsLeft_ = 0;
    void advanceTx(int64_t at); // front TX échéancé, indépendant du retard CPU
    int64_t txClockPhase_ = 0; // phase TX /64 8N1, origine du diviseur modulo1024
    bool txShifting_ = false;               // IKBD_TX termine le TSR, sinon remplit le TSR
    std::array<uint8_t, 8> inBuf_{};         // accumulation des octets d'une commande multi-octets
    int inBufLen_ = 0;                       // octets déjà reçus pour la commande en cours
    int cmdExpected_ = 0;                    // octets attendus au total (0 = aucune commande en cours)
    std::function<void(uint8_t&, uint8_t&)> joyProbe_;   // override manettes (fixture de bouclage)
    uint8_t hostJoy_[2] = {0, 0};            // état joystick hôte (port 0 / port 1), amorce la sonde

    // --- Mode souris (cf. Hatari ikbd.c KeyboardProcessor.MouseMode) -----------
    // REL = paquets relatifs $F8 (par défaut, bureau EmuTOS) ; ABS = position
    // absolue accumulée et lue à la demande via $0D ; CURSOR = Δ converti en
    // flèches clavier ($0A) ; OFF = souris désactivée ($12).
    enum MouseMode { REL, ABS, OFF, CURSOR };
    MouseMode mouseMode_ = REL;
    // Dernier mode souris CHOISI ($08/$09/$0A ; REL au boot), jamais OFF : c'est
    // lui que rapporte $88 quand la souris est coupée, la réponse devant rester
    // un paquet complet de 8 octets (cf. traitement $88).
    MouseMode lastMouseMode_ = REL;
    // Bit 7 de l'octet de mode $C9 de la ROM 6301 : souris ACTIVÉE. Distinct de
    // mouseMode_ == OFF, qui couvre aussi le port 0 rendu au joystick (bit 1 posé
    // par $14/$15/$17/$18/$19, ROM $FA47). Seul $12 l'efface, un mode souris le
    // repose ; c'est lui que rapporte $92 (ROM $FCA2). Cf. MiSTery ikbd/rom/IKBD.ASM.
    bool mouseOn_ = true;
    // Bit 1 de $C9 : port 0 attribué au joystick 0 ($14/$15/$17/$18/$19, ROM $FA47) ;
    // effacé par toute commande de mode souris ($FBAF). La souris ne rapporte que si
    // mouseOn_ && !port0Joy_ (cf. syncMouseMode) ; le joystick 0 n'est lu qu'ici.
    bool port0Joy_ = false;
    void syncMouseMode() { mouseMode_ = (mouseOn_ && !port0Joy_) ? lastMouseMode_ : OFF; }
    // Commande de mode souris ($08/$09/$0A) : ROM $FBAF — souris activée, port 0
    // rendu à la souris, joystick ramené au mode événement s'il surveillait.
    void selectMouseMode(MouseMode m);
    // $14/$15/$17/$18/$19 : port 0 rendu au joystick (ROM $FA47).
    void port0ToJoystick() {
        port0Joy_ = true;
        syncMouseMode();
        mouseDeltaX_ = mouseDeltaY_ = 0;      // le port ne compte plus d'impulsions souris
    }
    // Défauts = état après IKBD_Boot_ROM (Hatari), l'IKBD bootant sa ROM dès la
    // mise sous tension : bornes ABS_MAX_X/Y_ONRESET (320/200) et cache boutons
    // ABS_PREVBUTTONS (0x0A = rien à signaler à la 1re interrogation $0D).
    // ROM : la RAM $89-$FF est effacée au boot → position ET bornes à 0.
    uint16_t absX_ = 0, absY_ = 0;           // position absolue courante (mode ABS)
    uint16_t absMaxX_ = 0, absMaxY_ = 0;     // bornes inclusives (commande $09)
    // Registre d'événements boutons cumulés ($C2 de la ROM) : bit0 droit enfoncé,
    // bit1 droit relâché, bit2 gauche enfoncé, bit3 gauche relâché ; rendu et
    // effacé par $0D, effacé par $0E. (Champ nommé prevAbsButtons_ jusqu'en v51.)
    uint8_t  prevAbsButtons_ = 0;
    bool     prevL_ = false, prevR_ = false; // état persistant des boutons (mode ABS)

    // --- Paramètres du paquet souris relatif (cf. Hatari KeyboardProcessor.Mouse) ---
    // Seuil ($0B) : un paquet n'est émis que si |Δ| ≥ seuil EN VALEUR ABSOLUE
    // (défaut 1 → tout mouvement compte ; filtre le jitter quand un jeu le monte).
    // Échelle ($0C) : multiplie le Δ accumulé en mode ABSOLU si > 1 (défaut 0 =
    // pas d'échelle ; sans effet sur le paquet relatif, comme Hatari). yAxis_
    // ($0F/$10) : +1 = origine Y en haut (défaut), -1 = en bas — applique son
    // signe au Δy émis et à l'accumulation absolue. bOldL_/bOldR_ = dernier état
    // de bouton émis : sert à remonter un clic/relâchement SANS mouvement
    // (détection de front — boutons de passage de vitesse de Vroom).
    int  xThreshold_ = 1, yThreshold_ = 1;
    int  xScale_ = 1, yScale_ = 1;           // ROM $F106 : 1 par défaut ; pas par impulsion
    int  yAxis_ = 1;
    bool bOldL_ = false, bOldR_ = false;
    int  mouseDeltaX_ = 0, mouseDeltaY_ = 0;  // Δ souris hôte accumulé jusqu'au VBL
    bool mouseLeft_ = false, mouseRight_ = false;

    // --- MouseAction ($07) + mode curseur ($0A) (cf. KeyboardProcessor.Mouse) ---
    // mouseAction_ : bit0 = position abs reportée à l'APPUI, bit1 = au RELÂCHEMENT
    // (mode ABS), bit2 = boutons remontés comme scancodes touche (0x74 gauche /
    // 0x75 droit, |0x80 au relâché). keyCodeDeltaX_/Y_ : pas (en pixels) entre deux
    // pressions de flèche en mode CURSOR ($0A), défaut 1.
    uint8_t mouseAction_ = 0;
    int     keyCodeDeltaX_ = 1, keyCodeDeltaY_ = 1;

    // --- Horloge interne IKBD ($1B/$1C, cf. pIKBD->Clock[6]) --------------------
    // 6 octets BCD : année / mois / jour / heure / minute / seconde. Avancée d'une
    // seconde chaque fois que clockMicro_ atteint 1e6 µs (cumul au VBL). Effacée à
    // la construction (reset à froid) ; conservée au reset $80,$01 (reset à chaud).
    uint8_t clock_[6] = {0, 0, 0, 0, 0, 0};
    int64_t clockMicro_ = 0;

    // --- Mode joystick (cf. Hatari ikbd.c KeyboardProcessor.JoystickMode) -------
    // JOY_OFF = interrogation seule via $16 ; JOY_AUTO = report automatique des
    // changements d'état à chaque trame ($14) ; JOY_MONITOR = échantillonnage
    // périodique ($17). prevJoy0_/prevJoy1_ = dernier état émis.
    //   Défaut = JOY_AUTO, comme le boot ROM de l'IKBD (Hatari IKBD_Boot_ROM :
    //   KeyboardProcessor.JoystickMode = AUTOMODE_JOYSTICK). Sans entrée hôte la
    //   file reste vide (sendAutoJoysticks n'émet que sur changement) → aucun
    //   impact sur le boot EmuTOS ni les diagnostics qui interrogent via $16.
    // JOY_FIRE = surveillance du tir ($18), JOY_KEYCODE = joystick 0 en touches ($19).
    enum JoystickMode { JOY_OFF, JOY_AUTO, JOY_MONITOR, JOY_FIRE, JOY_KEYCODE };
    JoystickMode joyMode_ = JOY_AUTO;
    uint8_t prevJoy0_ = 0, prevJoy1_ = 0;
    uint32_t vblCount_ = 0;                   // garde Hatari : pas d'auto-send avant 20 VBL
    int64_t joystickMonitorPeriod_ = 0;       // cadence commune posée par $17 ; zéro = défaut 150000
    bool duringResetCriticalTime_ = false;    // bloque les sorties jusqu'à la réponse $F1

    // --- Fenêtre de reset (ROM $F010 sei … $F139 cli) ---
    // Le 6301 ne lit pas la liaison pendant son initialisation : UN octet reçu reste
    // dans son RDR et s'exécute après l'init ; un second déborde (overrun), et la
    // routine $FF40 jette alors tout. C'est ce qui explique les « quirks » d'Hatari
    // (Barbarian $08+$14, Hammerfist $12+$14, Just Buggin' $11+$13 perdus : il ne
    // reste que les défauts, souris relative + joystick 1 en événement).
    int16_t heldByte_ = -1;                   // octet retenu, -1 = aucun
    // Touches réellement tenues (hôte), indépendantes de scanState_ que le boot efface :
    // la ROM balaie la matrice au reset ($F086) et envoie le relâchement de la PREMIÈRE
    // touche trouvée avant $F1.
    uint8_t hostKeys_[128] = {};
    // Interruption OCF du HD6301 (ROM $FD9D, OCR += $3E8 : une par ms, phase posée à
    // l'init $F132 ≈ réponse $F1). En mode $18 son gestionnaire alterne un tic court
    // (128 µs) et un tic long (215 µs, balayage d'une colonne via $F1B7), +12 µs si
    // l'horloge tourne ($83 ≠ 0, posé par $1B). Cycles HD6301 comptés sur la ROM, non
    // mesurés. Ces tics retardent la boucle d'échantillonnage de $18.
    int64_t ocfOrigin_ = 0;
    bool    clockSet_ = false;
    int64_t addOcfDelay(int64_t start, int64_t cycles) const;
    bool    heldOverrun_ = false;             // second octet reçu : tout est perdu
    // Bit 5 de $CA : joysticks activés ; seul $1A l'efface, sans changer le mode
    // (après $14 puis $1A, $94 répond toujours $14). Rapporté par $9A.
    bool joysticksEnabled_ = true;
    // Joystick en keycode ($19) : RX RY TX TY VX VY (dixièmes de seconde), touche
    // tenue par axe (0 = aucune), instant de l'appui et de la dernière émission.
    uint8_t kjParam_[6] = {};
    uint8_t kjKey_[2] = {};
    int64_t kjStart_[2] = {};
    int64_t kjLast_[2] = {};
    bool    kjFire_ = false;
    // Surveillance du tir ($18) : registre $A4 (échantillons entrant par la droite)
    // et compteur $A5 (8 → 0). Délais comptés sur la ROM, cycles HD6301 à 1 MHz :
    // boucle $F13A → $F171 → $F8A2 → $F13A = 177 µs bouton enfoncé (SEC), 180 µs
    // relâché (TST) ; +15 µs au 8ᵉ échantillon (écriture du TDR) ; 57 µs si la
    // ligne est occupée, 34 µs sous pause. Hors interruptions du timer du 6301.
    uint8_t fireShift_ = 0;
    uint8_t fireCount_ = 8;
    static constexpr int64_t kFireByteCycles = 11550;  // ordre de grandeur (bornes d'état)
    // Taux $17 = 0 : un échantillon chaque fois que la ligne se libère (ROM $F778).
    bool monitorContinuous_ = false;
    // $19 : la ROM scrute le joystick à chaque tour de sa boucle principale ; NeoST
    // le fait toutes les millisecondes (une unité de ses délais vaut 100 ms).
    static constexpr int64_t kKeycodePollCycles = 8021;

    // PAUSE OUTPUT ($13) : bloque le chargement firmware → SCI au-delà des octets
    // déjà en file ; l’octet engagé finit sa trame. Levée par $11 et par les
    // commandes qui sortent par $F91B de la ROM (pas les rapports d'état).
    bool pauseOutput_ = false;
    // ROM $F9AF : $13 mémorise l'index d'écriture du tampon ; les octets déjà en
    // file avant la pause sortent quand même. pauseKeep_ = combien il en reste.
    uint32_t pauseKeep_ = 0;
    // $13 pendant $17 : décompte de la surveillance GELÉ (ROM $FEAC), reste en cycles.
    int64_t monitorFrozen_ = -1;

    // --- État du code 6301 custom ($20/$22, cf. Hatari ikbd.c) ------------------
    // Identifie le handler actif (NeoST utilise des id plutôt que des pointeurs de
    // fonction membre). CW_ = écritures $FFFC02 ; CR_ = lectures (event-driven).
    // CW_IGNORE = handler présent mais qui jette les octets (Transbeauce 2, cf.
    // IKBD_CustomCodeHandler_Transbeauce2Menu_Write) : en ExeMode les écritures
    // ne retombent pas dans le parseur standard.
    enum CustomW { CW_NONE, CW_BOOT, CW_IGNORE, CW_FROGGIES, CW_DRAGONNELS, CW_CHAOSAD, CW_AS };
    enum CustomR { CR_NONE, CR_TRANSB2, CR_CHAOSAD, CR_AS_COLOR, CR_AS_MONO };
    CustomW   customWrite_ = CW_NONE;
    CustomR   customRead_  = CR_NONE;
    bool      exeMode_     = false;          // le 6301 exécute du code custom ($22)
    int       memLoadLeft_ = 0;              // octets restant à charger ($20)
    int       memLoadTotal_ = 0;             // total chargé (pour le log/CRC)
    int       memExeNbBytes_ = 0;            // octets reçus en ExeMode (CommonBoot)
    uint32_t  memLoadCrc_  = 0xFFFFFFFF;     // CRC32 cumulé (poly IEEE 802.3)
    // État de suivi pour les handlers (équivalents des globales d'Hatari).
    uint8_t   scanState_[128] = {};          // 1 = touche pressée (ScanCodeState[])
    int       mDeltaX_ = 0, mDeltaY_ = 0;    // Δ souris accumulé sur la trame (ExeMode)
    int       mLatchDX_ = 0, mLatchDY_ = 0;  // Δ LATCHÉ au VBL (lu par les handlers custom —
                                             // stable toute la trame, comme DeltaX/Y d'Hatari)
    bool      lmb_ = false;                  // bouton souris gauche courant
    // ChaosAD (décodeur de protection) + Audio Sculpture (déchiffrement).
    bool      chaosFirst_ = true;
    int       chaosIgnore_ = 8, chaosIndex_ = 0, chaosCount_ = 0;
    bool      asMagic_ = false;
    int       asReadCount_ = 0;
};
