# DEV.md — Guide développeur NeoST

(c) 2026 VERHILLE Arnaud.

## Arborescence

```
src/
  main.cpp              Frontend GUI : fenêtre GLFW, rendu OpenGL de la texture
                        Shifter, fenêtres ImGui (écran ST, hexa, registres),
                        clavier/souris → IKBD, barre résolution, persistance.
  core/
    Bus.{hpp,cpp}       Memory map + dispatch MMIO + bus errors.
    Cpu68k.{hpp,cpp}    Wrapper Musashi : callbacks mémoire, int-ack vectorisé,
                        hook d'instruction (traceur), reset/IPL.
    Shifter.{hpp,cpp}   Décodage planaire basse/moyenne/haute → buffer ARGB.
    YM2149.{hpp,cpp}    PSG : registres + synthèse 3 voies + bruit.
    Glue.hpp            Reste du MMIO (config mémoire) — squelette.
    Machine.{hpp,cpp}   Assemble tout + boucle d'horloge d'une trame.
    Tracer.{hpp,cpp}    Trace d'instructions/IRQ via l'API Musashi.
  io/
    Mfp.{hpp,cpp}       MC68901 : IRQ vectorisées, Timer B/C, GPIP, lignes
                        ACIA(4)/FDC(5)/moniteur(7).
    Ikbd.{hpp,cpp}      ACIA 6850 + scancodes/paquets souris IKBD.
    Fdc.{hpp,cpp}       WD1772 + DMA disquette.
  audio/Audio.{hpp,cpp} Backend miniaudio (compile l'implémentation).
  headless/main_headless.cpp   Runner déterministe + traces.
extern/  Musashi/ imgui/ miniaudio/   (sous-modules)
rom/  disks/   ROMs TOS et images disquette
```

## Modèle d'horloge (`Machine::runFrame`)

PAL basse résolution : 313 lignes × 512 cycles CPU. Par ligne : `cpu.run(512)`,
puis `mfp.hblank()` sur les **lignes visibles** (Timer B compte le Display
Enable). 4 tics Timer C répartis sur la trame (≈200 Hz). VBL niveau 4 au début
du VBlank. Le GUI bride à 50 fps réels pour que le temps émulé colle au réel.

## Le Bus

Tout accès CPU passe par `Bus::read8/16/32` et `write8/16/32` (assemblage
big-endian). Aiguillage : RAM (`$0`), ROM (`romBase`), MMIO (`$FF8000+`).
`mmioRead8`/`mmioWrite8` routent vers Shifter ($FF8200), FDC/DMA ($FF8600),
PSG ($FF8800), MFP ($FFFA00), ACIA ($FFFC00). `busFault(addr)` renvoie vrai
pour les adresses non décodées qui doivent faire une **bus error** (ex. blitter).

## Le CPU (Musashi)

Musashi communique via des fonctions C globales (`m68k_read_memory_*`) redirigées
vers `g_bus`. Activé dans CMake :
- `M68K_INSTRUCTION_HOOK=1` → hook par instruction (alimente le `Tracer`).
- `M68K_EMULATE_INT_ACK=1` → acquittement d'IRQ **vectorisé** (indispensable
  pour les vecteurs MFP). `neostIntAck` renvoie le vecteur du MFP (niveau 6) et
  désarme le VBL (niveau 4). `neostUpdateIpl` recalcule l'IPL (MFP 6 > VBL 4).

## Ajouter / modifier un composant matériel

1. Créer `Xxx.{hpp,cpp}` exposant `read8(addr)` / `write8(addr,v)` (+ état public
   pour le débogueur).
2. L'ajouter en membre de `Machine`, le brancher au `Bus` dans le constructeur
   de `Machine`, router sa plage d'adresses dans `Bus::mmioRead8/Write8`.
3. L'ajouter aux sources de `neost_core` dans `CMakeLists.txt`.
4. **Valider en headless** avant le GUI (`--trace`, `--screenshot`).

Interruptions : pour lever une IRQ, le composant met à jour le `Mfp` (canal /
ligne GPIP), puis le `Bus` appelle `cpu->updateIpl()` après l'accès MMIO.

## Vérité matérielle

En cas de doute, **MAME / Hatari** font foi (les deux sont installables), et la
**source EmuTOS** documente précisément ce que le firmware attend du matériel.
Workflow type : tracer headless → localiser la boucle d'attente → lire le code
EmuTOS correspondant → implémenter le registre/signal manquant. Exemples vécus :
`timeout_gpip` (GPIP5 FDC), `_int_acia` (GPIP4 + clear ISR), `shifter_get_monitor_type`
(GPIP7), la synchro Timer B de TOS 1.x.

## Pièges connus

- Le 68000 est **big-endian** ; toujours assembler les mots octet par octet.
- Les bits d'**entrée** du GPIP (moniteur, ACIA, FDC) ne doivent **pas** être
  écrasés par les écritures CPU sur `$FFFA01` — ils sont forcés en lecture.
- Les registres MFP sont aux adresses **impaires** (`$FFFA01`, `$FFFA03`, …).
- En mode software-EOI (VR bit3), le handler doit effacer l'ISR ; sinon le canal
  reste bloqué.
- La haute résolution est **monochrome** : ignorer la palette couleur.
