#!/usr/bin/env bash
# Oracle Hatari headless : boot ST + disque → AVI PNG → frame PNG extraite.
# Usage : hatari_oracle.sh <tos> <disk> <run-vbls> <frame-n> <out.png> [machine]
set -euo pipefail
# Racine du dépôt = parent du dossier tools/ (robuste au nom du dossier, ex. neost↔NEOST).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -x "$ROOT/extern/hatari/build/src/hatari" ]]; then
  HATARI="$ROOT/extern/hatari/build/src/hatari"
elif command -v hatari >/dev/null; then
  HATARI="$(command -v hatari)"
else
  echo "hatari introuvable (extern/hatari ou PATH)" >&2; exit 1
fi
# ---------------------------------------------------------------- ÉPINGLAGE --
# Chantier A5. `extern/hatari` est gitignoré et n'est PAS un sous-module : rien ne
# le rapatrie, rien ne fixe SA VERSION, et la recette documentée faisait un
# `git clone --depth 1` — c'est-à-dire « le HEAD du jour ». Deux oracles bâtis à
# deux semaines d'écart pouvaient donc produire des références différentes sans
# que rien ne le signale, alors que TOUTE la méthode du projet repose sur lui.
# On ne bloque pas (un oracle plus récent reste utilisable) : on AVERTIT, pour
# qu'une référence régénérée contre un autre oracle soit visible dans le journal.
HATARI_PIN=36a8acf02bb3f08749d1a4db165f85328e60793b   # v2.6.1-devel, 2026-11-24
if [[ -d "$ROOT/extern/hatari/.git" ]]; then
  HAVE=$(git -C "$ROOT/extern/hatari" rev-parse HEAD 2>/dev/null || echo inconnu)
  if [[ "$HAVE" != "$HATARI_PIN" ]]; then
    echo "⚠ oracle Hatari NON ÉPINGLÉ : ${HAVE:0:12} au lieu de ${HATARI_PIN:0:12}." >&2
    echo "  Les références oracle du dépôt ont été posées avec la version épinglée." >&2
    echo "  Réaligner : tools/setup_hatari.sh   (ou assumer et re-poser les références)" >&2
  fi
fi

TOS=${1:?tos}; DISK=${2:?disk}; VBLS=${3:-1400}; FRAME=${4:-1300}; OUT=${5:-/tmp/hatari_frame.png}
MACHINE=${6:-st}
# 8ᵉ arg : taille RAM en Mo au format Hatari (--memsize : 0 = 512 Ko, 1 = 1 Mo…).
# SANS elle, Hatari bootait avec sa taille par défaut alors que NeoST utilise celle de
# l'étalon : timeline de boot différente, donc numéros de trame décalés — c'est ce qui
# rendait l'oracle de spectrum512_diapo inexploitable (image noire).
MEMSIZE=${8:-0}
# 7ᵉ arg optionnel « fastfdc » : aligne la timeline oracle sur un run NeoST --fastfdc
# (sinon FDC vitesse réelle — les numéros de trame ne correspondent PAS entre les deux).
FASTFDC=()
[[ "${7:-}" == "fastfdc" ]] && FASTFDC=(--fastfdc on)
# ⚠ `--cpu-exact on --compatible on` sont passés EXPLICITEMENT, alors que ce sont
# aujourd'hui les défauts d'Hatari. Mesuré le 2026-08-26 sur l'étalon blitter_timer :
# forcer les deux à `off` déplace la comparaison de 69 px (397 → 328). Une référence
# oracle dépend donc de ces deux réglages, et s'en remettre au DÉFAUT d'un binaire
# qu'on ne contrôle pas serait laisser une référence bouger sans qu'aucune ligne du
# dépôt n'ait changé.
# Chemins UNIQUES par processus : deux --oracle concurrents sur la même machine
# se marchaient dessus via /tmp/hatari_oracle.avi partagé (la frame extraite
# pouvait venir de l'AVI de L'AUTRE étalon, puis être installée en référence).
AVI=/tmp/hatari_oracle.$$.avi
LOG=/tmp/hatari_oracle.$$.log
# HATARI_ORACLE_KEEP=1 : garder l'AVI et le journal (diagnostic d'un run qui sort court).
[[ -n "${HATARI_ORACLE_KEEP:-}" ]] || trap 'rm -f "$AVI" "$LOG"' EXIT
rm -f "$AVI"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home
mkdir -p /tmp/hatari_home
# --avirecord : booléen explicite (« on ») requis sur le build Linux (extern/hatari),
# refusé par le binaire Homebrew macOS (flag nu). On tente « on », repli flag nu.
# --drive-led off : sans lui, Hatari INCRUSTE une LED disquette clignotante en haut à
# droite de chaque image, même avec --statusbar off. C'est elle qui imposait le masque
# `buffer_noled` de compare_screenshot.py et qui a coûté les « 22 px inexpliqués » de
# trace_odd (liseré de LED mêlé au fond par le sous-échantillonnage 2×). Réglé à la
# source le 2026-09-01 : la zone est noire, vérifié sur blitter_hog. Le masque reste
# nécessaire tant que des références COMMISES portent encore la LED.
COMMON=(--machine "$MACHINE" --tos "$TOS" --monitor rgb --memsize "$MEMSIZE"
        --cpu-exact on --compatible on --disk-a "$DISK"
        --sound off --fast-forward on --confirm-quit off --statusbar off --drive-led off
        --frameskips 0 --alert-level fatal ${FASTFDC[@]+"${FASTFDC[@]}"} --run-vbls "$VBLS")

# -------------------------------------------------- PILOTAGE CLAVIER (A11) --
# HATARI_ORACLE_KEYS="down:up:scancode [down:up:scancode …]" : touche ENFONCÉE à la
# VBL `down`, RELÂCHÉE à la VBL `up`, précis à la VBL près et sans aucune attente
# horloge. Mécanisme, mesuré le 2026-09-01 sur le build épinglé :
#   · `--parse` pose un point d'arrêt `b VBL = N :once` par événement : Hatari s'y
#     GÈLE et attend une commande du débogueur sur STDIN (une fifo à nous) ;
#   · on pousse alors « hatari-event keydown/keyup <scancode> » dans la fifo de
#     contrôle (`--cmd-fifo`), puis « c » sur stdin : l'événement, lu par la boucle
#     SDL au tour suivant (sdl/gui_event.c → Control_CheckUpdates), est appliqué à
#     la VBL N+1 — déterministe, à ±1 VBL ;
#   · ⚠ le fast-forward N'EST PAS désactivé par --cmd-fifo, contrairement à ce que
#     docs/HATARI_AUTOMATION.md affirmait : 562,9 VBL/s avec, 565,0 sans. La recette
#     « temps réel + sleep » de la doc était une contrainte imaginaire.
# Tout prompt du débogueur reçoit un « c », même inattendu (vu : ré-entrée après un
# `:file`) — la boucle ne se fie qu'au « VBL=N » imprimé par le prompt pour décider
# quels événements sont dus. C'est ce qui rend re-dérivable l'oracle de `nocooper`
# (espace tenue vers la VBL 900), posé jusqu'ici à la main.
run_hatari() {
  if [[ -z "${HATARI_ORACLE_KEYS:-}" ]]; then
    "$HATARI" "${COMMON[@]}" "$@" --avi-vcodec png --avi-file "$AVI" >"$LOG" 2>&1 || true
    return 0
  fi
  local PARSE=/tmp/hatari_oracle.$$.parse CFIFO=/tmp/hatari_oracle.$$.cmd SFIFO=/tmp/hatari_oracle.$$.stdin
  rm -f "$PARSE" "$CFIFO" "$SFIFO"; mkfifo "$SFIFO"
  local -a EV=()                      # "vbl event scancode", trié par vbl
  local spec d u sc
  for spec in $HATARI_ORACLE_KEYS; do
    IFS=: read -r d u sc <<<"$spec"
    [[ "$d" =~ ^[0-9]+$ && "$u" =~ ^[0-9]+$ && "$sc" =~ ^[0-9]+$ && "$u" -gt "$d" ]] \
      || { echo "HATARI_ORACLE_KEYS : entrée invalide « $spec » (attendu down:up:scancode)" >&2; return 1; }
    # Scancode en hexadécimal : Hatari lit une clé d'UN seul caractère comme un caractère à
    # simuler (control.c, Control_InsertKey), « keydown 2 » tapait « 2 » = scancode 3.
    printf -v sc '0x%02x' "$sc"
    EV+=("$d keydown $sc" "$u keyup $sc")
    printf 'b VBL = %d :once\nb VBL = %d :once\n' "$d" "$u" >>"$PARSE"
  done
  # Tri par VBL sans `mapfile` : le bash 3.2 de macOS (celui de /bin/bash, que
  # run_etalons.py invoque par « bash ») ne le connaît pas.
  local -a SORTED=(); local line
  while IFS= read -r line; do SORTED+=("$line"); done < <(printf '%s\n' "${EV[@]}" | sort -n)
  EV=("${SORTED[@]}")
  "$HATARI" "${COMMON[@]}" "$@" --cmd-fifo "$CFIFO" --parse "$PARSE" \
      --avi-vcodec png --avi-file "$AVI" <"$SFIFO" >"$LOG" 2>&1 &
  local HP=$!
  exec 4>"$SFIFO"                                     # tient stdin ouvert
  local n=0; while [[ ! -p "$CFIFO" ]] && (( n < 400 )); do sleep 0.05; n=$((n+1)); done
  [[ -p "$CFIFO" ]] || { echo "Hatari n'a pas créé la fifo de contrôle $CFIFO" >&2; kill $HP 2>/dev/null; wait $HP 2>/dev/null; return 1; }
  # Le writer n'est ouvert que le temps d'un message : un writer connecté en
  # permanence fait rendre EAGAIN à chaque lecture non bloquante, et Hatari le
  # journalise (« command FIFO read error ») à CHAQUE trame — sans conséquence,
  # mais illisible. Sans writer, read() rend 0 et se tait.
  local seen=0 cur v i=0 deadline=$((SECONDS + 1800))
  while kill -0 $HP 2>/dev/null && (( SECONDS < deadline )); do
    cur=$(grep -c 'VBL=[0-9]' "$LOG" 2>/dev/null || true)
    if (( cur > seen )); then
      seen=$cur
      v=$(grep -o 'VBL=[0-9]*' "$LOG" | tail -1 | cut -d= -f2)
      while (( i < ${#EV[@]} )) && (( ${EV[$i]%% *} <= v )); do
        set -- ${EV[$i]}                               # vbl event scancode
        echo "hatari-event $2 $3" > "$CFIFO"
        echo "oracle clavier : VBL=$v → $2 $3" >&2
        i=$((i + 1))
      done
      echo "c" >&4
    fi
    sleep 0.05
  done
  if kill -0 $HP 2>/dev/null; then echo "oracle clavier : délai dépassé, Hatari tué" >&2; kill $HP 2>/dev/null; fi
  wait $HP 2>/dev/null || true
  exec 4>&-
  rm -f "$PARSE" "$CFIFO" "$SFIFO"
  (( i == ${#EV[@]} )) || { echo "oracle clavier : ${#EV[@]} événement(s) attendus, $i envoyé(s) — le run s'est terminé avant (VBLS=$VBLS trop court ?)" >&2; return 1; }
  return 0
}
run_hatari --avirecord on
if [[ ! -s "$AVI" ]]; then
  run_hatari --avirecord
fi
if [[ ! -s "$AVI" ]]; then
  echo "hatari n'a produit aucun AVI (crash ? voir le log) :" >&2
  tail -20 "$LOG" >&2 || true
  exit 1
fi
# Le log est un fichier temporaire supprimé à la sortie : on remonte la ligne du
# script joystick (patch NeoST, cf. tools/hatari_neost_oracle.patch), sinon un
# oracle tournant SANS le patch se distingue mal d'un oracle qui l'applique — et
# un script silencieusement ignoré rendrait toute comparaison mensongère.
if [[ -n "${NEOST_JOY_SCRIPT:-}" ]]; then
  if grep -q "\[NEOST\] joystick script loaded" "$LOG"; then
    grep -m1 "\[NEOST\] joystick script" "$LOG"
  else
    echo "ATTENTION : NEOST_JOY_SCRIPT est posé mais hatari ne l'a pas chargé —" >&2
    echo "  le patch tools/hatari_neost_oracle.patch est-il appliqué ET recompilé ?" >&2
    exit 1
  fi
fi
# HATARI_ORACLE_SCAN=N : au lieu d'UNE image, extrait la FENÊTRE [FRAME-N, FRAME+N]
# dans "<OUT sans .png>.scan/f_%05d.png" (l'appelant choisit). Indispensable pour les
# étalons qui BOOTENT UN DISQUE : Hatari fait `Hatari_srand(time(NULL))` (sdl/main_sdl.c)
# et tire au hasard la POSITION ANGULAIRE INITIALE de la disquette
# (fdc.c, IndexPulse_Time = ... - Hatari_rand() % ...), donc la durée du boot — et
# donc la numérotation des trames — CHANGE d'un run à l'autre. Mesuré le 2026-08-19
# sur cuddly_demos : la même trame NeoST tombait sur la trame Hatari n+61 dans un run
# et n-2 dans un autre. Un `frame:` figé ne peut pas être juste par construction ;
# la fenêtre, elle, retrouve l'image.
if [[ -n "${HATARI_ORACLE_SCAN:-}" ]]; then
  SCAN_N="$HATARI_ORACLE_SCAN"
  SCAN_DIR="${OUT%.png}.scan"
  rm -rf "$SCAN_DIR"; mkdir -p "$SCAN_DIR"
  LO=$(( FRAME - SCAN_N )); [[ $LO -lt 0 ]] && LO=0
  HI=$(( FRAME + SCAN_N ))
  # ⚠ -reinit_filter 0 est OBLIGATOIRE (2026-09-01). Hatari encode chaque image PNG
  # en pal8 dès qu'elle tient en 256 couleurs, en rgb24 sinon : le format change donc
  # sans arrêt le long de l'AVI. Sans cette option, ffmpeg RECONSTRUIT son graphe de
  # filtres à chaque changement et le compteur `n` de select REPART DE ZÉRO — la
  # trame demandée n'est plus la bonne, ou n'existe plus (« frame N hors de l'AVI »
  # alors que ffprobe compte bien N images). Vu sur nocooper, trame 1000/1100.
  ffmpeg -y -loglevel error -reinit_filter 0 -i "$AVI" -vf "select='between(n,$LO,$HI)'" \
         -vsync 0 -start_number "$LO" -pix_fmt rgb24 "$SCAN_DIR/f_%05d.png"
  N=$(ls "$SCAN_DIR" | wc -l | tr -d ' ')
  if [[ "$N" -eq 0 ]]; then
    echo "fenêtre $LO-$HI hors de l'AVI (${VBLS} VBL demandées)" >&2; exit 1
  fi
  echo "oracle fenêtre $LO-$HI ($N images) -> $SCAN_DIR ($MACHINE)"
  exit 0
fi

# Extrait la frame demandée (l'AVI a 1 image par VBL avec --frameskips 0).
# ⚠ OUT est supprimé D'ABORD et vérifié APRÈS : si la frame demandée dépasse la
# fin de l'AVI (Hatari sorti court), ffmpeg n'écrit RIEN et sort 0 — un PNG
# périmé d'un run précédent aurait alors été installé comme référence.
rm -f "$OUT"
ffmpeg -y -loglevel error -reinit_filter 0 -i "$AVI" -vf "select=eq(n\,$FRAME)" \
       -frames:v 1 -update 1 -pix_fmt rgb24 "$OUT"      # -reinit_filter 0 : cf. la fenêtre
if [[ ! -s "$OUT" ]]; then
  echo "frame $FRAME hors de l'AVI (${VBLS} VBL demandées — Hatari sorti court ?)" >&2
  exit 1
fi
echo "oracle frame $FRAME -> $OUT ($MACHINE)"
identify "$OUT" 2>/dev/null || true
