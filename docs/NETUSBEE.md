# NetUSBee : Ethernet et périphériques USB

Deux ports USB virtuels accompagnent la NE2000 du NetUSBee. Sur chacun : aucun
périphérique, clavier HID, souris HID, ou disque SCSI/Bulk-Only sur une image brute.
Les deux ports peuvent être occupés simultanément ; changer de périphérique dans
Configuration → Network produit un débranchement/rebranchement USB. Tout est désactivé
par défaut. Les périphériques USB physiques de l'hôte ne sont pas ouverts.

## Utilisation

```sh
./build/neost-headless roms/etos256us.img --mem 4m --netusbee --nat \
  --usb1 keyboard --usb2 mouse --disk usbdrivers.st --frames 2400
./build/neost-headless roms/etos256us.img --mem 4m --netusbee \
  --usb1 keyboard --usb2 disk:hd/usb.img --disk usbdrivers.st --frames 2400
```

`--usb1` et `--usb2` acceptent `none`, `keyboard`, `mouse`, `disk:CHEMIN` et
`disk-ro:CHEMIN`. Une image doit être un multiple non nul de 512 octets, au maximum
4 Gio moins 512 octets. Elle contient la table de partitions et le système de fichiers,
comme une clé USB réelle. Les écritures sont persistantes ; `disk-ro:` interdit celles-ci.
Un fichier non accessible en écriture est ouvert en lecture seule. Un montage refusé
conserve le périphérique précédent. Les réglages `usb1=` et `usb2=` sont persistés dans
`neost.cfg` et dans les profils, et rejoués par `--from-cfg`.

Le clavier et la souris hôtes sont dirigés vers leur périphérique USB lorsqu'il est
branché ; en son absence, ils passent par l'IKBD. Le clavier graphique, les scripts
headless et les entrées de la manette utilisent le même routage. Les usages clavier
proviennent des positions ST déjà établies par le frontend.

**Il faut les pilotes dans le ST.** Avec USB4TOS : lancer `USB.PRG`, les classes
`KEYBOARD.PRG`, `MOUSE.PRG`, `STORAGE.PRG`, puis **`NETUS000.PRG`** (version 68000).
Les mêmes protocoles sont disponibles aux pilotes FreeMiNT, mais leur exécution sous
MiNT n'a pas été vérifiée ici. L'Ethernet utilise les pilotes EtherNEC/STinG existants.

## Modèle et limites

`Isp1160` cadence les trames à **1 ms de temps émulé**, par rattrapage aux lectures du
CPU et aux HBL, indépendamment des 50/60/71 Hz vidéo. L'IRQ de la carte reste non câblée
au ST ; les pilotes testés utilisent le polling. Le hub racine gère la puissance
globale/par port, les changements de connexion, l'activation, la suspension et un reset
de port de 10 ms. La reprise de suspension est immédiate : son délai électrique reste
approximatif `[F]` et demanderait une mesure ou un modèle plus complet.

La FIFO ATL respecte `HcTransferCounter`, EOT, l'alignement des PTD sur quatre octets,
les IN dont seul l'en-tête a été écrit, LastPTD, Active, les comptes/toggles et les
codes STALL, absence de périphérique, paquet court et NAK. Un NAK laisse le PTD actif,
à replanifier par le pilote. Les transactions sont traitées à la frontière USB ; les
durées de paquets, erreurs électriques et arbitrages submillisecondes ne sont pas
simulés `[F]`. ITL/isochrone, hubs externes et autres classes USB ne sont pas exécutés.

Les périphériques HID supportent les descripteurs, l'adressage/configuration standard,
les rapports boot clavier (modificateurs, six touches, rollover) et souris (boutons,
déplacements signés avec conservation du reste), GET/SET_IDLE, GET/SET_PROTOCOL et les
LED du clavier. Le disque fournit CBW/data/CSW, GET_MAX_LUN, reset BOT/clear-halt,
INQUIRY, REQUEST SENSE, TEST UNIT READY, MODE SENSE(6), READ CAPACITY(10), READ/WRITE(6/10),
START STOP, PREVENT/ALLOW et SYNCHRONIZE CACHE. Les commandes inconnues échouent avec
un sense SCSI. Les périphériques ont une identité explicitement virtuelle (VID 0),
pas celle d'un produit commercial.

Le format de sauvegarde **v59** conserve l'horloge, les registres/FIFO, les rapports
HID et les phases EP0/BOT, y compris une lecture disque partielle. Le même type et la
même image doivent être remontés avant le chargement. L'état n'ouvre jamais un chemin
issu de son contenu. Comme pour l'ACSI, les secteurs vivent dans le fichier hôte et
ne sont pas rembobinés par un save-state.

## Preuves et reproduction

- `[B]` Philips **ISP1160**, Rev.05, 24/12/2004, §9.3–9.4 (PTD/FIFO), §10.3
  (hub), §10.4.3 et §10.6.7 (compteur/EOT). Copie dans l'[archive du concepteur](https://hardware.atari.org/files/netusbee.zip),
  `netusbee/datasheets/ISP1160-05.pdf`, SHA-256
  `89e8df382cfbef84d71d4d4a6feac83bce526a9b6650c60b16015e9cb2960afa`.
- `[B]` [USB 2.0](https://www.usb.org/document-library/usb-20-specification), 27/04/2000,
  chap.9 ; [HID 1.11](https://www.usb.org/document-library/device-class-definition-hid-111),
  27/06/2001, §7 et annexe B ; [Bulk-Only 1.0](https://www.usb.org/document-library/mass-storage-bulk-only-10),
  31/08/1999, §5–6.
- `[C]` FreeMiNT/USB4TOS **098f002a**, `sys/usb/src.km/ucd/netusbee/isp116x.h`
  et `isp116x-hcd.c` (`pack_fifo`, `unpack_fifo`), contrats réellement exécutés.
- Le texte `program_spec.txt` de 2006, distribué avec les plans de 2007, place le
  latch USB en `$FA8000`, tandis que le pilote exécuté utilise `$FA0000`. Cette
  différence de révision reste à mesurer sur une carte réelle. Dans le modèle courant,
  `$FA0000` est aussi une **lecture** du CR Ethernet : le latch ne le modifie pas.
  L'ancienne documentation qui le décrivait comme une écriture du CR était erronée.

`./build/neost-headless --netusbee-selftest` couvre le protocole cartouche, les
périphériques, les erreurs, les images, le save-state et l'horloge USB (39 contrôles).
Ce test est dans le palier fast ; il ne prétend pas constituer une mesure matérielle.

Le banc logiciel utilise l'[archive officielle USB4TOS 098f002a](https://atari.joska.no/snapshots/freemint/usb4tos/usb4tos-1-19-098f002a.zip),
SHA-256 `5aa775555b3a8a73d1b122c131ece1fb24f3b09a7654f8faf1125d13d0110e99`.
Les pilotes ne sont pas copiés dans le dépôt. Une archive locale suffit :

```sh
python3 tools/make_netusbee_test.py /tmp/usb4tos.zip /tmp/usbtest.st
python3 tools/make_usatan_hd.py /tmp/usbdisk.img
./build/neost-headless roms/etos256us.img --mem 4m --netusbee \
  --usb1 keyboard --usb2 disk:/tmp/usbdisk.img --disk /tmp/usbtest.st \
  --frames 3000 --keys-at 1800 a --serial-dump /tmp/usbtest.txt

python3 tools/make_netusbee_test.py /tmp/usb4tos.zip /tmp/usbhid.st --hid
./build/neost-headless roms/etos256us.img --mem 4m --netusbee \
  --usb1 keyboard --usb2 mouse --disk /tmp/usbhid.st --frames 2600 \
  --keys-at 1800 a --mouse-at 1830 RRRRR --serial-dump /tmp/usbhid.txt
```

Validé le 2026-11-10 sous macOS : `usbdisk PASS`, `usbkey PASS`, `usbmouse PASS`.
Le premier passe par les appels **GEMDOS** et écrit `C:\USBTEST.TXT` (`USB!`), les autres
par **Bconin** et **mousevec** du TOS. L'énumération seule n'est donc pas le verdict.
Le pointeur GEM n'est pas encore initialisé pendant AUTO : le probe souris sonde
le paquet livré à son vecteur, puis restaure celui-ci.

`python3 tools/run_netusbee_boot.py --drivers /tmp/usb4tos.zip` automatise les deux
scénarios et relit le fichier FAT16 écrit dans l'image hôte. Le palier full lance
ce banc avec l'archive locale `extern/usb4tos/usb4tos-1-19-098f002a.zip` ; sans elle,
il est recensé SKIP (77), sans téléchargement automatique.
