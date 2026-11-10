#!/usr/bin/env python3
# =============================================================================
#  cpu_swap_gate.py — Le nouveau cœur 68000 se comporte-t-il exactement comme l'ancien ?
#
#  POURQUOI. Le Moira de NeoST sera remplacé par celui de POM68K (68000/030/040 + JIT),
#  qui doit rester cycle-exact en 68000. Un étalon pixel vert ne le prouve pas : une
#  image tolère des écarts que l'état machine, lui, révèle. Cette porte compare, trame
#  par trame, une empreinte INDÉPENDANTE DU CŒUR (`--arch-hash-every`) :
#    machine = l'état complet hors section CPU et hors en-tête de format ;
#    cpu     = registres, SR, préfetch IRC/IRD, horloge bus, halt/stop.
#  Le nouveau cœur peut sérialiser ses internes comme il veut ; il doit rendre ces deux
#  empreintes à l'identique sur chaque scénario.
#
#  COMMENT. Les scénarios sont les étalons pixel de tools/etalons.json, rejoués avec la
#  commande même de run_etalons.py.
#    1. Sur le DERNIER commit avant l'échange :  cpu_swap_gate.py --record FICHIER
#    2. Sur le commit de l'échange :              cpu_swap_gate.py --check  FICHIER
#  La première trame qui diffère est rapportée, avec le hachage fautif : `cpu` seul →
#  le cœur ; `machine` seul → un accès bus ou une date d'accès ; les deux → à départager
#  par trace (`--trace FICHIER --trace-from N --regs --irq`, puis tools/trace_diff.py).
#  Un écart se tranche au banc fx68k (docs/FX68K_BENCH.md), jamais par recalage.
#  Les références ne sont PAS versionnées : elles changent à chaque correction
#  d'émulation. Procédure → docs/CPU_SWAP_GATE.md.
#
#  Codes : 0 accord, 1 écart, 2 usage, 77 neost-headless absent.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_etalons as E  # noqa: E402

LINE = re.compile(r"arch-hash frame=(\d+) machine=([0-9a-f]+) cpu=([0-9a-f]+)")


def scenarios(only: list[str] | None) -> list[dict]:
    out = []
    for e in E.load_manifest():
        if e.get("type", "").endswith("selftest") or "frames" not in e:
            continue
        if only and e["id"] not in only:
            continue
        out.append(e)
    return out


def run(entry: dict, every: int) -> tuple[str, str, list[tuple[int, str, str]]]:
    """(id, statut, empreintes). Statut : 'ok', ou 'skip: …', ou 'error: …'."""
    eid = entry["id"]
    rom = entry.get("rom", "roms/etos192us.img")
    if not (E.ROOT / rom).exists():
        return eid, f"skip: ROM absente ({rom})", []
    if entry.get("disk") and not E.ensure_disk(entry):
        return eid, f"skip: disque absent ({entry['disk']})", []
    cmd = E.headless_cmd(entry) + ["--arch-hash-every", str(every)]
    r = E.run_timed(cmd, 900, cwd=E.ROOT, capture_output=True, text=True)
    rows = [(int(f), m, c) for f, m, c in LINE.findall(r.stdout or "")]
    if r.returncode != 0 and not rows:
        return eid, f"error: exit {r.returncode}", []
    return eid, "ok", rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Porte d'échange du cœur 68000 (docs/CPU_SWAP_GATE.md)")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", metavar="FICHIER", help="enregistre les empreintes du cœur courant")
    mode.add_argument("--check", metavar="FICHIER", help="compare au fichier enregistré")
    ap.add_argument("--every", type=int, default=10, help="une empreinte toutes les N trames (10)")
    ap.add_argument("--only", nargs="*", help="identifiants d'étalons à rejouer")
    ap.add_argument("--jobs", type=int, default=6)
    a = ap.parse_args()
    if not E.HEADLESS.exists():
        print("SKIP — neost-headless absent")
        return 77

    ref = None
    if a.check:
        ref = json.loads(Path(a.check).read_text(encoding="utf-8"))
        a.every = ref["every"]
    todo = scenarios(a.only)
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        results = list(pool.map(lambda e: run(e, a.every), todo))

    if a.record:
        rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=E.ROOT,
                             capture_output=True, text=True).stdout.strip()
        data = {"every": a.every, "commit": rev,
                "scenarios": {eid: rows for eid, st, rows in results if st == "ok"},
                "skipped": {eid: st for eid, st, _ in results if st != "ok"}}
        Path(a.record).write_text(json.dumps(data, indent=0) + "\n", encoding="utf-8")
        n = sum(len(v) for v in data["scenarios"].values())
        print(f"enregistré : {len(data['scenarios'])} scénarios, {n} empreintes, commit {rev}")
        for eid, st in data["skipped"].items():
            print(f"  {eid} : {st}")
        return 1 if any(st.startswith("error") for st in data["skipped"].values()) else 0

    bad = 0
    for eid, st, rows in results:
        want = ref["scenarios"].get(eid)
        if want is None:
            print(f"  --   {eid} : absent de la référence ({st})")
            continue
        if st != "ok":
            print(f"  ÉCHEC {eid} : {st}")
            bad += 1
            continue
        got = {f: (m, c) for f, m, c in rows}
        for f, m, c in want:
            g = got.get(f)
            if g == (m, c):
                continue
            if g is None:
                what = "trame non atteinte"
            else:
                what = " et ".join(k for k, x, y in (("machine", m, g[0]), ("cpu", c, g[1])) if x != y)
            prev = max((p for p, _, _ in want if p < f), default=0)
            print(f"  ÉCHEC {eid} : première divergence trame {f} ({what}) ; "
                  f"dernière concordance trame {prev}")
            bad += 1
            break
        else:
            print(f"  OK   {eid} ({len(want)} empreintes)")
    print(f"PORTE CPU : {'ÉCHEC, ' + str(bad) + ' scénario(s)' if bad else 'accord'} "
          f"(référence du commit {ref.get('commit', '?')})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
