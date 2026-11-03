#!/usr/bin/env python3
# =============================================================================
#  run_gstmcu_bench.py — Simule le GSTMCU + Shifter STE de MiSTery sur un écran connu.
#
#  POURQUOI. Échelon `rtl` de docs/REFERENCE_LADDER.md : lire les dates des signaux et la
#  couleur réelle pendant BLANK, que le banc amont masque. Ce n'est pas une mesure de
#  silicium : le grade reste D, et ces chiffres ne valent que pour le modèle épinglé.
#
#  COMMENT. L'écran est pseudo-aléatoire : un seul alignement peut coïncider, et un
#  décalage ne passe pas inaperçu. Le pixel attendu est calculé ici depuis la VRAM et la
#  palette, indépendamment du modèle. Un seul pixel faux fait échouer le banc.
#
#  Usage : run_gstmcu_bench.py [--rebuild]
#  Sorties : tests/out/gstmcu_bench/summary.json. Code 77 si l'outillage est absent.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import json
import random
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HDL = ROOT / "extern" / "mistery" / "gstmcu" / "hdl"
TB = ROOT / "extern" / "mistery" / "gstmcu" / "tb" / "ste_tb.v"
BUILD = ROOT / "build-rtl" / "gstmcu"
OUT = ROOT / "tests" / "out" / "gstmcu_bench"
BINARY = BUILD / "Vste_tb"
EXIT_SKIPPED = 77
SOURCES = ("gstmcu clockgen mcucontrol hsyncgen hdegen vsyncgen vdegen vidcnt sndcnt latch "
           "register modules gstshifter shifter_video shifter_video_async").split()

# Couleur 0 volontairement non noire : c'est elle qui distingue « noir forcé » de
# « palette 0 » pendant BLANK. Quatre bits distincts par composante, y compris le bit
# de poids faible propre au STE.
PALETTE = [0x0A5, 0xF00, 0x0F0, 0x00F, 0xFF0, 0xF0F, 0x0FF, 0x123,
           0x456, 0x789, 0xABC, 0xDEF, 0x314, 0x159, 0x265, 0x358]


def physical(color: int) -> int:
    """Valeur qui sort des DAC : sur STE, le bit 3 de chaque composante est le bit de poids
    faible. Attendu calculé ici depuis cette règle ; gstshifter.v:241 @ 4a6db7cc fait de même."""
    return sum((((color >> s & 7) << 1) | (color >> (s + 3) & 1)) << s for s in (0, 4, 8))


# (nom, mde, ntsc, largeur, plans, ticks par pixel, lignes par trame attendues, via_ntsc)
CASES = [("low_pal", 0, 0, 320, 4, 4, 313, 1),
         ("low_ntsc", 0, 1, 320, 4, 4, 263, 0),
         ("med_pal", 1, 0, 640, 2, 2, 313, 0)]
# [F] Observation, sans verdict : la basse résolution PAL issue du reset ne s'aligne pas
# (cf. tools/gstmcu_bench.cpp). Affichée à chaque exécution pour qu'un changement se voie.
OBSERVED = ("low_pal_reset", 0, 0, 320, 4, 4, 313, 0)
HEIGHT = 200


def make_screen(width: int, planes: int, seed: int):
    rng = random.Random(seed)
    pixels = [[rng.randrange(1 << planes) for _ in range(width)] for _ in range(HEIGHT)]
    vram = bytearray()
    for row in pixels:
        for x0 in range(0, width, 16):
            for p in range(planes):
                word = 0
                for i in range(16):
                    word |= ((row[x0 + i] >> p) & 1) << (15 - i)
                vram += struct.pack(">H", word)
    assert len(vram) == 32000
    return pixels, bytes(vram)


def build() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    cmd = ["verilator", "--cc", "--exe", "--build", "-j", "0", "--public-flat-rw",
           "--no-timing", "-Wno-fatal", "-Wno-lint", "-Wno-style", "-CFLAGS", "-O2",
           "--Mdir", str(BUILD), "--top-module", "ste_tb",
           str(ROOT / "tools" / "gstmcu_bench.cpp"), str(TB),
           *[str(HDL / f"{s}.v") for s in SOURCES], "-y", str(TB.parent)]
    done = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if done.returncode:
        print(done.stdout[-1500:], done.stderr[-1500:], file=sys.stderr)
        raise SystemExit(2)


def runs(values: list[int]) -> list[tuple[int, int, int]]:
    out, start = [], 0
    for i in range(1, len(values) + 1):
        if i == len(values) or values[i] != values[start]:
            out.append((start, i - start, values[start]))
            start = i
    return out


def falling(line: list[int], bit: int) -> list[int]:
    return [i for i in range(1, len(line)) if (line[i - 1] >> bit) & 1 and not (line[i] >> bit) & 1]


def rising(line: list[int], bit: int) -> list[int]:
    return [i for i in range(1, len(line)) if not (line[i - 1] >> bit) & 1 and (line[i] >> bit) & 1]


def analyse(case, words: list[int], pixels) -> dict:
    name, _mde, _ntsc, width, planes, per_px, want_lines, _via = case
    # Une ligne commence à un front descendant de HSYNC : origine commune à tous les bords.
    starts = falling(words, 13)
    periods = {b - a for a, b in zip(starts, starts[1:])}
    lines = [words[a:b] for a, b in zip(starts, starts[1:])]
    active = [i for i, l in enumerate(lines) if any(w >> 15 for w in l)]
    if len(active) != HEIGHT:
        return {"name": name, "error": f"{len(active)} lignes actives, {HEIGHT} attendues"}

    # Le retard du pipeline est inconnu : on le cherche, sur le seul écran où il est unique.
    want0 = [physical(PALETTE[v]) for v in pixels[0]]
    first = lines[active[0]]
    # Tous les ticks de chaque pixel : sinon un milieu de pixel passerait pour un alignement.
    offsets = [o for o in range(len(first) - width * per_px)
               if all(first[o + per_px * x + k] & 0xFFF == want0[x]
                      for x in range(width) for k in range(per_px))]
    if len(offsets) != 1:
        return {"name": name, "error": f"{len(offsets)} alignements de la ligne 0"}
    offset = offsets[0]

    wrong = sum(lines[y0][offset + per_px * x + k] & 0xFFF != physical(PALETTE[pixels[row][x]])
                for row, y0 in enumerate(active) for x in range(width) for k in range(per_px))

    # Couleur pendant BLANK, puis dans la bordure, sur toute la trame.
    blank = [w & 0xFFF for w in words if not (w >> 12) & 1]
    rgb_blank = {f"{v:03X}": blank.count(v) for v in sorted(set(blank))}
    mid = lines[active[100]]
    border = [w & 0xFFF for w in mid[offset + width * per_px + 8:falling(mid, 12)[0]]]

    return {
        "name": name, "pixels_wrong": wrong, "ticks_per_pixel": per_px,
        "ticks_per_line": sorted(periods), "lines": len(lines) + 1, "lines_expected": want_lines,
        "first_active_line": active[0], "hsync_ticks": rising(mid, 13)[0],
        "pipeline_offset": offset, "de_start": rising(mid, 15)[0], "de_end": falling(mid, 15)[0],
        "blank_start": falling(mid, 12)[0], "blank_end": rising(mid, 12)[0],
        "rgb_during_blank": rgb_blank,
        "border_colors": sorted({f"{v:03X}" for v in border}),
        "right_after_blank": [f"{w & 0xFFF:03X}" for w in mid[rising(mid, 12)[0] - 1:rising(mid, 12)[0] + 4]],
        "left_of_blank": [f"{w & 0xFFF:03X}" for w in mid[falling(mid, 12)[0] - 2:falling(mid, 12)[0] + 3]],
    }


def main() -> int:
    if not shutil.which("verilator"):
        print("SAUTÉ : verilator absent (brew install verilator)")
        return EXIT_SKIPPED
    if not TB.exists():
        print("SAUTÉ : RTL absent (tools/setup_silicon_refs.sh)")
        return EXIT_SKIPPED
    if "--rebuild" in sys.argv or not BINARY.exists():
        build()
    OUT.mkdir(parents=True, exist_ok=True)
    results, ok = [], True
    for seed, case in enumerate((*CASES, OBSERVED), 1):
        name, mde, ntsc, width, planes, *_, via_ntsc = case
        pixels, vram = make_screen(width, planes, seed)
        (OUT / f"{name}.vram").write_bytes(vram)
        out = OUT / f"{name}.bin"
        subprocess.run([str(BINARY), str(OUT / f"{name}.vram"), str(out), str(mde), str(ntsc), str(via_ntsc),
                        *[f"{c:03x}" for c in PALETTE]], check=True, timeout=300)
        raw = out.read_bytes()
        result = analyse(case, list(struct.unpack(f"<{len(raw) // 2}H", raw)), pixels)
        results.append(result)
        if case is OBSERVED:
            print(f"OBS  {name}: {result.get('error', 'aligné : changement à examiner')}")
            continue
        passed = not result.get("error") and result["pixels_wrong"] == 0 \
            and result["lines"] == result["lines_expected"]
        ok &= passed
        print(f"{'OK  ' if passed else 'FAIL'} {name}: {json.dumps(result, ensure_ascii=False)}")
    (OUT / "summary.json").write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n")
    print("BANC GSTMCU OK" if ok else "BANC GSTMCU FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
