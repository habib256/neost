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

⚠ Ce que ce tableau NE dit PAS : que `1efd6946` existe encore chez l'amont. Il n'a pas
été vérifié contre GitHub (aucun accès réseau utilisé pour l'établir) — c'est ce que le
dépôt NeoST a enregistré, ce qui est déjà infiniment mieux que rien. Le vérifier est un
`git ls-remote` à faire au premier rebase.

**Rebaser, désormais** : cloner l'amont au pin, appliquer le diff `Moira/` du fork
vendorisé, puis rejouer les six patches ci-dessous un par un (ils sont documentés
séparément et chacun porte son étalon de validation).

## Re-valider les patches HORS étalons ST — la question du `Cputester`

L'arbre upstream contient un `Cputester/` que la vendorisation a élagué (**~713 Mo de
corpus ADF** : c'est la raison de l'élagage, et elle tient toujours — ce corpus n'a rien
à faire dans l'historique d'un dépôt public de 100 Mo).

Aujourd'hui, les six patches ne sont validés QUE par les étalons ST du projet : un
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

Ce `diff` est le premier livrable utile : il transforme « six patches décrits en prose »
en une liste de hunks vérifiable. Le faire tourner le `Cputester` ensuite dépend de ses
dépendances propres (corpus ADF, cœur de référence) — **non évalué ici**, faute de
réseau ; ce qui précède, lui, l'a été.

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
`reg.ipl > reg.sr.ipl`. Pendant : garde `!irqDeliverable()` sur le saut
d'attente STOP de `Cpu68k::run` (NeoST) — sans elle l'horloge était téléportée
au prochain événement et l'IRQ déjà prenable partait ~350 cyc trop tard. Cas
mesuré : raster « Timer B + stop #$2100 + HBL » de Super Hang-On (bande blanche
à l'horizon, 3 écritures palette par activation au lieu de 2, dérive +1 ligne
par segment). Étalons 19/19 + EL + Cuddly re-validés après fix.

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
