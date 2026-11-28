#!/usr/bin/env bash
# Exécute les suites du Moira officiel épinglé contre le cœur vendorisé.
# Usage : tools/run_moira_runner.sh [--upstream]
#   --upstream ajoute le cœur officiel comme témoin.
#   NEOST_MOIRA_UPSTREAM désigne un clone qui contient déjà le commit.
#   NEOST_MOIRA_JOBS règle le parallélisme (4 par défaut).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="${NEOST_MOIRA_UPSTREAM:-$ROOT/extern/moira-upstream}"
OUT="$ROOT/build-moira-runner"
JOBS="${NEOST_MOIRA_JOBS:-4}"
PIN=bf98107ffa3bb24d576bbe99ef684c77e33de938
URL=https://github.com/dirkwhoffmann/Moira.git
TEST_DIRS=(Tests/Common Tests/Random Tests/Sequence Tests/Smoke Tests/SST
           Tests/ThirdParty Tests/Unit)

variants=(vendored)
case "${1:-}" in
  --upstream) variants+=(upstream) ;;
  '') ;;
  *) echo "Usage: $0 [--upstream]" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo "Usage: $0 [--upstream]" >&2
  exit 2
fi

if [[ ! -e "$UP/.git" ]]; then
  git clone --depth 1 --filter=blob:none --no-checkout "$URL" "$UP"
  git -C "$UP" sparse-checkout set Moira Examples "${TEST_DIRS[@]}"
  git -C "$UP" fetch --depth 1 origin "$PIN"
  git -C "$UP" checkout --detach "$PIN"
fi
if ! git -C "$UP" cat-file -e "$PIN^{commit}" 2>/dev/null; then
  git -C "$UP" fetch origin "$PIN"
fi

# Un index séparé permet d'extraire les seuls fichiers nécessaires au pin
# même avec un clone partiel, sans changer sa branche ni ses fichiers.
mkdir -p "$OUT"
INDEX="$OUT/moira-upstream.index"
GIT_INDEX_FILE="$INDEX" git -C "$UP" read-tree "$PIN"

stage() {
  local dir="$1" variant="$2"
  mkdir -p "$dir"
  local sources=(CMakeLists.txt Examples Tests/CMakeLists.txt "${TEST_DIRS[@]}")
  if [[ "$variant" == upstream ]]; then
    sources+=(Moira)
  fi
  GIT_INDEX_FILE="$INDEX" git -C "$UP" ls-files -z -- "${sources[@]}" |
    GIT_INDEX_FILE="$INDEX" git -C "$UP" checkout-index --stdin -z --force \
      --ignore-skip-worktree-bits --prefix="$dir/"
  if [[ "$variant" == vendored ]]; then
    mkdir -p "$dir/Moira"
    rsync -a --delete "$ROOT/extern/moira/Moira/" "$dir/Moira/"
    # La bibliothèque DEFAULT reprend les options de NeoST. Les variantes
    # MAME et UAE des tests gardent leurs options officielles.
    cat >> "$dir/Moira/CMakeLists.txt" <<'CMAKE'
target_compile_definitions(moira PUBLIC
    MOIRA_PRECISE_TIMING=true
    MOIRA_EMULATE_ADDRESS_ERROR=true
    MOIRA_WILL_EXECUTE=true)
CMAKE
  fi
}

status=0
for variant in "${variants[@]}"; do
  src="$OUT/src-$variant"
  build="$OUT/build-$variant"
  echo "→ Suites officielles sur Moira $variant"
  stage "$src" "$variant"
  cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$build" --parallel "$JOBS" \
    --target unit unitfast smoke sequenceuae retrymoira
  if ! ctest --test-dir "$build" --parallel "$JOBS" --output-on-failure \
    -R '^(Unit|UnitFast|Smoke|SequenceUAE020|SequenceUAE030|RetryMoira030|RetryMoira040)$' \
    | tee "$OUT/runner-$variant.log"; then
    status=1
  fi
done
exit "$status"
