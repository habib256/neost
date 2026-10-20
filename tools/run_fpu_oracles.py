#!/usr/bin/env python3
# =============================================================================
#  run_fpu_oracles.py — Lance les six oracles FPU (CIR NeoST contre le vrai
#  SoftFloat d'Hatari compilé) en une étape du palier `full`.
#
#  POURQUOI. Ces bancs prouvaient le bit-exact des ports FPU (FMOVE/FABS/FNEG,
#  FINT, arithmétique, décimal, FMOVECR, transcendantes FPSP) mais AUCUN palier
#  ni job de CI ne les lançait : un orphelin de la même famille que A19. Un
#  oracle qui ne tourne pas ne garde rien.
#
#  extern/hatari absent → étape SAUTÉE et recensée (code 77).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
EXIT_SKIPPED = 77
ORACLES = ("test_fpu_unary_oracle.py", "test_fpu_next_oracle.py",
           "test_fpu_arithmetic_oracle.py", "test_fpu_decimal_oracle.py",
           "check_fmovecr_oracle.py", "test_fpu_transcendental_oracle.py")


def main() -> int:
    if not (ROOT / "extern/hatari/src/cpu/softfloat/softfloat.c").exists():
        print("SAUTÉ : sources Hatari absentes (tools/setup_hatari.sh)")
        return EXIT_SKIPPED
    ok = True
    for name in ORACLES:
        proc = subprocess.run([sys.executable, str(TOOLS / name)], cwd=ROOT,
                              capture_output=True, text=True)
        lines = (proc.stdout + proc.stderr).strip().splitlines()
        verdict = lines[-1] if lines else "(aucune sortie)"
        print(f"  {'OK  ' if proc.returncode == 0 else 'FAIL'} {name} : {verdict}")
        ok = ok and proc.returncode == 0
    print("ORACLES FPU OK" if ok else "ORACLES FPU FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
