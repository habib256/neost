# Automatiser Hatari (oracle de référence)

Hatari est la **source de vérité matérielle** de NeoST (cf. `CLAUDE.md`). Au-delà de la
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
déjà été rendu **FAUX** par cet écart. Côté NeoST, utiliser `--key-hold N` pour égaliser.

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
NeoST ne le ramène pas, `git submodule update` non plus, et **aucun script d'installation
ne s'en occupe**. Sur une machine fraîche (ou après un ménage), il est simplement ABSENT —
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
