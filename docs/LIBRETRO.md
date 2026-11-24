# Libretro — carte de la frontière

(c) 2026 VERHILLE Arnaud. Relevé du 2026-11-24, avant toute ligne de code libretro.

Ce document confronte ce que l'API libretro demande à un cœur avec ce que `neost_core`
offre aujourd'hui. Il dit ce qui est **prêt**, ce qui est **à adapter** dans le futur
adaptateur, et ce qui est **à préparer** dans NeoST avant de commencer. Les items ouverts
vivent dans [`TODO.md`](../TODO.md), § 7, sous « Cœur libretro — préparer la frontière » ;
on les cite ici par leur titre.

## Principe

Un cœur libretro est un **cinquième frontend**, au même titre que le GUI de bureau, le
headless, le web et Android. Il vivrait dans `src/libretro/` et ne ferait que traduire :
`neost_core` ne dépend pas de libretro, comme il ne dépend pas de GLFW. La préparation
consiste donc surtout à **sortir des frontends ce que chacun recopie**, pour que le
cinquième n'en fasse pas une cinquième copie.

Trois contraintes libretro sont plus dures que celles des frontends existants :

1. **Un seul blob d'état** (`retro_serialize`), appelé à chaque trame par l'exécution
   anticipée (*run-ahead*), le jeu en réseau (*netplay*) et le retour arrière de
   RetroArch. Le blob doit être rapide, de taille stable, et contenir tout ce qui change.
2. **Un processus, plusieurs jeux** : `retro_load_game` / `retro_unload_game` se
   succèdent sans redémarrer le processus. Tout état global doit repartir à neuf.
3. **Aucune sortie propre** : ni fenêtre, ni périphérique audio, ni `stderr` comme
   canal officiel. L'image, le son et les journaux passent par des rappels.

## La carte, point par point

| Besoin libretro | Ce que NeoST a aujourd'hui | Verdict |
|---|---|---|
| `retro_run` : une trame, puis rendre la main | `Machine::runFrame` (`src/core/Machine.hpp:235`) ; la coroutine `EmuFiber` rend la main à chaque fin de trame, même au milieu d'un blit HOG | **Prêt** |
| Image `RETRO_PIXEL_FORMAT_XRGB8888` | `Shifter::pixels()` est déjà en ARGB8888 sur `uint32_t` (`src/core/Shifter.hpp:398`) : l'octet haut est ignoré par libretro | **Prêt** |
| Géométrie variable, recadrage des bordures | `shifter.width()/height()` changent avec la résolution et l'overscan ; `neost::stContentRegion` (`src/core/Framing.cpp`) donne la zone de contenu, mais ses verrous sont des **statiques de fonction** | **À préparer** — « État de processus remis à neuf », « Géométrie et cadence annoncées » |
| Cadence `timing.fps` (50, 60, 71 Hz) | Dérivée de `frameCycles()` à chaque trame (`neost::pacing::frameMillis`) ; aucun signal « la cadence nominale a changé » | **À préparer** — « Géométrie et cadence annoncées » |
| Son `audio_sample_batch` en `int16` | `neost::mixEmulatedFrame` (`src/core/AudioMix.hpp`) rend du `float` stéréo ; `AudioPacer::samplesForFrame` avec coussin et file à 0 donne le débit exact sans asservissement (`src/core/Pacing.hpp:185`) | **Prêt** (conversion `float` → `int16` dans l'adaptateur) |
| Horodatage des écritures son | `psg.setCycleClock` et `dmasnd.setCycleClock` doivent être posés **par le frontend**, sinon la machine est muette — recopié dans quatre frontends | **À préparer** — « Démarrage d'une machine partagé » |
| Bruits du lecteur dans le même flux | `DriveSound` joue par son propre moteur miniaudio (`src/audio/DriveSound.cpp:34-54`) ; le web les joue en Web Audio | **À préparer** — « Bruits du lecteur rendus en PCM » |
| `retro_serialize` rapide | `Machine::saveState` ≈ 5,5 à 7 ms, `loadState` ≈ 7,6 à 13 ms, contre ≈ 1 ms pour émuler une trame (mesure ci-dessous) | **Bloquant pour run-ahead et netplay** — « États sauvegardés assez rapides pour l'exécution anticipée » |
| Le blob contient tout ce qui change | Les **disquettes ne sont pas dans l'état** : le retour arrière les porte à part (`Rewind` + `Fdc::restoreMedia`), et une écriture disquette est recopiée **aussitôt** dans le fichier hôte (`Fdc::writeBack`) | **Bloquant** — « Médias dans l'état sauvegardé » |
| `retro_serialize_size` stable | 1 347 695 octets à 50 et à 300 trames (EmuTOS 192 Ko US, ST) ; jamais vérifié après un changement de disquette, avec un dossier GEMDOS ou une image `.stx` | **À vérifier** — « Taille d'état constante sur une session » |
| État refusé pendant un blit HOG garé | `saveState` rend faux tant qu'une instruction est garée (`Machine::kParkedReason`) | **À adapter** : `retro_serialize` rend faux, RetroArch saute la trame ; à surveiller sur les démos à grands blits |
| `retro_load_game` / `retro_unload_game` répétés | Quartz global (`gQuartz`, `src/core/Pacing.hpp:110`), verrous de cadrage (`src/core/Framing.cpp:28-32`), pays clavier (`g_kbdCountry`, `src/gui/StKeysSymbolic.cpp:19`), `g_cur` du CPU (A33). Le web et Android gardent une `Machine` **statique** et la remettent par `hardReset` plutôt que de la reconstruire | **À préparer** — « État de processus remis à neuf » |
| Déterminisme (netplay, rejeu) | Les horloges RTC, IKBD et UltraSatan partent de l'heure de l'hôte (`Rtc::initFromHostTime`, `Ikbd::initClockFromHostTime`, `src/io/UltraSatan.cpp:73`) ; seul le headless les fige (`src/headless/main_headless.cpp:2701-2712`) | **À préparer** — « Date fixe pour une session déterministe » |
| Journaux `retro_log_printf` | 244 `printf` / `fprintf(stderr, …)` dans `src/core`, `src/io`, `src/util` ; `util/Log.hpp` n'a qu'un interrupteur « verbeux » | **À préparer** — « Journaux du cœur vers un puits » |
| Clavier (`retro_keyboard_event`, `RETROK_*`) | `Machine::keyEvent(scancode ST, appui)` ; tables de position GLFW (`src/gui/StKeys.cpp`) et Android (54 renvois `AKEYCODE_`), tables par pays du TOS sans GLFW (`StKeysSymbolic.cpp`) | **À préparer** — « Clavier : une table de position neutre » |
| Manettes (`RETRO_DEVICE_JOYPAD`) | `ikbd.setJoystick` + `bus.setStePadInput` + `cpu.updateIpl`, recopiés dans trois frontends ; les bits IKBD sont définis dans `src/io/JoystickInput.hpp`, qui inclut GLFW | **À préparer** — « Manettes : un seul point d'entrée » |
| Souris (`RETRO_DEVICE_MOUSE`, relative) | `Machine::mouseEvent(dx, dy, gauche, droit)` | **Prêt** |
| MIDI (`RETRO_ENVIRONMENT_GET_MIDI_INTERFACE`) | ACIA MIDI et ponts hôtes (`src/audio/MidiInHost.*`, `MidiOutHost.*`) | **À adapter** : un troisième pont à côté de CoreMIDI, ALSA et winmm |
| Contenu | `.st/.msa/.dim/.stx/.ipf` par chemin (`Fdc::loadImage`) ; `.prg` par le GEMDOS et le démarrage automatique (`neost_set_autostart` du web) ; cartouche brute ou `.stc` (`Bus::loadCart`) | **Prêt** avec `need_fullpath = true` |
| Interface de changement de disque (`.m3u`) | `Fdc::loadImage(path, drive)` et l'éjection existent | **À adapter** dans l'adaptateur |
| BIOS dans le dossier système | `Bus::loadTos(path)`, `Machine::adjustMachineForTos` ; EmuTOS est libre, les TOS Atari jamais livrés | **Prêt** (règle de livraison : seul EmuTOS peut accompagner le cœur) |
| Options du cœur | `neost.cfg` analysé par `src/gui/AppConfig.cpp`, déjà dans `neost_core` | **À adapter** : options dérivées des clés de `neost.cfg` |
| Carte mémoire (succès, triches) | `bus.ram` est un `std::vector` **réalloué** par `Machine::reconfigure` | **À adapter** : republier le pointeur après toute reconfiguration |
| Bibliothèque partagée | `neost_core` est une bibliothèque **statique sans `-fPIC`** ; Moira exige C++20 ; le réseau se coupe déjà (`NEOST_WITH_NET=OFF`, web et Android) | **À préparer** — « `neost_core` en code indépendant de la position » |
| Fil unique | `EmuFiber` : ucontext (Linux, macOS), fibres Win32, fil dédié (Android, ASan), JSPI (web) ; libretro-common fournit `libco`, dont se servent beaucoup de cœurs | **À adapter** si une plate-forme visée n'a aucun des dorsaux |

## Mesure : coût d'un état face à une trame

Le 2026-11-24, `build/neost-headless` Release, Mac arm64, EmuTOS 192 Ko US en ST,
mode `--server` (états en mémoire). On joue 300 trames, puis on répète 300 fois la même
commande ; le temps de référence (300 trames seules) est soustrait.

| Commande répétée | Avec `disks/diskA.st` monté | Sans disquette |
|---|---|---|
| `save` (`Machine::saveState`) | 7,2 ms | 5,5 ms |
| `load` (`Machine::loadState`) | 12,9 ms | 7,6 ms |
| `run 1` (une trame) | ≈ 0,95 ms, hachage d'écran du serveur compris | — |

L'exécution anticipée d'une trame coûte un `save` et un `load` **par trame affichée** :
13 à 20 ms, soit toute la durée d'une trame PAL, alors qu'émuler la trame en coûte une.
Le profil n'a pas été fait : le contrôle d'en-tête, le CRC32 sur 1,35 Mo, les allocations
et les instantanés de fichiers GEMDOS sont les premiers suspects, pas des causes établies.
Rejouer la mesure : `tools/` n'a pas encore de script dédié ; la recette est le serveur
ci-dessus (`run 300`, puis N fois `save 0` ou `load 0`).

## Ce qui ne change pas

- **Le cœur reste sans dépendance libretro.** Aucun `#include "libretro.h"` hors de
  `src/libretro/`.
- **L'échange du cœur 68000 n'est pas concerné** : l'adaptateur parle à `Machine`, pas à
  Moira. La porte d'échange (`docs/CPU_SWAP_GATE.md`) n'a rien à apprendre de libretro ;
  le cœur libretro, une fois là, devra seulement se rebâtir après l'échange.
- **Rien de public** sur le futur cœur 68000 ni sur le TT et le Falcon dans les notes du
  cœur libretro.
