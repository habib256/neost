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
#  COMMENT. Trois suites, NeoST contre NeoST, sans Hatari ni Pillow :
#    etalons    — les étalons pixel de tools/etalons.json, commande de run_etalons.py ;
#    replays    — les parcours de jeu de tests/replays/replays.json, même partie que
#                 run_game_replay.py (entrées datées, disquettes changées), diagnostics
#                 compris : ils exercent la prise d'IRQ que le cœur change ;
#    collection — chaque image de disks/st et disks/stx, en ST/TOS 1.04, en
#                 STE/TOS 1.62 et en Mega STE/TOS 2.06, ESPACE frappé toutes les 500
#                 trames, jusqu'à 4000 ;
#    megaste    — le Mega STE à 16 MHz, où l'horloge du cœur vaut deux fois celle du
#                 bus (cpuMul, cpuBias) : les sondes blitter/IRQ et bus/cache de
#                 run_megaste_blitter_checks.py et run_megaste_bus_oracle.py (8 MHz,
#                 16 MHz, 16 MHz + cache), une empreinte à chaque trame, puis la suite
#                 Q du diagnostic Field Service si sa cartouche est là.
#  Les médias privés manquants (images, TOS Atari, cartouche) sont sautés et dits.
#    1. Sur le DERNIER commit avant l'échange :  cpu_swap_gate.py --record FICHIER
#    2. Sur le commit de l'échange :              cpu_swap_gate.py --check  FICHIER
#  La première trame qui diffère est rapportée, avec le hachage fautif : `cpu` seul →
#  le cœur ; `machine` seul → un accès bus ou une date d'accès ; les deux → à départager
#  par trace (`--trace FICHIER --trace-from N --regs --irq`, puis tools/trace_diff.py).
#  `--show ID` imprime la commande d'un scénario pour cette trace.
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
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_etalons as E  # noqa: E402
import run_game_replay as G  # noqa: E402
import title_cmd as T  # noqa: E402

LINE = re.compile(r"arch-hash frame=(\d+) machine=([0-9a-f]+) cpu=([0-9a-f]+)")
SUITES = ("etalons", "replays", "collection", "megaste")

# Collection : deux configurations PAL, celles de l'A/B du 2026-11-10 (CHANGELOG).
COLLECTION_DIRS = ("disks/st", "disks/stx")
COLLECTION_EXT = {".st", ".msa", ".stx", ".dim"}
COLLECTION_CFG = (("st", "roms/tos104uk.img"), ("ste", "roms/tos162uk.img"),
                  ("megaste", "roms/tos206uk.img"))
COLLECTION_FRAMES = 4000
COLLECTION_SPACE = range(1000, COLLECTION_FRAMES, 500)   # ESPACE : passe titres et menus


@dataclass
class Scenario:
    sid: str
    # Rend la commande (dans un dossier de travail jetable) ou une raison de sauter.
    build: Callable[[Path], list[str] | str]
    every: int | None = None      # pas propre au scénario (sondes courtes), sinon --every


def etalon_scenarios() -> list[Scenario]:
    out = []
    for e in E.load_manifest():
        if e.get("type", "").endswith("selftest") or "frames" not in e:
            continue

        def build(work: Path, e=e) -> list[str] | str:
            rom = e.get("rom", "roms/etos192us.img")
            if not (E.ROOT / rom).exists():
                return f"skip: ROM absente ({rom})"
            if e.get("disk") and not E.ensure_disk(e):
                return f"skip: disque absent ({e['disk']})"
            return E.headless_cmd(e)
        # Identifiant nu : compatible avec les références enregistrées avant les suites.
        out.append(Scenario(e["id"], build))
    return out


def replay_scenarios() -> list[Scenario]:
    out = []
    for r in json.loads((E.ROOT / "tests/replays/replays.json").read_text(encoding="utf-8"))["replays"]:
        def build(work: Path, r=r) -> list[str] | str:
            needed = [r["rom"], r["disk"], r.get("disk_b"), *(d for _, d in r.get("swaps", []))]
            missing = [p for p in needed if p and not (E.ROOT / p).exists()]
            if missing:
                return f"skip: média absent ({missing[0]})"
            if r.get("ref_kind") == "snapshot":
                return [str(E.HEADLESS), *r["args"]]
            # La même fabrique que compare_titles.py, côté NeoST : même partie, à l'octet.
            args = T.parse([str(work / "out"), r["disk"], *G.compare_argv(r, work)])
            return T.neost_cmd(Path(r["disk"]), args, work)
        out.append(Scenario("replay/" + r["id"], build))
    return out


def collection_scenarios() -> list[Scenario]:
    disks = sorted(p for d in COLLECTION_DIRS if (E.ROOT / d).is_dir()
                   for p in (E.ROOT / d).rglob("*") if p.is_file() and p.suffix.lower() in COLLECTION_EXT)
    out = []
    for machine, rom in COLLECTION_CFG:
        for disk in disks:
            def build(work: Path, disk=disk, machine=machine, rom=rom) -> list[str] | str:
                if not (E.ROOT / rom).exists():
                    return f"skip: ROM absente ({rom})"
                copy = work / ("c" + disk.suffix)    # le titre peut écrire sur sa disquette
                shutil.copy(disk, copy)
                cmd = [str(E.HEADLESS), str(E.ROOT / rom), "--machine", machine, "--mem", "1m",
                       "--disk", str(copy), "--frames", str(COLLECTION_FRAMES + 1)]
                for at in COLLECTION_SPACE:
                    cmd += ["--scancode-at", str(at), "39"]
                return cmd
            rel = disk.relative_to(E.ROOT).as_posix()
            out.append(Scenario(f"coll/{machine}/{rel}", build))
    return out


def megaste_scenarios() -> list[Scenario]:
    import run_megaste_blitter_checks as MB   # tardif : ces modules tirent run_timing_oracle
    import run_megaste_bus_oracle as MO
    rom = E.ROOT / "roms/etos256fr.img"       # celle de run_timing_oracle.py (ROM libre)
    out = []
    probes = [("blitter", c, MB.build, 30) for c in MB.CASES] + \
             [("bus", c, MO.build, 2 * MO.PROTOCOL["frames"]) for c in MO.CASES]
    for kind, case, make, frames in probes:
        def build(work: Path, case=case, make=make, frames=frames) -> list[str] | str:
            if not rom.exists():
                return f"skip: ROM absente ({rom.name})"
            cart = work / "probe.bin"
            cart.write_bytes(make(case)[0])
            return [str(E.HEADLESS), str(rom), "--machine", "megaste", "--mem", "1m",
                    "--cart", str(cart), "--frames", str(frames)]
        out.append(Scenario(f"mste/{kind}/{case}", build, every=1))

    def diag(work: Path) -> list[str] | str:
        # Recette de run_megaste_diag.py, copies sacrificielles : le test F formate A et B.
        rom2, cart = E.ROOT / "roms/tos206us.img", E.ROOT / "carts/MegaSTE_Diagnostic_v1.5.bin"
        for p in (rom2, cart):
            if not p.exists():
                return f"skip: média absent ({p.name})"
        disks = []
        for tag in "ab":
            disks.append(work / f"sac{tag}.st")
            shutil.copy(E.ROOT / "disks/diskA.st", disks[-1])
        return [str(E.HEADLESS), str(rom2), "--machine", "megaste", "--mem", "1m",
                "--cart", str(cart), "--disk", str(disks[0]), "--diskb", str(disks[1]),
                "--fastfdc", "--disk-ro", "--scancode-at", "320", "10,90,1c,9c",
                "--loopback-at", "330", "--dma-fixture", "--frames", "6600"]
    out.append(Scenario("mste/diag_q", diag))

    # Sondes du catalogue étendu (DMA disquette et blitter, tools/timing_extended.py), passées à
    # 16 MHz : seule la vitesse change, injectée en tête du programme. Plus le blit sur ses propres
    # registres. Empreintes NeoST contre NeoST, sans verdict extérieur.
    import timing_extended as TE
    speeds = {"8": 0, "16": 2, "16cache": 3}
    for case in ("fdc_read", "fdc_read_stx", "fdc_write", "blitter_hog", "blitter_shared", "blitter_hog_tas"):
        for speed in ("16", "16cache"):
            def build(work: Path, case=case, speed=speed) -> list[str] | str:
                if not rom.exists():
                    return f"skip: ROM absente ({rom.name})"
                cart = work / "probe.bin"
                cart.write_bytes(assemble_at_speed(TE, lambda: TE.build_extended(case), speeds[speed])[0])
                cmd = [str(E.HEADLESS), str(rom), "--machine", "megaste", "--mem", "1m",
                       "--cart", str(cart), "--frames", "30"]
                if case.startswith("fdc_"):
                    disk = work / ("d.stx" if case.endswith("_stx") else "d.st")
                    disk.write_bytes(TE.stx_image() if case.endswith("_stx") else TE.disk_image())
                    cmd += ["--disk", str(disk)]
                return cmd
            out.append(Scenario(f"mste/ext/{case}_{speed}", build, every=1))
    for mode in ("hog", "shared"):
        for speed in speeds:
            def build(work: Path, mode=mode, speed=speed) -> list[str] | str:
                if not rom.exists():
                    return f"skip: ROM absente ({rom.name})"
                cart = work / "probe.bin"
                cart.write_bytes(assemble_at_speed(TE, lambda: self_blit(TE, mode == "hog"), speeds[speed])[0])
                return [str(E.HEADLESS), str(rom), "--machine", "megaste", "--mem", "1m",
                        "--cart", str(cart), "--frames", "10"]
            out.append(Scenario(f"mste/selfblit_{mode}_{speed}", build, every=1))
    return out


_ASM_LOCK = threading.Lock()


def assemble_at_speed(TE, make, speed: int):
    """Assemble une sonde pour le Mega STE : `move.b #$40,$FF8E0D` (SCU : MFP autorisé, comme
    run_megaste_blitter_checks.py ; sans lui, une sonde qui attend l'IRQ du MFP par STOP ne
    se réveille jamais) puis `move.b #speed,$FF8E21`, en premières instructions. build_extended
    instancie timing_extended.Assembler par son nom : on le remplace le temps de l'appel, sous
    verrou (la porte assemble en parallèle)."""
    base_cls = TE.Assembler

    class AtSpeed(base_cls):
        def __init__(self, base=0xfa0000):
            super().__init__(base)
            self.byte(0x8e0d, 0x40)
            if speed:
                self.byte(0x8e21, speed)

    with _ASM_LOCK:
        TE.Assembler = AtSpeed
        try:
            return make()
        finally:
            TE.Assembler = base_cls


def self_blit(TE, hog: bool):
    """Le blitter écrit sa RAM de demi-teinte ($FF8A00), le CPU la relit, puis le blitter la
    relit comme source. HOP=2 (source seule), LOP=3 (copie), 16 mots."""
    a = TE.Assembler(0xfa0000)
    a.w(0x46fc, 0x2700, 0x2e7c); a.long(0x10000)
    a.w(0x207c); a.long(0x18000); a.w(0x303c, 15, 0x7201)       # motif en $18000 ; moveq #1,d1
    a.label("fill"); a.w(0x30c1, 0x5241); a.branch(0x51c8, "fill")  # move.w d1,(a0)+ ; addq.w #1,d1
    for address in (0x8a20, 0x8a22, 0x8a2e, 0x8a30):
        a.word(address, 2)
    for address in (0x8a28, 0x8a2a, 0x8a2c):
        a.word(address, 0xffff)
    a.byte(0x8a3a, 2); a.byte(0x8a3b, 3); a.byte(0x8a3d, 0)
    start = 0xc0 if hog else 0x80
    for n, (src, dst) in enumerate(((0x18000, 0xff8a00), (0xff8a00, 0x19100))):
        a.address(0x8a24, src); a.address(0x8a32, dst)
        a.word(0x8a36, 16); a.word(0x8a38, 1)
        a.byte(0x8a3c, start)
        a.label(f"poll{n}"); a.w(0x0838, 7, 0x8a3c); a.branch(0x6600, f"poll{n}")
    a.w(0x41f9); a.long(0xff8a00); a.w(0x43f9); a.long(0x19000); a.w(0x700f)  # lea ; lea ; moveq #15,d0
    a.label("copy"); a.w(0x32d8); a.branch(0x51c8, "copy")       # move.w (a0)+,(a1)+ ; dbra
    a.label("idle"); a.w(0x4e72, 0x2700); a.branch(0x6000, "idle")
    return a.finish()


def scenarios(suites: list[str], only: list[str] | None) -> list[Scenario]:
    make = {"etalons": etalon_scenarios, "replays": replay_scenarios,
            "collection": collection_scenarios, "megaste": megaste_scenarios}
    out = [s for name in suites for s in make[name]()]
    if only:
        out = [s for s in out if s.sid in only]
    return out


def suite_of(sid: str) -> str:
    for prefix, name in (("replay/", "replays"), ("coll/", "collection"), ("mste/", "megaste")):
        if sid.startswith(prefix):
            return name
    return "etalons"


def run(sc: Scenario, every: int) -> tuple[str, str, list[tuple[int, str, str]]]:
    """(id, statut, empreintes). Statut : 'ok', ou 'skip: …', ou 'error: …'."""
    with tempfile.TemporaryDirectory(prefix="gate_") as tmp:
        cmd = sc.build(Path(tmp))
        if isinstance(cmd, str):
            return sc.sid, cmd, []
        cmd += ["--arch-hash-every", str(sc.every or every)]
        r = E.run_timed(cmd, 1800, cwd=E.ROOT, capture_output=True, text=True, errors="replace")
    rows = [(int(f), m, c) for f, m, c in LINE.findall(r.stdout or "")]
    if r.returncode != 0 and not rows:
        return sc.sid, f"error: exit {r.returncode}", []
    return sc.sid, "ok", rows


def compare(want: list, rows: list) -> str | None:
    """None si accord ; sinon la description de la première divergence."""
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
        return f"première divergence trame {f} ({what}) ; dernière concordance trame {prev}"
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Porte d'échange du cœur 68000 (docs/CPU_SWAP_GATE.md)")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", metavar="FICHIER", help="enregistre les empreintes du cœur courant")
    mode.add_argument("--check", metavar="FICHIER", help="compare au fichier enregistré")
    mode.add_argument("--show", metavar="ID", help="imprime la commande neost-headless d'un scénario")
    mode.add_argument("--list", action="store_true", help="liste les scénarios")
    ap.add_argument("--suite", nargs="+", choices=SUITES,
                    help="suites à rejouer (défaut : toutes à --record, celles de la référence à --check)")
    ap.add_argument("--every", type=int, default=10, help="une empreinte toutes les N trames (10)")
    ap.add_argument("--only", nargs="*", help="identifiants de scénarios à rejouer")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    if not E.HEADLESS.exists():
        print("SKIP — neost-headless absent")
        return 77
    os.chdir(E.ROOT)          # les manifestes nomment leurs médias depuis la racine

    if a.list or a.show:
        todo = scenarios(a.suite or list(SUITES), None)
        if a.list:
            for s in todo:
                print(s.sid)
            return 0
        sc = next((s for s in todo if s.sid == a.show), None)
        if sc is None:
            print(f"scénario inconnu : {a.show}", file=sys.stderr)
            return 2
        work = Path(tempfile.mkdtemp(prefix="gate_show_"))   # gardé : la commande y pointe
        cmd = sc.build(work)
        print(cmd if isinstance(cmd, str) else " ".join(f"'{x}'" if " " in x else x for x in cmd))
        return 0

    ref = None
    suites = a.suite or list(SUITES)
    if a.check:
        ref = json.loads(Path(a.check).read_text(encoding="utf-8"))
        a.every = ref["every"]
        # Une référence d'avant les suites ne connaît que les étalons.
        suites = a.suite or ref.get("suites", ["etalons"])
    todo = scenarios(suites, a.only)
    t0 = time.monotonic()
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        results = list(pool.map(lambda s: run(s, a.every), todo))
    elapsed = time.monotonic() - t0

    if a.record:
        rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=E.ROOT,
                             capture_output=True, text=True).stdout.strip()
        data = {"every": a.every, "commit": rev, "suites": suites,
                "scenarios": {sid: rows for sid, st, rows in results if st == "ok"},
                "skipped": {sid: st for sid, st, _ in results if st != "ok"}}
        Path(a.record).write_text(json.dumps(data, indent=0) + "\n", encoding="utf-8")
        for name in suites:
            ids = [sid for sid in data["scenarios"] if suite_of(sid) == name]
            n = sum(len(data["scenarios"][sid]) for sid in ids)
            print(f"  {name:10} : {len(ids)} scénarios, {n} empreintes")
        for sid, st in data["skipped"].items():
            print(f"  {sid} : {st}")
        print(f"enregistré : {len(data['scenarios'])} scénarios, commit {rev}, {elapsed:.0f} s")
        return 1 if any(st.startswith("error") for st in data["skipped"].values()) else 0

    bad = 0
    per = {name: [0, 0] for name in suites}
    for sid, st, rows in results:
        want = ref["scenarios"].get(sid)
        if want is None:
            print(f"  --   {sid} : absent de la référence ({st})")
            continue
        per[suite_of(sid)][1] += 1
        if st != "ok":
            print(f"  ÉCHEC {sid} : {st}")
            bad += 1
            continue
        why = compare(want, rows)
        if why:
            print(f"  ÉCHEC {sid} : {why}")
            bad += 1
        else:
            per[suite_of(sid)][0] += 1
    for sid in ref["scenarios"]:
        if suite_of(sid) in suites and (not a.only or sid in a.only) and sid not in {s.sid for s in todo}:
            print(f"  ÉCHEC {sid} : scénario de la référence disparu (média retiré ?)")
            bad += 1
    print("  " + " · ".join(f"{k} {ok}/{n}" for k, (ok, n) in per.items()) + f" · {elapsed:.0f} s")
    print(f"PORTE CPU : {'ÉCHEC, ' + str(bad) + ' scénario(s)' if bad else 'accord'} "
          f"(référence du commit {ref.get('commit', '?')})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
