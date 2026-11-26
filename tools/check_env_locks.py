#!/usr/bin/env python3
# =============================================================================
#  check_env_locks.py — les verrous NEOST_* du cœur sont-ils tous CLASSÉS ? (A34)
#
#  POURQUOI. Le cœur lit ~84 variables d'environnement. Les unes changent ce que
#  la machine ÉMULE (donc ce que valent les étalons), les autres n'ajoutent que de
#  la trace. Rien ne les distinguait : ni le nom, ni un fichier, ni un test. Un
#  lecteur — ou un mainteneur avant une release — n'avait aucun moyen de répondre
#  à « qu'est-ce qui peut encore changer l'émulation sans qu'un fichier de config
#  ne le dise ? » autrement qu'en relisant le cœur.
#
#  Ce contrôle compare tools/env_locks.json à ce que le code lit VRAIMENT :
#    · une variable lue par le cœur mais ABSENTE du manifeste → échec (à classer) ;
#    · une variable classée mais que PLUS RIEN ne lit → échec (à déplacer dans
#      « removed », avec la raison — un vieux script qui la pose doit savoir).
#
#  Ce n'est pas de la bureaucratie : c'est ce qui a permis, dans le même chantier,
#  de voir que NEOST_SYNC_DISPATCH sélectionnait un MODÈLE D'EXÉCUTION entier que
#  rien ne validait.
#
#  Usage : check_env_locks.py [--verbose]   (code de sortie 1 si un verrou dérive)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = Path(__file__).resolve().parent / "env_locks.json"
# Le CŒUR, et lui seul : les frontends ont leurs propres réglages, qui sont de la
# configuration utilisateur (neost.cfg, options de ligne de commande) et non des
# verrous d'émulation.
SCANNED = ["src/core", "src/io"]

VAR = re.compile(r"NEOST_[A-Z0-9_]+")
# Noms NEOST_* du cœur qui ne sont pas des variables d'environnement : NEOST_F est la
# macro des compteurs d'inconnues [F] (src/util/Unknowns.hpp), aucun getenv ne la lit.
NOT_ENV = {"NEOST_F"}


def scan() -> set[str]:
    found: set[str] = set()
    for rel in SCANNED:
        for f in sorted((ROOT / rel).rglob("*")):
            if f.suffix in (".cpp", ".hpp"):
                found |= set(VAR.findall(f.read_text(errors="replace")))
    return found - NOT_ENV


def main() -> int:
    verbose = "--verbose" in sys.argv
    man = json.loads(MANIFEST.read_text(encoding="utf-8"))
    behaviour = {k for k in man["behaviour"] if not k.startswith("_")}
    trace = {k for k in man["trace"] if not k.startswith("_")}
    removed = {k for k in man["removed"] if not k.startswith("_")}
    classed = behaviour | trace

    found = scan()
    # Une variable « retirée » peut rester citée dans un COMMENTAIRE (c'est même
    # souhaitable : il explique pourquoi elle ne fait plus rien). On ne la compte
    # donc pas comme lue — seul un getenv la rendrait vivante.
    live = {v for v in found if v not in removed}

    errors = []
    for v in sorted(live - classed):
        errors.append(f"« {v} » est lue par le cœur mais N'EST PAS CLASSÉE — "
                      "comportement d'émulation ou trace ? (tools/env_locks.json)")
    for v in sorted(classed - found):
        errors.append(f"« {v} » est classée mais PLUS RIEN ne la lit — la déplacer "
                      "dans « removed » avec la raison")

    if verbose:
        for v in sorted(behaviour):
            print(f"  COMPORTEMENT  {v}")
        for v in sorted(trace):
            print(f"  trace         {v}")

    print(f"\nVerrous d'environnement du cœur : {len(live)} lus, "
          f"{len(behaviour)} de COMPORTEMENT, {len(trace)} de trace, "
          f"{len(removed)} retiré(s).")
    if errors:
        for e in errors:
            print(f"  ✗ {e}", file=sys.stderr)
        return 1
    print("VERROUS OK — et rappel : les étalons sont mesurés avec les DÉFAUTS. "
          "Armer un verrou de COMPORTEMENT invalide toute comparaison au corpus.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
