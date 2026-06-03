# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. Ce qui est **implémenté et validé**. Pas encore de versions
taguées (0.1.x). Le restant est dans [`TODO.md`](TODO.md).

## Cœur & boot
- `Bus` (memory map ST) + wrapper `Cpu68k` (Musashi) + `Shifter` (vidéo).
- Lib `neost_core` sans dépendance GUI ; frontends `neost` (fenêtré) et `neost-headless`.
- Boot 68000 : overlay ROM en `$0-$7` (SSP/PC), refermé après reset. TOS auto-détecté
  (192 Ko → `$FC0000`, sinon `$E00000`).
- **Cœur CPU sélectionnable** (`--cpu musashi|moira`, `neost.cfg`, WASM `?cpu=`).
  Moira (cycle-exact, sous-module) boote EmuTOS pixel-identique et délivre les IRQ.
- **Reconfiguration à chaud** : modèle / RAM / cœur / ROM changeables depuis le menu
  sans relancer (`Machine::reconfigure`, hard reset avec les nouveaux paramètres).
- **Ordonnanceur d'événements daté** (`Scheduler`, idée `cycInt.c`) : la trame est
  pilotée par les échéances (vidéo, timers MFP, FDC, son DMA…) à horloge CONTINUE.
- **Quantum CPU « sous la ligne »** (port du modèle Hatari `cycInt` + cœur cycle-exact
  Moira). Deux mécanismes :
  - **Horloge live** (`Scheduler::liveNow` = `Cycles_GetClockCounterImmediate`) : un
    timer programmé en plein bloc CPU est daté à l'instant RÉEL de l'écriture (et non
    au début du quantum), précis à la sous-instruction sous Moira.
  - **Préemption du timeslice** (`Cpu68k::endTimeslice`) : quand une écriture matérielle
    arme un événement plus proche que la cible du bloc, le CPU rend la main à la
    frontière d'instruction suivante (`m68k_end_timeslice` / drapeau Moira) et la boucle
    re-planifie. Latence d'IRQ timer ramenée de **~47 000 cyc** (un timer court armé
    juste avant un `STOP` était sauté par l'optimisation STOP) à **~130 cyc** (1 instr),
    sans changer le boot (EmuTOS/TOS pixel-identiques, histogramme d'IRQ inchangé).
    Métrique exposée par le headless (`timer IRQ retard max` / `préemptions`).

## Types de machine & mémoire
- **Profils** ST / Mega ST / STE / Mega STE (`MachineType`), choisis avant le boot
  (menu GUI, WASM `?machine=`, `--machine`). Matériel optionnel **gaté au modèle** :
  son DMA `$FF8900` et joypad `$FF9200` STE+, RTC Mega+, etc.
- **ST-RAM 256 Ko → 4 Mo** configurable (`--mem`, menu, WASM `?mem=`) ; `$FF8001` posé
  en cohérence. EmuTOS détecte la `phystop` exacte par sondage.

## Vidéo (Shifter)
- Décodage planaire basse (320×200/16c), moyenne (640×200/4c), haute (640×400 mono) →
  texture OpenGL, conversion `$0RGB` → ARGB. Haute rés forcée blanc/noir.
- Détection moniteur via **GPIP bit7** (couleur basse rés / mono haute rés).
- Base écran relisible (`$FF8201/03`, octet bas STE `$FF820D`) — les diagnostics y lisent
  leur framebuffer. Registre sync `$FF820A` relisible (défaut $02 = 50 Hz PAL).
- **Compteur d'adresse vidéo cycle-exact** (`$FF8205/07/09`, port `Video_CalculateAddress`
  Hatari : 2 cycles/octet, LineStart 56@50Hz).
- **Registres STE** (gatés STE) : fine scroll `$FF8264/65`, line width `$FF820F`, base
  basse `$FF820D`, palette 4 bits/canal, relecture sync.
- **Rendu STE câblé** : `renderLine` décode en tampon d'index puis émet avec offset →
  **fine-scroll** horizontal 0-15 px (décalage gauche + groupe de 16 px lu en plus à droite,
  modèle prefetch `$FF8265`), **line-offset** `$FF820F` (stride ligne `bpl + lineWidth*2`,
  aussi dans le compteur `$FF8205/07/09`), **base-basse** `$FF820D` composée dans `videoBase`.
  Défaut (scroll 0 / line-width 0) byte-identique au boot. *(Distinction prefetch/no-prefetch
  fine + bordures → cycle-accuracy, cf. TODO.)*

## Interruptions (MFP 68901)
- IER/IPR/IMR/ISR + registre vecteur, modes auto et software-EOI.
- **`M68K_EMULATE_INT_ACK`** activé dans Musashi (sans ça, IRQ auto-vectorisées, vecteurs
  MFP inutilisés).
- **Timer C 200 Hz** (tic système), **Timer B event-count** (Display Enable, lignes
  visibles ; nécessaire à TOS 1.x), **Timer B mode délai** (TBCR 1-7, daté sur
  l'ordonnanceur — corrige « T0 MFP timer »). VBL niveau 4 auto-vectorisé, latché.
- **Position du tic Timer B dérivée du Display-Enable** (port `Video_TimerB_GetDefaultPos`) :
  compte les **fins** de ligne (`DE_end+24`) ou les **débuts** (`DE_start+24`) selon l'AER
  bit3 du MFP (jeux/démos type *Seven Gates of Jambala*), positions selon résolution (71 Hz)
  et fréquence (50 Hz = 400, 60 Hz = 396) — au lieu du cycle 400 figé. Défaut 50 Hz/fin
  inchangé (boot pixel-identique).
- **Timers A/C/D mode délai** datés par le MFP (`Scheduler`). Backing-store timer/USART.
- **Replanification périodique anti-dérive** (port `PendingCyclesOver`) : un timer en mode
  délai se relance ancré sur l'**échéance servie** (`Scheduler::firingDue`) + période, et non
  sur l'horloge courante → le dépassement dû à la latence d'IRQ est absorbé, pas accumulé.
  Sans dérive sur les longues durées (timers musique haute fréquence) ; boot et histogramme
  d'IRQ inchangés (correction sous-trame).
- **Lecture du compteur vivant** des registres de données Timer A/B/C/D (`$FFFA1F/21/23/25`,
  port `MFP_ReadTimer_AB/CD`) : en mode délai actif on reconstruit le compteur décompté
  (`ceil(cycles_MFP_restants / prescaler)` via `Scheduler::cyclesUntil`) au lieu de renvoyer
  la valeur de recharge — indispensable aux boucles de délai qui pollent le compteur. Test
  *Timing* (STE Field Service Diag) Pass sur les deux cœurs, boot byte-identique.
- **Lecture GPIP** honore le registre de direction (DDR) et le latch CPU.
- **IRQ GPIP front-déclenchées réévaluées à l'écriture AER** (`$FFFA03`, port
  `MFP_GPIP_Update_Interrupt`) : état = GPIP ^ AER ; basculer le front actif (AER) alors
  qu'une ligne d'entrée est déjà au niveau correspondant lève le canal — même sans
  transition de la ligne (cas réel des démos « M »/« Realtime » : `bset/bclr #0,$FFFA03`).
  Gaté IER comme `MFP_InputOnChannel`. Boot + histogramme d'IRQ inchangés (TOS n'arme pas
  de front actif au boot), 2 cœurs.
- Chaînage des lignes : **I3** blitter, **I4** ACIA (clavier+MIDI en OU câblé), **I5** FDC,
  **I7** son DMA XSINT (moniteur XOR XSINT).

## Clavier, souris, joystick (ACIA 6850 / IKBD HD6301)
- ACIA clavier + file de scancodes ; mapping GLFW → scancodes ST. Ligne **GPIP4** câblée
  sur RDRF de l'ACIA. **Réponse de reset IKBD différée** (`$F1` ~502000 cyc après `$80,$01`).
- **Analyseur de commandes multi-octets** (table de longueurs + buffer d'accumulation).
- Souris **relative** (paquets `$F8`|boutons + Δx/Δy) **et absolue** (`$09`/`$0D`/`$0E`).
  Port fidèle de `IKBD_SendRelMousePacket` : **seuil d'émission** (`$0B`), **échelle** absolue
  (`$0C`), **signe d'axe Y** (`$0F`/`$10`), drain des gros Δ en plusieurs paquets, et émission
  **sur changement de bouton SANS mouvement** (détection de front — boutons de jeu type Vroom).
  Défauts de reset (REL, seuils 1, axe Y haut) remis sur `$80,$01`.
- **MouseAction `$07`** (`IKBD_SendOnMouseAction`) : boutons remontés comme scancodes touche
  (`$74`/`$75`, bit2) et/ou position absolue reportée à l'appui/relâchement (bits 0/1) ;
  **mode curseur-clavier `$0A`** (`IKBD_SendCursorMousePacket`) : Δ souris converti en flèches
  (72/80/75/77) ; **DisableMouse `$12`** (mode OFF).
- **Horloge interne IKBD `$1B`/`$1C`** (`IKBD_UpdateClockOnVBL`) : 6 octets BCD avancés d'une
  seconde par trame cumulée, propagation/retenue + bissextile fidèles à la ROM HD6301.
- **Joystick** : auto-report (`$14`), stop (`$15`), monitoring (`$17`), durée de feu (`$18`) ;
  interrogation `$16` → `$FD,joy0,joy1`.

## Disquette (FDC WD1772 + DMA)
- Accès indirect via DMA (`$FF8600`) ; Restore/Seek/Step/Read/Write/ReadAddress + WRITE/READ
  TRACK ; modèle « DMA instantané ». Sélection face/lecteur via PSG port A. INTRQ → **GPIP5**.
- **Adresse DMA relisible** (`$FF8609/0B/0D`, incrémente pendant le transfert — corrige
  « DMA count error »). **Lecteur B** (`--diskb`, PSG port A bits 1/2).
- **Write-protect auto-détecté** depuis les droits du fichier ; **changement de média**
  (Mediach via bascule WPRT à l'éjection/insertion à chaud).
- **Bit INDEX** WD1772 reflété (trou ~1.46 ms, 1/tour) ; impulsion d'index datée 1/tour
  (~200 ms, `Scheduler::FDC_INDEX`), moteur off après ~9-10 tours.
- Formats : `.st` (brut), `.msa` (décompression RLE), `.dim` (en-tête 32 o retiré, port
  `floppies/dim.c` : ID 'BB', non compressé). Détection par CONTENU (indépendante de
  l'extension). Écritures recopiées dans le `.st` ; `.msa`/`.dim` protégées en écriture.
  Les images `.stx` (Pasti, bas niveau) sont DÉTECTÉES (magic « RSY\0 ») et refusées
  proprement — incompatibles avec le modèle FDC logique actuel (cf. TODO).

## Audio
- **YM2149** : 3 voies carrées + bruit, enveloppe (R11-13, formes via Continue/Attack/
  Alternate/Hold), **table de volume 5 bits mesurée** (32 niveaux), vitesse d'enveloppe
  corrigée (diviseur de pas). Backend miniaudio (CoreAudio).
- **Son DMA STE** (`DmaSound`, `$FF8900-$FF8925`) : échantillons 8 bits signés en RAM
  (6.25/12.5/25/50 kHz, mono/stéréo, play/repeat, compteur d'adresse), mixé au YM2149.
  **Interruption de fin de trame** datée (`Scheduler::DMASND`) → entrée TAI du MFP → IRQ
  Timer A event-count (double-buffering streamé STE).
- **LMC1992 / Microwire** (`$FF8922/24`) : décodage commande série 11 bits, volume
  maître + G/D (gain), basses/aigus ±12 dB (filtres RBJ). **Shift série** `$FF8922`
  (16 décalages de 8 cyc, `Scheduler::MICROWIRE` — les diags qui pollent jusqu'à 0 OK).
- **Bruits mécaniques du lecteur** (immersion, pas du matériel — repris de STeem SSE) :
  le cœur émet des événements `FdcSound` (moteur/pas/seek/index) via un sink ; frontends
  GUI (`DriveSound`, miniaudio) et WASM (Web Audio). WAV embarqués dans `rom/drivesound/`.
- **Son PSG en WASM** : export `neost_audio_render` tiré par un `ScriptProcessorNode`.

## Bus error & cartouches de diagnostic
- **Modèle bus error = port fidèle Hatari** (`ioMem.c`+`ioMemTabST/STE.c`+`cpu/memory.c`) :
  tout `$FF8000-$FFFFFF` faute par défaut, whitelist des registres câblés par modèle
  (`Bus::buildIoFault`, carte octet par octet) + zones void + fixups ST/MegaST/MegaSTE.
  Hors IO : `$400000-$F9FFFF` et `$FF0000-$FF7FFF` fautent. Règle word/long : faute
  seulement si TOUS les octets fautent (`busFaultN`). Suivi par les DEUX cœurs.
- **Double bus fault → halt CPU** (Musashi `m68k_pulse_halt`, Moira `flags|=HALTED`) au
  lieu de segfault hôte → le headless peut vider trace + série.
- **Trame de bus error 68000 dans Musashi** (`m68kcpu.h`) : empilait la trame 68010
  (format-8, 58 o) au lieu de la trame 68000 (14 o) → les handlers `adda #8 ; rte` des
  diags revenaient sur PC corrompue. **Le déblocage principal.** Adresse fautive
  (`m68ki_aerr_*`) renseignée → diags affichent la vraie adresse.
- **Blitter** (`Blitter.cpp`, port fonctionnel Hatari, mode HOG) : HOP, LOP 16 ops,
  FXSR/NFSR, skew, smudge, halftone, endmasks, comptes X/Y, incréments signés. Présent
  Mega ST/STE/Mega STE, absent STF. **IRQ de fin sur GPIP3**, BUSY+HOG effacés à `y_count==0`.
- **RTC RP5C15** (Mega ST/Mega STE, `$FFFC21-$FFFC3F`) : modèle paresseux déterministe
  (cycle CPU du dernier top de seconde + rattrapage), registre RESET, débordement BCD
  calendaire. Corrige « C0 No clock installed » + « C1 clock increment error ».
- **MIDI** (`MidiAcia`, `$FFFC04/06`) : bouclage OUT→IN + IRQ canal 6.
- **Port série RS-232 / USART MFP** : RSR/UDR, IRQ RxFull (12)/TxEmpty (10)/RxErr (11)/
  TxErr (9), lignes RTS→CTS (GPIP2)/DTR→DCD (GPIP1)/RI (GPIP6) via PSG port A.
- **Disque dur ACSI** (`Fdc`, `$FF8604/06` bit `DMA_CSACSI`, port `hdc.c`) : commande
  6 octets, READ/WRITE(6), INQUIRY, READ CAPACITY, TEST UNIT READY ; disque virtuel
  agrandi à la demande (64 Mo).
- **PSG `$FF8802` relisible** (read-modify-write `bclr/bset` du port A).
- **Registre Cache/CPU MegaSTE `$FF8E21` relisible** (port `IoMemTabMegaSTE_CacheCpuCtrl_WriteByte`) :
  octet latché (bit0 = cache, bit1 = vitesse 8/16 MHz) avec la contrainte matérielle « cache
  impossible à 8 MHz » (bit0 forcé à 0 si bit1=0). Reset = 0. L'EFFET (débit cycles, cache 16 Ko)
  reste un item « précision cycle ». Boot MegaSTE byte-identique.
- **Lectures STE joypad/paddle/lightpen + DIP MegaSTE** (`$FF9200-$FF9223`, port `joy.c`) :
  valeurs au repos au lieu d'un `0xFF` générique — DIP MegaSTE `$FF9200` octet haut = `0xBF`
  (lecteur HD 1.44 Mo, logique inversée), boutons/directions `0xFF` relâchés, paddle au neutre
  `0x24`, lightpen `0x0000`. Boot STE byte-identique, MegaSTE (EmuTOS/diag) inchangé.
- **Bus map gaté par modèle** : sur Mega ST/STE `$FF8002-$FF800D` est void (pas de faute)
  contrairement au ST (`IoMem_FixVoidAccessForMegaST`).

**Résultat** : les **3 cartouches** (`ST_Diagnostic`, `STE_Test`, `MegaSTE_Diagnostic`)
atteignent leur menu et passent leur batterie de tests internes (Z) **sans erreur**, sur
les **2 cœurs**, avec un vrai TOS. Restes (« Hard error »/VME/FPU) = périphériques absents,
fidèles à Hatari, pas des bugs.

## Frontend & outillage
- Écran ST dans une fenêtre ImGui ; visualiseur hexa + registres 68000 ; boutons Reset /
  Hard Reset ; barre résolution. Bridage **50 fps réels**. Persistance (`neost.cfg`).
- **`neost-headless`** : trace d'instructions façon MAME, registres, IRQ (`--irq`), capture
  PPM, injection clavier (`--keys`) / souris (`--walk-mouse`), bouclage (`--loopback`).
  C'est l'outil de débogage principal.
- **`tools/trace_diff.py`** : aligne une trace NeoST et une trace Hatari sur un PC commun
  (`--align-pc`) et localise la première divergence (flux PC + registres).

## Validé
- EmuTOS (FR/US) : green desktop, fichiers disquette, double-clic, fenêtres.
- TOS 1.02 Mega ST FR : boot complet, green desktop basse rés.
- **Arkanoid** (Imagine 1987) : se lance via l'AUTO de la disquette, écran-titre.
