#!/usr/bin/env python3
# =============================================================================
#  run_gemdos_ab.py — banc A/B du GEMDOS hôte : disquette contre dossier.
#
#  Le MÊME programme 68000 (tools/gemdos_ab/, un appel GEMDOS par entrée de table,
#  D0 et un vidage hexadécimal sur le port série) tourne deux fois sous le même TOS :
#  depuis une disquette .st (le vrai GEMDOS de la ROM fait foi, échelon `rom`, grade C)
#  puis depuis un dossier --gemdos (NeoST intercepte). La différence des journaux est
#  l'écart. Trois programmes :
#    T.PRG  fichiers, dossiers, DTA, Fdatime, attributs, Fforce, Pexec (héritage du
#           dossier courant, enfant qui change de lecteur, Ptermres) ;
#    R.PRG  « X:NOM » et « X: » relatifs au dossier courant de X, Pexec refusé ;
#    H.PRG  noms hôtes longs (dossier seul : une disquette n'en a pas) — affiché.
#  Plus un lanceur AUTO (L.PRG) qui enchaîne des Pexec 0 depuis C: et depuis A: :
#  Malloc(-1) doit rester le même (TOS 1.00/1.02 n'ont pas Pexec 6).
#
#  Écarts attendus, propres au montage et non à NeoST : lecteur de boot (A: contre
#  C:), carte des lecteurs, Dfree, ordre des entrées (création sur FAT, alphabétique
#  sur l'hôte), étiquette de volume (convention d'Hatari), octets après le NUL d'un nom.
#
#  Usage : run_gemdos_ab.py [--build build] [--out DIR] [--verbose] [rom…]
#  (ROM par défaut : tos104us, tos206us, etos192us ; TOS Atari = médias privés)
# =============================================================================
import argparse, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'gemdos_ab'))
import gen  # noqa: E402

REG = {}
_enc = gen.enc_table
def _rec(d, ts):
    for t in ts: REG.setdefault(t['id'], []).append((t['fn'], t.get('dump')))
    return _enc(d, ts)
gen.enc_table = _rec
import cases_main, cases_host  # noqa: E402
from gen import floppy  # noqa: E402


def parse(path):
    d, order = {}, []
    try:
        lines = open(path, errors='replace').read().split('\n')
    except FileNotFoundError:
        lines = []
    for line in lines:
        p = line.split(' ', 1)
        if len(p) < 2: continue
        k, v = p[0], p[1].strip()
        while k in d: k += "'"
        d[k] = v; order.append(k)
    return d, order


# Handles (6… sur TOS, 64… ici) et heure/date des entrées listées (horloge hôte) ne
# sont pas des écarts. Les dates POSÉES par Fdatime restent comparées.
def norm(reg, k, v):
    tid = int(k.rstrip("'"), 16)
    parts = v.split(' ')
    ent = reg.get(tid, [])
    fns = {e[0] for e in ent}
    dumps = [e[1] for e in ent if e[1]]
    if fns & {0x3C, 0x3D, 0x45} and int(parts[0], 16) < 0x80000000:
        parts[0] = 'H'
    if len(parts) > 1 and any(dd[0] == 'a' and dd[1].startswith('dta') and dd[2] == 21 and dd[3] > 1
                              for dd in dumps):
        h = parts[1]
        if len(h) >= 10: parts[1] = h[:2] + 'tttttttt' + h[10:]
    return ' '.join(parts)


def compare(reg, a, b, normalize):
    da, oa = parse(a); db, ob = parse(b)
    keys = []
    for k in oa + ob:
        if k not in keys: keys.append(k)
    out = []
    for k in keys:
        va, vb = da.get(k, '-'), db.get(k, '-')
        if normalize and va != '-' and vb != '-':
            va, vb = norm(reg, k, va), norm(reg, k, vb)
        if va != vb: out.append((k, va, vb))
    return out


def main():
    ap = argparse.ArgumentParser(description='GEMDOS A/B bench: floppy (real GEMDOS) vs --gemdos folder')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--out', default=None)
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('roms', nargs='*', default=['tos104us.img', 'tos206us.img', 'etos192us.img'])
    args = ap.parse_args()
    hl = os.path.join(args.build, 'neost-headless')
    out = os.path.abspath(args.out or tempfile.mkdtemp(prefix='neost-gemdos-ab-'))
    os.makedirs(out, exist_ok=True)

    REG.clear(); t_prg = cases_main.mk_main(); reg_main = dict(REG)
    REG.clear(); rc = cases_host.mk_rel('C'); ra = cases_host.mk_rel('A'); reg_rel = dict(REG)
    h_prg = cases_host.mk_host(); l_prg = cases_main.mk_leak('C:')

    jobs = []
    def job(tag, rom, setup, frames):
        w = os.path.join(out, tag)
        shutil.rmtree(w, ignore_errors=True); os.makedirs(os.path.join(w, 'G'))
        open(os.path.join(w, 'blank.st'), 'wb').write(floppy([]))
        cmd = [hl, os.path.join(ROOT, 'roms', rom), '--machine', 'st', '--mem', '1m'] + setup(w) + \
              ['--serial-dump', os.path.join(w, 'ser.txt'), '--frames', str(frames)]
        jobs.append((w, cmd))

    def put(path, data): open(path, 'wb').write(data)
    for rom in args.roms:
        t = rom.replace('.img', '')
        k = 4 if '206' in t else 1       # TOS 2.06 sur ST : ~7 000 trames avant le bureau
        job('f_' + t, rom, lambda w: (put(os.path.join(w, 'a.st'), floppy([('T.PRG', t_prg)])),
            ['--disk', os.path.join(w, 'a.st'), '--fastfdc', '--auto', 'A:\\T.PRG'])[1], 3000 * k)
        job('g_' + t, rom, lambda w: (put(os.path.join(w, 'G', 'T.PRG'), t_prg),
            ['--gemdos', os.path.join(w, 'G'), '--disk', os.path.join(w, 'blank.st'), '--auto', 'C:\\T.PRG'])[1], 3000 * k)
        job('ra_' + t, rom, lambda w: (put(os.path.join(w, 'a.st'), floppy([('R.PRG', ra)])),
            ['--disk', os.path.join(w, 'a.st'), '--fastfdc', '--auto', 'A:\\R.PRG'])[1], 2500 * k)
        job('rc_' + t, rom, lambda w: (put(os.path.join(w, 'G', 'R.PRG'), rc),
            ['--gemdos', os.path.join(w, 'G'), '--disk', os.path.join(w, 'blank.st'), '--auto', 'C:\\R.PRG'])[1], 2500 * k)

    def host_setup(w):
        g = os.path.join(w, 'G')
        put(os.path.join(g, 'H.PRG'), h_prg)
        os.makedirs(os.path.join(g, 'lowdir')); os.makedirs(os.path.join(g, 'LongDirectoryName', 'SubdirectoryWithLongName'))
        for n, c in (('LongFileName1.txt', b'ONE'), ('LongFileName2.txt', b'TWO'), ('my file.txt', b'sp'),
                     ('caf\u00e9.txt', b'acc'), ('archive.tar.gz', b'gz'), ('lowercase.txt', b'lc'), ('.hidden', b'x')):
            put(os.path.join(g, n), c)
        return ['--gemdos', g, '--disk', os.path.join(w, 'blank.st'), '--auto', 'C:\\H.PRG']
    job('h_tos104us', 'tos104us.img', host_setup, 3000)

    leak_roms = [r for r in ('tos100us.img', 'tos102us.img', 'tos104us.img')
                 if os.path.exists(os.path.join(ROOT, 'roms', r))]
    for rom in leak_roms:
        def leak_setup(w):
            put(os.path.join(w, 'G', 'T.PRG'), t_prg)
            put(os.path.join(w, 'a.st'), floppy([('T.PRG', t_prg)], [('L.PRG', l_prg)]))
            return ['--gemdos', os.path.join(w, 'G'), '--disk', os.path.join(w, 'a.st'), '--fastfdc']
        job('l_' + rom.replace('.img', ''), rom, leak_setup, 3000)

    procs = [(w, subprocess.Popen(cmd, stdout=open(os.path.join(w, 'log.txt'), 'w'), stderr=subprocess.STDOUT))
             for w, cmd in jobs]
    for w, p in procs:
        p.wait()
        try: data = open(os.path.join(w, 'ser.txt'), 'rb').read().decode('latin-1').replace('\r', '')
        except FileNotFoundError: data = ''
        open(os.path.join(w, 'res.txt'), 'w').write(data)

    total_raw = total = 0
    empty = []
    for rom in args.roms:
        t = rom.replace('.img', '')
        for a, b, reg in (('f_', 'g_', reg_main), ('ra_', 'rc_', reg_rel)):
            fa, fb = os.path.join(out, a + t, 'res.txt'), os.path.join(out, b + t, 'res.txt')
            for f in (fa, fb):
                if not parse(f)[1]: empty.append(f)
            raw = compare(reg, fa, fb, False); nd = compare(reg, fa, fb, True)
            total_raw += len(raw); total += len(nd)
            print(f'{a}{t} vs {b}{t}: raw {len(raw)}, normalized {len(nd)}')
            if args.verbose:
                for k, va, vb in nd: print(f'   {k}  floppy={va}  gemdos={vb}')
    print(f'TOTAL raw {total_raw}, normalized {total}')
    leak_bad = False
    for rom in leak_roms:
        d, order = parse(os.path.join(out, 'l_' + rom.replace('.img', ''), 'res.txt'))
        free = [d.get(k, '-') for k in ('0010', '0012', '0014', '0016', '0022', '0024', '0026')]
        kept_c = (int(d['0016'], 16) - int(d['0032'], 16)) if '0032' in d and '0016' in d else None
        kept_a = (int(d['0032'], 16) - int(d['0034'], 16)) if '0034' in d and '0032' in d else None
        same = len(set(free)) == 1 and '-' not in free
        leak_bad |= not same or kept_c != kept_a
        print(f'Pexec 0 leak {rom}: Malloc(-1) {" ".join(free)} ->',
              'constant' if same else 'LEAKS', f'| Ptermres kept C: {kept_c} A: {kept_a}')
    if args.verbose:
        print('host names (NeoST only):')
        print(open(os.path.join(out, 'h_tos104us', 'res.txt')).read())
    print(f'runs in {out}')
    if empty:
        print('NO OUTPUT (boot too slow or ROM missing):', *empty, sep='\n  ')
        return 1
    return 1 if leak_bad else 0


if __name__ == '__main__':
    sys.exit(main())
