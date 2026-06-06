#!/usr/bin/env bash
# setup.sh — prépare l'arbre de build NeoST de zéro.
#
# Étapes : dépendances système (GLFW) → sous-modules → opcodes Musashi →
# configuration CMake → compilation. Idempotent : relançable sans danger.
#
# Usage :
#   ./setup.sh            # build Release complet
#   ./setup.sh --debug    # build Debug
#   ./setup.sh --no-deps  # saute l'installation des paquets système
set -euo pipefail

# Racine du dépôt = répertoire de ce script (marche depuis n'importe où).
cd "$(dirname "$0")"

BUILD_TYPE=Release
INSTALL_DEPS=1
for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE=Debug ;;
        --release) BUILD_TYPE=Release ;;
        --no-deps) INSTALL_DEPS=0 ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Option inconnue : $arg" >&2; exit 2 ;;
    esac
done

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

# 1) Dépendances système (GLFW3 + OpenGL). Best-effort selon le gestionnaire.
if [ "$INSTALL_DEPS" -eq 1 ]; then
    say "Vérification des dépendances système (GLFW3)…"
    if command -v pacman >/dev/null 2>&1; then
        pacman -Qi glfw glfw-x11 glfw-wayland >/dev/null 2>&1 \
            || sudo pacman -S --needed --noconfirm glfw cmake base-devel
    elif command -v apt-get >/dev/null 2>&1; then
        dpkg -s libglfw3-dev >/dev/null 2>&1 \
            || sudo apt-get install -y libglfw3-dev cmake build-essential
    elif command -v brew >/dev/null 2>&1; then
        brew list glfw >/dev/null 2>&1 || brew install glfw
    else
        say "Gestionnaire de paquets non reconnu — installez GLFW3 + CMake à la main."
    fi
fi

# 2) Sous-modules (Musashi, imgui, miniaudio, moira).
say "Initialisation des sous-modules…"
git submodule update --init --recursive

# 3) Génération des opcodes Musashi (obligatoire après le clone).
if [ ! -f extern/Musashi/m68kops.c ]; then
    say "Génération du cœur d'opcodes Musashi…"
    ( cd extern/Musashi && cc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c )
else
    say "Opcodes Musashi déjà générés — saut."
fi

# 4) Configuration + compilation.
say "Configuration CMake ($BUILD_TYPE)…"
cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

say "Compilation (neost, neost-headless, neost_core)…"
cmake --build build -j

say "Terminé. Lance l'émulateur avec : ./run.sh"
