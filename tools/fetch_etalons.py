#!/usr/bin/env python3
# =============================================================================
#  fetch_etalons.py — Rapatrie les disques freeware listés dans etalons.json.
#
#  Usage :
#    python3 tools/fetch_etalons.py           # tout
#    python3 tools/fetch_etalons.py cuddly_demos union_demo
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import io
import json
import os
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = Path(__file__).resolve().parent / "etalons.json"
UA = {"User-Agent": "Mozilla/5.0 (NeoST etalon fetcher)"}


def download_zip_member(url: str, member: str, dest: Path) -> None:
    print(f"  ↓ {url}")
    req = urllib.request.Request(url, headers=UA)
    data = urllib.request.urlopen(req, timeout=90).read()
    if data[:2] != b"PK":
        raise RuntimeError("archive ZIP attendue")
    zf = zipfile.ZipFile(io.BytesIO(data))
    names = {n.lower(): n for n in zf.namelist()}
    key = member.lower()
    if key not in names:
        raise RuntimeError(f"{member} absent de l'archive ({zf.namelist()[:5]}…)")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(zf.read(names[key]))
    print(f"  → {dest.relative_to(ROOT)}")


def fetch_entry(entry: dict) -> bool:
    fetch = entry.get("fetch")
    if not fetch:
        return True
    dest = ROOT / entry["disk"]
    if dest.exists():
        print(f"[skip] {entry['id']} : {dest.relative_to(ROOT)} déjà présent")
        return True
    url = fetch.get("url")
    if not url:
        print(f"[skip] {entry['id']} : pas d'URL directe ({fetch.get('hint', '')})")
        return False
    if "member" in fetch:
        download_zip_member(url, fetch["member"], dest)
        return True
    # fallback : délègue à fetch_disk.py (planetemu / lien direct)
    import subprocess
    dest_dir = str(dest.parent)
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "fetch_disk.py"),
                        "--dest", dest_dir, url], check=False)
    if r.returncode != 0:
        print(f"[échec] {entry['id']} — {fetch.get('hint', 'téléchargement manuel requis')}")
        return False
    # renomme le 1er fichier récupéré si besoin
    if not dest.exists():
        for p in sorted(dest.parent.glob("*")):
            if p.is_file() and p.suffix.lower() in (".st", ".msa", ".stx"):
                p.rename(dest)
                print(f"  → renommé en {dest.relative_to(ROOT)}")
                break
    return dest.exists()


def main() -> int:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    want = set(sys.argv[1:]) if len(sys.argv) > 1 else None
    ok = True
    for entry in data["etalons"]:
        if want and entry["id"] not in want:
            continue
        if not entry.get("fetch"):
            continue
        print(f"[fetch] {entry['id']} — {entry['name']}")
        if not fetch_entry(entry):
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
