#!/usr/bin/env python3
"""Mesure les patchs ROM/RAM d'Hatari avant la première instruction du TOS.

Les fichiers d'origine sont seulement lus. Les dumps sont des copies de la
mémoire de l'oracle, jamais des ROM à installer dans NeoST. Aucun disque monté.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HATARI = ROOT / 'extern/hatari/build/src/hatari'


def changes(before, after, base=0):
    if len(before) != len(after):
        raise ValueError('Memory dump has the wrong size')
    rows, start = [], None
    for i in range(len(before) + 1):
        different = i < len(before) and before[i] != after[i]
        if different and start is None:
            start = i
        elif not different and start is not None:
            rows.append(dict(address=f'{base + start:06x}',
                             before=before[start:i].hex(), after=after[start:i].hex()))
            start = None
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('rom', nargs='+', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git', '-C', str(ROOT / 'extern/hatari'),
                                        'rev-parse', 'HEAD'], text=True).strip()
    report = dict(hatari_revision=revision, source_sha256={
        name: hashlib.sha256((ROOT / 'extern/hatari/src' / name).read_bytes()).hexdigest()
        for name in ('tos.c', 'stMemory.c')}, roms=[])
    for rom in args.rom:
        rom = rom.resolve()
        original = rom.read_bytes()
        digest = hashlib.sha256(original).hexdigest()
        version = int.from_bytes(original[2:4], 'big')
        base = int.from_bytes(original[8:12], 'big')
        if version not in (0x100, 0x102, 0x104, 0x106, 0x162, 0x205, 0x206):
            ap.error(f'Unsupported ST/STE ROM version: {version:04x}')
        row = dict(rom=str(rom), sha256=digest, version=f'{version:04x}', modes={})
        baseline_ram = None
        for mode, patches, fast in [('original', 'off', 'off'),
                                     ('patches', 'on', 'off'),
                                     ('fastboot', 'on', 'on')]:
            with tempfile.TemporaryDirectory(prefix='tos-audit-') as tmp:
                work = Path(tmp)
                (work / 'dump.ini').write_text(
                    f'savebin rom.bin ${base:x} ${len(original):x}\n'
                    'savebin ram.bin $0 $1000\nquit\n')
                cmd = [str(HATARI), '--tos', str(rom), '--machine', 'st' if version <= 0x104 else 'ste',
                       '--memsize', '1', '--monitor', 'rgb', '--vdi', 'off',
                       '--cpu-exact', 'on', '--compatible', 'on', '--fastfdc', 'off',
                       '--patch-tos', patches, '--fast-boot', fast, '--sound', 'off',
                       '--confirm-quit', 'off', '--log-level', 'debug',
                       '--parse', str(work / 'dump.ini'), '--run-vbls', '1']
                proc = subprocess.run(cmd, cwd=work, capture_output=True, text=True, timeout=60,
                                      env=dict(os.environ, SDL_VIDEODRIVER='dummy',
                                               SDL_AUDIODRIVER='dummy', HATARI_SEED='1'))
                log = proc.stdout + proc.stderr
                (args.out / f'{rom.stem}-{mode}.log').write_text(log)
                proc.check_returncode()
                dumped, ram = (work / 'rom.bin').read_bytes(), (work / 'ram.bin').read_bytes()
                delta = changes(original, dumped, base)
                if mode == 'original':
                    if delta:
                        raise ValueError(f'Unpatched ROM differs from input: {rom}')
                    baseline_ram = ram
                row['modes'][mode] = dict(command=cmd, rom_changes=delta,
                                          ram_changes=changes(baseline_ram, ram))
                print(f'{rom.name} {mode}: {sum(len(r["before"]) // 2 for r in delta)} ROM bytes changed')
        if hashlib.sha256(rom.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Original ROM changed: {rom}')
        report['roms'].append(row)
    (args.out / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
