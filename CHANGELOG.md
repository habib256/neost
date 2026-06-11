# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. Ce qui est **implémenté et validé**. Pas encore de versions
taguées (0.1.x). Le restant est dans [`TODO.md`](TODO.md).

## Cœur & boot
- `Bus` (memory map ST) + wrapper `Cpu68k` (Musashi) + `Shifter` (vidéo).
- Lib `neost_core` sans dépendance GUI ; frontends `neost` (fenêtré) et `neost-headless`.
- Boot 68000 : overlay ROM en `$0-$7` (SSP/PC), refermé après reset. TOS auto-détecté
  (192 Ko → `$FC0000`, sinon `$E00000`).
- **Cœur CPU sélectionnable** (`--cpu musashi|moira`, `neost.cfg`, WASM `?cpu=`).
  Moira (cycle-exact, sous-module) boote EmuTOS pixel-identique et délivre les IRQ.
- **Moira en mode cycle-exact** (`MoiraConfig.h` : `MOIRA_PRECISE_TIMING = true`,
  `MOIRA_MIMIC_MUSASHI = false`) — c'est l'apport de Moira sur Musashi : l'IPL est
  échantillonné à la frontière de cycle exacte (sync avant chaque accès) au lieu de
  fin d'instruction. Corrige les **labels d'icônes EmuTOS 192** (« DISK A »/« DISQUE A »/
  « TRASH ») qui ne se traçaient pas sous Moira : une IRQ Timer C prise au mauvais cycle
  (`$FD7B22`) détournait le flux du blit texte VDI avant le redessin des labels. Bureau,
  diag ST (rapport série octet-identique à Musashi) et STX (Stunt Car Racer) inchangés.
- **Reconfiguration à chaud** : modèle / RAM / cœur / ROM changeables depuis le menu
  sans relancer (`Machine::reconfigure`, hard reset avec les nouveaux paramètres).
- **Ordonnanceur d'événements daté** (`Scheduler`, idée `cycInt.c`) : la trame est
  pilotée par les échéances (vidéo, timers MFP, FDC, son DMA…) à horloge CONTINUE.
- **Quantum CPU « sous la ligne »** (port du modèle Hatari `cycInt` + cœur cycle-exact
  Moira). Deux mécanismes :
  - **Horloge live** (`Scheduler::liveNow` = `Cycles_GetClockCounterImmediate`) : un
    timer programmé en plein bloc CPU est daté à l'instant RÉEL de l'écriture (et non
    au début du quantum), précis à la sous-instruction sous Moira.
  - **Préemption du timeslice** (`Cpu68k::endTimeslice`) : quand une écriture matérielle
    arme un événement plus proche que la cible du bloc, le CPU rend la main à la
    frontière d'instruction suivante (`m68k_end_timeslice` / drapeau Moira) et la boucle
    re-planifie. Latence d'IRQ timer ramenée de **~47 000 cyc** (un timer court armé
    juste avant un `STOP` était sauté par l'optimisation STOP) à **~130 cyc** (1 instr),
    sans changer le boot (EmuTOS/TOS pixel-identiques, histogramme d'IRQ inchangé).
    Métrique exposée par le headless (`timer IRQ retard max` / `préemptions`).
- **Wait states d'accès aux périphériques 8 bits** (PSG / MFP / ACIA) — port fidèle de
  Hatari (`psg.c`, `mfp.c`, `acia.c`). Sur le vrai 68000 chaque accès à une de ces puces
  « lentes » coûte des cycles de bus supplémentaires ; le `Bus` les injecte AVANT de router
  vers la puce, via le même mécanisme que l'alignement shifter (`Cpu68k::add{Psg,Mfp,Acia}-
  WaitCycles` → `addBusWaitCycles` → l'horloge Moira avance ; Musashi non cycle-exact → no-op) :
  - **YM2149 PSG** : **4 cyc** au PREMIER accès de l'instruction (port `PSG_WaitState` ; la
    détection « 1er accès » réutilise `instrStartClock_` figé avant chaque `execute()`, comme
    le test `PrevClock != CyclesGlobalClockCounter` de Hatari). Le surcoût movem `+4/4ᵉ accès`
    est omis (aucun logiciel réel n'accède au PSG via movem).
  - **MFP 68901** : **4 cyc** à CHAQUE accès registre (lecture ou écriture ; `M68000_WaitState(4)`).
  - **ACIA 6850** (clavier + MIDI) : **6 cyc** par accès **+ synchro E-Clock** (1 MHz = CPU/10 ;
    `10 − clock%10`, motif `[0 8 6 4 2]`, port `M68000_WaitEClock`) au seul 1ᵉʳ accès de l'instruction.
  Non-régression vérifiée : boot EmuTOS/TOS propre, **glue self-test 19/19** (géométrie bordures
  inchangée), **Spec512 stable** (diaporama : mêmes paires de flicker, mêmes magnitudes —
  une transition de diapo décalée d'1 trame, effet attendu du temps CPU réel passé à scruter
  MFP/PSG). NB : le timing absolu CPU↔vidéo se décale légèrement (le boot et les setups passent
  désormais le coût réel des accès périphériques), ce qui a demandé de **re-calibrer** la disquette
  de démo overscan gauche/droite `make_overscan_lr.py` (PAD1 20→12, rendu L+D plein PLUS propre
  qu'avant). Cf. TODO §Wait states.

## Types de machine & mémoire
- **Profils** ST / Mega ST / STE / Mega STE (`MachineType`), choisis avant le boot
  (menu GUI, WASM `?machine=`, `--machine`). Matériel optionnel **gaté au modèle** :
  son DMA `$FF8900` et joypad `$FF9200` STE+, RTC Mega+, etc.
- **ST-RAM 256 Ko → 4 Mo** configurable (`--mem`, menu, WASM `?mem=`) ; `$FF8001` posé
  en cohérence. EmuTOS détecte la `phystop` exacte par sondage.
- **Bascule machine selon la version TOS** (`Machine::adjustMachineForTos`, port Hatari
  `TOS_CheckSysConfig`) : un TOS **≤ 1.04** (TOS 1.0x ; EmuTOS 192 Ko se présente en
  « Atari ST » 1.4) ne gère ni STE ni Mega STE → bascule en **mode ST** avec avertissement,
  avant la construction. Le Mega ST (TOS 1.0x natif) est conservé. Évite l'écran noir
  d'etos192 sur MegaSTE (SCU non programmé). Pour le STE/Mega STE : EmuTOS 256 Ko ou TOS 1.62/2.06.

## Vidéo (Shifter)
- Décodage planaire basse (320×200/16c), moyenne (640×200/4c), haute (640×400 mono) →
  texture OpenGL, conversion `$0RGB` → ARGB. Haute rés forcée blanc/noir.
- Détection moniteur via **GPIP bit7** (couleur basse rés / mono haute rés).
- Base écran relisible (`$FF8201/03`, octet bas STE `$FF820D`) — les diagnostics y lisent
  leur framebuffer. Registre sync `$FF820A` relisible (défaut $02 = 50 Hz PAL).
- **Compteur d'adresse vidéo cycle-exact** (`$FF8205/07/09`, port `Video_CalculateAddress`
  Hatari : 2 cycles/octet, LineStart 56@50Hz).
- **Géométries vidéo 50/60/71 Hz** (port `video.h` : `CYCLES_PER_LINE_*`,
  `SCANLINES_PER_FRAME_*`, `LINE_START/END_CYCLE_*`). Plus de cadre PAL 313×512 figé :
  `Shifter::Geometry` dérive cycles/ligne, lignes/trame, lignes affichées et début/fin
  Display-Enable de la **résolution** (mono → 71 Hz, 501×224) et de **`$FF820A` bit1**
  (basse/moyenne → 50 Hz 313×512 ou 60 Hz 263×508), **verrouillée à `beginFrame`** (avec
  la fréquence). `Machine::runFrame` en découle (frameEnd, VBL, HBL = `cpl-4`, Timer B,
  rendu, durée VBL IKBD `lignes×cycles/8` = 20032/16700/14028 µs). Le **mono décode ses
  400 lignes** par créneaux datés (fin du hack « lignes restantes »), et le compteur
  `$FF8205/07/09` suit la fréquence verrouillée (512/56 n'étaient plus figés → correct en
  60 Hz). Validé : **50 Hz byte-identique** (EmuTOS fr + TOS 1.02, 2 cœurs ; IRQ inchangé) ;
  60 Hz / mono rendu **byte-identique** avec un **Timer C (200 Hz) remis à l'échelle** de la
  trame raccourcie (374→310→262 IRQ/100 trames) ; batteries Z des diagnostics STE/MegaSTE
  toujours Pass. *(Bascule 50/60 Hz EN COURS de trame pour les bordures → cf. TODO.)*
- **Registres STE** (gatés STE) : fine scroll `$FF8264/65`, line width `$FF820F`, base
  basse `$FF820D`, palette 4 bits/canal, relecture sync.
- **Rendu STE câblé** : `renderLine` décode en tampon d'index puis émet avec offset →
  **fine-scroll** horizontal 0-15 px (décalage gauche + groupe de 16 px lu en plus à droite,
  modèle prefetch `$FF8265`), **line-offset** `$FF820F` (stride ligne `bpl + lineWidth*2`,
  aussi dans le compteur `$FF8205/07/09`), **base-basse** `$FF820D` composée dans `videoBase`.
  Défaut (scroll 0 / line-width 0) byte-identique au boot. *(Distinction prefetch/no-prefetch
  fine + bordures → cycle-accuracy, cf. TODO.)*
- **Spectrum 512 — palette intra-ligne PIXEL-PERFECT vs Hatari** (port `spec512.c` + alignement
  bus `m68000.c`). Chaque écriture palette `$FF824x` est **datée au cycle live de Moira**
  (`recordColorWrite`) ; une trame qui réécrit la palette **> 512 fois** (image Spectrum 512,
  ≈ 48 couleurs × 200 lignes) déclenche en fin de trame (`finishFrame`) un re-rendu à **palette
  roulante** mise à jour AU CYCLE de chaque écriture → jusqu'à **512 couleurs/trame**. Quatre
  correctifs ont rendu le résultat **100 % pixel-identique à l'oracle Hatari** (0 px de diff
  sur les 4 images du diaporama, flicker éliminé) :
  - **Alignement bus 4 cyc du shifter** (port `M68000_SyncCpuBus`) : les registres couleur
    ($FF8240-5F), résolution ($FF8260) et scroll fin ($FF8264/65) ne s'accèdent que sur une
    frontière de 4 cycles → un accès non aligné gèle le CPU jusqu'à la frontière (0-3 cyc), ce
    qui **décale les accès suivants**. Désormais appliqué **EN LIVE** (`Shifter::syncCpuBus` →
    `Cpu68k::addBusWaitCycles` : le cœur Moira avance son horloge à chaque accès concerné) ; les
    écritures palette sont donc datées au cycle ALIGNÉ dès `recordColorWrite`, ce qui rend
    l'ancien recalage hors-ligne (`applyShifterBusAlignment`) **redondant (no-op)**. Sans cette
    contention, la boucle d'affichage (24× `move.l (a3)+,(ax)+` + `dbra` = **510 cyc/ligne** sous
    Moira 68000 pur) dérivait de **−2 cyc/ligne** ; avec, elle tient les 512 cyc/ligne du
    matériel. Spec512 reste **pixel-identique** (diaporama étalon byte-identique avant/après) ;
    Musashi (non cycle-exact) reste sans contention.
  - **Offset pixel↔couleur** `kSpec512AlignCyc = −23` : port du « +7 spans » de
    `Spec512_StartScanLine` (alignement pipeline shifter, `LineStartCycle + 28`) corrigé du
    décalage de datation de Moira (~4 cyc). Cale le front couleur sur le front pixel. Affiné
    de −24 à −23 (1 cyc) une fois le flicker corrigé : la correction du compteur vidéo a
    verrouillé l'état des écritures, figeant l'alignement rendu optimal à −23.
  - **Fusion octet→mot** de `recordColorWrite` : un `move.w` passe par le bus en 2 `write8`
    (gros-boutiste) ; on n'enregistre **qu'une écriture par mot** (valeur finale), comme Hatari.
  - **Datation de la LECTURE du compteur `$FF8205/07/09`** (`kVideoCounterReadOffsetCyc = −2`,
    port `Video_CalculateAddress`) — pendant côté **lecture** de `kSpec512AlignCyc`. Hatari date
    la lecture du compteur vidéo PLUS TÔT que le cycle de bus brut (`−8` « magic » + offset
    read-access de `cycles.c`) ; NeoST échantillonnait au cycle de lecture brut de Moira → **2 cyc
    trop tard**, tombant **pile sur la frontière de cellule-mot** de la quantification
    `(X−lineStart)>>1 &~1`. Les démos spec512 à **auto-synchro** (lecture `$FF8209` puis saut dans
    un nop-slide calculé) atterrissaient alors ±4 cyc **une trame sur deux** → image STATIQUE
    clignotant à 25 Hz (~1418 px/trame, ~110 paires/diaporama). `−2` recentre la lecture dans la
    cellule. Calé sur l'oracle Hatari (`TRACE_VIDEO_COLOR`) : 1ʳᵉ écriture palette ligne 64 datée
    **cyc=80 stable** (sans correction, NeoST oscillait 76↔80). **Flicker plein-diaporama : 0** ;
    STE_Test Timing (« MFP, Glue, Video ») **Pass**, rapport série byte-identique. Vérif :
    `tools/spec512_flicker_check.sh`, oracle `tools/hatari_oracle.sh`.
  - **Étalon — 100 % PIXEL-IDENTIQUE à l'oracle Hatari** : slideshow
    `disks/utils/spectrum_512_auto_diapo.st` (auto sous TOS 1.00) → les **4** images spec512
    (**BEE512** l'abeille, **sun** dégradé, **PLANET** sci-fi, **cougar** photo) diffent à
    **0 px** vs Hatari (zone active 320×200, `compare -metric AE`). Méthode : diff pixel par
    image figée (les 9552 écritures palette/trame matchent Hatari écriture-par-écriture, Δcyc
    constant absorbé par `kSpec512AlignCyc`). À l'ancien `−24` il restait 122/54/210/319 px
    (frontières décalées d'1 px). Gaté par le seuil → **zéro régression** (EmuTOS/jeux normaux
    byte-inchangés ; tos104us, Enchanted Land vérifiés). Outils : `--shot-every N PREFIX`,
    `--screenshot`, `NEOST_SPEC512_TRACE`, `NEOST_VC_OFF`/`NEOST_ALIGN_OFF` (sweep oracle),
    `NEOST_DISASM=addr,len` (headless).
- **Bordures overscan VISIBLES** (Phase 1 — basse rés couleur) : le Shifter rend désormais
  un buffer **416×276** (dimensions visibles Hatari : 48+320+48 px × 29+200+47 lignes,
  `conv_st.h` `NUM_VISIBLE_*`), l'écran actif 320×200 **centré** (offset 48,29), bordures =
  couleur registre 0. La **timeline d'événements est INCHANGÉE** (Machine itère sur
  `activeHeight()` ; faible risque) → IRQ/timers/diag byte-identiques, contenu actif
  byte-identique (décodage inchangé, juste recadré). Médium/mono sans bordure pour l'instant.
  **Fenêtre GUI « Atari ST Screen »** redimensionnée selon la résolution courante (bordures
  incluses), aspect pixel ST respecté (basse rés ×2/×2 → 832×552).
- **Timeline alignée sur VDE_On** (port `VIDEO_START_HBL_*`) : l'affichage actif commence
  désormais à la scanline **63** (50 Hz) / 34 (60/71 Hz) au lieu de la ligne 0 — la trame
  modélise les vraies bordures haut/bas, le HBL est émis à **chaque** scanline (313/263/501,
  comme le matériel), et `videoCounter`/le replay spec512 suivent l'offset. Prérequis du
  retrait de bordures (les manipulations 50/60 Hz se font DANS les bordures) et correction du
  décalage `dLine` spec512. **Non-régression vérifiée** : EmuTOS (fr/us/STE, 2 cœurs)
  byte-identique, histogramme IRQ inchangé (373/166/97 = Timer C/D/VBL), STE_Test Z et
  Arkanoid inchangés.
- **Retrait de bordures — MACHINE GLUE complète** (port fidèle de `Video_Update_Glue_State` +
  `Video_StartHBL` + section verticale, `video.c`, chemin STF) : rejouée **hors-ligne** en fin
  de trame sur les écritures freq/res datées (`replayGlue`/`updateGlueState`/`startHBL`) → la
  timeline live reste inchangée (zéro régression). Calcule par scanline `DisplayStartCycle/
  EndCycle/BorderMask/PixelShift` (port `SHIFTER_LINE`) et les bordures haut/bas
  (`nStartHBL`/`nEndHBL` + `V_OVERSCAN_*`). Tricks portés : LEFT_OFF, LEFT_PLUS_2, RIGHT_MINUS_2,
  RIGHT_OFF, STOP_MIDDLE, NO_DE, BLANK, NO_SYNC + retrait HAUT/BAS. Rendu fenêtré
  `renderGlueFrame()` : fenêtre d'affichage par ligne + **adresse vidéo ACCUMULÉE**
  (`Video_CalculateAddress`) + palette roulante (raster + spec512). **Validé par oracle Hatari**
  (`--trace video_border_v`) sur des programmes de test overscan faits-main
  (`tools/make_overscan_test.py`, bootsecteurs hand-assemblés) : **retrait HAUT** (bordure haute
  → contenu, « detect remove top ») et **retrait BAS** (« detect remove bottom ») reproduits au
  pixel comme Hatari, avec **zéro régression** (EmuTOS/diags/Arkanoid byte-identiques, titre
  Cuddly inchangé). Trace de debug `NEOST_BORDER_TRACE=1` pour le diff oracle.
- **Bordures GAUCHE/DROITE validées** + `DisplayPixelShift` au rendu : auto-test déterministe
  `neost-headless --glue-selftest` (`Shifter::glueSelfTest`, **19/19**) qui injecte des écritures
  freq/res à des cycles EXACTS et vérifie l'état contre les valeurs Hatari — LEFT_OFF (DE_start=4),
  RIGHT_OFF (DE_end=462), RIGHT_MINUS_2, STOP_MIDDLE, retraits haut/bas, écran normal. Test 68k
  end-to-end L/D (`tools/make_overscan_lr.py`, impulsion hi-res par ligne) : NeoST ET Hatari
  ouvrent les bordures latérales (oracle `video_border_h` : « detect remove left/right »). Le rendu
  applique `DisplayPixelShift` (décalage 4 px du retrait gauche ; no-op si 0 → écrans normaux
  inchangés). **Reste** (cf. TODO #7) : wakeup-state WS3 (+1 cyc, sous-pixel), med-res overscan,
  rendu des blank lines/NO_SYNC, et le pixel-perfect L/D end-to-end (lié aux wait states).
- **spec512 — boot du diaporama étalon** : `spectrum_512_auto_diapo.st` (SPSLIDE8 dans `\AUTO\`)
  s'auto-lance sous **vrai TOS** (`tos100us/fr` + `--disk`), PAS sous EmuTOS (qui ne traite pas
  l'AUTO de la même façon). Les images Spectrum 512 s'affichent **nettes de haut en bas**
  (cf. ci-dessus, dérive corrigée). Nouveau flag headless `--disk FILE` (lecteur A explicite,
  plus besoin d'écraser `disks/diskA.st`).
- **Compteur vidéo `$FF8205/07/09` : VDE_On LIVE (retrait bordure HAUTE)** — port du
  comportement `nStartHBL` de Hatari (`Video_Update_Glue_State`). Le compteur d'adresse
  vidéo n'avance qu'à partir de la 1ʳᵉ ligne **affichée** (VDE_On) ; une bascule 60 Hz
  pendant la **bordure haute** ouvre le haut de l'écran → VDE_On passe de 63 (50 Hz) à
  34, et `$FF8209` commence donc à monter dès la ligne 34. Suivi en **live** par
  `updateLiveStartHBL` (membre `liveStartHBL_`, lu par `videoCounter`), verrouillé pour
  la trame (la décision matérielle est latchée au passage de ligne). Corrige le **flicker
  « à mort » du menu fullscreen de The Cuddly Demo** : sa boucle d'auto-synchro sonde
  `$FF8209` pour se caler au faisceau ; sans VDE_On live le compteur ne montait qu'à la
  ligne 63 et la régulation `$1D10` (entrée de la boucle) divergeait (oscillation −5
  lignes/trame → géométrie fullscreen qui changeait chaque trame). Le menu STATIQUE est
  désormais **stable** et conforme aux briques d'Hatari (briques brunes, robot, échelles,
  fissures bleues). **Zéro régression** : un écran 50 Hz ordinaire ne fait aucune bascule
  freq → `liveStartHBL_` reste 63 (compteur inchangé) ; glue self-test 19/19, spec512
  pixel-identique, EmuTOS/TOS boot OK. ⚠ **Reste** (cf. TODO) : quand le robot se déplace,
  le **scrolling saute** (gros sauts d'images au lieu d'un défilement fluide) ; et le
  **scroller de la bordure BASSE** n'est pas rendu (retrait bas non modélisé dans le rendu live).
  *(Reste : scroller de la bordure BASSE du menu non rendu — cf. TODO, retrait bas live.)*

## Interruptions (MFP 68901)
- IER/IPR/IMR/ISR + registre vecteur, modes auto et software-EOI.
- **Chaîne IRQ fine du 68901** (port `mfp.c` : `MFP_UpdateIRQ` / `MFP_InputOnChannel` /
  `MFP_ProcessIACK`) — trois latences réelles du circuit désormais modélisées :
  - **Délai IRQ→CPU de 4 cycles** (`MFP_IRQ_DELAY_TO_CPU`, mfp.c:374) : le signal IRQ
    levé au cycle T n'est visible du 68000 qu'à T+4. Signal interne daté (`irq_`/
    `irqTime_`) + événement `Scheduler::MFP_IRQ` armé à T+4 qui recalcule l'IPL en mode
    **COMMIT** (`Cpu68k::updateIplNow` → `NeostMoira::commitIpl` : broche + `reg.ipl` +
    CHECK_IRQ) — à une frontière d'instruction, délai écoulé, l'exception part AVANT
    l'instruction suivante comme `MFP_ProcessIRQ`. Sans le commit, le délai s'ajoutait
    au pipeline IPL fidèle de Moira (poll à l'instruction suivante) → ~1 instruction de
    latence en trop, et le test « **T4 Video Counter** » des diagnostics échouait
    (régression détectée puis re-validée : T Pass sur les 3 batteries, 2 cœurs).
  - **Chronologie multi-IRQ** (`Pending_Time[]`, mfp.c:963-1120) : chaque requête
    pendante est datée (`pendingTime_`) ; les requêtes d'une même fenêtre sont servies
    dans l'ordre d'ARRIVÉE (gate `pendingTimeMin_`), pas seulement par priorité. Les
    timers servis en retard par l'ordonnanceur sont **antidatés** de leur échéance
    réelle (`raiseAt(due)`, port `Interrupt_Delayed_Cycles`) → le délai de 4 cycles
    court depuis l'expiration matérielle, pas depuis le dispatch de l'émulateur.
  - **Ré-évaluation du vecteur à l'IACK** (`MFP_ProcessIACK`, mfp.c:812-854) : `iack()`
    recalcule le signal au cycle de lecture du vecteur (sous Moira, cycle-exact, ~12
    cycles après le début de l'exception) — une IRQ plus prioritaire survenue
    entre-temps remplace le vecteur ; plus rien de pendant → -1 (spurious). Une requête
    sur un canal désactivé EFFACE désormais son bit pendant (port `MFP_InputOnChannel`).
  Les écritures IER/IPR/IMR/ISR/VR ré-évaluent le signal au cycle d'écriture (port
  `MFP_UpdateIRQ_All`). Validation : batteries Z des 3 diagnostics (ST/STE/MegaSTE)
  Pass sur Moira ET Musashi, timer IRQ retard max 130-140 cyc, boot/étalons inchangés.
  NB : l'« offset fin d'instruction » (`CycInt_AddRelativeInterruptWithOffset`) est déjà
  couvert : en mode CE Hatari date à `clock + currcycle` (cycles.c:315-321) = exactement
  `Scheduler::liveNow()` sous Moira (sous-instruction).
- **Reset matériel du MFP** (`Mfp::reset`, port de `MFP_Reset` mfp.c:519-569, appelé par
  `Machine::reset/hardReset` AVANT `cpu.reset()` comme `reset.c:74`) : remet à zéro GPIP/AER/DDR,
  IER/IPR/IMR/ISR, VR, les timers (mode/recharge/compteurs/backing store) et annule les échéances
  Scheduler → plus d'**IRQ Timer A / GPIP7 fantôme** survivant à un reset à chaud (Ctrl+reset)
  qui pouvait faire s'emballer/parasiter une musique chip. PRÉSERVE le moniteur, le flag son DMA
  et le bouclage (propriétés posées avant le reset) ; les lignes d'entrée des autres puces sont
  reforcées à la lecture du GPIP.
- **`M68K_EMULATE_INT_ACK`** activé dans Musashi (sans ça, IRQ auto-vectorisées, vecteurs
  MFP inutilisés).
- **Timer C 200 Hz** (tic système), **Timer B event-count** (Display Enable, lignes
  visibles ; nécessaire à TOS 1.x), **Timer B mode délai** (TBCR 1-7, daté sur
  l'ordonnanceur — corrige « T0 MFP timer »). VBL niveau 4 auto-vectorisé, latché.
- **Position du tic Timer B dérivée du Display-Enable** (port `Video_TimerB_GetDefaultPos`) :
  compte les **fins** de ligne (`DE_end+24`) ou les **débuts** (`DE_start+24`) selon l'AER
  bit3 du MFP (jeux/démos type *Seven Gates of Jambala*), positions selon résolution (71 Hz)
  et fréquence (50 Hz = 400, 60 Hz = 396) — au lieu du cycle 400 figé. Défaut 50 Hz/fin
  inchangé (boot pixel-identique).
- **VBL niveau 4 tiré en fin de trame** (port `Video_InterruptHandler_VBL`) : l'IRQ VBL est
  générée `VBL_VIDEO_CYCLE_OFFSET` cycles après la dernière ligne (64 STF / 68 STE = sommet
  de la trame, début du vblank), et non plus à la ligne 201 (~112 lignes / 57000 cyc trop
  tôt). Le handler VBL du jeu (base écran, palette, sprites) s'applique donc à la trame qui
  va s'afficher, comme sur le matériel. Boot EmuTOS/TOS atteint son écran normalement.
- **Timers A/C/D mode délai** datés par le MFP (`Scheduler`). Backing-store timer/USART.
- **Replanification périodique anti-dérive** (port `PendingCyclesOver`) : un timer en mode
  délai se relance ancré sur l'**échéance servie** (`Scheduler::firingDue`) + période, et non
  sur l'horloge courante → le dépassement dû à la latence d'IRQ est absorbé, pas accumulé.
  Sans dérive sur les longues durées (timers musique haute fréquence) ; boot et histogramme
  d'IRQ inchangés (correction sous-trame).
- **Lecture du compteur vivant** des registres de données Timer A/B/C/D (`$FFFA1F/21/23/25`,
  port `MFP_ReadTimer_AB/CD`) : en mode délai actif on reconstruit le compteur décompté
  (`ceil(cycles_MFP_restants / prescaler)` via `Scheduler::cyclesUntil`) au lieu de renvoyer
  la valeur de recharge — indispensable aux boucles de délai qui pollent le compteur. Test
  *Timing* (STE Field Service Diag) Pass sur les deux cœurs, boot byte-identique.
- **Lecture GPIP** honore le registre de direction (DDR) et le latch CPU.
- **IRQ GPIP front-déclenchées réévaluées à l'écriture AER** (`$FFFA03`, port
  `MFP_GPIP_Update_Interrupt`) : état = GPIP ^ AER ; basculer le front actif (AER) alors
  qu'une ligne d'entrée est déjà au niveau correspondant lève le canal — même sans
  transition de la ligne (cas réel des démos « M »/« Realtime » : `bset/bclr #0,$FFFA03`).
  Gaté IER comme `MFP_InputOnChannel`. Boot + histogramme d'IRQ inchangés (TOS n'arme pas
  de front actif au boot), 2 cœurs.
- Chaînage des lignes : **I3** blitter, **I4** ACIA (clavier+MIDI en OU câblé), **I5** FDC,
  **I7** son DMA XSINT (moniteur XOR XSINT).

## Phase 2 du plan d'écarts Hatari (cf. docs/HATARI_GAP_PLAN.md §8)
Validée par campagne complète (15/15 tests IKBD, 3 boots pixel-identiques, étalons,
diagnostics ST/MegaSTE, test fonctionnel .wd1772, axes Vroom) :
- **ACIA MIDI alignée sur l'ACIA clavier** (`MIDI_UpdateIRQ`/`ACIA_MasterReset`) :
  cause d'IRQ TX (TIE+TDRE), RDR persistant, master reset SANS purge de la file —
  **le test « M MIDI » du diagnostic ST passe désormais** (échouait avant).
- **IKBD : délais de réponse initiaux** (fixes, déterministes — $FD≈8000, $FC/$F6≈7200,
  $F7=10800 cyc, cf. `IKBD_Cmd_Return_Byte_Delay`) et **buffer de sortie borné 1024**
  avec rejet PAQUET-ENTIER (`IKBD_OutputBuffer_CheckFreeCount` — flood Downfall/Fokker,
  mémoire bornée).
- **Son DMA STE : compteur de trame $FF8909/0B/0D calculé sur l'horloge d'émulation**
  (`liveCounterAddr`, déterministe, vivant en headless — effets calés sur la position :
  Mental Hangover, Power Up Plus) + **filtre anti-repliement** (1,2,1)/4 par échantillon
  DMA quand la fréquence DMA dépasse la sortie hôte (port `DmaSnd_LowPassFilter*`).
- **STX : WRITE TRACK** (overlay piste brut + secteurs interprétés du flux MFM, relus
  en priorité — Hatari laisse pDataRead NULL, écart documenté) et **persistance des
  écritures dans `<image>.wd1772`** (format Hatari, interop : SECT/TRCK), sauvegardé à
  l'éjection/remplacement/destruction, rechargé à l'insertion. Les sauvegardes de jeux
  STX survivent au reboot.
- **Vidéo : ligne BLANK sans NO_DE** (= `BORDERMASK_BLANK_LINE`) rendue couleur 0 avec
  compteur vidéo avancé (octets fetchés) ; les lignes vides injectées par NO_SYNC/
  SYNC_HIGH repoussent le bas de l'affichage (`glueEndHBL_ + glueBlankLines_`).
  Le VBL `frameStart+offset` a été analysé NON-divergent (mêmes instants absolus
  que Hatari). Reste de phase : `bSteBorderFlag` 336 px, `LEFT_OFF_2_STE`,
  Timer B/AER mid-frame (cf. plan §8).

## Phase 1 du plan d'écarts Hatari (cf. docs/HATARI_GAP_PLAN.md)
Sept correctifs « S » à fort impact, chaque item vérifié contre l'oracle des deux côtés
et validé par campagne complète (tests unitaires dédiés, 3 boots pixel-identiques,
étalons, diagnostics ST/MegaSTE inchangés, axes Vroom, cœurs Musashi+Moira) :
- **Palette masquée par machine** (`Video_ColorReg_WriteWord`) : écriture $FF8240-5F
  masquée `&0x777` sur ST/Mega ST (bit3 non câblé, 512 couleurs), `&0xFFF` sur STE ;
  relecture masquée → la détection STF/STE par write/read de $FF8240 fonctionne.
- **MFP : écrire TxDR d'un timer EN MARCHE** ne touche plus le compteur vivant et ne
  replanifie plus l'échéance (effet au rebouclage suivant, cf. `MFP_TimerxData_WriteByte`) ;
  timer arrêté (TxCR=0) → compteur = valeur. Corrige les sauts de phase des replays
  changeant la période à la volée (digidrums).
- **MFP : VR bit3 1→0 (software→auto EOI) efface ISRA/ISRB** (`MFP_VectorReg_WriteByte`) —
  sinon des in-service restés posés bloquaient les IRQ basses pour toujours.
- **Bus : octets « void » du shifter par machine** : $FF820B/$FF8262-63/$FF8266-7F
  lisent 0x00 sur STE (`IoMem_VoidRead_00`), 0xFF sur ST ; $FF820D/$FF820F lisent 0xFF
  sur ST (registres STE inexistants). Détection vidéo STE des TOS/diagnostics.
- **Bus : fenêtre ROM décodée complète** (192 Ko à $FC0000 / 1 Mo à $E00000, cf.
  `memory.c` map_banks) : lecture au-delà de l'image → 0x00 SANS bus error ; l'autre
  fenêtre ROM faute. + **SCU MegaSTE : seuls les octets IMPAIRS répondent** (les pairs
  $FF8E02-0E fautent, comme `ioMem.c`).
- **YM2149 : test de rechargement du bruit à 250 kHz** (hors du garde 125 kHz, période
  0 admise — cf. `YM2149_DoSamples_250`) : le bruit le plus aigu (R6=0) retrouve son
  octave (recharge LFSR à 250 kHz au lieu de 125).
- **ACIA clavier : master reset 6850** (CR bits 0-1 = 11 → SR=$02, RDRF/IRQ effacés,
  file IKBD NON purgée — fix Froggies/Overdrive) et **liaison non initialisée** (octets
  IKBD jetés tant que le CR n'a pas reçu un diviseur valide, cf. `Clock_Divider`).

## Clavier, souris, joystick (ACIA 6850 / IKBD HD6301)
- ACIA clavier + file de scancodes ; mapping GLFW → scancodes ST. Ligne **GPIP4** câblée
  sur RDRF de l'ACIA. **Réponse de reset IKBD différée** (`$F1` ~502000 cyc après `$80,$01`).
- **Analyseur de commandes multi-octets** (table de longueurs + buffer d'accumulation).
- Souris **relative** (paquets `$F8`|boutons + Δx/Δy) **et absolue** (`$09`/`$0D`/`$0E`).
  Port fidèle de `IKBD_SendRelMousePacket` : **seuil d'émission** (`$0B`), **échelle** absolue
  (`$0C`), **signe d'axe Y** (`$0F`/`$10`), drain des gros Δ en plusieurs paquets, et émission
  **sur changement de bouton SANS mouvement** (détection de front — boutons de jeu type Vroom).
  Défauts de reset (REL, seuils 1, axe Y haut) remis sur `$80,$01`.
- **MouseAction `$07`** (`IKBD_SendOnMouseAction`) : boutons remontés comme scancodes touche
  (`$74`/`$75`, bit2) et/ou position absolue reportée à l'appui/relâchement (bits 0/1) ;
  **mode curseur-clavier `$0A`** (`IKBD_SendCursorMousePacket`) : Δ souris converti en flèches
  (72/80/75/77) ; **DisableMouse `$12`** (mode OFF).
- **Horloge interne IKBD `$1B`/`$1C`** (`IKBD_UpdateClockOnVBL`) : 6 octets BCD avancés d'une
  seconde par trame cumulée, propagation/retenue + bissextile fidèles à la ROM HD6301.
- **Joystick** : auto-report (`$14`), stop (`$15`), monitoring (`$17`), durée de feu (`$18`) ;
  interrogation `$16` → `$FD,joy0,joy1` (les DEUX ports bruts, sans couper le port 0 — comme
  `IKBD_Cmd_ReturnJoystick`).
- **Livraison série IKBD → ACIA cadencée** (`Scheduler::IKBD_RX`, ~10240 cyc/octet = 10 bits
  à 7812,5 bauds, la cadence du SCI d'Hatari) : un octet de la file ne lève RDRF/IRQ qu'à
  son tour, le suivant ~1,28 ms après sa lecture. **Corrige les axes souris « tournés de
  90° » de _Vroom_** (TODO historique) : le jeu identifie les octets du paquet `$F8,Δx,Δy`
  à leur cadence d'arrivée, pas à l'en-tête — la livraison instantanée des 3 octets lui
  faisait prendre Δy pour Δx (haut/bas braquait, gauche/droite accélérait). Diagnostiqué en
  comparant le flux consommé (`NEOST_DEBUG_ACIA`) au comportement d'`IKBD_Send_Byte_Delay`/
  SCI ; validé en course headless (`--mouse-at`) : gauche/droite braquent, haut/bas non.
  Boots EmuTOS/TOS 1.04/2.06 + drag GEM pixel-identiques, diag ST inchangé.
- **RDR persistant** : relire `$FFFC02` sans nouvel octet renvoie le DERNIER octet reçu
  (cf. `acia.c:ACIA_Read_RDR`), plus `$00`.
- **Duplication feu joystick ↔ boutons souris** (`IKBD_DuplicateMouseFireButtons`) : sur le
  vrai IKBD ce sont les MÊMES lignes. Souris coupée → boutons souris émis comme feux
  joystick (`$FE`/`$FF` bit7) ; souris active → le feu du joystick 1 est RETIRÉ du paquet
  joystick et remonte comme **bouton droit** dans le paquet souris (Big Run, et le bouton
  de feu de _Magic Pockets_ qui restait muet).
- **`$14` coupe la souris** (comme `IKBD_Cmd_ReturnJoystickAuto`), avec les quirks de la
  **fenêtre de reset** portés d'Hatari : `$08`+`$14` (Barbarian), `$12`+`$14` (Hammerfist)
  et `$12`+`$1A` (`IKBD_CheckResetDisableBug`) ré-activent souris ET joystick ensemble
  (`bothMouseAndJoy`) — le port 0 reste alors branché en souris relative.
- **PAUSE OUTPUT `$13` / RESUME `$11`** : gèle la livraison IKBD → ACIA jusqu'à la
  prochaine commande valide ; ignoré pendant la fenêtre de reset (loader de Just Bugging).
- **Commandes de rapport `$87-$9A`** (`IKBD_Cmd_Report*`) : réponse `$F6` + 7 octets d'état
  (mode souris, seuils, échelle, axe Y, disponibilité souris/joystick…).
- **Position absolue interne mise à jour dans TOUS les modes souris**
  (`IKBD_UpdateInternalMousePosition`) + bornes/position remises aux défauts sur reset.
- Frontend : **relâchements de touche toujours transmis** au ST si l'appui l'a été (une
  touche n'est plus « collée » quand ImGui prend le focus entre make et break) ; mapping
  clavier complété façon Hatari (`sdl/keymap.c`) : **pavé numérique**, Help (Impr. écran),
  Undo (Fin), `(`/`)` (PgUp/PgDn).
- Debug : `NEOST_DEBUG_IKBD=1` trace les commandes reçues par l'IKBD ;
  `NEOST_DEBUG_ACIA=1` trace chaque lecture du data register (valeur, file, cycle).
  Headless : `--mouse-at N "SCRIPT"` (script souris daté L/R/U/D/1/2/.) et
  `--joy-script N "SCRIPT"` (état joystick par trame U/D/L/R/F/.) pour piloter des menus
  de jeux ; `stScancode` étendu (flèches `<>[]`, Esc `=`, F1-F5 `!@#$%`…).

## Disquette (FDC WD1772 + DMA)
- **Modèle ROTATIONNEL daté** (port `extern/hatari/src/fdc.c`, chemin « _ST ») remplaçant
  l'ancien « DMA instantané ». Machine à états par commande (Restore/Seek/Step, Read/Write
  Sector, Read Address, Read/Write Track, Force Interrupt, Motor Stop) avançée par
  `Scheduler::FDC` ; chaque phase renvoie un nombre de cycles FDC (≈ cycle CPU à ~8 MHz).
  Modélise : **impulsions d'index** (300 tr/min, 1 tour = 1 604 249 cyc ≈ 200 ms),
  **spin-up** (6 tours), **chargement de tête** (15 ms), **latence rotationnelle** jusqu'au
  champ ID du secteur cherché (`FDC_NextSectorID_ST` : gaps GAP1/2/3, secteur brut 614 o),
  **transfert DMA octet par octet** (FIFO 16 o, débit MFM 256 cyc/octet), **INTRQ datée**,
  **arrêt moteur** après 9 tours d'inactivité. Validé : le diagnostic Atari « Floppy → Test
  Speed » mesure ~200 ms/tour (300 RPM) ; **débloque Arkanoid** (le gel `$31736` exigeait le
  spin-up + le débit MFM réels — cf. [[arkanoid-freeze-investigation]], comme Hatari sans
  `--fastfdc`). Déterminisme headless préservé (PRNG reproductible pour la phase d'index).
- Accès indirect via DMA (`$FF8600`). Sélection face/lecteur via PSG port A. INTRQ → **GPIP5**
  (+ canal 7). Statut type I avec bits TR00/INDEX/WPRT en temps réel ; remplacement de
  commande pendant prepare+spin-up ; Force Interrupt (`$Dx`) immédiat/sur-index.
- **Adresse DMA relisible** (`$FF8609/0B/0D`, incrémente par blocs de 16 o pendant le transfert
  — corrige « DMA count error »). FIFO/compteur de secteurs, bit erreur DMA. **Lecteur B**
  (`--diskb`, PSG port A bits 1/2).
- **FDC rapide** (équivalent `hatari --fastfdc`) : divise les délais de **commande/transfert**
  par 10 → accès disque ~10× plus courts (ex. Arkanoid charge son `.PRG` à la trame ~300 au
  lieu de ~1000). La **rotation** (index, spin-up, arrêt moteur) reste au rythme réel, comme
  Hatari — d'où une bonne compat (Arkanoid reste jouable) ; ⚠ peut néanmoins casser les loaders
  maison très sensibles au timing. Réglable partout : **`--fastfdc`** (headless), **menu GUI**
  « Machine → FDC rapide » (effet immédiat, sans reset), **`fastfdc=` dans `neost.cfg`**
  (mémorisé) ; API `Fdc::setFastFdc()`.
- **Write-protect auto-détecté** depuis les droits du fichier ; **changement de média**
  (Mediach via bascule WPRT à l'éjection/insertion à chaud).
- Formats : `.st` (brut), `.msa` (décompression RLE), `.dim` (en-tête 32 o retiré, port
  `floppies/dim.c` : ID 'BB', non compressé). Détection par CONTENU (indépendante de
  l'extension). Écritures recopiées dans le `.st` ; `.msa`/`.dim` protégées en écriture.
- **Images STX (Pasti)** — port d'`extern/hatari/src/floppies/stx.c` (`StxImage` +
  chemin `_STX` du FDC). Parse le conteneur Pasti (en-tête RSY, blocs piste/secteur) en
  structures bas niveau avec **champs ID RÉELS** (piste/face/secteur/taille/CRC,
  éventuellement NON standard), **statut FDC par secteur** (RNF, **erreur CRC**
  volontaire, record-type), **bits fuzzy** (données différentes à chaque lecture) et
  **timing variable** (vitesse par bloc de 16 o). Le FDC dispatche vers les variantes
  `nextSectorIDStx`/`readSectorStx`/`readAddressStx`/`readTrackStx`/`writeSectorStx`
  (écriture en overlay mémoire). Position angulaire via `BitPosition` (1 bit = 32 cyc),
  rotation par piste (`cyclesPerRev` dérivé de la longueur réelle). Débloque les jeux
  **PROTÉGÉS** : ✅ **Dungeon Master** (fuzzy bits), **Stunt Car Racer**, **Tower of
  Babel**, Golden Axe, Chessmaster… (séquence de lecture identique à Hatari, vérifiée à
  l'oracle). Quelques protections spécifiques restent à affiner (cf. TODO).
- **Erreurs d'adresse 68000** (`M68K_EMULATE_ADDRESS_ERROR`, exception 3 sur accès
  mot/long impair) activées sous Musashi (Moira les avait déjà) — requises par les
  anti-debug de certaines protections. Boot EmuTOS byte-identique, batterie Z des
  diagnostics inchangée.

## Audio
- **Son haché et ralenti (GUI) — RÉSOLU** par la refonte de la cadence de la boucle
  principale (`main.cpp`). Trois causes superposées, mesurées au compteur d'underruns :
  (1) le bridage FIXE à 20 ms ne suivait pas la durée émulée des trames — un écran
  60 Hz (263×508 cyc ≈ 16,66 ms, le défaut d'EmuTOS US) tournait 17 % trop lent ;
  (2) le **vsync** (`glfwSwapInterval(1)`) faisait bloquer `swapBuffers` jusqu'au
  vblank suivant → battements à ~30-37 fps sur écran 60 Hz ; (3) même corrigée, une
  itération GUI coûte ~22-25 ms réels (ImGui + GL + granularité de sommeil macOS) →
  à 1 trame émulée par itération, plafond ~40 trames/s = déficit permanent de 20 % :
  temps émulé RALENTI (tempo des musiques cadencé par les IRQ émulées) et anneau
  audio du modèle « push » affamé (son HACHÉ, bruits lecteur compris — même anneau).
  Correctif : **boucle de RATTRAPAGE** (pattern émulateur classique) — chaque
  itération GUI exécute autant de trames émulées que le temps réel l'exige (`emuNext`
  repoussé de la durée ÉMULÉE de chaque trame, géométrie 50/60/71 Hz ; garde-fou
  4 trames après une pause), l'affichage saute les trames intermédiaires. Vsync off
  (le sommeil cadence), sommeil plafonné à 20 ms (GUI réactif). **Mesuré : 0 underrun
  en 20 s** (contre ~6/s avant). Diagnostic pérenne : compteur d'underruns atomique +
  message stderr avec la cadence observée (`[Audio] underrun anneau … trames/s`) —
  un son haché s'auto-explique désormais dans la console.
- **YM2149** : 3 voies carrées + bruit, enveloppe (R11-13, formes via Continue/Attack/
  Alternate/Hold), vitesse d'enveloppe corrigée (diviseur de pas). Backend miniaudio (CoreAudio).
  **`YM2149::reset()`** remet tous les registres à 0 (volumes 0 = SILENCE) et est appelé par
  `Machine::reset()/hardReset()` → le son ne PERSISTE plus après un reset (soft/hard), qui
  laissait sinon une tonalité bipée. Port A (R14) remis à `0xFF` au reset (lignes I/O actives
  bas toutes inactives, cf. `psg.c:223`) → plus de sélection lecteur/face parasite au boot.
- **DAC non linéaire + porte ton/bruit + filtres de sortie** (port fidèle de Hatari `sound.c`,
  suite à l'analyse comparative `docs/SOUND_HATARI_DIFF.md`). Trois corrections dans `synthesize` :
  - **Table DAC 32×32×32 modélisée** (`YM2149_BuildModelVolumeTable`, sound.c:615-678) en
    remplacement de la somme linéaire des 3 voies ÷ 3 : le DAC du YM2149 débite dans une
    résistance de charge commune, la sortie suit la loi non linéaire (2^-¼)^(n-31) → empiler des
    voies n'additionne PAS les amplitudes (3 voies pleines ≈ ×1, pas ×3), et un volume « moyen »
    (index 8) est ~23 dB sous le plein volume. Index `(idxC<<10)|(idxB<<5)|idxA`, table normalisée
    construite une fois. Remplace l'ancienne table 1D `ymout1c5bit`.
  - **Combinaison ton+bruit par ET LOGIQUE** (porte) au lieu d'une moyenne arithmétique
    (sound.c:1098-1111) : la porteuse hache le bruit → bruitages ton+bruit (explosions, moteurs)
    rendus correctement. Voie désactivée ⇒ terme toujours haut.
  - **Filtres de sortie analogiques du ST** : passe-haut sous-sonique ~15 Hz anti-DC
    (`Subsonic_IIR_HPF`, sound.c:382-394 — indispensable car la table DAC est unipolaire :
    couplage AC du vrai HW) + passe-bas PWM par défaut de Hatari (`PWMaliasFilter`, sound.c:479-492,
    réduit l'aliasing des aiguës). État des filtres remis à zéro au reset.
  - Niveau de sortie aligné sur Hatari (`YM_OUTPUT_LEVEL=0x7fff` → float) : ~6 dB sous l'ancien
    modèle linéaire mais c'est le vrai niveau du DAC ST ; jamais clampé (3 voies pleines crêtent
    à ±0.5, transitoire d'attaque ≤1.0).
- **Demi-amplitude YM sur STE/Mega STE** (`YM2149::setOutputScale`, port de `YM_OUTPUT_LEVEL>>1`,
  sound.c:780-784) : le mixeur STE met le YM à ½ amplitude pour laisser la marge au son DMA →
  plus d'écrêtage dur quand YM + DMA jouent fort ensemble (YM 3 voies ≈ ±0.25 + DMA ≤ ±0.7 < 1.0).
  Posée par `Machine` selon le type machine (ST/Mega ST = pleine amplitude, pas de DMA), et suivie
  par la bascule auto STE→ST des TOS ≤ 1.04 (`adjustMachineForTos`). Non remise à zéro par reset.
- **Modèle « push » horodaté + anneau émulation→audio** (Phase C — le son est désormais GÉNÉRÉ sur
  le thread d'émulation, à l'horloge CPU, et le thread audio ne fait plus que recopier) :
  - **Écritures PSG horodatées** : `YM2149::write8` enregistre chaque écriture de registre sonore
    (0-13) avec son cycle CPU dans la trame (horloge câblée par le frontend, `Machine::frameRelCycle`).
  - **Synthèse par rejeu** (`YM2149::synthesizeFrame`) : rejoue ces écritures à leur position exacte
    (cycle → échantillon), en synthétisant par segments → capture les modulations SOUS-BUFFER
    (digidrums, sync-buzzer, arpèges très rapides) que l'ancien modèle « pull » (une lecture des
    registres par buffer audio) ratait complètement (testé : modulation rms 0.31 vs 0.00 en legacy).
  - **Anneau SPSC lock-free** (`SampleRing`, 32768 ech.) émulation→audio : `Audio::produceFrame` (après
    `runFrame`) génère PSG+DMA+LMC+lecteur, clampe et empile ; `Audio::render` (callback miniaudio) ne
    fait que drainer (plus aucune course sur l'état de synthèse).
  - **Amorçage + asservissement** (corrige « la musique démarre 30 s trop tard, seuls les drums au
    début ») : le consommateur attend un coussin de ~85 ms avant de jouer (sinon on draine un anneau
    quasi-vide en underrun permanent où seules les transitoires passent) et **ré-amorce après tout
    underrun** ; le producteur calibre le nombre d'échantillons par report fractionnaire + un
    asservissement proportionnel (|adj| ≤ 8 ech., < 0,8 % de hauteur) qui remplit vite à l'amorçage
    et recale ensuite. Latence régime ~80 ms, stable. **Validé à l'oreille sur _Magic Pocket_.**
  - Chemins **headless** (pas d'audio) et **WASM** (`synthesize` direct) inchangés : le modèle push
    n'est armé que si le frontend pose l'horloge (`setCycleClock`). _Reste (refinements) : synthèse YM
    interne 250 kHz + rééchantillonnage, FIFO/anti-repliement DMA (cf. `docs/SOUND_HATARI_DIFF.md`)._
- **Fidélité I/O du PSG** (port de `psg.c:252-358`) : sélecteur de registre stocké sur **8 bits non
  masqués** ; registre **≥ 16 → écriture ignorée, lecture 0xFF** (le YM2149 n'a que 16 registres ;
  compat *European Demo* qui « désactive » le PSG ainsi). **Masquage à l'écriture** des bits inutilisés :
  tons grossiers A/B/C (R1/3/5) + forme d'enveloppe (R13) sur 4 bits ; ampli A/B/C (R8/9/10) + bruit (R6)
  sur 5 bits → la relecture renvoie la valeur masquée, comme le matériel. `$FF8802` reste **relisible**
  (choix délibéré pour les RMW des cartouches de diagnostic). Revalidé : batterie `Z` du diagnostic ST
  (RAM/ROM/Clavier/**Audio sweep A·B·C**/Timing) **byte-identique sur Musashi ET Moira**.
- **Son DMA STE** (`DmaSound`, `$FF8900-$FF8925`) : échantillons 8 bits signés en RAM
  (6.25/12.5/25/50 kHz, mono/stéréo, play/repeat, compteur d'adresse), mixé au YM2149.
  **Ligne XSINT** datée (`Scheduler::DMASND`) câblée aux DEUX entrées MFP — GPIP7 ET TAI
  du Timer A — comme `DmaSnd_Update_XSINT_Line`. Timer A **event-count** (port
  `MFP_TimerA_Set_Line_Input`) : compte sur le front sélectionné par l'AER GPIP4 (défaut
  bit4=0 → fins de trame), recharge à 1 (data reg 0 = 256), IRQ canal 13 — double-buffering
  streamé STE.
- **Fidélité DMA STE** (port de `dmaSnd.c`, suite à `docs/SOUND_HATARI_DIFF.md`) :
  - **Cas `start==end`** (`DmaSnd_StartNewFrame`, dmaSnd.c:471-480) : trame vide + repeat off →
    arrêt SANS lever XSINT (`startNewFrame()`), corrige le GPIP7 figé HAUT (détection moniteur
    faussée, demos start==end type Amberstar cracktro).
  - **Adresse de trame à l'arrêt** = `startAddr`, pas la dernière position lue (`DmaSnd_GetFrameCount`,
    dmaSnd.c:756-759). _L'avance live cycle-exacte du compteur reste Phase C._
  - **Reset à chaud vs à froid** : le LMC1992/Microwire n'a pas de broche de reset → ses volumes/
    mixage PERSISTENT au reset à chaud (`reset(bool cold)`, propagé par `Machine::reset/hardReset`).
- **LMC1992 / Microwire** (`$FF8922/24`) : décodage commande série 11 bits, volume
  maître + G/D (gain), basses/aigus ±12 dB (filtres RBJ ; codes de tonalité **13-15 saturés à
  +12 dB** comme la table `LMC1992_Bass_Treble_Table` de Hatari, au lieu de +14/+16/+18). **Shift série** `$FF8922`
  (16 décalages de 8 cyc, `Scheduler::MICROWIRE` — les diags qui pollent jusqu'à 0 OK).
  **Registre de mixage (reg 0) désormais appliqué** dans `mix()` : mixing==1 → YM2149+DMA,
  0/2/3 → DMA SEUL qui écrase le YM (réf. `dmaSnd.c:555-568`), uniquement pendant une trame DMA
  (DMA à l'arrêt → le YM passe intact). EmuTOS STE programme mixing=1 au boot (vérifié) → YM
  audible par défaut, la correction ne mute le YM que si un programme route explicitement DMA-seul.
- **Bruits mécaniques du lecteur** (immersion, pas du matériel — repris de STeem SSE) :
  le cœur émet des événements `FdcSound` (moteur/pas/seek/index) via un sink ; frontends
  GUI (`DriveSound`, miniaudio) et WASM (Web Audio). WAV embarqués dans `roms/drivesound/`.
- **Son PSG en WASM** : export `neost_audio_render` tiré par un `ScriptProcessorNode`.

## Bus error & cartouches de diagnostic
- **Modèle bus error = port fidèle Hatari** (`ioMem.c`+`ioMemTabST/STE.c`+`cpu/memory.c`) :
  tout `$FF8000-$FFFFFF` faute par défaut, whitelist des registres câblés par modèle
  (`Bus::buildIoFault`, carte octet par octet) + zones void + fixups ST/MegaST/MegaSTE.
  Hors IO : `$400000-$F9FFFF` et `$FF0000-$FF7FFF` fautent. Règle word/long : faute
  seulement si TOUS les octets fautent (`busFaultN`). Suivi par les DEUX cœurs.
- **Double bus fault → halt CPU** (Musashi `m68k_pulse_halt`, Moira `flags|=HALTED`) au
  lieu de segfault hôte → le headless peut vider trace + série.
- **Trame de bus error 68000 dans Musashi** (`m68kcpu.h`) : empilait la trame 68010
  (format-8, 58 o) au lieu de la trame 68000 (14 o) → les handlers `adda #8 ; rte` des
  diags revenaient sur PC corrompue. **Le déblocage principal.** Adresse fautive
  (`m68ki_aerr_*`) renseignée → diags affichent la vraie adresse.
- **Blitter** (`Blitter.cpp`, port fonctionnel Hatari, mode HOG) : HOP, LOP 16 ops,
  FXSR/NFSR, skew, smudge, halftone, endmasks, comptes X/Y, incréments signés. Présent
  Mega ST/STE/Mega STE, absent STF. **IRQ de fin sur GPIP3**, BUSY+HOG effacés à `y_count==0`.
- **Blitter — icônes GEM correctes (Mega ST/STE)** : les icônes de fenêtre du bureau
  (TOS/EmuTOS) étaient corrompues (franges rouge/cyan, plans désalignés). Trois correctifs
  de fidélité Hatari, validés **byte-identiques** au VDI logiciel (mode `st`) sur les deux
  cœurs (capture bureau Pirates + TOS 1.02 FR, `megast` vs `st` = 0 octet) :
  - **Écriture mot/long ATOMIQUE du registre contrôle** (`Bus::write16/32`→`Blitter::write16/32`).
    *Le bug principal.* `move.w …,$FF8A3C` pose contrôle (BUSY, octet haut) **et** skew
    (`$FF8A3D`, octet bas) ; l'ancienne décomposition octet-par-octet déclenchait `run()`
    sur l'octet de contrôle **avant** l'écriture du skew → le blit du plan 0 partait avec
    le **skew périmé** de l'opération précédente, désalignant le plan 0 des plans 1-3.
  - **`bus_word`** (dernier mot du BUS : lecture src/dst **et** écriture dst, cf.
    `Blitter_ReadWord/WriteWord`) réinjecté par NFSR — et non plus la dernière source.
  - **2ᵉ passe du cas spécial NFSR** (`x_count==1`) après l'écriture + **persistance** du
    registre à décalage `buffer`/`bus_word` entre blits (remis à 0 au seul reset matériel).
- **RTC RP5C15** (Mega ST/Mega STE, `$FFFC21-$FFFC3F`) : modèle paresseux déterministe
  (cycle CPU du dernier top de seconde + rattrapage), registre RESET, débordement BCD
  calendaire. Corrige « C0 No clock installed » + « C1 clock increment error ».
- **MIDI** (`MidiAcia`, `$FFFC04/06`) : bouclage OUT→IN + IRQ canal 6.
- **Port série RS-232 / USART MFP** : RSR/UDR, IRQ RxFull (12)/TxEmpty (10)/RxErr (11)/
  TxErr (9), lignes RTS→CTS (GPIP2)/DTR→DCD (GPIP1)/RI (GPIP6) via PSG port A.
- **Disque dur ACSI** (`Fdc`, `$FF8604/06` bit `DMA_CSACSI`, port `hdc.c`) : commande
  6 octets, READ/WRITE(6), INQUIRY, READ CAPACITY, TEST UNIT READY ; disque virtuel
  agrandi à la demande (64 Mo).
- **PSG `$FF8802` relisible** (read-modify-write `bclr/bset` du port A).
- **SCU MegaSTE — gate d'interruptions complet** (`$FF8E01-$FF8E0F`, port `scu_vme.c`,
  `Scu.hpp`) : sur MegaSTE, **toutes** les IRQ sont gatées par `SysIntMask`/`VmeIntMask`
  avant d'atteindre l'IPL (MFP niv6/SCC niv5 via VmeIntMask, VSYNC niv4/HSYNC niv2/soft IRQ1
  via SysIntMask), **toujours actif** comme `SCU_IsEnabled()` d'Hatari (= MegaSTE/TT).
  `gatedLevel` consulté dans `neostUpdateIpl`, état synchronisé depuis les sources vivantes
  (MFP/VBL/HBL). 8 registres relisibles, écrire un masque remet l'état pending à 0 ;
  `GPR1`=0x01 au reset (contournement « TOS v2/v3 »). Validé (2 cœurs) : **TOS 2.06 et
  EmuTOS 256K (`Atari Mega STe`) bootent au bureau GEM** + diagnostic MegaSTE OK — tous
  programment le SCU tôt au boot (`SysIntMask=0x14`, `VmeIntMask=0x40/0x60`), comme sur
  Hatari. ST/STE/Mega ST inchangés (gating MegaSTE seul). ⚠ L'EmuTOS 192 Ko est un build
  « Atari ST » (TOS 1.4) qu'Hatari refuse aussi sur MegaSTE → utiliser `etos256us/fr` ou TOS 2.06.
- **Registre Cache/CPU MegaSTE `$FF8E21` relisible** (port `IoMemTabMegaSTE_CacheCpuCtrl_WriteByte`) :
  octet latché (bit0 = cache, bit1 = vitesse 8/16 MHz) avec la contrainte matérielle « cache
  impossible à 8 MHz » (bit0 forcé à 0 si bit1=0). Reset = 0. L'EFFET (débit cycles, cache 16 Ko)
  reste un item « précision cycle ». Boot MegaSTE byte-identique.
- **Lectures STE joypad/paddle/lightpen + DIP MegaSTE** (`$FF9200-$FF9223`, port `joy.c`) :
  valeurs au repos au lieu d'un `0xFF` générique — DIP MegaSTE `$FF9200` octet haut = `0xBF`
  (lecteur HD 1.44 Mo, logique inversée), boutons/directions `0xFF` relâchés, paddle au neutre
  `0x24`, lightpen `0x0000`. Boot STE byte-identique, MegaSTE (EmuTOS/diag) inchangé.
- **Bus map gaté par modèle** : sur Mega ST/STE `$FF8002-$FF800D` est void (pas de faute)
  contrairement au ST (`IoMem_FixVoidAccessForMegaST`).

**Résultat** : les **3 cartouches** (`ST_Diagnostic`, `STE_Test`, `MegaSTE_Diagnostic`)
atteignent leur menu et passent leur batterie de tests internes (Z) **sans erreur**, sur
les **2 cœurs**, avec un vrai TOS. Restes (« Hard error »/VME/FPU) = périphériques absents,
fidèles à Hatari, pas des bugs.

## Frontend & outillage
- Écran ST dans une fenêtre ImGui ; visualiseur hexa + registres 68000 ; boutons Reset /
  Hard Reset ; barre résolution. Bridage **50 fps réels**. Persistance (`neost.cfg`).
- **`neost-headless`** : trace d'instructions façon MAME, registres, IRQ (`--irq`), capture
  PPM, injection clavier (`--keys`) / souris (`--walk-mouse`), bouclage (`--loopback`).
  C'est l'outil de débogage principal.
- **`tools/trace_diff.py`** : aligne une trace NeoST et une trace Hatari sur un PC commun
  (`--align-pc`) et localise la première divergence (flux PC + registres).

## Validé
- EmuTOS (FR/US) : green desktop, fichiers disquette, double-clic, fenêtres.
- TOS 1.02 Mega ST FR : boot complet, green desktop basse rés.
- **Arkanoid** (Imagine 1987) : se lance via l'AUTO de la disquette et affiche son
  écran-titre **stable** (plus de gel `$31736`) — résolu par le **modèle FDC rotationnel**
  (spin-up + débit MFM réels), sous Musashi ET Moira. ⚠ **Le jeu ne démarre pas encore**
  (on atteint le titre, jamais la partie — cf. TODO §FDC). Lemmings (cracktro), Out Run
  (répertoire), etc. chargent depuis la disquette.
- **Diagnostic ST « Field Service » v4.4** (cartouche) : batterie Z (RAM/ROM/Clavier/Audio/
  MFP-Glue-Timing/BLiT) = Pass ; **Floppy → Test Speed** = ~200 ms/tour (300 RPM).
