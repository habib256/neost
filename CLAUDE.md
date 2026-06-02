# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

NeoST — émulateur Atari ST « boîte à hack » pédagogique. C++17, GLFW3 + OpenGL
(immediate mode) + Dear ImGui pour le GUI, miniaudio pour le son, Musashi (68000)
en sous-module. Cibles : macOS Silicon / CachyOS Linux. Commentaires en français.

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j                 # cibles : neost (GUI), neost-headless, neost_core (lib)
./build/neost                          # auto : dernier ROM (cf. neost.cfg) ou EmuTOS US + disks/diskA.st
./build/neost <rom> <disk.st>          # ROM + disquette explicites
```

Sous-modules requis (cf. README) : `extern/Musashi`, `extern/imgui`,
`extern/miniaudio`, `extern/moira` (cœur 68000 alternatif). Pour Musashi, générer
les opcodes après le clone :
`( cd extern/Musashi && cc -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c )`.

**`extern/hatari/src` = la source de vérité matérielle** (sources C complètes,
PAS un sous-module de build). On ne la compile pas : on la LIT pour porter le
comportement exact d'une puce. Fichiers clés (← composant NeoST correspondant) :
- `ioMem.c` + `ioMemTabST.c` / `ioMemTabSTE.c` → **carte des bus errors** MMIO
  (← `Bus::busFault`/`buildIoFault`). Modèle : tout `$FF8000-$FFFFFF` faute par
  défaut, on whiteliste les registres câblés.
- `cpu/memory.c` (`init_mem_banks`, `BusErrMem_bank`) → banques RAM/ROM/bus-error
  hors-IO (← `Bus::busFault` régions `$400000-$F9FFFF`, `$FF0000-$FF7FFF`).
- `stMemory.c` (`STMemory_MMU_Translate_*`) → décodage banques MMU (← `Bus::mmuTranslate`).
- `mfp.c` → MFP 68901 (← `Mfp`). `video.c` → HBL/VBL/Timer B, compteur faisceau (← `Machine`/`Shifter`).
- `fdc.c`, `psg.c`, `dmaSnd.c`, `acia.c`, `ikbd.c`, `blitter.c` → puces homonymes.

Ne PAS faire `rm -rf build` (ça casse le shell de l'utilisateur s'il y est `cd`) ;
un simple `cmake -B build` reconfigure. Sous macOS il n'y a pas de `timeout`.

## Débogage — le mode headless est l'outil n°1

Il n'y a pas de « tests » classiques : la validation se fait via `neost-headless`
(exécution déterministe, sans GUI) qui produit des **traces façon MAME** et des
**captures PPM**. C'est ainsi qu'on diagnostique un TOS/jeu qui bloque :

```sh
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq
tail t.txt                                  # localiser la boucle d'attente (PC qui tourne)
awk '/^FCxxxx:/{...}' t.txt                 # désassembler la zone bloquée
./build/neost-headless <rom> --frames N --screenshot s.ppm   # puis: sips -s format png s.ppm --out s.png
```

Options : `--cpu musashi|moira` (TESTER LES DEUX), `--machine st|megast|ste|megaste`,
`--mem 256k|512k|1m|2m|4m`, `--cart FILE` (cartouche `$FA0000`), `--disk`, `--mono`
(moniteur mono → haute rés), `--until-pc HEX`, `--walk-mouse` (injection souris).

**Méthode imposée (ordre strict)** : quand un test plante, NE PAS désassembler ni
chercher le point de divergence d'emblée. D'ABORD comparer la source de vérité
(`extern/hatari/src`) au code NeoST et porter ce qui manque, puis RETESTER. Ce
n'est QUE si l'on a la conviction d'avoir tout porté correctement et que l'erreur
persiste qu'on investigue en détail (trace → boucle → source EmuTOS
github.com/emutos/emutos). **MAME et Hatari sont les sources de vérité matérielle.**
Bugs trouvés ainsi (cf. CHANGELOG.md) : int-ack vectorisé, GPIP4/5/7, Timer B/C,
modèle de bus error (whitelist Hatari), double bus fault → halt.

### Techniques headless efficaces (vérifiées)
- **Cartouches de diagnostic** (`carts/*.bin|img`, magic `$FA52235F`) : exécutées
  au reset, elles écrivent leur rapport sur le **port série RS-232** (UDR `$FFFA2F`),
  vidé en fin de run par le headless. C'est LE moyen de savoir quel sous-système
  échoue, chip par chip. Lancer avec le bon `--machine` (ST_Diagnostic→st,
  STE_Test→ste, MegaSTE→megaste). `--keys "O"` tape au clavier après le boot pour
  piloter le menu du diagnostic (ex. `O`=test ROM, `Z`=tests internes, `Q`=tout).
- **`--irq` est indispensable** pour les bugs d'interruption : sans lui, le saut
  vers un vecteur (ex. VBL niveau 4 → `$70`) est invisible. `grep '>>> IRQ' t.txt`.
- **Sensibilité à la taille RAM** : un même diag peut échouer différemment selon
  `--mem` (ex. « RAM disturbance » à 256k mais pas à 512k) → révèle un bug de
  décodage MMU (`mmuTranslate`) plutôt qu'autre chose.
- **Garde double bus fault** (`Cpu68k.cpp`, `g_inBusError`) : un code parti en
  vrille fautait en boucle → l'hôte segfaultait. On halte désormais le CPU comme
  un vrai 68000, ce qui laisse le headless vider trace+série. Si EXIT≠0 réapparaît,
  vérifier cette garde sur les DEUX cœurs (Musashi + Moira `flags|=HALTED`).

## Architecture (le grand schéma)

Tout est découplé autour de deux idées : **le `Bus` EST le plan mémoire** (il ne
fait que router read8/write8 vers les composants), et **le cœur ne dépend pas du
GUI**.

- **`neost_core`** (lib statique, aucune dépendance GUI) = la carte mère :
  `Bus`, `Cpu68k` (wrapper Musashi), `Shifter` (vidéo, pur décodeur ARGB),
  `Mfp`, `Ikbd`, `Fdc`, `YM2149`, `Glue`, plus `Machine` et `Tracer`.
- **`Machine`** assemble tous les composants, les branche au `Bus`, et encapsule
  `runFrame()` : 313 lignes PAL × 512 cycles, Timer C (200 Hz), Timer B + HBL sur
  les lignes visibles, VBL niveau 4. C'est LE point d'entrée du timing.
- **`neost`** (GUI) et **`neost-headless`** partagent `Machine`. Le GUI ajoute
  GLFW/OpenGL/ImGui/miniaudio et bride à 50 fps réels.

Chaîne d'interruption (subtile) : un composant met à jour le `Mfp` (canal ou
ligne GPIP), puis le `Bus` appelle `cpu->updateIpl()` après l'accès MMIO. Musashi
est compilé avec `M68K_EMULATE_INT_ACK=1` (vecteurs MFP) et
`M68K_INSTRUCTION_HOOK=1` (traceur) — voir `Cpu68k.cpp` (`neostIntAck`,
`neostUpdateIpl`). DEV.md détaille l'extension d'un composant.

## Pièges matériels (vérifiés en debug)

- Le 68000 est **big-endian** : assembler les mots octet par octet (`read16` etc.).
- **Modèle de bus error = WHITELIST, pas blacklist** (`Bus::buildIoFault`, porté de
  Hatari `ioMem.c`) : tout `$FF8000-$FFFFFF` faute SAUF les registres câblés du
  modèle (+ zones « void » silencieuses). Hors IO, `$400000-$F9FFFF` et
  `$FF0000-$FF7FFF` fautent ; RAM/ROM/port cartouche jamais. Règle word/long :
  l'accès ne faute que si TOUS ses octets fautent (`busFaultN`) — c'est pourquoi
  `move.w $FF8204` marche mais `move.b $FF8204` faute. Les octets PAIRS du MFP
  (`$FFFAxx`) fautent (registres aux adresses impaires uniquement).
- **VBL/HBL autovecteurs LATCHÉS** (comme Hatari : « level 2/4 cleared only when
  processed ») : `g_vblPending` reste armé tant que le CPU n'a pas servi l'IRQ
  (mask ≥ niveau). Conséquence : si le SR ré-autorise le niveau 4 après une longue
  période masquée, la VBL en attente part AUSSITÔT vers `$70` — d'où crash si le
  handler n'y est plus (cf. TODO : diagnostic ST « T0 MFP timer »).
- **Vecteurs MFP** : canal = n° de source (Timer A=13, B=8, C=5, D=4, ACIA=6,
  FDC=7) ; vecteur présenté = `(VR & 0xF0) | canal`. En software-EOI (VR bit3) le
  handler DOIT effacer l'ISR sinon le canal reste bloqué.
- Les bits d'**entrée** du GPIP (moniteur bit7, ACIA bit4, FDC bit5) sont forcés en
  lecture — ne PAS les laisser écraser par une écriture CPU sur `$FFFA01`.
- **Haute résolution = monochrome** (blanc/noir), ignorer la palette couleur.
- **bit7 GPIP = 1 → moniteur couleur (basse rés)** ; 0 → mono (haute rés).
- **Différences de modèle** (Hatari `IoMem_FixVoidAccess*`) : le ST (chipset Ricoh)
  faute là où le Mega ST (IMP) est « void » ($FF8002-$FF800D, etc.) — c'est un des
  signaux qu'EmuTOS lit pour distinguer les machines. Le STE expose le son DMA
  ($FF8900) et le joypad ($FF9200) ; le ST non.

## Disquettes & zone de téléchargement

Le lecteur A monte une image `.st`. Outils (`tools/`) :
- `tools/make_floppy.py` → régénère `disks/diskA.st` (FAT12 720 Ko de test).
- `tools/fetch_disk.py <url>` → télécharge une disquette de test depuis la **zone
  planetemu** (`https://www.planetemu.net/machine/atari-st`) via **scrapling**
  (`pip install scrapling`), dans `disks/`. ⚠ À n'utiliser que pour des logiciels
  auxquels on a droit (domaine public, freeware, démos) — pour tester l'émulateur.

Les ROMs TOS étant propriétaires, le défaut est **EmuTOS** (libre) dans `rom/`.

## Docs

`README.md` (install/usage), `DEV.md` (architecture détaillée + extension),
`CHANGELOG.md` (fonctionnalités & bugs corrigés), `TODO.md` (feuille de route,
inclut l'état Arkanoid et les briques d'accuracy manquantes).
