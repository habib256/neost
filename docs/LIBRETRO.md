# Libretro — carte de la frontière

(c) 2026 VERHILLE Arnaud. Relevé du 2026-11-24, puis frontière préparée le même jour, avant
toute ligne de code libretro.

Ce document confronte ce que l'API libretro demande à un cœur avec ce que `neost_core`
offre. Il dit ce qui est **prêt**, ce qui reste **à adapter** dans le futur adaptateur, et
les **règles** que cet adaptateur doit suivre. Les items ouverts vivent dans
[`TODO.md`](../TODO.md), § 7, sous « Cœur libretro » ; on les cite ici par leur titre.

## Principe

Un cœur libretro est un **cinquième frontend**, au même titre que le GUI de bureau, le
headless, le web et Android. Il vivrait dans `src/libretro/` et ne ferait que traduire :
`neost_core` ne dépend pas de libretro, comme il ne dépend pas de GLFW. Ce que chaque
frontend recopiait est passé dans le cœur, pour que le cinquième n'en fasse pas une
cinquième copie.

Trois contraintes libretro sont plus dures que celles des frontends existants :

1. **Un seul blob d'état** (`retro_serialize`), appelé à chaque trame par l'exécution
   anticipée (*run-ahead*), le jeu en réseau (*netplay*) et le retour arrière de
   RetroArch. Le blob doit être rapide, de taille fixe, et contenir tout ce qui change.
2. **Un processus, plusieurs jeux** : `retro_load_game` / `retro_unload_game` se
   succèdent sans redémarrer le processus. Rien ne doit fuir d'un jeu au suivant.
3. **Aucune sortie propre** : ni fenêtre, ni périphérique audio, ni `stderr` comme
   canal officiel. L'image, le son et les journaux passent par des rappels.

## La carte, point par point

| Besoin libretro | Ce que NeoST offre | Verdict |
|---|---|---|
| `retro_run` : une trame, puis rendre la main | `Machine::runFrame` ; la coroutine `EmuFiber` rend la main à chaque fin de trame, même au milieu d'un blit HOG | **Prêt** |
| Image `RETRO_PIXEL_FORMAT_XRGB8888` | `Shifter::pixels()` est en ARGB8888 sur `uint32_t` (`src/core/Shifter.hpp:398`) : l'octet haut est ignoré par libretro | **Prêt** |
| Recadrage des bordures | `neost::stContentRegion` (`src/core/Framing.hpp`), verrous dans un `FramingLatch` tenu par l'appelant | **Prêt** |
| Annonce de taille et de cadence (`SET_GEOMETRY`, `SET_SYSTEM_AV_INFO`) | `neost::pacing::AvTracker` (`src/core/Pacing.hpp`) : annonce après trois trames identiques, distingue taille et cadence, `fps()` suit le quartz | **Prêt** |
| Son `audio_sample_batch` en `int16` | `neost::mixEmulatedFrame` (`src/core/AudioMix.hpp`) rend du `float` stéréo ; `AudioPacer::samplesForFrame` avec coussin et file à 0 donne le débit exact sans asservissement (`src/core/Pacing.hpp:185`) | **Prêt** (conversion `float` → `int16` dans l'adaptateur) |
| Horodatage des écritures son | `Machine::armAudioClocks()`, contrat de drainage écrit dans `Machine.hpp` | **Prêt** |
| Bruits du lecteur dans le même flux | `DriveSound` joue par son propre moteur miniaudio | **Ouvert** — « Bruits du lecteur rendus en PCM » |
| `retro_serialize` rapide | `saveState` 0,41 ms, `loadState` 0,52 ms ; avec les disquettes 0,66 et 0,76 ms ; une trame ≈ 1 ms (mesures ci-dessous) | **Prêt** |
| Le blob contient tout ce qui change | `Machine::saveStateWithMedia` / `loadStateWithMedia` : l'état `.state` intact, les deux lecteurs et un CRC | **Prêt**, sauf les écritures STX — « Écritures STX dans l'état autonome » |
| `retro_serialize_size` fixe | Conteneur à longueur inscrite, remplissage final ignoré ; taille constante tant que les médias ne changent pas, sauf GEMDOS (+322 octets au boot sous EmuTOS) | **Prêt** (borne à poser par l'adaptateur) |
| État refusé pendant un blit HOG garé | `saveState` rend faux tant qu'une instruction est garée (`Machine::kParkedReason`) | **À adapter** : `retro_serialize` rend faux, RetroArch saute la trame |
| `retro_load_game` / `retro_unload_game` répétés | Machine reconstruite = même état (gardé par `neost-selftest`) ; restent process-globaux le quartz (`setClocks`), le pays clavier (`stkeys`) et `g_cur` du CPU (A33, § 9) | **Prêt** avec les règles ci-dessous |
| Déterminisme (netplay, rejeu) | `Machine::freezeHostClocks()` : RTC, IKBD et UltraSatan au 1er janvier 2026, 12:00 | **Prêt** |
| Journaux `retro_log_printf` | `neost::log::setSink` (`src/util/Log.hpp`) : tous les journaux du cœur | **Prêt** |
| Clavier (`retro_keyboard_event`, `RETROK_*`) | `Machine::keyEvent(scancode ST, appui)` ; tables de position GLFW et Android, tables par pays sans GLFW (`src/gui/StKeysSymbolic.cpp`) | **Ouvert** — « Clavier : une table de position neutre » |
| Manettes (`RETRO_DEVICE_JOYPAD`) | `Machine::setJoysticks(joy0, joy1)` (IKBD + ports manette STE), bits `stjoy::` sans GLFW (`src/io/StJoyBits.hpp`) | **Prêt** |
| Souris (`RETRO_DEVICE_MOUSE`, relative) | `Machine::mouseEvent(dx, dy, gauche, droit)` | **Prêt** |
| MIDI (`RETRO_ENVIRONMENT_GET_MIDI_INTERFACE`) | ACIA MIDI et ponts hôtes (`src/audio/MidiInHost.*`, `MidiOutHost.*`) | **À adapter** : un pont de plus à côté de CoreMIDI, ALSA et winmm |
| Contenu | `.st/.msa/.dim/.stx/.ipf` par chemin (`Fdc::loadImage`) ; `.prg` par le GEMDOS et le démarrage automatique (`neost_set_autostart` du web) ; cartouche brute ou `.stc` (`Bus::loadCart`) | **Prêt** avec `need_fullpath = true` |
| Interface de changement de disque (`.m3u`) | `Fdc::loadImage(path, drive)` et l'éjection | **À adapter** |
| BIOS dans le dossier système | `Machine::switchTos(path, machine demandée)` | **Prêt** (seul EmuTOS peut accompagner le cœur) |
| Options du cœur | `neost.cfg` analysé par `src/gui/AppConfig.cpp`, déjà dans `neost_core` | **À adapter** : options dérivées des clés de `neost.cfg` |
| Carte mémoire (succès, triches) | `bus.ram` est un `std::vector` **réalloué** par `Machine::reconfigure` | **À adapter** : republier le pointeur après toute reconfiguration |
| Bibliothèque partagée | Option `NEOST_PIC` : cœur PIC à visibilité cachée ; `neost-shared-probe` le prouve (CI Linux x86_64, un seul symbole exporté) | **Prêt** (l'adaptateur ajoute un script de version) |
| Fil unique | `EmuFiber` : ucontext (Linux, macOS), fibres Win32, fil dédié (Android, ASan), JSPI (web) ; libretro-common fournit `libco` | **À adapter** si une plate-forme visée n'a aucun des dorsaux |

## Règles de l'adaptateur

1. **Au chargement d'un jeu** : `setClocks(quartz, phase)` (le quartz est process-global et
   une `Machine` neuve prend celui en service), construire la `Machine`, `switchTos(rom,
   machine demandée)`, `stkeys::setCountryFromTos(bus.rom)` (`src/gui/StKeysSymbolic.cpp`, à lier : il n'est pas
   dans `neost_core`), `armAudioClocks()`,
   `freezeHostClocks()` si le jeu en réseau ou le rejeu est en jeu, puis `hardReset()`.
2. **Chaque trame** : entrées (`keyEvent`, `mouseEvent`, `setJoysticks`), puis
   `cpu.updateIpl()` une fois, `runFrame()`, `mixEmulatedFrame` (même sans sortie : c'est
   le drainage exigé par `armAudioClocks`), `AvTracker::onFrame`.
3. **États** : `saveStateWithMedia`, complété par des zéros jusqu'à la borne annoncée par
   `retro_serialize_size` (la taille d'un premier état plus une marge pour GEMDOS) ;
   `loadStateWithMedia` ignore le remplissage. Couper `fdc.setHostWriteBack(false)` si les
   écritures disquette doivent aller ailleurs que dans le fichier du contenu (dossier de
   sauvegardes du frontend).
4. **Journaux** : `neost::log::setSink` vers `retro_log_printf` avant de construire la
   `Machine`, `setSink(nullptr)` au déchargement.
5. **Cadrage** : un `FramingLatch` par session, `reset()` au changement de jeu.

## Mesures : coût d'un état face à une trame

Le 2026-11-24, `neost_core` Release, Mac arm64, EmuTOS 192 Ko US en ST 1 Mo, 300 trames
jouées puis 300 sauvegardes et 300 reprises en boucle (programme de mesure lié au cœur, état
de 1,87 Mo ; 2,61 Mo avec une disquette de 720 Ko).

| Opération | Avant (CRC octet par octet) | Après | Avec la disquette |
|---|---|---|---|
| `saveState` | 6,3 ms | 0,41 ms | 0,66 ms (`saveStateWithMedia`) |
| `loadState` | 7,6 ms | 0,52 ms | 0,76 ms (`loadStateWithMedia`) |
| Une trame émulée | ≈ 1 ms | ≈ 1 ms | — |

Profil (`sample` macOS) avant correction : le CRC-32 pesait ~80 % des deux opérations. Après,
l'instruction CRC32 d'ARMv8 le calcule en 0,29 ms sur 1,87 Mo ; le chemin portable (x86,
Linux arm64 générique) en 1,4 ms, contre 6,6 ms octet par octet. Les valeurs sont
identiques : les `.state` déjà écrits restent lisibles.

## Ce qui ne change pas

- **Le cœur reste sans dépendance libretro.** Aucun `#include "libretro.h"` hors de
  `src/libretro/`.
- **L'échange du cœur 68000 n'est pas concerné** : l'adaptateur parle à `Machine`, pas à
  Moira. La porte d'échange (`docs/CPU_SWAP_GATE.md`) n'a rien à apprendre de libretro ;
  le cœur libretro, une fois là, devra seulement se rebâtir après l'échange.
- **Rien de public** sur le futur cœur 68000 ni sur le TT et le Falcon dans les notes du
  cœur libretro.
