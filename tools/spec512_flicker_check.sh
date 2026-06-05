#!/usr/bin/env bash
# Vérifie le flicker spec512 (Moira) : capture chaque trame d'une fenêtre où une
# image Spectrum 512 est affichée, et compte les pixels qui CHANGENT entre trames
# consécutives (= flicker, doit être 0 hors transitions d'image).
# Usage : spec512_flicker_check.sh [from] [to]
set -uo pipefail
# Racine du dépôt = parent du dossier tools/ (robuste au nom du dossier, ex. neost↔NEOST).
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM=roms/tos102uk.img
DISK=disks/utils/spectrum_512_auto_diapo.st
FROM=${1:-500}; TO=${2:-1010}
TMP=$(mktemp -d)
END=$((TO+2))
./build/neost-headless "$ROM" --disk "$DISK" --fastfdc --cpu moira \
   --frames "$END" --shot-every 1 "$TMP/f_" >/dev/null 2>&1
prev=""; maxflick=0; nflick=0
for n in $(seq "$FROM" 1 "$TO"); do
  nn=$(printf "%05d" "$n"); f="$TMP/f_$nn.ppm"; [ -f "$f" ] || continue
  if [ -n "$prev" ]; then
    d=$(compare -metric AE "$prev" "$f" null: 2>&1)
    # transitions d'image = gros diffs (>5000) ; flicker = petit diff non nul
    if [ "$d" != "0" ] && [ "$d" -lt 5000 ] 2>/dev/null; then
      echo "  flicker $prevn -> $nn : $d px"; nflick=$((nflick+1))
      [ "$d" -gt "$maxflick" ] && maxflick=$d
    fi
  fi
  prev=$f; prevn=$nn
done
echo "=> paires avec flicker : $nflick | pire : $maxflick px"
rm -rf "$TMP"
