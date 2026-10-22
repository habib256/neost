#!/usr/bin/env python3
"""Blitter/IRQ MegaSTE : données, répétabilité et conservation des cycles bus.

Le bus et le blitter restent à 8 MHz quand le CPU passe à 16 MHz. Une copie
HOP=2/LOP=3 exige une lecture et une écriture de quatre cycles bus par mot.
--oracle-audit confronte aussi Hatari à cette borne ; un échec reste un échec,
jamais une référence de timing ni une exception du comparateur strict.
"""
import argparse
import json
import math
import re
import struct
import subprocess
import sys

import run_timing_oracle as timing
from timing_extended import Assembler

OUT = timing.ROOT / "tests/out/megaste_blitter"
CASES = {f"blitter_mste_{mode}_{speed}_{place}": {
    "hog": mode == "hog", "mhz": 8 if speed == "8" else 16,
    "cache": speed == "16cache", "ram": place == "ram", "words": 128}
    for mode in ("hog", "shared") for speed in ("8", "16", "16cache")
    for place in ("rom", "ram")}


def build(case):
    cfg = CASES[case]
    a = Assembler(0x10000 if cfg["ram"] else 0xfa0000)
    a.w(0x46fc, 0x2700, 0x2e7c); a.long(0x10000)
    a.byte(0x820a, 2)
    for address in (0xfa07, 0xfa09, 0xfa13, 0xfa15, 0xfa19, 0xfa1b,
                    0xfa1d, 0xfa03, 0xfa05):
        a.byte(address, 0)
    a.byte(0xfa17, 0x40)
    a.byte(0x8e0d, 0x40)  # SCU : seul MFP6 autorisé
    a.byte(0x8e21, 3 if cfg["cache"] else 2 if cfg["mhz"] == 16 else 0)
    a.vector(0x10c, "irq")
    a.byte(0xfa09, 8); a.byte(0xfa15, 8)
    for address in (0x8a20, 0x8a22, 0x8a2e, 0x8a30):
        a.word(address, 2)
    for address in (0x8a28, 0x8a2a, 0x8a2c):
        a.word(address, 0xffff)
    a.byte(0x8a3a, 2); a.byte(0x8a3b, 3); a.byte(0x8a3d, 0)
    a.w(0x207c); a.long(0x18000); a.w(0x303c, cfg["words"]-1)
    a.label("fill"); a.w(0x30fc, 0x5aa5); a.branch(0x51c8, "fill")
    a.w(0x3e3c, 71)
    a.label("loop")
    a.w(0x207c); a.long(0x19000); a.w(0x303c, cfg["words"]-1)
    a.label("clear"); a.w(0x4258); a.branch(0x51c8, "clear")
    a.address(0x8a24, 0x18000); a.address(0x8a32, 0x19000)
    a.word(0x8a36, 16); a.word(0x8a38, cfg["words"] // 16)
    a.w(0x7c00)  # D6=0 ; IRQ encore masquée jusqu'au lancement
    a.label("arm"); a.byte(0x8a3c, 0xc0 if cfg["hog"] else 0x80)
    a.w(0x46fc, 0x2500)  # ouvrir la fenêtre d'IRQ APRÈS le lancement
    a.label("poll"); a.w(0x0838, 7, 0x8a3c); a.branch(0x6600, "poll")
    a.w(0x4a86); a.branch(0x6700, "poll")  # attendre aussi le handler réel
    a.label("handler"); a.w(0x4e71)
    a.w(0x0c86); a.long(1); a.branch(0x6600, "failure")  # exactement une IRQ
    a.w(0x207c); a.long(0x19000); a.w(0x303c, cfg["words"]-1)
    a.label("verify"); a.w(0x0c58, 0x5aa5); a.branch(0x6600, "failure")
    a.branch(0x51c8, "verify"); a.branch(0x51cf, "loop")
    a.label("success"); a.w(0x4e72, 0x2700); a.branch(0x6000, "success")
    a.label("failure"); a.w(0x4e72, 0x2700); a.branch(0x6000, "failure")
    a.label("irq")
    # GPU_DONE ne doit pas interrompre le CPU pendant une copie active.
    # D6=1 seul accepterait une IRQ parasite ou celle de la copie précédente.
    a.w(0x0838, 7, 0x8a3c); a.branch(0x6600, "irq_while_busy")
    # Une seule fenêtre d'IRQ par copie : le SR empilé est remis à IPL7.
    # Aucun acquittement hôte ni filtrage des traces ; la copie suivante
    # réautorise IPL5 explicitement, après vérification des données.
    a.w(0x5286, 0x0057, 0x0700, 0x4e73)
    a.label("irq_while_busy")
    a.w(0x4e72, 0x2700); a.branch(0x6000, "irq_while_busy")
    data, pcs = a.finish()
    if cfg["ram"]:
        stub = struct.pack(">I14H", 0xfa52235f, 0x207c, 0x00fa, 0x0020,
                           0x227c, 0x0001, 0, 0x303c, len(data)-1,
                           0x12d8, 0x51c8, 0xfffc, 0x4ef9, 0x0001, 0x0004)
        data = stub + data
    return data, pcs


def minimum_bus_cycles(cfg):
    # Borne inférieure : les 63 accès et les stalls ne peuvent qu'ajouter du
    # temps. 8 cycles bus par mot + arbitration 8/4 par tranche ; les fenêtres
    # intermédiaires CPU demandent chacune 64 accès de quatre cycles CPU.
    slices = 1 if cfg["hog"] else math.ceil(2 * cfg["words"] / 64)
    cpu_bus_access = 4 if cfg["mhz"] == 8 else 2
    return 8 * cfg["words"] + 12 * slices + (slices-1) * 64 * cpu_bus_access


def validate(case, measurement, emulator):
    cfg = CASES[case]
    factor = 2 if emulator == "hatari" and cfg["mhz"] == 16 else 1
    bound = minimum_bus_cycles(cfg)
    values = {field: [v / factor for v in measurement[field]]
              for field in ("durations", "irq_durations")}
    if any(len(seq) != 64 for seq in values.values()):
        raise ValueError(f"{case} : 64 observations requises")
    ok = all(min(seq) >= bound for seq in values.values())
    print(f"  {'OK' if ok else 'FAIL'} {emulator} {case}: "
          f"fin min={min(values['durations'])}, IRQ min={min(values['irq_durations'])}, "
          f"borne physique={bound} cycles bus")
    return ok


def read_iack_cycles(log, cfg):
    # Le bloc périphérique (12 cycles bus depuis le 2026-10-22, cf. g_iackMfp — 16
    # auparavant compensait le défaut STOP+4 de Moira) reste en cycles bus, mais les
    # quatre cycles internes après IACK appartiennent au CPU. À 16 MHz : 12*2 + 4.
    cycles = [int(n) for n in re.findall(r"\[EXC\] lvl=6 .*? iack=(\d+) ", log.read_text())]
    expected = 12 * (cfg["mhz"] // 8) + 4
    if len(cycles) != 72 or any(n != expected for n in cycles):
        raise ValueError(f"{log} : attendu 72 IACK de {expected} cycles CPU, "
                         f"reçu {len(cycles)} ({sorted(set(cycles))})")
    return cycles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-audit", action="store_true",
                        help="auditer aussi Hatari ; résultat non utilisé comme oracle temporel")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    report = {"schema": 1, "protocol": "megaste_blitter_irq_clocks_v3",
              "policy": "physical_bus_lower_bound", "cases": {}}
    if args.oracle_audit:
        report["hatari"] = timing.identity()
        report["clock_source_sha256"] = timing.sha(timing.ROOT / "extern/hatari/src/clocks_timings.c")
    ok = True
    for case in CASES:
        report["cases"][case] = {"config": CASES[case], "minimum_bus_cycles": minimum_bus_cycles(CASES[case])}
        for emulator in (("neost", "hatari") if args.oracle_audit else ("neost",)):
            try:
                captures = []
                for attempt in (1, 2):
                    measured = timing.capture(case, emulator, attempt, probe=build,
                                              machine="megaste", out=OUT, frames=10,
                                              extra_env={"NEOST_EXC_DIAG": "1"})
                    if emulator == "neost":
                        measured["iack_cpu_cycles"] = read_iack_cycles(
                            OUT / f"{case}.{emulator}-{attempt}.log", CASES[case])
                    captures.append(measured)
                first, second = captures
                if first != second:
                    raise ValueError(f"{emulator} {case} : non reproductible")
                passed = validate(case, first, emulator)
                divisor = 2 if emulator == "hatari" and CASES[case]["mhz"] == 16 else 1
                report["cases"][case][emulator] = {
                    "ok": passed, "conversion_to_bus_divisor": divisor, **first}
                ok = passed and ok
            except (ValueError, OSError, subprocess.SubprocessError) as error:
                print(f"  FAIL {emulator} {case}: {error}")
                report["cases"][case][emulator] = {"ok": False, "error": str(error)}
                ok = False
    (OUT / ("oracle_audit.json" if args.oracle_audit else "checks.json")).write_text(
        json.dumps(report, indent=2) + "\n")
    print("MEGASTE BLITTER CHECKS OK" if ok else "MEGASTE BLITTER CHECKS FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"MEGASTE BLITTER ERROR: {error}", file=sys.stderr)
        sys.exit(2)
