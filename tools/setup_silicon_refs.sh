#!/usr/bin/env bash
# =============================================================================
#  setup_silicon_refs.sh — Rapatrie RTL et documents primaires, À LEUR ÉPINGLE.
#
#  POURQUOI. Le TODO citait fx68k et MiSTery par numéro de ligne sans révision ni
#  copie locale : une citation qu'on ne pouvait ni vérifier ni dater. Ils forment
#  l'échelon `rtl` (docs/REFERENCE_LADDER.md), que rien ne rapatriait.
#
#  extern/fx68k et extern/mistery sont gitignorés, lus et JAMAIS liés au binaire
#  (GPL-3). Rien n'y est compilé ici. MAME (grade D, comme Hatari) n'entre dans
#  extern/mame que par les fichiers que le code cite, pour qu'une citation
#  `fichier:ligne @ révision` se rouvre. Idempotent ; une épingle ne bouge que par
#  décision humaine, car elle déplace toutes les citations de ligne.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FX68K_PIN=0602ee4627b10f301298f2673d826cdd6baa9327     # ijor/fx68k, 2021-02-16
MISTERY_PIN=a00deb5b4e964124987d80ae854163b3edbd7555   # gyurco/MiSTery, 2025-03-21
MAME_PIN=ce391499d37dc21a4cf8fba85e4b774698ff2945      # mamedev/mame, 2026-11-10

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

# MAME : dépôt de plusieurs Go, donc clone superficiel, sans blobs, réduit aux fichiers
# cités dans src/ (MFP, SCC, FDC, RTC, YM, IPF, RS-232, MMU et carte de l'Atari ST, FPU, MT-32).
fetch_sparse() {  # <url> <dossier> <sha> <chemins…>
  local url=$1 dir=$2 sha=$3; shift 3
  [[ -d "$dir/.git" ]] || { git init -q "$dir"; git -C "$dir" remote add origin "$url"; }
  git -C "$dir" sparse-checkout set --no-cone "$@"
  git -C "$dir" cat-file -e "$sha^{commit}" 2>/dev/null \
    || git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$sha"
  git -C "$dir" checkout -q --detach "$sha"
}
fetch_sparse https://github.com/mamedev/mame.git "$ROOT/extern/mame" "$MAME_PIN" \
  /src/devices/machine/mc68901.cpp /src/devices/machine/mc68901.h \
  /src/devices/machine/z80scc.cpp /src/devices/machine/z80scc.h \
  /src/devices/machine/wd_fdc.cpp /src/devices/machine/wd_fdc.h \
  /src/devices/machine/rp5c15.cpp /src/devices/machine/rp5c15.h \
  /src/devices/sound/ay8910.cpp /src/devices/sound/ay8910.h \
  /src/lib/formats/ipf_dsk.cpp /src/lib/formats/ipf_dsk.h \
  /src/devices/bus/rs232/rs232.cpp /src/devices/bus/rs232/rs232.h \
  /src/mame/atari/atarist.cpp /src/mame/atari/stmmu.cpp /src/mame/atari/stmmu.h \
  /src/devices/cpu/m68000/m68kfpu.cpp /src/mame/roland/roland_mt32.cpp

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
docgz() {  # <fichier> <sha256 du fichier décompressé> <url .gz>
  [[ -s "$DOCS/$1" ]] || curl -fsSL -A 'Mozilla/5.0' "$3" | gunzip > "$DOCS/$1"
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
# National Semiconductor, DP8390D/NS32490D NIC Network Interface Controller, juillet 1995 :
# le cœur de la NE2000 de l'EtherNEC et du NetUSBee. Copie de la documentation de Bochs.
docgz DP8390D.pdf 1447683959e5f164e5800fb2995e5df22f220da0ff675c105b4dd635b91991c5 \
    https://bochs.sourceforge.io/techspec/DP8390D.pdf.gz
# Realtek, RTL8019AS Full-Duplex Ethernet Controller with Plug and Play Function
# (RealPNP), Specification LS003.8 du 2000-08-20, 52 p. : la puce du NetUSBee et des
# cartes NE2000 courantes sur l'EtherNEC (page 3, identifiants $0A/$0B, CLDA).
# Copie de cours (Univ. Complutense de Madrid), à défaut d'une archive constructeur.
doc RTL8019AS_LS003.8_2000.pdf c6147648322daec48b200d0f2dfd4491d3690eac294e8d245dbd2191a989f62b \
    https://www.fdi.ucm.es/profesor/mendias/PSyD/docs/rtl8019as.pdf
# Hitachi, HD6301/HD6303 Series Handbook, 1989 (U07) : le microcontrôleur de l'IKBD.
doc HD6301_HD6303_Handbook_1989.pdf dbee534e751ccb9a84f91d59c9eb86547d0cd3a521dbfb681cacb0f930cb69a6 \
    "$BITS/hitachi/_dataBooks/1989_U07_HD6301_HD6303_Series_Handbook_1989.pdf"
# Transcription tierce du microcode du 68000 : même origine que fx68k, donc pas un témoin
# indépendant de lui. Chaque mot y dit quels accès bus la séquence fait, et dans quel ordre.
doc m68k_microcode_all.txt f678abb9ec4f32ac19a0b67cf7a9e5d1ab8c581e38588d2cdc5d17577e88eb8b \
    https://og.kervella.org/m68k/all.txt
# TT et Falcon (TODO § 10, « Fiches et oracle ») : la FPU MC68882 est déjà épinglée plus
# haut (MC68881_MC68882_UM_1987.pdf) ; le second MFP du TT est un MC68901, déjà là aussi.
# Motorola, MC68030 Enhanced 32-Bit Microprocessor User's Manual, 3ᵉ éd., 1990 : le CPU des deux.
doc MC68030_UM_3ed_1990.pdf ec2a2e6c926e4337b9bc982882ea247fe41eece15769a880dca4f9ebf08d185d \
    "$BITS/motorola/68000/68030/MC68030_Users_Manual_3ed_1990.pdf"
# Motorola, MC68030 Technical Summary (MC68030/D), 1991 : brochage, timings AC.
doc MC68030_Data_Sheet_1991.pdf 87534e555998a5fb48cbcc03dc1db7ec04ca8392980b2252b5b16a6002b9f287 \
    "$BITS/motorola/68000/68030/MC68030_Data_Sheet_1991.pdf"
# Motorola, DSP56000/DSP56001 Digital Signal Processor User's Manual, 1990 : le DSP du Falcon.
doc DSP56000_DSP56001_UM_1990.pdf 036102ba475711350483c544d4f6cee05cfb711ee0c13683a50e859b218ae36a \
    "$BITS/motorola/56000/56000/1990_DSP56000_DSP56001_Users_Manual.pdf"
# Motorola, DSP56001 Technical Data (DSP56001/D), 1991 : brochage, port hôte, timings.
doc DSP56001_Data_Sheet_1991.pdf 34d3bb2e26a8f5d30cf2b588e60e0a457147e9c49ed8cb273bbb0b6fad5c58d3 \
    "$BITS/motorola/56000/56000/DSP56001_Data_Sheet_1991.pdf"
# NCR, 5380-53C80 SCSI Interface Chip Design Manual (SP-1051), mars 1986 : le SCSI du TT
# (Atari y monte le LS5380 de Logic Devices, compatible ; cf. références du manuel TT).
doc NCR5380_53C80_Design_Manual_Mar86.pdf 8bf11ad5a6f25358917194b63f9c00d1f7ddaf3a76b3bae28bd9d721e9d19240 \
    "$BITS/ncr_symbios/scsi/5380/SP-1051_NCR_5380-53C80_SCSI_Interface_Chip_Design_Manual_Mar86.pdf"
# Motorola, MC146818A Real-Time Clock Plus RAM, Advance Information : horloge et NVRAM du
# TT et du Falcon. Copie de cours (specs Pintos), à défaut d'une archive constructeur ;
# copie identique, même empreinte : web.eecs.umich.edu/~ryanph/jhu/cs318/fall22/project/specs/.
doc MC146818A_RTC.pdf ab2a1a92080a6b1b5a66c24a25017a4bd6889a622993b695a79e95dfd88bbc7b \
    https://www.scs.stanford.edu/22wi-cs212/pintos/specs/mc146818a.pdf
# Documents Atari (scans sans couche texte), fichiers « original » d'archive.org : leurs
# dérivés (OCR, _text.pdf) sont régénérés par le site et changeraient d'empreinte.
ARCH=https://archive.org/download
# Atari Corp., TT030 Hardware Reference Manual, juin 1990 : carte mémoire, second MFP,
# SCSI, RTC, vidéo TT.
doc Atari_TT030_HW_Reference_Manual.pdf 93c17beebfc42517f1df3318f1c55c9f0ac68aadd07949369c0a0517d68063ee \
    "$ARCH/tos306uk/Atari-TT030-Hardware-Reference-Manual.pdf"
# Atari Corp., Falcon030 Developer Documentation, 1ᵉʳ oct. 1992 : guide matériel (DSP et
# sous-système audio, matrice de connexion du Crossbar, ports), XBIOS son/vidéo/DSP.
doc Atari_Falcon030_DevDoc_1992-10-01.pdf 035d6bac88dd659b73d01e77d2bd539930c71944d56b34670ad297f3ee227bc9 \
    "$ARCH/Atari_Falcon030_Developer_Documentation_Oct_1_1992/Atari_Falcon030_Developer_Documentation_Oct_1_1992.pdf"
# Même documentation, édition du 25 janv. 1993 (120 p.) : pages révisées et outils DSP.
doc Atari_Falcon030_DevDoc_1993-01-25.pdf abf48cb4ec0010417f9ea9852d341a0c187f54025b969ce3dbef5355dfbaa623 \
    "$ARCH/Atari_Falcon030_Developer_Documentation_Jan_25_1993/Atari_Falcon030_Developer_Documentation_Jan_25_1993.pdf"

doc ASICs-new.zip 89512ee7d588b60d1ea964e048152a66c04672560c216862ff7e9f797ca06097 \
    https://www.chzsoft.de/asic-web/ASICs-new.zip
# Noms d'entrée à antislashs : unzip les normalise, pas zipfile de Python.
mkdir -p "$DOCS/asic"
(cd "$DOCS/asic" && { unzip -qo ../ASICs-new.zip '*.pdf' '*.PDF' '*.txt' 2>/dev/null || true; })
[[ -s "$DOCS/asic/Atari_ASICs/ASIC/4081_COMBO/4081/DWG/pdf/ST4081S.PDF" ]] \
  || { echo "schéma ST4081S absent après extraction" >&2; exit 1; }

echo "fx68k   ${FX68K_PIN:0:12}"
echo "MiSTery ${MISTERY_PIN:0:12} (gstmcu $(git -C "$ROOT/extern/mistery/gstmcu" rev-parse --short=12 HEAD))"
echo "MAME    ${MAME_PIN:0:12}"
