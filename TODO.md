# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.** Le fait (implémenté + validé) est dans
[`CHANGELOG.md`](CHANGELOG.md) ; les diagnostics de bugs en cours sont en mémoire projet.

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

**Validation par logiciels réels** : chaque gros chantier a un **logiciel étalon** qui
n'affiche/sonne correctement QUE si l'effet limite est fidèle → catalogue par sous-système
dans [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md). Repère `🎯 étalon : …` sur les items
ci-dessous. Ordre de débogage affichage : **Spectrum 512 ✅ → Cuddly Demos (menu statique :
flicker RÉSOLU ✅ ; reste : scrolling qui saute quand le robot bouge, + scroller de bordure
basse) → Enchanted Land (plante après le LOADING)**.

---

## 🎯 Le grand chantier : précision cycle

> Plan détaillé : **[`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)** (ordonnanceur
> d'événements datés, vidéo/MFP/FDC au cycle, validation par `trace_diff` ↔ Hatari).
> **Inventaire exhaustif du travail restant** (diff Hatari ↔ NeoST par sous-système,
> trié par priorité) : **[`docs/CYCLE_EXACT_INVENTORY.md`](docs/CYCLE_EXACT_INVENTORY.md)**.
>
> **Phases 1-6 faites** (cf. CHANGELOG : `Scheduler`, `runFrame` événementiel à horloge
> continue, vidéo au cycle, timers MFP A/B/C/D datés, géométries 50/60/71 Hz, quantum
> « sous la ligne »). Reste :

- [x] **Latch palette mi-ligne** — FAIT pour la PALETTE (port `spec512.c`) : écritures
      `$FF824x` datées au cycle live de Moira, re-rendu de fin de trame à palette roulante
      (cf. CHANGELOG §Vidéo + item **Spec512** ci-dessous). Reste le **scroll** fin mi-ligne
      (sync-scroll Enchanted Land) et la latence sous-pixel — réf. `video.c:Video_RenderLine`.
- [~] **Wait states** d'accès YM2149 / mémoire (4 cycles + alignement) et contention bus
      _(précision cycle)_ — réf. `psg.c`, `cycles.c`, MAME `stmmu.cpp::bus_contention`.
      **FAIT pour l'alignement bus 4 cyc du SHIFTER, désormais EN LIVE** (port `M68000_SyncCpuBus`) :
      les registres couleur ($FF8240-5F), résolution ($FF8260) et scroll fin ($FF8264/65) s'accèdent
      sur une frontière de 4 cycles → un accès non aligné gèle le CPU jusqu'à la frontière (0-3 cyc),
      ce qui décale les suivants. Appliqué LIVE (`Shifter::syncCpuBus` → `Cpu68k::addBusWaitCycles`,
      le cœur Moira avance son horloge) ; l'ancien recalage hors-ligne `applyShifterBusAlignment`
      est devenu redondant (no-op). **Spectrum 512 reste pixel-identique** (byte-identique avant/après).
      **FAIT aussi pour les périphériques 8 bits PSG / MFP / ACIA** (port `psg.c` / `mfp.c` /
      `acia.c`) : PSG = 4 cyc / 1ᵉʳ accès d'instruction ; MFP = 4 cyc / accès ; ACIA = 6 cyc +
      synchro E-Clock (`M68000_WaitEClock`, 1ᵉʳ accès). Injectés par le `Bus` via
      `Cpu68k::add{Psg,Mfp,Acia}WaitCycles` (Moira ; Musashi no-op). Non-régression : glue
      self-test 19/19, Spec512 stable, boot propre. Cf. CHANGELOG. Effet attendu : le timing
      absolu CPU↔vidéo se décale → la démo `make_overscan_lr.py` a été re-calibrée (PAD1 20→12).
      **Reste** : **contention DMA vidéo générale** sur la RAM (shifter volant des cycles bus au
      CPU pendant l'affichage actif). ⚠ **Hatari NE la modélise PAS** (seulement l'alignement
      registre shifter + l'arbitrage blitter) — c'est un modèle **MAME** (`stmmu.cpp::bus_contention`).
      L'ajouter ferait DIVERGER NeoST de l'oracle Hatari (qui valide Spec512/bordures au pixel) ;
      non portable depuis la source de vérité → laissé de côté (ne concerne que la fidélité
      matériel réel que Hatari lui-même n'atteint pas).

### Cas concrets — état RÉEL mesuré
- [~] **Arkanoid** — écran-titre RÉSOLU, **mais le jeu ne démarre jamais** (retour utilisateur :
      on atteint le titre, jamais la partie). Le gel `$31736` de l'écran-titre est corrigé par le
      **modèle FDC rotationnel** (spin-up 6 tours + débit MFM 256 cyc/octet + latence rotationnelle,
      port `_ST` Hatari ; Musashi + Moira, comme Hatari sans `--fastfdc`), cf. CHANGELOG §Disquette,
      [[arkanoid-freeze-investigation]]. **Reste à investiguer** : le passage titre → jeu (appui
      bouton/touche ? second chargement disque ? protection ? IRQ ?) — à diff'er contre l'oracle
      Hatari (`--keys`/`--joy`, trace IRQ). 🎯 étalon de la suite FDC/protection.
- [ ] **Enchanted Land** (Thalion 1990, `disks/Enchanted Land (1990)(Thalion).st`) — affiche
      **LOADING puis PLANTE** (retour utilisateur). À investiguer : bus error / instruction
      illégale (trace headless `--irq`, comparer la divergence à l'oracle Hatari), ou protection/
      loader FDC. Étalon sync-scroll prévu, mais bloqué bien avant l'effet pour l'instant.

---

## Bus / memory map / MMU
- [ ] Registres vidéo STE « void » doivent lire `0x00` (`$FF820B`, `$FF8262-63`, `$FF8266-7F`)
      _(faible valeur)_ — réf. `ioMemTabSTE.c` (IoMem_VoidRead_00)
- [ ] La banque ROM doit couvrir toute la fenêtre 1 Mo (`$E00000-$EFFFFF`), pas la taille du
      fichier _(risque élevé)_ — réf. `cpu/memory.c:memory_map_Standard_RAM` (ROMmem aliasing)
- [ ] Accès mémoire FDC/son-DMA via la traduction MMU au lieu de `ram[]` physique _(risque
      élevé)_ — réf. `stMemory.c:STMemory_DMA_Read/Write*`
- [ ] **Remapping réel des banques MMU** (alias 128/512/2048 Ko) + bus error fidèle des zones
      non peuplées (`$400000-$F9FFFF` au-dessus de la RAM) — réf. `stMemory.c` + `cpu/memory.c`

## MFP 68901 + RS232 USART
- [ ] Config baud USART UCR/Timer-D non modélisée (backing-store seul) _(faible valeur)_ —
      réf. `rs232.c:RS232_HandleUCR + RS232_SetBaudRateFromTimerD`

## Vidéo / Shifter
- [ ] **Bug labels bureau EmuTOS 192 FR/US** : textes des icônes/menus mal affichés
      (captures `etos192fr`/`etos192us`) — comparer rendu Shifter/VDI avec Hatari.
- [ ] **Green desktop / GEM couleur corrompu** : bureau vert avec menus/fenêtres/icônes
      parasités (capture `Pirates!`) — comparer palette, plans bitplanes, masques VDI et
      rendu Shifter avec Hatari.
- [~] **Bordures** (gauche/droite/haut/bas, tricks 50/60 Hz) — base des démos _(précision cycle)_.
      **Phase 1 FAITE** : overscan VISIBLE (buffer 416×276, actif centré, fenêtre GUI ajustée).
      **Timeline alignée sur VDE_On FAITE** : affichage actif à la scanline 63 (50 Hz)/34, HBL à
      CHAQUE ligne, `videoCounter`/spec512 décalés (zéro régression vérifiée). **MACHINE GLUE
      complète FAITE** (port fidèle STF de `Video_Update_Glue_State`+`Video_StartHBL`+section
      verticale, rejouée hors-ligne : `replayGlue`/`updateGlueState`/`startHBL`/`renderGlueFrame`
      dans `Shifter`) → **retrait HAUT et BAS validés au pixel contre l'oracle Hatari** (tests
      overscan faits-main `tools/make_overscan_test.py`, `--trace video_border_v`), zéro régression.
      **Gauche/droite VALIDÉES** : auto-test déterministe `neost-headless --glue-selftest`
      (`Shifter::glueSelfTest`, 19/19 vs valeurs Hatari : LEFT_OFF DE_start=4, RIGHT_OFF DE_end=462,
      RIGHT_MINUS_2, STOP_MIDDLE, top/bottom, normal) + test 68k end-to-end `make_overscan_lr.py`
      (oracle Hatari ouvre L/D). **`DisplayPixelShift` appliqué au rendu** (décalage 4 px du left-off).
      **VDE_On LIVE du compteur vidéo FAIT** (retrait bordure HAUTE en live, `liveStartHBL_`/
      `updateLiveStartHBL`, port `nStartHBL`) : une bascule 60 Hz dans la bordure haute fait monter
      `$FF8209` dès la ligne 34 → **le menu fullscreen de The Cuddly Demos ne flicke plus** (sa
      boucle d'auto-synchro se verrouille au faisceau comme sur le vrai matériel ; conforme au menu
      briques d'Hatari). Cf. CHANGELOG. **Reste** (raffinements, faible priorité) : (1) wakeup-state
      WS3 (+1 cyc, sous-pixel) ; (2) med-res overscan ; (3) rendu des blank lines / NO_SYNC ;
      (4) pixel-perfect L/D end-to-end (dépend des wait states / contention bus, cf. item ci-dessus) ;
      (5) **menu Cuddly — scrolling qui SAUTE** quand le robot se déplace (gros sauts d'images au
      lieu du défilement fluide attendu — à diff'er à l'oracle : mécanisme de scroll = base vidéo
      $FF8201/03 ? scroll fin STE $FF8264/65 ? re-sync par position ?) ; (6) **scroller de la
      bordure BASSE du menu Cuddly** non rendu (retrait bas non modélisé dans le rendu live).
      🎯 étalons : `make_overscan_test.py` (haut/bas ✅), `make_overscan_lr.py` (gauche/droite ✅),
      **The Cuddly Demos** (menu statique : flicker ✅ ; scrolling + scroller bas à faire),
      **Enchanted Land** (sync-scroll — plante après LOADING, cf. ci-dessous).
- [x] **Spec512** (palette par scanline/cycle, 512 couleurs) — **RÉSOLU 2026, 100 % PIXEL-
      IDENTIQUE À HATARI** (port `spec512.c`). Re-rendu à palette roulante datée + **4 correctifs
      décisifs** validés au pixel (0 px de diff) contre l'oracle Hatari : (1) **alignement bus
      4 cyc du shifter** rejoué hors-ligne (`applyShifterBusAlignment`, port `M68000_SyncCpuBus`)
      — sans ça la boucle d'écriture (24× `move.l (a3)+,(ax)+` + `dbra` = 510 cyc/ligne sous
      Moira pur) dérivait de −2 cyc/ligne ; (2) **offset d'alignement pixel↔couleur**
      `kSpec512AlignCyc = −23` (port du « +7 spans » de `Spec512_StartScanLine` − le décalage de
      datation de Moira) ; (3) **fusion octet→mot** de `recordColorWrite` (un `move.w` = 1
      écriture, comme Hatari) ; (4) **datation de la LECTURE du compteur vidéo `$FF820x`**
      `kVideoCounterReadOffsetCyc = −2` (port `Video_CalculateAddress`) — élimine le **flicker
      25 Hz** des images statiques (le compteur tombait sur une frontière de cellule-mot, la
      synchro des démos basculait ±4 cyc une trame sur deux). Slideshow
      `disks/utils/spectrum_512_auto_diapo.st` : les **4 images (BEE512, sun, PLANET, cougar)
      diffent à 0 px vs Hatari** (diapo auto sous TOS 1.00). Outils : `--shot-every N PREFIX`,
      `--screenshot`, `NEOST_SPEC512_TRACE`, `NEOST_VC_OFF`/`NEOST_ALIGN_OFF` (sweep oracle),
      `NEOST_DISASM`, `tools/spec512_flicker_check.sh`, `tools/hatari_oracle.sh`. Reste : scroll
      fin sync mi-ligne (Enchanted Land), indépendant. Réf.
      `Shifter::finishFrame/applyShifterBusAlignment/recordColorWrite/videoCounter`.
- [ ] Quirk miroir d'écriture octet de palette (`$FF824x` .B) _(risque élevé)_ — réf.
      `video.c:Video_ColorReg_WriteWord`
- [ ] **Joypads/paddles/lightpen STE** (`$FF9200-$FF9222`) : directions, boutons, multiplexage,
      entrées analogiques — réf. `joy.c`, MAME
- [ ] **DIP switches MegaSTE** `$FF9200` : bit HD floppy, désactivation DMA sound, logique
      inversée — réf. `ioMemTabSTE.c`

## Blitter
- [ ] Partage de bus (mode non-hog) au cycle près _(précision cycle)_ — réf. `blitter.c`

## FDC WD1772 + DMA disquette
- [x] **Support STX (Pasti)** — FAIT (port `floppies/stx.c` → `StxImage` + chemin `_STX`
      du FDC ; cf. CHANGELOG §Disquette). Champs ID réels, statut/CRC par secteur, bits
      fuzzy, timing variable, overlay d'écriture. 🎯 **Dungeon Master**, **Stunt Car Racer**,
      **Tower of Babel** etc. jouables ; séquence de lecture IDENTIQUE à Hatari (oracle).
      **Reste** (long tail des protections) : (1) certains jeux plantent après le titre
      (ex. `Rick Dangerous.stx`, `SuperOffRoad` écran noir) — protection spécifique à
      diff'er à l'oracle ; (2) `WRITE TRACK` sur STX non géré (no-op) ; (3) sauvegarde de
      l'overlay d'écriture dans un fichier `.wd1772` (en mémoire pour l'instant, perdu à la
      fermeture) ; (4) son qui démarre un peu tard sur certains STX (à investiguer) ;
      (5) images STX HD / densité. Réf. `floppies/stx.c`.
- [x] **Masquage d'adresse DMA** (octet haut `&0x3f/0x7f/0xff`, bas word-align `&0xfe`) —
      port `FDC_WriteDMAAddress` / `DMA_MaskAddressHigh` (cf. CHANGELOG §Disquette).
- [x] **Compteur de secteurs DMA non relisible** : lecture SCREG renvoie `ff8604recent_`
      (pas `dmaSectorCount_`) ; statut DMA bits 3-15 depuis le dernier accès $FF8604 —
      réf. `fdc.c:FDC_DiskControllerStatus_ReadWord`, `FDC_DmaStatus_ReadWord`.
- [x] **Accès octet à `$FF8604/06` → bus error** (ST non-Falcon) : largeur d'accès
      propagée par le bus ; faute dans le handler FDC — réf. `fdc.c`, `ioMemTabSTE.c`.
- [x] **Timing réel** : modèle ROTATIONNEL fidèle (position tête / index, latence par secteur,
      spin-up 6 tours, head-load, DRQ/FIFO octet par octet, INTRQ daté) — port de la machine à
      états `_ST` d'Hatari (`fdc.c` : `FDC_Update*Cmd`, `FDC_NextSectorID_FdcCycles_ST`,
      `FDC_IndexPulse_*`, FIFO 16 o). **A débloqué Arkanoid** (cf. CHANGELOG §Disquette). Le
      socle pour le support STX (réécriture WD1772 au niveau piste) est désormais en place.
- [ ] **Lecteur HD MegaSTE** : DIP `$FF9200`, densité DD/HD, images 1.44 Mo — réf. `fdc.c`.
      Le facteur de densité est câblé (DD=1) mais le modèle reste DD ; à étendre pour HD/ED.
- [ ] **WRITE TRACK (format) sur `.ST`** : le modèle logique ne peut reformater à géométrie
      non standard (limite partagée avec Hatari, qui renvoie `LOST_DATA`). On extrait en
      best-effort les secteurs du flux écrit ; le « Floppy → Quick Test » du diagnostic Atari
      échoue donc au formatage (attendu). Vrai support = images flux (STX/SCP).
- [ ] **FIFO DMA/MMU** : ~~secteur-count, status bits, transfert par blocs~~ FAIT (port
      `FDC_DMA_FIFO_Push/Pull`) ; reste l'interaction MMU/ACSI fine — réf. MAME `stmmu.cpp`

## YM2149 PSG
- [ ] Données port B Centronics + front strobe (bit5) non émulés en sortie _(faible valeur)_ —
      réf. `psg.c:PSG_Set_DataRegister`
- [x] **Mixage DAC non linéaire 32³ + porte ton/bruit (ET logique) + filtres de sortie**
      (passe-haut anti-DC `Subsonic_IIR_HPF` + passe-bas PWM par défaut) — port fidèle de
      `sound.c` (table modélisée, mixage, filtres). Cf. `CHANGELOG.md` § Audio et
      `docs/SOUND_HATARI_DIFF.md`. _Reste : filtre passe-bas STF alternatif (`LowPassFilter`)
      sélectionnable, et table 16³ interpolée (`interpolate_volumetable`) en option._
- [x] **Masquage à l'écriture + sélecteur de registre ≥ 16** (`YM2149::write8/read8`, port psg.c:252-358) :
      select 8 bits non masqué ; registre ≥16 → écriture ignorée / lecture 0xFF (compat *European Demo*) ;
      masquage R1/3/5/13→&0x0F, R6/8/9/10→&0x1F. `$FF8802` reste relisible (choix délibéré RMW diags,
      revalidé : batterie `Z` du diagnostic ST byte-identique sur Musashi ET Moira). _Reste (très faible
      valeur) : read-latch `regReadData_`, $FF8801/03→0xFF (risque word-read, non fait)._

## Son DMA STE + Microwire/LMC1992
- [x] **Cas limites start==end** (`DmaSnd_StartNewFrame`) : trame vide + repeat off → arrêt
      SANS lever XSINT (`startNewFrame()`, corrige GPIP7 figé HAUT). Adresse de trame relue =
      `startAddr` à l'arrêt (`DmaSnd_GetFrameCount`). Reset cold/warm : le LMC1992 (sans broche
      de reset) persiste au reset à chaud. _Reste : avance live cycle-exacte du compteur (Phase C)._
- [ ] Décodage commande LMC1992 : run de masque contigu au lieu de tous les bits _(faible
      valeur)_ — réf. `dmaSnd.c:DmaSnd_InterruptHandler_Microwire`
- [x] **Registre mixage LMC1992 (reg 0) appliqué** (`DmaSound::mix`) : mixing==1 → YM2149+DMA,
      0/2/3 → DMA seul (écrase le YM), uniquement trame en cours. Réf. `dmaSnd.c:555-568`.
      _NB : EmuTOS STE programme mixing=1 au boot → YM audible par défaut._
- [x] **Décodage du son sur l'horloge d'émulation + anneau vers le thread audio** (Phase C) :
      écritures PSG horodatées + rejeu (`YM2149::synthesizeFrame`) → capture les modulations
      sous-buffer ; anneau SPSC (`SampleRing`) ; `Audio::produceFrame` (émulation) / `render` (drain).
      🎯 étalon : **Xenon 2**, **Turrican** — digidrums. _Reste : synthèse interne 250 kHz +
      rééchantillonnage, FIFO/anti-repliement DMA. La latence/anti-dérive est VALIDÉE :
      le « son haché et ralenti » venait de la CADENCE de la boucle GUI (1 trame/itération
      à ~40 fps réels + vsync bloquant + bridage 20 ms fixe ≠ géométrie 60 Hz) — résolu
      par la boucle de RATTRAPAGE (cf. CHANGELOG §Audio, 0 underrun mesuré ; compteur
      d'underruns + cadence observée désormais imprimés sur stderr en cas de problème)._
- [x] **Musique muette sur la majorité des titres** — RÉSOLU par la Phase C (écritures registres
      horodatées/rejouées) + le fix d'amorçage de l'anneau (latence ~80 ms au lieu de « 30 s »).
      **Validé à l'oreille sur _Magic Pocket_.**

## IKBD HD6301 + souris/joystick
- [ ] Keymap international / layouts TOS (FR/UK/DE, autorepeat) _(faible valeur)_ — réf. `keymap.c`

## ACIA 6850 (clavier + MIDI)
- [ ] IRQ émetteur (CR bits 5/6) + état TDRE (câblé à 1) _(risque élevé)_ — réf.
      `acia.c:ACIA_UpdateIRQ` + `midi.c`
- [ ] Lecture data-register renvoie 0x00 si FIFO vide au lieu du dernier RDR _(faible valeur)_
      — réf. `acia.c:ACIA_Read_RDR`
- [ ] SR n'expose pas overrun/framing/parity _(faible valeur)_ — réf. `acia.c`

## RTC RP5C15
- [ ] Sauvegarde persistante de la date/heure RTC entre sessions _(faible valeur)_.

## CPU : IRQ, Moira, MegaSTE
- [ ] **Bascule CPU 8/16 MHz MegaSTE** (`$FF8E21` bit1) — change le débit de cycles et tous
      les timings _(précision cycle)_ — réf. `m68000.c:MegaSTE_CPU_Cache_Update` + `clocks_timings.c`
- [ ] **Cache MegaSTE 16 Ko** (`$FF8E21` bit0, off à 8 MHz) — au moins les effets de timing
      visibles — réf. `ioMemTabSTE.c`
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
- [ ] **Vroom** : vers la droite accélère, vers la gauche ralentit, en bas tourne à droite et
      en haut tourne à gauche en mode souris (disquette testable au headless). MOUSE_X_SIGN/MOUSE_Y_SIGN sont tous deux
  +1 et le format paquet $F8 est dx-puis-dy standard (sinon TOUS les jeux souris seraient
  affectés, pas seulement Vroom). Ta description = contrôles tournés de 90° (X↔Y) uniquement
  pour Vroom → ça sent le code 6301 custom que Vroom téléverse via $20/$22 (les
  CustomCodeDefinitions de Hatari)

## Outillage / qualité
- [~] **Logiciels étalons au headless** — infra en place : `tools/etalons.json` (manifeste),
      `tools/fetch_etalons.py` (fetch freeware), `tools/run_etalons.py` (captures +
      régression vs `tests/reference/`), `tools/compare_screenshot.py` (diff pixel,
      crop active/buffer), `tools/hatari_oracle.sh` (oracle PNG, `--oracle`). Étalon
      intégrés : glue_selftest, EmuTOS STE boot, Spectrum 512 diapo, overscan_top ;
      fetch auto : Cuddly Demos (fujiology). **Reste** : calibrer frames + références
      Cuddly/Union/Troed/Hatari Test Suite ; rapatrier Union (planetemu manuel).
      Catalogue → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md).
- [ ] **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- [ ] Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- [ ] **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
      on/off, DD/HD, mono/couleur.
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI
- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.
