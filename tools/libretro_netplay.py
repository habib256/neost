#!/usr/bin/env python3
"""Banc netplay du cœur libretro : deux joueurs, deux processus, mêmes entrées.

Comme le netplay de RetroArch : l'HÔTE joue un script d'entrées et, à la trame K, son
état est remis à un INVITÉ qui le rejoint en cours de partie. Chacun a son propre
dossier de sauvegardes (donc ses propres chemins absolus de copies privées) ; l'invité
s'écarte même de quelques trames avant de rejoindre. Ensuite, mêmes entrées : l'image et
la RAM doivent coïncider à chaque trame. C'est l'item « Banc netplay du module » du TODO.

  python3 tools/libretro_netplay.py [--module M] [--frames N] [--join K]

(c) 2026 VERHILLE Arnaud — projet NeoST.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def inputs(frame):
    """Script d'entrées, fonction de la seule trame : joystick, souris, clic."""
    pad = {"RIGHT", "B"} if (frame // 12) % 3 == 0 else ({"LEFT"} if (frame // 12) % 3 == 1 else set())
    dx = 4 if frame % 9 == 0 else (-3 if frame % 13 == 0 else 0)
    click = frame % 50 < 3
    return pad, dx, click


def play(role, module, work, content, frames, join, blob):
    from run_libretro import core
    c, media = core(Path(module), Path(work))
    assert c.load(media / content), "load failed"
    hashes = []
    if role == "guest":
        # Un invité qui a déjà joué de son côté, autrement : l'état de l'hôte doit tout
        # remplacer.
        c.pad[0] = {"UP", "B"}
        c.run(37)
        c.pad[0] = set()
        state = Path(blob).read_bytes()
        assert c.load_state(state), "guest refused the host's state"
        start = join
    else:
        start = 0
    for f in range(start, frames):
        pad, dx, click = inputs(f)
        c.pad[0] = pad
        c.mouse[0], c.mouse[2] = dx, click
        c.run(1)
        if role == "host" and f == join - 1:
            state = c.save_state()
            assert state, "host could not serialize"
            Path(blob).write_bytes(state)
        if f >= join:
            hashes.append([f, c.frame_hash(), hashlib.sha1(c.ram()).hexdigest()[:12]])
    return hashes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", default=None)
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--join", type=int, default=500)
    ap.add_argument("--content", default="diskA.st")
    ap.add_argument("--role", choices=["host", "guest"])
    ap.add_argument("--work")
    ap.add_argument("--blob")
    ap.add_argument("--out")
    a = ap.parse_args()
    if a.role:
        Path(a.out).write_text(json.dumps(play(a.role, a.module, a.work, a.content, a.frames, a.join, a.blob)))
        return 0
    from run_libretro import default_module
    module = Path(a.module) if a.module else default_module()
    tmp = Path(tempfile.mkdtemp(prefix="neost-netplay-"))
    try:
        blob = tmp / "host.state"
        res = {}
        for role in ("host", "guest"):
            out = tmp / f"{role}.json"
            r = subprocess.run([sys.executable, __file__, "--role", role, "--module", str(module.resolve()),
                                "--work", str(tmp / role), "--blob", str(blob), "--out", str(out),
                                "--frames", str(a.frames), "--join", str(a.join), "--content", a.content],
                               capture_output=True, text=True)
            if r.returncode:
                print(f"FAIL {role}:\n{r.stderr[-2000:]}")
                return 1
            res[role] = json.loads(out.read_text())
            if role == "host":
                # Deux machines : les fichiers de l'hôte n'existent pas chez l'invité. Son état
                # nomme pourtant SES copies privées ; l'invité doit trouver les siennes.
                shutil.rmtree(tmp / "host")
        host, guest = res["host"], res["guest"]
        for h, g in zip(host, guest):
            if h != g:
                print(f"FAIL diverged at frame {h[0]}: host {h[1:]} guest {g[1:]}")
                return 1
        print(f"OK   guest joined at frame {a.join} with the host's state ({blob.stat().st_size} bytes, other "
              f"save folder) and matched it for {len(host)} frames")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
