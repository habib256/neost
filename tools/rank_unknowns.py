#!/usr/bin/env python3
"""Classe les inconnues [F] par nombre de titres qui traversent leur chemin de code.

Chaque chemin incertain porte NEOST_F("id") (src/util/Unknowns.hpp). Ce banc fait tourner
neost-headless sur une collection de disques, avec --unknowns, et agrège : pour chaque id,
combien de disques l'atteignent, sous quelles machines, avec combien de passages. Les sites
présents dans les sources mais jamais atteints sont listés à part : une inconnue que rien
ne traverse peut attendre.

Préréglages :
  collection  disks/st et disks/stx en ST/TOS 1.04, STE/TOS 1.62, Mega STE/TOS 2.06
              (la suite « collection » de cpu_swap_gate.py : ESPACE toutes les 500 trames
              de 1000 à 3500, 4000 trames)
  lgd         disks/LGD/*/*.ST (médias privés) en ST/TOS 1.04, 1 Mo, mêmes entrées

Usage :
  rank_unknowns.py OUT --preset collection lgd [--jobs N]
  rank_unknowns.py OUT --disks a.st b.msa --machine st --rom roms/tos104uk.img
Le disque est copié avant chaque run : un titre peut écrire sur sa disquette.

Bruit du boot : le TOS traverse lui-même des chemins [F] (sondage du port cartouche et de la
RAM, densité HD du TOS 2.06 sur Mega STE…). Pour chaque machine et ROM, un run de base sur la
disquette neutre disks/diskA.st, avec les mêmes entrées, dit lesquels. Un site ne compte pour un
titre que s'il manque à cette base ou s'il y passe plus de BOOT_FACTOR fois plus souvent ; les
sites de la base sont listés à part.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build/neost-headless"
EXT = {".st", ".msa", ".stx", ".dim"}
FRAMES = 4000
SPACE = range(1000, FRAMES, 500)
BASE_DISK = ROOT / "disks/diskA.st"
BOOT_FACTOR = 2
CFG_ALL = (("st", "roms/tos104uk.img"), ("ste", "roms/tos162uk.img"), ("megaste", "roms/tos206uk.img"))


def disks_in(*dirs: str) -> list[Path]:
    return sorted(p for d in dirs if (ROOT / d).is_dir()
                  for p in (ROOT / d).rglob("*") if p.is_file() and p.suffix.lower() in EXT)


def preset_jobs(name: str) -> list[tuple[str, str, Path]]:
    if name == "collection":
        return [(m, r, d) for m, r in CFG_ALL for d in disks_in("disks/st", "disks/stx")]
    if name == "lgd":
        return [("st", "roms/tos104uk.img", d) for d in disks_in("disks/LGD")]
    raise SystemExit(f"préréglage inconnu : {name}")


def source_sites() -> dict[str, str]:
    """id → fichier:ligne de chaque NEOST_F("id") des sources."""
    out = {}
    for path in sorted((ROOT / "src").rglob("*.[ch]pp")):
        if path.name == "Unknowns.hpp":
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in re.finditer(r'NEOST_F\("([^"]+)"\)', line):
                out.setdefault(m.group(1), f"{path.relative_to(ROOT).as_posix()}:{n}")
    return out


def run_one(job: tuple[str, str, Path], frames: int, work: Path) -> tuple[str, str, Path, list | str]:
    machine, rom, disk = job
    if not (ROOT / rom).exists():
        return machine, rom, disk, f"ROM absente ({rom})"
    with tempfile.TemporaryDirectory(dir=work) as tmp:
        copy = Path(tmp) / ("d" + disk.suffix)
        shutil.copy(disk, copy)
        report = Path(tmp) / "unknowns.json"
        cmd = [str(HEADLESS), str(ROOT / rom), "--machine", machine, "--mem", "1m",
               "--disk", str(copy), "--frames", str(frames), "--unknowns", str(report)]
        for at in SPACE:
            if at < frames:
                cmd += ["--scancode-at", str(at), "39"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if not report.exists():
            return machine, rom, disk, f"pas de rapport (rc={r.returncode}) {r.stderr[-200:]}"
        return machine, rom, disk, json.loads(report.read_text())["sites"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="dossier de sortie (rank.json, rank.txt)")
    ap.add_argument("--preset", nargs="*", default=[], choices=("collection", "lgd"))
    ap.add_argument("--disks", nargs="*", default=[], help="disques en plus")
    ap.add_argument("--machine", default="st")
    ap.add_argument("--rom", default="roms/tos104uk.img")
    ap.add_argument("--frames", type=int, default=FRAMES)
    ap.add_argument("--jobs", type=int, default=6)
    a = ap.parse_args()

    jobs = [j for p in a.preset for j in preset_jobs(p)]
    jobs += [(a.machine, a.rom, Path(d).resolve()) for d in a.disks]
    if not jobs:
        ap.error("rien à faire : --preset ou --disks")
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    # Base par machine et ROM : ce que le TOS traverse seul.
    base = {}
    for machine, rom in sorted({(m, r) for m, r, _ in jobs}):
        res = run_one((machine, rom, BASE_DISK), a.frames, out)[3]
        base[(machine, rom)] = {} if isinstance(res, str) else {x["id"]: x["hits"] for x in res}
    boot_sites = sorted({k for b in base.values() for k in b})

    titles = defaultdict(set)            # id → {disque}
    machines = defaultdict(set)          # id → {machine}
    runs = defaultdict(set)              # id → {(machine, rom, disque)} : à rejouer (sensitivity_unknown.py)
    hits = defaultdict(int)
    where = {}
    errors = []
    with ThreadPoolExecutor(a.jobs) as pool:
        futures = [pool.submit(run_one, j, a.frames, out) for j in jobs]
        for i, f in enumerate(futures, 1):
            machine, rom, disk, res = f.result()
            ref = base.get((machine, rom), {})
            name = disk.relative_to(ROOT).as_posix() if disk.is_relative_to(ROOT) else disk.name
            if isinstance(res, str):
                errors.append(f"{machine} {name}: {res}")
            else:
                for s in res:
                    if s["id"] in ref and s["hits"] <= BOOT_FACTOR * ref[s["id"]]:
                        continue                      # le boot seul l'explique
                    titles[s["id"]].add(name)
                    machines[s["id"]].add(machine)
                    runs[s["id"]].add((machine, rom, name))
                    hits[s["id"]] += s["hits"]
                    where[s["id"]] = f'{s["file"]}:{s["line"]}'
            if i % 50 == 0 or i == len(futures):
                print(f"  {i}/{len(futures)}", file=sys.stderr, flush=True)

    declared = source_sites()
    disk_count = len({d for _, _, d in jobs})
    rows = sorted(titles, key=lambda k: (-len(titles[k]), -hits[k], k))
    report = {
        "runs": len(jobs), "disks": disk_count, "frames": a.frames, "errors": errors,
        "reached": [{"id": k, "site": where[k], "titles": len(titles[k]), "hits": hits[k],
                     "machines": sorted(machines[k]), "disks": sorted(titles[k]),
                     "runs": [list(r) for r in sorted(runs[k])]} for k in rows],
        "boot": [{"id": k, "base": {f"{m}/{Path(r).name}": b.get(k, 0) for (m, r), b in base.items()}}
                 for k in boot_sites],
        "never_reached": sorted((k, v) for k, v in declared.items()
                                if k not in titles and k not in boot_sites),
    }
    (out / "rank.json").write_text(json.dumps(report, indent=1, ensure_ascii=False))
    lines = [f"{len(jobs)} runs, {disk_count} disques, {a.frames} trames ; {len(errors)} erreurs",
             f"{'titres':>6} {'passages':>12}  id  (site ; machines)"]
    for r in report["reached"]:
        lines.append(f"{r['titles']:>6} {r['hits']:>12}  {r['id']}  ({r['site']} ; {','.join(r['machines'])})")
    lines.append(f"atteints par le boot seul, comptés pour un titre au-delà de {BOOT_FACTOR}× la base "
                 f"({len(boot_sites)}) :")
    lines += [f"       {b['id']}  {b['base']}" for b in report["boot"]]
    lines.append(f"jamais atteints ({len(report['never_reached'])}) :")
    lines += [f"       {k}  ({v})" for k, v in report["never_reached"]]
    (out / "rank.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
