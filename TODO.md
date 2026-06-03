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

- [x] **Quantum CPU sous la ligne** — ✅ FAIT (port `cycInt`). Deux mécanismes : (1)
      **horloge live** (`Scheduler::liveNow`, = `Cycles_GetClockCounterImmediate`) → un
      timer programmé en plein bloc est daté à l'instant réel de l'écriture, pas au début
      du quantum ; (2) **préemption du timeslice** (`Cpu68k::endTimeslice` →
      `m68k_end_timeslice` / drapeau Moira) → le CPU rend la main à la frontière
      d'instruction quand un événement plus proche est armé. Latence IRQ timer **47513 →
      134 cyc** (cas critique : timer court armé juste avant un `STOP`, sauté par l'optim
      STOP de Moira). Boot EmuTOS/TOS pixel-identique, histogramme d'IRQ inchangé, Z/T0 Pass.
      Métrique headless `timer IRQ retard max`. **Préférer `--cpu moira`** (cycle-exact,
      horloge live à la sous-instruction). Reste (raffinement) : offset = cycles de
      l'instruction d'écriture (le timer démarre à la FIN de l'instruction chez Hatari).
- [x] **Timer B event-count lié au Display Enable** — ✅ FAIT (port `Video_TimerB_GetDefaultPos`).
      `Shifter::timerBLinePos(startOfLine)` : fins de ligne (`DE_end+24`) ou débuts
      (`DE_start+24`) selon l'AER bit3 du MFP (`Mfp::timerBStartOfLine`), positions par
      résolution (71 Hz=184/24) et fréquence (50 Hz=400/80, 60 Hz=396/76) ; recalculée par
      ligne (suit un changement AER/sync mi-trame). Plus de cycle 400 figé. Défaut 50 Hz/fin
      inchangé → boot EmuTOS pixel-identique, Z/T0 Pass. Reste : DE réel avec **bordures**
      (suppression L/R) et le latch mi-ligne (items ci-dessous) pour le DE_start/end exact.
- [ ] **Latch palette/scroll mi-ligne** — la scanline est rendue une fois à DE_END (cycle
      376) ; pas de changement intra-ligne — réf. `video.c:Video_RenderLine`
- [ ] **VBL tiré en fin de trame** (~ligne 312 + offset 64 cyc), pas ligne 201 _(risque élevé)_
      — réf. `video.c:Video_InterruptHandler_VBL` (VBL_VIDEO_CYCLE_OFFSET)
- [ ] **Géométries vidéo** : mono (71 Hz, 501×224) et 60 Hz (263×508) en plus du PAL 313×512
      figé _(risque élevé)_ — réf. `video.h` CYCLES/SCANLINES_PER_LINE/FRAME
- [ ] **Wait states** d'accès YM2149 / mémoire (4 cycles + alignement) et contention bus
      _(précision cycle)_ — réf. `psg.c`, `cycles.c`, MAME `stmmu.cpp::bus_contention`

### Cas concrets — état RÉEL mesuré (le quantum sous la ligne ne les débloque pas seul)
- [x] **« T0 MFP timer »** : **PASSE** déjà (cause = mode Timer B délai manquant, corrigé ;
      cf. CHANGELOG + [[cartridge-diagnostics-state]]) — PAS le quantum cycle. Vérifié après
      le quantum sous la ligne : STE_Test Z = vert + « Pass », retard timer max ≤ 134 cyc.
- [~] **Arkanoid** — DIAGNOSTIQUÉ. La « vrille » (PC → `$26000000` wrap→`$0`) = **corruption
      de la table des vecteurs par aliasing MMU**. Chaîne : (1) le sizing mémoire de TOS 1.02
      **sur-détecte 2 Mo de RAM à 512 Ko** (`phystop`=`$200000` même en `--mem 512k`) ; (2)
      Pexec charge donc Arkanoid haut (~`$1F8000`) ; (3) la boucle de clear TOS `FC4BB6` écrit à
      `$180114` qui, en config $08 (2 Mo déclaré, 512 Ko réel), **alias → phys `$114`** = vecteur
      Timer C, mis à 0 ; (4) le Timer C suivant lit le vecteur 0 → saut `$0` → vrille. **Avec
      `--mem 2m` : Arkanoid charge, AFFICHE son écran-titre, puis se fige sur `$31736/$26E7`**
      (le symptôme documenté d'origine), sur les 2 cœurs — le quantum sous la ligne ne le
      débloque pas. **Vrai bug NeoST = sur-détection mémoire** (Arkanoid 1987 tournait sur un
      512 Ko réel) : `mmuTranslate` alias la zone sur-déclarée de la banque 0 (mmuSz=2M, ramSz=512K)
      au lieu de la rendre « absente », donc le test mémoire TOS (boucle `FC0106`/`FC0672`, seuil
      `$200000`) conclut 2 Mo. Fix à faire avec **Hatari `stMemory.c` comme oracle**. Le gel `$26E7`
      lui-même reste à part (cf. note : peut caler aussi sous Hatari).

---

## Bus / memory map / MMU
- [x] **Joypad/lightpen STE + DIP MegaSTE (`$FF9200-$FF9223`) : valeurs au repos** — ✅ FAIT
      (`Bus::mmioRead8`, port `joy.c`). DIP MegaSTE `$FF9200` haut = `0xBF` (HD 1.44, logique
      inversée), boutons/directions `0xFF`, paddle `0x24` (neutre), lightpen `0x0000`. Boot STE
      byte-identique, MegaSTE inchangé. Reste : émulation réelle des entrées (item Vidéo) + le
      bus error sur accès OCTET de `$FF9200/9220/9222` (mots seuls) _(risque élevé, à part)_.
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
- [x] Bit3 GPIP (blitter busy/idle) en lecture — ✅ déjà câblé (`gpuLine_` via
      `Blitter` → `Mfp::setBlitterLine`, ligne GPU_DONE active bas dans `read8`).
- [x] **Lecture data-register Timer A/B/C/D = compteur VIVANT** — ✅ FAIT (port
      `MFP_ReadTimer_AB/CD`). `Mfp::readTimerData` : en mode délai actif, compteur reconstruit
      `ceil(cycles_MFP_restants / prescaler)` via `Scheduler::cyclesUntil` (= `CycInt_FindCyclesRemaining`) ;
      event-count (A/B) → compteur suivi ; arrêté → recharge. Test *Timing* Pass (2 cœurs),
      boot byte-identique. Reste : edge « stopping data reg entre 1 et 0 » (forcer TxDR).
- [x] **Replanning des timers délai = anti-dérive (PendingCyclesOver)** — ✅ FAIT.
      `Mfp::scheduleTimerAt` ancre la replanification PÉRIODIQUE sur l'échéance servie
      (`Scheduler::firingDue`) + période (et non l'horloge courante) → le dépassement de
      latence d'IRQ est absorbé, pas accumulé ; modulo si retard ≥ une période. Programmation
      fraîche (écriture TxCR/TxDR) inchangée (ancrée sur `liveNow`). Boot/histogramme IRQ
      inchangés ; +1 tic Timer C récupéré sur 80 s. Réf. `mfp.c:MFP_StartTimer_AB/CD`.
- [ ] Timer A event-count ignore la polarité AER GPIP4 et recharge à 0 au lieu de 1 _(risque
      élevé)_ — réf. `mfp.c:MFP_TimerA_Set_Line_Input`
- [ ] Config baud USART UCR/Timer-D non modélisée (backing-store seul) _(faible valeur)_ —
      réf. `rs232.c:RS232_HandleUCR + RS232_SetBaudRateFromTimerD`

## Vidéo / Shifter
- [x] **Rendu fine-scroll / line-width / base-basse STE** — ✅ FAIT. `renderLine` unifié décode
      en tampon d'index puis émet avec offset : **fine-scroll** `$FF8264/65` (décalage 0-15 px à
      gauche + groupe de 16 px lu en plus à droite = modèle prefetch `$FF8265`), **line-offset**
      `$FF820F` (stride ligne = `bpl + lineWidth*2`, aussi dans `videoCounter`), **base-basse**
      `$FF820D` déjà composée dans `videoBase`. Défaut (scroll=0/lw=0) **byte-identique** au boot.
      Validé par test ciblé (10/10) + boot EmuTOS-STE/TOS 1.04 inchangés. Reste (précision cycle) :
      distinction prefetch/no-prefetch fine (bord gauche, dérive compteur +8/ligne), bordures.
- [ ] **Suppression de bordures** (gauche/droite/haut/bas, tricks 50/60 Hz) — base des démos
      _(précision cycle)_ — réf. `video.c` BORDERMASK_*
- [ ] **Spec512** (palette par scanline/cycle, 512 couleurs) _(précision cycle)_ — réf.
      `spec512.c` + `video.c:Video_ColorReg_WriteWord`
- [ ] Quirk miroir d'écriture octet de palette (`$FF824x` .B) _(risque élevé)_ — réf.
      `video.c:Video_ColorReg_WriteWord`
- [x] `$FF820D` en lecture renvoie 0 forcé sur ST (octet bas STE seulement) — ✅ déjà en place
      (`Shifter::read8`), confirmé par test ciblé.
- [ ] **Joypads/paddles/lightpen STE** (`$FF9200-$FF9222`) : directions, boutons, multiplexage,
      entrées analogiques — réf. `joy.c`, MAME
- [ ] **DIP switches MegaSTE** `$FF9200` : bit HD floppy, désactivation DMA sound, logique
      inversée — réf. `ioMemTabSTE.c`

## Blitter
- [ ] Partage de bus (mode non-hog) au cycle près _(précision cycle)_ — réf. `blitter.c`

## FDC WD1772 + DMA disquette
- [x] **Loader d'image `.dim`** — ✅ FAIT (`Fdc::decodeDim`, port `floppies/dim.c:DIM_ReadDisk`).
      Validation d'en-tête fidèle Hatari (ID 'BB', offset 0x03=0 non compressé, 0x0A=0) puis
      retrait des 32 octets → contenu `.st` ; géométrie relue dans le BPB. Détection par contenu.
      Validé : boot de Vroom (`.dim`) **byte-identique** au `.st`. `.dim` compressées (pistes
      utilisées seules) non gérées (rares). Écriture désactivée (en-tête à préserver).
- [ ] **Support STX (Pasti)** — images BAS NIVEAU (pistes/secteurs bruts, IDs, CRC, bits
      faibles/fuzzy, timing) pour les jeux protégés (ex. `disks/Stunt Car Racer.stx`).
      Aujourd'hui DÉTECTÉ et refusé proprement (`Fdc::loadImage`, magic « RSY\0 »). Vrai
      support = **gros chantier** : parser le conteneur Pasti (en-tête RSY, enregistrements
      piste/secteur, données fuzzy/timing) + réécrire le WD1772 au niveau piste (ID fields,
      CRC par secteur, tailles variables, densité, READ ADDRESS réel) — **dépend du FDC
      cycle-exact** (item « Timing réel » ci-dessous). Réf. `floppies/stx.c` (~2100 lignes).
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
- [x] **Paquet souris relatif : axe Y** (`SetYAxisUp/Down 0x0F/0x10`) — ✅ FAIT. `yAxis_`
      (±1, défaut +1 = origine haut) applique son signe au Δy émis et à l'accumulation
      absolue (port `IKBD_SendRelMousePacket` l.1409). Reset → +1.
- [x] **Seuil (`0x0B`) / échelle (`0x0C`) souris** — ✅ FAIT. Seuil = porte d'émission du
      paquet relatif (`|Δ| ≥ seuil`, défaut 1) ; échelle appliquée à l'accumulation absolue
      (`> 1`). Gros Δ drainés en plusieurs paquets. Émission **sur changement de bouton sans
      mouvement** (front bouton) portée aussi → boutons de Vroom remontés. Reset → seuils 1,
      échelle 0. Réf. `ikbd.c:IKBD_Cmd_SetMouseThreshold/SetMouseScale`. Validé moira (bureau
      TOS 1.04 + clic-glissé, curseur suit).
- [x] **`MouseAction 0x07` + `MouseCursorKeycodes 0x0A`** — ✅ FAIT (port `IKBD_SendOnMouseAction`
      + `IKBD_SendCursorMousePacket`). `0x07` : bit2 = boutons remontés comme scancodes touche
      (`0x74`/`0x75`, `|0x80` au relâché, **en plus** du paquet de mode) ; bits 0/1 = report de
      position absolue à l'appui/relâchement (mode ABS). `0x0A` : mode CURSOR — le Δ sort en
      flèches clavier (72/80/75/77) par pas de keycode-delta. `0x12` (DisableMouse → mode OFF)
      câblé au passage.
- [x] **Horloge IKBD (`SetClock 0x1B` / `ReadClock 0x1C`)** — ✅ FAIT (port `IKBD_UpdateClockOnVBL`
      + helpers BCD `Check`/`Adjust`). 6 octets BCD (YY MM DD hh mm ss), avancée d'1 s par
      1e6 µs cumulés au VBL (durée trame ≈ 20032 µs), propagation/retenue + jours/mois +
      bissextile fidèles à la ROM HD6301 ; conservée au reset à chaud. Validé par test ciblé
      (set/read/tic/octet non-BCD).
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
- [~] **Vroom : boutons souris inopérants** (passage des vitesses). L'émission **sur
      changement de bouton sans mouvement** est désormais portée fidèlement
      (`IKBD_SendRelMousePacket`, front bouton via `bOldL_/bOldR_`). À confirmer sur le jeu réel
      (disquette non libre → pas testable au headless ici).
- [~] **Curseur GEM sort de l'écran et ne revient pas** : seuil/échelle/axe Y IKBD désormais
      modélisés (deltas plus propres). Reste à vérifier sur cas réel le bornage + la libération
      Échap côté frontend si le symptôme persiste.

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
