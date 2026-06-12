# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.** Le fait (implémenté + validé) est dans
`[CHANGELOG.md](CHANGELOG.md)` ; les diagnostics de bugs en cours sont en mémoire projet.

**Sources de vérité à croiser systématiquement :**

- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
`wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06,
STE video/sound/joypads, blitter, RTC, SCC, SCU, ACSI/SCSI, DD/HD) avec un timing assez
fidèle pour jeux, démos et utilitaires.

**Légende** : `lot suivant` = portable, faible risque · `précision cycle` = ordonnanceur daté
(`[docs/CYCLE_ACCURACY.md](docs/CYCLE_ACCURACY.md)`) · `risque élevé` = bus/IRQ éprouvé ·
`gros contrôleur` = puce entière · `faible valeur`.

**Validation** : catalogue logiciels étalon → `[docs/TEST_SOFTWARE.md](docs/TEST_SOFTWARE.md)`.
Ordre affichage : **Spectrum 512 ✅ → Enchanted Land → Cuddly Demos** (scrolling robot + scroller bordure basse).

---

## 🎯 Précision cycle

> Plan : `[docs/CYCLE_ACCURACY.md](docs/CYCLE_ACCURACY.md)` · Inventaire :
> `[docs/CYCLE_EXACT_INVENTORY.md](docs/CYCLE_EXACT_INVENTORY.md)`.
>
> Phases 1-6, latch palette Spec512, alignement bus shifter + wait states PSG/MFP/ACIA,
> machine Glue live, VDE_On live, Spec512 pixel-perfect, bordures haut/bas/gauche/droite :
> **FAIT** (cf. CHANGELOG).

- [ ] **Contention DMA vidéo sur la RAM** *(précision cycle, reporté)* — modèle MAME
  ```
  (`stmmu.cpp::bus_contention`), **non porté depuis Hatari** (qui ne le modélise pas) ;
  divergerait de l'oracle pixel. À ne traiter que si besoin matériel réel hors Hatari.
  ```
- [ ] **Arkanoid** — écran-titre OK (FDC rotationnel, cf. CHANGELOG), **mais le jeu ne
  ```
  démarre jamais** (titre → partie : protection ? second chargement ? IRQ ?). À diff'er
  contre Hatari (`--keys`/`--joy`, trace IRQ). 🎯 étalon suite FDC/protection.
  ```

## Bus / memory map / MMU

- [x] ~~Zone void `[fin RAM, $400000)` : lire le dernier mot du bus~~ → **FAIT**
  (`Bus::cpuDb` latché par les overrides Moira, cf. `CHANGELOG.md`).

## Vidéo / Shifter

- [ ] **Bordures — raffinements** *(précision cycle, faible priorité)* :
  ```
  (1) wakeup-state WS3 (+1 cyc, sous-pixel) ; (2) med-res overscan ; (3) blank lines /
  NO_SYNC ; (4) pixel-perfect L/D end-to-end ; (5) **Cuddly Demos — scrolling qui SAUTE**
  quand le robot bouge ; (6) **scroller bordure BASSE** du menu Cuddly non rendu.
  🎯 étalons : `make_overscan_test.py` / `make_overscan_lr.py` (✅), **The Cuddly Demos**.
  **Investigation (6) faite (2026-06)** : `renderGlueFrame` décode DÉJÀ correctement
  la bordure basse retirée (rangées 229-275 du buffer, scanlines → 309) — le maillon
  cassé est la DÉTECTION : sur le menu Cuddly, `recordSyncWrite`/`replayGlue` ne
  voient AUCUN retrait (`NEOST_BORDER_TRACE=1` muet, `bordersTrick`_ jamais armé)
  → tracer les écritures $FF820A/$FF8260 du menu (cycle dans la ligne, autour de
  `RemoveBottomBorder_Pos=502`) et comparer à `Video_EndHBL`. Atteindre le menu :
  SPACE au titre (`--keys-at 1500 " "`), menu ≈ trame 2800. (5) probablement même
  chemin (géométrie par ligne). ⚠ Oracle Hatari du menu IMPOSSIBLE avec le binaire
  Homebrew (pas d'injection clavier headless : ni `keypress` debugger ni cmd-fifo) —
  rebuilder Hatari avec le debugger complet, ou breakpoint+memwrite sur la boucle
  de poll. Captures de réf. : /tmp/cuddly_keep/ (volatil).
  ```

## FDC WD1772 + DMA disquette

- [x] ~~Lecteur HD MegaSTE~~ → **FAIT** : densité DD/HD/ED auto (géométrie), débit MFM
  ```
  ÷ facteur, porte `$FF860E` Mega STE, images 1,44 Mo (cf. `CHANGELOG.md`).
  ```
- [x] ~~WRITE TRACK (format) sur `.ST`~~ → **FAIT** : extraction des secteurs si géométrie
  ```
  standard, sinon `LOST_DATA` tout-ou-rien (limite Hatari). Reformatage non
  standard = images flux (STX/SCP), hors périmètre `.ST`.
  ```
- [x] ~~FIFO DMA/MMU vs MAME `stmmu.cpp`~~ → **TRANCHÉ** (recherche MAME master) : le modèle
  ```
  NeoST (= Hatari) est fidèle ; les écarts MAME (double FIFO 2×16 o, bit2 DRQ live,
  dernier bloc 8 mots) sont des choix que Hatari assume sans impact observable sur ST.
  Seule différence de fond : le chemin ACSI court-circuite le FIFO et `dmaSectorCount_`
  (transfert bloc piloté par le CDB) — à ne corriger QUE si un diagnostic qui
  désaligne sector-count DMA et longueur CDB échoue un jour.
  ```
- [ ] **STX — long tail protections** : jeux plantant après titre (`Rick Dangerous.stx`,
  ```
  images STX HD/densité
  (le débit suit déjà la densité, mais `nextSectorIDStx`/`MFM_BIT` restent en cellules DD) ;
  ré-interprétation en LECTURE d'une piste réécrite par WRITE TRACK (TODO partagé
  avec Hatari : le flux est conservé/persisté mais les lectures voient l'original).
  ```

## YM2149 PSG

- [ ] Données port B Centronics + front strobe (bit5) non émulés en sortie *(faible valeur)* —
  ```
  réf. `psg.c:PSG_Set_DataRegister`
  ```
- [ ] Filtre passe-bas STF alternatif (`LowPassFilter`) + table 16³ interpolée
  ```
  (`interpolate_volumetable`) en option _(faible valeur)_.
  ```
- [ ] Read-latch `regReadData_`, `$FF8801/03` → 0xFF *(faible valeur, risque word-read)*.

## Son DMA STE + Microwire/LMC1992

- [ ] FIFO 8 octets du DMA son remplie sur HBL (`DmaSnd_FIFO_Refill/PullByte`,
  ```
  dmaSnd.c:343-410) _(refinement résiduel : timing ±8 octets des débuts/fins de
  trame et drain post-PLAY ; le gros de la Phase C est fait)_.
  ```

## CPU : IRQ, Moira, MegaSTE

- [ ] **MC68881 — arithmétique flottante** (la sonde + trapping est FAITE, cf.
  ```
  `CHANGELOG.md` / `src/io/Fpu.hpp`) : registres FP0-7, formats simple/double/
  étendu/packed, dialogue Command/Response complet — réf. MC68881 UM §7, MAME.
  ```

## Stockage & contrôleurs

- [ ] **GEMDOS HD** : monter un dossier hôte comme lecteur C: — réf. `gemdos.c`
- [ ] **ACSI complet** (jusqu'à 8 périphériques, boot disque dur TOS) — réf. `hdc.c`, MAME
- [ ] **SCC Z85C30 MegaSTE** : canaux A/B, IRQ niv5, baudrate *(gros contrôleur)* — réf.
  ```
  `scc.c`, MAME `z80scc`
  ```
- [ ] **SCSI / NCR5380** (MegaSTE/TT) *(gros contrôleur)* — réf. `ncr5380.c`
- [ ] **Imprimante/Centronics** : port B YM, strobe PSG port A bit5, busy MFP I0 — réf.
  ```
  `printer.c`
  ```

## Périphériques & profils machine

- [ ] **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000`, choix pays, checksums, fallback
  ```
  EmuTOS MegaSTE.
  ```
- [ ] **NVRAM / préférences TOS MegaSTE** (résolution/boot device) si TOS 2.x l'exige.
- [ ] **Cartridge port** `$FA0000-$FBFFFF` générique — réf. `cart.c`

## Outillage / qualité

- [ ] **Étalons headless** — infra en place (cf. CHANGELOG) ; reste : calibrer frames +
  ```
  références Cuddly/Union/Troed/Hatari Test Suite ; rapatrier Union (planetemu manuel).
  ```
- [ ] **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- [ ] Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- [ ] **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
  ```
  on/off, DD/HD, mono/couleur.
  ```
- [ ] Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).
- [ ] CMake `FetchContent` pour les sous-modules ; CI Linux + macOS.

## Confort GUI

- [ ] Chargeur de ROM **dans l'appli** (la Disk Library gère déjà les disquettes).
- [ ] Désassembleur live + points d'arrêt ; plein écran ; zoom réglable.