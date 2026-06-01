# NeoST

Émulateur Atari ST « boîte à hack » pédagogique — transparence matérielle totale,
modélisation composant par composant de la carte mère.

> (c) 2026 VERHILLE Arnaud

## Philosophie

NeoST modélise le ST puce par puce : le `Bus` *est* le plan mémoire, chaque
composant (`Shifter`, `YM2149`, `Glue`/MMU) est branché dessus comme sur la vraie
carte. Le CPU 68000 n'est **pas** réécrit : on intègre [Musashi](https://github.com/kstenerud/Musashi)
via le wrapper `Cpu68k`.

## Arborescence

```
CMakeLists.txt
src/
  main.cpp            Boucle d'horloge (cycles CPU ↔ lignes Shifter) + UI ImGui
  web/main_web.cpp    Frontend WebAssembly (Emscripten + GLFW + shader WebGL)
  core/
    Bus.hpp/.cpp      Memory map ($0 RAM, $E00000/$FC0000 ROM TOS, $FF8000 MMIO)
    Cpu68k.hpp/.cpp   Wrapper Musashi (68000)
    Shifter.hpp/.cpp  Vidéo : décodage planaire → texture OpenGL
    YM2149.hpp        Son : PSG 16 registres (squelette)
    Glue.hpp          GLUE/MMU : config mémoire + routage MMIO (squelette)
extern/
  Musashi/            sous-module (CPU)
  imgui/              sous-module (UI debug)
```

## Pile UI

GLFW3 + OpenGL (immediate mode, GL 2.1) + Dear ImGui — comme POM1/POM2. Pas de
loader GL (GLEW/GLAD) requis : les symboles legacy sont fournis directement par
`libGL` (Linux) et `OpenGL.framework` (macOS Silicon). L'audio (YM2149) reste un
squelette ; backend conseillé à brancher plus tard : [miniaudio](https://github.com/mackron/miniaudio) (header-only).

## Dépendances

- **GLFW3** — `pacman -S glfw` (CachyOS) / `brew install glfw` (macOS Silicon)
- **OpenGL** — fourni par le système (Mesa / framework Apple)
- **Musashi** et **Dear ImGui** en sous-modules :

```sh
git submodule add https://github.com/kstenerud/Musashi extern/Musashi
git submodule add https://github.com/ocornut/imgui    extern/imgui
# Musashi génère son cœur d'opcodes :
( cd extern/Musashi && cc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c )
```

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./neost                                    # auto : EmuTOS US + disks/diskA.st
./neost rom/etos192fr.img                  # version FR
./neost rom/etos192us.img disks/diskA.st   # ROM + image disquette explicites
```

> Les chemins par défaut (`rom/`, `disks/`) sont résolus relativement au
> répertoire courant ET à l'exécutable, donc `./neost` marche aussi bien depuis
> la racine que depuis `build/`.

## Version WebAssembly (dans le navigateur)

**▶ Essayer NeoST en ligne : <https://habib256.github.io/neost/>**

Les artefacts compilés (`index.html`, `.js`, `.wasm`, `.data`) sont versionnés
dans le dossier **[`wasm/`](wasm/)** — c'est ce dossier que sert GitHub Pages.

> Le déploiement est automatisé par le workflow
> [`.github/workflows/deploy-web.yml`](.github/workflows/deploy-web.yml)
> (recompile `wasm/` au push sur `main`). Pour activer le lien :
> **Settings → Pages → Source = GitHub Actions**. La démo publique n'embarque que
> des contenus **libres** (EmuTOS GPL + `diskA.st` générée) ; les jeux sous
> copyright ne sont pas redistribués — chargez vos propres `.st` via le bouton
> « Charger un .st… ».

![NeoST WASM — boot EmuTOS](web/neost-wasm-emutos.png)

Le même cœur `neost_core` (CPU Musashi inclus) est compilé en WebAssembly via
**[Emscripten](https://emscripten.org/)**. Le frontend web dédié
(`src/web/main_web.cpp`) remplace l'OpenGL immédiat par un shader WebGL et la
boucle temporisée par `emscripten_set_main_loop`. Clic dans l'écran = capture
souris (curseur GEM), `Échap` la libère ; le clavier est routé vers l'IKBD.

Construction locale (nécessite l'[emsdk](https://emscripten.org/docs/getting_started/downloads.html)
activé, `source .../emsdk_env.sh`) — la cible `neost-web` écrit dans `wasm/` :

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DNEOST_WEB_FREE_ONLY=ON
cmake --build build-web -j --target neost-web      # → wasm/index.{html,js,wasm,data}
# Servir en HTTP (les .wasm/.data ne se chargent pas en file://) :
python3 -m http.server -d wasm 8000                # puis ouvrir http://localhost:8000/
```

ROM et disquettes sont embarquées dans le FS virtuel via `--preload-file` ; le
menu déroulant liste les images présentes. Sans `-DNEOST_WEB_FREE_ONLY=ON`, tout
`rom/` et `disks/` est embarqué (confort de test local, mais ne pas committer
le `wasm/` ainsi produit : il contiendrait des contenus sous copyright).

### Disquette (FDC WD1772 + DMA)

Le lecteur A monte une image `.st` (dump de secteurs brut). Une image FAT12 720 Ko
de démonstration est fournie dans `disks/diskA.st`. Dans le GUI, la fenêtre
**Disk Library** liste les `.st` de `disks/` et permet de **monter/éjecter** à
chaud. Sur le bureau, double-clique **Disque A** pour parcourir les fichiers.
Les écritures vont dans l'image en mémoire (pas encore recopiées dans le fichier).

Outils (`tools/`) :

```sh
python3 tools/make_floppy.py                 # (re)génère disks/diskA.st (FAT12 de test)
python3 tools/fetch_disk.py <url planetemu>  # télécharge une disquette de test (scrapling)
```

`fetch_disk.py` récupère une image depuis la zone
[planetemu.net/machine/atari-st](https://www.planetemu.net/machine/atari-st) via
**scrapling** (`pip install scrapling`). ⚠ À n'utiliser que pour des logiciels
auxquels vous avez droit (domaine public, freeware, démos), pour tester l'émulateur.

> Lancer depuis la racine du projet : la ROM par défaut est cherchée en
> `rom/<image>` (chemin relatif au répertoire courant).

## ROM (EmuTOS, libre)

Les vrais TOS Atari sont sous copyright et ne sont pas redistribués ici. NeoST
utilise par défaut **[EmuTOS](https://emutos.sourceforge.io/)** (GPL), placé dans
`rom/` à la racine :

```
rom/
  etos192fr.img   EmuTOS 192 Ko, français  (mappé à $FC0000, par défaut)
  etos192us.img   EmuTOS 192 Ko, US
```

Récupération (déjà fait dans ce dépôt) :

```sh
curl -L -o /tmp/emutos.zip \
  "https://sourceforge.net/projects/emutos/files/emutos/1.4/emutos-192k-1.4.zip/download"
unzip -j /tmp/emutos.zip '*etos192fr.img' '*etos192us.img' -d rom/
```

Options CMake : `-DNEOST_WITH_IMGUI=ON` (défaut), `-DNEOST_WARN_STRICT=ON` (défaut).

## Mode headless & traces (comparaison MAME)

Le cœur émulé (`neost_core`) est sans dépendance graphique : un second exécutable
`neost-headless` exécute la **même machine** sans fenêtre, de façon déterministe,
et produit des journaux fins comparables à la commande `trace` du débogueur MAME.

```sh
./build/neost-headless --frames 50 --trace trace.txt          # PC + désassemblage
./build/neost-headless --frames 50 --trace trace.txt --regs   # + D0-D7/A0-A7/SR
./build/neost-headless --frames 50 --trace trace.txt --irq    # + interruptions prises
./build/neost-headless --until-pc FC0030 --trace -            # vers stdout
./build/neost-headless --frames 250 --screenshot ecran.ppm    # dump framebuffer (PPM)
```

Format de trace (proche de MAME — la séquence de PC est le signal de diff) :

```
FC0030: bra     $fc004e
FC004E: move    #$2700, SR
>>> IRQ niveau 6, vecteur $45        (Timer C du MFP)
```

C'est cet outillage qui a permis de localiser le blocage de boot d'EmuTOS
(auto-vectorisation Musashi au lieu des vecteurs MFP).

### Diff de traces Hatari ↔ NeoST

`tools/trace_diff.py` aligne une trace NeoST et une trace Hatari du **même**
ROM/disquette puis localise la **première divergence** (flux PC *et* registres) :

```sh
./build/neost-headless --frames 200 --trace neost.txt --regs --irq
# Hatari : hatari --trace cpu_disasm,cpu_regs --log-file hatari.txt --tos ... --disk-a ...
python3 tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs
```

La sortie pointe l'instruction (et le registre) où les deux émulateurs cessent
de concorder — la méthode pour débloquer Arkanoid & co.

## Contrôles

| Touche | Action                          |
|--------|---------------------------------|
| F12    | Reset physique virtuel          |
| Échap  | Quitter                         |

Fenêtres ImGui : visualiseur hexa de la RAM + état des registres 68000 en direct.
