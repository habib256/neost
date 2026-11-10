# CLAUDE.md

Hub d'orientation pour Claude Code. **Ces instructions priment.**

NeoST — émulateur Atari ST « boîte à hack » pédagogique. C++17, GLFW3 + OpenGL
(immediate mode) + Dear ImGui, miniaudio, Moira (68000 cycle-exact, vendorisé).
Développé sur **macOS Silicon / CachyOS Linux** ; **Windows x64 est livré** depuis la
0.5.1 (MinGW-w64, `packaging/windows/`) mais n'est vérifié qu'en CI. Un **APK Android**
arm64 existe depuis le 2026-08-11 (`packaging/android/`, SDL2 + GLES2) — il démarre, sonne,
et a un **menu** (décalqué de la borne : ludothèque, disquette à chaud, page clavier), mais
n'a **jamais tourné sur un appareil réel** (validé sous QEMU arm64 seulement).
**Commentaires et documentation en français ; interface et journaux en ANGLAIS.**

Architecture en deux mots : **le `Bus` *est* le plan mémoire** (route read8/write8 vers
les puces) et **`neost_core` ne dépend pas du GUI**.

**Cap suivant : le TT et le Falcon**, sur le Moira de POM68K (68000/030/040, JIT
déterministe, FPU et MMU), qui remplacera aussi le cœur du ST. **Rien ne s'intègre avant
que l'auteur le déclare prêt** ; la porte d'échange est posée (`docs/CPU_SWAP_GATE.md`).
Jalons : **0.6.4**, puis **0.6.5** juste avant l'intégration du cœur POM68K — la dernière
sur le Moira de NeoST.
D'ici là, `TODO.md` range le travail en trois temps : **avant l'échange** (le filet, puis
tout ce qui ne dépend pas du cœur), **l'échange** (§ 9 — l'intérieur du cœur est **gelé** :
pas de patch dans `extern/moira` ni de refactor de `src/core/Cpu68k.cpp`, sauf défaut qui
casse un titre), **le TT et le Falcon** (§ 10).

## Où trouver quoi

**Un rôle par document.** L'ouvert ne vit que dans `TODO.md` ; le fait, par puce, dans
`docs/IMPLEMENTED.md` ; le récit daté dans `CHANGELOG.md`. Une doc de sous-système décrit
un modèle, un banc ou un cas, et renvoie au TODO par le **titre** de l'item. Ce qui est
clos et ne fait plus foi va dans `docs/archive/` (hors contrôle d'ancres).

**Le projet**

| Doc | Contenu |
|-----|---------|
| [`DEV.md`](DEV.md) | **Détails techniques** : architecture, horloge, bus, débogage headless, mapping Hatari, pièges matériels. |
| [`CHANGELOG.md`](CHANGELOG.md) | **Chronologie** : releases puis chantiers datés. |
| [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) | **Ce qui est fait, par puce** — répond à « NeoST gère-t-il X ? ». |
| [`TODO.md`](TODO.md) | **Ce qui reste**, priorisé (P1-P3) et rangé par rapport à l'échange du cœur : plan en lots, filet de l'échange, restes par puce, bugs jeux, harnais, extensions, livraison, dette, puis ce qui est gelé jusqu'à l'échange et le TT/Falcon. Un item clos en sort. |
| [`README.md`](README.md) | Présentation et usage (en anglais, public). |

**Méthode et preuve**

| Doc | Contenu |
|-----|---------|
| [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md) | **Échelle des références** : contre quoi chaque verdict du harnais est vrai (`hardware` > `primary` > `rom` > `rtl` > `hatari` > `snapshot`), règles d'arbitrage. **À lire avant de conclure « fidèle » sur un vert.** |
| [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md) | **Inventaire maître** des écarts NeoST↔Hatari (sévérité + `fichier:ligne`). |
| [`docs/HATARI_MAPPING.md`](docs/HATARI_MAPPING.md) | Correspondances Hatari↔NeoST↔docs. **À consulter AVANT tout audit.** |
| [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md) | Exécuter Hatari en **oracle headless** (boot → PNG, traces, `--cmd-fifo`). |
| [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md) | **Cas tranchés** : titres corrigés OU jugés fidèles, avec la recette. À lire avant de rouvrir un « bug ». |
| [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md) | Catalogue des **logiciels étalons** par sous-système. |
| [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md) | **Précision cycle** : modèle Hatari, acquis, inventaire priorisé du restant. |

**Bancs contre le silicium**

| Doc | Contenu |
|-----|---------|
| [`docs/FX68K_BENCH.md`](docs/FX68K_BENCH.md) | **Banc Moira↔fx68k** : le 68000 nu contre le RTL cycle-exact, conventions de datation, écarts trouvés et ce qui les tranche. |
| [`docs/STBLITTER_BENCH.md`](docs/STBLITTER_BENCH.md) | **Banc du blitter** : NeoST contre `stBlitter.sv` et fx68k, accès, données et dates ; ce que la netlist ST-4082 a tranché. |
| [`docs/STCPU_BENCH.md`](docs/STCPU_BENCH.md) | **Banc CPU + GSTMCU** : ce que le 68000 voit du faisceau, compteur et interruptions, contre les RTL, à l'octet. |
| [`docs/GLUE_SWEEP.md`](docs/GLUE_SWEEP.md) | **Balayage GLUE du STE** : impulsions 60 Hz et haute résolution à chaque phase, NeoST contre le modèle GSTMCU. |
| [`docs/HATARI_MEGASTE_AUDIT.md`](docs/HATARI_MEGASTE_AUDIT.md) | **Blitter et IRQ du Mega STE** : domaines d'horloge à 16 MHz contre Hatari, résidu ouvert. |
| [`docs/MFP_IACK_MEASUREMENT.md`](docs/MFP_IACK_MEASUREMENT.md) | **Protocole de mesure de l'IACK** sur un vrai Mega STE : sonde prête, capture à faire. |

**Journaux d'enquête** (chantiers clos, gardés pour leurs mesures et leurs pistes éliminées)

| Doc | Contenu |
|-----|---------|
| [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md) | **Beam-sync** : convergence Moira↔WinUAE, mesures, pistes éliminées. À relire à l'échange du cœur. |
| [`docs/CLOSURE_CHANTIER.md`](docs/CLOSURE_CHANTIER.md) | **Closure (Sync)** : la chaîne causale Timer B → GPIP → datation, résolue le 2026-08-12. |
| [`docs/archive/`](docs/archive) | Tables de l'audit de cartographie du 2026-07-08, dossiers des releases passées. |

**Livraison, usage et suite**

| Doc | Contenu |
|-----|---------|
| [`docs/RELEASE.md`](docs/RELEASE.md) | **Poser une version** : procédure, le piège du cache `NEOST_VERSION_STR`, ce qui bloque encore une release publique. |
| [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md) | **Validation sur matériel réel** : registre par cible de livraison (config + mesures), protocole en cinq pas, mesure du budget temps réel (`run_perfbench.py --budget`). |
| [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md) | Recette callgrind, points chauds, build PGO+LTO et son piège. |
| [`docs/KIOSK.md`](docs/KIOSK.md) | Mode borne : options, menu manette, zoom adaptatif, Raspberry Pi. |
| [`docs/NETUSBEE.md`](docs/NETUSBEE.md) | **NetUSBee en détail** : ports USB virtuels (HID, disque), modèle, limites et preuves. |
| [`docs/GRAPHIFY.md`](docs/GRAPHIFY.md) | **Carte du code** (`graphify-out/`) : consultation, périmètre, limites. |
| [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md) | **Extensions NeoST (stockage/réseau)** : **UltraSatan** (SD sur ACSI), **NetUSBee**/EtherNEC (NE2000 + USB, port cartouche), modem Hayes, anneau MIDI, **appareils MIDI hôtes** (destination/source matérielle), clés Steinberg et **adaptateurs de port** (dongles joystick/série, DAC Pro Sound, boutons Multiface/URC). Tout est du matériel qui a réellement existé sur ST. OFF par défaut, sans effet sur les étalons. |
| [`docs/OPENDST.md`](docs/OPENDST.md) | **Pilotage externe déterministe** : contrat de déterminisme, scripts joystick, sondes mémoire, boucle d'exploration d'états (Go-Explore), recette Rick Dangerous. |
| [`packaging/android/README.md`](packaging/android/README.md) | **Paquet Android** : build, pièges (jlink, Gradle/JDK), validation ARM64 sous QEMU. |
| [`docs/CPU_SWAP_GATE.md`](docs/CPU_SWAP_GATE.md) | **Échange du cœur 68000** (Moira POM68K) : porte d'empreintes indépendante du cœur, procédure, contrat attendu du cœur (surcharges, internes, patches). **Tout patch de `extern/moira` y ajoute sa ligne.** |

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j        # cibles : neost (GUI), neost-headless, neost-selftest,
                              #          neost-fuzz-disk, neost-stx-test,
                              #          neost_core / neost_net (libs)
./build/neost                 # auto : dernier ROM (neost.cfg) ou EmuTOS US
./build/neost <rom> <disk.st>
```

Sous-modules : `extern/imgui`, `extern/miniaudio`. `extern/moira` est **vendorisé**
(NeoST patche son code — cf. `extern/moira/NEOST_VENDOR.md`) ; il se compile tel quel en
C++20, sans générateur de code (CMake n'en recopie qu'une version sous
`build/generated/moira`, avec `MoiraConfig.h` réglé en cycle-exact). Le cœur Musashi a été retiré.

⚠ Ne PAS faire `rm -rf build` (casse le shell si l'utilisateur y est `cd`) ;
`cmake -B build` reconfigure. Sous macOS, pas de `timeout`.
⚠ `NEOST_VERSION_STR` est une variable de **cache**, ré-ensemencée automatiquement
après un bump de `project()` (`NEOST_VERSION_SEED`) — SAUF si une valeur a été forcée par
`-DNEOST_VERSION_STR=<ver>` : elle est alors préservée et `--version` ment jusqu'au
prochain `-D` (cf. `docs/RELEASE.md`).

## ⭐ Méthode imposée (ordre strict)

**L'objectif est le comportement d'un véritable Atari ST et des variantes prises
en charge. Le matériel réel prime sur les émulateurs.** Une erreur confirmée
d'Hatari ne doit pas être reproduite : corriger NeoST selon les preuves
matérielles disponibles, documenter la divergence et conserver l'incertitude
quand ces preuves manquent. L'égalité avec Hatari seule ne prouve pas la fidélité.

### Les grades de preuve — à écrire dans le code

Toute affirmation sur le matériel porte **son grade et sa source**, dans un
crochet en tête de commentaire, pour qu'un lecteur et un `grep` s'accordent sur
ce qui soutient le code :

| Grade | Preuve |
|---|---|
| **A** | Mesure sur **machine réelle** — oscilloscope, capture, exécution sur le fer. |
| **B** | **Document primaire** — fiche technique de la puce, documentation Atari, spécification, brevet. |
| **C** | **Désassemblage de ROM** — TOS/EmuTOS, ROM de cartouche. Preuve du *contrat* attendu par le logiciel, pas du silicium. |
| **D** | **Un autre émulateur** — Hatari, MAME, STeem, à égalité. Utile, faillible. |
| **E** | **Affirmé sans source.** |
| **F** | **Inconnu assumé** — deviné, ajusté, approximatif, non vérifié. |

```
// [B] MC68901 datasheet (Motorola) § 3.4 — le timer recharge sur le front.
// [D] Hatari mfp.c:412-430 @ v2.6.0 — idem, tel qu'Hatari le lit.
// [C] ROM TOS 1.04 $FC0A12 — le système attend l'ordre inverse.
// [A] Mesuré sur un 520STF : 160 cycles par ligne (DEV.md § timings).
// [F] Inconnu : ajusté jusqu'à ce que la démo passe. Tranché par : une
//     capture du signal sur un vrai ST.
```

Trois règles en découlent. **Une citation de grade D nomme sa révision** (tag ou
commit) : les numéros de ligne amont bougent à chaque commit, et une plage sans
révision ne désigne rien. **Un grade inférieur ne l'emporte jamais sur un grade
supérieur** : là où NeoST s'écarte d'Hatari parce qu'une fiche technique, une ROM
ou une mesure dit autre chose, c'est correct, et le commentaire le dit — cela
protège le code d'un futur « alignement sur Hatari » qui serait une régression.
**Un F se garde, il ne se cache pas** : écris ce qui le trancherait.

**Citer la référence dans le commentaire, pour qu'on puisse la rouvrir** : un document
se cite par titre, édition et section ou page, plus son empreinte s'il est dans
`extern/datasheets` ; un émulateur ou un RTL par `fichier:ligne @ révision` ; une ROM par
version et adresse. Une citation se vérifie à la révision qu'elle nomme avant d'être
écrite : une ligne ancienne d'Hatari peut dater d'un autre commit que l'épingle. Les
fiches et schémas ASIC se rapatrient par `tools/setup_silicon_refs.sh` : les consulter
d'abord, avant tout émulateur.

Cette hiérarchie est commune à tous les émulateurs du même auteur (POM2 porte la
même, adaptée à MAME et AppleWin).

**Le harnais suit la même hiérarchie** : chaque étalon déclare son échelon
(`ref_kind`, [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)). Un vert `hatari`
veut dire « d'accord avec Hatari », un vert `snapshot` « n'a pas bougé ». Le RTL épinglé
(fx68k, MiSTery : `tools/setup_silicon_refs.sh`) reste de grade D, mais il arbitre avant
Hatari sur un signal ou un cycle de ses blocs dérivés d'un schéma, à titre provisoire.

`extern/hatari/src` est une **référence d'implémentation à confronter au matériel**
(sources C complètes, lues et PAS compilées). Avec **MAME**, il aide à comprendre
le comportement des puces. Hatari
s'exécute aussi en **oracle headless** (`extern/hatari/build/src/hatari`).
⚠ `extern/hatari` est **gitignoré et n'est pas un sous-module** : rien ne le rapatrie, il
peut être ABSENT — clone + build (options macOS obligatoires) et recettes →
[`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

Quand un test/jeu plante, **NE PAS** désassembler ni chercher le point de divergence
d'emblée. **D'ABORD** comparer `extern/hatari/src` au code NeoST, **porter ce qui
manque et qui correspond au matériel, puis RETESTER**. Ne jamais porter un défaut
confirmé d'Hatari pour obtenir une égalité de traces. Ce n'est QUE si l'on a la conviction d'avoir tout porté
correctement et que l'erreur persiste qu'on investigue en détail (trace → boucle →
source EmuTOS [github.com/emutos/emutos](https://github.com/emutos/emutos)).
Une rustine d'Hatari réservée à un titre (test du PC, aléa) signale un phénomène
matériel non modélisé : chercher la physique qu'elle imite, pas la copier (cas Lethal
Xcess, `docs/CASE_STUDIES.md`).

Fichiers Hatari clés (← composant NeoST) — table complète dans `DEV.md` :
- `ioMem.c` + `ioMemTabST/STE.c` → carte des bus errors MMIO (← `Bus::busFault/buildIoFault`)
- `cpu/memory.c`, `stMemory.c` → banques RAM/ROM/bus-error + décodage MMU (← `Bus`)
- `mfp.c`, `video.c`, `fdc.c`, `psg.c`, `dmaSnd.c`, `acia.c`, `ikbd.c`, `blitter.c` → puces homonymes

## Tester = le headless (outil n°1)

Pas de framework de test : validation par `neost-headless` (déterministe, traces façon
MAME + captures PPM) et par la suite d'étalons. À côté, `neost-selftest`
(`tests/selftest_logic.cpp`, palier `fast`) couvre ce qui n'a besoin NI de machine NI de
ROM : la logique pure (chemins hôte, format `neost.cfg`) **et les tables de vérité « puce
nue + Scheduler »** — YM2149, MFP+ACIA, RTC, **Blitter, son DMA STE, FDC/DMA disquette**.
C'est l'étage entre la logique pure et le pixel : il dit « la tranche non-hog s'arrête au
32ᵉ mot » là où un étalon dit « 3 400 px divergents ». **Détail → `DEV.md`.**

```sh
python3 tools/run_all.py --tier fast   # ~12 s : logique + verdicts série + cycle-bench
                                       # + save-state + STX + boot GUI + 4 pixels RAPIDES
python3 tools/run_all.py --tier full   # + TOUS les étalons pixel (parallèles, --jobs)
                                       # + garde MegaSTE (suite Q du diagnostic)
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq
./build/neost-headless <rom> --frames N --screenshot s.ppm
```

⚠ **Avant de conclure quoi que ce soit sur le rendu, `--tier full`.** Le `fast` ne
compare que 4 étalons pixel courts (garde-fou, pas une couverture) : des commits ont
déjà sur-promis sur la base d'un fast vert.

**Hors paliers, à lancer soi-même** (Hatari et Pillow requis, médias privés) :

```sh
python3 tools/run_game_replay.py [--only ID]      # parties jouées contre Hatari
python3 tools/compare_titles.py OUT disque…       # --key/--joy/--mouse/--swap, --hatari-trace
python3 tools/cpu_swap_gate.py --record F | --check F   # état identique à l'octet (A/B)
```

Après un changement de timing : parcours de jeu, puis A/B de la collection
(`--arch-hash-every`, ou captures binaire avant/après). Grammaire souris : `L/R/U/D`
déplacent, `l/r/u/d` déplacent bouton gauche tenu (glisser), `1`/`2` cliquent.
⚠ Sous zsh, une variable non citée n'est **pas** découpée : `$K` multi-mots reste UN
argument.

Points critiques : `--irq` indispensable pour les bugs d'IRQ ; `--cart` + `--keys` pour
les cartouches de diagnostic (rapport sur port série), `--loopback` branché APRÈS
`--keys`. **VME/FPU MegaSTE « not found » est CORRECT** (Hatari n'émule pas le VME).

## Conventions non négociables

- Le 68000 est **big-endian** : assembler les mots octet par octet.
- Bus error = **whitelist** : un accès word/long ne faute que si TOUS ses octets fautent.
- Bits d'**entrée** du GPIP (moniteur bit7, ACIA bit4, FDC bit5) forcés en lecture.
- Haute résolution = **monochrome**. bit7 GPIP = 1 → couleur, 0 → mono.
- **Liste complète → `DEV.md` § Pièges matériels.**

## Disquettes & ROM

Lecteur A monte `.st`, `.msa`, `.dim` (détectés par contenu) et `.stx` (Pasti, jeux
protégés). Outils : `tools/make_floppy.py` (régénère `disks/diskA.st`),
`tools/fetch_disk.py <url>` (⚠ domaine public / freeware / démos uniquement).

**Médias privés** : `disks/st/`, `disks/stx/` et les TOS Atari sont gitignorés et ne se
versionnent jamais. Les jeux commerciaux ne se téléchargent pas : l'auteur les fournit.
`data/compdisks/CompDisks.json` (MIT) nomme 4 515 disquettes de compilation de la scène.

EmuTOS est le défaut libre. **Le build dépend de la machine** : **192 Ko**
(`etos192*`, se présente « Atari ST »/TOS 1.4) = **ST / Mega ST seulement** ; **256 Ko**
(`etos256us/fr`) = **STE / Mega STE** (programme le SCU). NeoST rétrograde
automatiquement en ST si un TOS ≤ 1.04 est lancé sur STE/MegaSTE
(`adjustMachineForTos`, comme Hatari).

⚠ **La ROM fixe la fréquence de balayage** : suffixe **`us` → 60 Hz NTSC**, suffixes
**`uk`/`fr`/`de`/`es` → 50 Hz PAL**. Contrôle en une commande — `neost-headless
roms/<rom>.img --frames 120` affiche `video: … @ NN Hz`. Ça **change l'image des
démos** : les images Spectrum 512 sont calculées pour le 50 Hz et sortent **déchirées**
en NTSC (fidèlement — Hatari fait pareil). Avant de soupçonner une régression sur un
« ça plante dans le GUI » : lire `neost.cfg`, rejouer avec
`--from-cfg neost.cfg --disk <image> --shot-every N PRÉFIXE`. Détails →
[`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md) § *Configuration : PAL/NTSC*.
