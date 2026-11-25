#!/bin/sh
# =============================================================================
#  retroarch_try.sh — Le cœur libretro de NeoST dans un VRAI RetroArch, isolé.
#
#  Monte une configuration jetable (dossier système avec EmuTOS, sauvegardes,
#  shaders de packaging/libretro/shaders, bruits du lecteur ; pilote vidéo par défaut de
#  RetroArch : Metal, Vulkan ou glcore lisent le slang, « gl » non) sans toucher
#  à celle de l'utilisateur, lance le module sur un contenu et garde une capture
#  GPU (shaders compris) à la trame demandée, avec le journal de RetroArch.
#  C'est l'item « Le module dans un vrai RetroArch » du TODO (§ 7).
#
#    tools/retroarch_try.sh [RetroArch] [contenu] [trames]
#      RetroArch : binaire ou .app (défaut /Applications/RetroArch.app)
#      contenu   : image, programme ou .m3u (défaut : aucun, bureau EmuTOS)
#      trames    : trame de la capture (défaut 1500 ; 0 = jouer à la main)
#
#  Sortie : build-libretro/retroarch-try/ (shot.png, retroarch.log).
#  RA_CORE_OPTIONS="neost_fast_floppy=disabled;neost_drive_sound=disabled" règle des options
#  du cœur, dans un fichier d'options PROPRE à l'essai (sinon RetroArch écrit dans le
#  dossier global de l'utilisateur, config/NeoST/NeoST.opt).
#  RA_VIDEO_DRIVER=metal (ou vulkan, glcore) impose un pilote vidéo ; par défaut, celui de
#  RetroArch (Vulkan/MoltenVK pour retroarch-metal sous macOS). Sous macOS, la capture de
#  fin (--max-frames-ss) ne capture rien, Vulkan comme Metal : F8 en jeu (mode 0). Le
#  pilote Metal de RetroArch 1.22.2 ralentit tout sauf bouton du touchpad tenu, avec ou
#  sans l'accélération du chargement (2026-11-25) ; Vulkan tourne normalement.
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RA=${1:-/Applications/RetroArch.app}
CONTENT=${2:-}
FRAMES=${3:-1500}
case "$RA" in *.app) RA="$RA/Contents/MacOS/RetroArch" ;; esac
[ -x "$RA" ] || { echo "RetroArch not found: $RA" >&2; exit 2; }

CORE=""
for f in "$ROOT"/build-libretro/neost_libretro.dylib "$ROOT"/build-libretro/neost_libretro.so; do
    [ -f "$f" ] && CORE=$f && break
done
[ -n "$CORE" ] || { echo "build the core first: cmake -B build-libretro -DNEOST_PIC=ON -DNEOST_BUILD_LIBRETRO=ON" >&2; exit 2; }

# Architecture : un RetroArch x86_64 (sous Rosetta) ne charge pas un module arm64. Sous
# macOS, le cask « retroarch » est x86_64 et OpenGL seul (pilote « gl », sans slang) ;
# « retroarch-metal » est natif et lit les shaders.
if command -v lipo >/dev/null 2>&1; then
    RA_ARCHS=$(lipo -archs "$RA" 2>/dev/null || true)
    CORE_ARCHS=$(lipo -archs "$CORE" 2>/dev/null || true)
    ok=0
    for a in $CORE_ARCHS; do case " $RA_ARCHS " in *" $a "*) ok=1 ;; esac; done
    if [ -n "$RA_ARCHS" ] && [ -n "$CORE_ARCHS" ] && [ $ok = 0 ]; then
        echo "RetroArch is $RA_ARCHS, the core is $CORE_ARCHS: they cannot load each other." >&2
        echo "On Apple Silicon: brew uninstall --cask retroarch && brew install --cask retroarch-metal" >&2
        exit 2
    fi
fi

W="$ROOT/build-libretro/retroarch-try"
rm -rf "$W"
mkdir -p "$W/system/neost" "$W/saves" "$W/states" "$W/shots" "$W/info"
cp "$ROOT"/roms/etos192fr.img "$ROOT"/roms/etos192us.img "$ROOT"/roms/etos256fr.img "$ROOT"/roms/etos256us.img "$W/system/neost/"
cp -R "$ROOT/roms/drivesound/epson_smd480l" "$W/system/neost/drivesound"
cp "$ROOT/src/libretro/neost_libretro.info" "$W/info/"
cp -R "$ROOT/packaging/libretro/shaders" "$W/shaders"
cat > "$W/retroarch.cfg" <<EOF
system_directory = "$W/system"
savefile_directory = "$W/saves"
savestate_directory = "$W/states"
screenshot_directory = "$W/shots"
libretro_info_path = "$W/info"
video_shader_dir = "$W/shaders"
video_shader_enable = "true"
auto_shaders_enable = "true"
video_gpu_screenshot = "true"
video_fullscreen = "false"
video_scale = "3.000000"
aspect_ratio_index = "22"
config_save_on_exit = "false"
pause_nonactive = "false"
core_options_path = "$W/neost.opt"
history_list_enable = "false"
EOF
: > "$W/neost.opt"
if [ -n "${RA_CORE_OPTIONS:-}" ]; then
    printf '%s\n' "$RA_CORE_OPTIONS" | tr ';' '\n' | while IFS='=' read -r k v; do
        [ -n "$k" ] && echo "$k = \"$v\"" >> "$W/neost.opt"
    done
fi
[ -n "${RA_VIDEO_DRIVER:-}" ] && echo "video_driver = \"$RA_VIDEO_DRIVER\"" >> "$W/retroarch.cfg"

set -- --config "$W/retroarch.cfg" -L "$CORE" --verbose --log-file "$W/retroarch.log"
[ -n "$CONTENT" ] && set -- "$@" "$CONTENT"
[ "$FRAMES" -gt 0 ] && set -- "$@" --max-frames="$FRAMES" --max-frames-ss --max-frames-ss-path="$W/shots/shot.png"
"$RA" "$@" || true
echo "log: $W/retroarch.log"
grep -iE "\[NeoST\]|preset|Geometry|SET_GEOMETRY|SET_SYSTEM_AV_INFO|Screenshot|ERROR" "$W/retroarch.log" | grep -v mvk | head -30 || true
[ -f "$W/shots/shot.png" ] && echo "capture: $W/shots/shot.png"
