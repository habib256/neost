#!/usr/bin/env python3
# =============================================================================
#  check_frame_grid.py — Le début de trame reste sur la grille de 4 cycles du bus
#  RAM après un reset à chaud.
#
#  Le CPU s'aligne sur une grille ABSOLUE de 4 cycles (Cpu68k::alignRamBus) et la
#  vidéo se date depuis Machine::frameStart_ : le modèle suppose frameStart_ ≡ 0
#  (mod 4). Le démarrage à froid le garantit (horloge à 0) ; un reset à chaud — le
#  bouton Reset, ou l'Apply d'une config dans le GUI — ré-ancre la trame sur l'heure
#  du moment, qui dépasse la fin de trame de la durée d'une instruction. Avant le
#  correctif du 2026-11-13 (Machine::beginFrame_), une heure à 2 (mod 4) décalait
#  toute la vidéo de 2 cycles : le diaporama Spectrum 512 sortait brouillé après
#  une bascule ST → STE dans le GUI.
#
#  Contrôle : plusieurs resets à chaud à des trames successives (l'heure d'un reset
#  varie d'une trame à l'autre), NEOST_FRAME_DIAG=1, et toutes les trames doivent
#  commencer à 0 (mod 4). EmuTOS seul : aucune ROM Atari requise.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / ("neost-headless.exe" if os.name == "nt" else "neost-headless")
ROM = ROOT / "roms" / "etos192us.img"
DISK = ROOT / "disks" / "diskA.st"
FRM = re.compile(r"\[FRM\] start=(-?\d+) mod4=(\d+)")


def main() -> int:
    if not HEADLESS.exists():
        print("SKIP : neost-headless non bâti", file=sys.stderr)
        return 0
    if not ROM.exists():
        print(f"SKIP : {ROM.name} absent", file=sys.stderr)
        return 0
    env = dict(os.environ, NEOST_FRAME_DIAG="1")
    bad, frames, resets = [], 0, 0
    for at in range(40, 52):   # douze resets : l'heure de fin de trame varie d'une trame à l'autre
        cmd = [str(HEADLESS), str(ROM), "--machine", "st", "--disk", str(DISK), "--disk-ro",
               "--frames", str(at + 6), "--reset-at", str(at)]
        p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, errors="replace")
        if p.returncode != 0:
            print(f"ÉCHEC : neost-headless a rendu {p.returncode} (reset à {at})", file=sys.stderr)
            return 1
        resets += "warm reset at frame" in p.stderr
        for m in FRM.finditer(p.stderr):
            frames += 1
            if int(m.group(2)) != 0:
                bad.append((at, int(m.group(1)), int(m.group(2))))
    if resets != 12:
        print(f"ÉCHEC : {resets} reset(s) observé(s) sur 12", file=sys.stderr)
        return 1
    if bad:
        for at, start, mod in bad[:5]:
            print(f"ÉCHEC : reset à la trame {at} → trame à {start} (≡ {mod} mod 4)")
        return 1
    print(f"OK : {frames} trames, 12 resets à chaud, toutes sur la grille de 4 cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
