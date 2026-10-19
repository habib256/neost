#!/usr/bin/env python3
"""Contrôle le protocole de la sonde IACK ; aucune validation temporelle matérielle."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import make_mfp_iack_probe as probe

ROOT = Path(__file__).resolve().parents[1]


def check_output(memory, log, expected, supported):
    # La sentinelle de fin doit suivre TROIS compteurs d'IRQ réellement servis,
    # pas des constantes préparées dans le programme ou dans la disquette.
    signature = struct.pack(">HHHH", expected, expected, expected, 0xBEEF)
    copies = memory.count(signature)
    if copies != (1 if supported else 0):
        raise ValueError(f"completion records: expected {int(supported)}, got {copies}")
    skipped = memory.count(struct.pack(">HHHH", 0x4941, 0x434B, 0x5A1F, 0))
    if skipped != (0 if supported else 1):
        raise ValueError(f"skipped records: expected {int(not supported)}, got {skipped}")
    if "68000 halted" in log or "CPU halted" in log:
        raise ValueError("unexpected CPU halt")
    return {"complete_records": copies, "skipped_records": skipped,
            "hardware_timing_validated": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neost", type=Path, default=ROOT / "build/neost-headless")
    parser.add_argument("--out", type=Path, default=ROOT / "tests/out/mfp_iack_probe")
    parser.add_argument("--rom", type=Path, default=ROOT / "roms/etos256fr.img")
    args = parser.parse_args()
    output = args.out.resolve()
    manifest = probe.generate(output)
    report = {"policy": "protocol_only_no_hardware_validation", "checks": {},
              "probe_sha256": manifest["sha256"],
              "neost_sha256": hashlib.sha256(args.neost.read_bytes()).hexdigest(),
              "rom_sha256": hashlib.sha256(args.rom.read_bytes()).hexdigest()}
    # La cartouche teste le corps à adresse fixe ; AUTO teste les relocations
    # TOS et la détection de machine. ST doit revenir au bureau sans lancer le test.
    for name, machine, medium, supported in (
            ("cart_megaste", "megaste", "cart", True),
            ("auto_megaste", "megaste", "disk", True),
            ("auto_st", "st", "disk", False),
            ("auto_ste", "ste", "disk", False)):
        frames = 12 if medium == "cart" else 800
        dump = output / (name + ".ram")
        log_file = output / (name + ".log")
        media_file = "iack-control.bin" if medium == "cart" else "IACKTEST.ST"
        cmd = [str(args.neost.resolve()), str(args.rom.resolve()), "--machine", machine,
               "--mem", "1m", "--" + medium, str(output / media_file),
               "--frames", str(frames), "--dump-at", str(frames-1), "0", "1048576", str(dump)]
        with log_file.open("w") as stream:
            subprocess.run(cmd, cwd=ROOT, stdout=stream, stderr=stream, check=True, timeout=60)
        report["checks"][name] = check_output(dump.read_bytes(), log_file.read_text(),
                                              manifest["samples_per_phase"], supported)
        print(f"PASS {name}: protocol only, hardware timing unvalidated")
    (output / "checks.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
