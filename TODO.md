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
- [ ] Zone void `[fin RAM, $400000)` : lire le dernier mot du bus (`regs.db`,
      `VoidMem_bget`) au lieu de 0 _(très faible valeur — « no program known to
      depend on it » dixit Hatari)_.

## Vidéo / Shifter
- [ ] **Bordures — raffinements** _(précision cycle, faible priorité)_ :
      (1) wakeup-state WS3 (+1 cyc, sous-pixel) ; (2) med-res overscan ; (3) blank lines /
      NO_SYNC ; (4) pixel-perfect L/D end-to-end ; (5) **Cuddly Demos — scrolling qui SAUTE**
      quand le robot bouge ; (6) **scroller bordure BASSE** du menu Cuddly non rendu.
      🎯 étalons : `make_overscan_test.py` / `make_overscan_lr.py` (✅), **The Cuddly Demos**.
      **Investigation (6) faite (2026-06)** : `renderGlueFrame` décode DÉJÀ correctement
      la bordure basse retirée (rangées 229-275 du buffer, scanlines → 309) — le maillon
      cassé est la DÉTECTION : sur le menu Cuddly, `recordSyncWrite`/`replayGlue` ne
      voient AUCUN retrait (`NEOST_BORDER_TRACE=1` muet, `bordersTrick_` jamais armé)
      → tracer les écritures $FF820A/$FF8260 du menu (cycle dans la ligne, autour de
      `RemoveBottomBorder_Pos=502`) et comparer à `Video_EndHBL`. Atteindre le menu :
      SPACE au titre (`--keys-at 1500 " "`), menu ≈ trame 2800. (5) probablement même
      chemin (géométrie par ligne). ⚠ Oracle Hatari du menu IMPOSSIBLE avec le binaire
      Homebrew (pas d'injection clavier headless : ni `keypress` debugger ni cmd-fifo) —
      rebuilder Hatari avec le debugger complet, ou breakpoint+memwrite sur la boucle
      de poll. Captures de réf. : /tmp/cuddly_keep/ (volatil).

## Blitter
- [ ] Bug « 63 accès au lieu de 64 » de la 1re tranche non-hog + arbitration cycles
      _(très faible valeur — cf. blitter.c:69-79 ; l'alternance 64/64 est FAITE,
      cf. CHANGELOG)_.

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
- [ ] FIFO 8 octets du DMA son remplie sur HBL (`DmaSnd_FIFO_Refill/PullByte`,
      dmaSnd.c:343-410) _(refinement résiduel : timing ±8 octets des débuts/fins de
      trame et drain post-PLAY ; le gros de la Phase C est fait)_.

## CPU : IRQ, Moira, MegaSTE
- [ ] **MC68881 — arithmétique flottante** (la sonde + trapping est FAITE, cf.
      `CHANGELOG.md` / `src/io/Fpu.hpp`) : registres FP0-7, formats simple/double/
      étendu/packed, dialogue Command/Response complet — réf. MC68881 UM §7, MAME.

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
