# Précision cycle — modèle, acquis, restant (oracle : Hatari)

> (c) 2026 VERHILLE Arnaud — **référence durable** de la précision cycle de NeoST.
>
> - **La cible** = le véritable Atari ST et les variantes prises en charge.
>   La documentation et les mesures matérielles priment sur les émulateurs.
> - **Référence comparative** = Hatari : `extern/hatari/src` (lu, pas compilé),
>   sans reproduire ses défauts confirmés — surtout
>   `cycInt.c`/`cycInt.h` (ordonnanceur), `cycles.c` (compteurs), `video.c` (raster), `mfp.c`
>   (timers), `fdc.c` (disque).
> - **Le cœur 68000** = Moira (cycle-exact, `MOIRA_PRECISE_TIMING=true`). Seul cœur ; cf.
>   [`DEV.md`](../DEV.md). *(L'ancien Musashi a été retiré — toute mention « 2 cœurs » dans
>   d'anciennes notes est périmée.)*
> - **Le front actif** (convergence Moira↔WinUAE, beam-sync) vit dans son propre dossier :
>   [`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md). Ce document-ci pose le cadre et
>   l'inventaire ; ce doc-là tient le journal de la dernière passe.

## 1. Pourquoi — le timing au cycle change le comportement

Le ST n'a ni scrolling matériel, ni copper : tout effet vidéo passe par la réécriture des
registres Shifter/Glue (`$820A`, `$8260`, palette, base) **à un cycle précis** du 68000 pendant
le balayage. Symétriquement, beaucoup de jeux **pollent** un registre (compteur vidéo `$FF8209`,
flag effacé par une IRQ) et décident en fonction. Si la **phase CPU↔faisceau** dérive ne
serait-ce que de quelques cycles, l'écriture tombe à la mauvaise ligne (image qui saute) ou la
boucle de poll ne sort jamais (gel).

Cas d'école historique : **Arkanoid** boucle sur `tst.b $26E7 / bne` (`$31736`), où `$26E7` doit
être remis à 0 par une IRQ à un instant précis. Le gel `$31736` exigeait un FDC au timing réel
(spin-up + débit MFM) — **résolu** par le modèle FDC rotationnel (cf. `CHANGELOG.md`,
`[[arkanoid-freeze-investigation]]`). Arkanoid atteint désormais son écran-titre mais ne franchit
pas la partie (cause distincte, suivie au catalogue de `TODO.md`).

## 2. Le modèle d'Hatari (l'architecture cible)

### 2.1 Une horloge globale, des compteurs par instruction (`cycles.c`)

Un compteur de cycles **global** (`CyclesGlobalClockCounter`) partagé CPU + vidéo, avancé au
cycle. Chaque instruction CPU rapporte son **coût exact** ; `Cycles_GetInternalCycleOn{Read,
Write}Access` date un accès à la **fin du cycle bus** (`currcycle*2 + 4`), après alignement.

### 2.2 Ordonnanceur d'événements datés (`cycInt.c`)

Le cœur du modèle. Une **table d'interruptions futures**, une par source matérielle
(`INTERRUPT_VIDEO_VBL/HBL/ENDLINE`, `MFP_TIMERA..D`, `ACIA_IKBD`, `FDC`, `BLITTER`,
`DMASOUND_MICROWIRE`, `HDC_ACSI`…). On ne garde dans `PendingInterruptCount` que le nombre de
cycles jusqu'au **prochain** événement ; la boucle CPU le décrémente du coût de chaque
instruction ; à `≤ 0`, le handler dû s'exécute puis se **re-planifie** (absolu = période fixe
sans dérive ; relatif = depuis l'instant courant). Le **retard** (une instruction longue dépasse
l'échéance) est reporté au handler → **pas de dérive cumulée**.

### 2.3 Unité interne CPU↔MFP (anti-arrondi)

Pour synchroniser CPU (8021248 Hz) et MFP 68901 (2457600 Hz) **sans flottant** :
`1 cycle CPU → 9600 unités`, `1 cycle MFP → 31333 unités` (ratio exact `31333/9600`,
`CYCINT_SHIFT`). Idée d'Arnaud Carre (sc68). Indispensable pour des périodes de timers exactes
sur des milliers de trames. NeoST conserve les échéances A/B/C/D en unités internes de
**1/256 cycle CPU** (`Mfp::timerDueSub_`) et n'arrondit au cycle entier qu'au moment de les
présenter au Scheduler ; la fraction reste l'ancre du rechargement suivant et survit au
save/load (format v11).

### 2.4 Vidéo au cycle (`video.c`)

`video.c` planifie **par scanline** des événements à des cycles précis : début/fin de
Display-Enable (pour Timer B event-count et les bordures), `VIDEO_ENDLINE`, `VIDEO_HBL`,
`VIDEO_VBL`, avec gestion 50/60 Hz, écrans courts/longs, et suppression de bordures
(`BORDERMASK_*`). L'origine de trame est `VBL_ClockCounter = GlobalClock − PendingCyclesOver −
VblVideoCycleOffset` (offset 64 STF / 68 STE).

### 2.5 MFP (`mfp.c`) et FDC (`fdc.c`)

- **MFP** : timers A-D = événements datés, modes **delay** (prescaler 4/10/16/50/64/100/200),
  **event-count** (piloté par le Display-Enable vidéo ou un front GPIP), **pulse-width** ;
  latence d'IRQ (4 cyc) et IACK au cycle.
- **FDC** : disque **temporel** — moteur on/off + spin-up, index pulse (3,71 ms/rotation),
  step-rate, BUSY, DRQ/INTRQ via événements `INTERRUPT_FDC`, transfert par blocs dans le temps.

## 3. Ce qui est ACQUIS (phases 0-6 + extensions)

Le squelette cycle-exact est en place. Tout ce qui suit est **fait et validé** (détails et
validations dans `CHANGELOG.md`) :

| Brique | Essence | Réf. NeoST |
|--------|---------|------------|
| **Cycles par instruction** | `Cpu68k::run` rend le coût réel ; Moira est cycle-exact | `Cpu68k.cpp` |
| **Scheduler daté** | une échéance « prochaine » par source, handlers qui se replanifient | `Scheduler.hpp` |
| **Horloge continue + carry** | `runFrame` n'efface plus l'horloge par trame ; le dépassement est reporté | `Machine.cpp` |
| **Vidéo scanline** | `beginFrame` verrouille la résolution, `renderLine(y)` décode avec l'état courant | `Shifter.cpp` |
| **Événements vidéo au cycle** | rendu fin-DE, Timer B event-count à 400, HBL à 508 | `Machine.cpp` |
| **Géométries 50/60/71 Hz** | cycles/ligne (512/508/224), lignes/trame, DE start/end dérivés, verrouillés à `beginFrame` | `Shifter::Geometry` |
| **Timers MFP datés** | A/C/D mode délai + B event-count sur DE ; conversion entière MFP→CPU | `Mfp.cpp` |
| **Chaîne IRQ MFP fine** | délai 4 cyc daté (`MFP_IRQ`), chronologie multi-IRQ (`pendingTime_`), IACK ré-évalué | `Mfp.cpp`, `Cpu68k.cpp` |
| **FDC temporel** | BUSY posé, durée calculée, INTRQ différé ; modèle rotationnel complet (spin-up, débit MFM) | `Fdc.cpp` |
| **Registres STE différés** | compteur vidéo matérialisé (`vcLineBase_`) ; `$8205/07/09`, HSCROLL, LINEWIDTH appliqués en fin de ligne | `Shifter.cpp` |
| **Quantum sous la ligne** | `liveNow()` = cycle absolu exact à l'écriture (sous-instruction Moira) + préemption | `Scheduler`, `Cpu68k` |
| **Bordures H/B/G/D** | machine Glue STF (`updateGlueState` ≙ `Video_Update_Glue_State`), self-test 19/19 | `VideoGlue.cpp`, `VideoGlue.hpp` |
| **Spec512** | palette intra-ligne pixel-identique à l'oracle (latch + alignement bus shifter) | `Shifter.cpp` |
| **Wait states bus** | shifter/PSG/MFP/ACIA + E-Clock | `Bus.cpp`, `Cpu68k.cpp` |
| **Microwire/LMC1992 datés** | filtres son STE horodatés | `DmaSound.cpp` |

> **Convergence Moira↔WinUAE au niveau instruction = atteinte** (`NEOST_RAM_SLOT` align créneau
> bus + fix DIV). C'est l'objet du doc [`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md),
> qui tient le détail et le front beam-sync.

## 4. Ce qui RESTE — inventaire priorisé

Établi en diffant `extern/hatari/src` contre `src/` sous-système par sous-système. Trié par
priorité d'impact. Les divergences logiques bornées (V1, S2, D3, M1…) sont cataloguées avec
sévérité + `fichier:ligne` des deux côtés dans
[`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md) ; ici, l'angle « précision cycle ».

| Priorité | Chantier | Effort | Étalon type |
|----------|----------|--------|-------------|
| ~~P1~~ ✅ | Géométrie mid-trame (V3) — **CLOS le 2026-09-01** : attribution de ligne à la grille réelle promue par défaut, prouvée à l'oracle sur l'étalon généré `freq_switch` (18/18 écritures freq à la même ligne/cycle qu'Hatari) ; le verrou ne faisait rien jusque-là (longueur de ligne non persistante) | — | overscan plein écran, ULM Dark Side |
| ~~P1~~ ✅ | Déclenchement du rendu des lignes masquées sans palette corrigé ; retrait bas gardé. Bord final STOP_MIDDLE (3 pixels) et couleur physique du blanking restent ouverts | — | sondes libres `blankline_*` |
| **P2** | Blitter : comparaison globale MegaSTE 16 MHz/IRQ (CPU/cache qualifié séparément ; HOG/partagé et BL5 clos) | moyen | démos CPU+blitter simultanés |
| **P2** | Son DMA STE : quantification HBL du refill à confronter à l'oracle (compteur live et FIFO PORTÉS) | moyen | STE_Test, sync zik/raster |
| **P2** | Restes vidéo : `VIDEO_ENDLINE`, VBL au cycle exact, phase Timer C | moyen | démos fullscreen |
| **P3** | Qualification WS1-4 (table portée, WS3 par défaut), overscan med-res ; NO_SYNC/SYNC_HIGH comparés sur sondes générées, mesures matérielles ouvertes | élevé | démos « extrêmes » (Closure…) |
| **P3** | Fréquences exactes centralisées (unité interne ×256 des timers MFP : faite) | faible | dérive long terme |

### ✅ Beam-sync : CLOS (2026-07-09, complété 2026-08-06) — ex-P1 de tête

Mis à jour le 2026-08-27 (A26) : cette section décrivait le chantier tel qu'ouvert le
2026-06-18 (« NeoST culmine cyc 476-492 vs Hatari 500-508 → le retrait haut d'EL ne
tient pas ») — c'était vrai ALORS, et trois passes l'ont clos depuis :

- **Cause racine** (5ᵉ passe, 2026-07-02) : double comptage du saut STOP dans la
  comptabilité de quantum → `sched.now()` en avance de δ∈{4..26} sur l'horloge CPU.
  Lock moteur Enchanted Land 46,9 % → **100 % (12402/12402)**.
- **Datation lecture/écriture** (2026-07-03) : couple read −6 / write +2 → menu robot
  Cuddly, clignotement 10-47 % → **0 (250/250)**.
- **Re-mesure oracle** (2026-07-09) : datation re-arm 438/442/446 (σ 3,0) — meilleure
  que la cible Hatari (~444 ±8) ; diff `$8209` d'entrée **byte-identique** ; verdict
  écrit « beam-sync EL CONVERGÉ, transitoire d'entrée inclus — aucun résidu NeoST ».
- **Super Hang-On** (10ᵉ passe, 2026-08-06) : IACK MFP vectorisé 12→16 cyc.

**Horloges IACK MegaSTE (2026-10-19)** : les quatre cycles internes après
acquittement restent des cycles CPU. Le bloc périphérique reste en cycles bus :
MFP 20 cycles CPU à 8 MHz, 36 à 16 MHz (au lieu de 40) avec la calibration
actuelle. HBL/VBL séparent aussi attente E-clock, bloc périphérique et idle CPU.
199 contrôles CPU/blitter sous ASan/UBSan, dont les dix phases vidéo aux deux
fréquences ; les douze sondes MegaSTE vérifient 72 blocs IACK par capture.
Les créneaux RAM peuvent masquer ce gain dans la date d'entrée du handler :
ce correctif ne clôt pas la qualification globale ni la mesure matérielle des
latences périphériques. Détails :
[`HATARI_MEGASTE_AUDIT.md`](HATARI_MEGASTE_AUDIT.md).

**Mesure IACK préparée (2026-10-19)** : le diagnostic d'Hatari converti en
cycles bus laisse +3/+4 cycles à 16 MHz, contre +11/+12 avant conversion.
Aucune constante de NeoST n'est ajustée sur cette base. Le PRG relogeable
et la disquette AUTO de la [sonde matérielle](MFP_IACK_MEASUREMENT.md) sont
contrôlés sur NeoST (MegaSTE, refus ST/STE) ; la capture réelle reste à faire.

Journal complet, mesures et pistes éliminées :
[`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md). L'heuristique **V2**
res-switch (line-shortening hi-res ≤56 cyc, opt-in `NEOST_V2`) est supplantée par le
canal longueurs de ligne (`NEOST_LINELEN`, ON par défaut depuis le tranchage WS3) —
elle reste un outil d'A/B, pas un chantier.

### P1 — Registres vidéo STE : 2 restes

**Capture live de la bordure basse vérifiée (2026-10-19)** :
`Machine::onHbl` appelle déjà `Shifter::commitScanline`, qui prolonge les captures
jusqu'à la fin d'affichage Glue lorsque le bas est ouvert. Le replay utilise
ces captures plutôt que la RAM de fin de trame. Une régression de
`glueSelfTest` remplit l'écran, capture jusqu'à la ligne 280, efface la RAM
puis vérifie les pixels conservés dans la bordure basse ; sans retrait, cette
ligne garde la couleur de bordure. Un second commit de la même ligne n'avance
pas le compteur. Les deux variantes de grille passent ; rétablir la limite
aux lignes actives fait échouer capture et pixels. Ce contrôle protège la
chronologie interne ; il ne constitue pas une nouvelle mesure matérielle.

**Lignes masquées sans palette (2026-10-19)** : `finishFrame` ne déclenchait
le rendu Glue que sur extension de bordure, écriture palette ou VBlank actif.
Une impulsion hi-res laissant NO_DE ou STOP_MIDDLE sans élargissement était
correctement détectée, mais l'image conservait les pixels du rendu nominal.
Le rendu final traite désormais ces masques même sans écriture palette,
sans transformer ce cas en extension de bordure pour le cadrage adaptatif.
Onze contrôles Glue vérifient stride (BLANK 162, NO_DE 0, STOP_MIDDLE 54 octets
pour les stimuli choisis), masquage et absence d'extension. Deux échouent
avant correction dans chacune des deux grilles. Le générateur libre
`make_blankline_test.py` exerce quatre séquences sur ST et STE, dont une
contre-épreuve de la fenêtre de préchargement propre au STE.
`EMPTY_LINE` et `BLANK_LINE` sont des drapeaux historiques d'Hatari, déclarés
mais plus produits au pin courant : aucune branche morte n'est portée.
Le masquage utilise encore la palette 0 comme Hatari ; sa couleur physique
reste à mesurer, indépendamment de cette correction du chemin de rendu.
Six sondes BLANK/NO_DE concordent à zéro pixel avec Hatari. Les deux
STOP_MIDDLE sont des **snapshots**, pas des oracles : résidu de trois pixels
au bout du dernier groupe de plans, stable sur 61 captures Hatari par machine.
Le choix entre découpage des pixels au DE et recopie du groupe partiel reste
ouvert faute de mesure matérielle ; aucune compensation ajoutée.

**NO_SYNC/SYNC_HIGH, qualification comparative (2026-10-19)** : les branches
étaient déjà portées. Huit sondes supplémentaires (deux tricks et leurs
annulations, ST/STE) exercent la ligne suivante NO_DE/NO_COUNT, l'absence
d'avance vidéo et de tic Timer B, et la ligne ajoutée en bas de l'affichage.
Les longs successifs en RAM rendent un stride faux visible sur les lignes
suivantes. 52 contrôles Glue vérifient aussi l'accord live/replay, l'avance
totale de 32 000 octets et l'annulation complète des masques et de l'extension.
Pas de changement d'exécution pour obtenir cet accord ; les signaux physiques
HSYNC/VDE et la couleur du blanking restent à mesurer sur Atari.

- ✅ **`bSteBorderFlag` / mode 336 px (2026-10-18)** : combo `$FF8265>0` puis
  `$FF8264=0` dans les 40 cycles de la même VBL → 16 px de plus à gauche et
  8 octets supplémentaires par ligne. Latch persistant, écriture différée, compteur
  vidéo et re-rendu par ligne portés. Cinq étalons générés `scroll_336*` couvrent
  activation octet/mot, délai dépassé, annulation et activation mi-écran suivie d'une
  palette tardive. Obsession et Pacemaker n'ont pas été rejoués dans cette passe.
- **`RestartVideoCounter` ligne 310/260** : ✅ **PORTÉ (2026-07-02)** — événement
  `Scheduler::VC_RESTART` (`src/core/Machine.cpp:385-389` callback, `:454-460` planification).
  Le couplage à la géométrie par-ligne (V3) pour les bascules 50/60 Hz en cours de trame est
  levé depuis la clôture de V3 (2026-09-01, attribution à la grille réelle par défaut).
  Étalon : ULM Dark Side of the Spoon.

### P2 — Blitter non-hog (`blitter.c:251,395`)

✅ **PORTÉ le 2026-07-07** — le partage de bus non-hog est implémenté : tranches de 64 accès
bus / 64 accès CPU **réels**, suspension MID-MOT (l'état du mot en cours survit à la coupure),
et le bug matériel « +1 accès CPU compté blitter » (`busCountError_` → tranche de 63).
Cf. `src/core/Blitter.cpp:22-26` (constantes), `:326-329` (`busCountError_` → tranche de 63)
et `:318-376` (`onSlice` + `noteCpuBusAccess` : tranche + comptage).

**Port complémentaire du 2026-10-19** : reprise PRE_START pendant l'instruction,
bornes des cycles bus avant/après accès, recouvrement des cycles CPU internes
HOG/partagé, verrou TAS mémoire et IRQ après restitution du bus. Dispatch DMA
par accès préservé via rebase du quantum. Dix-sept sondes temporelles Hatari
passent sans KNOWN, dont dix blitter (tailles, MULU, TAS et exécution RAM) ; fin
observée à +4 cycles, IRQ à +2 en ROM / +4 en RAM, compte CPU identique. Crédit
sauvegardé en v49. Alignement RAM déplacé au début du cycle, avant SYNC(2) :
BL5 pixel **clos**, `blitter_timer` à zéro pixel contre Hatari, promu en oracle.
**MegaSTE 16 MHz, complément** : alignement des misses avant le cycle CPU,
choix du hit avant les cycles, donnée déjà lue conservée pendant une DMA et
invalidation cache avant arbitration. Onze sondes CPU/cache sans IRQ ni DMA
passent en égalité exacte contre Hatari (`run_megaste_bus_oracle.py`). Les
intervalles Hatari en cycles CPU sont divisés par deux, jamais les dates de
bascule. Tests locaux des quatre phases RAM et du chevauchement cache/DMA.
**Complément global** : douze configurations NeoST passent les contrôles de
données, IRQ et bornes physiques. PRE_START corrigé à 16 MHz (cinq cycles bus
depuis le callback CPU, au lieu de sept). **Reste** : comparaison temporelle
exacte ; deux défauts Hatari isolés, et environ 12–24 cycles résiduels après
corrections expérimentales. Aucun échec promu en référence. Voir
[`HATARI_MEGASTE_AUDIT.md`](HATARI_MEGASTE_AUDIT.md).

### P2 — Son DMA STE (`dmaSnd.c:737`)

✅ **FIFO 8 octets PORTÉE** (`src/core/DmaSound.cpp:94` `fifoRefill`, `:131` `fifoPull`),
ainsi que le gain LMC ×2 (S3, `kLmcMakeup`, `:436-450`).

✅ **Compteur `$FF8909/0B/0D` au cycle PORTÉ (2026-08-06)** : `DmaSound::liveCounter`
(`DmaSound.cpp:500-510`) est un port de `DmaSnd_GetFrameCount` — il appelle `updateDac`
(≙ `Sound_Update`) à la lecture, et rend l'adresse de DÉBUT à l'arrêt. Il ne refill PAS la
FIFO au passage : le refill reste quantifié au HBL (`onHbl`) ou déclenché à vide (`fifoPull`),
ce qui est le comportement voulu — un poll serré dans une ligne doit voir l'adresse sauter par
paquets, pas avancer en continu.

**Reste** : confirmer cette quantification à l'oracle (aucun A/B Hatari n'a encore été fait sur
un poll serré de `$FF8909/0B/0D`). Étalon : STE_Test.

### P2 — Restes vidéo « plan »

`VIDEO_ENDLINE` distinct du HBL ; VBL au cycle exact (vraie fin de trame + offset 64/68) ; phase
exacte du tic Timer C (au cycle de programmation) ; lecture compteur à cheval sur deux lignes
(`Video_CalculateAddress`).

### P3 — Précision « démos extrêmes »

- **Wakeup states WS1-4** (`video.c:626`) : 4 jeux de timings ±1 cyc. La table est portée
  (`glue::wakestate()`, `kWsInc` dans `VideoGlue.hpp`, A/B par `NEOST_WS=1..4`) ; WS3 reste le
  défaut, choix acté pour le mode cycle-exact.
- **Jitter HBL/VBL** : ⚠ `HblJitterArray/VblJitterArray` sont **morts** dans le Hatari courant
  (déclarés, jamais définis/référencés) — NE PAS porter. Le vrai jitter d'autovecteur = synchro
  **E-Clock** (`M68000_WaitEClock`), porté (`NEOST_IACK`).
- **Branche STE de la Glue** (V1) : `Preload_Start_*` (table `VideoGlue.hpp`, utilisée par
  `VideoGlue.cpp`) et `LEFT_OFF_2_STE`/`_MED` sont désormais gérés ; l'overscan des démos STE
  (E605, DHS) reste à rejouer contre l'oracle.
- **Overscan med-res** ; NO_SYNC/SYNC_HIGH (lignes vides par manipulation du
  sync hi-res) comparés sur sondes ST/STE, comportement électrique non mesuré.

### P3 — Horloges & divers

- ✅ **Unité interne ×256** (`CYCINT_SHIFT=8`) : faite le 2026-08-14 pour les timers MFP
  A/B/C/D (`Mfp::timerDueSub_`, cf. § 2.3).
- **Fréquences exactes centralisées** (`clocks_timings.c` : CPU 8021248 Hz PAL, VBL ≈ 50,05 Hz) :
  seul impact réel = synchro audio long terme.
- **Offsets de datation Moira vs Hatari** : `kVideoCounterReadOffsetCyc = −6`
  (`ShifterInternal.hpp:37`), `kSyncWriteOffsetCyc = +2` (`ShifterInternal.hpp:41`) et
  `kSpec512AlignCyc = −25` (`Shifter.cpp:36`) ne sont **pas** des constantes empiriques : ce
  sont les valeurs fidèles dérivées de `Video_CalculateAddress` et
  `Cycles_GetInternalCycleOn{Read,Write}Access` (2026-07-03). ⚠ **read et write se déplacent
  PAR PAIRE** — bouger l'un seul casse Enchanted Land. Chaque nouveau mécanisme daté choisira
  son offset par la même méthode (dérivation, puis contrôle au sweep vs oracle).

## 5. Ce qu'on ne fera PAS (décisions actées)

- **Contention DMA vidéo générale sur la RAM** (le shifter vole des cycles au CPU pendant le DE) :
  modèle **MAME** (`stmmu.cpp::bus_contention`), **pas Hatari** → l'ajouter ferait diverger NeoST
  de l'oracle qui valide nos étalons au pixel.
- **Vol de cycles DMA son / FDC sur le bus** : Hatari ne le modélise pas non plus.
- **Pulse-width MFP (modes 9-15)** : Hatari l'approxime déjà en mode délai → même approximation.

## 6. Méthode de validation

Chaque chantier : **(1)** porter depuis `extern/hatari/src` → **(2)** re-tester les étalons
(`tools/run_etalons.py`, glue-selftest 19/19, Spec512/overscan_top byte-identiques) → **(3)**
diff à l'oracle Hatari.

```sh
# Hatari (référence) headless — cf. docs/HATARI_AUTOMATION.md
SDL_VIDEODRIVER=dummy hatari --machine st --monitor rgb --sound off \
  --tos <TOS> --disk-a <jeu.st> --trace cpu_disasm --trace-file hatari.txt --run-vbls N
# NeoST
./build/neost-headless <TOS> --frames N --disk <jeu.st> --trace neost.txt --regs --irq
# Diff : première divergence (PC + registres), ou périodes par PC-landmark
python3 tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs
python3 tools/trace_diff.py neost.txt hatari.txt --periods 173C 1742   # convergence cycle
```

**Indicateur de progrès** : à chaque phase, le point de première divergence recule, et les
points d'insertion d'IRQ coïncident avec Hatari (le diff cesse d'être « bruité » par le décalage
des interruptions). Pour le harnais différentiel de cycles (Moira↔WinUAE) et les bancs dédiés
(`make_cycle_bench.py`, `make_respulse_test.py`), voir
[`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md). Catalogue des logiciels étalons par
sous-système : [`TEST_SOFTWARE.md`](TEST_SOFTWARE.md).
