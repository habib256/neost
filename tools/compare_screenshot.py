#!/usr/bin/env python3
# =============================================================================
#  compare_screenshot.py — Diff pixel entre deux captures (PPM/PNG).
#
#  Compare la zone utile (active 320×200, buffer 416×276, ou plein cadre) et
#  recadre automatiquement si l'oracle Hatari est en 2× (832×552 → 416×276).
#
#  Usage :
#    python3 tools/compare_screenshot.py A.ppm B.png --crop active
#    python3 tools/compare_screenshot.py A.ppm B.png --crop active --max 0
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

# Zones utiles du framebuffer NeoST (overscan inclus), cf. Shifter.hpp.
ACTIVE = (48, 29, 320, 200)   # x, y, w, h
BUFFER = (0, 0, 416, 276)


def _read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:2] != b"P6":
        raise ValueError(f"{path} : PPM P6 attendu")
    i = 2
    while data[i : i + 1] != b"\n":
        i += 1
    i += 1
    while data[i : i + 1] == b"#":
        while data[i : i + 1] != b"\n":
            i += 1
        i += 1
    line_end = data.index(b"\n", i)
    w, h = map(int, data[i:line_end].split())
    i = line_end + 1
    while data[i : i + 1] != b"\n":
        i += 1
    i += 1
    px = data[i : i + w * h * 3]
    if len(px) != w * h * 3:
        raise ValueError(f"{path} : taille incohérente ({w}x{h})")
    return w, h, px


def _load_image(path: Path) -> tuple[int, int, bytes]:
    if path.suffix.lower() == ".ppm":
        return _read_ppm(path)
    if path.suffix.lower() in (".png", ".jpg", ".jpeg"):
        tmp = Path(tempfile.mkstemp(suffix=".ppm")[1])
        try:
            subprocess.run(
                ["ffmpeg", "-y", "-loglevel", "error", "-i", str(path),
                 "-f", "image2", "-pix_fmt", "rgb24", str(tmp)],
                check=True,
            )
            return _read_ppm(tmp)
        finally:
            tmp.unlink(missing_ok=True)
    raise ValueError(f"format non supporté : {path}")


def _crop(px: bytes, w: int, h: int, x: int, y: int, cw: int, ch: int) -> bytes:
    out = bytearray(cw * ch * 3)
    for row in range(ch):
        for col in range(cw):
            si = ((y + row) * w + (x + col)) * 3
            di = (row * cw + col) * 3
            out[di : di + 3] = px[si : si + 3]
    return bytes(out)


def _downscale2(px: bytes, w: int, h: int) -> tuple[int, int, bytes]:
    """Moyenne 2×2 — oracle Hatari plein écran → résolution NeoST."""
    ow, oh = w // 2, h // 2
    out = bytearray(ow * oh * 3)
    for y in range(oh):
        for x in range(ow):
            acc = [0, 0, 0]
            for dy in (0, 1):
                for dx in (0, 1):
                    si = ((y * 2 + dy) * w + (x * 2 + dx)) * 3
                    acc[0] += px[si]
                    acc[1] += px[si + 1]
                    acc[2] += px[si + 2]
            di = (y * ow + x) * 3
            out[di : di + 3] = bytes(v // 4 for v in acc)
    return ow, oh, bytes(out)


def _align_buffer(w: int, h: int, px: bytes) -> tuple[int, int, bytes]:
    """Recadre un plein écran Hatari 2× sur le buffer NeoST 416×276."""
    bw, bh = BUFFER[2], BUFFER[3]
    if w == bw and h == bh:
        return w, h, px
    if w == bw * 2 and h == bh * 2:
        x0 = (w - bw * 2) // 2
        y0 = (h - bh * 2) // 2
        cropped = _crop(px, w, h, x0, y0, bw * 2, bh * 2)
        return _downscale2(cropped, bw * 2, bh * 2)
    if w >= bw and h >= bh:
        x0 = (w - bw) // 2
        y0 = (h - bh) // 2
        return bw, bh, _crop(px, w, h, x0, y0, bw, bh)
    return w, h, px


def _region(crop: str) -> tuple[int, int, int, int]:
    if crop == "active":
        return ACTIVE
    if crop == "buffer":
        return BUFFER
    if crop == "full":
        return (0, 0, 0, 0)  # spécial : pas de recadrage relatif
    raise ValueError(f"crop inconnu : {crop}")


def compare(a_path: Path, b_path: Path, crop: str = "active") -> tuple[int, int]:
    aw, ah, apx = _load_image(a_path)
    bw, bh, bpx = _load_image(b_path)
    aw, ah, apx = _align_buffer(aw, ah, apx)
    bw, bh, bpx = _align_buffer(bw, bh, bpx)

    if crop == "full":
        if (aw, ah) != (bw, bh):
            raise ValueError(f"tailles différentes : {aw}x{ah} vs {bw}x{bh}")
        cw, ch = aw, ah
        ax = ay = bx = by = 0
    else:
        x, y, cw, ch = _region(crop)
        if aw < x + cw or ah < y + ch:
            raise ValueError(f"A trop petit pour crop {crop} : {aw}x{ah}")
        if bw < x + cw or bh < y + ch:
            raise ValueError(f"B trop petit pour crop {crop} : {bw}x{bh}")
        ax = ay = bx = by = 0
        if crop == "active" and aw == BUFFER[2] and bw == BUFFER[2]:
            ax = ay = bx = by = 0  # déjà dans le buffer aligné

    a = _crop(apx, aw, ah, x + ax, y + ay, cw, ch)
    b = _crop(bpx, bw, bh, x + bx, y + by, cw, ch)
    diff = sum(1 for i in range(0, len(a), 3) if a[i : i + 3] != b[i : i + 3])
    return diff, cw * ch


def main() -> int:
    ap = argparse.ArgumentParser(description="Diff pixel entre deux captures ST")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--crop", choices=("active", "buffer", "full"), default="active")
    ap.add_argument("--max", type=int, default=None, help="seuil max (exit 1 si dépassé)")
    args = ap.parse_args()
    diff, total = compare(Path(args.a), Path(args.b), args.crop)
    pct = 100.0 * diff / total if total else 0.0
    print(f"diff_px={diff} / {total} ({pct:.2f} %)")
    if args.max is not None and diff > args.max:
        print(f"ÉCHEC : {diff} > {args.max}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
