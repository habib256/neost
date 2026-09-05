# DEV.md — Guide développeur NeoST

(c) 2026 VERHILLE Arnaud. Référence technique : architecture, débogage, pièges matériels.
Orientation et méthode de travail → [`CLAUDE.md`](CLAUDE.md). État → [`CHANGELOG.md`](CHANGELOG.md) / [`TODO.md`](TODO.md).

## Architecture

Deux idées structurantes : **le `Bus` *est* le plan mémoire** (il ne fait que router
read8/write8 vers les composants), et **le cœur ne dépend pas du GUI**.

- **`neost_core`** (lib statique, aucune dépendance GUI) = la carte mère : `Bus`, `Cpu68k`
  (wrapper Moira), `Shifter`, `Mfp`, `Ikbd`, `MidiAcia`, `Fdc`/`StxImage`, `Acsi`, `Scc`,
  `YM2149`, `DmaSound`/`AudioMix`, `Blitter`, `Rtc`, `Fpu`, `GemdosHd`, `Glue`, plus
  `Machine`, `Scheduler`, `Tracer`/`Symbols`, `Framing`, `MediaScan`, `HostPath` et
  `AppConfig` (le format `neost.cfg` est du parsing pur, donc côté cœur).
- **`neost_net`** (lib statique, HORS cœur) = la couche réseau hôte : sockets et
  threads y vivent (`SlirpBackend` toujours ; `Socket`, `HayesModem`, `MidiRing`
  sous `NEOST_WITH_NET`).
- **`Machine`** assemble les composants, les branche au `Bus`, encapsule `runFrame()`.
- **`neost`** (GUI), **`neost-headless`**, **`neost-web`** et l'APK Android partagent
  `Machine`. Le GUI ajoute GLFW/OpenGL/ImGui/miniaudio et bride à 50 fps réels.

```
src/
  main.cpp                  Point d'entrée, 28 lignes : appInit → appLoop → appShutdown.
  core/
    Bus.{hpp,cpp}           Memory map + dispatch MMIO + bus errors (busFault/buildIoFault).
    Cpu68k.{hpp,cpp}        Wrapper Moira (cycle-exact) : accès mémoire, int-ack vectorisé,
                            hook d'instruction (traceur), reset/IPL.
    Shifter.{hpp,cpp}       Registres vidéo, palette, décodage planaire → buffer ARGB.
    VideoGlue.hpp           LE GLUE vidéo : masques de bordure, table de timings par
                            machine, wakeup state (port de Hatari video.c).
    VideoGlue.cpp           Sa MACHINE À ÉTATS : DE/HBL, retraits de bordure, Timer B.
    VideoCounter.cpp        Compteur vidéo ($FF8205/07/09), avance du faisceau, restart.
    ShifterInternal.hpp     Outillage commun aux trois unités ci-dessus (non public).
    MmuGlue.hpp             Le MMU + la « colle système » ($FF8001). ⚠ CE N'EST PAS le
                            GLUE vidéo — il s'appelait Glue.hpp et induisait en erreur.
    YM2149.{hpp,cpp}        PSG : registres + synthèse 3 voies + bruit + enveloppe.
    DmaSound.{hpp,cpp}      Son DMA STE + Microwire/LMC1992.
    Blitter.{hpp,cpp}       Blitter ST : données + partage de bus hog ET non-hog (64/64).
    AudioMix.{hpp,cpp}      Chaîne de mixage d'UNE trame (YM horodaté + DMA STE + LMC1992),
                            partagée par les frontends — elle en était la copie
                            divergente qui rendait les samples inaudibles en WASM.
    Framing.{hpp,cpp}       Région de CONTENU de la trame (zoom adaptatif) : une seule
                            règle pour le kiosk, la fenêtre bureau et le plein écran WASM.
    Machine.{hpp,cpp}       Assemble tout + runFrame() événementiel.
    Scheduler.hpp           Ordonnanceur d'événements datés (cycles).
    Tracer.{hpp,cpp}        Trace d'instructions/IRQ.  Symbols.{hpp,cpp} : .sym / TOS $601A.
    StateArchive.hpp        Sérialisation des save-states.  MachineType.hpp : profils ST→MegaSTE.
  io/
    Mfp.{hpp,cpp}           MC68901 : IRQ vectorisées, timers A-D, GPIP, USART.
    Ikbd.{hpp,cpp}          ACIA 6850 clavier + commandes/souris/joystick IKBD.
    MidiAcia.{hpp,cpp}      2e ACIA 6850 (MIDI).
    Fdc.{hpp,cpp}           WD1772 + DMA disquette + routage ACSI.
    StxImage.{hpp,cpp}      Images Pasti .stx (jeux protégés).
    Acsi.{hpp,cpp}          Disque dur ACSI (hdc.c) + rattachement de l'UltraSatan.
    GemdosHd.{hpp,cpp}      Disque dur GEMDOS (dossier hôte → C:), cf. § dédié.
    Scc.{hpp,cpp}           Z85C30 (Mega STE) ; Scu.hpp : gating d'IRQ Mega STE.
    Fpu.{hpp,cpp}           MC68881 optionnel (SoftFloatX80.hpp).
    Rtc.{hpp,cpp}           RP5C15 (Mega ST/Mega STE).
    MediaScan.{hpp,cpp}     Inventaire des supports (tri, images « sœurs » d'un même jeu).
    Ne2000.{hpp,cpp}        Extension NeoST : NE2000 (EtherNEC, cf. docs/EXTENSIONS.md).
    UltraSatan/Isp1160      Extensions NeoST : interface SD UltraSatan (ACSI, paquets 'US'), hôte USB
                            ISP1160 du NetUSBee (port cartouche, avec Ne2000). Cf. docs/EXTENSIONS.md.
  net/                      Couche réseau hôte (neost_net) : Socket (TCP/UDP partagé),
                            HayesModem, MidiRing, NetBackend/SlirpBackend.
  gui/                      LE frontend fenêtré (A9, 2026-08-30 : main.cpp est passé de
                            5 100 à 28 lignes, ses 84 globaux `g_*` ont un propriétaire).
    App.{hpp,cpp}           `struct App` : TOUT l'état du frontend (ex-`g_*`) + la session
                            (Machine, Audio, MIDI, réseau, écran) + les services que les
                            menus déclenchent (applyConfig, midiOutApply, switchKioskMode…).
                            `app()` = l'instance unique, pour les seuls callbacks GLFW.
    AppInit.cpp             Avant la 1re trame : ligne de commande (parseCommandLine),
                            chemins, fenêtre, Machine, montages, hôtes audio/MIDI/réseau,
                            ImGui. Renvoie < 0 pour « boucler », sinon un code de sortie.
    AppLoop.cpp             La boucle (entrées → trames dues → dessin → requêtes → swap)
                            et l'arrêt. ⚠ Encore d'un seul tenant — déplacée, pas découpée.
    ConfigWindow.{hpp,cpp}  Fenêtre « Configuration » (14 pages) + fenêtre Disquettes.
                            `ConfigUi` porte les requêtes ; la fenêtre ne monte rien.
    KioskMenu.{hpp,cpp}     Menu plein écran de la borne + scrutations (jeux, dossiers).
    DebugWindows.{hpp,cpp}  Hexa, CPU, joystick, débogueur.
    StScreenView.{hpp,cpp}  `GlScreen` (texture ARGB du Shifter) + passe CRT + les deux
                            cadrages (viewport borne / fenêtre bureau), même zoom adaptatif.
    InputCallbacks.{hpp,cpp} Callbacks GLFW clavier/souris (signature imposée → `app()`).
    DockLayout / CrtUi / JoyMap / GlHeaders / MediaPages / UiCommon / KeyboardWindow
    AppConfig.{hpp,cpp}     neost.cfg + profils profiles/*.cfg (parseConfigLine /
                            writeConfigKeys / writeConfigAtomic) — logique pure, testée.
    CrtEffectStack + OpenGLShader : la passe CRT elle-même.
  util/HostPath.{hpp,cpp}   UNE définition des chemins hôte (sémantiques POSIX ET Windows).
  audio/                    Backend miniaudio (Audio, DriveSound) + ponts MIDI hôtes :
                            MidiOutHost / MidiInHost, un fichier pour TROIS backends
                            (CoreMIDI, ALSA, winmm — cf. MidiWinmm.hpp pour Windows) ;
                            Mt32Synth (Munt), GmSynth (TinySoundFont).
  headless/                 Runner déterministe + traces.
  web/main_web.cpp          Frontend WebAssembly (Emscripten + WebGL).
  android/                  Frontend Android (SDL2 + GLES2) — démarre, pas d'interface.
tests/                      selftest_logic.cpp (cible neost-selftest, palier fast) +
                            stx_writetrack_test.cpp (EXCLUDE_FROM_ALL).
extern/  imgui/ miniaudio/ (sous-modules) · moira/ (VENDORISÉ, cf. NEOST_VENDOR.md) · hatari/ (clone gitignoré)
extern/hatari/src           SOURCE DE VÉRITÉ matérielle (lue, pas compilée)
```

## Mode kiosk — invariants d'implémentation (`gui/AppInit.cpp`, `gui/AppLoop.cpp`, `gui/KioskMenu.cpp`)

Usage, menu manette et réglages → [`docs/KIOSK.md`](docs/KIOSK.md). Ici, seulement ce
qu'il ne faut pas casser en touchant au code :

- **Parsing** : les drapeaux `--*` sont filtrés ; les arguments POSITIONNELS restants
  donnent ROM (0) et disquette (1). Ne pas remettre d'accès `argv[1/2]` en dur.
- **Config figée** : `saveConfig()` sort immédiatement si `App::kiosk` **ou**
  `App::kioskLaunched` (invariant de DÉPLOIEMENT : une session lancée en `--kiosk` reste
  figée même après un retour au bureau par F8), **sauf** `force=true` — réservé aux deux
  réglages que la borne doit mémoriser depuis son menu, `kiosk_romdir=` et `joymap=`.
  Un `force=true` repart de `App::cfgPristine` et n'y reporte QUE ces champs : la structure
  en mémoire est salie en séance, la réécrire entière ferait fuir les réglages du dernier
  visiteur. Un `--kiosk` de test n'écrase donc jamais rom/disk/machine. **Les profils
  nommés obéissent au même gel** : `profiles/*.cfg` reste lisible et chargeable, mais
  enregistrer/écraser/supprimer est grisé ET refusé côté boucle (double garde — c'est le
  seul autre chemin du frontend qui écrit sur le disque).
- **Chrome masqué** : tout le bloc ImGui est sous `if (!A.kiosk)` ; la trame ImGui reste
  créée/rendue à vide. Le dockspace est gardé VIVANT (`KeepAliveOnly`) sinon l'aller-retour
  bureau→borne→bureau désancre toutes les fenêtres.
- **Bascule à chaud (F8)** : instantané `saveState` → changement de moniteur GLFW →
  `loadState`. À faire ENTRE deux trames uniquement.
- **Rendu** : `drawStKiosk()` cale la zone active (`Shifter::activeWidth/Height/Top`) sur
  la HAUTEUR, ratio gardé. La passe CRT est demandée à la taille du cadre entier À CE
  ZOOM, pas à celle de l'écran — sinon masque et scanlines sont magnifiés par le viewport
  et moirent.
- **Entrées** : souris capturée + curseur masqué d'emblée ; `App::kbdJoy` forcé ON ; DEL ne
  libère PAS la souris. Sortie : **Alt+F4** (géré explicitement — l'exclusif ne relaie pas
  toujours le « close » du WM) ou chord **Ctrl+Shift+Q** ~0,7 s. On ne bloque jamais
  `glfwWindowShouldClose`.
- **Latence audio** : `--audio-latency MS` (persisté) fixe le coussin d'amorçage
  (`Audio::setLatencyMs`, défaut 85, borné `[20,250]` — au-delà on s'approche de la
  capacité de `SampleRing{32768}` = 341 ms à 48 kHz stéréo, et le producteur jetterait des
  échantillons). À appeler AVANT `start()` : c'est `start()` qui convertit en échantillons,
  la fréquence réelle n'étant connue qu'après `ma_device_init`.

## Modèle d'horloge (`Machine::runFrame`)

PAL basse résolution : **313 lignes × 512 cycles CPU**. `runFrame` est désormais
**événementiel à horloge continue** (`Scheduler`, cycles datés avec carry du dépassement) :
vidéo au cycle (rendu au cycle 376, HBL en fin de ligne, **Timer B à une position DÉRIVÉE**
du Display-Enable et de l'AER bit3 — `Machine::timerBPos` → `Shifter::timerBLinePos`, et non
un cycle fixe), timers MFP A/C/D en mode délai
datés par le MFP, Timer C ≈200 Hz, VBL niveau 4 au début du VBlank. Le GUI bride à 50 fps
réels pour que le temps émulé colle au réel. Le quantum **sous la ligne** est ACQUIS
(`Scheduler::liveNow` + préemption `Cpu68k::endTimeslice`) ; le chantier ouvert est le
**beam-sync par-ligne** — cf. [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md).

## Le Bus

Tout accès CPU passe par `Bus::read8/16/32` et `write8/16/32` (assemblage **big-endian** :
toujours assembler les mots octet par octet). Aiguillage : RAM (`$0`), ROM (`romBase`), MMIO
(`$FF8000+`). `mmioRead8`/`mmioWrite8` routent vers Shifter (`$FF8200`), FDC/DMA (`$FF8600`),
PSG (`$FF8800`), son DMA (`$FF8900`), MFP (`$FFFA00`), ACIA (`$FFFC00`), RTC (`$FFFC21`).

`busFault(addr)` renvoie vrai pour les adresses non décodées qui doivent faire une **bus
error**. Modèle **WHITELIST** porté de Hatari (`ioMem.c`) : tout `$FF8000-$FFFFFF` faute SAUF
les registres câblés du modèle (+ zones « void » silencieuses). Hors IO, `$400000-$F9FFFF` et
`$FF0000-$FF7FFF` fautent ; RAM/ROM/port cartouche jamais.

## Le CPU (Moira, cycle-exact)

NeoST n'a qu'**un seul cœur 68000 : Moira** (vAmiga, MIT, C++20, **vendorisé** dans
`extern/moira` — NeoST le patche, cf. `extern/moira/NEOST_VENDOR.md`).
L'ancien cœur Musashi — rapide mais **non cycle-exact** — a été retiré : il n'apportait plus
rien face à Moira et doublait inutilement chaque chemin du wrapper. Moira est **requis** pour
bâtir (CMake faute si `extern/moira` est absent), et compilé en mode cycle-exact
(`MOIRA_PRECISE_TIMING=true`, `MOIRA_MIMIC_MUSASHI=false`, cf. `CMakeLists.txt`).

`Cpu68k` (`NeostMoira`, sous-classe de `moira::Moira`) route les accès mémoire vers `g_bus` :
- `read8/16` et `write8/16` consultent `busFaultN` (whitelist Hatari) → lèvent `moira::BusError`
  (trame de groupe 0 reconstruite dans `raiseBusError`) ou haltent le CPU en double faute.
- `readIrqUserVector` (irqMode USER) reproduit le vectoring ST : vecteur MFP (niveau 6) via
  `mfp->iack()`, VBL/HBL (4/2) auto-vectorisés. `neostUpdateIpl` recalcule l'IPL (MFP 6 > VBL 4
  > HBL 2 ; gaté par le SCU sur MegaSTE).
- Le `Tracer` reçoit `onInstruction(pc)` après chaque `execute()` et désassemble via
  `Cpu68k::disassemble` → `moira::disassemble` (syntaxe `Syntax::MUSASHI`, format de trace
  inchangé pour le diff MAME).

L'option `--cpu` (headless) et la clé `cpu=` (`neost.cfg`) ne valent plus que `moira` ; une
ancienne valeur `musashi`/`uae` est tolérée (rétro-compat) mais **avertit** puis bascule sur
Moira (`Cpu68k::parseCore`).

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

## Builds spécialisés

**WebAssembly** — nécessite l'[emsdk](https://emscripten.org/) activé. La cible
`neost-web` écrit dans `wasm/` :

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j --target neost-web   # → wasm/index.{html,js,wasm,data}
python3 -m http.server -d wasm 8000
```

`-DNEOST_WEB_FREE_ONLY=ON` restreint le FS virtuel aux contenus libres (c'est ce que fait
la CI). La page hôte est `web/shell.html` ; `wasm/index.html` est l'artefact GÉNÉRÉ, pas
la source.

⚠ **`wasm/` n'est PLUS dans le dépôt** (2026-08-23) : il est gitignoré. La démo en ligne
est **construite et déployée par la CI** — le job `wasm` de `release.yml` dépose le bundle
en artefact Pages, le job `pages` le publie (`build_type=workflow`). Une seule
construction Emscripten par push, et la démo suit les sources toute seule.

Deux étapes intermédiaires ont été essayées et abandonnées, pour mémoire : la garde de
fraîcheur `tools/wasm_stamp.sh` rendait la CI **rouge** quand le bundle était périmé sans
le réparer (deux fois en trois jours) ; la **recommission** du bundle par la CI le
réparait, mais posait un commit de bot sur `main` à chaque push.

✅ Ce que la bascule règle au passage : en `build_type=legacy`, Pages publiait le dépôt
**en entier** sur habib256.github.io/neost/ — ROM Atari et jeux compris. Le déploiement
par artefact ne publie QUE le bundle.

Pour une démo hors ligne : `emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
-DNEOST_WEB_FREE_ONLY=ON && cmake --build build-web --target neost-web` régénère `wasm/`.

**Windows** — MinGW-w64 dans un shell MSYS2/MINGW64,
`NEOST_VERSION=<ver> packaging/windows/build_mingw.sh`. Tout est lié en statique et le
script REFUSE le paquet s'il importe une DLL non système : une DLL manquante est
invisible en CI (MSYS2 les a dans son `PATH`) et fatale au double-clic.

**Paquets de release** — 8 artefacts (dont l'APK Android arm64, cf.
`packaging/android/README.md`), cf. `.github/workflows/release.yml`. Le contenu
embarqué est une liste EXPLICITE (`packaging/stage_free_data.sh`) avec un garde-fou qui
refuse toute ROM hors liste.

## Débogage headless (l'outil n°1)

Pas de framework de test : la validation se fait via `neost-headless` (déterministe, sans
GUI), qui produit des **traces façon MAME** et des **captures PPM**. Ce qu'il ne peut PAS
couvrir — les fonctions sans machine ni ROM (chemins hôte, format `neost.cfg`) — l'est par
`./build/neost-selftest` (`tests/selftest_logic.cpp`), premier pas du palier `fast`.

Depuis A29 (2026-08-28), ce binaire porte aussi des **tables de vérité « puce nue +
Scheduler »** : on instancie la puce seule (plus un `Bus` de 512 Ko et un `Scheduler`
quand il en faut un), on écrit ses registres, on lit ce qui en sort. Couvertes : YM2149,
MFP+ACIA, RTC, **Blitter**, **son DMA STE**, **FDC/DMA disquette**. C'est l'étage manquant
entre la logique pure et le pixel — sans lui, chaque régression du blitter était une
enquête (« 3 400 px divergents à (112,57) ») là où une table dit « la tranche non-hog
s'arrête au 32ᵉ mot au lieu du 33ᵉ ». Chaque table a été **vérifiée par mutation** : couper
`kNonHogBusBlitter` de 64 à 63, retirer le masque 22 bits du pointeur son, inverser la
polarité des entrées du WD1772 — les trois font rougir la ligne exacte.

À côté, `./build/neost-fuzz-disk` (A30, 2026-08-28) martèle les **parseurs d'images
disquette** — `decodeMsa`, `decodeDim`, `StxImage::parse`, les seules fonctions du projet
qui digèrent un fichier venu de l'extérieur. Driver **déterministe** (PRNG xorshift semé),
pas libFuzzer : le clang d'Apple ne livre pas `libclang_rt.fuzzer`, `-fsanitize=fuzzer` ne
lie pas sur macOS. `--iters N` / `--seed N` ; un cas trouvé se rejoue à l'identique.

⚠ **Le palier `fast` n'est qu'un test de fumée pour ce harnais.** Mesuré en retirant deux
bornes de `decodeMsa` : en build normal, 20 000 itérations n'ont RIEN vu. C'est sous
sanitizers qu'il mord — la même mutation (`if (p + len > raw.size()) return false;`
retirée) donne un `heap-buffer-overflow` ASan en moins de 20 000 itérations, en 2 s. Le
job `sanitizers` de la CI le couvre, puisqu'il lance `--tier fast`. Pour une campagne
longue : `ASAN_OPTIONS=detect_leaks=0 ./build-asan/neost-fuzz-disk --iters 500000`.

⚠ Écrire une de ces tables demande de câbler ce que `Machine` câble : le callback
`Scheduler::BLITTER`/`FDC` notamment. Sans lui l'échéance est POSÉE mais jamais servie —
le blit non-hog ne démarre pas et le test mesure du vide (erreur commise en écrivant la
table du blitter).

```sh
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq
tail t.txt                                   # localiser la boucle d'attente (PC qui tourne)
./build/neost-headless <rom> --frames N --screenshot s.ppm   # sips -s format png s.ppm --out s.png
./build/neost-headless <rom> --frames N --sound-dump s.wav   # WAV 48 kHz (YM+DMA+LMC, chaîne GUI)
#   → A/B audio vs oracle Hatari (WAV) ou entre configs ; RMS/profil par seconde en python
#   DMA STE : NEOST_DMASND_TRACE=1 émet chaque fetch FIFO au format « DMA snd fifo refill »
#   d'Hatari (--trace dmasound) → diff direct des séquences adr/contenu ; étalon dédié
#   tools/make_dmasnd_test.py (tampon modifié pendant la lecture, cas Mental Hangover)
#   DIGIDRUM : tools/make_digidrum_test.py — carré ~997 Hz écrit dans le registre de VOLUME
#   du YM à 7 979 Hz (Timer A), tonalités coupées. C'est le seul de nos étalons qui
#   DISCRIMINE une synthèse horodatée d'une lecture « en direct » des registres : un flux
#   continu (make_dmasnd_test) sort au même niveau dans les deux cas. Chercher la raie à
#   ~997 Hz dans le WAV — son absence = modèle push non armé (cf. setCycleClock).

#   BASE DE TEMPS : NEOST_QDELTA_DIAG=<seuil> imprime, à chaque entrée de Cpu68k::run,
#   l'écart busOfClock(horloge CPU) − sched.now() quand il atteint <seuil> (récap tous les
#   100000 runs). C'est la sonde de non-régression de BL3 (cycles volés par le blitter non
#   facturés à l'ordonnanceur, cf. docs/HATARI_DIVERGENCES.md). ⚠ Ce delta vaut 40 en régime
#   NORMAL — décalage de RESET, constant, absorbé au 1er IACK, SANS rapport avec le blitter.
#   Ce qu'on traque est un ESCALIER (136, 272, … 1088 avant le correctif). Inerte si la
#   variable n'est pas posée. Blitter : NEOST_BLIT_TRACE=1 journalise chaque blit démarré.

# Suite étalons (captures + régression) : tools/run_etalons.py — voir docs/TEST_SOFTWARE.md
python3 tools/fetch_etalons.py && python3 tools/run_etalons.py --update-ref
python3 tools/run_etalons.py
```

Options : `--cpu moira` (seul cœur, optionnel), `--machine st|megast|ste|megaste`,
`--mem 256k|512k|1m|2m|4m`, `--cart FILE`, `--disk`, `--diskb`, `--mono`, `--until-pc HEX`,
`--walk-mouse`, `--keys "STR"`, `--loopback`. Pilotage daté (menus de jeux/démos) :
`--keys-at N "STR"` (scancodes étendus : flèches `<>[]`, Esc `=`, F1-F5 `!@#$%`, `.` et
`|` = point et Enter du **pavé numérique** — même caractère sur tout TOS ; `--azerty` pour
un TOS FR : A/Q, Z/W, M permutés, sinon « M » tombe en virgule dans un sélecteur GEM),
`--joy-at N VAL`, `--joy-script N "SCRIPT"` / `--joy-script-file N FICHIER` (1 token =
1 trame : `U/D/L/R/F/.`, combinaison `[UF]`, masque brut `[$88]`, répétition `TOKEN*N` —
grammaire et raisons dans `src/util/JoyScript.hpp`, testée par `neost-selftest`). ⚠ Deux
ruptures assumées avec l'ancien parseur : un caractère inconnu **refuse le run avant le
boot** (il valait « neutre »), et les minuscules `u/d/l/r/f` valent la direction (elles
valaient « neutre ») — une recette archivée avec un caractère de remplissage doit être
réécrite avec `.`,
`--mouse-at N "SCRIPT"` (L/R/U/D = ±8 px, `1`/`2` = clic gauche/droit, `.` = idle — c'est
ainsi qu'on pilote Vroom : clic droit au titre, clic droit en course). Debug entrées :
`NEOST_DEBUG_IKBD=1` (commandes reçues par l'IKBD), `NEOST_DEBUG_ACIA=1` (chaque lecture
du data register $FFFC02 : valeur, file restante, cycle). MIDI : `--midi-dump FILE`
(chaque octet MIDI OUT daté du cycle 68000 → `tools/midi_compare.py`), `--midi-list`
(appareils MIDI de l'hôte) et `--midi-in-device NAME` (un clavier maître entre dans le
MIDI IN du ST ; bilan « N bytes into the ACIA » en fin de run), `--dongle
cubase3|cubase2|auto` (clé Steinberg sur /ROM3, cf. `docs/EXTENSIONS.md`).

**Pilotage externe déterministe** (`docs/OPENDST.md`) : `--probe NOM=ADR:LEN` (répétable),
`--probe-every N`, `--hash-ram ADR:LEN` émettent sur **stdout** une ligne
`probe frame=… screen=<hash> ram=<hash> NOM=0x…` par échantillon — lecture `peek8`, **sans
effet de bord** (l'espace I/O se lit `$FF` ; pour une puce, c'est `--dump-at`). `--server`
remplace la boucle `--frames` par une **boucle de commandes** stdin/stdout (`run`, `play`,
`joy`, `key`, `mouse`, `peek`, `observe`, `save`/`load` sur des emplacements d'état EN
MÉMOIRE, `export`/`import`, `shot`) : `src/headless/Server.cpp`. L'équivalence serveur ↔
boucle `--frames` est un verdict du palier `fast` (`tools/run_server_equiv.py`), et il est
MUTATION-TESTÉ. Oracle DIFFÉRENTIEL NeoST↔Hatari : `tools/opendst_oracle.py`, qui exige
`tools/hatari_neost_oracle.patch` appliqué à `extern/hatari`.

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
- **`--loopback`** : branche les connecteurs de bouclage (MIDI/Serial/SCC/Printer-Joystick),
  APRÈS l'injection `--keys` — sinon l'écho du rapport série console reviendrait en réception
  et casserait la détection clavier. Avec les injections DATÉES (`--keys-at`/`--scancode-at`),
  le branchement se fait après la DERNIÈRE injection ; si le test démarre sitôt le Return
  avalé (test S lancé seul), **`--loopback-at N`** fixe la trame exactement.
  L'ACSI (test J/H) n'a PAS besoin de `--loopback`.
- ⚠ **« No loopback connector » dans `--serial-dump` ne prouve RIEN** : la routine série des
  cartouches Field Service émet le MESSAGE D'ERREUR COMME DONNÉES DE SONDE (chaque caractère
  doit revenir par le bouclage) — la chaîne apparaît donc même quand le test PASSE. Verdict
  fiable : l'écran (« Pass ») ou l'absence de « Fail at cycle » dans le dump.
- **Test J (Hard Disk W/R)** : image ACSI ≥ ~22 Mo (l'exerciseur lit la LBA 40732 en
  supposant le disque interne 48 Mo d'époque — sur 16 Mo, « Command error » est un CHECK
  CONDITION légitime). **Test D (DMA Port)** : exige le boîtier DMA du kit Field Service —
  émulé par **`--dma-fixture`** (cible ACSI 0 à un octet de commande `((count-1)<<6)|$10/$08`,
  exclusif d'un disque réel sur la cible 0) ; sans lui, échec D0/D1/D3 FIDÈLE (Hatari n'a
  pas le boîtier et échoue pareil). **Test F formate la disquette A** :
  toujours passer des copies sacrificielles `--disk`/`--diskb` (piège A14).
- **Sensibilité à `--mem`** : un même diag peut échouer différemment selon la taille RAM →
  révèle un bug de décodage MMU (`mmuTranslate`).
- **Garde double bus fault** (`Cpu68k.cpp`, `g_inBusError`) : un code en vrille fautait en
  boucle → segfault hôte. On halte désormais le CPU comme un vrai 68000 (Moira
  `flags|=HALTED`, cf. `faultOrHalt`). Si EXIT≠0 réapparaît, vérifier cette garde.
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
| `Fpu` (68881 optionnel)  | (Hatari n'émule pas le socket — réf. MC68881 UM §7 + AN-947, glue SFP004 MiNTLib ; émulation fonctionnelle, test : `tools/make_fpu_testrom.py`) |
| `Mfp`                    | `mfp.c` (timers A-D, modes, GPIP)              |
| `Ikbd` / `MidiAcia`      | `ikbd.c`, `acia.c`, `midi.c`, `keymap.c`       |
| `Shifter` / `Machine`    | `video.c` (HBL/VBL/Timer B, bordures), `spec512.c`, `conv_st.c` |
| `Scheduler`              | `cycInt.c`, `cycles.c` |
| `Cpu68k` (adaptateur)    | `cpu/hatari-glue.c` (`customreset`), `cpu/newcpu.c`, `cpu/memory.c` |
| `StxImage`               | `floppies/stx.c` ; `.msa`/`.dim` → `floppies/msa.c`, `floppies/dim.c` |
| `Fdc`                    | `fdc.c`, `floppy.c`                            |
| `Acsi` (disque dur ACSI) | `hdc.c` (routage DMA via `Fdc`)                |
| `Scc` (série Z85C30 Mega STE) | `scc.c` (IRQ niv5 via `Scu`)              |
| `YM2149` / `DmaSound`    | `psg.c`, `sound.c`, `dmaSnd.c`                 |
| `Blitter` / `Rtc`        | `blitter.c`, `rtc.c`                           |
| `GemdosHd` (disque dur GEMDOS) | `gemdos.c`, `cpu/hatari-glue.c` (`OpCode_GemDos/Pexec/SysInit`), `cart.c`/`cart_asm.s`/`cartData.c` |

### Disque dur GEMDOS (`GemdosHd`)

Redirection des appels GEMDOS d'un lecteur virtuel (C:…) vers un dossier hôte, au
lieu d'émuler un contrôleur ACSI/IDE. Activé par `--gemdos DIR` (headless) ou
`NEOST_GEMDOS_DIR` (GUI) ; **exclusif d'une cartouche externe** (`--cart`).

Mécanisme (port fidèle, adapté à Moira) :

1. **Cartouche système à `$FA0000`** : `setDirectory` recopie les octets assemblés de
   `cart_asm.s` (= `cartData.c`) dans `bus.cart`. Le TOS y détecte le magic
   `$ABCDEF42` et exécute son C-INIT (`sys_init`) au boot (drapeau bit 3 = après
   init GEMDOS, avant boot disque).
2. **Opcodes « illégaux » magiques** : le code cartouche déclenche les opcodes
   `$0008` (GEMDOS), `$0009` (PEXEC), `$000A` (SYSINIT). Hatari patche sa table
   d'opcodes ; NeoST/Moira les capte dans `Cpu68k::run` **avant `execute()`** : si le
   PC est dans la cartouche (`$FA0000-$FBFFFF`) et `bus.gemdos` actif, on appelle
   `GemdosHd::handleOpcode`, puis on remplace l'IRD par un `NOP` (`$4E71`) que le
   `execute()` suivant consomme (avance PC + prefetch + 4 cyc) — équivalent exact du
   `CpuDoNOP()` d'Hatari.
3. **`SYSINIT`** installe le hook : sauve l'ancien vecteur GEMDOS dans la cartouche
   (`CART_OLDGEMDOS=$FA0024`, écrit DIRECTEMENT dans `bus.cart`), pose `$84` →
   `CART_GEMDOS=$FA002A`, calcule `act_pd` (osheader+`$28`) et ajoute C: au masque
   `_drvbits` (`$4C2`).
4. **`GEMDOS`** (`GemdosHd::trap`) lit le n° de fonction sur la pile (USP si appelant
   user, SSP+6 sinon), dispatche, pose D0 et les codes condition **N/Z/V** du SR que
   le code cartouche teste : Z=1 → `rte` (traité), Z=0 → ancien vecteur (TOS), V=1 →
   Pexec.
5. **`PEXEC`** : `gemPexec` fait créer la basepage par le TOS (Pexec 5/7 via la
   cartouche) puis `pexecBpCreated` charge+relocalise le PRG depuis C:
   (`loadAndReloc`) et relance un Pexec « just-go » (6/4) pour l'exécuter.

Helpers Bus : `hostRamPtr(addr,len)` (pointeur RAM contigu, traduction MMU — port de
`STMemory_STAddrToPointer`+`CheckAreaType`) et `tosVersion` (en-tête ROM offset 2).
Debug : `NEOST_GEMDOS_TRACE=1` journalise hook, traductions de chemin et appels
fichier. Simplifications vs Hatari : `bUseTos` toujours vrai, pas d'images
ACSI/IDE (lecteurs dès C:), pas d'autostart INF ni de conversion de charset.

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
  (accès cadencés bus) ; ROM et cache 16 Ko si (cf. `readMste16Mhz`/`chipWait16`).
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
