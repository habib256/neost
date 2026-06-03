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

## Audit de fidélité Hatari — inventaire complet (lot juin 2026)

Inventaire NeoST ↔ **Hatari** (source de vérité) issu d'un audit composant-par-
composant : **12 sous-systèmes, 71 écarts identifiés**. **14 portés** (lot juin 2026,
branche `feat/hatari-fidelity-port`, chacun validé par le filet de non-régression :
build + boot EmuTOS sur les 2 cœurs + 3 cartouches de diag atteignant leur menu ;
détail dans `CHANGELOG.md`) ; **58 différés**, étiquetés :
`lot suivant` = portable, faible risque · `précision cycle` = nécessite
l'ordonnanceur daté (cf. `docs/CYCLE_ACCURACY.md`) · `risque élevé` = touche le
modèle bus/IRQ déjà éprouvé · `gros contrôleur` = puce entière à écrire ·
`faible valeur`. ✅ = fait, ☐ = à faire.

### Bus / memory map / MMU
- [x] **Banque 1 MMU miroir de la banque 0 sur STE/MegaSTE** (bits 0-1 de `$FF8001` ignorés) — réf. `stMemory.c:STMemory_MMU_ConfToBank` + `Config_IsMachineST`
- [ ] Joypad/lightpen STE + DIP switches MegaSTE (`$FF9200-$FF9223`) whitelistés mais relisent `0xFF` _(différé : lot suivant)_ — réf. `ioMemTabSTE.c` (Joy_StePad*, IoMemTabMegaSTE_DIPSwitches_Read → `0xBF`)
- [ ] Contrôle cache/CPU MegaSTE `$FF8E21` sans registre relisible (lit `0xFF`) _(différé : lot suivant)_ — réf. `ioMem.c:IoMem_FixAccessForMegaSTE` + `ioMemTabSTE.c:IoMemTabMegaSTE_CacheCpuCtrl_WriteByte`
- [ ] Registres vidéo STE « void » doivent lire `0x00` (`$FF820B`, `$FF8262-63`, `$FF8266-7F`) _(différé : faible valeur)_ — réf. `ioMemTabSTE.c` (IoMem_VoidRead_00)
- [ ] Fine-scroll/line-width STE `$FF8264/65/$FF820F` whitelistés mais non gérés au niveau Bus _(différé : lot suivant)_ — réf. `ioMemTabSTE.c` (Video_HorScroll/LineWidth)
- [ ] La banque ROM doit couvrir toute la fenêtre 1 Mo (`$E00000-$EFFFFF`)/192 Ko, pas seulement la taille du fichier _(différé : risque élevé)_ — réf. `cpu/memory.c:memory_map_Standard_RAM` (ROMmem_bank + ROMmem_mask aliasing)
- [ ] Les accès mémoire FDC/son-DMA court-circuitent la traduction MMU (utilisent `ram[]` physique) _(différé : risque élevé)_ — réf. `stMemory.c:STMemory_DMA_Read/Write*` (via STMemory_MMU_Translate_Addr)

### MFP 68901 + RS232 USART
- [x] **Lecture GPIP honore le registre de direction (DDR) + le latch CPU** — réf. `mfp.c:MFP_GPIP_ReadByte_Main`
- [ ] Les écritures AER/DDR/GPIP ne réévaluent pas les IRQ GPIP front-déclenchées _(différé : risque élevé)_ — réf. `mfp.c:MFP_GPIP_Update_Interrupt` + AER/DDR/GPIP_WriteByte
- [ ] Bit3 GPIP (blitter busy/idle) non représenté en lecture _(différé : lot suivant)_ — réf. `mfp.c:MFP_GPIP_ReadByte_Main`
- [ ] Lecture du data-register Timer A/C/D renvoie la valeur de recharge, pas le compteur vivant _(différé : lot suivant)_ — réf. `mfp.c:MFP_ReadTimer_AB/CD`
- [ ] Le replanning des timers délai perd le dépassement (pas de PendingCyclesOver) _(différé : lot suivant)_ — réf. `mfp.c:MFP_StartTimer_AB/CD` + Timer*_Interrupt
- [ ] Timer A event-count ignore la polarité AER GPIP4 et recharge à 0 au lieu de 1 _(différé : lot suivant)_ — réf. `mfp.c:MFP_TimerA_Set_Line_Input`
- [ ] Le chemin de réception loopback n'honore pas RSR Receiver-Enable (bit0) _(différé : faible valeur)_ — réf. `rs232.c:RS232_Update / RS232_RSR_ReadByte`
- [ ] Config baud USART UCR/Timer-D non modélisée (backing-store seul) _(différé : faible valeur)_ — réf. `rs232.c:RS232_HandleUCR + RS232_SetBaudRateFromTimerD`

### Vidéo / Shifter
- [x] **Registres STE (lot) : fine-scroll `$FF8264/65`, line-width `$FF820F`, base-basse `$FF820D`, palette 4 bits/canal, relecture sync `$FF820A`** (état registre ; rendu scroll/line-width réel encore différé) — réf. `video.c` (HorScroll/LineWidth/ScreenBase/Sync) + `conv_st.c:ConvST_SetupRGBTable`
- [ ] Quirk miroir d'écriture octet de palette (`$FF824x` .B) non modélisé _(différé : risque élevé)_ — réf. `video.c:Video_ColorReg_WriteWord` (« special strange case »)
- [ ] `$FF820D` en lecture renvoie l'octet bas sur ST au lieu de 0 forcé _(différé : lot suivant)_ — réf. `video.c:Video_BaseLow_ReadByte`
- [ ] Suppression de bordures (gauche/droite/haut/bas, tricks 50/60 Hz) non émulée _(différé : précision cycle)_ — réf. `video.c` BORDERMASK_*
- [ ] Spec512 (palette par scanline/par cycle, 512 couleurs) non émulé _(différé : précision cycle)_ — réf. `spec512.c` + `video.c:Video_ColorReg_WriteWord`

### Timing machine / ordonnanceur
- [ ] VBL (niv. 4) tiré ligne 201 au lieu de la fin de trame (~ligne 312 + offset 64 cyc) _(différé : risque élevé)_ — réf. `video.c:Video_InterruptHandler_VBL/HBL` (VBL_VIDEO_CYCLE_OFFSET)
- [ ] Géométrie figée PAL 313×512 ; mono (71 Hz, 501×224) et 60 Hz (263×508) non modélisés _(différé : risque élevé)_ — réf. `video.h` CYCLES/SCANLINES_PER_LINE/FRAME
- [ ] Timer B/HBL plafonnés à 200 lignes visibles même en 400 lignes (mono) _(différé : lot suivant)_ — réf. `video.c:Video_InterruptHandler_HBL`
- [ ] Le quantum CPU = tout l'écart entre événements, pas le cycle bus → latence IRQ grossière _(différé : précision cycle)_ — réf. `cycInt.c` + `m68000.c` + `cycles.c`
- [ ] Timer B event-count figé au cycle 400 ; devrait suivre Display-Enable (DE_start/end+24) par ligne et 50/60 Hz _(différé : précision cycle)_ — réf. `video.c:Video_TimerB_GetPosFromDE`
- [ ] La scanline est rendue une fois à DE_END (cycle 376) ; pas de latch palette/scroll mi-ligne _(différé : précision cycle)_ — réf. `video.c` (shifter par cycle, Video_RenderLine)

### Blitter
- [x] **Interruption de fin sur MFP GPIP3 (canal I3 / GPU_DONE)** — réf. `blitter.c:Blitter_Start` (MFP_GPIP_LINE_GPU_DONE) + `mfp.c:MFP_INT_GPIP3`
- [x] **Sortie anticipée `y_count==0` efface BUSY + HOG** — réf. `blitter.c:Blitter_Control_WriteByte` (efface bits 0x80|0x40)

### FDC WD1772 + DMA disquette
- [x] **Détection de changement de média (Mediach) à l'éjection/insertion à chaud (bascule WPRT)** — réf. `floppy.c:Floppy_DriveTransition*` + `fdc.c:FDC_DiskControllerStatus_ReadWord` (ForceWPRT)
- [x] **Write-protect auto-détecté depuis les droits du fichier image** — réf. `floppy.c:Floppy_IsWriteProtected`
- [x] **Largeur d'impulsion INDEX 3.71 ms + moteur off à 9 tours** — réf. `fdc.c:FDC_DELAY_US_INDEX_PULSE_LENGTH / FDC_DELAY_IP_MOTOR_OFF`
- [ ] Loader d'image `.dim` (en-tête 32 o + charge `.st`) _(différé : lot suivant)_ — réf. `floppies/dim.c:DIM_ReadDisk`
- [ ] Registre densité `$FF860E` exposé sur tout modèle ; devrait être void sur STE, réel seulement MegaSTE/TT/Falcon _(différé : faible valeur)_ — réf. `ioMemTabSTE.c` + `fdc.c:FDC_CanMachineHandleDensity`
- [ ] Masquage d'adresse DMA absent (octet haut `&0x3f`, octet bas word-align `&0xfe`) _(différé : faible valeur)_ — réf. `fdc.c:FDC_WriteDMAAddress`
- [ ] Le compteur de secteurs DMA n'est pas relisible sur le vrai HW (renvoie le dernier `$FF8604`) _(différé : risque élevé)_ — réf. `fdc.c:FDC_DiskControllerStatus_ReadWord`
- [ ] Un accès octet à `$FF8604/06` devrait fauter sur un ST non-Falcon _(différé : risque élevé)_ — réf. `fdc.c` (M68000_BusError sur accès octet)
- [ ] Durée BUSY réelle par commande + INTRQ différé / sémantique FIFO DRQ _(différé : précision cycle)_ — réf. `fdc.c` (FDC_DELAY_*, FIFO, FDC_UpdateAll)

### YM2149 PSG
- [x] **Fréquence de pas d'enveloppe corrigée (diviseur 16× trop lent)** — réf. `sound.c:YM2149_DoSamples_250` + YM2149_EnvPer
- [x] **Table de volume 5 bits mesurée (32 niveaux) au lieu de 16 approx.** — réf. `sound.c` ymout1c5bit[32] + YmVolume4to5[16]
- [ ] Données port B Centronics + front strobe (bit5) non émulés en sortie _(différé : faible valeur)_ — réf. `psg.c:PSG_Set_DataRegister` (Printer_TransferByteTo)
- [ ] Filtre passe-bas RC de sortie (STF) non appliqué _(différé : faible valeur)_ — réf. `sound.c:LowPassFilter`
- [ ] Masquage à l'écriture manquant + sélecteur de registre ≥ 16 mal géré _(différé : faible valeur)_ — réf. `psg.c:PSG_Set_SelectRegister/DataRegister`

### Son DMA STE + Microwire/LMC1992
- [x] **Ligne XSINT → GPIP7 (STE : GPIP7 = moniteur XOR XSINT)** — réf. `dmaSnd.c:DmaSnd_Update_XSINT_Line` + `mfp.c:MFP_Main_Compute_GPIP7`
- [ ] Timer A event-count ignore le front AER et tire à 0 au lieu d'atteindre 1 _(différé : risque élevé)_ — réf. `mfp.c:MFP_TimerA_Set_Line_Input`
- [ ] Cas limites start==end (stop/loop sans IRQ) non gérés _(différé : faible valeur)_ — réf. `dmaSnd.c:DmaSnd_StartNewFrame`
- [ ] Décodage commande LMC1992 : collecte tous les bits masqués au lieu d'un run de masque contigu _(différé : faible valeur)_ — réf. `dmaSnd.c:DmaSnd_InterruptHandler_Microwire`
- [ ] Registre masque Microwire `$FF8924` non tourné pendant le shift en relecture _(différé : faible valeur)_ — réf. `dmaSnd.c` (rotation du masque par pas)
- [ ] Registre mixage LMC1992 (reg 0) décodé mais jamais appliqué au mix (mute/route YM) _(différé : faible valeur)_ — réf. `dmaSnd.c:DmaSnd_GenerateSamples`

### IKBD HD6301 + joystick/souris
- [x] **Analyseur de commandes IKBD multi-octets (table de longueurs)** — réf. `ikbd.c` KeyboardCommands[] + IKBD_RunKeyboardCommand
- [x] **Mode souris absolu (AbsMouseMode 0x09, ReadAbsMousePos 0x0D, SetInternalMousePos 0x0E)** — réf. `ikbd.c:IKBD_Cmd_AbsMouseMode/ReadAbsMousePos/SetInternalMousePos`
- [x] **Joystick auto-report (0x14), stop (0x15), monitoring (0x17), fire-duration (0x18)** — réf. `ikbd.c:IKBD_Cmd_ReturnJoystickAuto/StopJoystick/SetJoystickMonitoring`
- [ ] Le paquet souris relatif ignore la direction d'axe Y (SetYAxisUp/Down 0x0F/0x10) _(différé : lot suivant)_ — réf. `ikbd.c:IKBD_SendRelMousePacket`
- [ ] Pas de seuil (0x0B) ni d'échelle (0x0C) souris — deltas relatifs bruts _(différé : lot suivant)_ — réf. `ikbd.c:IKBD_Cmd_SetMouseThreshold/SetMouseScale`
- [ ] Pas de MouseAction 0x07 / report bouton-en-touche / MouseCursorKeycodes 0x0A _(différé : lot suivant)_ — réf. `ikbd.c:IKBD_Cmd_MouseAction/SendOnMouseAction`
- [ ] Pas d'horloge IKBD (SetClock 0x1B, ReadClock 0x1C) _(différé : lot suivant)_ — réf. `ikbd.c:IKBD_Cmd_SetClock/ReadClock`
- [ ] Keymap international / layouts TOS non modélisés _(différé : faible valeur)_ — réf. `keymap.c` + ikbd.c (scancodes)

### ACIA 6850 (clavier + MIDI)
- [x] **Lignes d'IRQ ACIA clavier + MIDI en OU câblé sur GPIP4 (plus d'écrasement mutuel)** — réf. `mfp.c:MFP_Main_Compute_GPIP_LINE_ACIA`
- [ ] IRQ émetteur (CR bits 5/6) et état TDRE non modélisés ; TDRE câblé à 1 _(différé : risque élevé)_ — réf. `acia.c:ACIA_UpdateIRQ` + `midi.c:MIDI_UpdateIRQ`
- [ ] La lecture data-register renvoie 0x00 si FIFO vide au lieu du dernier RDR _(différé : faible valeur)_ — réf. `acia.c:ACIA_Read_RDR` + `midi.c:Midi_Data_ReadByte`
- [ ] SR n'expose jamais overrun (0x20)/framing (0x10)/parity (0x40) _(différé : faible valeur)_ — réf. `acia.c` (ACIA_SR_BIT_*)
- [ ] SR MIDI recalculé à chaque lecture au lieu d'un état effacé au master-reset _(différé : faible valeur)_ — réf. `midi.c:Midi_Reset/Control_WriteByte`

### RTC RP5C15
- [ ] Aliasing BANK=1 AM/PM de `$FFFC25/27` non modélisé (chemin TOS 1.0x) _(différé : faible valeur)_ — réf. `rtc.c:Rtc_Minutes*_Read/WriteByte` (rtc_bank)
- [ ] Registres année (`$FFFC37/39`) en année calendaire 2 chiffres au lieu de l'offset GEMDOS Hatari _(différé : faible valeur)_ — réf. `rtc.c:Rtc_Init` (year_offset=80)

### CPU : IRQ, bus error, Moira, MegaSTE
- [ ] Registre cache/vitesse MegaSTE `$FF8E21` sans handler (lit 0xFF, écritures perdues) _(différé : lot suivant)_ — réf. `ioMemTabSTE.c` + `m68000.c:MegaSTE_CPU_Cache_*`
- [ ] Masques d'IRQ SCU MegaSTE (`$FF8E01/$FF8E0D`) non modélisés ; IRQ atteignent l'IPL non filtrées _(différé : risque élevé)_ — réf. `scu_vme.c` (SCU_*) + `m68000.c:M68000_SetIRQ`
- [ ] Pas de chemin IRQ niveau 5 (SCC) / pas de puce SCC sur MegaSTE _(différé : gros contrôleur)_ — réf. `m68000.c:M68000_Update_intlev` + `scc.c` (Z85C30)
- [ ] Bascule CPU 8/16 MHz MegaSTE (`$FF8E21` bit1) ne change pas le débit de cycles/timings _(différé : précision cycle)_ — réf. `m68000.c:MegaSTE_CPU_Cache_Update` + `clocks_timings.c`

> Le détail texte (current/correct/impl) de chaque écart reste disponible dans la trace
> de l'audit ; les sections ci-dessous gardent la feuille de route thématique d'origine.

## Cartouches de diagnostic (`carts/`) — campagne en cours

Objectif : faire passer SANS erreur les cartouches de test (`ST_Diagnostic_v4.4`,
`STE_Test_v1.9`, `MegaSTE_Diagnostic_v1.5`). Elles s'exécutent au reset (magic
`$FA52235F`, saut `$FA0004`) et impriment leur rapport sur le **port série**
(`./build/neost-headless rom/etos192us.img --machine <m> --cart carts/<f> --frames 2500`).

**ST_Diagnostic_v4.4 BOOTE JUSQU'À SON MENU INTERACTIF** (« Mega and ST Field
Service Diagnostic Rev. 4.4 … 520k RAM Keyboard rev. 2 … Enter letter(s) »),
self-tests de démarrage RÉUSSIS (bus error + clavier), à l'écran ET sur série, sur
les DEUX cœurs (Musashi + Moira). 5 corrections (cf. mémoire cartridge-diagnostics) :

1. **Bus error = whitelist Hatari** (`Bus::buildIoFault`/`busFault`/`busFaultN`,
   port de `ioMem.c`+`ioMemTabST/STE.c`+`cpu/memory.c`).
2. **Double bus fault → halt CPU** (Musashi `m68k_pulse_halt`, Moira `flags|=HALTED`)
   au lieu de segfault hôte.
3. **⭐ Trame bus error 68000 dans Musashi** (`extern/Musashi/m68kcpu.h`,
   `m68ki_exception_bus_error`) : Musashi empilait la trame 68010 (format-8, 58 o)
   au lieu de la trame 68000 (14 o, `m68ki_stack_frame_buserr`) → les handlers de
   bus error des diags font `adda #8 ; rte` (trame 14 o) → revenaient sur PC
   corrompue → vrille vers `$0`. C'est LE déblocage principal (menu atteint).
4. **Readback base écran `$FF8201/03/0D`** (`Shifter::read8`, Hatari
   IoMem_ReadWithoutInterception) : les diags relisent ces registres pour calculer
   leur framebuffer ; sans readback → base 0 → dessinaient sur la table des vecteurs.
5. **Reset IKBD différé** (`$F1` ~502000 cycles après `$80,$01`, via `Scheduler::IKBD`,
   cf. Hatari IKBD_RESET_CYCLES) : répondre instantanément levait l'IRQ ACIA avant
   que le diag l'arme → perdue → « K1 Keyboard not responding » (corrigé → « rev. 2 »).

6. **Registre sync `$FF820A` relisible** (`Shifter`, défaut $02 = 50 Hz PAL, cohérent
   313 lignes) — les diags lisaient 60 Hz et faussaient leurs mesures de fréquence.
+ Outil : option headless **`--keys "..."`** = injection clavier (pilote les menus :
  `O`=ROM, `Z`=tests internes, `Q`=tout). Les tests s'affichent à l'ÉCRAN (pas série).

« I7 Bus error not detected » = résultat CORRECT (écriture en `$0` = RAM, pas de faute).

7. **Shift série Microwire `$FF8922`** (`DmaSound::onMicrowireShift` + `Scheduler::MICROWIRE`,
   port Hatari) : 16 décalages (8 cyc), `$FF8922` → 0, puis décode LMC1992. **A débloqué
   STE_Test ET MegaSTE_Diagnostic** (qui pollaient `$FF8922` jusqu'à 0).
8. **Adresse fautive de trame bus error** (`m68ki_aerr_address/_write_mode/_fc`) → diags
   affichent la VRAIE adresse (« Bus Error Access Address: $E00000… », sondage ROM).
9. **Blitter** (`Blitter.cpp`, port fonctionnel Hatari, mode HOG) : HOP/LOP/FXSR/NFSR/
   skew/smudge/halftone/endmasks. Présent Mega ST/STE/Mega STE. **Tests BLiT court+long
   PASSENT** ; EmuTOS STE VDI OK ; « No Blitter installed » disparu.

**LES 3 CARTOUCHES ATTEIGNENT LEUR MENU INTERACTIF** (vrai TOS par machine, 2 cœurs) :
ST_Diagnostic, STE_Test, MegaSTE_Diagnostic.

**ST_Diagnostic batterie Z : 7/8 PASSENT** ; **STE_Test : ROM OK + BLiT OK**. Avec EmuTOS
le ROM donne « cs error » (checksums ≠ TOS Atari → utiliser un vrai TOS).

10. **Adresse DMA disquette relisible** (`$FF8609/0B/0D`, `Fdc::read8`) — le compteur
    incrémente pendant le transfert (cf. Hatari `FDC_GetDMAAddress`). **A corrigé le
    « F8 DMA count error »** de STE_Test (NeoST renvoyait `$FF`). + WRITE/READ TRACK
    ($F0/$E0) consomment la DMA.

11. **Compteur vidéo `$FF8205/07/09` cycle-exact** (`Shifter::videoCounter`, port de
    Hatari `Video_CalculateAddress` : 2 cyc/octet, LineStart 56@50Hz). Avant : 1 octet/
    cycle dès le cycle 216 (faux mid-ligne). UN composant de T0 corrigé. Non régressif.

**Reste à faire (vers « zéro erreur ») :**
- **T0 MFP timer** — SEUL bug NeoST restant qui affecte les 3 cartouches. Mécanisme
  identifié au cycle près : le test met `$284=3`, attend dans une boucle de délai, et un
  handler MFP partagé (`FA13FE`, sur Timer A/B/C) efface les bits de `$284` (`bclr`).
  Pour PASSER, les bons timers doivent fire et effacer les 2 bits DANS la fenêtre →
  **précision cycle-exact du timing IRQ vs cycles CPU** (NeoST fire aux cycles planifiés
  mais le CPU avance par instruction = dépassement). + compteur vidéo lu au cycle près
  (composant corrigé). → chantier « Précision temporelle » : resserrer le quantum sous
  la ligne, valider par diff de trace Hatari. **C'est LE blocage architectural.**
- **Drive B / Hard disk** (PAS un bug NeoST) : « Cannot write drive B », « Hard error »
  = périphériques ABSENTS. Un vrai ST minimal donnerait les mêmes. Pour « zéro erreur »
  de la batterie complète il faudrait ATTACHER un 2ᵉ lecteur + un disque dur.
- `STE_Test_v1.9` : écran bleu sans texte. `MegaSTE_Diagnostic` : « VME/FPU not found » puis stop.
- **256K : PAS un bug NeoST** (écarté). Le diag FIGE `conf=$05` (512+512) au démarrage
  sans jamais l'adapter ; à 256K (banques 128K) ça sur-déclare → aliasing `$8`↔`$208`,
  comportement CORRECT (formules = Hatari, vérifié). 512K/1M/2M/4M atteignent tous le
  menu → `mmuTranslate` fidèle. Le diagnostic ne supporte simplement pas 256K (un vrai
  260ST ferait pareil). Rien à corriger côté NeoST.

## Chantier courant — MegaST & vérité Hatari (`extern/hatari/src`)

Source de référence désormais en sous-module : **`extern/hatari/src`** (focus
machine : **MegaST**). Méthode : traces CPU Hatari headless ↔ NeoST + diff
resynchronisant (`SDL_VIDEODRIVER=dummy hatari --trace cpu_disasm --run-vbls N`,
machine + RAM appariées).

**Déjà corrigé (vérité Hatari, cœur Musashi par défaut)** :
- [x] Bus error sur MMIO non décodé `$FF8002-$FF81FF` (dont `$FF8006`) ✓ — sonde
      matérielle EmuTOS au boot (FC007C, vecteur bus error armé juste avant).
- [x] Trou RAM `$80000-$3FFFFF` renvoie `0x00` (et non `0xFF`) ✓ — détection
      mémoire (512K → 4M détectés ; boot ST 512K validé screenshot).
- [x] Dump registres du Tracer **core-aware** ✓ : `--cpu moira --regs` affiche
      enfin les vrais registres Moira (`Tracer` câblé sur `Cpu68k::reg()/sr()`,
      avant il lisait Musashi non initialisé → garbage).
- [x] **Reconfiguration à chaud** ✓ : modèle / RAM / cœur / ROM changeables depuis
      le menu **sans relancer l'appli** (`Machine::reconfigure` + `Cpu68k::setCore`,
      bascule de cœur Moira↔Musashi à chaud) — applique un hard reset avec les
      nouveaux paramètres. Boutons **Reset** + **Hard Reset** (barre + menu).
      Pratique pour basculer ST↔MegaST↔cœur pendant la comparaison Hatari.

**À faire (priorisé, avec réf. Hatari)** :
- [x] ⚠ **Bus error `$FF80xx` gaté par modèle** ✓ — `Bus::busFault()` : sur
      Mega ST/STE (`machineIsMega`), `$FF8002-$FF800D` est **void** (pas de bus
      error) au lieu de fauter comme sur ST. Validé vérité Hatari : `tst.w $FF8006`
      faute en `st`, pas en `megast` (réf. `IoMem_FixVoidAccessForMegaST`).
- [ ] **RTC RP5C15 `$FFFC21` pour la détection Mega ST** — c'est le levier
      restant pour qu'EmuTOS affiche « Mega ST » (le bus map gaté ne suffit pas, le
      _MCH reste 0=ST). Trace Hatari megast : `tst.b $FFFC21` (sonde, FC0638), puis
      écriture/relecture `$FFFC25`/`$FFFC27` (validation, FC2410+), absente en ST.
      → ajouter une RP5C15 minimale gatée Mega (`rtc.c`, MAME `rp5c15`). NeoST
      renvoie encore `0xFF` sur `$FFFC21` (no-fault) sur tout modèle → la validation
      échoue → label « Atari ST ».
- [ ] **Décodage banques MMU `$FF8001`** (détection mémoire exacte). Hatari
      `stMemory.c` : `STMemory_MMU_ConfToBank` (bits 2-3 banque0, 0-1 banque1 ;
      00=128K / 01=512K / 10=2M), `STMemory_MMU_Size`, et l'aliasing/miroir via
      `STMemory_MMU_Translate_Addr_STF` / `_STE` + `memory_map_Standard_RAM`.
      NeoST mappe via `ram.size()` seul → la boucle de scan EmuTOS (FC0220-FC0274)
      ne matche pas Hatari instr-pour-instr (sans impact fonctionnel actuel).
- [ ] **Bus error réel `$400000-$F9FFFF`** (au-dessus de 4 Mo ST-RAM, sous
      cartouche/ROM) : NeoST renvoie encore `0xFF`, doit fauter. Hatari
      `cpu/memory.c:memory_map_Standard_RAM` (`BusErrMem_bank` à `0x400000`),
      `ioMem.c:IoMem_SetBusErrorRegion`. (Note : un accès **DMA** à une zone
      bus-error renvoie `0x0000`, pas d'exception.)
- [ ] **Détection ST / STE / MegaST / MegaSTE par EmuTOS** : registres
      présents/absents selon modèle. Hatari `ioMem.c:IoMem_Init` (table
      `IoMemTable_ST` vs `_STE`) + gating : son DMA `$FF8900` (STE+), Microwire
      `$FF8924`, joypads `$FF9200` (STE+), **RTC `$FFFC21`** (Mega+),
      cache/vitesse `$FF8E21` + SCU `$FF8E0x` (MegaSTE). NeoST n'expose pas encore
      ces différences → EmuTOS affiche « Atari ST » même en `--machine ste/megast`
      (confirmé pré-existant, non lié aux fixes bus).
- [ ] **Moira n'honore pas `busFault`** : `NeostMoira::read8/16` (`Cpu68k.cpp`)
      appellent `g_bus` sans tester `busFault` → **aucun bus error sous Moira**
      (les sondes blitter / son DMA / `$FF8006` ne fautent pas). Pas de hook propre
      côté Moira : il faut throw `moira::BusError(makeFrame…)`, interne/fragile
      (`extern/moira/Moira/MoiraExceptions_cpp.h`).

**Souris / entrées (jeux)** :
- [ ] **Vroom : boutons souris inopérants en jeu** (passage des vitesses). Piste :
      en mode relatif l'IKBD ne renvoie l'état des boutons **qu'avec un mouvement**.
      Hatari `ikbd.c:IKBD_SendRelMousePacket` (en-tête `0xF8`, bit1 = bouton G,
      bit0 = D) envoie le paquet **au changement de bouton OU au mouvement** ;
      `IKBD_SendOnMouseAction` + cmd `0x07` (`MouseAction`) gèrent le bouton-seul.
      Vérifier que NeoST (`src/io/Ikbd.cpp`, `main.cpp:onMouseButton`) émet bien un
      paquet sur changement de bouton sans mouvement ; sinon Vroom lit peut-être le
      feu via le **port joystick** (ACIA `$FFFC02` ou pad STE `$FF9200`).
- [ ] **Curseur GEM sort de l'écran et ne revient plus** : capture relative
      (`GLFW_CURSOR_DISABLED`, `main.cpp:390-399`). Sans **seuil / échelle / axe Y /
      bornage** IKBD, le curseur GEM peut filer en coin et rester bloqué. Réf.
      Hatari `ikbd.c` (`SetMouseThreshold`, `SetMouseScale`, `SetYAxis*`, bornage
      de position) + mode **absolu**. Vérifier signe/accumulation des deltas et la
      libération de capture (Échap).

## Socle MegaSTE / types de machine

- [~] **Profils machine réels** ST / Mega ST / STE / **MegaSTE** : socle posé
      (`MachineType`, `Bus::machine`, `Machine(.., MachineType)`), sélectionnable
      avant le boot (GUI menu « Modèle », WASM `?machine=`, headless `--machine`,
      `neost.cfg`). Premier gating : **son DMA STE** présent sur STE/Mega STE,
      bus error sur ST/Mega ST (comme le vrai matériel). Restent à câbler au type :
      **blitter** (Mega ST/STE, émulation à venir), **RTC**, SCC, SCU, mémoire,
      ROM attendue, et le reste des registres présents/absents selon le modèle.
- [ ] **ROM TOS MegaSTE** : chargement fiable des TOS 2.05/2.06 256 Ko à
      `$E00000`, choix pays, vérification des checksums, fallback EmuTOS MegaSTE.
- [~] **ST-RAM 256 Ko / 512 Ko / 1 / 2 / 4 Mo** : taille choisie avant le boot
      (`Machine(ramBytes,…)`, sélecteur GUI « Mémoire », WASM `?mem=`, headless
      `--mem`, `neost.cfg`) ; `$FF8001` posé en cohérence (`memConfigForBytes`).
      EmuTOS détecte la bonne `phystop` par sondage (validé en headless : 512 Ko→4 Mo
      exacts). Restent : **remapping réel des banques** MMU (alias 128/512/2048 Ko),
      zones non peuplées en **bus error** fidèles, et > 4 Mo (TT-RAM, hors ST-RAM).
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
      Moira (divergence fonctionnelle à diffé­rencier de Musashi). Tracer regs
      sous Moira **corrigé** ✓ (cf. chantier courant). **Reste** : Moira n'honore
      pas `busFault` (aucun bus error). Cf. `docs/CYCLE_ACCURACY.md` §5bis.
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
- [~] **Chaînage matériel complet** : I3 blitter ✓, I4 ACIA (OU clavier+MIDI) ✓,
      I5 FDC ✓, I7 DMA-sound XSINT ✓ (lot juin 2026) ; restent I0 busy imprimante,
      I1 DCD, I2 CTS, I6 RI (cf. MAME `machine_start`, `psg_pa_w`, `dmasound_set_state`).
- [ ] **Timer B lié au Display Enable** au cycle près, pas seulement par ligne,
      car les overscans et démos STE/MegaSTE s'en servent.

## IKBD HD6301 (`ikbd.c`, 3250 lignes)

NeoST gère désormais un **analyseur de commandes multi-octets** complet (table de
longueurs, cf. inventaire ci-dessus), avec **souris relative + absolue** et
**joystick auto-report/monitoring** ; le reset (réponse 0xF1 différée) est préservé.
Restent du jeu de commandes Hatari (`IKBD_Cmd_*`) :

- [~] Souris : **mode absolu** ✓ (`AbsMouseMode`, `ReadAbsMousePos`,
      `SetInternalMousePos`) ; restent **seuil**/**échelle** (`SetMouseThreshold`,
      `SetMouseScale`), **axe Y haut/bas** (`SetYAxisUp/Down`),
      **mode keycode** (`MouseCursorKeycodes`), `TurnMouseOff`, `MouseAction`.
- [~] **Joystick** : auto-report / monitoring / stop / durée-de-feu ✓
      (`ReturnJoystick*`, `SetJoystickMonitoring`) ; reste le curseur-joystick.
- [ ] **Horloge IKBD** (`SetClock` / `IKBD_Cmd_ReadClock`) — date/heure.
- [ ] Pause/resume transfert, exécution de programme custom 6301 (rare).
- [ ] **Mapping clavier international** et layout TOS (`keymap.c`, MAME `stkbd`) :
      touches spéciales FR/UK/DE, autorepeat, reset clavier, scan codes exacts.

## Vidéo / Shifter (`video.c`, 6100 lignes)

NeoST décode un framebuffer fixe par trame. Hatari fait du raster cycle-précis :

- [ ] **Suppression de bordures** (gauche/droite/haut/bas) par bascule 50/60 Hz
      et résolution — base de la plupart des démos (`BORDERMASK_*`).
- [~] **Sync 50/60 Hz** ($FF820A) : relecture (bits 2-7 forcés à 1) ✓ ; restent les
      écrans « courts/longs » (171 lignes, etc.) — précision cycle.
- [ ] **Spec512** : changement de palette par scanline → 512 couleurs.
- [ ] **Hardware scrolling** STE ($FF8264/8265, fine scroll + line width $FF820F)
      et tricks STF (plane shifting, scroll 4 px).
- [ ] **Compteur d'adresse vidéo** lisible ($FF8205/07/09) — utilisé par les jeux.
- [~] Palette STE **4 bits/canal** ($FF824x) ✓ (expansion STe, lot juin 2026).
- [~] **Base vidéo basse STE** `$FF820D` et **line width** `$FF820F` : registres ✓
      (relisibles, gatés STE) ; reste le câblage du stride/scroll dans le rendu.
- [~] **Registres fine scroll** `$FF8264/$FF8265` : état registre ✓ ; reste le
      décalage par pixel (préfetch/no-prefetch) au cycle près.
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
- [x] **Write protect** (✓ refus d'écriture + bit WPRT, **auto-détecté** depuis les
      droits du fichier image) + **détection de changement de média** ✓ (Mediach via
      bascule WPRT à l'éjection/insertion à chaud — monter/éjecter sans reset).
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

- [~] YM2149 : **enveloppe** ✓ (registres 11-13, 8 formes utiles via Continue/
      Attack/Alternate/Hold ; voies en mode enveloppe par bit 4 de R8/9/10),
      **table de volume** ✓ (désormais **5 bits mesurée Hatari, 32 niveaux**) +
      **vitesse d'enveloppe corrigée** ✓. Restent : **port B** (Centronics) et
      **filtrage** RC de sortie.
- [~] **DMA sound STE** ($FF8900+, `DmaSound`) ✓ : lecture d'échantillons 8 bits
      signés en RAM, fréquence sélectionnable (6.25/12.5/25/50 kHz), mono/stéréo
      (downmix), play/repeat, compteur d'adresse exposé, mixage avec le YM2149
      (GUI `Audio::render` + WASM `neost_audio_render`). **Interruption de fin de
      trame** ✓ : datée sur l'ordonnanceur (thread émulation, `Scheduler::DMASND`)
      → pulse l'entrée TAI du MFP (`Mfp::timerA_eventCount`) ; en event-count
      (TACR=0x08) lève l'IRQ Timer A — débloque le double-buffering streamé STE.
      Restent : décodage du son sur l'horloge d'émulation + anneau vers le thread
      audio (aujourd'hui la forme d'onde est générée côté audio, seul l'instant
      d'interruption est émulé-exact) et la micro-précision du rééchantillonnage.
- [x] **LMC1992 / Microwire** ($FF8922/24, `DmaSound`) ✓ : décodage de la commande
      série (mot 11 bits %10 + reg + donnée), **volume maître + gauche/droite**
      (gain au mix) et **basses/aigus** ±12 dB (deux filtres en plateau RBJ
      appliqués au mix YM2149 + DMA, bypass à 0 dB). 0 dB partout par défaut →
      aucun effet/coût hors programmation. Restent (mineur) : registre **mixage**
      (reg 0) et le timing de shift bit-à-bit (on décode le mot d'un coup).
- [~] **IRQ/GPIP liés au DMA sound** : **ligne XSINT → GPIP7** ✓ (moniteur XOR XSINT,
      gaté STE — lot juin 2026) ; restent repeat/loop et cas limites start==end.
- [x] **Bruits du lecteur de disquette** ✓ (confort/immersion : ronron moteur,
      « clac » de pas de tête, bruit de seek).

      *Source* — Ce n'est **pas** du matériel : ni Hatari ni MAME n'émulent les
      bruits **mécaniques** du lecteur. C'est **STeem SSE** qui a popularisé la
      fonctionnalité, à partir d'**échantillons WAV** déclenchés sur les événements
      du FDC. Les WAV de STeem SSE sont embarqués dans `rom/drivesound/`
      (`basic/` générique + `epson_smd480l/` = vrai lecteur, échantillonné par
      Stefan jL) : `drive_spin` (boucle moteur), `drive_startup` (mise en route),
      `drive_seek` (rafale de pas), `drive_click` (un pas).

      *Implémentation* — Le cœur reste sans dépendance audio : `Fdc` SIGNALE des
      événements `FdcSound` (`MotorOn`/`Step`/`Seek`) via `setSoundSink(fn)`, émis
      dans `executeCommand()` (toute commande → `MotorOn` ; type I → `Step` si un
      seul pas, `Seek` si plusieurs). Le frontend JOUE :
        - GUI : `src/audio/DriveSound.cpp` (miniaudio `ma_engine`) — boucle moteur
          + one-shots, arrêt du moteur après inactivité ; bascule « Son lecteur ».
        - WASM : `src/web/main_web.cpp` → `window.neostDriveSound(code)` dans
          `web/shell.html` (Web Audio `AudioBufferSourceNode`), WAV préchargés via
          `--preload-file` ; bouton « Son lecteur : on/off ».
      Modèle moteur/index **dans le cœur** ✓ : impulsion d'index datée 1/tour
      (~200 ms à 300 tr/min, `Scheduler::FDC_INDEX`) → événements `Index` (léger
      tic) ; le moteur s'arrête après `MOTOR_OFF_REVS` tours d'inactivité →
      événement `MotorOff` (plus de minuterie côté frontend). Côté GUI, **mixage
      avec le YM2149** ✓ : un seul périphérique miniaudio (`Audio::render`) somme
      le PSG et la sortie « sans périphérique » de `DriveSound`.
      Bit **INDEX** du statut WD1772 ✓ : actif au passage du trou d'index
      (~1.46 ms, 1/tour) sur les lectures de statut type I (`Fdc::read8`, phase
      calculée depuis `indexRef_`). **Son du PSG côté WASM** ✓ : export
      `neost_audio_render` + `ScriptProcessorNode` dans `web/shell.html`,
      partageant l'AudioContext de DriveSound → le navigateur mixe PSG + lecteur.
      Reste : spin-up réel (délai ~6 tours avant exécution) — volontairement non
      modélisé, incompatible avec le modèle « DMA instantané » accéléré.

## Blitter, SCU & stockage

- [~] **Blitter** ($FF8A00, `blitter.c`) — émulé (halftone RAM, HOP/LOP, end masks,
      skew, FXSR/NFSR, smudge, hog) sur Mega ST / STE / Mega STE ; **IRQ MFP I3** ✓
      (lot juin 2026). Reste : partage de bus (non-hog) au cycle près.
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
