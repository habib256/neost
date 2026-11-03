#!/usr/bin/env python3
# =============================================================================
#  run_stcpu_bench.py — Ce que le CPU voit du faisceau : NeoST contre fx68k + GSTMCU.
#
#  POURQUOI. Toute la synchro des démos repose sur la relation entre le 68000 et le balayage, jusqu'ici
#  calée sur Hatari. Le même programme tourne sur les deux RTL (échelon `rtl`, blocs « forts », grade D)
#  et dans NeoST ; il relève $FF8209 en RAM, et on compare les tables à l'octet. STE seulement.
#
#  Usage : run_stcpu_bench.py [--rebuild] [--write-refs]
#  Code : 0 si les écarts égalent tests/reference/stcpu_bench.json ; 1 sinon ; 77 sans Verilator ou sans
#  les RTL. Détail : docs/STCPU_BENCH.md.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_stcpu_bench as builder  # noqa: E402
import make_stcpu_rom as rom  # noqa: E402

HEADLESS = ROOT / "build" / "neost-headless"
OUT = ROOT / "tests" / "out" / "stcpu_bench"
REF = ROOT / "tests" / "reference" / "stcpu_bench.json"
FRAMES = 22       # jusqu'au remplissage de la dernière table
LOOPS = ("nop", "nops", "stop", "mulu")


def tables(a: bytes, b: bytes) -> dict:
    res = {}
    for k in range(3):
        lo, hi = k * rom.POLL_LEN, (k + 1) * rom.POLL_LEN
        res[f"poll{k}"] = [i for i in range(lo, hi) if a[i] != b[i]]
    base = rom.HBLTAB - rom.POLL0
    for k, name in enumerate(LOOPS):
        lo = base + k * 256
        res[f"hbl_{name}"] = [i - lo for i in range(lo, lo + 256) if a[i] != b[i]]
    base = rom.VBLTAB - rom.POLL0
    res["vbl"] = [i - base for i in range(base, base + 32) if a[i] != b[i]]
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--write-refs", action="store_true")
    a = ap.parse_args()
    if not shutil.which("verilator"):
        print("SKIP — banc CPU + GSTMCU : verilator absent")
        return 77
    if not (builder.FX / "fx68k.sv").exists() or not (builder.G / "tb" / "ste_tb.v").exists():
        print("SKIP — banc CPU + GSTMCU : RTL absents (tools/setup_silicon_refs.sh)")
        return 77
    if a.rebuild or not builder.BINARY.exists():
        builder.build()
    OUT.mkdir(parents=True, exist_ok=True)
    image = OUT / "rom.img"
    image.write_bytes(rom.build())
    ra, nb = OUT / "rtl.bin", OUT / "neost.bin"
    subprocess.run([str(builder.BINARY), str(image), str(FRAMES), f"{rom.POLL0:x}", str(rom.TOTAL), str(ra)],
                   check=True, capture_output=True, timeout=600, cwd=builder.BUILD)
    subprocess.run([str(HEADLESS), str(image), "--machine", "ste", "--mem", "4m", "--frames", str(FRAMES + 1),
                    "--disk", "disks/diskA.st", "--dump-at", str(FRAMES - 1), f"{rom.POLL0:x}", str(rom.TOTAL), str(nb)],
                   check=True, capture_output=True, timeout=600, cwd=ROOT)
    A, B = ra.read_bytes(), nb.read_bytes()
    if len(A) != rom.TOTAL or len(B) != rom.TOTAL:
        print("FAIL banc CPU + GSTMCU : table incomplète")
        return 1
    res = tables(A, B)
    for name, bad in res.items():
        total = rom.POLL_LEN if name.startswith("poll") else (32 if name == "vbl" else 256)
        print(f"  {name:10s} {total - len(bad):6d}/{total} lectures identiques")
    if a.write_refs:
        REF.write_text(json.dumps(res, indent=0, sort_keys=True) + "\n")
        print(f"référence écrite : {REF}")
        return 0
    if not REF.exists():
        print("Pas de référence : un écart fait échouer.")
        return 1 if any(res.values()) else 0
    if json.loads(REF.read_text()) != json.loads(json.dumps(res)):
        print("BANC CPU + GSTMCU : verdicts changés")
        return 1
    print("BANC CPU + GSTMCU OK : verdicts identiques à la référence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
