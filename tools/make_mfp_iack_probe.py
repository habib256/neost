#!/usr/bin/env python3
"""Sonde libre d'IACK MegaSTE : PRG relogeable, disquette AUTO et cartouche de contrôle.

Le programme prend la main jusqu'au reset. Timer A provoque les IRQ ; la
première instruction du handler écrit la palette. Aucun chiffre de latence
n'est supposé : CLK, IACK, DTACK et le marqueur se mesurent sur la carte.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from make_usatan_test import Asm, build_floppy


PHASES = (("8_off", 0, 0x0700), ("16_off", 2, 0x0070), ("16_cache", 3, 0x0007))
RAM_BASE = 0x10000


def byte(a, address, value):
    a.w(0x11FC, value, address)  # MOVE.B #imm,abs.w


def body(a, samples):
    a.lbl("body")
    a.w(0x46FC, 0x2700)
    a.lea_lbl("stack_end", 7)
    # Toutes les sources autres que Timer A sont masquées ; pas de services TOS.
    for address in (0xFA07, 0xFA09, 0xFA13, 0xFA15, 0xFA19, 0xFA1B, 0xFA1D):
        byte(a, address, 0)
    byte(a, 0x8E01, 0)          # SCU : masquer HBL/VBL
    byte(a, 0x8E0D, 0x40)       # autoriser uniquement le MFP
    byte(a, 0xFA17, 0x40)       # EOI automatique, vecteurs $40
    a.w(0x21FC); a.abs32("handler"); a.w(0x0134)
    a.w(0x21FC); a.abs32("spurious"); a.w(0x0060)
    a.lea_lbl("counts", 0)
    for name, control, color in PHASES:
        a.lbl(name)
        byte(a, 0x8E21, control)  # chaque phase commence avec un cache invalidé
        byte(a, 0xFA0B, 0)
        byte(a, 0xFA0D, 0)
        byte(a, 0xFA1F, 192)      # /4 × 192 ; XTAL MFP, indépendant du CPU
        byte(a, 0xFA07, 0x20)
        byte(a, 0xFA13, 0x20)
        a.w(0x7A00, 0x3C3C, color, 0x3E3C, samples - 1)  # D5=0, D6=couleur, D7=N-1
        a.lbl("arm_" + name)
        byte(a, 0xFA19, 1)
        a.lbl("idle_" + name)
        a.w(0x4E72, 0x2500)       # STOP : seule l'IRQ MFP réveille le CPU
        a.dbra(7, "idle_" + name)
        a.w(0x46FC, 0x2700)
        byte(a, 0xFA19, 0)
        a.w(0x30C5)               # MOVE.W D5,(A0)+ : nombre d'IRQ réellement servi
    byte(a, 0xFA07, 0)
    byte(a, 0xFA13, 0)
    byte(a, 0x8E21, 0)
    a.w(0x30FC, 0xBEEF)           # témoin de fin, après les trois compteurs
    a.w(0x31FC, 0x0777, 0x8240)   # blanc : terminé ; reset pour quitter
    a.lbl("done")
    a.w(0x4E72, 0x2700)
    a.br(0x6000, "done")
    a.lbl("handler")
    a.w(0x31C6, 0x8240)           # PREMIÈRE instruction : MOVE.W D6,palette0.w
    a.w(0x0A46, 0x0111, 0x5245, 0x4E73)  # alterner le repère, compter, RTE
    a.lbl("spurious")
    a.w(0x30BC, 0xDEAD)           # diagnostic distinct d'une fin normale
    a.w(0x31FC, 0x0700, 0x8240)
    a.br(0x6000, "done")
    a.lbl("counts"); a.w(0, 0, 0, 0)
    a.lbl("stack"); a.w(*([0] * 128))
    a.lbl("stack_end")


def prg(samples):
    a = Asm(0)
    a.w(0x4879); a.abs32("banner")
    a.w(0x3F3C, 9, 0x4E41, 0x5C8F)  # Cconws
    # Super(0) avant de lire la cookie jar dans la mémoire système protégée.
    a.w(0x42A7, 0x3F3C, 0x0020, 0x4E41, 0x5C8F, 0x2840)  # ancien SSP dans A4
    a.w(0x2078, 0x05A0, 0x2008)  # A0=cookie jar ; MOVE.L A0,D0
    a.br(0x6700, "unsupported")
    a.lbl("cookie")
    a.w(0x2018)                  # identifiant
    a.br(0x6700, "unsupported")
    a.w(0x2218, 0x0C80); a.l32(0x5F4D4348)  # valeur, CMP.L #'_MCH',D0
    a.br(0x6600, "cookie")
    a.w(0x0C81); a.l32(0x00010010)  # MegaSTE ; ni STE simple, ni TT/Falcon
    a.br(0x6600, "unsupported")
    a.br(0x6000, "body")
    a.lbl("unsupported")
    a.lea_lbl("skipped_record", 0)
    a.w(0x317C, 0x5A1F, 4)       # témoin d'exécution du chemin de refus
    a.w(0x2F0C, 0x3F3C, 0x0020, 0x4E41, 0x5C8F)  # Super(ancien SSP)
    a.w(0x4879); a.abs32("skipped")
    a.w(0x3F3C, 9, 0x4E41, 0x5C8F)
    a.w(0x4267, 0x4E41)          # Pterm0 ; aucun accès aux registres MegaSTE
    body(a, samples)
    a.string("banner", "MegaSTE IACK probe: red / green / blue phases.\r\n"
                        "White border = complete. Reset to exit.\r\n")
    a.string("skipped", "Unsupported machine: probe skipped.\r\n")
    a.lbl("skipped_record"); a.w(0x4941, 0x434B, 0, 0)
    text = a.assemble()
    relocation = bytearray(struct.pack(">I", a.relocs[0]))
    previous = a.relocs[0]
    for offset in a.relocs[1:]:
        delta = offset - previous
        while delta > 254:
            relocation.append(1); delta -= 254
        relocation.append(delta); previous = offset
    relocation.append(0)
    header = struct.pack(">HIIIIIIH", 0x601A, len(text), 0, 0, 0, 0, 0, 0)
    return header + text + relocation, a.labels


def cartridge(samples):
    a = Asm(RAM_BASE)
    body(a, samples)
    code = a.assemble()
    # La cartouche de contrôle exécute le même corps depuis la RAM, comme le PRG.
    # En-tête NeoST/Hatari, puis copie à $10000 avant le démarrage du TOS.
    # Construire explicitement les longs évite de reloger le stub lui-même.
    stub = bytearray(struct.pack(">I", 0xFA52235F))
    stub += struct.pack(">HIHIHH", 0x207C, 0, 0x227C, RAM_BASE, 0x303C, len(code)//2-1)
    stub += struct.pack(">HHhHI", 0x32D8, 0x51C8, -4, 0x4EF9, RAM_BASE)
    struct.pack_into(">I", stub, 6, 0xFA0000 + len(stub))
    labels = {key: RAM_BASE + offset for key, offset in a.labels.items()}
    return bytes(stub) + code, labels


def generate(output, samples=72):
    if not 9 <= samples <= 32767:
        raise ValueError("samples must be between 9 and 32767 (first 8 are warmup)")
    output.mkdir(parents=True, exist_ok=True)
    program, offsets = prg(samples)
    cart, labels = cartridge(samples)
    files = {"IACKTEST.PRG": program, "IACKTEST.ST": build_floppy(program, "IACKTEST"),
             "iack-control.bin": cart}
    for name, data in files.items():
        (output / name).write_bytes(data)
    manifest = {"schema": 1, "policy": "measurement_probe_no_timing_reference",
                "machine": "megaste", "samples_per_phase": samples, "warmup": 8,
                "timer_a": {"prescaler": 4, "reload": 192},
                "phases": [{"name": n, "cache_control": c, "marker_color": v}
                           for n, c, v in PHASES],
                "prg_text_offsets": offsets, "cartridge_addresses": labels,
                "sha256": {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("tests/out/mfp_iack_probe"))
    parser.add_argument("--samples", type=int, default=72)
    args = parser.parse_args()
    try:
        generate(args.out, args.samples)
    except ValueError as error:
        parser.error(str(error))
    print(f"Probe written to {args.out}; hardware measurement pending. Reset to exit.")


if __name__ == "__main__":
    main()
