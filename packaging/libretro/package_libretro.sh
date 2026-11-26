#!/usr/bin/env bash
# =============================================================================
#  package_libretro.sh — Le cœur libretro de NeoST en un zip prêt à copier dans
#  RetroArch, rangé comme ses dossiers (Settings → Directory) :
#
#    NeoST-libretro-<ver>-<plateforme>/
#      cores/neost_libretro.{so,dylib,dll}   → dossier « Cores »
#      info/neost_libretro.info              → dossier « Core Info »
#      shaders/                              → dossier « Video Shaders »
#      system/neost/                         → dossier « System/BIOS » : EmuTOS (GPL),
#                                              bruits du lecteur (freeware, crédités)
#      licenses/, README.md
#
#  Le module est bâti avant (CMake, -DNEOST_PIC=ON -DNEOST_BUILD_LIBRETRO=ON) ; ce
#  script ne fait qu'assembler, dépouiller les symboles et vérifier le contenu.
#  Même liste blanche que les autres paquets : EmuTOS seul, jamais un TOS Atari.
#
#    packaging/libretro/package_libretro.sh <module> <plateforme> [dossier de sortie]
#    NEOST_VERSION=0.6.5 packaging/libretro/package_libretro.sh \
#        build-libretro/neost_libretro.dylib macos-universal2
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail
MODULE="${1:?usage: package_libretro.sh <module> <plateforme> [dist]}"
PLATFORM="${2:?usage: package_libretro.sh <module> <plateforme> [dist]}"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${3:-$SRC/dist}"
VERSION="${NEOST_VERSION:-}"
INFO="$(dirname "$MODULE")/neost_libretro.info"     # celui du build, version posée
[ -f "$INFO" ] || INFO="$SRC/src/libretro/neost_libretro.info"
if [ -z "$VERSION" ]; then
    VERSION="$(sed -n 's/^display_version = "\(.*\)"/\1/p' "$INFO")"
fi
test -f "$MODULE" || { echo "ERREUR : module introuvable : $MODULE" >&2; exit 1; }

NAME="NeoST-libretro-$VERSION-$PLATFORM"
STAGE="$OUT/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/cores" "$STAGE/info" "$STAGE/system/neost" "$STAGE/licenses"

# --- Module, symboles de débogage retirés -----------------------------------
EXT="${MODULE##*.}"
CORE="$STAGE/cores/neost_libretro.$EXT"
cp "$MODULE" "$CORE"
case "$EXT" in
    # strip défait la signature ad hoc que l'éditeur de liens pose sur arm64 : on la
    # repose, comme le .app (pas de Developer ID, cf. docs/RELEASE.md).
    dylib) strip -x "$CORE" && codesign --force --sign - "$CORE" ;;
    # STRIP : l'outil GNU de la cible (x86_64-w64-mingw32-strip en compilation croisée).
    so|dll) "${STRIP:-strip}" --strip-unneeded "$CORE" ;;
    *) echo "ERREUR : extension de module inattendue : .$EXT" >&2; exit 1 ;;
esac

# --- .info : la version du paquet, même si le module vient d'un build « dev » ----
sed "s/^display_version = \".*\"/display_version = \"$VERSION\"/" "$INFO" > "$STAGE/info/neost_libretro.info"

# --- Shaders slang (préréglage de cœur compris) -----------------------------
cp -R "$SRC/packaging/libretro/shaders" "$STAGE/shaders"

# --- Dossier système : EmuTOS et bruits du lecteur ---------------------------
for rom in etos192us.img etos192fr.img etos256us.img etos256fr.img; do
    cp "$SRC/roms/$rom" "$STAGE/system/neost/"
done
mkdir -p "$STAGE/system/neost/drivesound"
cp -R "$SRC/roms/drivesound/epson_smd480l" "$STAGE/system/neost/drivesound/"
cp "$SRC/roms/drivesound/README.md" "$STAGE/system/neost/drivesound/"

# --- Licences et notice -----------------------------------------------------
cp "$SRC/LICENSE" "$STAGE/licenses/GPL-3.0.txt"
cp "$SRC/packaging/licenses/GPL-2.0.txt" "$SRC/packaging/licenses/THIRD-PARTY.txt" "$STAGE/licenses/"
cp "$SRC/packaging/libretro/README.md" "$STAGE/README.md"

# --- Garde-fous -------------------------------------------------------------
STRAY=$(find "$STAGE/system" -name '*.img' ! -name 'etos*')
[ -z "$STRAY" ] || { echo "ERREUR : ROM non autorisée dans le paquet : $STRAY" >&2; exit 1; }
grep -q "^display_version = \"$VERSION\"" "$STAGE/info/neost_libretro.info" \
    || { echo "ERREUR : version absente du .info" >&2; exit 1; }
test -s "$STAGE/shaders/presets/NeoST/NeoST.slangp" \
    || { echo "ERREUR : préréglage de cœur absent" >&2; exit 1; }

# --- Zip ---------------------------------------------------------------------
rm -f "$OUT/$NAME.zip"
(cd "$OUT" && zip -qr "$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip"
