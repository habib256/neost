#!/usr/bin/env python3
"""Décompose le blitter MegaSTE face à un Hatari diagnostique instrumenté.

Aucune référence n'est promue. Le binaire Hatari et ses sources doivent être
fournis explicitement ; les empreintes et le diff sont conservés dans la sortie.
"""
import argparse
import difflib
import json
import os
from pathlib import Path
import re
import subprocess

import run_timing_oracle as timing
import run_megaste_blitter_checks as checks


def phases(trace, log, case, emulator):
    data, pcs = checks.build(case)
    # Le comparateur invité contrôle aussi données, ordre, IRQ et piège BUSY.
    timing.read_transfer_trace(trace, pcs, data, case)
    insns, _ = timing.td.parse(str(trace))
    factor = 2 if emulator == "hatari" and checks.CASES[case]["mhz"] == 16 else 1
    arms = [i.cyc / factor for i in insns if i.pc == pcs["arm"]]
    handlers = [i.cyc / factor for i in insns if i.pc == pcs["irq"]]
    polls_done = [i.cyc / factor for i in insns if i.pc == pcs["handler"]]
    body = log.read_text()
    def times(pattern):
        return [int(n) / factor for n in re.findall(pattern, body)]
    starts = times(r"\[BLSTART\] (\d+)")
    ends = times(r"\[BLEND\] (\d+)")
    done = times(r"\[BLDONE\] (\d+)")
    entry = times(r"\[BLIRQ\] entry level=6 clock=(\d+)")
    iack = times(r"\[BLIRQ\] iack level=6 clock=(\d+)" if emulator == "neost"
                 else r"AUDIACK t=(\d+)")
    if any(len(seq) != 72 for seq in (arms, handlers, polls_done, done, entry, iack)):
        raise ValueError(f"{case} {emulator}: attendu 72 marqueurs de chaque phase")
    if len(starts) != len(ends) or not starts:
        raise ValueError(f"{case} {emulator}: tranches incomplètes")
    samples = []
    cursor = 0
    for a, d, e, ack, h, p in zip(arms, done, entry, iack, handlers, polls_done):
        slices = []
        while cursor < len(starts) and starts[cursor] < d:
            s, end = starts[cursor], ends[cursor]
            if not a < s < end < d or (slices and s < slices[-1][1] + 4):
                raise ValueError(f"{case} {emulator}: ordre des tranches incohérent")
            slices.append((s, end))
            cursor += 1
        if not slices or not d <= e < ack < h <= p or d-slices[-1][1] != 4:
            raise ValueError(f"{case} {emulator}: ordre de fin/IRQ incohérent")
        row = {
            "arm_to_first_start": slices[0][0]-a,
            "dma_and_arbitration": sum(end-s+4 for s, end in slices),
            "cpu_windows": sum(slices[i][0]-slices[i-1][1]-4 for i in range(1, len(slices))),
            "done_to_exception": e-d,
            "exception_to_iack": ack-e,
            "iack_to_handler": h-ack,
            "handler_to_poll_done": p-h,
            "slice_count": len(slices),
            "irq_total": h-a,
            "poll_total": p-a,
        }
        if sum(row[k] for k in ("arm_to_first_start", "dma_and_arbitration", "cpu_windows",
                                "done_to_exception", "exception_to_iack", "iack_to_handler")) != h-a:
            raise ValueError("Décomposition non conservative")
        if len(samples) >= 8 and row["dma_and_arbitration"] != 8*checks.CASES[case]["words"] + 12*len(slices):
            raise ValueError(f"{case} {emulator}: coût DMA/arbitrage incohérent")
        samples.append(row)
    if cursor != len(starts):
        raise ValueError("Tranches sans copie associée")
    return samples[8:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neost", type=Path, default=timing.NEOST)
    parser.add_argument("--neost-moira-source", type=Path, default=timing.ROOT / "build/generated/moira/Moira")
    parser.add_argument("--hatari", required=True, type=Path)
    parser.add_argument("--hatari-source", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--case", action="append", choices=checks.CASES)
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    timing.HATARI = args.hatari.resolve()
    timing.NEOST = args.neost.resolve()
    source = args.hatari_source.resolve()
    files = (*timing.SOURCES, "src/clocks_timings.c")
    report = {"policy": "diagnostic_only_no_reference", "schema": 1,
              "probe_protocol": "megaste_blitter_irq_clocks_v3", "clock_unit": "8MHz_bus_cycles",
              "capture_settings": {"machine": "megaste", "frames": 10, "warmup": 8,
                                   "samples": 64, "seed": 1, "attempts": 2},
              "inherited_clock_overrides": {k: os.environ.get(k) for k in
                                            ("NEOST_IACK", "NEOST_IACK_AT", "NEOST_IACK_MFP",
                                             "NEOST_IACK_VIDEO", "NEOST_RAM_SLOT", "NEOST_RAM_SLOT_PHASE")},
              "neost_binary_sha256": timing.sha(timing.NEOST),
              "declared_neost_moira_source": str(args.neost_moira_source.resolve()),
              "neost_moira_source_sha256": {f: timing.sha(args.neost_moira_source / f) for f in
                                             ("MoiraExec_cpp.h", "MoiraDataflow_cpp.h", "Moira.h")},
              "hatari_binary_sha256": timing.sha(timing.HATARI),
              "declared_hatari_source": str(source),
              "canonical_hatari": timing.identity(),
              "hatari_source_sha256": {f: timing.sha(source / f) for f in files},
              "neost_source_sha256": {f: timing.sha(timing.ROOT / f) for f in
                                      ("src/core/Cpu68k.cpp", "src/core/Blitter.cpp")}, "cases": {}}
    diff = "".join("".join(difflib.unified_diff(
        (timing.ROOT / "extern/hatari" / f).read_text().splitlines(True),
        (source / f).read_text().splitlines(True), fromfile="canonical/"+f, tofile="diagnostic/"+f))
        for f in files)
    (output / "hatari-source.patch").write_text(diff)
    report["hatari_diff_sha256"] = timing.sha(output / "hatari-source.patch")
    for case in args.case or checks.CASES:
        row = {}
        for emulator in ("neost", "hatari"):
            captures = []
            for attempt in (1, 2):
                timing.capture(case, emulator, attempt, probe=checks.build, machine="megaste",
                               out=output, frames=10, extra_env={"NEOST_BLIT_START_DIAG": "1", "NEOST_EXC_DIAG": "1"})
                captures.append(phases(output / f"{case}.{emulator}-{attempt}.trace",
                                       output / f"{case}.{emulator}-{attempt}.log", case, emulator))
            if captures[0] != captures[1]:
                raise ValueError(f"{case} {emulator}: phases non reproductibles")
            row[emulator] = captures[0]
        row["probe_sha256"] = timing.sha(output / (case+".bin"))
        report["cases"][case] = row
        delta = {k: sorted({n[k]-h[k] for n, h in zip(row["neost"], row["hatari"])}) for k in row["neost"][0]}
        print(case, json.dumps(delta))
    (output / "phases.json").write_text(json.dumps(report, indent=2)+"\n")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
