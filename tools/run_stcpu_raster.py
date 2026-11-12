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
#  Cas fixes (CASES) : le hardscroll « LO/MED/LO » de Paulo Simoes (`med2` de make_stcpu_raster.py),
#  quatre largeurs de bascule moyenne résolution posées deux fois dans la trame — toutes les lignes hors
#  des deux lignes de bascule sont identiques au modèle ; sur elles, la fenêtre moyenne elle-même et la
#  colonne exacte du rechargement restent en écart (cf. VideoGlue::detectPhaseScroll). Puis les deux
#  impulsions haute résolution des étalons blankline_stop_ste et blankline_node_late_ste, aux mêmes
#  cycles : 3 px d'écart chacune, à la colonne de l'écriture (le Shifter sort du monochrome le temps de
#  la bascule, non modélisé). Verdict : le compte par ligne égale tests/reference/stcpu_raster_cases.json.
#
#  Usage : run_stcpu_raster.py [--rebuild] [--write-refs] [--kind f60] [--width 8] [--step 8] [--jobs 8]
#  Code : 0 si les dates en écart égalent tests/reference/stcpu_raster.json, et les cas fixes
#  tests/reference/stcpu_raster_cases.json ; 1 sinon ; 77 sans outillage.
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
REF_CASES = ROOT / "tests" / "reference" / "stcpu_raster_cases.json"
# Cas fixes, en plus du balayage : (forme, largeur, date). Date 436 : moyenne au cycle 84 de la
# ligne 74 dans NeoST. Haute résolution aux cycles des étalons blankline_stop_ste (108 → 204,
# STOP_MIDDLE) et blankline_node_late_ste (56 → 104), ligne 74 (NEOST_SYNC_TRACE).
CASES = [("med2", w, 436) for w in (8, 12, 16, 20)] + [("hi", 96, 460), ("hi", 48, 408)]
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
    wrong, lines, per_line = 0, set(), {}
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
                per_line[str(y)] = per_line.get(str(y), 0) + 1
    return {"date": date, "wrong": wrong, "lines": sorted(lines), "per_line": per_line}


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
    with ThreadPoolExecutor(a.jobs) as pool:
        cases = {f"{k}_{w}_{d}": r["per_line"] for (k, w, d), r in zip(CASES, pool.map(one, CASES))}
    for name, per in cases.items():
        print(f"  {name:<14} : {sum(per.values()):5d} px, lignes {sorted(per, key=int)}")
    if a.write_refs:
        REF.write_text(json.dumps(bad, indent=0, sort_keys=True) + "\n")
        REF_CASES.write_text(json.dumps(cases, indent=0, sort_keys=True) + "\n")
        return 0
    status = 0
    if REF.exists() and json.loads(REF.read_text()) != bad or not REF.exists() and bad:
        print("BANC AU PIXEL : verdicts changés")
        status = 1
    if REF_CASES.exists() and json.loads(REF_CASES.read_text()) != cases:
        print("BANC AU PIXEL : cas fixes changés (LO/MED/LO, impulsions haute résolution)")
        status = 1
    if status == 0:
        print("BANC AU PIXEL OK : verdicts identiques à la référence")
    return status


if __name__ == "__main__":
    sys.exit(main())
