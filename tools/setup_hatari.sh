#!/usr/bin/env bash
# =============================================================================
#  setup_hatari.sh — Met en place l'oracle Hatari À LA VERSION ÉPINGLÉE.
#
#  POURQUOI (chantier A5). `extern/hatari` est GITIGNORÉ et n'est PAS un
#  sous-module : sur une machine fraîche il est simplement ABSENT, et rien ne le
#  rapatrie. Or toute la méthode imposée du projet repose sur lui — « comparer
#  d'abord le source Hatari, porter, puis retester ». Pire, la recette
#  documentée jusqu'ici faisait un `git clone --depth 1`, c'est-à-dire « le HEAD
#  du jour » : deux oracles bâtis à deux semaines d'écart pouvaient produire des
#  références PIXEL différentes sans qu'aucune ligne du dépôt n'ait bougé.
#
#  Ce script fixe les deux : il rapatrie la version ÉPINGLÉE, et il la bâtit avec
#  les options obligatoires (macOS comprise). Idempotent.
#
#  Usage : tools/setup_hatari.sh [--update-pin]
#          --update-pin : ne clone rien, affiche la commande pour ré-épingler sur
#                         la version actuellement présente (décision humaine :
#                         changer d'oracle peut déplacer les références).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/extern/hatari"

# ⚠ Doit rester IDENTIQUE au HATARI_PIN de tools/hatari_oracle.sh, qui avertit
# quand l'arbre présent ne correspond pas. Les références `ref_kind: oracle` du
# dépôt ont été posées avec CETTE version.
PIN=f0736b24b32b0439300b52107ba6ab434469ec3c        # v2.6.1-devel, 2026-08-18
URL=https://framagit.org/hatari/hatari.git

if [[ "${1:-}" == "--update-pin" ]]; then
  [[ -d "$DIR/.git" ]] || { echo "extern/hatari absent — rien à ré-épingler." >&2; exit 1; }
  NEW=$(git -C "$DIR" rev-parse HEAD)
  echo "Version actuellement présente : $NEW"
  echo "Pour ré-épingler, remplacer PIN dans CES DEUX fichiers :"
  echo "  tools/setup_hatari.sh   (PIN=)"
  echo "  tools/hatari_oracle.sh  (HATARI_PIN=)"
  echo "⚠ Puis REGÉNÉRER les références oracle et vérifier ce qui bouge :"
  echo "  python3 tools/run_etalons.py --oracle && python3 tools/run_all.py --tier full"
  exit 0
fi

if [[ ! -d "$DIR/.git" ]]; then
  echo "→ clonage de Hatari (complet : un --depth 1 ne permet pas de se placer sur un SHA)"
  git clone "$URL" "$DIR"
fi

echo "→ mise à la version épinglée ${PIN:0:12}"
git -C "$DIR" fetch --tags origin
git -C "$DIR" checkout --detach "$PIN"

# Instrumentation NeoST de l'oracle (événements souris de la fifo, script joystick
# daté par VBL, graine HATARI_SEED figeable, diagnostics sous variable d'environnement).
# extern/hatari est gitignoré : sans cette étape, une installation fraîche donnait un
# Hatari NU, et tools/opendst_oracle.py s'arrêtait net (« Hatari n'a pas chargé le
# script »). Le patch est versionné dans le dépôt NeoST ; il ne change pas le matériel
# émulé (cf. son en-tête). On échoue ici plutôt que de livrer un oracle qui « marche »
# sans répondre aux entrées.
PATCH="$ROOT/tools/hatari_neost_oracle.patch"
echo "→ application de l'instrumentation NeoST ($(basename "$PATCH"))"
if ! git -C "$DIR" apply --check "$PATCH"; then
  echo "échec : $PATCH ne s'applique pas sur ${PIN:0:12} — rebaser le patch avant de" >&2
  echo "        déplacer l'épingle (cf. docs/HATARI_AUTOMATION.md)." >&2
  exit 1
fi
git -C "$DIR" apply "$PATCH"

# Les deux options macOS sont OBLIGATOIRES : sans -DCMAKE_OSX_ARCHITECTURES=arm64 le
# build tombe en x86_64 sous Rosetta, et ENABLE_OSX_BUNDLE=0 est requis pour obtenir
# un binaire en ligne de commande utilisable en headless (et non un .app).
CMAKE_ARGS=(-S "$DIR" -B "$DIR/build" -DCMAKE_BUILD_TYPE=Release)
if [[ "$(uname -s)" == "Darwin" ]]; then
  CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DENABLE_OSX_BUNDLE=0)
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "$DIR/build" -j"$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu )"

BIN="$DIR/build/src/hatari"
[[ -x "$BIN" ]] || { echo "échec : $BIN absent après le build." >&2; exit 1; }
echo
"$BIN" --version | head -1
echo "oracle prêt : $BIN"
echo "Recettes → docs/HATARI_AUTOMATION.md"
