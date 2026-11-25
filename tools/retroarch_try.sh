#!/bin/sh
# =============================================================================
#  retroarch_try.sh — Le cœur libretro de NeoST dans un VRAI RetroArch, isolé.
#
#  Monte une configuration jetable (dossier système avec EmuTOS, sauvegardes,
#  shaders de packaging/libretro/shaders, pilote glcore, son coupé) sans toucher
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

W="$ROOT/build-libretro/retroarch-try"
rm -rf "$W"
mkdir -p "$W/system/neost" "$W/saves" "$W/states" "$W/shots" "$W/info"
cp "$ROOT"/roms/etos192fr.img "$ROOT"/roms/etos192us.img "$ROOT"/roms/etos256fr.img "$ROOT"/roms/etos256us.img "$W/system/neost/"
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
video_driver = "glcore"
video_gpu_screenshot = "true"
video_fullscreen = "false"
video_scale = "3.000000"
aspect_ratio_index = "22"
audio_driver = "null"
config_save_on_exit = "false"
pause_nonactive = "false"
EOF

set -- --config "$W/retroarch.cfg" -L "$CORE" --verbose --log-file "$W/retroarch.log"
[ -n "$CONTENT" ] && set -- "$@" "$CONTENT"
[ "$FRAMES" -gt 0 ] && set -- "$@" --max-frames="$FRAMES" --max-frames-ss --max-frames-ss-path="$W/shots/shot.png"
"$RA" "$@" || true
echo "log: $W/retroarch.log"
grep -iE "\[NeoST\]|shader|slang|preset" "$W/retroarch.log" | head -30 || true
[ -f "$W/shots/shot.png" ] && echo "capture: $W/shots/shot.png"
