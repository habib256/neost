#!/usr/bin/env python3
"""Compare le décimal NeoST au source C de l'oracle Hatari épinglé.

Aucun téléchargement : tools/setup_hatari.sh installe la source nécessaire.
Les fonctions d'emballage sont extraites telles quelles de fpp_softfloat.c ;
les conversions compilent les vrais softfloat.c et softfloat_decimal.c.
"""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PIN = "f0736b24b32b0439300b52107ba6ab434469ec3c"


def function(source: str, name: str) -> str:
    start = source.index("static void " + name + "(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cases", type=int, default=10000, help="cas par sens/mode")
    ap.add_argument("--sanitize", action="store_true", help="active UBSan sur le port NeoST")
    args = ap.parse_args()
    hatari = ROOT / "extern/hatari"
    if not (hatari / "src/cpu/softfloat/softfloat_decimal.c").exists():
        print("SKIP recensé : oracle absent ; lancer tools/setup_hatari.sh")
        return 77
    revision = subprocess.check_output(
        ["git", "-C", str(hatari), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise SystemExit(f"Oracle non épinglé : {revision}, attendu {PIN}")
    source = (hatari / "src/cpu/fpp_softfloat.c").read_text()
    wrapper = """#include <stdint.h>
#include "softfloat/softfloat.h"
typedef uint16_t uae_u16;
typedef uint32_t uae_u32;
typedef uint64_t uae_u64;
typedef int32_t uae_s32;
typedef int64_t uae_s64;
typedef struct { floatx80 fpx; } fpdata;
static float_status fs;
#define fpp_to_exten to_exten
"""
    for name in ("to_exten", "fp_to_pack", "fp_from_pack"):
        wrapper += function(source, name) + "\n"
    wrapper += """
floatx80 oracle_from_packed(const uint32_t *words, float_status *status) {
    fpdata fp; uint32_t copy[3] = {words[0], words[1], words[2]};
    fs = *status; fp_to_pack(&fp, copy, 0); *status = fs; return fp.fpx;
}
void oracle_to_packed(floatx80 value, int k, uint32_t *words, float_status *status) {
    fpdata fp = {value}; fs = *status;
    fp_from_pack(&fp, words, k); *status = fs;
}
"""
    cc = shlex.split(os.environ.get("CC", "cc"))
    cxx = shlex.split(os.environ.get("CXX", "c++"))
    options = ["-O1" if args.sanitize else "-O2", "-I", str(hatari / "src/cpu")]
    instrumented = options + (["-g", "-fsanitize=undefined", "-fno-sanitize-recover=all"]
                              if args.sanitize else [])
    with tempfile.TemporaryDirectory(prefix="neost-decimal-oracle-") as tmp:
        work = Path(tmp)
        (work / "packed.c").write_text(wrapper)
        objects = []
        for src in (hatari / "src/cpu/softfloat/softfloat.c",
                    hatari / "src/cpu/softfloat/softfloat_decimal.c", work / "packed.c"):
            obj = work / (src.stem + ".o")
            subprocess.run(cc + options + ["-c", str(src), "-o", str(obj)], check=True)
            objects.append(str(obj))
        binary = work / "compare"
        # L'emballage C amont décale un int signé (f.high << 16), notamment pour
        # −inf ; instrumenter le port, sans modifier ni normaliser cet oracle.
        subprocess.run(cxx + instrumented + ["-std=c++17", "-I", str(ROOT / "src"),
                       str(ROOT / "tests/fpu_decimal_oracle.cpp"), *objects,
                       "-o", str(binary)], check=True)
        return subprocess.run([str(binary), str(args.cases)], cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
