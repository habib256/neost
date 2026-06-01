#!/usr/bin/env python3
# =============================================================================
#  trace_diff.py — Aligne et compare une trace NeoST et une trace Hatari.
#
#  Méthode (cf. CLAUDE.md) : « MAME et Hatari sont les sources de vérité
#  matérielle » et « la séquence de PC est le signal de diff ». Cet outil prend
#  deux traces d'exécution du MÊME ROM/disquette, les aligne sur un PC commun,
#  puis localise la PREMIÈRE divergence — soit du flux (PC différent), soit d'un
#  registre (PC identique mais D0-D7/A0-A7/SR qui divergent en amont du branch).
#
#  Générer les traces :
#    NeoST  : ./build/neost-headless --frames N --trace neost.txt --regs --irq
#    Hatari : hatari --trace cpu_disasm --log-file hatari.txt \
#                    --tos rom/etos192us.img --disk-a disks/diskA.st <...>
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
#   NeoST  : "FC0030: bra     $fc004e [ D0=.. .. SR=2704]"
#   Hatari : "$00fc0030 : 601c        bra.s     $fc004e"  (cpu_disasm)
RE_NEOST  = re.compile(r'^([0-9A-Fa-f]{5,8})\s*:\s+(.*)$')
RE_HATARI = re.compile(r'^\$([0-9A-Fa-f]{1,8})\s*:\s+(.*)$')

# Registres, tolérant aux deux notations ("D0=12345678" ou "D0 12345678").
RE_REG = re.compile(r'\b([DA][0-7])\s*[= ]\s*([0-9A-Fa-f]{1,8})\b')
RE_SR  = re.compile(r'\bSR\s*[= ]\s*([0-9A-Fa-f]{1,4})\b')


class Insn:
    """Une instruction : PC, texte brut (1re ligne) et registres éventuels."""
    __slots__ = ('pc', 'text', 'regs')

    def __init__(self, pc, text):
        self.pc = pc
        self.text = text
        self.regs = {}

    def parse_regs(self, blob):
        for name, val in RE_REG.findall(blob):
            self.regs[name] = int(val, 16)
        m = RE_SR.search(blob)
        if m:
            self.regs['SR'] = int(m.group(1), 16)


def detect_format(lines):
    """Devine le format (neost/hatari) en regardant les premières lignes."""
    for ln in lines[:200]:
        if RE_HATARI.match(ln):
            return 'hatari'
        if RE_NEOST.match(ln):
            return 'neost'
    return None


def parse(path, fmt=None):
    """Lit un fichier de trace → liste d'Insn. Les lignes de continuation
    (dump registres multi-lignes Hatari, marqueurs IRQ NeoST) sont rattachées à
    l'instruction courante pour l'extraction des registres."""
    with open(path, 'r', errors='replace') as f:
        lines = f.read().splitlines()
    if fmt is None:
        fmt = detect_format(lines)
        if fmt is None:
            sys.exit(f"[trace_diff] format non reconnu : {path}")
    rx = RE_HATARI if fmt == 'hatari' else RE_NEOST

    insns, cur, blob = [], None, []
    def flush():
        if cur is not None:
            cur.parse_regs(' '.join(blob))
    for ln in lines:
        m = rx.match(ln)
        if m:
            flush()
            cur = Insn(int(m.group(1), 16), ln.rstrip())
            insns.append(cur)
            blob = [m.group(2)]
        elif cur is not None:
            blob.append(ln)                 # continuation (regs, flags, IRQ…)
    flush()
    return insns, fmt


def trim_to_pc(insns, pc):
    """Tronque la liste pour démarrer à la première occurrence de pc."""
    for i, ins in enumerate(insns):
        if ins.pc == pc:
            return insns[i:]
    return []


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
