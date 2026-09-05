#!/usr/bin/env python3
# =============================================================================
#  run_server_equiv.py — le mode serveur (--server) doit rendre EXACTEMENT ce
#  que rend la boucle --frames.
#
#  C'est LE contrat du mode serveur : un pilote externe archive des cellules et
#  rejoue des scripts au tuyau ; si le rejeu au tuyau divergeait du rejeu en
#  ligne de commande, toute son archive serait fausse — et silencieusement, car
#  les deux chemins « marchent ». On compare donc, sur la même configuration :
#    - les champs d'observation (frame, hachage d'écran, hachage RAM, sondes) ;
#    - la capture d'écran, au bit près ;
#    - la relecture par --load-state d'un état EXPORTÉ par le serveur ;
#    - le round-trip save/load des emplacements EN MÉMOIRE.
#
#  Aucune ROM propriétaire, aucun jeu : EmuTOS + la disquette livrée, donc
#  exécutable en CI. Câblé au palier `fast` de run_all.py.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS = os.path.join(ROOT, "build", "neost-headless")
ROM = os.path.join(ROOT, "roms", "etos192fr.img")
DISK = os.path.join(ROOT, "disks", "diskA.st")

# Un script qui exerce toute la grammaire, combinaisons comprises.
#
# ⚠ Il FINIT SUR UNE TRANSITION (un dernier masque différent du précédent), et ce
# n'est pas un détail : mesuré, une mutation posant l'entrée APRÈS la trame au lieu
# d'AVANT reste invisible si le script se termine par plusieurs trames de même
# masque — le décalage d'une trame se résorbe dans la répétition, et l'état final
# est identique des deux côtés. Sur une transition finale, la dernière trame est
# jouée avec le masque précédent au lieu du bon, et l'écart se voit.
SCRIPT = "R*10 [DF]*5 .*8 [$88]*4 F*3 [UL]"
SCRIPT_FRAMES = 10 + 5 + 8 + 4 + 3 + 1
TOTAL_FRAMES = 200

COMMON = ["--machine", "st", "--mem", "512k", "--disk", DISK, "--fastfdc"]
# Fenêtre de hachage LARGE, $0-$8000 : elle couvre les vecteurs d'exception, ceux
# de l'IKBD/ACIA et la page de variables système du TOS (hz_200, frclock, vbclock)
# EN PLUS de la zone où le passage du joystick laisse sa trace.
#
# ⚠ Elle avait été rétrécie à $1000-$7000 sur un raisonnement FAUX : j'avais cru
# que $0-$8000 « ne couvrait pas » l'effet du joystick, alors qu'un écart dans
# $1000-$8000 est par construction un écart dans $0-$8000 — mesuré, l'ancienne
# fenêtre le détectait très bien. La mutation « entrée posée APRÈS la trame »
# passait pour deux tout autres raisons, corrigées ailleurs : la comparaison se
# faisait 169 trames APRÈS la fin du script (le décalage s'y était résorbé) et le
# script finissait sur des masques identiques. Rétrécir ne servait à rien et
# retirait de la couverture.
OBSERVE = ["--probe", "frclock=466:4", "--probe", "palram=45A:2", "--hash-ram", "0:8000"]

ok, fail = 0, 0


def check(what, got, want):
    global ok, fail
    if got == want:
        ok += 1
        print("  OK   %s" % what)
    else:
        fail += 1
        print("  FAIL %s\n         obtenu  %s\n         attendu %s" % (what, got, want))


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


class EmulatorFailed(Exception):
    """Le binaire est sorti ≠ 0 : le message du binaire DOIT remonter, sinon le
    verdict fast n'est qu'un traceback CalledProcessError sans la cause."""


def run_bin(cmd, stdin=None):
    r = subprocess.run(cmd, input=stdin, capture_output=True, text=True)
    if r.returncode != 0:
        raise EmulatorFailed("%s\n  → code %d\n%s" % (" ".join(cmd[:4]) + " …", r.returncode,
                                                       "\n".join("  | " + l for l in
                                                                  r.stderr.splitlines()[-8:])))
    return r.stdout


def run_cli(frames, shot=None, script=True):
    """Boucle --frames : la référence."""
    cmd = [HEADLESS, ROM] + COMMON + OBSERVE + ["--frames", str(frames),
                                                "--probe-every", str(frames)]
    if script:
        cmd += ["--joy-script", "0", SCRIPT]
    if shot:
        cmd += ["--screenshot", shot]
    out = run_bin(cmd)
    lines = [l for l in out.splitlines() if l.startswith("probe ")]
    if not lines:
        raise EmulatorFailed("la boucle --frames n'a émis aucun échantillon")
    return lines[-1][len("probe "):]


def run_server(commands, shot=None):
    """Session serveur : rend la liste des réponses."""
    cmd = [HEADLESS, ROM] + COMMON + OBSERVE + ["--server"]
    script = "\n".join(commands) + "\nquit\n"
    return run_bin(cmd, stdin=script).splitlines()


def main():
    # Un BINAIRE absent est un échec, pas un saut : « SKIP » sur build/neost-headless
    # faisait lire VERT un palier fast lancé sans avoir compilé.
    if not os.path.exists(HEADLESS):
        print("  ÉCHEC : %s absent (compiler d'abord)" % HEADLESS)
        return 1
    # ROM EmuTOS et disquette sont COMMITÉES : leur absence signale un dépôt
    # incomplet, pas un environnement légitimement dépourvu. Un « SKIP » y ferait
    # lire vert un palier fast qui n'a jamais exercé le contrat du serveur.
    for p in (ROM, DISK):
        if not os.path.exists(p):
            print("  ÉCHEC : %s absent (dépôt incomplet ?)" % p)
            return 1

    tmp = tempfile.mkdtemp(prefix="neost-srv-")
    cli_shot = os.path.join(tmp, "cli.ppm")
    srv_shot = os.path.join(tmp, "srv.ppm")
    exported = os.path.join(tmp, "cell.state")

    print("Équivalence --server / --frames")
    ref = run_cli(TOTAL_FRAMES, shot=cli_shot)

    # 1. Même script, même reste de trames, au tuyau.
    rest = TOTAL_FRAMES - SCRIPT_FRAMES
    rsp = run_server(["play " + SCRIPT, "run %d" % rest, "shot " + srv_shot])
    obs = [l[len("ok "):] for l in rsp if l.startswith("ok frame=")]
    got = obs[-1] if obs else None      # la DERNIÈRE observation : après « run »
    check("observation identique à la boucle --frames", got, ref)
    check("capture d'écran identique au bit près", md5(srv_shot), md5(cli_shot))

    # 2. Round-trip des emplacements EN MÉMOIRE : rejouer depuis un état
    #    sauvegardé doit redonner exactement le même point d'arrivée.
    rsp = run_server([
        "play " + SCRIPT, "save 0",
        "run %d" % rest, "observe",          # trajet direct
        "load 0", "run %d" % rest, "observe",  # même trajet après restauration
    ])
    obs = [l[len("ok "):] for l in rsp if l.startswith("ok frame=")]
    # play, run, observe, load, run, observe → les deux « observe » aux indices 2 et 5
    check("save/load en mémoire : arrivée identique", obs[2], obs[5])
    check("save/load en mémoire : == boucle --frames", obs[2], ref)

    # 2 bis. LE CHEMIN DES ENTRÉES, comparé À LA FIN DU SCRIPT — pas 170 trames
    #    plus loin, où un décalage d'une trame s'est déjà résorbé. C'est ce que le
    #    verdict ne couvrait pas : une mutation posant l'entrée APRÈS la trame au
    #    lieu d'AVANT passait les six vérifications sans rien faire rougir.
    ref_script = run_cli(SCRIPT_FRAMES)
    rsp = run_server(["play " + SCRIPT])
    obs = [l[len("ok "):] for l in rsp if l.startswith("ok frame=")]
    check("entrées posées à la MÊME trame (fin du script)", obs[-1] if obs else None, ref_script)

    # Et le script doit VRAIMENT laisser une trace : sans quoi la vérification
    # ci-dessus comparerait deux fois la même absence d'effet.
    check("le script joystick a un effet observable",
          ref_script != run_cli(SCRIPT_FRAMES, script=False), True)

    # 2 ter. FIDÉLITÉ DE REPRISE D'UNE CELLULE : reprendre un emplacement doit rejouer
    #    à l'identique QUEL QUE SOIT ce que le client a fait entre-temps. La première
    #    version reposait au « load » le joystick tenu AU MOMENT DU LOAD, pas celui du
    #    « save » : même cellule, deux hachages RAM selon la branche explorée (fuzz).
    direct = run_server(["run 30", "play F*10", "save 0", "run 20", "observe"])
    branch = run_server(["run 30", "play F*10", "save 0", "play .*5", "load 0", "run 20", "observe"])
    d = [l[len("ok "):] for l in direct if l.startswith("ok frame=")][-1]
    b = [l[len("ok "):] for l in branch if l.startswith("ok frame=")][-1]
    check("reprise d'une cellule : identique quelle que soit la branche entre-temps", b, d)

    # 2 quater. « joy » (serveur) et --joy (ligne de commande) lisent le MÊME masque.
    #    --joy lisait « 80 » en base 0 (80 décimal = $50, bas+droite) là où le serveur
    #    lisait de l'hexa : le bit FEU, le plus utilisé, était celui qui cassait.
    cli_joy = subprocess.run([HEADLESS, ROM] + COMMON + OBSERVE +
                             ["--frames", "60", "--joy", "80", "--probe-every", "60"],
                             capture_output=True, text=True)
    cli_ram = [l for l in cli_joy.stdout.splitlines()
               if l.startswith("probe ")][-1][len("probe "):].split(" ", 1)[1]
    srv_joy = run_server(["joy 80", "run 60"])
    srv_ram = [l[len("ok "):] for l in srv_joy if l.startswith("ok frame=")][-1].split(" ", 1)[1]
    check("--joy 80 (ligne de commande) == joy 80 (serveur)", srv_ram, cli_ram)

    # 2 quinquies. La même session rejouée deux fois rend les mêmes octets.
    session = ["run 20", "play " + SCRIPT, "save 0", "key make 39", "run 5", "key break 39",
               "mouse 8 -4 1", "run 5", "peek 400 32", "load 0", "run 10", "observe"]
    check("session rejouée deux fois : réponses identiques",
          run_server(session), run_server(session))

    # 3. Un état EXPORTÉ par le serveur doit se relire par --load-state, sinon
    #    l'archive du pilote ne serait pas rejouable hors serveur.
    run_server(["play " + SCRIPT, "save 0", "export 0 " + exported])
    cmd = ([HEADLESS, ROM] + COMMON + OBSERVE +
           ["--load-state", exported, "--frames", str(rest),
            "--probe-every", str(rest)])
    out = run_bin(cmd)
    lines = [l for l in out.splitlines() if l.startswith("probe ")]
    after = lines[-1][len("probe "):]
    # La datation repart de 0 après --load-state : on compare tout sauf « frame= ».
    check("état exporté relu par --load-state",
          after.split(" ", 1)[1], ref.split(" ", 1)[1])

    # 4. Erreurs : un emplacement vide, une commande inconnue et un script fautif
    #    doivent répondre « err », pas mourir ni mentir.
    rsp = run_server(["load 3", "bogus", "play R*30[DF", "peek 400 99999"])
    check("erreurs signalées sans casser la session",
          sum(1 for l in rsp if l.startswith("err ")), 4)

    print("[server-equiv] %d OK, %d FAIL" % (ok, fail))
    # Ménage sur SUCCÈS seulement : chaque exécution laissait 2 Mo d'états dans
    # /tmp, et ce verdict tourne au palier `fast`, donc à chaque push. En échec on
    # garde tout — les images et les états SONT la matière du diagnostic.
    if fail == 0:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        print("  artefacts conservés dans %s" % tmp)
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")   # verdict lisible même en locale ASCII
    except Exception:
        pass
    try:
        sys.exit(main())
    except EmulatorFailed as e:
        print("  FAIL l'émulateur a échoué :\n%s" % e)
        print("[server-equiv] ÉCHEC")
        sys.exit(1)
