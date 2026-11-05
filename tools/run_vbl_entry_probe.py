#!/usr/bin/env python3
"""Sonde exploratoire VBL : PC empilé, NeoST contre fx68k + GSTMCU STE.

Trois STOP, puis seize VBL pendant une boucle LEA/MOVEA/NOP/CMPI/BLE.
Échelon rtl (grade D), pas une preuve matérielle. Code 1 si les PC diffèrent,
77 si le modèle manque. Le désaccord reste ouvert : ce test ne désigne pas le modèle fautif.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

import build_stcpu_bench as rtl
from make_stcpu_rom import P, movebi

ROOT = Path(__file__).resolve().parent.parent
TABLE, SIZE = 0x20000, 19 * 4


def program():
    p = P()
    p.w(0x46FC, 0x2700, 0x4FF9).l(0x8000)  # SR, pile
    movebi(p, 10, 0xFF8001)
    movebi(p, 0, 0xFF8260)
    movebi(p, 2, 0xFF820A)
    p.w(0x21FC).addr('irq').w(0x70)
    p.w(0x45F9).l(TABLE)                    # lea table,a2
    for _ in range(3):
        p.w(0x4E72, 0x2300)
    p.w(0x7C00)                            # moveq #0,d6
    p.label('loop')
    # LEA comme dans la fenêtre TOS ; MOVEA vise A4 pour garder une pile sûre.
    p.w(0x41EF, 0x32, 0x287C).l(0x31D0)
    p.w(0x4E71, 0x4E71, 0x0C46, 15)
    p.b8(0x6F00, 'loop')
    p.w(0x46FC, 0x2700)
    p.label('end').b8(0x6000, 'end2').w(0x4E71)
    p.label('end2').b8(0x6000, 'end')
    p.label('irq')
    p.w(0x24EF, 2, 0x5246, 0x4E73)          # move.l 2(sp),(a2)+ ; addq.w #1,d6 ; rte
    return p.done()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--headless', type=Path, default=ROOT / 'build/neost-headless')
    ap.add_argument('--out', type=Path, default=ROOT / 'build/vbl-entry-probe')
    args = ap.parse_args()
    exe = args.headless.resolve()
    if not rtl.BINARY.exists() or not exe.exists():
        print('SKIP: build the CPU + GSTMCU model and neost-headless first')
        return 77
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    image = out / 'probe.img'
    image.write_bytes(program())
    env = {k: v for k, v in os.environ.items() if not k.startswith(('NEOST_', 'HATARI_'))}
    env['NEOST_ECLOCK_PHASE'] = '0'
    results, commands = {}, []
    for name, binary in (('rtl', rtl.BINARY), ('neost', exe)):
        target = out / f'{name}.bin'
        if name == 'rtl':
            cmd = [str(binary), str(image), '22', f'{TABLE:x}', str(SIZE), str(target)]
            cwd = rtl.BUILD
        else:
            cmd = [str(binary), str(image), '--machine', 'ste', '--mem', '4m',
                   '--frames', '23', '--dump-at', '21', f'{TABLE:x}', str(SIZE), str(target)]
            cwd = ROOT
        commands.append(cmd)
        with (out / f'{name}.log').open('w') as log:
            subprocess.run(cmd, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                           timeout=180, check=True)
        raw = target.read_bytes()
        if len(raw) != SIZE:
            raise RuntimeError(f'{name}: incomplete PC table')
        pcs = [int.from_bytes(raw[i:i+4], 'big') for i in range(0, SIZE, 4)]
        if any(pc < 0xE00030 or pc >= 0xE00100 or pc & 1 for pc in pcs):
            raise RuntimeError(f'{name}: invalid or unfinished PC table')
        results[name] = pcs
    bad = [i for i, (a, b) in enumerate(zip(results['rtl'], results['neost'])) if a != b]
    report = dict(ref_kind='rtl', references=dict(fx68k='0602ee46', gstmcu='4a6db7cc'),
                  pcs=results, mismatches=bad, commands=commands,
                  sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in (image, exe, rtl.BINARY)},
                  limits=['Model agreement does not establish hardware accuracy.',
                          'This probe does not validate TOS boot or Vroom compatibility.'])
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{"FAIL" if bad else "PASS"}: VBL entry PC, {SIZE//4-len(bad)}/{SIZE//4} match; differences: {bad}')
    return int(bool(bad))


if __name__ == '__main__':
    raise SystemExit(main())
