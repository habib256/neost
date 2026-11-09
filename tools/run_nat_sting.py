#!/usr/bin/env python3
"""STinG/ENEC → NAT → HTTP loopback, avec DNS localhost, sans accès Internet.

Le disque suivi est lu seulement ; le PRG de test est remplacé dans une copie
temporaire. Le serveur utilise un port éphémère choisi par le système.
"""
from __future__ import annotations

import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from make_sting_test import build_code, build_prg

ROOT = Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / ("neost-headless.exe" if sys.platform == "win32" else "neost-headless")


def replace_program(image: bytes, program: bytes) -> bytes:
    """Remplace AUTO/STGTEST.PRG dans les clusters déjà alloués du FAT12."""
    disk = bytearray(image)
    sector = struct.unpack_from("<H", disk, 11)[0]
    cluster_size = disk[13] * sector
    fat = struct.unpack_from("<H", disk, 14)[0] * sector
    root = fat + disk[16] * struct.unpack_from("<H", disk, 22)[0] * sector
    root_size = struct.unpack_from("<H", disk, 17)[0] * 32
    data = root + (root_size + sector - 1) // sector * sector

    def chain(first):
        seen = set()
        while 2 <= first < 0xFF8:
            if first in seen:
                raise ValueError("cyclic FAT12 chain")
            seen.add(first)
            yield data + (first - 2) * cluster_size
            value = struct.unpack_from("<H", disk, fat + first * 3 // 2)[0]
            first = (value >> 4 if first & 1 else value) & 0xFFF

    def find(blocks, length, name):
        for block in blocks:
            for offset in range(block, block + length, 32):
                if disk[offset:offset + 11] == name:
                    return offset
        raise ValueError("missing FAT12 entry: " + repr(name))

    auto = find([root], root_size, b"AUTO       ")
    clusters = chain(struct.unpack_from("<H", disk, auto + 26)[0])
    entry = find(clusters, cluster_size, b"STGTEST PRG")

    def replace(entry, payload):
        blocks = list(chain(struct.unpack_from("<H", disk, entry + 26)[0]))
        if len(payload) > len(blocks) * cluster_size:
            raise ValueError("test file exceeds its existing allocation")
        for index, block in enumerate(blocks):
            disk[block:block + cluster_size] = payload[index * cluster_size:(index + 1) * cluster_size].ljust(cluster_size, b"\0")
        struct.pack_into("<I", disk, entry + 28, len(payload))

    replace(entry, program)
    # STinG ajoute son DOMAIN aux noms sans point. Le nom localhost doit arriver
    # tel quel au résolveur hôte, sans devenir localhost.sting.org.
    sting = find([root], root_size, b"STING      ")
    directory = chain(struct.unpack_from("<H", disk, sting + 26)[0])
    config_entry = find(directory, cluster_size, b"DEFAULT CFG")
    blocks = chain(struct.unpack_from("<H", disk, config_entry + 26)[0])
    size = struct.unpack_from("<I", disk, config_entry + 28)[0]
    config = b"".join(bytes(disk[block:block + cluster_size]) for block in blocks)[:size]
    config, changed = re.subn(rb"(?im)^DOMAIN[ \t]*=[^\r\n]*", b"DOMAIN = ", config)
    if changed != 1:
        raise ValueError("expected one STinG DOMAIN setting")
    replace(config_entry, config)
    return bytes(disk)


def main() -> int:
    requests = []
    errors = []
    marker = b"NEOST-NAT-LOCAL-HTTP"
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(90)
        port = listener.getsockname()[1]

        def serve():
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(10)
                    request = b""
                    while b"\r\n\r\n" not in request and len(request) < 4096:
                        part = peer.recv(4096)
                        if not part:
                            break
                        request += part
                    requests.append(request)
                    peer.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: " + str(len(marker)).encode()
                                 + b"\r\nConnection: close\r\n\r\n" + marker)
            except OSError as error:
                errors.append(str(error))

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        with tempfile.TemporaryDirectory(prefix="neost-nat-sting-") as tmp:
            disk = Path(tmp) / "sting.st"
            serial = Path(tmp) / "serial.txt"
            config = Path(tmp) / "neost.cfg"
            config.write_text("ethernec=1\nnat=1\nnat_allow_loopback=1\nnat_restricted=0\n")
            program = build_prg(build_code("localhost", port=port, connect_ip=0x0A000202))
            disk.write_bytes(replace_program((ROOT / "disks/sting_ethernec.st").read_bytes(), program))
            cp = subprocess.run([str(HEADLESS), "roms/etos256us.img", "--machine", "ste", "--mem", "1m",
                                 "--from-cfg", str(config), "--disk", str(disk),
                                 "--disk-ro", "--frames", "2200", "--serial-dump", str(serial)],
                                cwd=ROOT, capture_output=True, text=True, timeout=180)
            output = serial.read_text(errors="replace") if serial.exists() else ""
        worker.join(timeout=1)
    expected = ("DNS=127.0.0.1", "STG: TCP connected", "HTTP/1.0 200 OK", marker.decode())
    missing = [value for value in expected if value not in output]
    if cp.returncode or missing or not requests or not requests[0].startswith(b"GET / HTTP/1.0\r\n"):
        print("NAT STinG FAIL:", missing, errors, "exit", cp.returncode)
        print(output)
        print(cp.stderr[-4000:])
        return 1
    print("NAT STinG OK: DNS localhost, GET HTTP, response via NE2000; no Internet access")
    return 0


if __name__ == "__main__":
    sys.exit(main())
