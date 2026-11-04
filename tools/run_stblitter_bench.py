#!/usr/bin/env python3
# =============================================================================
#  run_stblitter_bench.py — Banc du blitter : NeoST contre fx68k + stBlitter.sv.
#
#  POURQUOI. Échelon `rtl` de docs/REFERENCE_LADDER.md, bloc « fort » jusqu'ici sans banc. Le
#  même programme tourne des deux côtés, et on compare chaque accès du blitter (sens, adresse,
#  donnée) et leurs dates. Grade D des deux côtés : un écart LOCALISE une question, il ne dit pas
#  qui a raison, et rien ici n'ajuste NeoST. Détail : docs/STBLITTER_BENCH.md.
#
#  COMMENT. tools/make_blitter_programs.py assemble ; Vstblitter_bench et neost-blitter-bare
#  exécutent, avec les créneaux de 4 cycles de la MMU des deux côtés (--flat les retire) ;
#  chaque paire de traces donne un verdict : `same`, `timing` (mêmes accès, autres
#  dates) ou `seq` (autres accès, le plus grave). Un écart de 2 cycles CPU entre les programmes
#  ne se voit pas : on compare les intervalles, après le premier témoin.
#
#  Usage : run_stblitter_bench.py [--rebuild] [--write-refs] [--only A,B] [--verbose] [--flat]
#  Code : 0 verdicts égaux à tests/reference/stblitter_bench.json ; 1 sinon ; 77 sans Verilator,
#  sans extern/fx68k ou sans extern/mistery (tools/setup_silicon_refs.sh).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import make_blitter_programs as programs  # noqa: E402
from run_fx68k_bench import FX, ROMS, SOURCES  # noqa: E402

MISTERY = ROOT / "extern" / "mistery" / "atarist"
BUILD = ROOT / "build-rtl" / "stblitter"
OUT = ROOT / "tests" / "out" / "stblitter_bench"
REF = ROOT / "tests" / "reference" / "stblitter_bench.json"
RTLBIN = BUILD / "Vstblitter_bench"
BARE = ROOT / "build" / "neost-blitter-bare"
DRIVER = ROOT / "tools" / "stblitter_bench.cpp"
TOP = ROOT / "tools" / "stblitter_bench_top.sv"
RTL = MISTERY / "stBlitter.sv"


def skip(reason: str) -> int:
    print(f"SKIP — banc stBlitter : {reason}")
    return 77


def build_rtl() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    cmd = ["verilator", "--cc", "--exe", "--build", "-j", "0", "--no-timing", "--no-assert-case",
           "-Wno-fatal", "-Wno-lint", "-Wno-style", "-CFLAGS", "-O2", "--Mdir", str(BUILD / "obj"),
           "--top-module", "stblitter_bench_top", "-o", str(RTLBIN),
           *[str(FX / s) for s in SOURCES], str(RTL), str(TOP), str(DRIVER)]
    print("→ Verilator : fx68k + stBlitter")
    r = subprocess.run(cmd, cwd=BUILD, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit(1)
    for rom in ROMS:                    # $readmemb lit à côté de l'exécutable (cwd)
        shutil.copy(FX / rom, BUILD / rom)


def parse(path: Path) -> tuple[list[tuple], dict]:
    acc, end = [], {}
    for line in path.read_text().splitlines():
        f = line.split()
        if f and f[0] == "END":
            end = dict(x.split("=") for x in f[1:])
        elif len(f) == 5:
            acc.append((int(f[0]), f[1], f[2], f[3], f[4]))
    return acc, end


def compare(rtl: list[tuple], neo: list[tuple]) -> dict:
    """Les accès identiques dans l'ordre, puis les intervalles depuis l'accès précédent."""
    res = {"rtl": len(rtl), "neost": len(neo)}
    first_seq = next((i for i, (a, b) in enumerate(zip(rtl, neo)) if a[1:] != b[1:]), None)
    if first_seq is None and len(rtl) != len(neo):
        first_seq = min(len(rtl), len(neo))
    if first_seq is not None:
        res["verdict"] = "seq"
        res["first"] = first_seq
        res["at_rtl"] = " ".join(map(str, rtl[first_seq])) if first_seq < len(rtl) else "fin"
        res["at_neost"] = " ".join(map(str, neo[first_seq])) if first_seq < len(neo) else "fin"
        return res
    gaps = Counter()
    first_gap = None
    for i in range(1, len(rtl)):
        d = (neo[i][0] - neo[i - 1][0]) - (rtl[i][0] - rtl[i - 1][0])
        if d:
            gaps[d] += 1
            if first_gap is None:
                first_gap = i
    total = (neo[-1][0] - neo[0][0]) - (rtl[-1][0] - rtl[0][0]) if rtl else 0
    res["verdict"] = "timing" if gaps else "same"
    res["span_delta"] = total
    if gaps:
        res["gaps"] = {str(k): v for k, v in sorted(gaps.items())}
        res["first"] = first_gap
        res["at_rtl"] = " ".join(map(str, rtl[first_gap]))
        res["at_neost"] = " ".join(map(str, neo[first_gap]))
    return res


def signature(r: dict) -> dict:
    return {k: r[k] for k in ("verdict", "span_delta", "gaps", "rtl", "neost") if k in r}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--write-refs", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--flat", action="store_true", help="sans créneaux MMU (RAM plate), pour lire un écart ; pas de référence")
    a = ap.parse_args()
    if not shutil.which("verilator"):
        return skip("verilator absent du PATH")
    if not RTL.exists() or not (FX / "fx68k.sv").exists():
        return skip("extern/fx68k ou extern/mistery absents (tools/setup_silicon_refs.sh)")
    if not BARE.exists():
        return skip("build/neost-blitter-bare absent (cmake --build build --target neost-blitter-bare)")
    if a.rebuild or not RTLBIN.exists() or any(p.stat().st_mtime > RTLBIN.stat().st_mtime for p in (TOP, DRIVER, RTL)):
        build_rtl()
    OUT.mkdir(parents=True, exist_ok=True)
    names = a.only.split(",") if a.only else list(programs.PROGRAMS)
    env = dict(os.environ, NEOST_RAM_SLOT="0" if a.flat else "1")
    results = {}
    for n in names:
        img = OUT / f"{n}.bin"
        img.write_bytes(programs.build(n))
        rt, nt = OUT / f"{n}.rtl.txt", OUT / f"{n}.neost.txt"
        subprocess.run([str(RTLBIN), str(img), str(rt), *([] if a.flat else ["--slot"])], cwd=BUILD, check=True, capture_output=True, timeout=300)
        subprocess.run([str(BARE), str(img), str(nt)], cwd=ROOT, check=True, capture_output=True, timeout=300, env=env)
        ra, re_ = parse(rt)
        na, ne = parse(nt)
        r = compare(ra, na)
        if re_.get("reason") != "sentinel" or ne.get("reason") != "sentinel":
            r["verdict"] = "unfinished"
            r["reasons"] = [re_.get("reason"), ne.get("reason")]
        results[n] = r
        if a.verbose or r["verdict"] != "same":
            extra = "" if r["verdict"] == "same" else f"  1er #{r.get('first')} rtl[{r.get('at_rtl')}] neost[{r.get('at_neost')}]"
            print(f"  {r['verdict']:7s} {n:24s} {r['rtl']:5d}/{r['neost']:5d} acc  Δ={r.get('span_delta', '-')} {r.get('gaps', '')}{extra}")
    tally = Counter(r["verdict"] for r in results.values())
    print("BILAN", dict(tally), f"sur {len(results)} programmes")
    if a.flat:
        return 0
    if a.write_refs:
        REF.write_text(json.dumps({k: signature(v) for k, v in results.items()}, indent=1, sort_keys=True) + "\n")
        print(f"référence écrite : {REF}")
        return 0
    if not REF.exists():
        print("Pas de référence : un écart fait échouer.")
        return 0 if set(tally) == {"same"} else 1
    ref = json.loads(REF.read_text())
    changed = [k for k, v in results.items() if ref.get(k) != signature(v)]
    if changed:
        print("VERDICTS CHANGÉS :", ", ".join(changed))
        return 1
    print("verdicts identiques à la référence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
