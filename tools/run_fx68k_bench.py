#!/usr/bin/env python3
# =============================================================================
#  run_fx68k_bench.py — Banc différentiel Moira↔fx68k : même programme, deux cœurs nus,
#  comparaison cycle bus par cycle bus.
#
#  POURQUOI. Le 68000 est le cœur de tout le reste : chaque datation vidéo, MFP ou
#  blitter de NeoST est posée sur l'horloge de Moira. Jusqu'ici Moira n'était confronté
#  qu'à Hatari (grade D, autre émulateur, `run_timing_oracle.py`) et aux totaux de
#  Musashi (`run_moira_runner.sh`). fx68k est un RTL cycle-exact dérivé du microcode
#  (bloc « fort » de l'échelon `rtl`, docs/REFERENCE_LADDER.md) : ce banc relève sur les
#  deux cœurs la date de chaque accès bus, la prise d'IPL et les séquences d'exception,
#  sur une RAM plate sans état d'attente — le cœur seul, pas la carte mère. Un écart
#  LOCALISE une question ; il ne dit pas qui a raison (grade D des deux côtés), et rien
#  ici n'ajuste Moira.
#
#  COMMENT. tools/make_fx68k_programs.py assemble les programmes ; tools/fx68k_bench.cpp
#  (Verilator) et tools/moira_bare_bench.cpp (cible CMake neost-moira-bare) les exécutent
#  et écrivent une trace au même format, datée au front descendant d'AS. Les deux traces
#  sont alignées sur leur premier accès (lecture du vecteur de reset) et comparées :
#  nature de l'accès (FC, sens, taille, adresse, donnée) puis date. La date du cycle
#  IACK est rapportée à part (sa place dans le bloc d'IACK de Moira n'est pas connue) :
#  si les accès suivants concordent, le total concorde et le verdict reste « agree ».
#
#  RÉFÉRENCE. tests/reference/fx68k_bench.json consigne le verdict de chaque variante
#  (accord, ou signature du premier écart). Sans ce fichier, un écart fait échouer ;
#  avec lui, c'est un CHANGEMENT de verdict qui fait échouer, dans les deux sens — un
#  écart qui se referme se re-pose aussi (--write-refs), après avoir été jugé.
#
#  Usage : run_fx68k_bench.py [--rebuild] [--write-refs] [--only NOM[,NOM]] [--verbose]
#  Sorties : tests/out/fx68k_bench/<variante>.{fx,moira}.txt, summary.json.
#  Code 77 (SKIP recensé) si Verilator, extern/fx68k (tools/setup_silicon_refs.sh) ou
#  build/neost-moira-bare (cmake --build build) manquent.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_fx68k_programs import PROGRAMS, build as build_program  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FX = ROOT / "extern" / "fx68k"
BUILD = ROOT / "build-rtl" / "fx68k"
OUT = ROOT / "tests" / "out" / "fx68k_bench"
REF = ROOT / "tests" / "reference" / "fx68k_bench.json"
_EXE = ".exe" if sys.platform == "win32" else ""
MOIRA = ROOT / "build" / ("neost-moira-bare" + _EXE)
DRIVER = ROOT / "tools" / "fx68k_bench.cpp"
FXBIN = BUILD / "Vfx68k_bench"
SOURCES = ["fx68k.sv", "fx68kAlu.sv", "uaddrPla.sv"]
ROMS = ["microrom.mem", "nanorom.mem"]
EXIT_SKIPPED = 77
SENTINEL = 0xF00000


def skip(reason: str) -> int:
    print(f"SKIP — banc fx68k : {reason}")
    return EXIT_SKIPPED


def need_rebuild() -> bool:
    if not FXBIN.exists():
        return True
    stamp = FXBIN.stat().st_mtime
    return any((FX / s).stat().st_mtime > stamp for s in SOURCES) or DRIVER.stat().st_mtime > stamp


def build_fx68k() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    # --no-assert-case : fx68kAlu.sv:313 @ 0602ee46 est un `unique case` qui ne couvre
    # pas l'état des signaux AVANT le reset ($stop à l'instant 0 sous Verilator 5.054) ;
    # sans incidence une fois le cœur sorti de reset.
    cmd = ["verilator", "--cc", "--exe", "--build", "-j", "0", "--no-timing",
           "--no-assert-case", "-Wno-fatal", "-Wno-lint", "-Wno-style", "-CFLAGS", "-O2",
           "--Mdir", str(BUILD / "obj"), "--top-module", "fx68k", "-o", str(FXBIN),
           *[str(FX / s) for s in SOURCES], str(DRIVER)]
    print("→ Verilator :", " ".join(cmd[:8]), "…")
    subprocess.run(cmd, check=True, cwd=BUILD, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    for r in ROMS:                      # $readmemb lit à côté de l'exécutable (cwd)
        shutil.copy(FX / r, BUILD / r)


def parse(path: Path) -> tuple[list[dict], dict]:
    trace, end = [], {"t": None, "reason": "missing"}
    for line in path.read_text().splitlines():
        if line.startswith("END"):
            kv = dict(p.split("=") for p in line.split()[1:])
            end = {"t": int(kv["t"]), "reason": kv["reason"]}
            continue
        t, fc, rw, size, addr, data = line.split()
        trace.append({"t": int(t), "fc": int(fc), "rw": rw, "size": int(size),
                      "addr": int(addr, 16), "data": int(data, 16)})
    return trace, end


def cut(trace: list[dict]) -> list[dict]:
    for i, a in enumerate(trace):
        if a["rw"] == "W" and (a["addr"] & 0xFF0000) == SENTINEL:
            return trace[: i + 1]
    return trace


def fmt(a: dict) -> str:
    return f"fc{a['fc']} {a['rw']}{a['size']} {a['addr']:06X}={a['data']:0{4 if a['size'] == 2 else 2}X}"


RESET_ACCESSES = 6   # SSP, PC (4 mots) puis les deux préfetchs : la séquence de reset


def compare(fx: list[dict], mo: list[dict], efx: dict, emo: dict, skip: int = RESET_ACCESSES) -> dict:
    """Même suite d'accès, puis mêmes INTERVALLES entre accès consécutifs. Comparer des
    intervalles plutôt que des dates absolues : un écart ne masque pas les suivants, et le
    rapport dit combien d'intervalles diffèrent, pas seulement le premier. L'intervalle qui
    entre dans un IACK est rapporté à part ; celui qui en sort est pris depuis l'accès qui
    précède l'IACK, pour que la place du cycle IACK dans le bloc de Moira ne compte pas.
    Les `skip` premiers accès (séquence de reset) ne sont pas datés : le programme
    reset_nop (skip = 0) est seul à porter cet écart, au lieu de tous les programmes."""
    fx, mo = cut(fx), cut(mo)
    res: dict = {"accesses": [len(fx), len(mo)], "end": [efx["reason"], emo["reason"]],
                 "iack_skew": [], "first": None, "mismatches": 0, "samples": [],
                 "swaps": 0, "access_mismatches": 0, "access_samples": []}
    if not fx or not mo:
        res["first"] = {"index": 0, "kind": "empty"}
        res["status"] = "diverge"
        return res
    res["total"] = [fx[-1]["t"] - fx[0]["t"], mo[-1]["t"] - mo[0]["t"]]
    prev_f, prev_m = fx[0], mo[0]
    key = lambda a: (a["fc"], a["rw"], a["size"], a["addr"], a["data"])  # noqa: E731
    i = j = 0
    n = 0                                   # rang de la paire comparée (pour skip)
    swapped = (-1, -1)
    res["extra"] = [0, 0]                   # accès présents d'un seul côté (fx, moira)
    while i < len(fx) and j < len(mo):
        a, b = fx[i], mo[j]
        if key(a) != key(b) and (i, j) != swapped:
            # Deux mots d'un même long écrits dans l'ordre inverse : compté comme « swap »,
            # pas comme deux accès étrangers ; les dates restent comparées.
            if i + 1 < len(fx) and j + 1 < len(mo) and key(fx[i + 1]) == key(b) and key(mo[j + 1]) == key(a):
                res["swaps"] += 1
                swapped = (i + 1, j + 1)
                kind = "swap"
            # Accès présent d'un seul côté : on le saute pour resynchroniser la suite.
            elif i + 1 < len(fx) and key(fx[i + 1]) == key(b):
                res["extra"][0] += 1
                kind = "extra_fx"
            elif j + 1 < len(mo) and key(mo[j + 1]) == key(a):
                res["extra"][1] += 1
                kind = "extra_moira"
            else:
                res["access_mismatches"] += 1
                kind = "access"
            if kind != "swap" and len(res["access_samples"]) < 6:
                res["access_samples"].append({"index": i, "kind": kind, "fx": fmt(a), "moira": fmt(b)})
            if res["first"] is None:
                res["first"] = {"index": i, "kind": kind, "fx": fmt(a), "moira": fmt(b)}
            # L'accès en plus devient l'origine de l'intervalle suivant de son côté : la
            # différence est déjà portée par le compte `extra`, pas deux fois par les dates.
            if kind == "extra_fx":
                prev_f = a; i += 1; continue
            if kind == "extra_moira":
                prev_m = b; j += 1; continue
        if n == 0 or n < skip:
            prev_f, prev_m = a, b
            i += 1; j += 1; n += 1
            continue
        da, db = a["t"] - prev_f["t"], b["t"] - prev_m["t"]
        if a["fc"] == 7 and b["fc"] == 7:
            res["iack_skew"].append(db - da)
            i += 1; j += 1; n += 1
            continue                       # prev_* restent l'accès AVANT l'IACK
        if da != db:
            res["mismatches"] += 1
            if len(res["samples"]) < 6:
                res["samples"].append({"index": i, "after": fmt(prev_f), "access": fmt(a),
                                       "fx": da, "moira": db})
            if res["first"] is None:
                res["first"] = {"index": i, "kind": "time", "after": fmt(prev_f),
                                "access": fmt(a), "delta": [da, db]}
        prev_f, prev_m = a, b
        i += 1; j += 1; n += 1
    if len(fx) != len(mo):
        res["first"] = res["first"] or {"index": min(len(fx), len(mo)), "kind": "length"}
    elif efx["reason"] != emo["reason"]:
        res["first"] = res["first"] or {"index": len(fx), "kind": "end",
                                        "fx": efx["reason"], "moira": emo["reason"]}
    res["status"] = "agree" if res["first"] is None else "diverge"
    return res


def variants(name: str, opts: dict):
    irq = opts.get("irq", [])
    nb, step = opts.get("sweep", (1, 0))
    for k in range(nb):
        label = f"{name}+{k * step}" if nb > 1 else name
        yield label, [(t + k * step, lvl) for t, lvl in irq]


def run_one(binary: Path, image: Path, trace: Path, irq: list, cwd: Path, extra: tuple = ()) -> None:
    cmd = [str(binary), str(image), str(trace), *extra]
    for t, lvl in irq:
        cmd += ["--irq", f"{t}:{lvl}"]
    subprocess.run(cmd, check=True, cwd=cwd, timeout=300, capture_output=True, text=True)


def signature(r: dict) -> dict:
    sig = {"status": r["status"], "first": r["first"], "mismatches": r.get("mismatches", 0),
           "swaps": r.get("swaps", 0), "access_mismatches": r.get("access_mismatches", 0),
           "extra": r.get("extra"), "total": r.get("total")}
    if "iplfetch" in r:
        sig["iplfetch"] = r["iplfetch"]
    return sig


def main() -> int:
    ap = argparse.ArgumentParser(description="Banc différentiel Moira↔fx68k")
    ap.add_argument("--rebuild", action="store_true", help="rebâtir le modèle Verilator")
    ap.add_argument("--write-refs", action="store_true", help="poser tests/reference/fx68k_bench.json")
    ap.add_argument("--only", help="programmes à exécuter, séparés par des virgules")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not shutil.which("verilator"):
        return skip("verilator absent du PATH")
    if not all((FX / s).exists() for s in SOURCES + ROMS):
        return skip("extern/fx68k absent ou incomplet — tools/setup_silicon_refs.sh")
    if not MOIRA.exists():
        return skip(f"{MOIRA.relative_to(ROOT)} absent — cmake --build build")

    try:
        if args.rebuild or need_rebuild():
            build_fx68k()
    except subprocess.CalledProcessError as e:
        print(e.stderr[-4000:] if e.stderr else "", file=sys.stderr)
        print("Verilator a échoué sur fx68k", file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)
    progs = BUILD / "programs"
    progs.mkdir(parents=True, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None
    results: dict[str, dict] = {}
    for name, _gen, opts in PROGRAMS:
        if only and name not in only:
            continue
        image = progs / f"{name}.bin"
        image.write_bytes(build_program(name))
        for label, irq in variants(name, opts):
            tf, tm = OUT / f"{label}.fx.txt", OUT / f"{label}.moira.txt"
            run_one(FXBIN, image, tf, irq, BUILD)
            run_one(MOIRA, image, tm, irq, ROOT)
            fx, efx = parse(tf)
            mo, emo = parse(tm)
            skip = 0 if opts.get("reset") else RESET_ACCESSES
            r = compare(fx, mo, efx, emo, skip)
            r["irq"] = irq
            if irq:
                # Même programme, Moira avec le port de WinUAE ipl_fetch_next (NEOST_IPLFETCH) :
                # l'option, désactivée dans NeoST, est-elle plus proche du RTL ?
                ti = OUT / f"{label}.moira-iplfetch.txt"
                run_one(MOIRA, image, ti, irq, ROOT, ("--ipl-fetch",))
                mi, emi = parse(ti)
                ri = compare(fx, mi, efx, emi, skip)
                r["iplfetch"] = {"status": ri["status"], "mismatches": ri["mismatches"],
                                 "first": ri["first"], "total": ri.get("total")}
            results[label] = r
            tot = r.get("total", ["?", "?"])
            skew = f" iack{r['iack_skew']}" if r["iack_skew"] else ""
            line = f"  {label:18} accès {r['accesses'][0]:4}/{r['accesses'][1]:<4} cycles {tot[0]}/{tot[1]}  {r['status']}{skew}"
            if "iplfetch" in r:
                line += f" | iplfetch {r['iplfetch']['status']} ({r['iplfetch']['mismatches']})"
            if r["first"]:
                f = r["first"]
                counts = (f"{r['mismatches']} intervalle(s), {r['swaps']} swap(s), "
                          f"{r['extra'][0]}+{r['extra'][1]} accès en plus fx/moira, "
                          f"{r['access_mismatches']} étranger(s)")
                line += f"  @{f['index']} {f['kind']} [{counts}]"
                if f["kind"] == "time":
                    line += f" {f['after']} → {f['access']} : {f['delta'][0]} vs {f['delta'][1]} cyc"
                elif f["kind"] in ("access", "swap", "extra_fx", "extra_moira"):
                    line += f" fx[{f['fx']}] moira[{f['moira']}]"
                elif f["kind"] == "end":
                    line += f" fx={f['fx']} moira={f['moira']}"
            print(line)

    (OUT / "summary.json").write_text(json.dumps(results, indent=1))
    n_agree = sum(r["status"] == "agree" for r in results.values())
    print(f"\nBanc fx68k : {n_agree}/{len(results)} variantes en accord cycle à cycle "
          f"(échelon rtl, grade D — localise, ne tranche pas).")

    if args.write_refs:
        REF.parent.mkdir(parents=True, exist_ok=True)
        REF.write_text(json.dumps({k: signature(v) for k, v in results.items()}, indent=1,
                                  ensure_ascii=False) + "\n")
        print(f"référence posée : {REF.relative_to(ROOT)}")
        return 0
    if REF.exists():
        ref = json.loads(REF.read_text())
        changed = [k for k, v in results.items() if ref.get(k) != signature(v)]
        missing = [k for k in results if k not in ref]
        if changed:
            print("VERDICT CHANGÉ par rapport à la référence :", ", ".join(changed))
            print("  → juger l'écart (docs/REFERENCE_LADDER.md § arbitrage), puis --write-refs.")
            return 1
        print("verdicts identiques à la référence" + (f" ({len(missing)} variante(s) non consignée(s))" if missing else ""))
        return 0
    return 0 if n_agree == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
