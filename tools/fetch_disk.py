#!/usr/bin/env python3
# =============================================================================
#  fetch_disk.py — Récupère une image disquette Atari ST pour les tests NeoST.
#
#  Zone de téléchargement : https://www.planetemu.net/machine/atari-st
#  Utilise « scrapling » (scraper Python) pour analyser la page et trouver le
#  lien, télécharge dans disks/, et dézippe les .st si besoin.
#
#  ⚠ Usage : uniquement pour des logiciels auxquels tu as droit (domaine public,
#  freeware, démos, ou tes propres sauvegardes). Sert à tester l'émulateur.
#
#  Dépendance : pip install scrapling   (sinon fallback urllib pour le download)
#
#  Exemples :
#    python3 tools/fetch_disk.py https://www.planetemu.net/rom/atari-st-...    # page
#    python3 tools/fetch_disk.py https://.../jeu.zip                            # direct
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import io, os, re, sys, zipfile, urllib.request

BASE = "https://www.planetemu.net/machine/atari-st"
DISKS_DIR = os.path.join(os.path.dirname(__file__), "..", "disks")
ETALONS_DIR = os.path.join(DISKS_DIR, "etalons")
EXTS = (".st", ".msa", ".stx", ".zip")
UA = {"User-Agent": "Mozilla/5.0 (NeoST test fetcher)"}


def find_download_links(url):
    """Analyse une page planetemu avec scrapling, renvoie les liens candidats."""
    try:
        from scrapling.fetchers import Fetcher
    except ImportError:
        sys.exit("scrapling absent — `pip install scrapling`, ou passe un lien direct.")
    page = Fetcher.get(url, stealthy_headers=True)
    hrefs = page.css("a::attr(href)") or []
    links = []
    for h in hrefs:
        h = str(h)
        low = h.lower()
        if low.endswith(EXTS) or "download" in low or "telecharg" in low:
            links.append(h if h.startswith("http") else urllib.request.urljoin(url, h))
    # dédoublonne en gardant l'ordre
    seen, out = set(), []
    for l in links:
        if l not in seen:
            seen.add(l); out.append(l)
    return out


def download(url, dest_dir):
    print(f"  ↓ {url}")
    req = urllib.request.Request(url, headers=UA)
    data = urllib.request.urlopen(req, timeout=60).read()
    name = os.path.basename(url.split("?")[0]) or "disk.bin"
    # zip → extrait les images disquette
    if data[:2] == b"PK":
        zf = zipfile.ZipFile(io.BytesIO(data))
        got = []
        for n in zf.namelist():
            if n.lower().endswith((".st", ".msa", ".stx")):
                out = os.path.join(dest_dir, os.path.basename(n))
                open(out, "wb").write(zf.read(n))
                got.append(out)
        return got
    out = os.path.join(dest_dir, name)
    open(out, "wb").write(data)
    return [out]


def main():
    if len(sys.argv) < 2:
        sys.exit(f"usage: {sys.argv[0]} [--dest DIR] <url planetemu | lien direct>\n"
                 f"zone planetemu : {BASE}")
    dest = DISKS_DIR
    args = sys.argv[1:]
    if args[0] == "--dest":
        if len(args) < 3:
            sys.exit(f"usage: {sys.argv[0]} --dest DIR <url>")
        dest = args[1]
        args = args[2:]
    url = args[0]
    os.makedirs(dest, exist_ok=True)
    targets = [url] if url.lower().endswith(EXTS) else find_download_links(url)
    if not targets:
        sys.exit("aucun lien de téléchargement trouvé sur la page.")
    saved = []
    for t in targets:
        try:
            saved += download(t, dest)
            if saved:
                break          # un disque suffit
        except Exception as e:  # noqa
            print(f"  (échec {t}: {e})")
    if not saved:
        sys.exit("rien de récupéré.")
    print("Images montables dans NeoST (Disk Library) :")
    for s in saved:
        print("  ", os.path.relpath(s))


if __name__ == "__main__":
    main()
