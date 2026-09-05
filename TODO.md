# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire — uniquement l'OUVERT.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (le clos détaillé vit là-bas, y compris tout ce qui a été retiré d'ici) → [`CHANGELOG.md`](CHANGELOG.md)

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06, STE
vidéo/son/joypads, blitter, RTC, SCC, SCU, ACSI — le disque interne d'époque est un pont
ACSI-SCSI, PAS un NCR5380, DD/HD) avec un timing assez fidèle pour jeux, démos et utilitaires.
**Atteint et GARDÉ** (2026-08-27) : `tools/run_megaste_diag.py` rejoue la suite Q du
diagnostic Field Service (12/12) à chaque palier `full`. Ce qui suit affine, il ne
conditionne plus l'objectif.

**Sources de vérité à croiser systématiquement** (cf. [`CLAUDE.md`](CLAUDE.md)) :
- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
  `wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Documentation connexe** :
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Beam-sync (chantier CLOS — journal, « ÉTAT COURANT » en tête) → [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md)
- Divergences NeoST↔Hatari (inventaire maître) → [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md)
- Logiciels étalons par sous-système → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md)

---

## Save-states : octets NON INITIALISÉS gravés et hachés (trouvé le 2026-09-05, cœur)

`Machine::saveState` sérialise `Fpu` d'un bloc (`src/core/Bus.cpp` : `ar(fpu)`), or la
classe a du **bourrage** — `struct Ext { uint16_t se; uint64_t man; }` (6 octets de padding
par registre × 8), plus les alignements après `present`, `excVector_`/`buf_`, `bufIn_`,
`cmd_`. Valgrind sur un `export` du serveur : `Syscall param writev(vector[1]) points to
uninitialised byte(s)` + `Use of uninitialised value of size 8` dans le CRC de `saveState`
ET sa vérification dans `loadState`. Premier octet fautif à l'offset 565 383 = **premier
octet du bloc `fpu`** (`NEOST_STATE_MAP=1` : `cpu @ 565723`, et `sizeof(Fpu)+sizeof(Scu)
+sizeof(StePads) = 304+8+28 = 340`). Reproduit à l'identique par `--frames 2 --save-state`
— ce n'est pas propre au serveur.

Conséquences : le CRC32 d'un état dépend de mémoire indéfinie (stable en pratique sur 3
processus et 3 environnements, mais rien ne le garantit), et un `.state` partagé emporte
des octets de mémoire du processus. Le projet applique déjà le bon remède à `DmaEvent`
(`objVec` champ par champ, `DmaSound.hpp`) mais pas ici.

Recette : `printf 'save 0\nexport 0 /tmp/x.state\nquit\n' | valgrind --leak-check=no
./build/neost-headless roms/etos192fr.img --machine st --server` → 67 erreurs, 2 contextes.
Remède à trancher : sérialiser `Fpu` champ par champ (format inchangé si l'ordre et les
tailles sont conservés) ou déclarer le bourrage explicitement avec initialiseurs. Vérifier
ensuite au valgrind ET par `--save-state-test`. Non corrigé à chaud : cœur, hors périmètre
du chantier « pilotage externe ».

## 🚨 BLOQUANT RELEASE — l'historique est PURGÉ (2026-08-30) ; restent les paquets et les vieilles releases

**Pas 3 FAIT le 2026-08-30** : l'historique public est réécrit (`git filter-repo`,
69 motifs, pack **165 → 12 Mio**). Sont sortis du dépôt ET de tout son historique :
les TOS Atari (`roms/tos*`, l'ancien `rom/`), les jeux commerciaux (`disks/st`,
`disks/stx` et leurs emplacements d'avant déménagement), les cartouches Field
Service (`carts/`), Cubase Lite (`disks/midi/CUBLITE`), Spectrum 512, les devkits
vendorisés sans licence (`dev/agt`, `dev/reservoir-gods`, `gemdos/etalon`), plus
deux poids morts que seul l'historique portait : `wasm/index.data` (73 Mo, TOS +
disquettes embarqués dans l'image Emscripten) et `build/` commité par erreur en
juin. Récit complet, preuves et décisions → `CHANGELOG.md` (2026-08-30).

**Rien n'est perdu pour le développement** : les chemins purgés sont gitignorés,
chaque machine les garde LOCALEMENT, et les tests qui en dépendent se ré-arment
tout seuls quand les fichiers sont présents (vérifié : clone purgé + fichiers
restaurés = palier `fast` entièrement vert, séquenceur MIDI compris ; sans eux,
SKIP recensés). Outils : `tools/private_assets.sh` (pack/unpack du tarball privé)
et `tools/setup_devkits.sh` (re-clone GODLIB + AGT aux pins d'avant purge).
Étalons demoscene (Cuddly, No Cooper, Closure, CURLY) : GARDÉS — décision assumée,
diffusion libre par usage de la scène.

**Le séquencement en 5 pas est SOLDÉ** (1, 2, 5 le 2026-08-28 ; 3 et 4 le
2026-08-30). Pas 4 : le paquet est **100 % libre par défaut** — la purge rendait
la bascule obligatoire (la CI n'a plus les TOS), le défaut est inversé dans
`stage_free_data.sh` ET dans les 8 gardes de `release.yml`/`pi-borne.yml` ;
`NEOST_PACKAGE_NO_ATARI_TOS=0` ré-embarque des copies locales pour un paquet
personnel, jamais pour une release. Testé dans les deux sens.

✅ **GitHub Pages était un TROISIÈME canal, il est fermé** (2026-08-30) : le bundle web
servi par `habib256.github.io/neost` embarquait les deux TOS Atari depuis le
2026-08-03, et la purge ne l'avait pas fermé — elle avait seulement fait échouer le
job `wasm`, le site continuant de servir le dernier bundle réussi. Premier déploiement
réellement libre : celui de 10:03 ce jour-là, vérifié sur le site (cf. `CHANGELOG.md`).

✅ **SOLDÉ le 2026-09-02 — le § BLOQUANT n'a plus d'objet.** Les releases **0.5.2 et
0.5.4 sont SUPPRIMÉES** (`gh release delete`), leurs 17 assets avec elles. Les **tags git
restent** : la traçabilité des versions est conservée, seule la diffusion des paquets
cesse. Plus aucun canal public ne distribue de ROM Atari — historique, Pages, paquets et
releases sont tous propres. Ce qui suit est le récit de ce qui a mené là.

~~Seul reste, calé sur la 0.6~~ — ⚠ **assets des releases GitHub 0.5.2 / 0.5.4** :
leurs paquets bureau publiés contiennent `tos102uk.img` + `tos162uk.img` (vérifié
le 2026-08-30 en ouvrant le zip Windows 0.5.4) — la purge git n'y touche pas.
**Décision du mainteneur (2026-08-30) : assumé tel quel, les deux releases seront
SUPPRIMÉES quand la 0.6 sortira.** Le zip web-wasm 0.5.4 est propre (EmuTOS seul,
vérifié). ⏳ **Le tag `0.6` est posé le 2026-09-01** : la suppression est DUE dès que
les paquets 0.6 sont publiés — c'est la dernière action de ce blocage.

Conformité annexe (non bloquante) :
- `packaging/linux/make_appimage.sh` tire `linuxdeploy`/`appimagetool` depuis le tag mouvant
  `continuous` sans somme de contrôle pour arm64. Le `Dockerfile.bionic`, lui, est passé au
  tag **immuable** + SHA256 le 2026-08-30, après que son pin SHA-sur-`continuous` eut cassé
  le build tout seul (l'amont a republié le tag ; cf. `CHANGELOG.md`) — **la leçon vaut
  ici** : épingler une somme contre une cible mouvante ne protège de rien, et n'en épingler
  aucune protège encore moins. Reste à trancher pour arm64, où AppImageKit ne propose plus
  d'asset non `obsolete-*`.
- ◐ **Signature : palier 0 fait le 2026-09-01, notarisation ouverte.** Le `.app` macOS
  est désormais SCELLÉ par une signature ad-hoc (`package_macos.sh`, gardes
  `--verify --deep --strict` + « Sealed Resources ») : le `.dmg` 0.6 publié répondait
  « code object is not signed at all », d'où « NeoST est endommagé » — un cul-de-sac.
  Gatekeeper refuse toujours (pas de Developer ID), mais le refus a une sortie (clic
  droit → Ouvrir). **Reste** : Developer ID + notarisation (99 $/an), recette écrite dans
  [`docs/RELEASE.md`](docs/RELEASE.md) ; et le `.zip` Windows, non signé — mauvais rapport
  (token FIPS/HSM obligatoire depuis 2023, et SmartScreen n'est qu'un clic).

---

## 🏛 Dette d'architecture — items ouverts

Issus de l'audit quatre dimensions du 2026-08-27 et des revues antérieures. Le soldé
(A1-A8, A16-A27, les leçons de méthode) vit au `CHANGELOG.md` — la numérotation A*n*
est continue, les trous sont du travail fait.

### Reproductibilité & maturité produit (hérités)

- **A3 ◐ — Le corpus de régression n'est pas livrable — et depuis la purge du
  2026-08-30 il est LOCAL par construction** : jeux et ROM propriétaires ne sont plus
  suivis par git, chaque machine les restaure via `tools/private_assets.sh unpack`
  (§ BLOQUANT). Recompté 2026-09-02 (pose de `dmasnd_poll`) : **15 étalons
  pixel sur 19** survivent au retrait des TOS Atari (`etos_ste_boot`, `overscan_top`,
  `trace_odd`, `scroll_8264`, `scroll_8265`, `blitter_timer`, `blitter_hog`, `mfp_poll`,
  plus `cuddly_demos`, `nocooper`, `nocooper_greetings` et `closure` migrés/posés sur
  EmuTOS, `spec512_bands` — l'étalon GÉNÉRÉ qui rend la couverture « palette en cours
  de ligne » —, `freq_switch`, l'exhibiteur généré de V3, et `dmasnd_poll`, celui de la
  quantification HBL du refill FIFO son) ; les 4 restants sont le reliquat d'**A10**.
- **A9 ◐ — `main.cpp` : 5 100 → 28 lignes ; reste LA BOUCLE.** Fait le 2026-08-30
  (détail au `CHANGELOG.md`). `main()` tient en **10 lignes** — `appInit` → `appLoop`
  → `appShutdown` — et les **84 globaux `g_*` n'existent plus** : ils sont les membres
  d'une `struct App` (`src/gui/App.hpp`) qui possède aussi la SESSION (Machine, Audio,
  MIDI, réseau, écran) et les treize services que les menus déclenchent (ex-lambdas de
  `main()` : `applyConfig`, `midiOutApply`, `switchKioskMode`…). Le frontend vit
  désormais en **onze modules** de `src/gui/` (Configuration, menu borne, fenêtres de
  debug, écran ST, callbacks GLFW, ancrage, CRT, manettes), chacun recevant `App&` en
  paramètre : la discipline de requêtes de `MediaPages` est généralisée, une page ne
  fait rien, elle pose une requête que la boucle consomme.
  **Reste : `appLoop` — 1909 lignes d'un seul tenant** (`src/gui/AppLoop.cpp` ; 1813
  au 2026-08-30, +96 le 2026-09-02 pour les deux instruments d'affichage —
  `--shot-window` et `NEOST_WBAND_DIAG`, cf. CHANGELOG). Elle a
  été DÉPLACÉE, pas découpée, et c'est délibéré : le garde-fou du plan interdit de
  combiner deux refontes, et celle-ci a deux verrous propres — une vingtaine de
  variables de trame partagées entre les phases (`fbw/fbh`, `cTop/cH/cW`, `menuH`,
  `reqMount*`, `cfgUi`…) et un corps qui traverse des blocs `#if defined(NEOST_WITH_IMGUI)`
  de plusieurs centaines de lignes, `#else` compris. Prochain pas concret : nommer ces
  variables de trame dans une `struct Frame` (le même geste qu'`App`, à l'échelle du
  tour), PUIS couper aux frontières déjà commentées (entrées / trames dues / dessin /
  requêtes / présentation).
  ⚠ Ce qui a rendu le chantier faisable sans filet de test neuf : chaque déplacement
  était MÉCANIQUE (aucun corps réécrit — les fonctions extraites reçoivent un paramètre
  nommé `A`, et chacune ouvre sur des alias `Machine& machine = *A.machine;` qui rendent
  le corps déplacé identique au caractère près). Le compilateur a donc attrapé ce qu'un
  test n'aurait pas vu, et les paliers `fast` puis `full` ont gardé le reste.

- **A10 ◐ — Étalons adossés à des ROM propriétaires : 3 migrés, 4 restants.**
  Migrés sur `etos192fr` le 2026-08-28, référence commise INCHANGÉE (0 px / 114816,
  crop `buffer_noled`) : `cuddly_demos` (--frames 3500 → 3655), `nocooper` (6802 → 6932),
  `nocooper_greetings` (29500 → 29700). Ces démos bootent depuis le disque : le TOS ne
  fait que les charger, SEULE la durée du boot change. Recette (à réutiliser) — balayer
  les trames à **pas 1** autour de la cible et retenir CELLE qui est à 0 px, jamais la
  moins pire ; sur `cuddly_demos` la trame voisine est déjà à 7 548 px, sur `nocooper` à
  19 069 px. Détail et preuves dans le `rom_note` de chaque entrée d'`etalons.json`.
  Restent :
  - ✅ **La couverture spec512 est RENDUE, sans ROM Atari** (2026-08-28) :
    `spec512_bands`, étalon **généré** (`tools/make_spec512_test.py`) — secteur de boot
    autonome qui martèle `palette[1]` trois fois par ~100 cycles sur un écran entièrement
    à l'index 1. La position horizontale de chaque bascule dépend du cycle exact de
    l'écriture : c'est le seul endroit du rendu où un cycle de CPU se voit à l'œil.
    **0 px contre l'oracle Hatari** sur ROM libre. Les trois étalons ci-dessous restent
    donc des SKIP recensés le jour de la purge, mais plus rien d'essentiel ne part avec eux.
  - **`spectrum512_diapo`, `spectrum512_diapo2`, `spectrum512_diapo_ste` — migration
    EmuTOS RÉFUTÉE, ne pas retenter.** Ce disque n'a pas de secteur de boot exécutable
    (somme $FB35) : la diapo est lancée par le dossier `AUTO`, et sous EmuTOS le
    programme démarre puis abandonne (bureau figé dès la trame 600). Ce n'est pas un bug
    NeoST — **l'oracle Hatari + EmuTOS rend le même bureau** (écart entièrement dans la
    bande de la LED disquette d'Hatari) ; `etos256fr` échoue pareil. Seule voie restante :
    l'**étalon généré** (esprit `tools/make_overscan_test.py`) — un secteur de boot
    autonome qui écrit la palette en cours de ligne, calé à l'oracle.
  - **`union_demo`** : disque absent du dépôt (fetch planetemu manuel), donc non testable
    — appliquer la recette ci-dessus le jour où il revient.
  - **Prix de la migration, mesuré au repos** : le palier pixel passe de **46 s à 50 s**
    (`run_etalons.py` complet, 2 runs de chaque côté en alternance). Le mur reste
    `nocooper_greetings` : **41,3 → 45,1 s**, dont 0,7 % de trames ajoutées et ~8 % de
    cœur — NeoST émule cette démo un peu plus lentement sous EmuTOS, à image identique.
    ⚠ Mesuré d'abord SOUS CHARGE, ces mêmes écarts sortaient à 46→90 s et 43→63 s : une
    durée sans description de la charge n'est pas une mesure (leçon du 2026-08-25,
    re-jouée).
  - Bonus NON acquis : raccourcir `nocooper_greetings` (il borne à lui seul le mur du
    palier pixel). Trois tentatives mesurées le 2026-08-28 — espaces resserrés à 2 000
    puis à 600 trames d'intervalle, puis AUCUN espace : l'écran greetings n'est jamais
    atteint (au mieux 24 508 px). Et décaler les 5 espaces de +141 trames ne change rien
    à l'arrivée (greetings toujours à 29 610) : la durée de la dernière partie ne dépend
    pas d'eux. La démo joue ses parties à son rythme (la trame change sans touche à
    2 000, 2 800, 3 700…) — un espace anticipé n'est pas pris. Trancher demande de savoir
    QUAND la démo relit le clavier, pas de re-tirer au hasard un calendrier.
- ✅ **A11 — SOLDÉ le 2026-09-01 : l'oracle tourne en CI, et le triangle est fermé.**
  `.github/workflows/oracle.yml` — **hebdomadaire** (lundi 04:00 UTC) + `workflow_dispatch`,
  jamais au push : il clone et bâtit Hatari **au pin** (`tools/setup_hatari.sh`, cache CI
  dont la clé PORTE le pin, donc un changement de pin invalide le cache tout seul),
  puis lance le mode ajouté pour ce chantier :
  ```sh
  python3 tools/run_etalons.py --oracle-check      # régénère et CONFRONTE, sans écraser
  ```
  `--oracle` **écrase** les références : il sert à en POSER une, jamais à en contrôler une
  — il efface la preuve qu'il devrait comparer. `--oracle-check` rejoue Hatari, retient
  (via `oracle_scan`) l'image identique à la capture NeoST du jour, puis la compare à la
  référence COMMISE sans jamais écrire dans `tests/reference/`. Trois choses sont donc
  vraies quand il passe : **NeoST == référence commise == Hatari (aujourd'hui, au pin)**.
  Ce qu'il attrape et qu'aucun autre palier ne voit : une référence régénérée contre un
  oracle non épinglé, un pin déplacé sans repose des références, un ffmpeg dont le décodage
  bouge. Garde vérifiée par mutation : **1 pixel** modifié dans une référence commise
  → échec. Mesuré au repos : **5 min 31** pour les 8 étalons re-dérivables.
  **Ce que la première exécution a trouvé, et qui n'était écrit nulle part** — deux
  références oracle n'étaient pas RE-DÉRIVABLES, chacune pour une raison propre. Le manifeste
  le déclare (`oracle_check: false` + `oracle_check_note`, la raison est OBLIGATOIRE — sans
  elle le script refuse de tourner) et le journal les NOMME :
  - ✅ **`nocooper` — RÉGLÉ le même jour.** Son oracle exige une touche **TENUE** (espace,
    vbl ~900), et `hatari_oracle.sh` sait désormais la tenir **à la VBL près** :
    `oracle_keys: [[900, 960, 57]]` → point d'arrêt `VBL = N` du débogueur (Hatari se gèle
    sur stdin) + `hatari-event keydown/keyup` par la fifo de contrôle + `c`. Sans attente
    horloge, et **le fast-forward survit** (562,9 VBL/s avec fifo, 565,0 sans — la doc
    disait l'inverse). `oracle_fastfdc` ajouté (il manquait, timelines désalignées).
    Re-dérivé et confronté : trame 6929 retenue (décalage +139), **identique à la capture
    NeoST et à la référence commise**. Deux pièges d'outillage réglés en chemin, qui
    menaçaient TOUS les oracles : la LED disquette incrustée (`--drive-led off`, zone noire
    vérifiée) et le compteur `n` de `select` que ffmpeg **remet à zéro** à chaque changement
    de format `pal8↔rgb24` de l'AVI (`-reinit_filter 0` sur les deux extractions).
  - ✅ **`spec512_bands` — RÉGLÉ le même jour aussi.** Hatari ne se reproduisait pas
    lui-même dessus (deux runs → deux jeux de phases disjoints, 2 460 px au mieux) : le RNG
    de boot décale le démarrage du programme, et la resynchro par scrutation de `$FF8207` ne
    se recale qu'à ~20 cycles près — `oracle_scan` n'y peut rien, le décalage est sous-trame.
    Remède dans le GÉNÉRATEUR : séquence dans le handler de VBL, attente en **`stop #$2300`**
    (latence d'exception FIXE — une boucle `bra.s` prendrait l'interruption à une frontière
    d'instruction, jusqu'à 10 cycles de jitter). Mesuré : 2 runs Hatari → mêmes 3 phases ;
    NeoST rend ces mêmes 3 images (matrice 3×3, zéro sur la diagonale). L'image cycle sur
    5 trames (+4 cyc/trame au démarrage du handler), identiquement chez Hatari : c'est le
    programme, et `oracle_scan` retient la trame identique. Référence régénérée (trame
    nominale, décalage 0), run frais identique.
  **Périmètre réel en CI : 8 étalons** (blitter_hog, cuddly_demos, **mfp_poll**, nocooper,
  scroll_8264, scroll_8265, spec512_bands, trace_odd) — les 3 `spectrum512_diapo*` dépendent d'une
  ROM Atari que le dépôt ne porte plus. Le décompte et les noms sont imprimés à chaque exécution.
  `mfp_poll` a rejoint le corpus le 2026-09-02 (promu snapshot → oracle, cf. § Divergences n°3) ;
  il est le plus ROBUSTE des huit : son programme finit sur `bra.s *`, donc l'image est figée et
  ne dépend pas de la durée de boot — il n'a pas besoin d'`oracle_scan`.
- ✅ **A41 — SOLDÉ le 2026-09-01 : les 27 px de `closure` sont à HATARI, NeoST est fidèle.**
  L'écart tenait sur la seule ligne 0 (première ligne affichée, `sl=34`, celle qu'ouvre le
  retrait de bordure haute), chaque pixel fautif portant une couleur voisine d'un cran.
  **Mesuré** en instrumentant `Spec512_StartFrame` dans l'arbre gitignoré (sonde révoquée,
  binaire rebâti au pin ensuite) : l'amorce de palette d'Hatari pour cette ligne est
  `000 100 200 210 310 310 320 420 430 531 442 541 552 652 652 763` — **exactement, sur les
  16 registres**, le bloc que la démo écrit aux **cycles 438-508 de cette même ligne 34**,
  donc 380 cycles APRÈS les pixels qu'il colore. Un faisceau au cycle 56 ne peut pas afficher
  une couleur écrite au cycle 446 : la palette causale de NeoST est la bonne. Cause :
  `nScanLine += OVERSCAN_TOP` sous `V_OVERSCAN_NO_TOP` (`spec512.c:233`) saute les
  `CyclePalettes` des scanlines 0 à 28 — où vit le bloc d'init de la démo — et l'amorce
  retombe sur `pHBLPalettes[]`. Verdicts : `docs/CASE_STUDIES.md`,
  `docs/HATARI_DIVERGENCES.md` § *Cas où NeoST améliore Hatari*.
  ⚠ **`closure` reste `ref_kind: snapshot` DÉFINITIVEMENT** — le passer en `oracle`
  installerait l'anticipation d'Hatari comme référence. Le minimum a été CHERCHÉ avant de
  conclure : 70 trames NeoST voisines comparées à l'oracle commis, toutes à 27 ou 43 px,
  jamais 0 — ce n'était donc pas un défaut d'alignement de capture.
  📌 Leçon d'outillage : la palette d'init de cette démo **change à chaque trame**, donc une
  trace `--trace video_color` d'Hatari doit être armée sur LA MÊME trame que la capture
  NeoST (`--parse` + `b VBL = N :once :file …` ; l'égalité s'écrit `=`, pas `==` — le parseur
  de breakcond scinde `==` et échoue). Comparer deux trames différentes envoie droit sur une
  fausse piste, ce qui est arrivé ici avant de recouper.
- **A12 ◐ — Le registre existe et macOS est passé ; 4 cibles sur 5 restent.**
  Ouvert le 2026-09-01 : [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md) tient une
  ligne PAR CIBLE avec la config de la machine, et un protocole en cinq pas (intégrité,
  contenu, chaîne de confiance, exécution, débit) pour que chaque passe soit une recette
  et non une improvisation. L'outil manquant est posé : `run_perfbench.py --budget` rend
  le **facteur temps réel absolu** (trames/s ÷ balayage annoncé par la machine émulée)
  là où les ratios, machine-indépendants par construction, ne pouvaient pas répondre ;
  `NEOST_HEADLESS=…` le pointe sur le binaire LIVRÉ. ⚠ Il ne doit JAMAIS entrer dans un
  palier de test — un seuil absolu sur un runner de CI est le piège que l'en-tête de
  l'outil décrit.
  **macOS arm64 : passé le 2026-09-01** sur le `.dmg` 0.6 publié (M1, 8 Gio) — somme
  conforme, contenu 100 % libre VÉRIFIÉ sur le paquet servi, scellement du palier 0
  confirmé (`syspolicy_check` ne relève plus que *Notary Ticket Missing*), binaire livré
  à **×26,9 temps réel** au pire, soit rien de mesurable face au build natif (×26,4).
  **Ce que la passe a trouvé** : le paquet ne pouvait JAMAIS enregistrer sa configuration
  chez un premier utilisateur — bundle en lecture seule → règle A36 sur
  `~/.config/neost/`, dossier que rien ne créait. Corrigé et revérifié dans les
  conditions du défaut (détail au `CHANGELOG.md`).
  **Les pas 1 et 2 sont soldés sur les HUIT paquets** (ils ne demandent aucun matériel) :
  sommes 8/8, et **aucune ROM Atari nulle part** — la promesse « 100 % libre » passe du
  script qui fabrique aux artefacts réellement servis. `tools/appimage_ls.py` rend les
  quatre AppImage inspectables sans `unsquashfs`. Ce passage a trouvé une
  **non-conformité GPL vivante** : le paquet web ne portait aucune licence, alors qu'il
  est distribué deux fois (zip de release + GitHub Pages) — corrigé, huit jobs gardent
  désormais les licences (ils étaient sept, là où la doc en annonçait huit).
  **Ce qui se tranche sans la cible est tranché** : Windows n'a **aucune DLL manquante**
  (table d'importation PE = DLL système seulement, build MinGW statique), et le plancher
  glibc du Pi était **déjà gardé** en CI. Constat associé : **six jobs** lancent
  `smoke_package.sh` sur le paquet — ce qui manque n'est pas « ça ne démarre jamais »
  mais l'INTERFACE, le MATÉRIEL et le CHEMIN D'INSTALLATION.
  ⚠ **Trou structurel mis au jour** : les six smoke-tests tournent sur un dossier
  EXTRAIT donc inscriptible ; le support de livraison réel (monté en lecture seule) n'est
  jamais exercé — c'est ce qui a laissé passer le défaut de configuration, lequel touche
  en réalité **5 paquets sur 8** (le `.dmg` mesuré, les 4 AppImage déduits). Garde posée
  dans `neost-selftest` et vérifiée par mutation (2 FAIL sans le correctif).
  **Restent** : le pas visuel de macOS (ouvrir le `.app` dans une vraie session
  graphique — l'automatisation n'obtient pas de fenêtre, et le build de dev se comporte
  à l'identique, donc rien n'incrimine le paquet) ; **Windows** jamais lancé hors CI ;
  **APK** jamais posé sur un appareil (QEMU seul — un émulateur ne solderait pas la
  case) ; **Raspberry Pi**, la cible qui motive le chantier, toujours sans budget mesuré ;
  **Linux** non consigné. Autrement dit : les pas 3, 4 et 5 pour quatre cibles sur cinq,
  et eux exigent du matériel.
- **A13** = save-states × GEMDOS HD → § *Périphériques & profils machine*.
- **A15** = DSL d'injection sans token « mouvement bouton tenu » (pas de DRAG GEM).

### Consolidation

*(vide — A16b, A28, A29 et A30 sont soldés le 2026-08-28 ; détail au `CHANGELOG.md`.)*

### Chantiers structurels (UN à la fois, jamais combinés)

- ✅ **A42 — SOLDÉ le 2026-09-03 : le flush d'IPL passe du CALLBACK au LOT DE DISPATCH.**
  Ouvert le 2026-09-02, suite directe du port partiel de `MFP_UpdateNeeded`
  (cf. `CHANGELOG.md` et `docs/HATARI_DIVERGENCES.md` § MFP).
  **Le problème, en une phrase** : Hatari élit l'interruption MFP **une fois par
  instruction CPU** (`if (MFP_UpdateNeeded) MFP_UpdateIRQ_All(0)`, newcpu.c:3005 et 5509),
  NeoST l'élit **une fois par callback d'ordonnanceur**, parce que `Machine` fait suivre
  chacun d'eux d'un `cpu.updateIpl()` — **18 sites**. Deux timers MFP échus dans le même
  `runTo` sont donc élus séparément, et la règle « seules les plus anciennes concourent »
  (`pendingTime_ <= pendingTimeMin_`) ne peut pas les départager.
  **Ce qui est DÉJÀ fait** : `raiseAt` n'élit plus, il arme `irqUpdateNeeded_` ;
  `flushIrqUpdate()` élit au calcul d'IPL et en fin de `Mfp::updateTimers`. Les entrées
  multiples d'une MÊME fonction (`TXERR`→`TXEMPTY`, `RXERR`→`RXFULL`) sont correctes.
  Verrou d'A/B : `NEOST_MFP_BATCH=0`.
  **Ce qui a été fait** : `Scheduler::setDispatchHooks` borne le LOT (garde RAII,
  compteur de profondeur — `runTo` est ré-entrant via `addStolenCycles`) ; `Machine` y
  suspend l'élection MFP pendant le lot et la tranche UNE FOIS à la fin, avant de relire
  l'IPL. Les `cpu.updateIpl()` des callbacks sont LAISSÉS EN PLACE : le CPU est arrêté
  pendant le lot, Moira ne relit sa broche qu'à une frontière d'instruction, donc les
  recalculs intermédiaires n'ont aucun effet observable — les retirer aurait été une
  seconde refonte pour rien. Verrou d'A/B : `NEOST_IPL_BATCH=0`.
  **Ce que ça change, mesuré** : le groupement passe de **0** à **123** entrées groupées
  sur Super Hang-On (1 000 123 entrées) et **1 240** sur le bureau EmuTOS (17,6 M), soit
  ≈ 1 entrée sur 10 000 — « rarissime », mais non nul, et désormais servi dans le bon
  ordre. Exhibiteur posé comme prescrit : 4 cas de table de vérité dans `mfp-selftest`
  (« la plus ancienne gagne », ordre des entrées indifférent, à date égale la plus
  prioritaire, plus ancienne ET prioritaire) — vérifiés par mutation, `NEOST_MFP_BATCH=0`
  fait échouer le premier (`got=13` Timer A au lieu de `want=4` Timer D).
  **Effet de bord assumé** : `dmasnd_poll` (snapshot, VBL armée) se décale de 288 px —
  structurellement NUL (mêmes valeurs distinctes, même histogramme de deltas, une position
  à 2 octets près). Référence re-posée, raison écrite dans son `ref_note`.
  📌 **Les deux pièges annoncés, et ce qu'ils ont donné.**
  1. **Aucun exhibiteur connu.** Mesuré sur Super Hang-On : **0 groupement sur 1 000 000
     d'entrées**, et l'A/B rend 0 px sur Super Hang-On, `mfp_poll`, `blitter_timer` et
     `trace_odd`. Hatari cite « Fuzion CD Menus 77, 78, 84 » — disques absents du dépôt.
     Exhibiteur posé — mais en TABLE DE VÉRITÉ (`mfp-selftest`) plutôt qu'en étalon pixel :
     la règle se teste sur la puce nue en quatre lignes, là où un programme ST aurait
     demandé deux handlers, un journal et un rendu, pour une observation moins directe.
  2. **`mfp_poll` NE PEUT PAS arbitrer ce chantier** : son programme masque les IRQ
     (`SR=$2700`), il est aveugle à toute la datation et à la livraison des interruptions —
     vérifié, `NEOST_MFP_WRITE_END=40` le laisse à 0 px. C'est précisément cet étalon qui a
     fait rejeter `MFP_UpdateTimers` à tort le matin du 2026-09-02.
  **Filet minimum** : la cartouche diagnostic (verdicts série `cpu`/`timing`/`frame`/`ipl`),
  la suite Q MegaSTE et les 25 étalons pixel — tous verts aujourd'hui avec le port partiel.

- **A32 ◐ — Découper `Shifter` : le fichier et les NOMS sont faits, les CLASSES
  restent.** Fait le 2026-08-28 (détail au `CHANGELOG.md`) : `Shifter.cpp` passe de
  **2 917 à 1 331 lignes**, la machine à états des bordures vit dans `VideoGlue.cpp`
  (1 032 l.) et le compteur vidéo dans `VideoCounter.cpp` (441 l.) ; le vrai GLUE
  vidéo a son en-tête (`VideoGlue.hpp`) et le stub MMU son vrai nom (`MmuGlue.hpp`) ;
  les **4 `const_cast`** de `videoCounter()` ont disparu (la méthode n'est plus
  `const` — elle avançait la machine Glue).
  **Ce qui reste, et pourquoi ça n'a pas été fait d'un bloc** : les trois rôles sont
  toujours des MÉTHODES DE `Shifter`, parce qu'ils partagent son état par-ligne
  (`glueLines_`, `glueLineStart_`, `liveGlue*`, `vc*`). En faire trois classes
  demande d'abord de trancher **qui possède cet état** — et une réponse bâclée y
  ajouterait des accesseurs croisés, c'est-à-dire le même couplage avec plus de
  cérémonie. Prochain pas concret : extraire `VideoCounter` en objet membre (ses
  champs `vc*` sont les moins partagés — `vcFrameBase_`, `vcLineBase_`, `vcLineY_`,
  `vcRestart*`), mesurer ce que ça casse, et seulement ensuite regarder la Glue.
- **A33 ◐ — Mono-instance CPU LEVÉ ; l'état est par instance, le parallélisme reste.**
  Fait le 2026-08-28 (détail au `CHANGELOG.md`) : le
  `throw std::logic_error("Cpu68k supports only one live instance")` a disparu, les
  **25 globaux d'état** sont devenus une `struct CpuState` possédée par `Cpu68k`, et
  ses **135 accès internes** passent par `state_` (son propre état) et non plus par le
  pointeur d'instance active. Prouvé, pas supposé : `selftest_logic.cpp` construit
  DEUX `Cpu68k` sur deux `Bus`, vérifie que chacun prend le vecteur de reset de SON
  bus, et qu'en faire tourner un ne bouge ni l'horloge ni le PC de l'autre. Les
  **23 verrous de configuration de processus** restent globaux — les rendre
  par-instance serait faux (cf. `tools/env_locks.json`).
  **Reste** : le vrai PARALLÉLISME. Le modèle est « à tour de rôle » — les callbacks
  Moira et les fonctions libres du fichier n'ont pas de `this` et passent par `g_cur`,
  posé à l'entrée de `run()`/`reset()`. Deux CPU dans DEUX THREADS demanderaient de
  supprimer ces 94 accès restants (passer le contexte aux callbacks, ou un
  `thread_local`) — ce n'est pas ce qu'A33 promettait, et aucune fonctionnalité ne le
  réclame aujourd'hui.
- **A35 ◐ — Le fork Moira : le pin est RETROUVÉ, le `Cputester` reste à évaluer.**
  Fait le 2026-08-28 : `extern/moira/NEOST_VENDOR.md` porte désormais le **pin de
  départ** — `1efd69467ca13b27b2fb40febd5cb31dbecdea5f`, l'amont
  `dirkwhoffmann/Moira` au premier commit d'intégration (2026-06-01) — retrouvé dans
  l'historique NeoST lui-même : le gitlink du sous-module y est encore, et la commande
  qui le déterre est écrite dans le fichier. Le tableau donne aussi les trois commits
  du fork local disparu, dont le tip (`a1e52ec`) dont le CONTENU est le code vendorisé.
  La recette de rebase et la commande de `diff` contre l'amont sont écrites.
  **Reste** : (1) vérifier d'un `git ls-remote` que `1efd6946` existe toujours chez
  l'amont — non fait, aucun accès réseau utilisé pour établir ce qui précède ; (2) le
  `Cputester`. Sa réintégration DANS le dépôt est écartée (≈713 Mo de corpus ADF, dans
  un dépôt public qu'on cherche justement à alléger) : la voie est la même que pour
  l'oracle Hatari — clone hors arbre, gitignoré, recette documentée. Ses dépendances
  propres n'ont pas été évaluées.
- **A37 ◐ — Discipline de release : la procédure est écrite et gardée, le TAG est
  une décision de mainteneur.** Fait le 2026-08-28 : [`docs/RELEASE.md`](docs/RELEASE.md)
  écrit la procédure en sept pas (dont le piège du cache `NEOST_VERSION_STR`, celui
  qu'on saute) ; `tools/check_release.py` (palier `fast`) exige que les TROIS numéros
  disent la même chose — `CMakeLists`, « Version courante » du CHANGELOG, dernière
  en-tête de release — et refuse un numéro sauté en silence ; le saut de **0.5.3** est
  désormais consigné au `CHANGELOG.md` (§ *Numéros de version sautés*) avec ce qu'on
  sait : elle n'a jamais existé, le bump `dec5929` est passé de 0.5.2 à 0.5.4, et la
  raison n'est pas reconstituable.
  ✅ **Le tag est POSÉ : `0.6`, le 2026-09-01** — 192 commits depuis la 0.5.4 du
  2026-08-23 (MegaSTE 12/12, station MIDI sur les trois plateformes, CAB/theoldnet,
  l'audit et le plan A16-A42, la purge complète). Procédure suivie en sept pas, palier
  `full` vert sans une seule étape sautée, `--version` vérifié à 0.6 après
  reconfiguration avec `-DNEOST_VERSION_STR=0.6`.
  **Reste, décisions du mainteneur** : (1) **notariser** le `.dmg` (99 $/an) — le palier
  gratuit est fait le même jour, cf. § *Conformité annexe* ci-dessus ; (2) **supprimer les
  releases GitHub 0.5.2 et 0.5.4** dès que les paquets 0.6 sont publiés (§ BLOQUANT
  ci-dessus — leurs assets contiennent des ROM Atari).
  ⚠ Précédent posé le 2026-09-01 : l'asset macOS de la 0.6 a été **remplacé après
  publication** (paquet signé ad-hoc, `SHA256SUMS.txt` refait, note d'addendum dans la
  Release). Remplacer un asset publié se dit, sans quoi une somme qui change passe pour
  une compromission.

### Issus de l'évaluation d'architecture du 2026-08-28

- **A38 ⭘ — Le palier `fast` lance 17 outils : un garde-fou qui ne mord jamais devient
  du bruit.** Quatre ont été ajoutés le 2026-08-28 seul (`check_licenses`,
  `check_env_locks`, `check_release`, le fuzz des parseurs). Chacun se justifie et
  chacun a attrapé quelque chose de réel *le jour de sa pose* — mais rien ne le
  vérifiera plus jamais. Le risque n'est pas le coût (~12 s) : c'est qu'un contrôle
  cesse d'attraper quoi que ce soit, que personne ne le sache, et qu'on finisse par
  désarmer le lot en bloc.
  À faire : (1) tenir, pour chaque contrôle, la **trace de son dernier
  déclenchement RÉEL** — un fil-piège volontaire ne compte pas ; (2) décider d'une
  règle de retrait (un contrôle qui n'a rien attrapé en N mois se justifie à nouveau ou
  s'en va) ; (3) re-mesurer le mur du palier `fast` à chaque ajout, et le dire.
  ⚠ Ne PAS traiter en ajoutant un 18ᵉ outil qui surveille les 17 autres.

- **A39 ◐ — Trois modules importants n'ont JAMAIS été audités ; `GemdosHd` l'a été.**
  Premier passage fait le 2026-08-28 sur `io/GemdosHd.cpp` (détail au `CHANGELOG.md`) :
  chaque opération sur le système de fichiers hôte a été remontée jusqu'à la
  provenance de son chemin (**toutes** passent par `createHostFileName` →
  `clampToSandbox`), les contrôles mémoire à taille variable ont été vérifiés contre
  le débordement, et le bac à sable a reçu **son premier test** (13 assertions,
  `--gemdos-selftest`, palier `fast`). Verdict : le module est **plus solide que sa
  réputation** — durci à plusieurs reprises, chaque durcissement portant le récit de
  l'évasion qu'il ferme. Ce qui manquait n'était pas la robustesse mais la GARDE.
  **Restent** :
  - ✅ `io/Ikbd.cpp` (1 189 l.) **audité et pincé le 2026-08-29** : sa table de
    longueurs de commande est comparée ligne à ligne à Hatari `KeyboardCommands[]`
    (39 opcodes), et le protocole a sa table de vérité — accumulation multi-octets,
    opcode inconnu = NOP, PAUSE OUTPUT levée par TOUTE commande valide, forme des
    paquets `$FD`/`$F7`. Une seule anomalie trouvée, d'étiquette : `$19` était
    commenté « SetJoystickFireDuration » (le nom que Hatari donne à `$18`) alors que
    c'est le mode keycode manette. **Restent** dans ce module : les modes souris
    absolus sous mise à l'échelle, l'horloge, et le code 6301 custom (`Execute`) ;
  - la pile réseau (`src/net`, ~1 030 lignes) — pas d'audit de lecture, mais elle
    est la MOINS démunie des trois : `--slirp-selftest`, `enec_selftest`,
    `netusbee_selftest` et un job de CI dédié la couvrent déjà par le comportement ;
  - dans `GemdosHd` : la table de handles et le suivi Pexec (c'est **A13**), les codes
    d'erreur face à `gemdos.c`, et le comportement de `Fsfirst`/DTA sous réutilisation
    d'index — non couverts par le test du bac à sable ;
  - **un test par COUCHE** du bac à sable. Le test posé garantit la PROPRIÉTÉ (rien ne
    sort) et pas les couches : mesuré par mutation, il faut retirer LES DEUX défenses
    pour le faire rougir. Il ne signalera donc pas la perte silencieuse d'une seule.
    Le corriger demande de tester des états intermédiaires, donc de coupler le test à
    l'implémentation : à décider, pas à bâcler.

- ⚠ **Et le GUI n'a jamais été exercé INTERACTIVEMENT dans une passe de validation.**
  Il est jugé sur sa forme (A9 : 4 814 lignes, `main()` 2 430, 82 globaux) et couvert
  par une capture au boot plus trois assertions d'arguments — jamais par un usage.
  C'est la même famille qu'**A12** (aucune cible de livraison validée à la main) et ça
  se traite avec : une passe d'usage réel, consignée.

### Garde-fous du plan (à NE PAS faire)

- **Croire que le plan se vide** : l'audit du 2026-08-27 a produit 22 items ; la journée
  du 2026-08-28 en a soldé 4 entièrement et avancé 5 — et en a ajouté 2 (A38, A39). La
  méthode **VOIT mieux qu'elle ne RÉPARE**, et c'est structurel : auditer coûte une
  journée, réparer coûte des semaines. Ce n'est pas un défaut tant qu'on le sait ;
  ça le devient si l'on planifie comme si la liste allait se refermer toute seule.
  Arbitrer explicitement entre « ouvrir » et « fermer » à chaque session.

- **Rouvrir BL5 sans concevoir une 3ᵉ mesure indépendante** : le paradoxe de signe entre
  les deux instrumentations existantes est documenté (`docs/HATARI_DIVERGENCES.md` § BL5,
  6 hypothèses réfutées) — re-mesurer avec les mêmes sondes ne tranchera rien.
- **Combiner A9 + A31 + A32 en un « grand refactor »** : chaque chantier structurel
  séparément, filet de test posé AVANT (le boot GUI l'est ; A29 pour le cœur).
- ~~Supprimer un des deux modèles d'exécution sans la mesure d'A34.~~ **La mesure a
  été prise le 2026-08-28 et le perdant est supprimé** (cf. `CHANGELOG.md`) : il n'y
  a plus qu'un modèle, le BLOC. Le garde-fou reste utile comme précédent — on ne
  supprime pas une branche d'exécution sur une intuition.

---

## Catalogue logiciels — bugs OUVERTS

Rapports terrain non expliqués. TOS 1.02fr sauf mention. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff
Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari — le pilotage **joystick** de l'oracle est possible (recette A5 → `docs/HATARI_AUTOMATION.md`) ; égaliser la durée d'appui (`--key-hold`). |

Suivis mineurs laissés ouverts sur des cas par ailleurs tranchés :
- **Lethal Xcess** — titre « buggé à ~8 % » constaté en GUI (2026-07-02), probablement la
  même calibration `$8209` que l'in-game déjà réparé ; à re-vérifier en GUI.
- **Stardust STE** — résidu non élucidé du dossier D-PSG (clos) : l'oracle affichait « INSERT
  DISK 2 IN ANY DRIVE » là où NeoST fond au noir puis poll le lecteur B ; à revoir si les
  disquettes 2/3 (absentes du dépôt) réapparaissent.

> ⚠ **Avant de déclarer un bug : vérifier la RAM, puis la ROM.** Le réflexe et les cas
> qu'il a tranchés → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md). Les « déjà
> expliqués » (Captain Blood, Enchanted Land, Cuddly, Rick Dangerous II, Stardust,
> Spectrum 512 STE, Blood Money, Arkanoid, Wings of Death…) y sont — ne pas rouvrir.

---

## 🔬 Divergences Hatari & précision cycle — restes

**Inventaire maître** (sévérité + impact + `fichier:ligne` des deux côtés) :
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md). **Aucune divergence de
sévérité haute n'est ouverte** (vérifié entrée par entrée le 2026-08-27) ; la
convergence instruction Moira↔WinUAE est complète et le beam-sync joueur est **clos**
(→ « ÉTAT COURANT » de `docs/MOIRA_WINUAE_CONVERGENCE.md`). Le restant, à rendement
décroissant, par priorité d'impact :

1. ✅ **[VIDÉO] V3 — CLOS le 2026-09-01 : l'attribution de ligne à la grille RÉELLE est le
   défaut, prouvée à l'oracle.** Le « vert avec le verrou armé » ne prouvait rien, et pour une
   raison qu'on ne soupçonnait pas : **le verrou ne faisait rien** — sous `NEOST_LINELEN_ATTR`
   la longueur de ligne retombait à 512 à chaque ligne et n'était corrigée que par un
   `Freq_match` tombant SUR la ligne ; 140 lignes de 60 Hz sans écriture restaient à 512, la
   grille réelle ne dérivait jamais. Exhibiteur construit exprès (`tools/make_freqswitch_test.py`
   → étalon **`freq_switch`**, généré, ROM libre, ancré sur la VBL) : plage de 140 lignes 60 Hz
   (dérive 560 cyc) puis 8 bascules alternées. Correctif : longueur de base posée par l'état
   freq/res au début de ligne (`glueLineLenFor`, ≙ `Video_StartHBL` → `nCyclesPerLine`), les
   `Freq_match` la raffinent. **Mesure décisive au niveau de la Glue** — trace `video_sync`
   d'Hatari contre `[attr]` (`NEOST_VARLINE_TRACE`) : les **18 écritures** de la trame
   attribuées à la même ligne ET au même cycle (`169/336 174/268 … 242/508 247/468`), là où
   la grille fixe en manque 17 sur 18, d'une à deux lignes. Palier pixel entier à **0 px** avec
   le canal armé (rien d'existant ne bouge — seul l'exhibiteur le voit, c'est ce qu'il fallait) ;
   `NEOST_LINELEN_ATTR=0` fait rougir `freq_switch` à 16 408 px (garde vérifiée par mutation) ;
   `glue_selftest_attr` garde désormais la position DÉSARMÉE pour que l'A/B reste exécutable.
   ⚠ Le pixel n'était PAS juge ici : les lignes 60 Hz d'une trame 50 Hz sortent chez Hatari
   avec l'artefact « left+2 » tranché en A40 (toute la ligne à l'index 8) — 56 % d'écart image
   qui ne dit rien de l'attribution ; d'où `ref_kind: snapshot`, preuve consignée au `ref_note`.
2. ✅ **[SON] CLOS le 2026-09-02 — confronté, et l'oracle est le PERDANT.** L'exhibiteur manquait :
   `tools/make_dmasnd_poll_test.py` → étalon **`dmasnd_poll`** (généré, ROM libre, STE), 100 tours
   d'un poll de `$FF890B/0D` pendant que le DMA joue à 50066 Hz stéréo. Il contraint ce qu'aucun
   test ne touchait : le compteur ne doit **avancer qu'au HBL** (39 deltas nuls sur 99, puis des
   sauts de 6 et de 8) — le WAV, lui, mesure ce que le DAC CONSOMME, pas la date du FETCH.
   **Verdict : NeoST rend le découpage IDÉAL implanté par le débit** (6,398 o/ligne ⇒ 19,9 % de
   sauts de 8 ; mesuré 20,3 %), là où Hatari jitte sur 4/6/8/12 — sa consommation DAC passant par
   le rééchantillonnage vers le taux hôte. Le débit MOYEN est identique des deux côtés
   (**382 octets exactement** sur la fenêtre) : c'est bien la granularité, pas le débit.
   ⚠ **Hatari ne se reproduit pas lui-même dessus** — 664 à 1432 px entre deux runs identiques,
   `--sound off` comme `--sound 50066`, l'ancrage VBL de `spec512_bands` n'y changeant rien (il
   fixe la phase du programme, pas celle du resampler, dont l'accumulateur court depuis le
   démarrage de l'émulateur). **Aucun oracle n'est dérivable sur ce chemin** : `ref_kind: snapshot`,
   premier étalon du corpus refusé à l'oracle pour non-reproductibilité d'HATARI. Garde vérifiée
   par mutation : réintroduire le `fifoRefill()` de `DmaSound::liveCounter` rend 1592 px et une
   RAMPE CONTINUE au lieu du palier-saut. Détail → `docs/HATARI_DIVERGENCES.md` § *Cas où NeoST
   améliore Hatari*.
3. ✅ **[MFP] CLOS le 2026-09-02 — mais PAS par le correctif prescrit, qui est réfuté.**
   L'écart « pas d'`UpdateTimers` avant lecture IPR/ISR » **n'existait plus** : confronté à
   l'oracle sur `mfp_poll` (l'étalon bâti exprès), IPRA est identique à Hatari sur les
   100 lignes — le modèle BLOC préempte déjà à chaque échéance de timer. Le port du `runTo`
   ciblé `TIMER_*` a quand même été écrit pour le vérifier : il ne ferme rien et **dégrade
   l'étalon de 80 à 88 px** (balayage de l'instant sur ±12 cyc, aucun offset à 0 px) — retiré.
   Le résidu de 80 px était AILLEURS et n'était écrit nulle part : `readTimerData` lisait le
   compteur vivant sur l'échéance **arrondie au plafond entier** du Scheduler au lieu de
   l'échéance sous-cyclique que NeoST tenait déjà (`timerDueSub_`), d'où un `ceil` un cran
   trop haut sur 6 lignes / 100. Corrigé → **0 px contre l'oracle** (les 100 octets IPRA et
   les 100 octets TADR), et `mfp_poll` promu snapshot → **oracle** : le corpus oracle de la
   CI passe de 7 à 8 étalons. Récit → `docs/HATARI_DIVERGENCES.md` § MFP.
   📌 Leçon : la borne « 157 cycles » qui justifiait ce chantier était une métrique
   (`Scheduler::timerMaxLate`, maximum sur toute la trace, boot compris), pas un écart de
   rendu — et l'inventaire l'a portée un an sans qu'on la confronte à une image.
4. ◐ **[FPU]** ~~arrondis de conversion sortante~~ **FAITS le 2026-09-02** (double arrondi,
   INEX2, mode FPCR en sortie, payload NaN — cf. § Roadmap / FPU, banc porté à 12 tests) ;
   restent la précision FSGLMUL/FSGLDIV, le décimal empaqueté et FMOVECR/FMOD.
5. **[BLITTER]** résidu BL5 : ~10 cyc par démarrage de blit + ~3,3 par reprise de tranche,
   **paradoxe de signe non levé** — cf. Garde-fous (aucune correction sans 3ᵉ mesure).
6. **[VIDÉO, P3]** wakeup-state WS3 sous-pixel, mode 336 px STE (`bSteBorderFlag`), rendu
   live du retrait bas, interfoliage blitter → `docs/CYCLE_ACCURACY.md` §4.

**Faisables sans oracle** : FPU packed decimal **bit-exact** (port de `softfloat_decimal.c`) —
détaillé dans `docs/HATARI_DIVERGENCES.md`. ✅ Les deux points BORNÉS de cette puce sont faits
le 2026-09-02 (±inf/NaN → exposant $FFF, OPERR si k > 17) ; INEX1 reste délibérément non posé
tant que la conversion entrante passe par `strtod` — cf. § Roadmap / FPU. ✅ La **recomposition Unicode NFD→NFC (cible macOS)** est FAITE le
2026-09-02 : `neost::hostpath::precomposeUtf8`, branchée sur `matchHostDirEntry` et le listing
Fsfirst. Le bug était réel et mesuré — sans elle, un fichier accentué écrit en NFD (ce que macOS
rend à `readdir`) est INTROUVABLE depuis le TOS (vérifié par mutation sur APFS). Gardé à deux
niveaux : `selftest_logic` pour la fonction pure (13 cas, exerçables depuis n'importe quelle
plateforme) et `gemdos-selftest` pour le câblage, avec un vrai fichier sur disque.

**Décisions actées (NE PAS « corriger » vers Hatari)** : palette de la PREMIÈRE LIGNE quand
la bordure haute est retirée (A41 — Hatari amorce avec des écritures postérieures de
380 cycles) ; SCC `WR14` bit4 loopback (datasheet Zilog, NeoST plus fidèle) ; WRITE/READ TRACK STX réinterprétés (NeoST rend la piste lisible) ;
densité HD/ED STX (NeoST plus cohérent) ; RTC en temps émulé (déterminisme headless).

> **L'oracle se bâtit, il n'arrive pas tout seul** : `extern/hatari` est GITIGNORÉ et n'est
> PAS un sous-module — sur une machine fraîche il est ABSENT. `tools/setup_hatari.sh` clone au
> pin (`f0736b2`) et bâtit avec les options macOS obligatoires ; recettes →
> [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

---

## Roadmap par sous-système — items ouverts

> Le reste (Bus/MMU, FDC, YM2149, GEMDOS, ACSI, SCC, FPU, imprimante, MegaSTE 8/16 MHz + cache…)
> est **fait et validé** — voir `CHANGELOG.md`. Ci-dessous, uniquement ce qui reste ouvert.

### Vidéo / Shifter
- Raffinements cycle-exact → § Divergences ci-dessus.
- **Résidu du latch couleur bordure gauche** : 16 px (cols 45-60) = la **position
  horizontale exacte** où l'écriture palette prend effet (Hatari bascule ~16 px après le
  début nominal de l'aire active = latence pipeline ; NeoST bascule pile à `activeX_`).
  Invisible aux étalons. _Valeur très basse._

### Interface — kiosk & effets CRT
- ⭘ **Souris ABSOLUE pour GEM/bureau** — la souris ST n'est pilotée qu'en mode
  capturé/relatif (`g_mouseCaptured`, `GLFW_RAW_MOUSE_MOTION`), pensé pour les jeux.
  🎯 Un mode absolu (position curseur hôte → curseur ST, sans capture) pour l'usage
  GEM/desktop/navigateur — confort, pas bug.
- ⭘ **Trace clavier permanente `NEOST_KBD_TRACE`** (comme `NEOST_ENEC_TRACE`) — éviterait
  un cycle rebuild/revert au prochain doute clavier. _Valeur faible, coût nul._
- Cosmétique : membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ; répétition de
  navigation kiosk (tenir gauche/droite bloque la répétition haut/bas). Limitations CRT v1
  assumées (baril/vignette sur le buffer entier en kiosk ; GL 2.1 → passthrough).

### Stockage & contrôleurs
- **SCSI / NCR5380** — TT/Falcon **uniquement** (le MegaSTE n'en a pas). Hors périmètre,
  non commencé. Réf. `ncr5380.c`.
- SCC : restes faible valeur — timers du BRG / Zero Count, baudrate temporisé, série hôte.
- ⭘ **Test F (disquette) de la cartouche STE_Test v1.9 : « Cannot write drive A/B »,
  drives vus SS** (pré-existant au chantier MegaSTE ; le test F du diagnostic MegaSTE,
  lui, PASSE avec le même FDC émulé — la cartouche STE détecte les faces/l'écriture
  autrement). Trace façon FDC + Hatari en oracle sur la même cartouche. _Valeur moyenne._

### FPU MC68881 (audit 2026-07-12 — différés)
- ✅ **Arrondis de conversion SORTANTE bit-exacts — FAIT le 2026-09-02.**
  `floatx80_to_int32/16/8` et `floatx80_to_float32/64` (+ leurs `roundAndPack*`) sont portés
  dans `src/io/SoftFloatX80.hpp` (`sf::toInt`, `sf::toFloat32/64`) et `Fpu::encodeFmt` ne
  traverse plus de `double` hôte pour L/W/B/S/D. Sont réglés du même coup : le **double
  arrondi** (64 → 53 bits AVANT l'arrondi demandé), **INEX2 jamais levé**, le **mode FPCR
  ignoré** en sortie, l'**UNFL** absent et l'**OVFL silencieux en D**, et un NaN rend enfin
  son **payload** au lieu de 0.
  Exhibé et gardé par trois cas ajoutés au banc (`tools/make_fpu_testrom.py`, 9 → 12 tests,
  palier `fast` via l'auto-test série `fpu_cir`) : **test 10** l'étendu juste au-dessus de 0,5
  ($3FFE 80000000_00000001) doit rendre 1 en FMOVE.L — il rendait **0**, le `double` le
  ramenant à 0,5 exact puis la règle du pair l'envoyant à zéro ; **test 11** INEX2 armé par
  FMOVE.L de 1,5 ; **test 12** FPCR en RZ → FMOVE.S de 1/3 TRONQUE ($3EAAAAAA) au lieu
  d'arrondir au plus près ($3EAAAAAB) comme le faisait l'hôte. Les trois vérifiés par
  mutation (chacun échoue si l'on rétablit le chemin `double`).
  ⚠ **Piège d'outillage débusqué au passage** : `disks/etalons/fpu_testrom.img` est COMMISE et
  `ensure_rom_asset` ne la régénère que si elle est ABSENTE — les tests ajoutés sont restés
  invisibles du palier tant qu'on ne l'avait pas re-commise. Et une ROM `rom_generate`
  manquante était classée « TOS Atari absent » donc SKIP (vert sans rien exécuter) : garde
  corrigée dans `tools/run_selftests.py`.
  **Reste sur cette puce** : FSGLMUL/FSGLDIV (plage d'exposant ÉTENDUE avec mantisse 24 bits
  → porter `roundSigAndPackFloatx80`, softfloat.c:1502) — non touché.
- ◐ **Packed decimal — 2 des 3 points FAITS le 2026-09-02.**
  ✅ **±inf/NaN → exposant $FFF** : le 68881 n'émet pas de BCD pour un infini ou un NaN, il
  recopie le motif étendu (≙ `fp_from_pack`, fpp_softfloat.c:702). NeoST tombait dans son
  `snprintf("%+.*e")`, où la libc rend « +inf » et où le parseur BCD tirait des chiffres
  ARBITRAIRES de « inf ». Le payload d'un NaN traverse désormais intact (vérifié).
  ✅ **OPERR si k > 17** (≙ softfloat_decimal.c:412-414) : l'écrêtage à 17 se faisait EN
  SILENCE. Ne vise que le k positif — un k négatif sélectionne le style point fixe.
  Gardés par les tests 13 et 14 du banc (`make_fpu_testrom.py`, 12 → 14 tests), chacun
  vérifié par mutation.
  ⏸ **INEX1 : DÉLIBÉRÉMENT NON FAIT**, et la raison n'est pas le temps. Chez Hatari le
  drapeau est `float_flag_decimal` → `FPSR_INEX1` (fpp_softfloat.c:100), et il est armé sur
  la direction **décimal → étendu** (`floatdecimal_to_floatx80`, softfloat_decimal.c:369),
  pas sur la sortie. Or NeoST approxime cette direction par `std::strtod` (53 bits pour
  17 chiffres décimaux) : on ne SAIT pas quand la conversion a été exacte. Poser le drapeau
  à l'estime serait pire que ne pas l'avoir — un programme qui teste INEX1 se fierait à une
  information parfois fausse. Le débloquer demande le vrai `floatdecimal_to_floatx80`.
  **Reste donc** : port de `softfloat_decimal.c` (génération de chiffres bit-exacte,
  direction entrante, style point fixe k ≤ 0), qui livrerait INEX1 au passage.
- FMOVECR : précision FPCR non appliquée après la table ; offsets indéfinis → table silicium
  (`fpp_cr_undef`) au lieu de 0.0. FMOD précision < étendu : ré-arrondir a (expDiff<−1).

### Périphériques & profils machine
- **A13 — Save-states × GEMDOS HD** : les handles fichiers hôtes ouverts / suivi Pexec de
  `GemdosHd` sont HORS snapshot (bug hunt 2026-07-12, F7) — un état sauvé pendant qu'un
  programme a des fichiers ouverts sur C: donne des handles morts au load. Sérialiser la
  table de handles (chemin + offset + mode) et rouvrir au load. (En attendant, le load est
  refusé si le drapeau GEMDOS diffère — garde en place.)
- **Cartridge port** `$FA0000-$FBFFFF` générique (au-delà du système GEMDOS) — réf. `cart.c`.

### Système de régression — restes
(La pyramide, ses paliers et ses chiffres → `CLAUDE.md` et `DEV.md`.)
- gate `trace_diff --periods` vs oracle Hatari (le cycle-bench actuel est une auto-régression
  NeoST) ;
- self-tests P0 supplémentaires (autres Timers, ACIA) ;
- si une vraie démo spec512 **overscan** (bordures ouvertes) libre est rapatriée un jour →
  l'ajouter en étalon oracle (l'auto_diapo est 100 % borderless).

### Outillage / qualité
- **Étalons headless** : calibrer frames + références Cuddly / Union / Troed / Hatari Test
  Suite ; rapatrier Union (planetemu manuel). Infra en place (`tools/run_etalons.py`).
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- **Matrice MegaSTE — restes** : combinaisons DD/HD × cache par balayage systématique si un
  jour un titre l'exige.
- Capturer des **traces Hatari de référence** pour `trace_diff` (Arkanoid & co).
- ⭘ **Hygiène FujiNet — décision de mainteneur** : le code est retiré (2026-08-22), restent
  deux mentions historiques (commentaire de version save-state dans `src/core/Machine.cpp`,
  entrées `CHANGELOG.md`). Reformuler ou assumer — un changelog garde normalement la trace
  de ce qu'il a supprimé.

### Station MIDI — le ST comme séquenceur d'un studio moderne (relevé 2026-08-29)

> Cas d'usage visé : un Mac porte les AU/VST et les claviers USB, le ST porte Cubase ou
> Notator et mène la séance. La **sortie** est déjà au niveau : gigue σ 0,4-1,7 ms et
> pente de tempo 1,001 mesurées sur 200 notes (`run_midi_sequencer.py`). Ce qui suit est
> ce qui manque pour y brancher un vrai studio plutôt qu'un seul appareil.

- ~~**Débit d'entrée — plafond à 4,5 % d'un câble MIDI.**~~ **LIVRÉ le 2026-08-29.**
  L'injection hôte→ACIA se faisait une fois par trame et le 6850 n'accepte que 2 octets :
  plafond 2 octets/trame, ~143 o/s en mono contre 3 125 o/s sur un vrai câble. C'est
  désormais l'ACIA qui TIRE, sur `Scheduler::MIDI_RX` (2 560 cycles/octet), comme
  `IKBD_RX` le fait pour l'ACIA clavier. **Mesuré en temps réel : 40,4 octets/trame,
  soit ~2 885 o/s — 92 % d'un câble, ×20.** Le débordement redevient celui du matériel
  (le 6850 perd l'octet neuf) au lieu d'être masqué par une rétention côté hôte.
- ~~**Un seul appareil de chaque côté.**~~ **LIVRÉ le 2026-08-29.** Entrée : boîtier de
  **fusion** (N sources → l'unique ACIA, entrelacement aux frontières de MESSAGES, un
  décodeur par source) avec **canalisation** par source, sans quoi deux claviers émettant
  tous deux sur le canal 1 seraient inséparables pour le séquenceur. Sortie : **aiguillage
  par canal** (chaque destination reçoit le masque de canaux qu'on lui donne ; les
  messages système vont à toutes). Reste ouvert ci-dessous : le multi-PORTS, qui est un
  autre sujet — l'aiguillage répartit 16 canaux, il n'en crée pas 32.
- ~~**Latence de jeu.**~~ **RÉGLABLE depuis le 2026-08-29** (`midi_lead_ms=`, curseur
  0-100 ms sur la page MIDI ; mesuré : 190 ms d'écart de livraison pour 200 ms commandés).
  La part « entrée » était déjà tombée avec le point précédent. ⚠ L'avance ne protège PAS
  du cas courant — l'émulation d'une trame prend moins de temps réel qu'une trame, donc à
  0 ms d'avance aucun octet n'est en retard sur un run sain (mesuré) ; elle protège du
  RATTRAPAGE quand la boucle décroche. D'où le témoin `lateBytes` : le réglage se baisse
  jusqu'à ce qu'il se déclenche, plutôt qu'à l'aveugle. Ce qui reste ouvert : ~30 ms
  (`MidiOutHost::kLeadMs`, le prix payé pour tuer la gigue). Un pianiste le sent. Rendre
  cette avance **réglable** : c'est un arbitrage gigue/latence qui appartient à
  l'utilisateur. Le premier point ci-dessus supprime la part « entrée ».
- **16 canaux — pas d'interface multi-ports.** (L'aiguillage livré répartit les 16 canaux
  du ST entre plusieurs appareils ; il n'en ajoute pas.) Le ST n'a qu'un MIDI OUT ; les **C-Lab
  Unitor** et **Steinberg Midex** en ajoutaient (32/64 canaux, SMPTE). Curiosité : la
  *clé* de l'Unitor-N est **déjà émulée** (c'est la même que celle de Notator), mais pas
  le boîtier. `CartDevice.hpp` sait déjà que MIDEX et Combiner empilaient des clés.
- **Notator n'a jamais tourné.** Il est cité comme séquenceur voulu au même titre que
  Cubase, mais aucun étalon ne l'exerce et sa clé garde **deux incertitudes** non
  tranchées faute de Notator SL original (cf. le point « Clé Notator » plus bas). Tant
  que le logiciel n'a pas démarré une fois, c'est une promesse, pas une capacité.
- ~~**Profils d'appareil.**~~ **LIVRÉ le 2026-08-29** (`src/audio/MidiDeviceProfiles.hpp`).
  Une entrée pour l'instant : **Novation Circuit Tracks**, d'après son *Programmer's
  Reference Guide* v3 — Synth 1 canal 1, Synth 2 canal 2, pistes MIDI 1-2 canaux 3-4,
  **Drums 1-4 tous sur le canal 10** (ils se distinguent par la NOTE : 60, 62, 64, 65),
  canal 16 réservé au Project Control. Le bouton pose le masque `$020F`, l'infobulle
  donne le plan. ⚠ Ce sont des défauts d'usine, réassignables en Setup View : la table
  n'accepte qu'une source constructeur **citée**, jamais une supposition.
- ~~**Appareils HOMONYMES non distinguables.**~~ **LIVRÉ le 2026-08-29**
  (`src/audio/MidiEndpoint.hpp`). La config mémorise désormais l'identifiant unique de
  l'hôte (`kMIDIPropertyUniqueID` sous CoreMIDI) À CÔTÉ du nom, et l'appariement fait
  deux passes — identifiant d'abord, nom ensuite — en n'attribuant **jamais deux fois le
  même point de terminaison**. Deux claviers du même modèle s'ouvrent donc chacun sur le
  sien, y compris sous **ALSA** qui n'a pas d'identifiant stable (la règle de
  non-réattribution suffit tant qu'ils sont branchés ensemble). L'identifiant est
  **appris** à la première ouverture d'un appareil désigné par son seul nom, si bien
  qu'une config d'avant devient sûre toute seule. Étiquettes suffixées « #1 / #2 » dans
  l'interface, sans quoi deux lignes seraient rigoureusement identiques.
  ⚠ Ce qui reste hors de portée : deux appareils homonymes dont un seul est branché,
  sous ALSA — rien ne permet alors de savoir lequel.
- **Canal forcé par source : pas de « splitter » clavier.** Un vrai boîtier de fusion sait
  aussi couper un clavier en zones (grave → canal 1, aigu → canal 2). Ici une source = un
  canal.
- **Pas de synchro SMPTE/MTC.** L'horloge MIDI traverse (ce ne sont que des octets), donc
  le ST peut mener un DAW. Le code de temps SMPTE de l'Unitor, non.
- ~~**Windows : le synthé GM intégré marche, le reste non.**~~ **LIVRÉ le 2026-08-31**
  (backend winmm, `src/audio/MidiWinmm.hpp`) : destinations matérielles et sources
  hôtes marchent sous Windows comme sous macOS/Linux, identifiant unique compris
  (chemin d'interface USB — mieux qu'ALSA, qui n'en a aucun). Vérifié sur matériel
  réel (Novation Circuit Tracks). **Reste hors de portée : le port virtuel**
  « NeoST MIDI OUT » — Windows n'a aucune API pour créer un port que les autres
  applications voient (ni winmm, ni WinRT MIDI 1.0). Le contournement est un pilote
  tiers (loopMIDI), dont le port apparaît ensuite comme un appareil ordinaire ; la
  page MIDI le dit. Windows MIDI Services (MIDI 2.0, Win11) le ferait nativement mais
  s'installe à part et exigerait une pile WinRT/COM incompatible avec le zip autonome.

### Réseau (extensions NeoST — base livrée 2026-08-12, cf. `docs/EXTENSIONS.md`)

> Les chantiers **clos** de ce front (Slirp 5/5, fenêtres EtherNEC ROM3/ROM4, CAB affiche
> theoldnet.com) → `CHANGELOG.md` (2026-08-27), recettes incluses.

- ~~**MIDI OUT Windows**~~ : **LIVRÉ le 2026-08-31** — `MidiOutHost` et `MidiInHost`
  couvrent désormais CoreMIDI (macOS), ALSA (Linux) et **winmm** (Windows). Cf. la
  section *Station MIDI* ci-dessus pour la seule case qui reste vide (port virtuel).
- **Périphériques des ports — validation** : `PortDevices` transcrit Steem/WinUAE sans
  logiciel à clé sous la main. À exercer : Leader Board / 10th Frame (dump ST), B.A.T. II,
  Music Master, et l'option « Pro Sound » de Wings of Death / Lethal Xcess (présents en
  STX) pour entendre le DAC. **Clé Notator** (`--dongle notator`) : à confronter à un
  Notator SL original — deux incertitudes à trancher sur le vrai matériel (front de /ROM4
  cadençant FEEDB1 ; ordre UDS↔/ROM4 à l'armement). Restent sans relevé public : Log 3
  (EP330), Pro-24 (GAL16V8), Avalon / Synthworks, Zodiac, DynaBlaster. L'outil pour
  trancher existe : capture matérielle `R3`/`R4`/`U` + `--key-replay`
  (recette → `docs/EXTENSIONS.md`).
- **Dongles — frontends WASM/Android** : `PortDevices`/`CartridgeKey` ne sont exposés que
  par le GUI et le headless ; le menu Android et la démo web n'ont pas de page Dongles.
- **Clé Steinberg — validation** : `CartridgeKey` (rouge/noire, équations MiSTery) n'a
  jamais vu un Cubase 3.10 / Score / 2.01 réel — il faut une disquette originale (non
  crackée). (La « destination CoreMIDI au lieu de la seule source virtuelle » qui figurait
  ici est **livrée** le 2026-08-29, dans les deux sens — cf. `docs/EXTENSIONS.md`.)
- **NetUSBee — périphériques USB hôte** : l'ISP1160 (`io/Isp1160`) est un hub racine
  VIDE ; brancher un clavier/souris HID puis un stockage de masse derrière
  `HcRhPortStatus`. Banc d'essai : pilotes FreeMiNT `netusbee.ucd` + `usb.km`.
- **NetUSBee — fenêtre LSB partagée** : `$FA0000-$FA01FF` = latch ISP1160 ET lecture du
  registre CR NE2000 ; NeoST laisse les deux puces voir l'accès faute de schéma. À
  trancher sur le schéma du NetUSBee (hardware.atari.org), puis ajuster `Bus::read8Slow`.
- **UltraSatan — `US_CONF.TOS` réel** : l'outil de Jookie (ce-atari) compile avec Pure C ;
  le passer sur NeoST pour valider au-delà du programme de test maison. Idem HDDRIVER/ICD
  PRO sur une image 2 slots.
- **EtherNEC — validation TOS 1.04** : le backend réel existe (`SlirpNat`, 5/5) ; reste à
  valider STinG + `ENEC.STX` sous TOS 1.04 (DHCP + ping/GET), consigner dans
  `docs/CASE_STUDIES.md`, et **livrer les pilotes libres GPL** dans les paquets.
- **Modem/STinG** : documenter l'installation STinG (noyau + `sting.inf` dans `AUTO`,
  modules dans `C:\STING`) dans `docs/TEST_SOFTWARE.md` ; banc SLIP bout-en-bout.
- **MIDI ring** : option GUI (saisie du pair) ; test en anneau à 2 nœuds (deux instances
  NeoST — ⚠ bloqué par le mono-instance A33 si c'est en un seul processus).
- **Sécurité** : liste blanche de domaines optionnelle pour les backends sortants.
