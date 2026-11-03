#!/usr/bin/env python3
# =============================================================================
#  run_glue_sweep.py — Machine GLUE de NeoST contre le modèle GSTMCU, écriture par écriture.
#
#  POURQUOI. Échelon `rtl`, bloc « fort » (docs/REFERENCE_LADDER.md) : les retraits de bordure du
#  STE sont un port d'Hatari, jamais confronté au modèle du circuit. Ce banc pose une impulsion
#  60 Hz ou haute résolution à chaque phase de la ligne, et compare DE et octets consommés.
#  Grade D : un écart localise une question. STE seulement, le ST n'a pas de modèle.
#
#  COMMENT. Les deux côtés reçoivent le même plan. Date GLUE d'une écriture = chute d'AS dans le
#  banc + un décalage calibré, qui change de valeur selon le registre : 5 pour $FF820A, 8 pour
#  $FF8260, qui attend en plus le créneau de 4 cycles. Hors du retrait droit, où le modèle finit DE à
#  464 et NeoST à 460 pour les mêmes 44 octets, hors du cadre visible : tolérance écrite.
#
#  Usage : run_glue_sweep.py [--write-refs] [--kinds hi,f60] [--widths 8,12] [--step 4] [--offsets N,N]
#  Sorties : tests/out/glue_sweep/report.json. Verdicts : tests/reference/glue_sweep.json, un écart
#  qui change fait échouer. Code 77 sans Verilator. Détail : docs/GLUE_SWEEP.md.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RTL = ROOT / "build-rtl" / "gstmcu" / "Vste_tb"
BARE = ROOT / "build" / "neost-glue-bare"
OUT = ROOT / "tests" / "out" / "glue_sweep"
REF = ROOT / "tests" / "reference" / "glue_sweep.json"
OFFSETS = {"hi": 8, "f60": 5}       # cf. COMMENT
# (forme, largeurs, pas) : les retraits de bordure réels tiennent dans ces largeurs ; au-delà, une
# impulsion enjambe HSYNC et le rechargement de HSC, et aucun logiciel connu ne l'emploie.
PLAN = (("hi", (8, 12, 16, 24, 32), 4), ("f60", (8, 12, 24, 44), 2))
LINES = list(range(70, 245, 3))     # ligne de chaque expérience dans la trame ; 3 d'écart
HS = 2                              # ligne de la GLUE = numéro de HSYNC depuis VSYNC − 2
WINDOW = 2 * 512                    # cycles observés depuis le début de la ligne de l'impulsion
FIRST_FRAME = 2                     # la trame 2 : après la pose du mode


def experiments(plan):
    ex = []
    for kind, widths, step in plan:
        for w in widths:
            for c0 in range(0, 512, step):
                ex.append({"kind": kind, "w": w, "c0": c0})
    for i, e in enumerate(ex):
        e["id"] = i
        e["frame"] = FIRST_FRAME + i // len(LINES)
        e["line"] = LINES[i % len(LINES)]
    return ex


def writes_of(e):
    """Écritures en coordonnées de la GLUE : ligne, cycle, résolution ?, valeur."""
    res = e["kind"] == "hi"
    on_v, off_v = (2, 0) if res else (0, 2)
    return [(e["line"], e["c0"], res, on_v), (e["line"], e["c0"] + e["w"], res, off_v)]


def rtl_plan_line(frame, line, cycle, res, val):
    # Cycle 460 et au-delà : avant le HSYNC suivant, donc déjà dans la ligne RTL suivante.
    while cycle >= 460:
        line, cycle = line + 1, cycle - 512
    addr = 0xFF8260 if res else 0xFF820A
    return f"{frame} {line + HS} {cycle} {addr:06X} {val << 8:04X}"


def line_pieces(edges, hsyncs, frame, line):
    """Morceaux de DE (début, fin, octets) des deux lignes qui suivent, en cycles depuis le début de
    la ligne `line`, dans la grille posée par la ligne d'avant : le HSYNC de `line` tombe 512 cycles
    après le précédent, et une impulsion peut déplacer HSYNC sans déplacer DE."""
    t_prev = hsyncs.get((frame, line + HS - 1))
    if t_prev is None:
        return None
    t0 = t_prev + 2048
    pieces, start = [], None
    for tick, level, vid in edges:
        if level and t0 <= tick < t0 + WINDOW * 4:
            start = (tick, vid)
        elif not level and start is not None:
            # Le compteur avance après le dernier mot chargé pendant DE : un mot de plus.
            pieces.append(((start[0] - t0) // 4 - 52, (tick - t0) // 4 - 52, (vid - start[1]) * 2 + 2))
            start = None
    return pieces


def run_rtl(ex, work: Path, jobs: int) -> dict:
    frames = defaultdict(list)
    for e in ex:
        frames[e["frame"]].append(e)
    keys = sorted(frames)
    # Un processus par paquet de trames : chaque simulation repart du reset.
    size = max(1, len(keys) // jobs + 1)
    chunks = [keys[i:i + size] for i in range(0, len(keys), size)]

    def one(idx, chunk):
        base = chunk[0] - 1                 # la trame 0 suit la pose du mode : on la saute
        plan, order = [], []
        for f in chunk:
            for e in frames[f]:
                for ln, cy, r, v in writes_of(e):
                    plan.append((f - base, rtl_plan_line(f - base, ln, cy, r, v)))
                order.append((f - base, e))
        plan_path = work / f"plan{idx}.txt"
        plan_path.write_text("\n".join(p for _, p in plan) + "\n")
        out_path = work / f"out{idx}.txt"
        subprocess.run([str(RTL), "--glue", str(plan_path), str(out_path)], check=True, timeout=3000)
        edges, hsyncs = defaultdict(list), {}
        for line in out_path.read_text().splitlines():
            f = line.split()
            if f[0] == "E":
                edges[int(f[1])].append((int(f[2]), int(f[3]), int(f[4])))
            elif f[0] == "H":
                hsyncs[(int(f[1]), int(f[2]))] = int(f[3])
        res = {}
        for fr, e in order:
            res[e["id"]] = line_pieces(edges[fr], hsyncs, fr, e["line"])
        return res

    merged = {}
    with ThreadPoolExecutor(jobs) as pool:
        for part in pool.map(lambda a: one(*a), enumerate(chunks)):
            merged.update(part)
    return merged


def run_neost(ex, offsets) -> dict:
    text = []
    for e in ex:
        ws = writes_of(e)
        text.append(f"{e['id']} {e['line']} {len(ws)} " + " ".join(
            f"{ln} {cy + offsets[e['kind']]} {int(r)} {v}" for ln, cy, r, v in ws))
    r = subprocess.run([str(BARE), "ste"], input="\n".join(text) + "\n", capture_output=True, text=True, check=True)
    rows = defaultdict(list)
    for line in r.stdout.splitlines():
        i, l, st, en, b, m, ls = line.split()
        rows[int(i)].append((int(st), int(en), int(b), m, int(ls)))
    res = {}
    for i, lines in rows.items():
        origin = lines[0][4]
        pieces = []
        for st, en, b, m, ls in lines:
            # Le rejeu ne tient l'échelle des lignes qu'en mode LINELEN ; à défaut, 512 par ligne.
            if b > 0 and st >= 0:
                on = ls - origin + st
                if -52 <= on < WINDOW - 52:
                    pieces.append((on, ls - origin + en, b))
        res[i] = (pieces, lines[0][3], lines[1][3])
    return res


def compare(ex, rtl, neo):
    diffs = {}
    for e in ex:
        r, (n, m0, m1) = rtl[e["id"]], neo[e["id"]]
        # Fin de DE du retrait droit : 464 contre 460, mêmes octets, cf. COMMENT.
        r = None if r is None else [(a, 460 if b == 464 else b, c) for a, b, c in r if a < WINDOW - 52]
        if r != n:
            diffs[f"{e['kind']} c0={e['c0']} w={e['w']}"] = {"rtl": r, "neost": n, "mask": f"{m0}/{m1}"}
    return diffs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--write-refs", action="store_true")
    ap.add_argument("--kinds")
    ap.add_argument("--widths")
    ap.add_argument("--step", type=int)
    ap.add_argument("--offsets", help="essai : décalages à balayer, sur toutes les formes")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--show", type=int, default=12)
    a = ap.parse_args()
    if not shutil.which("verilator") or not RTL.exists():
        print("SKIP — balayage GLUE : Vste_tb absent (tools/run_gstmcu_bench.py le bâtit)")
        return 77
    if not BARE.exists():
        print("SKIP — balayage GLUE : build/neost-glue-bare absent")
        return 77
    OUT.mkdir(parents=True, exist_ok=True)
    trial = bool(a.kinds or a.widths or a.step or a.offsets)
    plan = PLAN
    if trial:
        plan = tuple((k, tuple(int(x) for x in a.widths.split(",")) if a.widths else w, a.step or st)
                     for k, w, st in PLAN if not a.kinds or k in a.kinds.split(","))
    ex = experiments(plan)
    rtl = run_rtl(ex, OUT, a.jobs)
    if a.offsets:
        for o in (int(x) for x in a.offsets.split(",")):
            n = len(compare(ex, rtl, run_neost(ex, defaultdict(lambda o=o: o))))
            print(f"décalage {o:+d} : {n} expériences en écart sur {len(ex)}")
        return 0
    diffs = compare(ex, rtl, run_neost(ex, OFFSETS))
    (OUT / "report.json").write_text(json.dumps(diffs, indent=0))
    print(f"{len(ex)} écritures en impulsion, {len(diffs)} en écart avec le modèle GSTMCU")
    for key, v in list(diffs.items())[:a.show]:
        print(f"  {key} rtl={v['rtl']} neost={v['neost']} masque={v['mask']}")
    if trial:
        return 0
    if a.write_refs:
        REF.write_text(json.dumps(diffs, indent=0, sort_keys=True) + "\n")
        print(f"référence écrite : {REF}")
        return 0
    if not REF.exists():
        print("Pas de référence : un écart fait échouer.")
        return 1 if diffs else 0
    ref = json.loads(REF.read_text())
    # Passage par JSON : les tuples de la mesure deviennent des listes, comme dans la référence.
    now = json.loads(json.dumps(diffs))
    changed = sorted(k for k in set(ref) | set(now) if ref.get(k) != now.get(k))
    if changed:
        print(f"VERDICTS CHANGÉS ({len(changed)}) : " + ", ".join(changed[:10]))
        return 1
    print("GLUE SWEEP OK : verdicts identiques à la référence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
