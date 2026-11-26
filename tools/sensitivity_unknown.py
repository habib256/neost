#!/usr/bin/env python3
"""Sensibilité d'une inconnue [F] : le titre change-t-il si le choix incertain change ?

rank_unknowns.py dit quels titres TRAVERSENT un chemin [F] ; ce banc dit lesquels en
DÉPENDENT. On rejoue chaque run du classement (machine, ROM, disque, mêmes entrées) avec
neost-headless et avec un ou plusieurs binaires alternatifs, où le choix [F] est remplacé
par un autre candidat (une autre valeur de bus ouvert, une autre date…). Ces binaires se
construisent hors du dépôt : un worktree patché sous #ifdef, un répertoire de build par
candidat. On compare :
  - le hachage d'état de la machine (--arch-hash-every), indépendant du cœur : première
    trame où il diverge, c'est-à-dire où le choix a laissé une trace quelque part ;
  - les captures toutes les SHOT trames : nombre de captures différentes, et pixels
    différents sur la pire, c'est-à-dire ce qui se voit.

Usage :
  sensitivity_unknown.py OUT RANK.json ID --alt FF=chemin/neost-headless [--alt 00=…]
Sortie : OUT/sensitivity.txt et .json, un run par ligne.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import rank_unknowns as ru  # noqa: E402  (mêmes entrées que le classement)

HASH_EVERY = 25
SHOT = 500


def run(binary: Path, machine: str, rom: str, disk: Path, frames: int, tmp: Path):
    copy = tmp / ("d" + disk.suffix)
    shutil.copy(disk, copy)
    shots = tmp / "s"
    shots.mkdir()
    cmd = [str(binary), str(ROOT / rom), "--machine", machine, "--mem", "1m", "--disk", str(copy),
           "--frames", str(frames), "--arch-hash-every", str(HASH_EVERY),
           "--shot-every", str(SHOT), str(shots / "f")]
    for at in ru.SPACE:
        if at < frames:
            cmd += ["--scancode-at", str(at), "39"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    hashes = re.findall(r"^arch-hash frame=(\d+) machine=(\S+)", out, re.M)
    pics = {p.name: p.read_bytes() for p in sorted(shots.glob("*.ppm"))}
    return hashes, pics


def pixel_diff(a: bytes, b: bytes) -> int:
    if len(a) != len(b):
        return -1
    # En-tête PPM P6 : trois lignes ; les pixels suivent.
    off = 0
    for _ in range(3):
        off = a.index(b"\n", off) + 1
    return sum(1 for i in range(off, len(a), 3) if a[i:i + 3] != b[i:i + 3])


def one(job, alts, frames, work):
    machine, rom, name = job
    disk = ROOT / name if not Path(name).is_absolute() else Path(name)
    res = {"machine": machine, "rom": rom, "disk": name}
    with tempfile.TemporaryDirectory(dir=work) as t0:
        base_h, base_p = run(ru.HEADLESS, machine, rom, disk, frames, Path(t0))
    if not base_h:
        res["error"] = "pas de hachage (option --arch-hash-every ?)"
        return res
    for label, binary in alts:
        with tempfile.TemporaryDirectory(dir=work) as t1:
            h, p = run(binary, machine, rom, disk, frames, Path(t1))
        first = next((int(a[0]) for a, b in zip(base_h, h) if a != b), None)
        diffs = {k: pixel_diff(base_p[k], p[k]) for k in base_p if k in p and base_p[k] != p[k]}
        res[label] = {"first_hash_frame": first, "shots_diff": len(diffs),
                      "shots": len(base_p), "worst_px": max(diffs.values(), default=0)}
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("rank", help="rank.json de rank_unknowns.py (champ runs)")
    ap.add_argument("id", help="identifiant NEOST_F")
    ap.add_argument("--alt", action="append", required=True, help="ÉTIQUETTE=binaire alternatif")
    ap.add_argument("--frames", type=int, default=ru.FRAMES)
    ap.add_argument("--jobs", type=int, default=6)
    a = ap.parse_args()

    rank = json.loads(Path(a.rank).read_text())
    entry = next((r for r in rank["reached"] if r["id"] == a.id), None)
    if entry is None or "runs" not in entry:
        raise SystemExit(f"{a.id} absent du classement, ou classement sans champ runs")
    alts = [(s.split("=", 1)[0], Path(s.split("=", 1)[1]).resolve()) for s in a.alt]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(a.jobs) as pool:
        rows = list(pool.map(lambda j: one(j, alts, a.frames, out), [tuple(r) for r in entry["runs"]]))

    lines = [f"{a.id} : {len(rows)} runs, {a.frames} trames ; hachage toutes les {HASH_EVERY} trames, "
             f"captures toutes les {SHOT}"]
    for label, _ in alts:
        touched = [r for r in rows if r.get(label, {}).get("first_hash_frame") is not None]
        seen = [r for r in rows if r.get(label, {}).get("shots_diff")]
        lines.append(f"[{label}] état modifié : {len(touched)}/{len(rows)} ; image modifiée : {len(seen)}")
    for r in sorted(rows, key=lambda r: r["disk"]):
        cells = [f"{lab}: " + ("erreur" if "error" in r else
                 f"h@{r[lab]['first_hash_frame']} img {r[lab]['shots_diff']}/{r[lab]['shots']} "
                 f"max {r[lab]['worst_px']} px") for lab, _ in alts]
        lines.append(f"  {r['machine']:7} {r['disk']}  |  " + "  |  ".join(cells))
    (out / "sensitivity.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False))
    (out / "sensitivity.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
