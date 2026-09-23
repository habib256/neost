# =============================================================================
#  NeoST — Émulateur Atari ST "boîte à hack" pédagogique
#  Cible : compilation croisée CachyOS Linux (x86_64) / macOS Silicon (arm64)
#  Pile UI : GLFW3 + OpenGL + Dear ImGui (immediate mode), comme POM1/POM2.
#
#  Architecture : un cœur SANS GUI (neost_core) partagé par tous les frontends :
#    - neost            : fenêtré (GLFW/OpenGL/ImGui/miniaudio)
#    - neost-headless   : sans interface, traces déterministes pour diff MAME
#    - neost-web        : WebAssembly (Emscripten + WebGL), cf. wasm/
#    - APK Android      : SDL2 + GLES2, cf. packaging/android/
#  Plus neost_net (backends réseau hôte, hors cœur) et neost-selftest (logique pure).
#
#  (c) 2026 VERHILLE Arnaud — code généré dans le cadre du projet NeoST.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(NeoST
    VERSION 0.6.1
    DESCRIPTION "Emulateur Atari ST orienté transparence matérielle"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
# Garder les drapeaux Release fournis par CMake, la toolchain ou l'appelant.
# Écraser CMAKE_CXX_FLAGS_RELEASE ici rendait par exemple
# -DCMAKE_CXX_FLAGS_RELEASE="-O0 -g" totalement inopérant.

# Version affichée par `--version` et par l'« À propos ». Les scripts d'empaquetage
# passent -DNEOST_VERSION_STR=<tag> pour que le binaire livré annonce la MÊME version
# que le nom du paquet : sans cela tous les paquets d'une release 1.2.3 se présentent
# comme la version figée du project() ci-dessus, et aucun rapport de bug utilisateur
# n'est rattachable à une release.
set(NEOST_VERSION_STR "${PROJECT_VERSION}" CACHE STRING "Version affichée par les binaires")

# ⚠ PIÈGE DE CACHE, déjà payé une fois. Étant une variable de CACHE, NEOST_VERSION_STR
# garde À JAMAIS la valeur du PREMIER `cmake -B build` : bumper le project() ci-dessus
# ne change rien, et `--version` ment jusqu'à ce qu'on repasse -D à la main. On mémorise
# donc le PROJECT_VERSION qui a servi de graine ; s'il a changé ET que la valeur en cache
# est toujours cette graine (donc personne ne l'a surchargée), on ré-ensemence.
# Un -DNEOST_VERSION_STR=<tag> explicite diffère de la graine : il est PRÉSERVÉ.
if(NOT NEOST_VERSION_SEED STREQUAL PROJECT_VERSION)
    if(NEOST_VERSION_SEED AND NEOST_VERSION_STR STREQUAL NEOST_VERSION_SEED)
        set(NEOST_VERSION_STR "${PROJECT_VERSION}" CACHE STRING "" FORCE)
        message(STATUS "NeoST: version ré-ensemencée ${NEOST_VERSION_SEED} -> ${PROJECT_VERSION}")
    endif()
    set(NEOST_VERSION_SEED "${PROJECT_VERSION}" CACHE INTERNAL "PROJECT_VERSION ayant ensemencé NEOST_VERSION_STR")
endif()

# Identité de build (commit git + empreinte des modifications + horodatage), regénérée À
# CHAQUE BUILD par cmake/BuildInfo.cmake — le fichier n'est réécrit (donc recompilé) que si
# l'identité change. Liée à neost et neost-headless : `--version` et `hello` l'annoncent.
set(NEOST_BUILD_INFO_CPP "${CMAKE_BINARY_DIR}/build_info.cpp")
execute_process(COMMAND "${CMAKE_COMMAND}" -DSRC_DIR=${CMAKE_SOURCE_DIR}
                        -DOUT=${NEOST_BUILD_INFO_CPP} -P "${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cmake")
add_custom_target(neost_build_info_gen
    COMMAND "${CMAKE_COMMAND}" -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -DOUT=${NEOST_BUILD_INFO_CPP} -P "${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cmake"
    BYPRODUCTS "${NEOST_BUILD_INFO_CPP}"
    COMMENT "NeoST: refreshing build identity" VERBATIM)
add_library(neost_build_info STATIC "${NEOST_BUILD_INFO_CPP}")
target_include_directories(neost_build_info PUBLIC "${CMAKE_SOURCE_DIR}/src")
add_dependencies(neost_build_info neost_build_info_gen)

option(NEOST_WITH_IMGUI  "Greffe Dear ImGui (visualiseur hexa + registres)" ON)
option(NEOST_WARN_STRICT "Compilation stricte (-Wall -Wextra -Wpedantic)"   ON)
# Réseau hôte : sockets réelles (modem Hayes, anneau MIDI). OFF → aucune socket
# n'est compilée — c'est le cas forcé des cibles WASM et Android, qui n'ont ni
# threads POSIX complets ni intérêt v1.
option(NEOST_WITH_NET "Extensions réseau : sockets hôte (modem Hayes, anneau MIDI)" ON)
if(EMSCRIPTEN OR ANDROID)
    set(NEOST_WITH_NET OFF)
endif()

set(NEOST_STRICT_OPTS "")
if(NEOST_WARN_STRICT)
    set(NEOST_STRICT_OPTS "-Wall;-Wextra;-Wpedantic")
endif()

# =============================================================================
#  Bibliothèque cœur — AUCUNE dépendance GUI (ni GL ni GLFW).
#  Contient la carte mère + le CPU (Moira, cycle-exact) + le traceur.
# =============================================================================
add_library(neost_core STATIC
    src/core/Bus.cpp
    src/core/Cpu68k.cpp
    src/core/Shifter.cpp
    # A32 : Shifter.cpp portait SIX rôles en 2 917 lignes. Découpé par rôle —
    # la machine à états des bordures et le compteur vidéo ont leur fichier.
    src/core/VideoGlue.cpp
    src/core/VideoCounter.cpp
    src/core/YM2149.cpp
    src/core/DmaSound.cpp
    src/core/AudioMix.cpp
    src/core/Blitter.cpp
    src/core/Machine.cpp
    src/core/Tracer.cpp
    src/core/Symbols.cpp
    src/io/Mfp.cpp
    src/io/Ikbd.cpp
    src/io/Fdc.cpp
    src/io/StxImage.cpp
    src/io/Rtc.cpp
    src/io/MidiAcia.cpp
    src/io/Fpu.cpp
    src/io/GemdosHd.cpp
    src/io/Acsi.cpp
    src/io/Scc.cpp
    src/io/MediaScan.cpp
    src/io/Ne2000.cpp
    src/io/UltraSatan.cpp
    src/io/Isp1160.cpp
    src/io/CartridgeKey.cpp
    src/io/PortDevices.cpp
    src/io/DongleTable.cpp
    src/core/Framing.cpp
    src/util/HostPath.cpp
    src/util/ConfigPath.cpp   # A36 : où vit neost.cfg (règle testable)
    src/gui/AppConfig.cpp)

target_include_directories(neost_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
# (Cpu68k.cpp est traité à part : il inclut l'en-tête tiers Moira.h → C++20 sans
#  -Wpedantic strict, cf. le bloc Moira plus bas.)
set_source_files_properties(
    src/core/Bus.cpp src/core/Shifter.cpp src/core/VideoGlue.cpp
    src/core/VideoCounter.cpp src/core/YM2149.cpp src/core/DmaSound.cpp
    src/core/Blitter.cpp
    src/core/Machine.cpp src/core/Tracer.cpp src/core/Symbols.cpp src/io/Mfp.cpp src/io/Ikbd.cpp src/io/Fdc.cpp src/io/StxImage.cpp src/io/Rtc.cpp src/io/MidiAcia.cpp src/io/Fpu.cpp src/io/GemdosHd.cpp src/io/Acsi.cpp src/io/Scc.cpp src/io/MediaScan.cpp src/io/Ne2000.cpp src/io/UltraSatan.cpp src/io/Isp1160.cpp src/io/CartridgeKey.cpp src/io/PortDevices.cpp src/io/DongleTable.cpp src/core/Framing.cpp src/util/HostPath.cpp src/util/ConfigPath.cpp src/gui/AppConfig.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost_net — couche réseau hôte (HORS cœur : c'est ici que vivent sockets et
#  threads). Toujours bâtie ; les sources à sockets n'entrent que si
#  NEOST_WITH_NET.
# =============================================================================
add_library(neost_net STATIC
    src/net/SlirpBackend.cpp)
target_link_libraries(neost_net PUBLIC neost_core)

# libslirp (BSD-3-Clause — corrigé le 2026-08-28 : ce commentaire annonçait
# « LGPL 2.1+ », la licence du fichier LICENSE livré avec la bibliothèque est bien
# BSD-3-Clause) : NAT en espace utilisateur — l'accès Internet RÉEL de la
# NE2000 émulée (NetUSBee/EtherNEC), sans root ni pcap. Optionnel : sans lui,
# SlirpBackend compile en coquille vide et l'option reste grisée.
#
# ⚠ VERSION MINIMALE 4.7, et un plancher EXPLICITE. SlirpBackend appelait
# slirp_pollfds_fill_socket() (libslirp >= 4.8) sans que rien ne le vérifie : toute
# machine portant la libslirp de sa distribution — Debian 12 bookworm = Raspberry
# Pi OS, Ubuntu 24.04, toutes en 4.7.0 — échouait à COMPILER NeoST, sans aucun
# moyen de refuser la fonctionnalité. SlirpBackend accepte désormais les DEUX API
# (cf. son bloc SLIRP_CONFIG_VERSION_MAX) ; le plancher reste ici pour donner un
# diagnostic net, et NEOST_WITH_SLIRP permet de trancher à la main.
#
#   AUTO (défaut) : activé si une libslirp >= 4.8 est là, sinon ignoré en silence.
#   ON            : exigé — la configuration ÉCHOUE si elle manque (CI, paquets).
#   OFF           : jamais compilé, même si la bibliothèque est présente.
set(NEOST_WITH_SLIRP "AUTO" CACHE STRING
    "Internet réel pour la NE2000 via libslirp >= 4.7 : AUTO, ON ou OFF")
set_property(CACHE NEOST_WITH_SLIRP PROPERTY STRINGS AUTO ON OFF)

if(NOT NEOST_WITH_SLIRP STREQUAL "OFF")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SLIRP QUIET "slirp>=4.7")
    endif()
endif()
if(SLIRP_FOUND)
    target_compile_definitions(neost_net PUBLIC NEOST_WITH_SLIRP=1)
    target_include_directories(neost_net PUBLIC ${SLIRP_INCLUDE_DIRS})
    target_link_libraries(neost_net PUBLIC ${SLIRP_LINK_LIBRARIES})
    message(STATUS "NeoST: libslirp ${SLIRP_VERSION} trouvée — Internet réel pour la NE2000")
elseif(NEOST_WITH_SLIRP STREQUAL "ON")
    # Diagnostic UTILE : distinguer « absente » de « trop vieille » évite de
    # chercher un paquet déjà installé.
    if(PkgConfig_FOUND)
        pkg_check_modules(SLIRP_ANY QUIET slirp)
    endif()
    if(SLIRP_ANY_FOUND)
        message(FATAL_ERROR "NeoST: NEOST_WITH_SLIRP=ON mais libslirp ${SLIRP_ANY_VERSION} "
                            "est trop ancienne (4.7 minimum).")
    else()
        message(FATAL_ERROR "NeoST: NEOST_WITH_SLIRP=ON mais libslirp est introuvable "
                            "(paquet libslirp-dev / libslirp-devel, 4.7 minimum).")
    endif()
elseif(NEOST_WITH_SLIRP STREQUAL "OFF")
    message(STATUS "NeoST: libslirp désactivée (NEOST_WITH_SLIRP=OFF)")
else()
    message(STATUS "NeoST: libslirp >= 4.7 absente — NE2000 en boucle locale seulement")
endif()
set(NEOST_NET_SRC)
if(NEOST_WITH_NET)
    target_sources(neost_net PRIVATE src/net/Socket.cpp
                                     src/net/HayesModem.cpp src/net/MidiRing.cpp)
    list(APPEND NEOST_NET_SRC src/net/Socket.cpp
                              src/net/HayesModem.cpp src/net/MidiRing.cpp)
    target_compile_definitions(neost_net PUBLIC NEOST_WITH_NET=1)
    find_package(Threads QUIET)
    if(Threads_FOUND)
        target_link_libraries(neost_net PUBLIC Threads::Threads)
    endif()
    if(WIN32)
        target_link_libraries(neost_net PUBLIC ws2_32)
    endif()
endif()
set_source_files_properties(${NEOST_NET_SRC}
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# --- Moira (cœur 68000 cycle-exact, MIT) — SEUL cœur CPU de NeoST ------------
# Vendorisé (plus un sous-module) car NeoST patche Moira — cf. extern/moira/NEOST_VENDOR.md.
# Upstream : https://github.com/dirkwhoffmann/Moira
# L'ancien cœur Musashi (rapide mais NON cycle-exact) a été retiré : Moira est
# désormais le seul cœur 68000, et il est REQUIS pour bâtir NeoST.
# Moira nécessite C++20 (std::rotr/rotl, std::map::contains) → on compile ses
# sources ET Cpu68k.cpp (qui inclut Moira.h) en C++20, le reste reste en C++17.
set(MOIRA_SRC_DIR "${CMAKE_SOURCE_DIR}/extern/moira/Moira")
set(MOIRA_BUILD_DIR "${CMAKE_BINARY_DIR}/generated/moira/Moira")
if(EXISTS "${MOIRA_SRC_DIR}/Moira.cpp")
    file(MAKE_DIRECTORY "${MOIRA_BUILD_DIR}")
    file(GLOB _moira_files CONFIGURE_DEPENDS "${MOIRA_SRC_DIR}/*")
    foreach(_moira_file IN LISTS _moira_files)
        if(NOT IS_DIRECTORY "${_moira_file}")
            get_filename_component(_moira_name "${_moira_file}" NAME)
            configure_file("${_moira_file}" "${MOIRA_BUILD_DIR}/${_moira_name}" COPYONLY)
        endif()
    endforeach()

    # NeoST EXIGE Moira en mode cycle-exact : PRECISE_TIMING=true (IPL échantillonné
    # à la frontière de cycle, sync avant chaque accès) + MIMIC_MUSASHI=false (priorité
    # à la précision plutôt qu'à la compat avec l'ancien Musashi). Ces réglages vivent
    # dans MoiraConfig.h ;
    # on les applique sur la COPIE générée pour garder le sous-module upstream propre.
    set(_moira_cfg "${MOIRA_BUILD_DIR}/MoiraConfig.h")
    file(READ "${_moira_cfg}" _moira_cfg_txt)
    string(REGEX REPLACE "#define[ \t]+MOIRA_PRECISE_TIMING[ \t]+(true|false)"
        "#define MOIRA_PRECISE_TIMING true" _moira_cfg_new "${_moira_cfg_txt}")
    string(REGEX REPLACE "#define[ \t]+MOIRA_MIMIC_MUSASHI[ \t]+(true|false)"
        "#define MOIRA_MIMIC_MUSASHI false" _moira_cfg_new "${_moira_cfg_new}")
    # ERREUR D'ADRESSE 68000 (exception 3, accès word/long impair) : indispensable —
    # des protections/cracktros s'en servent délibérément (TDA Rick Dangerous installe
    # son handler et provoque des accès impairs). Hatari l'émule (exception3_*).
    string(REGEX REPLACE "#define[ \t]+MOIRA_EMULATE_ADDRESS_ERROR[ \t]+(true|false)"
        "#define MOIRA_EMULATE_ADDRESS_ERROR true" _moira_cfg_new "${_moira_cfg_new}")
    if(NOT _moira_cfg_new STREQUAL _moira_cfg_txt)
        file(WRITE "${_moira_cfg}" "${_moira_cfg_new}")
        message(STATUS "Moira : MoiraConfig.h configuré dans ${MOIRA_BUILD_DIR}.")
    endif()

    target_sources(neost_core PRIVATE
        "${MOIRA_BUILD_DIR}/Moira.cpp"
        "${MOIRA_BUILD_DIR}/MoiraDebugger.cpp")
    target_include_directories(neost_core PUBLIC "${MOIRA_BUILD_DIR}")
    target_compile_definitions(neost_core PUBLIC NEOST_HAS_MOIRA=1)
    set_source_files_properties("${MOIRA_BUILD_DIR}/Moira.cpp" "${MOIRA_BUILD_DIR}/MoiraDebugger.cpp"
        PROPERTIES COMPILE_OPTIONS "-std=c++20")
    # Cpu68k.cpp inclut Moira.h (en-tête tiers) → C++20, sans -Wpedantic strict.
    set_source_files_properties(src/core/Cpu68k.cpp
        PROPERTIES COMPILE_OPTIONS "-std=c++20")
else()
    message(FATAL_ERROR
        "Moira introuvable (extern/moira/Moira) — c'est le SEUL cœur 68000 de NeoST et il est requis. "
        "Moira est vendorisé (cf. extern/moira/NEOST_VENDOR.md) : restaurez-le depuis l'historique Git "
        "(git checkout -- extern/moira) ou réclonez le dépôt NeoST.")
endif()

# =============================================================================
#  neost-headless — sans interface, pour les traces comparables à MAME.
# =============================================================================
# ALSA (Linux) : port MIDI VIRTUEL « NeoST MIDI OUT » auquel n'importe quel synthé
# logiciel s'abonne (FluidSynth, Qsynth, un DAW, un expandeur matériel). C'est
# l'équivalent du port CoreMIDI de macOS — sans lui, la sortie MIDI de l'ACIA
# n'atteignait RIEN sous Linux, et l'interface proposait une case morte.
# Optionnel : sans libasound, le port reste simplement indisponible.
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ALSA QUIET alsa)
    endif()
    if(ALSA_FOUND)
        message(STATUS "NeoST: ALSA ${ALSA_VERSION} trouvée — port MIDI virtuel disponible")
    else()
        message(STATUS "NeoST: ALSA absente — pas de port MIDI virtuel (paquet libasound2-dev)")
    endif()
endif()

# MidiInHost (et LUI SEUL des deux ponts MIDI hôtes) : --midi-in-device fait entrer
# un appareil réel dans le MIDI IN du ST. Le pendant sortant reste hors du headless —
# --midi-dump capture déjà ce que le ST émet, sans dépendre du matériel branché, et
# lier MidiOutHost tirerait AudioToolbox (synthé GM) dans une cible qui n'a pas d'audio.
add_executable(neost-headless src/headless/main_headless.cpp src/audio/MidiInHost.cpp src/headless/Server.cpp)
target_link_libraries(neost-headless PRIVATE neost_net)  # neost_core via neost_net (PUBLIC)
if(APPLE)
    target_link_libraries(neost-headless PRIVATE "-framework CoreFoundation" "-framework CoreMIDI")
endif()
if(ALSA_FOUND)
    target_compile_definitions(neost-headless PRIVATE NEOST_MIDI_ALSA=1)
    target_include_directories(neost-headless PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(neost-headless PRIVATE ${ALSA_LINK_LIBRARIES})
endif()
# winmm (Windows) : appareils MIDI hôtes réels, donc --midi-list et --midi-in-device
# vivants. Rien à détecter — winmm fait partie de Windows depuis toujours.
if(WIN32)
    target_compile_definitions(neost-headless PRIVATE NEOST_MIDI_WINMM=1)
    target_link_libraries(neost-headless PRIVATE winmm)
endif()
target_compile_definitions(neost-headless PRIVATE NEOST_VERSION="${NEOST_VERSION_STR}")
target_link_libraries(neost-headless PRIVATE neost_build_info)
set_source_files_properties(src/headless/main_headless.cpp src/headless/Server.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost-selftest — auto-test de la LOGIQUE PURE (chemins hôte, parseurs, formats).
#
#  Le pendant des auto-tests du headless pour tout ce qui n'a besoin NI de machine
#  NI de ROM. Bâti par défaut (pas EXCLUDE_FROM_ALL) et lancé par le palier `fast`
#  de tools/run_all.py : sans cela il pourrirait, comme tout test qu'aucun script
#  n'exécute. Ne dépend que de neost_core.
#
#  ⚠ Il exerce la sémantique de chemins WINDOWS depuis n'importe quelle plateforme
#  (hostpath::Style est un paramètre d'exécution) : c'est la seule façon d'attraper
#  sur un Mac ou une CI Linux la classe de bugs de l'issue #37.
# =============================================================================
# MidiInHost y entre pour son TAMPON DE GIGUE : le drainage vers l'ACIA (n'injecter
# que ce que le 6850 accepte, perdre le NEUF en saturation) est de la logique pure,
# testable sans le moindre appareil branché — et c'est précisément la partie qui
# casse silencieusement, un débordement de 6850 mangeant le statut d'un message.
# GmSynth y entre pour sa TABLE DE VÉRITÉ (banque livrée roms/gm + datation au
# cycle du rendu) : c'est le garde du synthé GM intégré des plateformes non-macOS.
# AppConfig y entre pour son ÉCRITURE : `writeConfigAtomic` est la seule partie de
# la chaîne neost.cfg qui touche le disque, et c'est là que le défaut d'A12 s'est
# logé (le dossier de destination n'était jamais créé) alors que la règle PURE
# d'A36, elle, était testée. Le fichier ne dépend pas d'ImGui — il compile tel quel
# contre neost_core.
add_executable(neost-selftest tests/selftest_logic.cpp src/audio/MidiInHost.cpp
    src/audio/GmSynth.cpp src/gui/AppConfig.cpp)
target_link_libraries(neost-selftest PRIVATE neost_core)
target_include_directories(neost-selftest PRIVATE "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/extern/tsf")
if(APPLE)
    # Le backend CoreMIDI est compilé dans le fichier, donc ses symboles doivent se
    # résoudre. Pas d'ALSA ici À DESSEIN : le test ne vise que le tampon de gigue, qui
    # vit hors des #if de backend — l'y lier n'ajouterait aucune couverture.
    target_link_libraries(neost-selftest PRIVATE "-framework CoreFoundation" "-framework CoreMIDI")
endif()
# Même raison sous Windows : le backend winmm est compilé DANS MidiInHost.cpp, ses
# symboles doivent donc se résoudre. Le test, lui, ne vise que le tampon de gigue et
# les fonctions pures (dont shortMessageLength, la table de vérité de winmm).
if(WIN32)
    target_compile_definitions(neost-selftest PRIVATE NEOST_MIDI_WINMM=1)
    target_link_libraries(neost-selftest PRIVATE winmm)
endif()
set_source_files_properties(tests/selftest_logic.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost-fuzz-disk — FUZZING des parseurs d'images disquette (chantier A30).
#
#  decodeMsa / decodeDim / StxImage::parse sont les seules fonctions du projet qui
#  digèrent un fichier venu de l'EXTÉRIEUR. Driver DÉTERMINISTE (PRNG semé), pas
#  libFuzzer : le clang d'Apple ne livre pas libclang_rt.fuzzer, un harnais
#  libFuzzer serait donc du code que personne n'exécute sur la plateforme de
#  développement. Bâti par défaut et lancé par le palier `fast` — la leçon d'A20
#  (un test qu'aucun script n'invoque est du code mort) s'applique ici aussi.
#  Il devient MORDANT dans le job `sanitizers` de la CI (ASan/UBSan).
# =============================================================================
add_executable(neost-fuzz-disk tests/fuzz_diskimage.cpp)
target_link_libraries(neost-fuzz-disk PRIVATE neost_core)
target_include_directories(neost-fuzz-disk PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_source_files_properties(tests/fuzz_diskimage.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost-stx-test — test autonome de la ré-interprétation WRITE TRACK (StxImage).
#  Ne dépend QUE de StxImage (aucun GUI, aucun Moira). Lancer :
#    ./build/neost-stx-test "disks/stx/Rick Dangerous - Firebird (UK) (France).stx"
# =============================================================================
# A20 (2026-08-27) : EXCLUDE_FROM_ALL retiré — ce test était du code MORT (jamais
# compilé, jamais invoqué) pendant que StxImage.cpp (510 l. de parseur binaire exposé
# à des fichiers arbitraires) n'avait aucun autre test. Branché au palier fast de
# tools/run_all.py, sur une image STX FORGÉE (aucune dépendance à un disque commercial).
add_executable(neost-stx-test tests/stx_writetrack_test.cpp src/io/StxImage.cpp)
target_include_directories(neost-stx-test PRIVATE "${CMAKE_SOURCE_DIR}/src")
# ⚠ Le test vérifie par assert() : le -DNDEBUG du profil Release les VIDERAIT tous
# (vert creux garanti). On les réactive pour CETTE cible uniquement.
target_compile_options(neost-stx-test PRIVATE -UNDEBUG)

# =============================================================================
#  neost (Android) — bibliothèque native chargée par SDL2 (libneost.so).
#
#  Même logique que la branche EMSCRIPTEN ci-dessous : on réutilise neost_core
#  TEL QUEL et on ne construit QUE le frontend de la plateforme — jamais les
#  cibles de bureau, qui dépendent de GLFW et de l'OpenGL en mode immédiat,
#  absents d'Android. La couche plateforme (fenêtre, contexte GLES, tactile,
#  manettes, audio, cycle de vie) vient de SDL2, vendorisé dans extern/SDL2.
#
#  Ce fichier n'est PAS lancé à la main : c'est Gradle qui l'appelle, via
#  packaging/android (externalNativeBuild). Cf. packaging/android/README.md.
# =============================================================================
if(ANDROID)
    # OFF = on ne bâtit que le cœur (et neost-headless, défini plus haut) pour
    # arm64, sans SDL : c'est ainsi qu'on valide le CŒUR sur l'architecture cible
    # — sous qemu-aarch64 ou sur appareil — sans dépendre de la couche graphique.
    option(NEOST_ANDROID_APP "Android : bâtir l'application SDL (sinon cœur seul)" ON)
    set(NEOST_SDL_DIR "${CMAKE_SOURCE_DIR}/extern/SDL2" CACHE PATH "Source SDL2 (Android)")
    if(NOT NEOST_ANDROID_APP)
        return()
    endif()
    if(NOT EXISTS "${NEOST_SDL_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "SDL2 absent de ${NEOST_SDL_DIR} — lancer packaging/android/fetch_sdl.sh")
    endif()
    add_subdirectory("${NEOST_SDL_DIR}" SDL2-build)

    # Dear ImGui pour le MENU (grammaire du menu borne). Backends SDL2 +
    # OpenGL3 : ce dernier sait produire du GLSL ES 1.00, donc il tourne sur le
    # contexte GLES2 de l'application.
    set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")
    if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
        message(FATAL_ERROR "Dear ImGui absent (extern/imgui) — git submodule update --init")
    endif()

    # SDL charge « libSDL2.so » puis la bibliothèque de l'application ; le nom est
    # déclaré côté Java (NeoSTActivity.getLibraries) — ici, « neost ».
    add_library(neost SHARED
        src/android/main_android.cpp
        src/android/AndroidMenu.cpp
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(neost PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_compile_definitions(neost PRIVATE IMGUI_IMPL_OPENGL_ES2=1)
    target_link_libraries(neost PRIVATE neost_core SDL2::SDL2 SDL2::SDL2main
                                        GLESv2 EGL android log)
    # -Wpedantic sur NOS sources seulement : ImGui et SDL ont leurs propres usages.
    set_source_files_properties(src/android/main_android.cpp src/android/AndroidMenu.cpp
        PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")
    return()   # sous Android on ne construit QUE le cœur + le frontend SDL
endif()

# =============================================================================
#  neost-web — frontend WebAssembly (Emscripten + GLFW3 port + WebGL).
#
#  Réutilise tel quel neost_core ; remplace l'OpenGL immédiat par un shader
#  WebGL et la boucle sleep par emscripten_set_main_loop. ROM + disquettes sont
#  embarquées dans le FS virtuel via --preload-file. Construire avec emcmake :
#    emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
#    cmake --build build-web -j           # → build-web/index.html (+ .js/.wasm/.data)
#  puis servir le dossier en HTTP (les .wasm/.data ne se chargent pas en file://).
# =============================================================================
if(EMSCRIPTEN)
    # EXCEPTIONS : Moira SIGNALE les bus/address errors du 68000 en LANÇANT
    # (`throw moira::BusError` dans Cpu68k::raiseBusError, `DoubleFault`, et le
    # `catch` de Moira::processException). Emscripten neutralise les `catch` par
    # défaut : le premier `throw` appelait donc `__cxa_throw` → `abort()`, soit
    # « Aborted(undefined) » dans la console dès qu'un TOS sonde le matériel —
    # or sonder en provoquant des bus errors est EXACTEMENT la façon dont EmuTOS
    # détecte la machine. Le port web se coupait donc au boot, alors que le même
    # cœur tourne partout ailleurs. -fexceptions doit s'appliquer AUSSI à
    # neost_core (Moira y est compilé), et à la compilation comme au lien.
    target_compile_options(neost_core PUBLIC -fwasm-exceptions)
    target_link_options(neost_core PUBLIC -fwasm-exceptions)

    # gui/StKeys : clavier partagé bureau/web (A21) — seul fichier de gui/ compilé ici,
# il ne dépend que du GLFW d'Emscripten.
    add_executable(neost-web src/web/main_web.cpp src/gui/StKeys.cpp)
    target_link_libraries(neost-web PRIVATE neost_core)
    set_source_files_properties(src/web/main_web.cpp
        PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")
    set_target_properties(neost-web PROPERTIES
        OUTPUT_NAME "index" SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/wasm")
    target_link_options(neost-web PRIVATE
        "-sUSE_GLFW=3"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,HEAPF32"
        "-sEXPORTED_FUNCTIONS=_main,_neost_reset,_neost_hard_reset,_neost_set_mono,_neost_mount_disk,_neost_mount_disk_b,_neost_load_tos,_neost_set_kbd_joystick,_neost_set_joy_deadzone,_neost_set_fullscreen,_neost_set_auto_zoom,_neost_audio_open,_neost_audio_close,_neost_audio_set_queued,_neost_set_volume,_neost_get_volume,_malloc,_free"
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html")

    # Choix des données embarquées dans le FS virtuel (--preload-file). Pour un
    # déploiement PUBLIC (GitHub Pages / artefact web-wasm), on n'embarque que la
    # liste autorisée des paquets (EmuTOS GPL + TOS 1.02/1.62 UK des profils
    # 520 ST / 1040 STE + diskA généré) — jamais les autres TOS ni les jeux sous
    # copyright. En local (défaut), on embarque tout roms/ et disks/ pour le confort
    # de test.
    option(NEOST_WEB_FREE_ONLY "WASM : n'embarquer que les ROM/disquettes du paquet" OFF)
    # Même NOM et même polarité que packaging/stage_free_data.sh, à dessein : le
    # bundle web est 100 % LIBRE PAR DÉFAUT depuis la purge du 2026-08-30, comme les
    # paquets bureau. Les TOS Atari ne sont plus suivis par git ; la CI ne les a plus
    # et le lien mourait ici sur un « file_packager: error: … does not exist » que
    # rien n'expliquait — le pas 4 avait inversé les gardes de release.yml et de
    # stage_free_data.sh, mais PAS cette liste-ci, qui vit dans le CMake et non dans
    # le YAML. NEOST_PACKAGE_NO_ATARI_TOS=0 les ré-embarque depuis des copies LOCALES
    # (gitignorées, cf. tools/private_assets.sh) — paquet personnel, jamais release.
    option(NEOST_PACKAGE_NO_ATARI_TOS "Paquet 100 % libre : aucun TOS Atari embarqué" ON)
    if(NEOST_WEB_FREE_ONLY)
        set(NEOST_WEB_PRELOAD
            roms/etos192us.img
            roms/etos192fr.img
            # EmuTOS 256 Ko : GPL lui aussi, et INDISPENSABLE au STE/Mega STE — sans lui
            # le frontend web se rabat sur l'EmuTOS 192 Ko, qui se présente en TOS 1.4 et
            # fait rétrograder la machine en ST (adjustMachineForTos) : le mode STE du
            # paquet public était donc silencieusement inopérant. Les paquets bureau, eux,
            # embarquent bien les 4 (packaging/stage_free_data.sh) — c'est l'incohérence
            # entre les deux listes qui était le bug. Coût : +512 Ko sur 40 Mo de marge.
            roms/etos256us.img
            roms/etos256fr.img
            roms/drivesound
            disks/diskA.st)
        if(NOT NEOST_PACKAGE_NO_ATARI_TOS)
            # TOS 1.02 UK / 1.62 UK : profils 520 ST et 1040 STE.
            list(APPEND NEOST_WEB_PRELOAD roms/tos102uk.img roms/tos162uk.img)
        endif()
        # Démos de démonstration : les trois productions demoscene que la purge du
        # 2026-08-30 a délibérément gardées. MÊME liste que packaging/stage_free_data.sh
        # pour les paquets bureau — les deux doivent rester d'accord, c'est justement
        # une divergence entre ces deux listes qui avait rendu le mode STE du paquet
        # web silencieusement inopérant. +1,9 Mo sur 40 Mo de marge.
        # ⚠ Montées À PLAT dans /disks, pas sous /disks/etalons/ : le sélecteur de
        # disquette du shell fait un FS.readdir('/disks') NON RÉCURSIF (web/shell.html,
        # populateSelect) — dans un sous-dossier elles seraient embarquées mais
        # INVISIBLES dans le menu.
        set(NEOST_WEB_DEMOS
            disks/etalons/cuddly_demos.msa
            disks/etalons/nocooper.msa
            disks/etalons/closure.msa)

        # Manquer une donnée doit se dire AU CONFIGURE, en nommant le fichier :
        # laissée au file_packager, l'absence sort au LIEN, noyée dans la ligne de
        # commande em++ complète.
        function(neost_web_preload src dst)
            if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${src}")
                message(FATAL_ERROR
                    "WASM : donnée à embarquer absente : ${src} "
                    "(TOS Atari ? ils sont hors dépôt — cf. tools/private_assets.sh)")
            endif()
            target_link_options(neost-web PRIVATE
                "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/${src}@${dst}")
        endfunction()

        foreach(item IN LISTS NEOST_WEB_PRELOAD)
            neost_web_preload("${item}" "/${item}")
        endforeach()
        foreach(item IN LISTS NEOST_WEB_DEMOS)
            get_filename_component(demo_name "${item}" NAME)
            neost_web_preload("${item}" "/disks/${demo_name}")
        endforeach()
    else()
        target_link_options(neost-web PRIVATE
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/roms@/roms"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/disks@/disks")
    endif()
    return()   # sous Emscripten on ne construit QUE le cœur + le frontend web
endif()

# =============================================================================
#  neost — frontend fenêtré (GLFW3 + OpenGL + ImGui + miniaudio).
#  Optionnel : si GLFW3/OpenGL sont absents (CI, machine sans pile graphique),
#  on construit quand même neost_core + neost-headless (l'outil de debug n°1).
# =============================================================================
find_package(glfw3 QUIET)
find_package(OpenGL QUIET)
find_package(Threads QUIET)

if(NOT glfw3_FOUND OR NOT OPENGL_FOUND)
    message(STATUS
        "GLFW3/OpenGL introuvables — frontend GUI 'neost' désactivé. "
        "neost_core + neost-headless restent disponibles "
        "(installer libglfw3-dev / mesa pour activer le GUI).")
    return()
endif()

# Munt (libmt32emu, LGPL 2.1) : Roland MT-32/CM-32L sur le MIDI OUT du GUI.
# VENDORISÉ dans extern/mt32emu (cf. son NEOST_VENDOR.md) — plus de find_package :
# la fonctionnalité dépendait d'un paquet système (brew install mt32emu /
# libmt32emu-dev) présent sur la machine du développeur et NULLE PART ailleurs, donc
# le MT-32 était silencieusement absent de tous les binaires livrés. Comme Moira, le
# code d'amont vit désormais dans l'arbre et se compile avec NeoST.
# APRÈS le garde-fou GLFW à dessein : seul le GUI s'en sert, et le compiler pour rien
# coûterait 26 fichiers aux cibles sans pile graphique (CI headless, Android, WASM).
# (Détection ALSA remontée AVANT les cibles : neost-headless et neost-selftest
#  compilent src/audio/MidiInHost.cpp et ont besoin d'ALSA_FOUND, or CMake
#  évalue ce fichier de haut en bas — la détection laissée ici rendait
#  --midi-in-device silencieusement mort sous Linux.)

add_subdirectory(extern/mt32emu)

add_executable(neost
    src/main.cpp
    src/audio/Audio.cpp
    src/audio/DriveSound.cpp
    src/gui/App.cpp
    src/gui/AppInit.cpp
    src/gui/AppLoop.cpp
    src/gui/ConfigWindow.cpp
    src/gui/CrtUi.cpp
    src/gui/DebugWindows.cpp
    src/gui/DockLayout.cpp
    src/gui/InputCallbacks.cpp
    src/gui/JoyMap.cpp
    src/gui/KioskMenu.cpp
    src/gui/StScreenView.cpp
    src/gui/OpenGLShader.cpp
    src/gui/UiCommon.cpp
    src/gui/MediaPages.cpp
    src/gui/CrtEffectStack.cpp
    src/gui/KeyboardWindow.cpp
    src/gui/StKeys.cpp
    src/gui/StbImage.cpp
    src/audio/MidiOutHost.cpp
    src/audio/MidiInHost.cpp
    src/audio/Mt32Synth.cpp
    src/audio/GmSynth.cpp)
# stb_image (extern/stb, domaine public) : décodage de la photo du clavier (pic/).
target_include_directories(neost PRIVATE "${CMAKE_SOURCE_DIR}/extern/stb")
# TinySoundFont (extern/tsf, MIT, un seul header) : le synthé GM intégré des
# plateformes sans DLSMusicDevice — cf. audio/GmSynth et extern/tsf/NEOST_VENDOR.md.
target_include_directories(neost PRIVATE "${CMAKE_SOURCE_DIR}/extern/tsf")
# Plus de branche « absent » : la bibliothèque est dans l'arbre, donc toujours là.
target_compile_definitions(neost PRIVATE NEOST_WITH_MT32=1)
if(ALSA_FOUND)
    target_compile_definitions(neost PRIVATE NEOST_MIDI_ALSA=1)
    target_include_directories(neost PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(neost PRIVATE ${ALSA_LINK_LIBRARIES})
endif()
target_link_libraries(neost PRIVATE MT32Emu::mt32emu)
# ${CMAKE_DL_LIBS} : miniaudio charge sa backend par dlopen ; avant la glibc
# 2.34 (bionic — l'image release Linux), libdl est séparée et doit être liée
# explicitement. Vide sur les systèmes où dlopen vit dans libc → sans effet.
# GLFW : la cible importée `glfw` de glfw3Config passe le chemin ABSOLU de la
# bibliothèque installée. Sous MinGW c'est l'import-lib de glfw3.dll, et un
# chemin explicite ne se laisse pas rattraper par -static — le paquet Windows
# ressortait donc avec une dépendance à glfw3.dll (attrapée par le garde de
# packaging/windows/build_mingw.sh). On passe alors par -lglfw3, que -static
# résout vers libglfw3.a, plus les bibliothèques système que GLFW statique et
# miniaudio réclament ; la liste exacte vient de pkg-config (NEOST_WIN_GLFW_LIBS,
# posé par le script de packaging) plutôt que d'une devinette de notre part.
if(WIN32 AND NEOST_WIN_GLFW_LIBS)
    separate_arguments(_neost_win_glfw NATIVE_COMMAND "${NEOST_WIN_GLFW_LIBS}")
    set(NEOST_GLFW_LINK ${_neost_win_glfw})
    message(STATUS "GLFW (Windows, statique) : ${NEOST_WIN_GLFW_LIBS}")
else()
    set(NEOST_GLFW_LINK glfw)
endif()
target_link_libraries(neost PRIVATE neost_net ${NEOST_GLFW_LINK} OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    # miniaudio (WASAPI) n'auto-lie ses dépendances COM que sous MSVC (#pragma
    # comment), jamais sous MinGW : sans elles, l'édition de liens échoue sur
    # CoInitializeEx / les IID. Inoffensif si déjà tirées par pkg-config.
    target_link_libraries(neost PRIVATE ole32 oleaut32 uuid winmm)
    # winmm sert ici DEUX fois : miniaudio ci-dessus, et le pont MIDI hôte
    # (destinations matérielles + sources, cf. audio/MidiWinmm.hpp) — plus la
    # résolution de minuteur à 1 ms sans laquelle la livraison horodatée du MIDI
    # serait quantifiée à ~15,6 ms.
    target_compile_definitions(neost PRIVATE NEOST_MIDI_WINMM=1)
endif()
target_compile_definitions(neost PRIVATE NEOST_VERSION="${NEOST_VERSION_STR}")
target_link_libraries(neost PRIVATE neost_build_info)
# Les modules d'interface sortis de main.cpp (A9) gardent la MÊME sévérité que lui :
# sans cette ligne, extraire du code aurait silencieusement désarmé -Wall/-Wextra
# sur les 2 400 lignes déplacées.
set_source_files_properties(src/main.cpp src/audio/DriveSound.cpp
    src/gui/App.cpp src/gui/AppInit.cpp src/gui/AppLoop.cpp
    src/gui/ConfigWindow.cpp src/gui/CrtUi.cpp
    src/gui/DebugWindows.cpp src/gui/DockLayout.cpp src/gui/InputCallbacks.cpp
    src/gui/JoyMap.cpp src/gui/KioskMenu.cpp src/gui/StScreenView.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# miniaudio (header-only, implémentation dans Audio.cpp).
target_include_directories(neost PRIVATE "${CMAKE_SOURCE_DIR}/extern/miniaudio")

if(APPLE)
    target_compile_definitions(neost PRIVATE GL_SILENCE_DEPRECATION=)  # vide, comme imgui (évite la redéfinition)
    target_link_libraries(neost PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio"
        "-framework AudioToolbox"   "-framework AudioUnit"   "-framework CoreMIDI")
endif()

# --- Dear ImGui (activé par défaut, GUI uniquement) ------------------------
# git submodule add https://github.com/ocornut/imgui extern/imgui
if(NEOST_WITH_IMGUI)
    set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")
    if(EXISTS "${IMGUI_DIR}/imgui.cpp")
        target_sources(neost PRIVATE
            "${IMGUI_DIR}/imgui.cpp"
            "${IMGUI_DIR}/imgui_draw.cpp"
            "${IMGUI_DIR}/imgui_tables.cpp"
            "${IMGUI_DIR}/imgui_widgets.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp")
        target_include_directories(neost PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
        target_compile_definitions(neost PRIVATE NEOST_WITH_IMGUI=1)
    else()
        message(WARNING "Dear ImGui introuvable dans extern/imgui — UI de debug désactivée.")
    endif()
endif()

# =============================================================================
#  neost-menu-preview — aperçu BUREAU du menu Android (EXCLUDE_FROM_ALL).
#
#  Le menu Android ne dépend que d'ImGui et de io/MediaScan : on peut donc le
#  dessiner ici pour itérer sur sa mise en page SANS téléphone — c'est le seul
#  moyen de voir ce qu'on fait quand on n'a pas d'appareil sous la main.
#      cmake --build build --target neost-menu-preview
#      ./build/neost-menu-preview disks/st 2.0
# =============================================================================
if(NEOST_WITH_IMGUI AND EXISTS "${IMGUI_DIR}/imgui.cpp")
    add_executable(neost-menu-preview EXCLUDE_FROM_ALL
        tests/android_menu_preview.cpp
        src/android/AndroidMenu.cpp
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp")
    target_include_directories(neost-menu-preview PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
    target_link_libraries(neost-menu-preview PRIVATE neost_core glfw ${OPENGL_LIBRARIES})
endif()
