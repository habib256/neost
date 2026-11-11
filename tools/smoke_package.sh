#!/usr/bin/env bash
# =============================================================================
#  smoke_package.sh — Smoke-test du PAQUET LIVRÉ (AppImage, .app, .zip Windows).
#
#  Pourquoi ce script existe : jusqu'ici la CI ne faisait qu'AMORCER le binaire
#  du paquet (--version + 500 trames d'EmuTOS + capture non uniforme). Elle
#  n'ouvrait jamais ni la disquette ni le disque dur EMBARQUÉS — deux défauts
#  livrés pendant des mois sont passés par ce trou :
#    · #38 — disks/diskA.st écrasée par un test d'écriture secteur : plus aucun
#      système de fichiers, inutilisable sous TOS, dans TOUS les paquets ;
#    · #37 — HD GEMDOS mort sous Windows : un chemin absolu à lettre de lecteur
#      était pris pour relatif, donc « GEMDOS folder not found » sur tout dossier
#      glissé (le lecteur C: n'a jamais fonctionné sur ce paquet).
#  Les deux se voient en deux commandes depuis le paquet — d'où les phases 3 et 4.
#
#  Le script tourne depuis la RACINE DU DÉPÔT (il a besoin de tools/) mais teste
#  le binaire et les données DU PAQUET, jamais ceux de l'arbre de travail.
#
#  Usage :
#      tools/smoke_package.sh <dossier_paquet> <binaire_relatif> [version_attendue]
#
#  Exemples (les trois dispositions empaquetées) :
#      tools/smoke_package.sh squashfs-root/usr        bin/neost-headless      0.5.3
#      tools/smoke_package.sh dist/NeoST.app/Contents  MacOS/neost-headless    0.5.3
#      tools/smoke_package.sh _check/NeoST-0.5.3-...   ./neost-headless.exe    0.5.3
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"

if [ $# -lt 2 ]; then
    echo "usage : tools/smoke_package.sh <dossier_paquet> <binaire_relatif> [version]" >&2
    exit 2
fi
PKG="$(cd "$1" && pwd)"
EXE="${2#./}"                     # « ./neost-headless.exe » → « neost-headless.exe »
WANT_VERSION="${3:-}"
ROM="roms/etos192us.img"          # seul TOS présent dans TOUS les paquets (EmuTOS, GPL)
DISK="disks/diskA.st"

say() { printf '\n\033[1m— %s\033[0m\n' "$1"; }

cd "$PKG"
[ -x "$EXE" ] || { echo "ERREUR : $PKG/$EXE absent ou non exécutable" >&2; exit 1; }
[ -f "$ROM" ]  || { echo "ERREUR : $PKG/$ROM absent du paquet" >&2; exit 1; }

# Nettoyage systématique : le paquet est déjà zippé à ce stade, mais on ne laisse
# pas traîner de capture ni de dossier de test dedans (ceinture et bretelles).
# Le nettoyage REND d'abord l'écriture : la phase 5 retire le droit d'écrire sur le
# paquet, et un échec au milieu laisserait sinon un arbre non supprimable.
cleanup() {
    chmod -R u+w "$PKG" 2>/dev/null || true
    rm -rf "$PKG/smoke_boot.ppm" "$PKG/smoke_disk.ppm" "$PKG/smoke_gemdos"
}
trap cleanup EXIT

# --- 1) Version annoncée = version du paquet --------------------------------
# Le define était figé sur le project() de CMake : tous les paquets d'une release
# se présentaient comme « 0.1.0 » et aucun rapport de bug n'était rattachable.
say "1/5 version"
"./$EXE" --version
if [ -n "$WANT_VERSION" ]; then
    "./$EXE" --version | grep -qF "$WANT_VERSION" \
        || { echo "ERREUR : version binaire ≠ $WANT_VERSION" >&2; exit 1; }
fi

# --- 2) Boot EmuTOS : la capture ne doit pas être uniforme ------------------
say "2/5 boot EmuTOS (500 trames)"
"./$EXE" "$ROM" --frames 500 --screenshot smoke_boot.ppm
python3 "$REPO/tools/check_ppm_nonuniform.py" smoke_boot.ppm

# --- 3) DISQUETTE LIVRÉE : montée, et réellement formatée (issue #38) -------
say "3/5 disquette livrée ($DISK)"
[ -f "$DISK" ] || { echo "ERREUR : $PKG/$DISK absent du paquet" >&2; exit 1; }
# a) structure : FAT12 720 Ko conforme au générateur du dépôt.
python3 "$REPO/tools/check_disk_assets.py" --image "$PKG/$DISK"
# b) l'émulateur du paquet la monte vraiment (le FDC journalise la géométrie lue).
"./$EXE" "$ROM" --disk "$DISK" --frames 500 --screenshot smoke_disk.ppm 2> smoke_disk.log
grep -q "drive A:" smoke_disk.log \
    || { echo "ERREUR : le FDC n'a pas monté la disquette livrée :" >&2
         cat smoke_disk.log >&2; exit 1; }
grep "drive A:" smoke_disk.log
python3 "$REPO/tools/check_ppm_nonuniform.py" smoke_disk.ppm
rm -f smoke_disk.log

# --- 4) HD GEMDOS sur un chemin ABSOLU de l'hôte (issue #37) ----------------
# LE cas qui cassait sous Windows : un chemin à lettre de lecteur (« D:\a\… »)
# était traité comme relatif et préfixé du répertoire courant. On passe donc
# délibérément un chemin ABSOLU au format NATIF de l'hôte — d'où cygpath sous
# MSYS2, pour ne pas dépendre de la conversion automatique des arguments.
say "4/5 disque dur GEMDOS (chemin absolu hôte)"
mkdir -p smoke_gemdos/SUBDIR
printf 'NeoST smoke test\r\n' > smoke_gemdos/HELLO.TXT
GD="$PKG/smoke_gemdos"
if command -v cygpath >/dev/null 2>&1; then
    GD="$(cygpath -w "$GD")"
fi
echo "dossier monté : $GD"
"./$EXE" "$ROM" --gemdos "$GD" --frames 300 2> smoke_gemdos.log || {
    echo "ERREUR : l'émulateur a échoué avec --gemdos" >&2; cat smoke_gemdos.log >&2; exit 1; }
grep -q "HDD GEMDOS : C:" smoke_gemdos.log \
    || { echo "ERREUR : dossier hôte NON monté en C: (chemin absolu mal résolu ?) :" >&2
         cat smoke_gemdos.log >&2; exit 1; }
grep "HDD GEMDOS : C:" smoke_gemdos.log
# Une évasion du bac à sable signale un chemin mal résolu même quand le montage
# a l'air réussi (c'était le symptôme résiduel du bug Windows).
if grep -q "REFUSED" smoke_gemdos.log; then
    echo "ERREUR : accès refusés par le bac à sable — chemins mal résolus :" >&2
    grep "REFUSED" smoke_gemdos.log >&2; exit 1
fi
rm -f smoke_gemdos.log

# --- 5) LE SUPPORT DE LIVRAISON EST EN LECTURE SEULE (chantier A12) ---------
# Les quatre phases ci-dessus écrivent DANS le paquet (captures, journaux, dossier
# GEMDOS) : elles exigent donc un dossier inscriptible, et c'est ce qui les rend
# structurellement incapables d'exercer le support réel. Or un utilisateur ne lance
# JAMAIS un dossier extrait — il lance un .dmg monté ou un AppImage, tous deux en
# LECTURE SEULE. Le défaut du 2026-09-01 est passé exactement par là : le paquet
# n'enregistrait aucun réglage sur 5 paquets sur 8, et aucun palier ne pouvait le
# voir puisque tous testaient une copie inscriptible.
#
# Cette phase-ci ne prouve pas le réglage (l'écriture de neost.cfg est le fait du
# binaire GUI, qu'aucune CI ne peut lancer faute d'affichage — elle est gardée par
# `neost-selftest`). Elle prouve la PROPRIÉTÉ dont ce défaut n'était qu'un cas :
# le binaire livré démarre et rend une image sans rien écrire dans son paquet.
#
# ⚠ `chmod` plutôt qu'un vrai montage : le montage dépend de la plateforme (hdiutil,
# FUSE, droits root) alors que le retrait du droit d'écriture capture la même
# propriété partout. Sous MSYS2 il est largement inopérant — la phase y est donc
# faible, et c'est écrit plutôt que sous-entendu.
say "5/5 paquet en LECTURE SEULE (support de livraison réel)"
RO_WORK="$(mktemp -d)"
chmod -R a-w "$PKG"
if [ -w "$PKG" ]; then
    echo "note : le droit d'écriture n'a pas pu être retiré (plateforme ou droits) — phase non concluante"
else
    # Tout en ABSOLU : la phase tourne hors du paquet (c'est le but — un utilisateur
    # lance depuis n'importe où), et `neost-headless` résout `--disk` par rapport
    # au RÉPERTOIRE COURANT. Sans chemin absolu la disquette n'est pas trouvée et
    # le run échoue — le contrôle rougirait pour une raison qui n'a rien à voir
    # avec la lecture seule. (Mesuré le 2026-09-01 ; le frontend GUI, lui, résout
    # par rapport au binaire. Depuis le 2026-11-11, sans --disk, le lecteur A du
    # headless reste vide.)
    ( cd "$RO_WORK" && "$PKG/$EXE" "$PKG/$ROM" --disk "$PKG/$DISK" \
        --frames 500 --screenshot "$RO_WORK/ro.ppm" ) \
        || { echo "ERREUR : le binaire livré échoue quand son paquet est en lecture seule" >&2
             chmod -R u+w "$PKG"; rm -rf "$RO_WORK"; exit 1; }
    python3 "$REPO/tools/check_ppm_nonuniform.py" "$RO_WORK/ro.ppm"
    # Rien ne doit avoir été déposé dans le paquet.
    STRAY=$(find "$PKG" -newer "$RO_WORK" -type f 2>/dev/null | head -3)
    test -z "$STRAY" || { echo "ERREUR : le paquet a été écrit alors qu'il est en lecture seule : $STRAY" >&2
                          chmod -R u+w "$PKG"; rm -rf "$RO_WORK"; exit 1; }
    echo "OK : démarre depuis un support en lecture seule, sans rien y écrire"
fi
chmod -R u+w "$PKG"
rm -rf "$RO_WORK"

say "OK : paquet $PKG validé (version, boot, disquette livrée, HD GEMDOS, lecture seule)"
