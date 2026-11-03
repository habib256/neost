#!/usr/bin/env python3
# =============================================================================
#  run_stcpu_raster.py — Le pixel : NeoST contre fx68k + GSTMCU, sur une vraie écriture du CPU.
#
#  POURQUOI. Toute la chaîne d'un coup : le CPU écrit $FF820A à une date, et la trame sort du Shifter.
#  Compare chaque pixel hors BLANK (le modèle y sort du noir, NeoST la couleur 0 : question inscrite au
#  TODO). Échelon `rtl`, grade D, STE seulement. Détail : docs/STCPU_BENCH.md.
#
#  Haute résolution (`--kind hi`) hors verdict : après une impulsion, le Shifter du modèle, bloc « moyen »,
#  garde une phase de 8 pixels jusqu'à la trame suivante. Effet réel ou non, rien ne le dit.
#
#  Usage : run_stcpu_raster.py [--rebuild] [--write-refs] [--kind f60] [--width 8] [--step 8] [--jobs 8]
#  Code : 0 si les dates en écart égalent tests/reference/stcpu_raster.json ; 1 sinon ; 77 sans outillage.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_stcpu_bench as builder  # noqa: E402
import make_stcpu_raster as raster  # noqa: E402

HEADLESS = ROOT / "build" / "neost-headless"
OUT = ROOT / "tests" / "out" / "stcpu_raster"
REF = ROOT / "tests" / "reference" / "stcpu_raster.json"
FRAMES = 12
PIPELINE, FIRST_LINE = 528, 65  # tools/run_gstmcu_bench.py, low_pal : 1er pixel et 1re ligne affichée


def falling(words, bit):
    return [i for i in range(1, len(words)) if (words[i - 1] >> bit) & 1 and not (words[i] >> bit) & 1]


def read_ppm(path: Path):
    data = path.read_bytes()
    parts, pos = [], 0
    while len(parts) < 4:
        end = pos
        while data[end] not in b" \n\t":
            end += 1
        parts.append(data[pos:end])
        pos = end + 1
    assert parts[0] == b"P6"
    return int(parts[1]), int(parts[2]), data[pos:]


def one(sc) -> dict:
    kind, width, date = sc
    work = OUT / f"{kind}_{width}_{date}"
    work.mkdir(parents=True, exist_ok=True)
    rom = work / "r.img"
    rom.write_bytes(raster.build(kind, width, date))
    video, ppm = work / "v.bin", work / "n.ppm"
    env = dict(os.environ, NEOST_STCPU_VIDEO=str(video))
    subprocess.run([str(builder.BINARY), str(rom), str(FRAMES), "20000", "16", str(work / "x.bin")],
                   check=True, capture_output=True, timeout=900, cwd=builder.BUILD, env=env)
    subprocess.run([str(HEADLESS), str(rom), "--machine", "ste", "--mem", "4m", "--frames", str(FRAMES),
                    "--disk", "disks/diskA.st", "--screenshot", str(ppm)],
                   check=True, capture_output=True, timeout=300, cwd=ROOT)
    raw = video.read_bytes()
    words = list(struct.unpack(f"<{len(raw) // 2}H", raw))
    starts = falling(words, 13)
    w, h, pix = read_ppm(ppm)
    wrong, lines = 0, set()
    for y in range(h):
        li = FIRST_LINE + y - 29
        if not 0 <= li < len(starts) - 1:
            continue
        line = words[starts[li]:starts[li + 1]]
        for x in range(w):
            t = PIPELINE + (x - 48) * 4 + 1
            if not 0 <= t < len(line) or not (line[t] >> 12) & 1:
                continue
            if tuple(((line[t] >> s) & 15) * 17 for s in (8, 4, 0)) != tuple(pix[(y * w + x) * 3:(y * w + x) * 3 + 3]):
                wrong += 1
                lines.add(y)
    return {"date": date, "wrong": wrong, "lines": sorted(lines)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--write-refs", action="store_true")
    ap.add_argument("--kind", default="f60")
    ap.add_argument("--width", type=int, default=8)
    ap.add_argument("--step", type=int, default=8)
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()
    if not shutil.which("verilator"):
        print("SKIP — banc au pixel : verilator absent")
        return 77
    if not (builder.FX / "fx68k.sv").exists() or not (builder.G / "tb" / "ste_tb.v").exists():
        print("SKIP — banc au pixel : RTL absents (tools/setup_silicon_refs.sh)")
        return 77
    if a.rebuild or not builder.BINARY.exists():
        builder.build()
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(a.jobs) as pool:
        results = list(pool.map(one, [(a.kind, a.width, d) for d in range(0, 512, a.step)]))
    bad = {str(r["date"]): r["wrong"] for r in results if r["wrong"]}
    for r in results:
        if r["wrong"]:
            print(f"  date {r['date']:3d} : {r['wrong']:6d} px, lignes {r['lines'][:4]}{'…' if len(r['lines']) > 4 else ''}")
    print(f"{len(results) - len(bad)}/{len(results)} dates identiques au pixel ({a.kind}, largeur {a.width})")
    if (a.kind, a.width, a.step) != ("f60", 8, 8):
        return 0
    if a.write_refs:
        REF.write_text(json.dumps(bad, indent=0, sort_keys=True) + "\n")
        return 0
    if not REF.exists():
        return 1 if bad else 0
    if json.loads(REF.read_text()) != bad:
        print("BANC AU PIXEL : verdicts changés")
        return 1
    print("BANC AU PIXEL OK : verdicts identiques à la référence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
