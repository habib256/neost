# Ce qui est implémenté — par sous-système

(c) 2026 VERHILLE Arnaud. **L'inventaire de ce que NeoST sait faire**, puce par puce, avec
les pièges refermés en chemin et les références Hatari correspondantes. C'est ici qu'on
vient répondre à « NeoST gère-t-il X ? ».

Pour *quand* une chose est arrivée → [`../CHANGELOG.md`](../CHANGELOG.md).
Pour ce qui reste → [`../TODO.md`](../TODO.md).

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
- Lib `neost_core` sans dépendance GUI ; frontends `neost` (fenêtré), `neost-headless`,
  `neost-web` (WebAssembly) et l'APK Android — plus `neost_net` pour les backends réseau.
- Boot 68000 : overlay ROM en `$0-$7` (SSP/PC), refermé après reset. TOS auto-détecté
  (192 Ko → `$FC0000`, sinon `$E00000`).
- **Sélecteur de cœur CPU** (`--cpu`, `neost.cfg` `cpu=`, WASM `?cpu=`) : ne vaut plus que
  `moira` depuis le retrait de Musashi (ci-dessus) — une ancienne valeur avertit et bascule.
  Moira (cycle-exact, VENDORISÉ dans `extern/moira`) boote EmuTOS pixel-identique et délivre
  les IRQ.
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
- **Dispatch d'événements BLOC (réfute le sync-driven) — Enchanted Land DÉ-DEADLOCKÉ.** Le modèle
  « sync-driven » (dispatch des events HBL/VBL/Timer-B EN COURS d'instruction depuis `Moira::sync()`,
  do_cycles WinUAE) deadlockait EL (boucle beam-sync `$EE78` jamais servie → écran noir dès la trame
  ~1200) SANS corriger le jitter (falsifié). Retour au **dispatch BLOC** (CPU borné à l'événement
  suivant + dispatch à la frontière via `runTo`, pré-sync-driven) **par DÉFAUT**, en gardant
  PT=true → intro/écrans statiques d'EL propres. Sync-driven en opt-in `NEOST_SYNC_DISPATCH` (reproduit
  le deadlock, A/B). Validé : étalons 19/0, LX inchangé. Reste la corruption EL EN JEU (scroll), chantier
  vidéo V3 (cf. [`docs/MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md)).
- **Convergence cycle Moira↔WinUAE — beam-sync (DÉFAUT ON depuis 2026-06-17).** Harnais différentiel
  (`NEOST_TRACE_CYC` colonne cycle absolue dans le `Tracer` + `tools/trace_diff.py --periods`) :
  compare les cycles/boucle des deux cœurs. `NEOST_RAM_SLOT` (align créneau bus 4 cyc sur la RAM CHIP16,
  port `wait_cpu_cycle`) + reorder DIV (`SYNC(idle)` avant prefetch, fork Moira) → **14/14 boucles
  d'instructions = WinUAE** au différentiel (bancs `tools/make_cycle_bench.py`). **PERCÉE** : `NEOST_RAM_SLOT`
  + `NEOST_IACK` (E-clock @ IACK) ENSEMBLE font tomber la dérive du faisceau sur Hatari (+78/ligne) et
  DÉCLENCHENT l'overscan beam-sync d'Enchanted Land (banc `tools/make_respulse_test.py` vs oracle
  `--trace video_res`) — séparément, aucun des deux ne suffit. **Désormais défaut ON** (`=0` pour
  désactiver) ; zone active byte-identique Hatari, `overscan_top` re-baseliné (56 px en bordure overscan
  seule). Reste l'overscan VERTICAL (phase absolue par-ligne). Détail/limites → doc maître.
- **Refonte beam-sync COORDONNÉE (2026-07-02) — le résidu de phase +24 ATTRIBUÉ et CORRIGÉ.**
  Oracle Hatari 2.6.1-devel bâti dans `extern/hatari/build` (Ubuntu). Quatre biais mesurés et
  retirés ENSEMBLE : **(1)** IACK sur-compté +8 (E-clock+bloc appliqués PAR-DESSUS les SYNC stock
  de Moira) → hooks `iackSyncBefore/After` AU point d'IACK réel (`MoiraExceptions_cpp.h` vendorisé,
  `NEOST_IACK_AT` défaut ON) — fait émerger le motif mod-20 des positions d'IRQ = Hatari ; **(2)**
  `chipWait8` alignait le point-MILIEU de l'accès au lieu du DÉBUT (WinUAE) → fin d'accès ≡0 mod 4
  = Hatari ; **(3)** origine d'horloge trame +8 vs coordonnées ligne Hatari → datations calibrées à
  l'oracle : read `$FF8205/07/09` **−14** (banc poll + variante `lsr#3` : l'ancien +4 était faux de
  +8 octets, invisibles en palette), write freq/res **−6** (calibrateur loader EL beam-syncé =
  Hatari EXACT) — **rustines +16/+4 retirées** ; **(4)** IRQ HBL à la frontière de ligne (512,
  `HBL_VIDEO_CYCLE_OFFSET=0`) au lieu de cpl−4. + 2 bugs structurels débusqués par l'oracle :
  **commit du compteur vidéo à DE_end** (une impulsion 60 Hz datée 376 arrivait APRÈS → `$8209`
  figé → **loader EL bloqué à jamais**) → commit PARESSEUX en tête de `renderLine` (≙ Video_EndHBL) ;
  **`Video_RestartVideoCounter` porté** (ligne 310/260 cycle 56, event `VC_RESTART`) : la base est
  relue à CET instant (après le handler VBL du jeu) — sans quoi le stabilisateur beam-sync d'EL
  lisait l'ancien buffer double-buffer toute la trame. **Validé** : poll-bench 180/180 byte-identique
  Hatari (2 variantes), loader EL réparé, bordure haute EL stable (38-40/40), **Lethal Xcess titre
  0,00 % de churn** (était ~1,5 %), étalons 19/0 + TOUS OK (`overscan_top` re-baseliné : l'ancienne
  référence était fausse de 24 px, vérifié pixel-à-pixel contre Hatari). Reste (pièce vidéo) : le
  moteur fullscreen d'EL verrouille à 72 % avec ses impulsions freq à −16 vs oracle → micro-sauts
  de scroll résiduels (cf. doc maître, bloc 2026-07-02).
- **Saut STOP : double comptage du quantum CORRIGÉ (5ᵉ passe, 2026-07-02) — lock moteur EL
  100 %.** Dans le chemin STOP de `Cpu68k::run` (modèle bloc), `setClock(jumpTo)` +
  `syncTo(jumpTo)` avançaient `sched.now_`, mais le retour `ran` (et `cyclesRunInQuantum`)
  mesuraient toujours depuis l'ANCIEN début de quantum → le `runTo(now+ran)` de Machine
  recomptait le saut : `sched.now()`/`liveNow()` prenait une avance **δ = 4..26 cyc** sur
  l'horloge CPU, stable jusqu'au STOP suivant. Toute la datation vidéo (beamClock, lectures
  `$8209`, écritures freq/res) était décalée de δ vs les créneaux bus — impossible sur le vrai
  matériel. Quand δ ≡ 2 (mod 4), le calibrateur beam-sync d'Enchanted Land déverrouillait
  (corrélation mesurée 546/546 lock à phase ≡0 vs 69/69 unlock à ≡2). Fix : **rebase de
  `quantumStartBus_/Clock_` après le saut**. Le taux de lock du moteur fullscreen passe de
  46,9 % à **100,0 %** (12402/12402 écritures freq à la position Hatari-exacte) — micro-sauts
  de scroll éliminés. Résout aussi deux mystères de la 4ᵉ passe (mêmes causes) : le **commit
  VBL ne casse plus le loader EL** → `NEOST_RAISE_COMMIT` **défaut 3 = HBL+VBL** (modèle
  fidèle Hatari CE complet), et `NEOST_IPLFETCH=1` ne casse plus le loader (reste opt-in).
  + **Broche MFP exacte portée** (`NEOST_MFP_EXACT`, défaut 3) : anti-datation du tic Timer B
  event-count (≙ `MFP_TimerB_EventCount` avec `Delayed_Cycles` — le délai de 4 cyc court
  depuis l'échéance TIMER_B servie, pas la frontière de bloc) + prise à la frontière courante
  quand le délai est déjà écoulé (≙ `MFP_ProcessIRQ`) — fidèle, non nécessaire au lock (A/B).
  Diagnostics gated ajoutés : `NEOST_FRAME_DIAG`, `NEOST_BUS_DIAG=<page-pc>`, champ `into=`
  dans `NEOST_VC_TRACE`. **Validé** : étalons 19/0 TOUS OK aux défauts finaux, loader EL OK,
  LX titre 0,00 % churn, SHO titre/menus propres (période HBL régulière, pas de double-prise).

## Types de machine & mémoire
- **Zone RAM « void » : on relit le dernier mot du bus de données** (port Hatari
  `VoidMem_bget/wget` → `regs.db`). Une lecture dans une banque absente / au-delà
  de la config MMU (< `$400000`) renvoyait 0 ; rien ne pilote le bus sur le vrai
  matériel → il garde sa dernière valeur. `Bus::cpuDb` est latché par les
  overrides mémoire de NeostMoira (mot = valeur, octet = dupliqué sur les deux
  voies, comme UAE `cpu_prefetch.h`) ; un accès mot void relit exactement
  `cpuDb` (octet fort à l'adresse paire), un accès octet l'octet faible
  (`VoidMem_bget`). Les fetches vidéo/blitter/DMA ne polluent PAS le latch
  (registre du CPU, pas du bus). Validé : sondage RAM EmuTOS intact
  (`phystop` exact pour 256k/512k/1m/2m/4m) et boots 60/50 Hz byte-identiques.
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
- **Ancre verticale du commit de scanline = fenêtre Glue LIVE — Lethal Xcess (STX) EN JEU
  réparé (2026-07-12)** : régression du commit au HBL ci-dessous. `commitScanline` /
  `endVideoLine` calaient la scanline committée sur `liveStartHBL_` (VDE_On **sticky** :
  passe à 34 sur TOUTE bascule 60 Hz avant la ligne 63 — même une paire 60/50 qui
  n'ouvre PAS réellement le haut, Hatari exigeant que le 60 Hz couvre la comparaison
  de fin de ligne 33). La calibration du loader de Lethal Xcess émet une telle paire à
  la ligne 32 de CHAQUE trame puis relit $FF8209 à la ligne 262 pour vérifier si son
  overscan a pris : le commit sur l'ancre sticky faisait avancer le compteur vidéo à
  travers les 29 lignes de bordure haute (160 octets chacune, `glueLineBytes` ignorant
  la fenêtre VERTICALE) → le poll lisait base+228 lignes au lieu de base+199 → le jeu
  croyait le haut ouvert → **écran en jeu déchiré** (HUD dupliqué mi-écran, bandes
  décalées, palettes croisées) puis chargements en échec. Fix : `commitAnchor()` =
  `max(liveStartHBL_, glueStartHBL_)` sur trame à écritures freq/res (la machine Glue
  LIVE, port fidèle de `nStartHBL`, fait foi), **latché au 1ᵉʳ commit** de la trame
  (une re-fermeture tardive ne ré-indexe pas `lineSnap_`/`vcLineY_` en cours de trame
  — verrou Cuddly conservé) ; reset à `beginFrame` ; sérialisé (save-states → **v6**).
  Un vrai retrait haut (EL) donne 34 des deux côtés → chemin EL inchangé. Validation :
  in-game LX pixel-identique au commit pré-régression sur toutes les trames sondées
  (22000–25900, mêmes entrées), flux de lectures $FF8209 byte-identique sur 2 900
  trames (~3 M lectures), oracle Hatari en jeu conforme (Xvfb + xdotool, disque B
  monté — le jeu ne démarre qu'avec le disque 2 : titre → SPACE → music select → FEU
  → briefing → FEU tenu) ; suite `run_all --tier full` verte, sprites EL en jeu
  toujours présents (100/100 trames), `--save-state-test` déterministe. Diag ajouté :
  `NEOST_VC_TRACE=1` trace chaque lecture du compteur vidéo (fc/ligne/valeur).
- **Commit de scanline au HBL — sprites d'Enchanted Land EN JEU réparés (2026-07-12)** :
  `Shifter::commitScanline`, appelé depuis `Machine::onHbl` = port de l'appel
  `Video_EndHBL` du handler HBL d'Hatari (video.c:3319). La capture par-ligne au
  faisceau (`lineSnap_`) se fait désormais à la fin de la ligne MÊME (~cycle 512),
  plus au RENDER (cycle 376) de la ligne active suivante : ce commit paresseux datait
  la capture ~380 cycles trop tard — et en overscan HAUT, la grille RENDER restant
  ancrée sur `dispStartLine=63` alors que l'affichage démarre à 34, les captures
  traînaient de **29 lignes entières** (~15 000 cycles). Le moteur d'EL efface ses
  sprites logiciels en CHASSANT le faisceau (buffer UNIQUE, effacement trame paire /
  retracé trame impaire — mesuré aux dumps RAM `--dump-at`) : les lignes du sprite
  étaient capturées APRÈS l'effacement → **sprite invisible ou tronqué selon sa
  position verticale** (héros absent pendant les sauts, mèche de quelques lignes
  seule visible). Étendu au-delà de `curAH_` aux lignes affichées par la machine
  Glue LIVE (bordure basse retirée : sans capture, la bande basse retombait sur la
  RAM de fin de trame — même artefact pour un sprite en bas d'écran). Oracle Hatari
  (crack Hotline, Xvfb + xdotool + `--joystick 1`, machine à états sur la fenêtre
  live) : à l'arrêt le héros est affiché à CHAQUE trame (130-135 px) — NeoST
  post-fix idem après le transitoire de verrouillage du moteur (~50 trames au
  démarrage du jeu, alternance 1/2 puis stable, même famille que le stabilisateur
  beam-sync d'EL). Repro : `--joy-script` feu → SPACE au titre → sauts `U` ; suite
  `run_all --tier full` verte (étalons pixel, oracle, cycle-bench inchangés).
- **Latch couleur de bordure GAUCHE = registre 0 de la ligne PRÉCÉDENTE (2026-07-09)** :
  résidu du beam-sync EL — la bordure gauche de la ligne N prend la couleur `palette[0]`
  telle qu'elle était en fin de ligne N−1 (la droite garde la couleur courante). Aligné
  sur le matériel (le Shifter latche la couleur de fond au bord gauche avant le 1er accès
  vidéo de la ligne). Invisible aux étalons croppés sur l'aire active ; validé contre oracle
  frais (`make_poll_test` aire active byte-exacte des deux côtés). Complète le beam-sync EL
  (poll `$8209` byte-identique NeoST↔Hatari).
- **Ports de LECTURE résolution `$FF8260/$FF8261` fidèles (2026-07-09)** : port
  `Video_Res_ReadByte` (video.c) — `$FF8260` relit les bits de résolution corrects (pas la
  valeur brute écrite), `$FF8261` est l'alias attendu. Les jeux/démos qui relisent le
  registre de résolution voient la même chose que sur matériel.
- **Espace registres blitter `$FF8A3E/$FF8A3F` = zone void (2026-07-09)** : lit 0xFF,
  écritures ignorées (fin de la carte des registres du BLiTTER — port du comportement
  Hatari). Un accès parasite dans ce trou ne fait plus rien d'anormal.
- **V2 — tricks par changement de RÉSOLUTION portés (2026-07-08)** : `updateGlueRes`
  (post-traitement de chaque écriture $FF8260 après la machine Glue commune) = port de
  `Video_WriteToGlueRes` (video.c:1637-1753). Détections : **overscan MED-RES** — retrait
  gauche hi/lo suivi d'une bascule MED aux cycles 20/36 (No Cooper) ou 28 (PYM) → la ligne
  overscan est en MOYENNE résolution (masque `OVERSCAN_MED_RES` + décalage source en octets
  dans les bits 20-23) ; **hi→med tôt** → `LEFT_OFF_MED` (+26 octets) ; **variante STE**
  `LEFT_OFF_2_STE_MED` (+20 o, −16 px) ; **stab med** (hi/med/lo@16) ; **scrolls « hardware »
  droite 13/9/5/1 px** (deux familles de fenêtres). Rendu **multi-résolution PAR LIGNE**
  (≙ `Video_StoreResolution`) : les lignes med d'une trame basse rés se décodent en 2 plans,
  base de décodage décalée de (2−champ) **octets** — piège débusqué : le stride overscan
  (186 o) n'est pas multiple de 4, l'appariement des mots de plans dépend de l'origine OCTET,
  un décalage d'index pixel « rayait » le logo une ligne sur deux — émission = MOYENNE des
  2 px med par colonne (même réduction 2× que l'oracle → les DEUX phases med comptent),
  calage émission −4 px med calibré à l'oracle. **VALIDÉ : écran greetings No Cooper
  PIXEL-IDENTIQUE (0 px) à l'oracle Hatari** — nouvel étalon `nocooper_greetings` RÉFÉRENCÉ
  SUR L'ORACLE (`tests/reference/nocooper_greetings.png`, max_diff 0, recette 5 espaces datés
  — support liste `keys_at` ajouté au runner). Selftest : 4 scénarios V2 (31 ST / 34 STE).
  Étalons TOUS OK, spectrum/Cuddly byte-identiques. L'« écart 891 px » de l'écran principal
  était un artefact de phase de touche (0 px à sa phase, texte scrollé 2 px/trame). Restent
  documentés : hardscroll 4 px Paulo Simoes (med@84, pointeur vidéo par ligne), hacks TEMP
  Closure/DOLB (reniflage PC chez Hatari), alias $FF8261, med 640 natif en trame mixte.
- **Canal « longueurs de ligne par-ligne » ACTIVÉ par défaut (2026-07-08)** : le port
  `HBL_Pos/nCyclesPerLine` (chaque « Freq_match » de la Glue fixe la position de l'IRQ HBL et
  la longueur — 224/508/512 — de la ligne courante, cumul `lineCarry_` pour les suivantes)
  était complet mais gated opt-in `NEOST_LINELEN`. Débloqué par le tranchage WS3 (les
  positions Hbl_Pos de la table sont désormais résolues), validé : étalons TOUS OK, Cuddly-ST
  190/250 vs oracle (identique au canal OFF), A/B interne EL/spec512 0 px, cloche STE
  bit-identique. `NEOST_LINELEN=0` pour désactiver (A/B). LX à re-vérifier au premier disque.
  Prérequis pratique de V2 (lignes hi-res 224 par-ligne).
- **V1 — branche STE de la Glue portée (2026-07-08)** : la machine Glue n'appliquait que le
  chemin STF sur toutes les machines. La phase 1 (freq avant DE_start) est désormais PAR
  MACHINE (`glue::Timing` : table STF WS3 / table STE), port de la branche STE de
  `Video_Update_Glue_State` (video.c:2444-2651) — le GST MCU du STE teste les positions de
  PRELOAD du MMU (36/40 : le shifter charge 16 cyc avant DE), `Line_Set_Pal` 56, HSync
  −52/−12, RemoveBorder 500, et n'a PAS le latch res à −1 cyc du GLUE STF (video.c:2224).
  Nouveau trick : **LEFT_OFF_2_STE** (retour lo-res PILE au cycle 4 → retrait gauche COURT,
  +20 octets, DE_start 16, écran décalé de 8 px). Selftest : 3 cas STE ajoutés (22/22 sur
  STE, 19/19 sur ST). **Validé** : étalons TOUS OK, chemin ST strictement inchangé
  (Cuddly-ST 0 px vs oracle) ; le menu Cuddly lancé sur machine STE **casse désormais comme
  le vrai STE** (couleurs faussées, hachures, scroller corrompu) — **196/250 trames
  pixel-identiques à l'oracle Hatari-STE** (le reste = glissements de phase du clignotement
  bistable de la casse, sensible au cycle de la touche). Avant : NeoST-STE rendait le menu
  « parfait » (timings STF partout), divergence documentée du 2026-07-03 résorbée.
- **Wakeup state STF TRANCHÉ : WS3 complet (2026-07-08)** — fin de l'« hybride WS1/WS3 »
  (positions Glue WS1 + HBL 508 + VBL 64) documenté dans `docs/HATARI_DIVERGENCES.md`. NeoST
  adopte le défaut de l'oracle (`VIDEO_TIMING_DEFAULT = WS3`, video.c:624), qui partage avec le
  STE l'IRQ HBL à cpl : positions horizontales de la Glue **+1** (`glue::kWsInc` — fenêtres de
  tricks, DE stockés, RemoveBorder 503, HSync −49/−9, canal Hbl_Pos 512/508/224), **IRQ HBL à la
  frontière de ligne** (`kHblOff` 0 ; 512 en 50 Hz comme l'oracle ET comme le STE), VBL 64
  (inchangé). Découverte structurante dans le code Hatari : compteur vidéo
  (`Video_CalculateAddress`), `spec512.c`, copie écran et Timer B par défaut utilisent les
  constantes **FIXES** `LINE_START/END_CYCLE_*` (video.h:91-95) HORS table wakestate → les
  ancres de rendu NeoST (56/376) restent fixes, les DE stockés (table WS, ≙ `ShifterLines`)
  sont re-normalisés −inc au rendu, et les datations read −6 / write +2 / spec512 −25 sont
  **inchangées** (fidèles-théoriques, WS-indépendantes — zéro recalibration). `NEOST_WS=1..4`
  pour A/B (WS1 = HBL cpl−4 + VBL 60). **Validé** : glue-selftest 19/19, étalons TOUS OK,
  boot STF 50 Hz **0 px** vs oracle, menu Cuddly **pixel-identique à la baseline** (190/250
  trames à 0 px vs oracle Hatari fifo-piloté, le résidu = phase d'anim des drapeaux due à la
  latence de touche oracle, identique baseline), flicker spec512 inchangé, son STE : cloche
  bit-identique (fetch FIFO suit le HBL déplacé, 0,003 % sur l'étalon DMA — voulu). Débloque
  V1/V2 (la branche STE d'Hatari se portera telle quelle) et `NEOST_LINELEN`.
- **Wait-state de la lecture du compteur vidéo `$FF8205/07/09` (+2 cyc, valeur d'abord)** —
  `Shifter::read8` échantillonne la valeur du compteur AU CYCLE D'ACCÈS (façon Hatari
  `Cycles_GetCounterOnReadAccess`) PUIS retarde le CPU d'un wait-state FIXE de **+2 cyc bus**.
  Mesuré à l'oracle Hatari sur DEUX jeux qui pollent `$FF8209` en boucle serrée pour caler
  leurs effets raster : **Lethal Xcess** (`$14ef6 : move.b $8209,d0 / beq`) = 24 cyc/itér
  chez Hatari vs **22** sans wait ; **Enchanted Land** (`$ee78`, sync-scroll) = 20 vs **18**.
  L'**ordre** est crucial (valeur AVANT le wait) : retarder le CPU sans fausser la valeur lue
  → étalons spec512 & overscan_top `--max 0` **pixel-exact inchangés**. C'est un wait-state
  FIXE, ≠ l'align-4 variable des registres couleur/résolution (`syncCpuBus`) qui, lui,
  jitterait et casserait EL. **Effet** : Lethal Xcess (STX) ne deadlocke plus sa calibration
  fullscreen (l'avance compteur atteint enfin `0xbe`=190, `$14ef6` ~94k iters ≈ Hatari) et
  **démarre** (écran-titre) ; Enchanted Land atteint son jeu au lieu de se bloquer. Diag :
  `NEOST_VC_TRACE=1` (= format `--trace video_addr` de Hatari), override `NEOST_VC_WAIT`.
  *Reste* (séparé) : le rendu cycle-exact des splits per-ligne (« beam-sync » : l'image saute
  trame à trame en jeu — commun à Lethal Xcess en jeu / Enchanted Land / Cuddly Demos), cf.
  TODO §Bordures.
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
    (`etalons.json`), conformes à l'oracle Hatari ligne par ligne. Depuis le 2026-08-19 la
    référence EST l'oracle (`ref_kind: oracle`, PNG Hatari 832×552 commis, ROM EmuTOS) :
    NeoST y était déjà à 0 px, la self-capture n'ajoutait rien.
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
    `disks/etalons/spectrum_512_auto_diapo.st` (auto sous TOS 1.00) → les **4** images spec512
    (**BEE512** l'abeille, **sun** dégradé, **PLANET** sci-fi, **cougar** photo) diffent à
    **0 px** vs Hatari (zone active 320×200, `compare -metric AE`). Méthode : diff pixel par
    image figée (les 9552 écritures palette/trame matchent Hatari écriture-par-écriture, Δcyc
    constant absorbé par `kSpec512AlignCyc`). À l'ancien `−24` il restait 122/54/210/319 px
    (frontières décalées d'1 px). Gaté par le seuil → **zéro régression** (EmuTOS/jeux normaux
    byte-inchangés ; tos104us, Enchanted Land vérifiés). Outils : `--shot-every N PREFIX`,
    `--screenshot`, `NEOST_SPEC512_TRACE`, `NEOST_VC_OFF` (sweep oracle ; l'ancien
    `NEOST_ALIGN_OFF` a disparu — l'alignement est figé dans `kSpec512AlignCyc`),
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
- **Bascule software→automatique (VR bit3 1→0) efface les in-service ISRA/ISRB**
  (port `MFP_VectorReg_WriteByte`) : sinon un in-service resté posé bloquerait pour
  toujours les IRQ de priorité inférieure. EmuTOS (mode software-EOI, bit3=1) inchangé ;
  corrige les handlers/replays qui rebasculent en EOI automatique en cours de route.
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
- **ACIA MIDI alignée sur le 6850** : le master reset ne **purge plus** la file de réception
  (l'octet en transit est conservé, cf. note Hatari « don't clear bytes in transit ») et la lecture
  du registre de données à vide renvoie le **dernier octet reçu** (RDR persistant) au lieu de `0x00`,
  comme l'ACIA clavier. Cf. `docs/HATARI_DIVERGENCES.md` (MIDI). **Bit RDRF distinct de la file** :
  le master reset efface RDRF (SR → TDRE seul, cf. `ACIA_MasterReset`) **sans purger** la file →
  l'octet reste relisible et RDRF retombe correctement (cf. §2ᵉ passe, M-MIDI).
- **Analyseur de commandes multi-octets** (table de longueurs + buffer d'accumulation).
- **Tampon de sortie IKBD borné à 1024 octets** (port `IKBD_OutputBuffer_CheckFreeCount`,
  SIZE_KEYBOARD_BUFFER) : chaque émetteur de paquet (souris/clavier/joystick) teste la place
  AVANT son 1er octet → paquet jeté ENTIER si plein (jamais de demi-paquet) ; `pushRx` borne
  en dernier ressort les octets isolés (réponses de commande, quirks). Reproduit la saturation
  du vrai HD6301 (vieux paquets perdus, pas de retard qui s'accumule) et borne la mémoire.
  Hors saturation : comportement strictement identique (boots pixel-identiques).
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
  `--joy-script N "SCRIPT"` / `--joy-script-file N FICHIER` (état joystick par trame) pour
  piloter des menus de jeux ; `stScancode` étendu (flèches `<>[]`, Esc `=`, F1-F5 `!@#$%`…).
  Le script compile vers **un masque par trame** (`src/util/JoyScript.hpp`, couvert par
  `neost-selftest`) : `U/D/L/R/F/.`, **combinaisons** `[UF]`/`[DL]` (feu + direction,
  diagonales — sans quoi ni le tir ni la dynamite de Rick Dangerous ne sont exprimables),
  masque brut `[$88]`, répétition `TOKEN*N`, total borné à 10 M de trames. Un script
  fautif est refusé avant le boot au lieu d'être traduit en « neutre ».
- **Pilotage externe déterministe** (headless, cf. `docs/OPENDST.md`) :
  `--probe NOM=ADR:LEN` (répétable, 1/2/4 octets big-endian), `--probe-every N`,
  `--hash-ram ADR:LEN` → une ligne `probe frame=… screen=<hash> ram=<hash> NOM=0x…` par
  échantillon sur **stdout** (les journaux restent sur stderr). Lecture `Bus::peek8` :
  **aucun effet de bord**, donc l'espace I/O se lit `$FF`.
- **Mode serveur** (`--server`) : boucle de commandes texte sur stdin/stdout — `run N`,
  `play SCRIPT`, `joy`, `key`, `mouse`, `peek`, `observe`, `save`/`load` sur des
  emplacements d'état **en mémoire**, `export`/`import`, `probe`, `shot`, `slots`.
  `run`/`play`/`load`/`observe` répondent avec les champs d'observation : un rollout = UN
  aller-retour. Équivalence avec la boucle `--frames` vérifiée au palier `fast`
  (`tools/run_server_equiv.py`), verdict MUTATION-TESTÉ.
- **Point d'entrée unique** `tools/opendst.py` (menu : `server`, `memdiff`, `explore`,
  `oracle`, `compile`, `equiv`, `hatari`, `doc`) et **`tools/opendst_memdiff.py`** : trouver
  les variables d'un jeu en RAM par diff d'états pilotés par le serveur (candidats = « change
  avec l'entrée » − « change avec le temps »), 512 Ko en 0,4 s. Spécification du protocole
  serveur, indépendante du langage, dans `docs/OPENDST.md` § 5.
- **Save-states plus rapides** : CRC-32 par table (il était calculé bit à bit) et filet de
  `loadState` sérialisé sans CRC — reprise 21,6 → 3,9 ms, sauvegarde 10,2 → 2,6 ms
  (mesuré sur la lignée précédente), à format et valeurs INCHANGÉS. Le GUI (F7/F8) en
  profite autant.
- **Oracle différentiel NeoST↔Hatari** (`tools/opendst_oracle.py`) : le même script
  d'entrées rejoué des deux côtés, images comparées, décalage de boot mesuré en deux
  passes. Repose sur `tools/hatari_neost_oracle.patch` (script joystick daté par VBL +
  graine `HATARI_SEED`) et sur `--joy-script-compile`. Client Go-Explore d'exemple :
  `tools/opendst_explore.py`.

## Disquette (FDC WD1772 + DMA)
- **WRITE TRACK (formatage) sur image .ST** : le flux MFM écrit par le programme
  (via DMA) est PARSÉ en deux passes — extraction des secteurs (IDAM $FE →
  piste/face/secteur/taille, DAM $FB/$F8 → 512 o) puis, si la géométrie est
  STANDARD et complète (taille 512, secteurs 1..spt, compte == spt, piste/face
  dans l'image), écriture tout-ou-rien dans l'image + `writeBack`. Géométrie non
  standard → **LOST_DATA sans rien écrire** (limite assumée : une .ST ne
  représente qu'un format standard ; Hatari, lui, refuse TOUT write track sur
  .ST — `FDC_WriteTrack_ST` = TODO). Validé headless par programme 68k en
  secteur de boot (XBIOS Flopfmt) : format piste 4 spt=9 → piste remplie du
  motif « virgin » $E5E5, D0=0 ; Flopfmt spt=11 sur image 9 spt → D0=−16
  (EWRITF), image byte-identique (md5).
- **Écritures STX persistées en fichier compagnon `.wd1772`** (port stx.c :
  `STX_WriteDisk`/`STX_LoadSaveFile`, format **byte-compatible Hatari** — en-tête
  « WD1772 » v1.0 + blocs SECT/TRCK, multi-octets BE) : les 'write sector' (overlay
  par secteur, champs ID inclus) et 'write track' (flux brut de la piste) sont
  écrits AU FIL DE L'EAU dans `<image>.wd1772` (Hatari ne sauve qu'à l'éjection)
  et **restaurés au montage** (association SECT→secteur par piste/face/bitPosition,
  TRCK→piste). `Fdc::writeTrackStx` porté (`FDC_WriteTrack_STX`) : conserve le flux
  écrit, invalide les overlays secteur de la piste (le write track prime) ; comme
  Hatari, la piste réécrite n'est pas ré-interprétée en lecture (TODO partagé).
  L'image `.stx` d'origine n'est JAMAIS modifiée. Validé headless (boot 68k XBIOS) :
  Flopwr $CAFE → `.wd1772` créé (bloc SECT), remontage → « écritures restaurées »,
  Floprd → motif relu ; Flopfmt → bloc TRCK, md5 de la `.stx` inchangé.
- **Disquettes HD 1,44 Mo (et ED) + porte de densité Mega STE** (port `fdc.c` :
  `FDC_ComputeFloppyDensity`/`FDC_TransferByte_FdcCycles`/`FDC_CanMachineHandleDensity`).
  La densité du média est DÉDUITE de la géométrie (18 spt → HD, 36 → ED ; longueur
  réelle de piste pour les STX, marges ×1,5/×3) et rafraîchie au montage, à la
  sélection lecteur/face et à chaque pas de tête. Le WD1772 reste à 8 MHz : le débit
  MFM est divisé par le facteur (DD 256 cyc/octet, HD 128), la piste porte 6268 ×
  facteur octets, la rotation (300 tr/min) ne change pas — `transferDelay()` partout
  (recherche d'ID, gaps, CRC, write, position depuis l'index). Sur **Mega STE**, le
  registre `$FF860E` (octets haut/bas corrects, routé Mega STE seulement, cf. patch
  Hatari `IoMem_FixAccessForMegaSTE`) doit être ACCORDÉ à la densité du média, sinon
  champ ID introuvable → RNF (type I verify, read/write sector, read address),
  bruit (read track) ou LOST_DATA (write track) — fidèle `FDC_CanMachineHandleDensity` ;
  ST/STE acceptent tout (convenance, comme Hatari). DIP `$FF9200` déjà à 0xBF
  (« lecteur HD présent »). Validé headless : image 1,44 Mo FAT12 lue sur STE et
  Mega STE (entrées de répertoire vérifiées en RAM, 0 RNF) ; oracle Hatari confirme
  qu'EmuTOS 256 programme `$FF860E=3` puis lit, et la séquence d'auto-détection sur
  média DD (sonde HD → 1 RNF → bascule DD) est reproduite ; **non-régression DD
  byte-identique** (boot 60/50 Hz, Arkanoid, Enchanted Land — facteur 1 ⇒ délais inchangés).
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
  (écriture en overlay mémoire). Position angulaire via `BitPosition` (1 bit = 32 cyc
  en DD, **÷ densité** pour HD/ED — cf. ci-dessous), rotation par piste (`cyclesPerRev`
  dérivé de la longueur réelle). Débloque les jeux **PROTÉGÉS** : ✅ **Dungeon Master**
  (fuzzy bits), **Stunt Car Racer**, **Tower of Babel**, Golden Axe, Chessmaster,
  **Rick Dangerous** (secteur à erreur CRC volontaire), Tower Toppler, Eliminator…
  (séquence de lecture identique à Hatari, vérifiée à l'oracle).
- **STX — densité HD/ED + ré-interprétation WRITE TRACK** (deux finitions du chemin
  Pasti, *au-delà de Hatari*) :
  - `nextSectorIDStx` exprimait `BitPosition` et la taille de piste en cellules **DD
    brutes** (32 cyc/bit, 256 cyc/octet — exactement comme Hatari). Or `cyclesPerRev`
    et `indexCurrentPosCycles` suivent déjà le débit du média (÷ densité). Sur une image
    **HD/ED** (2×/4× plus de bits par tour), les positions débordaient donc le tour d'un
    facteur 2/4 → champs ID jamais alignés. Corrigé : conversion bit→cyc et octet→cyc à
    la densité courante (`MFM_BIT/dens`, `MFM_BYTE/dens`). En **DD (dens=1) : valeurs
    inchangées** (32/256) → **non-régression byte-identique** (Tower Toppler, Eliminator,
    Rick Dangerous, Bubble Ghost re-vérifiés à l'écran).
  - **WRITE TRACK ré-interprété en LECTURE** : Hatari conserve le flux écrit
    (`pDataWrite`) mais laisse `pDataRead` à `NULL` (TODO « convert pDataWrite into
    pDataRead ») → les lectures voient toujours l'original. NeoST PARSE désormais le flux
    (IDAM `$FE` → champ ID, DAM `$FB`/`$F8` → données) en secteurs lisibles
    (`StxImage::reinterpretSaveTrack`, vue active `Track::sectorsView`) → **les lectures
    voient le nouveau contenu**. Reconstruit aussi au rechargement d'un `.wd1772`. Le flux
    brut reste la source persistée. Validé : `tests/stx_writetrack_test.cpp` (flux forgé de
    3 secteurs relus à la place des 10 d'origine, données octet-exactes, round-trip
    sauvegarde→rechargement `.wd1772`).
  - **Rick Dangerous.stx** (signalé « plante après titre ») : en fait **fonctionne** —
    le rapport était périmé (le test headless n'injectait pas d'entrée). Chargement des
    pistes 0-48 puis re-lecture de protection (piste 0, secteur 6 : **erreur CRC
    volontaire + offset de données chevauchant**, correctement émulée) → **écran-titre
    « RICK DANGEROUS »** (SPACE) → **jeu** (feu). Le secteur-protection à CRC erronée est
    rendu fidèlement (statut bit 3, données lues, RNF absent).
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

## Disque dur GEMDOS (émulation HD façon Hatari)
- **Port complet de `gemdos.c`** (`io/GemdosHd.{hpp,cpp}`) : un dossier hôte est
  monté comme lecteur **C:** (multi-partitions C..Z si le dossier ne contient que
  des sous-dossiers d'une lettre) en INTERCEPTANT les appels GEMDOS (trap #1) et en
  les redirigeant vers le système de fichiers hôte (POSIX). Activé par `--gemdos DIR`
  (headless) ou `NEOST_GEMDOS_DIR` (GUI). Exclusif d'une cartouche externe.
- **Mécanisme d'interception fidèle à Hatari** : une CARTOUCHE système (octets
  assemblés de `cart_asm.s`/`cartData.c`) est exposée à `$FA0000` ; au boot le TOS
  exécute son C-INIT (`sys_init`) qui installe un nouveau vecteur GEMDOS (`$84`)
  pointant dans la cartouche et ajoute C: au masque `_drvbits` (`$4C2`). Le code
  cartouche déclenche des **opcodes « illégaux » magiques** (8 = GEMDOS, 9 = PEXEC,
  10 = SYSINIT) captés par le cœur Moira **avant `execute()`** (`Cpu68k::run`) : le
  handler C traite l'appel, pose les codes condition N/Z/V du SR, puis l'opcode est
  remplacé par un NOP consommé par `execute()` (port de `OpCode_*` + `CpuDoNOP`).
- **Appels portés 1:1** : Dsetdrv/Dfree/Dcreate/Ddelete/Dsetpath/Dgetpath,
  Fcreate/Fopen/Fclose/Fread/Fwrite/Fseek/Fdelete/Fattrib/Fdatime/Frename,
  Fsfirst/Fsnext (DTA + cache circulaire), Fforce, et **Pexec** (création de
  basepage par le TOS via la cartouche puis chargement+relocation du PRG depuis C:
  par `GemDOS_LoadAndReloc`, démarrage par un Pexec « just-go »). Table de handles
  internes (base 64), traduction de chemins ST↔hôte avec correspondance 8+3
  insensible à la casse, gestion `..`/`.`, jokers.
- **Validé headless** (EmuTOS 192 Ko, ST) : bureau affichant « GEMDOS drives: ABC »
  et une icône **DISK C** de disque dur ; un PRG dans `C:\AUTO\` est **lancé via
  Pexec** au boot, lit `C:\HELLO.TXT` et écrit `C:\OUTPUT.TXT` côté hôte à
  l'identique (Pexec + Fopen + Fread + Fcreate + Fwrite + Fclose). Sans `--gemdos`,
  boot inchangé (« GEMDOS drives: AB »). Helpers mémoire : `Bus::hostRamPtr`
  (pointeur RAM contigu, traduction MMU) et `Bus::tosVersion`.
- **`Fsfirst`/`Fsnext` énumèrent « . » et « .. » en sous-répertoire** (paramètre `subdir` de
  `fsfirst_match`, port Hatari) : à la racine d'un lecteur GEMDOS les `.*` restent ignorés, mais
  dans un sous-dossier `.`/`..` sont retournés comme sur TOS réel → gestionnaires de fichiers /
  archiveurs récursifs corrects.
- **`DESKTOP.INF` / `NEWDESK.INF` par défaut → `.TOS` lançable au bureau (2026-07-09)** :
  TOS 1.x (192/256 Ko) n'enregistre les `*.TOS` comme exécutables que si un `DESKTOP.INF`
  les déclare (`#F 03 04 *.TOS@`) ; TOS 2.x utilise `NEWDESK.INF`. Sans INF, double-cliquer
  un `.TOS` propose « imprimer ou voir » — comportement FIDÈLE (Hatari n'injecte l'INF que
  sur `--auto`/`--tos-resolution`). NeoST livre donc `gemdos/DESKTOP.INF` (TOS 1.x) et
  `gemdos/NEWDESK.INF` (TOS 2.x), templates Hatari, libellés FR, format exact (CRLF, `\032`
  de DESKTOP absent de NEWDESK). Vérifié : 1.62 lance un `.TOS` au double-clic ; 2.06
  applique l'INF et lance (autostart `#Z` en headless). Une ligne `#Z 01 C:\JEU.TOS@`
  démarre un jeu directement (attract / kiosk).
- **Résolution de chemin — passe « caractères invalides » (`only_invalid`)** (`addPathComponent`,
  port `add_path_component`/`Str_Filename_Invalid_Char`) : la conversion nom GEMDOS → chemin hôte
  fait désormais DEUX passes de masque séparées comme Hatari — d'abord la **troncature** 8+3
  (`*`, les `+` restant littéraux), puis les **caractères invalides** (`+`→`?`). Dans cette
  seconde passe un `?` ne matche QUE des caractères réellement invalides pour un nom Atari
  (`filenameInvalidChar`), au lieu de n'importe quel caractère → plus de risque d'ouvrir/écraser
  le mauvais fichier hôte quand un nom contient `: ? \ /` ou des points en trop.

## Audio
- **Filtre de sortie YM par machine câblé** (`Machine` → `YM2149::setStfLowPass(!STE)`) : ST/Mega ST
  passent par le passe-bas analogique STF (`applyLpfStf250`, condensateur C10), STE/Mega STE par le
  PWM (front montant passe-tout) — comme `Sound_Update_Filters` (sound.c). Le code STF existait mais
  n'était jamais activé (toutes machines en PWM). Cf. `docs/HATARI_DIVERGENCES.md` (S1).
- **Sortie STÉRÉO + panoramique LMC1992 + son DMA horodaté** (`src/audio/Audio.{cpp,hpp}`,
  `src/core/DmaSound.{cpp,hpp}`, `YM2149.{cpp,hpp}`) — la chaîne native passe en **2 canaux
  entrelacés** : le son DMA STE conserve sa vraie image **L/R** (au lieu d'être moyennée) et
  les gains gauche/droite du LMC1992 réalisent le **panoramique** (`mixStereo`, `gainLeft/
  Right`, `applyToneStereo`). En plus, les transitions PLAY/STOP du DMA sont **horodatées au
  cycle** (`setCycleClock`/`recordEvent`) et rejouées par segments (modèle « push » du YM) →
  un **bruitage one-shot court** intra-trame n'est plus avalé et la **queue d'un sample** n'est
  plus écrêtée par l'effacement CPU du bit PLAY. Le PSG gagne aussi le **read-latch `$FF8800`**
  et le **strobe Centronics** (port B). Chemin mono WASM inchangé. Boot ST/STE non régressé.
- **Bit PLAY ($FF8901) auto-effacé en fin de trame DMA one-shot** dans le MOTEUR DMA
  (fin de trame détectée AU FETCH dans `DmaSound::fifoRefill`, port
  `DmaSnd_EndOfFrameReached` dmaSnd.c:510) et plus
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
  4 trames après une pause), l'affichage saute les trames intermédiaires. Depuis le
  2026-08-14, **VSync ON** : le sommeil vise désormais `emuNext` en absolu et ne
  s'ajoute donc plus au blocage du swap ; cela supprime le tearing horizontal sans
  ralentir le temps émulé. Sommeil plafonné à 20 ms (GUI réactif). **Mesuré : 0 underrun
  en 20 s** (contre ~6/s avant). Diagnostic pérenne : compteur d'underruns atomique +
  message stderr avec la cadence observée (`[Audio] underrun anneau … trames/s`) —
  un son haché s'auto-explique désormais dans la console. Après un underrun, la
  ré-amorce est courte (~20 ms, au moins un bloc backend) au lieu de réimposer le
  coussin complet de 85 ms qui pouvait masquer une percussion ou une note entière.
- **YM2149** : 3 voies carrées + bruit, enveloppe (R11-13, formes via Continue/Attack/
  Alternate/Hold), vitesse d'enveloppe corrigée (diviseur de pas). Backend miniaudio (CoreAudio).
  **`YM2149::reset()`** remet tous les registres à 0 (volumes 0 = SILENCE) et est appelé par
  `Machine::reset()/hardReset()` → le son ne PERSISTE plus après un reset (soft/hard), qui
  laissait sinon une tonalité bipée. Port A (R14) remis à `0xFF` au reset (lignes I/O actives
  bas toutes inactives, cf. `psg.c:223`) → plus de sélection lecteur/face parasite au boot.
- **DAC non linéaire + porte ton/bruit + filtres de sortie** (port fidèle de Hatari `sound.c`,
  suite à l'analyse comparative de l'époque, `SOUND_HATARI_DIFF.md` — doc
  depuis supprimé, verdicts repris dans [`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md)). Trois corrections dans `synthesize` :
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
    quasi-vide en underrun permanent où seules les transitoires passent). Après le premier départ,
    un underrun ne ré-amorce plus qu'environ 20 ms (au moins un bloc backend), ce qui évite de
    masquer une note entière ; le producteur calibre le nombre d'échantillons par report fractionnaire + un
    asservissement proportionnel (|adj| ≤ 8 ech., < 0,8 % de hauteur) qui remplit vite à l'amorçage
    et recale ensuite. Latence régime ~80 ms, stable. **Validé à l'oreille sur _Magic Pocket_.**
  - Le modèle push n'est armé que si le frontend pose l'horloge (`setCycleClock`). **Les TROIS
    frontends le posent désormais** : GUI, headless (`--sound-dump`) et WASM (2026-08-11 — il
    était resté sur `synthesize` direct, ce qui aplatissait tous les samples). _Reste
    (refinement) : FIFO 8 octets du DMA remplie sur HBL (cf. [`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md))._
  - **Chaîne de mixage UNIQUE** (`core/AudioMix.cpp`, 2026-08-11) : YM horodaté → DMA STE
    horodaté → HPF → gains/tonalité LMC1992. Elle était recopiée dans les trois frontends, et
    c'est la copie web qui avait dérivé. Extraction prouvée neutre : `--sound-dump` avant/après
    donne des WAV identiques au bit près.
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
- **Fidélité DMA STE** (port de `dmaSnd.c`, suite à l'ancien `SOUND_HATARI_DIFF.md`, supprimé —
  cf. [`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md)) :
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
- **Son de la machine en WASM — modèle « push », stéréo** (2026-08-11) : le cœur produit
  le son PAR TRAME ÉMULÉE (après `runFrame`, chaîne `core/AudioMix.cpp`) et le remet à la
  page (`neost_audio_open/close/set_queued`, `neost_set_volume`) ; sortie par **AudioWorklet**
  (mixage sur le thread audio, insensible aux à-coups du thread principal) avec repli
  automatique sur `ScriptProcessorNode`. File d'attente à coussin de 90 ms, amorçage,
  asservissement de débit (±8 ech./trame) sur la profondeur de file renvoyée par la page,
  garde-fous anti-enflement quand le contexte est suspendu, curseur de volume (rampe
  anti-clic côté cœur). **Avant** : synthèse mono NON horodatée tirée par le nœud audio →
  digidrums et bruitages courts aplatis (« les samples ne s'entendent presque pas »).
  Étalon dédié : `tools/make_digidrum_test.py` (carré ~997 Hz écrit dans le registre de
  volume à 7 979 Hz — un frontend non horodaté ne peut pas le rendre).
- **Imprimante Centronics** (port de `psg.c:388-390` + `printer.c`) : sur chaque FRONT de strobe
  (PSG port A R14 bit5), l'octet du port parallèle (port B R15) est **capturé dans un fichier hôte**
  (`Machine::setPrinterFile`, option headless `--printer FILE`) et la ligne **BUSY (GPIP0)** est
  assertée bas comme le vrai handshake. No-op tant qu'aucun fichier n'est ouvert (défaut inchangé).
  Validé : mini-ROM imprimant « NeoST\n » via le protocole Centronics → fichier capturé identique.

## Bus error & cartouches de diagnostic
- **Miroir matériel du YM2149** : tout `$FF8800-$FF88FF` (et plus seulement `$FF8800-03`) est routé
  vers le PSG (décodage `addr & 3`, wait-states inclus), comme le shadow PSG d'Hatari (`ioMem.c`
  `IoMem_Init`). Cf. `docs/HATARI_DIVERGENCES.md` (BU1).
- **Modèle bus error = port fidèle Hatari** (`ioMem.c`+`ioMemTabST/STE.c`+`cpu/memory.c`) :
  tout `$FF8000-$FFFFFF` faute par défaut, whitelist des registres câblés par modèle
  (`Bus::buildIoFault`, carte octet par octet) + zones void + fixups ST/MegaST/MegaSTE.
  Hors IO : `$400000-$F9FFFF` et `$FF0000-$FF7FFF` fautent. Règle word/long : faute
  seulement si TOUS les octets fautent (`busFaultN`). Suivi par les DEUX cœurs.
- **SCU Mega STE décodé sur les adresses IMPAIRES uniquement** (`$FF8E01/03/…/0F`, même
  câblage que le MFP) : les octets PAIRS `$FF8E02-$FF8E0E` ne sont pas décodés → bus error
  en accès octet (cf. `ioMem.c`). Boot EmuTOS 256 Mega STE (qui programme le SCU) inchangé.
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
- **Blitter — compteur X/Y à 0 = 65536** (port `Blitter_WordsPerLine_WriteWord` /
  `Blitter_LinesPerBitblock_WriteWord`) : un compteur de mots/lignes écrit à 0 lance un transfert
  **maximal** (65536), et non un blit « vide » avorté. **Accès OCTET aux registres MOT ignoré**
  (`off < 0x3A` ; seuls HOP/LOP/contrôle/skew sont accessibles en octet, cf.
  `Blitter_CheckAccess_Byte`). Boots STE/Mega STE pixel-identiques. Cf. `docs/HATARI_DIVERGENCES.md` (B1, BL2).
- **Blitter — ligne GPU_DONE (GPIP3) ré-armée à chaque blit** (port `Blitter_Start`) : la ligne est
  dé-assertée (« pas fini ») au (re)démarrage de chaque blit et rabaissée à l'achèvement, au lieu de
  rester « fini » dès le 1ᵉʳ blit → un programme qui scrute GPIP3 / son IRQ pour la fin de blit voit
  désormais un front correct à chaque blit. Cf. `docs/HATARI_DIVERGENCES.md` §2ᵉ passe (BL-GPIP3).
- **Blitter — audit de correspondance ligne à ligne vs `blitter.c` (2026-07-07)** : cœur de
  données, restart/pause, 0→65536, GPIP3, arbitration, bug « 63 accès » confirmés fidèles.
  Trois résidus corrigés : **accès bus = modèle DMA** (`Blitter::readWord/writeWord` porte
  `STMemory_DMA_ReadWord/WriteWord` : zone fautive → lecture `0x0000`/écriture absorbée, et
  vecteurs `$0-$7` protégés en écriture comme `SysMem_wput`) ; **masques matériels à l'écriture**
  des incréments (`&0xFFFE`) et adresses src/dst (`&0x00FFFFFE`) via `regWriteMask` ;
  **`busCountError_` remis à zéro** à chaque entrée en PRE_START (blitter.c:1457).
  Étalons `run_etalons.py` TOUS OK. Cf. `docs/HATARI_DIVERGENCES.md` § Blitter.
- **GUI — contrôle de volume dans la barre de menu** : menu haut-parleur (icône selon le
  niveau : muet/bas/haut) avec slider 0-100 % et bascule « Muet » (mémorise et restaure le
  niveau). Volume MAÎTRE de la sortie hôte, appliqué au mix final dans `Audio::produceFrame`
  (avant clamp) — indépendant du LMC1992 émulé, qui appartient à la machine. Persisté dans
  `neost.cfg` (`volume=`, sauvé en fin de glissé), ré-appliqué au démarrage.
- **Son — S3 : le YM STE ressort à pleine amplitude (×2 LMC porté)** : Hatari compense la
  demi-amplitude du YM en STE (`YM_OUTPUT_LEVEL>>1`, marge anti-saturation DMA) en DOUBLANT
  les gains LMC1992 (`left/right_gain × 2`, dmaSnd.c:1152-1153/1460-1461) — NeoST ne le
  faisait pas → **YM STE 6 dB sous le ST**, enterré sous le DMA. Fix : `kLmcMakeup = 2.0`
  dans `gainLeft/gainRight/masterGain` (`DmaSound.cpp`) + `kDmaGain` 0.7 → **0.375** (= ¾ × ½ :
  la comptabilité « DMA sound is 3/4 level of YM sound » d'Hatari, rapportée à l'échelle demie
  puis re-doublée). À volume LMC plein (init TOS) : YM = 1.0, DMA = 0.75, exactement Hatari.
  **Validé** : cloche/keyclick GEM (EmuTOS, YM pur) — ratio RMS STE/ST = **1.000** (avant :
  0.5) ; musique Rick Dangerous II STE saine, pas d'écrêtage.
- **Son — S4 : table DAC YM MESURÉE par défaut (comme Hatari)** : le rendu des 3 voies passait
  par le seul « modèle de circuit » (`YM2149_BuildModelVolumeTable`) alors que le DÉFAUT
  d'Hatari est `YM_TABLE_MIXING` — la table 16³ mesurée sur un vrai ST par Paulo Simoes
  (`ym2149_fixed_vol.h`, © 2012, **vendorisé** dans `src/core/`) interpolée en 32³ par moyennes
  géométriques (port exact d'`interpolate_volumetable`, sound.c:505-543). L'interaction
  NON-LINÉAIRE réelle des 3 voies (timbre/balance des accords) remplace le modèle ;
  `NEOST_YM_MIXING=model` rebranche l'ancien pour A/B. Indexation plate 1:1 (idx = A|B<<5|C<<10
  ≙ `ymout5[Tone3Voices]`), normalisation ≙ `YM2149_Normalise_5bit_Table` (le niveau STE ÷2
  restant porté par `outScale_`).
- **Headless — `--sound-dump F.wav`** : dump audio 48 kHz stéréo s16 de la boucle `--frames`,
  même chaîne que la GUI (YM2149 horodaté modèle push + DMA STE + gains/tonalité LMC1992,
  débit exact sans asservissement d'anneau) → l'A/B audio contre l'oracle Hatari (WAV) ou
  entre configs devient scriptable (profil RMS par seconde). Cf. `DEV.md`.
- **Son — S2 : FIFO 8 octets du DMA STE fetchée AU FAISCEAU (2026-07-07 soir)** : port complet
  du modèle Hatari (`DmaSnd_FIFO_*`, `DmaSnd_STE_HBL_Update`). Le DMA fetche des MOTS dans une
  FIFO anneau de 8 octets entretenue à **chaque HBL** (`DmaSound::onHbl` ← `Machine::onHbl`,
  ≙ video.c:3322) ; la consommation DAC est datée au reste fractionnaire (`updateDac` ≙
  `Sound_Update`) et **capture les octets tirés** dans un anneau fixe que le rendu audio
  consomme — `mixStereo`/`mix` ne relisent PLUS la RAM en fin de trame. Conséquences fidèles :
  un programme qui **modifie le tampon pendant la lecture** est correct (Mental Hangover,
  Power Up Plus) ; la **fin de trame tombe au FETCH** du dernier mot (XSINT/Timer A en avance
  ≤ 8 octets, quantifiée HBL, comme le vrai HW — `Scheduler::DMASND` supprimé) ; le compteur
  `$FF8909+` montre l'adresse de **fetch** après synchronisation (≙ `DmaSnd_GetFrameCount`) ;
  passage mono→stéréo réaligné sur frontière paire (`fifoSetStereo`). Piège : la dette de
  rééchantillonnage du rendu hôte est PLAFONNÉE à 1 octet en sous-alimentation, sinon le
  rattrapage saute des octets en rafale (mesuré : 21 % d'octets B au lieu de 33 % sur l'étalon).
  **Validé contre l'oracle Hatari bâti dans le conteneur** (SDL2 présent — AVI audio + `--trace
  dmasound`) via un étalon dédié `tools/make_dmasnd_test.py` (secteur de boot STE : sample en
  boucle + handler VBL qui écrit un plateau B transitoire mid-trame, invisible à la frontière
  de trame) : NeoST fetch **33,3 %** de mots B = oracle **33,2 %**, et le WAV rendu montre le
  même ratio. `NEOST_DMASND_TRACE=1` émet le refill au format Hatari (diff direct). Étalons
  19/0 + 8 OK ; WAV cloche GEM / Rick Dangerous II **bit-identiques** (YM intact).
- **Divergences Hatari — 5ᵉ passe de rafraîchissement (2026-07-07, 4 agents)** : tous les
  statuts de `docs/HATARI_DIVERGENCES.md` remis en phase avec le code réel. Découverte : le
  commit `bc15a67` contient l'INTÉGRALITÉ du bug hunt 39-findings (pas seulement le fix STOP) —
  9 entrées passées à ✅ (M1 fronts GPIP, Timer B evt=0, $FF8264, VoidRead 0x00, read32 void,
  trou MMU STF, bruit 250 kHz, `mode_&0x8f`, filtre freq/res redondant), 4 reclassées FAUX
  POSITIFS (D4 6268, BL-MST, cartouche 0xFF, bits SR MIDI — Hatari fait pareil), V3
  partiellement résolu (restart compteur porté). Nouvelles entrées : **S4 table DAC YM**
  (défaut Hatari = mesures P. Simoes, NeoST = modèle circuit), **hybride WS1/WS3** (HBL 508 =
  WS1 vs oracle WS3 à 512), **troncature MFP→CPU sans reste** (dérive de phase timer↔faisceau,
  depuis portée en unités ×256 le 2026-08-14).
  Deux chantiers ciblés et testables documentés : lignes raster transitoires SHO (3 candidats +
  traces) et son YM STE (S3 gain LMC ×2 + S4, oracle `--ym-mixing model`). Cf.
  `docs/HATARI_DIVERGENCES.md` § 5ᵉ passe.
- **Bug hunt pré-release, 3ᵉ passe (2026-07-12, 5 agents parallèles) — suite complète verte** :
  - **Chargeurs de fichiers (P0, 5 crashs reproduits puis corrigés)** : sous Linux,
    `tellg()` sur un RÉPERTOIRE renvoie 2⁶³−1 (pas −1) → la garde `n <= 0` de
    `loadTos`/`loadImage`/`loadStateFile` laissait passer un `resize` de ~9 Eo
    (`bad_alloc` → abort). Bornes hautes ajoutées (ROM 1 Mo = fenêtre $E00000,
    disquette 8 Mo, état 64 Mo, sur le modèle de `loadCart` déjà correct) ;
    `--frames` négatif clampé (le `reserve` de `--sound-dump` explosait). Les
    parseurs de CONTENU (STX/MSA/DIM/ST, cfg, CLI) re-fuzzés : 130+ cas, zéro crash.
  - **Save-states v5 (P0/P1)** : **CRC32 du payload** dans l'en-tête, vérifié AVANT
    toute mutation (la seule troncature était couverte — un fichier corrompu de la
    bonne longueur mutait la machine sans rollback) ; rollback aussi sur EXCEPTION
    (`bad_alloc` à mi-restauration) ; **invariants vérifiés au load** (`ar.check`) :
    `fifoSize_`∈[0,16], `driveSel_`∈{−1,0,1}, `bufPos_ ≤ buf_ == bufTiming_`,
    `byteCount_`/`dataLen_`/`target_` ACSI, anneau `cap_` = kCapSize, `lineSnap_` ==
    `lineSnapLen_`×256 — un `.state` forgé déclenchait des écritures hors tas
    contrôlées (Shifter/DmaSound/FDC/ACSI). Message explicite sur version d'état
    obsolète.
  - **Déterminisme byte-exact restauré** : `podVec` sérialisait le PADDING non
    initialisé des structs `SyncWrite`/`GlueLine`/`ColorWrite`/`DmaEvent`/`RegEvent`
    (2-5 octets/élément, `--save-state-test` divergait sur des octets morts) →
    nouveau `StateArchive::objVec` champ-par-champ ; diagnostic `NEOST_STATE_MAP=1`
    (offset de chaque puce dans le flux) et offset de divergence du test relatif au
    payload.
  - **ACSI (P1)** : statut DMA INVERSÉ dans `writeAcsi` (jumeau du bug corrigé en
    2ᵉ passe dans `acsiDmaTransfer`) — bit0 de $FF8606 rapportait « erreur » après
    chaque octet de commande accepté, écrasant le statut correct juste après un
    transfert réussi.
  - **IKBD** : l'interrogation `$0D` compte le feu du joystick 1 comme bouton droit
    quand la souris est active (`IKBD_DuplicateMouseFireButtons`, jeux Big Run…).
  - **FPU 68881** : FSCALE extrait n de l'ÉTENDU (exact jusqu'à ±131071 puis
    saturation −$6001/$E000, port exact `floatx80_scale` — le clamp ±32768 faussait
    la bande étroite au-delà) ; ±inf décodé S/D avec mantisse 0 (forme canonique).
  - **Hygiène** : `*.state` ignoré (F5 écrit à la racine), `a.out` dé-traqué.
    Divergences basses restantes consignées → `docs/HATARI_DIVERGENCES.md` § 6ᵉ passe.
  - **Release** : **LICENSE GPLv3** à la racine (texte canonique gnu.org, choix
    utilisateur — compatible avec le portage Hatari GPLv2+) + section Licence/crédits
    dans le README (Moira MIT, imgui, miniaudio, EmuTOS) ; `extern/capsimg`
    (licence SPS non-commerciale, non compilé) dé-traqué et ignoré ;
    `deploy-web.yml` réparé (étape Musashi fantôme retirée — le job échouait à
    chaque push depuis le retrait du cœur Musashi). ⚠ Le contenu propriétaire
    servi par Pages (point 1) reste EN ATTENTE d'une décision utilisateur.
- **Bug hunt pré-release, 2ᵉ passe (2026-07-12, 5 agents + sanitizers) — 24 correctifs, suite verte** :
  - **Validation dynamique** : build ASan+UBSan (`build-asan/`) — selftests, boots ST/STE/MegaSTE,
    nocooper 6800 trames, déterminisme save-state et **72 mutants de fuzz** des chargeurs
    d'images (byte-flips/troncatures/en-têtes saturés, seed fixe) : **zéro rapport sanitizer**.
  - **Audio hôte (P1)** : la chaîne DMA/LMC1992 est GATÉE sur le modèle courant
    (`Audio::setDmaGate`) — sur ST/Mega ST le gain de rattrapage LMC (×2, compensation du
    ½-YM STE) doublait le YM (clipping) et l'état microwire d'une session STE colorait le ST
    après reconfigure (aussi corrigé : `dmasnd.reset(cold)` dans `reconfigure`). Le headless
    avait déjà la garde → GUI et `--sound-dump` de nouveau représentatifs l'un de l'autre.
    P2 : sink DriveSound armé seulement si la sortie audio existe (sinon accumulation non
    bornée de sons miniaudio jamais drainés), volume maître en RAMPE par bloc (clic au mute),
    reprise auto d'un périphérique audio arrêté (~1 tentative/s).
  - **GEMDOS HD (P1)** : `Fsfirst("C:\")` scannait le dossier hôte PARENT du montage (sortie
    du bac à sable + entrée DTA fantôme) — port de `fsfirst_dirname` (racine → EFILNF comme
    TOS). P2 : `Dfree` lit l'espace RÉEL du disque hôte (statvfs, bornes TOS conservées) ;
    `{`/`}` ne sont plus mutilés par host2atari (fichier listé mais inouvrable) ; `Fwrite`
    taille négative → ERANGE ; plafond DTA aligné (16 384).
  - **IKBD (P1)** : Δ souris LATCHÉ au VBL pour les handlers 6301 custom (Froggies,
    Dragonnels — l'accumulation en cours était lue puis effacée : menu souris mou/mort selon
    la phase d'injection hôte). P2 : `$0A` pas 0 stocké brut (== 6301), `$0D` rapporte l'état
    de bouton VIVANT, reset logiciel `$80,$01` ne purge plus le RDR de l'ACIA (côté 6301
    seul, comme IKBD_Boot_ROM), drain relatif borné 256 (16 tronquait les gros Δ).
  - **FPU 68881** : FSCALE ré-écrit (l'`int(trunc(±1e12))` était de l'UB et le clamp ±16383
    perdait les résultats dénormalisables — port de l'arithmétique d'exposant de
    `floatx80_scale`) ; FCMP/FTST portent `floatx80_cmp` (Z **et** N pour −0/−∞, SNaN →
    FPSR.SNAN) ; FMOVE/FABS/FNEG quiètent un NaN (propagateNaNOneArg) ; formats S/D décodés
    BIT À BIT (l'hôte quiétait les SNaN et perdait le payload) ; FMOVEM contrôle masque
    vide = FPIAR (quirk 68881) ; exceptions d'encodeFmt livrables en fin de drain CIR.
    Différés (TODO § FPU) : arrondis de conversion sortante S/D/entier bit-exacts
    (`roundSigAndPackFloatx80`, `floatx80_to_float32/64/int32`), packed decimal ∞/NaN/INEX1.
  - **Release-readiness** : `--version` (GUI + headless, version CMake), titre de fenêtre
    versionné, README corrigé (Moira vendorisé ≠ sous-module, builds EmuTOS 256 Ko
    documentés), `.gitignore` couvre `build-*/` et `a.out`, gating audio GUI == headless.
    ⚠ Les BLOQUANTS juridiques (ROMs TOS/jeux crackés publiés via Pages, LICENSE absente,
    capsimg non-commercial inutilisé, artefacts wasm 73 Mo) sont documentés dans le rapport
    de session — décisions utilisateur requises (purge d'historique, choix GPL).
- **Bug hunt pré-release (2026-07-12, 5 agents d'audit) — 27 correctifs, tous étalons verts** :
  - **Save-states durcis (P0/P1)** : bornes `vec`/`podVec` vérifiées AVANT `resize` (un préfixe
    corrompu 0xFFFFFFFF allouait 4 Go → terminate) ; en-tête **v4** avec empreinte
    machine/RAM/TOS et REFUS du chargement croisé (ST↔STE = machine hybride) ; **rollback**
    sur fichier tronqué (l'état de session est figé puis rejoué — plus de machine à moitié
    restaurée) ; écriture **atomique** (tmp+rename) ; champs manquants sérialisés : `g_cpuBias`
    (bascule 8/16 MHz Mega STE — sans lui le domaine d'horloge bus divergeait au load),
    `g_vblPending`/`g_hblPending` + broches IPL opt-in, `Glue::memConfig_` ($FF8001, lu en live
    par le MMU), densité FDC recalculée au load, carte de bus-errors invalidée (`fpu.present`
    restauré contournait `setFpuPresent`). Déterminisme re-validé STE **et** Mega STE.
  - **Crashs (P0)** : débordement de pile `idx[960]` de `renderGlueFrame` (ligne med à
    DE_end=512 → 1040 octets ; buffer 1072 + clamp) ; `mons[-1]` sur `--kiosk-monitor` négatif ;
    `filesystem_error` non rattrapée des scans disquettes récursifs (dossier illisible —
    `skip_permission_denied` + `increment(ec)`, le scan fenêtré tournait à CHAQUE frame) ;
    overlay `.wd1772` plus court que son secteur STX (lecture heap hors bornes livrée au
    guest) ; gardes `tellg()<=0` (loadImage/loadTos) ; table timing STX rév. 0 débordée
    (secteur 1024 o) ; READ ADDRESS/RNF et READ TRACK sur tampon vide (UB vector) ; division
    par zéro `cyclesPerRev` (STX malformée) ; hex viewer sans clamp haut.
  - **Comportement (P1)** : **inversion `dmaError_` ACSI** — une erreur DMA était rapportée
    après chaque transfert RÉUSSI (`!acsi_.dmaError()` ; bit0 $8606 = parité
    `FDC_SetDMAStatus`) + contrôle de plage RAM porté (hdc.c) ; reset DMA (bit 8 $8606) purge
    le STATUT ACSI seul, le paquet en vol continue (`HDC_ResetCommandStatus`) ; base vidéo
    **$FF8201 masquée 0x3F** comme le compteur (Hatari video.c:5084 — le rendu pouvait
    dispatcher la MMIO : consommation UDR MFP/INTRQ FDC, bus error déclenchée HORS CPU) ;
    **capture par-ligne au faisceau étendue aux trames « palette seule »** (`spec512Active_`,
    cf. seuil 1) → **ferme le résidu V2 assumé de No Cooper écran principal : 891 px → 0 px
    vs oracle** (référence de l'étalon promue `ref_kind: oracle`) ; touches hôte F5/F7 (+F9/F10
    kiosk) ne fuient plus vers l'IKBD (le jeu recevait F5 pendant que l'état était écrasé) ;
    `stepInstruction` dispatche les événements en mode bloc (le pas-à-pas ne servait JAMAIS
    HBL/VBL/timers) ; `Scheduler::runTo` déclenche en ordre **chronologique** (l'énum ne sert
    plus que de tie-break — un HBL dû 200 cyc avant Timer A partait après) ; `setCore` no-op si
    cœur inchangé (les breakpoints du débogueur survivaient pas au reconfigure) ; garde-fou
    `onFdcEvent` reprogramme à `REFRESH_INDEX` (livelock) ; MSA bornes Hatari (spt ≤ 56).
  - **Mineur (P2)** : resets complétés (biquads gauche DmaSound, `serialBaud_` MFP) ; RDR MIDI
    amorcé à 1 (midi.c:110) ; montage disquette/ROM échoué non persisté dans `neost.cfg` (une
    image corrompue était retentée à chaque boot) ; `exeDir` résolu via `/proc/self/exe` /
    `_NSGetExecutablePath` (lancé via PATH, la config s'écrivait dans `../` du cwd) ; sorties
    headless bruyantes en échec (`--screenshot`/`--dump-at`/`--sound-dump`… → stderr + exit≠0) ;
    SR restauré après `--bus-selftest`.
  - Consigné sans correction (divergences volontaires / hors périmètre) : SCC RR9 (NeoST suit
    la datasheet, Hatari rend WR9), $FF8E07 (bug copier-coller Hatari), purge des pendings SCU
    à l'accès des masques (modèle « sources vivantes »), handles GEMDOS HD hors snapshot.
- **Blitter — partage de bus non-hog cycle-exact (2ᵉ passe, 2026-07-07 soir)** : les deux
  dernières divergences de timing portées depuis le modèle CE d'Hatari (celui de l'oracle).
  **Suspension MID-WORD** (`BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED`) : la tranche rend le
  bus exactement au 64ᵉ (ou 63ᵉ) accès, même entre la lecture et l'écriture d'un même mot —
  l'état du mot en cours (`haveSrc_/haveDst_/fetchSrc_/dstWord_` ≙ `BlitterState` d'Hatari)
  persiste entre tranches. ⚠ Piège corrigé pendant la mise au point : ces drapeaux doivent être
  SAUVEGARDÉS en fin de tranche, sinon la reprise refait la lecture source (double `srcShift` →
  pipeline skew corrompu → mots perdus au bord des icônes GEM). **Part CPU = 64 accès bus CPU
  RÉELS** (port `BLITTER_PHASE_COUNT_CPU_BUS`) : comptés par les callbacks mémoire de Moira
  (`Blitter::noteCpuBusAccess` via `Bus::blitterCountCpu`), le 64ᵉ accès arme la fenêtre
  PRE_START et date la tranche suivante à +4 cycles — remplace le forfait de 256 cycles (un CPU
  qui ne touche pas le bus, cycles internes ou STOP, retarde désormais le blitter comme sur le
  vrai matériel). Diag `NEOST_BLIT_TRACE=1` ajouté (un état des lieux par blit, pendant du
  `--trace blitter` d'Hatari). Validation : étalons TOUS OK ; scénario blitter-intensif
  `--walk-mouse` tos106fr STE (132 blits, redraws d'icônes FXSR/NFSR/skew) **byte-identique**
  à l'ancien modèle ; Enchanted Land et Super Hang-On inchangés (SHO n'utilise pas le blitter :
  0 blit mesuré jusqu'au menu). Reste documenté : pas d'exécution CPU parallèle pendant la
  tranche (hooks par-cycle Moira requis), cf. `docs/HATARI_DIVERGENCES.md`.
- **Bus — largeur d'accès `ioAccessWidth_` non fuitée par le blitter** : `write16`/`write32`
  restaurent la largeur sauvegardée avant leur `return` de la branche blitter ; sans ça, après le
  1ᵉʳ blit mot, les bus-errors d'accès **octet** ($FF9200/lightpen/FDC) restaient désarmées en
  permanence. Cf. `docs/HATARI_DIVERGENCES.md` §2ᵉ passe (BUS-LEAK).
- **RTC RP5C15** (Mega ST/Mega STE, `$FFFC21-$FFFC3F`) : modèle paresseux déterministe
  (cycle CPU du dernier top de seconde + rattrapage), registre RESET, débordement BCD
  calendaire. La seconde vaut une seconde de la BASE DE TEMPS DE LA MACHINE
  (trame × Hz, posée par `Machine::beginFrame_`) et non une constante : sinon
  l'horloge dérivait de 0,105 % contre le reste. **TIMER EN** (bit3 du registre
  mode) honoré : à 0 le compteur est arrêté — TOS 1.02 et EmuTOS écrivent `$9`
  puis `$8` au boot (ils ne basculent que le bit0 de banque), seule la cartouche
  de diagnostic le coupe pour figer l'heure. Corrige « C0 No clock installed »
  + « C1 clock increment error » : la batterie **Z de la cartouche Atari Field
  Service v4.4 passe INTÉGRALEMENT sur Mega ST** (RAM, ROM, couleur, clavier,
  audio, MFP/Glue/vidéo, horloge avec rollover de siècle, blitter).
  ⚠ Non modélisé : la sortie CLKOUT du RP5C15 — aucun consommateur observé
  (Hatari ne la modélise pas non plus, elle n'apparaît que dans son brochage).
- **Persistance RTC entre sessions** (`neost.cfg` : clés `rtc=` date/heure BCD,
  `rtc_saved=` horodatage hôte) : reprise au boot avec rattrapage du temps écoulé ;
  snapshot à chaque sauvegarde de config (`saveConfig` / `snapshotRtc`).
- **MIDI** (`MidiAcia`, `$FFFC04/06`) : IRQ canal 6 (RX/TX), TDRE cadencé à 31250 bauds ; fiche de bouclage OUT→IN **optionnelle** (`--loopback`, menu Machine, `midi_loopback=`), **débranchée par défaut** depuis le 2026-08-21 (Cubase/MROS + Thru = larsen sinon).
  Validé par l'étalon `tools/run_midi_sequencer.py` (2026-08-23) : Cubase Lite importe un SMF et le joue, `--midi-dump` journalise le MIDI OUT daté au cycle, `tools/midi_compare.py` confronte notes/vélocités/durées/pédale/tempo au fichier (pente 1,001, gigue σ ≈ 1 ms).
- **Périphériques des ports** (`PortDevices`, un par port : `joy0=`/`joy1=`/`rs232=`/`printer=`/`cartbutton=`, `--plug PORT=DEVICE`, page Dongles, auto-branchement `disks/dongles.txt`, save-state v16, format courant v17) — 2026-08-23, **OFF par défaut**. Onze périphériques de Steem SSE : Leader Board / 10th Frame (joystick 1, haut+bas), Cricket Captain / Soccer Manager (joystick 0) et Rugby Coach (joystick 1) (oscillateur `%1100`/`%1101`), B.A.T. II (CTS=0), Music Master (DTR→DCD +200 cycles), Jeanne d'Arc (DCD sur décroissance RTS|DTR), Pro Sound Designer (DAC 8 bits port parallèle, rejoué par le YM), boutons Multiface ST (GPIP7) / Ultimate Ripper (RI) avec `--button-at`. Protocoles Steem/WinUAE, aucun jeu à clé dans le dépôt. Cf. `docs/EXTENSIONS.md`.
- **Port cartouche abstrait** (`core/CartDevice.hpp`) — 2026-08-23 : abonnés aux signaux /ROM3, /ROM4, /UDS, consultés par le `Bus` dans l'ordre ; plusieurs clés possibles. **Oracle de rejeu** : `--key-log` / `--key-replay` (format `R3`/`R4`/`U`), cf. `docs/EXTENSIONS.md`.
- **Clé C-Lab Notator / Creator** (`CartridgeKey::Model::Notator`, `--dongle notator`, page Dongles) — 2026-08-23, **OFF par défaut**. EP600 : armement `FEEDB1 := STER` sur /ROM4 (`$FA00EA`), 8 bascules D actives bas cadencées par UDS (désarmée) ou par la descente de /ROM3 (armée, lecture après horloge), resets asynchrones D9 (A4·A2) / D8 (A3·A1). Équations TPH (atari-forum t=43078, 10/2025) via le firmware SidecarTridge `md-notator`. Crochet `Bus → rom4Read`. Save-state v15. ⚠ Pas confrontée à un Notator réel.
- **Clé Steinberg** (`CartridgeKey`, `/ROM3` `$FB0000-$FBFFFF`, `--dongle cubase2|cubase3|auto`, page Dongles, `dongle=`) — 2026-08-23, **OFF par défaut**. Clé **rouge** (Cubase 3.10 / Score / Audio : EPLD 5C060, 16 bascules T, entrée A8, sortie D8, cadencée par /ROM3) et clé **noire** (Cubase 2.01 : PAL16R8, A1-A8 → D8-D15, cadencée par **chaque** front /UDS du CPU — crochet dans `NeostMoira`, « au mieux »). Équations transcrites de MiSTery (`cubase2_dongle.v`, `cubase3_dongle.v`). Invisible du TOS (/ROM4 seul sondé), cohabite avec le HD GEMDOS. ⚠ Pas encore confrontée à un vrai Cubase 3.10 (aucun logiciel à clé dans le dépôt) ; auto-test de transcription dans `neost-selftest`.
  **Sorties hôte** (GUI, 2026-08-21) : synthé GM intégré, port virtuel « NeoST MIDI OUT »
  (CoreMIDI sous macOS, séquenceur ALSA sous Linux) — livraison **horodatée** (cycle ST →
  heure réelle de la trame + 30 ms, thread dédié : gigue mesurée ±60 ms → nulle) — et
  **Roland MT-32/CM-32L** via libmt32emu (Munt 2.8.3, **vendorisé** dans `extern/mt32emu`
  et lié en statique — plus de paquet système à installer ; ROM Roland à fournir dans
  `roms/mt32/`), rendu dans la sortie audio avec événements datés à l'échantillon.
  **Synthé GM sur toutes les plateformes** depuis le 2026-08-30 : là où macOS a le
  DLSMusicDevice, Linux/Windows ont `audio/GmSynth` (**TinySoundFont vendorisé** dans
  `extern/tsf` + banque **TimGM6mb livrée** dans `roms/gm/`, remplaçable par
  `gm_soundfont=`), rendu daté à l'échantillon comme le MT-32, fader dédié page Sound,
  garde `neost-selftest` (banque livrée + datation mi-trame).
  **Appareils MIDI hôtes sur les TROIS plateformes** depuis le 2026-08-31 : le backend
  **winmm** (`audio/MidiWinmm.hpp`) donne à Windows les destinations matérielles ET les
  sources qu'avaient déjà CoreMIDI et ALSA — énumération, aiguillage par canal, fusion,
  canalisation, panique, apprentissage d'identifiant. Windows y gagne même un
  **identifiant unique** (chemin d'interface `DRV_QUERYDEVICEINTERFACE`, hub et prise
  compris) là où ALSA n'en a aucun, et `timeBeginPeriod(1)` tant qu'une sortie est
  ouverte ramène la gigue de livraison de σ 5,34 ms à **σ 0,30 ms** (mesuré sur
  matériel réel). Seul manque, **irréductible sans pilote tiers** : le port virtuel
  « NeoST MIDI OUT » — Windows n'a aucune API pour en créer un (loopMIDI le fournit, et
  son port apparaît alors comme un appareil ordinaire).
  `audio/MidiOutHost`, `audio/MidiInHost`, `audio/MidiWinmm.hpp`, `audio/Mt32Synth`,
  `audio/GmSynth`.
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
- **Disque dur ACSI complet** (`io/Acsi`, port de `hdc.c` ; routage DMA via `Fdc`,
  `$FF8604/06` bit `DMA_CSACSI`) : boot et lecture/écriture depuis une **vraie image
  de disque dur** (dump de secteurs brut, `--acsi`/`--hd` ou `NEOST_ACSI_IMG`),
  jusqu'à **8 cibles**. Paquets de commande 6 octets (classe 0) / 10 octets (classe 1)
  reçus octet par octet (broche A1 = `DMA_A0`), transfert DMA RAM↔image piloté par le
  `Fdc` (sens contrôlé par `DMA_WRBIT`, déclenché à la fin du paquet et sur
  `HDC_DmaTransfer` à l'écriture $FF8606). Commandes SCSI portées 1:1 : TEST UNIT
  READY, INQUIRY (LUN), REQUEST SENSE (sens court/long + `nLastBlockAddr`), MODE SENSE
  (pages 0x00/0x04/0x3f), READ CAPACITY, READ/WRITE(6 et 10), SEEK, FORMAT, REPORT
  LUNS, SHIP. Comptage de partitions (DOS MBR + Atari/AHDI, `partitionCount`). IRQ HDC
  (INTRQ/GPIP5) + statut DMA acquittés par octet ; cible vide → « pas de disque ».
  Validé headless (EmuTOS 192 Ko) : sonde ACSI (TEST UNIT READY → INQUIRY → READ
  CAPACITY → lecture table de partitions LBA 0 + BPB LBA 1), bureau « GEMDOS drives:
  ABC » avec icône **DISK C**, `_bootdev=C:`, **boot + Pexec depuis C:**, et un PRG
  `C:\AUTO` recopiant `HELLO.TXT`→`OUTPUT.TXT` **persisté dans l'image** (WRITE(6),
  relu par mtools). _(Remplace l'ancien disque virtuel en mémoire, qui n'était qu'un
  bouchon pour le diagnostic « Hard Disk DMA Exerciser ».)_
- **Boîtier de test DMA du kit Field Service** (2026-08-27, `Fdc::setDmaFixture`,
  `--dma-fixture` headless, OFF par défaut — matériel de banc d'atelier, exclusif d'un
  disque réel sur la cible 0) : cible ACSI 0 à **UN octet de commande**
  `((count-1)<<6) | opcode` — $10 = avale count×512 octets (RAM→boîtier), $08 = les
  rend — transfert immédiat, compteur de secteurs décompté à zéro, adresse DMA avancée,
  IRQ GPIP5. Fait passer le test **« D DMA Port »** des diagnostics Atari (suite Q du
  MegaSTE_Diagnostic v1.5 : **12/12**) ; Hatari, sans boîtier, échoue D0/D1/D3.
- **ACSI INQUIRY — « Additional Length » fixe (31)** : `buf[4]` n'est plus écrasé par
  `count()-5` (valeur variable erronée) → la longueur additionnelle reste 31 comme Hatari
  (`HDC_Cmd_Inquiry` n'écrit jamais cet octet) ; un pilote HD lisant ce champ n'est plus trompé.
- **SCC Z85C30 — contrôleur série Mega STE** (`io/Scc`, port de `scc.c`,
  `$FF8C80-$FF8C87`) : deux canaux A/B, jeu complet WR0-15 / RR0-15 par canal
  (pointeur de registre via WR0, commandes WR0/WR9, reset matériel/canal), statut RR0
  (TX buffer empty, RX char available, CTS/DCD au repos = assertés comme Hatari sans
  TTY), vecteur RR2A/RR2B (+ statut, Status High/Low, VIS/NV), IP RR3 + sources
  d'interruption, **IRQ niveau 5 vectorisée gatée par le SCU** (VmeIntState bit 5,
  IACK → vecteur, IUS, `Reset Highest IUS`). Décodage des accès façon Hatari (octets
  IMPAIRS seulement ; `$..81/85` = ctrl A/B, `$..83/87` = data A/B). TX immédiat
  (puits série + **bouclage local** WR14 bit4) et RX par injection. Câblé via le SCU
  (`Scu::syncState` niveau 5) et `Cpu68k::neostUpdateIpl`/`readIrqUserVector`.
  Validé headless : **EmuTOS 256K et TOS 2.06 Mega STE initialisent les deux canaux**
  (WR9=$C0 reset, WR4 async, WR2 vecteur, WR3/5 8 bits…) et **bootent au bureau** sans
  plante ; bouclage TX→RX prouvé (programme superviseur via Supexec écrit `$42` sur le
  canal A en loopback, le relit et le renvoie sur l'AUX → sortie série `<B>`).
  **Prises de bouclage EXTERNES du kit Field Service** (2026-08-27, `Scc::setLoopback`,
  `--loopback`/`--loopback-at` headless, OFF par défaut) : TxD→RxD, RTS→CTS, DTR→DCD,
  DTR→DSR (/SYNC) et **BREAK émis (WR5 bit4) → RR0 bit7 Break/Abort** au retour — le
  mécanisme de détection des prises du test « I SCC » du diagnostic MegaSTE, qui rend
  **Pass** (Port A, Port B et LAN détectés, async, modem control, IRQ ext-status).
  _Non porté (faible valeur ici) : timers du BRG (Zero Count), baudrate temporisé,
  série hôte réelle._
- **SCC No-Vector → vecteur spurious 24** (`Cpu68k::readIrqUserVector`) : sur IACK niveau 5 avec
  WR9 NV armé (`Scc::processIack()` = -1), le wrapper renvoie désormais le vecteur **spurious 24**
  ($60) — comme la branche MFP et `iack_cycle` d'Hatari (`vector<0 → 24`) — au lieu de
  l'auto-vecteur 29 ($74) erroné. Boots ST/STE/MegaSTE inchangés.
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
- **SCU réinitialisé au reset** (port `SCU_Reset`) : `Scu::reset(bool cold)` remet `SysIntMask`/
  `VmeIntMask`/états à 0 (le SCU masque tout jusqu'à reprogrammation par l'OS), GPR1=0x01, GPR2
  effacé au cold boot ; appelé par `Machine::reset()`/`hardReset()`. Avant, les masques persistaient
  à travers un reset doux (non déterministe selon l'historique de session).
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
- **MC68881 — mantisse 64 bits RÉELLE (softfloat) + livraison d'exception FP**
  (`src/io/SoftFloatX80.hpp`, `src/io/Fpu.{cpp,hpp}`) : l'arithmétique ALGÉBRIQUE
  (FADD/FSUB/FMUL/FDIV/FSQRT/FCMP/FINT/FINTRZ/FREM/FMOD/FSCALE/FGETEXP/FGETMAN/FSGLxxx) ne
  passe plus par le `double` hôte (53 bits) mais par un **portage propre de SoftFloat-2a
  (Hauser), format étendu 80 bits** → **64 bits de mantisse réels**, 4 modes d'arrondi FPCR,
  précision étendu/double/simple et drapeaux d'exception IEEE exacts (leaf multi-mots via
  `__uint128_t`). Les transcendantes restent en `double` hôte (le 68881 les approxime — comme
  MAME/Previous). Les **exceptions FP activées dans le FPCR** sont en plus **LIVRÉES** via le
  Response CIR (primitive « Take Pre-Instruction Exception »). Validé : `make_fpu_testrom.py`
  étendu à **9/9 PASS**, dont `1.0/3.0` relu en FMOVE.X = `$3FFD AAAAAAAA_AAAAAAAB` exact (un
  calcul 53 bits donnerait `…A800`) et FDIV par 0 avec DZ activée → Response CIR `$7032`.
- **MC68881 FONCTIONNEL — mode périphérique complet** (`src/io/Fpu.{hpp,cpp}`) : le
  niveau « sonde + trapping » devient une vraie émulation du 68881 câblé en
  périphérique (MC68881 UM §7 + AN-947 ; Hatari n'émule pas ce socket — rien à
  porter, références = manuel Motorola, MAME `m68kfpu`, et la **glue SFP004 de la
  MiNTLib** qui est la spec de facto : écrire le mot de commande F-line en `$FFFA4A`,
  scruter `$FFFA40` tant qu'il vaut `$8900`, transférer par `$FFFA50`). Implémenté :
  registres **FP0-FP7 en étendu 80 bits** (FMOVE.X aller-retour bit-exact),
  FPCR/FPSR/FPIAR, machine à états CIR (Command/Response avec primitives de
  transfert `$95xx/$96xx/$B1xx/$B2xx`, fenêtre Operand bouclante, Condition avec les
  32 prédicats, Save/Restore trame IDLE `$1F18` + reset par trame nulle), formats
  **B/W/L/S/D/X/P** (packed BCD avec k-factor), **FMOVECR bit-exact** (table ROM
  silicium recoupée MAME/Previous/WinUAE, y compris le `e` à 1 ulp), FMOVEM données
  et registres de contrôle, et toute l'arithmétique (FADD/FSUB/FMUL/FDIV/FSQRT/
  FCMP/FTST/FREM/FMOD/FSCALE/FGETEXP/FGETMAN/FINT/FINTRZ/FSGLMUL/FSGLDIV/FSINCOS +
  transcendantes via libm hôte), codes condition N/Z/I/NAN, octet quotient,
  exceptions OPERR/DZ/OVFL/BSUN en bits FPSR (pas d'IRQ : le socket se scrute).
  **Limite documentée** : calculs en double hôte (53 bits de mantisse au lieu de
  64). Validations : mini-ROM `tools/make_fpu_testrom.py` (dialogue SFP004 réel)
  — FADD.S, FDIV.D, FMOVECR π **bit-exact**, FSQRT.D **bit-exact**, FINTRZ+
  FMOVE.L, FCMP+Condition CIR, aller-retour FPCR → **7/7 PASS** ; diagnostic
  cartouche MegaSTE (tos206us) : boot self-test « **FPU idle** » (détecté via la
  trame Save CIR) avec `--fpu`, « FPU not found » fidèle Hatari sans (la
  cartouche n'a pas de test FPU dédié au menu : F = Floppy, V = VME) ;
  non-régression : écrans byte-identiques avec/sans `--fpu` (EmuTOS 256K,
  TOS 2.06 megaste) et gate EmuTOS 192K/ST intact.
- **MC68881 — fidélité NaN / SNaN / masques / FSGLMUL** (recoupé `cpu/fpp*.c` softfloat) :
  la **propagation des NaN** renvoie désormais l'opérande NaN réel quiété (signe + payload) au
  lieu d'un default-NaN ; une **entrée SNaN** lève `FPSR.SNAN` (vecteur 54) et non plus OPERR
  (flag softfloat `flag_signaling` distinct) ; les **masques FPCR/FPSR** forcent à 0 les bits
  réservés (`&0xFFF0` / `&0x0FFFFFF8`) ; **FSGLMUL** tronque ses entrées à 24 bits avant le produit
  (cf. `floatx80_sglmul`). Mini-ROM FPU **9/9 PASS** inchangé.
- **MC68881 — FSCALE exposant ∞/NaN + octet AEXC** : **FSCALE** par un exposant ∞/NaN ne fait plus
  d'UB (`int(trunc(±inf/nan))`) — NaN → propagation, ±∞ → OPERR + NaN par défaut (cf.
  `floatx80_scale`) ; l'**octet AEXC accumulé** suit `updateaccrued`/`fpsr_make_status` (UNFL
  accumulé seulement si INEXACT l'est aussi ; INEX accumulé sur INEX2 OU OVFL). Mini-ROM FPU 9/9 inchangé.
- **MC68881 — FMOVECR arrondi par mode + INEX2** (`romConstant`, port table `fpp_cr` de `fpp.c`) :
  les constantes ROM portent désormais une colonne `inex` (la valeur est-elle inexacte en étendu ?)
  et `rnd[4]` (ajustement des 32 bits de poids faible par mode d'arrondi RN/RZ/RM/RP). `FMOVECR`
  applique l'ajustement selon le mode FPCR, puis arme `EXC_INEX2`/`AEXC_INEX` et déclenche la
  livraison d'exception si activée — au lieu de toujours renvoyer la valeur RN sans drapeau. Mini-ROM FPU 9/9.
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

## Réseau — extensions NeoST (INACTIVES par défaut, cf. `docs/EXTENSIONS.md`)

Ces fonctionnalités **n'existent pas dans Hatari** (extensions assumées, consignées dans
`docs/HATARI_DIVERGENCES.md` § Extensions) ; OFF par défaut, **sans effet sur les
étalons** (réseau jamais ouvert par `run_all.py`). Cœur sans socket : `neost_core`
*signale*, la lib `neost_net` (frontends) *fait l'I/O* — patron `Fdc::soundSink_`/
`Mfp::serialSink_`.

- **Modem Hayes sur RS-232** (`net/HayesModem`, `--modem`) : commandes `AT` sur l'USART
  MFP → **pont TCP réel** (`ATDT hôte:port` → `CONNECT`, `+++`/`ATH`, DCD suit la
  porteuse). Débloque STiK/STinG (SLIP/PPP), terminaux, BBS. S'appuie sur
  **`Mfp::receiveByte`** — injection RX **cadencée** au débit série (`Scheduler::SERIAL_RX`,
  IRQ RxFull par octet). Vérifié `MODMTEST.TOS` ↔ serveur TCP local.
- **EtherNEC — NE2000 sur le port cartouche** (`io/Ne2000`, `--ethernec`) : carte réseau
  NE2000/DP8390 (pages 0/1, anneau RX, Remote DMA, filtrage MAC) décodée dans la fenêtre
  cartouche — **lecture** `$FB0000+n*512`, **écriture** = fausse lecture
  `$FA0000+n*512+d*2`. Fait tourner les pilotes STinG/MiNTnet/MagiCNet **sans modif**.
  Backend `NetBackend` (boucle locale ; SLIRP/pcap = point d'extension). **Exclusif d'une
  cartouche** ($FA0000). Auto-test fil : `--enec-selftest` (palier `fast`).
- **UltraSatan — interface SD sur le bus ACSI** (`io/UltraSatan`, `--ultrasatan`, 2026-08-21) :
  le vrai boîtier de Jookie — **2 slots** = 2 cibles (IDs 0-1), INQUIRY `JOOKIE  UltraSatan`
  (RMB, n° de slot, v1.20), slot vide = **NOT READY / medium not present**, **horloge propre**
  sur les cycles émulés, et les **paquets ICD `$20 'US…'`** du firmware v1.20 (`CurntFW`,
  `RdCl`/`WrCl`, `RdINQRN`/`WrINQRN`, `RdSt`/`WrSt` avec magies, `RdLog` ; flash refusée).
  EmuTOS monte **C:** depuis une image SD générée sans pilote (`tools/make_usatan_hd.py`).
  Auto-tests : `--usatan-selftest` (fil, 15 checks) + verdict série `usatan_netusbee`
  (programme ST, séquence LongRW de `US_CONF`). Save-states **v12**.
- **NetUSBee — NE2000 + ISP1160 USB sur le port cartouche** (`io/Isp1160` + `io/Ne2000`,
  `--netusbee`, 2026-08-21) : la NE2000 de l'EtherNEC (inchangée) + le contrôleur hôte USB
  décodé aux adresses du pilote FreeMiNT (`$FA0000` latch, `$FA8000` lecture, `$FB8000`/`$FBC000`
  données/commande, mots 16 bits) — ID `$6120`, reset logiciel, registres OHCI/ISP, **hub racine
  vide**, ATL achevée en `DeviceNotResponding`. Les pilotes s'initialisent, rien n'est énuméré
  (périphérique USB hôte = point d'extension). Auto-tests : `--netusbee-selftest` (11 checks)
  + verdict série. ⚠ fenêtre LSB partagée avec le CR NE2000 (cf. `docs/EXTENSIONS.md`).
- **Anneau MIDI réseau** (`net/MidiRing`, `--midi-net H:P[:L]`) : **MIDIMaze en ligne** —
  MIDI OUT → UDP → pair aval, datagrammes amont → MIDI IN (`MidiAcia::setMidiSink`/
  `receiveExternal`, tampon de gigue respectant les 2 octets du 6850). Vérifié
  `MIDITEST.TOS` (OUT 10/10 octets ordre exact ; round-trip complet OUT→réseau→IN→ACIA).

## Frontend & outillage
- **Capture souris accessible sans bouton central (2026-08-15)** : clic molette ou
  **Ctrl+Alt+G** pour accrocher/décrocher la souris dans les frontends bureau et Web ;
  seul ce chord est réservé à l'hôte, tandis que `G` seul reste transmis à l'IKBD.
  Le kiosk conserve sa capture imposée et son overlay F12.
- **Menu kiosk « Joysticks » : affectation des manettes hôte aux ports ST + boutons
  ESPACE/RETURN (2026-07-12)** : nouvelle action du menu in-game (START) — une ligne par
  manette détectée (pastille ● d'activité pour identifier physiquement chaque stick), le
  FEU fait tourner son rôle **AUTO → PORT 1 → PORT 0 → OFF** (AUTO = affectation
  historique 1ʳᵉ→P1/2ᵉ→P0 sur les ports libres ; plusieurs manettes épinglées au même
  port sont OR-ées). Persisté **par GUID** (`joymap=` dans neost.cfg, écrit même en kiosk
  comme ROM FOLDERS — le jid GLFW change au rebranchement, pas le GUID). Côté
  `JoystickInput.hpp` : `resolveAssign()` (partagé compose/menu), `compose(…, roles)`
  rétro-compatible (web inchangé). **Boutons X/Y = touches ESPACE/RETURN** (`readAux` /
  `composeAux`, make/break IKBD sur fronts, coupés pendant l'overlay) — X/Y sont retirés
  du FEU (A/B + gâchettes restent le feu) ; repli manette brute : boutons 2/3 (« standard
  mapping ») ou 1/2 (encodeur arcade). Les jeux « press SPACE » (EL…) se jouent 100 %
  manette. Vérifié sous Xvfb (page, cycle des 4 rôles, persistance/retrait `joymap=`,
  DS4 réel détecté) ; étalons `--tier fast` verts.
- **Save-states (sauvegarde/restauration d'état complète) (2026-07-11)** : `StateArchive`
  (sérialiseur SYMÉTRIQUE — une méthode `serialize()` gère save ET load, l'ordre ne peut
  pas diverger) + `serialize()` sur **chaque puce** (Shifter incl. framebuffer/palette/état
  glue-spec512, MFP, YM2149, DmaSound, Blitter, IKBD, ACIA, RTC, FDC — contrôleur, pas le
  contenu des images disque ; **SCC** série Mega STE ; **état de commande ACSI**) et le Bus
  (RAM + config + overlay + latch db + cache MegaSTE).
  Deux pièges résolus : Moira n'a pas de serialize intégré (→ `NeostMoira::serializeState`
  sur les membres protégés) ; l'ordonnanceur n'est PAS une file de `std::function` mais un
  tableau fixe de sources (callbacks re-liés à la construction) → on ne sérialise que les
  échéances. Format `'NSTS'` versionné. **Test de DÉTERMINISME** (`--save-state-test`) :
  save → run 200 → load → run 200 → l'état re-sérialisé ET l'écran doivent être
  byte-identiques (divergence = 1ᵉʳ offset localisé) — PASSE sur boot ST, STE (son DMA +
  vidéo STE), Mega STE (SCC actif), démo No Cooper (overscan med-res). I/O fichier prouvée :
  save@60 → fichier 1,3 Mo → load + run → écran **diff_px=0** vs run direct (ST comme Mega
  STE). Format `'NSTS'` versionné — **v17 actuel** ; toute autre version est rejetée
  d'office (`Machine::loadState` teste `version != 17`). GUI : **F5** sauver / **F7** charger (slot
  `neost.state`, menu Machine, overlay). Headless : `--save-state`/`--load-state`, auto-test
  `--save-state-test`. Hors-snapshot **par conception** : le CONTENU des images disquette/
  disque dur vit dans les fichiers hôtes (`writeBack` les persiste au fil de l'eau, comme
  Hatari) — un load ne les *rembobine* donc pas.
- **Débogueur interactif — breakpoints, watchpoints, pas-à-pas, symboles (2026-07-11)** :
  réutilise le `Debugger` intégré de Moira (conteneur `Guards`). **Breakpoints PC**
  « break-before » (pré-check dans `Cpu68k::run`, coût nul si aucun ; skip-once à la
  reprise). **Watchpoints mémoire** lecture/écriture « break-after » (via la couche
  dataflow de Moira ; patch vendorisé masquant l'adresse au bus 24 bits pour matcher un
  accès I/O en court absolu — cf. `NEOST_VENDOR.md`). **Pas-à-pas instruction** :
  `runFrame` rendu RÉSUMABLE mi-trame (`beginFrame_`/`finalizeFrame_`, garde
  `frameInProgress_`) → au breakpoint on rend la main sans finaliser, la reprise/le pas
  continue la MÊME trame en **lockstep** (aucune dérive d'horloge CPU↔vidéo). **Symboles**
  (`SymbolTable`, cœur) : `.sym` nm-style OU symboles DRI + noms étendus GST d'un
  exécutable TOS `$601A`, décalés d'une base ; `lookup` + `nameFor` (« nom+offset »).
  GUI (fenêtre Débogueur) : pause/continue/step, listes breakpoints & watchpoints,
  désasm cliquable annoté, chargement de symboles + BP par nom. Headless : `--break`,
  `--watch`, `--symbols`/`--symbols-base`/`--break-sym`. Non-régression : self-tests P1,
  glue-selftest 31/31, étalons byte-exact (le refactor `runFrame` ne change pas le rendu).
- **Effets CRT (façade moniteur) — passe shader opt-in (2026-07-10)** : pile d'effets
  portée de POM2 (`src/gui/CrtEffectStack`, `OpenGLShader`, `CrtParams`). Une passe FBO
  applique par-dessus l'écran ST rendu la « façade verre » d'un CRT : distorsion de baril,
  scanlines (faisceau doux anti-aliasé analytiquement via `fwidth`), shadow mask
  (triade / grille d'ouverture / points, préservant la luma moyenne — modèle Lottes),
  rémanence phosphore (ping-pong), luminosité/contraste/saturation/teinte (rotation chroma
  YUV BT.601), vignette, gain de luminance et courbe gamma. **Sûreté = échec gracieux** :
  points d'entrée GL 3.x chargés paresseusement par `glfwGetProcAddress` (autonome, sans
  GLEW/GLAD) ; si le shader ne compile pas (contexte compat 2.1 macOS) `available()` reste
  faux et `process()` est un no-op → l'appelant présente la texture brute. **Coexistence
  immediate-mode** : `process()` sauve/restaure tout l'état GL (FBO, viewport, blend/depth/
  cull, filtres de la texture source) et — CRUCIAL vs POM2 core-profile — revient au programme
  0 et **débinde le VBO** (`GL_ARRAY_BUFFER=0`), sinon `imgui_impl_opengl2` (tableaux côté
  client) interpréterait ses pointeurs comme des offsets → UI vide. Réglages : menu
  **Affichage → Effets CRT** (panneau live), CLI `--crt` / `--crt-preset off|leger|arcade|
  phosphor`, persistance dans `neost.cfg` (13 clés `crt_*`). Figé en kiosk.
- **Mode kiosk (borne / expo) — plein écran + menu in-game (2026-07-10)** : `--kiosk`
  (vrai plein écran EXCLUSIF, garde le focus, reste au-dessus de tout), `--kiosk-monitor N`.
  Config **figée** (`neost.cfg` jamais réécrit),
  souris capturée, joystick clavier activé. **Zoom adaptatif** (défaut, bascule F10) : cale
  la ZONE ACTIVE (rectangle matériel `activeTop`/`activeHeight`, jamais au pixel → zéro
  saccade) sur la hauteur écran ; quand la Glue signale une **bordure ouverte**
  (`Shifter::bordersOpen()` = `bordersTrick_` ou overscan V) on montre le buffer entier
  (hystérésis ~0,6 s anti-clignotement). **Menu in-game** (START/F9, jeu en PAUSE) : deux
  menus basculés G/D — liste des jeux triée par proximité (les phases B/C/D du jeu monté en
  tête via `neost::areSiblingImages`, `src/io/MediaScan.cpp`, préfixe+suffixe commun) et actions (Redémarrer / Clavier &
  souris / Quitter). Insérer une disquette = **échange à chaud SANS reboot** ; seul
  « Redémarrer » relance. **Page Clavier & souris** (SELECT/K, sans pause) : frappe brève
  (MAKE puis BREAK différé de ~4 trames) injectée au jeu qui tourne dessous. Navigation
  manette/clavier à **répétition temporelle** (400 ms puis 150 ms, indépendante du framerate
  à vide). Sorties : Alt+F4 ou Ctrl+Shift+Q (~0,7 s).
- **Menu kiosk — anglais, icônes FA, dossiers ROM & défilement rapide (2026-07-10)** :
  libellés du menu in-game passés en **anglais** (borne) + pictogrammes Font Awesome
  (gamepad, disque, clavier, dossier…). Nouvelle action **ROM FOLDERS** : gérer des dossiers
  de jeux/disques **additionnels** (scannés en plus de `disks/`), via un **navigateur piloté
  à la manette** parcourant tout le FS (chemin ABSOLU → « .. » remonte jusqu'à `/`) avec
  **raccourcis** racine / Home / **volumes montés** (portable : `/Volumes` macOS,
  `/run/media`·`/media`·`/mnt` Linux, filtrés par `is_directory`). Chaque dossier se retire
  d'une **croix ×** et est **auto-purgé** s'il n'existe plus ; liste **persistée** même en
  kiosk (`kiosk_romdir=` multiples, `saveConfig(force)`). **L1/R1** (ou Page↑/↓) = saut de
  page dans la liste des jeux. Footer « Roms found: N » (réserve calée sur le contenu, sans
  espace vide). Navigation manette/clavier à répétition temporelle sur toutes les pages.
- **Étalon V2 « nocooper » rapatrié et intégré (2026-07-08)** : No Cooper (1984, freeware —
  archive Fujiology) dans `disks/etalons/nocooper.msa`, entrée `etalons.json` avec fetch auto
  (ZIP membre) et **pilotage daté** (`keys_at` : espace tenue vbl 900 — support ajouté à
  `run_etalons.py`). L'écran principal (~trame 6800) ouvre les bordures G/D en **MED-RES**
  (cas d'école `Video_WriteToGlueRes`, chantier V2) : la CIBLE oracle est archivée
  (`tests/reference/nocooper_oracle.png`, Hatari WS3, alignement frame 6788 ↔ NeoST 6800) ;
  la référence courante (`--update-ref`) capture l'état NeoST = non-régression, écart V2
  assumé **891 px** (crop actif — chiffre de départ du chantier). Suite étalons : 9 + selftest,
  TOUS OK.
- Écran ST dans une fenêtre ImGui ; visualiseur hexa + registres 68000 ; boutons Reset /
  Hard Reset ; barre résolution. Bridage **50 fps réels**. Persistance (`neost.cfg`).
- **Profils de réglages nommés (2026-08-10)** : page `Profiles` de la fenêtre Configuration
  (ou `Machine → Settings profiles…`) — `Save current settings` / `Load` / `Overwrite` /
  `Delete` (confirmation en deux temps). Un fichier `profiles/<nom>.cfg` par profil, à côté
  de `neost.cfg` et **au même format** (`parseConfigLine`/`writeConfigKeys` partagés,
  écriture atomique). Contenu : modèle, RAM, FPU, ROM, supports montés (A/B/cartouche/
  GEMDOS/ACSI), moniteur, CRT, son, entrées. **Hors profil** : `rtc=` (état machine),
  `kiosk_romdir=` (installation), `dock=`/`showXxx=`/`uiVersion=` (disposition ImGui) —
  et ce qu'un profil ne dit pas reste inchangé au chargement. Charger = `reqRebuild`
  (`applyConfig`) + requêtes de montage pour les lecteurs. Nom de fichier assaini
  (anti-traversée de chemin). **Écriture interdite en kiosk** (config figée), lecture OK.
- **Son du lecteur mémorisé** (`drivesound=`, 2026-08-10) : la case « Floppy drive sound »
  ne survivait pas au relancement. Le câblage `DriveSound`→`Audio` suit désormais la
  DISPONIBILITÉ des échantillons (`roms/drivesound/`) et non le réglage — qui reste
  basculable à chaud ; case grisée si les échantillons manquent.
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
