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

### Phase 3 — Timers MFP datés (`mfp.c`) → **Arkanoid**

- Timers A–D en événements (delay/event-count/pulse-width), latence IACK au cycle.
- **Critère de réussite** : le diff NeoST↔Hatari ne diverge plus sur la séquence
  de jeu, et la boucle `$26E7` sort → Arkanoid passe l'écran-titre vers l'in-game.

### Phase 4 — FDC/DMA temporel (`fdc.c`)

- Spin-up, index pulse, BUSY, DRQ/INTRQ datés ; remplace le DMA instantané.

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
