# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.** Le fait est dans [`CHANGELOG.md`](CHANGELOG.md).

**Sources de vérité à croiser systématiquement :**
- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
  `wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06,
STE video/sound/joypads, blitter, RTC, SCC, SCU, ACSI/SCSI, DD/HD) avec un timing assez
fidèle pour jeux, démos et utilitaires.

**Légende des étiquettes** : `lot suivant` = portable, faible risque · `précision cycle` =
nécessite l'ordonnanceur daté ([`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)) ·
`risque élevé` = touche le modèle bus/IRQ éprouvé · `gros contrôleur` = puce entière à
écrire · `faible valeur`.

---

## 🎯 Le grand chantier : précision cycle

> Plan détaillé : **[`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)** (ordonnanceur
> d'événements datés, vidéo/MFP/FDC au cycle, validation par `trace_diff` ↔ Hatari).
>
> **Phases 1-3 faites** (`Scheduler.hpp`, `runFrame` événementiel à horloge continue,
> vidéo au cycle, timers MFP A/C/D datés). Reste :

- [ ] **Quantum CPU sous la ligne** — aujourd'hui le CPU avance par instruction (= dépasse
      l'instant planifié) → latence IRQ grossière. C'est **LE blocage architectural** des
      deux cas concrets ci-dessous — réf. `cycInt.c` + `m68000.c` + `cycles.c`
- [ ] **Timer B event-count lié au Display Enable** par ligne (DE_start/end+24) et 50/60 Hz,
      pas figé au cycle 400 — réf. `video.c:Video_TimerB_GetPosFromDE`
- [ ] **Latch palette/scroll mi-ligne** — la scanline est rendue une fois à DE_END (cycle
      376) ; pas de changement intra-ligne — réf. `video.c:Video_RenderLine`
- [ ] **VBL tiré en fin de trame** (~ligne 312 + offset 64 cyc), pas ligne 201 _(risque élevé)_
      — réf. `video.c:Video_InterruptHandler_VBL` (VBL_VIDEO_CYCLE_OFFSET)
- [ ] **Géométries vidéo** : mono (71 Hz, 501×224) et 60 Hz (263×508) en plus du PAL 313×512
      figé _(risque élevé)_ — réf. `video.h` CYCLES/SCANLINES_PER_LINE/FRAME
- [ ] **Wait states** d'accès YM2149 / mémoire (4 cycles + alignement) et contention bus
      _(précision cycle)_ — réf. `psg.c`, `cycles.c`, MAME `stmmu.cpp::bus_contention`

### Cas concrets à débloquer (dépendent du quantum cycle)
- [ ] **Arkanoid** se fige sur `031736: tst.b $26E7 / bne`. Son handler VBL (`$70`) tourne
      mais ne touche pas `$26E7` ; le flag doit être remis à 0 par une **autre IRQ à un
      cycle précis** qui, en modèle ligne/trame, ne tombe pas au bon moment (diff Hatari
      confirme : divergence temporelle).
- [ ] **« T0 MFP timer »** (3 cartouches) : le test met `$284=3`, attend, et un handler MFP
      partagé (`FA13FE`) efface les bits par `bclr` ; les bons timers doivent fire **dans la
      fenêtre**. NeoST fire aux cycles planifiés mais le CPU avance par instruction → dépassement.

---

## Bus / memory map / MMU
- [ ] Joypad/lightpen STE + DIP switches MegaSTE (`$FF9200-$FF9223`) whitelistés mais relisent
      `0xFF` _(lot suivant)_ — réf. `ioMemTabSTE.c` (Joy_StePad*, DIP → `0xBF`)
- [ ] Contrôle cache/CPU MegaSTE `$FF8E21` sans registre relisible _(lot suivant)_ — réf.
      `ioMem.c:IoMem_FixAccessForMegaSTE` + `ioMemTabSTE.c:...CacheCpuCtrl_WriteByte`
- [ ] Registres vidéo STE « void » doivent lire `0x00` (`$FF820B`, `$FF8262-63`, `$FF8266-7F`)
      _(faible valeur)_ — réf. `ioMemTabSTE.c` (IoMem_VoidRead_00)
- [ ] La banque ROM doit couvrir toute la fenêtre 1 Mo (`$E00000-$EFFFFF`), pas la taille du
      fichier _(risque élevé)_ — réf. `cpu/memory.c:memory_map_Standard_RAM` (ROMmem aliasing)
- [ ] Accès mémoire FDC/son-DMA via la traduction MMU au lieu de `ram[]` physique _(risque
      élevé)_ — réf. `stMemory.c:STMemory_DMA_Read/Write*`
- [ ] **Remapping réel des banques MMU** (alias 128/512/2048 Ko) + bus error fidèle des zones
      non peuplées (`$400000-$F9FFFF` au-dessus de la RAM) — réf. `stMemory.c` + `cpu/memory.c`

## MFP 68901 + RS232 USART
- [ ] Écritures AER/DDR/GPIP ne réévaluent pas les IRQ GPIP front-déclenchées _(risque élevé)_
      — réf. `mfp.c:MFP_GPIP_Update_Interrupt`
- [ ] Bit3 GPIP (blitter busy/idle) non représenté en lecture _(lot suivant)_ — réf.
      `mfp.c:MFP_GPIP_ReadByte_Main`
- [ ] Lecture data-register Timer A/C/D renvoie la recharge, pas le compteur vivant _(lot
      suivant)_ — réf. `mfp.c:MFP_ReadTimer_AB/CD`
- [ ] Replanning des timers délai perd le dépassement (PendingCyclesOver) _(lot suivant)_ —
      réf. `mfp.c:MFP_StartTimer_AB/CD`
- [ ] Timer A event-count ignore la polarité AER GPIP4 et recharge à 0 au lieu de 1 _(risque
      élevé)_ — réf. `mfp.c:MFP_TimerA_Set_Line_Input`
- [ ] Config baud USART UCR/Timer-D non modélisée (backing-store seul) _(faible valeur)_ —
      réf. `rs232.c:RS232_HandleUCR + RS232_SetBaudRateFromTimerD`

## Vidéo / Shifter
- [ ] **Câbler le rendu** fine-scroll/line-width/base-basse STE (registres déjà relisibles,
      stride/scroll absents du rendu) _(lot suivant→précision cycle)_ — réf. `video.c`
- [ ] **Suppression de bordures** (gauche/droite/haut/bas, tricks 50/60 Hz) — base des démos
      _(précision cycle)_ — réf. `video.c` BORDERMASK_*
- [ ] **Spec512** (palette par scanline/cycle, 512 couleurs) _(précision cycle)_ — réf.
      `spec512.c` + `video.c:Video_ColorReg_WriteWord`
- [ ] Quirk miroir d'écriture octet de palette (`$FF824x` .B) _(risque élevé)_ — réf.
      `video.c:Video_ColorReg_WriteWord`
- [ ] `$FF820D` en lecture renvoie l'octet bas sur ST au lieu de 0 forcé _(lot suivant)_ —
      réf. `video.c:Video_BaseLow_ReadByte`
- [ ] **Joypads/paddles/lightpen STE** (`$FF9200-$FF9222`) : directions, boutons, multiplexage,
      entrées analogiques — réf. `joy.c`, MAME
- [ ] **DIP switches MegaSTE** `$FF9200` : bit HD floppy, désactivation DMA sound, logique
      inversée — réf. `ioMemTabSTE.c`

## Blitter
- [ ] Partage de bus (mode non-hog) au cycle près _(précision cycle)_ — réf. `blitter.c`

## FDC WD1772 + DMA disquette
- [ ] Loader d'image `.dim` (en-tête 32 o + charge `.st`) _(lot suivant)_ — réf.
      `floppies/dim.c:DIM_ReadDisk`
- [ ] Masquage d'adresse DMA (octet haut `&0x3f`, bas word-align `&0xfe`) _(faible valeur)_ —
      réf. `fdc.c:FDC_WriteDMAAddress`
- [ ] Compteur de secteurs DMA non relisible sur le vrai HW _(risque élevé)_ — réf.
      `fdc.c:FDC_DiskControllerStatus_ReadWord`
- [ ] Accès octet à `$FF8604/06` devrait fauter sur ST non-Falcon _(risque élevé)_ — réf. `fdc.c`
- [ ] **Timing réel** : durée BUSY par commande, INTRQ différé, FIFO DRQ, spin-up
      _(précision cycle)_ — réf. `fdc.c` (FDC_DELAY_*, FDC_UpdateAll)
- [ ] **Lecteur HD MegaSTE** : DIP `$FF9200`, densité DD/HD, images 1.44 Mo — réf. `fdc.c`
- [ ] **FIFO DMA/MMU** : secteur-count, status bits, transfert par blocs, interaction ACSI/FDC
      — réf. MAME `stmmu.cpp`

## YM2149 PSG
- [ ] Données port B Centronics + front strobe (bit5) non émulés en sortie _(faible valeur)_ —
      réf. `psg.c:PSG_Set_DataRegister`
- [ ] Filtre passe-bas RC de sortie (STF) non appliqué _(faible valeur)_ — réf.
      `sound.c:LowPassFilter`
- [ ] Masquage à l'écriture + sélecteur de registre ≥ 16 _(faible valeur)_ — réf. `psg.c`

## Son DMA STE + Microwire/LMC1992
- [ ] Cas limites start==end (stop/loop sans IRQ) _(faible valeur)_ — réf.
      `dmaSnd.c:DmaSnd_StartNewFrame`
- [ ] Décodage commande LMC1992 : run de masque contigu au lieu de tous les bits _(faible
      valeur)_ — réf. `dmaSnd.c:DmaSnd_InterruptHandler_Microwire`
- [ ] Registre mixage LMC1992 (reg 0) décodé mais jamais appliqué (mute/route YM) _(faible
      valeur)_ — réf. `dmaSnd.c:DmaSnd_GenerateSamples`
- [ ] Décodage du son sur l'horloge d'émulation + anneau vers le thread audio (aujourd'hui la
      forme d'onde est générée côté audio, seul l'instant d'IRQ est exact) _(précision cycle)_

## IKBD HD6301 + souris/joystick
- [ ] Paquet souris relatif ignore l'axe Y (`SetYAxisUp/Down 0x0F/0x10`) _(lot suivant)_ —
      réf. `ikbd.c:IKBD_SendRelMousePacket`
- [ ] Seuil (`0x0B`) / échelle (`0x0C`) souris — deltas bruts _(lot suivant)_ — réf.
      `ikbd.c:IKBD_Cmd_SetMouseThreshold/SetMouseScale`
- [ ] `MouseAction 0x07` / report bouton-en-touche / `MouseCursorKeycodes 0x0A` _(lot suivant)_
      — réf. `ikbd.c:IKBD_Cmd_MouseAction`
- [ ] Horloge IKBD (`SetClock 0x1B` / `ReadClock 0x1C`) _(lot suivant)_ — réf. `ikbd.c`
- [ ] Keymap international / layouts TOS (FR/UK/DE, autorepeat) _(faible valeur)_ — réf. `keymap.c`

## ACIA 6850 (clavier + MIDI)
- [ ] IRQ émetteur (CR bits 5/6) + état TDRE (câblé à 1) _(risque élevé)_ — réf.
      `acia.c:ACIA_UpdateIRQ` + `midi.c`
- [ ] Lecture data-register renvoie 0x00 si FIFO vide au lieu du dernier RDR _(faible valeur)_
      — réf. `acia.c:ACIA_Read_RDR`
- [ ] SR n'expose pas overrun/framing/parity _(faible valeur)_ — réf. `acia.c`

## RTC RP5C15
- [ ] Aliasing BANK=1 AM/PM de `$FFFC25/27` (chemin TOS 1.0x) _(faible valeur)_ — réf.
      `rtc.c:Rtc_Minutes*` (rtc_bank)
- [ ] Détection **Mega ST** par EmuTOS : sonde `$FFFC21` + validation `$FFFC25/27` (NeoST
      renvoie `0xFF` → label « Atari ST »). C'est le levier restant pour le label « Mega ST ».

## CPU : IRQ, Moira, MegaSTE
- [ ] **Moira n'honore pas `busFault`** : `NeostMoira::read8/16` appellent `g_bus` sans tester
      `busFault` → aucun bus error sous Moira. Nécessite throw `moira::BusError(makeFrame…)`
      _(risque élevé)_ — réf. `extern/moira/Moira/MoiraExceptions_cpp.h`
- [ ] **Bascule CPU 8/16 MHz MegaSTE** (`$FF8E21` bit1) — change le débit de cycles et tous
      les timings _(précision cycle)_ — réf. `m68000.c:MegaSTE_CPU_Cache_Update` + `clocks_timings.c`
- [ ] **Cache MegaSTE 16 Ko** (`$FF8E21` bit0, off à 8 MHz) — au moins les effets de timing
      visibles — réf. `ioMemTabSTE.c`
- [ ] **Masques d'IRQ SCU MegaSTE** (`$FF8E01/0D`) non modélisés _(risque élevé)_ — réf.
      `scu_vme.c` + `m68000.c:M68000_SetIRQ`
- [ ] **MC68881 optionnel** : réponse à la sonde TOS/diagnostic, puis émulation ou trapping
      — réf. `configuration.h`, MAME
- [ ] **Séparation user/supervisor** : bus errors en mode utilisateur sur I/O et ROM/low mem
      — réf. MAME `st_user_map`

## Stockage & contrôleurs
- [ ] **GEMDOS HD** : monter un **dossier hôte comme lecteur C:** — très pratique sans image
      — réf. `gemdos.c`
- [ ] **ACSI complet** (jusqu'à 8 périphériques, boot disque dur TOS) — réf. `hdc.c`, MAME
- [ ] **SCC Z85C30 MegaSTE** : canaux A (LAN) / B (série), IRQ niveau 5, baudrate _(gros
      contrôleur)_ — réf. `scc.c`, MAME `z80scc`
- [ ] **SCSI / NCR5380** (MegaSTE/TT) _(gros contrôleur)_ — réf. `ncr5380.c`
- [ ] **Imprimante/Centronics** : port B YM data, strobe PSG port A bit5, busy sur MFP I0 —
      réf. `printer.c`

## Périphériques & profils machine
- [ ] **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000`, choix pays, checksums, fallback
      EmuTOS MegaSTE.
- [ ] **NVRAM / préférences TOS MegaSTE** (résolution/boot device) si TOS 2.x l'exige.
- [ ] **Cartridge port** `$FA0000-$FBFFFF` générique (jeux, extensions de boot) — réf. `cart.c`

## Souris / entrées (jeux)
- [ ] **Vroom : boutons souris inopérants** (passage des vitesses). En mode relatif l'IKBD ne
      renvoie l'état des boutons qu'avec un mouvement → vérifier l'émission d'un paquet sur
      **changement de bouton sans mouvement** (`IKBD_SendRelMousePacket`, en-tête `0xF8`).
- [ ] **Curseur GEM sort de l'écran et ne revient pas** : sans seuil/échelle/axe Y/bornage
      IKBD, le curseur file en coin. Vérifier signe/accumulation des deltas + libération Échap.

## Outillage / qualité
- [ ] **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- [ ] Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- [ ] **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
      on/off, DD/HD, mono/couleur.
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI
- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.
