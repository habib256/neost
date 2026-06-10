# Logiciels étalons — cas limites d'émulation

> (c) 2026 VERHILLE Arnaud — catalogue des jeux/démos/suites qui poussent un émulateur ST
> dans ses retranchements, classés par **sous-système** matériel.
>
> Complète la **méthode imposée** (`CLAUDE.md`) : on porte d'abord la source de vérité
> (`extern/hatari/src`), PUIS on valide avec du logiciel réel. Chaque cible ci-dessous
> exerce un effet non documenté (affichage détourné, timing au cycle près, astuce de
> synchro HW) qui ne sort que si l'émulation est fidèle. Les chantiers correspondants
> sont dans [`TODO.md`](../TODO.md) ; le rendu attendu se valide à l'oracle Hatari
> ([`HATARI_AUTOMATION.md`](HATARI_AUTOMATION.md)) et l'instant d'IRQ au cycle via
> [`CYCLE_ACCURACY.md`](CYCLE_ACCURACY.md).

⚠ **Redistribution** : seules les démos freeware (Cuddly Demos, Union Demo, suites de test
HW) sont rapatriables via `tools/fetch_disk.py` (domaine public / freeware). Les jeux
commerciaux (Dungeon Master, Xenon 2, Arkanoid, Enchanted Land, Turrican) ne le sont PAS —
tester avec ses propres images.

## Ordre de débogage affichage conseillé

Du plus simple au plus violent, chaque étape suppose la précédente acquise :

1. **Spectrum 512** — ✅ **RÉSOLU** (100 % pixel-identique à Hatari, sans flicker) : stabilité
   de la palette par ligne (synchro CPU↔faisceau de base).
2. **The Cuddly Demos** — suppression des 4 bordures (timings HSYNC/VSYNC).
3. **Enchanted Land** — sync-scroll horizontal (bascule 50/60 Hz au cycle exact).

---

## 1. Shifter / synchro CPU-vidéo

Le ST n'a ni scrolling matériel, ni split-screen, ni copper : tout passe par la
réécriture des registres Shifter (fréquence 50/60/71 Hz, résolution, palette) à des
cycles précis du 68000 PENDANT le balayage. → réf. NeoST : `Shifter`, `Machine::runFrame` ;
Hatari : `video.c`, `spec512.c`.

| Étalon | Éditeur | Mécanisme | Cas limite testé | TODO NeoST |
|--------|---------|-----------|------------------|------------|
| **Spectrum 512** | Inshape | Réécrit la palette plusieurs fois par ligne → >512 couleurs affichées | Synchro cycle-près `MOVE.W` ↔ position du faisceau. Défaut : couleurs décalées verticalement, bandes/flicker | ✅ **RÉSOLU** — diaporama étalon **0 px vs Hatari** (4 images), flicker éliminé. Reste : scroll fin mi-ligne |
| **Enchanted Land** | Thalion | Scrolling horizontal pixel-près SANS Blitter : bascule 50↔60 Hz en fin de ligne pour tromper le compteur d'adresse interne du Shifter et décaler l'adresse de base | 1 cycle d'erreur → écran qui saute/déchire ou plante. Exige la géométrie variable EN COURS de trame | « Suppression de bordures » (géométrie mi-trame) |
| **The Cuddly Demos** | The Carebears (TCB), 1989 | 1ʳᵉ démo à ouvrir les **4 bordures** (haut/bas/gauche/droite) simultanément : boucles de NOP calibrées qui commutent la fréquence au moment où le canon atteint les limites de l'affichage standard | Précision des timings de génération HSYNC/VSYNC + tampons internes Shifter | « Suppression de bordures » (BORDERMASK_*) |

## 2. MFP 68901 / interruptions

Timers et IRQ poussés à fréquences extrêmes (rasters, musique, digidrums). → réf. NeoST :
`Mfp`, `Scheduler` ; Hatari : `mfp.c`, `cycInt.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **The Union Demo** (menu) | Timer B = IRQ par ligne de balayage (rasters/lignes de couleur) **+** Timer A = musique en fond | Priorité des IRQ, latence de prise en compte de l'exception 68000, **réentrance** (IRQ pendant IRQ). Défaut : blocage CPU (Line F / Bus Error) | Timer B/A faits ; reste latence IRQ au cycle (cf. `cycle-accuracy`) |

## 3. YM2149 PSG / digidrums (PCM)

Le YM2149 ne sait pas jouer d'échantillons : les jeux émulent une voix numérique en
modulant le **volume** des canaux à très haute fréquence, cadencé par le **Timer A** du
MFP (souvent >8 kHz). → réf. NeoST : `YM2149`, `Mfp::timerA_*` ; Hatari : `psg.c`, `sound.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **Xenon 2: Megablast**, **Turrican** (musiques J. Hippel / D. Whittaker) | Volume des canaux YM réécrit à chaque IRQ Timer A → voix/percussions PCM | Synchro MFP↔YM2149 parfaite. Défaut : aliasing sévère, craquements, dérive de la hauteur tonale | Timer A event-count fait ; reste « décodage son sur l'horloge d'émulation » (précision cycle) |

## 4. Contrôleur disquette WD1772 / protections

Les protections (souvent Rob Northen) exploitent des caractéristiques PHYSIQUES non
standards du flux magnétique, qu'une émulation WD1772 « haut niveau » (logique) ne
reproduit pas. → réf. NeoST : `Fdc` ; Hatari : `fdc.c`, `floppies/stx.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **Dungeon Master** | FTL | « **Fuzzy bits** » : flux affaibli volontairement → le WD1772 lit alternativement 0 ou 1 à chaque passage. + secteurs de tailles exotiques (8192 o) | Fidélité au flux physique (format `.STX`/`.IPF`) : timing de rotation exact + registres d'erreur du contrôleur. Une émulation logique échoue à lancer le jeu | « Support STX (Pasti) » + « Timing réel » (FDC cycle-exact) |

## Suite headless NeoST (`tools/run_etalons.py`)

Infra de non-régression par captures PPM (déterministe, sans GUI) :

```sh
# 1. Rapatrier les disques freeware listés dans tools/etalons.json
python3 tools/fetch_etalons.py

# 2. Générer les captures de référence (1ʳᵉ fois ou après correctif validé)
python3 tools/run_etalons.py --update-ref

# 3. Régression (compare NeoST vs tests/reference/*.ppm)
python3 tools/run_etalons.py

# Sous-ensemble + oracle Hatari pour une nouvelle référence externe
python3 tools/run_etalons.py --only spectrum512_diapo --oracle
python3 tools/compare_screenshot.py tests/out/foo_neost.ppm tests/reference/foo.png --crop active
```

Étalons intégrés aujourd'hui : **glue_selftest**, **EmuTOS STE boot**, **Spectrum 512 diapo**,
**overscan_top** ; fetch auto : **Cuddly Demos** (`disks/etalons/cuddly_demos.msa`). Union Demo
et Troed : à rapatrier / calibrer (frames `etalons.json`).

---

## 5. Suites de test automatisées

Micro-tests formalisés par la communauté (Hatari, Steem), à exécuter au headless plutôt
que jeu par jeu.

| Suite | Cible de validation | Réf. |
|-------|---------------------|------|
| **Hatari Test Suite** | Micro-tests 68000 (instructions/exceptions non documentées) + Shifter | dépôt Hatari (`tests/`) |
| **ST-STE Hardware Test** (Troed / Sync) | Timings fins du Shifter, détection des modes de rémanence, variations de cycles mémoire (accès RAM asynchrones CPU/vidéo) | scène Atari |
| **Anatool / Discovery Cartridge** | Lignes de statut WD1772 lors du formatage / lecture de pistes corrompues | utilitaire bas niveau |
