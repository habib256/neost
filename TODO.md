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

## Catalogue logiciels — bugs en cours

Rapports terrain (2026-06). TOS 1.02fr sauf mention contraire. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`).

- [ ] **Arkanoid (1987)** (`Arkanoid (1987)(Imagine).st`) — plante sur la page de titre
  ```
  (« ARkanoid ») sans jamais arriver au jeu, même avec TOS 1.02fr. Cf. aussi §Précision cycle
  / FDC (gel titre → partie). Oracle : `--keys`/`--joy`, trace IRQ, diff Hatari.
  ```
- [ ] **Captain Blood (1988)** (`Captain Blood (1988)(ERE)(ST)[cr 42-Crew][one disk].st`) —
  ```
  arrive au jeu puis plante et redémarre.
  ```
- [ ] **Enchanted Land (1990)** (`Enchanted Land (1990)(Thalion).st`) — logo + gouttes Thalion
  ```
  OK ; son Talion absent (press bouton joystick) ; scrolling saute terriblement en jeu
  (symptôme proche du bug Cuddly / sync-scroll). Cf. §Bordures.
  ```
- [ ] **Super Hang-On (1988)** (`Super Hang-On (1988)(Sega).st`) — démarre ; musique abîmée
  ```
  par un bruit blanc de fond anormal ; lignes colorées horribles sur les 3/4 bas de l'écran.
  À corriger (son DMA/PSG ? géométrie vidéo ?). Cf. CHANGELOG (retry secteurs FDC).
  ```
- [ ] **Shadow Warriors (2Hot2Handle)** (`ShadowWarriors[2Hot2Handle]-D1/2/3.stx`) — après
  ```
  SPACE : page de titre + musique OK ; appuyer sur un bouton joystick ne lance pas le jeu.
  (Castle Warrior fonctionne parfaitement.)
  ```
- [ ] **Rick Dangerous II (1989)** (`Rick Dangerous II (1989)(Core Design)[cr Empire][t +2][a].st`) —
  ```
  SPACE, puis `n`, encore `n` : plante avec 4 bombes.
  ```
- [ ] **Stardust (1994)** (`Stardust (1994)(Daze Marketing Ltd.)(Disk 1 of 3)[cr Hardcore][t].st`) —
  ```
  plante sur écran noir.
  ```
- [ ] **Lethal Xcess** (`Lethal_Xcess_Disk_1.STX`, `Lethal_Xcess_Disk_2.STX`) — ne démarre
  ```
  pas du tout (écran noir). Cf. §STX long tail protections.
  ```
- [ ] **Stardust Bloodhouse** (`stardust_bloodhouse_a/b/c.STX`) — plante au démarrage
  ```
  (écran noir).
  ```
- [ ] **Wings of Death** (`Wings_Of_Death_Disk_1/2.stx`) — après bouton : page de titre
  ```
  avec forte corruption graphique ; chargement avec son ralenti/bizarre ; SPACE lance le jeu
  qui fonctionne très bien ensuite.
  ```
- [ ] **The Cuddly Demos** (`disks/etalons/cuddly_demos.msa`) — première page OK mais son de
  ```
  mauvaise qualité ; après une touche, menu de sélection (robot) : scrolling complètement
  bugué qui saute. Cf. §Bordures (items 5-6).
  ```

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
- [ ] **Arkanoid** — page de titre « ARkanoid » atteinte (FDC rotationnel, cf. CHANGELOG),
  ```
  **mais plante / ne franchit jamais la partie** (même TOS 1.02fr — protection ? second
  chargement ? IRQ ?). Détail terrain → §Catalogue logiciels. À diff'er contre Hatari
  (`--keys`/`--joy`, trace IRQ). 🎯 étalon suite FDC/protection.
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

- [x] ~~MC68881 — arithmétique flottante~~ → **FAIT** (cf. `CHANGELOG.md`,
  `src/io/Fpu.{hpp,cpp}`) : FP0-7 étendu 80 bits, formats B/W/L/S/D/X/P, dialogue
  Command/Response/Operand/Condition/Save/Restore complet, FMOVECR bit-exact ;
  validé mini-ROM SFP004 (`tools/make_fpu_testrom.py`, 7/7) + diag MegaSTE
  « FPU idle ». _Reste (faible priorité) : mantisse 64 bits réelle (softfloat —
  les calculs passent par le double hôte, 53 bits) ; IRQ d'exception FP non
  câblée (le socket se scrute, la glue SFP004 n'en a pas besoin)._

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