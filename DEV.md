# DEV.md — Guide développeur NeoST

(c) 2026 VERHILLE Arnaud. Référence technique : architecture, débogage, pièges matériels.
Orientation et méthode de travail → [`CLAUDE.md`](CLAUDE.md). État → [`CHANGELOG.md`](CHANGELOG.md) / [`TODO.md`](TODO.md).

## Architecture

Deux idées structurantes : **le `Bus` *est* le plan mémoire** (il ne fait que router
read8/write8 vers les composants), et **le cœur ne dépend pas du GUI**.

- **`neost_core`** (lib statique, aucune dépendance GUI) = la carte mère : `Bus`, `Cpu68k`
  (wrapper Musashi/Moira), `Shifter`, `Mfp`, `Ikbd`, `Fdc`, `YM2149`, `DmaSound`,
  `Blitter`, `Rtc`, `Glue`, plus `Machine`, `Scheduler` et `Tracer`.
- **`Machine`** assemble les composants, les branche au `Bus`, encapsule `runFrame()`.
- **`neost`** (GUI) et **`neost-headless`** partagent `Machine`. Le GUI ajoute
  GLFW/OpenGL/ImGui/miniaudio et bride à 50 fps réels.

```
src/
  main.cpp                  Frontend GUI : GLFW + OpenGL (texture Shifter) + ImGui,
                            clavier/souris → IKBD, barre résolution, persistance.
  core/
    Bus.{hpp,cpp}           Memory map + dispatch MMIO + bus errors (busFault/buildIoFault).
    Cpu68k.{hpp,cpp}        Wrapper Musashi/Moira : callbacks mémoire, int-ack vectorisé,
                            hook d'instruction (traceur), reset/IPL.
    Shifter.{hpp,cpp}       Décodage planaire basse/moyenne/haute → buffer ARGB.
    YM2149.{hpp,cpp}        PSG : registres + synthèse 3 voies + bruit + enveloppe.
    DmaSound.{hpp,cpp}      Son DMA STE + Microwire/LMC1992.
    Blitter.{hpp,cpp}       Blitter ST (HOG).
    Machine.{hpp,cpp}       Assemble tout + runFrame() événementiel.
    Scheduler.hpp           Ordonnanceur d'événements datés (cycles).
    Tracer.{hpp,cpp}        Trace d'instructions/IRQ.
  io/
    Mfp.{hpp,cpp}           MC68901 : IRQ vectorisées, timers A-D, GPIP.
    Ikbd.{hpp,cpp}          ACIA 6850 clavier + commandes/souris/joystick IKBD.
    MidiAcia.{hpp,cpp}      2e ACIA 6850 (MIDI).
    Fdc.{hpp,cpp}           WD1772 + DMA disquette + ACSI.
    Rtc.{hpp,cpp}           RP5C15 (Mega ST/Mega STE).
  audio/                    Backend miniaudio (Audio, DriveSound).
  headless/                 Runner déterministe + traces.
  web/main_web.cpp          Frontend WebAssembly (Emscripten + WebGL).
extern/  Musashi/ moira/ imgui/ miniaudio/   (sous-modules)
extern/hatari/src           SOURCE DE VÉRITÉ matérielle (lue, pas compilée)
```

## Modèle d'horloge (`Machine::runFrame`)

PAL basse résolution : **313 lignes × 512 cycles CPU**. `runFrame` est désormais
**événementiel à horloge continue** (`Scheduler`, cycles datés avec carry du dépassement) :
vidéo au cycle (rendu/Timer B/HBL aux cycles 376/400/508), timers MFP A/C/D en mode délai
datés par le MFP, Timer C ≈200 Hz, VBL niveau 4 au début du VBlank. Le GUI bride à 50 fps
réels pour que le temps émulé colle au réel. Le passage au quantum **sous la ligne** (vs par
instruction) reste le grand chantier — cf. [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md).

## Le Bus

Tout accès CPU passe par `Bus::read8/16/32` et `write8/16/32` (assemblage **big-endian** :
toujours assembler les mots octet par octet). Aiguillage : RAM (`$0`), ROM (`romBase`), MMIO
(`$FF8000+`). `mmioRead8`/`mmioWrite8` routent vers Shifter (`$FF8200`), FDC/DMA (`$FF8600`),
PSG (`$FF8800`), son DMA (`$FF8900`), MFP (`$FFFA00`), ACIA (`$FFFC00`), RTC (`$FFFC21`).

`busFault(addr)` renvoie vrai pour les adresses non décodées qui doivent faire une **bus
error**. Modèle **WHITELIST** porté de Hatari (`ioMem.c`) : tout `$FF8000-$FFFFFF` faute SAUF
les registres câblés du modèle (+ zones « void » silencieuses). Hors IO, `$400000-$F9FFFF` et
`$FF0000-$FF7FFF` fautent ; RAM/ROM/port cartouche jamais.

## Le CPU (Musashi / Moira)

Musashi communique via des fonctions C globales (`m68k_read_memory_*`) redirigées vers
`g_bus`. Activé dans CMake :
- `M68K_INSTRUCTION_HOOK=1` → hook par instruction (alimente le `Tracer`).
- `M68K_EMULATE_INT_ACK=1` → acquittement d'IRQ **vectorisé** (indispensable aux vecteurs
  MFP). `neostIntAck` renvoie le vecteur MFP (niveau 6) et désarme le VBL (niveau 4).
  `neostUpdateIpl` recalcule l'IPL (MFP 6 > VBL 4).

Moira (cycle-exact, C++20, sous-module) est sélectionnable via `--cpu moira`. **Limite
connue** : `NeostMoira::read8/16` n'honore pas encore `busFault` → aucun bus error sous Moira.

## Chaîne d'interruption (subtile)

Un composant met à jour le `Mfp` (canal ou ligne GPIP), puis le `Bus` appelle
`cpu->updateIpl()` **après** l'accès MMIO. Lignes câblées : I3 blitter, I4 ACIA (clavier+MIDI
en OU câblé), I5 FDC, I7 son DMA XSINT, bit7 moniteur.

## Ajouter / modifier un composant

1. Créer `Xxx.{hpp,cpp}` exposant `read8(addr)` / `write8(addr,v)` (+ état public pour le
   débogueur).
2. L'ajouter en membre de `Machine`, le brancher au `Bus` dans le constructeur de `Machine`,
   router sa plage d'adresses dans `Bus::mmioRead8/Write8`.
3. L'ajouter aux sources de `neost_core` dans `CMakeLists.txt`.
4. **Valider en headless** avant le GUI (`--trace`, `--screenshot`).
5. Pour lever une IRQ : mettre à jour le `Mfp` (canal / ligne GPIP), `updateIpl` est appelé
   par le `Bus` après l'accès MMIO.

## Débogage headless (l'outil n°1)

Pas de « tests » classiques : la validation se fait via `neost-headless` (déterministe, sans
GUI), qui produit des **traces façon MAME** et des **captures PPM**.

```sh
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq
tail t.txt                                   # localiser la boucle d'attente (PC qui tourne)
./build/neost-headless <rom> --frames N --screenshot s.ppm   # sips -s format png s.ppm --out s.png

# Suite étalons (captures + régression) : tools/run_etalons.py — voir docs/TEST_SOFTWARE.md
python3 tools/fetch_etalons.py && python3 tools/run_etalons.py --update-ref
python3 tools/run_etalons.py
```

Options : `--cpu musashi|moira` (TESTER LES DEUX), `--machine st|megast|ste|megaste`,
`--mem 256k|512k|1m|2m|4m`, `--cart FILE`, `--disk`, `--diskb`, `--mono`, `--until-pc HEX`,
`--walk-mouse`, `--keys "STR"`, `--loopback`. Pilotage daté (menus de jeux/démos) :
`--keys-at N "STR"` (scancodes étendus : flèches `<>[]`, Esc `=`, F1-F5 `!@#$%`),
`--joy-at N VAL`, `--joy-script N "SCRIPT"` (U/D/L/R/F/`.` = 1 trame),
`--mouse-at N "SCRIPT"` (L/R/U/D = ±8 px, `1`/`2` = clic gauche/droit, `.` = idle — c'est
ainsi qu'on pilote Vroom : clic droit au titre, clic droit en course). Debug entrées :
`NEOST_DEBUG_IKBD=1` (commandes reçues par l'IKBD), `NEOST_DEBUG_ACIA=1` (chaque lecture
du data register $FFFC02 : valeur, file restante, cycle).

Format de trace (la séquence de PC est le signal de diff) :
```
FC0030: bra     $fc004e
>>> IRQ niveau 6, vecteur $45        (Timer C du MFP)
```

### Techniques vérifiées
- **Cartouches de diagnostic** (`carts/*.bin|img`, magic `$FA52235F`) : exécutées au reset,
  elles écrivent leur rapport sur le **port série RS-232** (`$FFFA2F`), vidé en fin de run.
  C'est LE moyen de savoir quel sous-système échoue. Bon `--machine` (ST_Diagnostic→st,
  STE_Test→ste, MegaSTE→megaste) ; `--keys "O"` pilote le menu (`O`=ROM, `Z`=tests, `Q`=tout).
- **`--irq` indispensable** pour les bugs d'interruption (sinon le saut vers un vecteur est
  invisible). `grep '>>> IRQ' t.txt`.
- **`--loopback`** : branche les connecteurs de bouclage (MIDI/Serial/Printer-Joystick), APRÈS
  l'injection `--keys` — sinon l'écho du rapport série console reviendrait en réception et
  casserait la détection clavier. L'ACSI (test J/H) n'a PAS besoin de `--loopback`.
- **Sensibilité à `--mem`** : un même diag peut échouer différemment selon la taille RAM →
  révèle un bug de décodage MMU (`mmuTranslate`).
- **Garde double bus fault** (`Cpu68k.cpp`, `g_inBusError`) : un code en vrille fautait en
  boucle → segfault hôte. On halte désormais le CPU comme un vrai 68000 (Musashi
  `m68k_pulse_halt`, Moira `flags|=HALTED`). Si EXIT≠0 réapparaît, vérifier cette garde sur
  les DEUX cœurs.
- **`tools/trace_diff.py`** : aligne une trace NeoST et une trace Hatari du même ROM/disquette
  sur un PC commun et localise la première divergence (flux PC + registres) :
  ```sh
  ./build/neost-headless --frames 200 --trace neost.txt --regs --irq
  SDL_VIDEODRIVER=dummy hatari --trace cpu_disasm,cpu_regs --log-file hatari.txt --tos ... --disk-a ...
  python3 tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs
  ```

## Vérité matérielle : composant NeoST ↔ Hatari

`extern/hatari/src` = la référence (lue, pas compilée). EmuTOS
([github.com/emutos/emutos](https://github.com/emutos/emutos)) documente ce que le firmware
attend du matériel.

| NeoST                    | Hatari `src/`                                  |
|--------------------------|------------------------------------------------|
| `Bus` / MMIO bus errors  | `ioMem.c`, `ioMemTabST.c`, `ioMemTabSTE.c`     |
| `Bus` régions hors-IO    | `cpu/memory.c` (init_mem_banks, BusErrMem_bank)|
| `Bus::mmuTranslate`      | `stMemory.c` (STMemory_MMU_Translate_*)        |
| `Cpu68k`                 | `m68000.c`, `cycInt.c`, `cycles.c`             |
| 8/16 MHz + cache MegaSTE | `m68000.c` (`MegaSTE_CPU_Cache_Update`, `MegaSTE_Cache_*`, `mem_access_delay_*_megaste_16`) |
| `Fpu` (68881 optionnel)  | (Hatari n'émule pas le socket : bus error `$FFFA40` ; cf. MC68881 UM §7) |
| `Mfp`                    | `mfp.c` (timers A-D, modes, GPIP)              |
| `Ikbd` / `MidiAcia`      | `ikbd.c`, `acia.c`, `midi.c`, `keymap.c`       |
| `Shifter` / `Machine`    | `video.c` (HBL/VBL/Timer B, bordures, spec512), `screen.c` |
| `Fdc`                    | `fdc.c`, `floppy.c`, `hdc.c`                   |
| `YM2149` / `DmaSound`    | `psg.c`, `sound.c`, `dmaSnd.c`                 |
| `Blitter` / `Rtc`        | `blitter.c`, `rtc.c`                           |

## Pièges matériels (vérifiés en debug)

- **Big-endian** : assembler les mots octet par octet (`read16` etc.).
- **Bus error = WHITELIST, pas blacklist** : règle word/long → l'accès ne faute que si TOUS
  ses octets fautent (`busFaultN`). C'est pourquoi `move.w $FF8204` marche mais
  `move.b $FF8204` faute. Les octets PAIRS du MFP (`$FFFAxx`) fautent (registres aux adresses
  **impaires** uniquement : `$FFFA01`, `$FFFA03`…).
- **Protection superviseur (GLUE)** : en mode utilisateur (bit S=0), `$0-$7FF` et TOUT
  l'espace IO `$FF8000-$FFFFFF` fautent AVANT la whitelist (`busFaultN(addr, n, write)`).
  Les écritures ROM TOS / cartouche / `$0-$7` fautent même en superviseur. Le CPU seul est
  concerné — blitter et DMA passent par `read8/write8` sans test (BusMode Hatari).
- **MegaSTE 16 MHz** : l'ordonnanceur reste en cycles BUS 8 MHz ; seul `Cpu68k` convertit
  (×2 sous `$FF8E21` bit1). Une boucle en RAM SANS cache ne va PAS plus vite à 16 MHz
  (accès cadencés bus) ; ROM et cache 16 Ko si. Sous Musashi : ×2 uniforme (non-CE).
- **VBL/HBL autovecteurs LATCHÉS** (comme Hatari, « cleared only when processed ») :
  `g_vblPending` reste armé tant que le CPU n'a pas servi l'IRQ (mask ≥ niveau). Si le SR
  ré-autorise le niveau 4 après une longue période masquée, la VBL en attente part AUSSITÔT
  vers `$70` → crash si le handler n'y est plus.
- **Vecteurs MFP** : canal = n° de source (Timer A=13, B=8, C=5, D=4, ACIA=6, FDC=7) ;
  vecteur présenté = `(VR & 0xF0) | canal`. En software-EOI (VR bit3), le handler DOIT
  effacer l'ISR sinon le canal reste bloqué.
- **Bits d'entrée GPIP** (moniteur bit7, ACIA bit4, FDC bit5) forcés en lecture — ne PAS les
  laisser écraser par une écriture CPU sur `$FFFA01`.
- **bit7 GPIP = 1 → moniteur couleur** (basse rés) ; 0 → mono (haute rés). Haute rés =
  **monochrome** (blanc/noir), ignorer la palette couleur.
- **Différences de modèle** (`IoMem_FixVoidAccess*`) : le ST (Ricoh) faute là où le Mega ST
  (IMP) est « void » (`$FF8002-$FF800D`) — un des signaux qu'EmuTOS lit pour distinguer les
  machines. Le STE expose le son DMA (`$FF8900`) et le joypad (`$FF9200`) ; le ST non.
