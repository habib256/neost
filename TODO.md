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

**Légende** : `lot suivant` = portable, faible risque · `précision cycle` = ordonnanceur daté
([`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)) · `risque élevé` = bus/IRQ éprouvé ·
`gros contrôleur` = puce entière · `faible valeur`.

**Validation** : catalogue logiciels étalon → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md).
Ordre affichage : **Spectrum 512 ✅ → Enchanted Land (JOUABLE ✅) → Cuddly Demos** (menu
statique ✅ ; reste scrolling robot + scroller bordure basse).

---

## 🎯 Précision cycle

> Plan : [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md) · Inventaire :
> [`docs/CYCLE_EXACT_INVENTORY.md`](docs/CYCLE_EXACT_INVENTORY.md).
>
> Phases 1-6, latch palette Spec512, alignement bus shifter + wait states PSG/MFP/ACIA,
> machine Glue live, VDE_On live, Spec512 pixel-perfect, bordures haut/bas/gauche/droite :
> **FAIT** (cf. CHANGELOG).

- [ ] **Scroll fin mi-ligne dans le RENDU** + latence sous-pixel _(précision cycle)_ —
      réf. `video.c:Video_RenderLine`. Fenêtre DE par-ligne déjà live (Enchanted Land OK).
      🎯 étalon : **Enchanted Land** (sync-scroll en jeu).
- [ ] **Contention DMA vidéo sur la RAM** _(précision cycle, reporté)_ — modèle MAME
      (`stmmu.cpp::bus_contention`), **non porté depuis Hatari** (qui ne le modélise pas) ;
      divergerait de l'oracle pixel. À ne traiter que si besoin matériel réel hors Hatari.
- [ ] **Arkanoid** — écran-titre OK (FDC rotationnel, cf. CHANGELOG), **mais le jeu ne
      démarre jamais** (titre → partie : protection ? second chargement ? IRQ ?). À diff'er
      contre Hatari (`--keys`/`--joy`, trace IRQ). 🎯 étalon suite FDC/protection.

## Bus / memory map / MMU
- [x] Banque ROM = toute la fenêtre décodée — FAIT (cf. `CHANGELOG.md`) : 1 Mo à
      `$E00000` (lecture 0 au-delà du fichier, écriture fautive partout), 192 Ko
      à `$FC0000` (= le fichier). `Bus::romWindowSize`.
- [x] Accès FDC/ACSI/son-DMA via le plan mémoire — FAIT : `Bus::dmaRead8/dmaWrite8`
      (port `STMemory_DMA_Read/WriteByte`) : traduction MMU incluse, jamais de bus
      error (lecture fautive → 0, écriture perdue).
- [x] Remapping réel des banques MMU + bus error des zones non peuplées — DÉJÀ
      couvert : `Bus::mmuTranslate` (port `STMemory_MMU_Translate_Addr` STF/STE,
      aliasing 128/512/2048 Ko, validé par le sizing RAM du Test Kit) ; bus error
      `$400000-$F9FFFF` hors cartouche/ROM (carte `busFault`). Reste éventuel
      _(très faible valeur)_ : la zone void `[fin RAM, $400000)` lit 0 au lieu du
      dernier mot du bus (`regs.db`, cf. `VoidMem_bget` — « no program known to
      depend on it » dixit Hatari).

## MFP 68901 + RS232 USART
- [x] Config baud USART UCR/Timer-D — FAIT (cf. `CHANGELOG.md`) : `Mfp::updateSerialConfig`
      (port `RS232_SetBaudRateFromTimerD` + `RS232_HandleUCR`) calcule bauds + format à
      chaque écriture UCR/TDDR/TCDCR, exposés (`serialBaud()`) et journalisés. Comme
      Hatari, pure config (tty hôte chez lui) : le débit émulé reste instantané.

## Vidéo / Shifter
- [ ] Quirk miroir d'écriture octet de palette (`$FF824x` .B) _(risque élevé)_ — réf.
      `video.c:Video_ColorReg_WriteWord`
- [ ] **Bordures — raffinements** _(précision cycle, faible priorité)_ :
      (1) wakeup-state WS3 (+1 cyc, sous-pixel) ; (2) med-res overscan ; (3) blank lines /
      NO_SYNC ; (4) pixel-perfect L/D end-to-end ; (5) **Cuddly Demos — scrolling qui SAUTE**
      quand le robot bouge ; (6) **scroller bordure BASSE** du menu Cuddly non rendu.
      🎯 étalons : `make_overscan_test.py` / `make_overscan_lr.py` (✅), **The Cuddly Demos**.
- [ ] **Joypads STE** — reste : paddles analogiques réels, lightpen (entrée hôte), bus error
      sur accès octet de `$FF9200/20/22` (Hatari faute, NeoST whiteliste).

## Blitter
- [ ] Partage de bus (mode non-hog) au cycle près _(précision cycle)_ — réf. `blitter.c`

## FDC WD1772 + DMA disquette
- [ ] **Lecteur HD MegaSTE** : DIP `$FF9200`, densité DD/HD, images 1.44 Mo — réf. `fdc.c`.
      Facteur de densité câblé (DD=1) mais modèle reste DD.
- [ ] **WRITE TRACK (format) sur `.ST`** : pas de reformatage géométrie non standard (limite
      Hatari → `LOST_DATA`). Vrai support = images flux (STX/SCP).
- [ ] **FIFO DMA/MMU** : interaction MMU/ACSI fine — réf. MAME `stmmu.cpp`
- [ ] **STX — long tail protections** : jeux plantant après titre (`Rick Dangerous.stx`,
      `SuperOffRoad` écran noir) ; `WRITE TRACK` STX no-op ; persistance overlay `.wd1772` ;
      son tardif sur certains STX ; images STX HD/densité.

## YM2149 PSG
- [ ] Données port B Centronics + front strobe (bit5) non émulés en sortie _(faible valeur)_ —
      réf. `psg.c:PSG_Set_DataRegister`
- [ ] Filtre passe-bas STF alternatif (`LowPassFilter`) + table 16³ interpolée
      (`interpolate_volumetable`) en option _(faible valeur)_.
- [ ] Read-latch `regReadData_`, `$FF8801/03` → 0xFF _(faible valeur, risque word-read)_.

## Son DMA STE + Microwire/LMC1992
- [ ] Avance live cycle-exacte du compteur DMA _(Phase C)_ — réf. `dmaSnd.c`.
- [ ] Synthèse YM interne 250 kHz + rééchantillonnage, FIFO/anti-repliement DMA
      _(refinements, cf. `docs/SOUND_HATARI_DIFF.md`)_.

## IKBD HD6301 + souris/joystick
- [x] Keymap international / layouts TOS — FAIT (cf. `CHANGELOG.md`) : mapping
      SYMBOLIQUE port de `sdl/keymap.c` (caractère hôte via `glfwGetKeyName` →
      scancode ST, surcharges US/DE/FR/UK choisies par le pays de la ROM, os_conf
      `$1C`). Autorepeat déjà conforme (GLFW_REPEAT ignoré, TOS répète lui-même).
      Reste éventuel : autres pays (ES/IT/SE/CH…) en surcharges dédiées (la table
      par défaut couvre déjà leurs lettres nationales) et fichier de remap utilisateur.

## ACIA 6850 (clavier + MIDI)
- [x] IRQ émetteur (CR bits 5/6) + état TDRE — FAIT (cf. `CHANGELOG.md`) : clavier
      (existant, IKBD_TX) **et MIDI** (`Scheduler::MIDI_TX`, TDRE re-rempli après
      2560 cyc = 1 octet à 31250 bauds). Hors TIE, TDRE reste câblé à 1 (simplification).
- [x] SR overrun — FAIT : bit OVRN porté de `acia.c` (livraison SCI continue, octet
      perdu si RDR plein, OVRN posé à la lecture RDR, acquitté par SR→RDR). FE/PE
      restent à 0 (la liaison émulée ne produit pas d'erreur de trame/parité).

## CPU : IRQ, Moira, MegaSTE
- [x] **Bascule CPU 8/16 MHz MegaSTE** (`$FF8E21` bit1) — FAIT (cf. `CHANGELOG.md`) :
      conversion horloge CPU↔bus dans `Cpu68k` (Moira cycle-exact : wait states RAM par
      créneau bus ; Musashi : débit ×2 comme Hatari non-CE).
- [x] **Cache MegaSTE 16 Ko** (`$FF8E21` bit0, off à 8 MHz) — FAIT : port de
      `MegaSTE_Cache_*` dans `Bus` (timing Moira) ; flush sur disable/reset/bus
      error/BGACK (blitter + DMA FDC/ACSI).
- [~] **MC68881 optionnel** : FAIT au niveau « sonde + trapping » (`--fpu`, CIR
      $FFFA40-$FFFA5F journalisés, réponses neutres — cf. `src/io/Fpu.hpp`). RESTE :
      l'arithmétique flottante (registres FP0-7, formats simple/double/étendu/packed,
      dialogue Command/Response complet) — réf. MC68881 UM §7, MAME.
- [x] **Séparation user/supervisor** : FAIT — bus errors en mode utilisateur sur
      $0-$7FF et IO, écritures ROM/cartouche/$0-$7 fautives (Hatari `SysMem_*`,
      `ROMmem_*put`, `is_super_access` d'`ioMem.c`).

## Stockage & contrôleurs
- [ ] **GEMDOS HD** : monter un dossier hôte comme lecteur C: — réf. `gemdos.c`
- [ ] **ACSI complet** (jusqu'à 8 périphériques, boot disque dur TOS) — réf. `hdc.c`, MAME
- [ ] **SCC Z85C30 MegaSTE** : canaux A/B, IRQ niv5, baudrate _(gros contrôleur)_ — réf.
      `scc.c`, MAME `z80scc`
- [ ] **SCSI / NCR5380** (MegaSTE/TT) _(gros contrôleur)_ — réf. `ncr5380.c`
- [ ] **Imprimante/Centronics** : port B YM, strobe PSG port A bit5, busy MFP I0 — réf.
      `printer.c`

## Périphériques & profils machine
- [ ] **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000`, choix pays, checksums, fallback
      EmuTOS MegaSTE.
- [ ] **NVRAM / préférences TOS MegaSTE** (résolution/boot device) si TOS 2.x l'exige.
- [ ] **Cartridge port** `$FA0000-$FBFFFF` générique — réf. `cart.c`

## Outillage / qualité
- [ ] **Étalons headless** — infra en place (cf. CHANGELOG) ; reste : calibrer frames +
      références Cuddly/Union/Troed/Hatari Test Suite ; rapatrier Union (planetemu manuel).
- [ ] **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- [ ] Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- [ ] **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
      on/off, DD/HD, mono/couleur.
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI
- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.
