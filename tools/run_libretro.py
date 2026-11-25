#!/usr/bin/env python3
"""Banc du cœur libretro neost_libretro (docs/LIBRETRO.md).

Chaque cas tourne dans son propre processus (un module par processus, ctypes) et
rend OK ou FAIL avec ce qu'il a mesuré. Médias libres du dépôt seulement : EmuTOS
(roms/etos*), disks/diskA.st, disks/PDATS295.msa, disks/CHUCHU_L/CHUCHU.TOS. Tout
est recopié dans un dossier temporaire : rien du dépôt n'est monté ni écrit.

  python3 tools/run_libretro.py [--module build-libretro/neost_libretro.dylib] [--only NOM]

(c) 2026 VERHILLE Arnaud — projet NeoST.
"""

import argparse
import hashlib
import json
import multiprocessing as mp
import shutil
import sys
import tempfile
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

FREE_TOS = ["etos192us.img", "etos192fr.img", "etos256us.img", "etos256fr.img"]


def default_module():
    for name in ("neost_libretro.dylib", "neost_libretro.so", "neost_libretro.dll"):
        for d in ("build-libretro", "build"):
            p = ROOT / d / name
            if p.exists():
                return p
    return ROOT / "build-libretro" / "neost_libretro.so"


def sandbox(work):
    system = work / "system" / "neost"
    system.mkdir(parents=True)
    for t in FREE_TOS:
        shutil.copy(ROOT / "roms" / t, system / t)
    (work / "saves").mkdir()
    media = work / "media"
    media.mkdir()
    shutil.copy(ROOT / "disks" / "diskA.st", media / "diskA.st")
    shutil.copy(ROOT / "disks" / "PDATS295.msa", media / "PDATS295.msa")
    shutil.copytree(ROOT / "disks" / "CHUCHU_L", media / "CHUCHU_L")
    (media / "two.m3u").write_text("diskA.st|Boot disk\nPDATS295.msa|Public domain\n")
    return work / "system", work / "saves", media


def core(module, work, options=None, av_enable=3):
    from libretro_frontend import Core
    system, saves, media = sandbox(work)
    return Core(module, system, saves, options or {}, av_enable), media


def write_prg(path, words):
    """Programme GEMDOS minimal : en-tête $601A, texte = `words`, sans relocation."""
    text = b"".join(w.to_bytes(2, "big") for w in words)
    head = (0x601A).to_bytes(2, "big") + len(text).to_bytes(4, "big") + bytes(4 * 5) + bytes(2)
    path.write_bytes(head + text + bytes(4))   # relocation : long nul = aucune


def write_floppy(path, files):
    """Disquette FAT12 720 Ko NON amorçable, `files` = {"NOM.EXT": octets} à la racine."""
    import struct
    img = bytearray(1440 * 512)
    img[0:2] = b"\x60\x1c"
    struct.pack_into("<HBHBHHBHHH", img, 0x0B, 512, 2, 1, 2, 112, 1440, 0xF9, 3, 9, 2)
    root, data = (1 + 2 * 3) * 512, (1 + 2 * 3 + 7) * 512

    def fat(idx, val):
        for base in (512, 512 + 3 * 512):
            off = base + idx * 3 // 2
            if idx & 1:
                img[off] = (img[off] & 0x0F) | ((val << 4) & 0xF0)
                img[off + 1] = (val >> 4) & 0xFF
            else:
                img[off] = val & 0xFF
                img[off + 1] = (img[off + 1] & 0xF0) | ((val >> 8) & 0x0F)
    fat(0, 0xFF9)
    fat(1, 0xFFF)
    cluster = 2
    for slot, (name, content) in enumerate(files.items()):
        base, ext = name.split(".")
        e = root + slot * 32
        img[e:e + 11] = (base.ljust(8) + ext.ljust(3)).encode()
        img[e + 11] = 0x20
        n = max(1, (len(content) + 1023) // 1024)
        struct.pack_into("<HI", img, e + 26, cluster, len(content))
        off = data + (cluster - 2) * 1024
        img[off:off + len(content)] = content
        for k in range(n):
            fat(cluster + k, cluster + k + 1 if k + 1 < n else 0xFFF)
        cluster += n
    path.write_bytes(bytes(img))


SUPER = [0x42A7, 0x3F3C, 0x0020, 0x4E41, 0x5C8F]          # Super(0) : mode superviseur
READ_DMA_SOUND = [0x3038, 0x8900]                          # move.w $FFFF8900.w,d0
LOOP = [0x60FE]                                            # bra.s *


# --- Cas -------------------------------------------------------------------------------
def case_boot_pal(module, work):
    """Sans contenu : EmuTOS PAL choisi, 50 Hz annoncés d'emblée, aucune réannonce."""
    c, _ = core(module, work)
    assert c.load(None), "load failed"
    c.run(400)
    fps = c.av[3]
    assert abs(fps - 50.0527) < 0.01, f"fps {fps}"
    assert not c.av_infos, f"timing re-announced at boot: {c.av_infos}"
    assert c.frame[:2] == (320, 200), f"frame {c.frame[:2]}"
    assert c.av[2] == 1.6, f"aspect {c.av[2]}"
    assert c.lit_fraction() > 0.5, "desktop not drawn"
    assert "50 Hz" in c.messages[0], c.messages
    rate = c.audio_frames / 400
    assert abs(rate - 48000 / 50.0527) < 2, f"audio {rate}/frame"
    return f"{c.messages[0]} | {fps} Hz | {c.frame[0]}x{c.frame[1]} @ {c.av[2]} | {rate:.1f} samples/frame"


def case_boot_ntsc(module, work):
    """TOS US choisi par l'option : 60 Hz annoncés d'emblée, sans réinitialisation.

    Le quartz suit la ROM (Machine::resolveQuartz « auto ») : un ST américain a le quartz
    NTSC de 32,0424 MHz, d'où 8 010 600 / (263 × 508) = 59,958 Hz.
    """
    c, _ = core(module, work, {"neost_tos": "etos192us.img"})
    assert c.load(None)
    c.run(300)
    assert abs(c.av[3] - 59.9578) < 0.01, f"fps {c.av[3]}"
    assert not c.av_infos, f"timing re-announced: {c.av_infos}"
    return f"{c.messages[0]} | {c.av[3]} Hz"


def case_framing(module, work):
    """Cadrage : zone active 320x200, cadre entier 416x276 bordé, annoncé par SET_GEOMETRY."""
    c, _ = core(module, work)
    assert c.load(None)
    c.run(300)
    assert c.frame[:2] == (320, 200), c.frame[:2]
    c.set_option("neost_framing", "full")
    c.run(10)
    assert c.frame[:2] == (416, 276), c.frame[:2]
    assert c.geometries and c.geometries[-1][:2] == (416, 276), c.geometries
    full = c.geometries[-1]
    c.set_option("neost_framing", "active")
    c.run(10)
    assert c.frame[:2] == (320, 200) and c.geometries[-1][:2] == (320, 200), c.geometries
    return f"active 320x200 @ 1.6, full {full[0]}x{full[1]} @ {full[2]}"


def case_mono(module, work):
    """Moniteur mono : 640x400, 71 Hz, même aspect 1,6."""
    c, _ = core(module, work, {"neost_monitor": "mono"})
    assert c.load(None)
    c.run(300)
    assert c.frame[:2] == (640, 400), c.frame[:2]
    fps = c.av_infos[-1][3] if c.av_infos else c.av[3]
    assert 70 < fps < 73, f"fps {fps}"
    assert (c.geometries[-1][2] if c.geometries else c.av[2]) == 1.6
    return f"640x400 @ {fps} Hz"


def case_floppy_private_copy(module, work):
    """Une disquette est copiée dans saves/neost/disks ; l'original n'est jamais écrit."""
    c, media = core(module, work)
    original = media / "diskA.st"
    before = hashlib.sha1(original.read_bytes()).hexdigest()
    assert c.load(original)
    c.run(600)
    copy = work / "saves" / "neost" / "disks" / "diskA.st"
    assert copy.exists(), "no private copy"
    assert hashlib.sha1(original.read_bytes()).hexdigest() == before, "original written"
    return f"{c.messages[0]} | copy at saves/neost/disks/diskA.st"


def case_fast_floppy(module, work):
    """Chargement accéléré : moins de trames hôte pour lire la disquette, même état émulé."""
    def frames_until_motor_off(opts):
        c, media = core(module, work / opts["neost_fast_floppy"], opts)
        assert c.load(media / "PDATS295.msa")
        seen_on = False
        for n in range(1, 3000):
            c.run(1)
            on = bool(c.led.get(0))
            seen_on |= on
            if seen_on and not on:
                return n
        return None
    fast = frames_until_motor_off({"neost_fast_floppy": "enabled"})
    slow = frames_until_motor_off({"neost_fast_floppy": "disabled"})
    assert fast and slow and fast < slow, f"fast {fast}, slow {slow}"
    return f"first drive activity over after {fast} host frames (fast) vs {slow} (exact speed)"


def case_program(module, work):
    """Un .TOS démarre seul depuis son dossier, monté en C:."""
    c, media = core(module, work)
    assert c.load(media / "CHUCHU_L" / "CHUCHU.TOS")
    c.run(900)
    assert "C:" in c.messages[0], c.messages
    return c.messages[0]


def case_playlist(module, work):
    """Liste .m3u : deux disquettes, libellés, R3 passe à la suivante."""
    c, media = core(module, work)
    assert c.load(media / "two.m3u")
    assert c.disk.get_num_images() == 2
    import ctypes as C
    buf = C.create_string_buffer(256)
    assert c.disk.get_image_label(0, buf, 256) and buf.value == b"Boot disk", buf.value
    c.run(100)
    c.pad[0] = {"R3"}
    c.run(2)
    c.pad[0] = set()
    c.run(2)
    assert c.disk.get_image_index() == 1, c.disk.get_image_index()
    assert any("Public domain" in m for m in c.messages), c.messages
    return c.messages[-1]


def case_state_replay(module, work):
    """Un état repris rejoue à l'identique (exécution anticipée, retour arrière, réseau)."""
    c, media = core(module, work)
    assert c.load(media / "diskA.st")
    c.run(500)
    blob = c.save_state()
    assert blob, "serialize refused"

    def scripted():
        hashes = []
        for i in range(120):
            c.pad[0] = {"RIGHT", "B"} if (i // 10) % 2 else {"LEFT"}
            c.mouse[0] = 3 if i % 7 == 0 else 0
            c.run(1)
            hashes.append((c.frame_hash(), hashlib.sha1(c.ram()).hexdigest()[:12]))
        c.pad[0] = set()
        c.mouse[0] = 0
        return hashes

    a = scripted()
    assert c.load_state(blob), "unserialize refused"
    b = scripted()
    assert a == b, f"diverged at frame {next(i for i in range(len(a)) if a[i] != b[i])}"
    return f"state {len(blob)} bytes, 120 frames replayed identically"


def case_runahead_hidden(module, work):
    """Trames cachées (anticipation) : pas de son ni d'image, le cadre reste celui de la trame montrée."""
    c, _ = core(module, work, av_enable=0)
    assert c.load(None)
    c.run(200)
    assert c.audio_frames == 0 and c.frame is None, (c.audio_frames, c.frame is None)
    c.av_enable = 3
    c.run(5)
    assert c.frame[:2] == (320, 200)
    return "hidden frames: no audio, no picture; shown frames as usual"


def case_needs_ste(module, work):
    """Machine automatique : un programme qui lit le son DMA du STE sans gestionnaire
    de bus error redémarre seul en STE (docs/LIBRETRO.md)."""
    c, media = core(module, work)
    write_prg(media / "NEEDSTE.PRG", SUPER + READ_DMA_SOUND + LOOP)
    assert c.load(media / "NEEDSTE.PRG")
    c.run(1500)
    assert any("restarted as Atari STE" in m for m in c.messages), c.messages
    return c.messages[-1]


def case_probe_kept(module, work):
    """Un programme qui pose son gestionnaire de bus error et sonde le STE reste sur ST."""
    c, media = core(module, work)
    # lea handler(pc),a0 ; move.l a0,$8.w ; lecture ; bra.s * ; handler: lea 14(sp),sp ; bra.s *
    words = SUPER + [0x41FA, 0x000C, 0x21C8, 0x0008] + READ_DMA_SOUND + LOOP + [0x4FEF, 0x000E] + LOOP
    write_prg(media / "PROBE.PRG", words)
    assert c.load(media / "PROBE.PRG")
    c.run(1500)
    assert not any("restarted" in m for m in c.messages), c.messages
    assert "Atari ST " in c.messages[0], c.messages
    return "probe caught by the program: still an Atari ST"


def case_autorun(module, work):
    """Disquette non amorçable à un seul programme : il démarre seul (io/FloppyCatalog)."""
    c, media = core(module, work)
    prg = media / "RED.PRG"
    write_prg(prg, SUPER + [0x31FC, 0x0700, 0x8240] + LOOP)   # move.w #$700,$FFFF8240.w : fond rouge
    write_floppy(media / "Red game.st", {"README.TXT": b"hello\r\n", "RED.PRG": prg.read_bytes()})
    assert c.load(media / "Red game.st")
    assert "starts A:\\RED.PRG" in c.messages[0], c.messages
    c.run(1500)
    # Couleur 0 passée au rouge : la moitié de la trame du bureau, au moins.
    w, h, raw = c.frame
    red = sum(1 for i in range(0, len(raw), 4) if raw[i + 2] > 200 and raw[i + 1] < 40 and raw[i] < 40)
    assert red > w * h * 0.3, f"{red} red pixels of {w * h}"
    return c.messages[0]


CASES = {n[5:]: f for n, f in sorted(globals().items()) if n.startswith("case_")}


def _worker(name, module, q):
    work = Path(tempfile.mkdtemp(prefix=f"neost-lr-{name}-"))
    try:
        q.put((True, CASES[name](module, work)))
    except AssertionError as e:
        q.put((False, f"assertion: {e}"))
    except Exception:
        q.put((False, traceback.format_exc(limit=3)))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", type=Path, default=default_module())
    ap.add_argument("--only")
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    if not args.module.exists():
        print(f"module not found: {args.module} (cmake -DNEOST_PIC=ON -DNEOST_BUILD_LIBRETRO=ON)")
        return 2
    ctx = mp.get_context("spawn")
    results, failed = {}, 0
    for name in CASES:
        if args.only and name != args.only:
            continue
        q = ctx.Queue()
        p = ctx.Process(target=_worker, args=(name, args.module.resolve(), q))
        p.start()
        p.join(300)
        ok, detail = q.get() if not q.empty() else (False, f"process exit {p.exitcode}")
        results[name] = {"ok": ok, "detail": detail}
        failed += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {name:24s} {detail}")
    if args.json:
        args.json.write_text(json.dumps(results, indent=2))
    print(f"\n{len(results) - failed}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
