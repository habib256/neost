#!/usr/bin/env bash
# =============================================================================
#  Fabrique dist/NeoST-<version>-[tag-]<arch>.AppImage à partir d'un build
#  existant.
#
#    packaging/linux/make_appimage.sh [dossier-build]     (défaut : build)
#    NEOST_VERSION=1.0.0 packaging/linux/make_appimage.sh
#    NEOST_PKG_TAG=raspberry … → NeoST-<ver>-raspberry-aarch64.AppImage
#
#  Disposition (resolveData cherche exeDir/../roms) :
#    AppDir/usr/bin/neost, neost-headless
#    AppDir/usr/roms/ (EmuTOS SEUL par défaut), AppDir/usr/disks/diskA.st
#
#  Mécanique reprise de POM1 (packaging/linux/build_appimage.sh) :
#    1. linuxdeploy met en place l'AppDir : libs non-blacklist embarquées
#       (libglfw3…, rpath $ORIGIN/../lib), libGL/glibc EXCLUES (règle AppImage :
#       le pilote graphique vient de l'hôte), AppRun → usr/bin/neost.
#    2. appimagetool d'AppImageKit (l'ANCIEN dépôt) assemble le paquet : son
#       runtime est ELF ET_EXEC + squashfs gzip, accepté par AppImageLauncher.
#       Le nouveau AppImage/appimagetool (et le plugin appimage de linuxdeploy,
#       vérifié ici même) produisent un runtime static-pie ET_DYN rejeté en
#       « type -1 ». En aarch64, même le `continuous` d'AppImageKit est ET_DYN :
#       on épingle le runtime de la release figée 12 (--runtime-file).
#
#  Les outils sont pris dans $NEOST_APPIMAGE_TOOLS_DIR (image bionic gelée),
#  sinon téléchargés/extraits dans build-appimage/tools/ (cache local, pas de
#  FUSE requis). ⚠ neost.cfg (exeDir/../) tombe dans le squashfs lecture
#  seule : la config ne se persiste pas depuis l'AppImage — assumé (kiosk).
# =============================================================================
set -euo pipefail
BUILD_DIR="${1:-build}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
ARCH="$(uname -m)"                       # x86_64 | aarch64
VERSION="${NEOST_VERSION:-dev}"
# NEOST_PKG_TAG=raspberry → NeoST-<ver>-raspberry-aarch64.AppImage : distingue
# le paquet Pi (plancher glibc bookworm) de l'arm64 générique bâti sur le runner.
PKG_ARCH="${NEOST_PKG_TAG:+$NEOST_PKG_TAG-}$ARCH"

test -x "$BUILD_DIR/neost" || { echo "ERREUR : $BUILD_DIR/neost absent (build GUI requis)"; exit 1; }
test -x "$BUILD_DIR/neost-headless" || { echo "ERREUR : $BUILD_DIR/neost-headless absent"; exit 1; }

APPDIR="build-appimage/AppDir"
TOOLS="${NEOST_APPIMAGE_TOOLS_DIR:-build-appimage/tools}"
# Nom EXACT, pas un glob : « NeoST-*aarch64.AppImage » emportait aussi le paquet
# « …-raspberry-aarch64.AppImage » d'un empaquetage précédent, sans un mot.
rm -rf "$APPDIR"; rm -f "dist/NeoST-$VERSION-$PKG_ARCH.AppImage"   # (cache tools/ conservé)
mkdir -p "$APPDIR/usr/bin" "$TOOLS" dist

# --- Outils AppImage (extraits → utilisables sans FUSE) ----------------------
# Tout téléchargement est épinglé ET vérifié, par architecture (mêmes pins que
# Dockerfile.bionic pour x86_64). linuxdeploy : tag IMMUABLE — son `continuous`
# est republié (2026-08-01) et un SHA256 contre une cible mouvante ne protège de
# rien. appimagetool : `continuous` faute de mieux (les releases numérotées
# d'AppImageKit sont renommées `obsolete-*`), figé depuis le 2025-07-26 ; s'il
# est republié, la somme cassera le build ici plutôt que de livrer en silence.
LINUXDEPLOY_TAG=1-alpha-20251107-1
case "$ARCH" in
    x86_64)  LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
             APPIMAGETOOL_SHA256=b90f4a8b18967545fda78a445b27680a1642f1ef9488ced28b65398f2be7add2 ;;
    aarch64) LINUXDEPLOY_SHA256=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
             APPIMAGETOOL_SHA256=a48972e5ae91c944c5a7c80214e7e0a42dd6aa3ae979d8756203512a74ff574d ;;
    *) echo "ERREUR : architecture $ARCH sans outils AppImage épinglés"; exit 1 ;;
esac
RUNTIME_AARCH64_SHA256=207f8955500cfe8dd5b824ca7514787c023975e083b0269fc14600c380111d85

sha256_of() {
    if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
fetch_verified() {   # url fichier sha256
    curl -fsSL -o "$2" "$1"
    local got; got="$(sha256_of "$2")"
    [ "$got" = "$3" ] || { echo "ERREUR : SHA256 de $(basename "$2") = $got, attendu $3 ($1)"; rm -f "$2"; exit 1; }
}
fetch_extract() {    # url nom sha256
    local url="$1" name="$2" sha="$3" appdir="$TOOLS/$2.AppDir"
    # Outils cuits par l'image bionic : déjà vérifiés par son Dockerfile.
    if [ -n "${NEOST_APPIMAGE_TOOLS_DIR:-}" ] && [ -d "$appdir" ]; then return 0; fi
    # Cache local : réutilisé seulement s'il provient du MÊME binaire épinglé.
    if [ -d "$appdir" ] && [ "$(cat "$appdir/.neost-sha256" 2>/dev/null)" = "$sha" ]; then return 0; fi
    rm -rf "$appdir"
    echo "[appimage] Téléchargement de $name…"
    fetch_verified "$url" "$TOOLS/$name.AppImage" "$sha"
    chmod +x "$TOOLS/$name.AppImage"
    (cd "$TOOLS" && "./$name.AppImage" --appimage-extract >/dev/null && mv squashfs-root "$name.AppDir")
    echo "$sha" > "$appdir/.neost-sha256"
}
fetch_extract "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$ARCH.AppImage" \
    linuxdeploy "$LINUXDEPLOY_SHA256"
fetch_extract "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$ARCH.AppImage" \
    appimagetool "$APPIMAGETOOL_SHA256"

# Runtime aarch64 : le `continuous` d'AppImageKit est ET_DYN côté ARM ; la
# release 12 est la dernière à publier un runtime-aarch64 ET_EXEC (cf. POM1).
RUNTIME_ARG=()
if [ "$ARCH" = "aarch64" ]; then
    RUNTIME_FILE="$TOOLS/runtime-aarch64-et_exec"
    if [ ! -f "$RUNTIME_FILE" ] || [ "$(sha256_of "$RUNTIME_FILE")" != "$RUNTIME_AARCH64_SHA256" ]; then
        fetch_verified "https://github.com/AppImage/AppImageKit/releases/download/12/runtime-aarch64" \
            "$RUNTIME_FILE" "$RUNTIME_AARCH64_SHA256"
    fi
    RUNTIME_ARG=(--runtime-file "$RUNTIME_FILE")
fi

# --- Staging AppDir ----------------------------------------------------------
install -m 755 "$BUILD_DIR/neost" "$BUILD_DIR/neost-headless" "$APPDIR/usr/bin/"
packaging/stage_free_data.sh "$APPDIR/usr"

cat > build-appimage/neost.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=NeoST
Comment=Émulateur Atari ST pédagogique (cycle-exact, Moira)
Exec=neost
Icon=neost
Categories=Game;Emulator;
Terminal=false
EOF

# --- 1. linuxdeploy : libs embarquées + desktop/icône + AppRun ---------------
"$TOOLS/linuxdeploy.AppDir/AppRun" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/neost" \
    --executable "$APPDIR/usr/bin/neost-headless" \
    --desktop-file build-appimage/neost.desktop \
    --icon-file packaging/neost.png

# --- 2. appimagetool (via AppRun : ses libgpgme embarquées, mksquashfs inclus)
OUT="dist/NeoST-$VERSION-$PKG_ARCH.AppImage"
ARCH="$ARCH" VERSION="$VERSION" \
"$TOOLS/appimagetool.AppDir/AppRun" \
    ${RUNTIME_ARG[@]+"${RUNTIME_ARG[@]}"} \
    "$APPDIR" "$OUT"

echo "OK : $OUT"
