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
    shutil.copy(ROOT / "disks" / "CHUCHU_L.ZIP", media / "CHUCHU_L.ZIP")
    (media / "two.m3u").write_text("diskA.st|Boot disk\nPDATS295.msa|Public domain\n")
    return work / "system", work / "saves", media


def core(module, work, options=None, av_enable=3, language=0):
    from libretro_frontend import Core
    system, saves, media = sandbox(work)
    return Core(module, system, saves, options or {}, av_enable, language), media


def write_prg(path, words):
    """Programme GEMDOS minimal : en-tête $601A, texte = `words`, sans relocation."""
    text = b"".join(w.to_bytes(2, "big") for w in words)
    head = (0x601A).to_bytes(2, "big") + len(text).to_bytes(4, "big") + bytes(4 * 5) + bytes(2)
    path.write_bytes(head + text + bytes(4))   # relocation : long nul = aucune


def fat12_volume(files):
    """Volume FAT12 de 720 Ko (BPB d'une disquette 2 faces, 9 secteurs), NON amorçable.
    `files` = {"NOM.EXT" ou "DOSSIER/NOM.EXT": octets} ; un niveau de dossiers."""
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
    state = {"cluster": 2}

    def entry(at, name, attr, cluster, size):
        base, _, ext = name.partition(".")
        img[at:at + 11] = (base.ljust(8) + ext.ljust(3)).encode()
        img[at + 11] = attr
        struct.pack_into("<HI", img, at + 26, cluster, size)

    def alloc(content):
        c = state["cluster"]
        n = max(1, (len(content) + 1023) // 1024)
        off = data + (c - 2) * 1024
        img[off:off + len(content)] = content
        for k in range(n):
            fat(c + k, c + k + 1 if k + 1 < n else 0xFFF)
        state["cluster"] += n
        return c

    dirs, slot = {}, 0
    for path, content in files.items():
        folder, _, name = path.rpartition("/")
        if folder and folder not in dirs:
            c = alloc(bytes(1024))
            entry(root + slot * 32, folder, 0x10, c, 0)
            slot += 1
            d = data + (c - 2) * 1024
            entry(d, ".", 0x10, c, 0)
            entry(d + 32, "..", 0x10, 0, 0)
            img[d:d + 11] = b".          "
            img[d + 32:d + 43] = b"..         "
            dirs[folder] = [d, 2]
        c = alloc(content)
        if folder:
            d, n = dirs[folder]
            entry(d + n * 32, name, 0x20, c, len(content))
            dirs[folder][1] += 1
        else:
            entry(root + slot * 32, name, 0x20, c, len(content))
            slot += 1
    return bytes(img)


def write_floppy(path, files):
    """Disquette FAT12 720 Ko NON amorçable (cf. fat12_volume)."""
    path.write_bytes(fat12_volume(files))


def write_hard_disk(path, files):
    """Disque dur ACSI : secteur racine Atari (taille du disque à $1C2, table de partitions
    à $1C6 : drapeau, « GEM », début, taille, en grand-boutiste) et UNE partition FAT12
    au secteur 2. Non amorçable sous TOS Atari (pas de pilote) ; EmuTOS la monte seul."""
    import struct
    vol = fat12_volume(files)
    sectors = 2 + len(vol) // 512
    rootsec = bytearray(512)
    struct.pack_into(">I", rootsec, 0x1C2, sectors)
    rootsec[0x1C6] = 0x01
    rootsec[0x1C7:0x1CA] = b"GEM"
    struct.pack_into(">II", rootsec, 0x1CA, 2, len(vol) // 512)
    path.write_bytes(bytes(rootsec) + bytes(512) + vol)


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
    """Chargement accéléré : ChuChu Rocket (330 Ko) copié sur une disquette non amorçable,
    lancé par l'autorun ; trames de l'hôte jusqu'à son premier écran coloré."""
    def frames_to_title(opts):
        c, media = core(module, work / opts["neost_fast_floppy"], opts)
        files = {p.name: p.read_bytes() for p in sorted((media / "CHUCHU_L").iterdir())}
        write_floppy(media / "ChuChu.st", files)
        assert c.load(media / "ChuChu.st")
        assert "starts A:\\CHUCHU.TOS" in c.messages[0], c.messages
        for n in range(1, 6000):
            c.run(1)
            if n % 10 == 0 and len(set(c.frame[2][i:i + 3] for i in range(0, len(c.frame[2]), 4 * 13))) > 8:
                return n
        return None
    fast = frames_to_title({"neost_fast_floppy": "enabled"})
    slow = frames_to_title({"neost_fast_floppy": "disabled"})
    assert fast and slow and fast < slow, f"fast {fast}, slow {slow}"
    return f"ChuChu Rocket title after {fast} host frames (fast) vs {slow} (exact speed)"


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
    # La carte mémoire suit la Machine neuve (triches, succès).
    assert len(c.memory_maps) == 2, c.memory_maps
    assert c.memory_maps[-1] == (c.lib.retro_get_memory_data(2), c.lib.retro_get_memory_size(2)), c.memory_maps
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


def case_zip_program(module, work):
    """Un jeu GEMDOS zippé : l'arbre extrait une fois, le programme démarre sur C:."""
    c, media = core(module, work)
    assert c.load(media / "CHUCHU_L.ZIP")
    assert "on C:" in c.messages[0] and "files on C:" in c.messages[0], c.messages
    c.run(900)
    assert c.lit_fraction() > 0.3
    return c.messages[0]


def case_zip_disks(module, work):
    """Un zip de deux disquettes : une liste, dans l'ordre des noms ; R3 passe à la suivante."""
    import zipfile
    c, media = core(module, work)
    with zipfile.ZipFile(media / "Two disks.zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.write(media / "diskA.st", "Game (Disk 1 of 2).st")
        z.write(media / "PDATS295.msa", "Game (Disk 2 of 2).msa")
    assert c.load(media / "Two disks.zip")
    assert c.disk.get_num_images() == 2, c.disk.get_num_images()
    assert "2 disks in the archive" in c.messages[0], c.messages
    c.run(60)
    c.pad[0] = {"R3"}
    c.run(2)
    c.pad[0] = set()
    c.run(2)
    assert c.disk.get_image_index() == 1
    unpacked = work / "saves" / "neost" / "archives" / "Two disks"
    assert (unpacked / "Game (Disk 1 of 2).st").exists(), list(unpacked.iterdir())
    return c.messages[0]


def case_gzip(module, work):
    """Une disquette .st.gz : décompressée une fois, montée en A:."""
    import gzip
    c, media = core(module, work)
    (media / "diskA.st.gz").write_bytes(gzip.compress((media / "diskA.st").read_bytes()))
    assert c.load(media / "diskA.st.gz")
    assert "A: diskA.st" in c.messages[0], c.messages
    c.run(300)
    return c.messages[0]


def case_options_french(module, work):
    """RetroArch en français : options traduites (SET_CORE_OPTIONS_V2_INTL), anglais sinon."""
    c, _ = core(module, work, language=2)
    assert c.option_labels == ("local", "Machine (redémarrage)"), c.option_labels
    return f"french labels: {c.option_labels[1]}"


def case_fast_floppy_idle(module, work):
    """Lecteur vide (moteur jamais coupé, faute d'index) : pas d'accélération. Une touche
    tenue 4 trames donne UN caractère, comme sans accélération."""
    c, media = core(module, work)
    write_prg(media / "KEY.PRG", SUPER + [0x3F3C, 0x0001, 0x4E41, 0x548F, 0x31C0, 0x8240, 0x60F2])
    assert c.load(media / "KEY.PRG")
    c.run(900)
    c.key("a", True); c.run(4); c.key("a", False); c.run(20)
    # L'écho de Cconin s'écrit en haut à gauche, avant le titre « KEY.PRG » (x ≥ 120) :
    # un « q » fait une dizaine de points d'encre, une touche répétée en ferait dix fois plus.
    w, h, raw = c.frame
    ink = sum(1 for x in range(100) for y in range(1, 10)
              if raw[(y * w + x) * 4] < 40 and raw[(y * w + x) * 4 + 1] < 40)
    assert 0 < ink < 30, f"{ink} ink pixels in the echo line: the key repeated"
    return f"one echoed character ({ink} ink pixels), drive empty, motor on"


def case_hard_disk(module, work):
    """Image de disque dur (.hd) : montée en ACSI 0, EmuTOS lit sa partition et lance
    C:\\AUTO\\RED.PRG au démarrage (fond rouge)."""
    c, media = core(module, work)
    prg = media / "RED.PRG"
    write_prg(prg, SUPER + [0x31FC, 0x0700, 0x8240] + LOOP)
    write_hard_disk(media / "Games.hd", {"AUTO/RED.PRG": prg.read_bytes(), "README.TXT": b"hd\r\n"})
    assert c.load(media / "Games.hd")
    assert "ACSI hard disk Games.hd" in c.messages[0], c.messages
    c.run(1500)
    w, h, raw = c.frame
    red = sum(1 for i in range(0, len(raw), 4) if raw[i + 2] > 200 and raw[i + 1] < 40 and raw[i] < 40)
    assert red > w * h * 0.3, f"{red} red pixels of {w * h}: C:\\AUTO not run"
    assert (work / "saves" / "neost" / "harddisks" / "Games.hd").exists(), "no private copy"
    return c.messages[0]


def case_touch_pointer(module, work):
    """Option « Touch moves the GEM pointer » : un double tap sur l'icône du lecteur A du
    bureau EmuTOS ouvre sa fenêtre (zone blanche unie au milieu de l'écran)."""
    c, media = core(module, work, {"neost_pointer": "enabled"})
    assert c.load(media / "diskA.st")
    c.run(700)
    def white():
        w, h, raw = c.frame
        return sum(1 for y in range(60, 180) for x in range(60, 300)
                   if raw[(y * w + x) * 4] > 200 and raw[(y * w + x) * 4 + 1] > 200 and raw[(y * w + x) * 4 + 2] > 200)
    before = white()
    icon = (36 / 320, 30 / 200)          # icône « DISQUE A » d'EmuTOS FR, basse résolution
    c.touch(*icon, hold=2, after=2)
    c.touch(*icon, hold=2, after=120)
    after = white()
    assert after > before + 8000, f"white pixels {before} -> {after}: the window did not open"
    return f"double tap on drive A: {before} -> {after} white pixels (window open)"


def case_drive_sound(module, work):
    """Bruits du lecteur : un boot sur disquette (EmuTOS muet) porte nettement plus de son
    avec eux que sans ; les trames cachées de l'anticipation ne sonnent pas."""
    energies = {}
    for opt in ("enabled", "disabled"):
        c, media = core(module, work / opt, {"neost_drive_sound": opt, "neost_fast_floppy": "disabled"})
        shutil.copytree(ROOT / "roms" / "drivesound" / "epson_smd480l", work / opt / "system" / "neost" / "drivesound")
        assert c.load(media / "diskA.st")
        c.measure_audio = True
        c.run(400)
        energies[opt] = c.audio_energy / max(1, c.audio_frames)
    assert energies["enabled"] > 4 * max(energies["disabled"], 1.0), energies
    return f"mean |sample| with drive sounds {energies['enabled']:.0f}, without {energies['disabled']:.0f}"


def case_netplay_keyboard(module, work):
    """Port 3 « clavier pour le netplay » : la touche tenue est relue chaque trame ; un
    rappel clavier est alors ignoré (le netplay de RetroArch ne l'enverrait pas)."""
    c, media = core(module, work)
    # Super ; boucle : Cconin → d0 ; move.w d0,$FFFF8240.w (couleur 0 = code ASCII) ; bra
    write_prg(media / "KEY.PRG", SUPER + [0x3F3C, 0x0001, 0x4E41, 0x548F, 0x31C0, 0x8240, 0x60F2])
    assert c.load(media / "KEY.PRG")
    c.set_port(2, 3)                     # RETRO_DEVICE_KEYBOARD
    c.run(900)
    def colour0():
        # Couleur la plus présente : le fond (couleur 0, tramée avec du vert au bureau).
        from collections import Counter
        w, h, raw = c.frame
        return Counter((raw[i + 2], raw[i + 1], raw[i]) for i in range(0, len(raw), 4 * 7)).most_common(1)[0][0]
    before = colour0()
    c.key("b", True); c.run(5); c.key("b", False); c.run(20)   # rappel : ignoré
    assert colour0() == before, f"callback key reached the ST: {before} -> {colour0()}"
    c.keys = {ord("a")}; c.run(4); c.keys = set(); c.run(20)  # port 3 : « a » = $61
    after = colour0()
    assert after != before, f"polled key not seen: {after}"
    return f"background {before} -> {after} after 'a' on the keyboard port"


def case_probe_restored(module, work):
    """Le motif de Wings of Death : poser un gestionnaire, sonder, remettre le vecteur du
    TOS dans la même trame. En fin de trame le vecteur est de nouveau en ROM ; seul celui
    EN VIGUEUR pendant la faute (Bus::lastMissingIoVector) dit que le programme la rattrape."""
    c, media = core(module, work)
    # move.l $8.w,a1 ; lea handler(pc),a0 ; move.l a0,$8.w ; lecture du son DMA ;
    # cont: move.l a1,$8.w ; bra.s * ; handler: lea 14(sp),sp ; bra.s cont
    words = SUPER + [0x2278, 0x0008, 0x41FA, 0x0010, 0x21C8, 0x0008] + READ_DMA_SOUND + \
        [0x21C9, 0x0008] + LOOP + [0x4FEF, 0x000E, 0x60F4]
    write_prg(media / "PROBE2.PRG", words)
    assert c.load(media / "PROBE2.PRG")
    c.run(1500)
    assert not any("restarted" in m or "needs" in m for m in c.messages), c.messages
    vec = int.from_bytes(c.ram()[8:12], "big") & 0xFFFFFF
    assert vec >= 0xE00000, f"vector not restored: ${vec:06X}"
    return f"probe with a handler, vector restored to ${vec:06X}: still an Atari ST"


def case_ste_name(module, work):
    """Le mot STE isolé dans le nom démarre en STE ; « Stellar » ne le fait pas."""
    c, media = core(module, work / "a")
    shutil.copy(media / "diskA.st", media / "racer_ste_v1.st")
    assert c.load(media / "racer_ste_v1.st")
    assert c.messages[0].startswith("Atari STE"), c.messages
    c.close()
    c, media = core(module, work / "b")
    shutil.copy(media / "diskA.st", media / "Stellar Steel.st")
    assert c.load(media / "Stellar Steel.st")
    assert c.messages[0].startswith("Atari ST "), c.messages
    return "racer_ste_v1.st → STE, Stellar Steel.st → ST"


def case_tos_fallback(module, work):
    """Repli de TOS : une disquette qui devait démarrer seule (programme dans \\AUTO) et
    n'offre rien d'autre laisse la machine au bureau — redémarrage avec un autre TOS,
    sans boucle (les TOS déjà essayés ne reviennent pas)."""
    c, media = core(module, work)
    quit_prg = media / "QUIT.PRG"
    write_prg(quit_prg, [0x4267, 0x4E41])          # clr.w -(sp) ; trap #1 : Pterm0
    write_floppy(media / "Boots.st", {"AUTO/QUIT.PRG": quit_prg.read_bytes(), "DATA.BIN": b"x" * 100})
    assert c.load(media / "Boots.st")
    c.run(3000)
    restarts = [m for m in c.messages if "did not start with" in m]
    assert len(restarts) == 1, c.messages
    assert "EmuTOS FR" in restarts[0] and "restarted with EmuTOS US" in restarts[0], restarts
    return restarts[0]


def case_sibling_disks(module, work):
    """Disquettes sœurs : « (Disk 1 of 2) »/« (Disk 2 of 2) » forment une liste ; un
    original et son crack (« [cr Elite] ») ne la forment pas."""
    c, media = core(module, work / "a")
    for n in ("Game (Disk 1 of 2).st", "Game (Disk 2 of 2).st", "Other (1991).st", "Other (1991)[cr Elite].st"):
        shutil.copy(media / "diskA.st", media / n)
    assert c.load(media / "Game (Disk 1 of 2).st")
    assert c.disk.get_num_images() == 2, c.disk.get_num_images()
    c.close()
    c, media = core(module, work / "b")
    for n in ("Other (1991).st", "Other (1991)[cr Elite].st"):
        shutil.copy(media / "diskA.st", media / n)
    assert c.load(media / "Other (1991).st")
    assert c.disk.get_num_images() == 1, c.disk.get_num_images()
    return "Disk 1/2 paired; original and crack kept apart"


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
