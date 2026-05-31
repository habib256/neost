# =============================================================================
#  NeoST — Émulateur Atari ST "boîte à hack" pédagogique
#  Cible : compilation croisée CachyOS Linux (x86_64) / macOS Silicon (arm64)
#  Pile UI : GLFW3 + OpenGL + Dear ImGui (immediate mode), comme POM1/POM2.
#
#  Architecture : un cœur SANS GUI (neost_core) partagé par deux frontends :
#    - neost            : fenêtré (GLFW/OpenGL/ImGui/miniaudio)
#    - neost-headless   : sans interface, traces déterministes pour diff MAME
#
#  (c) 2026 VERHILLE Arnaud — code généré dans le cadre du projet NeoST.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(NeoST
    VERSION 0.1.0
    DESCRIPTION "Emulateur Atari ST orienté transparence matérielle"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

option(NEOST_WITH_IMGUI  "Greffe Dear ImGui (visualiseur hexa + registres)" ON)
option(NEOST_WARN_STRICT "Compilation stricte (-Wall -Wextra -Wpedantic)"   ON)

set(NEOST_STRICT_OPTS "")
if(NEOST_WARN_STRICT)
    set(NEOST_STRICT_OPTS "-Wall;-Wextra;-Wpedantic")
endif()

# =============================================================================
#  Bibliothèque cœur — AUCUNE dépendance GUI (ni GL ni GLFW).
#  Contient la carte mère + le CPU (Musashi) + le traceur.
# =============================================================================
add_library(neost_core STATIC
    src/core/Bus.cpp
    src/core/Cpu68k.cpp
    src/core/Shifter.cpp
    src/core/YM2149.cpp
    src/core/Machine.cpp
    src/core/Tracer.cpp
    src/io/Mfp.cpp
    src/io/Ikbd.cpp)

target_include_directories(neost_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
set_source_files_properties(
    src/core/Bus.cpp src/core/Cpu68k.cpp src/core/Shifter.cpp src/core/YM2149.cpp
    src/core/Machine.cpp src/core/Tracer.cpp src/io/Mfp.cpp src/io/Ikbd.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# --- Musashi (core 68000) en sous-module, compilé DANS le cœur -------------
# git submodule add https://github.com/kstenerud/Musashi extern/Musashi
# ( cd extern/Musashi && cc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c )
set(MUSASHI_DIR "${CMAKE_SOURCE_DIR}/extern/Musashi")
if(EXISTS "${MUSASHI_DIR}/m68kops.c")
    target_sources(neost_core PRIVATE
        "${MUSASHI_DIR}/m68kcpu.c"
        "${MUSASHI_DIR}/m68kops.c"
        "${MUSASHI_DIR}/m68kdasm.c"
        "${MUSASHI_DIR}/softfloat/softfloat.c")
    target_include_directories(neost_core PUBLIC "${MUSASHI_DIR}")
    target_compile_definitions(neost_core PUBLIC  NEOST_HAS_MUSASHI=1)
    # Active le hook d'instruction (traceur) ET l'acquittement d'interruption
    # vectorisé : sinon Musashi auto-vectorise TOUT et les vecteurs MFP du ST
    # (clavier, Timer C) ne sont jamais utilisés → EmuTOS reste bloqué.
    target_compile_definitions(neost_core PRIVATE
        M68K_INSTRUCTION_HOOK=1
        M68K_EMULATE_INT_ACK=1)
else()
    message(WARNING
        "Musashi introuvable — CPU en stub. "
        "git submodule add https://github.com/kstenerud/Musashi extern/Musashi "
        "puis générer m68kops.c (cf. README).")
endif()

# =============================================================================
#  neost-headless — sans interface, pour les traces comparables à MAME.
# =============================================================================
add_executable(neost-headless src/headless/main_headless.cpp)
target_link_libraries(neost-headless PRIVATE neost_core)
set_source_files_properties(src/headless/main_headless.cpp
    PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# =============================================================================
#  neost — frontend fenêtré (GLFW3 + OpenGL + ImGui + miniaudio).
# =============================================================================
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_executable(neost
    src/main.cpp
    src/audio/Audio.cpp)
target_link_libraries(neost PRIVATE neost_core glfw OpenGL::GL Threads::Threads)
set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "${NEOST_STRICT_OPTS}")

# miniaudio (header-only, implémentation dans Audio.cpp).
target_include_directories(neost PRIVATE "${CMAKE_SOURCE_DIR}/extern/miniaudio")

if(APPLE)
    target_compile_definitions(neost PRIVATE GL_SILENCE_DEPRECATION=1)  # immediate mode legacy
    target_link_libraries(neost PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio"
        "-framework AudioToolbox"   "-framework AudioUnit")
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
