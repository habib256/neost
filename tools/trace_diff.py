#!/usr/bin/env python3
# =============================================================================
#  trace_diff.py — Aligne et compare une trace NeoST et une trace Hatari.
#
#  Hatari est une référence comparative, pas une preuve matérielle. Cet outil prend
#  deux traces d'exécution du MÊME ROM/disquette, les aligne sur un PC commun,
#  puis localise la PREMIÈRE divergence — soit du flux (PC différent), soit d'un
#  registre (PC identique mais D0-D7/A0-A7/SR qui divergent en amont du branch).
#
#  Générer les traces :
#    NeoST  : ./build/neost-headless --frames N --trace neost.txt --regs --irq
#    Hatari : hatari --trace cpu_disasm --trace-file hatari.txt \
#                    --tos roms/etos192us.img --disk-a disks/diskA.st <...>
#             (ajouter cpu_regs au --trace pour comparer aussi les registres)
#
#  Exemples :
#    tools/trace_diff.py neost.txt hatari.txt
#    tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs -C 8
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import re
import sys

# --- Repérage d'une ligne d'instruction selon le format ----------------------
#   NeoST  : "FC0030: bra     $fc004e [ D0=.. .. SR=2704]"   (adresse + ':')
#   NeoST  : "cyc=12345 FC0030: bra ..."                     (NEOST_TRACE_CYC=1)
#   Hatari : "00fc0030 46fc 2700      move.w #$2700,sr"      (cpu_disasm, octets)
#   Hatari : "cpu video_cyc=12 0@0 12345 : 00fc0030 46fc ..."(cpu_video_cycles)
#   Hatari : "$00fc0030 : 601c  bra.s $fc004e"               (ancien format ':')
# Le préfixe « cyc=N » (NeoST) / « cpu video_cyc=… GCLK : » (Hatari) est OPTIONNEL :
# capté pour le harnais de timing différentiel (--periods), sinon ignoré.
RE_NEOST  = re.compile(r'^(?:cyc=(\d+)\s+)?([0-9A-Fa-f]{5,8}):\s+(.*)$')   # [cyc] adr ':'
RE_HATARI = re.compile(
    r'^(?:cpu video_cyc=\s*\d+\s+\d+@\s*\d+\s+(\d+)\s*:\s*)?'              # [video_cyc … GCLK :]
    r'([0-9A-Fa-f]{6,8})\s+([0-9A-Fa-f]{2,4}\s.*)$')                       # adresse + octets

# Registres, tolérant aux deux notations ("D0=12345678" ou "D0 12345678").
# La largeur évite de lire « D0 D0=... » comme un registre valant $D0.
RE_REG = re.compile(r'\b([DA][0-7])\s*[= ]\s*([0-9A-Fa-f]{8})\b')
RE_SR  = re.compile(r'\bSR\s*[= ]\s*([0-9A-Fa-f]{4})\b')


class Insn:
    """Une instruction : PC, texte brut (1re ligne), registres et cycle éventuels.
    `cyc` = horloge ABSOLUE (NeoST Scheduler liveNow / Hatari CyclesGlobalClockCounter)
    si la trace la porte, sinon None."""
    __slots__ = ('pc', 'text', 'regs', 'cyc')

    def __init__(self, pc, text, cyc=None):
        self.pc = pc
        self.text = text
        self.regs = {}
        self.cyc = cyc

    def parse_regs(self, blob):
        for name, val in RE_REG.findall(blob):
            self.regs[name] = int(val, 16)
        m = RE_SR.search(blob)
        if m:
            self.regs['SR'] = int(m.group(1), 16)


def detect_format(lines):
    """Devine le format (neost/hatari) en regardant les premières lignes."""
    for ln in lines[:200]:
        if RE_NEOST.match(ln):       # NeoST d'abord ('adresse:' sans ambiguïté)
            return 'neost'
        if RE_HATARI.match(ln):
            return 'hatari'
    return None


def parse(path, fmt=None):
    """Lit un fichier de trace → liste d'Insn avec état avant exécution.
    Hatari a un lecteur distinct : ses dumps suivent l'exécution précédente.
    """
    with open(path, 'r', errors='replace') as f:
        lines = f.read().splitlines()
    if fmt is None:
        fmt = detect_format(lines)
        if fmt is None:
            sys.exit(f"[trace_diff] format non reconnu : {path}")
    if fmt == 'hatari':
        return parse_hatari(lines), fmt
    rx = RE_NEOST

    insns, cur, blob = [], None, []
    def flush():
        if cur is not None:
            cur.parse_regs(' '.join(blob))
    for ln in lines:
        m = rx.match(ln)
        if m:
            flush()
            cyc = int(m.group(1)) if m.group(1) is not None else None
            cur = Insn(int(m.group(2), 16), ln.rstrip(), cyc)
            insns.append(cur)
            blob = [m.group(3)]             # texte = désasm (groupe 3, cycle = 1, PC = 2)
        elif cur is not None:
            blob.append(ln)                 # continuation (regs, flags, IRQ…)
    flush()
    return insns, fmt


def parse_hatari(lines):
    """Sépare les instructions exécutées des aperçus du dump de registres.

    [D] Hatari debug/debugcpu.c:1389 et cpu/newcpu.c:948 @ f0736b24 :
    cpu_disasm précède l'exécution, cpu_regs suit l'instruction et désassemble
    le PC suivant. Ce dump décrit donc l'entrée de l'instruction suivante,
    pas celle qui vient d'être exécutée. Le compter deux fois crée un faux
    écart de flux ; rattacher ses registres au PC précédent en crée un autre.
    """
    events, dump, preview = [], None, None
    for line in lines:
        if re.match(r'^\s+D0\s+[0-9A-Fa-f]{8}\b', line):
            dump, preview = [line], None
            continue
        if dump is not None:
            if line.startswith('Next PC:'):
                if preview is not None:
                    preview.parse_regs(' '.join(dump))
                    events.append(('dump', preview))
                dump, preview = None, None
            else:
                match = RE_HATARI.match(line)
                if match:
                    preview = Insn(int(match.group(2), 16), line.rstrip())
                else:
                    dump.append(line)
            continue
        match = RE_HATARI.match(line)
        if match:
            cycle = int(match.group(1)) if match.group(1) is not None else None
            events.append(('exec', Insn(int(match.group(2), 16), line.rstrip(), cycle)))
    if not any(kind == 'exec' for kind, _ in events):
        return [ins for _, ins in events]  # cpu_regs seul
    result, pending = [], None
    for kind, ins in events:
        if kind == 'dump':
            pending = ins
        else:
            if pending is not None and pending.pc == ins.pc:
                ins.regs = pending.regs
            pending = None
            result.append(ins)
    return result


def trim_to_pc(insns, pc):
    """Tronque la liste pour démarrer à la première occurrence de pc."""
    for i, ins in enumerate(insns):
        if ins.pc == pc:
            return insns[i:]
    return []


def cycle_periods(insns, pc):
    """Deltas de l'horloge absolue entre passages CONSÉCUTIFS au même PC.
    = cycles par itération d'une boucle dont `pc` est un point fixe (p.ex. la
    boucle de poll, ou le handler HBL revu une fois par scanline). Nécessite une
    trace avec colonne cycle (NEOST_TRACE_CYC=1 / Hatari cpu_video_cycles)."""
    last, deltas = None, []
    for ins in insns:
        if ins.pc == pc and ins.cyc is not None:
            if last is not None:
                deltas.append(ins.cyc - last)
            last = ins.cyc
    return deltas


def histo(deltas, top=4):
    import collections
    return collections.Counter(deltas).most_common(top)


def report_periods(a, b, pcs, col):
    """Harnais de timing DIFFÉRENTIEL : compare, pour chaque PC-repère, la période
    (cycles/itération) côté Moira(NeoST) vs WinUAE(Hatari). La période DOMINANTE
    (mode) est robuste aux IRQ ponctuelles qui détournent la boucle."""
    if not any(ins.cyc is not None for ins in a[:5000]):
        sys.exit("[trace_diff] trace NeoST sans colonne cycle : relancer avec "
                 "NEOST_TRACE_CYC=1 ./build/neost-headless … --trace …")
    if not any(ins.cyc is not None for ins in b[:5000]):
        sys.exit("[trace_diff] trace Hatari sans colonne cycle : ajouter "
                 "cpu_video_cycles au --trace (ex. --trace cpu_disasm,cpu_video_cycles).")
    print(f"\n{'PC':>8} | {'Moira (NeoST) cyc/itér':>26} | "
          f"{'WinUAE (Hatari) cyc/itér':>26} | verdict")
    print('-' * 88)
    rc = 0
    for pc_hex in pcs:
        pc = int(pc_hex, 16)
        na, hb = histo(cycle_periods(a, pc)), histo(cycle_periods(b, pc))
        nmode = na[0][0] if na else None
        hmode = hb[0][0] if hb else None
        nstr = ' '.join(f"{d}×{n}" for d, n in na[:3]) or '—'
        hstr = ' '.join(f"{d}×{n}" for d, n in hb[:3]) or '—'
        if nmode is None or hmode is None:
            verdict, c = 'ABSENT', '1;33'
            rc = 1
        elif nmode == hmode:
            verdict, c = f'OK ({nmode})', '1;32'
        else:
            verdict, c = f'DIFF {nmode} vs {hmode} (Δ{hmode - nmode:+d})', '1;31'
            rc = 1
        print(f"{pc:>8X} | {nstr:>26} | {hstr:>26} | "
              + color(verdict, c, col))
    return rc


def color(s, c, on):
    return f"\033[{c}m{s}\033[0m" if on else s


def show_window(a, b, idx, ctx, col):
    """Affiche une fenêtre ±ctx autour de l'indice de divergence, deux colonnes."""
    lo = max(0, idx - ctx)
    hi = min(max(len(a), len(b)), idx + ctx + 1)
    print(f"  {'#':>5}  {'NeoST':<44} {'Hatari'}")
    for i in range(lo, hi):
        la = a[i].text if i < len(a) else '—'
        lb = b[i].text if i < len(b) else '—'
        mark = '>>' if i == idx else '  '
        line = f"{mark}{i:>5}  {la[:44]:<44} {lb[:44]}"
        print(color(line, '1;31', col) if i == idx else line)


def main():
    ap = argparse.ArgumentParser(
        description="Localise la première divergence entre une trace NeoST et "
                    "une trace Hatari (flux PC + registres).")
    ap.add_argument('neost', help="trace NeoST (neost-headless --trace)")
    ap.add_argument('hatari', help="trace Hatari (--trace cpu_disasm[,cpu_regs])")
    ap.add_argument('--align-pc', metavar='HEX',
                    help="aligne les deux traces sur la 1re occurrence de ce PC")
    ap.add_argument('--regs', action='store_true',
                    help="compare aussi D0-D7/A0-A7/SR (si présents des deux côtés)")
    ap.add_argument('--periods', nargs='+', metavar='PC',
                    help="HARNAIS TIMING : pour chaque PC-repère (hex), compare la "
                         "période (cycles/itér) Moira vs WinUAE. Exige les colonnes "
                         "cycle (NEOST_TRACE_CYC=1 ; Hatari cpu_video_cycles).")
    ap.add_argument('-C', '--context', type=int, default=6,
                    help="lignes de contexte autour de la divergence (défaut 6)")
    ap.add_argument('--no-color', action='store_true', help="désactive la couleur")
    ap.add_argument('--neost-format', choices=['neost', 'hatari'])
    ap.add_argument('--hatari-format', choices=['neost', 'hatari'])
    args = ap.parse_args()
    col = sys.stdout.isatty() and not args.no_color

    a, fa = parse(args.neost, args.neost_format)
    b, fb = parse(args.hatari, args.hatari_format)
    print(f"[trace_diff] NeoST  : {len(a):>7} insn ({fa})  ←  {args.neost}")
    print(f"[trace_diff] Hatari : {len(b):>7} insn ({fb})  ←  {args.hatari}")

    if args.periods:                        # mode harnais de timing différentiel
        return report_periods(a, b, args.periods, col)

    if args.align_pc:
        pc = int(args.align_pc, 16)
        a, b = trim_to_pc(a, pc), trim_to_pc(b, pc)
        if not a or not b:
            sys.exit(f"[trace_diff] PC d'alignement ${pc:06X} absent d'au moins une trace.")
        print(f"[trace_diff] aligné sur ${pc:06X} "
              f"(reste {len(a)} / {len(b)} insn)")

    n = min(len(a), len(b))
    for i in range(n):
        # 1) Divergence de flux : les PC ne correspondent plus.
        if a[i].pc != b[i].pc:
            print(color(f"\n✗ DIVERGENCE DE FLUX à l'instruction #{i} "
                        f": NeoST=${a[i].pc:06X}  Hatari=${b[i].pc:06X}",
                        '1;31', col))
            show_window(a, b, i, args.context, col)
            return 1
        # 2) Divergence de registres : même PC mais état CPU différent.
        if args.regs and a[i].regs and b[i].regs:
            common = set(a[i].regs) & set(b[i].regs)
            diff = [(r, a[i].regs[r], b[i].regs[r])
                    for r in sorted(common) if a[i].regs[r] != b[i].regs[r]]
            if diff:
                print(color(f"\n✗ DIVERGENCE DE REGISTRES à l'instruction #{i} "
                            f"(PC=${a[i].pc:06X})", '1;31', col))
                for r, va, vb in diff:
                    print(f"    {r}: NeoST={va:08X}  Hatari={vb:08X}")
                show_window(a, b, i, args.context, col)
                return 1

    print(color(f"\n✓ Identiques sur {n} instructions"
                f"{' (registres inclus)' if args.regs else ''}.", '1;32', col))
    if len(a) != len(b):
        print(f"  (longueurs différentes : NeoST={len(a)}, Hatari={len(b)} — "
              f"l'une s'arrête avant l'autre)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
