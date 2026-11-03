#!/usr/bin/env python3
# Bâtit Vstcpu_bench (fx68k + GSTMCU). Importé par tools/run_stcpu_bench.py.
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build-rtl" / "stcpu"
BINARY = BUILD / "Vstcpu_bench"
G = ROOT / "extern" / "mistery" / "gstmcu"
FX = ROOT / "extern" / "fx68k"
HDL = ("gstmcu clockgen mcucontrol hsyncgen hdegen vsyncgen vdegen vidcnt sndcnt latch "
       "register modules gstshifter shifter_video shifter_video_async").split()


def build() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    cmd = ["verilator", "--cc", "--exe", "--build", "-j", "0", "--no-timing", "--no-assert-case",
           "-Wno-fatal", "-Wno-lint", "-Wno-style", "-CFLAGS", "-O2", "--Mdir", str(BUILD / "obj"),
           "--top-module", "stcpu_bench_top", "-o", str(BINARY),
           str(ROOT / "tools" / "stcpu_bench.cpp"), str(ROOT / "tools" / "stcpu_bench_top.v"), str(G / "tb" / "ste_tb.v"),
           *[str(G / "hdl" / f"{x}.v") for x in HDL],
           *[str(FX / f) for f in ("fx68k.sv", "fx68kAlu.sv", "uaddrPla.sv")], "-y", str(G / "tb")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stderr[-2000:])
    for rom in ("microrom.mem", "nanorom.mem"):
        shutil.copy(FX / rom, BUILD / rom)


if __name__ == "__main__":
    build()
