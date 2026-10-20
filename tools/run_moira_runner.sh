#!/usr/bin/env bash
# =============================================================================
#  run_moira_runner.sh — Rejoue le testeur différentiel de Moira sur la copie
#  VENDORISÉE (patches NeoST compris).
#
#  POURQUOI (chantier A35). Les patches NeoST de `extern/moira/Moira` ne sont
#  validés que par les étalons ST : une instruction que ni EmuTOS ni nos démos
#  n'exécutent pourrait casser sans bruit. L'amont fournit `Runner/` : Moira
#  contre Musashi sur des opcodes et données aléatoires, avec comparaison du
#  désassemblage, du total de cycles (hors TAS), de SR, des registres et des
#  écritures mémoire. On le bâtit ici contre NOTRE Moira.
#
#  Le `Cputester/` amont (≈713 Mo) n'est PAS utilisé : ce sont des disquettes
#  Amiga du cputester de Toni Wilen, à exécuter dans vAmiga, pas sur un ST.
#  Les cycles comparés sont les totaux de la table de Musashi : ce banc ne dit
#  rien de la position des cycles bus (c'est le rôle de l'oracle Hatari).
#
#  Usage : tools/run_moira_runner.sh [--upstream]
#          --upstream : bâtit aussi le témoin (Moira amont au pin, sans patch)
#
#  Clone hors historique dans extern/moira-upstream/ (gitignoré), build dans
#  build-moira-runner/. Idempotent.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="$ROOT/extern/moira-upstream"
OUT="$ROOT/build-moira-runner"

# Pin de départ du fork (extern/moira/NEOST_VENDOR.md) ; vérifié présent chez
# l'amont, sur master, le 2026-10-20.
PIN=1efd69467ca13b27b2fb40febd5cb31dbecdea5f
URL=https://github.com/dirkwhoffmann/Moira.git

if [[ ! -d "$UP/.git" ]]; then
  echo "→ clonage de Moira amont (sans extraire l'arbre : Cputester/ pèse ≈713 Mo)"
  git clone --no-checkout "$URL" "$UP"
fi
git -C "$UP" cat-file -e "$PIN^{commit}" 2>/dev/null || git -C "$UP" fetch origin

# Arbre de build : Runner/ et CMakeLists du pin, Moira/ au choix.
stage() {   # dossier source_moira
  local dir="$1" moira="$2"
  rm -rf "$dir"; mkdir -p "$dir"
  git -C "$UP" archive "$PIN" Runner CMakeLists.txt Moira | tar -x -C "$dir"
  if [[ "$moira" == vendored ]]; then
    rm -rf "$dir/Moira"; cp -R "$ROOT/extern/moira/Moira" "$dir/Moira"
  fi
}

variants=(vendored)
[[ "${1:-}" == "--upstream" ]] && variants+=(upstream)

status=0
for v in "${variants[@]}"; do
  echo "→ Runner contre Moira $v"
  stage "$OUT/src-$v" "$v"
  cmake -S "$OUT/src-$v" -B "$OUT/build-$v" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$OUT/build-$v" -j >/dev/null
  # Le Runner tire sa graine de l'heure : la ligne « Random seed » est dans le journal.
  if (cd "$OUT" && "$OUT/build-$v/testRunner" | tee "$OUT/runner-$v.log"); then
    grep -q "All tests completed" "$OUT/runner-$v.log" && ! grep -q "MISMATCH" "$OUT/runner-$v.log" \
      || status=1
  else
    status=1
  fi
done
[[ $status -eq 0 ]] && echo "MOIRA RUNNER OK" || echo "MOIRA RUNNER FAIL (journaux : $OUT)"
exit $status
