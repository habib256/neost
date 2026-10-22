# Moira — copie vendorisée (fork NeoST)

Ce dossier n'est **plus un sous-module Git**. C'est une copie versionnée
directement dans le dépôt NeoST (« vendorisée ») du cœur 68000 cycle-exact
[Moira](https://github.com/dirkwhoffmann/Moira) de Dirk W. Hoffmann (licence MIT,
cf. `LICENSE`).

## Pourquoi vendorisé

NeoST **modifie** des fichiers internes de Moira (cf. patch ci-dessous). Avec un
sous-module, ces edits ne sont pas dans le dépôt parent et sont **écrasés au
premier `git submodule update`** — ce qui s'est déjà produit une fois (fork local
perdu, commits `e4da365`/`a1e52ec` introuvables). La vendorisation fige le code et
le patch dans l'historique NeoST, à l'épreuve du clobber.

## ANDI/ORI/EORI #,SR/#,CCR et MOVE to CCR : double rechargement (2026-10-20)

- `MoiraExec_cpp.h`, `execAndisr` (ANDI/ORI/EORI #,SR), `execAndiccr`
  (ANDI/ORI/EORI #,CCR) et `execMoveToCcr`, branche `C68000` : même correctif que
  `execMoveToSr` le 2026-10-19 — `fullPrefetch<POLL>` remplace la lecture fictive de
  `pc+2` suivie de `prefetch`. La [transcription du
  microcode](https://og.kervella.org/m68k/all.txt) donne pour `003c`/`023c`/`0a3c`,
  `007c`/`027c`/`0a7c` la queue `1cc stiw1` (recul de deux octets) → `321 stiw4` →
  `1e1 malw3` (lecture, IRC) → `363 b` → `34c mmrw3` (IRD ← IR ← IRC), et pour
  `44c0`..`44fc` `301 rstw1`/`159 mstw1` puis la même queue. Accès : pc0+4, **pc0+4**,
  pc0+6 (au lieu de pc0+4, pc0+6, pc0+6) ; IRD reçoit le mot relu APRÈS l'écriture de
  SR/CCR, en espace PROGRAMME (l'ancienne lecture fictive des #,SR/#,CCR était en
  DATA). Hatari CE fait de même (`op_027c_13_ff` : `get_word_ce000_prefetch(0)` après
  `MakeFromSR`). Nombre de cycles inchangé ; les autres modèles CPU sont conservés.
- Conséquence observable : `ANDI #$DFFF,SR` (retour en utilisateur) refait pc0+4 en
  FC utilisateur. Placé en `$7FE`, ce mot est protégé par le GLUE → bus error, comme
  sur le vrai 68000 ; l'ancien code exécutait le mot préfetché en superviseur.
- Tests (`tests/blitter_rmw_logic.cpp`, 8 et 16 MHz) : MOVE D0,CCR exécute l'opcode
  modifié après le reset ; ANDI #,SR et EORI #,SR vers l'utilisateur en `$7FA`
  fautent sur `$7FE` ; ORI #,CCR garde sa durée (20/10 cycles).

## DIVU/DIVS : cycles internes avant le préfetch (2026-10-20)

- `MoiraExec_cpp.h`, `execDivsMoira` / `execDivuMoira` : sur 68000, `SYNC(cycles)`
  précède désormais `writeD` et le préfetch. Port de l'amont
  [`f90fc50`](https://github.com/dirkwhoffmann/Moira/commit/f90fc501f8aa1025072ce96def343eaf14f1a185)
  (« Fixed DIVU/DIVS prefetch timing », 2026-09-23), restreint au 68000 comme chez
  lui. Hatari CE fait de même (`op_80c0_13_ff` : `do_cycles_ce000_internal` puis
  `get_word_ce000_prefetch`).
- **Ce correctif avait déjà existé** : commit de fork `e4da365` (2026-06-17, « DIV idle
  avant prefetch », cf. `docs/MOIRA_WINUAE_CONVERGENCE.md`). Il n'était **pas** dans le
  code vendorisé le 2026-06-25 — perdu avec le fork écrasé — et l'audit du 2026-08-13
  ne l'a pas vu, faute d'exhibiteur : le cycle-bench tourne en cartouche (FAST), où
  l'ordre n'a aucun effet.
- Exhibiteur : sonde `cpu_div_ram` de `tools/run_timing_oracle.py` (définie dans
  `tools/timing_extended.py`) (24 couples
  DIVU puis DIVS exécutés en RAM, durée de chaque itération comparée à Hatari au
  cycle près). Avant : 14 itérations DIVU et 9 DIVS courtes de 4 cycles ; après : 0.

## Bornes bus et trace d'instruction (2026-10-19)

- `Moira.h` / `Moira.cpp` : hook protégé `willProcessFault(StackFrame)`
  appelé pour les erreurs de bus et d'adresse, initiales ou imbriquées.
  NeoST y relève la dernière adresse, le sens et le PC afin qu'un halt sur
  adresse impaire ne conserve pas le diagnostic d'une ancienne sonde MMIO.
  Le hook reste virtuel même hors `MOIRA_VIRTUAL_API`, comme les callbacks
  de faute employés par le wrapper ; aucune modification du flux CPU.

- `MoiraExec_cpp.h`, `execMoveToSr<C68000>` : recharger les deux mots de
  préfetch après écriture de SR, via `fullPrefetch<POLL>`. La transcription
  du [microcode du silicium](https://og.kervella.org/m68k/all.txt), entrées
  `46c0` et `46fc`, décrit `301 rstw1` (recul de deux octets), `321 stiw4`,
  `1e1 malw3` (lecture et mise à jour IRC), puis `363 b`/`34c mmrw3`
  (lecture suivante et transfert IR/IRD). L'ancien code lisait deux fois
  `pc+2` et gardait l'ancien opcode. Les autres modèles CPU sont conservés.
  Un test modifie l'opcode déjà préfetché entre reset et MOVE D0,SR : le
  nouvel opcode doit s'exécuter, à 8 comme à 16 MHz. Les deux cas échouent
  aussi avec la variante expérimentale « lecture pc, résultat ignoré ».

- `Moira.h` / `MoiraDataflow_cpp.h` : hooks `willAccessBus(u32 addr, bool write, unsigned size)` et `didAccessBus`
  avant/après chaque cycle bus (deux pour un long). Le SYNC(4) entre les mots
  est séparé en 2+2 sans ajouter de cycles. Les implémentations par défaut sont
  vides ; NeoST s'en sert pour PRE_START et les 64 accès CPU non-hog d'Hatari.
  Direction et largeur décrivent chaque cycle (long = deux mots, ordre inversé
  conservé) : décision hit/miss MegaSTE et préalignement RAM avant SYNC(2).
  Le hit est latché avant les cycles, afin qu'une DMA ne change pas la donnée
  que le CPU avait déjà obtenue du cache.
- `Moira.h` / `MoiraExec_cpp.h` : `setBusLock` protège le read-modify-write TAS
  mémoire ; libération avant préfetch et sur exception. Le blitter peut ainsi
  reprendre sous-instruction sans casser l'atomicité de TAS.
- La copie de `MoiraConfig.h` générée par CMake active `MOIRA_WILL_EXECUTE` pour
  le traçage. Le hook existant trace avant l'instruction réelle, une seule fois
  par execute (garde NeoST), sans fausses instructions lors des IRQ/STOP.
- Le crédit de recouvrement CPU/blitter est détenu et sérialisé par `Cpu68k`
  (save-state v49), pas dans les structures du cœur Moira.

Validation : dix-sept sondes temporelles strictes contre Hatari, dont dix blitter
(tailles, ROM/RAM, HOG/partagé, MULU et TAS), tests TAS mémoire/D0 et reprise du
crédit après snapshot dans les deux modes. Le hook avant accès reçoit l'adresse
masquée de chaque mot (ordre inversé compris pour les écritures longues) :
l'alignement RAM est calculé au début du cycle avant SYNC(2), comme Hatari,
pas au callback médian après une éventuelle tranche blitter. BL5 pixel clos :
les 100 valeurs TADR de `blitter_timer` concordent avec Hatari, zéro pixel.
Le helper PRE_START ne sert que BLITTER ; la DMA conserve le dispatch BL4 par accès.

## Pin d'origine — retrouvé le 2026-08-28 (chantier A35)

Ce fichier décrivait les six patches locaux mais **n'enregistrait ni commit ni tag
upstream** : le fork n'était donc ni rebasable, ni comparable à l'amont. Le pin a été
retrouvé dans l'historique NeoST lui-même — la trace du sous-module y est encore, et
elle est reproductible :

```sh
# Le gitlink du sous-module, commit par commit, jusqu'à sa vendorisation :
for c in $(git log --format=%H -- extern/moira); do
    git ls-tree "$c" extern/moira | awk -v c="$c" '{print c, $2, $3}'
done
```

| Date | Commit NeoST | Gitlink Moira | Ce que c'est |
|------|--------------|---------------|--------------|
| 2026-06-01 | `6e7ab7a` (1ʳᵉ intégration) | **`1efd69467ca13b27b2fb40febd5cb31dbecdea5f`** | **LE PIN DE DÉPART** — avant tout patch local |
| 2026-06-16 | `a3b5d2c` | `10f77f6b…` | 1ᵉʳ commit du fork local |
| 2026-06-17 | `c19ba10` | `e4da3650…` | fork local (cité comme « introuvable » ci-dessus) |
| 2026-06-18 | `54f8faf` | `a1e52eca…` | **tip du fork local** au moment de la vendorisation |
| 2026-06-25 | `0b96cab` | — | vendorisation : l'arbre remplace le sous-module |

Donc : l'amont d'origine est **`1efd6946`** de `github.com/dirkwhoffmann/Moira`, et le
code vendorisé correspond au contenu du fork local à `a1e52ec` (dépôt disparu, d'où
l'impossibilité de rejouer son historique — mais son CONTENU est ici, dans
l'historique NeoST).

✅ **Vérifié le 2026-10-20** : `1efd6946` existe toujours chez l'amont et il est atteignable
depuis `master` (« Added .nojekyll », 2025-09-11). L'amont compte 11 commits de plus à
cette date, dont `f90fc50` (DIVU/DIVS, porté ci-dessus) ; les autres concernent
68020/68030, vAmiga et l'empreinte mémoire.

**Rebaser, désormais** : cloner l'amont au pin, appliquer le diff `Moira/` du fork
vendorisé, puis rejouer un par un les patches documentés dans ce fichier (chacun porte
son étalon de validation).

## Re-valider les patches HORS étalons ST — la question du `Cputester`

L'arbre upstream contient un `Cputester/` que la vendorisation a élagué (**~713 Mo de
corpus ADF** : c'est la raison de l'élagage, et elle tient toujours — ce corpus n'a rien
à faire dans l'historique d'un dépôt public de 100 Mo).

Aujourd'hui, les patches locaux ne sont validés QUE par les étalons ST du projet : un
patch qui casserait une instruction jamais exécutée par EmuTOS ni par nos démos
passerait inaperçu. La bonne réponse n'est pas de commettre 713 Mo — c'est la même que
pour l'oracle Hatari : **cloner hors de l'arbre, gitignoré**, et documenter la recette.

```sh
# Hors du dépôt NeoST, ou dans un chemin gitignoré :
git clone https://github.com/dirkwhoffmann/Moira /tmp/moira-upstream
git -C /tmp/moira-upstream checkout 1efd69467ca13b27b2fb40febd5cb31dbecdea5f
# Diff des sources compilées par NeoST — c'est l'inventaire EXACT des patches locaux :
diff -ru /tmp/moira-upstream/Moira extern/moira/Moira
```

Ce `diff` est le premier livrable utile : il transforme « des patches décrits en prose »
en une liste de hunks vérifiable.

**Évalué le 2026-10-20.** Le `Cputester/` amont est le cputester de Toni Wilen sur
disquettes Amiga (ADF/HDF), à exécuter dans vAmiga : inutilisable sur un ST, écarté.
L'outil retenu est `Runner/` : Moira contre Musashi sur opcodes et données aléatoires
(désassemblage, total de cycles hors TAS, SR, registres, écritures mémoire), tous
modèles CPU. `tools/run_moira_runner.sh` le bâtit contre la copie vendorisée
(clone sans extraction dans `extern/moira-upstream/`, gitignoré) ; `--upstream` ajoute
le témoin au pin. Résultat : tous les modèles PASSED, patches NeoST compris. Une
mutation de l'ALU (`EOR`) est rejetée (« DATA REGISTER MISMATCH »). Limite : les
cycles comparés sont des totaux de table, pas la position des cycles bus — ce banc
n'aurait pas vu la perte du correctif DIV ci-dessus.

## Contenu conservé

Seul `Moira/` (les sources compilées par NeoST), plus `LICENSE`, `README.md` et
`CMakeLists.txt` upstream (référence). L'arbre upstream complet — `Cputester/`
(~713 Mo de corpus ADF), `Documentation/`, `docs/`, `Runner/`, `Moira.xcodeproj/`
— a été **élagué** (inutile au build NeoST, qui ne compile que `Moira/*`).

## Patch local : `NEOST_IPLFETCH` (reconnaissance IPL différée)

Port fidèle de WinUAE `ipl_fetch_next` (mécanisme B). **OFF par défaut**
(`iplDelay4 == 0` ⇒ `pollIpl()` ≡ `reg.ipl = ipl`, byte-identique à l'upstream).
Activé via l'env `NEOST_IPLFETCH=1` côté NeoST. Détails et mesures :
`docs/MOIRA_WINUAE_CONVERGENCE.md` (mécanisme B).

Fichiers touchés vs upstream :
- `Moira/MoiraMacros.h` — `POLL_IPL` → `pollIpl()`
- `Moira/Moira.h` — membres `iplPrev`/`iplChangeClock`/`iplDelay4`/`iplDelay2`,
  méthodes `setIplDelay()` + `pollIpl()`
- `Moira/Moira.cpp` — historisation de la broche dans `setIPL`, règle 3-cas dans
  `pollIpl()`
- `Moira/Moira.cpp` — **gardes d'exception** (2026-07-03) : les chemins TRACE,
  PRIVILEGE-depuis-STOP et LOOPING de `execute()` passent par `processException`
  (un vecteur impair y jette `AddressError` → address error 68000, pas un abort) ;
  `processException` corrige le `throw df` (pointeur jamais rattrapé) et traite
  AddressError/BusError imbriquées comme **double faute → HALT**. Étalon :
  `trace_odd` (`tools/make_trace_odd_test.py`).

## Patch local : STOP niveau-sensible (2026-07-03)

Le 68000 compare IPL/masque **en continu** pendant un STOP (broches niveau-
sensibles). L'upstream ne re-teste `checkForIrq()` que sur `CHECK_IRQ`, posé au
CHANGEMENT de broche — une IRQ levée AVANT le `stop` (masquée par le SR d'alors,
démasquée par l'opérande du stop) n'était jamais re-testée : le CPU dormait
jusqu'au prochain changement de broche. Fix : dans la branche STOPPED
d'`execute()` (Moira.cpp), après `POLL_IPL`, re-armer `CHECK_IRQ` si
`reg.ipl > reg.sr.ipl || reg.ipl == 7` (NMI non masquable). Pendant : garde `!irqDeliverable()` sur le saut
d'attente STOP de `Cpu68k::run` (NeoST) — sans elle l'horloge était téléportée
au prochain événement et l'IRQ déjà prenable partait ~350 cyc trop tard. Cas
mesuré : raster « Timer B + stop #$2100 + HBL » de Super Hang-On (bande blanche
à l'horizon, 3 écritures palette par activation au lieu de 2, dérive +1 ligne
par segment). Étalons 19/19 + EL + Cuddly re-validés après fix.

## Patch local : STOP+8 — retenue d'IRQ de 4 cycles après STOP (2026-10-22)

Quand une IRQ est déjà pendante et que l'opérande du `stop` la démasque, le 68000
ne la prend pas à la fin du `stop` (+4) mais à l'itération suivante (+8) : le
microcode teste l'interruption AVANT d'écrire le nouveau masque (fx68k.sv :
`rInterrupt <= intPend` verrouillé au même T3 que `pswI`, nanomots 0x3A2/0x327,
et STOP se ré-exécute toutes les 4 cycles car `Ir2Ird` bloque IR←IRC ; Hatari
gencpu.c : `checkint()` avant `regs.sr = sr`, puis `do_cycles_stop(4)`). L'upstream
fait `setSR` puis `checkForIrq` immédiat → exception à STOP+4.
- `Moira.h` : membre `i64 stopGraceUntil {-1}` ; `MoiraExec_cpp.h`, `execStop<C68000>` :
  `stopGraceUntil = clock + 4` après `CYCLES_IP` ; `Moira.cpp`, `execute()` : `checkForIrq`
  retenu tant que `STOPPED && clock < stopGraceUntil`. Sérialisé par `Cpu68k` (v55).
- Mesuré sur la chaîne de Super Hang-On (Timer B → handler → `stop #$2100` → HBL
  pendante, sonde cartouche, 3 997 chaînes) : segment stop→exception 4 → 8, chaîne
  136 → 140 = Hatari. C'est ce +4 que la constante `g_iackMfp = 16` (au lieu de 12)
  compensait depuis le 2026-08-06.
- Complété le même jour : (a) réveil quantifié — une IRQ arrivant APRÈS l'entrée en
  STOP n'est prise qu'à la fin de l'itération de 4 cycles en cours (`(clock −
  stopGraceUntil) & 3 == 0`), comme fx68k et la ré-exécution de l'opcode chez Hatari ;
  la broche reste scrutée à chaque cycle. (b) Plus de cycle bus pendant STOP : l'opérande
  est pris dans IRC sans préfetch (`reg.pc += 2`), STOP = 4(0/0) ; les sorties de STOP
  (exception puis RTE) rechargent la file. Non porté : la synchronisation IPL à deux
  étages de fx68k (~2 cycles), du ressort de l'option `NEOST_IPLFETCH`.

## Patch local : reset gardé (2026-07-08)

Une bus/address error pendant le fetch des vecteurs de reset (SSP/PC à $0-$7,
ex. image ROM tronquée — `roms/tos106us.img` fait 192 Ko au lieu de 256) fuyait
hors de l'émulateur (`terminate called after throwing moira::BusError`), le
`processException` ne couvrant que les chemins d'`execute()`. Fidèle 68000 :
double faute au reset = **HALT** (cpu_halt(CPU_HALT_DOUBLE_FAULT) chez
WinUAE/Hatari). Fix : `Moira::reset<C>()` (Moira.cpp) enveloppe les
`read16OnReset` + prefetch dans un try → `halt()`. Étalons 19/19 re-validés.

## Patch local : watchpoints masqués au bus 24 bits (2026-07-11)

Le débogueur NeoST pose des watchpoints mémoire via `debugger.watchpoints`. Moira
teste l'accès dans `Moira::read<>`/`write<>` (`MoiraDataflow_cpp.h` — nommés
`peekM`/`pokeM` dans d'anciennes versions) avec l'adresse EA **non
masquée**, alors que l'accès réel juste en dessous utilise `addr & addrMask<C>()`
(24 bits). Un accès I/O en adressage court absolu (`$8001.w` → EA `$FFFF8001`) ne
matchait donc jamais un watchpoint posé sur `$FF8001`. Fix : masquer l'`addr` par
`addrMask<C>()` **avant** `watchpointMatches`/`didReachWatchpoint` (les 2 sites, lecture
et écriture). Court-circuité hors watchpoints (`flags & CHECK_WP &&`) → zéro impact en
marche normale. Vérifié : `--watch FF8001` (via `$8001.w`) et `--watch 10` (RAM directe)
se déclenchent ; self-tests + glue-selftest 31/31 intacts.

Toute modif future d'un fichier `Moira/` se commit **normalement** dans NeoST.

## Patch local : hooks d'IACK (`iackSyncBefore` / `iackSyncAfter`)

Le cycle d'interruption 68000 d'`execInterrupt<C68000>` avait ses temps d'attente codés en
dur (`SYNC(4)` avant l'IACK, `SYNC(4)` après). NeoST doit y placer la synchro **E-Clock**
d'Hatari (`M68000_WaitEClock`) au POINT D'IACK RÉEL — c'est ce qui fait émerger le motif
mod-20 des positions d'IRQ mesuré côté Hatari. Ajouter le délai en amont (ancien
`willInterrupt`) sur-comptait de +8 cycles (cf. `docs/MOIRA_WINUAE_CONVERGENCE.md`).

Fichiers touchés vs upstream :
- `Moira/Moira.h` — deux virtuelles `iackSyncBefore(u8)` / `iackSyncAfter(u8)`, valeur de
  repli 4/4 (≡ upstream si non surchargées).
- `Moira/MoiraExceptions_cpp.h` — `SYNC(4)` → `SYNC(iackSyncBefore(level))` et
  `SYNC(iackSyncAfter(level))` autour du cycle d'IACK.

NeoST les surcharge dans `NeostMoira` (`src/core/Cpu68k.cpp`), sous `NEOST_IACK_AT`
(défaut ON). ⚠ **Patch STRUCTURANT** : le retirer déverrouille le beam-sync (Enchanted
Land, Cuddly Demos, Lethal Xcess).

## Patch local : instruction illégale délégable (`illegalOpcodeHook`) (2026-10-01)

Le disque hôte GEMDOS (`--gemdos`) repose sur des opcodes « illégaux » magiques
($0008 GEMDOS, $0009 PEXEC, $000A SYSINIT) placés dans la cartouche système. NeoST les
interceptait dans `Cpu68k::run`, **avant** `execute()`, et remplaçait l'IRD par un NOP. Or
`execute()` arbitre les interruptions avant d'exécuter l'IRD : si une IRQ était prenable à
cette frontière, l'appel hôte était déjà servi, l'IRQ prise, et le PC empilé désignait encore
l'opcode magique, rejoué au `rte`. Résultat : un Fread servi deux fois et un bloc sauté en
silence (TOSFC, Verify sur C:). Hatari traite ces opcodes **dans** leur gestionnaire
d'instruction (`OpCode_GemDos`), donc une seule fois par exécution réelle. Le patch fait
de même.

Fichiers touchés vs upstream :
- `Moira/Moira.h` — virtuelle `illegalOpcodeHook(u16)`, valeur de repli `false` (≡ upstream).
- `Moira/MoiraExec_cpp.h` — `execIllegal` l'appelle avant `execException(ILLEGAL)` ;
  `true` → l'opcode est consommé comme un NOP (`prefetch<C, POLL>`, 4 cycles).

NeoST la surcharge dans `NeostMoira` (`src/core/Cpu68k.cpp`). Garde : auto-test série
`gemdos_irq` (`tools/make_gemdos_irq_test.py`), dont le verdict `gdforced` rend l'IRQ
prenable pile à la frontière de $0008.

## Patch local : diagnostic d'exception (`NEOST_EXC_DIAG`)

Instrumentation OFF par défaut (`std::getenv`, `static const` → évaluée une fois) qui date
au cycle les trois instants de la séquence d'exception : changement de la broche IPL
(`Moira.cpp`), entrée du cycle d'IACK (`MoiraExceptions_cpp.h`) et fin de `jumpToVector`
(`MoiraDataflow_cpp.h`, ligne `[JTV] nr=… clk=…`). Sert au diff cycle-à-cycle contre
l'oracle Hatari instrumenté (`[HPIN]`/`[HFETCH]`/`[HEXC]`). Aucun coût en marche normale.

## DIVS : valeurs absolues sans comportement indéfini (2026-10-20)

`cyclesDiv<C68000|C68010, DIVS>` et `setUndefinedDIVS` calculaient `abs(dividend)` sur un
`i32` : pour `0x80000000`, `abs(INT32_MIN)` est indéfini. clang et GCC prenaient des branches
différentes pour `0x80000000 / 0x8000` (banc `cpu_div_ram` : vert sous macOS, Δ = +136 cycles
sous Linux). Les valeurs absolues sont désormais calculées en non signé ; |−2³¹| >> 16 =
`0x8000` ≥ `0x8000` → débordement, comme `getDivs68kCycles` d'Hatari.

