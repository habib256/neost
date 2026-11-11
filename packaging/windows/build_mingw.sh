#!/usr/bin/env bash
# =============================================================================
#  Paquet Windows x86_64 — bâti dans un shell MSYS2/MINGW64.
#
#  Pourquoi MinGW-w64 et pas MSVC : le code est écrit pour GCC/Clang (attributs,
#  extensions, options -W…), Moira exige C++20 et le reste du dépôt se compile
#  partout ailleurs avec GCC. Passer par MSVC voudrait dire porter les options de
#  compilation ET la chaîne de dépendances (vcpkg) ; MinGW réutilise tout tel quel.
#
#  TOUT EST LIÉ EN STATIQUE (-static) : libgcc, libstdc++, winpthread et GLFW.
#  Un utilisateur Windows qui déballe un zip ne va pas installer des runtimes,
#  et une DLL oubliée ne se voit pas ici (le smoke tourne dans MSYS2, qui a les
#  DLL dans son PATH) — elle se voit chez lui, par une boîte « libstdc++-6.dll
#  introuvable ». Statique = le zip contient tout, ou il ne se construit pas.
#
#  MANIFESTE UTF-8 (packaging/windows/neost.rc, lié par CMake) : les deux .exe
#  tournent en page de code UTF-8, donc chemins et noms d'utilisateur accentués
#  fonctionnent à partir de Windows 10 1903 ; plus ancien, Windows ignore ce réglage
#  et ces chemins restent cassés (page ANSI).
#
#  Sortie : dist/NeoST-<version>-windows-x86_64.zip
#           (neost.exe + neost-headless.exe + roms/ disks/ fonts/ + LISEZMOI)
#
#  Usage (dans un shell MINGW64) :
#      NEOST_VERSION=0.5.1 packaging/windows/build_mingw.sh
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

VERSION="${NEOST_VERSION:-dev}"
BUILD_DIR="build-win"
STAGE="dist/NeoST-$VERSION-windows-x86_64"
OUT="dist/NeoST-$VERSION-windows-x86_64.zip"

rm -rf "$BUILD_DIR" "$STAGE" "$OUT"
mkdir -p dist

# -- Compilation -------------------------------------------------------------
# -static : voir l'en-tête. GLFW_USE_STATIC : la config CMake de mingw-w64-glfw
# expose la cible `glfw` en statique quand on ne demande pas la DLL.
# La cible CMake importée `glfw` pointe l'import-lib de glfw3.dll par chemin
# ABSOLU, que -static ne peut pas contourner. On demande donc à pkg-config la
# ligne de lien STATIQUE exacte (libglfw3.a + ses dépendances système) : c'est le
# paquet lui-même qui la déclare, pas nous.
GLFW_STATIC_LIBS="$(pkg-config --static --libs glfw3)"
test -n "$GLFW_STATIC_LIBS" || { echo "ERREUR : pkg-config ne connaît pas glfw3" >&2; exit 1; }
echo "GLFW statique : $GLFW_STATIC_LIBS"

cmake -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DNEOST_VERSION_STR="$VERSION" \
      -DNEOST_WIN_GLFW_LIBS="$GLFW_STATIC_LIBS" \
      -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build "$BUILD_DIR" -j"$(nproc)"

test -f "$BUILD_DIR/neost.exe" \
  || { echo "ERREUR : neost.exe absent — GLFW/OpenGL n'ont pas été trouvés, le GUI n'a pas été construit" >&2; exit 1; }
test -f "$BUILD_DIR/neost-headless.exe" || { echo "ERREUR : neost-headless.exe absent" >&2; exit 1; }

# -- Garde ANTI-DLL ----------------------------------------------------------
# La raison d'être du -static ci-dessus. objdump liste les DLL importées : seules
# celles FOURNIES PAR WINDOWS sont tolérées. Toute libgcc/libstdc++/libwinpthread/
# glfw3 dans cette liste = un zip qui ne démarre pas sur une machine nue, et le
# smoke de ce script ne le verrait pas (MSYS2 a ces DLL dans son PATH).
check_dlls() {
    local exe="$1" bad="" out dlls
    # Sortie capturée d'abord : un `done < <(objdump …)` échappe à set -e/pipefail, et un
    # objdump en échec (binaire illisible, outil absent) laissait la boucle vide imprimer OK.
    out=$(objdump -p "$exe") \
        || { echo "ERREUR : objdump -p a échoué sur $exe" >&2; exit 1; }
    dlls=$(printf '%s\n' "$out" | awk '/DLL Name:/ {print $3}')
    [ -n "$dlls" ] \
        || { echo "ERREUR : aucune ligne « DLL Name: » pour $exe — table d'import illisible" >&2; exit 1; }
    while read -r dll; do
        case "${dll,,}" in
            kernel32.dll|user32.dll|gdi32.dll|shell32.dll|advapi32.dll|ole32.dll|\
            oleaut32.dll|winmm.dll|imm32.dll|version.dll|setupapi.dll|cfgmgr32.dll|\
            msvcrt.dll|opengl32.dll|dwmapi.dll|shlwapi.dll|ws2_32.dll|iphlpapi.dll|bcrypt.dll|\
            avrt.dll|mmdevapi.dll|api-ms-win-*|ext-ms-*) ;;
            *) bad="$bad $dll" ;;
        esac
    done <<< "$dlls"
    if [ -n "$bad" ]; then
        echo "ERREUR : $exe dépend de DLL non système :$bad" >&2
        echo "         (le zip ne démarrerait pas sur une machine sans MSYS2)" >&2
        exit 1
    fi
    echo "OK : $(basename "$exe") n'importe que des DLL Windows"
}
check_dlls "$BUILD_DIR/neost.exe"
check_dlls "$BUILD_DIR/neost-headless.exe"

# -- Contenu du zip ----------------------------------------------------------
mkdir -p "$STAGE"
cp "$BUILD_DIR/neost.exe" "$BUILD_DIR/neost-headless.exe" "$STAGE/"
# Même liste autorisée que tous les autres paquets — EmuTOS SEUL (aucune ROM
# propriétaire depuis la purge du 2026-08-30) + diskA + les trois démos libres +
# la banque GM + polices. Jamais de TOS Atari ni de jeu du dépôt.
packaging/stage_free_data.sh "$STAGE"

# resolveData() cherche les données à côté de l'exécutable PUIS un cran au-dessus :
# ici tout est à plat dans le dossier déballé, donc le premier candidat suffit.
cat > "$STAGE/README.txt" <<EOF
NeoST $VERSION — Atari ST emulator
==================================

Double-click neost.exe. Nothing to install: everything is in this folder, so keep
it in one piece (the emulator looks for roms\\ and disks\\ next to the executable).

  neost.exe            graphical interface
  neost-headless.exe   console build, deterministic (--help for the options)
  roms\\                EmuTOS — no proprietary ROM ships with NeoST
  disks\\               a formatted floppy plus three demoscene productions
  licenses\\            GPL-3.0, GPL-2.0 and the third-party list
  neost.cfg            created on first launch, next to the executable

SmartScreen may say "Windows protected your PC": the package is not code-signed
(this project has no code-signing certificate). Click "More info", then
"Run anyway".

GNU GPL v3. Source code and bug reports:
https://github.com/habib256/neost
EOF

# -- Zip ---------------------------------------------------------------------
( cd dist && zip -qr "$(basename "$OUT")" "$(basename "$STAGE")" )
rm -rf "$STAGE"
ls -lh "$OUT"
echo "OK : $OUT"
