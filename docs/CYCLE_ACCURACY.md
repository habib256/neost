# Cycle-accuracy NeoST — plan calqué sur Hatari

> (c) 2026 VERHILLE Arnaud — feuille de route technique.
>
> **Source de vérité : Hatari** (`extern/hatari`, non committé — cf. `.gitignore`).
> Fichiers de référence cités tout au long : `src/cycInt.c` / `src/includes/cycInt.h`
> (ordonnanceur), `src/cycles.c` / `cycles.h` (compteurs), `src/video.c` (raster),
> `src/mfp.c` (timers 68901), `src/fdc.c` (disque), plus le cœur CPU UAE.

## 1. Pourquoi (le symptôme Arkanoid)

Arkanoid (1987, Imagine) est **auto-bootable** via le dossier `AUTO\ARKANOID.PRG`
de sa disquette (et **non** via le boot sector, dont le checksum vaut `$F9B9` ≠
`$1234`). NeoST le charge correctement et affiche l'écran-titre, puis **se fige**
sur :

```
031736: tst.b $26e7.l
03173C: bne   $31736          ; boucle tant que ($26E7) != 0
```

Analyse par les traces (`neost-headless --trace` + Hatari `--trace cpu_disasm`) :

- Le jeu installe son handler VBL en `$70` (`034CB2: move.l #$34c9a,$70.l`), qui
  s'exécute bien 117 fois (vectorisation auto-vecteur niveau 4 correcte) ; il
  chaîne vers le VBL TOS sans toucher `$26E7`.
- `$26E7` doit donc être remis à 0 par une **autre interruption**, à un **instant
  précis** relatif à la boucle d'attente.
- NeoST lève ses IRQ à la granularité **ligne/trame** (cf. §2) → l'interruption
  qui efface `$26E7` ne tombe pas au bon cycle → la boucle ne sort jamais.

C'est un cas d'école : **sans timing au cycle près, un flag effacé par IRQ au
mauvais moment fige le jeu.** Beaucoup de jeux/démos sont dans ce cas.

## 2. État actuel de NeoST — le modèle « par blocs »

`Machine::runFrame()` (cf. `src/core/Machine.cpp`) :

```cpp
for (int line = 0; line < LINES_PER_FRAME; ++line) {   // 313 lignes PAL
    cpu.run(CYCLES_PER_LINE);                           // 512 cycles EN BLOC
    if (line < VISIBLE_LINES) { mfp.hblank(); cpu.raiseHbl(); }   // HBL par ligne
    if (line == 78|156|234|312) { mfp.raise(SRC_TIMERC); cpu.updateIpl(); }
    if (line == VISIBLE_LINES) cpu.raiseVbl();          // VBL niveau 4
}
shifter.renderFrame();                                  // 1 décodage / trame
```

Limites structurelles :

- Une IRQ **ne peut pas tomber au milieu** d'une ligne (granularité 512 cycles).
- **Timer C** approximé par 4 tics à des lignes fixes ; **Timer A, B (delay), D**
  absents ; **Timer B sur Display-Enable** impossible au cycle.
- **FDC = DMA instantané** : pas de spin-up, index pulse, BUSY, DRQ/INTRQ datés.
- **Vidéo** = un seul décodage par trame : pas de bordures, ni Spec512, ni rasters.
- `cpu.run(512)` exécute « au moins » 512 cycles puis s'arrête à la fin d'une
  instruction : le **dépassement** (lateness) n'est ni mesuré ni reporté → dérive.

Suffisant pour booter EmuTOS/TOS et le bureau ; **insuffisant** dès qu'un logiciel
dépend du cycle (jeux protégés, démos, et le `$26E7` d'Arkanoid).

## 3. Le modèle d'Hatari (à porter)

### 3.1 Compteurs de cycles (`cycles.c` / `cycles.h`)

Un compteur de cycles **global** ; chaque instruction CPU rapporte son **coût
exact**. Hatari maintient `Cycles_GetCounter(CYCLES_COUNTER_CPU)` et convertit
selon l'horloge machine (8/16 MHz).

### 3.2 Ordonnanceur d'événements datés (`cycInt.c` / `cycInt.h`)

C'est le cœur. Une **table d'interruptions futures** (`InterruptHandler`), une par
source matérielle (`cycInt.h`) :

```
INTERRUPT_VIDEO_VBL / VIDEO_HBL / VIDEO_ENDLINE
INTERRUPT_MFP_MAIN_TIMERA..D
INTERRUPT_ACIA_IKBD, INTERRUPT_FDC, INTERRUPT_BLITTER,
INTERRUPT_DMASOUND_MICROWIRE, INTERRUPT_HDC_ACSI, ...
```

Principe (commentaire d'en-tête de `cycInt.c`) :

- On garde, dans `PendingInterruptCount`, le nombre de cycles jusqu'au **prochain**
  événement seulement (pas besoin de décrémenter chaque entrée).
- La boucle d'exécution **décrémente** `PendingInterruptCount` du coût de chaque
  instruction ; à `≤ 0`, le handler dû est appelé (`CycInt_AcknowledgeInterrupt`)
  puis se **re-planifie** :
  - **Absolute** (`CycInt_AddAbsoluteInterrupt`) : période fixe depuis l'événement
    précédent — ex. HBL toutes les 512 cycles, sans dérive.
  - **Relative** (`CycInt_AddRelativeInterrupt`) : à partir de l'instant courant.
- **Retard** : si une instruction de 20 cycles dépasse de 16 un événement dû dans
  4, le handler reçoit/gère ces 16 cycles de retard (fonctions *adjust*) → **pas
  de dérive cumulée**.

### 3.3 L'unité « interne » CPU↔MFP (anti-arrondi)

Pour synchroniser le CPU (8021248 Hz) et le MFP 68901 (2457600 Hz) **sans
flottant**, Hatari convertit tout en unité interne :

```
1 cycle CPU  → 9600  unités internes
1 cycle MFP  → 31333 unités internes      (ratio exact 31333/9600)
```

(cf. `INT_CONVERT_TO_INTERNAL` / `INT_CONVERT_FROM_INTERNAL`, `CYCINT_SHIFT`).
Idée d'Arnaud Carre (Saint/sc68). Indispensable pour des périodes de timers MFP
exactes sur des milliers de trames.

### 3.4 Vidéo au cycle (`video.c`)

`video.c` planifie, **par scanline**, des événements à des positions en cycles
précises : début/fin de **Display-Enable** (pour Timer B event-count et les
bordures), `VIDEO_ENDLINE`, `VIDEO_HBL`, `VIDEO_VBL`, avec gestion 50/60 Hz,
écrans courts/longs et suppression de bordures (`BORDERMASK_*`).

### 3.5 MFP 68901 (`mfp.c`)

Les 4 timers (A–D) sont des **événements datés** : modes **delay** (prescaler
4/10/16/50/64/100/200), **event-count** (piloté par le Display-Enable vidéo ou un
front GPIP), **pulse-width** ; rebouclage via la période ; latence d'IRQ et IACK
**au cycle**. C'est ici que se règle le `$26E7` d'Arkanoid.

### 3.6 FDC/DMA (`fdc.c`)

Disque **temporel** : moteur on/off + spin-up, **index pulse** (3.71 ms/rotation),
step rate, **BUSY**, **DRQ/INTRQ** via événements `INTERRUPT_FDC`, transfert par
blocs dans le temps. Nécessaire aux protections et au timing fin.

## 4. Plan incrémental pour NeoST

Chaque phase est **validable** et garde le repo fonctionnel. On porte l'**idée**
d'Hatari, pas son code (licences/architecture différentes) : NeoST garde son
`Bus`-plan-mémoire et son cœur Musashi.

### Phase 0 — Cycles par instruction (fondation) — ✅ FAIT

- `Cpu68k::run(int cycles)` retourne déjà `m68k_execute(cycles)`, soit le nombre
  de cycles **réellement** consommés (le 68000 finit toujours son instruction).
  La fondation « coût réel par paquet » est donc acquise ; il restera (Phase 5) à
  *carry* le dépassement entre paquets plutôt que de le jeter.
- Référence : cœur UAE d'Hatari + `cycles.c`.

### Phase 1 — `Scheduler` événementiel (refactor iso-comportement) — ✅ FAIT

- Nouveau composant `neost_core` : `src/core/Scheduler.hpp` (header-only)
  - une échéance en cycles par source (`enum Source { HBL, TIMER_C, VBL, … }`),
    `schedule(src, atCycle)`, `cancel`, `nextDue()`, `runTo(cycle)` qui déclenche
    les handlers échus **dans l'ordre des sources** (priorité à cycle égal) ;
  - chaque handler se **replanifie** lui-même (idée `cycInt` : une seule échéance
    « prochaine » par source).
- `Machine::runFrame()` réécrit en boucle pilotée par événements (cf. `Machine.cpp`):
  ```cpp
  while (sched.now() < frameEnd) {
      int64_t next = min(sched.nextDue(), sched.now() + CYCLES_PER_LINE);  // quantum = 1 ligne
      cpu.run(next - sched.now());      // exécute le CPU jusqu'à l'événement
      sched.runTo(next);                // déclenche HBL / Timer C / VBL (+ replanif.)
  }
  ```
- **Quantum CPU = la ligne (512 cycles)** : comme toutes les échéances sont sur la
  grille de 512, chaque pas exécute exactement une ligne → **chunking et trace
  identiques** au modèle « par blocs » précédent.
- **Validation faite** : `diff` des traces (Arkanoid 3 058 584 instructions, et
  EmuTOS avec registres) **strictement identiques** avant/après le refactor →
  zéro régression. Le *carry* du dépassement et la subdivision du quantum
  viendront aux phases suivantes (c'est là que le timing changera réellement).

### Phase 2 — Vidéo sur l'ordonnanceur (`video.c`) — ✅ FAIT (rendu scanline)

- `Shifter::beginFrame()` (verrouille la résolution de la trame) + `renderLine(y)`
  (décode UNE scanline avec l'état **courant** des registres palette/base vidéo).
- `Machine` planifie un événement `RENDER` par ligne : la scanline est décodée à
  la fin de sa ligne, donc un changement de palette/base via un handler HBL/VBL
  s'applique **ligne à ligne** (rasters, scroll vertical par base). En mono
  (400 lignes > 313 du cadre PAL), les lignes restantes sont finies après la
  boucle.
- Le rendu est purement « sortie » (lit la RAM, n'altère ni CPU ni IRQ) →
  **trace CPU inchangée** ; **validation** : trace Arkanoid identique +
  screenshots **pixel-identiques** (Arkanoid couleur, bureau EmuTOS, mono).
- **Reste dans la Phase 2** (ces points CHANGENT la trace, à valider via Hatari) :
  positionner **HBL et Display-Enable au cycle exact** dans la ligne (et non en
  fin de ligne), `VIDEO_ENDLINE`, bascule 50/60 Hz, et rendu **sous-ligne** pour
  Spec512 / changements multiples par scanline.
- Débloque ensuite : Timer B sur DE au cycle, base des **bordures**.

### Phase 2b — Cycles vidéo exacts + carry du dépassement — ✅ FAIT

- **Boucle avec carry** (`Machine::runFrame`) : plus de quantum « ligne » ; on
  exécute le CPU jusqu'au prochain événement et on avance l'horloge du nombre de
  cycles **réellement** consommés par `m68k_execute` (le dépassement est reporté,
  comme Hatari) → l'événement échu se déclenche « en retard » de quelques cycles,
  sans dérive. (C'est aussi la Phase 5 « carry ».)
- **Événements vidéo au cycle exact dans la ligne** (constantes STF PAL 50 Hz,
  `Hatari video.h`) : rendu de la scanline à la fin du Display-Enable (**cycle
  376**), **Timer B** event-count à **400** (`376+24`), **HBL** niveau 2 à **508**
  (`512-4`). Le rendu d'une ligne précède donc les handlers Timer B/HBL de la même
  ligne (qui modifient les registres pour la ligne suivante) → rasters corrects.
- **Validation** : screenshots **pixel-identiques** (bureau EmuTOS, Arkanoid
  couleur, mono) malgré le changement de timing ; boot OK. `tools/trace_diff.py`
  corrigé pour le vrai format Hatari `cpu_disasm` (`adresse octets disasm`) et
  validé contre une trace Hatari réelle.
- **Reste** : `VIDEO_ENDLINE`/bascule 50-60 Hz, rendu **sous-ligne** (Spec512),
  positionnement cycle-exact du VBL et du Timer C (ce dernier en Phase 3).

### Phase 3 — Timers MFP datés (`mfp.c`) — ✅ FAIT (mode délai A/C/D)

- **Horloge continue** : `runFrame` ne remet plus l'horloge à 0 par trame ; les
  événements vidéo sont armés à `frameStart_ + offset`. Les timers MFP traversent
  donc les trames.
- **MFP ↔ Scheduler** : `Mfp::setScheduler` ; sur écriture de TACR/TCDCR/TxDR, le
  MFP calcule la **période en cycles CPU** (`prescaler × données × 31333/9600`,
  conversion entière exacte MFP→CPU comme `cycInt.c`) et (re)date l'échéance du
  timer. À l'échéance : IRQ levée + replanification (mode délai).
- **Timer C réel** (remplace le faux « 4 tics/trame » de `Machine`) + **Timer D**
  (jusque-là totalement absent) + Timer A. Timer B reste event-count sur DE.
- **Validation fonctionnelle** : boot **pixel-identique** (bureau EmuTOS, Arkanoid),
  et la trace `--irq` montre désormais les vecteurs Timer C (`$45`) **et** Timer D
  (`$44`) qui se déclenchent réellement.
- **Note** : le diff instruction-par-instruction avec Hatari diverge sur les
  boucles de poll (ex. sync vidéo `FC0E3C` qui poll le compteur Timer B) car
  **Musashi et l'UAE d'Hatari n'ont pas exactement les mêmes coûts cycles** ; les
  deux sortent correctement la boucle. La validation pertinente est donc
  fonctionnelle (rythme des timers, boot, rendu), pas l'égalité de trace.
- **Reste** : Timer B mode délai, pulse-width, latence IACK au cycle, et la
  position exacte du tic Timer C (phase au cycle de programmation).

> **Arkanoid** : rappel — ce dump cale au même endroit (`$26E7`) **dans Hatari
> aussi** ; ce n'est pas un problème de timers. Cf. `TODO.md`.

### Phase 4 — FDC/DMA temporel (`fdc.c`) — ✅ FAIT (BUSY + INTRQ différé)

- `Fdc::setScheduler` ; une commande WD1772 n'est plus instantanée : on POSE
  **BUSY** (statut bit 0), on calcule une **durée** (`Fdc::commandDelayCycles` :
  type I = step-rate × pas ; type II/III = ~temps de transfert par secteur), et
  on date la fin sur `Scheduler::FDC`. À l'échéance (`onCommandComplete`), BUSY
  tombe, le statut final s'applique et l'**INTRQ** est levée (GPIP5 pour le
  polling TOS + canal 7 pour les jeux).
- Le transfert DMA des données reste immédiat (données en RAM) ; seule la
  **signalisation** (BUSY → INTRQ) est datée — suffisant pour que TOS/jeux qui
  attendent la fin de commande voient un délai réaliste.
- **Validation** : EmuTOS (sans disque) **pixel-identique** ; TOS 1.02 + diskA
  boote ; **Arkanoid se charge et son titre est pixel-identique** (données FDC
  correctes) ; smoke-test + montage WASM OK.
- **Reste** : débit réel (~16 ms/secteur, ici accéléré ~1 ms), index pulse /
  spin-up moteur, DRQ octet-par-octet, latence de rotation, write-protect/Mediach.

### Phase 5 — Unité interne CPU/MFP (`cycInt.c`)

- Adopter la conversion entière 9600/31333 (ou un rationnel 64 bits) pour zéro
  dérive sur le long terme.

## 5. Validation continue — l'oracle existe déjà

`tools/trace_diff.py` (livré) aligne une trace NeoST et une trace Hatari et pointe
la **première divergence (PC + registres)**. Méthode :

```sh
# Hatari (référence) — headless, déterministe
SDL_VIDEODRIVER=dummy ./extern/hatari/build/src/hatari --configfile /dev/null \
  --machine st --memsize 0 --monitor rgb --sound off --patch-tos off --fast-boot off \
  --tos <TOS1.02> --disk-a <jeu.st> --trace cpu_disasm --trace-file hatari.txt \
  --run-vbls N --benchmark
# NeoST
./build/neost-headless --frames N --disk <jeu.st> --trace neost.txt --regs <TOS1.02>
# Diff
python3 tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs
```

> Note de parsing : Hatari `cpu_disasm` écrit `00fc0030 46fc 2700  move.w #$2700,sr`
> (PC sur 8 hexa, sans `$` ni `:`). Le format Hatari de `trace_diff.py` doit
> reconnaître `^([0-9A-Fa-f]{6,8})\s` en plus de la forme `$adr :`.

**Indicateur de progrès** : à chaque phase, le **point de première divergence
recule**. Bonus : en cycle-accurate, les points d'insertion d'IRQ coïncident avec
Hatari → le diff index-par-index cesse d'être « bruité » par le décalage des
interruptions (un bénéfice en soi).

## 5bis. Second cœur CPU : Moira (cycle-exact) — sélectionnable

Plutôt que de porter le cœur **UAE/WinUAE** (~60k lignes, GPLv2, très couplé à
Hatari), NeoST intègre **Moira** (cœur 68000 de vAmiga, **MIT**, C++20,
**cycle-exact** avec timing inter-instructions) en sous-module `extern/moira`.

- Sélection **au démarrage** : `--cpu musashi|moira` (headless), `cpu=` dans
  `neost.cfg` (GUI), `?cpu=` / sélecteur dans l'UI WASM. **Musashi reste le
  défaut.**
- Intégration (`Cpu68k`) : sous-classe `moira::Moira` routant `read8/16` et
  `write8/16` vers le `Bus`, `irqMode = USER` + `readIrqUserVector` reproduisant
  le vectoring ST (MFP vectorisé niveau 6, VBL/HBL auto-vectorisés). `run()`
  exécute via `executeUntil`/`execute` et reporte `getClock()` à l'ordonnanceur.
- **État** : Moira **boote EmuTOS pixel-identique** à Musashi, et **délivre
  correctement les IRQ** — sur 100 trames : **538 IRQ niveau 6 (MFP) + 98 niveau
  4 (VBL)**, contre 541+98 pour Musashi (quasi identique).
- **Optimisation `stop`** : le 68000 en attente (`stop`) était simulé cycle par
  cycle par Moira (~25× plus d'`execute()` par trame → en WASM temps-réel,
  l'émulation ramait et EmuTOS restait coincé sur l'écran d'accueil). `Cpu68k::run`
  détecte l'état STOP (`NeostMoira::isStopped`) et **saute** directement au prochain
  événement daté (rien ne se passe avant lui) : ~6900 instr/2 trames au lieu de
  158000, boot pixel-identique, IRQ inchangées, et **le bureau GEM s'affiche
  désormais à pleine vitesse** (icônes disque A/B, corbeille, barre de menu).
- **Reste (divergence fonctionnelle, PAS les IRQ) — diagnostic affiné** : sous
  TOS 1.02 + Arkanoid, l'autoloader `STARTGEM.PRG` s'exécute correctement sous
  Moira (il **hooke le vecteur LINE-F `$2C`** et dispatche les opcodes `$Fxxx` ;
  le PC fauté empilé `A0=[A7+2]` est **identique** à Musashi). Le déclencheur
  `$F2C8` finit même par matcher (sortie du dispatcher `$CD38` atteinte). MAIS le
  `Pexec("A:ARKANOID.PRG")` qui suit **n'aboutit pas** : le FDC est sollicité
  (~1500 accès `$FF860x`) mais `ARKANOID.PRG` (`$14000`) ne s'exécute jamais. La
  divergence est donc dans le **chargement Pexec → FDC** sous le timing
  cycle-exact de Moira (et non dans les IRQ ni le LINE-F). Prochaine étape : diff
  de trace **NeoST ↔ Hatari (UAE, source de vérité)** sur la séquence Pexec/FDC
  pour isoler l'écart (Musashi y sert seulement de proxy « qui marche »).

## 6. Effort / risque

| Phase | Effort | Risque | Gain |
|------|--------|--------|------|
| 0+1  | moyen  | faible (refactor iso-comportement, validé par trace) | fondation |
| 2    | moyen  | moyen  | bordures, rasters, Timer B DE |
| 3    | moyen+ | moyen  | **Arkanoid**, démos, timers complets |
| 4    | moyen  | moyen  | protections, timing disque |
| 5    | faible | faible | anti-dérive long terme |

## 7. Premier pas recommandé

**Phases 0+1** : `Scheduler` autonome + `runFrame` événementiel **iso-comportement**
(mêmes IRQ aux mêmes instants), prouvé sans régression par `trace_diff`. Ensuite,
resserrer Vidéo (2) puis MFP (3) en mesurant, à chaque étape, le recul de la
divergence avec Hatari — jusqu'à voir Arkanoid franchir le `$26E7`.
