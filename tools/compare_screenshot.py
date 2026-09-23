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
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# Zones utiles du framebuffer NeoST (overscan inclus), cf. Shifter.hpp.
ACTIVE = (48, 29, 320, 200)   # x, y, w, h
BUFFER = (0, 0, 416, 276)
# Rectangle de la LED disquette incrustée par Hatari dans ses captures AVI (coin haut
# droit, #E00000). Elle n'existe pas côté NeoST : toute comparaison couvrant le buffer
# entier accuserait un écart constant qui n'est PAS une divergence de rendu.
#
# La LED mesure 10x5 à l'œil, mais Hatari capture en 2x et le sous-échantillonnage
# MÊLE ses bords au fond : il reste un LISERÉ d'un pixel tout autour, invisible sur
# fond noir (noir mêlé de noir) et bien visible sur fond coloré. Le masque a longtemps
# valu (403, 3, 10, 5) et laissait donc passer ce liseré — c'est l'intégralité des
# « 22 px inexpliqués » de l'étalon trace_odd (fond vert), soldés le 2026-08-29 : les
# 72 pixels concernés portent des teintes que le Shifter ne PEUT PAS produire
# (octets à nibbles inégaux ou impairs, cf. stColorToArgb), donc aucune n'est du rendu.
# D'où le rectangle élargi d'un pixel sur chaque bord.
# Depuis le 2026-09-01 hatari_oracle.sh passe --drive-led off : les captures NEUVES n'ont
# plus de LED. Le masque reste tant qu'une référence commise la porte ; le jour où elles
# sont toutes régénérées, `buffer_noled` peut redevenir `buffer`.
HATARI_LED = (402, 2, 12, 6)   # x, y, w, h


def _read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:2] != b"P6":
        raise ValueError(f"{path} : PPM P6 attendu")
    # Bornées par len(data) : sur un en-tête TRONQUÉ (réf interrompue, pointeur
    # git-LFS, disque plein), « data[i:i+1] != b'\n' » restait vrai pour toujours
    # (tranche vide ≠ '\n') → le run d'étalons entier se figeait sans diagnostic.
    n = len(data)

    def skip_to_nl(i: int) -> int:
        while i < n and data[i : i + 1] != b"\n":
            i += 1
        if i >= n:
            raise ValueError(f"{path} : en-tête PPM tronqué")
        return i + 1

    i = skip_to_nl(2)
    while data[i : i + 1] == b"#":
        i = skip_to_nl(i)
    line_end = data.index(b"\n", i)
    w, h = map(int, data[i:line_end].split())
    i = skip_to_nl(line_end + 1)
    px = data[i : i + w * h * 3]
    if len(px) != w * h * 3:
        raise ValueError(f"{path} : taille incohérente ({w}x{h})")
    return w, h, px


def _read_png(path: Path) -> tuple[int, int, bytes]:
    """Décodeur PNG en Python pur (zlib seulement) : 8 bits, gris/RGB/RGBA (± alpha),
    non entrelacé — ce que sont TOUTES les références (captures d'oracle Hatari).
    Repli quand ffmpeg manque OU plante (2026-09-23 : un ffmpeg Homebrew dont la
    bibliothèque x265 avait disparu abattait deux étalons du palier fast avec un
    SIGABRT, et le palier accusait le rendu). Lève ValueError sur un PNG hors
    contrat (palette, 16 bits, entrelacé) — l'appelant retombe alors sur ffmpeg."""
    import struct
    import zlib

    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} : pas un PNG")
    pos = 8
    w = h = 0
    ctype = depth = interlace = -1
    idat = bytearray()
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos : pos + 8])
        body = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            w, h, depth, ctype, _c, _f, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth != 8 or interlace != 0:
        raise ValueError(f"{path} : PNG hors contrat (type {ctype}, {depth} bits, "
                         f"entrelacé={interlace})")
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    if len(raw) != h * (stride + 1):
        raise ValueError(f"{path} : données IDAT incohérentes")
    prev = bytearray(stride)
    out = bytearray(w * h * 3)
    bpp = channels
    for row in range(h):
        base = row * (stride + 1)
        ftype = raw[base]
        cur = bytearray(raw[base + 1 : base + 1 + stride])
        if ftype == 1:                       # Sub
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 0xFF
        elif ftype == 2:                     # Up
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif ftype == 3:                     # Average
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:                     # Paeth
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[i] = (cur[i] + pred) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path} : filtre PNG inconnu {ftype}")
        o = row * w * 3
        if channels == 3:
            out[o : o + stride] = cur
        elif channels == 4:
            out[o : o + w * 3] = bytes(v for i, v in enumerate(cur) if i & 3 != 3)
        elif channels == 1:
            out[o : o + w * 3] = bytes(v for v in cur for _ in range(3))
        else:                                # gris + alpha
            out[o : o + w * 3] = bytes(cur[i] for i in range(0, stride, 2) for _ in range(3))
        prev = cur
    return w, h, bytes(out)


def _load_image(path: Path) -> tuple[int, int, bytes]:
    if path.suffix.lower() == ".ppm":
        return _read_ppm(path)
    if path.suffix.lower() == ".png":
        try:
            return _read_png(path)           # sans dépendance : d'abord
        except ValueError:
            pass                             # PNG hors contrat → ffmpeg ci-dessous
    if path.suffix.lower() in (".png", ".jpg", ".jpeg"):
        # mkstemp rend un descripteur OUVERT : le refermer avant de laisser ffmpeg
        # écrire dans le fichier. Sous POSIX l'oubli ne se voyait pas (une simple
        # fuite de descripteur, une par comparaison) ; sous Windows le fichier est
        # VERROUILLÉ tant qu'il est ouvert, et ffmpeg échouait — donc toute référence
        # PNG (les captures d'oracle Hatari) était incomparable sur cette plateforme.
        _fd, _name = tempfile.mkstemp(suffix=".ppm")
        os.close(_fd)
        tmp = Path(_name)
        try:
            try:
                subprocess.run(
                    ["ffmpeg", "-y", "-loglevel", "error", "-i", str(path),
                     "-f", "image2", "-pix_fmt", "rgb24", str(tmp)],
                    check=True,
                )
            except (FileNotFoundError, subprocess.CalledProcessError) as exc:
                raise RuntimeError(
                    "ffmpeg est requis pour lire les références JPEG et les PNG "
                    "hors contrat (palette, 16 bits, entrelacé) — les PNG 8 bits "
                    "se lisent sans lui — "
                    "(macOS : brew install ffmpeg ; Debian/Ubuntu : "
                    "sudo apt install ffmpeg)"
                ) from exc
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
    if crop in ("buffer", "buffer_noled"):
        return BUFFER
    if crop == "full":
        return (0, 0, 0, 0)  # spécial : pas de recadrage relatif
    raise ValueError(f"crop inconnu : {crop}")


def compare(a_path: Path, b_path: Path, crop: str = "active",
            report: bool = False) -> tuple[int, int, dict]:
    aw, ah, apx = _load_image(a_path)
    bw, bh, bpx = _load_image(b_path)
    aw, ah, apx = _align_buffer(aw, ah, apx)
    bw, bh, bpx = _align_buffer(bw, bh, bpx)

    if crop == "full":
        if (aw, ah) != (bw, bh):
            raise ValueError(f"tailles différentes : {aw}x{ah} vs {bw}x{bh}")
        cw, ch = aw, ah
        x = y = ax = ay = bx = by = 0
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
    # crop « buffer_noled » = tout le framebuffer SAUF la LED disquette d'Hatari. C'est
    # le seul crop qui couvre les BORDURES tout en restant comparable à un oracle : une
    # mutation réelle du rendu de bordure (31 616 px corrompus) passait inaperçue de bout
    # en bout de la suite tant que tout le monde comparait en « active ».
    skip = None
    if crop == "buffer_noled":
        lx, ly, lw, lh = HATARI_LED
        skip = (lx - x, ly - y, lw, lh)
    diff = 0
    info = {"rows": [], "first": None, "w": cw, "h": ch}   # diagnostic par ligne
    for row in range(ch):
        rc = 0
        for col in range(cw):
            if skip and skip[1] <= row < skip[1] + skip[3] and skip[0] <= col < skip[0] + skip[2]:
                continue
            i = (row * cw + col) * 3
            if a[i : i + 3] != b[i : i + 3]:
                diff += 1
                rc += 1
                if report and info["first"] is None:
                    info["first"] = (col, row, tuple(a[i:i+3]), tuple(b[i:i+3]))
        if report and rc:
            info["rows"].append((row, rc))
    return diff, cw * ch, info


def _print_report(info: dict) -> None:
    # Diagnostic « palette par ligne » : quelles scanlines divergent, et de combien.
    # Un décalage vertical spec512 se voit comme une bande de lignes contiguës ; un
    # décalage horizontal comme un petit compte constant sur beaucoup de lignes.
    rows = info["rows"]
    if not rows:
        return
    if info["first"]:
        col, row, va, vb = info["first"]
        print(f"  1ᵉʳ écart : (x={col}, y={row})  A={_rgb(va)}  B={_rgb(vb)}", file=sys.stderr)
    print(f"  {len(rows)} scanline(s) divergentes (sur {info['h']}), pires lignes :",
          file=sys.stderr)
    for row, rc in sorted(rows, key=lambda r: -r[1])[:12]:
        print(f"    y={row:3d} : {rc:4d}/{info['w']} px", file=sys.stderr)


def _rgb(t) -> str:
    return f"#{t[0]:02X}{t[1]:02X}{t[2]:02X}"


def main() -> int:
    ap = argparse.ArgumentParser(description="Diff pixel entre deux captures ST")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--crop", choices=("active", "buffer", "buffer_noled", "full"), default="active")
    ap.add_argument("--max", type=int, default=None, help="seuil max (exit 1 si dépassé)")
    ap.add_argument("--report", action="store_true",
                    help="diagnostic par scanline (localise un décalage spec512)")
    args = ap.parse_args()
    try:
        diff, total, info = compare(Path(args.a), Path(args.b), args.crop, report=args.report)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERREUR : {exc}", file=sys.stderr)
        return 2
    pct = 100.0 * diff / total if total else 0.0
    print(f"diff_px={diff} / {total} ({pct:.2f} %)")
    if args.report and diff:
        _print_report(info)
    if args.max is not None and diff > args.max:
        print(f"ÉCHEC : {diff} > {args.max}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
