#!/usr/bin/env python3
# =============================================================================
#  run_determinism.py — L'état complet de la machine est-il reproductible, et rejouable ?
#
#  POURQUOI. Une perte de déterminisme fausse tous les étalons sans rien casser de visible : une image
#  identique ne prouve pas un état identique. L'empreinte hache l'archive du save-state, RAM et puces.
#  C'est aussi le socle d'un retour arrière et d'un jeu en réseau en lockstep.
#
#  COMMENT. Trois passes, avec les mêmes entrées joystick et clavier :
#    A   : le boot en entier, une empreinte toutes les N trames ;
#    A'  : la même chose, qui doit rendre les mêmes empreintes ;
#    B   : reprise depuis un save-state pris en cours de route, mêmes entrées : mêmes empreintes.
#  La première ligne qui diffère date la perte. Le test porte sur la ROM libre, sans disque privé.
#
#  Usage : run_determinism.py [--rom roms/etos192fr.img] [--frames 800] [--every 50]
#  Code 77 sans neost-headless ou sans la ROM.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"


def hashes(cmd: list[str]) -> dict[int, str]:
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600, check=True).stdout
    return {int(f): h for f, h in re.findall(r"state-hash frame=(\d+) (\w+)", out)}


def first_gap(a: dict[int, str], b: dict[int, str], offset: int = 0) -> int | None:
    for frame in sorted(b):
        if a.get(frame + offset) != b[frame]:
            return frame + offset
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default="roms/etos192fr.img")
    ap.add_argument("--machine", default="st")
    ap.add_argument("--frames", type=int, default=800)
    ap.add_argument("--every", type=int, default=50)
    a = ap.parse_args()
    if not HEADLESS.exists() or not (ROOT / a.rom).exists():
        print("SKIP — déterminisme : neost-headless ou ROM absents")
        return 77
    base = [str(HEADLESS), a.rom, "--machine", a.machine, "--state-hash-every", str(a.every)]
    split = a.frames // 2
    start = split + 100                 # les entrées commencent après la reprise
    script = "F*10 .*20 R*30 U*30 .*10 "
    joy = ["--joy-script", str(start), script * 8]
    full = base + ["--frames", str(a.frames), *joy, "--scancode-at", str(start + 40), "39"]
    first = hashes(full)
    if len(first) < 2:
        print("ÉCHEC — aucune empreinte relevée")
        return 1
    gap = first_gap(first, hashes(full))
    if gap is not None:
        print(f"ÉCHEC — deux passes identiques divergent à la trame {gap}")
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        state = str(Path(tmp) / "mid.state")
        subprocess.run(base + ["--frames", str(split), "--save-state", state], cwd=ROOT, capture_output=True,
                       timeout=600, check=True)
        resumed = hashes(base + ["--load-state", state, "--frames", str(a.frames - split),
                                 "--joy-script", str(start - split), script * 8,
                                 "--scancode-at", str(start + 40 - split), "39"])
    gap = first_gap(first, resumed, split)
    if gap is not None or not resumed:
        print(f"ÉCHEC — la reprise depuis le save-state diverge à la trame {gap}")
        return 1
    print(f"DÉTERMINISME OK : {len(first)} empreintes, reprise à la trame {split} identique sur {len(resumed)} empreintes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
