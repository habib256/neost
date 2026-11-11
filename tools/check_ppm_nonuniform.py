#!/usr/bin/env python3
# =============================================================================
#  check_ppm_nonuniform.py — La capture montre-t-elle QUELQUE CHOSE ?
#
#  Contrôle minimal partagé par les smoke-tests de paquet : un écran d'une seule
#  couleur = boot raté (machine figée, écran noir, palette morte). Volontairement
#  grossier — la comparaison PIXEL, elle, est le métier des étalons
#  (tools/run_etalons.py) et exige un oracle.
#
#  Usage : python3 tools/check_ppm_nonuniform.py <capture.ppm>
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import sys
from pathlib import Path


def parse_p6(data: bytes) -> tuple[int, int, bytes]:
    """(largeur, hauteur, pixels) d'un PPM P6 à maxval <= 255, commentaires « # » tolérés."""
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while end < len(data) and data[end:end + 1].isdigit():
            end += 1
        if end == pos:
            raise ValueError("champ numérique attendu")
        fields.append(int(data[pos:end]))
        pos = end
    width, height, maxval = fields
    if maxval > 255:
        raise ValueError(f"maxval {maxval} (16 bits) non géré")
    return width, height, data[pos + 1:]                # un seul blanc avant les pixels


def main() -> int:
    if len(sys.argv) != 2:
        print("usage : check_ppm_nonuniform.py <capture.ppm>", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"ERREUR : {path} absent", file=sys.stderr)
        return 1
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        print(f"ERREUR : {path} n'est pas un PPM binaire (P6)", file=sys.stderr)
        return 1
    try:
        width, height, body = parse_p6(data)
    except ValueError as e:
        print(f"ERREUR : {path} : en-tête PPM illisible ({e})", file=sys.stderr)
        return 1
    if width * height == 0 or len(body) < 3 * width * height:
        print(f"ERREUR : {path} tronqué ({len(body)} octets pour {width}x{height})", file=sys.stderr)
        return 1
    body = body[:3 * width * height]
    # Des COULEURS distinctes, pas des octets : un écran uni #006000 a deux valeurs d'octet
    # (0x00 et 0x60) mais une seule couleur.
    first = body[:3]
    if body == first * (width * height):
        print(f"ERREUR : {path} est uniforme (#{first.hex()}) — le boot a échoué", file=sys.stderr)
        return 1
    distinct = len({body[i:i + 3] for i in range(0, len(body), 3)})
    print(f"OK : {path} — {width}x{height}, {distinct} couleurs distinctes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
