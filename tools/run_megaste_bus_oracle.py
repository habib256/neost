#!/usr/bin/env python3
"""Sondes CPU/cache MegaSTE 16 MHz, sans DMA ni IRQ, contre Hatari épinglé.

Hatari date en cycles CPU 16 MHz, NeoST en cycles bus 8 MHz : comparer les
intervalles après division exacte par deux, sans mélanger les dates de bascule.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import run_timing_oracle as timing
from timing_extended import Assembler

CASES = tuple(f"{size}_{mode}" for size in ("byte", "word", "long")
              for mode in ("off", "fill", "hit")) + ("write_word", "write_long")
OUT = timing.ROOT / "tests/out/megaste_bus"
REFERENCE = timing.ROOT / "tests/reference/megaste_bus_hatari.json"
PROTOCOL = {"schema": 1, "frames": 4, "warmup": 8, "samples": 64,
            "hatari_cpu_cycles_per_bus": 2}


def build(case):
    a = Assembler()
    a.w(0x46fc, 0x2700, 0x2e7c); a.long(0x10000)
    a.w(0x207c); a.long(0x18000)
    a.w(0x20bc); a.long(0x12345678)  # MOVE.L #motif,(A0)
    a.byte(0x8e21, 2)              # 16 MHz, cache off ; SCU reste masqué
    a.w(0x3e3c, PROTOCOL["warmup"] + PROTOCOL["samples"] - 1)
    a.label("loop")
    a.byte(0x8e21, 2)              # invalider le cache à chaque échantillon
    if not case.endswith("off"):
        a.byte(0x8e21, 3)
    if case.endswith("hit"):
        a.w(0x2210)                # MOVE.L (A0),D1 : les deux mots en cache
    if case.startswith("write"):
        a.w(0x203c); a.long(0x12345678)
    else:
        a.w(0x7000)
    a.label("begin")
    a.w({"byte": 0x1010, "word": 0x3010, "long": 0x2010,
         "write_word": 0x3080, "write_long": 0x2080}[
             case if case.startswith("write") else case.split("_")[0]])
    a.label("end"); a.w(0x4e71)
    if case.startswith("write"):
        a.w(0x2210 if case == "write_long" else 0x7200)
        if case == "write_word":
            a.w(0x3210)
        a.w(0x0c81); a.long(0x12345678 if case == "write_long" else 0x5678)
    else:
        a.w(0x0c80)
        a.long(0x12 if case.startswith("byte") else 0x1234 if case.startswith("word") else 0x12345678)
    a.branch(0x6600, "failure")
    a.branch(0x51cf, "loop")
    a.label("success"); a.w(0x4e72, 0x2700); a.branch(0x6000, "success")
    a.label("failure"); a.w(0x4e72, 0x2700); a.branch(0x6000, "failure")
    return a.finish()


def capture(case, emulator, attempt=1):
    data, pcs = build(case)
    cart = OUT / f"{case}.bin"
    cart.write_bytes(data)
    trace = OUT / f"{case}.{emulator}-{attempt}.trace"
    log = OUT / f"{case}.{emulator}-{attempt}.log"
    env = {**os.environ, "NEOST_TRACE_CYC": "1", "HATARI_SEED": "1"}
    if emulator == "neost":
        command = [timing.NEOST, timing.ROM, "--machine", "megaste", "--mem", "1m",
                   "--cart", cart, "--frames", str(PROTOCOL["frames"]), "--trace", trace, "--irq"]
        timing.run(command, log, env)
    else:
        with tempfile.TemporaryDirectory(prefix="neost-megaste-bus-") as directory:
            config = Path(directory) / "empty.cfg"
            config.write_text("")
            env.update(HOME=directory, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            command = [timing.HATARI, "--configfile", config, "--tos", timing.ROM,
                       "--machine", "megaste", "--cpuclock", "8", "--memsize", "1",
                       "--cartridge", cart, "--cpu-exact", "on", "--compatible", "on",
                       "--sound", "off", "--timer-d", "off", "--fast-forward", "on",
                       "--confirm-quit", "off", "--alert-level", "fatal", "--run-vbls", str(PROTOCOL["frames"]),
                       "--trace", "cpu_disasm,cpu_video_cycles", "--trace-file", trace]
            timing.run(command, log, env)
    instructions, _ = timing.td.parse(str(trace))
    times = lambda name: [i.cyc for i in instructions if i.pc == pcs[name]]
    begin, end = times("begin"), times("end")
    count = PROTOCOL["warmup"] + PROTOCOL["samples"]
    if (times("failure") or not times("success") or len(begin) != count or len(end) != count
            or any(t is None for t in begin + end)):
        raise ValueError(f"{case} : exécution incomplète ou données incorrectes ({trace})")
    intervals = [y-x for x, y in zip(begin, end)]
    if any(x <= 0 for x in intervals) or any(y <= x for x, y in zip(begin, begin[1:])):
        raise ValueError(f"{case} : horloge non monotone")
    if emulator == "hatari":
        if any(x % 2 for x in intervals):
            raise ValueError(f"{case} : intervalle CPU 16 MHz non pair")
        intervals = [x // 2 for x in intervals]
    return {"probe_sha256": hashlib.sha256(data).hexdigest(), "bus_cycles": intervals[PROTOCOL["warmup"]:]}


def compare(case, actual, expected):
    if actual["probe_sha256"] != expected["probe_sha256"]:
        raise ValueError(f"{case} : programme différent de la référence")
    if len(actual["bus_cycles"]) != 64 or len(expected["bus_cycles"]) != 64:
        raise ValueError(f"{case} : 64 observations requises")
    ok = actual == expected
    print(f"  {'OK' if ok else 'FAIL'} {case}: bus NeoST={sorted(set(actual['bus_cycles']))}, "
          f"Hatari={sorted(set(expected['bus_cycles']))}, égalité exacte")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--oracle-check", action="store_true")
    mode.add_argument("--record-reference", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    live = args.oracle_check or args.record_reference
    identity = timing.identity() if live else None
    if args.record_reference:
        reference = {**PROTOCOL, "oracle": identity, "rom_sha256": timing.sha(timing.ROM),
                     "cases": {}}
    else:
        reference = json.loads(REFERENCE.read_text())
        if (any(reference.get(k) != v for k, v in PROTOCOL.items())
                or set(reference["cases"]) != set(CASES)
                or reference["rom_sha256"] != timing.sha(timing.ROM)
                or (live and reference["oracle"] != identity)):
            raise ValueError("Protocole, ROM, sources Hatari ou sondes incompatibles")
    ok = True
    for case in CASES:
        if live:
            first, second = capture(case, "hatari", 1), capture(case, "hatari", 2)
            if first != second:
                raise ValueError(f"{case} : Hatari non reproductible")
            if args.record_reference:
                reference["cases"][case] = first
            elif first != reference["cases"][case]:
                raise ValueError(f"{case} : référence Hatari non reproduite")
        ok = compare(case, capture(case, "neost"), reference["cases"][case]) and ok
    if args.record_reference and ok:
        REFERENCE.write_text(json.dumps(reference, indent=2) + "\n")
    print("MEGASTE BUS ORACLE OK" if ok else "MEGASTE BUS ORACLE FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"MEGASTE BUS ERROR: {error}", file=sys.stderr)
        sys.exit(2)
