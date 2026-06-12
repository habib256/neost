# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. Ce qui est **implémenté et validé**. Pas encore de versions
taguées (0.1.x). Le restant est dans [`TODO.md`](TODO.md).

## Cœur & boot
- **Cœur Musashi RETIRÉ — Moira seul cœur 68000.** L'ancien cœur Musashi (rapide mais
  **non cycle-exact**) n'apportait plus rien face à Moira (cycle-exact) et doublait chaque
  chemin du wrapper `Cpu68k`. Suppression : sous-module `extern/Musashi` (`git rm`,
  `.gitmodules`), sources `m68k*.c`/étape `m68kmake` (CMake), tous les `#if NEOST_HAS_MUSASHI`
  de `Cpu68k`/`Tracer`/`Blitter`, sélecteur de cœur du GUI et de l'UI WASM. Moira devient
  **requis** (CMake faute s'il manque). Désassemblage du `Tracer` et du `--disasm` headless
  reporté sur `moira::disassemble` (`Syntax::MUSASHI` → format de trace inchangé). **Rétro-compat
  conservée** : `--cpu`/`cpu=`/`?cpu=` n'acceptent plus que `moira`, mais une ancienne valeur
  `musashi`/`uae` est tolérée — on AVERTIT puis on bascule sur Moira (`Cpu68k::parseCore`).
  Boot EmuTOS 192 → bureau GEM et diagnostics inchangés.
- `Bus` (memory map ST) + wrapper `Cpu68k` (Moira) + `Shifter` (vidéo).
- Lib `neost_core` sans dépendance GUI ; frontends `neost` (fenêtré) et `neost-headless`.
- Boot 68000 : overlay ROM en `$0-$7` (SSP/PC), refermé après reset. TOS auto-détecté
  (192 Ko → `$FC0000`, sinon `$E00000`).
- **Cœur CPU sélectionnable** (`--cpu musashi|moira`, `neost.cfg`, WASM `?cpu=`).
  Moira (cycle-exact, sous-module) boote EmuTOS pixel-identique et délivre les IRQ.
- **Erreur d'adresse 68000 émulée** (`MOIRA_EMULATE_ADDRESS_ERROR = true`, appliqué
  comme PRECISE_TIMING sur la copie générée de MoiraConfig.h) : un accès word/long
  à adresse IMPAIRE déclenche l'exception 3, comme Hatari (`exception3_*`). Des
  cracks s'en servent délibérément — le cracktro TDA de **Rick Dangerous** installe
  son handler et provoque des accès impairs : sans l'exception, Moira « réussissait »
  l'accès et le PC partait à $0 (écran de points rouges) ; désormais cracktro →
  trainer (y/n) → **écran-titre du jeu**. Étalons et jeux déjà validés inchangés.
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
  qu'avant).

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
- **Sync-scroll / bordures EN JEU (Enchanted Land)** — chaîne complète :
  `videoCounter` consulte la machine Glue **LIVE** (VDE_On/Off + fenêtre DE réelle,
  re-fermeture comprise — l'ancien « sticky » mentait aux calibrations) ; tics
  **Timer B pilotés par la Glue live** (par scanline, un retrait haut/bas en cours de
  trame ajoute ses tics comme `Video_AddInterruptTimerB`) ; **datation des écritures
  freq/res +16 cycles** (accès bus daté en fin d'instruction comme Hatari CE —
  calibré oracle : impulsions du jeu verrouillées à L63 c376→384) ; ancre **prefetch
  STE** (MMU −16 cyc / +8 octets, port `Video_CalculateAddress`). Résultat :
  Enchanted Land en jeu passe de 0 à 5800+/12000 trames avec tricks détectés,
  **bordures gauche/droite ouvertes** (haut partiel — stabilité haut/bas = chantier
  wait states). Étalons inchangés (spec512, overscan_top, scrolls, glue 19/19).
- **Adresse vidéo accumulée par OCTETS FIXES selon les drapeaux de bordure**
  (`renderGlueFrame`, port des `BORDERBYTES_*` de `Video_CopyScreenLineColor`,
  video.h:111-115) au lieu de `(DE_end−DE_start)/2` : une ligne RIGHT_OFF lit 204
  octets (160+44, bord réel cycle 464) alors que la fenêtre s'arrête à 462 —
  l'ancien calcul perdait 1 octet PAR LIGNE et le décor des écrans overscan
  dérivait cumulativement (loader TDA de Rick Dangerous : bandes de garbage
  empirant vers le bas, maintenant net). Étalons (glue selftest 19/19,
  overscan_top, spec512, scrolls STE) inchangés.
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
  toujours Pass.
- **Registres STE** (gatés STE) : fine scroll `$FF8264/65`, line width `$FF820F`, base
  basse `$FF820D`, palette 4 bits/canal, relecture sync.
- **Zones « void » du shifter fidèles par machine** (port `ioMemTabST.c`/`ioMemTabSTE.c`,
  handlers `IoMem_VoidRead`=0xFF / `IoMem_VoidRead_00`=0x00) : sur **STE/MegaSTE**,
  `$FF820B`, `$FF8262-63` et `$FF8266-7F` lisent **0x00** ; le reste (dont `$FF820C/0E`)
  lit **0xFF**. Sur **ST/MegaST**, TOUTES les zones void lisent **0xFF** (l'ancien
  fallback renvoyait 0x00 partout). Whitelist bus-error inchangée (déjà conforme).
  Validé : glue self-test 19/19, boots ST 192 / STE byte-stables.
- **Rendu STE câblé** : `renderLine` décode en tampon d'index puis émet avec offset →
  **fine-scroll** horizontal 0-15 px (décalage gauche + groupe de 16 px lu en plus à droite,
  modèle prefetch `$FF8265`), **line-offset** `$FF820F` (stride ligne `bpl + lineWidth*2`,
  aussi dans le compteur `$FF8205/07/09`), **base-basse** `$FF820D` composée dans `videoBase`.
  Défaut (scroll 0 / line-width 0) byte-identique au boot.
- **Scroll fin STE — prefetch vs $FF8264 + avance compteur EXACTE** (port
  `Video_CopyScreenLineColor`/`Mono` + `Video_GetMMUStartCycle`), validé **pixel-identique
  à l'oracle Hatari** sur étalon synthétique :
  - **$FF8265 (prefetch)** : le compteur vidéo avance d'**1 mot PAR PLAN** par ligne
    (+8 octets en basse rés, +4 en moyenne, +2 en mono) — l'ancien pas uniforme (+2)
    désalignait les PLANS sur un remplissage contigu (couleurs parasites, lignes
    fantômes 1/4). Sur le motif de test, la dérive donne la DIAGONALE de 16 px/ligne
    du vrai matériel (`scrollCounterAdvance`, appliqué au compteur matérialisé, au
    stride analytique et au stride du compteur live `$FF8205/07/09`).
  - **$FF8264 (sans prefetch)** : désormais DISTINGUÉ — aucun mot supplémentaire lu,
    aucune avance compteur ; l'affichage démarre 16 px plus tard : les 16 premiers
    pixels sont couleur 0 et `dst[c] = source[c-16+scroll]` (memmove+memset d'Hatari,
    pré-transformé dans `decodeLineIndices` → les émetteurs, spec512 inclus, héritent).
  - **MMU start −16 cycles** gaté sur le prefetch seul (avant : sur tout scroll).
  - **Étalons permanents** : `tools/make_scroll_test.py` (motif 1 colonne/64 px sur
    remplissage contigu — rend visibles décalage ET pas du compteur) → disques générés
    `scroll_8265.st`/`scroll_8264.st`, références verrouillées à **0 px d'écart**
    (`etalons.json`), conformes à l'oracle Hatari ligne par ligne.
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
  inchangés). Raffinements restants (WS3, med-res overscan, blank lines, pixel-perfect L/D,
  scrolling Cuddly) → cf. TODO §Bordures.
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
  pixel-identique, EmuTOS/TOS boot OK. Scrolling robot + scroller bordure basse → cf. TODO.
- **Machine Glue LIVE → compteur vidéo par-ligne — DÉBLOQUE ENCHANTED LAND** (Thalion
  1990, qui passait « LOADING » puis écran noir à jamais). La machine Glue STF complète
  (`startHBL` + `updateGlueState`, port `Video_Update_Glue_State`) tourne désormais
  **au fil de la trame** via un curseur incrémental (`liveGlueCatchUp` : startHBL des
  lignes atteintes + consommation chronologique des écritures freq/res — exactement la
  boucle de `replayGlue`, qui ré-écrase tout en fin de trame → live et replay donnent
  le même résultat par construction). `videoCounter()` lit alors la **fenêtre DE réelle
  de la ligne courante** (`displayStartCycle/EndCycle`) au lieu des constantes de la
  géométrie de trame — port fidèle de `Video_CalculateAddress`, qui lit
  `ShifterLines[HBL]`. Pourquoi c'est indispensable : le loader d'Enchanted Land embarque
  une **routine de calibration fullscreen** ($EE76-$EFCC) qui se cale au faisceau
  (poll `$FF8209` 0→≠0 + compensation de gigue `lsr.w d2,d2` + saut calculé dans un tapis
  de NOPs), puis émet une **impulsion 60→50 Hz dos à dos** ($820A=0 puis =2, 8 cyc d'écart)
  en balayant la position cycle par cycle, et **mesure sur `$FF8209`** si l'impulsion a
  raccourci la ligne (-2 octets : comparateur HDE_Off 60 Hz = cycle 372 < 376) — deux
  lectures consécutives + un test « compteur figé / fini à `…9E` au lieu de `…A0` ».
  Sans l'effet live, la calibration scanne à l'infini → noir. Avec : logo Thalion +
  pluie **conformes à l'oracle Hatari** (trames 1300-9000), puis **JEU JOUABLE** après
  une touche (écran de gameplay complet, 2 cœurs Musashi ET Moira). Trame sans écriture
  freq/res → chemin historique strictement inchangé. **Zéro régression** : glue
  self-test 19/19, étalons `run_etalons` TOUS OK (boot STE, spec512 diapo, overscan top),
  overscan L/D (`make_overscan_lr`) ouvre toujours les bordures, menu Cuddly stable.

## Interruptions (MFP 68901)
- IER/IPR/IMR/ISR + registre vecteur, modes auto et software-EOI.
- **Modèle recharge/compteur des timers fidèle à Hatari** (port des
  `MFP_TimerXCtrl/Data_WriteByte` + `MFP_ReadTimer_AB/CD`) : écrire TxDR pendant qu'un
  délai court ne touche NI le compteur NI l'échéance (seule la RECHARGE change, appliquée
  au prochain rechargement) ; réécrire la même valeur de TxCR ne redate pas le timer ;
  arrêter un délai FIGE le compteur courant (relisible, et le timer REPREND de là si
  relancé sans réécrire TxDR — règle « < 1 unité → recharge » comprise) ; démarrer part
  du COMPTEUR (continuation), pas de la recharge. Lecture du compteur vivant : repli
  modulo la période quand l'échéance vient d'expirer mais n'est pas encore dispatchée
  (`Scheduler::rawCyclesUntil`, reste négatif sous-instruction) — le matériel a déjà
  rechargé, l'écrêtage à 0 rendait la valeur de recharge illisible. Débloque **Captain
  Blood** (le player musical réécrit TADR en boucle et compare au compteur vivant —
  l'ancien modèle redémarrait le timer à chaque écriture → boucle infinie, écran noir).
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
- **Overrun récepteur (bit OVRN, `$20` du SR)** — port de `acia.c` (`ACIA_Clock_RX`
  état STOP_BIT + `ACIA_Read_RDR`/`SR_Read`) : le SCI de l'IKBD livre EN CONTINU
  (~10240 cyc/octet), que le CPU lise ou non — un octet qui arrive RDR plein est
  **PERDU** (RDR conserve l'ancien) et `rxOverrun_` reste pendant (la cause d'IRQ RX
  reste active, comme `RX_Overrun` dans `ACIA_UpdateIRQ`). Le bit **OVRN** ne se pose
  qu'à la **lecture de RDR** (pas au moment de la perte) et s'acquitte par la séquence
  « lire SR puis RDR ». Master reset ACIA (`CR` bits 0-1 = 11) efface RDRF/OVRN sans
  toucher la file IKBD (elle vit côté 6301). Avant : NeoST RETENAIT l'octet suivant
  jusqu'à la lecture de RDR (flow-control irréaliste, l'ancien TODO « SR n'expose pas
  overrun »). FE/PE restent à 0 : la liaison émulée ne produit ni erreur de trame ni
  de parité ; DCD/CTS à la masse sur l'ST.
- **Keymap international (layouts TOS FR/UK/DE…)** — port du mapping SYMBOLIQUE de
  Hatari `sdl/keymap.c` dans le GUI : une touche imprimable est traduite par le
  CARACTÈRE qu'elle produit sur la disposition HÔTE (`glfwGetKeyName`, UTF-8 décodé)
  via la table par défaut + surcharges par PAYS du TOS chargé (US/DE/FR/UK —
  `Keymap_SetCountry`, pays lu dans l'en-tête ROM `os_conf $1C >> 1`, re-détecté à
  chaque changement de ROM ; 127 = EmuTOS multilangue → défaut). Un hôte AZERTY sous
  TOS FR tape « a » → scancode `$10`, un hôte QWERTY sous TOS FR obtient aussi les
  bons caractères. Touches non imprimables (Entrée, flèches, F1-F10, pavé, modifs) :
  mapping positionnel inchangé. **Autorepeat** : déjà conforme — `GLFW_REPEAT` ignoré,
  c'est le TOS qui répète (l'IKBD n'émet qu'un make par appui), comme Hatari. Pays
  vérifié sur les 40 ROMs du dépôt (FR/UK/DE/ES/IT/US/multilangue corrects).
- **IRQ d'émission ACIA MIDI (TIE, CR bits 5-6 = 01) + TDRE cadencé** — l'ACIA MIDI
  (`$FFFC04/06`) suit désormais le même modèle que l'ACIA clavier (port
  `ACIA_Write_CR`/`ACIA_UpdateIRQ`) : écrire une donnée sous TIE vide TDRE, re-rempli
  ~1 octet MIDI plus tard (10 bits à 31250 bauds = **2560 cycles**, `Scheduler::MIDI_TX`)
  → IRQ « transmetteur prêt » qui cadence la sortie des séquenceurs MIDI. Hors TIE,
  TDRE reste câblé à 1 (statut), comme côté clavier. L'ancien TODO « TDRE câblé à 1 +
  CR bits 5/6 ignorés » de l'ACIA MIDI est levé.
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
- **FDC rapide neutralisé sur image STX** (écart assumé avec Hatari, anti-piège) : les
  protections Pasti MESURENT les durées (timing par octet, rotation) — `fastfdc` ÷10 les
  casse (Stunt Car Racer : 11 bombes en GUI avec `fastfdc=1` persisté, écran blanc en
  headless ; Hatari casse pareil avec son `--fastfdc on`). Une STX montée dans le lecteur
  sélectionné ignore l'accélération (avertissement stderr une fois). Stunt Car Racer
  atteint son titre même avec `fastfdc=1`.
- **Détection de géométrie recoupée par la taille de l'image** (port `floppy.c` :
  `Floppy_FindDiskDetails` + `Floppy_DoubleCheckFormat`). Le BPB du secteur de boot est
  souvent FAUX sur les cracks ; on ne lui fait confiance que si `secteurs totaux ==
  taille/512` et spt/faces plausibles, sinon recalcul depuis la taille (faces = 2 si
  ≥ 500 Ko ; spt ∈ {9,10,11,12} × 80-84 pistes). Débloque **Xenon 2** (BPB faces=1 au
  lieu de 2 → code corrompu, 4 bombes), **Epic** (BPB bidon → 9 spt au lieu de 11,
  11 bombes) et **Super Hang-On** (9 spt au lieu de 10 → retry infini, écran noir).
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
  l'oracle).   Quelques protections spécifiques restent à affiner (cf. TODO).
- **Masquage d'adresse DMA** (port `FDC_WriteDMAAddress` / `DMA_MaskAddressHigh`) :
  octet haut `&0x3f/0x7f/0xff` selon le modèle, bas forcé word-align `&0xfe`.
- **Compteur de secteurs DMA non relisible** : lecture SCREG `$FF8604` renvoie
  `ff8604recent_` (pas `dmaSectorCount_`) ; bits statut DMA 3-15 depuis le dernier
  accès `$FF8604` (`FDC_DiskControllerStatus_ReadWord`, `FDC_DmaStatus_ReadWord`).
- **Accès octet à `$FF8604/06` → bus error** (ST non-Falcon) : largeur d'accès
  propagée par le bus, faute dans le handler FDC (`ioMemTabSTE.c`).
- **Erreurs d'adresse 68000** (`M68K_EMULATE_ADDRESS_ERROR`, exception 3 sur accès
  mot/long impair) activées sous Musashi (Moira les avait déjà) — requises par les
  anti-debug de certaines protections. Boot EmuTOS byte-identique, batterie Z des
  diagnostics inchangée.

## Audio
- **Bit PLAY ($FF8901) auto-effacé en fin de trame DMA one-shot** dans le MOTEUR DMA
  (`DmaSound::onFrameEnd`, port `DmaSnd_EndOfFrameReached` dmaSnd.c:510) et plus
  seulement dans le mixeur hôte (qui ne tourne pas en headless). Le handler VBL du TOS
  surveille ce bit (détection moniteur/son) : un PLAY collé déclenchait un RESET en
  boucle — la démo STE **Faster** rebootait au lieu d'entrer en course.
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
    n'est armé que si le frontend pose l'horloge (`setCycleClock`). _Reste (refinement) : FIFO
    8 octets du DMA remplie sur HBL (cf. `docs/SOUND_HATARI_DIFF.md`)._
- **Compteur de trame DMA LIVE cycle-exact** (`DmaSound::liveCounter`, équivalent du
  `Sound_Update` en tête de `DmaSnd_GetFrameCount`) : `$FF8909/0B/0D` sont désormais
  dérivés de l'**horloge émulée** (trame latchée au démarrage — début/fin/cycle de
  départ, comme `DmaSnd_StartNewFrame` — puis position = écoulé × fréquence, bornée à
  la fin de trame ; l'événement DMASND gère repeat/arrêt). Avant, le compteur ne
  bougeait qu'au rythme de la synthèse audio HÔTE : **figé en headless** (mix jamais
  appelé) et imprécis pour les lecteurs qui le POLLENT pour se synchroniser. Validé
  par mini-ROM (trame $1000-$2000 mono 50066 Hz : $107D→$10FA→$1177 à ~20 000 cycles
  d'intervalle = 125 octets exacts, identique Moira/Musashi ; avant : figé à $1000).
- **Anti-repliement du canal DMA** (port `DmaSnd_LowPassFilter`, dmaSnd.c:1316-1349) :
  FIR 3 points (1,2,1)/4 appliqué à CHAQUE octet tiré **à la cadence DMA** quand elle
  dépasse la fréquence de sortie hôte (50066 Hz → 48 kHz) ; filtre coupé = retard d'un
  échantillon ×4 (gain/latence constants, « divide by 4 » d'Hatari au mixage). Au
  passage, l'octet courant est TENU entre deux pas DMA (plus de relecture RAM par
  trame de sortie).
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
- **Fenêtre ROM complète** (`Bus::romWindowSize`, port `memory.c` map_banks ROMmem) :
  une ROM à `$E00000` répond sur TOUT `$E00000-$EFFFFF` (1 Mo, 16 banques), pas
  seulement sur la taille du fichier — lire au-delà du TOS chargé renvoie 0 (tampon
  Hatari) SANS bus error ; les ÉCRITURES fautent sur toute la fenêtre. ROM historique
  `$FC0000` : 3 banques = 192 Ko (= le fichier). Le cache Mega STE cache toute la
  fenêtre en lecture (comme Hatari `$E00000-$F00000`).
- **DMA via le plan mémoire** (`Bus::dmaRead8/dmaWrite8`, port
  `STMemory_DMA_ReadByte/WriteByte`) : les accès RAM du FDC (FIFO 16 octets), de
  l'ACSI et du son DMA STE traversent désormais le MÊME plan mémoire que le CPU —
  traduction MMU / aliasing de banques inclus, ROM lisible — au lieu de `ram[]`
  physique. Jamais de bus error côté DMA : lire une zone fautive renvoie 0
  (`DMA_READ_BYTE_BUS_ERR`), y écrire est perdu. Pas de wait states ni de test
  superviseur (protections propres aux accès CPU, équivalent `BusMode` d'Hatari).
- **Double bus fault → halt CPU** (Musashi `m68k_pulse_halt`, Moira `flags|=HALTED`) au
  lieu de segfault hôte → le headless peut vider trace + série.
- **Trame de bus error 68000 dans Musashi** (`m68kcpu.h`) : empilait la trame 68010
  (format-8, 58 o) au lieu de la trame 68000 (14 o) → les handlers `adda #8 ; rte` des
  diags revenaient sur PC corrompue. **Le déblocage principal.** Adresse fautive
  (`m68ki_aerr_*`) renseignée → diags affichent la vraie adresse.
- **Blitter** (`Blitter.cpp`, port Hatari) : HOP, LOP 16 ops, FXSR/NFSR, skew, smudge,
  halftone, endmasks, comptes X/Y, incréments signés. Présent Mega ST/STE/Mega STE,
  absent STF. **IRQ de fin sur GPIP3**, BUSY+HOG effacés à `y_count==0`.
- **Blitter — partage de bus (hog ET non-hog)** (port du modèle non-CE d'Hatari,
  blitter.c:864-944) : le transfert n'est plus instantané. **HOG** (bit6) : le blitter
  garde le bus jusqu'à `y_count=0`, le CPU est arrêté toute la durée (4 cycles par
  accès bus réellement effectué, facturés via `addBusWaitCycles` — Moira). **Non-hog** :
  TRANCHES de 64 accès bus (256 cycles, CPU arrêté) alternées avec 64 accès CPU
  (256 cycles), via `Scheduler::BLITTER` — l'alternance 64/64 du vrai matériel. Le
  moteur de données est devenu REPRENABLE (état de reprise dans les registres
  relisibles + `xReset_/haveFxsr_/nfsrInt_`) : **BUSY et compteurs/adresses sont
  lisibles EN COURS de blit** (progression par tranche), et effacer BUSY pendant le
  transfert met le blitter en PAUSE (reprise au prochain BUSY=1), comme le « CPU can
  stop the blitter » d'Hatari. Limites documentées : découpe à la frontière de mot
  (±3 accès), stall no-op sous Musashi (durée BUSY/IRQ seule). Étalons
  byte-identiques (bureau EmuTOS STE dessiné au blitter).
- **Blitter — cycles d'arbitration + bug « 63 accès »** (port `Blitter_BusArbitration`
  + `Blitter_HOG_CPU_mem_access_before`, blitter.c:69-79 et 380-420) : prendre le bus
  coûte **4 cycles (8 sur Mega STE**, avec flush du cache externe — déjà fait), le
  rendre au CPU **4 cycles** — facturés dans le stall de chaque tranche/blit. En
  non-hog, chaque prise de bus est précédée d'une fenêtre **PRE_START de 4 cycles**
  (BUSY posé, bus pas encore pris — la 1re tranche est désormais DATÉE à +4, le CPU
  finit son instruction) pendant laquelle le blitter compte déjà les accès : un accès
  bus CPU qui tombe dans la fenêtre (signalé par les callbacks mémoire de Moira via
  `Bus::blitterWinStart/End`, daté à l'horloge bus absolue) lui **vole un accès** →
  tranche de **63** au lieu de 64 (cf. la calibration de « Relapse » citée par
  Hatari). Sous Musashi : pas de datation sous-instruction → toujours 64, durée
  seule. Étalons byte-identiques, diags blitter Pass (G/Y/Z).
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
- **Persistance RTC entre sessions** (`neost.cfg` : clés `rtc=` date/heure BCD,
  `rtc_saved=` horodatage hôte) : reprise au boot avec rattrapage du temps écoulé ;
  snapshot à chaque sauvegarde de config (`saveConfig` / `snapshotRtc`).
- **MIDI** (`MidiAcia`, `$FFFC04/06`) : bouclage OUT→IN + IRQ canal 6.
- **Port série RS-232 / USART MFP** : RSR/UDR, IRQ RxFull (12)/TxEmpty (10)/RxErr (11)/
  TxErr (9), lignes RTS→CTS (GPIP2)/DTR→DCD (GPIP1)/RI (GPIP6) via PSG port A.
- **Config effective de l'USART** (`Mfp::updateSerialConfig`, port de `rs232.c`
  `RS232_SetBaudRateFromTimerD` + `RS232_HandleUCR`) : bauds dérivés du Timer D
  (2.4576 MHz, sortie ÷2, prescaler /16 de l'UCR — seul mode supporté, comme Hatari)
  avec les arrondis « TOS » vers les bauds standards (80→75, 109/120→110,
  1745/1920→1800), format du mot depuis l'UCR (taille bits 5-6, parité bits 1-2,
  stops bits 3-4). Recalculée à chaque écriture UCR/TDDR/TCDCR, exposée
  (`serialBaud()`/`serialUcr()`) et JOURNALISÉE au changement — au boot on voit
  EmuTOS/TOS négocier `9600 bauds, 8N1`. Comme chez Hatari c'est de la pure
  configuration (appliquée à son tty hôte) : le débit d'émission émulé reste
  instantané (`RS232_TSR_ReadByte`), backing-store des registres inchangé.
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
  impossible à 8 MHz » (bit0 forcé à 0 si bit1=0). Reset = 0.
- **Bascule CPU 8/16 MHz MegaSTE — EFFET RÉEL** (`$FF8E21` bit1, port
  `m68000.c:MegaSTE_CPU_Cache_Update` / `MegaSTE_CPU_Set_16Mhz`) : l'ordonnanceur et
  toutes les puces restent en cycles **bus 8 MHz** ; le cœur CPU convertit
  (`Cpu68k.cpp` : `bus = (clock + biais) / mul`, biais rebasé à chaque bascule pour
  une horloge bus **continue**, même en plein quantum). **Moira (cycle-exact)** : port
  des `mem_access_delay_*_megaste_16` — accès **RAM ST** cadencés bus (attente du
  créneau CPU/Shifter de 8 cycles CPU + accès 8 cycles au lieu de 4), **ROM/cartouche/
  IO « FAST »** sans wait state (mesuré sur vrai STF par Hatari) → 2× plus rapides ;
  wait states PSG/MFP/ACIA et alignement shifter ×2 ; E-Clock recalée sur l'horloge
  bus. **Musashi** : débit ×2 uniforme, comme Hatari **non** cycle-exact. Validé par
  ROMs de test synthétiques (boucle `nop`+`bra` comptée sur 10 trames, Moira) :
  **ROM 16 MHz = 2.000×**, **RAM 16 MHz sans cache = 0,88×** (aucun bénéfice, le bus
  8 MHz domine — fidèle au matériel), **RAM 16 MHz + cache = 2.000×** ; Musashi = 2×
  partout (non-CE assumé). Reset/reconfigure → retour 8 MHz (`MegaSTE_CPU_Cache_Reset`).
- **Cache externe 16 Ko MegaSTE** (`$FF8E21` bit0, port `m68000.c:MegaSTE_Cache_*`) :
  8192 lignes × 1 mot (tag = bits 14-23, ligne = bits 1-13, bit 0 ignoré), données
  dans `Bus::megaSteCache`, facturation des cycles côté Moira (hit = 4 cycles CPU
  16 MHz ; miss → accès bus + remplissage, lecture octet remplie par le MOT du bus ;
  écriture **write-through**, jamais accélérée, maj de ligne octet seulement si déjà
  cachée). Cachable : RAM ST installée (< 4 Mo) + ROM TOS en lecture ; jamais IO/
  cartouche, ni accès fautifs (mot impair, `$0-$3` en écriture, `$0-$7FF` en user).
  **Invalidation** : bit0 → 0, reset, bus error, et BGACK (départ blitter, DMA
  FDC/ACSI) — les écritures DMA ne traversent pas le cache, comme sur le vrai matériel.
- **Séparation user/supervisor** (`Bus::busFaultN(addr, n, write)`, port des banques
  `SysMem_*`/`ROMmem_*` de `cpu/memory.c` + `is_super_access` d'`ioMem.c`) — pour TOUS
  les modèles : en mode **utilisateur** (bit S=0, lu via `Cpu68k::supervisor()`), tout
  accès à `$0-$7FF` (variables système) ou à l'espace **IO** `$FF8000-$FFFFFF` → bus
  error ; en **écriture** (même superviseur) : ROM TOS, port cartouche et `$0-$7`
  (miroir ROM des vecteurs reset) → bus error. Code fonction de la trame d'exception
  (user/super) désormais correct sous Musashi aussi. Blitter/DMA exemptés (équivalent
  `BusMode != BUS_MODE_CPU`). Validé : ROMs de test (lecture `$400` en user → handler
  de bus error atteint, écriture ROM en superviseur → idem, 2 cœurs) ; étalons
  ST/STE/MegaSTE inchangés (EmuTOS/TOS ne violent jamais ces protections).
- **MC68881 optionnel — sonde + trapping** (`src/io/Fpu.hpp`, `--fpu` headless /
  case « FPU 68881 » du menu Modèle, `neost.cfg fpu=1`) : interface mémoire des
  registres coprocesseur (CIR) du socket 68881 du Mega STE en `$FFFA40-$FFFA5F`.
  Par défaut **absent** = fidèle Hatari (bus error, TOS/diagnostic concluent « FPU
  not found », cf. `M68000_IsVerboseBusError` qui silence la sonde `$fffa42`). Avec
  `--fpu` : la zone répond (Response CIR = `$0802` « null primitive, processing
  finished », Save CIR = trame IDLE `$1F18`, autres registres latched) → **TOS 2.06
  détecte le FPU** (lecture du Response CIR observée, cookie `_FPU`) et le dialogue
  CIR est **journalisé** sur stderr (trapping : tout usage flottant réel est visible).
  L'arithmétique 68881 n'est PAS émulée (cf. `TODO.md`).
- **Joypads STE COMPLETS + DIP MegaSTE** (`$FF9200-$FF9223`, port fidèle `joy.c` /
  `ioMemTabSTE.c`) : le stub « valeurs au repos » est remplacé par un vrai module
  `StePads` (`src/io/StePads.hpp`, membre `Bus::stePads`) — **multiplexage** par le
  latch de sélection `$FF9202` (nibble bas = pad A, haut = pad B, ligne active à 0),
  **boutons feu** `$FF9201` (feu A pad A → bit1, pad B → bit3), **directions** lues en
  `$FF9202` (`~dir`, nibble par pad), paddles `$FF9211-17` au neutre `0x24`, lightpen
  `$FF9220/22` à `0x0000`. **DIP MegaSTE** octet haut de `$FF9200` = `0xBF` (logique
  inversée : switch 7 ON → lecteur HD 1.44 monté, switch 8 OFF → son DMA actif, fidèle
  `IoMemTabMegaSTE_DIPSwitches_Read`) ; STE simple → `0xFF`. Les pads reçoivent le
  **même état joystick** que l'IKBD (pad A = port 1 « jeux », pad B = port 0) depuis le
  GUI, le web et le headless (`--joy`/`--joy-at`/`--joy-script`), comme le mapping
  manette global d'Hatari. Validé : glue self-test 19/19, boots STE/MegaSTE propres,
  `--joy 0x88` maintenu sans faute parasite.
- **Joypads STE — finitions** (port `joy.c`) : **bus error sur accès OCTET** de
  `$FF9200` (adresse paire seulement, lecture ET écriture — `$FF9201` reste lisible
  en octet) et des mots lightpen `$FF9220/22` en lecture (écritures ignorées sans
  faute, `IoMem_WriteWithoutInterception`) — déclenchée par le périphérique comme le
  FDC `$FF8604` octet. **Paddles analogiques réels** (`StePads::readAnalog`, port
  `Joy_GetStickAnalogData`) : plage `$04`-`$43` (neutre `$24`), axes manette hôte
  (stick gauche GLFW, conversion REALSTICK exacte `MIN + (upos>>8)/4`) avec REPLI
  numérique façon « mode clavier » Hatari (gauche/haut → `$04`, droite/bas → `$43`).
  Lightpen : non supporté (0 + bus error octet), fidèle à Hatari. Validé par mini-ROM
  (3 fautes attendues prises, `$FF9201`/mot sans faute, paddle `$24`→`$43` sous
  `--joy 0x08`, identique Moira/Musashi) + étalons inchangés.
- **Quirk palette — écriture octet miroir + masque** (port `Video_ColorReg_WriteWord`) :
  une écriture OCTET sur `$FF8240-$FF825F` duplique l'octet sur les DEUX moitiés du
  mot (le 68000 pose l'octet sur les deux moitiés du bus de données, le Shifter
  latche le mot : `move.b #$07,$FF8240` → couleur `$0707`, adresse paire ou impaire) ;
  la couleur est STOCKÉE masquée — `$777` (ST, 512 couleurs) / `$FFF` (STE, 4096) —
  donc RELUE masquée : des jeux écrivent `$FFFF` et relisent pour détecter le STE.
  Validé par mini-ROM (`$0707` ; `$FFFF`→`$0777` ST / `$0FFF` STE ; octet impair
  `$AB`→`$0323`/`$0BAB`) ; étalons byte-identiques (gate spec512).
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
- **Suite étalons headless** : `tools/etalons.json` (manifeste), `fetch_etalons.py`
  (fetch freeware), `run_etalons.py` (captures + régression vs `tests/reference/`),
  `compare_screenshot.py` (diff pixel, crop active/buffer), `hatari_oracle.sh`
  (oracle PNG, `--oracle`). Étalon intégrés : glue_selftest, EmuTOS STE boot,
  Spectrum 512 diapo, overscan_top ; fetch auto : Cuddly Demos (`fujiology`).
- **Horloge IKBD figée en headless** (`Ikbd::setClock`, 1ᵉʳ jan 2026 12:00:00 comme la
  RTC) : EmuTOS affiche la date/heure du bureau depuis l'horloge IKBD (commande `$1C`),
  pas la RTC — elle suivait l'heure HÔTE et cassait le diff pixel de `etos_ste_boot`
  (la référence embarquait l'heure de sa capture). Référence régénérée, étalon
  désormais **déterministe** et au vert.

## Validé
- EmuTOS (FR/US) : green desktop, fichiers disquette, double-clic, fenêtres.
- TOS 1.02 Mega ST FR : boot complet, green desktop basse rés.
- **Arkanoid** (Imagine 1987) : se lance via l'AUTO de la disquette et affiche son
  écran-titre **stable** (plus de gel `$31736`) — résolu par le **modèle FDC rotationnel**
  (spin-up + débit MFM réels), sous Musashi ET Moira.   ⚠ **Le jeu ne démarre pas encore**
  (on atteint le titre, jamais la partie — cf. TODO §Arkanoid). Lemmings (cracktro), Out Run
  (répertoire), etc. chargent depuis la disquette.
- **Diagnostic ST « Field Service » v4.4** (cartouche) : batterie Z (RAM/ROM/Clavier/Audio/
  MFP-Glue-Timing/BLiT) = Pass ; **Floppy → Test Speed** = ~200 ms/tour (300 RPM).
- **Enchanted Land** (Thalion 1990) : logo + pluie conformes à l'oracle Hatari, **jeu jouable**
  après une touche (2 cœurs) — débloqué par la machine Glue LIVE dans `videoCounter()`
  (calibration fullscreen du loader sur `$FF8209`).
- **The Cuddly Demos** : menu fullscreen statique stable (flicker résolu par VDE_On live) ;
  conforme aux briques d'Hatari.
