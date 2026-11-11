#!/usr/bin/env python3
# =============================================================================
#  classify_selftests.py — À quel échelon chaque test de logique se déclare-t-il ?
#
#  POURQUOI. docs/REFERENCE_LADDER.md : un vert vaut ce que vaut sa référence. Les tables de vérité
#  de neost-selftest n'avaient pas d'échelon, alors qu'une table tirée d'une fiche et une table recopiée
#  d'Hatari ne prouvent pas la même chose.
#
#  COMMENT. Lit les commentaires de tests/*.cpp et retient l'échelon le plus haut qu'ils citent. C'est
#  ce que le test DÉCLARE, pas une relecture de ses attendus. « interne » = contrat de NeoST sans
#  affirmation sur le matériel, marqué [interne]. « aucun » = il ne nomme aucune source.
#
#  Usage : classify_selftests.py [--list ÉCHELON]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Du plus haut au plus bas : le premier qui correspond l'emporte.
LADDER = (
    ("hardware", r"\[A\]"),
    ("primary", r"\[B\]|datasheet|\bfiche\b|netlist|M68000UM|MC68\d|WD177|YM2149_|Zilog|UM\d{4}|Ricoh|RP5C15|\bpp?\. ?\d|\bpp\d|§ ?\d"),
    ("rom", r"\[C\]|ROM \$|TOS \d\.\d+ \$|EmuTOS"),
    ("rtl", r"\.sv\b|\.v:\d|gstmcu|fx68k|stBlitter|MiSTery"),
    ("hatari", r"\[D\]|[Hh]atari|\b[a-zA-Z_0-9]+\.c\b"),
    ("interne", r"\[interne\]"),
)


def classify(path: Path) -> str:
    comments = "\n".join(re.findall(r"//[^\n]*", path.read_text(errors="replace")))
    return next((name for name, pat in LADDER if re.search(pat, comments)), "aucun")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list", help="liste les tests de cet échelon")
    a = ap.parse_args()
    kinds = {p.name: classify(p) for p in sorted((ROOT / "tests").glob("*.cpp"))}
    if a.list:
        print("\n".join(n for n, k in kinds.items() if k == a.list))
        return 0
    count = Counter(kinds.values())
    print(" · ".join(f"{k} {count.get(k, 0)}" for k in [n for n, _ in LADDER] + ["aucun"]), f"(total {len(kinds)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
