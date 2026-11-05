#!/usr/bin/env python3
"""Sonde des deux séquences horizontales de l'intro Elite : NeoST et GSTMCU.

Les dates viennent des traces du jeu, aucun octet de son programme n'est inclus.
La séquence est translatée des lignes 33–36 aux lignes 99–102 pour isoler
l'horizontal, sans dépendre de l'ouverture verticale ni du CPU.
Échelon rtl, grade D : cet accord ne valide pas la datation matérielle du CPU.
Code 77 sans les modèles, 1 si les observations changent, 0 sinon.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import run_glue_sweep as sweep


def logical_writes(case):
    late = case["kind"] == "late"
    shift = -4 if late else 0
    return [
        (99, 500 if late else 480, False, 0),
        (100, 40 if late else 20, False, 2),
        (100, 508 + shift, True, 2), (101, 12 + shift, True, 0),
        (101, 376 + shift, False, 0), (101, 384 + shift, False, 2),
        (101, 444 + shift, True, 2), (101, 456 + shift, True, 0),
        (101, 508 + shift, True, 2), (102, 12 + shift, True, 0),
    ]


def as_writes(case):
    # [D] Convention du banc CPU/raster existant : écriture NeoST à AS+8.
    # Le modèle GSTMCU reçoit AS, pas la date ajustée par sa réponse propre.
    return [(line, cycle - 8, res, val)
            for line, cycle, res, val in logical_writes(case)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=sweep.ROOT / "build/vroom-intro-glue")
    args = ap.parse_args()
    if sweep.BASE60:
        ap.error("This probe requires PAL; unset NEOST_GLUE_60")
    if not sweep.RTL.exists() or not sweep.BARE.exists():
        print("SKIP: build Vste_tb and neost-glue-bare first")
        return 77
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cases = [dict(id=i, frame=2+i, line=100, kind=kind)
             for i, kind in enumerate(("normal", "late"))]
    previous = sweep.writes_of
    try:
        sweep.writes_of = as_writes
        rtl = sweep.run_rtl(cases, out, 1)
        neo = sweep.run_neost(cases, {"normal": 8, "late": 8})
    finally:
        sweep.writes_of = previous
    # Observations indépendantes des deux modèles. La fin DE du retrait droit
    # reste décalée de 4 cycles (976/972), déjà connue dans GLUE_SWEEP.md.
    expected_rtl = {0: [(56, 376, 160), (516, 976, 230)],
                    1: [(52, 376, 162), (516, 884, 184)]}
    expected_neo = {0: [(56, 376, 160), (516, 972, 230)],
                    1: [(52, 376, 162), (516, 884, 184)]}
    ok = rtl == expected_rtl and {k: v[0] for k, v in neo.items()} == expected_neo
    report = dict(ref_kind="rtl", references={"gstmcu": "4a6db7cc"},
                  rtl=rtl, neost=neo, expected_rtl=expected_rtl, expected_neost=expected_neo,
                  logical_writes={c["kind"]: logical_writes(c) for c in cases},
                  passed=ok, limits=[
                      "No hardware measurement; model agreement only.",
                      "CPU interrupt entry and vertical border opening are not tested.",
                      "Normal right-border DE ends 4 cycles apart; byte counts agree."])
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    for case in cases:
        i = case["id"]
        print(f"{case['kind']}: RTL {rtl.get(i)}, NeoST {neo.get(i)}")
    print("INTRO GLUE OBSERVATIONS UNCHANGED" if ok else "INTRO GLUE OBSERVATIONS CHANGED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
