# TODO — NeoST

(c) 2026 VERHILLE Arnaud. Feuille de route ancrée sur les deux références à
croiser systématiquement :

- **Hatari** : source principale pour le comportement ST/STE/MegaSTE déjà
  éprouvé (`src/*.c`, `src/includes/*.h`). Cloner pour référence :
  `git clone --depth 1 https://github.com/hatari/hatari`.
- **MAME** : source complémentaire pour les composants séparés et les tables de
  mapping (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`,
  `ataristb.cpp`, devices `mc68901`, `wd_fdc`, `6850acia`, `z80scc`, `rp5c15`,
  `ay8910`, `lmc1992`). Même si le driver MegaSTE MAME est encore marqué
  `MACHINE_NOT_WORKING`, il documente des blocs matériels essentiels.

Objectif qualité : NeoST doit pouvoir émuler proprement un **Atari MegaSTE** :
68000 8/16 MHz, 1/2/4 Mo ST-RAM, TOS 2.05/2.06, STE video/sound/joypads,
blitter, RTC RP5C15, SCC Z85C30, SCU, ACSI/SCSI DMA, lecteur DD/HD, et timing
assez fidèle pour jeux, démos et utilitaires système.

État NeoST : boote EmuTOS + TOS 1.02 (green desktop), disquette FAT12, clavier,
souris relative, son YM2149 tons+bruit. Modèle « DMA instantané » + horloge
ligne-par-ligne (≈ pas cycle-accurate).

## Socle MegaSTE / types de machine

- [ ] **Profils machine réels** ST / Mega ST / STE / **MegaSTE** (`configuration.h`,
      `configuration.c`, MAME `atarist.cpp`) : ROM attendue, mémoire, blitter,
      RTC, STE DMA sound, SCC, SCU, bus errors et registres présents/absents selon
      le modèle. Aujourd'hui NeoST émule un ST de base avec quelques briques.
- [ ] **ROM TOS MegaSTE** : chargement fiable des TOS 2.05/2.06 256 Ko à
      `$E00000`, choix pays, vérification des checksums, fallback EmuTOS MegaSTE.
- [ ] **ST-RAM MegaSTE 1/2/4 Mo** (`stMemory.c`, MAME `stmmu.cpp`) : banques
      MMU via `$FF8001`, remapping réel des banques 128/512/2048 Ko, zones non
      peuplées, accès superviseur/utilisateur et bus errors cohérents.
- [ ] **Bus map complet** (`ioMemTabST.c`, `ioMemTabSTE.c`, MAME `atarist.cpp`) :
      différencier registres ST, STE, Mega ST et MegaSTE, y compris les adresses
      qui renvoient `void`, `0x00`, `0xff`, open-bus ou bus error.
- [ ] **Cartridge port** `$FA0000-$FBFFFF` (Hatari `cart.c`, MAME `stcart`) :
      cartouches diagnostic, jeux et extensions de boot.

## CPU, cache, FPU, bus

- [~] **Cœur CPU sélectionnable au démarrage (Musashi / Moira)** : abstraction
      `CpuCore` + sélection via `--cpu`, `neost.cfg` (`cpu=`) et l'UI WASM
      (`?cpu=`). **Musashi** (MAME, MIT) reste le défaut. **Moira** (cœur de
      vAmiga, MIT, **cycle-exact**, sous-module `extern/moira`, compilé en C++20)
      **boote EmuTOS pixel-identique** et **délivre correctement les IRQ** (≈538
      niveau 6 + 98 niveau 4 sur 100 trames, comme Musashi). Le cœur UAE/WinUAE
      (60k lignes, GPLv2) a été écarté au profit de Moira. **Reste (≠ IRQ)** :
      sous TOS 1.02, l'autoloader `STARTGEM.PRG` ne lance pas `ARKANOID.PRG` sous
      Moira (divergence fonctionnelle à diffé­rencier de Musashi) ; tracer regs
      sous Moira (le Tracer lit encore les registres Musashi). Cf.
      `docs/CYCLE_ACCURACY.md` §5bis.
- [ ] **Horloge 8/16 MHz MegaSTE** : registre `$FF8E21` bit 1 (`ioMemTabSTE.c`,
      MAME `cache_w`) avec changement de fréquence CPU à chaud et recalcul des
      timings vidéo, MFP, DMA, FDC, ACIA.
- [ ] **Cache MegaSTE 16 Ko** : `$FF8E21` bit 0, désactivé si CPU à 8 MHz comme
      Hatari ; pour la qualité, modéliser au moins les effets de timing et
      d'invalidation visibles par les logiciels sensibles.
- [ ] **MC68881 optionnel** (`configuration.h`, MAME `fpu_r/fpu_w`) : réponse à
      la sonde TOS/diagnostic, puis émulation réelle ou trapping propre.
- [ ] **Wait states et contention bus** (`cycles.c`, `cycInt.c`, MAME
      `stmmu.cpp::bus_contention`) : alignement CPU/mémoire, retards MMIO, bus
      errors après délai matériel, accès PSG/MFP avec cycles supplémentaires.
- [ ] **Séparation user/supervisor** : bus errors en mode utilisateur sur I/O et
      ROM/low memory protégées comme dans MAME `st_user_map`.

## Précision temporelle (le grand chantier)

> Plan détaillé calqué sur Hatari : **[`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)**
> (ordonnanceur d'événements datés, vidéo/MFP/FDC au cycle, validation par
> `trace_diff` ↔ Hatari ; cas concret : le `$26E7` d'Arkanoid).

- [ ] **Horloge cycle-accurate** (`cycles.c`, `cycInt.c`, `m68000.c`). Hatari
      compte les cycles bus précisément et planifie les interruptions au cycle
      près (`CycInt_AddRelativeInterrupt`). NeoST exécute 512 cycles/ligne en
      bloc → suffisant pour booter, insuffisant pour beaucoup de jeux/démos.
- [ ] **Wait states** d'accès YM2149 / mémoire (`psg.c` : 4 cycles + alignement).
- [~] **Ordonnanceur unique d'événements** CPU/MFP/vidéo/FDC/DMA/ACIA : remplacer
      les tics par frame/ligne par des événements datés en cycles, comme Hatari
      `CycInt_*` ou les timers MAME. **Phases 1-3 faites** : `src/core/Scheduler.hpp`,
      `Machine::runFrame` événementiel à **horloge continue** avec carry du
      dépassement ; vidéo au cycle (rendu/Timer B/HBL aux cycles 376/400/508) ;
      **timers MFP réels** A/C/D en mode délai datés par le MFP (Timer C remplace
      le faux, Timer D désormais actif). Reste : FDC/DMA datés, Timer B délai,
      pulse-width, bordures, 50/60 Hz (cf. `docs/CYCLE_ACCURACY.md`).
- [ ] **Modes PAL/NTSC** : quartz, nombre de lignes, 50/60 Hz, timings couleur et
      mono ; MegaSTE doit respecter les mêmes bases que STE.

## MFP 68901 (`mfp.c`, 3500 lignes)

- [x] Timer C (200 Hz) et Timer B (event-count) approximatifs. ✓
- [x] HBL niveau 2 + interruption FDC (canal 7). ✓
- [~] **Timers A et D** + modes complets de chaque timer : **delay** (prescaler
      4/10/16/50/64/100/200), **event-count**, **pulse-width**. Compteur qui
      reboucle via `PendingCyclesOver` (cf. en-tête de `mfp.c`).
- [ ] Timing d'interruption au cycle près (latence MFP, IACK), GPIP toutes lignes,
      AER (edge), interruptions RS232/USART (canaux transmit/receive).
- [ ] **Chaînage matériel complet** : I0 busy imprimante, I1 DCD, I2 CTS, I3
      blitter, I4 ACIA, I5 FDC, I6 RI, I7 mono/DMA sound selon STE/MegaSTE
      (cf. MAME `machine_start`, `psg_pa_w`, `dmasound_set_state`).
- [ ] **Timer B lié au Display Enable** au cycle près, pas seulement par ligne,
      car les overscans et démos STE/MegaSTE s'en servent.

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
- [ ] **Mapping clavier international** et layout TOS (`keymap.c`, MAME `stkbd`) :
      touches spéciales FR/UK/DE, autorepeat, reset clavier, scan codes exacts.

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
- [ ] **Base vidéo basse STE** `$FF820D` et **line width** `$FF820F` :
      indispensables pour le scrolling matériel et les écrans non standards.
- [ ] **Registres fine scroll** `$FF8264/$FF8265` : préfetch/no-prefetch, effets
      au cycle près, interactions avec line width.
- [ ] **Joypads/paddles/lightpen STE** (`joy.c`, MAME `$FF9200-$FF9222`) :
      directions, boutons, sélection multiplexée, entrées analogiques.
- [ ] **DIP switches MegaSTE** `$FF9200` (`ioMemTabSTE.c`) : bit HD floppy,
      désactivation DMA sound, logique inversée.

## Disquette (`fdc.c` 7600 lignes, `floppy.c`)

- [x] WD1772 instant DMA : Restore/Seek/Read/Write/ReadAddress, géométrie BPB. ✓
- [~] **Timing réel** : commande non-instantanée (BUSY + INTRQ différé daté sur
      l'ordonnanceur, Phase 4) + **statut WD1772 fidèle** (cf. Hatari `fdc.c`) :
      MOTOR_ON, SPIN_UP (type I), WPRT, TR00, RNF selon le type de commande.
      Reste : moteur on/off temporisé + spin-up réel, **index pulse**
      (3.71 ms/rotation), step rate exact — pour les protections et le timing fin.
- [~] **Write protect** (✓ : refus d'écriture + bit WPRT) + **détection de
      changement de média** (Mediach, reste à faire) : pour monter/éjecter à chaud
      sans reset (la Disk Library).
- [~] **Densité** DD/HD ($FF860E ✓ : registre relisable), **Flopwr** ✓ : les
      écritures sont recopiées dans le fichier `.st` monté (`Fdc::writeBack`).
- [~] Formats : **.msa** ✓ (décompression RLE → .st en mémoire, `Fdc::decodeMsa`).
      **.dim** (simple) à ajouter ; **.stx**/**.ipf** (flux bas niveau / protections)
      et **.zip** (archive) restent hors périmètre actuel (nécessitent un moteur
      flux ou une lib d'archive).
- [x] **Lecteur B** ✓ : `drive_[2]` routé par le PSG (port A bits 1/2),
      `loadImage(path, drive)` ; montage en B via l'UI WASM (`neost_mount_disk_b`).
- [ ] **FIFO DMA/MMU** (`stmmu.cpp`) : secteur-count, status bits, DRQ/INTRQ,
      transfert par blocs, erreurs DMA, adresse 24 bits et interaction ACSI/FDC.
- [ ] **Lecteur HD MegaSTE** : DIP `$FF9200`, densité DD/HD, WD1772 à fréquence
      appropriée, images 1.44 Mo et changement de média propre.

## Audio (`psg.c`, `dmaSnd.c`)

- [ ] YM2149 : **enveloppe** (registres 11-13, 16 formes), **table de volume**
      logarithmique correcte, port B (Centronics), filtrage.
- [ ] **DMA sound STE** ($FF8900+, `dmaSnd.c`) : fréquence sélectionnable,
      mono/stéréo, microwire ($FF8922), mixage avec le YM2149.
- [ ] **LMC1992 / Microwire** (Hatari `dmaSnd.c`, MAME `lmc1992`) : volume,
      bass/treble, balance, masque/data `$FF8922/$FF8924`, timing de shift.
- [ ] **IRQ/GPIP liés au DMA sound** : fin de frame, repeat/loop, interaction I7
      avec détection mono comme dans MAME `dmasound_set_state`.
- [ ] **Bruits du lecteur de disquette** (confort/immersion : ronron moteur,
      « clac » de pas de tête, click de chargement, tic d'index).

      *Où trouver la source ?* — Ce n'est **pas** du matériel : ni Hatari ni MAME
      n'émulent les bruits **mécaniques** du lecteur (vérifié : `floppy.c`/`sound.c`
      de Hatari ne contiennent aucun module de bruit-lecteur, et `--help` n'a pas
      d'option dédiée). C'est **STeem SSE** qui a popularisé la fonctionnalité, à
      partir d'**échantillons WAV** déclenchés sur les événements du FDC, pas d'une
      synthèse. Donc la « source de vérité » ici, ce sont des **samples** :
        - paquets de sons de lecteur de STeem SSE (cf. son dépôt / forums Atari) ;
        - banques libres de bruits de floppy 3"½ (freesound.org, domaine public) ;
        - ou enregistrer un **vrai lecteur ST** (le plus fidèle) : boucle moteur,
          « clac » d'un pas de tête, chargement de tête, tic d'index ~3.7 ms.
      Quatre échantillons suffisent : `motor` (boucle), `step` (un pas), `seek`
      (rafale de pas, ou `step` répété au step-rate) et option `index`.

      *Où brancher dans NeoST ?* — Sur les **événements** déjà présents dans
      `src/io/Fdc.cpp`, sans toucher au timing :
        - `executeCommand()` type I (RESTORE/SEEK/STEP) : un click `step` par
          piste franchie (le nombre de pas est déjà calculé dans
          `commandDelayCycles()` via le step-rate `{6,12,2,3} ms`) ;
        - passage de `MOTOR_ON` à 1/0 (bit `FDC_MOTOR_ON`) : démarrer/arrêter la
          boucle `motor` (avec la temporisation moteur-off à implémenter) ;
        - option : tic `index` cadencé sur la rotation (~3.71 ms) quand le moteur
          tourne.
      Émettre ces événements via un petit *callback* (ex. `setSoundSink(fn)`) pour
      garder `neost_core` sans dépendance audio. Le mixage se fait côté frontend :
        - GUI : `src/audio/Audio.cpp` (miniaudio) — décoder les WAV et les mixer
          au flux YM2149 ;
        - WASM : `src/web/main_web.cpp` — Web Audio API (`AudioBufferSourceNode`),
          échantillons préchargés via `--preload-file` (cf. `CMakeLists.txt`).
      Prévoir un interrupteur on/off (menu GUI + bouton/`?sound=` côté WASM).

## Blitter, SCU & stockage

- [ ] **Blitter** ($FF8A00, `blitter.c`) — actuellement bus error (= absent sur
      ST de base). À émuler pour le Mega ST / MegaSTE : halftone RAM, HOP/LOP,
      end masks, skew, FXSR/NFSR, smudge, hog/bus sharing, IRQ MFP I3.
- [ ] **SCU MegaSTE** `$FF8E01-$FF8E0F` (`ioMem.c`, sources TT/MegaSTE Hatari) :
      system interrupt mask/state, interrupter registers, VME mask/state, GPR1/2.
- [ ] **Cache/CPU control MegaSTE** `$FF8E21` (`ioMemTabSTE.c`) : lecture/écriture
      exacte, adresses voisines sans bus error, contrainte cache impossible à
      8 MHz.
- [ ] **GEMDOS HD** (`gemdos.c`) : monter un **dossier hôte comme lecteur** C: —
      très pratique pour échanger des fichiers sans image disquette.
- [ ] **ACSI / disque dur** (`hdc.c`, `ncr5380.c`, MAME `stmmu.cpp`) : commandes
      DMA HDC, jusqu'à 8 périphériques, block size 512, boot disque dur TOS.
- [ ] **SCSI / NCR5380** pour la gamme MegaSTE/TT selon configuration Hatari,
      avec attention aux différences ACSI externe vs contrôleur interne.
- [ ] **IDE** (`ide.c`) reste utile hors MegaSTE strict, mais ne doit pas polluer
      le profil MegaSTE si absent du modèle choisi.

## Périphériques (`acia.c`, `rtc.c`, `midi.c`, `rs232.c`, `scc.c`)

- [ ] **ACIA 6850** complète (clavier + MIDI), timing TX/RX, MIDI ($FFFC04).
- [ ] **MIDI ACIA** : deuxième 6850 `$FFFC04/$FFFC06`, horloge 500 kHz, IRQ
      fusionnée sur MFP I4, ports host MIDI.
- [ ] **RTC RP5C15** Mega ST/MegaSTE (`rtc.c`, MAME `rp5c15`) : registres BCD
      `$FFFC21-$FFFC3F`, bank select, test/reset, année configurable.
- [ ] **NVRAM / préférences TOS MegaSTE** si nécessaire : résolution/boot device
      et paramètres persistants attendus par TOS 2.x.
- [ ] **RS232 MFP USART** (`rs232.c`) : SCR/UCR/RSR/TSR/UDR, RTS/DTR via PSG port
      A, DCD/CTS/RI sur GPIP, fichiers/ports host.
- [ ] **SCC Z85C30 MegaSTE** (`scc.c`, MAME `z80scc`) : canaux A/B, canal A LAN,
      canal B série, interruptions, baudrate, routage host.
- [ ] **Imprimante/Centronics** (`printer.c`, MAME `centronics`) : port B YM data,
      strobe PSG port A bit 5, busy sur MFP I0.

## Types de machine

- [ ] **STE** (DMA sound, blitter, fine scroll, palette 4 bits, joypads),
      **Mega ST** (blitter + RTC), **MegaSTE** (STE + 16 MHz/cache + SCU + SCC +
      RTC + HD floppy + ACSI/SCSI). **TT/Falcon** restent hors périmètre.

## Cas concret à débloquer

- [ ] **Arkanoid** (Imagine 1987) : **auto-bootable via `AUTO\ARKANOID.PRG`**
      (pas via le boot sector, checksum `$F9B9` ≠ `$1234`). Charge + affiche son
      écran-titre (✓ basse rés couleur, TOS 1.02), puis se fige sur
      `031736: tst.b $26E7 / 03173C: bne`. Constats par trace :
      - le jeu installe son handler VBL en `$70` (`034CB2: move.l #$34c9a,$70`),
        qui s'exécute bien (vectorisation auto-vecteur niv. 4 OK) et chaîne vers le
        VBL TOS **sans** toucher `$26E7` ;
      - `$26E7` doit donc être remis à 0 par une **autre IRQ à un cycle précis** ;
        avec le modèle ligne/trame de NeoST, elle ne tombe pas au bon moment ;
      - côté Hatari, le diff confirme que la divergence est temporelle.
      → Correctif structurel : **[`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)**
      (Phases 0→3 : ordonnanceur daté + vidéo + timers MFP au cycle).

## Outillage / qualité

- [x] **Comparaison automatique de traces Hatari ↔ NeoST** ✓ : `tools/trace_diff.py`
      aligne une trace NeoST (`neost-headless --trace --regs`) et une trace Hatari
      (`--trace cpu_disasm[,cpu_regs]`) sur un PC commun (`--align-pc`) et localise
      la première divergence — flux (PC) ET registres (D0-D7/A0-A7/SR). C'est la
      méthode pour Arkanoid & co. (Reste à capturer la trace Hatari de référence.)
- [ ] **Comparaison MAME ↔ NeoST** : exploiter le débogueur MAME et les logs
      devices pour comparer memory map, bus errors, FDC/MMU FIFO, blitter et SCC.
- [ ] **Suite de ROMs de diagnostic** : Atari/MegaSTE diagnostics, Yaart/ST-RAM,
      tests blitter, tests DMA sound, tests SCC/RTC/FDC HD.
- [ ] **Matrice de compatibilité MegaSTE** : TOS 2.05/2.06, EmuTOS MegaSTE,
      1/2/4 Mo, 8/16 MHz, cache on/off, DD/HD, mono/couleur.
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI

- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.
