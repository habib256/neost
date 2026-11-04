#!/usr/bin/env python3
# =============================================================================
#  run_sting_boot.py — Le vrai pilote STinG ENEC.STX trouve-t-il la NE2000 de NeoST ?
#
#  POURQUOI. Les fenêtres ROM3/ROM4 de l'EtherNEC ont été inversées jusqu'au 2026-08-27 : les auto-tests
#  passaient, car ils partageaient les constantes du code, et seul le vrai pilote voyait le défaut. Échelon
#  `rom` : le pilote est le logiciel réel, ce qu'il attend est le contrat, pas le silicium.
#
#  COMMENT. Boote disks/sting_ethernec.st sous EmuTOS, sans aucun accès réseau, et lit sur le port série les
#  étapes de son programme de test. Il s'arrête à la résolution de nom, qui demanderait un réseau.
#
#  Usage : run_sting_boot.py     Code 77 sans neost-headless ou sans la ROM.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "neost-headless"
ROM = "roms/etos256us.img"
WANTED = ("STG: config EtherNet 10.0.2.15/24", "STG: port up", "STG: ACTIVATE=TRUE", "STG: resolving...")


def main() -> int:
    if not HEADLESS.exists() or not (ROOT / ROM).exists():
        print("SKIP — STinG : neost-headless ou ROM EmuTOS absents")
        return 77
    with tempfile.TemporaryDirectory() as tmp:
        dump = Path(tmp) / "serial.txt"
        subprocess.run([str(HEADLESS), ROM, "--machine", "ste", "--mem", "1m", "--ethernec", "--disk",
                        "disks/sting_ethernec.st", "--disk-ro", "--frames", "1500", "--serial-dump", str(dump)],
                       cwd=ROOT, capture_output=True, timeout=300, check=True)
        text = dump.read_text(errors="replace")
    missing = [w for w in WANTED if w not in text]
    if missing:
        print("ÉCHEC — étapes absentes : " + " ; ".join(missing))
        return 1
    print(f"STING OK : pilote chargé, port actif, {len(WANTED)} étapes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
