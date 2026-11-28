# Atari TT030 — jalon d’amorçage

Le profil TT expérimental amorce TOS 3.06 UK et DE jusqu’au bureau GEM
en headless.
Le cœur CPU est le Moira officiel 6.0b1, épinglé à `c6fbb4ea`. Le 68030 et
la carte mère TT doivent être vérifiés ensemble. Les bancs autonomes de
Moira couvrent le cœur CPU et sa MMU sans exercer la carte mère.

## ROM et trace

Les exemplaires privés `roms/tos306uk.img` et `roms/tos306de.img` du poste
de développement ont chacun 524 288 octets. Le champ de version du
premier vaut `$0306` et son en-tête indique une base ROM à `$00E00000`.
Ces ROM restent hors Git. Utiliser un dump provenant de sa propre machine.

Avec un binaire headless construit sur ce jalon, lancer le profil TT avec
4 Mio de ST-RAM pour reproduire le relevé local. Atari décrit une capacité
de base de 2 Mio (§ II.4).

```sh
./build/neost-headless roms/tos306uk.img --machine tt --mem 4m --no-disk \
  --frames 1800 --screenshot /tmp/neost-tt.ppm
```

Le headless propose `--mem 2m` et `--mem 4m`. L’option `--tt-ram` accepte
`0`, `4m` ou `16m`. Pour diagnostiquer un arrêt, borner la trace à une
trame.

```sh
./build/neost-headless roms/tos306uk.img --machine tt --mem 4m --no-disk \
  --frames 90 --trace-from 89 --trace /tmp/neost-tt.trace --regs --irq
```

Pour relever un amorçage, garder le journal, la dernière image et, au
besoin, la trace d’instructions. Noter le SHA de NeoST, la région de la ROM
et la mémoire configurée. Le journal doit identifier le profil TT et le
68030. L’image et la trace permettent de distinguer un bureau GEM atteint d’un
arrêt plus tôt dans le démarrage.

## Relevé d’amorçage

TOS 3.06 UK avec 4 Mio de ST-RAM atteint l’initialisation du MFP et renseigne
les cookies `_CPU=30`, `_MCH=$00020000` et `_VDO=$00020000`. Avec
`--tt-ram 4m`, la ROM inscrit `ramtop=$01400000` et `ramvalid=$1357BD13`.
Sans disquette et sans TT-RAM, la capture à 1 800 trames montre le logo
Atari et le message « Memory Test Complete. » avec 4 096 Kio de ST-RAM.

Après 7 500 trames sans disque, le bureau GEM affiche les menus et les
icônes en ST basse. La capture avec bordures mesure 416 × 276 pixels.
Les deux essais utilisent TOS 3.06 sans modification.

| ROM | ST-RAM | TT-RAM |
| --- | ---: | ---: |
| UK | 4 Mio | 4 Mio |
| DE | 2 Mio | 0 |

```sh
./build/neost-headless roms/tos306uk.img --machine tt --mem 4m --tt-ram 4m \
  --no-disk --frames 7500 --screenshot /tmp/neost-tt-uk-gem.ppm
./build/neost-headless roms/tos306de.img --machine tt --mem 2m --tt-ram 0 \
  --no-disk --frames 7500 --screenshot /tmp/neost-tt-de-gem.ppm
```

Sur la ROM UK, un appui sur Espace à la trame 800 abrège l’attente de
démarrage. La séquence souris injectée à la trame 3 300 déroule le menu
`Desk`. L’entrée `Desktop Info...` est visible à la trame 3 450.

```sh
./build/neost-headless roms/tos306uk.img --machine tt --mem 4m --tt-ram 4m \
  --no-disk --frames 3450 --key-hold 5 --scancode-at 800 39 \
  --mouse-at 3300 UUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUUULLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL........RRRR \
  --screenshot /tmp/neost-tt-uk-desk.ppm
```

L’amorçage exerce l’autovecteur vidéo de niveau 2, les interruptions MFP,
le cache de données et la PMMU du 68030. Deux corrections ont été nécessaires
pour atteindre GEM. Les parcours de tables de la PMMU accèdent aux
descripteurs par cycles physiques superviseur. Le cache de données
invalide le mot long concerné lorsqu’une écriture partielle rate le tag
avec l’allocation sur écriture activée.
[Détails et tests du cœur Moira](../extern/moira/NEOST_VENDOR.md)

## Affichage TT-medium

Le mode 4 du registre `$FF8262` produit une image de 640 × 480 pixels
à quatre plans. NeoST décode les 153 600 octets de ST-RAM en fin de trame,
à partir de la base vidéo, et utilise une banque de 16 couleurs 0RGB
choisie par l’octet `$FF8263`. L’interface graphique et les captures headless
lisent la même image. Le test synthétique contrôle les quatre plans, la banque
de palette, le changement de ligne et le rejet d’une plage hors ST-RAM.
À l’amorçage, TOS 3.06 UK écrit les couleurs dans la palette ST. Leur
conversion dans la banque de palette TT rend visibles les plans déjà
dessinés en mémoire écran.

L’image de fin de trame réutilise la cadence de planification existante.
Les autres modes TT natifs et les effets de palette en cours de ligne restent
hors du jalon. Le SCSI et le RTC disposent de registres provisoires.
La sauvegarde et la restauration d’un état TT renvoient une erreur explicite.

### Bureau GEM en TT-medium

TOS 3.06 UK lit la résolution du bureau dans le deuxième champ de la ligne
`#E` de `NEWDESK.INF`. La valeur `04` sélectionne TT-medium. La disquette
FAT12 suffit à configurer le bureau, avec la NVRAM d’origine de l’essai.
Depuis la racine du dépôt, créer l’image et l’insérer en lecture seule.

```sh
python3 - <<'PY'
from pathlib import Path
from tools.run_libretro import write_floppy

desktop = (
    b"#E 18 04 00 06 \r\n"
    b"#M 00 00 00 FF A FLOPPY DISK@ @ \r\n"
    b"#M 01 00 00 FF B FLOPPY DISK@ @ \r\n"
    b"#T 00 03 02 FF   TRASH@ @ \r\n"
)
write_floppy(Path("/tmp/neost-tt-ttmed.st"), {"NEWDESK.INF": desktop})
PY
./build/neost-headless roms/tos306uk.img --machine tt --mem 4m --tt-ram 4m \
  --disk /tmp/neost-tt-ttmed.st --disk-ro --key-hold 5 --scancode-at 800 39 \
  --frames 3450 --screenshot /tmp/neost-tt-uk-ttmed.ppm
```

La capture NeoST à la trame 3 450 mesure 640 × 480 et contient trois couleurs.
Les 307 200 pixels concordent avec la capture Hatari à la trame 7 500 avec
la même ROM, 4 Mio de ST-RAM et 4 Mio de TT-RAM, PMMU et 68882 actifs et
`--tos-res ttmed`. Hatari produit un `NEWDESK.INF` virtuel pour cette
option. La comparaison porte sur ce bureau et cette configuration.

## Sources

- [B] Atari Corporation, *TT030 Hardware Reference Manual*, juin 1990.
  Sections II.1–II.4 pour le processeur, la FPU, la ROM et la RAM.
  La section II.3 décrit les huit premiers octets de ROM et leur accès
  superviseur, ainsi que la fenêtre ROM à `$00E00000`. La section III.4
  décrit les deux MC68901. La section V donne le plan mémoire du 68030
  et place la TT-RAM optionnelle à `$01000000`. Le scan local est
  `extern/datasheets/Atari_TT030_HW_Reference_Manual.pdf` (SHA-256
  `93c17beebfc42517f1df3318f1c55c9f0ac68aadd07949369c0a0517d68063ee`).
  L’introduction décrit une conception à 16 MHz.
- [B] Atari Corporation, *TT030 Computer Field Service Manual*, août 1991,
  C302483-001, § 2.2.1–2.2.4. La documentation de maintenance décrit le
  68030 et le 68882 à 32 MHz, les 512 Kio de ROM et les 2 Mio de RAM de base.
  [Scan du manuel](https://www.atari-computermuseum.de/archiv/tech/sm_tt.pdf)
- [C] TOS 3.06 UK, en-tête et trace du démarrage. Les valeurs et accès
  observés décrivent les attentes de la ROM. Les dates du bus et les
  résultats sur machine réelle demandent leurs propres relevés.
- [C] EmuTOS, `bios/screen.c` @ `05285e51` utilise le mode TT-medium
  sur écran couleur et écrit la banque dans l’octet bas de `$FF8262`.
  [Source épinglée](https://github.com/emutos/emutos/blob/05285e510226c5ca4258376b8aed120cd02278a4/bios/screen.c)
- [D] Hatari, `video.c`, `Video_GetTTPaletteWords` et
  `Video_RenderTTScreen` @ `36a8acf0` pour la banque de palette et
  l’organisation des plans en TT-medium.
