# `disks/midi/` — lecteur GEMDOS « musique » (Cubase Lite + corpus SMF)

Monté tel quel en **C:** (`gemdos=disks/midi` dans `neost.cfg`, ou `--gemdos disks/midi`),
ce dossier est un ST de studio prêt à jouer : **TOS 1.04, Mega ST 1 Mo, mono** (MROS
panique sous EmuTOS — incompatibilité connue, pas un bug NeoST ; cf.
`docs/CASE_STUDIES.md`).

```
C:\CUBLITE\        Cubase Lite (Steinberg, 1996, freeware) + MROS — pas de clé
C:\DESKTOP.INF     bureau TOS 1.04 ; la ligne #Z auto-lance CB_LITE.PRG au boot
C:\BACH\           78   inventions, sinfonies, Clavier bien tempéré I (préludes + fugues)
C:\MOZART\         49   17 sonates (K.279 → K.576), K.545 « facile », K.331 Alla turca…
C:\BEETHOVE\       12   Clair de lune (3 mvts), Pathétique, Waldstein, Appassionata, Tempête…
C:\CHOPIN\         13   préludes op. 28 (1, 3, 4, 6, 7, 15, 17, 20, 24), mazurkas
C:\HAYDN\           3   sonates Hob. XVI:22, 37, 52
C:\SCARLATT\        4   sonates K.1, 84, 384, 514
C:\BLUES\          32   blues / boogie (le corpus d'origine de midi_simplify.py)
C:\ALBERTAM.ALL         un morceau ré-enregistré depuis l'émulateur dans Cubase Lite
sources/                ce qui n'est PAS pour le ST : SMF modernes + partitions .krn
```

**Un dossier 8.3 par compositeur**, et dans chacun : les `.MID` que Cubase Lite sait
importer (format 1, 96 PPQN, **un seul canal, une piste de tempo + une piste de notes**,
ni SysEx ni textes) et un `NOMS.TXT` qui relie chaque nom 8.3 à son titre long.
`sources/<compositeur>/` contient les SMF modernes (noms longs) et `kern/` les partitions.

## Provenance et licence

Musique dans le domaine public. Les **partitions** (`sources/*/kern/*.krn`) sont les
encodages Humdrum de **Craig Stuart Sapp** (KernScores ; dépôts GitHub `craigsapp/*` et
`humdrum-tools/*`), licence **CC BY-NC-SA 4.0** : les `.mid` et `.MID` en sont des rendus
et restent sous cette licence (attribution ici, usage non commercial). Rendu par
**music21** (qui lit les nuances → vélocités) ; quand music21 perd des voix (les divisions
de spine `*^` des préludes du Clavier bien tempéré), par **Verovio** (`brew install
verovio`, vélocité fixe). Pas d'interprétation humaine : c'est un corpus de **test**, mais
Pianoteq en fait quelque chose de très écoutable. Le blues vient tel quel du lot mesuré
pour `midi_simplify.py`.

Régénération d'un dossier : `python3 tools/midi_simplify.py disks/midi/sources/bach
disks/midi/BACH --per-channel --detach`. Le préfixe avant le premier `_` du nom long
devient le nom 8.3 (`wtc1p01_bach_bwv-846.mid` → `WTC1P01.MID`). `--detach` est
indispensable pour du piano polyphonique (fusion des unissons, notes répétées séparées
d'un tick — sinon Cubase les coupe à 2 ms), et le convertisseur plafonne le tempo à
250 bpm (au-delà Cubase Lite reste à 120) et ne garde l'armure qu'au tick 0 (plus loin,
Cubase jette la piste de notes). Tout cela a été **mesuré** avec l'étalon ci-dessous.

## Jouer une pièce dans Pianoteq (ou tout synthé CoreMIDI)

1. `neost.cfg` : `gemdos=disks/midi`, `rom=roms/tos104fr.img`, `machine=megast`,
   `mem=1m`, `mono=1`, `midi_out_port=1` (et `midi_out_gm=0`, `midi_out_mt32=0` pour
   ne pas doubler le son). Ou Configuration → MIDI → *Virtual port*.
2. Dans Pianoteq : *Options → Devices → MIDI input* : cocher **« NeoST MIDI OUT »**.
   Le port virtuel est une *source* PASSIVE : c'est au logiciel de s'y abonner.
3. Cubase se lance seul. *File → Import…* → OK → `C:\CHOPIN\RAINDROP.MID` →
   **Enter du pavé numérique** = Play (`0` = Stop).

La même copie est sur la disquette `disks/cubase_lite.st` (fichiers à la racine,
`DESKTOP.INF` lance `A:\CB_LITE.PRG`). Elle part avec les paquets. Sous TOS 1.04,
booter sur cette disquette ouvre Cubase ; MROS doit rester à côté du `.PRG`.
EmuTOS ne lance pas la ligne `#Z` et MROS n'y démarre pas.

## Jouer dans un appareil MATÉRIEL (expandeur, groovebox, clavier)

Un synthé matériel ne s'abonne à rien : le port virtuel ci-dessus ne l'atteint pas, et
il fallait un patchbay tiers au milieu. Depuis le 2026-08-29, NeoST **choisit lui-même
sa destination** : `midi_out_device=<nom>` dans `neost.cfg`, ou Configuration → MIDI →
*Hardware device*. Le nom est celui qu'affiche le système (« Circuit Tracks MIDI ») ;
`neost-headless --midi-list` les énumère. Symétriquement, `midi_in_device=<nom>` fait
ENTRER un clavier maître dans le MIDI IN du ST — Cubase l'enregistre alors comme il
enregistrerait un clavier branché sur le DIN de la machine. Détails et pièges
(branchement à chaud, notes bloquées) → [`../../docs/EXTENSIONS.md`](../../docs/EXTENSIONS.md).

Les octets MIDI sont livrés à l'heure de leur cycle 68000 (+30 ms fixes) : pas de
gigue de trame ; tempo mesuré exact à +0,1 %. Si Pianoteq signale des « duplicate
note-ON », c'est un fichier à plusieurs canaux jouant les mêmes notes (certains blues
doublent le piano sur deux canaux) — les dossiers classiques sont mono-canal.

## Vérifier sans oreille : `tools/run_midi_sequencer.py`

Rejoue tout ça en headless (boot → Cubase → Import → Play), journalise le MIDI OUT
daté au cycle (`--midi-dump`) et compare note à note au fichier (hauteur, vélocité,
durée, pédale, tempo, gigue). Étalon du palier `fast` ; sur une pièce donnée :
`python3 tools/run_midi_sequencer.py --song disks/midi/BACH/WTC1P01.MID`. Les 159
pièces classiques ont été passées ainsi (2026-08-23).
