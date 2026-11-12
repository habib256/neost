#!/usr/bin/env bash
# =============================================================================
#  pgo_train.sh — Parcours d'ENTRAÎNEMENT pour la compilation guidée par profil.
#
#  Appelé par build_native_pi.sh --pgo entre les deux passes de compilation. Le
#  binaire passé en argument est la version INSTRUMENTÉE (-fprofile-generate) :
#  chaque exécution dépose des compteurs, que la seconde passe relit.
#
#  POURQUOI le PGO compte ici plus qu'ailleurs : la boucle chaude de NeoST est
#  l'interpréteur Moira, c'est-à-dire un branchement indirect sur l'opcode suivi
#  d'un très grand nombre de branches conditionnelles rares. Sans profil, GCC
#  suppose les deux issues équiprobables ; avec, il ordonne les blocs de façon
#  que le cas fréquent tombe en séquence — moins de sauts pris, moins de pression
#  sur le prédicteur, et surtout un cache d'instructions bien mieux utilisé. Sur
#  un Cortex-A72 (32 Ko de L1i, prédicteur modeste face à un cœur x86 de bureau)
#  c'est précisément là que se joue le débit.
#
#  CE QUE LE PARCOURS DOIT COUVRIR — un profil trop étroit est pire qu'aucun
#  profil : il fait déclarer « froid » du code qui ne l'est pas. On balaie donc
#  les grandes familles de charge, pas seulement un boot :
#    · boot TOS et bureau GEM (ST et STE, 50 et 60 Hz)
#    · un jeu en basse résolution avec musique YM et lecteur disquette
#    · une démo à effets de bordure / écritures freq-res (chemin Glue complet)
#    · la haute résolution monochrome (chemin de rendu séparé)
#    · le blitter et le son DMA du STE
#
#  Chaque famille a une charge PUBLIQUE (EmuTOS + disquettes suivies par git),
#  donc présente en CI ; les TOS Atari et les jeux privés ne font qu'enrichir le
#  profil sur la machine de l'auteur.
#
#  Usage :  pgo_train.sh <chemin/neost-headless> [racine-du-dépôt]
#  PGO_MIN_PUBLIC=N : nombre minimal de charges publiques réussies (défaut : toutes).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -uo pipefail

HEADLESS="${1:?usage: pgo_train.sh <neost-headless> [racine]}"
ROOT="${2:-$(cd "$(dirname "$0")/../.." && pwd)}"
cd "$ROOT"

[ -x "$HEADLESS" ] || { echo "ERREUR : $HEADLESS introuvable ou non exécutable"; exit 1; }

# DEUX SORTES DE CHARGES (2026-11-12). En CI, une seule charge tournait : les autres
# exigeaient tos102uk/tos162uk ou des disquettes privées, absents du runner, et la
# charge No Cooper ne vérifiait que sa disquette, pas sa ROM (« ignoré »). Le profil
# des paquets Pi n'avait donc vu qu'un boot EmuTOS de 300 trames — exactement le
# profil trop étroit que l'en-tête interdit.
#   · PUBLIQUES : ROM EmuTOS et disquettes suivies par git (disks/diskA.st,
#     disks/etalons/). Présentes partout, chacune DOIT réussir : le script échoue
#     sous PGO_MIN_PUBLIC réussites (défaut : toutes). Un profil appauvri ne sort
#     plus en silence.
#   · OPTIONNELLES : TOS Atari et jeux privés, chez l'auteur seulement. Absentes,
#     elles sont sautées ; leur échec ne fait pas échouer le build.
PUB_TOTAL=0; PUB_OK=0; OPT_OK=0
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

pub() {                      # pub <libellé> <arguments…> — charge publique, obligatoire
    local label="$1"; shift
    PUB_TOTAL=$((PUB_TOTAL + 1))
    printf '  [pgo] %-40s' "$label"
    if "$HEADLESS" "$@" >/dev/null 2>"$LOG"; then
        PUB_OK=$((PUB_OK + 1)); echo "ok"
    else
        echo "ÉCHEC"; tail -n 5 "$LOG" | sed 's/^/        /'
    fi
}

opt() {                      # opt <libellé> <arguments…> — charge privée, facultative
    local label="$1"; shift
    printf '  [pgo] %-40s' "$label"
    if "$HEADLESS" "$@" >/dev/null 2>&1; then OPT_OK=$((OPT_OK + 1)); echo "ok"; else echo "ignoré"; fi
}

# Un fichier manque → on saute la charge optionnelle qui en dépend (plutôt que de
# compter sur le code d'erreur, qui entraînerait alors les chemins d'ERREUR).
have() { local f; for f in "$@"; do [ -f "$f" ] || return 1; done; }

echo "[pgo_train] parcours d'entraînement — $HEADLESS"

# --- 1. Boot / bureau : ST et STE, 50 et 60 Hz (EmuTOS) ---------------------
# La disquette est nommée : la commande ne dépend pas du répertoire de lancement.
DA=disks/diskA.st
pub "boot EmuTOS 192K PAL (ST)"    roms/etos192fr.img --machine st  --mem 1m --disk "$DA" --frames 400
pub "boot EmuTOS 192K NTSC 60 Hz"  roms/etos192us.img --machine st  --disk "$DA" --frames 400
pub "boot EmuTOS 256K PAL (STE)"   roms/etos256fr.img --machine ste --disk "$DA" --frames 400

# --- 2. Haute résolution monochrome : autre chemin de rendu -----------------
pub "boot monochrome (640x400)"    roms/etos256us.img --machine ste --mono --disk "$DA" --frames 300

# --- 3. Chemin Glue complet : retraits de bordure, écritures freq/res, YM ----
# Mêmes ROM, machine et touche que les étalons homonymes (tools/etalons.json).
pub "overscan med-res (No Cooper)" roms/etos192fr.img --machine st --mem 1m --fastfdc \
    --disk disks/etalons/nocooper.msa --keys-at 900 " " --frames 1200
pub "bordures + YM (Cuddly Demos)" roms/etos192fr.img --machine st --mem 512k --fastfdc \
    --disk disks/etalons/cuddly_demos.msa --frames 1200
pub "Spectrum 512 (spec512_view)"  roms/etos192fr.img --machine st --mem 512k --fastfdc \
    --disk disks/etalons/spec512_view.st --frames 402

# --- 4. Blitter et son DMA du STE --------------------------------------------
pub "blitter HOG (blitter_hog)"    roms/etos256us.img --machine ste --mem 512k \
    --disk disks/etalons/blitter_hog.st --frames 400
pub "son DMA STE (dmasnd_poll)"    roms/etos256us.img --machine ste --mem 1m \
    --disk disks/etalons/dmasnd_poll.st --frames 400

# --- 5. Auto-tests : Glue, Spectrum 512, bus, MFP ---------------------------
# Déterministes, sans dépendance à une image de disquette, et ils exercent des
# chemins (re-rendu palette par pixel, carte de bus error) qu'aucun boot n'atteint.
pub "auto-test Glue"               roms/etos256fr.img --glue-selftest    --frames 60
pub "auto-test Spectrum512"        roms/etos256fr.img --spec512-selftest --frames 60
pub "auto-test bus error"          roms/etos256fr.img --bus-selftest     --frames 60
pub "auto-test MFP"                roms/etos256fr.img --mfp-selftest     --frames 60

# --- 6. Optionnelles : TOS Atari et jeux privés -----------------------------
T162=roms/tos162uk.img
have "$T162"           && opt "boot TOS 1.62 PAL (STE)"  "$T162" --frames 400
have roms/tos102uk.img && opt "boot TOS 1.02 PAL (ST)"   roms/tos102uk.img --machine st --mem 1m --frames 400
have roms/tos162us.img && opt "boot TOS 1.62 NTSC 60 Hz" roms/tos162us.img --frames 300
NZS="disks/st/New Zealand Story.st"
have "$T162" "$NZS" && opt "jeu (New Zealand Story)" "$T162" --disk "$NZS" --fastfdc --frames 700
EL="disks/st/Enchanted Land (1990)(Thalion).st"
have "$T162" "$EL"  && opt "démo/jeu à raster (Enchanted Land)" "$T162" --disk "$EL" --fastfdc --frames 700

echo "[pgo_train] charges publiques : $PUB_OK/$PUB_TOTAL réussies, optionnelles : $OPT_OK"
MIN="${PGO_MIN_PUBLIC:-$PUB_TOTAL}"
if [ "$PUB_OK" -lt "$MIN" ]; then
    echo "ERREUR : $PUB_OK charge(s) publique(s) réussie(s) sur $PUB_TOTAL (minimum $MIN)" \
         "— profil PGO trop étroit, build refusé"
    exit 1
fi

echo "[pgo_train] terminé."
