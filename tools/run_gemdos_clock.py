#!/usr/bin/env python3
# =============================================================================
#  run_gemdos_clock.py — banc de l'horloge GEMDOS : date d'un fichier créé sur le
#  lecteur hôte (--gemdos, C:) contre celle que le vrai GEMDOS de la ROM pose sur la
#  disquette (A:), dans le MÊME run.
#
#  Un programme AUTO (lancé depuis A:\AUTO, donc aussi sous TOS 1.00/1.02 qui n'ont pas
#  d'--auto ; par --auto sous EmuTOS, qui démarre alors sur C:) encadre chaque création
#  par Tgetdate/Tgettime :
#    · CLK.TXT créé sur C: puis sur A: à l'heure du boot ;
#    · Tsetdate 2003-07-14 / Tsettime 10:20:30 par l'invité, puis SET.TXT sur C: et A: ;
#  puis relit les deux entrées par Fsfirst (DTA : heure, date). La disquette fait foi
#  (échelon `rom`, grade C : c'est le GEMDOS de la ROM qui écrit l'entrée FAT).
#
#  Verdict par création : date de C: = date de A:, heure de C: et de A: dans
#  l'encadrement Tgettime avant/après (pas de 2 s du format), écart C:/A: ≤ 2 pas
#  (le temps d'écrire la disquette). Le dossier créé (Dcreate) est affiché, non jugé.
#
#  Hors palier : les TOS Atari sont des médias privés (roms/, gitignoré). Les ROM
#  absentes sont sautées ; EmuTOS (libre) suit la puce d'horloge.
#  Usage : run_gemdos_clock.py [--build build] [--out DIR] [--verbose] [rom:machine…]
# =============================================================================
import argparse, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'gemdos_ab'))
from gen import Data, Prog, build, floppy, A, W  # noqa: E402

DEFAULT = ['tos100us:st', 'tos102us:st', 'tos102megafr:megast', 'tos104us:st', 'tos104us:megast',
           'tos106us:st', 'tos106us:ste', 'tos162us:st', 'tos162us:ste', 'tos205us:megaste',
           'tos206us:st', 'tos206us:ste', 'tos206us:megaste',
           'etos192us:st', 'etos256us:ste', 'etos256us:megaste']
ALIAS = {'tos102megafr': 'TOS v1.02 (1987)(Atari)(Mega ST)(FR)[MEGA TOS]'}

SET_DATE = ((2003 - 1980) << 9) | (7 << 5) | 14
SET_TIME = (10 << 11) | (20 << 5) | (30 >> 1)


def mk_prog():
    d = Data()
    for n, s in (('cclk', 'C:\\CLK.TXT'), ('aclk', 'A:\\CLK.TXT'), ('cset', 'C:\\SET.TXT'),
                 ('aset', 'A:\\SET.TXT'), ('cdir', 'C:\\CLKD'), ('adir', 'A:\\CLKD')):
        d.s(n, s)
    d.b('dta', 44)
    T = []
    def t(id, fn, args, **kw):
        kw.setdefault('save', 0xFFFF); T.append(dict(id=id, fn=fn, args=args, **kw))
    DT = ('a', 'dta', 22, 4)                         # d_time, d_date
    t(0x10, 0x1A, [A('dta')])                        # Fsetdta
    def creation(base, c, a):
        t(base + 0, 0x2A, []); t(base + 1, 0x2C, [])            # Tgetdate, Tgettime
        t(base + 2, 0x3C, [A(c), W(0)], save=1); t(base + 3, 0x3E, [('sw', 1)])
        t(base + 4, 0x3C, [A(a), W(0)], save=1); t(base + 5, 0x3E, [('sw', 1)])
        t(base + 6, 0x2A, []); t(base + 7, 0x2C, [])
        t(base + 8, 0x4E, [A(c), W(0)], dump=DT); t(base + 9, 0x4E, [A(a), W(0)], dump=DT)
    creation(0x20, 'cclk', 'aclk')
    t(0x30, 0x2B, [W(SET_DATE)]); t(0x31, 0x2D, [W(SET_TIME)])
    creation(0x40, 'cset', 'aset')
    t(0x50, 0x39, [A('cdir')]); t(0x51, 0x39, [A('adir')])
    t(0x52, 0x4E, [A('cdir'), W(0x10)], dump=DT); t(0x53, 0x4E, [A('adir'), W(0x10)], dump=DT)
    p = Prog(d); p.table('main', T)
    return build(p)


def fmt(time, date):
    return '%04d-%02d-%02d %02d:%02d:%02d' % ((date >> 9) + 1980, (date >> 5) & 15, date & 31,
                                              time >> 11, (time >> 5) & 63, (time & 31) * 2)


def secs(time):
    return (time >> 11) * 3600 + ((time >> 5) & 63) * 60 + (time & 31) * 2


def parse(path):
    out = {}
    try: text = open(path, 'rb').read().decode('latin-1').replace('\r', '')
    except FileNotFoundError: return out
    for line in text.split('\n'):
        p = line.split()
        if len(p) >= 2:
            try: out[int(p[0], 16)] = [int(x, 16) for x in p[1:]]
            except ValueError: pass
    return out


def judge(r, base, label):
    """Renvoie (ok, texte) pour la création encadrée en base..base+9."""
    need = [base + i for i in (0, 1, 6, 7, 8, 9)]
    if any(k not in r for k in need): return False, f'{label}: missing output'
    if r[base + 8][0] != 0 or r[base + 9][0] != 0: return False, f'{label}: Fsfirst failed'
    d0, t0, d1, t1 = r[base][0] & 0xFFFF, r[base + 1][0] & 0xFFFF, r[base + 6][0] & 0xFFFF, r[base + 7][0] & 0xFFFF
    def ent(k):
        b = r[k][1]
        return (b >> 24) << 8 | ((b >> 16) & 0xFF), ((b >> 8) & 0xFF) << 8 | (b & 0xFF)
    (ct, cd), (at, ad) = ent(base + 8), ent(base + 9)
    inside = lambda tm, dt: dt in (d0, d1) and (d0 != d1 or secs(t0) <= secs(tm) <= secs(t1))
    ok = cd == ad and abs(secs(ct) - secs(at)) <= 4 and inside(ct, cd) and inside(at, ad)
    txt = f'{label}: C: {fmt(ct, cd)}  A: {fmt(at, ad)}  (Tget {fmt(t0, d0)} .. {fmt(t1, d1)})'
    return ok, txt


def main():
    ap = argparse.ArgumentParser(description='GEMDOS clock bench: date of a file created on '
                                 'the host drive (C:) vs the floppy (A:), same run')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('runs', nargs='*', default=DEFAULT, help='rom:machine (rom without .img)')
    args = ap.parse_args()
    hl = os.path.join(args.build, 'neost-headless')
    out = os.path.abspath(args.out or tempfile.mkdtemp(prefix='neost-gemdos-clock-'))
    os.makedirs(out, exist_ok=True)
    prg = mk_prog()
    jobs, skipped = [], []
    for spec in args.runs:
        rom, mach = spec.split(':')
        img = os.path.join(ROOT, 'roms', ALIAS.get(rom, rom) + '.img')
        if not os.path.exists(img): skipped.append(spec); continue
        w = os.path.join(out, f'{rom}_{mach}')
        shutil.rmtree(w, ignore_errors=True); os.makedirs(os.path.join(w, 'G'))
        # EmuTOS démarre sur C: quand le lecteur hôte existe : A:\AUTO n'y est pas lu,
        # le programme part alors par --auto (même disquette, même création sur A:).
        auto = rom.startswith('etos')
        open(os.path.join(w, 'a.st'), 'wb').write(
            floppy([('CLK.PRG', prg)]) if auto else floppy([], [('CLK.PRG', prg)]))
        frames = 9000 if '20' in rom and 'etos' not in rom else 3000   # TOS 2.0x : boot long
        cmd = [hl, img, '--machine', mach, '--mem', '1m', '--gemdos', os.path.join(w, 'G'),
               '--disk', os.path.join(w, 'a.st'), '--fastfdc',
               '--serial-dump', os.path.join(w, 'ser.txt'), '--frames', str(frames)]
        if auto: cmd += ['--auto', 'A:\\CLK.PRG']
        jobs.append((spec, w, subprocess.Popen(cmd, stdout=open(os.path.join(w, 'log.txt'), 'w'),
                                               stderr=subprocess.STDOUT)))
    bad = 0
    for spec, w, p in jobs:
        p.wait()
        r = parse(os.path.join(w, 'ser.txt'))
        res = [judge(r, 0x20, 'boot'), judge(r, 0x40, 'Tset')]
        ok = all(x[0] for x in res)
        bad += not ok
        print(f'{"PASS" if ok else "FAIL"} {spec}')
        for good, txt in res:
            if args.verbose or not good: print('   ', txt)
        if args.verbose and 0x52 in r and 0x53 in r and len(r[0x52]) > 1 and len(r[0x53]) > 1:
            def e(b): return fmt((b >> 24) << 8 | ((b >> 16) & 0xFF), ((b >> 8) & 0xFF) << 8 | (b & 0xFF))
            print(f'    Dcreate (not judged): C: {e(r[0x52][1])}  A: {e(r[0x53][1])}')
    for s in skipped: print(f'SKIP {s} (ROM missing)')
    print(f'{len(jobs) - bad}/{len(jobs)} PASS, runs in {out}')
    return 1 if bad or not jobs else 0


if __name__ == '__main__':
    sys.exit(main())
