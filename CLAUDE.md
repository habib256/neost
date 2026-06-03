# CLAUDE.md

Hub d'orientation pour Claude Code (claude.ai/code). **Ces instructions priment.**

NeoST — émulateur Atari ST « boîte à hack » pédagogique. C++17, GLFW3 + OpenGL (immediate
mode) + Dear ImGui (GUI), miniaudio (son), Musashi/Moira (68000) en sous-modules. Cibles :
macOS Silicon / CachyOS Linux. **Commentaires en français.**

## Où trouver quoi

| Doc                            | Contenu                                                        |
|--------------------------------|---------------------------------------------------------------|
| [`DEV.md`](DEV.md)             | **Détails techniques** : architecture, horloge, bus, débogage headless, mapping Hatari, pièges matériels. |
| [`CHANGELOG.md`](CHANGELOG.md) | **Ce qui est fait** (implémenté + validé).                    |
| [`TODO.md`](TODO.md)           | **Ce qui reste** (fidélité Hatari, MegaSTE, précision cycle). |
| [`README.md`](README.md)       | Présentation + install/usage utilisateur.                     |

Architecture en deux mots : **le `Bus` *est* le plan mémoire** (route read8/write8 vers les
puces) et **le cœur `neost_core` ne dépend pas du GUI**. Détails → `DEV.md`.

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j                 # cibles : neost (GUI), neost-headless, neost_core (lib)
./build/neost                          # auto : dernier ROM (neost.cfg) ou EmuTOS US
./build/neost <rom> <disk.st>          # ROM + disquette explicites
```

Sous-modules : `extern/Musashi`, `extern/imgui`, `extern/miniaudio`, `extern/moira`. Pour
Musashi, générer les opcodes : `( cd extern/Musashi && cc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c )`.

⚠ Ne PAS faire `rm -rf build` (casse le shell si l'utilisateur y est `cd`) ; `cmake -B build`
reconfigure. Sous macOS, pas de `timeout`.

## ⭐ Méthode imposée (ordre strict)

`extern/hatari/src` = **la source de vérité matérielle** (sources C complètes, lues et PAS
compilées). Avec **MAME**, c'est la référence du comportement des puces. Hatari est aussi
installé (`/opt/homebrew/bin/hatari`) et peut être **exécuté en oracle headless** (boot →
PNG, sans affichage) pour comparer à NeoST → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

Quand un test/jeu plante, **NE PAS** désassembler ni chercher le point de divergence d'emblée.
**D'ABORD** comparer la source de vérité (`extern/hatari/src`) au code NeoST et **porter ce qui
manque, puis RETESTER**. Ce n'est QUE si l'on a la conviction d'avoir tout porté correctement
et que l'erreur persiste qu'on investigue en détail (trace → boucle → source EmuTOS
[github.com/emutos/emutos](https://github.com/emutos/emutos)).

Bugs trouvés ainsi (cf. `CHANGELOG.md`) : int-ack vectorisé, GPIP4/5/7, Timer B/C, modèle de
bus error (whitelist Hatari), double bus fault → halt, trame bus error 68000 dans Musashi.

Fichiers Hatari clés (← composant NeoST) — table complète dans `DEV.md` :
- `ioMem.c` + `ioMemTabST/STE.c` → carte des bus errors MMIO (← `Bus::busFault/buildIoFault`).
- `cpu/memory.c`, `stMemory.c` → banques RAM/ROM/bus-error + décodage MMU (← `Bus`).
- `mfp.c`, `video.c`, `fdc.c`, `psg.c`, `dmaSnd.c`, `acia.c`, `ikbd.c`, `blitter.c` → puces homonymes.

## Débogage = le headless (outil n°1)

Pas de « tests » classiques : validation via `neost-headless` (déterministe, traces façon
MAME + captures PPM). **Procédure complète, options et techniques vérifiées → `DEV.md`.**

```sh
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq   # localiser la boucle
./build/neost-headless <rom> --frames N --screenshot s.ppm           # sips -s format png ...
```

Points critiques (détail dans `DEV.md`) : `--irq` indispensable pour les bugs d'IRQ ; tester
les DEUX cœurs (`--cpu musashi|moira`) ; `--cart` + `--keys` pour les cartouches de diagnostic
(rapport sur port série) ; `--loopback` branché APRÈS `--keys`. **VME/FPU MegaSTE « not found »
est CORRECT** (Hatari n'émule pas le VME, FPU_NONE par défaut).

## Conventions non négociables

- Le 68000 est **big-endian** : assembler les mots octet par octet.
- Bus error = **whitelist** : un accès word/long ne faute que si TOUS ses octets fautent.
- Bits d'**entrée** du GPIP (moniteur bit7, ACIA bit4, FDC bit5) forcés en lecture.
- Haute résolution = **monochrome**. bit7 GPIP = 1 → couleur, 0 → mono.
- **Liste complète des pièges matériels → `DEV.md` § Pièges matériels.**

## Disquettes & ROM

Lecteur A monte une image `.st` (`.msa` et `.dim` aussi, détectés par contenu). Outils : `tools/make_floppy.py` (régénère
`disks/diskA.st`), `tools/fetch_disk.py <url>` (télécharge depuis planetemu via scrapling — à
n'utiliser que pour du domaine public / freeware / démos). ROMs TOS propriétaires → défaut
**EmuTOS** (libre) dans `rom/`. ⚠ Pour `--machine megaste`, utiliser un EmuTOS **256 Ko**
(`rom/etos256us/fr.img`, build « Mega STe » qui programme le SCU) ou TOS 2.06 : l'EmuTOS
192 Ko est un build « Atari ST » (TOS 1.4) qu'Hatari lui-même refuse sur MegaSTE.
