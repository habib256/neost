#!/usr/bin/env bash
# Oracle Hatari headless : boot ST + disque → AVI PNG → frame PNG extraite.
# Usage : hatari_oracle.sh <tos> <disk.st> <run-vbls> <frame-n> <out.png>
set -euo pipefail
# Racine du dépôt = parent du dossier tools/ (robuste au nom du dossier, ex. neost↔NEOST).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HATARI="$ROOT/extern/hatari/build/src/hatari"
TOS=${1:?tos}; DISK=${2:?disk}; VBLS=${3:-1400}; FRAME=${4:-1300}; OUT=${5:-/tmp/hatari_frame.png}
AVI=/tmp/hatari_oracle.avi
rm -f "$AVI"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home
mkdir -p /tmp/hatari_home
"$HATARI" --machine st --tos "$TOS" --monitor rgb \
  --disk-a "$DISK" \
  --sound off --fast-forward on --confirm-quit off --statusbar off \
  --frameskips 0 --alert-level fatal \
  --run-vbls "$VBLS" \
  --avirecord --avi-vcodec png --avi-file "$AVI" >/tmp/hatari_oracle.log 2>&1 || true
# Extrait la frame demandée (l'AVI a 1 image par VBL avec --frameskips 0).
ffmpeg -y -loglevel error -i "$AVI" -vf "select=eq(n\,$FRAME)" -frames:v 1 -update 1 "$OUT"
echo "oracle frame $FRAME -> $OUT"
identify "$OUT" 2>/dev/null || true
