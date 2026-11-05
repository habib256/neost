#!/usr/bin/env python3
"""Contre-épreuves du témoin VBL, sans modifier les sources tierces ni NeoST.

Reconstruit quatre témoins : inchangé, VSYNC asynchrone, verrou natif, les deux.
Relève les deux VSYNC et les deux étages IPL de fx68k, puis varie le reset
et la phase E-clock séparément. Aucun score ne désigne le matériel fidèle.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import build_stcpu_bench as b
from run_vbl_entry_probe import program, TABLE, SIZE

ROOT = Path(__file__).resolve().parent.parent


def replace_once(s, old, new):
    if s.count(old) != 1:
        raise ValueError(f'Unexpected source: {old}')
    return s.replace(old, new)


def execute(cmd, cwd, log, env):
    with log.open('w') as f:
        subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, stdout=f,
                       stderr=subprocess.STDOUT, check=True, timeout=240)


def pcs(path):
    data = path.read_bytes()
    if len(data) != SIZE:
        raise ValueError(f'Incomplete table: {path}')
    values = [int.from_bytes(data[i:i+4], 'big') for i in range(0, SIZE, 4)]
    if any(v < 0xe00030 or v >= 0xe00100 or v & 1 for v in values):
        raise ValueError(f'Invalid table: {path}')
    return values


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=ROOT / 'build/vbl-witness-audit')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('NEOST_', 'HATARI_', 'AUDIT_'))}
    image = out / 'probe.img'
    image.write_bytes(program())
    top = (ROOT / 'tools/stcpu_bench_top.v').read_text()
    top = replace_once(top, 'output [3:0] R,', 'output [8:0] audit, output [3:0] R,')
    top = replace_once(top, '    wire uds_n,', '''    assign audit = {tb.gstmcu.iivsync_a, tb.gstmcu.iivsync,
                         ipl2_n, cpu.rIpl, cpu.iIpl};
    wire uds_n,''')
    driver = (ROOT / 'tools/stcpu_bench.cpp').read_text()
    reset_edge = '    t->clk32 = 0; t->porb = 1; t->resb = 1; t->pwrUp = 1; t->extReset = 1; t->eval();\n    t->porb = 0; t->resb = 0; t->eval();'
    driver = replace_once(driver, reset_edge,
                          '    if (!std::getenv("AUDIT_LEGACY_RESET")) {\n' + reset_edge + '\n    }')
    driver = replace_once(driver, '    t->porb = 1;', '''    t->porb = 1;
    const int extra = std::getenv("AUDIT_RESET_EXTRA") ? std::atoi(std::getenv("AUDIT_RESET_EXTRA")) : 0;
    for (int i = 0; i < extra; ++i) { tick(1); tick(0); }''')
    driver = replace_once(driver, '    while (seen < frames', '''    FILE* audit = std::fopen("signals.txt", "w");
    unsigned oldAudit = ~0u;
    while (seen < frames''')
    driver = replace_once(driver, '        ++ticks;', '''        ++ticks;
        if (audit && t->audit != oldAudit) {
            std::fprintf(audit, "%ld %d %ld %03x\\n", guard, seen, ticks, unsigned(t->audit));
            oldAudit = t->audit;
        }''')
    driver = replace_once(driver, '    delete t;\n    return 0;', '    if (audit) std::fclose(audit);\n    delete t;\n    return 0;')
    commands = {}

    def build_variant(name, async_sync, native):
        work = out / name
        work.mkdir(exist_ok=True)
        s = (b.G / 'hdl/gstmcu.v').read_text()
        old = "register vintb_r(clk32, ~(resb & porb & vclrb), 1'b0, iivsync, 1'b0, vintb);"
        new = old.replace('iivsync,', 'iivsync_a,') if async_sync else old
        if native:
            new = new.replace('register vintb_r', 'audit_register vintb_r')
        s = replace_once(s, old, new)
        if native:
            s += '''\nmodule audit_register(input clock,s,r,c,d,output reg q);
always @(posedge c or posedge s or posedge r) begin
 if(r) q <= 0; else if(s) q <= 1; else q <= d;
end
endmodule
'''
        (work / 'gstmcu.v').write_text(s)
        (work / 'top.v').write_text(top)
        (work / 'driver.cpp').write_text(driver)
        cmd = ['verilator', '--cc', '--exe', '--build', '-j', '2', '--no-timing', '--no-assert-case',
               '-Wno-fatal', '-Wno-lint', '-Wno-style', '-CFLAGS', '-O2', '--Mdir', work / 'obj',
               '--top-module', 'stcpu_bench_top', '-o', work / 'model', work / 'driver.cpp',
               work / 'top.v', b.G / 'tb/ste_tb.v',
               *[work / 'gstmcu.v' if x == 'gstmcu' else b.G / 'hdl' / f'{x}.v' for x in b.HDL],
               *[b.FX / x for x in ('fx68k.sv', 'fx68kAlu.sv', 'uaddrPla.sv')], '-y', b.G / 'tb']
        commands[name] = dict(build=list(map(str, cmd)))
        execute(cmd, ROOT, work / 'build.log', env)
        for f in ('microrom.mem', 'nanorom.mem'):
            shutil.copy(b.FX / f, work / f)
        cmd = [work / 'model', image, '22', f'{TABLE:x}', str(SIZE), work / 'pcs.bin']
        commands[name]['run'] = list(map(str, cmd))
        execute(cmd, work, work / 'run.log', env)
        return name, pcs(work / 'pcs.bin')

    with ThreadPoolExecutor(2) as pool:
        results = dict(pool.map(lambda x: build_variant(*x), [
            ('baseline', False, False), ('async_vsync', True, False),
            ('native_latch', False, True), ('both', True, True)]))
    # Reset et E-clock : sensibilités distinctes, aucune recherche de meilleur score.
    for extra in (1, 2, 3):
        work = out / f'reset_plus{extra}'
        work.mkdir(exist_ok=True)
        for f in ('microrom.mem', 'nanorom.mem'):
            shutil.copy(b.FX / f, work / f)
        cmd = [out / 'baseline/model', image, '22', f'{TABLE:x}', str(SIZE), work / 'pcs.bin']
        execute(cmd, work, work / 'run.log', dict(env, AUDIT_RESET_EXTRA=str(extra)))
        commands[work.name] = list(map(str, cmd))
        results[work.name] = pcs(work / 'pcs.bin')
    for variant in ('baseline', 'async_vsync'):
        work = out / (variant + '_legacy_reset')
        work.mkdir(exist_ok=True)
        for f in ('microrom.mem', 'nanorom.mem'):
            shutil.copy(b.FX / f, work / f)
        cmd = [out / variant / 'model', image, '22', f'{TABLE:x}', str(SIZE), work / 'pcs.bin']
        execute(cmd, work, work / 'run.log', dict(env, AUDIT_LEGACY_RESET='1'))
        commands[work.name] = list(map(str, cmd))
        results[work.name] = pcs(work / 'pcs.bin')
    for phase in (0, 2, 4, 6, 8):
        work = out / f'neost_phase{phase}'
        work.mkdir(exist_ok=True)
        cmd = [ROOT / 'build/neost-headless', image, '--machine', 'ste', '--mem', '4m',
               '--frames', '23', '--dump-at', '21', f'{TABLE:x}', str(SIZE), work / 'pcs.bin']
        execute(cmd, ROOT, work / 'run.log', dict(env, NEOST_ECLOCK_PHASE=str(phase)))
        commands[work.name] = list(map(str, cmd))
        results[work.name] = pcs(work / 'pcs.bin')
    differences = {k: [i for i, (a, c) in enumerate(zip(results['baseline'], v)) if a != c]
                   for k, v in results.items()}
    sources = [Path(__file__).resolve(), ROOT / 'tools/run_vbl_entry_probe.py',
               ROOT / 'tools/make_stcpu_rom.py', ROOT / 'tools/build_stcpu_bench.py',
               ROOT / 'tools/stcpu_bench_top.v', ROOT / 'tools/stcpu_bench.cpp',
               *b.G.glob('hdl/*.v'), *b.G.glob('tb/*.v'), *b.FX.glob('*.sv'), *b.FX.glob('*.mem')]
    artifacts = [image, ROOT / 'build/neost-headless', *out.glob('*/model'),
                 *out.glob('*/gstmcu.v'), *out.glob('*/top.v'), *out.glob('*/driver.cpp')]
    report = dict(pcs=results, differences_from_baseline=differences, build_and_run_commands=commands,
                  source_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                  artifact_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts},
                  environments=dict(reset_plus={str(n): dict(AUDIT_RESET_EXTRA=str(n)) for n in (1,2,3)},
                                    legacy_reset=dict(AUDIT_LEGACY_RESET='1'),
                                    neost_phase={str(n): dict(NEOST_ECLOCK_PHASE=str(n)) for n in (0,2,4,6,8)}),
                  revisions={name: subprocess.check_output(['git', '-C', str(p), 'rev-parse', 'HEAD'], text=True).strip()
                             for name, p in [('fx68k', b.FX), ('gstmcu', b.G)]},
                  limits=['Alternative VSYNC only drives the VBL latch; frame counting remains unchanged.',
                          'These variants measure model sensitivity, not hardware accuracy.',
                          'signals.txt: absolute clk32 tick, frame, frame tick, hex bits [8]=async VSYNC, [7]=sync VSYNC, [6]=IPL2_N, [5:3]=rIpl, [2:0]=iIpl.'])
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(differences, indent=2))


if __name__ == '__main__':
    main()
