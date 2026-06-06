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
#include <cstdint>
#include <deque>
#include <functional>

#include "core/Scheduler.hpp"

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
    void    onResetResponse() { pushRx(0xF1); }

    // Échéance : le registre d'émission de l'ACIA s'est vidé (~1 octet série après
    // une écriture $FFFC02 sous TIE) → TDRE repasse à 1 et ré-arme l'IRQ TX. Datée
    // par write8 seulement quand l'IRQ d'émission est armée (cf. raiseIfReady).
    void    onTxEmpty();

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

    // Événement clavier venant de l'hôte (scancode ST déjà traduit).
    void keyEvent(uint8_t scancode, bool pressed);

    // Mouvement/boutons souris (cf. signe frontend : dx>0 = droite, dy>0 = bas).
    // En mode relatif, draine le Δ en paquets $F8 de 3 octets sous contrôle du
    // seuil ($0B), du signe d'axe Y ($0F/$10), et émet aussi sur changement de
    // bouton SANS mouvement. En mode absolu, accumule la position (échelle $0C).
    void mouseEvent(int dx, int dy, bool left, bool right);

    // Tic de trame (VBL), `vblMicro` = durée d'une trame en µs. Deux rôles :
    //  - avance l'horloge interne IKBD ($1B/$1C, cf. IKBD_UpdateClockOnVBL) ;
    //  - en mode joystick auto, émet spontanément un paquet $FE/$FF dès qu'un état
    //    de manette change (cf. IKBD_SendAutoJoysticks ; no-op hors JOY_AUTO).
    void onVbl(int64_t vblMicro);

private:
    void pushRx(uint8_t b);                  // empile un octet IKBD → CPU
    void raiseIfReady();                     // tire GPIP4 si une cause d'IRQ ACIA est active
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
    void exitExeMode();                      // sortie du mode Execute (jmp $f000 / reset 68000)
    int  checkPressedKey() const;            // 1er scancode pressé dans scanState_, ou -1
    // Handlers de programmes connus (cf. CustomCodeDefinitions[] de Hatari).
    void froggiesWrite(uint8_t v);
    void transbeauce2Read();
    void dragonnelsWrite(uint8_t v);
    void chaosRead();
    void chaosWrite(uint8_t v);
    void audioSculptureRead(bool colorMode);
    void audioSculptureWrite(uint8_t v);

    // Sonde les manettes et émet $FE+joy0 / $FF+joy1 pour celles dont l'état a
    // changé depuis la dernière émission (cf. Hatari IKBD_SendAutoJoysticks).
    void sendAutoJoysticks();

    // Reporting lié à MouseAction ($07, cf. IKBD_SendOnMouseAction) : boutons
    // remontés comme scancodes (bit2), ou position absolue à l'appui/relâchement
    // (bits 0/1, en mode ABS seulement). Comparé à l'ancien état (bOldL_/bOldR_).
    void sendOnMouseAction(bool left, bool right);

    // Émet le paquet $F7 « position absolue » (cf. IKBD_Cmd_ReadAbsMousePos) :
    // boutons (changements depuis la dernière interrogation) + X/Y sur 16 bits.
    void sendAbsMousePos(bool curL, bool curR);

    // Émet le Δ souris comme pressions de flèches (cf. IKBD_SendCursorMousePacket,
    // mode $0A) : 72 haut / 80 bas / 75 gauche / 77 droite par pas de keyCodeDelta.
    void sendCursorKeys(int dx, int dy, bool left, bool right);

    // Avance l'horloge interne BCD d'une seconde par 1e6 µs cumulés (cf.
    // IKBD_UpdateClockOnVBL) avec la propagation/retenue de la ROM HD6301.
    void initClockFromHostTime();
    void updateClock(int64_t vblMicro);

    Mfp& mfp_;
    Scheduler* sched_ = nullptr;             // pour différer la réponse de reset
    std::deque<uint8_t> rx_;                 // file IKBD → CPU
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    bool    txEnableInt_ = false;            // IRQ d'émission armée : CR bits5-6 = 01 (ex. $b6, Hades Nebula)
    bool    tdre_ = true;                    // Transmit Data Register Empty : 1 au repos, 0 en émission sous TIE
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
    uint16_t absX_ = 0, absY_ = 0;           // position absolue courante (mode ABS)
    uint16_t absMaxX_ = 0, absMaxY_ = 0;     // bornes inclusives (commande $09)
    uint8_t  prevAbsButtons_ = 0;            // boutons signalés à la dernière interrogation $0D
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
    int  xScale_ = 0, yScale_ = 0;
    int  yAxis_ = 1;
    bool bOldL_ = false, bOldR_ = false;

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
    enum JoystickMode { JOY_OFF, JOY_AUTO, JOY_MONITOR };
    JoystickMode joyMode_ = JOY_AUTO;
    uint8_t prevJoy0_ = 0, prevJoy1_ = 0;

    // --- État du code 6301 custom ($20/$22, cf. Hatari ikbd.c) ------------------
    // Identifie le handler actif (NeoST utilise des id plutôt que des pointeurs de
    // fonction membre). CW_ = écritures $FFFC02 ; CR_ = lectures (event-driven).
    enum CustomW { CW_NONE, CW_BOOT, CW_FROGGIES, CW_DRAGONNELS, CW_CHAOSAD, CW_AS };
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
    bool      lmb_ = false;                  // bouton souris gauche courant
    // ChaosAD (décodeur de protection) + Audio Sculpture (déchiffrement).
    bool      chaosFirst_ = true;
    int       chaosIgnore_ = 8, chaosIndex_ = 0, chaosCount_ = 0;
    bool      asMagic_ = false;
    int       asReadCount_ = 0;
};
