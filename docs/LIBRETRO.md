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

## Où en est le cœur

Le 2026-11-25, le module existe : `src/libretro/neost_libretro.cpp`, bâti par
`-DNEOST_PIC=ON -DNEOST_BUILD_LIBRETRO=ON` (cible `neost_libretro`), n'exportant que les 25
`retro_*`. Son guide d'utilisation est `packaging/libretro/README.md` ; ses shaders sont
dans `packaging/libretro/shaders/`. Il est jugé sans RetroArch :

- `tools/run_libretro.py` le joue par un frontend ctypes (`tools/libretro_frontend.py`,
  sur le modèle de celui de POM2), un cas par processus, avec les seuls médias libres du
  dépôt : démarrage à 50 et 60 Hz annoncés d'emblée, cadrage, mono, copie privée des
  disquettes, chargement accéléré, programme sur C:, liste `.m3u`, autorun d'une disquette
  non amorçable, machine automatique (redémarrage en STE, sonde rattrapée laissée sur ST),
  état rejoué à l'identique, trames cachées ;
- `tools/check_slang.py` compile chaque étape des shaders en SPIR-V (glslangValidator) et
  contrôle les préréglages ;
- le job « Cœur libretro » de la CI fait les deux, et vérifie les exports.

Ce qu'il ajoute aux règles plus bas, et que les cœurs Hatari n'ont pas :

- **La machine automatique.** Un programme qui lit un périphérique absent (son DMA du STE
  sur un ST…) sans gestionnaire de bus error à lui redémarre sur la machine qui le porte
  (`Cpu68k::lastFault` et `mmioNeedsBetterMachine`, déjà utilisés par le bandeau du GUI).
  Deux signatures : le 68000 arrêté (TOS Atari), ou une faute prise par une instruction en
  RAM quand le vecteur 2 pointe encore dans la ROM (EmuTOS ne s'arrête pas). Les sondes du
  TOS (PC en ROM) et celles d'un programme qui rattrape la faute (vecteur en RAM) passent.
  Stardust (STE seulement, nom TOSEC sans « STE ») démarre ainsi sans réglage.
- **L'autorun des disquettes non amorçables** (`io/FloppyCatalog`) : secteur de boot non
  exécutable, rien dans `\AUTO`, un seul programme de jeu à la racine (utilitaires
  écartés, le nom de l'image départage) → lancé par le démarrage automatique GEMDOS,
  sans rien écrire sur la disquette. Bubble Ghost démarre ainsi.
- **Les disquettes sœurs** (`MediaScan::areSiblingImages`) forment seules la liste : la
  face B de Rainbow Islands est à un R3.
- **Le chargement accéléré** : jusqu'à huit trames émulées par trame de l'hôte tant
  qu'une commande du contrôleur est en cours sur une disquette (`Fdc::floppyCommandActive`)
  et qu'aucune entrée n'est tenue ; le nombre dépend de l'état émulé et des entrées, donc
  se rejoue. *ChuChu Rocket* (330 Ko) atteint son titre en 1 020 trames de l'hôte au lieu
  de 2 190. ⚠ La première version (même jour) suivait le **moteur** : lecteur vide, le
  WD1772 ne voit aucune impulsion d'index et ne coupe jamais le moteur, si bien qu'un
  programme sur C: tournait huit fois trop vite et que le TOS répétait chaque touche.
  Gardé par le cas `fast_floppy_idle`.
- **Les archives** (`util/Archive`, deflate de stb_image) : `.zip` et `.gz` décompressés
  une fois dans `saves/neost/archives/<nom>/`, jamais réécrits ; la liste `.m3u` de
  l'archive, sinon ses disquettes en liste, sinon son programme sur C: (jeu GEMDOS zippé),
  sinon son dossier en C:. `block_extract = true` : RetroArch n'en extrairait qu'un fichier.
- **Les options en français** (`SET_CORE_OPTIONS_V2_INTL`) quand RetroArch l'est.
- **Les ports du netplay** : port 3 « Keyboard (for netplay) », clavier relu à chaque trame
  et rappels ignorés ; port 4 « Mouse (for netplay) ».
- **Un invité rejoint l'hôte** : un état nomme ses disquettes par leur chemin absolu, celui
  des copies privées de l'hôte. `Fdc::setMediaPathResolver` laisse le module retrouver la
  sienne par son nom (copie privée, disquette d'archive, ou disquette de la liste) ; le
  contenu vient de l'état. `tools/libretro_netplay.py` le prouve en deux processus aux
  dossiers distincts, ceux de l'hôte effacés avant que l'invité ne le rejoigne à la trame
  500 : image et RAM identiques pendant 400 trames (CI).
- **Les bruits du lecteur** dans le flux de la trame (`audio/DriveSoundMix`, cœur, sans
  bibliothèque) : échantillons Epson de `roms/drivesound` posés dans
  `system/neost/drivesound`. Le ronron suit l'état du moteur à chaque trame entendue (juste
  après une reprise d'état), les pas sont des événements du FDC joués seulement pour une
  trame entendue (une fois sous anticipation). Lecteur vide : silence, choix d'agrément.
- **La carte mémoire** (`SET_MEMORY_MAPS`, ST-RAM grand-boutiste à 0) republiée après un
  redémarrage sur une autre machine.
- **Les écritures des disquettes protégées dans l'état** (état autonome v2) : une STX/IPF y
  porte son overlay d'écriture au format `.wd1772` (`StxImage::overlayBytes`) ; à la
  reprise, s'il diffère de celui du lecteur, l'image revient à l'origine
  (`StxImage::resetOverlay`, durées de tour d'une IPF gardées) et l'overlay de l'état est
  rejoué, fichier compagnon compris. Un état v1 se relit (overlay laissé tel quel). Les
  sauvegardes d'un jeu protégé ne divergent plus entre joueurs ni sous l'anticipation.
- **Les disques durs** `.hd`/`.vhd` en ACSI 0 (copie privée) : EmuTOS monte la partition
  et lance `C:\AUTO`.
- **Le toucher** : option « Touch moves the GEM pointer » (active sur Android) —
  `RETRO_DEVICE_POINTER` → `Machine::desktopMouseMove`, actions en file pour qu'un tap plus
  court que le trajet du curseur garde son clic. Un double tap ouvre le lecteur A.
- **Un TOS choisi** parmi ceux du dossier système (`TosChoice`), Atari PAL d'abord. Il
  compte : Stunt Car Racer tourne sous TOS 1.02 et 1.04 et retombe au bureau sous
  EmuTOS 192 Ko (vérifié au headless, même résultat).

Restent ouverts au TODO (§ 7, « Cœur libretro ») : le MIDI, le disque dur ACSI dans
l'état, l'essai du module et des shaders dans un vrai RetroArch.

## Positionnement : un cœur ST qui n'est pas Hatari

Relevé du 2026-11-25. Le RetroArch d'aujourd'hui a **deux** cœurs Atari ST, tous deux tirés
d'Hatari 2.6.1 :

- **`hatari`** (libretro/hatari) : `display_version = "tech preview"`, annonce `.ipf` ; sa page
  de documentation libretro coche ✕ pour les états, le netplay et le retour arrière, alors que
  son `.info` dit `savestate = "true"`. Le nom « hatari2014 » n'est pas un cœur : c'est la
  branche de l'ancienne version (`hitari2014-mercurial`).
- **`hatarib`** (bbbradsmith/hatariB, v1.1 du 2026-10-15) : se dit remplaçant du premier. États
  « seamless, allowing run-ahead and netplay », retour arrière, netplay. L'IPF et le CTR exigent
  que l'utilisateur fournisse **capsimg 5.1** (bibliothèque SPS non libre) dans le dossier
  système, avec un détour pour Android. Son README prévient : le netplay peut diverger après
  des accès disque sur une image IPF.

Les états rapides, l'anticipation et le netplay **ne suffisent donc pas** à distinguer NeoST :
hatariB les annonce déjà. Les arguments tiennent si chacun porte sa preuve :

| Argument | Ce qui le prouve dans NeoST | Ce qui manque |
|---|---|---|
| **Un autre moteur** | 68000 Moira, plan mémoire (`Bus`) et puces écrits pour NeoST ; Hatari n'y est qu'une référence lue et un oracle de grade D. Les modules portés d'Hatari (Pasti, `StxImage`) le disent dans leur en-tête. Chaque verdict du harnais nomme son échelon (`docs/REFERENCE_LADDER.md`) : un écart avec Hatari tranché par une fiche technique ou le RTL reste un écart | — |
| **IPF sans bibliothèque à fournir** | Décodeur libre dans le cœur (`src/io/IpfImage.*`, adapté de MAME, BSD-3) : rien à déposer dans le dossier système, Android compris | **Un seul titre IPF validé** (*Hammerfist*) — « Campagne IPF » ; pas de DPLL pendant l'émulation (`docs/IMPLEMENTED.md`, « Images IPF ») |
| **Netplay fiable sur IPF** | Les bits faibles tirent d'un générateur xorshift **sérialisé** (`Fdc::rng_`) : deux pairs relisent les mêmes octets ; les **écritures** sur IPF et STX voyagent dans l'état (v2, 2026-11-25) | — |
| **Déterminisme démontré** | Contrat écrit (`docs/OPENDST.md` § 1), horloges hôte figées (`freezeHostClocks`), `--state-hash-every` et `tools/run_determinism.py` au palier `fast`, reprise d'état comprise | Le déclarer au frontend : `savestate_features = "deterministic"` dans le `.info` |
| **États bon marché** | 0,41 / 0,52 ms par sauvegarde / reprise pour une trame ≈ 1 ms (mesures plus bas) : avec une trame d'anticipation, chaque trame affichée coûte deux trames émulées, une sauvegarde et une reprise, ≈ 2,9 ms | Aucun chiffre en face pour hatariB — « Mesure face à hatariB » |

Rien de cette section ne parle du futur cœur 68000 ni du TT et du Falcon : le cœur libretro se
présente sur ce qu'il fait le jour où il paraît.

## La carte, point par point

| Besoin libretro | Ce que NeoST offre | Verdict |
|---|---|---|
| `retro_run` : une trame, puis rendre la main | `Machine::runFrame` ; la coroutine `EmuFiber` rend la main à chaque fin de trame, même au milieu d'un blit HOG | **Prêt** |
| Image `RETRO_PIXEL_FORMAT_XRGB8888` | `Shifter::pixels()` est en ARGB8888 sur `uint32_t` (`src/core/Shifter.hpp:398`) : l'octet haut est ignoré par libretro | **Prêt** |
| Recadrage des bordures | `neost::stContentRegion` (`src/core/Framing.hpp`), verrous dans un `FramingLatch` tenu par l'appelant | **Prêt** |
| Annonce de taille et de cadence (`SET_GEOMETRY`, `SET_SYSTEM_AV_INFO`) | `neost::pacing::AvTracker` (`src/core/Pacing.hpp`) : annonce après trois trames identiques, distingue taille et cadence, `fps()` suit le quartz | **Prêt** |
| Son `audio_sample_batch` en `int16` | `neost::mixEmulatedFrame` (`src/core/AudioMix.hpp`) rend du `float` stéréo ; `AudioPacer::samplesForFrame` avec coussin et file à 0 donne le débit exact sans asservissement (`src/core/Pacing.hpp:185`) | **Prêt** (conversion `float` → `int16` dans l'adaptateur) |
| Horodatage des écritures son | `Machine::armAudioClocks()`, contrat de drainage écrit dans `Machine.hpp` | **Prêt** |
| Bruits du lecteur dans le même flux | `DriveSound` joue par son propre moteur miniaudio ; `audio/DriveSoundMix` rend du PCM | **Fait** pour libretro ; Android ouvert |
| `retro_serialize` rapide | `saveState` 0,41 ms, `loadState` 0,52 ms ; avec les disquettes 0,66 et 0,76 ms ; une trame ≈ 1 ms (mesures ci-dessous) | **Prêt** |
| Le blob contient tout ce qui change | `Machine::saveStateWithMedia` / `loadStateWithMedia` : l'état `.state` intact, les deux lecteurs (et l'overlay d'écriture d'une STX/IPF depuis la v2) et un CRC | **Prêt**, sauf un disque dur ACSI — « Disque dur ACSI dans l'état » |
| `retro_serialize_size` fixe | Conteneur à longueur inscrite, remplissage final ignoré ; taille constante tant que les médias ne changent pas, sauf GEMDOS (+322 octets au boot sous EmuTOS) | **Prêt** (borne à poser par l'adaptateur) |
| État refusé pendant un blit HOG garé | `saveState` rend faux tant qu'une instruction est garée (`Machine::kParkedReason`) | **Fait** : `retro_serialize` rend faux, RetroArch saute la trame |
| `retro_load_game` / `retro_unload_game` répétés | Machine reconstruite = même état (gardé par `neost-selftest`) ; restent process-globaux le quartz (`setClocks`), le pays clavier (`stkeys`) et `g_cur` du CPU (A33, § 9) | **Prêt** avec les règles ci-dessous |
| Déterminisme (netplay, rejeu) | `Machine::freezeHostClocks()` : RTC, IKBD et UltraSatan au 1er janvier 2026, 12:00 | **Prêt** |
| Journaux `retro_log_printf` | `neost::log::setSink` (`src/util/Log.hpp`) : tous les journaux du cœur | **Prêt** |
| Clavier (`retro_keyboard_event`, `RETROK_*`) | `Machine::keyEvent(scancode ST, appui)` ; tables de position GLFW et Android, tables par pays sans GLFW (`src/gui/StKeysSymbolic.cpp`) | **Ouvert** — « Clavier : une table de position neutre » |
| Manettes (`RETRO_DEVICE_JOYPAD`) | `Machine::setJoysticks(joy0, joy1)` (IKBD + ports manette STE), bits `stjoy::` sans GLFW (`src/io/StJoyBits.hpp`) | **Prêt** |
| Souris (`RETRO_DEVICE_MOUSE`, relative) | `Machine::mouseEvent(dx, dy, gauche, droit)` | **Prêt** |
| MIDI (`RETRO_ENVIRONMENT_GET_MIDI_INTERFACE`) | ACIA MIDI et ponts hôtes (`src/audio/MidiInHost.*`, `MidiOutHost.*`) | **À adapter** : un pont de plus à côté de CoreMIDI, ALSA et winmm |
| Contenu | `.st/.msa/.dim/.stx/.ipf` par chemin (`Fdc::loadImage`) ; `.prg` par le GEMDOS et le démarrage automatique (`neost_set_autostart` du web) ; cartouche brute ou `.stc` (`Bus::loadCart`) | **Prêt** avec `need_fullpath = true` |
| Interface de changement de disque (`.m3u`) | `Fdc::loadImage(path, drive)` et l'éjection | **Fait** : interface étendue, `.m3u` à libellés, disquettes sœurs, R3 |
| BIOS dans le dossier système | `Machine::switchTos(path, machine demandée)` | **Prêt** (seul EmuTOS peut accompagner le cœur) |
| Options du cœur | `neost.cfg` analysé par `src/gui/AppConfig.cpp`, déjà dans `neost_core` | **Fait** : options v2 en quatre catégories, anglais et français |
| Carte mémoire (succès, triches) | `bus.ram` est un `std::vector` **réalloué** par `Machine::reconfigure` | **Fait** pour `retro_get_memory_data` ; le redémarrage sur une autre machine change le pointeur (ouvert) |
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

## L'image : zoom adaptatif et shaders

Relevé du 2026-11-25. C'est là que le cœur doit dépasser les cœurs Hatari, et là que le cœur
libretro de POM2 (`~/src/pom2/libretro`) sert d'étalon. POM2 n'a pas de zoom à résoudre : son
image est fixe (280 ou 560 × 192). Ses shaders, en revanche, sont le niveau à atteindre :

- **cinq passes slang** (`pom2-cable`, `pom2-bloom-h`/`-v`, `pom2-glass`, `pom2-present`),
  portées de la passe CRT du bureau « avec le même code, les mêmes défauts et les mêmes
  préréglages » ; la rémanence relit la sortie précédente de sa propre passe (*feedback*) ;
- **un préréglage par moniteur** (composite, monochrome vert ou blanc, RGB Péritel) ;
- **un préréglage de cœur** (`presets/POM2/POM2.slangp`) que RetroArch charge seul pour POM2,
  une fois les shaders activés ;
- **tous les réglages en direct** (*Shader Parameters*), sous Vulkan, Metal, D3D11/12 et GL core.

NeoST part de plus près qu'il n'y paraît : `CrtEffectStack` est la passe « verre » de POM2,
déjà portée pour le bureau, mais réduite à `CrtParams` : ni la passe de câble analogique, ni
le bloom. Ses trois préréglages (`light`, `arcade`, `phosphor`) ne sont pas des moniteurs ST.

### Zoom adaptatif : ce que libretro permet

Le cœur ne voit pas l'écran : il ne connaît ni la taille ni les proportions de la fenêtre de
RetroArch. La règle du kiosque (la hauteur pilote l'échelle, les bordures latérales en trop
sont rognées selon la largeur de l'écran) ne se transpose donc pas telle quelle. Il reste
deux leviers, et ils suffisent :

1. **Rogner l'image envoyée.** `video_refresh` reçoit un pointeur dans le tampon du Shifter et
   le `pitch` du tampon entier : la région de `stContentRegion` part sans copie, centrée à
   `(largeur − cW) / 2`.
2. **Annoncer la géométrie.** `SET_GEOMETRY` (base et rapport d'aspect) à chaque changement de
   région : RetroArch recalcule son cadre sans réinitialiser le pilote vidéo. `AvTracker`
   n'annonce qu'après trois trames identiques, et `FramingLatch` ne change de cadre que
   franchement, avec hystérésis : quelques annonces par scène au plus.

Les règles qui en découlent :

- **Un maximum qui couvre tout.** `retro_get_system_av_info` annonce `max_width × max_height =
  640 × 400`, le plus grand tampon du Shifter (`geometryFor` : basse résolution 416 × 276 avec
  ses bordures, moyenne 640 × 200, mono 640 × 400). Une géométrie hors de ce cadre forcerait
  `SET_SYSTEM_AV_INFO`, qui réinitialise le pilote. Celui-ci n'est permis qu'au changement de
  cadence (50, 60, 71 Hz), rare.
- **L'aspect se calcule comme au bureau.** `aspect = (cW · sx) / (cH · sy)`, avec `sx = 2` si le
  tampon fait au plus 480 px de large et `sy = 2` s'il fait au plus 300 lignes
  (`src/gui/StScreenView.cpp`). La zone active basse (320 × 200), moyenne (640 × 200) et mono
  (640 × 400) donne alors le même 1,6 : un passage du bureau GEM en moyenne à un jeu en basse
  résolution ne fait pas sauter l'image. C'est le gain le plus visible face au cadre fixe.
- **Le cadre ne suit que les trames montrées.** `stContentRegion` compte son hystérésis en
  trames : l'appeler sur les trames cachées de l'exécution anticipée
  (`GET_AUDIO_VIDEO_ENABLE` sans le bit vidéo) l'accélérerait d'autant. Le latch ne va pas
  dans l'état : il appartient à celui qui regarde, si bien que deux joueurs en réseau peuvent
  cadrer différemment sans diverger.
- **Le latch repart à froid au chargement d'un jeu**, pas à la reprise d'un état : un retour
  arrière sur une démo à bordures ouvertes ne doit pas refaire l'attaque de l'hystérésis.
- **Une option « Cadrage »** : *Adaptatif* (défaut, la règle de `Framing.cpp`), *Zone active
  fixe*, *Cadre entier* (l'équivalent de F10). Limite : la moyenne résolution et la mono n'ont
  pas encore de bordures dans le tampon, si bien que le cadre entier change d'aspect avec
  la résolution.
- **L'échelle entière de RetroArch** porte sur la base annoncée : une zone active de 200
  lignes passe en ×5 sur un écran de 1080 lignes, une démo à bordures ouvertes (276 lignes)
  en ×3. L'écart vient du contenu, le cœur n'y peut rien.

### Shaders : ce qu'un shader slang peut savoir

Un shader slang ne reçoit du cœur que l'image : `OriginalSize`, `SourceSize`, `FrameCount`.
Aucun canal ne lui dit « mode mono » ou « 50 Hz ». Le tampon de NeoST le dit pourtant par sa
taille, parce qu'il garde une ligne par ligne de balayage. Au-delà de 300 lignes, c'est la
mono à 71 Hz ; en deçà, la couleur à 200 lignes, avec ou sans bordures, zoom compris. Le seuil
est celui du bureau.

D'où le plan des shaders :

- **Un préréglage de cœur automatique** (`presets/NeoST/NeoST.slangp`, mécanisme de POM2). Il
  bascule seul entre le **moniteur couleur SC1224** (scanlines entre les 200 lignes, masque à
  points) et le **SM124 monochrome** (blanc, pas de scanlines visibles à 400 lignes, pas de
  masque), selon `OriginalSize.y`. Un jeu qui passe du bureau mono à la couleur change
  d'écran comme sur la machine, quand on rebranchait l'autre moniteur.
- **Des préréglages par sortie** : `neost-sc1224`, `neost-sm124`, `neost-peritel` (RGB sur
  téléviseur Péritel, la sortie des STF en France : la bande passante du câble adoucit la
  moyenne résolution), puis `neost-rf` (modulateur PAL des STFM). Ce dernier demande un codage
  composite PAL qui n'existe nulle part dans le code, ni dans NeoST ni dans POM2 (POM2 code
  du NTSC Apple II) : c'est un chantier à part.
- **Une seule source.** Les passes slang et `CrtEffectStack` partagent le même code et les
  mêmes défauts, comme dans POM2. Le bureau gagne au passage le câble et le bloom.
- **Des réglages sourcés.** Chaque défaut d'un préréglage de moniteur porte son grade, comme le
  reste du code : pas de masque, pas de rémanence, pas de pas de triade sans source. Un
  défaut ajusté à l'œil s'écrit `[F]`.
- **La rémanence se compte en trames présentées.** Le *feedback* est mis à jour à chaque trame
  de l'hôte. Un contenu à 50 Hz sur un écran à 60 Hz voit donc des trames doublées, et une
  image qui mélange deux trames alternées (couleurs par scintillement) bat. Le remède est
  côté RetroArch (cadence exacte du contenu, VRR) ; la notice du cœur doit le dire.
- **Les pilotes.** Le slang couvre Vulkan, Metal, D3D et GL core. Le Raspberry Pi et une
  partie des appareils Android tournent souvent sous le pilote GLES de RetroArch, qui ne lit
  que le GLSL. Une borne Pi est un usage de NeoST : à vérifier, et sinon livrer des `.glslp`.

### Le reste du métier, à hauteur de POM2

Ce que le cœur de POM2 fait en plus de l'image, et dont NeoST a besoin pour égaler son niveau :

| POM2 | Pour NeoST |
|---|---|
| Options rangées en catégories, traduites en français (`SET_CORE_OPTIONS_V2_INTL`) | Les clés de `neost.cfg`, rangées : Système, Image, Son, Entrées, Disquettes |
| Port 3 « clavier pour le netplay » lu comme un périphérique, port 4 « souris pour le netplay » | Le clavier et la souris du ST, indispensables au netplay hors manette |
| Profils de manette par jeu (touches nommées dans *Controls*) | La ludothèque du kiosque et `CompDisks.json` nomment déjà les titres |
| Copies privées des disquettes dans *Save Files*, l'original jamais écrit, noms portables | `fdc.setHostWriteBack(false)` (règle 3) et un dossier par jeu |
| Notification au démarrage : machine, cartes, états disponibles | Machine, TOS, 50/60 Hz, médias montés |
| Témoin du lecteur (`GET_LED_INTERFACE`) ; bruits du lecteur entendus une fois sous anticipation | « Bruits du lecteur rendus en PCM » |
| `.zip`/`.gz` ouverts comme leur contenu ; `.m3u` | « Disquettes `.zip` et `.gz` ouvertes directement » |
| `tools/libretro_netplay.py` en CI : deux instances, mêmes entrées, état identique, rejoindre en cours de partie | Le déterminisme est notre argument : le banc doit exister avant la première annonce |
| CI qui démarre RetroArch avec le module, le tue pendant le boot et vérifie la reprise ; cœur statique Emscripten testé sous Node ; armhf sous QEMU | Les mêmes cibles que nos paquets, plus le web |
| Rapport de compatibilité daté | Une campagne de titres jouée par le module, pas par le headless |

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
