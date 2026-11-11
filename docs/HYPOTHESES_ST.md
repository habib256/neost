# Hypothèses ST dans le code — ce que le TT et le Falcon remettent en cause

(c) 2026 VERHILLE Arnaud. **Où le code suppose un Atari ST, STE ou Mega STE** : bus de
24 bits, un 68000, une seule carte MMIO, la liste des machines ST. Relevé le 2026-11-10 à
`ee571b2`, **sans rien changer au code**. Il sert à chiffrer le TT (68030, bus 32 bits,
TT-RAM, second MFP, SCSI, vidéo TT) et le Falcon (68030, VIDEL, DSP56001, Crossbar, IDE) ;
ce qui reste à faire vit dans [`../TODO.md`](../TODO.md) § 10. Les numéros de ligne
bougent à chaque commit : relire à la révision courante avant de s'y fier.

`src/core/Cpu68k.cpp` est hors périmètre, car l'échange du cœur le réécrit
([`CPU_SWAP_GATE.md`](CPU_SWAP_GATE.md)) : 3 masques 24 bits, 3 tests `MachineType::`,
environ 23 mentions du Mega STE (`cpuMul`, cache, 16 MHz). La composition de l'IPL (MFP, SCU,
HBL/VBL) y vit aussi, et devra en sortir à l'échange.

**Ce qui commande le reste.** Le décodage passe par un seul masque, `stmap::ADDR_MASK`, et
une seule frontière RAM, `0x400000`. La carte MMIO est une table fixe. `MachineType` compte
quatre valeurs, testées directement plutôt que par capacités. L'unité de l'ordonnanceur est
le cycle bus à 8 MHz.

Effort : **trivial** (une ligne), **local** (une fonction ou un fichier), **structurel**
(une interface ou un modèle à changer).

## 1. Masques d'adresse 24 bits

| Fichier:ligne | Hypothèse | Effort |
|---|---|---|
| src/core/Bus.hpp:84 | `ADDR_MASK = 0x00FFFFFF`, appliqué en tête de chaque accès : Bus.hpp:121, 134, 151, 258 ; Bus.cpp:390, 408, 503, 616, 712, 758, 773, 785, 814, 826, 841, 888, 905 (17 sites) | structurel : un masque par machine (TT 32 bits ; le Falcon décode 24 bits). Il faut aussi le miroir $FFxxxxxx → $00xxxxxx, que le masque donne aujourd'hui gratuitement |
| src/core/Bus.cpp:564 | `clear()` de `buildIoFault` borne l'IO à `<= 0xFFFFFF` | local |
| src/core/Bus.cpp:674 | auto-test : balaie `MMIO_BASE..0xFFFFFE` | trivial |
| src/core/MachineType.hpp:42 | `mmioNeedsBetterMachine` masque à 24 bits (« le 68000 n'a que 24 bits ») | trivial |
| src/core/Blitter.cpp:71 | `regWriteMask` : octet fort de $FF8A24/$FF8A32 forcé à 0 (adresses source et destination sur 24 bits) | local (le Falcon a un blitter 24 bits, le TT aucun) |
| src/core/Blitter.cpp:113, 127, 477, 480 | `readWord`/`writeWord`, `srcAddr`/`dstAddr` : `& 0xFFFFFE` | local |
| src/core/DmaSound.cpp:108 | pointeur FIFO `(curAddr_+2) & 0xFFFFFF` | local |
| src/io/GemdosHd.cpp:461 | `checkArea` : ROM et cartouche testées sur 24 bits | local |
| src/io/GemdosHd.cpp:1967 | `inRom(pc)` masque à 24 bits | trivial |

Traces et débogage seulement (vraies adresses, effet cosmétique, trivial) : Shifter.cpp:704,
893, 990, 1400 · VideoCounter.cpp:410 · Blitter.cpp:309-310 · AppLoop.cpp:1826 ·
main_headless.cpp:2536 · headless/Observe.hpp:41 · Symbols.cpp:74, 140, 156 ; plus 45 formats
`%06X` dans 12 fichiers (DebugWindows.cpp 7, main_headless.cpp 8, Shifter.cpp 7…).

Faux positifs, des constantes qui ressemblent à un masque d'adresse : couleurs ARGB
(Shifter.cpp:1172, main_headless.cpp:2681), FPSR et mantisses (Fpu.cpp:105, 251, 280, 785 ;
SoftFloatX80.hpp:210, 363 ; SoftFloatFpsp.hpp:135), LBA ACSI de 21 bits (Acsi.cpp:199),
masques d'IRQ USB (Isp1160.cpp:75, 279), masque RAS/CAS dans une banque (Bus.cpp:269).

## 2. Plan mémoire figé

| Fichier:ligne | Hypothèse | Effort |
|---|---|---|
| src/core/Bus.hpp:46-84 | `stmap` : ROM $FC0000/$E00000, cartouche $FA0000-$FBFFFF, MMIO $FF8000, bases des puces | local (les bases restent ; les nouvelles plages manquent) |
| src/core/Bus.hpp:259 | chemin rapide `busFaultN` : RAM = `< 0x400000` | local |
| src/core/Bus.cpp:368 | `mmuFastLimit_` plafonné à 4 Mo | local |
| src/core/Bus.cpp:392 | `hostRamPtr` refuse au-delà de 4 Mo : un tampon GEMDOS en TT-RAM échouerait (`checkArea`) | local |
| src/core/Bus.cpp:416, 507, 619, 842 | `read8Slow`/`write8Slow`/`busFault`/`peek8` : RAM ST = `[0, $400000)`, le reste du bas est un trou | structurel (ST-RAM jusqu'à 14 Mo sur Falcon ; TT-RAM à $01000000) |
| src/core/Bus.hpp:401 | `std::vector<uint8_t> ram` : un seul bloc | structurel (second bloc TT-RAM) |
| src/core/Bus.cpp:219-275, 278-327 | MMU ST/STE : deux banques de 128 Ko/512 Ko/2 Mo, `mmuXlatSTF`/`mmuXlatSTE` | structurel (autres contrôleurs mémoire ; à court-circuiter) |
| src/core/Bus.cpp:63-67 | save-state : liste fermée des tailles de RAM (≤ 4096 Ko) | trivial |
| src/core/MachineType.hpp:75-120 | `parseRamBytes`/`ramLabel`/`memConfigForBytes` : 256 Ko à 4 Mo | local |
| src/core/Bus.cpp:142, 154 | TOS ≤ 1 Mo ; `romBase` déduite de la taille (≤ 192 Ko → $FC0000) | trivial (TOS 3.x/4.x : 512 Ko à $E00000) |
| src/core/Bus.hpp:202-204 | `romWindowSize` : 768 Ko sur STE, 1 Mo sinon | local |
| src/core/Bus.cpp:537-556 | listes blanches `ST_OK`/`STE_OK` des bus errors | structurel (tables TT et Falcon à écrire) |
| src/core/Bus.cpp:560 | `ioFault_.assign(0x8000, 1)` : IO = 32 Ko à $FF8000 | local |
| src/core/Bus.cpp:568-610 | `buildIoFault` : branches ST, MegaSt, MegaSte | structurel |
| src/core/Bus.cpp:1138-1153 | table `kSlots` MMIO, une ligne par puce | local (des lignes à ajouter : 2ᵉ MFP $FFFA80, SCSI, VIDEL, DSP, Crossbar, IDE) |
| src/core/Bus.cpp:681-683 | auto-test : $FF7FFE faute, cartouche protégée | trivial |
| src/core/Machine.cpp:1363 | fichier d'état plafonné à 64 Mo (« RAM ≤ 4 Mo ») | trivial (court avec de la TT-RAM) |
| src/headless/main_headless.cpp:1696 | `hashRamLen` ≤ 0x400000 | trivial |
| src/gui/DebugWindows.cpp:40 | la vue hexa n'affiche que `bus.ram` | local |

## 3. Types de machine

`enum class MachineType { St, MegaSt, Ste, MegaSte }` (MachineType.hpp:19). **124 tests**
`MachineType::X`, `machineIs*` ou `machineHas*` hors cœur, dans 16 fichiers : Machine.cpp 25,
Bus.cpp 21, MachineType.hpp 20, Bus.hpp 14, Machine.hpp 8, VideoGlue.cpp 7, Shifter.cpp 7,
Fdc.cpp 6, main_web.cpp 4, main_headless.cpp 3, ConfigWindow.cpp 2, AppInit.cpp 2,
main_android.cpp 2, App.cpp 1, VideoGlue.hpp 1, Blitter.cpp 1. Plus des tests sur des
chaînes : ConfigWindow.cpp:86, 143-153 · AppLoop.cpp:842 · App.cpp:102.

| Fichier:ligne | Hypothèse | Effort |
|---|---|---|
| src/core/MachineType.hpp:19, 21, 58 | énumération, nom, analyse de la configuration | trivial |
| src/core/MachineType.hpp:41-55 | `mmioNeedsBetterMachine` : plages ST/STE/Mega STE | local |
| src/core/MachineType.hpp:126-145 | `machineHasDmaSound`/`HasBlitter`/`IsSte`/`IsMega` ; `machineIsSte` mêle « timings du GLUE STE » et « matériel STE » (TT sans blitter, Falcon avec ; son par le Crossbar sur Falcon) | structurel : passer à des capacités |
| src/core/Machine.cpp:32-87 | `adjustMachineForTos` ne connaît que TOS ≤ 2.06 | local |
| src/core/Machine.cpp:104-131, Machine.hpp:148-167 | câblage des puces en double (constructeur et `reconfigure`) : SCC, pads, YM | local, deux endroits à tenir synchrones |
| src/core/Machine.cpp:213, 321, 447-476, 656, 696-729 | HBL/VBL et redémarrage du compteur vidéo STF ou STE (64/68, 56/60) | structurel (remplacé par la vidéo TT et le VIDEL) |
| src/core/Bus.hpp:203, 215, 225, 483-495 | fenêtre ROM, `/ROM5-6`, gamecart, portes `claims*` | local |
| src/core/Bus.cpp:307, 325, 359 | banques MMU ST ou STE | structurel (§ 2) |
| src/core/Bus.cpp:956, 960 | $FF860E (densité HD) propre au Mega STE | local (TT et Falcon lisent aussi la HD) |
| src/core/Bus.cpp:966-967, 1058, 1081, 1185-1199 | particularités de la netlist STE (`steNetlist`, pads, $FF8001, $FF9000) | trivial (rester faux hors STE) |
| src/core/VideoGlue.cpp:189, 222, 272, 588, 1324, 1420, 1595 ; VideoGlue.hpp:163 | timings GLUE STF/STE | structurel : remplacé, pas branché |
| src/core/Shifter.cpp:1366, 1415, 1431, 1437-1459 | registres vidéo visibles sur STE seulement | structurel (§ 5) |
| src/io/Fdc.cpp:1023, 1055, 1059, 1353, 3347, 3409 | densité, SCLK du FDC ST/STE, retenue d'adresse DMA ST, 16 MHz du Mega STE | local |
| src/core/Blitter.cpp:358 | arbitrage à 8 sur Mega STE, 4 sinon | local |
| src/gui/AppInit.cpp:496, App.cpp:480, main_headless.cpp:1902 | FPU présent sur Mega STE seulement | trivial |
| src/gui/ConfigWindow.cpp:86, 143-153, 353 ; AppLoop.cpp:842 ; App.cpp:102 ; web/main_web.cpp:645-649 ; android/main_android.cpp:547 | listes, préréglages et choix de ROM en dur | trivial |

## 4. Le 68000, hors du cœur

| Fichier:ligne | Hypothèse | Effort |
|---|---|---|
| src/core/Pacing.hpp:27, 36 | `kCpuHz = 8021248`, horloge de référence unique | structurel |
| src/core/Scheduler.hpp:72-73, Fdc.cpp:3404-3409 | l'ordonnanceur compte des cycles bus à 8 MHz ; le 16 MHz passe par des demi-périodes | structurel : base de temps plus fine (32 MHz TT, 16 MHz Falcon) |
| src/io/Mfp.cpp:671, 781, 853, 899, 952 | rapport MFP/CPU exact 31333/9600, propre au CPU à 8 MHz | local (à paramétrer, second MFP compris) |
| src/io/Fdc.cpp:1054-1061 | conversion FDC/bus 8021247/8010613 | local |
| src/io/Scc.cpp:143, 160, 606 ; Rtc.hpp:76 ; UltraSatan.hpp:91 ; DmaSound.cpp:36 ; Isp1160.cpp:334 | horloges des puces en `kCpuHzInt` | local |
| src/core/Bus.cpp:936, 940, 985, 995, 1005-1026 | états d'attente du 68000 (PSG 4, MFP 4, ACIA 6 + synchro E-clock), `addPsg/Mfp/AciaWaitCycles` | structurel (contrat de Cpu68k.hpp:116-145, à redéfinir pour le bus du 030) |
| src/core/Shifter.cpp:735-745 | `syncCpuBus` : frontière de bus de 4 cycles 68000 | local à structurel |
| src/core/Cpu68k.hpp:87-88 | `setMegaSteSpeed`/`megaSte16Mhz`, utilisés par Blitter.cpp:330, 334, 402 et Fdc.cpp:1374 | structurel (une API d'horloge générique) |
| src/core/Bus.cpp:1047, 1081, 1085 ; Fdc.cpp:3106, 3193 | `triggerBusError(addr, write)` levé par un périphérique | local (signature juste ; sur 030 le cœur empile une trame $A/$B) |
| src/io/GemdosHd.cpp:2015-2018 | trame d'exception de 6 octets (SR + PC) : `params = sp+2+4` ; le 030 empile en plus un mot format/vecteur (Hatari lit `_longframe`) | local |
| src/io/GemdosHd.cpp:2121 | vecteur GEMDOS écrit en dur à $84 : VBR supposé nul | trivial (TOS le laisse nul) |
| src/io/Fpu.hpp:61-62 ; Bus.hpp:493 ; Bus.cpp:84-86 | 68881 en périphérique, registres CIR à $FFFA40, Mega STE seulement | local, ci-dessous |
| src/core/Bus.hpp:388-417 ; Bus.cpp:758-790 | cache 16 Ko du Mega STE, étiquette sur les bits 14-23 | trivial (reste propre au Mega STE) |
| src/core/Machine.cpp:890, 957, 1037 ; Cpu68k.hpp:238 | save-state : `machineType_` sur un `uint8`, section CPU = registres du 68000 | structurel (passe par la porte d'échange) |

**FPU.** Sur TT (68882) et sur Falcon (68882 en option), le cœur exécute lui-même les
instructions de ligne F par l'interface coprocesseur ; les registres CIR de $FFFA40 restent
propres au Mega STE (et à la SFP004). Le moteur de calcul (`SoftFloatX80`, FPSP) peut servir
de FPU au cœur, ou céder la place à celui de POM68K : décision au TODO § 9.

## 5. Vidéo, par classe

| Fichier:ligne | Hypothèse | Effort |
|---|---|---|
| src/core/Shifter.hpp:40 | `Mode { Low, Medium, High }` : résolutions 0-2 | structurel |
| src/core/Shifter.hpp:298 | palette de 16 `uint16` en $0RGB 12 bits (TT : 256 entrées ; VIDEL : 256 × 18 bits et true color) | structurel |
| src/core/Shifter.hpp:638-645 | géométrie : 512/508/224 cycles par ligne, 313/263/501 lignes, tampon 416×276 | structurel |
| src/core/Shifter.hpp:388 ; VideoCounter.cpp:455 ; Shifter.cpp:1466 | compteur vidéo sur 22 bits (`0x3FFFFF`, octet fort & $3F) | local |
| src/core/Shifter.cpp, ShifterInternal.hpp, VideoCounter.cpp | rendu en plans 1/2/4, spec512, scroll fin et `lineWidth` STE | structurel : garder pour les modes compatibles ST, une nouvelle puce à côté |
| src/core/VideoGlue.cpp/.hpp | GLUE : bordures, overscan, wakestates, timings STF/STE | structurel (sans équivalent TT/Falcon) |
| src/core/Framing.hpp:48 ; AppLoop.cpp:478, 550, 564 ; web/main_web.cpp:438 ; headless/Observe.hpp:53 | les frontends lisent `Shifter&`/`shifter.pixels()` directement | local : une interface de puce vidéo, à poser après A32 (fait le 2026-11-11 : `VideoGlue`, `VideoCounter`, `GlueView`) |

## 6. Divers

| Fichier:ligne | Hypothèse | Effort |
|---|---|---|
| src/io/Fdc.cpp:57-69 | masque du DMA disque : octet fort selon la RAM ($3F, $7F, $FF) | déjà généralisé (port d'Hatari) |
| src/io/Fdc.cpp:3323, 3376 | DMA ACSI borné à `bus_.ram.size()` | trivial (le DMA du TT ne vise que la ST-RAM) |
| src/core/DmaSound.cpp:581-586 | adresses du son DMA : octet fort & $3F (22 bits) | local (selon la machine, comme le FDC) |
| src/io/Mfp.hpp:43-58 | câblage GPIP du MFP ST ; une seule instance `Mfp mfp` (Machine.hpp:213) | structurel : 2ᵉ MFP, son câblage et son routage d'IPL |
| src/io/Acsi.hpp:1-14 | jeu de commandes SCSI réutilisable ; le DMA appartient au `Fdc` | local (NCR 5380 et IDE à ajouter) |
| src/io/GemdosHd.cpp:1601 | `memtop` connaît déjà la TT-RAM ($436 sous $1000000, $5A4 au-delà) | déjà prêt |

## Chiffrage

| Catégorie | Entrées | Structurel |
|---|---|---|
| 1. Masques 24 bits | 10 sites (dont `ADDR_MASK`, 17 appels), 3 masques vidéo de 22 bits, 13 lignes de trace, 45 formats `%06X`, 11 faux positifs | `ADDR_MASK` et le miroir $FF000000 |
| 2. Plan mémoire | 19 | frontière de 4 Mo, bloc RAM unique, banques MMU ST/STE, tables de bus errors |
| 3. Types de machine | 124 tests dans 16 fichiers, plus ~6 tests de chaînes ; une quarantaine demandent une branche TT/Falcon, les autres rendent « absent » | prédicats → capacités ; timings vidéo de `Machine` et `VideoGlue` |
| 4. Le 68000 hors du cœur | 14 | horloge unique à 8 MHz, contrat des états d'attente, API de vitesse du Mega STE, save-state CPU |
| 5. Vidéo | 7 | Shifter, VideoGlue, VideoCounter ; une interface de puce vidéo pour les frontends |
| 6. Divers | 7 | 2ᵉ MFP et routage de l'IPL (aujourd'hui dans `Cpu68k.cpp`) |

**Le Falcon reste en 24 bits décodés** : 14 Mo de ST-RAM, pas de TT-RAM. Il touche surtout
à la frontière de 4 Mo, à la vidéo, au son, au DSP et à l'IDE. Le TT impose en plus
`ADDR_MASK` sur 32 bits, un second bloc de RAM et un second MFP.
