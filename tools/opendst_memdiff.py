#!/usr/bin/env python3
# =============================================================================
#  opendst_memdiff.py — trouver les VARIABLES D'UN JEU en RAM (position, munitions,
#  numéro d'écran…) par diff d'états, pilotage du serveur NeoST.
#
#  C'est le travail qu'un explorateur d'états fait en premier et à la main pour
#  chaque nouveau jeu : de quelle adresse faire une clé de cellule ? La méthode
#  est celle des « cheat engines », posée sur le déterminisme de NeoST :
#
#    1. un point de départ commun (état importé, ou N trames de boot) ;
#    2. rollout NEUTRE de N trames  → image mémoire D1 ;
#    3. rollout NEUTRE de 2N trames → image D2 ; ce qui diffère entre D1 et D2
#       bouge AVEC LE TEMPS (compteurs, animations, timers) : à exclure ;
#    4. rollout avec TON ENTRÉE (même longueur N) → image DI ; ce qui diffère
#       entre DI et D1 a bougé À CAUSE DE L'ENTRÉE.
#    Candidats = (DI ≠ D1) − (D2 ≠ D1) : « change quand j'appuie, pas quand
#    j'attends ». Le déterminisme rend le raisonnement exact : deux rollouts
#    identiques donnent la même image, donc TOUTE différence a une cause.
#
#  La lecture passe par « peek » (Bus::peek8, sans effet de bord) : aucun
#  format de fichier à connaître, et la machine n'est jamais perturbée.
#
#  Exemple (Rick Dangerous, depuis un état en jeu, « aller à droite ») :
#    python3 tools/opendst_memdiff.py --rom roms/tos102uk.img --disk <jeu.st> \
#        --machine st --load-state rick_l1.state --input "R*30" --frames 30
#  Auto-test sans jeu (EmuTOS) : l'entrée joystick doit laisser une trace, et
#  $466 (compteur VBL) / $4BA (200 Hz) doivent être EXCLUS comme temporels.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS = os.path.join(ROOT, "build", "neost-headless")
CHUNK = 4096                                  # borne de « peek »


class Server:
    def __init__(self, argv):
        self.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, bufsize=1)

    def cmd(self, line):
        try:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()
        except (BrokenPipeError, ValueError):
            raise RuntimeError("serveur arrêté sur « %s »\n%s" % (line, self.p.stderr.read()[-800:]))
        rsp = self.p.stdout.readline()
        if rsp == "":
            raise RuntimeError("serveur arrêté sur « %s »\n%s" % (line, self.p.stderr.read()[-800:]))
        rsp = rsp.rstrip("\n")
        if rsp.startswith("err "):
            raise RuntimeError("%s → %s" % (line, rsp))
        return rsp[3:] if rsp.startswith("ok ") else ""

    def dump(self, start, length):
        """Image mémoire [start, start+length) par tranches de 4 Ko."""
        out = bytearray()
        a = start
        while a < start + length:
            n = min(CHUNK, start + length - a)
            out += bytes.fromhex(self.cmd("peek %X %d" % (a, n)))
            a += n
        return bytes(out)

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.p.wait(timeout=10)


def parse_hex(t):
    t = t.strip()
    if t.startswith("$"):
        t = t[1:]
    elif t.lower().startswith("0x"):
        t = t[2:]
    return int(t, 16)


def runs(addrs):
    """Adresses triées → plages contiguës [(début, fin)]."""
    out = []
    for a in sorted(addrs):
        if out and a == out[-1][1] + 1:
            out[-1][1] = a
        else:
            out.append([a, a])
    return out


def read_word(img, base, a, w):
    off = a - base
    return int.from_bytes(img[off:off + w], "big")


def main():
    ap = argparse.ArgumentParser(description="Recherche d'adresses RAM par diff d'états (serveur NeoST)")
    ap.add_argument("--rom", required=True)
    ap.add_argument("--disk", required=True)
    ap.add_argument("--machine", default="st")
    ap.add_argument("--mem", default="512k")
    ap.add_argument("--load-state", default="", help="état de départ (sinon --boot-frames)")
    ap.add_argument("--boot-frames", type=int, default=200)
    ap.add_argument("--input", required=True, help="script joystick de l'expérience (grammaire JoyScript)")
    ap.add_argument("--frames", type=int, default=0,
                    help="longueur des rollouts neutres (défaut : celle du script)")
    ap.add_argument("--range", default="", help="ADR:LEN en hexa (défaut : toute la RAM)")
    ap.add_argument("--width", type=int, default=1, choices=(1, 2, 4),
                    help="largeur des valeurs affichées (l'analyse se fait à l'octet)")
    ap.add_argument("--max", type=int, default=60, help="nombre max de plages affichées")
    args = ap.parse_args()

    if not os.path.exists(HEADLESS):
        sys.exit("absent : %s (compiler d'abord)" % HEADLESS)

    argv = [HEADLESS, args.rom, "--machine", args.machine, "--mem", args.mem,
            "--disk", args.disk, "--fastfdc", "--server", "--server-slots", "2"]
    srv = Server(argv)
    try:
        hello = srv.cmd("hello")
        ram_txt = hello.split(" ram=")[1].split()[0]           # « 512k », « 1m »…
        ram = int(ram_txt[:-1]) * (1024 if ram_txt.endswith("k") else 1024 * 1024)
        if args.range:
            a, l = args.range.split(":")
            base, length = parse_hex(a), parse_hex(l)
        else:
            base, length = 0, ram

        # 1. point de départ commun
        if args.load_state:
            srv.cmd("import 0 " + args.load_state)
            srv.cmd("load 0")
        else:
            srv.cmd("run %d" % args.boot_frames)
        srv.cmd("save 0")

        # longueur du script = nombre de trames qu'il joue, lue dans la réponse
        f0 = int(srv.cmd("observe").split()[0].split("=")[1])
        n_script = int(srv.cmd("play " + args.input).split()[0].split("=")[1]) - f0
        n = args.frames or n_script
        if n <= 0:
            sys.exit("le script ne joue aucune trame")
        di = srv.dump(base, length)                    # 4. image APRÈS L'ENTRÉE (déjà jouée)
        if n != n_script:
            # même longueur exigée pour comparer à trame égale : on rejoue en complétant
            srv.cmd("load 0")
            srv.cmd("play %s .*%d" % (args.input, max(0, n - n_script)))
            di = srv.dump(base, length)

        srv.cmd("load 0"); srv.cmd("play .*%d" % n);       d1 = srv.dump(base, length)   # 2.
        srv.cmd("load 0"); srv.cmd("play .*%d" % (2 * n)); d2 = srv.dump(base, length)   # 3.

        temporal = {base + i for i in range(length) if d1[i] != d2[i]}
        by_input = {base + i for i in range(length) if di[i] != d1[i]}
        cand = by_input - temporal

        print("point de départ : %s ; rollouts de %d trames ; zone $%X..$%X (%d Ko)"
              % (args.load_state or "boot %d trames" % args.boot_frames, n, base, base + length - 1,
                 length // 1024))
        print("  octets qui bougent avec le TEMPS (exclus)      : %6d" % len(temporal))
        print("  octets qui bougent avec L'ENTRÉE               : %6d" % len(by_input))
        print("  → candidats (entrée oui, temps non)            : %6d" % len(cand))
        print()
        if not cand:
            print("Aucun candidat : l'entrée n'a rien changé qui ne change pas déjà tout seul.")
            print("Pistes : script plus long, ou l'entrée n'est pas lue à ce moment du programme.")
            return 1
        if temporal and len(runs(temporal)) <= 16:
            print("exclus comme temporels : " + ", ".join(
                "$%X" % lo if lo == hi else "$%X..$%X" % (lo, hi) for lo, hi in runs(temporal)))
            print()
        print("%-9s %-6s %-20s %-20s" % ("adresse", "taille", "neutre", "avec entrée"))
        w = args.width
        for i, (lo, hi) in enumerate(runs(cand)):
            if i >= args.max:
                print("… %d plage(s) de plus (--max pour en voir davantage)" % (len(runs(cand)) - i))
                break
            size = hi - lo + 1
            if size == w and (lo - base) % w == 0:
                # exactement une valeur de la largeur demandée : lisible comme un nombre
                print("$%06X   %-6d %-20s %-20s" % (lo, size,
                      "0x%0*X" % (w * 2, read_word(d1, base, lo, w)),
                      "0x%0*X" % (w * 2, read_word(di, base, lo, w))))
            else:
                # plage d'une autre taille : les octets bruts, en entier
                print("$%06X   %-6d %-20s %-20s" % (lo, size,
                      d1[lo - base:hi - base + 1].hex(), di[lo - base:hi - base + 1].hex()))
        print()
        print("Ensuite : --probe nom=$ADR:%d dans le serveur ou en --probe-every, et --cell-from probes" % w)
        print("dans opendst_explore.py. Affiner : refaire avec une AUTRE entrée (L*30) et croiser.")
        return 0
    finally:
        srv.close()


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass
    try:
        sys.exit(main())
    except RuntimeError as e:
        print("ÉCHEC : %s" % e, file=sys.stderr)
        sys.exit(1)
