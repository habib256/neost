#!/usr/bin/env python3
# Garde-fous du témoin pixel et des entrées datées, sans disque ni ROM.
# (c) 2026 VERHILLE Arnaud — projet NeoST.
import argparse
import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from compare_titles import Image, mouse_events, parse_mouse, pixel_difference, read_neost_frames
import run_game_replay


def main() -> int:
    with TemporaryDirectory() as tmp:
        prefix = Path(tmp) / "n_"
        Image.new("RGB", (2, 1), "red").save(f"{prefix}00101.ppm")
        Image.new("RGB", (2, 1), "blue").save(f"{prefix}00200.ppm")
        # Une image voisine ne peut remplacer la capture finale manquante.
        try:
            read_neost_frames(prefix, [101, 201])
        except RuntimeError as exc:
            assert "201" in str(exc)
        else:
            raise AssertionError("Missing final capture replaced by an earlier image")
        Image.new("RGB", (2, 1), "green").save(f"{prefix}00201.ppm")
        frames = read_neost_frames(prefix, [101, 201])
        assert frames[101].getpixel((0, 0)) == (255, 0, 0)
        assert frames[201].getpixel((0, 0)) == (0, 128, 0)
        # Un instantané natif ne doit pas valider un halt avec la bonne image,
        # ni une autre scène. Médias factices ; aucun émulateur lancé ici.
        root = Path(tmp)
        (root / "disk.st").write_bytes(b"source")
        scene = Image.new("RGB", (2, 1), "green")
        scene.save(root / "native.ppm")
        spec = dict(args=[], size=[2, 1],
                    media_sha256={"disk.st": hashlib.sha256(b"source").hexdigest()},
                    rgb_sha256=hashlib.sha256(scene.tobytes()).hexdigest())
        with patch.object(run_game_replay, "ROOT", root):
            for stderr, expected in (("", 0), ("CPU halted", 1)):
                with patch.object(run_game_replay.subprocess, "run",
                                  return_value=SimpleNamespace(returncode=0, stdout="", stderr=stderr)):
                    assert run_game_replay.play_snapshot(spec, root)[0] == expected
            Image.new("RGB", (2, 1), "red").save(root / "native.ppm")
            with patch.object(run_game_replay.subprocess, "run",
                              return_value=SimpleNamespace(returncode=0, stdout="", stderr="")):
                assert run_game_replay.play_snapshot(spec, root)[0] == 1
    black = Image.new("RGB", (2, 1))
    assert pixel_difference(black, black) == 0
    for color in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
        changed = black.copy()
        changed.putpixel((0, 0), color)
        assert pixel_difference(black, changed) == 50, color
    try:
        pixel_difference(black, Image.new("RGB", (1, 1)))
    except ValueError:
        pass
    else:
        raise AssertionError("Different capture dimensions accepted")

    # Clic tenu, ajout du second bouton, puis déplacement : les deux boutons
    # doivent se relâcher à la même trame, avant le mouvement relatif.
    assert mouse_events([(5, "113D.")], 60) == [
        (5, "leftdown"), (7, "rightdown"), (8, "leftup"),
        (8, "rightup"), (8, "mousemove 0 60"),
    ]
    assert mouse_events([(5, "2")]) == [(5, "rightdown"), (6, "rightup")]
    for text in ("0:D", "1:", "1:X", "1:D*5", "-1:1"):
        try:
            parse_mouse(text)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError(f"Invalid mouse script accepted: {text}")
    try:
        mouse_events([(1, "11"), (3, "D")])
    except ValueError:
        pass
    else:
        raise AssertionError("Final button release overlaps the next script")
    print("PIXEL AND INPUT COMPARISON CHECKS OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
