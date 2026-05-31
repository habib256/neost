# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

NeoST — émulateur Atari ST « boîte à hack » pédagogique. C++17, GLFW3 + OpenGL
(immediate mode) + Dear ImGui pour le GUI, miniaudio pour le son, Musashi (68000)
en sous-module. Cibles : macOS Silicon / CachyOS Linux. Commentaires en français.

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j                 # cibles : neost (GUI), neost-headless, neost_core (lib)
./build/neost                          # auto : dernier ROM (cf. neost.cfg) ou EmuTOS US + disks/diskA.st
./build/neost <rom> <disk.st>          # ROM + disquette explicites
```

Sous-modules requis (cf. README) : `extern/Musashi`, `extern/imgui`,
`extern/miniaudio`. Pour Musashi, générer les opcodes après le clone :
`( cd extern/Musashi && cc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c )`.

Ne PAS faire `rm -rf build` (ça casse le shell de l'utilisateur s'il y est `cd`) ;
un simple `cmake -B build` reconfigure. Sous macOS il n'y a pas de `timeout`.

## Débogage — le mode headless est l'outil n°1

Il n'y a pas de « tests » classiques : la validation se fait via `neost-headless`
(exécution déterministe, sans GUI) qui produit des **traces façon MAME** et des
**captures PPM**. C'est ainsi qu'on diagnostique un TOS/jeu qui bloque :

```sh
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq
tail t.txt                                  # localiser la boucle d'attente (PC qui tourne)
awk '/^FCxxxx:/{...}' t.txt                 # désassembler la zone bloquée
./build/neost-headless <rom> --frames N --screenshot s.ppm   # puis: sips -s format png s.ppm --out s.png
```

Options : `--disk`, `--mono` (moniteur mono → haute rés), `--until-pc HEX`,
`--walk-mouse` (injection souris). Méthode type : tracer → trouver la boucle →
lire la **source EmuTOS** (github.com/emutos/emutos) pour savoir quel registre/
signal manque → l'implémenter. **MAME et Hatari sont les sources de vérité
matérielle.** Plusieurs bugs ont été trouvés ainsi (cf. CHANGELOG.md) : int-ack
vectorisé, GPIP4/5/7, Timer B/C, bus error blitter.

## Architecture (le grand schéma)

Tout est découplé autour de deux idées : **le `Bus` EST le plan mémoire** (il ne
fait que router read8/write8 vers les composants), et **le cœur ne dépend pas du
GUI**.

- **`neost_core`** (lib statique, aucune dépendance GUI) = la carte mère :
  `Bus`, `Cpu68k` (wrapper Musashi), `Shifter` (vidéo, pur décodeur ARGB),
  `Mfp`, `Ikbd`, `Fdc`, `YM2149`, `Glue`, plus `Machine` et `Tracer`.
- **`Machine`** assemble tous les composants, les branche au `Bus`, et encapsule
  `runFrame()` : 313 lignes PAL × 512 cycles, Timer C (200 Hz), Timer B + HBL sur
  les lignes visibles, VBL niveau 4. C'est LE point d'entrée du timing.
- **`neost`** (GUI) et **`neost-headless`** partagent `Machine`. Le GUI ajoute
  GLFW/OpenGL/ImGui/miniaudio et bride à 50 fps réels.

Chaîne d'interruption (subtile) : un composant met à jour le `Mfp` (canal ou
ligne GPIP), puis le `Bus` appelle `cpu->updateIpl()` après l'accès MMIO. Musashi
est compilé avec `M68K_EMULATE_INT_ACK=1` (vecteurs MFP) et
`M68K_INSTRUCTION_HOOK=1` (traceur) — voir `Cpu68k.cpp` (`neostIntAck`,
`neostUpdateIpl`). DEV.md détaille l'extension d'un composant.

## Pièges matériels (vérifiés en debug)

- Le 68000 est **big-endian** : assembler les mots octet par octet (`read16` etc.).
- Les bits d'**entrée** du GPIP (moniteur bit7, ACIA bit4, FDC bit5) sont forcés en
  lecture — ne PAS les laisser écraser par une écriture CPU sur `$FFFA01`.
- Registres MFP aux adresses **impaires** ; en mode software-EOI (VR bit3) le
  handler DOIT effacer l'ISR sinon le canal reste bloqué.
- **Haute résolution = monochrome** (blanc/noir), ignorer la palette couleur.
- **bit7 GPIP = 1 → moniteur couleur (basse rés)** ; 0 → mono (haute rés).

## Disquettes & zone de téléchargement

Le lecteur A monte une image `.st`. Outils (`tools/`) :
- `tools/make_floppy.py` → régénère `disks/diskA.st` (FAT12 720 Ko de test).
- `tools/fetch_disk.py <url>` → télécharge une disquette de test depuis la **zone
  planetemu** (`https://www.planetemu.net/machine/atari-st`) via **scrapling**
  (`pip install scrapling`), dans `disks/`. ⚠ À n'utiliser que pour des logiciels
  auxquels on a droit (domaine public, freeware, démos) — pour tester l'émulateur.

Les ROMs TOS étant propriétaires, le défaut est **EmuTOS** (libre) dans `rom/`.

## Docs

`README.md` (install/usage), `DEV.md` (architecture détaillée + extension),
`CHANGELOG.md` (fonctionnalités & bugs corrigés), `TODO.md` (feuille de route,
inclut l'état Arkanoid et les briques d'accuracy manquantes).
