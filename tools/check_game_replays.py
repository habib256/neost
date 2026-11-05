#!/usr/bin/env python3
# Contrôles du verdict des rejeux : une capture figée ou manquante ne doit pas
# valider un jeu. Sans ROM, disque privé, Pillow ou émulateur externe.
# (c) 2026 VERHILLE Arnaud — projet NeoST.
from __future__ import annotations

import contextlib
import io
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

import run_game_replay as replay


def main() -> int:
    case = {"id": "game", "frames": [100, 200], "max_diff": 0.5}
    good = {"frames": {"100": {"diff": 0.0}, "200": {"diff": 0.1}}}
    checks = [
        ("complete matching captures", case, good, 0),
        ("empty capture set", case, {"frames": {}}, 1),
        ("missing final capture", case, {"frames": {"100": {"diff": 0.0}}}, 1),
        ("NeoST halted with matching pixels", case, dict(good, neost_halted=True), 1),
        ("Hatari halted with matching pixels", case, dict(good, hatari_halted=True), 1),
        ("comparison error with captures", case, dict(good, error="timeout"), 1),
        ("pixel mismatch", case, {"frames": {"100": {"diff": 0.0}, "200": {"diff": 1.0}}}, 1),
        ("explicit diagnostic can observe a halt", dict(case, diagnostic=True), dict(good, neost_halted=True), 0),
        ("matching emulators at the wrong scene", dict(case, scene_hashes={"200": "race"}), good, 1),
        ("missing scene checkpoint", dict(case, scene_hashes={"300": "race"}), good, 1),
        ("inspected scene checkpoint", dict(case, scene_hashes={"200": "race"}),
         {"frames": {"100": {"diff": 0.0}, "200": {"diff": 0.1, "neost_rgb_sha256": "race"}}}, 0),
    ]
    for name, spec, result, expected in checks:
        code, notes = replay.assess(spec, result)
        if code != expected:
            print(f"FAIL {name}: {code}, {notes}")
            return 1
        print(f"OK   {name}")

    # La suite normale doit ignorer un diagnostic même si ses fichiers existent.
    # --only permet de le demander, sans le compter comme compatibilité réussie.
    with TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "tests/replays").mkdir(parents=True)
        (root / "disk.st").write_bytes(b"disk")
        (root / "tos.img").write_bytes(b"rom")
        normal = dict(case, disk="disk.st", rom="tos.img")
        diagnostic = dict(normal, id="known_failure", diagnostic=True)
        (root / "tests/replays/replays.json").write_text(json.dumps({"replays": [normal, diagnostic]}))
        for args, ids, text in [
            ([], ["game"], "1 compatibility, 0 diagnostic, 0 failed"),
            (["--only", "known_failure"], ["known_failure"], "0 compatibility, 1 diagnostic, 0 failed"),
        ]:
            output = io.StringIO()
            with patch.object(replay, "ROOT", root), patch.object(sys, "argv", ["run_game_replay.py", *args]), \
                    patch.object(replay, "play", return_value=(0, [])) as play, contextlib.redirect_stdout(output):
                code = replay.main()
            actual = [call.args[0]["id"] for call in play.call_args_list]
            if code != 0 or actual != ids or text not in output.getvalue():
                print(f"FAIL replay selection: {actual}, {output.getvalue()}")
                return 1
        with patch.object(replay, "ROOT", root), patch.object(sys, "argv", ["run_game_replay.py", "--only", "typo"]), \
                contextlib.redirect_stderr(io.StringIO()):
            try:
                replay.main()
            except SystemExit as exc:
                if exc.code != 2:
                    raise
            else:
                print("FAIL unknown replay ID accepted")
                return 1
    print("REPLAY VERDICT CHECKS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
