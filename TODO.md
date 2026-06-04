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
ci-dessous. Ordre de débogage affichage : **Spectrum 512 → Cuddly Demos → Enchanted Land**.

---

## 🎯 Le grand chantier : précision cycle

> Plan détaillé : **[`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)** (ordonnanceur
> d'événements datés, vidéo/MFP/FDC au cycle, validation par `trace_diff` ↔ Hatari).
>
> **Phases 1-6 faites** (cf. CHANGELOG : `Scheduler`, `runFrame` événementiel à horloge
> continue, vidéo au cycle, timers MFP A/B/C/D datés, géométries 50/60/71 Hz, quantum
> « sous la ligne »). Reste :

- [x] **Latch palette mi-ligne** — FAIT pour la PALETTE (port `spec512.c`) : écritures
      `$FF824x` datées au cycle live de Moira, re-rendu de fin de trame à palette roulante
      (cf. CHANGELOG §Vidéo + item **Spec512** ci-dessous). Reste le **scroll** fin mi-ligne
      (sync-scroll Enchanted Land) et la latence sous-pixel — réf. `video.c:Video_RenderLine`.
- [ ] **Wait states** d'accès YM2149 / mémoire (4 cycles + alignement) et contention bus
      _(précision cycle)_ — réf. `psg.c`, `cycles.c`, MAME `stmmu.cpp::bus_contention`.
      🎯 **DÉBLOQUE Spectrum 512** : Moira (68000 pur) écrit la palette ~2 cyc/ligne trop tôt
      vs Hatari (qui modélise ces wait states) → le flux spec512 dérive intra-ligne. Socle
      commun avec la **suppression de bordures**.

### Cas concrets — état RÉEL mesuré
- [x] **Arkanoid** — RÉSOLU 2026 par le **modèle FDC rotationnel** (cf. CHANGELOG §Disquette,
      [[arkanoid-freeze-investigation]]). Le gel `$31736` exigeait le **spin-up** (6 tours) ET
      le **débit MFM réel** (256 cyc/octet) ET la **latence rotationnelle** par secteur — tout
      cela fourni par le port de la machine à états `_ST` d'Hatari. L'écran-titre est désormais
      stable (Musashi + Moira), comme Hatari sans `--fastfdc`.

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
      **Phase 1 FAITE** : overscan VISIBLE (buffer 416×276 = dims Hatari, actif centré, bordure
      = registre 0 ; fenêtre GUI ajustée ; timeline inchangée → zéro régression). **Reste le
      RETRAIT** : **socle FAIT** (enregistrement switches sync/res au cycle ; rendu fenêtré par
      ligne + adresse vidéo accumulée ; gaté → zéro régression vérifiée). MAIS la **détection**
      (`computeBorderWindows`) n'est qu'un STUB idéalisé : **mesure oracle sur The Cuddly Demos**
      (écran overscan, 64 switches/trame) → les démos enchaînent par ligne des pulses freq 60/50
      + res hi/lo EN FIN de ligne (cyc ~300-450) enveloppant la frontière, avec dérive −2 cyc/ligne
      (sync-scroll). **Le retrait réel EXIGE le portage de la MACHINE GLUE complète d'Hatari**
      (`Video_Update_Glue_State` + `Video_EndHBL`, ~400 lignes : DisplayStartCycle/EndCycle/PixelShift
      incrémentaux + tables `pVideoTiming`) **et l'alignement de la timeline sur HBL 63** (display à
      la ligne 63 — corrige aussi le décalage spec512 dLine=−60). C'est le gros morceau restant.
      🎯 étalon en place : `disks/demos/The_Cuddly_demo.msa` (charge ; titre byte-identique Hatari).
      🎯 étalon : **The Cuddly Demos** (4 bordures), **Enchanted Land** (sync-scroll horizontal)
- [~] **Spec512** (palette par scanline/cycle, 512 couleurs) _(précision cycle)_ — MÉCANISME
      FAIT (port `spec512.c` : enregistrement daté + re-rendu palette roulante, détection
      > 1024 écritures/trame, jusqu'à 512 couleurs ; cf. CHANGELOG §Vidéo). **Bloqué pixel-perfect**
      par la dérive ~2 cyc/ligne du flux d'écritures (Moira 68000 pur sans wait states /
      contention bus → cf. item **Wait states** ci-dessus). Diff oracle Hatari : couleurs et
      synchro ligne OK, position intra-ligne dérive (BEE512 net en haut, dérive en bas).
      🎯 étalon : **Spectrum 512** (Antic) — `BEE512.SPC` via SPSLIDE8 en `AUTO` (auto-affiché).
      Réf. `Shifter::finishFrame/recordColorWrite`, `spec512.c`, `video.c:Video_ColorReg_WriteWord`.
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
- [ ] Masquage d'adresse DMA (octet haut `&0x3f`, bas word-align `&0xfe`) _(faible valeur)_ —
      réf. `fdc.c:FDC_WriteDMAAddress`
- [ ] Compteur de secteurs DMA non relisible sur le vrai HW _(risque élevé)_ — réf.
      `fdc.c:FDC_DiskControllerStatus_ReadWord`
- [ ] Accès octet à `$FF8604/06` devrait fauter sur ST non-Falcon _(risque élevé)_ — réf. `fdc.c`
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
      🎯 étalon : **Xenon 2**, **Turrican** — digidrums (volume YM modulé par Timer A >8 kHz)
- [ ] **Musique muette sur la majorité des titres** — à investiguer (PSG/digidrums/timing
      d'écriture registres ; cf. retour utilisateur). Lié probablement à l'item ci-dessus.

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
- [ ] **STX / Pasti** : chantier commencé (parseur `StxImage` + premiers hooks FDC), à terminer
      plus tard : montage réel, timings/fuzzy bits, champs ID et validation protections.
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
      en haut tourne à gauche en mode souris (disquette testable au headless).

## Outillage / qualité
- [ ] **Logiciels étalons au headless** — rapatrier les démos freeware via `tools/fetch_disk.py`
      et les passer au headless (`--frames`/`--screenshot`/`--irq`, diff oracle Hatari) :
      **The Cuddly Demos** (bordures), **The Union Demo** (réentrance IRQ Timer A+B),
      **ST-STE Hardware Test** (Troed : timings Shifter/mémoire), **Hatari Test Suite** (68000).
      Jeux commerciaux (Spectrum 512, Dungeon Master, Xenon 2, Enchanted Land) = images perso.
      Catalogue complet → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md).
- [ ] **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- [ ] Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- [ ] **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
      on/off, DD/HD, mono/couleur.
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI
- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.
