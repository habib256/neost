# TODO — NeoST

(c) 2026 VERHILLE Arnaud. Feuille de route ancrée sur la **source d'Hatari**
(émulateur ST de référence) : chaque item cite le fichier Hatari `src/*.c` qui
documente le comportement matériel exact à reproduire. Cloner pour référence :
`git clone --depth 1 https://github.com/hatari/hatari`.

État NeoST : boote EmuTOS + TOS 1.02 (green desktop), disquette FAT12, clavier,
souris relative, son YM2149 tons+bruit. Modèle « DMA instantané » + horloge
ligne-par-ligne (≈ pas cycle-accurate).

## Précision temporelle (le grand chantier)

- [ ] **Horloge cycle-accurate** (`cycles.c`, `cycInt.c`, `m68000.c`). Hatari
      compte les cycles bus précisément et planifie les interruptions au cycle
      près (`CycInt_AddRelativeInterrupt`). NeoST exécute 512 cycles/ligne en
      bloc → suffisant pour booter, insuffisant pour beaucoup de jeux/démos.
- [ ] **Wait states** d'accès YM2149 / mémoire (`psg.c` : 4 cycles + alignement).

## MFP 68901 (`mfp.c`, 3500 lignes)

- [x] Timer C (200 Hz) et Timer B (event-count) approximatifs. ✓
- [x] HBL niveau 2 + interruption FDC (canal 7). ✓
- [ ] **Timers A et D** + modes complets de chaque timer : **delay** (prescaler
      4/10/16/50/64/100/200), **event-count**, **pulse-width**. Compteur qui
      reboucle via `PendingCyclesOver` (cf. en-tête de `mfp.c`).
- [ ] Timing d'interruption au cycle près (latence MFP, IACK), GPIP toutes lignes,
      AER (edge), interruptions RS232/USART (canaux transmit/receive).

## IKBD HD6301 (`ikbd.c`, 3250 lignes)

NeoST ne gère que souris relative + reset (réponse 0xF1). Hatari implémente le
jeu de commandes complet (`IKBD_Cmd_*`) :

- [ ] Souris : **mode absolu** (`AbsMouseMode`, `ReadAbsMousePos`,
      `SetInternalMousePos`), **seuil** et **échelle** (`SetMouseThreshold`,
      `SetMouseScale`), **axe Y haut/bas** (`SetYAxisUp/Down`),
      **mode keycode** (`MouseCursorKeycodes`), `TurnMouseOff`, `MouseAction`.
- [ ] **Joystick** : auto-report, monitoring, durée de feu, curseur-joystick
      (`ReturnJoystick*`, `SetJoystickMonitoring`, `SetJoystickFireDuration`).
- [ ] **Horloge IKBD** (`SetClock` / `IKBD_Cmd_ReadClock`) — date/heure.
- [ ] Pause/resume transfert, exécution de programme custom 6301 (rare).

## Vidéo / Shifter (`video.c`, 6100 lignes)

NeoST décode un framebuffer fixe par trame. Hatari fait du raster cycle-précis :

- [ ] **Suppression de bordures** (gauche/droite/haut/bas) par bascule 50/60 Hz
      et résolution — base de la plupart des démos (`BORDERMASK_*`).
- [ ] **Sync 50/60 Hz** ($FF820A) et écrans « courts/longs » (171 lignes, etc.).
- [ ] **Spec512** : changement de palette par scanline → 512 couleurs.
- [ ] **Hardware scrolling** STE ($FF8264/8265, fine scroll + line width $FF820F)
      et tricks STF (plane shifting, scroll 4 px).
- [ ] **Compteur d'adresse vidéo** lisible ($FF8205/07/09) — utilisé par les jeux.
- [ ] Palette STE **4 bits/canal** ($FF824x sur STE).

## Disquette (`fdc.c` 7600 lignes, `floppy.c`)

- [x] WD1772 instant DMA : Restore/Seek/Read/Write/ReadAddress, géométrie BPB. ✓
- [ ] **Timing réel** : moteur on/off + spin-up, **index pulse** (3.71 ms/rotation),
      step rate, BUSY — nécessaire pour les protections et le timing fin.
- [ ] **Write protect** + **détection de changement de média** (Mediach) : pour
      monter/éjecter proprement à chaud (la Disk Library le fera alors sans reset).
- [ ] **Densité** DD/HD ($FF860E), **Flopwr** complet → recopie dans le `.st`.
- [ ] Formats : **.msa**, **.dim**, **.stx** (protégés, timing variable, le seul
      moyen de lancer beaucoup d'originaux), **.ipf**, et archives **.zip**
      (`file_archive.c`, `unzip.c`). NeoST ne lit que `.st` brut.
- [ ] **Lecteur B** (sélection déjà décodée via PSG port A).

## Audio (`psg.c`, `dmaSnd.c`)

- [ ] YM2149 : **enveloppe** (registres 11-13, 16 formes), **table de volume**
      logarithmique correcte, port B (Centronics), filtrage.
- [ ] **DMA sound STE** ($FF8900+, `dmaSnd.c`) : fréquence sélectionnable,
      mono/stéréo, microwire ($FF8922), mixage avec le YM2149.

## Blitter & stockage

- [ ] **Blitter** ($FF8A00, `blitter.c`) — actuellement bus error (= absent sur
      ST de base). À émuler pour le Mega ST / les logiciels qui l'exigent.
- [ ] **GEMDOS HD** (`gemdos.c`) : monter un **dossier hôte comme lecteur** C: —
      très pratique pour échanger des fichiers sans image disquette.
- [ ] **ACSI / disque dur** (`hdc.c`, `ncr5380.c`), **IDE** (`ide.c`).

## Périphériques (`acia.c`, `rtc.c`, `midi.c`, `rs232.c`, `scc.c`)

- [ ] **ACIA 6850** complète (clavier + MIDI), timing TX/RX, MIDI ($FFFC04).
- [ ] **RTC / NVRAM** Mega ST/STE (`rtc.c`, MC146818) : horloge + résolution de
      boot mémorisée côté machine.
- [ ] RS232 ($FFFC00 MFP USART), SCC (TT/Mega STE), imprimante (`printer.c`).

## Types de machine

- [ ] **STE** (DMA sound, blitter, fine scroll, palette 4 bits, joypads),
      **Mega ST** (blitter + RTC), **TT/Falcon** (hors périmètre pédagogique ST).

## Cas concret à débloquer

- [ ] **Arkanoid** (Imagine 1987) : charge + affiche son écran-titre via le FDC
      (✓ basse rés couleur, TOS 1.02), puis se fige sur `tst.b $26E7 / bne`. Le
      flag n'est effacé par aucun code exécuté ; le jeu n'utilise ni Xbtimer ni
      timer MFP, son VBL ne touche pas `$26E7` (il appelle seulement
      Getrez/Setscreen/Setpalette/Floprd). Piste : **diff de trace avec Hatari**
      (`neost-headless --trace` vs trace Hatari du même point) pour isoler le
      registre/signal divergent — probablement lié à un timing cycle-précis.

## Outillage / qualité

- [ ] **Comparaison automatique de traces Hatari ↔ NeoST** : Hatari sait tracer
      le CPU (`--trace cpu_disasm`). Un script qui aligne les deux traces sur le
      même ROM/disquette validerait la fidélité instruction par instruction et
      localiserait les divergences (méthode pour Arkanoid & co).
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI

- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.
