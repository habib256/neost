#!/usr/bin/env python3
# =============================================================================
#  opendst_explore.py — boucle Go-Explore MINIMALE pilotant `neost-headless
#  --server`. Un point de départ, pas une implémentation de recherche.
#
#  Ce qu'elle fait, et qui est tout l'algorithme (cf. Go-Explore, Uber 2018) :
#    1. ARCHIVE de cellules — une cellule = une situation, identifiée par une
#       clé (hachage d'écran, de RAM, ou valeurs de sondes) ;
#    2. on REPREND une cellule prometteuse au lieu de rejouer depuis le début ;
#    3. on EXPLORE au hasard depuis là, quelques dizaines de trames ;
#    4. toute clé jamais vue devient une nouvelle cellule.
#  Le déterminisme de NeoST est ce qui rend (2) exact : reprendre une cellule et
#  rejouer les mêmes entrées redonne exactement la même chose, toujours.
#
#  Ce que ce script N'EST PAS : une politique d'exploration sérieuse. Le choix
#  de cellule est un tirage biaisé par le nombre de visites, les actions sont
#  aléatoires, il n'y a pas de robustification. C'est délibéré — le sujet ici
#  est l'INTERFACE avec l'émulateur, pas la recherche.
#
#  Exemple (Super Sprint, feu = démarrer la course) :
#    python3 tools/opendst_explore.py --rom roms/tos102uk.img --disk <jeu.st> \
#        --machine st --boot-frames 2600 --boot-script ".*36 F*4 " --boot-repeat 45 \
#        --iterations 200 --rollout 40 --out /tmp/explore
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import os
import random
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS = os.path.join(ROOT, "build", "neost-headless")

# Masques ST : haut $01, bas $02, gauche $04, droite $08, feu $80. Les
# combinaisons comptent — sans feu+direction, la moitié des jeux ne se joue pas.
ACTIONS = ["." , "U", "D", "L", "R", "F", "[UF]", "[DF]", "[LF]", "[RF]",
           "[UL]", "[UR]", "[DL]", "[DR]"]


class Server:
    """Le serveur NeoST au bout d'un tuyau : une commande, une réponse."""

    def __init__(self, argv, log_path):
        # stderr du serveur → fichier : un serveur qui meurt doit pouvoir DIRE pourquoi
        # (ROM absente, disquette absente…), pas seulement « code 1 ».
        self.log_path = log_path
        self.log = open(log_path, "w")
        self.p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=self.log, text=True, bufsize=1)

    def tail(self, n=6):
        try:
            self.log.flush()
            with open(self.log_path) as f:
                return "".join("  | " + l for l in f.readlines()[-n:])
        except OSError:
            return ""

    def cmd(self, line):
        # L'écriture est gardée elle aussi : si le serveur est déjà mort, c'est
        # elle qui lève (BrokenPipeError) et le lecteur ci-dessous n'est jamais
        # atteint — le diagnostic soigné était remplacé par une trace brute.
        try:
            self.p.stdin.write(line + "\n")
            self.p.stdin.flush()
        except (BrokenPipeError, ValueError):
            raise RuntimeError("le serveur s'est arrêté (commande : %s), code %s\n%s"
                               % (line, self.p.poll(), self.tail()))
        rsp = self.p.stdout.readline()
        # Ligne vide = fin de flux : le serveur est mort. Sans ce test, chaque
        # commande suivante rendait "" et la boucle continuait sur du vide, en
        # archivant des cellules dont la clé était la chaîne vide.
        if rsp == "":
            raise RuntimeError("le serveur s'est arrêté (commande : %s), code %s\n%s"
                               % (line, self.p.poll(), self.tail()))
        rsp = rsp.rstrip("\n")
        if rsp.startswith("err "):
            raise RuntimeError("%s → %s" % (line, rsp))
        return rsp[3:] if rsp.startswith("ok ") else ""

    def fields(self, line):
        """Réponse « frame=… screen=… ram=… nom=0x… » → dictionnaire."""
        out = {}
        for tok in self.cmd(line).split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                out[k] = v
        return out

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        try:
            self.p.wait(timeout=10)
        finally:
            self.log.close()


def main():
    ap = argparse.ArgumentParser(description="Boucle Go-Explore minimale sur neost-headless --server")
    ap.add_argument("--rom", required=True)
    ap.add_argument("--disk", required=True)
    ap.add_argument("--machine", default="st")
    ap.add_argument("--mem", default="512k")
    ap.add_argument("--boot-frames", type=int, default=0,
                    help="trames à jouer une fois pour atteindre le point de départ")
    ap.add_argument("--boot-script", default="",
                    help="script joystick joué pendant le boot (répété --boot-repeat fois)")
    ap.add_argument("--boot-repeat", type=int, default=1)
    ap.add_argument("--load-state", default="", help="repartir d'un état déjà gravé")
    ap.add_argument("--iterations", type=int, default=100)
    ap.add_argument("--rollout", type=int, default=40, help="trames par exploration")
    ap.add_argument("--cell-from", default="screen", choices=("screen", "ram", "probes"),
                    help="ce qui identifie une cellule")
    ap.add_argument("--probe", action="append", default=[], help="sonde NOM=ADR:LEN (répétable)")
    ap.add_argument("--hash-ram", default="", help="ADR:LEN pour la clé « ram »")
    ap.add_argument("--score", default="", help="nom d'une sonde à maximiser (facultatif)")
    ap.add_argument("--out", default="", help="dossier où graver les cellules")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    if args.cell_from == "ram" and not args.hash_ram:
        sys.exit("--cell-from ram exige --hash-ram ADR:LEN")
    if args.cell_from == "probes" and not args.probe:
        sys.exit("--cell-from probes exige au moins une --probe")
    random.seed(args.seed)

    argv = [HEADLESS, args.rom, "--machine", args.machine, "--mem", args.mem,
            "--disk", args.disk, "--fastfdc", "--server", "--server-slots", "4"]
    for p in args.probe:
        argv += ["--probe", p]
    if args.hash_ram:
        argv += ["--hash-ram", args.hash_ram]

    out = args.out or os.path.join(ROOT, "tests", "out", "explore")
    os.makedirs(out, exist_ok=True)

    # F4 : --score sur une sonde non déclarée dégénérait en nouveauté pure, score 0
    # partout, sans un mot — précisément le réglage vendu comme décisif.
    probe_names = [p.split("=", 1)[0] for p in args.probe if "=" in p]
    if args.score and args.score not in probe_names:
        sys.exit("--score %s : aucune --probe de ce nom (déclarées : %s)"
                 % (args.score, ", ".join(probe_names) or "aucune"))
    # F18 : --load-state rend les options de boot muettes — le dire.
    if args.load_state and (args.boot_frames or args.boot_script):
        print("⚠ --load-state donné : --boot-frames/--boot-script/--boot-repeat sont ignorés")

    srv = Server(argv, os.path.join(out, "server.log"))
    try:
        print(srv.cmd("hello"))

        # --- Point de départ : soit un état gravé, soit un boot scripté --------
        if args.load_state:
            srv.cmd("import 0 " + args.load_state)
            f = srv.fields("load 0")
        else:
            if args.boot_script:
                # La réponse porte la trame ATTEINTE : c'est elle qui dit combien de
                # trames le script a consommées. Les compter à la main comptait des
                # TOKENS (« R*36 » = un token, trente-six trames) et le boot
                # s'arrêtait des centaines de trames trop tôt.
                f = srv.fields("play " + args.boot_script * args.boot_repeat)
                played = int(f["frame"])
            else:
                played = 0
            if args.boot_frames > played:
                f = srv.fields("run %d" % (args.boot_frames - played))
            elif not args.boot_script:
                f = srv.fields("observe")
        srv.cmd("save 0")
        srv.cmd("export 0 %s/cell_00000.state" % out)

        def key_of(f):
            if args.cell_from == "screen":
                return f.get("screen", "")
            if args.cell_from == "ram":
                return f.get("ram", "")
            return "|".join("%s=%s" % (k, v) for k, v in sorted(f.items())
                            if k not in ("frame", "screen", "ram"))

        def score_of(f):
            return int(f.get(args.score, "0"), 16) if args.score else 0

        # cellule : clé → [fichier d'état, visites, score, trame]
        archive = {key_of(f): ["%s/cell_00000.state" % out, 0, score_of(f), int(f["frame"])]}
        next_id = [0]
        new_cells, best = 0, score_of(f)

        for it in range(1, args.iterations + 1):
            # --- 1. CHOISIR : biais vers les cellules peu visitées ------------
            keys = list(archive)
            weights = [1.0 / (1 + archive[k][1]) ** 2 for k in keys]
            k = random.choices(keys, weights=weights)[0]
            cell = archive[k]
            cell[1] += 1

            # --- 2. REPRENDRE (exact, c'est tout l'intérêt du déterminisme) ---
            srv.cmd("import 1 " + cell[0])
            srv.cmd("load 1")

            # --- 3. EXPLORER : quelques actions tenues, pas du bruit par trame.
            #     Une action tenue plusieurs trames ressemble à un geste humain et
            #     va bien plus loin qu'un tirage indépendant à chaque trame.
            script = []
            left = args.rollout
            while left > 0:
                hold = min(left, random.randint(4, 12))
                script.append("%s*%d" % (random.choice(ACTIONS), hold))
                left -= hold
            f = srv.fields("play " + " ".join(script))

            # --- 4. ARCHIVER si la situation n'a jamais été vue ---------------
            nk = key_of(f)
            sc = score_of(f)
            if nk not in archive or sc > archive[nk][2]:
                new = nk not in archive
                # Compteur MONOTONE, pas len(archive) : améliorer le score d'une
                # cellule existante gravait un fichier sans faire croître l'archive,
                # et la cellule NEUVE suivante réutilisait le même nom — deux entrées
                # pointant un seul fichier, donc un rejeu qui ne redonne pas la
                # situation archivée. C'est précisément la garantie que ce script vend.
                next_id[0] += 1
                path = "%s/cell_%05d.state" % (out, next_id[0])
                srv.cmd("save 2")
                srv.cmd("export 2 " + path)
                if not new:
                    # Cellule AMÉLIORÉE : l'ancien fichier n'est plus référencé — sans
                    # ceci, huit orphelins de 1,4 Mo pour une seule cellule (mesuré).
                    try:
                        os.remove(archive[nk][0])
                    except OSError:
                        pass
                archive[nk] = [path, 0, sc, int(f["frame"])]
                new_cells += new
                best = max(best, sc)
            if it % 25 == 0 or it == args.iterations:
                print("  itération %4d : %4d cellules (%d nouvelles)%s"
                      % (it, len(archive), new_cells,
                         ", meilleur %s=%d" % (args.score, best) if args.score else ""))

        print("\n%d cellules archivées dans %s" % (len(archive), out))
        print("Rejouer une cellule :  %s <rom> … --load-state %s/cell_XXXXX.state"
              % (os.path.relpath(HEADLESS, ROOT), out))
        print("La comparer à Hatari : tools/opendst_oracle.py (cf. docs/OPENDST.md)")
        return 0
    finally:
        srv.close()


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")   # lisible même en locale ASCII
    except Exception:
        pass
    try:
        sys.exit(main())
    except (RuntimeError, FileNotFoundError) as e:
        # Un message, pas une trace : la cause (stderr du serveur) est dedans.
        print("ÉCHEC : %s" % e, file=sys.stderr)
        sys.exit(1)
