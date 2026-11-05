#!/usr/bin/env python3
"""Capture appariée du TOS 2.06 UK original, STE 1 Mo, disque public diskA.st.

Trois fenêtres : reset → fin de synchronisation vidéo, preauto (trame 340 →
entrée AUTO), ou processus AUTO → retour de Fsetdta. SPACE 100/300, tenu huit trames, dans les deux émulateurs.
Les traces, commandes, empreintes et divergences restent dans --out.
Code 0 = capture réussie, PAS compatibilité validée. Aucune source média écrite.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--phase', choices=('reset', 'preauto', 'auto'), default='reset')
    args = ap.parse_args()
    # Le pilote FIFO existant exige Pillow ; aucune image n'est produite ici.
    from compare_titles import HATARI, HEADLESS, run_hatari_keys
    import trace_diff

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    # Des répertoires neufs empêchent de confondre une ancienne trace avec la capture.
    nw, hw = out / 'neost', out / 'hatari'
    nw.mkdir(); hw.mkdir()
    # Le témoin local porte des sondes/rejeux : le commit seul ne décrit pas
    # ses sources. Conserver aussi le diff exact, en plus de l'empreinte binaire.
    hatari_diff = subprocess.check_output(
        ['git', '-C', str(ROOT / 'extern/hatari'), 'diff', '--binary', 'HEAD'])
    (hw / 'source.patch').write_bytes(hatari_diff)
    rom, disk = ROOT / 'roms/tos206uk.img', ROOT / 'disks/diskA.st'
    hashes = {str(p): sha(p) for p in (rom, disk)}
    if hashes[str(rom)] != 'b776d5ade5ace0d09cb9f13a85ac07f6d492ab69fcac1b56995573f620f74e6c':
        raise ValueError('This probe requires the original TOS 2.06 UK ROM')
    for work in (nw, hw):
        shutil.copyfile(disk, work / 'diskA.st')
    # Ne pas hériter d'un commutateur expérimental d'une précédente investigation.
    env = {k: v for k, v in os.environ.items() if not k.startswith(('NEOST_', 'HATARI_'))}
    env.update(NEOST_TRACE_CYC='1', HATARI_SEED='4', HATARI_TEST='1',
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    commands = []

    def run(cmd, work, log):
        commands.append(cmd)
        with (work / log).open('w') as f:
            subprocess.run(cmd, cwd=work, env=env, stdout=f, stderr=subprocess.STDOUT,
                           check=True, timeout=180)

    start, end = dict(reset=('e00030', 'e000a8'),
                      preauto=(None, 'e01240'), auto=('e01240', 'e012b0'))[args.phase]
    neo = [str(HEADLESS), str(rom), '--machine', 'ste', '--mem', '1m',
           '--disk', str(nw / 'diskA.st'), '--disk-ro', '--fdc-seed', '4']
    keys = ['--key-hold', '8', '--scancode-at', '100', '39', '--scancode-at', '300', '39']
    if args.phase == 'auto':
        state = nw / 'entry.state'
        run(neo + keys + ['--frames', '800', '--break', start, '--save-state', str(state)], nw, 'boot.log')
        if f'BREAK ${start.upper()}' not in (nw / 'boot.log').read_text():
            raise RuntimeError('NeoST did not reach the AUTO entry breakpoint')
        # Les deux appuis sont déjà terminés avant cette entrée (>308 trames).
        neo += ['--load-state', str(state)]
    else:
        neo += keys
    if args.phase == 'preauto':
        neo += ['--trace-from', '340']
    run(neo + ['--frames', '4' if args.phase == 'reset' else '800', '--break', end,
               '--trace', str(nw / 'cpu.trace'), '--regs', '--irq'], nw, 'trace.log')

    # Neutraliser les extensions susceptibles de venir du fichier utilisateur.
    config = hw / 'hatari.cfg'
    config.write_text('[System]\nbPatchTimerD = FALSE\n'
                      '[Memory]\nbAutoSave = FALSE\n'
                      '[ROM]\nszCartridgeImageFileName =\n'
                      '[Floppy]\nbAutoInsertDiskB = FALSE\nszDiskBFileName =\n'
                      'EnableDriveA = TRUE\nEnableDriveB = TRUE\n'
                      'DriveA_NumberOfHeads = 2\nDriveB_NumberOfHeads = 2\n'
                      '[HardDisk]\nbUseHardDiskDirectory = FALSE\nbBootFromHardDisk = FALSE\n'
                      + ''.join(f'[{section}]\n' + ''.join(f'bUseDevice{i} = FALSE\n' for i in range(n))
                                for section, n in (('ACSI', 8), ('SCSI', 8), ('IDE', 2))))
    trigger = 'VBL = 340' if args.phase == 'preauto' else f'pc = ${start}'
    (hw / 'start.ini').write_text(f'b {trigger} :once :trace :file {hw}/begin.ini\n')
    (hw / 'begin.ini').write_text('trace cpu_disasm,cpu_regs,cpu_video_cycles,cpu_exception,mfp_read,mfp_write\n'
                                 f'b pc = ${end} :once :trace :file {hw}/end.ini\n')
    (hw / 'end.ini').write_text('trace none\nquit\n')
    if args.phase == 'reset':
        # Le débogueur teste ses breakpoints après la première instruction ;
        # armer une condition sur le PC initial manquerait toute cette fenêtre.
        (hw / 'start.ini').write_text((hw / 'begin.ini').read_text())
    hat = [str(HATARI), '--configfile', str(config), '--machine', 'ste', '--tos', str(rom),
           '--monitor', 'rgb', '--memsize', '1', '--cpuclock', '8', '--cpu-exact', 'on',
           '--compatible', 'on', '--vdi', 'off', '--disk-a', str(hw / 'diskA.st'),
           '--protect-floppy', 'off', '--patch-tos', 'off', '--fast-boot', 'off', '--fastfdc', 'off',
           '--sound', '25033', '--fast-forward', 'on', '--confirm-quit', 'off', '--statusbar', 'off',
           '--drive-led', 'off', '--frameskips', '0', '--alert-level', 'fatal']
    # Fige aussi la configuration effective héritée, pour permettre sa revue.
    run(hat + ['--saveconfig'], hw, 'config.log')
    hat += ['--run-vbls', '4' if args.phase == 'reset' else '800', '--parse', str(hw / 'start.ini'),
            '--trace-file', str(hw / 'cpu.trace')]
    commands.append(hat + ['--cmd-fifo', str(hw / 'cmd.fifo')])
    run_hatari_keys(hat, [(100, 57), (300, 57)], hw, env, 8)
    traces = [nw / 'cpu.trace', hw / 'cpu.trace']
    parsed = [trace_diff.parse(str(p))[0] for p in traces]
    if any(not rows or (start is not None and rows[0].pc != int(start, 16)) for rows in parsed):
        raise RuntimeError('Missing trace or wrong entry PC')
    for regs in (False, True):
        cmd = [sys.executable, str(ROOT / 'tools/trace_diff.py'), *map(str, traces), '-C', '6']
        if regs:
            cmd += ['--regs']
        result = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if result.returncode not in (0, 1):
            raise RuntimeError(result.stderr)
        (out / ('registers.txt' if regs else 'flow.txt')).write_text(result.stdout + result.stderr)
    for path, digest in hashes.items():
        if sha(Path(path)) != digest:
            raise RuntimeError(f'Source media changed: {path}')
    # Occurrences brutes : une même adresse dans une boucle n'est pas un
    # alignement d'état. Conserver A0 et A7 pour distinguer ses passages.
    checkpoints = {}
    for name, rows in zip(('neost', 'hatari'), parsed):
        checkpoints[name] = {f'{pc:06x}': [
            dict(cycle=ins.cyc, registers=ins.regs) for ins in rows if ins.pc == pc]
            for pc in (0xe00c06, 0xe03662, 0xe01730, 0xe0140e, 0xe01444, 0xe01240)}
    (out / 'checkpoints.json').write_text(json.dumps(checkpoints, indent=2) + '\n')
    report = dict(phase=args.phase, media_sha256=hashes, commands=commands,
                  environment={k: env[k] for k in ('NEOST_TRACE_CYC', 'HATARI_SEED', 'HATARI_TEST',
                                                   'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER')},
                  keys=dict(scancode=57, frames=[100, 300], hold_frames=8),
                  hatari_revision=subprocess.check_output(['git', '-C', str(ROOT / 'extern/hatari'),
                                                           'rev-parse', 'HEAD'], text=True).strip(),
                  hatari_source_diff_sha256=sha(hw / 'source.patch'),
                  binaries_sha256={str(p): sha(p) for p in (HEADLESS, HATARI)},
                  config_sha256=sha(config), instructions=[len(rows) for rows in parsed],
                  halted=dict(neost='CPU halted' in (nw / 'trace.log').read_text(),
                              hatari='CPU halted' in (hw / 'hatari.log').read_text()),
                  limits=['Identical seeds do not imply identical disk angles.',
                          'Keyboard events use each emulator frame convention, not matched pin cycles.',
                          'Hatari agreement is not hardware validation.'])
    (out / 'capture.json').write_text(json.dumps(report, indent=2) + '\n')
    print((out / 'registers.txt').read_text())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
