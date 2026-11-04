#!/usr/bin/env python3
# =============================================================================
#  run_game_replay.py — Des parties jouées, identiques à l'image près avec Hatari.
#
#  POURQUOI. Les étalons voient des écrans fixes et des démos. Une partie fait passer entrées, IRQ, FDC et
#  vidéo ensemble, sur des milliers de trames : une dérive d'un cycle finit par changer le jeu.
#
#  COMMENT. tests/replays/replays.json donne le disque, les touches et un script joystick. Hatari et NeoST
#  jouent les mêmes masques (tools/compare_titles.py --joy), et chaque trame demandée doit rester sous
#  max_diff %. Échelon `hatari` : un vert dit « d'accord avec Hatari », pas « comme le fer ».
#
#  Usage : run_game_replay.py [--only ID]
#  Code 77 sans Pillow, sans Hatari, ou si aucun disque n'est présent.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def play(r: dict, work: Path) -> tuple[int, list[str]]:
    joy = work / "joy.txt"
    joy.write_text(" ".join([r["joy"]] * r.get("joy_repeat", 1)) + "\n")
    cmd = [sys.executable, str(ROOT / "tools" / "compare_titles.py"), str(work / "out"), r["disk"],
           "--machine", r["machine"], "--rom", r["rom"], "--mem", str(r["mem"]),
           "--frames", ",".join(map(str, r["frames"])), "--window", str(r.get("window", 6)), "--step", "2",
           "--joy", f'{r["joy_at"]}:{joy}', "--jobs", "1"]
    for at in r.get("keys", []):
        cmd += ["--key", f"{at}:57"]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=1800)
    if done.returncode == 77:
        return 77, [done.stdout.strip()]
    if done.returncode != 0:
        return 1, [done.stderr.strip()[-400:]]
    row = json.loads((work / "out" / "summary.json").read_text())[0]
    if row.get("error"):
        return 1, [row["error"]]
    bad = [f'trame {f} : {v["diff"]} %' for f, v in row["frames"].items() if v["diff"] > r["max_diff"]]
    return (1 if bad else 0), bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--only")
    a = ap.parse_args()
    replays = json.loads((ROOT / "tests" / "replays" / "replays.json").read_text())["replays"]
    ran = failed = 0
    for r in replays:
        if a.only and r["id"] not in a.only.split(","):
            continue
        if not (ROOT / r["disk"]).exists() or not (ROOT / r["rom"]).exists():
            print(f'SKIP {r["id"]} : disque ou ROM absents')
            continue
        with tempfile.TemporaryDirectory() as tmp:
            code, notes = play(r, Path(tmp))
        if code == 77:
            print(f'SKIP {r["id"]} : {notes[0]}')
            continue
        ran += 1
        failed += code != 0
        print(f'{"OK  " if code == 0 else "FAIL"} {r["id"]} {"; ".join(notes)}')
    if not ran:
        return 77
    print("PARTIES OK" if not failed else "PARTIES EN ÉCHEC")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
