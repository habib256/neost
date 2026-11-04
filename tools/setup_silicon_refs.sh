#!/usr/bin/env bash
# =============================================================================
#  setup_silicon_refs.sh — Rapatrie RTL et documents primaires, À LEUR ÉPINGLE.
#
#  POURQUOI. Le TODO citait fx68k et MiSTery par numéro de ligne sans révision ni
#  copie locale : une citation qu'on ne pouvait ni vérifier ni dater. Ils forment
#  l'échelon `rtl` (docs/REFERENCE_LADDER.md), que rien ne rapatriait.
#
#  extern/fx68k et extern/mistery sont gitignorés, lus et JAMAIS liés au binaire
#  (GPL-3). Rien n'y est compilé ici. Idempotent ; une épingle ne bouge que par
#  décision humaine, car elle déplace toutes les citations de ligne.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FX68K_PIN=0602ee4627b10f301298f2673d826cdd6baa9327     # ijor/fx68k, 2021-02-16
MISTERY_PIN=a00deb5b4e964124987d80ae854163b3edbd7555   # gyurco/MiSTery, 2025-03-21

fetch() {  # <url> <dossier> <sha>
  [[ -d "$2/.git" ]] || git clone -q "$1" "$2"
  git -C "$2" cat-file -e "$3^{commit}" 2>/dev/null || git -C "$2" fetch -q origin
  git -C "$2" checkout -q --detach "$3"
}

fetch https://github.com/ijor/fx68k.git "$ROOT/extern/fx68k" "$FX68K_PIN"
fetch https://github.com/gyurco/MiSTery.git "$ROOT/extern/mistery" "$MISTERY_PIN"
# TG68K.C reste hors champ : autre cœur, sans rapport avec le 68000 de référence.
git -C "$ROOT/extern/mistery" submodule update -q --init gstmcu fdc1772 ikbd fx68k

# RTL seul (job CI des bancs) : les documents ne servent qu'à relire, et leurs sites tombent parfois.
if [[ "${1:-}" == "--rtl-only" ]]; then
  exit 0
fi

# Documents primaires (grade B), sous copyright : jamais versionnés, seules leur URL et
# leur empreinte le sont. Une empreinte qui change arrête le script : un autre scan change
# les numéros de page cités dans le code.
DOCS="$ROOT/extern/datasheets"
BITS=http://www.bitsavers.org/components
mkdir -p "$DOCS"
doc() {  # <fichier> <sha256> <url>
  [[ -s "$DOCS/$1" ]] || curl -fsSL -A 'Mozilla/5.0' -o "$DOCS/$1" "$3"
  echo "$2  $DOCS/$1" | shasum -a 256 -c --status \
    || { echo "empreinte inattendue : $1" >&2; exit 1; }
}
doc MC68901_Jan84.pdf 5441a092562cc5b55bb167298030647622132e0061c0be8d976e25fb60ba959c \
    "$BITS/motorola/68000/MC68901_Multi-Function_Peripheral_Jan84.pdf"
doc MK68901.pdf 8a032fa12da2d0e15fb9f4ca8747d96fffb3d07321d55099464f8a00086a8c9b \
    https://www.komponenten.es.aau.dk/fileadmin/komponenten/Data_Sheet/Microprossor/MK68901.pdf
doc M68000UM_Rev8_1993.pdf e41cbe7e14dc7cb853f1185adb1dcd7043d2a2a3f43cdc909e0f6c146007a8e5 \
    "$BITS/motorola/68000/68000/M68000UM_AD_M68000_Microprocessor_Users_Manual_Rev8_1993.pdf"
doc MC68881_MC68882_UM_1987.pdf 4d3f90294ed05859032db7a2b8e03af03cc9f197a0b4274a7f49a6f252dc8381 \
    "$BITS/motorola/68000/68020/MC68881_MC68882_Floating-Point_Coprocessor_Users_Manual_1ed_1987.pdf"
doc YM2149_199209.pdf a4b946bf525fc1a7c6dec9e7662ce2342d3a3c627f980c7c3b1a396df7e91d20 \
    "$BITS/yamaha/YM2149_199209.pdf"
doc WD177X-00.pdf a2b267094ec9af10c0f4d68fbc5c6ea94ec873897ad9b129a2ff607ad7fe8388 \
    "https://hxc2001.com/download/datasheet/floppy/thirdparty/FDC/WesternDigital/WD%201773%20Floppy%20Disk%20Controller.pdf"
doc MC6850.pdf 294a8b9f557d03a2a7f4d736d060244f39216dad2d5479d2e47954feb4f34655 \
    https://docs.ampnuts.ru/eevblog.docs/Tektronix/TLA_Ensoniq/Datasheets/MC6850.pdf
doc Zilog_SCC_ESCC_UM.pdf 2a13683ee0e423ee3cdf2781a4ce6b4df65c1e069a269e4234fa6964b4a3bea0 \
    https://www.zilog.com/docs/serial/scc_escc_um.pdf
doc Zilog_PS0117.pdf feb4b46341bf20cab3ce5e555b02b7c13e1c2422cb12a9ba2dbfa121d0a34faa \
    https://www.zilog.com/docs/serial/ps0117.pdf
doc RP5C15_Ricoh.pdf b929eeb470dfc0df7b2673cd3810debe17d08e8e1f155126e6c274dc0fadcf3e \
    https://static.datasheets.com/doc/21798444-ricoh-rj5c15-ds.pdf
# Transcription tierce du microcode du 68000 : même origine que fx68k, donc pas un témoin
# indépendant de lui. Chaque mot y dit quels accès bus la séquence fait, et dans quel ordre.
doc m68k_microcode_all.txt f678abb9ec4f32ac19a0b67cf7a9e5d1ab8c581e38588d2cdc5d17577e88eb8b \
    https://og.kervella.org/m68k/all.txt
doc ASICs-new.zip 89512ee7d588b60d1ea964e048152a66c04672560c216862ff7e9f797ca06097 \
    https://www.chzsoft.de/asic-web/ASICs-new.zip
# Noms d'entrée à antislashs : unzip les normalise, pas zipfile de Python.
mkdir -p "$DOCS/asic"
(cd "$DOCS/asic" && { unzip -qo ../ASICs-new.zip '*.pdf' '*.PDF' '*.txt' 2>/dev/null || true; })
[[ -s "$DOCS/asic/Atari_ASICs/ASIC/4081_COMBO/4081/DWG/pdf/ST4081S.PDF" ]] \
  || { echo "schéma ST4081S absent après extraction" >&2; exit 1; }

echo "fx68k   ${FX68K_PIN:0:12}"
echo "MiSTery ${MISTERY_PIN:0:12} (gstmcu $(git -C "$ROOT/extern/mistery/gstmcu" rev-parse --short=12 HEAD))"
