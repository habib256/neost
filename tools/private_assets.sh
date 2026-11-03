#!/usr/bin/env bash
# =============================================================================
#  private_assets.sh — Emballe / restaure les données NON REDISTRIBUABLES
#  (TOS Atari, jeux, cartouches, Spectrum 512) d'une machine à l'autre, SANS
#  passer par git. Cubase Lite est un freeware (disks/cubase_lite.st, dans les
#  paquets) ; l'arbre GEMDOS disks/midi/CUBLITE reste local, c'est lui que
#  l'étalon séquenceur monte.
#
#  POURQUOI (purge copyright 2026-08-30). Ces fichiers sont purgés de
#  l'historique public et gitignorés : chaque machine les garde LOCALEMENT.
#  Les tests qui en dépendent se ré-arment tout seuls quand ils sont présents
#  (run_etalons ré-exécute les étalons Spectrum 512/TOS, run_megaste_diag
#  retrouve sa cartouche, run_midi_sequencer retrouve Cubase Lite) et restent
#  des « SKIP recensés » sinon — le palier fast est vert dans les deux cas.
#
#  Le script ne contient que des MOTIFS de chemins, jamais de contenu : il est
#  publiable ; le tarball produit ne l'est PAS (le garder sur un support privé
#  — disque externe, rsync entre machines, cloud personnel).
#
#  Usage : tools/private_assets.sh pack   [archive.tar.gz]   # machine source
#          tools/private_assets.sh unpack <archive.tar.gz>   # machine cible
#
#  Défaut pack : ~/NeoST-archives/neost-private-assets-<date>.tar.gz (+ .sha256)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Les mêmes périmètres que la purge (dev/agt et dev/reservoir-gods ont leur
# propre outil, setup_devkits.sh — pas la peine de les trimballer en tarball).
PATTERNS=(
  "roms/tos"*.img
  "roms/TOS "*
  disks/st
  disks/stx
  carts
  disks/midi/CUBLITE
  disks/tools/spectrum_512_st_format.msa
  disks/etalons/spectrum_512_auto_diapo.st
)

case "${1:-}" in
  pack)
    out="${2:-$HOME/NeoST-archives/neost-private-assets-$(date +%Y-%m-%d).tar.gz}"
    mkdir -p "$(dirname "$out")"
    present=()
    for p in "${PATTERNS[@]}"; do [[ -e "$p" ]] && present+=("$p"); done
    [[ ${#present[@]} -gt 0 ]] || { echo "Rien à emballer : aucun des chemins privés n'est présent." >&2; exit 1; }
    tar czf "$out" "${present[@]}"
    (cd "$(dirname "$out")" && shasum -a 256 "$(basename "$out")" > "$(basename "$out").sha256")
    echo "Emballé : $out ($(du -h "$out" | cut -f1 | tr -d ' ')) + .sha256"
    ;;
  unpack)
    in="${2:?unpack demande le chemin du tarball}"
    [[ -f "$in" ]] || { echo "Introuvable : $in" >&2; exit 1; }
    [[ -f "$in.sha256" ]] && (cd "$(dirname "$in")" && shasum -a 256 -c "$(basename "$in").sha256")
    tar xzf "$in" -C "$ROOT"
    echo "Restauré dans $ROOT — les étalons/diagnostics concernés se ré-armeront au prochain run."
    ;;
  *)
    grep '^#  Usage' -A3 "$0" | sed 's/^#  //'
    exit 1
    ;;
esac
