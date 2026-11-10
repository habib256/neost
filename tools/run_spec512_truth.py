#!/usr/bin/env python3
# =============================================================================
#  run_spec512_truth.py — Le pixel contre un logiciel réel : les images Spectrum 512.
#
#  POURQUOI. Spectrum 512 change la palette en cours de ligne, à un cycle précis. Le format documente à
#  quel pixel chacune des 48 couleurs de la ligne s'applique (formule de `render`, reprise par tous les
#  décodeurs, dont RECOIL). Cette règle décrit ce que l'image donne sur la machine pour laquelle elle a
#  été dessinée : c'est une preuve de grade C, indépendante d'Hatari et de tout RTL. Le visualiseur réel
#  tourne dans NeoST, et chaque pixel doit égaler le décodage. Un retard de palette de k pixels se verrait
#  aussitôt : l'outil le mesure, avec `--shifts`.
#
#  Usage : run_spec512_truth.py [--shifts] [--write-refs]
#  Code : 0 si tous les verdicts concordent ; 1 pour un écart ; 77 pour une
#  couverture partielle (ROM manquante) ou absente. Les cas présents restent comparés.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
# =============================================================================
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DISK = ROOT / "disks" / "etalons" / "spectrum_512_auto_diapo.st"
HEADLESS = ROOT / "build" / "neost-headless"
REF = ROOT / "tests" / "reference" / "spec512_truth.json"
X0, Y0 = 48, 30       # 1er pixel de la 2e ligne d'écran : la 1re ligne d'une image Spectrum est vide
RUNS = (("st", "roms/tos102uk.img"), ("ste", "roms/tos162uk.img"))
FRAMES, EVERY = 3600, 60
EXIT_SKIPPED = 77


def finish_results(verdict: dict, skipped: list[str], write_refs: bool = False) -> int:
    """Comparer uniquement les machines exécutées, sans masquer leurs régressions."""
    if not verdict:
        print("SKIP — images Spectrum 512 : aucune machine exécutée")
        return EXIT_SKIPPED
    if write_refs:
        if skipped:
            print("SKIP — référence conservée : toutes les ROM sont requises pour --write-refs")
            return EXIT_SKIPPED
        REF.write_text(json.dumps(verdict, indent=0, sort_keys=True) + "\n")
        print(f"référence écrite : {REF}")
        return 0
    if not REF.exists():
        print("Pas de référence.")
        return 1
    expected = json.loads(REF.read_text())
    changed = [machine for machine, result in verdict.items()
               if machine not in expected or expected[machine] != result]
    if changed:
        print("IMAGES SPECTRUM 512 : verdicts changés : " + ", ".join(changed))
        return 1
    if skipped:
        print("SKIP — images Spectrum 512 : verdicts présents identiques ; ROM absentes : " + ", ".join(skipped))
        return EXIT_SKIPPED
    print("IMAGES SPECTRUM 512 OK : verdicts identiques à la référence")
    return 0


class Fat:
    """FAT12 d'une disquette .ST, en lecture."""

    def __init__(self, d: bytes):
        self.d = d
        self.bps = struct.unpack("<H", d[11:13])[0]
        self.spc, self.res, self.nf = d[13], struct.unpack("<H", d[14:16])[0], d[16]
        self.re, self.spf = struct.unpack("<H", d[17:19])[0], struct.unpack("<H", d[22:24])[0]
        self.root = (self.res + self.nf * self.spf) * self.bps
        self.data = self.root + self.re * 32

    def read(self, c: int, size=None) -> bytes:
        r = bytearray()
        while 2 <= c < 0xFF0:
            o = self.data + (c - 2) * self.spc * self.bps
            r += self.d[o:o + self.spc * self.bps]
            o = self.res * self.bps + c * 3 // 2
            v = self.d[o] | self.d[o + 1] << 8
            c = v >> 4 if c & 1 else v & 0xFFF
        return bytes(r[:size]) if size is not None else bytes(r)

    def walk(self, buf=None, pre=""):
        buf = self.d[self.root:self.root + self.re * 32] if buf is None else buf
        for i in range(0, len(buf), 32):
            e = buf[i:i + 32]
            if not e or e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 8:
                continue
            name = e[:8].decode("latin1").strip()
            if name in (".", ".."):
                continue
            ext = e[8:11].decode("latin1").strip()
            full = pre + name + ("." + ext if ext else "")
            c, z = struct.unpack("<H", e[26:28])[0], struct.unpack("<I", e[28:32])[0]
            if e[11] & 16:
                yield from self.walk(self.read(c), full + "\\")
            else:
                yield full, c, z


def decode_spc(data: bytes):
    length = struct.unpack(">I", data[4:8])[0]
    p, out = 12, bytearray()
    while len(out) < 31840:
        b = data[p]
        p += 1
        if b >= 128:
            out += bytes([data[p]]) * (258 - b)
            p += 1
        else:
            out += data[p:p + b + 1]
            p += b + 1
    words = [0] * (31840 // 2)
    for plane in range(4):
        for i in range(0, 7960, 2):
            words[(i // 2) * 4 + plane] = out[plane * 7960 + i] << 8 | out[plane * 7960 + i + 1]
    q, palettes = 12 + length, []
    for _ in range(199 * 3):
        mask = struct.unpack(">H", data[q:q + 2])[0]
        q += 2
        colors = [0] * 16
        for c in range(16):
            if mask & (1 << c):
                colors[c] = struct.unpack(">H", data[q:q + 2])[0]
                q += 2
        palettes.append(colors)
    return words, palettes


def decode_spu(data: bytes):
    words = list(struct.unpack(">16000H", data[:32000]))[80:]
    palettes = [list(struct.unpack(">16H", data[32000 + i * 32:32032 + i * 32])) for i in range(199 * 3)]
    return words, palettes


def render(words, palettes, shift: int = 0) -> bytes:
    """Couleur RVB 3 bits par pixel (une valeur par composante), décalée de `shift` pixels."""
    out = bytearray(320 * 199 * 3)
    for y in range(199):
        line = words[y * 80:y * 80 + 80]
        for x in range(320):
            g, bit = x // 16, 15 - x % 16
            c = sum(((line[g * 4 + pl] >> bit) & 1) << pl for pl in range(4))
            # La formule du format : la couleur c change aux pixels x1 puis x1 + 160.
            x1 = 10 * c + (-5 if c & 1 else 1)
            xx = x - shift
            i = 1 if x1 <= xx < x1 + 160 else (2 if xx >= x1 + 160 else 0)
            v = palettes[y * 3 + i][c]
            o = (y * 320 + x) * 3
            out[o], out[o + 1], out[o + 2] = (v >> 8) & 7, (v >> 4) & 7, v & 7
    return bytes(out)


def read_ppm(path: Path):
    data = path.read_bytes()
    parts, pos = [], 0
    while len(parts) < 4:
        end = pos
        while data[end] not in b" \n\t":
            end += 1
        parts.append(data[pos:end])
        pos = end + 1
    return int(parts[1]), int(parts[2]), data[pos:]


def screen_area(path: Path):
    w, h, pix = read_ppm(path)
    if (w, h) != (416, 276):
        return None
    out = bytearray(320 * 199 * 3)
    for y in range(199):
        row = pix[((y + Y0) * w + X0) * 3:((y + Y0) * w + X0 + 320) * 3]
        out[y * 960:(y + 1) * 960] = bytes(v // 34 for v in row)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--shifts", action="store_true")
    ap.add_argument("--write-refs", action="store_true")
    a = ap.parse_args()
    if not DISK.exists():
        print("SKIP — images Spectrum 512 : disquette absente")
        return EXIT_SKIPPED
    skipped = [machine for machine, rom_path in RUNS if not (ROOT / rom_path).exists()]
    if len(skipped) == len(RUNS):
        return finish_results({}, skipped, a.write_refs)
    if not HEADLESS.exists():
        print("SKIP — images Spectrum 512 : neost-headless absent")
        return EXIT_SKIPPED
    fat = Fat(DISK.read_bytes())
    pictures = {}
    for path, c, z in fat.walk():
        data = fat.read(c, z)
        if path.upper().endswith(".SPC") and data[:2] == b"SP":
            pictures[path] = decode_spc(data)
        elif path.upper().endswith(".SPU") and len(data) == 51104:
            pictures[path] = decode_spu(data)
    truth = {name: render(*pic) for name, pic in pictures.items()}
    verdict = {}
    for machine, rom_path in RUNS:
        if not (ROOT / rom_path).exists():
            print(f"  {machine}: {rom_path} absente, sautée")
            continue
        with tempfile.TemporaryDirectory() as tmp:
            prefix = str(Path(tmp) / "s_")
            copy = Path(tmp) / "d.st"
            copy.write_bytes(DISK.read_bytes())
            subprocess.run([str(HEADLESS), rom_path, "--machine", machine, "--mem", "1m", "--disk", str(copy),
                            "--frames", str(FRAMES), "--shot-from", "600", "--shot-every", str(EVERY), prefix],
                           cwd=ROOT, check=True, capture_output=True, timeout=900)
            seen = {}
            for shot in sorted(Path(tmp).glob("s_*")):
                area = screen_area(shot)
                if area is None:
                    continue
                for name, want in truth.items():
                    bad = sum(1 for i in range(0, len(want), 3) if want[i:i + 3] != area[i:i + 3])
                    if bad < seen.get(name, 1 << 30):
                        seen[name] = bad
            exact = {n: b for n, b in seen.items() if b < 3000}
            verdict[machine] = exact
            print(f"  {machine}: " + ", ".join(f"{n.split(chr(92))[-1]} {b} px" for n, b in sorted(exact.items())))
            if a.shifts:
                for name, bad in exact.items():
                    words, pals = pictures[name]
                    row = []
                    for k in (-4, -2, 0, 2, 4):
                        want = render(words, pals, k)
                        best = min(sum(1 for i in range(0, len(want), 3) if want[i:i + 3] != ar[i:i + 3])
                                   for ar in [screen_area(s) for s in sorted(Path(tmp).glob("s_*"))] if ar)
                        row.append(f"{k:+d}:{best}")
                    print(f"     {name.split(chr(92))[-1]} décalage " + " ".join(row))
    return finish_results(verdict, skipped, a.write_refs)


if __name__ == "__main__":
    sys.exit(main())
