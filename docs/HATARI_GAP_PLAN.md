# Plan d'implémentation — écarts NeoST ↔ Hatari (oracle)

> **Fichier de travail** issu d'une recherche comparée systématique du code de
> `extern/hatari/src` (source de vérité matérielle, cf. `CLAUDE.md`) et de `src/`.
> Chaque item a été vérifié dans le code des DEUX côtés (réfs fichier:fonction/ligne).
> Date : juin 2026, post-commit `fa153b1` (IKBD/ACIA cadencé).
>
> État des docs liés : `docs/SOUND_HATARI_DIFF.md` est **périmé** (l'essentiel est
> corrigé — cf. §5) ; `docs/IKBD_HATARI_DIFF.md` est **antérieur à fa153b1** (Phases
> A/B/C portées — reste §7). `TODO.md` reste la liste maîtresse ; ce fichier la
> complète avec les références exactes et l'ordre d'attaque.

## 0. Synthèse exécutive

NeoST est globalement un port fidèle d'Hatari sur les chemins nominaux (boot, bureau,
jeux courants, diagnostics). Les écarts restants se regroupent autour de **trois
verrous structurels** et d'une **poignée de correctifs S à fort impact** :

| Verrou structurel | Composants bloqués | Réf. |
|---|---|---|
| **Géométrie vidéo verrouillée par trame** (pas de glue « live » ligne à ligne) | restart compteur vidéo, bascule 50/60 mid-frame, Timer B suivant le DE réel, rendu live bordure basse, compteur vidéo pendant bordures | §1.B, §2.5 |
| **Blit instantané synchrone** (pas de modèle temporel blitter) | partage de bus 64/64, HOG, BUSY observable, IRQ GPIP3 datée | §4 |
| **Pipeline audio mono** | stéréo DMA STE, panning LMC gauche/droite | §5.4 |

**Correctifs « S » à plus fort rapport bénéfice/effort (toutes sections confondues)** :

1. Masquage palette ST/STE `0x777`/`0xFFF` + half-step gardé à STE (§1.C1) — détection machine de nombreux jeux.
2. Écriture TxDR d'un timer MFP **en marche** ne doit pas écraser le compteur ni replanifier (§2.1) — replays YM/digidrums.
3. Registres vidéo STE « void » → lire **0x00** (pas 0xFF) (§6.1) — détection vidéo STE.
4. Fenêtre ROM = 1 Mo aliasé (pas `rom.size()`) (§6.2) — sondes ROM/diagnostics.
5. Reload du bruit YM à 250 kHz pour période 0 (§5.1) — bruits aigus une octave trop graves.
6. Passage VR MFP en mode auto doit effacer ISRA/ISRB (§2.2).
7. Master reset 6850 côté ACIA clavier (§7.2).

---

## 1. VIDÉO (`Shifter.cpp`/`Glue.hpp` ↔ `video.c` 6139 l.)

**Conformes vérifiés** (ne pas re-auditer) : glue STF complète (retraits G/D/H/B,
RIGHT_OFF_FULL, STOP_MIDDLE — self-test 19/19), Spec512 pixel-exact, géométries
50/60/71 Hz, Timer B event-count DE+24, HSCROLL/LINEWIDTH/compteur différés STE.

### A. Bordures / overscan

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| A1 | MANQUANT | **Mode 336 px STE (`bSteBorderFlag`)** : `$FF8265>0` puis `$FF8264=0` même VBL (≤40 cyc) → +16 px à gauche, adresse −8 octets | `Video_HorScroll_Write` video.c:5843-5921 ; rendu l.4028-4040 | `Shifter.cpp:1124-1139` gère hwScroll/prefetch mais pas le combo | Obsession (Sync), Just Musix 2, Digiworld 2 | M (TODO ✓) |
| A2 | MANQUANT | **`LEFT_OFF_2_STE`** (retrait gauche court STE, +20 octets, shift −8 px) — exige une table de timings STE (`Preload_Start_*`) distincte des ancres STF | `Video_Update_Glue_State` video.c:2523-2529 ; rendu l.3949-3951 | `updateGlueState` Shifter.cpp:526-695 = chemin STF uniquement | LoSTE (Sync), démos STE plein écran | M |
| A3 | MANQUANT | **Overscan med-res** (`OVERSCAN_MED_RES`, `LEFT_OFF_MED`) + scroll par switch hi/med/lo (13/9/5/1 px) | `Video_Res_WriteByte` video.c:1637-1731 ; rendu l.3932-3979 | masques absents du namespace `glue` (Shifter.cpp:63-104) | No Cooper, Punish Your Machine, Best Part Of The Creation, Closure, HighResMode | L (TODO ✓) |
| A4 | INEXACT | **`EMPTY_LINE` vs `BLANK_LINE`** : deux masques distincts (compteur vidéo incrémenté ou non) | video.c:486,489 ; effets l.1499,1564,4081 | `glue::` n'a ni l'un ni l'autre ; avance d'adresse liée à `nPix>0` | mélanges 50/60 Hz bordures H/B : stride décalé | S |
| A5 | MANQUANT | **Rendu des lignes NO_SYNC/SYNC_HIGH** : détectées (`glueBlankLines_`) mais jamais rendues (lignes à noircir, compteur gelé) | video.c:2696-2718, memset l.4081 | `updateGlueState` calcule, `renderGlueFrame` ignore | Closure, tests Troed/SYNC | S (TODO ✓) |
| A6 | INEXACT | **Rendu live bordure basse** : détection OK (`replayGlue`), rendu live absent | `Video_EndHBL`/`Video_CopyScreenLine` | `renderLine`/`onRender` ne suivent pas | scroller bordure basse du menu Cuddly | M (TODO ✓) |

### B. Timings ligne / IRQ (la moitié dépend du verrou « glue live »)

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| B1 | INEXACT | **Timer B ne suit pas le DE réel** quand une bordure est retirée sur la ligne | video.c:2880-2889 `Video_AddInterruptTimerB` recalé sur chaque changement de DE | `Machine.cpp:216-225` position fixe `timerBPos()` par trame | rasters plein écran synchronisés Timer B | L |
| B2 | INEXACT | **VBL armé au début de trame** au lieu de fin-de-trame+offset (64 STF/68 STE) | video.c:4926-5044 | `Machine.cpp:198-199` `frameStart_+64/68` | cycle absolu décalé d'une trame (diff de traces) | S (TODO ✓) |
| B3 | INEXACT | HBL à 508 (WS1) alors que les ancres glue visent WS3 (512) | `Video_HBL_GetDefaultPos` video.c:3116-3132 | `Machine.cpp:190` `cpl_-4` | ±4 cyc, démos extrêmes | S |
| B4 | MANQUANT | **Jitter HBL/VBL** (motif 0/4/8 cyc) | `HblJitterArray` video.h:162-169 | aucun jitter | démos mesurant la latence d'autovecteur | M (TODO ✓) |
| B5 | MANQUANT | **Wakeup states WS1-4** | video.c:626-680, 936-1007 | constantes figées (un seul WS) | Closure, tests SYNC | L (TODO ✓) |

### C. Registres / compteur

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| C1 | **INCORRECT** | **Palette non masquée par machine** : ST = `&0x777`, STE = `&0xFFF` à l'écriture (relecture masquée) ; et `stColorToArgb` applique le half-step STE même en mode ST | `Video_ColorReg_WriteWord` video.c:5373-5397 | `Shifter.cpp:1142-1147` stocke brut ; `stColorToArgb` Shifter.cpp:110-126 toujours STE | **détection STF/STE par relecture $FF8240** (Place To Be Again et bcp de jeux) ; teintes ST | **S** |
| C2 | MANQUANT | Écriture OCTET d'un registre couleur duplique l'octet sur le mot | video.c:5385-5389 | écrit l'octet seul | démos palette-par-octet (rare) | S (TODO ✓) |
| C3 | MANQUANT | **Restart compteur vidéo ligne 310/260** (`Video_RestartVideoCounter`) | video.c:4608 | volontairement absent (Shifter.cpp:1028-1038) — bloqué par le verrou géométrie | ULM Dark Side of the Spoon | L (TODO ✓) |
| C4 | INEXACT | Lecture compteur vidéo à cheval sur 2 lignes (taille ligne précédente selon bordermask) | `Video_CalculateAddress` video.c:1479-1505 | `videoCounter` Shifter.cpp:981-1039 stride uniforme | marginal | S (TODO ✓) |
| C5 | INEXACT | Compteur vidéo lu PENDANT une ligne à bordure ouverte (offsets LEFT_OFF +26, RIGHT_OFF +44…) | video.c:1499-1564 | Shifter.cpp:1022-1025 sans correction | démos auto-synchro lisant $FF8205/07/09 | M |
| C6 | INEXACT | Bascule 50/60 Hz mid-frame = géométrie globale figée (impossible de mélanger 313/263 lignes) | machine d'état continue | `beginFrame` verrouille (Shifter.cpp:154-157) | verrou structurel central | L (TODO ✓) |

---

## 2. MFP 68901 (`Mfp.cpp` 450 l. ↔ `mfp.c` 3526 l.)

**Conformes vérifiés** : IPR/ISR/IMR/IER (clear-only, `IPR &= IER`), priorités,
chronologie `pendingTime_`, délai 4 cyc, IACK ré-évalué, EOI auto/software, GPIP
fronts/AER, bit4 wire-OR ACIA, GPIP7 moniteur^XSINT, antidatage des timers servis
en retard.

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| 2.1 | **INCORRECT** | **Écriture TxDR timer EN MARCHE** : ne doit PAS toucher le compteur vivant ni replanifier (prise d'effet au rebouclage suivant) ; uniquement si TxCR==0 (arrêté) | `MFP_TimerAData_WriteByte` mfp.c:3354-3359 (idem B/C/D : 3389, 3424, 3478) | `Mfp.cpp:120,124-125` : `*Reload_ = *Counter_ = v; scheduleTimer()` inconditionnel | musiques chip changeant la période à la volée (digidrums Timer A : Xenon 2, Turrican) — saut de phase | **S** |
| 2.2 | MANQUANT | **VR bit3 1→0 (software→auto EOI) doit effacer ISRA/ISRB** | `MFP_VectorReg_WriteByte` mfp.c:3133-3143 | `write8 case 0x17` : `vr = v` sans toucher l'ISR | IRQ basses bloquées à jamais après bascule | S |
| 2.3 | MANQUANT | Arrêt d'un timer délai « entre 1 et 0 » → compteur forcé à TxDR | `MFP_ReadTimer_AB/CD` mfp.c:1566-1620 (TimerIsStopping) | `scheduleTimer` cancel simple | calibrations start/stop/read | M |
| 2.4 | INEXACT | **Timer B event-count : changement AER bit3 EN COURS de trame** ne repositionne pas le tic déjà armé | `MFP_ActiveEdge_WriteByte` mfp.c:2775-2811 | `write8 case 0x03` ne replanifie pas (position lue à beginFrame seulement) | Seven Gates of Jambala (cité Mfp.hpp:80) | M |
| 2.5 | INEXACT | USART : TXEMPTY (canal 10) levé **uniquement en loopback** ; Hatari le lève dès qu'une sortie existe | `RS232_TransferBytesTo` rs232.c:569 | `Mfp.cpp:138-145` gated sur `loopback_` | Treasure Trap, The Deep (écriture série debug) | S |
| 2.6 | MANQUANT | Baud USART (UCR + Timer D) | `RS232_SetBaudRateFromTimerD` (appelé mfp.c:3311, 3474) | UCR = backing-store | débit série réel | M (TODO ✓) |
| 2.7 | MANQUANT | Jitter Lethal Xcess (hack PC==$14d72 : ±2 cyc aléatoires sur Timer) | `MFP_StartTimer_AB` mfp.c:1386-1393 | déterministe | Lethal Xcess bordure haute | S |
| 2.8 | INEXACT | Reset : `GPIP=0` (Hatari) vs `gpip=0xFF` (NeoST, latch de sortie) | mfp.c:523 | `Mfp.cpp:18` | théorique (DDR=0 au reset) | S |

Notes : le « patch Timer D » d'Hatari (mfp.c:3285-3303) est une **optimisation de
perf**, pas de la fidélité — NeoST émule mieux, ne rien faire.

---

## 3. FDC / DISQUETTE (`Fdc.cpp` 1792 l. + `StxImage` ↔ `fdc.c` 7593 l., `stx.c` 2134 l., `floppy.c`)

**Conformes vérifiés** : modèle rotationnel daté complet (index/spin-up/latence/FIFO
DMA/INTRQ datée/arrêt moteur), machines à états type I/II/III/IV `_ST`, status
(WPRT/ForceWPRT), ForceInterrupt idle, priorité lecteur A, write-multi (retour
MOTOR_ON), masquage adresse DMA.

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| 3.1 | MANQUANT | **Persistance overlay d'écriture STX** (fichier `.wd1772` à l'éjection, rechargé à l'insertion) | `STX_WriteDisk`/`STX_LoadSaveFile` via floppy.c:658-737 | `StxImage.hpp:83` overlay mémoire seulement | **sauvegardes de jeux STX perdues au reboot** (high-scores, progression) | M (TODO ✓) |
| 3.2 | MANQUANT | **WRITE TRACK sur STX** (overlay piste, suppression des write-sector antérieurs) | `FDC_WriteTrack_STX` stx.c:2027-2133 | `writeTrackBuffer` Fdc.cpp:643 : `if (IMG_STX) return 0` no-op | protections/formateurs réécrivant une piste | M (TODO ✓) |
| 3.3 | MANQUANT | Format MFM bas niveau (IPF/CTRaw, lib CAPS) | chemin `_MFM` fdc.c:6796+ | absent (dispatch `_ST`/`_STX` seulement) | protections SPS/IPF | L (hors périmètre proche) |
| 3.4 | MANQUANT | Densité HD/ED (`cyclesPerRev/densité`), DIP `$FF9200`, registre `$FF860E` inerte | `FDC_GetFloppyDensity`, stx.c:1441 | Fdc.cpp:388 densité DD en dur | disquettes 1.44 Mo MegaSTE | M (TODO ✓) |
| 3.5 | INEXACT | Deleted DAM (bit a0 write sector) : `dr_` jamais consulté, pas de flag deleted STX | fdc.c:7019-7022 (chemin MFM) | Fdc.cpp:577, 1299 | fidélité RECORD_TYPE après réécriture | S |
| 3.6 | INEXACT | WRITE TRACK sur `.ST` : NeoST formate best-effort, Hatari renvoie LOST_DATA | `FDC_WriteTrack_ST` fdc.c:5477-5491 | Fdc.cpp:640 parse et écrit | écart ASSUMÉ (documenté TODO l.180-182) | — |

---

## 4. BLITTER (`Blitter.cpp` 206 l. ↔ `blitter.c` 1696 l.)

**Conforme vérifié** : tout le **datapath** — 16 LOP, 4 HOP, halftone+smudge,
endmasks 1/2/3, NFSR/FXSR (double réinjection bus_word), incréments signés,
persistance du registre à décalage, écritures 8/16/32 atomiques avant démarrage,
xReset==0/yCount==0 dégénéré, IRQ GPIP3 (gating Mega).

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| 4.1 | **INEXACT** | **Modèle temporel entier absent** : le blit est instantané et synchrone dans `run()`. Manquent : comptage de cycles par accès bus (4 cyc/mot RAM), **partage de bus 64/64 cycles** avec le CPU en mode partagé, **mode HOG** (bit6 : bus monopolisé), **BUSY observable** pendant le blit (relecture de $FF8A3C/compteurs en cours), **redémarrage par écriture du contrôle pendant BUSY**, **IRQ GPIP3 datée** à la vraie fin (NeoST la lève au cycle de l'écriture) | `Blitter_Step`/machine à états + `INTERRUPT_BLITTER` (blitter.c, structure générale ; bus access l.440-446 ; contrôle l.916+) | `Blitter::run()` Blitter.cpp:58-206 boucle complète dans l'écriture MMIO | tout programme qui « travaille pendant le blit », pole BUSY, ou synchronise des rasters sur la durée du blit ; vitesse CPU faussée (blit gratuit) | **L** |
| 4.2 | INEXACT | $FF8A3E/3F : void (0xFF) chez Hatari, routés au blitter chez NeoST | ioMem.c:184, ioMemTabSTE.c:199 | `clear(0xFF8A00,0x40)` couvre tout | négligeable | S |

Esquisse pour 4.1 : transformer `run()` en machine à états résumable pilotée par une
source `Scheduler::BLITTER` ; compter 4 cycles par `readWord`/`writeWord` ; en mode
partagé, exécuter des rafales de 64 cycles bus puis rendre la main 64 cycles au CPU
(via le mécanisme de préemption existant) ; en HOG, enchaîner ; dater le
`setBlitterLine` à la fin réelle. Les registres relisibles doivent refléter l'état
COURANT à chaque pause.

---

## 5. SON (`YM2149.cpp`/`DmaSound.cpp` ↔ `psg.c`/`sound.c`/`dmaSnd.c`)

**`docs/SOUND_HATARI_DIFF.md` est périmé** : moteur 250 kHz + rééchantillonnage
pondéré, table DAC 32³, ET ton/bruit, PWM+HPF, demi-amplitude STE, mixage LMC reg0,
start==end, read8 à l'arrêt, masquages, enveloppes, LFSR — tous portés et vérifiés.

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| 5.1 | INEXACT | **Reload du bruit testé à 125 kHz au lieu de 250 kHz** (le test `Noise_count >= Noise_per` est HORS du garde `Freq_div_2` chez Hatari) + `noisePer_` forcé ≥1 | `YM2149_DoSamples_250` sound.c:1051-1058 | `YM2149.cpp:166-173` test DANS le garde | période 0 : bruit le plus aigu une octave trop grave (hi-hats) | **S** |
| 5.2 | MANQUANT | **Compteur de trame DMA ($FF8909/0B/0D) avancé sur l'horloge d'émulation** (HBL/FIFO), pas dans le thread audio — figé en headless ! | `DmaSnd_FIFO_Refill` dmaSnd.c:361, `DmaSnd_GetFrameCount` l.748-761 | `curAddr_` avance dans `mix()` (DmaSound.cpp:310) | Mental Hangover, Power Up Plus (effets calés sur la position) ; **préalable à toute validation oracle du DMA** | M (TODO ✓) |
| 5.3 | MANQUANT | FIFO 8 octets + filtre anti-repliement (si freq DMA > freq hôte) | dmaSnd.c:342-409, 577-626 | lecture RAM directe, ZOH | aliasing samples 50 kHz ; courses CPU/DMA | M (filtre seul : S) |
| 5.4 | INEXACT | **Pipeline mono** : stéréo DMA moyennée, gains LMC G/D fusionnés | dmaSnd.c:589-676 (chaînes L/R complètes) | `Audio.cpp:120` channels=1 ; DmaSound.cpp:222 `(L+R)/2` | spatialisation STE perdue | L (choix d'archi à trancher) |
| 5.5 | INEXACT | Gains LMC : `pow(10,dB/20)` au lieu des tables mesurées + facteur ×2 (compensation demi-amplitude YM) absent → balance DMA/YM légèrement différente | tables dmaSnd.c:189-207, facteur l.530-531, application l.1152-1153 | `masterGain()` DmaSound.cpp:206-214 | balance relative DMA/YM | S |
| 5.6 | INEXACT | Shelfs LMC : RBJ fc=200/8000 Hz au lieu de 1er ordre fc=118.276/8438.756 Hz | dmaSnd.c:1410-1420 | `shelfCoeffs` DmaSound.cpp:157-194 | timbre graves/aigus si programmés | S |
| 5.7 | INEXACT | Latch de lecture PSG : relecture après write = valeur NON masquée (latch séparé) | psg.c:263-264, 316, 346 | `read8` renvoie `regs_[sel]` masqué | RMW/diagnostics fins (Mindbomb/BBC, Murders In Venice) | S |
| 5.8 | MANQUANT | Port A PSG : strobe Centronics (bit5 → Printer + GPIP0 BUSY), notification eager drive/side | psg.c:367-460 | sink RS232 seulement | imprimante absente | M (TODO ✓) |

---

## 6. BUS / MMU / PLAN MÉMOIRE (`Bus.cpp` 519 l. ↔ `ioMem.c`, `ioMemTab*.c`, `memory.c`, `stMemory.c`)

**Conformes vérifiés** : whitelist ST ($FF8001, palette, $FF8240-5F, $FF8260/61,
void $FF8262-7F), MFP impairs, ACIA/RTC $FFFC00-$FFFDFF, miroirs PSG, RTC
Mega-only, DIP $FF9200=0xBF, MegaST void $FF8000/$FF8002-0D, $FF8E20/22/23 → 0xFF,
MMU/aliasing STF+STE.

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| 6.1 | **INCORRECT** | **Registres STE « void » lisent 0xFF au lieu de 0x00** : $FF820B, $FF8262-63, $FF8266-7F → `IoMem_VoidRead_00` | ioMemTabSTE.c:98,121,124 ; ioMem.c:896 | fallback `0xFF` Bus.cpp:451-453 | détection vidéo STE (TOS/diags) | **S** (TODO ✓) |
| 6.2 | **INCORRECT** | **Fenêtre ROM = 1 Mo aliasé** : lecture au-delà de `rom.size()` ne doit pas fauter (masque d'adresse) ; l'AUTRE fenêtre ROM faute | memory.c:1783-1784, 1036-1040 | Bus.cpp:187, 319 : borne à `rom.size()` | sondes/checksums ROM des diagnostics | **S** (TODO ✓) |
| 6.3 | INCORRECT | **Octets PAIRS SCU $FF8E02-0E doivent fauter** (seuls les impairs sont mappés) | ioMem.c:215-222 | `clear(0xFF8E01,0x0F)` Bus.cpp:302 couvre les pairs | accès byte pair MegaSTE | S |
| 6.4 | INEXACT | Écriture en ROM : bus error chez Hatari, ignorée chez NeoST (`busFault` symétrique R/W) | `ROMmem_wput` memory.c:1043-1062 | Bus.cpp:220, 319 | programmes testant la protection ROM | M |
| 6.5 | INEXACT | RAM non peuplée $0-$3FFFFF : `regs.db` (floating bus) vs 0x00 | `VoidMem_bget` memory.c:913-926 | Bus.cpp:183 | marginal | M (basse) |
| 6.6 | MANQUANT | Cas MMU bank0=128K + bank1=2048K → trou void $40000-$7FFFF | memory.c:1622-1629 | `mmuTranslate` sans ce cas | sizing exotique | S (très basse) |
| 6.7 | MANQUANT | `.STC` : en-tête 4 octets à sauter (taille==0x20004) | cart.c:78-81 | chargement brut | cartouches .STC | S |

---

## 7. ACIA / IKBD / MIDI / RTC / RS232 (post-`fa153b1`)

**Porté en fa153b1** (ne pas refaire) : livraison RX cadencée 10240 cyc/octet, RDR
persistant, DuplicateMouseFireButtons, $14 coupe la souris + quirks fenêtre de reset
($08+$14, $12+$14, $12+$1A), $13/$11 pause, rapports $87-$9A, $16 ports bruts,
position absolue tous modes, fenêtre critique + garde 20 VBL.

| # | Type | Écart | Hatari | NeoST | Impact | Effort |
|---|------|-------|--------|-------|--------|--------|
| 7.1 | **INCORRECT** | **`MidiAcia` n'a PAS reçu les correctifs de l'ACIA clavier** : pas de cause TX (TIE/TDRE), pas de RDR persistant, et `rx_.clear()` au master reset (contraire à `ACIA_MasterReset` qui NE vide PAS les octets en transit) | `MIDI_UpdateIRQ` midi.c:143-177 ; `ACIA_MasterReset` acia.c:669-705 | `MidiAcia.cpp:16-53` modèle pré-fa153b1 | séquenceurs MIDI pilotés par IRQ TX ; item TODO « risque élevé » | M (TODO ✓) |
| 7.2 | MANQUANT | **Master reset 6850 côté clavier** (`(CR&0x03)==0x03`) : SR→TDRE, RDRF/IRQ effacés, SANS purger la file | acia.c:794-797, 669-705 | `Ikbd.cpp` write8 $FFFC00 ne décode pas le cas | loaders type Transbeauce 2 attendant SR==0x02 | S |
| 7.3 | MANQUANT | `Clock_Divider==0` (CR jamais programmé) → octets IKBD ignorés | ikbd.c:1027-1032 | livraison dès le boot | fidélité boot à froid | S |
| 7.4 | MANQUANT | Délai initial des réponses ($FD/$FC/$F6 ≈ 7000-10000 cyc, $F7 = 18000−ACIA_CYCLES) | `IKBD_Delay_Random` ikbd.c:971-974, 2085, 2271 | `pushRx` sans délai initial (cadence seule) | loaders mesurant le délai de réponse | M (fixe, pas random — déterminisme) |
| 7.5 | MANQUANT | Buffer de sortie IKBD borné (1024) + rejet **paquet entier** si plein | `IKBD_OutputBuffer_CheckFreeCount` ikbd.c:945-959 | `rx_` deque non bornée | flood $16 (Downfall/Fokker) ; mémoire non bornée | M |
| 7.6 | INEXACT | `$19` (SetCursorForJoystick) : framing OK (7 octets) mais handler absent (et mislabel « $18 » en commentaire) | `IKBD_Cmd_SetCursorForJoystick` ikbd.c:244 | `cmdLength` OK, pas de case 0x19 | mode joystick→flèches (quasi inutilisé) | M |
| 7.7 | MANQUANT | SR ACIA : OVRN/FE/PE | acia.c:856-881, 1103-1137 | statut TDRE\|RDRF\|IRQ | fidélité pure (TODO ✓, faible) | M |
| 7.8 | — | RTC : NeoST ≥ Hatari (bank0 + alias AM/PM, déterministe). Vérifier seulement la **whitelist bus error $FFFC20-3F pairs/impairs** vs ioMemTab | rtc.c:16-18, 142-148 | `Rtc.cpp` complet | diag Mega | S (vérif) |

---

## 8. Ordre d'attaque proposé

### Phase 1 — « quick wins » S à fort impact — ✅ **FAITE** (validée par campagne complète :
tests unitaires dédiés, 3 boots pixel-identiques, étalons, diags ST/MegaSTE inchangés,
axes Vroom, cœur Moira — cf. commit)
1. ✅ §1.C1 palette ST/STE masquée (`&0x777`/`&0xFFF` à l'écriture, relecture masquée).
2. ✅ §2.1 TxDR timer en marche (effet au rebouclage) + §2.2 VR→auto efface ISRA/ISRB.
3. ✅ §6.1 void STE → 0x00 (+ $FF820D/0F → 0xFF sur ST) + §6.2 fenêtre ROM décodée
   complète + §6.3 octets pairs SCU fautent.
4. ✅ §5.1 bruit rechargé à 250 kHz (période 0 admise). _Validation à l'oreille à faire._
5. ✅ §7.2 master reset 6850 clavier (SR=$02, file non purgée) + §7.3 Clock_Divider
   (octets jetés tant que le CR n'est pas programmé).

### Phase 2 — fidélité jeux/démos (M)
6. §7.1 MidiAcia alignée sur l'ACIA clavier (réutiliser le modèle d'Ikbd).
7. §5.2 compteur DMA sur l'horloge émulation (+ §5.3 filtre anti-repliement).
8. §3.1 persistance `.wd1772` + §3.2 WRITE TRACK STX.
9. §1.A1 `bSteBorderFlag` 336 px + §1.A2 `LEFT_OFF_2_STE` (timings STE).
10. §1.A4/A5 EMPTY/BLANK_LINE + rendu NO_SYNC ; §1.B2 VBL fin-de-trame.
11. §2.4 Timer B/AER mid-frame ; §7.4 délais de réponse IKBD ; §7.5 buffer borné.

### Phase 3 — chantiers structurels (L)
12. **Glue vidéo « live »** (lève le verrou : §1.B1, §1.C3, §1.C5, §1.C6, §1.A6).
13. **Modèle temporel blitter** (§4.1).
14. **Pipeline audio stéréo** (§5.4, décision d'architecture) + tables LMC (§5.5/5.6).
15. Overscan med-res (§1.A3), wakeup states (§1.B5), jitter (§1.B4).

### Hors périmètre proche (consigné, pas planifié)
IPF/CAPS (§3.3), SCI bit-à-bit, baud USART réel, OVRN/FE/PE, floating bus `regs.db`,
Falcon (masque sector count), GEMDOS HD (cartouche interne `cartData.c`).

## 9. Méthode de validation (rappel)

- Chaque item DOIT être validé au `neost-headless` (déterministe) : étalons
  `tools/run_etalons.py`, diags (`--cart` + `--keys`), scripts datés
  (`--mouse-at`/`--joy-script`/`--keys-at`), traces (`--trace`, `NEOST_DEBUG_IKBD`,
  `NEOST_DEBUG_ACIA`), et l'oracle Hatari exécutable (`docs/HATARI_AUTOMATION.md`).
- Tester les DEUX cœurs (`--cpu musashi|moira`) et les 4 machines quand le
  composant en dépend.
- Après chaque lot : boots EmuTOS STE / TOS 1.04 / TOS 2.06 + drag GEM
  pixel-identiques (non-régression de référence).
