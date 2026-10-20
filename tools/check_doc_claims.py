#!/usr/bin/env python3
# =============================================================================
#  check_doc_claims.py — Les CHIFFRES de la documentation disent-ils encore vrai ?
#
#  POURQUOI (A26, audit 2026-08-27). check_doc_anchors.py garde les SYMBOLES cités
#  par la doc — mais la dérive constatée était ailleurs : les AFFIRMATIONS
#  CHIFFRÉES. Huit périmées trouvées à la main le même jour, dans les sections les
#  plus visibles : « 44 ROM » (37 réelles), « 6 étalons sur 13 » (8 sur 15),
#  « main.cpp 4 980 lignes » (5 017), « palier fast ~3 s » (4,8 s), un § BLOQUANT
#  listant un artefact sorti du dépôt quatre jours plus tôt… Une doc maintenue à la
#  main pour un lecteur qui la relit intégralement dérive là où personne ne
#  recompte. Ce contrôle recompte.
#
#  CE QUI EST VÉRIFIÉ : chaque entrée de CLAIMS extrait UN nombre d'un document
#  (regex à un groupe capturant) et le compare à une valeur RECALCULÉE depuis la
#  source de vérité (git ls-files, etalons.json, wc -l). `tol` autorise une marge
#  relative pour les grandeurs qui bougent à chaque édition légitime (lignes de
#  main.cpp) — 0 = égalité stricte. Un motif INTROUVABLE est un échec aussi :
#  une reformulation qui échappe au contrôle le désarme en silence.
#
#  Même philosophie que les ancres : le contrôle est grossier À DESSEIN — il
#  attrape la doc qui ment, pas les nuances de rédaction. Pour retirer une
#  affirmation de la doc, retirer AUSSI son entrée ici (l'échec « motif
#  introuvable » le rappellera).
#
#  Usage : check_doc_claims.py [--verbose]   (code de sortie 1 si un chiffre ment)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def git_count(*patterns: str) -> int:
    out = subprocess.run(["git", "ls-files", "--", *patterns],
                         cwd=ROOT, capture_output=True, text=True, timeout=60)
    return len([l for l in out.stdout.splitlines() if l.strip()])


def machine_etalons() -> list[dict]:
    man = json.loads((ROOT / "tools" / "etalons.json").read_text())
    ents = man["etalons"] if isinstance(man, dict) and "etalons" in man else man
    return [e for e in ents if not str(e.get("type", "")).endswith("_selftest")]


def free_etalons() -> int:
    # Même critère que rom_is_free() de run_etalons.py : EmuTOS = libre.
    return sum(1 for e in machine_etalons()
               if Path(e["rom"]).name.startswith("etos"))


def line_count(rel: str) -> int:
    return len((ROOT / rel).read_text(errors="replace").splitlines())


def func_line_count(rel: str, name: str) -> int:
    # Lignes d'une fonction définie au niveau fichier : de sa signature (en
    # colonne 0) à la première accolade fermante en colonne 0. 0 si introuvable.
    lines = (ROOT / rel).read_text(errors="replace").splitlines()
    sig = re.compile(r"^\S.*\b" + re.escape(name) + r"\(")
    for i, l in enumerate(lines):
        if sig.match(l) and not l.rstrip().endswith(";"):
            for j in range(i, len(lines)):
                if lines[j].startswith("}"):
                    return j - i + 1
    return 0


# (document, regex à 1 groupe, calcul de la valeur vraie, tolérance relative)
CLAIMS = [
    # Purge du 2026-08-30 : les six compteurs du tableau § BLOQUANT (TOS, jeux,
    # cartouches, Cubase Lite) sont retirés AVEC leur tableau — le contenu n'est
    # plus suivi par git, il n'y a plus rien à recompter. À la place, un garde
    # NÉGATIF : si l'un de ces chemins redevient suivi, la purge a fuité.
    # La phrase vit au CHANGELOG depuis la réorganisation du TODO (2026-10-20).
    ("CHANGELOG.md", r"pack \*\*165 → (\d+) Mio\*\*",
     lambda: 12 + git_count("roms/tos*", "roms/TOS*", "disks/st", "disks/stx",
                            "carts", "disks/midi/CUBLITE",
                            "dev/agt", "dev/reservoir-gods"), 0.0),
    ("TODO.md", r"\*\*(\d+) étalons\s+pixel sur \d+\*\*", free_etalons, 0.0),
    ("TODO.md", r"\*\*\d+ étalons\s+pixel sur (\d+)\*\*",
     lambda: len(machine_etalons()), 0.0),
    # A9 (2026-08-30) : main.cpp est retombé à un point d'entrée de 28 lignes.
    # A9 (2026-10-20) : appLoop découpée en phases (struct Frame) ; le chiffre à
    # garder est celui de la plus grosse pièce restée d'un seul tenant, le menu
    # kiosk. Marge 3 % : elle bouge à chaque édition légitime ; au-delà, la doc
    # raconte une autre fonction que celle du dépôt.
    ("TODO.md", r"`runKioskMenu` — (\d+) lignes d'un seul tenant",
     lambda: func_line_count("src/gui/AppLoop.cpp", "runKioskMenu"), 0.03),
    ("tools/etalons.json", r"COUVERTURE \((\d+) étalons machine",
     lambda: len(machine_etalons()), 0.0),
]


def main() -> int:
    verbose = "--verbose" in sys.argv
    errors, checked = [], 0
    for rel, pattern, compute, tol in CLAIMS:
        text = (ROOT / rel).read_text()
        m = re.search(pattern, text)
        checked += 1
        if not m:
            errors.append(f"{rel} — motif « {pattern} » INTROUVABLE : l'affirmation a "
                          "été reformulée ou retirée sans mettre à jour CLAIMS")
            continue
        claimed = int(re.sub(r"[\s ]", "", m.group(1)))
        actual = compute()
        ok = claimed == actual if tol == 0 else \
            abs(claimed - actual) <= max(1, round(actual * tol))
        if verbose or not ok:
            line = text[:m.start()].count("\n") + 1
            print(f"  {'✓' if ok else '✗'} {rel}:{line} — annoncé {claimed}, "
                  f"recompté {actual}" + (f" (tolérance ±{tol:.0%})" if tol else ""))
        if not ok:
            errors.append(f"{rel} : annoncé {claimed}, recompté {actual}")
    print(f"\nAffirmations chiffrées : {checked} vérifiées, {len(errors)} fausse(s).")
    if errors:
        print("Un chiffre faux dans la doc coûte plus cher qu'aucun chiffre : le "
              "lecteur décide sur lui. Corriger la doc (ou l'entrée CLAIMS si "
              "l'affirmation a changé de forme).")
        return 1
    print("CHIFFRES OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
