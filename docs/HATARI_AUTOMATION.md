# Automatiser Hatari (oracle de référence)

Hatari est une **référence comparative** de NeoST ; le vrai Atari reste la
cible et ses preuves matérielles priment (cf. `CLAUDE.md`). Les défauts
confirmés d’Hatari ne sont pas des comportements à reproduire. Au-delà de la
lecture des sources (`extern/hatari/src`), on peut **exécuter** Hatari de façon
déterministe et **sans affichage** pour comparer son comportement à NeoST (boot, écran,
détection HW, IRQ). Ce doc note la recette vérifiée (Hatari v2.6.1, macOS Silicon, juin 2026).

## Piloter l'oracle au JOYSTICK (et pas seulement au clavier)

⚠ `--cmd-fifo` n'injecte **que des scancodes clavier** : `hatari-event key{press,down,up}`
ne débouche que sur `IKBD_PressSTKey` / `Keymap_SimulateCharacter` (`control.c:87-140`), et
un `grep` de tout chemin joystick dans `control.c` rend **vide**. C'est ce qui faisait porter
au `TODO.md` la mention « cross-check Hatari BLOQUÉ » pour tout titre piloté au tir — soit
l'essentiel des jeux d'action.

Le contournement (trouvé le 2026-08-25, vérifié deux fois indépendamment) : faire passer le
tir par une **touche**, via la configuration joystick d'Hatari.

```ini
# hatari.cfg passé avec -c : le port ST 1 est piloté au CLAVIER
[Joystick1]
nJoystickMode = 2      ; 2 = émulation clavier
kFire = f              ; la touche « f » devient le bouton de tir
```

puis, sur la FIFO de commandes : `hatari-event keydown f` … `hatari-event keyup f`.
Le chemin est `Keymap_SimulateCharacter → Keymap_KeyDown → Joy_KeyDown`
(`sdl/keymap.c:836/904`).

⚠ **Égaliser la DURÉE d'appui.** `--cmd-fifo keydown/keyup` tient la touche ~600 ms, là où
`--keys-at` du headless NeoST ne la tenait que **2 trames ≈ 40 ms** (câblées en dur jusqu'au
2026-08-26). Comparer 40 ms à 600 ms n'est pas une A/B : un verdict « confirmé à l'oracle » a
déjà été rendu **FAUX** par cet écart. Côté NeoST, utiliser `--key-hold N` (1 ≤ N ≤ 216000 trames ; hors bornes, refus et code de sortie 2) pour égaliser.

Corrections au passage, mesurées : `--fast-forward on` **fonctionne** avec `--cmd-fifo` (1070
à 1707 VBL/s), contrairement à ce qui était écrit ; et `hatari-debug screenshot <chemin>`
exige un nom de fichier.

## ⚠ L'oracle local est PATCHÉ (et le patch, lui, est versionné)

`extern/hatari` est gitignoré : les modifications NeoST qu'il porte ne survivent à aucun
clone frais. Elles sont donc conservées dans **`tools/hatari_neost_oracle.patch`** —
événements souris de la fifo, **script joystick daté par VBL** (`NEOST_JOY_SCRIPT` /
`NEOST_JOY_START`), **graine figeable** (`HATARI_SEED`, déterminisme run-à-run) et trois
diagnostics conditionnés par variable d'environnement. Aucune ne change le matériel émulé.

```sh
git -C extern/hatari apply ../../tools/hatari_neost_oracle.patch
cmake --build extern/hatari/build -j
```

**L'épingle est la référence, le patch la suit.** Tranché le 2026-09-05 : l'épingle
`f0736b24` (2026-08-18) protège les 11 références `ref_kind: oracle` du dépôt ; le clone local
était sur `981f291` (2026-07-01), un **ancêtre** en retard de 26 commits dont `fdc.c` (+176),
`video.c`, `cpu/newcpu.c`. Remonter l'épingle aurait fait *reculer* la référence de sept
semaines et invalidé onze images pour rien. Le patch s'applique tel quel sur l'épingle
(`git apply --check` = 0), et **`tools/setup_hatari.sh` l'applique désormais lui-même** —
une installation fraîche ne peut plus donner un oracle nu.

⚠ Corollaire : les décalages NeoST↔Hatari mesurés le 2026-09-04 (Super Sprint, ci-dessous)
l'ont été contre le clone **non épinglé et plus vieux** — précisément sur un `fdc.c` différent,
là où la dérive saute. À re-mesurer contre l'épingle quand les ROM propriétaires seront de
retour sur cette machine.

⚠ Figer la graine ne rend PAS les deux timelines parallèles : le décalage NeoST↔Hatari
**dérive** et saute à chaque chargement disque (mesuré sur Super Sprint, graine 1 : −7
trames à la trame 600, −11 à 1000, −110 à 1500 ; le −200 à 2000 égale la demi-fenêtre
utilisée et peut être un artefact d'ancre statique, cf. `docs/OPENDST.md` § 9). Cf. `docs/OPENDST.md` § 9.

## Se procurer l'oracle (rien ne le fait à votre place)

⚠ `extern/hatari` est **gitignoré et n'est PAS un sous-module** : `git clone` du dépôt
NeoST ne le ramène pas, `git submodule update` non plus, et **rien ne le rapatrie
automatiquement** (ni la configuration CMake ni la suite de tests). Sur une machine fraîche (ou après un ménage), il est simplement ABSENT —
c'était le cas ici le 2026-08-19, alors que `CLAUDE.md` et ce document le décrivaient
comme « bâti dans le dépôt ». Le récupérer et le bâtir :

```sh
tools/setup_hatari.sh        # clone À LA VERSION ÉPINGLÉE + build (options macOS incluses)
```

⚠ **La version est ÉPINGLÉE depuis le 2026-08-26 (chantier A5)** : `f0736b2`, v2.6.1-devel.
La recette précédente faisait un `git clone --depth 1`, c'est-à-dire « le HEAD du jour » —
deux oracles bâtis à deux semaines d'écart pouvaient donc produire des références **pixel**
différentes sans qu'aucune ligne du dépôt n'ait bougé, alors que toutes les entrées
`ref_kind: oracle` du dépôt ont été posées avec CETTE version. `tools/hatari_oracle.sh`
**avertit** (sans bloquer) quand l'arbre présent ne correspond pas au pin. Pour changer
d'oracle en connaissance de cause : `tools/setup_hatari.sh --update-pin`, puis régénérer les
références et regarder ce qui bouge.

⚠ **Les options CPU sont passées EXPLICITEMENT** par `hatari_oracle.sh`
(`--cpu-exact on --compatible on`), alors que ce sont aujourd'hui les défauts d'Hatari.
Mesuré le 2026-08-26 sur `blitter_timer` : forcer les deux à `off` déplace la comparaison de
**69 px** (397 → 328). Une référence oracle dépend donc de ces réglages, et s'en remettre au
défaut d'un binaire qu'on ne contrôle pas laisserait une référence bouger toute seule.
Vérifié : rendre les options explicites ne déplace **aucune** référence existante (0 px sur
`blitter_hog` et `scroll_8264`).

Le build à la main reste possible (`cmake -S extern/hatari -B extern/hatari/build
-DCMAKE_BUILD_TYPE=Release [-DCMAKE_OSX_ARCHITECTURES=arm64 -DENABLE_OSX_BUNDLE=0]`).

Les deux options macOS ne sont pas cosmétiques (mesuré le 2026-08-19, Hatari
v2.6.1-devel, macOS 15 / Silicon) :
- **sans `-DCMAKE_OSX_ARCHITECTURES=arm64`**, la configuration vise **x86_64** et l'édition
  de liens échoue sur `_png_write_row… symbol(s) not found for architecture x86_64` (les
  bibliothèques Homebrew sont arm64) ;
- **sans `-DENABLE_OSX_BUNDLE=0`**, la cible est un `Hatari.app` dont l'étape finale lance
  `ibtool` sur un XIB : celui-ci part en `Abort trap: 6` (« A required plugin failed to
  load »), et make **supprime le binaire déjà lié**. Avec `=0`, on obtient exactement le
  chemin qu'attend l'outillage : `extern/hatari/build/src/hatari`.

Sous Linux (CachyOS / Ubuntu) les deux options sont inutiles ; dépendance : `libsdl2-dev`.
`ffmpeg` et `imagemagick` sont requis pour extraire les images (`tools/hatari_oracle.sh`).

Binaire selon la machine :
- **macOS** : `extern/hatari/build/src/hatari` (recette ci-dessus), ou
  `/opt/homebrew/bin/hatari` si le paquet Homebrew est installé — il ne l'est PAS par
  défaut. ⚠ Pas de `timeout` — on s'appuie sur `--run-vbls` qui fait sortir Hatari seul.
- **Linux (CachyOS / Ubuntu)** : `extern/hatari/build/src/hatari` (v2.6.1, aligné sur la
  source de vérité du repo, plus récent que le `hatari` d'apt en 2.4.1).
  ⚠ Un ancien symlink `~/.local/bin/hatari` a pu rester CASSÉ (chemin `src/NeoST/…`, casse
  différente) : invoquer le binaire **par son chemin**, ou le réparer avec
  `ln -sf "$PWD/extern/hatari/build/src/hatari" ~/.local/bin/hatari`.
  `timeout` y est disponible et conseillé en plus de `--run-vbls`.
  💡 `tools/hatari_oracle.sh` fait cette découverte de binaire toute seule — le préférer
  aux invocations manuelles.

## ⚠ Hatari n'est PAS déterministe d'un run à l'autre (et ça change la méthode)

`sdl/main_sdl.c` fait `Hatari_srand(time(NULL))`. Ce RNG alimente notamment :

| Site Hatari | Ce qui est tiré au hasard |
|-------------|---------------------------|
| `fdc.c` (`IndexPulse_Time = … - Hatari_rand() % FdcCyclesPerRev`) | **position angulaire initiale de la disquette** au démarrage |
| `video.c:1155` | wakeup state MMU/GLUE **si** `--video-timing random` (défaut : `ws3`, donc figé) |
| `mfp.c:1392` | jitter de timer (« for wod2 ») |
| `ikbd.c` | délai de réponse de l'IKBD à l'ACIA |

Conséquence pratique, mesurée le 2026-08-19 sur `cuddly_demos` : **deux runs avec la
MÊME ligne de commande ne donnent pas la même image au même numéro de trame** dès que le
programme boote d'une disquette — la durée du boot varie, et tout le film glisse. Mesure :
la trame NeoST 3499 tombait sur la trame Hatari **3560** dans un run et **3497** dans un
autre (deux runs lancés à quelques secondes d'écart sont, eux, identiques : le sel est
l'horloge en secondes). C'est ce qui avait fait conclure à tort, le 2026-08-01, que
NeoST rendait Cuddly « à une phase d'animation différente » : le balayage à `frame:` figé
ne pouvait pas converger. En réalité NeoST rend cette démo **byte-identique** à Hatari
(0 px sur 220 trames consécutives).

**Règle** : pour tout étalon qui boote un disque, ne JAMAIS épingler un numéro de trame
oracle. Utiliser `oracle_scan: N` dans `tools/etalons.json` — `run_etalons.py --oracle`
extrait alors la fenêtre `[frame-N, frame+N]` (via `HATARI_ORACLE_SCAN` que comprend
`hatari_oracle.sh`) et retient la trame **identique** à la capture NeoST, jamais la moins
pire ; s'il n'y en a aucune, il le dit et échoue — c'est alors une vraie divergence.
Côté NeoST rien de tout ceci ne se pose : l'émulation est déterministe, et une référence
une fois commise se compare de façon reproductible.

⚠ **`oracle_scan` ne rattrape PAS tout — mesuré le 2026-09-01 (A11).** Il corrige une
**renumérotation de trames**, pas un décalage **sous-trame**. Sur `spec512_bands` —
l'étalon généré dont TOUT l'objet est de rendre *un cycle CPU visible à l'œil* (la
position horizontale des bascules de palette) — le tirage RNG décale le démarrage du
programme de quelques cycles, donc les BANDES elles-mêmes. Deux runs de la même ligne de
commande donnent alors deux jeux de phases **entièrement disjoints** (md5 des trames de la
fenêtre : aucun commun), et aucune des 4 phases présentes dans une fenêtre de 181 trames
ne correspond à la référence commise — la moins pire à 2 460 px, couleurs permutées
circulairement sur 4 px au bord de chaque bande, signature d'un décalage de 4 cycles.
Conséquence : une référence oracle peut être **vraie et pourtant non re-dérivable**. Celle
de `spec512_bands` reste un authentique oracle Hatari (elle porte sa LED disquette) et
prouve ce qu'elle prouve — un jour, sur un run donné, NeoST a rendu cette image AU PIXEL
comme Hatari — mais elle ne se régénère pas. Le manifeste le déclare
(`oracle_check: false` + `oracle_check_note`, obligatoire) et `--oracle-check` la saute
**en la nommant**.
**Résolu le même jour, dans le programme** : la séquence vit dans le handler de VBL et le
programme attend en `stop #$2300` — latence d'exception FIXE, là où une boucle de scrutation
(ou un `bra.s` d'attente) prend l'interruption à une frontière d'instruction. Deux runs Hatari
rendent alors les mêmes phases, NeoST les mêmes images ; l'exclusion est levée. **Règle pour
tout étalon généré dont la mesure est sous-ligne** : ancrer sur la VBL par `stop`, jamais sur
le compteur vidéo.

## Contrôler les références SANS les écraser (`--oracle-check`, A11)

`run_etalons.py --oracle` **régénère et écrase** les références `ref_kind: oracle`. C'est
l'outil pour en POSER une, pas pour en contrôler une : il efface la preuve qu'il devrait
comparer. D'où le mode ajouté pour A11 :

```sh
python3 tools/run_etalons.py --oracle-check            # tous les étalons oracle
python3 tools/run_etalons.py --oracle-check --only blitter_hog
```

Il rejoue Hatari au pin, retient (via `oracle_scan`) l'image identique à la capture NeoST
du jour, puis la **confronte** à la référence commise — sans jamais écrire dans
`tests/reference/`. Trois choses sont donc vraies quand il passe :

```
NeoST  ==  référence commise  ==  Hatari (aujourd'hui, au pin)
```

Ce que ça attrape et qu'aucun autre palier ne voit : une référence régénérée à la main
contre un oracle **non épinglé**, un pin déplacé sans repose des références, un ffmpeg
dont le décodage bouge. Le périmètre est imprimé à chaque exécution (`--oracle-check sur
N étalon(s)`), et ce qui en sort — `ref_kind ≠ oracle`, ou ROM propriétaire absente — est
recensé nommément : un vert sur deux étalons au lieu de sept doit se voir.

Il tourne en CI **hebdomadairement** (`.github/workflows/oracle.yml`, plus
`workflow_dispatch`), pas au push : bâtir Hatari et rejouer les fenêtres coûte des
minutes, et ce qui dérive ici dérive en semaines. Le réflexe reste de le lancer à la main
après un changement de pin ou une repose de références.

## Recette headless : boot → image PNG

```sh
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy   # aucune fenêtre / audio (CI, headless)
HATARI=extern/hatari/build/src/hatari                # cf. ci-dessus : PAS sur le PATH
"$HATARI" --machine megaste --tos roms/etos256us.img --monitor rgb \
       --sound off --fast-forward on --confirm-quit off --statusbar off --drive-led off \
       --alert-level fatal \
       --run-vbls 400 \
       --avirecord --avi-vcodec png --avi-file /tmp/h.avi
# Extraire une frame (ffmpeg dispo via Homebrew). N = n° de frame ; ~60 fps dans l'AVI.
# ⚠ -reinit_filter 0 : sans lui le n de select REPART DE ZÉRO à chaque changement de
# format pal8↔rgb24 de l'AVI (cf. Pièges vérifiés). --drive-led off : pas de LED incrustée.
ffmpeg -y -reinit_filter 0 -i /tmp/h.avi -vf "select=eq(n\,300)" -frames:v 1 -update 1 -pix_fmt rgb24 /tmp/h.png
```

- **`--avirecord` capture l'écran ÉMULÉ** (pas la fenêtre hôte) → marche avec
  `SDL_VIDEODRIVER=dummy`. C'est la clé du headless : pas besoin d'un display ni d'envoyer
  la touche screenshot. `--avi-vcodec png` = frames PNG sans perte dans un conteneur AVI.
- Choisir une frame **du milieu** (pas la dernière : les toutes dernières frames peuvent
  être noires/transition de sortie). L'AVI fait la taille de l'overscan (ex. 832×552) avec
  de larges bordures noires ; le contenu utile est au centre.
- `--alert-level fatal` : **indispensable** — sinon Hatari ouvre des **dialogues GUI
  bloquants** (ex. l'avertissement TOS≤1.4, cf. piège ci-dessous) qui figent l'exécution.
- `--run-vbls N` : exécute N VBL (≈ N/50 s de temps ST en PAL) puis quitte proprement.
  `--fast-forward on` accélère (ne change pas le nombre de VBL émulées).

## Comparaison temporelle des IRQ

```sh
python3 tools/run_timing_oracle.py                    # NeoST ↔ références commises
python3 tools/run_timing_oracle.py --oracle-check     # + Hatari rejoué deux fois
python3 tools/test_timing_oracle.py                   # mutations du comparateur
```

Ce banc complète le cycle-bench CPU (auto-régression NeoST) et les comparaisons
pixel. Il réutilise le parseur de `trace_diff.py` et son principe de périodes,
mais vérifie aussi la phase cumulée : une IRQ perdue ou une petite dérive répétée
peuvent conserver la même période dominante.

Vingt cartouches libres générées par `tools/make_timing_probe.py` et
`tools/timing_extended.py` prennent la main avant TOS. Pile et tampons restent
sous 128 Ko : la MMU n'a pas encore été programmée. Les dates sont relevées sur
les PC du programme invité, sans interpréter les diagnostics propres à chaque
émulateur.

| Sonde | Configuration et mesure |
|---|---|
| Timer A | delay, division 4, donnée 192 ; 256 IRQ après 32 échauffements |
| Timer B | delay, division 10, donnée 83 ; idem |
| Timer C | delay, division 64, donnée 37 ; idem |
| Timer D | delay, division 200, donnée 17 ; idem |
| Son DMA STE | 256 octets, 50066 Hz stéréo, repeat, Timer A event-count 1 ; idem |
| Timer B vidéo | event-count 1, DE descendant ; idem, après deux VBL PAL |
| DMA disquette | lecture réelle du secteur 2 synthétique, 32 rafales de 16 octets + IRQ |
| DMA disquette, STX | même programme qu'en lecture, sur une piste Pasti à descripteurs : IDs à des angles non standard, `ReadTime` de 20 000 µs pour le secteur lu |
| DMA disquette, écriture | WRITE SECTOR du secteur 2 ; rafales vues au compteur `$FF860D` (le DMA lit la RAM) ; statut contrôlé, secteur relu sur l'image NeoST |
| Blitter HOG | 64 copies de 16×8 mots, après huit échauffements, IRQ GPIP3 |
| Blitter partagé | mêmes données, CPU scrutant BUSY pendant les transferts |
| Partagé petit / grand | 16×1 et 16×16 mots ; 64 copies après huit échauffements |
| Partagé / HOG MULU | 16×8 mots, multiplication entière dans la boucle de poll |
| Partagé / HOG TAS | BUSY posé par TAS sous verrou read-modify-write |
| Partagé / HOG RAM | même programme copié en RAM ST, créneaux GLUE actifs |
| DIVU / DIVS RAM | 24 couples (débordements compris) exécutés en RAM ST ; chaque itération comparée au cycle près |

Les deux machines utilisent STE, 1 Mo, 8 MHz, RGB/PAL et EmuTOS 256 Ko FR.
Hatari tourne avec `--cpu-exact on --compatible on --timer-d off`, configuration
et HOME temporaires, SDL dummy, graine fixe 1. Le FDC est physique (`--fastfdc off`),
sa disquette est créée dans le dossier de sortie, jamais une image utilisateur.
Les traces portent `NEOST_TRACE_CYC=1` et `cpu_disasm,cpu_video_cycles`.
Les fichiers complets, dont les deux captures Hatari distinctes (`hatari-1` et
`hatari-2`), restent dans `tests/out/timing/` pour le diagnostic.

**Trace et horloge corrigées.** `willExecute` trace une seule fois avant chaque
instruction réelle. L'ancien appel après `execute()` journalisait aussi les
continuations d'IRQ/RTE et les attentes STOP : certains tours CPU étaient donc
comptés deux fois. Le PC et les registres décrivent maintenant l'entrée réelle.
L'horloge reste dans le domaine `Scheduler::liveNow`. Le compteur CPU brut
incluait les 40 cycles de reset, hors Scheduler, absorbés au premier IACK : une
mesure à cheval sur ce rebase donnait un faux écart de 38 cycles. Le changement
d'horodatage l'a ramené à **2 cycles** sur les quatre timers, sans changer leur
émulation. Le comparateur refuse une mutation réintroduisant ces 40 cycles.

Chaque capture dure 40 trames. Pour les six sondes MFP/son, les 256 dates après
32 échauffements sont normalisées sur la première ; aucun recalage ultérieur
n'est permis. La période dominante doit être identique. Phase cumulée et
première IRQ depuis l'armement sont bornées à **4 cycles sur les timers**, et
**512 sur le son DMA** (une ligne PAL, résolution du refill HBL ; premier écart
actuel 158 cycles). La sonde Timer B attend deux VBL avant l'armement pour ne pas
confondre le régime PAL avec la trame transitoire 60→50 Hz du reset.

**DMA disquette.** Le programme scrute le premier mot de chaque rafale, avance
son pointeur de 16 octets, puis attend l'IRQ. Il vérifie tous les mots du secteur
et le statut WD1772. Les 32 dates sont comparées depuis la première rafale
(borne de 24 cycles, une itération du poll), ainsi que la date de l'IRQ depuis
la dernière rafale (4 cycles). Les 31 comptes de tours CPU entre rafales doivent
être **identiques** : cette mesure du travail effectué pendant la DMA complète
la cadence des données et détecte un stall CPU supprimé sans changement des
échéances. L'attente rotationnelle initiale est exclue, jamais ajustée pour
masquer une dérive dans les rafales. Mesure actuelle : phases et travail CPU
identiques, IRQ à −2 cycles. L'écriture, STX et les erreurs FDC restent à couvrir.

**Blitter.** Chaque copie réinitialise la destination et les adresses, scrute
BUSY et vérifie tous les mots copiés. Les durées armement→fin observée et
armement→IRQ sont comparées individuellement ; le compte de tours CPU qualifie
le partage de bus. Les dix sondes HOG/partagées rendent maintenant **fin à +4
cycles, IRQ à +2 en ROM / +4 en RAM et compte CPU identique**. Aucune exception KNOWN n'est admise,
les bornes restent ±4 cycles et zéro tour CPU. Les références restent des
mesures pures de Hatari, vérifiées par deux captures identiques.

Le correctif porte les reprises PRE_START sous-instruction, les bornes avant et
après accès bus, l'exclusion de l'accès pendant lequel la tranche a commencé et
le recouvrement CPU interne jusqu'au prochain accès bus. TAS mémoire reste
verrouillé entre lecture et écriture, préfetch libre. Le dispatch DMA par accès
est préservé par rebase du quantum ; les autres événements ne sont pas servis
par le seul helper PRE_START. Le crédit CPU est sauvegardé en **v49** : v48 et
antérieurs sont refusés. Le mutant omettant ce crédit fait échouer le test réel
TAS D0 → snapshot → BRA (deux cycles perdus).

```sh
python3 tools/run_timing_oracle.py --strict          # même politique que le mode normal
```

L'étalon pixel historique `blitter_timer`, exécuté en RAM, passe de **68 à zéro
pixel** contre Hatari : les 100 valeurs TADR et les données copiées concordent.
L'alignement RAM se fait au début du cycle, avant SYNC(2) ; le calcul au callback
médian ajoutait une attente après une tranche blitter. HOG suit désormais le même
PRE_START et le même recouvrement que le partagé. La référence PNG provient de
Hatari (`--oracle`), sans modification des sondes ni tolérance pixel ajoutée ;
l'oracle a été reproduit deux fois. Les variantes MegaSTE 16 MHz/cache restent
à qualifier dans `docs/HATARI_DIVERGENCES.md`.

**Résolution de l'écriture disquette.** La première rafale n'arrive pas au même
angle de rotation chez les deux émulateurs (NeoST rencontre le secteur environ
9 500 cycles plus tard ; cette attente initiale est exclue, comme en lecture).
La scrutation de `$FF860D` coûte 22 cycles par tour : la phase est donc bornée à
24 cycles et le travail CPU toléré à un tour près par intervalle. La lecture,
elle, reste exacte sur le travail CPU. La lecture STX a la même limite, et sa
fin se mesure depuis une dernière détection elle aussi arrondie : fin bornée à
24 cycles. Le cadencement `ReadTime` reste vérifié sans ambiguïté (une rafale
toutes les ~5 000 cycles au lieu de 4 096).

## Suite de tests d'Hatari rejouée sous NeoST

```sh
python3 tools/run_hatari_suite.py        # palier full ; SKIP sans extern/hatari
```

Les programmes de `extern/hatari/tests` qui écrivent leur verdict dans un fichier
sont posés dans `C:\AUTO` d'un disque hôte (`--gemdos`) : EmuTOS les lance au boot,
à défaut du mode `--tos none` d'Hatari. Le fichier produit est comparé ligne pour
ligne au résultat attendu versionné par Hatari. Couverts : `cycles/cyccheck.prg`,
`blitter/blitemu.ttp` (STE) et `buserror/buserr_b|w.prg` sur ST, Mega ST 4 Mo, STE et
MegaSTE 4 Mo — 10/10 au 2026-10-20. `NEOST_RAM_SLOT=0` fait échouer `cycles` sur sept
lignes.

`cpu/int_test.tos` (30 tests d'instructions entières, 68000) et `gemdos/gmdostst.tos`
(chemins, fichiers, appels système sur le disque hôte) écrivent leur verdict sur la
console : Hatari le lit sur sa sortie en `--tos none`, NeoST le relève avec
`--console-log FILE` (copie de `Cconout`/`Cconws`/`Fwrite(1)` au passage du trap #1 de
la cartouche système, donc seulement avec `--gemdos`, et de `Bconout` sur CON ou l'écran
brut au trap #13). Seuls comptent les appels émis hors ROM : le GEMDOS du TOS repasse
par le BIOS (texte compté deux fois sinon) et le desktop n'est pas le programme testé.
La redirection `Fforce` est suivie comme à l'écran : vers un fichier géré par le TOS
(octet `p_devx[1]` de la basepage ≥ 0, relevé sous EmuTOS, TOS 1.04 et 2.06),
`Cconout`/`Cconws`/`Fwrite(1)` partent dans le fichier et ne sont pas relevés ; vers un
fichier hôte (le TOS n'en sait rien), seul `Fwrite(1)` l'est. Limite assumée : un
`Fforce(1, h)` de restauration avec `h` issu de `Fdup(1)` laisse `p_devx[1]` ≥ 0 et le
texte suivant est perdu — échec visible, jamais un faux succès. Verdict : celui de leur
`run_test.sh` (ni « fail » ni « error »), plus un compte exact des lignes `Test '…' OK`
contre la table `tests[]` du source. 12/12 au 2026-10-24.

Restent hors banc : `xbios` et `mem_end`, qui appellent des extensions propres à Hatari
(`Dbmsg` en XBIOS 11, `Scrdmp` intercepté en capture) et non du matériel ST, `screen`
(PNG au format de capture d'Hatari) et `serial`.

Les traces sans horloge, incomplètes ou non monotones, les sources absentes,
les redémarrages, les erreurs de données, les IRQ manquantes et les exceptions
mémoire font échouer le banc. Les tests du comparateur injectent aussi des
mutations : IRQ perdue, dérive lente, ancien biais de reset, suppression des
32 cycles par rafale, modification du travail CPU et retard de fin, quatre
cycles de moins sur une seule division, deux tours de scrutation de plus en écriture.

Le palier `full` compare NeoST à la référence sans requérir Hatari. La CI
hebdomadaire `oracle.yml` vérifie en plus que deux captures Hatari reproduisent
exactement la référence commise, y compris les comptes CPU. La référence porte
le pin et les SHA-256 des principaux sources temporels Hatari (dont FDC et
blitter), de la ROM, des cartouches et de la disquette synthétique. Une variation
du pin ou de ces sources bloque la redérivation. Pour reposer volontairement
les références après examen des écarts :

```sh
python3 tools/run_timing_oracle.py --record-reference
```

Cette commande exige le pin de `tools/setup_hatari.sh`, deux captures Hatari
identiques et la comparaison NeoST stricte réussie avant
toute écriture. Les autres modes ne modifient jamais les références. Le banc
reste limité aux configurations listées ; MegaSTE 16 MHz et cache ne sont pas
couverts.

## Autres signaux (sans image)

- `--conout 2` : redirige la **console EmuTOS/VT-52** vers stdout — utile pour suivre le
  boot (messages, panics) sans image.
- `--trace <flags>` (`--trace help` pour la liste) + `--trace-file FILE` : trace CPU /
  IRQ / vidéo… façon MAME. Comparable aux traces NeoST headless (`trace_diff.py`).
- `--parse FILE` : exécute des commandes du **débogueur** intégré (points d'arrêt, dump
  mémoire/registres après N cycles) → introspection scriptée.
- `--log-file FILE`, `--log-level info|warn|...`.

### Injection d'entrée headless — l'oracle appuie sur les touches TOUT SEUL (A11, 2026-09-01)

`tools/hatari_oracle.sh` sait tenir une touche **à la VBL près**, sans aucune attente
horloge :

```sh
HATARI_ORACLE_KEYS="900:960:57" bash tools/hatari_oracle.sh <tos> <disk> <vbls> <frame> out.png st fastfdc 1
#                    down:up:scancode  (répétable, séparés par des espaces ; espace = 57, entrée = 28)
```

Dans le manifeste, c'est `"oracle_keys": [[900, 960, 57]]` — `run_etalons.py` le traduit
en `HATARI_ORACLE_KEYS` pour `--oracle` comme pour `--oracle-check`. C'est ce qui a rendu
**re-dérivable l'oracle de `nocooper`** (espace tenue vers la VBL 900), posé à la main
jusque-là.

**Le mécanisme, mesuré sur le build épinglé** :
- `--parse` pose un point d'arrêt `b VBL = N :once` par événement ; Hatari s'y GÈLE et
  attend une commande du débogueur sur **stdin** — une fifo que le script tient ;
- le script pousse alors `hatari-event keydown|keyup <scancode>` dans la fifo de contrôle
  (`--cmd-fifo`), puis `c` sur stdin. L'événement est lu par la boucle SDL au tour suivant
  (`sdl/gui_event.c:133` → `Control_CheckUpdates`) : appliqué à la VBL **N+1**, déterministe ;
- tout prompt du débogueur reçoit un `c`, même inattendu (vu : ré-entrée après un `:file`) ;
  la boucle ne se fie qu'au `VBL=N` imprimé par le prompt pour décider ce qui est dû.

**Deux affirmations de l'ancienne recette étaient FAUSSES**, et leur coût était réel :
- ~~« `--cmd-fifo` désactive le fast-forward → temps réel ~50 vbl/s »~~ — **non** :
  562,9 VBL/s avec la fifo, 565,0 sans (blitter_hog, 3 000 VBL). Toute la chorégraphie
  « attendre 30 s réelles » n'avait pas lieu d'être ;
- ~~« Hatari bloque à l'ouverture de la fifo jusqu'à ce qu'un writer s'y connecte »~~ —
  la fifo est ouverte `O_RDONLY | O_NONBLOCK` (`control.c:553`), ça ne bloque pas. En
  revanche un writer laissé connecté fait rendre `EAGAIN` à chaque lecture, que Hatari
  journalise à chaque trame (« command FIFO read error ») : le script n'ouvre le writer
  que le temps d'un message.

Ce qui reste vrai : un `keypress` (make + break instantanés) peut être **ignoré** par le
poll clavier d'une démo — tenir la touche (`down:up` distants de quelques dizaines de
VBL). Les scancodes sont ceux de l'ST (57 = espace, 28 = entrée). Joystick : pas
d'événement direct ; `--joystick <port>` (touches curseur) ou scancodes.

## Options machine utiles

| Option | Effet |
|--------|-------|
| `--machine st\|megast\|ste\|megaste\|tt\|falcon` | profil matériel |
| `--tos <file>` | image TOS/EmuTOS |
| `--cpulevel <0..>` | type 680x0 (EmuTOS/TOS 2.06 seulement) |
| `--monitor mono\|rgb\|vga\|tv` | type moniteur (mono = haute rés) |
| `--country <x>` | code pays pour EmuTOS multi-langue |
| `--fast-boot on` | patche TOS/memvalid pour booter plus vite |

## Pièges vérifiés

- **ffmpeg renumérote les trames en cours d'AVI si on le laisse faire.** Hatari encode chaque
  image PNG en `pal8` dès qu'elle tient en 256 couleurs, en `rgb24` sinon : le format change
  sans arrêt le long de l'AVI (nocooper : 0-2 pal8, 3 rgb24, 4-408 pal8, 409 rgb24…). À
  chaque changement ffmpeg RECONSTRUIT son graphe de filtres et le `n` de `select` **repart
  de zéro** — « frame 1000 hors de l'AVI » alors que `ffprobe` compte 1 100 images, ou pire,
  une fenêtre de scan silencieusement décalée. `hatari_oracle.sh` passe `-reinit_filter 0`
  (+ `-pix_fmt rgb24`) sur ses deux extractions depuis le 2026-09-01 ; ne pas l'oublier
  dans une commande à la main.
- **La LED disquette n'est pas une fatalité** : `--drive-led off` la supprime des captures
  (zone vérifiée entièrement noire). Le masque `buffer_noled` de `compare_screenshot.py`
  reste nécessaire tant que des références COMMISES la portent encore.
- **TOS ≤ 1.4 → forcé en mode ST.** Hatari lit la version dans l'en-tête TOS ; un EmuTOS
  **192 Ko** (`etos192*.img`) se présente en **« TOS 1.4 / Atari ST »** et Hatari **refuse**
  de le lancer en MegaSTE/TT (« TOS versions <= 1.4 work only in ST mode », bascule auto en
  ST). Pour MegaSTE il faut un **EmuTOS 256 Ko** (`etos256us/fr.img`, qui se présente
  « Atari Mega STe ») ou un TOS 2.05/2.06. C'est ainsi qu'on a tranché la question du SCU
  (cf. `CHANGELOG` : EmuTOS 256K **programme** le SCU comme TOS 2.06).
- **`--avirecord` peut exiger un booléen explicite** : sur le build Linux du sous-module
  (v2.6.1-devel), la forme drapeau `--avirecord` échoue (« Usage: … ») — écrire
  `--avirecord on`. La forme drapeau passe sur le binaire Homebrew macOS.
- **Oracle AUDIO** : l'AVI embarque la piste son → `ffmpeg -i h.avi -vn -acodec pcm_s16le h.wav`
  donne le WAV de référence (48 kHz via `--sound 48000`). ⚠ Hatari applique un HPF sous-sonique
  + IIR LMC au mix : les métriques à composante continue ne sont PAS comparables à NeoST (pas
  de HPF sur le canal DMA) — comparer des ratios de contenu (fenêtres, spectres), ou mieux :
  `--trace dmasound` logge chaque fetch FIFO (« DMA snd fifo refill adr=… ») à diff-er contre
  `NEOST_DMASND_TRACE=1` côté NeoST (cf. `DEV.md`).
- **`--avirecord` exige `--avi-file`** ; sans `--avi-vcodec png` le défaut peut être un
  codec moins pratique à décoder.
- Au **premier lancement** Hatari crée `~/Library/Application Support/Hatari/` (config,
  NVRAM). Un `INFO : NVRAM not found` au boot est normal.

## Récupérer un EmuTOS récent (libre, GPL)

```sh
curl -sL -o /tmp/e.zip "https://downloads.sourceforge.net/project/emutos/emutos/1.4/emutos-256k-1.4.zip"
unzip -o /tmp/e.zip -d /tmp/e && cp /tmp/e/emutos-256k-1.4/etos256us.img roms/
```
Le paquet `256k` contient toutes les langues (`etos256us.img`, `etos256fr.img`, …). Le
paquet `192k` est pour ST/STE (TOS 1.x), le `256k` pour Mega ST/STE/TT/Falcon (TOS 2.x/3.x).


## CPU et cache MegaSTE à 16 MHz

```sh
python3 tools/run_megaste_bus_oracle.py
python3 tools/run_megaste_bus_oracle.py --oracle-check
python3 tools/run_megaste_bus_oracle.py --record-reference
```

Onze cartouches libres, MegaSTE + EmuTOS 256 Ko FR, RAM sous 128 Ko. Les sondes
basculent à 16 MHz via $FF8E21 : lectures byte/word/long avec cache off, miss
avec remplissage ou hit amorcé, écritures word/long et contrôle des données.
Elles masquent les interruptions et n'exécutent pas de DMA. Quatre trames,
huit échauffements puis 64 durées mesurées sur les PC début/fin de l'instruction.
Hatari exprime ces intervalles stables en cycles CPU 16 MHz, NeoST en cycles bus
8 MHz : division exacte par deux côté Hatari, hors intervalle de bascule.
Aucune tolérance de cycle : les 64 durées doivent coïncider.

Référence `tests/reference/megaste_bus_hatari.json`, schéma/protocole, empreintes
ROM/programmes et sources au pin vérifiés. Enregistrement seulement après deux
captures Hatari identiques et comparaison NeoST réussie ; `--oracle-check`
rejoue ces conditions sans modifier les références. Vérifié dans `full` et la
CI oracle ; les tests des comparateurs refusent un cycle d'écart, un facteur
deux, une sonde modifiée ou un échantillon manquant.

Ces résultats qualifient les accès CPU/cache en régime établi, pas la chaîne
globale blitter/IRQ à 16 MHz. Le banc
`python3 tools/run_megaste_blitter_checks.py` vérifie douze configurations
NeoST contre des bornes physiques. Ajouter `--oracle-audit` rejoue le pin :
dix cas échouent, sans tolérance ni référence promue. Deux défauts Hatari
sont isolés par des corrections expérimentales ; les écarts temporels
résiduels restent à qualifier. Protocole, mesures et patch diagnostique :
[`HATARI_MEGASTE_AUDIT.md`](HATARI_MEGASTE_AUDIT.md). Les tests
locaux couvrent séparément les quatre phases RAM, l'invalidation avant
arbitration et la conservation d'un hit déjà lu quand la DMA change la RAM.
