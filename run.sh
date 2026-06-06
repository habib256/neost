#!/usr/bin/env bash
# run.sh — recompile au besoin puis lance NeoST.
#
# Recompile l'incrément (rapide si rien n'a changé), puis exécute la cible
# voulue avec les arguments passés tels quels.
#
# Usage :
#   ./run.sh                                   # GUI, ROM/disquette par défaut
#   ./run.sh roms/etos192fr.img disks/diskA.st # GUI, ROM + disquette explicites
#   ./run.sh --headless --frames 50 --screenshot s.ppm   # mode headless déterministe
#
# Variables d'env :
#   NEOST_BUILD=build   répertoire de build (défaut : build)
set -euo pipefail

# Racine du dépôt = répertoire de ce script.
cd "$(dirname "$0")"

BUILD_DIR="${NEOST_BUILD:-build}"

# Choix de la cible : --headless bascule sur neost-headless.
TARGET=neost
ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--headless" ]; then
        TARGET=neost-headless
    else
        ARGS+=("$arg")
    fi
done

# Configure si l'arbre de build n'existe pas encore.
if [ ! -d "$BUILD_DIR" ]; then
    echo "==> Pas de build — configuration CMake (Release)…"
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

# Recompile l'incrément (no-op si à jour).
echo "==> Compilation de $TARGET…"
cmake --build "$BUILD_DIR" -j --target "$TARGET"

echo "==> Lancement : $TARGET ${ARGS[*]:-}"
exec "$BUILD_DIR/$TARGET" "${ARGS[@]}"
