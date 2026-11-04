#!/usr/bin/env python3
# =============================================================================
#  fix_short_reloc.py — Repère et répare, sur une copie, un PRG dont la table de relocation est trop courte.
#
#  POURQUOI. [C] TOS 1.62 $E08A34-$E08A50 lit 4 octets de relocation sans tester le résultat. Un fichier qui n'en a que
#  2 laisse le mot bas à ce qui traîne sur la pile : le programme sort corrompu, ou Pexec le refuse, selon le moment des
#  interruptions (docs/CASE_STUDIES.md, Vroom).
#
#  COMMENT. Lit la FAT12 d'une image .st, racine et AUTO. Si un PRG finit moins de 4 octets après son texte, ses données et
#  ses symboles, et que les octets suivants valent zéro sur le disque, porte sa taille à +4 dans le répertoire. Une
#  seule valeur change : aucun octet du programme. L'image d'origine n'est jamais modifiée.
#
#  Usage : fix_short_reloc.py IMAGE.st [SORTIE.st]    Sans SORTIE, ne fait que signaler.
#          Code 0 : rien à signaler ou réparé ; 1 : un PRG court non réparable ; 2 : image illisible.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import struct
import sys
from pathlib import Path


def bpb(img: bytes) -> tuple[int, int, int, int]:
    """Taille d'un cluster en octets, début de la racine, entrées de la racine, début des données."""
    sect, spc = struct.unpack("<H", img[11:13])[0], img[13]
    if sect != 512 or spc == 0:
        raise ValueError("BPB illisible")
    res, nfat, ndir, spf = (struct.unpack("<H", img[14:16])[0], img[16],
                            struct.unpack("<H", img[17:19])[0], struct.unpack("<H", img[22:24])[0])
    root = (res + nfat * spf) * sect
    return spc * sect, root, ndir, root + ndir * 32


def entries(img: bytes, start: int, count: int):
    for i in range(count):
        at = start + i * 32
        if at + 32 > len(img) or img[at] == 0:
            return
        if img[at] != 0xE5:
            yield at, img[at:at + 32]


def next_cluster(img: bytes, cluster: int) -> int:
    fat = struct.unpack("<H", img[14:16])[0] * 512
    at = fat + cluster * 3 // 2
    pair = img[at] | img[at + 1] << 8
    return pair >> 4 if cluster & 1 else pair & 0xFFF


def read_file(img: bytes, first: int, size: int, csize: int, data: int) -> tuple[bytes, list[int]]:
    """Le contenu suivi jusqu'à size + 4 octets, pour voir ce qui suit la fin du fichier."""
    out, chain, cluster = bytearray(), [], first
    while 2 <= cluster < 0xFF0 and len(chain) < 4096:
        chain.append(cluster)
        at = data + (cluster - 2) * csize
        out += img[at:at + csize]
        if len(out) >= size + 4:
            break
        cluster = next_cluster(img, cluster)
    return bytes(out), chain


def scan(img: bytearray, repair: bool) -> tuple[list[str], bool]:
    csize, root, ndir, data = bpb(img)
    places = [("", root, ndir)]
    for _, e in entries(img, root, ndir):
        if e[11] & 0x10 and e[:8].rstrip() == b"AUTO":
            first = struct.unpack("<H", e[26:28])[0]
            places.append(("AUTO\\", data + (first - 2) * csize, csize // 32))
    notes, ok = [], True
    for prefix, start, count in places:
        for at, e in entries(img, start, count):
            if e[11] & 0x18:
                continue
            first, size = struct.unpack("<H", e[26:28])[0], struct.unpack("<I", e[28:32])[0]
            name = e[:8].decode("latin-1").rstrip() + "." + e[8:11].decode("latin-1").rstrip()
            content, _ = read_file(img, first, size, csize, data)
            if len(content) < 28 or content[:2] != b"\x60\x1a":
                continue
            tlen, dlen, _bss, slen = struct.unpack(">IIII", content[2:18])
            abs_flag = struct.unpack(">H", content[26:28])[0]
            end = 28 + tlen + dlen + slen
            if abs_flag or not end <= size < end + 4:
                continue
            tail = size - end
            fixable = len(content) >= end + 4 and content[size:end + 4] == bytes(end + 4 - size)
            note = f"{prefix}{name} : {tail} octet(s) de relocation au lieu de 4"
            if fixable and repair:
                struct.pack_into("<I", img, at + 28, end + 4)
                note += f", taille portée à {end + 4}"
            elif not fixable:
                note += ", les octets suivants ne valent pas zéro"
                ok = False
            notes.append(note)
    return notes, ok


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__ or "usage : fix_short_reloc.py IMAGE.st [SORTIE.st]", file=sys.stderr)
        return 2
    img = bytearray(Path(sys.argv[1]).read_bytes())
    try:
        notes, ok = scan(img, len(sys.argv) == 3)
    except (ValueError, IndexError, struct.error) as exc:
        print(f"{sys.argv[1]} : {exc}")
        return 2
    for note in notes:
        print(f"{sys.argv[1]} : {note}")
    if len(sys.argv) == 3 and notes:
        Path(sys.argv[2]).write_bytes(img)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
