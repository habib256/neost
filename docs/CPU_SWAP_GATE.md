# Porte d'échange du cœur 68000 (Moira de NeoST → Moira officiel)

NeoST 0.7.0 utilise le cœur officiel de Dirk W. Hoffmann en mode 68000. Le
[plan de migration](MIGRATION_MOIRA_070.md) donne le pin, les machines couvertes,
les adaptations et la recette d'acceptation.

## La porte

Ce qui doit être prêt avant l'échange (porte élargie, banc étendu, sort des `.state`) et
ce qui est gelé d'ici là : [`../TODO.md`](../TODO.md) § 2 et § 9.

`tools/cpu_swap_gate.py` rejoue quatre suites, NeoST contre NeoST, sans Hatari ni Pillow :

| Suite | Scénarios | Commande |
|-------|-----------|----------|
| `etalons` | les scénarios avec un nombre de trames de `tools/etalons.json` | celle de `run_etalons.py` |
| `replays` | les parcours de `tests/replays/replays.json`, diagnostic compris | celle de `compare_titles.py` côté NeoST, par `tools/title_cmd.py` : mêmes touches, joystick, souris, disquettes changées |
| `collection` | chaque image de `disks/st` et `disks/stx`, en ST/TOS 1.04, STE/TOS 1.62 et Mega STE/TOS 2.06 | ESPACE toutes les 500 trames de 1000 à 3500, 4000 trames |
| `megaste` | sondes blitter/IRQ et bus/cache à 8, 16 et 16 MHz + cache ; DMA disquette et blitter du catalogue étendu à 16 MHz ; blit sur ses propres registres (demi-teinte écrite puis relue) ; suite Q du diagnostic Field Service | générateurs de `run_megaste_blitter_checks.py`, `run_megaste_bus_oracle.py` et `timing_extended.py` (SCU ouvert et vitesse injectés en tête) ; recette de `run_megaste_diag.py` |

Les médias privés manquants (images, TOS Atari, cartouche) sont sautés et nommés à
l'enregistrement ; à la vérification, un scénario de la référence qui a disparu est un
échec. Toutes les 10 trames (à chaque trame pour les sondes Mega STE), la porte relève
l'empreinte `--arch-hash-every`, qui ne dépend pas de la façon dont le cœur sérialise ses
internes :

| Hachage | Contenu | Un écart désigne |
|---------|---------|------------------|
| `machine` | archive du save-state, **sans** l'en-tête (version de format) **ni** la section `Cpu68k::serialize` | un accès bus, sa date ou sa valeur |
| `cpu` | `Cpu68k::archState()` : D0-D7, A0-A7, USP, SSP, PC, SR, IRC, IRD, horloge **bus**, halt, stop | le cœur lui-même |

```sh
# 1. Sur la base immédiate du portage, avec l'ancien cœur et le palier full vert :
python3 tools/cpu_swap_gate.py --record /chemin/gate.json      # toutes les suites
# 2. Sur le commit de l'échange :
python3 tools/cpu_swap_gate.py --check  /chemin/gate.json      # les suites de la référence
# Localiser : la commande d'un scénario, pour --trace F --trace-from N --regs --irq
python3 tools/cpu_swap_gate.py --show replay/golden_axe_ste
python3 tools/cpu_swap_gate.py --list --suite megaste
```

La 0.6.5 publiée (`b3ced823`) garde sa référence historique, enregistrée avec
le binaire du tag. La référence avant/après du portage utilise le dernier
commit avec l'ancien cœur et son propre binaire. Les empreintes restent hors Git.

La porte a été éprouvée le 2026-11-10 (commit `8dcbf65`) : accord contre elle-même ;
`NEOST_MFP_BATCH=0` est vu en `machine` seul, sur plusieurs scénarios ; `NEOST_IPLDELAY=1`
en `machine` et `cpu` dès la trame 10.

**Un écart se localise, il ne se recale pas.** On prend la dernière trame en accord, puis
`--trace F --trace-from N --regs --irq` sous les deux binaires, et `tools/trace_diff.py`.
L'instruction fautive se tranche au banc fx68k (`docs/FX68K_BENCH.md`) : si le cœur candidat est plus
proche de fx68k, c'est un progrès, consigné et la référence ré-enregistrée ; sinon c'est
une régression du cœur.

### Ce que la porte ne couvre pas, à rejouer à part

- `python3 tools/run_all.py --tier full` : les auto-tests de logique, le banc fx68k (verdicts
  figés dans `tests/reference/fx68k_bench.json`), le banc CPU + GSTMCU, le diagnostic Mega STE.
- `tools/run_moira_runner.sh` : le cœur nu.
- `/usr/bin/python3 tools/run_game_replay.py` : l'accord des parcours avec Hatari (la porte
  ne vérifie que leur égalité NeoST contre NeoST ; Pillow requis, le `python3` de Homebrew
  ne l'a pas et sort en SKIP).
- `tools/run_determinism.py` : reprise de save-state avec le **nouveau** format CPU, et refus
  explicite d'un état d'une autre version (passe C).
- Mega STE à 16 MHz face à une référence extérieure : la porte ne dit que « inchangé ».
  L'accord avec Hatari et la borne physique du bus restent à `run_megaste_bus_oracle.py`
  et `run_megaste_blitter_checks.py`.

## Ce que NeoST attend du cœur

Relevé le 2026-11-10 dans `src/core/Cpu68k.cpp` (classe `NeostMoira`). Tout ce qui suit doit
exister dans le cœur officiel adapté, avec la même sémantique en 68000.

### Mots du bus CPU pour la vidéo

Depuis le correctif Ika du 15 novembre 2026, les adaptateurs mémoire transmettent
chaque mot observé à `Bus::latchCpuDb(value, busOfClock(getClock()))`. Les octets
conservent la duplication sur les deux voies du modèle existant. Le nouveau cœur
doit préserver la valeur **et la date en cycles bus** : la vidéo consulte cet
historique pour la RAM absente. L’historique fait partie de l’état machine v68.
Aucun changement du cœur vendorisé ; ajout limité à l’adaptateur `latchDb`.

### Méthodes surchargées

| Méthode | Usage dans NeoST |
|---------|------------------|
| `read8/read16/read32, write8/write16/write32` | accès bus, wait-states, cache Mega STE |
| `read16OnReset`, `read16Dasm` | vecteur de reset par l'overlay ROM ; désassembleur sans effet de bord |
| `willAccessBus`, `didAccessBus`, `setBusLock` | arbitrage blitter sous l'instruction, verrou TAS |
| `sync(int)` | cycles internes : blitter, broches HBL/VBL datées au cycle |
| `willExecute` (instruction et exception), `didExecute` | trace, observateur de TRAP |
| `willProcessFault(StackFrame)` | dernière faute de groupe 0 (A42) |
| `illegalOpcodeHook` | instruction illégale déléguée (GEMDOS HD) |
| `didHaltCPU` | double faute → halt |
| `willInterrupt`, `iackSyncBefore`, `iackSyncAfter`, `readIrqVector` | IACK : synchro E-clock, vecteur MFP/SCC |
| `didReachWatchpoint` | débogueur |

### Membres internes touchés

`clock`, `reg`, `queue`, `ipl`, `iplPrev`, `iplChangeClock(Prev)`, `iplDelay2/4`,
`iplDeferred`, `stopGraceUntil`, `fcl`, `readBuffer`, `flags` ; méthodes `pollIpl`,
`irqDeliverable`, `faultOrHalt`. Ils servent au save-state et à la prise d'IPL : un
renommage dans l'amont se reporte ici, une disparition demande un équivalent.

### Instruction garée (coroutine de `runFrame`)

Depuis le 2026-11-11, `Machine::runFrame` tourne sur une pile à part (`src/core/EmuFiber`) :
quand un blit HOG parti dans une instruction franchit la fin de trame, la trame est close à
son heure et la pile est **suspendue au milieu de l'instruction**, depuis `sync()` (Moira →
`sync` → `Blitter::onSlice` → `Scheduler::syncToRollingFrames` →
`Machine::rollFrameInInstruction_`). Aucun patch de Moira ; le cœur doit seulement :

- tolérer qu'une instruction soit suspendue puis reprise **sur le même fil** entre deux
  appels hôte — aucun état lié à l'adresse de pile ou au fil ;
- laisser traverser `execute()` une exception qui ne dérive **pas** de `std::exception`
  (`EmuFiber::Abort`, levée pour abandonner une instruction garée au reset ou à la
  destruction). Moira n'intercepte que `std::exception` (`Moira.cpp:276` et suivantes) ; un
  `catch (...)` dans le chemin d'exécution du cœur casserait l'abandon. Le JIT de POM68K
  devra garder cette propriété sur ses blocs compilés qui appellent `sync`.

`tools/check_hog_park.py` (palier `fast`) vérifie l'accord à l'octet avec une exécution d'un
seul tenant (`NEOST_NO_FIBER=1`), le refus de l'état pendant le garage et l'abandon au reset.

### Patches de NeoST à vérifier à chaque import

Détail et preuves → `extern/moira/NEOST_VENDOR.md`. Origine des adaptations à confronter à chaque pin :

| Date | Patch |
|------|-------|
| 2026-07-03 | STOP sensible au niveau |
| 2026-07-08 | reset gardé |
| 2026-07-11 | watchpoints masqués au bus 24 bits |
| — | `NEOST_IPLFETCH` (reconnaissance IPL différée), hooks d'IACK, `NEOST_EXC_DIAG` |
| 2026-10-01 | `illegalOpcodeHook` |
| 2026-10-19 | `willProcessFault` |
| 2026-10-20 | double rechargement ANDI/ORI/EORI #,SR/#,CCR et MOVE to CCR ; DIVU/DIVS : cycles avant le préfetch ; DIVS sans comportement indéfini |
| 2026-10-22 | STOP+8 : retenue d'IRQ de 4 cycles après STOP |
| 2026-11-03 | ordre des accès d'après fx68k (MOVEM, ADD/SUB.L mémoire, ADDX/SUBX.L, DBcc, JSR, trame de groupe 0, reset +2) |
| 2026-11-09 | `<cstdlib>` explicite (Emscripten 7) |

Le moyen le plus sûr : un `diff` de `extern/moira/Moira` contre le pin officiel, en mode 68000.
