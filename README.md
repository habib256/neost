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
./build/neost chemin/vers/tos.img
```

Options CMake : `-DNEOST_WITH_IMGUI=ON` (défaut), `-DNEOST_WARN_STRICT=ON` (défaut).

## Contrôles

| Touche | Action                          |
|--------|---------------------------------|
| F12    | Reset physique virtuel          |
| Échap  | Quitter                         |

Fenêtres ImGui : visualiseur hexa de la RAM + état des registres 68000 en direct.
