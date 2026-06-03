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

- [ ] **Latch palette/scroll mi-ligne** — la scanline est rendue une fois à DE_END (cycle
      376) ; pas de changement intra-ligne — réf. `video.c:Video_RenderLine`
      🎯 prérequis de **Spectrum 512** (changement de palette intra-ligne)
- [ ] **Wait states** d'accès YM2149 / mémoire (4 cycles + alignement) et contention bus
      _(précision cycle)_ — réf. `psg.c`, `cycles.c`, MAME `stmmu.cpp::bus_contention`

### Cas concrets — état RÉEL mesuré
- [~] **Arkanoid** — RE-DIAGNOSTIQUÉ 2026 (détail vivant → [[arkanoid-freeze-investigation]]).
      Symptôme : titre affiché puis **gel sur `$31736: tst.b $26e7 ; bne`** (`$26E7=$3F`), beep
      YM continu. **L'ancienne piste « sur-détection mémoire » est ÉCARTÉE** : NeoST détecte
      512K correctement (`phystop=$80000`, `$FF8001=$04`, identique Hatari) ; `mmuTranslate` est
      un port exact de `stMemory.c`. **Cause confirmée = TIMING FDC** : `hatari --fastfdc on`
      REPRODUIT le gel, Hatari défaut (FDC réaliste) **ne gèle pas** (`$31736` 1 hit / 12000 vbls,
      titre animé). Facteur dominant = **spin-up** (6 tours ≈ 1 s, `FDC_DELAY_IP_SPIN_UP`) absent
      de NeoST. **MAIS timing FDC nécessaire ≠ suffisant** : en portant transfert MFM réel +
      spin-up + latence rotationnelle, le gel est **retardé** (frame ~450→~6000) mais **PAS
      éliminé** (NeoST re-spinne ; Hatari non) → reste un bug NeoST spécifique. **Reste** :
      (1) modèle FDC rotationnel propre (cf. « Timing réel » §FDC) ; (2) facteur NeoST-spécifique ;
      (3) diff trace post-chargement NeoST↔Hatari.

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
- [ ] **Suppression de bordures** (gauche/droite/haut/bas, tricks 50/60 Hz) — base des démos
      _(précision cycle)_ — réf. `video.c` BORDERMASK_*
      🎯 étalon : **The Cuddly Demos** (4 bordures), **Enchanted Land** (sync-scroll horizontal)
- [ ] **Spec512** (palette par scanline/cycle, 512 couleurs) _(précision cycle)_ — réf.
      `spec512.c` + `video.c:Video_ColorReg_WriteWord`
      🎯 étalon : **Spectrum 512** (Inshape) — palette réécrite plusieurs fois/ligne
- [ ] Quirk miroir d'écriture octet de palette (`$FF824x` .B) _(risque élevé)_ — réf.
      `video.c:Video_ColorReg_WriteWord`
- [ ] **Joypads/paddles/lightpen STE** (`$FF9200-$FF9222`) : directions, boutons, multiplexage,
      entrées analogiques — réf. `joy.c`, MAME
- [ ] **DIP switches MegaSTE** `$FF9200` : bit HD floppy, désactivation DMA sound, logique
      inversée — réf. `ioMemTabSTE.c`

## Blitter
- [ ] Partage de bus (mode non-hog) au cycle près _(précision cycle)_ — réf. `blitter.c`

## FDC WD1772 + DMA disquette
- [ ] **Support STX (Pasti)** — images BAS NIVEAU (pistes/secteurs bruts, IDs, CRC, bits
      faibles/fuzzy, timing) pour les jeux protégés (ex. `disks/Stunt Car Racer.stx`).
      🎯 étalon : **Dungeon Master** (FTL) — protection « fuzzy bits » + secteurs 8192 o.
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
- [ ] **Timing réel** : modèle ROTATIONNEL fidèle (position tête / index, latence par secteur,
      spin-up 6 tours, head-load, DRQ/FIFO octet par octet, INTRQ daté) _(précision cycle)_ —
      réf. `fdc.c` (FDC_DELAY_*, FDC_UpdateAll, FDC_NextSectorID_NbBytes). **Débloque Arkanoid**
      (cf. [[arkanoid-freeze-investigation]]) : une 1ère approche (spin-up + transfert MFM dans
      `Fdc::commandDelayCycles`) retarde le gel sans le supprimer et sur-ralentit les lectures
      multi-secteurs → le vrai modèle rotationnel est requis. Sert aussi au support STX.
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
- [ ] Aliasing BANK=1 AM/PM de `$FFFC25/27` (chemin TOS 1.0x) _(faible valeur)_ — réf.
      `rtc.c:Rtc_Minutes*` (rtc_bank)
- [ ] Détection **Mega ST** par EmuTOS : sonde `$FFFC21` + validation `$FFFC25/27` (NeoST
      renvoie `0xFF` → label « Atari ST »). C'est le levier restant pour le label « Mega ST ».

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
