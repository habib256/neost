# =============================================================================
#  NeoST — Émulateur Atari ST "boîte à hack" pédagogique
#  Cible : compilation croisée CachyOS Linux (x86_64) / macOS Silicon (arm64)
#  Pile UI : GLFW3 + OpenGL + Dear ImGui (immediate mode), comme POM1/POM2.
#
#  (c) 2026 VERHILLE Arnaud — code généré dans le cadre du projet NeoST.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(NeoST
    VERSION 0.1.0
    DESCRIPTION "Emulateur Atari ST orienté transparence matérielle"
    LANGUAGES C CXX)

# --- Standard & garde-fous -------------------------------------------------
# C++17 minimum (le projet vise 17/20). Musashi est en C → on garde C99.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# Le coeur émulation est chaud : on optimise agressivement en Release.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# --- Options ---------------------------------------------------------------
option(NEOST_WITH_IMGUI "Greffe Dear ImGui (visualiseur hexa + registres)" ON)
option(NEOST_WARN_STRICT "Compilation stricte (-Wall -Wextra -Werror)" ON)

# --- Fenêtre / entrées : GLFW3 + OpenGL (legacy, sans loader, comme POM) ----
# Immediate mode (GL 2.1) : aucun GLEW/GLAD requis, les symboles sont fournis
# directement par libGL (Linux) / OpenGL.framework (macOS Silicon).
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# --- Musashi (core 68000) en sous-module ----------------------------------
# git submodule add https://github.com/kstenerud/Musashi extern/Musashi
set(MUSASHI_DIR "${CMAKE_SOURCE_DIR}/extern/Musashi")
set(MUSASHI_SOURCES
    "${MUSASHI_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/m68kops.c"
    "${MUSASHI_DIR}/m68kdasm.c"
    "${MUSASHI_DIR}/softfloat/softfloat.c")

# --- Cible exécutable ------------------------------------------------------
add_executable(neost
    src/main.cpp
    src/core/Bus.cpp
    src/core/Cpu68k.cpp
    src/core/Shifter.cpp)

if(EXISTS "${MUSASHI_DIR}/m68kcpu.c")
    target_sources(neost PRIVATE ${MUSASHI_SOURCES})
    target_include_directories(neost PRIVATE "${MUSASHI_DIR}")
    target_compile_definitions(neost PRIVATE NEOST_HAS_MUSASHI=1)
else()
    message(WARNING
        "Musashi introuvable dans extern/Musashi — le CPU sera un stub. "
        "Lance: git submodule add https://github.com/kstenerud/Musashi extern/Musashi")
endif()

target_include_directories(neost PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(neost PRIVATE glfw OpenGL::GL)

if(NEOST_WARN_STRICT)
    # Warnings stricts sur NOTRE code uniquement (pas Musashi/ImGui, C/legacy).
    set_source_files_properties(
        src/main.cpp src/core/Bus.cpp src/core/Cpu68k.cpp src/core/Shifter.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic")
endif()

# --- Dear ImGui (activé par défaut) ----------------------------------------
# git submodule add https://github.com/ocornut/imgui extern/imgui
# Backends : GLFW + OpenGL2 (immediate mode) — pas de loader GL nécessaire.
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
        message(WARNING
            "Dear ImGui introuvable dans extern/imgui — UI de debug désactivée. "
            "Lance: git submodule add https://github.com/ocornut/imgui extern/imgui")
    endif()
endif()
