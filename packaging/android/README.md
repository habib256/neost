# Paquet Android

APK **arm64-v8a**, Android 5.0 (API 21) minimum. `./build_apk.sh [debug|release]`.

## Ce que c'est (et ce que ce n'est pas)

C'est un **troisième frontend**, calqué sur le **web** et non sur le GUI de bureau —
et ce n'est pas un choix esthétique : le GUI de bureau (`src/gui/`) rend en OpenGL **mode immédiat**
(`glBegin`/`GL_QUADS`) et pilote son interface avec `imgui_impl_opengl2`, deux choses
qui n'existent pas sur Android. Le frontend web, lui, rend déjà en **GLES 2** et
produit son son au modèle « push ». `src/android/main_android.cpp` en est la
transposition : même shader, même chaîne audio partagée (`core/AudioMix.cpp`), même
cadence sur le **temps émulé**.

Le cœur (`neost_core`) est repris **tel quel** : il ne dépend ni de GL ni de GLFW.

SDL2 fournit ce que GLFW ne sait pas faire ici : fenêtre, contexte GLES, cycle de vie
(mise en veille), tactile, manettes et sortie audio. Il est **vendorisé mais non
commité** (`extern/SDL2`, comme Hatari) — `fetch_sdl.sh` le récupère.

**État actuel** : la machine démarre sur EmuTOS, l'image et le son sont là, la souris
se pilote au doigt, une manette physique tient le port joystick 1, et un **menu**
permet de changer de disquette, de redémarrer et d'envoyer des touches. Pas encore de
stick virtuel, ni de sélecteur de ROM, ni de réglages machine. Un clavier physique
(Bluetooth, USB) est relayé au ST menu fermé (depuis le 2026-11-12) : symbolique
d'abord, selon le pays du TOS (`gui/StKeysSymbolic`, tables partagées avec le bureau),
positionnel sinon — non vérifié sur un appareil réel.

Le **bouton Retour** d'Android (et le geste de bord, le clic droit sous DeX/ChromeOS)
est piégé (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`) : il **ouvre et ferme le menu**, il ne
quitte jamais — on ne sort que par `QUIT`. Ne pas le confondre avec le `REWIND` du
menu, qui recule la machine.

## Où mettre ses disquettes

La ludothèque scanne **deux** dossiers :

- le stockage **interne** (`…/files/data`) — la disquette embarquée `diskA.st` et les
  ROM ; inatteignable sans root ;
- le stockage **externe propre à l'application**,
  `Android/data/net.gistlabs.neost/files` (`.debug` en plus pour l'APK de debug) —
  c'est là qu'on **copie ses images depuis un ordinateur, par USB (MTP)**, sans
  aucune permission. Le menu affiche ce chemin ; `RESCAN` relit la liste.

⚠ Depuis Android 11, les gestionnaires de fichiers *sur l'appareil* n'ouvrent plus
`Android/data` : la copie se fait depuis un ordinateur. Un import par le sélecteur
de fichiers système (SAF) reste à faire.

## Mises à jour de l'APK et données déballées

Les **ROM EmuTOS** sont redéballées quand le contenu embarqué change : un témoin
`data/.assets-stamp` porte une empreinte FNV-1a des assets ROM (noms, tailles, octets).
Ce n'est pas l'identité de build de `src/BuildInfo.hpp` : `neost_build_info` n'est pas
lié à la bibliothèque Android, et l'empreinte change exactement quand les octets
changent. **`diskA.st` n'est jamais écrasée** si elle existe : le FDC y écrit au fil de
l'eau, c'est la disquette de l'utilisateur. Toutes les copies sont atomiques
(fichier `.tmp` puis `rename`).

## Le menu, décalqué de la borne

Le menu (`src/android/AndroidMenu.cpp`) reprend la **grammaire du menu borne** —
elle a été pensée pour être lue à distance et pilotée sans clavier, ce qui est
exactement la contrainte d'un téléphone :

- voile sombre, machine **en pause** derrière ;
- ludothèque en rangées énormes, le disque inséré en vert, les **suites** du jeu en
  cours (face B, disk 2…) teintées et remontées en tête ;
- **insérer ne redémarre pas** (modèle « vraie machine ») : seul `RESTART` relance ;
- page **clavier** séparée, ancrée en bas, machine qui **tourne** — c'est ce qui
  permet de répondre à un « PRESS SPACE » sans figer le jeu ;
- `REWIND` recule d'environ une seconde et referme le menu. Comme sur le bureau, le
  retour arrière est **éteint par défaut** : la bascule `REWIND: OFF/ON` est en tête du
  menu. Budget de 24 Mo, une vingtaine de secondes. Un refus s'affiche en pied de menu.

Le tri de la ludothèque n'est pas réécrit : `io/MediaScan` est **partagé** avec la
borne (scan borné, détection des suites, ordre de proximité).

Deux écarts assumés avec la borne, dictés par le support : les rangées sont
**tapables** (pas seulement navigables au curseur), et les actions sont sur une
**rangée horizontale** — empilées comme sur un téléviseur, elles ne laissaient que
deux jeux visibles sur un écran de téléphone tenu en paysage.

### Voir le menu sans téléphone

Le menu ne dépend que d'ImGui et de `io/MediaScan` : une cible de bureau le dessine.

```sh
cmake --build build --target neost-menu-preview
./build/neost-menu-preview disks/st 2.0
```

C'est ainsi qu'a été trouvée l'erreur de mise à l'échelle du premier jet (tailles en
pixels multipliées par l'échelle alors que la police l'était déjà : rangées deux fois
trop hautes, deux jeux visibles, actions hors cadre). Sur appareil, ça se serait vu à
la première installation — ici, en dix secondes.

## Construire

```sh
sdkmanager --install "platforms;android-34" "build-tools;34.0.0" \
                     "ndk;27.2.12479018" "cmake;3.22.1"
export ANDROID_HOME=~/android-sdk
export JAVA_HOME=~/jdk17          # cf. le piège jlink ci-dessous
./build_apk.sh debug              # → app/build/outputs/apk/debug/app-debug.apk
```

Le natif est bâti par le **CMakeLists racine** du dépôt (branche `if(ANDROID)`), pas
par un fichier dupliqué ici : une seule définition de `neost_core` pour les quatre
plateformes.

## Pages de 16 Ko (Android 15)

Les appareils Android 15 à pages de **16 Ko** refusent de charger une bibliothèque dont
les segments `LOAD` sont alignés sur 4 Ko. `app/build.gradle` passe donc
`-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` à CMake (NDK r27), et AGP ≥ 8.5.1 aligne
les `.so` non compressées sur 16 Ko dans l'APK. Contrôle après un build :

```sh
unzip -o app/build/outputs/apk/debug/app-debug.apk 'lib/*' -d apk-libs
llvm-readelf -l apk-libs/lib/arm64-v8a/libneost.so | grep LOAD
                         # la colonne Align doit valoir 0x4000 (et non 0x1000)
```

(`llvm-readelf` est dans `$ANDROID_HOME/ndk/<version>/toolchains/llvm/prebuilt/*/bin`.
Même contrôle pour `libSDL2.so` et `libc++_shared.so`.)

**`targetSdk` reste à 34**, volontairement : Google Play exige désormais un
`targetSdk` plus récent pour publier, mais le relever peut imposer une autre version
d'AGP/Gradle — et les pièges ci-dessous montrent ce que coûte un tel décalage. À faire
avec un appareil sous la main, pas à l'aveugle.

## Trois pièges rencontrés

- **`jlink` manquant.** Le plugin Android construit ses « system modules » avec
  `jlink` ; un `openjdk-21-jre-headless` n'en a pas, et l'erreur ne dit pas qu'il
  manque un paquet. Il faut un **JDK complet** (Temurin déballé dans `$HOME` suffit).
- **Gradle 8.1 du gabarit SDL2 refuse le JDK 21.** On épingle donc Gradle 8.9 +
  AGP 8.5.2, décalés par rapport à `extern/SDL2/android-project` — c'est voulu.
- **La garde SDL bloquait le cœur seul.** `-DNEOST_ANDROID_APP=OFF` construit
  `neost_core` + `neost-headless` pour arm64 **sans** SDL : c'est ainsi qu'on valide
  le cœur sur l'architecture cible (cf. ci-dessous).

## Vérifier sans appareil

Le cœur pour arm64 se valide **hors Android**, en le compilant pour ARM64 Linux et en
le lançant sous `qemu-aarch64` : on compare alors sa sortie à celle du x86-64.

```sh
cmake -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=<toolchain-arm64>.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 --target neost-headless -j
qemu-aarch64 -L <sysroot> ./build-arm64/neost-headless roms/etos192us.img \
    --frames 200 --screenshot a.ppm --sound-dump a.wav
./build/neost-headless roms/etos192us.img --frames 200 --screenshot x.ppm --sound-dump x.wav
cmp a.ppm x.ppm && cmp a.wav x.wav      # doit être IDENTIQUE
```

Mesuré le 2026-08-11 : **image et son identiques au bit près**. Perf : 1000 trames ST
(20 s émulées) en 7,1 s sous QEMU — or QEMU coûte lui-même un facteur 5 à 10, donc du
temps réel très confortable sur du silicium ARM réel. À re-mesurer sur appareil.

## Données embarquées, et la licence

`stage_assets.sh` ne copie que **EmuTOS** (GPL), `disks/diskA.st` et les textes de
licence — ~1 Mo, avec un garde-fou qui refuse tout autre fichier. **Aucun TOS Atari**, aucun jeu : la politique
du Play Store est plus stricte que celle des paquets de bureau, qui redistribuent
`tos102uk`/`tos162uk`. L'utilisateur importera ses propres images quand le sélecteur
de fichiers existera.

## Signature

`assembleRelease` produit un APK **non signé** : aucune clé n'est dans le dépôt, et il
n'y en aura pas. Pour publier :

```sh
apksigner sign --ks ma-cle.jks --out neost.apk app-release-unsigned.apk
```

L'APK `debug`, lui, est signé avec la clé de debug d'Android : il s'installe
directement (`adb install -r app-debug.apk`) mais ne se publie pas.

## Reste à faire

Stick et boutons virtuels · import de disquettes (SAF) · sélecteur de ROM · réglages
(machine, RAM, volume, zoom) · sauvegarde d'état · bruits de lecteur · effets CRT
(portage du GLSL de bureau vers ES 3) · **mesures et essais sur appareil réel**.
