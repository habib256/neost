# Divergences NeoST ↔ Hatari — cartographie pour corrections futures

**But.** Inventaire des écarts entre NeoST et la **référence comparative Hatari**,
sous-système par sous-système, pour prioriser les corrections futures. Le
comportement du vrai Atari prime : un écart peut venir d’un défaut d’Hatari. Chaque écart est ancré `fichier:ligne`
des deux côtés, classé par sévérité (**haute** = casse logiciels/boot · **moyenne** = fidélité
visible · **basse** = cas-limite/cosmétique) avec son impact connu.

⚠ **Les `fichier:ligne` NeoST sont des ANCRES, pas des adresses.** Ils dérivent à chaque
édition ; c'est le **symbole cité** qui fait foi (`grep`, pas `sed -n`). Passe de
ré-ancrage : **2026-08-19**, sur l'ensemble du document (Blitter, Bus, Cpu68k, Fdc, Mfp, Scc,
Shifter, DmaSound, YM2149, Fpu, SoftFloatX80, Acsi, GemdosHd, MidiAcia, Machine).

**Méthode.** Comparaison ligne à ligne du code NeoST (`src/`) au source C de Hatari
(`extern/hatari/src`, gitignoré — l'inventaire ci-dessous a été établi sur le commit
`c9906f1` mais l'arbre PRÉSENT est `981f291` : ⚠ les numéros de ligne Hatari cités peuvent
avoir glissé. Lu comme
référence, cf. CLAUDE.md). Produit par un workflow de 7 sous-agents, un par sous-système.
Cible NeoST : ST / Mega ST / STE / Mega STE, 68000 (Moira cycle-exact). Hors périmètre
volontaire (NON comptés comme divergences) : moteur FDC `_MFM`/DPLL (SCP/KFS ; l'IPF est lue
depuis 2026-10-22 par un décodeur libre → modèle STX, cf. § FDC « IPF »), TT/Falcon,
VME, FPU « not found », USART relayée vers un tty hôte, son en mode headless.

**Bilan global.** La fidélité est **élevée à très élevée** sur tous les sous-systèmes. Aucune
divergence ne casse un boot EmuTOS (ST/STE/MegaSTE) ni un boot disquette `.ST` normal. Les
écarts restants sont surtout des cas-limites matériels, des tricks de démos « hardcore », et
quelques branches spécifiques STE/STF non câblées.

**Statut des corrections (passe du 2026-06-15).** Les écarts **bornés et vérifiables** ont été
corrigés (✅ ci-dessous) : BL1 (Blitter 65536), BL2 (rejet accès octet aux registres mot),
S1 (filtre LPF STF câblé), BU1 (miroir PSG), MIDI (master reset sans purge + RDR persistant).
Validés : `glue-selftest` 19/0, boots ST/STE/MegaSTE **pixel-identiques** à avant. Les chantiers
**cycle-exacts** (V1-V3 vidéo, S2 FIFO son, D3 stall FIFO) restent **différés** : leur
validation exige l'oracle Hatari headless, désormais **BÂTI** (`extern/hatari/build/src/hatari`,
v2.6.1) → ces chantiers sont validables, cf. `docs/HATARI_AUTOMATION.md`. Les
points « choix de comportement » **tranchés** (SC1 loopback SCC honoré = datasheet, D1/D2 WRITE
TRACK STX, NeoST plus correct en HD/ED — tous « NE PAS corriger ») et les marginaux (`$FF8264`,
F1, M1) sont laissés documentés pour suivi.

**⟳ Rafraîchissement (5ᵉ passe, 2026-07-07, 4 agents — § en fin de document).** Beaucoup de
statuts ci-dessous ont bougé depuis : le commit `bc15a67` (2026-07-03, titre trompeur « STOP
handling ») contient l'INTÉGRALITÉ du bug hunt 39-findings — **M1 (fronts GPIP), Timer B evt=0,
$FF8264, VoidRead 0x00, read32 void, trou MMU STF, bruit ≥/250 kHz, `mode_ &0x8f` sont CORRIGÉS** ;
**D4, BL-MST, cartouche 0xFF, bits SR MIDI sont des FAUX POSITIFS** (Hatari fait pareil) ; V3 est
**partiellement résolu** (restart compteur porté ; CLOS le 2026-09-01). Nouvelles entrées : **S4 table DAC YM**
(défaut Hatari = table mesurée, NeoST = modèle — corrigé le 2026-07-07), **hybride WS1/WS3**
(tranché WS3 le 2026-07-08, `VideoGlue.hpp:65-147`) et, à l'époque,
troncature MFP→CPU sans reste (**portée depuis le 2026-08-14**). La 5ᵉ passe fait foi en cas
de contradiction avec les sections historiques.

---

## Lignes NO_DE et STOP_MIDDLE sans palette — corrigé le 2026-10-19

**Sévérité moyenne, défaut NeoST démontré.** `Shifter::finishFrame` ne lançait
`renderGlueFrame` que pour une bordure élargie, une écriture palette ou un
VBlank couvrant l'aire normale. Les masques NO_DE/STOP_MIDDLE calculés par la
Glue restaient donc invisibles sur une trame sans ces déclencheurs, malgré
une avance mémoire correcte. Le boot généré `blankline_node_st` différait
de toutes les 61 captures Hatari de la fenêtre 350–410 (157 pixels sur la
ligne masquée pour la capture 380). Le défaut est aussi reproduit par deux
assertions de pixels dans chaque variante de grille Glue.

Le rendu final est désormais déclenché par NO_DE/BLANK/STOP_MIDDLE aussi.
`bordersTrick_` conserve son sens d'extension pour le cadrage adaptatif.
La référence comparative est `Video_CopyScreenLineColor` dans `video.c` au
pin `f0736b24` : NO_DE saute la lecture et l'avance RAM, BLANK efface après
lecture, STOP_MIDDLE raccourcit la quantité lue. Onze contrôles Glue et les
sondes `blankline_*` protègent ces différences. `BORDERMASK_EMPTY_LINE` et
`BORDERMASK_BLANK_LINE` sont déclarés mais plus produits à ce pin : aucun
port de leurs branches mortes. L'usage de palette 0 pour masquer reste une
convention d'émulateur ; sa couleur physique attend une mesure sur Atari.

**Résidu STOP_MIDDLE ouvert, sévérité basse.** Les deux captures ST/STE
diffèrent de Hatari sur trois pixels au dernier groupe de plans, premier
écart `(156,65)` du buffer 416×276. Chacune des 61 captures Hatari 350–410
est identique. `Video_CopyScreenLineColor` recopie 54 octets puis efface la
suite du buffer planaire, laissant décoder un dernier groupe partiel ;
NeoST limite les pixels par la fin DE à 164 cycles (normalisée). Les autres
lignes et l'adresse accumulée concordent. Les deux sondes STOP_MIDDLE sont
donc gardées en **snapshot**, jamais comme références Hatari tolérées.
La [capture comparative Hatari](../tests/reference/blankline_stop_oracle.png)
est conservée à côté des snapshots pour examiner le résidu sans relancer
l'oracle (ST et STE byte-identiques). Reproduction :
`python3 tools/make_blankline_test.py /tmp/blankline_stop.st`, puis
`bash tools/hatari_oracle.sh roms/etos192fr.img /tmp/blankline_stop.st 435 380 /tmp/blankline_stop.png st fastfdc 0`.
L'[explication du shifter par Paulo Simoes](https://alive.atari.org/alive11/oscan2b.php)
décrit les groupes de quatre plans et précise que le modèle est simplifié ;
les [notes de Steven Tattersall et Troed](https://github.com/tattlemuss/atari_st_wiki/wiki/StevesVideoNotes)
décrivent les décalages GLUE/MMU. Aucune ne tranche ces trois pixels dans
notre séquence. Mesure du bord sur Atari requise avant changement.

## NO_SYNC/SYNC_HIGH — gardes comparatives ajoutées le 2026-10-19

Les branches `Shifter::updateGlueState` correspondaient déjà à
`Video_Update_Glue_State` au pin `f0736b24` : impulsion hi-res avant HSYNC
(NO_SYNC) ou pendant celui-ci (SYNC_HIGH), masque NO_DE/NO_COUNT sur la ligne
suivante, et extension d'une ligne de la fenêtre basse. Le retour avant le
comparateur concerné annule les masques et l'extension. Le retour SYNC_HIGH
sur la ligne suivante doit conserver NO_DE grâce à la garde NO_COUNT.

Le générateur `make_blankline_test.py` couvre les quatre séquences sur ST
et STE ; les longs successifs rendent une avance mémoire fausse visible.
52 contrôles Glue vérifient les masques live/replay, l'absence de tic Timer B
sur la ligne vide, le stride nul, le retour au stride normal, l'extension
basse et les 32 000 octets consommés au total. Ce travail protège le modèle
existant ; il ne démontre pas la forme d'onde HSYNC/VDE réelle ni la couleur
physique du blanking, et ne change aucune constante pour rejoindre Hatari.

## Bilan de fidélité par sous-système

| Sous-système | NeoST | Hatari (vérité) | Fidélité | Écarts ouverts M / B (5ᵉ passe 2026-07-07) |
|---|---|---|---|---|
| MFP 68901 | `Mfp.cpp` | `mfp.c` | très élevée | 0 moyennes (UpdateTimers CLOS + compteur vivant ✅ 2026-09-02) / 5 |
| Vidéo (Shifter/Glue) | `Shifter.cpp` | `video.c` | très élevée (STF WS3 + Glue STE + tricks res) | 0 moyennes (WS/V1/V2 ✅ 2026-07-08 ; résidu V2 : Paulo Simoes — $FF8261/res 3 ✅ 2026-10-20) / ~9 |
| FDC + DMA + STX | `Fdc.cpp`, `StxImage.cpp` | `fdc.c`, `floppies/stx.c` | très élevée | 0 (D3 ✅ 2026-08-26 ; wait-state 4 cyc des registres ✅ 2026-10-18, `Fdc::registerWait`) / 4 (+2 assumés D1/D2) |
| Son (YM2149 + DMA STE) | `YM2149.cpp`, `DmaSound.cpp` | `psg.c`, `dmaSnd.c`, `sound.c` | très élevée (générateurs 1:1, FIFO DMA au faisceau) | 0 (S2/S3/S4 ✅ 2026-07-07) / ~6 |
| ACIA 6850 / IKBD / MIDI | `Ikbd.cpp`, `MidiAcia.cpp` | `acia.c`, `ikbd.c` | très élevée | 0 / 7 (délais IKBD…) |
| Bus / mémoire / bus-error | `Bus.cpp` | `ioMem*.c`, `memory.c`, `stMemory.c` | très élevée | 0 / 0 (densité STE corrigée, void blitter déjà conforme) |
| Blitter | `Blitter.cpp` | `blitter.c` | très élevée sur les **données** (byte-identique à l'oracle, mesuré 2026-08-26) ; base de temps unifiée (BL3 ✅) et dispatch par accès (BL4 ✅) | BL3/BL4/BL5 clos ; MegaSTE 16 MHz/cache à qualifier |
| SCC Z85C30 (MegaSTE) | `Scc.cpp` | `scc.c` | élevée (cœur registre) | 1 (SC1, tranché) / 2 |

---

## Priorités — divergences HAUTE et MOYENNE

⚠ **Deux systèmes d'identifiants coexistent dans ce document, ne les confondez pas.** Ici (et
dans les sections « Détail par sous-système ») le préfixe désigne le **SOUS-SYSTÈME** : `BL`
Blitter, `V` vidéo, `S` son, `D` FDC, `M` MFP, `BU` bus, `SC` SCC, `MC` conversion MFP→CPU.
Dans le tableau **« Ouvertes (basses, consignées sans correction) »** en fin de document, le
préfixe désigne la **SÉVÉRITÉ** (`M` moyenne, `B` basse) et la numérotation est indépendante :
son `B4` est un point Ikbd, sans aucun rapport avec `BL4` ci-dessous. *Les entrées Blitter
étaient numérotées `B1`/`B3`/`B4` jusqu'au 2026-08-25 — renommées `BL1`/`BL3`/`BL4` pour lever
la collision, `BL2` portait déjà le bon préfixe. Les messages de commit antérieurs à cette date
citent les anciens identifiants.*

| # | Sous-système | Divergence | Sévérité | NeoST | Hatari |
|---|---|---|---|---|---|
| BL1 ✅ | Blitter | Compteur X/Y écrit à `0` non interprété comme **65536** (blit avorté au lieu de maximal) | **HAUTE** | `Blitter.cpp:201-211` | `Blitter_WordsPerLine/LinesPerBitblock_WriteWord` `blitter.c:1343-1366` |
| BL3 ✅ | Blitter | ~~**Cycles de stall facturés HORS de l'horloge de l'ordonnanceur**~~ **corrigé (2026-08-25)**. `Blitter::onSlice` est le callback de l'échéance `Scheduler::BLITTER` : il tourne dans `Scheduler::runTo`, donc ENTRE deux `cpu.run()`. Ses cycles n'étaient ni dans `ran` (mesuré depuis un `quantumStartBus_` réancré à chaque entrée de `Cpu68k::run`) ni dans `sched.now()` — **perdus**, puis résorbés d'un coup par le `syncTo` de `Cpu68k::rebaseQuantumAndSync`. Mesuré (`NEOST_QDELTA_DIAG`) : escalier 136→**1088 cycles bus** sur *Lethal Xcess*, mangeant 2 tics de prescaler Timer A → `Mfp::readTimerData` rendait TADR `$3C` → garde `ILLEGAL` du jeu. Cassait `megast`, `ste` ET `megaste`. Porté par `Blitter::billCycles` → `Scheduler::addStolenCycles`, discriminé par `Cpu68k::inRun` (le mode HOG est déjà capté par `ran`). | **HAUTE** | `Blitter.cpp:billCycles`, `Scheduler.hpp:addStolenCycles`, `Cpu68k.hpp:inRun` | `Blitter_AddCycles` `blitter.c:342-354` (`nCyclesMainCounter`/`CyclesGlobalClockCounter`), `CycInt_Process` `cycInt.h` |
| BL4 ✅ | Blitter | ~~Dispatch en FIN de tranche, pas après chaque accès~~ **CORRIGÉ (2026-08-25)** : la facturation est passée **par accès**. `Blitter::billCycles` (port de `Blitter_AddCycles` + `Blitter_FlushCycles`) est appelé par `readWord`/`writeWord` pour leurs 4 cycles et à **chaque arbitration**, à sa position réelle (prise du bus AVANT le transfert, restitution APRÈS) au lieu d'un lot en fin de tranche ; `Scheduler::addStolenCycles` DISPATCHE désormais (`syncTo`) au lieu d'avancer `now_` en silence. L'obstacle — `Scheduler::runTo` non ré-entrant — est levé : `fired`/`minAll` étaient déjà des locales, seul `firingDue_` manquait, il est sauvegardé/restauré par un garde RAII (`FiringGuard`). C'est le modèle d'Hatari, dont `CycInt_Process` (`cycInt.h:85-88`, `while (ActiveInt <= now) CallActiveHandler()`, **sans masque anti-relance**) est lui aussi ré-entré depuis le handler `INTERRUPT_BLITTER`. Le membre `inDispatch_` introduit par BL3 devient inutile et est retiré. **Mesuré** : `timer IRQ max lateness` **265 → 132** sur Lethal Xcess/`megast` (= la valeur ST, sans plus rien masquer), **170 → 132** sur `ste`/`megaste`, **265 → 161** au boot EmuTOS `megast` nu. Validé assertions ACTIVES (`-UNDEBUG`) : 26000 trames, ~1,6 M préemptions, aucune assertion de `runTo` déclenchée. `--tier full` OK ; `machine=st` capture **bit-identique** (le patch y est inerte, pas de blitter). | — | `Blitter.cpp:billCycles` / `readWord` / `writeWord` / `onSlice`, `Scheduler.hpp:addStolenCycles` / `runTo` | `Blitter_FlushCycles` `blitter.c:356-374` (appel `CycInt_Process` `blitter.c:366`), `CycInt_Process` `cycInt.h:85-88` |
| BL5 ✅ | Blitter / MFP | **Clos le 2026-10-19** : reprises sous-instruction, recouvrement CPU, PRE_START commun HOG/partagé et alignement au début du cycle RAM. `blitter_timer` : **68 → 0 px**, promu en oracle Hatari reproductible. Dix-sept sondes temporelles strictes, dont dix blitter (ROM/RAM, TAS, MULU, tailles). Les anciennes estimations et réfutations du 2026-08-26 sont historiques ; voir la section Blitter pour la clôture. |
| V1 ✅ | Vidéo | ~~Branche STE de la Glue absente~~ **portée (2026-07-08)** : table STE (preload MMU 36/40, pal 56, HSync −52/−12) + `LEFT_OFF_2_STE` (+20 o, −8 px) + latch res sans −1 ; Cuddly-STE casse comme le vrai STE (196/250 = oracle) | moyenne | `VideoGlue.hpp` (`glue::Timing`, phase 1 STE) | `Video_Update_Glue_State` (branche STE) `video.c:2442-2652` |
| V2 ✅ | Vidéo | ~~Tricks par changement de résolution~~ **portés (2026-07-08)** : overscan med-res (No Cooper greetings **0 px vs oracle**), stab med, scrolls hard 13/9/5/1 px, rendu multi-rés par ligne. Résidu : hardscroll Paulo Simoes (alias $FF8261 et Shifter arrêté res 3 ✅ portés le 2026-10-20, `Shifter::writeShifterRes`) | moyenne | `VideoGlue.cpp` (`updateGlueRes`) | `Video_WriteToGlueRes` `video.c:1618-1820` |
| V3 ✅ | Vidéo | **CLOS le 2026-09-01** — l'attribution à la grille RÉELLE (longueur de ligne persistante, ≙ `Video_StartHBL`/`nCyclesPerLine`) est le défaut ; exhibiteur généré `freq_switch`, 18/18 écritures à la même ligne et au même cycle qu'Hatari (trace `video_sync`), palier pixel à 0 px, `=0` fait rougir l'étalon. Historique : Géométrie mid-trame : **restart compteur PORTÉ** (VC_RESTART, 2026-07-02). ⚠ **« CyclesPerVBL ±4 » RETIRÉ — FAUX POSITIF prouvé (2026-08-25)** : chez Hatari la seule affectation *pleine* de `CyclesPerVBL` est dans la branche **VDI** (`video.c:4937-4942`, avec le commentaire d'Hatari « *When using VDI, we setup the next VBL here ; else it will be setup at the start of the last HBL* ») ; en mode normal la VBL vient de la chaîne `ShifterLines[].StartCycle` (`video.c:3354, 3370-3374`), que NeoST reproduit par `lineCarry_`/`beginFrame_` **actifs par défaut** — mesuré sur No Cooper : distribution des longueurs de trame `{160256: 6788, 160252: 10, 133604: 3}`, et 160252 = 313×512−4. Le mécanisme fonctionne. **Reste** l'attribution de ligne : le canal `NEOST_LINELEN` est **HYBRIDE** — moitié Machine ON (`Machine.cpp`), 4 sites Shifter OFF (`Shifter.cpp`). **Aucun étalon ne l'exhibe** (11/11 à 0 px dans les 3 configurations : défaut, `=1`, `=0` ; deux recontrôlés en MD5 byte-identique). ~~Seul exhibiteur connu = Closure~~ — **RÉFUTÉ le 2026-08-30** : `closure` est désormais un étalon (écran 153 couleurs, trame 10500) et il rend une image **BIT-IDENTIQUE avec `NEOST_LINELEN_ATTR=1`** (même md5, 0 px). Cet écran n'exerce donc PAS V3 ; la phrase datait du chantier CLOSURE (écran noir au boot), pas d'une mesure du verrou. V3 reste **sans exhibiteur mesuré**. Ce que Closure a rapporté à la place : **A40** — décalage de 4 px de la fenêtre gauche, non pas sur `glue::LEFT_OFF` mais sur le **retrait gauche med** (`LEFT_OFF_MED`, « remove left + med stab ») ; **CORRIGÉ le 2026-08-30** (`shEff = 4 + scrollFinal` → −4, plus les 8 dernières colonnes laissées à l'index 0) : oracle 64,08 % → **0,02 %**, cf. `CHANGELOG.md` et § *Cas où NeoST améliore Hatari* (A41) pour les 27 px restants. | basse | `VideoCounter.cpp` (`restartVideoCounter`), `Machine.cpp` | `Video_RestartVideoCounter` `video.c`, `video.c:3354,3370-3374,4937-4942` |
| WS ✅ | Vidéo | ~~Hybride WS1/WS3~~ **TRANCHÉ : WS3 complet (2026-07-08)** — positions Glue +1 (`glue::kWsInc`), IRQ HBL à cpl (512/508/224, `kHblOff` 0), VBL 64 ✓. Ancres rendu/compteur/spec512 **fixes** 56/376 (≙ `LINE_START/END_CYCLE_*` hors table WS chez Hatari) ; DE stockés re-normalisés −inc au rendu. `NEOST_WS=1..4` pour A/B. Datations read −6/write +2/spec512 −25 **inchangées** (fidèles-théoriques, WS-indépendantes). Validé : étalons TOUS OK, boot STF 50 Hz 0 px, Cuddly menu == HEAD au px près (190/250 vs oracle, identique baseline) | moyenne (systémique) | `VideoGlue.hpp`/`VideoGlue.cpp` (glue::), `Machine.cpp` | `VIDEO_TIMING_DEFAULT=WS3` `video.c:624`, `video.c:976-1007` |
| S1 ✅ | Son | **Filtre passe-bas STF (C10) jamais activé** → STF/Mega ST en PWM (code mort) | moyenne | `setStfLowPass` jamais appelé, `YM2149.hpp:186` | `Sound_Update_Filters` `sound.c:1946-1951` |
| S2 ✅ | Son | ~~DMA STE sans FIFO 8 octets ni avance HBL~~ **corrigé (2026-07-07 soir)** : FIFO 8 octets fetchée par MOTS à chaque HBL (`DmaSound::onHbl` ← `Machine::onHbl`), fin de trame **au fetch** (XSINT/Timer A en avance, HBL-quantifié), octets **capturés au faisceau** pour le rendu (plus de relecture RAM en fin de trame — cas Mental Hangover), réalignement mono→stéréo, compteur $FF8909 = adresse de fetch. Étalon `tools/make_dmasnd_test.py` : 33,3 % d'octets B = oracle Hatari (33,2 %) | moyenne | `DmaSound.cpp` (`fifoRefill`/`updateDac`) | `DmaSnd_FIFO_*` / `DmaSnd_STE_HBL_Update` `dmaSnd.c:342-438,727-741` |
| D1 | FDC/DMA | **WRITE TRACK STX réinterprété** en secteurs (CRC « nettoyé », statut neutralisé) | moyenne | `StxImage.cpp:293-344` (`reinterpretSaveTrack`), `Fdc.cpp:1674-1701` (`writeTrackStx`) | `FDC_WriteTrack_STX` (TODO, pas de relecture) `stx.c:2027-2134` |
| D2 | FDC/DMA | **READ TRACK STX** renvoie la piste réécrite (conséquence de D1) | moyenne | `Fdc.cpp:1829` (`readTrackStx`) | `FDC_ReadTrack_STX` `stx.c:1863` |
| D3 ✅ | FDC/DMA | ~~Flush FIFO↔RAM ne stalle pas le CPU~~ **CORRIGÉ (2026-08-26, 3ᵉ tentative — la bonne formulation est `due + stall + delay`).** Les quatre formulations MESURÉES (sonde `NEOST_FDC_FLUSH_DIAG=1`, régime de rafale, Blood Money) : `nowCyc()+delay` (l'ancien code) = **4203** cyc/flush (+76 vs Hatari) ; `due+delay` (ancrage pur) = 4096 (stall absorbé) ; `nowCyc()+stall+delay` = 4235 (ancrage neutralisé) ; **`due+stall+delay` = 4127,8** — cible Hatari **4127**. La clé est l'ORDRE chez Hatari : `PendingCyclesOver` est capturé EN TÊTE de handler (fdc.c:2332), PUIS `M68000_AddCycles_CE` avance le compteur global, PUIS le réarmement retranche l'overshoot (fdc.c:2388) — le stall échappe donc à l'ancrage et décale réellement la suite. Implémenté : stall 32 cyc aux deux flushs avec l'**invariant BL3** (`addBusWaitCycles` + `Scheduler::addStolenCycles`, hors quantum) ; ancrage sur `Scheduler::firingDue()` ; report du stall via `dmaStallPending_` (transitoire, remis à zéro par RAII à chaque événement, toujours nul à une frontière de trame → non sérialisé). **Validé** : mécanisme chiffré (`NEOST_FDC_LATE_DIAG=1` : dispatch en retard de 6,7 cyc en moyenne, ~107/cycle de 16 o = l'écart 4203→4096) ; **14/14 étalons pixel verts**, dont `nocooper` **0 px vs oracle** (réf re-posée, cf. sa note : la démo exige un ESPACE que `hatari_oracle.sh` n'injecte pas — `--cmd-fifo` + `hatari-event keydown 57`) ; canaris Lethal Xcess (aucun break) et Stardust STE OK. ✅ Suivi lateness 132→252 CLOS (instrumenté le jour même) : **deux pics isolés de Timer D** (161 et 252 cyc) sur ~300 000 échéances — un timer échu pendant une instruction allongée par le stall DMA intra-quantum attend la fin de l'instruction, comme pour tout wait-state. Fidèle ; le régime est inchangé. ⚠ Leçon consignée : DEUX faux verdicts successifs venaient d'artefacts d'outillage — sonde logée dans le correctif (inactive côté témoin), puis AVI oracle FIGÉ (7297 × la même frame) lu comme « diff constante ». Contrôle désormais obligatoire avant tout scan : **md5 de 2-3 frames éloignées de l'AVI**. | — | `Fdc.cpp` (`billDmaCycles`, `fifoPush`/`fifoPull`, `onFdcEvent`) | `FDC_DMA_FIFO_Push/Pull` `fdc.c:1355,1411`, `-PendingCyclesOver` `fdc.c:2332,2388` |
| M1 ✅ | MFP | ~~Lignes GPIP on-chip sans machine de fronts AER/DDR~~ **corrigé (bc15a67)** : `gpipSetLine`/`gpipUpdateInterrupt` = port de `MFP_GPIP_Set_Line_Input`, tous les appelants convertis | moyenne | `Mfp.hpp:547-560`, `Mfp.cpp:1101-1116` | `MFP_GPIP_Set_Line_Input` `mfp.c:1143-1219` |
| S4 ✅ | Son | ~~Table DAC « model » seul~~ **corrigé (2026-07-07 soir)** : table MESURÉE par défaut (`ym2149_fixed_vol.h` vendorisé + port de `interpolate_volumetable`), modèle conservé sous `NEOST_YM_MIXING=model` | moyenne | `YM2149.cpp` `dacTable()` | `YM_TABLE_MIXING` défaut `configuration.c:807`, `sound.c:505-543` |
| S3 ✅ | Son | ~~Gain LMC ×2 manquant (YM STE −6 dB)~~ **corrigé (2026-07-07 soir)** : `kLmcMakeup=2.0` dans `gainLeft/Right/masterGain` + `kDmaGain` 0.7→0.375 (= ¾×½). Validé : cloche GEM ST vs STE ratio RMS **1.000** ; ratio DMA 0.75 exact | moyenne (audible) | `DmaSound.cpp` | ×2 `dmaSnd.c:1152-1153,1460-1461` + « 3/4 level » `dmaSnd.c:1146-1158` |
| MC ✅ | MFP | ~~**Conversion MFP→CPU tronquée par période, sans reste accumulé**~~ **corrigée (2026-08-14)** : échéances absolues ×256, plafond vers le Scheduler, phase sérialisée v11 | moyenne-basse | `Mfp.cpp` (`timerPeriodSubCycles`, `scheduleTimerAt`) | unités internes ×256 `cycInt.c:26-45` |
| BU1 ✅ | Bus | **Miroir matériel du PSG `$FF8804-$FF88FF`** non routé vers le YM2149 (lit `0xFF`, écritures ignorées) | moyenne | `Bus.cpp:532-533,844-862` | `IoMem_Init` shadow PSG `ioMem.c:386-393` |
| BL2 ✅ | Blitter | Accès **OCTET** aux registres mot/long non rejetés | moyenne | `Blitter.cpp:148-159` | `Blitter_CheckAccess_Byte` `blitter.c:972-989` |
| SC1 ✅ | SCC | TX/RX cadencés, fractions directes/BRG et Zero Count avant /2/WR4 ; bouclage local et multiplexage RxD/TxD portés. Restent phase sous-bit, DPLL/SDLC et backend hôte. | limites documentées | `Scc::serialPeriod/updateBrg/onBrgEvent/serialWriteByte` | `SCC_WriteDataReg/SCC_Process_TX`, Zilog UM0109 et MAME |
| D4 ✖ | FDC | ~~Piste 6268 vs 6250~~ **FAUX POSITIF (5ᵉ passe)** : le `#define FDC_TRACK_BYTES_STANDARD` actif d'Hatari vaut **6268** (la formule 6250 y est commentée) = NeoST | — | `Fdc.cpp:163` | `fdc.c:~410` |

---

## Détail par sous-système

### MFP 68901 — `Mfp.cpp` ↔ `mfp.c`

- ✅ **GPIP7 composé et XSINT (2026-10-19)** : détection du front après
  calcul complet du niveau, incluant le bouton Multiface déjà modélisé.
  Plus d’IRQ parasite lorsque XSINT change sous un bouton maintenant la pin
  basse ; DDR/AER conservés. Aucun nouveau modèle électrique Multiface.

- ✅ **VR bits réservés (2026-10-19)** : bits2..0 lus zéro et ignorés en
  écriture, selon Motorola MC68HC901UM §4.1.3 et MAME REGISTER_VR. Base
  vectorielle et bit S inchangés ; anciennes valeurs brutes sauvegardées
  masquées à la lecture. Hatari garde les bits permissifs, contrairement au
  manuel. Aucun champ ajouté ni changement de format v44.
Chaîne IRQ (IER/IPR/IMR/ISR/VR, élection priorité + chronologie, délai 4 cyc, IACK, EOI
auto/software, antidatage), timers datés, compteur vivant, event-count Timer A/B : **conformes**.

- **[M1 — moyenne] ✅ corrigé (bc15a67, 2026-07-03)** — machine de fronts centralisée :
  `gpipSetLine`/`gpipUpdateInterrupt` (`Mfp.hpp:547-560`, `Mfp.cpp:1101-1116`) = port de
  `MFP_GPIP_Set_Line_Input`/`_Update_Interrupt` (mfp.c:1143-1219), AER/DDR respectés, tous les
  appelants on-chip convertis (Ikbd/MidiAcia wire-OR/Fdc/Blitter). Cf. 5ᵉ passe.
- **[basse] ✅ corrigé (2026-08-14)** — conversion cycles MFP↔CPU en échéances absolues
  de 1/256 cycle CPU (`timerPeriodSubCycles` + `timerDueSub_`), même `CYCINT_SHIFT=8`
  qu'Hatari. Le plafond n'est appliqué qu'à la frontière du Scheduler et la fraction
  reste l'ancre du rechargement suivant ; phase sérialisée en save-state v11. *Hatari
  ajoute aussi un jitter aléatoire volontaire pour Lethal Xcess, non reproduit.*
- **[basse] ✅ corrigé (bc15a67)** — Event-count Timer B : garde `tbCounter_==0` supprimée,
  wrap uint8 0→255 (`Mfp.cpp:834-850`, `hblankAt` = `MFP_TimerB_EventCount` mfp.c:1297-1320).
- **[basse]** Retombée d'IRQ immédiate (`Mfp.cpp:1165-1193`, `updateIrq`) vs TODO Hatari d'un délai mesuré (`mfp.c:789`) — identique aujourd'hui, suivi seulement.
- *Faux positif écarté :* les wait-states 4 cyc d'accès MFP **sont** appliqués, mais par le
  `Bus` (`Bus.cpp:896`, `addMfpWaitCycles()`), pas par `Mfp.cpp` → **pas une divergence**.

### Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`

- ✅ **Longueur de la ligne qui commence = registres LIVE (2026-10-22, bug NeoST)** —
  `Machine::onHbl` replanifiait le HBL suivant sur la longueur de ligne verrouillée à
  `beginFrame` (`cpl_`), alors que le Timer B suivait la grille Glue réelle : trame
  verrouillée 60 Hz (`$FF820A` = 0 au reset) passée en 50 Hz après `Line_Set_Pal` →
  HBL à 508 contre lignes de 512, dérive −4 cycles/ligne (sonde Super Hang-On : 53 chaînes
  avec HBL non pendante au `stop`, première VBL 1 048 cycles trop tôt = 262 × 4). Désormais
  `Shifter::liveLineLength()` (224 / 512 / 508 d'après `$FF8260` et `$FF820A`) à chaque
  HBL, comme `Video_StartHBL` + `Video_HBL_GetDefaultPos` (`video.c:3541/3134`) et le mux
  de rechargement du compteur `hsc` du GST MCU (`gstmcu.v:779-801`) — FRÉQUENCE seule
  (512/508) : l'événement HBL de NeoST tombe quelques cycles après le début de ligne, où une
  démo plein écran est encore dans son impulsion hi-res de retrait gauche (504→8) ; lire la
  résolution à cet instant donnait 224 à tort (Cuddly : lignes raccourcies, 11 trames
  d'avance). Les lignes 224 restent décidées par le verrou de trame et les Freq_match 71 Hz.
  Le restart du compteur vidéo (ligne 310/260) est re-posé sur la grille réelle quand des
  lignes ont été raccourcies (sinon `overscan_top` voyait `$FF8207` repartir 148 cycles trop
  tard). Effet de bord : la première VBL arrive 1 048 cycles plus tard (= Hatari), ce qui
  tourne d'un cran la phase E-clock (1048 ≡ −2 mod 10) des étalons ancrés VBL+STOP et
  déplace les trajectoires de boot (Cuddly : une rotation de disquette) — `spec512_*`,
  `cuddly_demos`, `nocooper`, `dmasnd_poll`, `blankline_blank_st/ste` recalés sur une trame
  bit-identique (balayages consignés dans `etalons.json`).
- ✅ **Événement échu à la frontière d'instruction (2026-10-22, bug NeoST)** — l'élection
  MFP différée en fin de lot de dispatch posait `MFP_IRQ` « à now » après la boucle de
  `runTo` ; `Machine::runFrame` exécutait alors une instruction de plus avant de le servir
  (tic Timer B tombé dans un `bra.s` : exception 4 cycles après Hatari, périodes 508
  par paires). Désormais un événement déjà échu à la frontière est servi avant toute
  instruction (une passe, anti-livelock), comme la boucle CPU d'Hatari.
- ✅ **Fin de trame et dernière ligne (2026-10-22, bug NeoST)** — `frameEnd_` n'intégrait
  l'écart d'une ligne (live ≠ verrouillée) qu'à son HBL de FIN ; pour la dernière ligne ce
  HBL tombe à la frontière de trame, donc APRÈS `frameEnd_` quand la ligne est plus longue :
  trame de reset (60 Hz verrouillée, 263 lignes) passée à 50 Hz par le programme → 4 cycles
  trop courte, HBL de la ligne 262 jamais servi, toute la suite à −4 cycles d'Hatari (sonde
  `timer_b_event` : première IRQ Δ=6 au lieu de 2). `Machine::syncFrameEnd_` recalcule la
  fin de trame dès que la longueur de la ligne en cours est connue (HBL de début, Freq_match)
  — équivalent d'Hatari qui pose la VBL depuis le dernier HBL réel (`video.c:3370`).
- ✅ **Fin de trame sur ligne raccourcie (2026-10-22, bug NeoST hors Glue)** — `Machine::onHbl`
  cumulait `lineCarry_` sans raccourcir `frameEnd_` : la trame durait sa longueur nominale,
  puis `beginFrame_` posait `frameStart_` (−carry) et la VBL dans le passé → gestionnaire
  VBL servi avec le retard du raccourcissement (224 cycles sur une ligne « no DE » hi-res
  tardive ; invisible pour une ligne 508). Étalon `blankline_node_late_st` : écriture
  `$8260` alternant 44 et 268 d'une trame à l'autre, 10 714 px → 0 px. Hatari pose la VBL
  depuis le dernier HBL réel (`video.c:3370`).
- ❗ **STE : écriture 60 Hz aux cycles 32-36 — divergence réelle, OUVERTE (2026-10-22).**
  `blankline_blank_ste` : 0 px à la phase 28, mais aux phases 32/36 du cycle de 5 trames
  NeoST rend une image (10 714 px, signature d'un décalage d'une ligne) qu'aucune des 61
  trames de l'oracle ne produit (`--oracle-check`). Pré-existante, masquée par la phase ;
  l'étalon est recalé sur la phase 28. À analyser : branche 60 Hz `<= Preload_Start_Low_60`
  (36) / fenêtre NO_DE (36,40] de `VideoGlue.cpp` contre `video.c` et la netlist (cf. TODO 4.1).
- ⚠ **STE : impulsion hi-res tardive (40,56] = « no DE » + ligne 224 (2026-10-22, netlist
  GST MCU) — écart assumé avec Hatari, aligné sur le matériel.** Hatari (`video.c:2503`)
  borne la détection no-DE à `Preload_Start_Low_50` = 40 sur STE et rend STOP_MIDDLE +
  ligne 512 entre 40 et 56 ; la netlist SYNC.PIN (4081P26 SYM 18-31 : longueur de ligne =
  f(MDE1) au rechargement du compteur HSC, bord au cycle ~56 ; 4081P29 SYM 38-40 : HDE
  jamais posé si MDE1=1 à ~44 puis MDE1B=0 à ~56) donne no DE + 224 jusqu'à 56, comme le
  STF. `VideoGlue.cpp` branche 71 Hz : `<= HDE_On_Low_50`. Étalon `blankline_node_late_ste`
  en snapshot. Réserves : cycle 56 à ±4 ; fenêtre (40,44] (DE précoce prédit à ~48) non
  portée ; aucune mesure STE.
- **[assumée, 2026-10-22] Placement des pixels d'une ligne LEFT_PLUS_2 — Hatari décale de
  4 px.** Même état Glue des deux côtés (`H_DE 53<->377 border=2`, DE 52→376) pour une
  impulsion 60 Hz aux cycles 32-36 ; `Video_CopyScreenLineColor` (`video.c:4047-4056`)
  copie 160 octets à un offset fixe et range les 2 octets left+2 dans le plan 3 du groupe
  x=32-47, NeoST émet depuis DE=52 plans alignés — compteur de pixels du Shifter réarmé au
  premier LOAD après DE (decap Cwik, `shifter_video.v`), principe qu'Hatari applique
  lui-même à LEFT_OFF (`DisplayPixelShift=-4`, `video.c:256`). Même famille qu'`overscan_top`.
  Étalon `blankline_blank_st` passé en `ref_kind: snapshot` (phase 32 ; à phase 28 l'image
  est bit-identique à l'oracle).
Cœur Glue **STF** = transcription quasi ligne-à-ligne (`updateGlueState` ≙ `Video_Update_Glue_State`
branche STF), constantes = `VIDEO_TIMING_STF_WS1`, compteur vidéo, bordures H/B/G/D 50/60 Hz,
spec512, scroll fin STE de base, masquage palette par machine : **conformes**.

- **[V1 — moyenne] ✅ PORTÉ (2026-07-08)** — branche **STE** de la Glue (phase 1 de
  `updateGlueState` + table `glue::Timing` STE : preload MMU, `Line_Set_Pal=56`,
  `LEFT_OFF_2_STE` +20 o/−8 px). Cf. 5ᵉ passe (statut à jour). Le menu Cuddly sur NeoST-STE
  casse désormais comme le vrai STE (196/250 trames = oracle Hatari-STE) — un rapport GUI
  « menu cassé en STE » est FIDÈLE, vérifier la machine avant de conclure à un bug.
- **[V2 — moyenne] ✅ PORTÉ (2026-07-08)** — ~~tricks de bordure par **changement de
  résolution** non répliqués~~ : `Shifter::updateGlueRes` (`VideoGlue.cpp:731-805`, appliqué au
  rejeu de fin de trame `VideoGlue.cpp:893`) = port des détections 1637-1753 de
  `Video_WriteToGlueRes` — overscan med-res, `LEFT_OFF_MED`, variante STE, stab med, scrolls
  « hardware » 13/9/5/1 px ; rendu multi-résolution par ligne (`Shifter.cpp:873-878`).
  No Cooper greetings 0 px vs oracle. Restent hors port : hardscroll 4 px (video.c:1755-1789),
  hacks Closure/DOLB (1791-1834). L'écriture `$FF8261` et le Shifter arrêté (res 3) sont
  portés depuis le 2026-10-20 (cf. § *Nouveaux — BASSES*, Vidéo). `NEOST_V2` ne gate
  plus que le raccourcissement HBL (`Machine.cpp:158-163`, opt-in).
- **[assumée, 2026-10-20] Shifter arrêté (res 3) : stop et retour sur la MÊME ligne**
  (`Shifter::writeShifterRes`, `VideoGlue.cpp`). Hatari ne compare que des cycles-LIGNE
  (`ResPosStop_Shifter.LineCycles`) : un arrêt posé à la ligne N et levé à la ligne N+1 passe
  son « ≤ 32 ». Sur une durée négative, il réécrit `DisplayPixelShift = 0` sur les lignes
  suivantes ; sur une durée positive qui chevauche le HBL, il arme un décalage. NeoST
  n'arme rien dans les deux cas. Avant le correctif, la normalisation modulo 16 en faisait
  −4/−8/−12 px et −2/−4/−6 octets à chaque ligne. Le trick Troed/Sync décrit un arrêt de
  8 à 32 cycles ; le comportement matériel d'un arrêt plus long n'est pas documenté.
- **[2026-10-20] Datation res 3 sur la grille RÉELLE** : `realLinePos` date l'écriture sur
  `glueLineStart_`, comme `Video_ConvertPosition` (video.c:1224) et comme la machine Glue
  pour freq/res. Ce n'est pas une divergence : NeoST rejoint Hatari. L'ancienne grille
  nominale `fc/cpl` dérivait de 4 cycles par ligne de 508 cycles. Avec
  `NEOST_LINELEN_ATTR=0`, c'est la grille nominale qui s'applique.
- **[EXTENSION NeoST, 2026-10-20] Ligne noire « Shifter arrêté »** : une ligne est noircie
  (aire active seulement, bordures intactes) si un intervalle res 3, en cours ou le dernier
  terminé, couvre toute sa fenêtre DE (`lineStoppedOver`). Hatari n'affiche pas de noir.
  Un arrêt partiel (trick, 8 à 32 cycles) ne noircit rien, comme Hatari (TODO video.c:1914).
  Noir franc ou couleur 0 : non mesuré (TODO.md § 2).
- **[V3] ✅ CLOS le 2026-09-01** — attribution de ligne à la grille réelle **promue par défaut** (`NEOST_LINELEN_ATTR`, ON) après preuve à l'oracle sur l'exhibiteur `freq_switch` : la longueur de ligne ne persistait pas d'une ligne à l'autre (retombait à 512 sans écriture), donc le verrou ne pouvait pas produire la dérive qu'il vise — corrigé (`glueLineLenFor`), 18/18 écritures freq attribuées comme Hatari (ligne et cycle), `TODO.md` § restes. Historique : `RestartVideoCounter` **PORTÉ** (2026-07-02,
  `restartVideoCounter`, événement `VC_RESTART`, check freq live). ⚠ **« CyclesPerVBL ±4 »
  RETIRÉ le 2026-08-25 : FAUX POSITIF prouvé** (cf. la ligne V3 du tableau des priorités —
  `CyclesPerVBL` n'est vivant qu'en mode VDI chez Hatari, la VBL normale vient de la chaîne
  `ShifterLines[].StartCycle`, portée par `lineCarry_`/`beginFrame_` actifs par défaut).
  ~~**Reste** : l'attribution de ligne à géométrie NOMINALE hors `NEOST_LINELEN` côté Shifter.~~
  ~~Canal **HYBRIDE**, moitié `Machine` ON et 4 sites `Shifter` OFF~~ — **périmé** : les deux
  verrous sont ON par défaut, `NEOST_LINELEN` (Machine, `Shifter.cpp:92-95`) et
  `NEOST_LINELEN_ATTR` (attribution Shifter, `ShifterInternal.hpp:31`, depuis le 2026-09-01).
  ⚠ **Réserve honnête** : le cas décisif — une trame où la **dernière** ligne (`lpf_-1`) change
  de fréquence — n'est établi que par **LECTURE** des gardes, jamais mesuré ; la ligne raccourcie
  effectivement observée est la 34. D'où une reformulation plutôt qu'une radiation.
- **[basse]** spec512 : pas de stagger +4 cyc des écritures `move.l`/`movem` au même cycle ni
  wrap de fin de ligne (`Spec512_StoreCyclePalette` `spec512.c:185-199`). *0 px de diff
  revendiqué sur le slideshow Spectrum 512.*
- ✅ **STE « left+16 px » (2026-10-18)** : latch 336 px de `Video_HorScroll_Write`
  porté dans `Shifter`, `VideoCounter` et la Glue ; cinq étalons oracle générés
  `scroll_336*`. L'état scroll/prefetch/latch de chaque ligne est mémorisé même avant
  la première écriture palette, afin qu'un re-rendu tardif ne modifie pas les lignes
  passées. Sauvegardes v20. Obsession et Pacemaker restent à vérifier sur leurs disques.
- ✅ **VBlank (2026-10-18)** : comparateurs 50/60 Hz et remise à ON au VSYNC
  portés ; lignes masquées à la palette 0 sans avancer la base vidéo. Deux étalons
  oracle `vblank_on/off` gardent le masquage et son évitement (640 → 0 px pour on).
  Le compteur intra-ligne conserve le comportement du calcul d’adresse Hatari.
- **[basse]** Datation des écritures couleur spec512 (offset calibré) ≠ datation freq/res (+16) —
  fonctionne par calibration séparée, fragile.
- ~~**[basse]** Bordures uniquement en basse résolution (overscan med-res non géré)~~ — **obsolète** : l'overscan med-res est porté depuis V2 (2026-07-08, `updateGlueRes`, `VideoGlue.cpp`).

### GEMDOS HD — autostart `--auto` (2026-10-24)

- Port d'`inffile.c` (`GemdosHd::setAutoStart/createInfOverride`) : INF virtuel servi au
  Fopen du desktop, cartouche système installée même sans dossier (`AUTOSTART_INTERCEPT`).
- **Défaut d'Hatari non reproduit** : `get_inf_file` garde `size = sizeof(INF intégré)`
  alors que `get_builtin_inf` vient d'y insérer la ligne `#W` du lecteur de boot ;
  `write_inf_file` (`size-offset-1`, inffile.c:998) tronque alors la fin de l'INF virtuel
  de la longueur de cette ligne. NeoST l'écrit en entier.
- **Écarts assumés** : nom de l'INF comparé sans lettre de lecteur (Hatari compare la
  chaîne exacte, alors qu'EmuTOS la préfixe du lecteur de boot, que NeoST ne force pas) ;
  pas de surcharge de résolution (`--tos-res`), la ligne `#E` est recopiée telle quelle.

### FDC WD1772 + DMA + STX — `Fdc.cpp`, `StxImage.cpp` ↔ `fdc.c`, `floppies/stx.c`

- **[assumée, 2026-10-22] IPF sans libcapsimage** (`IpfImage.cpp` ↔ `floppies/ipf.c`) :
  Hatari délègue l'IPF à libcapsimage (licence SPS non libre, chargée dynamiquement) et
  la lit par son moteur `_MFM`/DPLL. NeoST décode le conteneur lui-même (adaptation de
  MAME `ipf_dsk.cpp`, BSD-3), lit le flux de cellules UNE fois au montage avec un
  séparateur idéal (règles MAME `wd_fdc.cpp`) et monte le résultat sur le chemin STX.
  *Impact* : identique pour les protections « secteurs » (IDs non standard, CRC
  volontaires, chevauchements, pistes longues, densité variable, weak bits sur les
  données) ; divergent pour ce qui exige un DPLL vivant (perte de synchro aléatoire,
  IDs instables d'un tour à l'autre). Non comparable à l'oracle local (bâti sans
  capsimage).

- ✅ **STX WRITE SECTOR A8/A9 (2026-10-19)** : bit0 choisit FB normal
  ou F8 supprimé, statut RECTYPE et CRC du READ TRACK reconstruit cohérents.
  F8 persiste dans un supplément SECT `NDAM` + marque ; ancien SECT sans
  métadonnée = FB. Hatari lit les données mais ignore la métadonnée ; une
  réécriture par Hatari/ancien NeoST peut la perdre. Archive v45 inchangée,
  commande déjà sérialisée et contenu média hors snapshot (pas de rewind).
  CRC données : voir l’entrée du 2026-10-20 ci-dessous. READ TRACK : fusion locale des overlays
  sur image décodée dans le périmètre qualifié ci-dessous ; ST/MSA/DIM sans
  DAM persistante, pas de moteur MFM général ajouté.
- ✅ **READ TRACK et image décodée (2026-10-19)** : fusion locale des
  overlays SECT dans une copie du dump, y compris champs coupés par l’index.
  Parcours circulaire strictement inférieur à un tour, synchronisation zéro,
  ancrage ID aligné dans le dump, données non fuzzy et CRC concordants.
  Seuls DAM/données/CRC sont remplacés ; longueur, timings et octets extérieurs
  conservés. Collisions circulaires, ambiguïtés et champs incomplets refusés,
  de même que la traversée d’une fin artificielle de dump écrêté. Aucune
  reconstruction de secours ni mutation du dump source. Extension NeoST.
- ✅ **STX réécrit et F8 (2026-10-19)** : WRITE TRACK conserve la marque
  supprimée en RECTYPE lors de READ SECTOR et dans READ TRACK reconstruit.
  CRC régénérée sur la marque choisie ; snapshot et .wd1772 conservés.
  Extension de réinterprétation NeoST : Hatari READ SECTOR rend le flag,
  son READ TRACK reconstruit encore FB ; aucun flux physique exact revendiqué.

READ ADDRESS : CRC ID signalé après le sixième octet depuis le 2026-10-19,
selon WD1772/MAME ; Hatari partageait l’omission. CRC de données sans effet ici.

Machines à états type I-IV, bits de statut par type, timings (spin-up, head-load, index),
`NextSectorID`, FIFO 16 o, densité `$FF860E`, fuzzy/timing STX et persistance
`.wd1772` : **conformes dans le périmètre décrit** ; conteneur lisible par Hatari,
extension DAM NeoST qualifiée ci-dessus.

- ✅ **WRITE TRACK ST, emplacement des IDs (2026-10-19, révisé le 2026-10-22)** : un flux
  qui colle à la géométrie de l'image (C/H = tête physique et face, numéros 1..spt uniques,
  512 o) est écrit dans le .st. Un flux NON représentable n'est plus rejeté par LOST DATA :
  un WD1772 formate sans erreur (LOST DATA = DRQ non servie, `fdc1772.v:885`) et relit les
  IDs écrits. NeoST garde la piste EN MÉMOIRE (`FloppyDisk::stOverride`, volatile, ni dans
  le .st ni dans le save-state) et la relit selon ses IDs (READ SECTOR/ADDRESS/TRACK, WRITE
  SECTOR, recherche d'ID) ; un WRITE TRACK standard la rend au .st. Hatari refuse tout
  WRITE TRACK ST (LOST DATA, défaut). Corrige le test F de STE_Test v1.9 (secteurs 10-18
  sur la piste 79 : « Disk A: DS / Disk B: DS », Pass). Les IDs n'actionnent ni STEP ni SIDE.
  ✅ *Corrigé le 2026-10-24* : READ SECTOR, WRITE SECTOR et READ ADDRESS servaient le
  PREMIER champ ID de la liste portant le même numéro, et non celui que la recherche
  d'ID venait de trouver sous la tête. Sur une piste à IDs dupliqués (protection
  classique), deux lectures enchaînées rendaient donc deux fois les mêmes données. READ
  ADDRESS comparait aussi l'octet de taille brut à ses bits 0-1 : un ID de taille `$06`
  finissait en RNF. L'indice du champ trouvé est désormais retenu (`Fdc::ovSecIdx_`), et
  les positions angulaires comptent les `$F7` comme deux octets de CRC
  (`fdc_track_id_location_logic.cpp`).
- ✅ **Secteur long et sélection STX→ST (2026-10-19)** : WRITE SECTOR
  engagé sur un ID STX de 1 024 octets puis terminé sur un lecteur ST est
  limité à 512 octets dans l’image cible. Corrige l’écrasement du voisin ou
  le faux RNF à la fin de l’image ; DMA engagé inchangé. Référence Hatari
  `FDC_WriteSector_ST` / `Floppy_WriteSectors`, un secteur logique par appel.
- ✅ **Changements rapides de média (2026-10-19)** : première action puis
  dernière action opposée, répétition de la première annulant la seconde ;
  remplacement direct par éjection/insertion, comme `Floppy_DriveTransitionSetState`.
  Deux phases persistées en v41. Fenêtre historique de 18 durées de trame,
  pas quatre ; désormais 18 VBL réels par phase, même lors d’un changement
  PAL/NTSC/monochrome. Compteur et fins VBL sauvegardés en v43. Le compteur
  survit au reset FDC pour préserver le contrat existant ; aucune équivalence
  du reset global Hatari (Video_Reset remet nVBLs à zéro) revendiquée.
- **[D0 — moyenne] ✅ CORRIGÉ (2026-08-01)** — `.MSA`/`.DIM` étaient montées en **LECTURE
  SEULE**, et le bit **WPRT était présenté au programme émulé** (`writeProtect = !raw ||
  !writable`). Hatari ne dérive WPRT que du réglage et de `stat()` — **jamais du format
  d'image** (`floppy.c:205-225` `Floppy_IsWriteProtected`) : les écritures vont dans le
  tampon RAM et sont ré-encodées (`MSA_WriteDisk`, `DIM_WriteDisk`). Chez NeoST le
  drapeau ne bloquait pas que la recopie hôte, il pilotait le statut du WD1772
  (`updateWriteSectors`, `updateWriteTrack`, statut type I) : sur toute `.msa`/`.dim`,
  sauvegardes en jeu, high-scores, écritures depuis le bureau TOS et protections
  « écrit puis relit » échouaient « disque protégé », alors que la même disquette en
  `.st` fonctionnait.
  **Portage** : `encodeMsa` (port de `MSA_WriteDisk`/`MSA_FindRunOfBytes`, msa.c:275-420)
  et dispatch de `writeBack` par `FloppyDisk::imgFormat` — écriture partielle in situ pour
  le `.ST`, idem décalée de l'en-tête 32 o pour le `.DIM` (en-tête préservé, comme
  dim.c:134-149), ré-encodage complet et **atomique** (tmp + rename) pour le `.MSA`.
  `writeProtect` ne vient plus que de `stat()`. `!raw` ne subsiste que là où l'on ne SAIT
  PAS ré-encoder (STX, ou en-tête `.msa`/`.dim` reconnu mais indécodable) — y écrire
  détruirait le fichier. NeoST reste en **write-through** là où Hatari n'écrit qu'à
  l'éjection (une coupure y perd la sauvegarde).
  Couverture : `neost-headless --msa-selftest` (étalon `msa_selftest`, palier *fast*) —
  44 cas, aller-retour byte-exact sur 6 géométries × 7 motifs (dont `$E5` isolé à
  échapper, runs longs, incompressible qui force la piste brute) **plus** deux cas de bout
  en bout montage → écriture → remontage sur fichier réel `.msa` et `.dim`.
  ⚠ Cette ligne affirmait auparavant que `.MSA`/`.DIM` étaient « conformes (vérifiés
  ligne à ligne) » — c'était faux, et cela a masqué l'écart (audit du 2026-08-01).

- **[D1 — moyenne]** WRITE TRACK STX : NeoST parse le flux MFM écrit (`$FE`→ID, `$FB/$F8`→data)
  et le rend visible aux lectures, **régénère un CRC valide + `fdcStatus=0`** (`StxImage.cpp:293-344`, `reinterpretSaveTrack`).
  Hatari laisse un TODO (`pDataRead=NULL`, `stx.c:2027`) → lecture inchangée. *Impact :
  protections « formate + relit une piste corrompue » (Copylock/Rob Northen) « nettoyées » par
  NeoST ; mais utilisabilité accrue pour les formateurs ordinaires.*
- **[D2 — moyenne]** READ TRACK STX renvoie la piste réécrite (conséquence de D1, `Fdc.cpp:1829`, `readTrackStx`).
- ✅ **STX piste effacée (2026-10-19)** : un WRITE TRACK sans secteur
  exploitable remplace aussi la vue des secteurs par une vue vide. Les anciennes
  données ne réapparaissent plus, même après remontage du `.wd1772`. Choix
  D1/D2 conservés ; READ TRACK reste une reconstruction, sans promesse de flux brut exact.
- ✅ **D3 déjà porté** : stall 32 cycles bus aux flushs FIFO, échéance suivante
  `due + stall + delay`. Revérifié le 2026-10-18 ; voir le bilan D3 en tête.
- **[D4 — faux positif, revérifié le 2026-10-18]** La piste standard de repli
  vaut **6268** dans les deux émulateurs ; la formule 6250 est commentée chez Hatari.
- ✅ **Registres DMA (2026-10-18)** : le reset FIFO par bascule du bit 8 conserve
  le statut d’erreur et annule les octets restant à transférer. Seul le reset
  matériel, chaud comme froid, efface l’erreur (Hatari FDC_Reset/FDC_ResetDMA).
  Les écritures $FF860B/0D reproduisent la retenue des bits 7/15 sur ST et Mega ST,
  puis appliquent le masque d’adresse ; STE/Mega STE restent sans cette retenue.
  Garde publique MMIO : `tests/fdc_register_logic.cpp`.
- **[basse]** Densité **HD/ED** STX : NeoST recadre `BitPosition` sur la densité (`Fdc.cpp:913-915` `densityFactor`, recadrage `:1538-1544`)
  → **plus cohérent que Hatari** (qui a une incohérence HD/ED) ; **identique en DD**.
  Étendu le 2026-10-20 à la **durée de lecture** : la valeur standard d'un secteur
  (`ReadTime` nul) vaut 32 µs/octet divisé par `densityFactor()` (16 µs en HD, 8 en ED),
  cohérente avec `nextSectorIDStx`. Hatari (`floppies/stx.c`) garde la constante DD : un
  secteur HD y dure deux secteurs et le suivant d'un multi-secteurs coûte un tour.
- **[assumée, 2026-10-20] WPRT après la montée en régime** : WRITE SECTOR teste la protection
  dans `RUN_WS_MOTORON`, après le spin-up (6 index si h=0) et le délai E, avant la recherche
  d'ID, et la reteste à chaque secteur d'un multi-secteurs ; WRITE TRACK la teste dans
  `RUN_WT_MOTORON`, avant l'attente de l'index. Preuve : organigrammes type II/III du WD177x,
  MAME `wd_fdc.cpp` (`SETTLE_DONE` → `wpt_r`). Hatari (`fdc.c`) faute WRITE SECTOR dès le
  premier événement, avant le moteur, et WRITE TRACK après l'index (jusqu'à un tour trop
  tard). Test : `tests/fdc_media_change_logic.cpp`.
- **[assumée, 2026-10-20] CRC données STX réinscriptible** : un WRITE SECTOR sur un secteur
  à erreur CRC du champ DONNÉES (statut Pasti CRC sans RNF) écrit un champ données neuf au
  CRC juste ; la relecture (secteur et dump READ TRACK) ne signale plus de CRC. Le WD1772 ne
  contrôle que le CRC d'ID (MAME `wd_fdc.cpp` `write_sector`). Hatari refuse ce cas
  (`stx.c:1735`, contre son propre commentaire « ID field CRC » de la ligne 1704) et sa
  machine à états, qui ne teste que RNF, annonce un SUCCÈS sans rien écrire. Un CRC d'ID
  faux (codé RNF+CRC par Pasti) reste refusé. Test : `tests/stx_data_crc_write_logic.cpp`.
- **[assumée, 2026-10-20] Compteur de secteurs DMA appliqué à l'ACSI** : le transfert est
  borné à count×512 octets, rien ne part à compte nul (les données restent en attente
  jusqu'à un compte programmé et un retour en mode DMA), et le compteur décompte par bloc de
  512 o (`$8606` bit 1 = 0 après un transfert complet ; une queue < 512 o le laisse). C'est
  la même puce DMA que pour la disquette ; le diagnostic Field Service vérifie ce bit.
  Hatari (`hdc.c`, `Acsi_DmaTransfer`) ignore le compteur. Test : `tests/fdc_acsi_logic.cpp`.
- **[basse] ✅ vérifié SANS OOB (7ᵉ passe, 2026-08-06)** — Bornes parseur STX (`StxImage.cpp:73-242`, `parse`) :
  rejette une image tronquée que Hatari monterait ; l'**OOB « latent »** annoncé sur le chemin
  « simple » tronqué est FERMÉ. Une piste simple tronquée pose `sectorsCount=0`/`sectors` vide
  (le contrôle de taille `dataBytes` échoue avant `buildSectorsSimple`) ; les 4 consommateurs bornent sur
  `sectorsCountView()` (RNF après 5 tours). Compléments : clamp `TrackImageSize` (commit `7fe14bb`)
  + borne save-state `stxNextSector_` 0..255. Aucune image forgée ne provoque de lecture hors bornes.
- **[basse]** `pData==NULL` sans RNF → secteur lu 512×`0x00` + statut OK (image de bord, `Fdc.cpp:1596` : `sec.pData ? … : 0`).
- **[basse] ✅ vérifié SANS bug (2026-07-09)** — INTRQ : **pas de double déclenchement**. Le
  `raise(SRC_FDC)` doublé a été retiré à la refonte M1 (`bc15a67`) ; entrée périmée (les anciennes
  lignes 638-645 = aujourd'hui `applyFastFdc`). Chemin unique : `fdcSetIrq` (edge-gardé `if(!was)`)
  → `setIntrqLine` → `Mfp::setFdcLine` → `gpipSetLine` (`if(line==active) return` + front AER,
  `Mfp.hpp:555-560`). Une INTRQ maintenue ne regénère aucune IRQ ; aucun `raise` manuel ni chemin
  contournant `gpipSetLine` (grep arbre entier).
- **[basse]** Signal DC (Disk Change)→GPIP4 (TT-only chez Hatari) non émulé ; modèle WPRT distinct.
- **[basse]** Hot-swap : 1 phase (eject) vs 2 (eject+insert) chez Hatari (`floppy.c:438-514`).

### Son (YM2149 + DMA STE) — `YM2149.cpp`, `DmaSound.cpp` ↔ `psg.c`, `dmaSnd.c`, `sound.c`

- ✅ **Microwire DATA octet (2026-10-19)** : fusion avec la valeur visible
  après décalage, sans réutiliser l’autre octet d’une ancienne commande.
  Déclenchements high/low isolés, staging mot/long, rejet occupé et état v45
  conservés ; commande LMC effective vérifiée avec et sans Scheduler.

- ✅ **Retard Microwire (2026-10-19)** : les décalages dus sont rattrapés
  sur la grille de huit cycles, au plus seize fronts, selon le principe
  PendingCyclesOver de Hatari. Data/Mask sont actualisés avant MMIO ; nouveau
  départ à liveNow+8. Busy, mots haut/bas et décodage unique final préservés,
  sans dispatch général ni nouveau champ. Les anciennes mentions de signe
  LMC manquant, shelves RBJ et HPF YM seul sont périmées dans le code actuel.
Moteur 250 kHz, table DAC 32³, ET logique ton/bruit, LFSR, enveloppe, demi-amplitude STE,
PWM+HPF, FIR anti-repliement DMA, Microwire/LMC (gains/panoramique), XSINT→GPIP7+Timer A,
cold/warm reset : **conformes** (l'ancien doc `SOUND_HATARI_DIFF.md`, périmé, a été **supprimé**).

- **[S1 — moyenne] ✅ corrigé** — `setStfLowPass(!STE)` est désormais appelé (`Machine.cpp`/`Machine.hpp`)
  → ST/Mega ST utilisent `applyLpfStf250`. *Avant :* filtre câblé mais `setStfLowPass()` défini mais
  **jamais appelé** (`YM2149.hpp:186`) → STF/Mega ST en PWM au lieu de `LPF_STF`
  (`sound.c:1946-1951`). Code `applyLpfStf250` mort. *Impact : timbre YM plus dur sur ST.*
- **[S2 — moyenne] ✅ corrigé (2026-07-07 soir)** — FIFO 8 octets + capture au faisceau portées
  (cf. tableau des priorités et 5ᵉ passe). *Avant :* lecture RAM directe à la cadence DMA en fin
  de trame, pas de réalignement mono→stéréo. *Impact : Mental Hangover, Power Up Plus.*
- ~~**[basse]** Fréquences de coupure du correcteur LMC1992 : RBJ 2e ordre 200/8000 Hz
  vs Savinkoff 1er ordre 118.28/8438.76 Hz (`dmaSnd.c:1418`)~~ **✅ corrigé (10ᵉ passe,
  2026-08-06)** : port exact des plateaux 1er ordre, coupures 118.2763/8438.756 Hz.
- ✅ **Chemin mono DMA revérifié (2026-10-18)** : la queue après STOP était déjà
  drainée ; les frontends, y compris WASM, utilisent le mixage stéréo. L’ancienne
  API mono perdait toutefois les demi-échantillons lors de la moyenne L/R entière.
  Moyenne flottante corrigée, sans changer les filtres ni le timing du FIFO.
- **[basse, méthodo] ✅ levé** — le headless dump l'audio : `--sound-dump F` écrit un WAV 48 kHz
  stéréo s16 de la MÊME chaîne que le GUI (YM horodaté + DMA STE + LMC), donc A/B contre un WAV
  Hatari ou entre deux configs. Traces registre : `NEOST_DMASND_TRACE=1` (format « DMA snd fifo
  refill » d'Hatari). *Avant :* aucun son en headless, donc aucun oracle audio.

### FDC — phase INDEX des médias (actualisé le 2026-10-18)

- ✅ Éjecter le lecteur actif coupe immédiatement son INDEX ; insérer un
  nouveau média moteur lancé amorce une nouvelle phase. Le lecteur non
  sélectionné ne change pas la phase active. Références : Hatari
  `FDC_EjectFloppy/FDC_InsertFloppy`. FORCE INTERRUPT et IRQ forcée étaient
  déjà conformes ; les bits ready ne correspondent pas à une entrée WD1772
  câblée sur le ST et ne produisent pas de transition dans Hatari.
- ✅ **FDC sélection live et secteurs (2026-10-18)** : les primitives ST/MSA/DIM
  vérifient secteur non nul et ≤ secteurs/piste sur le lecteur réellement actif.
  Un changement de lecteur entre ID et données ou pendant DMA renvoie RNF si
  le secteur manque, sans alias vers la face/piste suivante. Références :
  Hatari `Floppy_ReadSectors/Floppy_WriteSectors`. STX conserve sa délégation.

### GEMDOS HD — sauvegardes (2026-10-19)

- **A13 clos** : contexte invité sérialisé en v48 (handles, position, mode,
  basepage, Fforce, chemins, caches DTA et Pexec), selon la surface de
  `GemDOS_MemorySnapShot_Capture`. Politique plus stricte qu'Hatari : fichier
  ouvert absent/modifié (taille/horodatage) ou inaccessible → refus du load
  complet, plutôt qu'avertissement ou handle perdu. Tous les descripteurs sont
  préparés avant remplacement de la session ; `wb+` n'est jamais rejoué.
  Racines identiques et confinement des chemins vérifiés au chargement.
  Les données des fichiers hôtes ne sont pas rembobinées.

### GEMDOS HD — codes d'erreur (audit 2026-10-20)

Confrontation appel par appel avec `gemdos.c` : valeurs des codes, table
errno → GEMDOS et replis sur le TOS sont un portage 1:1. Écarts :

- ✅ **Corrigé** : un tampon Fwrite ou un nom de fichier dans le port cartouche
  (`$FA0000-$FBFFFF`) était refusé (ERANGE, ou repli TOS). Hatari l'accepte
  (banque ABFLAG_ROM) et le TOS lit la mémoire sans contrôle. `GemdosHd::checkArea`
  l'admet en lecture, jamais en écriture ; quatre contrôles dans `--gemdos-selftest`.
- **NeoST plus sûr qu'Hatari, à conserver** : Fwrite de taille négative → ERANGE
  (Hatari : `STmem_check` déborde en uint32, `fwrite` d'environ 4 Go, cpu/memory.c:604) ;
  Pexec d'un PRG plus court que son en-tête annonce, ou d'une table de relocation à
  3 octets de la fin → EPLFMT (Hatari lit hors tampon, gemdos.c:4422 et :4479) ;
  effacement du tas avec `p_hitpa` < fin du BSS borné (Hatari boucle ~4 Go,
  :4466) ; somme text+data+BSS en 64 bits (Hatari : 32 bits, :4410).
- **Dsetpath sur un chemin terminé par un masque** (`C:\DIR\*.*`) : NeoST retire le
  masque et rend EOK, Hatari rend EPTHNF. Comportement du bureau TOS 1.62 selon le
  commentaire de NeoST, jamais relevé sur matériel.
- **Écarts communs aux deux émulateurs face au TOS** (non corrigés, à mesurer) :
  Fdelete/Fopen sur un dossier (EACCDN ou ERROR selon l'hôte, TOS probablement
  EFILNF), Frename entre systèmes de fichiers hôtes (EXDEV → ERROR, TOS : ENSAME −48),
  Fwrite sur un fichier ouvert en lecture (refus à l'écriture, TOS : à l'ouverture),
  contrôles ERANGE/EINTRN des tampons propres aux émulateurs.

### ACIA 6850 / IKBD HD6301 / MIDI — `Ikbd.cpp`, `MidiAcia.cpp` ↔ `acia.c`, `ikbd.c`

- ✅ **Empreinte des programmes 6301 custom (corrigé le 2026-10-20)** : NeoST calculait
  un vrai CRC32 ; la fonction d'Hatari (`utils.c:crc32_add_byte`) teste
  `(c & 0x80) ^ (crc & 0x80000000)`, vrai dès que l'un des deux bits est levé. Les
  valeurs de la table venant d'Hatari, AUCUN programme réel n'était reconnu (Froggies,
  Transbeauce 2, Dragonnels, Chaos A.D., Audio Sculpture) ; seuls les tests à charge
  synthétique forgée passaient. Ce n'est pas du matériel émulé mais une clé
  d'identification : la fonction d'Hatari est reprise à l'identique, vérifiée contre
  son `utils.c` compilé.

- ✅ **Datation IRQ clavier (2026-10-19)** : front GPIP4 daté au transfert
  TDR→TSR ou au stop RX, au lieu du dispatch tardif. Le rattrapage avant
  écriture CR/TDR publie le front au START même s’il dépasse ensuite le stop.
  Propagation MFP de quatre cycles depuis ce front ; AER/DDR et OU câblé
  avec le MIDI préservés. Accès CR/RDR datés de l’accès courant. Banc dédié :
  77 contrôles, dix échecs avant correction, zéro après sous ASan/UBSan.
  Aucun nouveau format série ni changement de sauvegarde.

**Très complet** : modèle TIE, master reset 6850, overrun/cadence série, fenêtre critique de
reset, buffer borné 1024, pause `$13`, commandes usuelles et rapports `$07-$9A`, les 6 handlers 6301
custom (Froggies, Transbeauce, Dragonnels, Chaos, Audio Sculpture), quirks reset, duplication
feu/boutons : **conformes** (l'ancien doc `IKBD_HATARI_DIFF.md`, périmé, a été **supprimé**).

- **Limites confirmées (2026-10-18)** : `$18` (monitoring du feu) et `$19`
  (keycode joystick) sont reconnus mais non émulés, comme dans Hatari. `$19`
  consomme sept octets en tout ; `$99` est seulement un alias du rapport
  `$94/$95`. Cet alias n’implique aucun support du mode keycode.
- ✅ **Délais des réponses et Froggies portés (2026-10-18)** : attente firmware
  globale quantifiée en bits, valeurs centrales déterministes des plages Hatari.
  La phase SCI sortante sous-octet reste approximative.
- ✅ **Transit CPU→IKBD (2026-10-18)** : un TDR et un TSR, TDRE au
  chargement TSR sur la grille 1024 cycles, réception de la commande au stop.
  Une écriture CPU prématurée remplace le TDR ; TIE ne change que l’IRQ.
  Resets et phase sérialisés. Modèle borné au clavier /64 8N1 : formats,
  autres diviseurs restent ouverts. Le break suspend les nouveaux départs,
  sans simuler la ligne physique basse ni les erreurs SCI associées.
- ✅ **Autosend normal temporisé (2026-10-18)** : timer libre de 150000 cycles,
  ancré à son échéance précédente, préservé lors des resets rapprochés.
  Le VBL avance seulement l’horloge et la garde des vingt premières trames.
  **Monitoring $17 porté le 2026-10-18** : taux 0 ramené à 1, période en centièmes
  de seconde sur la base fixe 8021247 d’Hatari, phase sauvegardée.
  $17 remplace la cadence commune jusqu’au boot ROM, même après changement de
  mode ; MouseAction et mise à jour absolue précèdent le paquet monitoring, comme
  `IKBD_SendAutoKeyboardCommands` (ikbd.c:1688-1696). Aucun second timer de
  scrutation ne reste actif. Boot ROM conserve l’échéance et restaure 150000.
- ✅ **Break émission clavier (2026-10-18)** : CR bits5–6=11 empêchent
  le départ d’un nouveau TSR, sans tronquer celui déjà engagé. Le TDR reste
  plein et repart au prochain front après libération. Ligne physique basse et
  récupération des erreurs SCI restent hors modèle ; formats/diviseurs inchangés.
- ✅ **Date du front IRQ MIDI (2026-10-19)** : RX hôte cadencé,
  transfert TDR→TSR et retour au stop de bouclage transmettent l’échéance
  série au détecteur GPIP4, au lieu de l’heure tardive du callback. Même
  qualification AER/DDR et ligne partagée clavier ; délai MFP quatre cycles
  conservé. CR/RDR et autres lignes restent à leur date d’accès actuelle.
  Aucun nouveau champ ni changement de format v44.
- ✅ **Phase MIDI RX (2026-10-18)** : initialisation sur `liveNow`, puis
  échéance précédente +2560 cycles ; resets et remplacement de source gardent
  une échéance active. Le flux ne dérive plus aux callbacks tardifs et les resets
  rapprochés ne peuvent pas affamer l’entrée. Phase portée par le Scheduler.
- ✅ **Émission MIDI TDR/TSR (2026-10-18)** : TDRE au chargement du TSR,
  puits et bouclage au stop, attente écrasable et horodatage à l’échéance série.
  Le calcul du débit suit `midi.c` ; le transfert suit `acia.c::ACIA_Clock_TX`.
  **Différence volontaire** : Hatari `midi.c:277` écrit encore au backend dès
  l’accès CPU ; NeoST attend la fin du caractère. Réception MIDI désormais sur un seul RDR avec overrun différé.
  TX respecte les huit formats CR2..4 et /1, /16, /64, avec masquage du
  caractère à 7/8 bits et durée fixée au chargement TSR. Source RX hôte
  encore à 31250 bauds 8N1 ; aucune erreur physique synthétisée. BREAK retient
  le TDR, laisse finir le TSR et reprend au front du diviseur sélectionné.
- **[basse]** `IKBD_CheckForDoubleClicks` et joystick→barre d'espace absents (confort fast-forward, non matériel).
- ✅ **MIDI RX, registre unique (2026-10-18)** : RDRF protège un seul caractère
  achevé ; les suivants sont perdus et posent RX_Overrun. OVRN apparaît lors de
  la lecture RDR puis s’acquitte par SR→RDR, comme le cœur ACIA Hatari.
  L’ancien commentaire qui assimilait RDR+RSR à deux caractères achevés était faux.
  L’IRQ suit RDRF et l’overrun pendant, en OU câblé avec le clavier. Master reset
  efface statut et erreurs sans effacer la valeur persistante RDR. Le module MIDI
  simplifié d’Hatari ne modélise pas tout ce statut ; la référence est `acia.c`.
- ✅ **Capacité sortie IKBD (2026-10-18)** : les 1024 octets en attente
  ne comptent ni le SCI engagé ni le RDR ACIA, situé dans une autre puce.
  Références : ikbd.h:44-45, `IKBD_OutputBuffer_CheckFreeCount` et
  `IKBD_Check_New_TDR`. Admission des paquets entiers préservée ; overrun
  toujours traité en aval. Archives dépassant 1024 octets refusées.
- ✅ **Initialisation ACIA clavier (2026-10-19)** : sorties firmware rejetées
  avant initialisation du diviseur, comme IKBD_Send_Byte_Delay ; aucun octet
  ni délai ne ressuscite ensuite. Latch conservé aux resets et sauvegardé v46.
  CR00 identique initial ne l’arme pas ; reset03 puis CR00 l’arme.
- ✅ **Registres ACIA au reset matériel (2026-10-19)** : CR/SR/RDR, erreurs,
  TDR/TSR et causes IRQ conservés, comme ACIA_Reset sans broche de reset.
  SCI sortant annulé séparément par IKBD_Reset, puis boot ROM ; ligne IRQ
  republiée après reset du MFP. Ancienne remise à zéro CR/SR corrigée.
- ✅ **Transit CPU→SCI au reset matériel (2026-10-19)** : progression des
  bits ACIA conservée, SCI remis en attente de start et horloge réancrée.
  Tout zéro ultérieur peut réamorcer la réception ; données, stop, bits de repos
  et caractère suivant participent au décodage, selon les états Hatari.
  Port borné au /64 8N1 ; autres formats/diviseurs physiques différés.
- ✅ **PAUSE IKBD $13 (2026-10-18)** : seul le prochain départ SCI est
  bloqué ; l’octet engagé finit à son échéance. Il est distinct de la file
  firmware et sauvegardé en v32. Boot ROM conserve cet octet, reset matériel
  l’annule ; master reset ACIA laisse le SCI émettre. Références :
  `IKBD_SCI_Set_Line_TX` (ikbd.c:835-866), `IKBD_Check_New_TDR` (:917-929).
  L’ancienne mention « deux octets » était fausse : Hatari ne charge TDR qu’en
  SCI IDLE et le copie immédiatement dans TSR, sans précharger le suivant.
- **[basse, assumée — NeoST plus robuste]** Monitoring `$17` : paquet « entier ou rien » ; Hatari est le SEUL émetteur sans `CheckFreeCount` et peut déchirer un paquet à 1 octet libre (désynchronise le flux, ikbd.c:1484). Observable seulement avec un tampon plein (21 octets, ROM).
- **[basse, assumée]** Horloge IKBD amorcée sur l'heure hôte à la construction (Hatari l'efface au cold boot, ikbd.c:539) — confort bureau ; `--keys`/`setClock` couvrent le déterminisme headless. Idem : état des handlers 6301 custom ré-initialisé à chaque reconnaissance (les `static` d'Hatari casseraient un 2ᵉ lancement).
- ⚠ **IKBD calé sur la ROM 6301 désassemblée (2026-10-22) — écarts assumés avec Hatari,
  alignés sur le firmware réel** (MiSTery `ikbd/rom/IKBD.ASM` ; révision tardive du clavier,
  dont la logique des commandes ne dépend pas a priori). `Ikbd.cpp` :
  - **Octets de mode** : `$C9` = souris activée (bit 7, `mouseOn_`), port 0 attribué au
    joystick (bit 1, `port0Joy_`, posé par `$14/$15/$17/$18/$19` via `$FA47`, effacé par
    toute commande souris `$FBAF`), mode souris (bits 6-5) ; `$CA` = mode joystick et
    activation (bit 5, `joysticksEnabled_`). `$92` rapporte le seul bit 7 ; `$1A` n'efface
    que le bit 5 (`$94` répond toujours `$14`) ; une commande souris arrête `$17/$18/$19`.
  - **Fenêtre de reset** ($F010 sei … $F139 cli) : un seul octet reçu pendant l'init attend
    dans le RDR et s'exécute après ; un second déborde et tout est perdu ($FF40). Remplace
    les quatre quirks d'Hatari (Barbarian `$08+$14`, Hammerfist `$12+$14`, `$12+$1A`,
    `$13` ignoré pour Just Buggin') : il ne reste que les défauts (souris relative +
    joystick 1 en événement ; jamais le joystick 0). Vérifié : Just Buggin' envoie
    `$80 $01 $11 $13` d'un trait, `$11` est retenu puis perdu avec `$13` ; Barbarian
    (Psygnosis) envoie `$80 $01 $08 $0B 03 03 $14`, tout est perdu, souris et joystick 1
    répondent ensemble (cf. `CASE_STUDIES.md`).
  - **Tampon de sortie de 21 octets** ($D9-$ED ; Hatari 1024) ; les réponses d'état sont
    admises appel par appel ($FCD7) et peuvent être tronquées.
  - **PAUSE `$13`** ($F9AF) : les octets déjà en file sortent encore ; seules les commandes
    qui sortent par $F91B lèvent la pause (pas les rapports d'état, ni `$0D` hors mode
    absolu, `$16` refusé, `$80 xx`) ; rapports d'état ignorés pendant la pause et pendant
    `$17/$18` ; décompte de `$17` gelé ($FEAC).
  - **Souris absolue** : `$09` ne remet PAS la position à zéro ($FACB — Hatari avait raison,
    la doc du protocole a tort) ; position mise à jour en mode absolu seulement ; `$0D`
    ($FB39) ne répond qu'en mode absolu, avec les événements boutons CUMULÉS ($C2) qu'il
    efface ; `$07` bits 0-1 envoient les événements du passage ; `$0E` ($FB5F) passe en mode
    absolu ; échelle par défaut 1, 0 fige l'axe ; bornes 0/0 après boot (RAM effacée).
  - **`$07` bit 2** en relatif : un changement de bouton seul ne produit plus de `$F8`.
  - **`$16`** refusé si joysticks désactivés ou hors modes événement/interrogation ; joystick 0
    lu à 0 si le port 0 sert la souris.
  - **Émission « dès que la ligne est libre »** ($F54A) : paquets `$F8` envoyés quand la
    ligne se libère et que le cumul atteint le seuil, ou sur changement de bouton, ou au-delà
    de ±127 ; le cumul sous le seuil est gardé (Hatari scrute toutes les 150 000 cycles et
    abandonne le reliquat). Même règle pour les flèches `$0A` (une paire par axe, cumul
    effacé) et pour `$17` à taux 0.
  - **`$18`** ($F8A2) : un échantillon du feu joystick 1 par tour de boucle, 177 µs enfoncé
    / 180 µs relâché, +15 µs au 8ᵉ (octet écrit dans le TDR), attente 57 µs ligne occupée et
    34 µs sous pause — cycles HD6301 comptés sur la ROM, hors interruptions du timer ;
    clavier non scruté. **`$19`** (joystick 0 en flèches, RX/TX/VX en dixièmes de seconde,
    feu `$74/$F4`) scruté toutes les 1 ms ; `$94` rapporte `$19` + paramètres.
  - **Valeurs extrêmes** : taux `$17` à 0, seuil `$0B` à 0 et pas `$0A` à 0 = flux continus
    au débit de la ligne ; mois 0 = horloge figée ; `$1B` recharge le compteur de ms.
  Conservés (la ROM les confirme) : `$88` complété à 8 octets avec le dernier mode choisi,
  `$9A` piloté par `$1A` seul.
  Même lot : `Blitter::reset` relâche GPU_DONE (GPIP3), donc pas de blit fantôme après un
  reset à chaud en plein blit. Le bouchon RS-232 (`Mfp::loopback_`) n'est plus dans l'état
  depuis la v51 ; c'est un réglage du frontal, comme les bouchons MIDI et SCC.

### Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`

- ⚠ **Décodage du STE d'après la netlist du GST MCU (2026-10-22) — écarts assumés avec Hatari,
  alignés sur le matériel.** Ne pas réaligner sur Hatari : la netlist est le câblage du circuit.
  Sources : netlist texte Atari du 4081 (archive ASIC de Christian Zietz,
  chzsoft.de/asic-web, `ASIC/4081_COMBO/4081/PIN/4081.PIN` et `REGISTER.PIN`), sa
  transcription `gstmcu.v` (cœurs MiSTer/MiSTery). Le MCU ne donne DTACK, en **lecture**
  seulement, qu'à $E00000-$EBFFFF (/ROM2-/ROM0), $D00000-$D7FFFF (/ROM6, /ROM5),
  $FE0000-$FE1FFF (/ROMP) et au port cartouche (4081P2, porte AF8 `ROMXB`) ; le reste
  time-oute (`berr_cnt`, 64 cycles 8 MHz). NeoST, **STE seulement** (`Bus::romWindowSize`,
  `steRomSelectVoid`, `steCartWindow`, `mmuBank0Size`, `mmioRead8`) :
  - $EC0000-$EFFFFF faute (Hatari : fenêtre ROM de 1 Mo, lit 0) ;
  - /ROM5, /ROM6 et /ROMP lisent le bus ouvert (Hatari : `BusErrMem_bank`) ; le
    « `irom6 | irom6` » de `gstmcu.v` (sans /ROM5) est une coquille de transcription,
    présente depuis son premier commit ;
  - registre « gamecart » `$FF9000` (4081P10 : bascule `GAMECART`, D = ID8, effacée par
    RESB/PORB) : bit 0 de l'octet pair relu (bit 1 à 0, bits 2-7 flottants rendus à 1) ; à 1,
    /ROM4 et /ROM3 passent en $D80000-$DBFFFF et $DC0000-$DFFFFF (fenêtres de 64 Ko répétées)
    et $FA0000-$FBFFFF faute. Les extensions NeoST du port cartouche (NE2000, ISP1160, clés)
    ne suivent pas le déménagement, et la cartouche système du HD GEMDOS (absente d'un vrai
    STE) ne déménage pas du tout : son trap #1 reste en $FA002A (`steCartRelocated`) ;
  - `$FF8001` relu sur 4 bits (`REGISTER.PIN` : latches ID0-3, IDB4-7 à VSS) ; banque 0
    configurée « 11 » = 2 Mo (4081P18 : RS3 = 1 prend le chemin 2 Mo quel que soit RS2).
    Confirme aussi le modèle Hatari « banque 1 calquée sur la banque 0 » : RS0/RS1 ne
    pilotent rien.
  - pads `$FF9200-01`, `$FF9210-17`, `$FF9220-23` : acquittés en LECTURE seule
    (BUTR/PADR/PENR contiennent IRWZ) → toute écriture fait bus error (`Bus::wrStePads`) ;
  - sélection des pads : bascule JOYWE (gstmcu.v:352-360) mise à 1 au reset et par toute
    LECTURE de l'octet bas `$FF9203`, remise à 0 par son ÉCRITURE ; inactive, aucune
    rangée n'est sélectionnée et tout se lit au repos (`StePads::sel`). Hatari garde la
    sélection écrite indéfiniment ;
  - chien de garde `/BERR` (4081P11, compteur 7 bits à 8 MHz remis à zéro hors /AS) : une
    bus error arrive ~64 cycles après /AS, soit 60 cycles d'attente en plus d'un accès
    normal (`kBerrTimeoutWait`, Cpu68k.cpp). Hatari n'en compte aucun ;
  - son DMA : `$FF8901` relu sur 4 bits (REGISTER.PIN) ; fin `$FF890F-13` relue via l'étage
    SFT, figé à chaque début de trame pendant la lecture (4081P14, `DmaSound::read8`). Mega
    STE inchangé (`DmaSound::steNetlist`, posé par le Bus).
  **Confirmé fidèle** : $FC0000-$FEFFFF faute avec un TOS en $E00000 (`memory.c:1784`), cf.
  cas IK+ dans `CASE_STUDIES.md`. **Non touché** : Mega STE (aucune netlist de son
  décodage). Garde : `--bus-selftest` sur STE (24 contrôles).

- ✅ **API Bus, miroir blitter atomique (2026-10-19)** : adresse masquée
  sur 24 bits avant les raccourcis write16/write32, comme pour les octets.
  Les miroirs gardent l’écriture contrôle/skew atomique, la largeur MMIO
  est restaurée et le rebouclage en RAM conservé. Défaut de l’API Bus directe
  et des outils ; Moira normalise déjà ses cycles CPU, aucune nouvelle
  régression CPU68000 corrigée revendiquée.
MMU/banques ST vs STE (`ConfToBank` : `bank1=bank0` sur STE), aliasing RAS/CAS, whitelist
bus-error par modèle (octet par octet, patches MegaST/MegaSTE, SCU octets impairs), fenêtres
ROM, accès DMA (retour 0/écriture ignorée en zone fautive), protection superviseur `<0x800`,
règle word/long : **conformes**.

- ✅ **RAM 2176/2560 Ko (banques mixtes, 2026-10-20)** — `ramBanks` (`Bus.cpp`) découpe
  désormais 2048+128 et 2048+512 Ko comme `STMemory_RAM_SetBankSize` ; `memConfigForBytes`
  pose `$FF8001` = $08/$09 comme Hatari ; tailles `2176k` et `2560k` (alias `2.5m`) acceptées
  par `--mem`, `neost.cfg`, la fenêtre de config, le shell web et le contrôle de save-state.
  phystop = Hatari : ST/Mega ST sous TOS 1.02/1.04/EmuTOS → $220000/$280000 ; STE/Mega STE
  sous TOS 2.06 → $400000 (banque 1 calquée sur la banque 0, `ConfToBank`) ; EmuTOS 256 →
  taille exacte (sondage par banque). Modèle Hatari **conservé** sur STE car seul cohérent
  avec le matériel : FAQ comp.sys.atari.st §3.1.1 (« STEs will not recognize a 2.5Mb
  configuration without silkboot2e or simmfix ») et l'existence de SIMMFIX2 ; MAME
  (`stmmu.cpp`, banques indépendantes) contredit ces témoignages. ⚠ À confirmer sur vrai STE :
  lecture `$FF8001` bits 0-1, décodage d'une banque 1 de 128 Ko (cf. `TODO.md`).

- **[BU1 — moyenne] ✅ corrigé** — Miroir matériel du PSG `$FF8804-$FF88FF` désormais routé vers
  le YM2149 (`Bus.cpp` : plage étendue à `+0x100`, décodage `addr&3`, wait-states inclus), comme
  le shadow Hatari `ioMem.c:386-393`.
- **[basse]** `$FF8264` STE (scroll fin) lu comme void `0xFF` au lieu du registre réel
  (`Video_HorScroll_Read_8264` `ioMemTabSTE.c:122`).
- **[basse]** Distinction `IoMem_VoidRead`(0xFF)/`_00`(0x00) gérée seulement pour les registres
  vidéo STE (dans le Shifter), pas générale.
- **[basse]** Zone cartouche vide `$FA0000-$FBFFFF` lit `0xFF` au lieu du tampon ROM (sans impact : le magic ne matche pas).
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — Write-protect `$0-$7` pour les accès
  **non-CPU** : `Blitter::writeWord` ignore désormais `addr < 0x8` (Hatari `SysMem_wput`,
  `memory.c:758`). *Avant :* un blit ciblant la table des vecteurs corrompait la RAM.
- **[basse]** `read32` en zone RAM void assemblé octet par octet vs `(db<<16)|db` — équivalent en accès aligné.
- *Note :* la zone RAM vide renvoie `0x00` (NeoST) ; Hatari renvoie la dernière valeur du bus
  (`regs.db`, `VoidMem`). Différence théorique, sans impact connu.

### Blitter — `Blitter.cpp` ↔ `blitter.c`

- ⚠ **Registres du blitter d'après la netlist Atari 4051/4082 (2026-10-22) — écarts assumés
  avec Hatari.** Registre mot chargé sur écriture mot seule (strobe WW = UDS & LDS), chaque
  mot indépendamment (adresses : deux mots) ; lecture octet du registre VIVANT — le tampon
  IoMem d'Hatari (octets rejetés relus ou recombinés) n'existe pas dans le circuit
  (`Blitter::writeRegisters`, `read8`). `$FF8A3D` : bits 4-5 sans verrou, relus 0 (masque
  `0xCF`). Reset à chaud : seuls BUSY et HOG ont une remise à zéro (`Blitter::reset`).
- ⚠ **Ligne INT (GPIP3) = BUSY, d'après le RTL de J. Cwik (2026-10-22), confirmé par la
  netlist 4051/4082 — écart assumé avec Hatari, aligné sur le matériel.**
  MiSTery `stBlitter.sv:185-190` : `INTn = rBusy & !ctrlWr`, câblée sans inversion sur GPIP3
  (`atarist_sdram.sv:623`). NeoST (`Blitter::controlWritten`, `start`, `pauseTransfer`) : la
  ligne retombe à la pause (BUSY écrit à 0) et remonte à la reprise ; toute écriture CPU de
  l'octet `$FF8A3C` pendant un blit la fait passer 1→0→1 (relance « `bset #7` » du non-hog),
  ce qui peut lever le canal 3 s'il est armé. Hatari la laisse haute pendant la pause et
  n'a pas d'impulsion. Non mesuré sur matériel.

**Passe temporelle du 2026-10-19 — état courant, prioritaire sur l'historique BL5.**
L'écart synthétique partagé **+60 cycles / IRQ +42 / trois tours CPU** est corrigé :
reprise PRE_START pendant l'instruction, callbacks de début/fin de cycle bus,
exclusion de l'accès initiateur et recouvrement des cycles CPU internes jusqu'au
prochain accès. GPU_DONE est émis après restitution du bus, comme Hatari.
BUSY est désormais effacé au même instant que GPU_DONE, conformément au lien
décrit par le manuel Atari. Avant correction, un événement pouvait observer
BUSY=0 pendant les quatre cycles de restitution alors que GPIP3 restait haut.
Quatre échecs reproduits avant correction ; 155 contrôles CPU/blitter passent
après sous ASan/UBSan. La garde ISR MegaSTE v2 refuse une IRQ pendant BUSY.
Le rebase préserve le dispatch DMA par accès sans double facturation du quantum.
Le verrou TAS couvre le read-modify-write et se libère avant le préfetch.
Les traces portent désormais les instructions réelles avant exécution ; les
continuations d'IRQ/RTE ne sont plus comptées comme travail CPU supplémentaire.

Dix sondes blitter au pin (ROM/RAM, HOG/partagé, tailles, MULU et TAS), 64
copies chacune après huit échauffements : **fin observée +4 cycles, IRQ +2 en
ROM / +4 en RAM, compte CPU identique**. Les dix-sept sondes du banc passent
sans exception KNOWN ni décalage incorporé aux références Hatari. Crédit CPU
sérialisé en v49, avec test de reprise TAS D0 → BRA pour les deux modes.

**BL5 pixel clos le 2026-10-19** : les 68 pixels restants provenaient de
l'alignement RAM calculé au callback médian, après une éventuelle tranche DMA.
Hatari aligne avant les cycles du bus. Le hook Moira reçoit maintenant l'adresse
réelle de chaque mot et aligne le début de l'accès avant SYNC(2), sans nouvelle
constante. Les 100 relevés TADR et les données copiées sont identiques ; comparaison
pixel **0/114816** contre Hatari. Deux oracles identiques ; `blitter_timer.png`
produit par `--oracle`, ancien snapshot retiré, provenance promue en oracle.

HOG utilise maintenant PRE_START et le recouvrement communs, au lieu de tourner
dans l'écriture du registre. Les anciens essais ajoutant seulement un délai au
chemin synchrone ne réfutent pas ce port complet. MegaSTE 16 MHz/cache reste à
qualifier globalement ; CPU/cache vérifié séparément ci-dessous. Les paragraphes BL5 ci-dessous décrivent l'investigation historique.

**MegaSTE 16 MHz, qualification CPU/cache du 2026-10-19** : alignement RAM au
début du cycle, attente supplémentaire passant par `sync`, décision hit/miss
avant les cycles et conservation de la valeur du hit pendant une DMA. Cache
invalidé avant les huit cycles d'arbitrage, conformément à `blitter.c:395-421`.
Onze sondes CPU/cache sans IRQ ni DMA donnent une égalité exacte contre deux
captures Hatari identiques, après conversion des intervalles CPU 16 MHz en
cycles bus 8 MHz. Tests locaux des quatre phases RAM et du cache pendant DMA.
Les mutants de l'ancien alignement/relecture et de l'invalidation tardive sont
rejetés (neuf et un échecs). La comparaison globale blitter/IRQ à 16 MHz reste
ouverte. Le banc global vérifie désormais douze configurations NeoST et
révèle deux défauts du pin : cycles DMA sans conversion à 16 MHz et IPL CPU
périmé quand aucun interrupt n'est pending. Les deux corrections expérimentales
rétablissent les bornes physiques, avec des écarts résiduels d'environ 12–24
cycles. Aucune référence issue des échecs. Voir
[`HATARI_MEGASTE_AUDIT.md`](HATARI_MEGASTE_AUDIT.md).

- ✅ **Reset chaud (2026-10-19)** : conserve RAM halftone et pipeline
  source comme `Blitter_Reset` Hatari ; transfert annulé et registres de
  commande remis à zéro. Initialisation froide déterministe NeoST conservée,
  sans nouvelle mesure de la valeur power-on matérielle.
Logique de données **fidèle** (HOP/LOP 16 cas + tables `need_src`/`need_dst`, FXSR/NFSR, smudge,
halftone, masques de bord, bug « 63 accès », IRQ GPIP3 fin de blit, arbitration MegaSTE/STE).

- **[BL1 — HAUTE] ✅ corrigé (2 passes)** — Compteur X **ou** Y écrit à `0` interprété comme **65536**
  (X via bouclage 16 bits ; Y via `yLatch_`, conversion **à l'écriture** du registre `$FF8A38` ≙
  `Blitter_LinesPerBitblock_WriteWord` blitter.c:1356). ⚠ La 1ʳᵉ passe (0→65536 relu dans
  `runSlice`) avait INTRODUIT une régression HAUTE : le restart du driver TOS (`bset #7` après
  chaque blit, y_count **résiduel** 0) relançait un blit de 65536 lignes → RAM labourée →
  **bureau TOS 1.06 STE scramblé au moindre redraw**. Modèle complet fidèle (commit c96311c) :
  start avec résiduel 0 → BUSY+HOG **effacés**, aucun blit (`Blitter_Control_WriteByte`
  blitter.c:1433-1437) ; écrire 0 PUIS démarrer → 65536 lignes (légal).
- **[BL2 — moyenne] ✅ corrigé** — `write8` ignore désormais un accès octet aux registres MOT
  (`off < 0x3A`, `Blitter.cpp`) ; seuls HOP/LOP/contrôle/skew restent accessibles en octet.
  *Avant :* accès **octet** aux registres mot/long non rejetés (`Blitter.cpp:99-105`)
  vs `Blitter_CheckAccess_Byte` (`blitter.c:972`) qui les ignore.
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — Masquage matériel à l'écriture des
  incréments (`&0xFFFE`, `blitter.c:1229`) et des adresses src/dst (`&0x00FFFFFE`,
  `Blitter_SourceAddr_WriteLong` blitter.c:1254) : `regWriteMask` masque désormais les octets
  faibles `$FF8A21/23/2F/31/27/35` (0xFE) et les octets forts `$FF8A24/32` (0x00). *Avant :*
  un incrément impair s'accumulait tel quel (dérive progressive des adresses vs Hatari).
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — Accès bus du blitter via le plan CPU brut
  (`bus_.read16/write16`) au lieu du modèle DMA (`STMemory_DMA_ReadWord/WriteWord`,
  `stMemory.c:724`) : une zone fautive lit désormais `0x0000` (`DMA_READ_WORD_BUS_ERR`) et
  absorbe l'écriture (`Blitter::readWord/writeWord` + `Bus::busFault`). *Avant :* un blit sur
  zone fautive lisait `0xFF`/void via le dispatch CPU.
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — `busCountError_` (bug « 63 accès ») remis
  à zéro à chaque entrée en phase PRE_START (`start()` non-hog), comme Hatari
  `Blitter_HOG_CPU_BusCountError = 0` (blitter.c:1457) — un accès volé dans une fenêtre
  périmée d'avant restart/reprise ne compte plus.
- **[basse] ✅ corrigé (2026-07-07 soir)** — Suspension **MID-WORD** portée
  (`BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED`) : la tranche non-hog rend le bus exactement au
  64ᵉ accès, même entre deux accès d'un même mot — état du mot (`haveSrc_/haveDst_/fetchSrc_/
  dstWord_` ≙ `BlitterState`) persisté entre tranches. ⚠ Piège débusqué à la mise au point : sans
  la SAUVEGARDE de ces drapeaux en fin de tranche, la reprise refait la lecture source (double
  `srcShift` → pipeline skew corrompu → mots perdus au bord des icônes GEM — 6 px verts, repro
  `--walk-mouse` tos106fr STE, diagnostic à la fenêtre d'accès blitter).
- **[basse] ✅ corrigé (2026-07-07 soir)** — Part CPU non-hog = **64 accès bus CPU réels** (port
  `BLITTER_PHASE_COUNT_CPU_BUS` / `Blitter_HOG_CPU_mem_access_after`, modèle CE — celui de
  l'oracle) : comptés par les callbacks mémoire de Moira (`Bus::blitterCountCpu` →
  `Blitter::noteCpuBusAccess`), le 64ᵉ arme PRE_START et date la tranche à +4 cyc. *Avant :*
  forfait fixe 256 cycles (modèle non-CE) — le CPU « payait » sa part même sans toucher le bus.
- **[HAUTE] ✅ corrigé (2026-08-25, BL3)** — **Base de temps unifiée** : les cycles volés par le
  blitter avancent désormais l'horloge de l'ordonnanceur (`Blitter::billCycles` →
  `Scheduler::addStolenCycles`), port de `Blitter_AddCycles` (`blitter.c:342-354`) qui écrit
  dans `nCyclesMainCounter` / `CyclesGlobalClockCounter` — les compteurs mêmes que lit
  `CycInt_Process`. *Avant :* `Cpu68k::addBusWaitCycles` n'avançait que l'horloge Moira ; pour
  la tranche non-hog, facturée depuis `Blitter::onSlice` (callback d'échéance, donc HORS
  `cpu.run()`), ces cycles échappaient à `ran` comme à `sched.now()`. Le crédit est
  **discriminé par `Cpu68k::inRun`** : dans le quantum (mode HOG, atteint depuis une écriture
  `$FF8A3C` routée par un callback mémoire de Moira) `ran` capte déjà le stall, et créditer
  aussi le double-compterait. Il n'y a **pas** de crochet générique dans `addBusWaitCycles` :
  les helpers PSG/MFP/ACIA sont aussi atteignables par une lecture d'**observation** hors
  machine (`serialLoopbackSelfTest` fait `bus.read8(0xFFFA01)`), qu'une horloge émulée ne doit
  pas suivre. Le dispatch est **par accès** depuis le même jour (**BL4 ✅** ci-dessus) : plus de
  résidu de fin de tranche.
- **[basse] ✅ porté le 2026-10-19** — ~~Pas d'exécution CPU parallèle pendant un blit~~
  (`Blitter_Check_Simultaneous_CPU` `blitter.c:1641`) : hooks Moira par cycle bus, les cycles
  CPU internes recouvrent désormais la tranche HOG/partagée jusqu'au prochain accès bus
  (`Cpu68k.cpp:57, 577-621`, `blitterOverlap`), crédit sérialisé depuis v49 (`kStateVersion`, `Machine.cpp`).
  Les hits du cache MegaSTE 16 MHz comptés comme accès bus sont un faux positif (Hatari les
  compte aussi, cf. HATARI_MAPPING ✗). Le
  cycle RMW/TAS est en revanche déjà atomique dans le modèle courant : aucun
  dispatch Scheduler entre lecture et écriture CPU. Banc réel ajouté le 2026-10-19 ;
  propriété à conserver si le dispatch CPU devient interne à l’instruction.
- ~~**[basse]** HOP/LOP et bit4 du contrôle masqués seulement à l'exécution, pas à la relecture du registre.~~
  *(obsolète : `regWriteMask` masque à l'écriture depuis la refonte — relecture fidèle.)*

- ✅ **Reset PSG R7 (2026-10-19)** : valeur remise à zéro selon Hatari/MAME,
  ancien $FF supprimé. Silence legacy/push, registres, ports, strobe et snapshot
  qualifiés ; aucune modification de la déviation documentée de lecture $FF8802.

- ✅ **Coexistence GEMDOS/ACSI (2026-10-19)** : dossier simple après le nombre
  de partitions ACSI, réévalué au montage et au reset/boot ; lettres explicites
  C..Z inchangées. Ancien montage/handles conservés après refus d’un nouveau
  dossier ; table vidée au reset si aucune lettre disponible, sans perdre la
  configuration. Retrait ACSI puis reset remappe le dossier. UI n’annonce plus
  systématiquement une collision C:, elle utilise le masque GEMDOS réel.
  Limite commune à Hatari : comptage des racines DOS/Atari, pas d’exploration XGM.

### USART MFP — réception hôte

- ✅ **Bouclage USART interne (2026-10-19)** : TSR H/L=11 sélectionne TX
  vers RX sans fixture externe ; SI/RC hôte déconnectés, même si TE=0.
  SO reste observable, RE qualifie toujours la réception et la prise externe
  ne double pas la livraison. Référence Motorola §2.10.1/§2.10.3/§7.3.2 et
  MAME `rc_w/tc_w`. Commutation évaluée au stop abstrait selon le mode courant,
  sans phase bit ni reconstruction des trames traversant une bascule H/L.
  Aucun nouvel état ; TSR était déjà sérialisé.

- ✅ **Overrun RX différé (2026-10-19)** : un octet non lu reste conservé ;
  le suivant est perdu, mais OE et RXERR attendent la lecture UDR. Lire RSR
  avant UDR n’acquitte pas cette erreur en attente. Une fois OE publié, les
  nouvelles données sont ignorées jusqu’à lecture RSR. RE=0 et reset annulent
  les deux étapes, sans effacer une IRQ MFP déjà pendante lors de RE=0.
  Référence Motorola MC68HC901UM §7.2.2 p.7-5 et MAME `REGISTER_UDR` /
  `rx_async_frame_complete`. Corrige le modèle du 2026-10-18 qui signalait
  immédiatement RXERR au caractère perdu. L’étape en attente est sauvée v44.

- ✅ **Durée et phase USART RX (2026-10-18)** : format asynchrone UCR,
  retenue rationnelle et échéance ancrée pour éviter la dérive. Configuration
  en vol conservée jusqu’au prochain caractère. Reste et phase sauvegardés.
  Références : Hatari `rs232.c:329-365` et MAME `mc68901.cpp`, format USART.
  RXFULL est datée au stop du caractère, même si le callback est servi en
  retard ; RXERR d’overrun est datée de la lecture UDR qui publie OE.
  UCR7 sélectionne /1 ou /16, sans arrondis tty ni plancher RX (2026-10-18).
  Référence : MC68901UM §7.1.3. Le rapport Timer D/UCR exact cadence TX/RX,
  même aux débits non entiers ; la fraction est conservée aux reconfigurations.
  Diviseur effectif sauvegardé en v30, même après arrêt du Timer D. Le débit
  entier exposé reste informatif ; transport synchrone en repli 8N1 /16.
- ✅ **USART RX désactivé et UDR persistant (2026-10-18)** : RE=0 efface
  RSR, Buffer Full et overrun, sans effacer la donnée reçue ni les IRQ MFP
  déjà pendantes. UDR lit toujours le registre RX, même vide, jamais le dernier
  octet TX. Référence : MAME `REGISTER_RSR/REGISTER_UDR`. La file hôte garde
  sa cadence ; réacquisition après RE bas puis haut au milieu d’un caractère
  encore approximative, faute de représentation des bits du câble.
- ✅ **USART underrun mémorisé (2026-10-18)** : UE reste posé lors des
  écritures/rechargements UDR ; les fins de caractères suivantes ne redemandent
  pas TXERR tant qu’il est mémorisé. Lecture TSR et TE=0 le réarment ; IRQ
  déjà pendante distincte. Référence : MAME `tx_clock/REGISTER_UDR`. Le délai
  minimal d’une horloge TX avant effacement par lecture est porté : seuil
  latched sur la phase TX engagée, indépendant d’un ancien UE et des nouvelles
  écritures UDR. Une lecture précoce garde UE ; à la frontière, elle le retourne
  puis l’efface. Délais de trame/UE séparés et sauvegardés en v36.
- ✅ **USART END/auto-turnaround au repos (2026-10-18)** : désactiver
  TE quand aucun caractère n’est en vol pose END immédiatement ; AT active RE
  puis se désarme. En vol, effets à la fin du caractère. IRQ END uniquement
  sur nouveau front, datée à la fin d’accès MMIO ou au stop théorique.
  Référence : MC68901UM §7.3.2 ; MAME `tx_clock` pour le chemin en vol.
  TDR, RDR et IRQ déjà pendantes restent conservés.
- ✅ **USART RSR, droits d’écriture (2026-10-18)** : seuls RE/SS sont
  contrôlables, plus F/S en synchrone. Les écritures ne créent ni n’effacent
  les bits de statut matériel ; RE=0 conserve son effacement complet établi.
  Référence : MC68901UM §7.2.2, MAME `REGISTER_RSR`. Les droits TSR étaient
  déjà portés et sont gardés par le même banc.
- ✅ **USART RX, mots courts (2026-10-18)** : l’octet hôte est aligné à
  droite et masqué à 5..8 bits, bits supérieurs à zéro. Largeur mémorisée au
  début du caractère, conservée dans l’état v31. Référence : MAME mc68901.cpp
  `rx_frame_start/rx_clock`, UCR CL MC68901UM. Bouclage ordonnancé déjà
  masqué par TX ; repli sans Scheduler instantané historique conservé. Aucune
  erreur de parité ou framing synthétisée depuis un callback d’octet.

- ✅ **USART TX asynchrone (2026-10-18)** : TDR/TSR, Buffer Empty au
  chargement TSR et émission/bouclage en fin de caractère. La désactivation
  laisse finir le TSR ; underrun et EOT sont distingués. La cadence suit UCR
  et conserve une phase rationnelle, sauvegardée avec SERIAL_TX. Référence :
  MAME `mc68901.cpp:958-1015,1387-1432` ; Hatari garde un puits instantané.
  Phase du premier front TC, framing synchrone et forme d’onde
  BREAK restent approximatifs ; le puits hôte n’a pas de timestamp explicite.

### SCC Z85C30 (MegaSTE) — `Scc.cpp` ↔ `scc.c`
Cœur registre/IRQ **fidèle** (RR/WR, `updateRR0/RR2/RR3`, `updateIRQ` IUS/prio, IACK VIS/NV,
reset HW/canal, IRQ niveau 5 gatée par le SCU).

- ✅ **SC1 — caractères TX/RX cadencés (2026-10-18)** : WR4/WR3/WR5
  déterminent format et longueur ; WR11 sélectionne les horloges, le BRG fournit
  le débit. TDR et TSR distincts : le second octet attend la fin du premier ;
  IRQ TX au transfert vers TSR, RR1 All Sent après vidage. Réception hôte
  mise en file (256 octets, dernier rejeté si pleine), un caractère par échéance.
  Bouclages interne/externe livrés à la fin TX, sans second délai artificiel.
  Horloge bus indépendante du 8/16 MHz CPU. FIFO RX matérielle de trois octets
  portée selon Zilog UM0109 et MAME : erreurs par emplacement, débordement
  remplaçant le plus récent, modes premier/tous/spécial, verrouillage sur erreur
  et Error Reset. Choix matériel qui dépasse le modèle mono-octet Hatari.
  Restent DPLL, FIFO SDLC et série hôte réelle ; TRxC garde la fréquence indicative Hatari.
- ✅ **First et Error Reset (2026-10-19)** : retirer le premier caractère
  en erreur efface aussi son IP ; suivant sans IRQ héritée. UM0109 p.47/p.142,
  Enable Next explicite et lecture verrouillée sans retrait préservés.
- ✅ **WR1 et verrou d’erreur (2026-10-19)** : changer entre
  First/Special et Special Only conserve le verrou/IP spécial déjà acquis.
  UM0109 p.146 et MAME ; Error Reset libère toujours le caractère, politiques
  Disabled/All conservées. Aucun nouveau champ d’archive.
- ✅ **RX All/Special et FIFO vide (2026-10-19)** : IP/IRQ effacées
  au retrait du dernier caractère, même si RR1 conserve une erreur collante.
  Prochain caractère qualifié spécial jusqu’à Error Reset ; modes First/Special
  verrouillés conservés. UM0109 p.47–48/p.179 et MAME.
- ✅ **Lectures étendues Z85C30 (2026-10-19)** : WR7’ D6 expose
  WR3/4/5/7’/10 via RR9/4/5/14/11 sans consommer FIFO RX ni IRQ.
  Gate D6 seul selon description spécifique UM0109 p.157–158 ; en-tête
  de table générale Z85X30 ambigu sur WR15 D2, sans mesure matérielle ici.
  WR15 D0 sélectionne les écritures WR7’ seulement ; autres fonctions
  WR7’ et FIFO SDLC hors périmètre.
- ✅ **WR14 BRG et reprise d’écho (2026-10-19)** : D1..0 ne touchent
  que source/enable BRG ; leur modification conserve l’écho RX utilisant
  RTxC/TRxC directs. Les autres changements WR14 et RX BRG/DPLL restent
  invalidants. Après arrêt/reprise du BRG, une trame écho déjà invalidée
  garde cet état jusqu’à son retrait ; le caractère suivant repart neuf.
  Durée abstraite de reprise inchangée, sans reconstruction bit à bit ni
  nouveau champ. Zilog UM0109 p.166/p.172.
- ✅ **Réglage BRG et écho RX direct (2026-10-19)** : WR12/13 ne
  corrompent plus l’écho engagé lorsque RX choisit RTxC/TRxC directs.
  Invalidation gardée pour RX BRG/DPLL ; cadence BRG et Zero Count inchangées.
  Zilog UM0109 p.166–168, sélection RX indépendante du compteur TC.
- ✅ **Horloge TX et Auto Echo (2026-10-19)** : changer seulement la
  source TX (WR11 D4..3) ne corrompt plus l’écho RX engagé. Source, délai
  et phase RX restent indépendants ; les autres modifications WR11 gardent
  leur invalidation. Zilog UM0109 p.166–167, multiplexeurs TX/RX séparés.
  DPLL sélectionnable côté TX sans moteur ajouté ; archive v44 inchangée.
- ✅ **Stop RX indépendant de TX (2026-10-19)** : WR4 D3..2 sélectionne
  seulement les stops émis ; RX en vérifie toujours un (Zilog UM0109 p.93 et
  p.151). Délais et IRQ RX corrigés, TX inchangé. Auto Echo huit bits sans
  parité accepte les trois choix de stops TX et conserve un caractère engagé
  si seuls ces bits changent entre modes asynchrones. Parité, mode synchrone
  et changements d’horloge gardent leur invalidation. Aucun gap/stop source
  physique reconstruit ; échéance engagée et archive v44 conservées.
- ✅ **Priorité INTACK (2026-10-19)** : l’acquittement respecte la chaîne
  des IUS, y compris par lecture RR2 Software INTACK. Une source moins
  prioritaire déjà bloquée ne reçoit plus un IUS fantôme ; une source plus
  prioritaire reste acquittable. Référence Zilog UM0109 p.41–42. Lecture
  vectorielle de polling inchangée ; l’amont MAME softRR2 partage le raccourci
  corrigé ; comportement défini ici par le manuel matériel.
- ✅ **MIE et IUS (2026-10-19)** : désarmer WR9 D3 masque l’IRQ sans
  effacer les interruptions en service. Un acquittement matériel ou logiciel
  effectué pendant MIE=0 ne crée aucun nouvel IUS ; Reset Highest IUS garde
  son effet. Référence Zilog UM0109 p.161 et MAME. Corrige le raccourci encore
  présent dans Hatari ; aucune nouvelle donnée sérialisée.
- ✅ **Reset canal IP/IUS (2026-10-19)** : WR9 efface uniquement les
  interruptions pendantes et en service du canal ciblé. RR3A conserve les IP
  de B lors d’un reset A ; les IUS du canal réinitialisé cessent de bloquer
  les autres demandes. FIFO, vecteur partagé et IUS de l’autre canal conservés.
  Référence Zilog UM0109 p.160, MAME `reset_interrupts`. Hatari possède aussi
  l’ancien raccourci RR3/IUS ; correction fondée sur les sources matérielles.
- **[SC1b — reset et multiplexage Local Loopback portés]** `WR14=$30`
  au reset sélectionne le bouclage interne (Zilog), valeur conservée. TX est
  livré à RX interne et au sink représentant TxD ; RxD externe est ignoré
  (UM0109 p.69, MAME write_rx). Bascule en cours de réception : caractère
  hôte interrompu invalidé, FIFO déjà complétée conservée. La prise externe
  occupe le sink et ne double pas la réception. Priorité Auto Echo préservée.
- ✅ **BRG / Zero Count (2026-10-18)** : `Scc::updateBrg/onBrgEvent` portent
  les sources PCLK/RTxC MegaSTE, TC WR12/13 et WR14. Zero Count précède
  le diviseur /2 et WR4 : (TC+2)/source, avec retenue rationnelle.
  Deux événements Scheduler, ancrés à leur échéance précédente, alimentent les
  interruptions externes avec masques WR1/WR15, MIE, IUS et vecteurs A/B.
  RR0 ZC reflète une pulse d’un cycle source, non latchée et forcée à zéro
  lorsque WR15 ZC IE est coupé. Les constantes TC ne prennent effet qu’au
  prochain rechargement. Références : UM0109 p.57/71/177 et MAME
  `update_baudrate_timer`. Le reset canal conserve phase et débit mémorisé.
  **Choix explicite** : WR9 reset matériel et reset machine annulent les timers
  quand WR14 désactive le BRG ; l'amont laisse sinon un événement fantôme.
  Les périodes et phases sont sauvegardées, avec bornage des périodes au chargement.
  **Horloge indépendante du CPU** : la période est exprimée sur la base bus 8 MHz
  de NeoST. Le code Hatari utilise `CPU_Freq` (16 MHz fixe sur MegaSTE), même
  quand `CPU_Freq_Emul` vaut 8 MHz : il ralentit alors le BRG d'un facteur deux.
  NeoST garde la cadence PCLK/RTxC lors d'une bascule 8/16 MHz.
- **[SC2 — basse, TRANCHÉ : NeoST suit la datasheet (2026-07-12)]** **Lecture RR9** : NeoST
  renvoie `WR[13]` (`Scc.cpp:747`, RR9 = image de RR13 selon la datasheet Zilog) là où Hatari
  renvoie `WR[9]` (`scc.c:1606-1610` — son propre commentaire « also returns RR13 » contredit
  son code). Diff au byte près face à l'oracle si un programme lit RR9 — à garder en tête
  lors d'un diff SCC, ne PAS « corriger » sans le documenter.
- **[SC3 — basse, bug Hatari]** **Lecture $FF8E07 (VME Interrupter)** : Hatari renvoie
  `SCU.SysInterrupter` (copier-coller, `scu_vme.c:282-286`) ; NeoST renvoie bien
  `vmeInterrupter` — conforme au matériel, cf. § « NeoST améliore Hatari ».
- **[SC4 — basse, non porté]** **Accès aux masques SCU ($FF8E01/$FF8E0D)** : chez Hatari,
  LIRE ou ÉCRIRE un registre de masque **purge toutes les IRQ pendantes** SCU
  (`scu_vme.c:202-221,351-369`) ; dans NeoST le modèle « sources vivantes » (`Cpu68k::syncState`
  re-peuple les pendings à chaque updateIpl) les fait survivre. Observable seulement sur
  MegaSTE si un programme polle $FF8E01 avec une VBL/HBL en attente — divergence assumée du
  modèle niveau, à re-trancher si un étalon MegaSTE l'expose.
- ✅ **SCC Auto Enables WR3 (2026-10-18)** : CTS/DCD qualifient les bits
  TX/RX logiciels, avec exceptions local loopback/auto echo. TSR courant terminé
  avant attente CTS ; DCD perd le seul caractère en cours, conserve FIFO et
  futurs octets hôtes. Broches brutes indépendantes du verrou RR0. Référence :
  Zilog UM0109 p.106/p.149 et MAME `cts_w/tra_complete`.
  RTS asynchrone Auto Enables reste actif jusqu’au dernier stop du TSR et du
  TDR (p.95/p.154). Le câblage CTS suit la sortie physique, distincte de WR5 ;
  cette sortie est sauvegardée en v29.
- ✅ **SCC RR8, formats courts (2026-10-18)** : bits inutilisés forcés à un
  selon WR3 en asynchrone, à l’entrée de la FIFO. Réécrire WR3 ne change pas
  les caractères déjà reçus. Référence : manuel Zilog UM010904, p.148.
  Formats synchrones conservés.
- ✅ **SCC parité RX courte en bouclage (2026-10-18)** : selon WR4 p.152,
  le bit de parité reçu occupe le bit suivant les données pour RX 5/6/7 bits.
  La parité émise est mémorisée au chargement TSR et transmise par les boucles
  locale/externe de même largeur ; seuls les bits restants sont forcés à un.
  FIFO et snapshots conservent la valeur. L’API d’octets hôtes n’apporte aucun
  bit physique : pas de parité ni erreur inventée, ni décodage de formats discordants.
- ✅ **SCC External/Status (2026-10-18)** : verrous WR15 indépendants du
  masque maître WR1 D0 (Zilog p.56–57). Désactiver WR1 efface l’IP sans
  ouvrir les verrous ; le réactiver retrouve une condition mémorisée, y compris
  un Zero Count déjà expiré. WR0 conserve le protocole de réarmement.
- ✅ **MFP BREAK selon le mode (2026-10-19)** : B agit seulement en
  asynchrone (MC68901UM §7.3.2, MAME tx_clock). En repli synchrone 8N1,
  il ne bloque plus TDR ni UE ; changement UCR libère le TDR retenu. Pas de
  protocole synchrone complet. BREAK asynchrone désormais cadencé par caractère
  selon MC68HC901 p.7-8, TSR préservé et BE antérieur conservé. TE coupé finit
  à la frontière sans stop ; reprise modélisée avec un stop d’une période TX
  (durée déduite), puis TDR ou UE. Pas de phase TC initiale ni d’onde physique.
- ✅ **SCC priorité Auto Echo (2026-10-19)** : WR14 D3 déconnecte la sortie
  TSR avant les bouclages et le sink, tout en laissant le pipeline/IRQ fonctionner
  (Zilog p.69–70/p.172). D3+D4 conserve l’exception DCD documentée p.70.
  L’écho hôte RX huit bits sans parité est livré après un stop RX abstrait,
  indépendamment des stops TX et de RXEN/DCD. Reconfiguration RX en vol invalide le caractère ;
  autres formats, parité et fragments physiques restent hors modèle. Trois
  états de trame écho/RX sauvegardés v40, aucune boucle récursive externe.
- ✅ **SCC Reset Tx Underrun/EOM (2026-10-19)** : WR0 D7–D6 indépendants
  des commandes et pointeurs. Statut brut effacé aussi en asynchrone selon
  l’exception RR0 détaillée p.176 ; image externe verrouillée conservée, sans
  front IRQ descendant. Commandes combinées vérifiées, sans moteur CRC physique.
- ✅ **SCC statuts selon le mode (2026-10-19)** : Sync/Hunt forcé à zéro
  sans fermer les verrous quand /SYNC sert à l’oscillateur en asynchrone ou
  External Sync ; BREAK nul en synchrone hors SDLC (Zilog p.58–59). Les
  niveaux bruts restent disponibles après désactivation de l’option. Pas de
  moteur Hunt, synchronisation ni détection d’abort physique ajoutés.
- ✅ **SCC fronts BREAK sous verrou (2026-10-18)** : assertion et fin
  conservées même pendant une autre cause External/Status, selon Zilog p.58.
  WR0 sert les polarités mémorisées ; cycles répétés coalescés, sans compteur
  d’événements ni RX FIFO/framing inventés. Nouveaux champs sauvegardés v39.
- ✅ **SCC caractères issus du BRG rationnels (2026-10-18)** : rapport
  exact PCLK/RTxC, WR12/13 et WR4, sans arrondi du baud ni seuil à 1 baud.
  Les fractions TX/RX existantes sont réutilisées, source de même fréquence
  conservée lors d’un changement TC/multiplicateur. Gardes v35 élargies pour
  PCLK et longues suspensions BREAK. Référence : UM0109 p.167–168 et p.172.
  Zero Count possède désormais sa phase rationnelle indépendante ; précision
  sous-bit de BREAK toujours absente.
- ✅ **SCC horloges directes rationnelles (2026-10-18)** : RTxC/TRxC
  utilisent le rapport CPU/horloge et WR4 sans tronquer chaque bit. Fractions
  TX/RX indépendantes par canal, sauvegardées en v34 ; incrément égal à la
  différence des plafonds cumulés, y compris quand le reste revient à zéro.
  Source changée effective au prochain caractère, fraction réinitialisée ;
  nouveau flux après silence ancré sur l’horloge vive. Référence : UM0109
  WR4/WR11 ; MAME horloges séparées. Hatari tronque aussi CPUFreq/baud avant
  le caractère. Cadence BRG des caractères portée ci-dessus ; Zero Count est
  également rationnel. BREAK conserve son reste en
  cycles sans phase exacte du bit.
- ✅ **SCC SEND BREAK WR5 (2026-10-18)** : suspension TSR, TDR retenu,
  reprise du temps restant ; aucun octet intact émis pour une trame interrompue.
  Rattrapage des caractères déjà finis avant une écriture BREAK tardive.
  Référence : UM0109 p.153 (prochain front TX, indépendant de TX Enable),
  MAME `tra_callback/tra_complete`. Suspension sauvegardée en v33.
  Limites : résolution au caractère et reste en cycles, sans phase exacte du
  front de bit, erreurs RX ni symbole corrompu inventé ; indication RR0 BREAK
  bouclée immédiate conservée. Auto-clear X21 non porté.
- ✅ **SCC TX Enable (2026-10-18)** : désactiver WR5 bit3 laisse finir la
  trame engagée à son échéance, avec ses données et son format. Un TDR écrit
  pendant cet arrêt ne remplace pas le TSR actif ; sa reprise attend TX Enable
  et CTS. Indicateur de départ sauvegardé en v32. Référence : UM0109 p.153.
- ✅ **SCC TX 1..8 bits (2026-10-18)** : WR5 « cinq ou moins » décode la
  longueur depuis les bits hauts du TDR ; seuls les bits de données bas sont
  émis. Modes 6/7 bits également masqués. Format du TSR et longueur mémorisés
  au chargement, durée et sauvegarde v31 associées. Référence : UM0109 p.153,
  tableau WR5 ; chargement TSR p.114. Hatari garde cinq bits fixes pour ce mode.
- ✅ **SCC Sync Character Load Inhibit WR3 (2026-10-18)** : l’octet égal à
  WR6 est retiré avant FIFO, overrun et IRQ, y compris en asynchrone. SDLC
  est exclu ; en Bisync 12 bits (WR10 D0), le filtre est ignoré. Comparaison
  sur les huit bits du caractère prêt pour la FIFO. Référence : UM0109 p.149,
  avertissement asynchrone explicite. Hatari ne porte pas ce filtre ; MAME
  journalise seulement le bit. Recherche et assemblage de synchronisation non ajoutés.
- ✅ **SC5 — miroirs RR6/RR7 corrigés (2026-10-18)** : choix FIFO par WR15 du
  canal adressé ; hors FIFO, RR6B lit RR2B et RR7B rend RR3B (=0), sans dépendre
  du canal A. Le vecteur est actualisé et le soft IACK suit le chemin RR2.
  **Correction volontaire par rapport à Hatari**, qui partageait ce défaut :
  [manuel Zilog UM0109](https://www.zilog.com/docs/serial/UM0109.pdf), table Z85X30
  pages 31–32, et [MAME z80scc.cpp](https://github.com/mamedev/mame/blob/master/src/devices/machine/z80scc.cpp),
  `scc_register_read` remappe RR6 avant `do_sccreg_rr2` lorsque la FIFO est désactivée.
  La FIFO SDLC reste non implémentée. Les choix RR9 et RR15 déjà corrigés sont conservés.

---

### CPU (Moira + intégration) — suivi (audit vendor 2026-08-13)

- ✅ **STOP avec IRQ pendante : exception à STOP+8 (2026-10-22)** — Moira prenait l'IRQ
  démasquée par l'opérande du `stop` dès la fin de l'instruction (+4) ; le 68000 teste
  l'interruption AVANT d'écrire le masque et ré-itère STOP 4 cycles plus tard (fx68k
  `rInterrupt`/`pswI` au même T3, nanomots 0x3A2/0x327 ; Hatari gencpu `checkint()` avant
  `regs.sr = sr`). Patch vendor `Moira::stopGraceUntil`. Conséquence : la constante IACK
  MFP repasse de 16 à 12 (`g_iackMfp`) — le 16 compensait ce défaut depuis le 2026-08-06.
  Sonde cartouche (chaîne Timer B → handler → stop → HBL, 3 997 chaînes) : NeoST = Hatari
  segment par segment. Les étalons `blankline_blank_st`, `blankline_node_late_st`,
  `nocooper` et les sondes `fdc_read/write` de l'oracle temporel, verts avec 16, NE
  mesuraient PAS l'IACK : ils glissaient de phase avec le boot et masquaient deux écarts
  réels (fenêtre de blank GLUE pour une écriture `$820A` aux cycles 32-36 ; fin de commande
  WD1772 −6 cycles), traités à part.

Audit complet des patches vendorisés (`extern/moira/NEOST_VENDOR.md`) : diff intégral
contre l'upstream — 5 fichiers modifiés, tous les hunks documentés, aucun écart
accidentel. Le crash « BusError hors execute() via GEMDOS HD » a été **corrigé le jour
même** (primitives non-fautives + filet au site d'appel). Restent trois points bas :

- **[CPU1 — basse]** Le fast-forward du STOP (`Cpu68k.cpp`, garde `isStopped() &&
  !irqDeliverable()`) saute par-dessus une exception trace ou privilège en attente
  (`stop` sous bit T armé ; `stop #imm` effaçant S) : l'exception part avec jusqu'à un
  écart d'événement de retard (vs immédiat sur 68000 réel). Timing seul, motifs
  exotiques (moniteurs pas-à-pas) — NE PAS toucher sans re-valider Closure (le
  fast-forward est sur son chemin critique).
- **[CPU2 — basse]** `chipWait16` (Mega STE 16 MHz) n'a pas reçu la correction de
  phase mi-accès `(c − 2)` appliquée à `chipWait8` le 2026-07-02 — skew structurel
  probable de l'alignement slot RAM en 16 MHz. À trancher par A/B contre l'oracle
  Hatari, pas à corriger à l'aveugle (mode peu validé).
- ✅ **[CPU4 — DIVU/DIVS, 2026-10-20]** Le préfetch précédait les cycles internes de
  division, alors que Hatari CE (`op_80c0_13_ff`) et le 68000 le placent après :
  jusqu'à 4 cycles d'écart par DIV exécuté en RAM sous `NEOST_RAM_SLOT`. Le correctif
  du fork (`e4da365`) avait été perdu à la vendorisation ; rétabli par le port de
  l'amont `f90fc50`. Sonde `cpu_div_ram` : 0 cycle d'écart sur 46 intervalles.
- ✅ **[CPU5 — #,SR/#,CCR et MOVE to CCR, 2026-10-20]** ANDI/ORI/EORI #,SR, ANDI/ORI/EORI
  #,CCR et MOVE to CCR rechargent les deux mots de préfetch après l'écriture de SR/CCR
  (`fullPrefetch`, comme MOVE to SR le 2026-10-19), en 68000 seulement. Le préfetch suit le
  microcode (`003c`/`023c`/`0a3c`, `007c`/`027c`/`0a7c`, `44c0..44fc`) et Hatari CE
  (`op_027c_13_ff`). Accès pc0+4, pc0+4, pc0+6, la relecture se faisant en espace
  PROGRAMME après le changement de SR. Un `ANDI #$DFFF,SR` en `$7FA` prend la bus error sur
  `$7FE` en mode utilisateur. Durées inchangées. Détail : `extern/moira/NEOST_VENDOR.md`.
- ✅ **[CPU — faute levée hors instruction, 2026-10-20]** `triggerBusError` appelé hors de
  `moira->execute()` (accès hôte : `--dump-at`, débogueur) ne lève plus `moira::BusError`
  (`std::terminate`) et ne pose ni `inBusError` ni `faultValid` : l'accès est abandonné avec
  une valeur neutre. Les seuils `NEOST_IPLFETCH4/2` sont aussi relus et jetés au load, puis
  recalculés d'après la configuration du processus.
- ✅ **[CPU3 — basse, résolu le 2026-10-20]** L'échantillon IPL différé du mode
  `g_iplFetch` (NEOST_IPLFETCH=1, opt-in expérimental) n'était pas sérialisé : armé au
  save, il était perdu au load (IRQ reconnue une frontière trop tard). `iplDeferred` est
  désormais dans l'état CPU (`Cpu68k.cpp`, après `iplDelay2`) et refusé au load hors
  [-1,7] ; format **v50** (`kStateVersion`, partagé avec la résolution Shifter `$FF8261`).

---

## Documents internes périmés — ✅ SUPPRIMÉS

Deux docs décrivaient un état du code **antérieur** et listaient comme « absents » des
comportements aujourd'hui implémentés (faux positifs). Ils ont été **supprimés** et remplacés
par le présent document :

- ~~`docs/IKBD_HATARI_DIFF.md`~~ : TIE, master reset, `$08`/`$1A`/`$19`/`$20`/`$21`/`$87-$9A`,
  pause `$13`, overrun, cadence série, buffer borné, quirks reset — tous implémentés.
- ~~`docs/SOUND_HATARI_DIFF.md`~~ : table DAC, ET ton/bruit, filtres, FIFO/anti-repliement,
  start==end, cold/warm reset, reset port A, `Mfp::reset()` — tous implémentés.

## Cas où NeoST améliore Hatari (choix à préserver / documenter)

- **STX densité HD/ED** (D, F3) : NeoST recadre `BitPosition` sur la densité → cohérent là où
  Hatari a une incohérence interne (sans effet en DD).
- **WRITE TRACK STX** (D1) : NeoST rend la piste réécrite lisible (Hatari = TODO) → utile aux
  formateurs ordinaires ; à arbitrer pour les protections format-and-verify.
- **WRITE TRACK `.ST`** : NeoST écrit les secteurs d'une géométrie standard (Hatari renvoie
  LOST_DATA, TODO).
- **SCC `WR14` bit4** : bouclage interne et multiplexage TxD/RxD portés
  selon Zilog, avec priorité Auto Echo et convention de prise externe (SC1b).

- **Granularité du refill FIFO du son DMA STE** (2026-09-02, étalon `dmasnd_poll`) : sur un poll
  serré de $FF890B/0D, NeoST rend le découpage par HBL **IDÉAL implanté par le débit** — à
  50066 Hz stéréo, 100 132 o/s ÷ 15 650 lignes/s = **6,398 o/ligne**, donc des sauts de 6 avec
  19,9 % de sauts de 8 ; mesuré : 12 sauts de 8 pour 47 de 6, soit **20,3 %**. Hatari, lui, jitte
  sur 4/6/8/12 autour du même débit moyen, parce que sa consommation DAC passe par le
  rééchantillonnage vers le **taux hôte** : le découpage y hérite d'une granularité d'échantillon
  hôte, étrangère au matériel.
  ⚠ **Et c'est ce qui l'empêche de se reproduire** : son accumulateur fractionnaire court depuis
  le DÉMARRAGE de l'émulateur, donc dépend de la durée du boot — que son RNG tire au sort. Deux
  runs Hatari identiques donnent **664 à 1432 px d'écart entre eux** selon les options son
  (`--sound off` comme `--sound 50066`), et l'ancrage VBL de la recette `spec512_bands` n'y peut
  rien : il fixe la phase du PROGRAMME, pas celle du resampler. **Aucun oracle n'est donc dérivable
  sur ce chemin** — d'où `ref_kind: snapshot`, et c'est le premier étalon du corpus refusé à
  l'oracle pour non-reproductibilité d'HATARI et non pour une raison de modèle.
  **Ce qui a quand même été confronté**, et qui valide le débit : sur la fenêtre des 100 polls, les
  deux émulateurs avancent le compteur de **382 octets EXACTEMENT**.
- **SCC RR9** (SC2) : NeoST rend l'image de RR13 (datasheet) ; Hatari rend WR9 (son commentaire
  dit l'inverse de son code).
- **SCU $FF8E07** (SC3) : NeoST renvoie le VME Interrupter ; Hatari renvoie le Sys Interrupter
  (copier-coller `scu_vme.c:282-286`).
- **Lignes 60 Hz isolées `left+2 / right-2`** (A40, 2026-08-30) : NeoST rend les 160 octets
  **décalés de 4 px** — ce que fait une ligne dont le DE part 4 cycles plus tôt. Hatari, lui,
  RECOPIE 2 octets dans les 2 derniers octets de la bordure gauche du tampon
  (`video.c:4041-4053`) et met le reste à 0 : or 2 octets en basse résolution ne sont pas
  4 pixels, c'est **un mot de plan sur 16 pixels**. Le groupe de 16 px à cheval sur la limite
  sort donc avec des plans mixtes. Mesuré sur l'étalon `overscan_top` : tête = plans 0-2 à
  zéro + plan 3 à $FFFF → **index 8** ($333), queue (memset `right-2`) = plans 0-2 à $FFFF +
  plan 3 à zéro → **index 7** ($555) — palette du run relevée à `--dump-at 380 FF8240 32`,
  et l'oracle montre exactement 16 px de $333 en x=32..47 et 16 px de $555 en x=352..367.
  Compte total : 4 lignes `bm=00a` × 32 px + 1 ligne `bm=002` × 16 px = **144 px**, soit
  l'intégralité de l'écart. Les DEUX émulateurs sont d'accord sur l'ÉTAT Glue (trace Hatari
  `--trace video_border_h` : `detect left+2 / right-2 60Hz 53<->373`, identique au masque
  NeoST) : l'écart est purement dans la recopie. ⚠ **Conséquence** : `overscan_top` reste
  `ref_kind: snapshot` **définitivement** — le passer en `oracle` installerait l'artefact
  d'Hatari comme référence. Aussi visible sur `blankline_blank_ste` (2026-10-20, STE
  etos256fr) : les phases VBL 32/36 y donnent `left+2` et divergent de **204 px** sur la
  ligne buffer 65 — c'est pourquoi l'étalon capture la phase 40 (NO_DE, 0 px).
- **Première ligne affichée quand la bordure HAUTE est retirée** (A41, 2026-09-01) : Hatari
  amorce sa palette avec des écritures qui n'ont pas encore eu lieu. `Spec512_StartFrame`
  (`spec512.c:233`) fait `nScanLine += OVERSCAN_TOP` sous `V_OVERSCAN_NO_TOP` : les
  `CyclePalettes` des scanlines **0 à 28 ne sont jamais rejouées**, et l'amorce vient de
  `pHBLPalettes[]` — que le `pHBLPalettes -= OVERSCAN_TOP` de `video.c:3429` (commenté
  « FIXME useless ? » par Hatari lui-même) a garni d'écritures postérieures. **Mesuré** sur
  Closure en instrumentant `Spec512_StartFrame` : l'amorce vaut
  `000 100 200 210 310 310 320 420 430 531 442 541 552 652 652 763`, soit **exactement, sur
  les 16 registres**, le bloc que la démo écrit aux cycles **438-508 de cette même ligne 34**
  — 380 cycles APRÈS les pixels qu'il colore. Le faisceau au cycle 56 ne peut pas afficher
  une couleur écrite au cycle 446 : NeoST, qui applique les écritures à leur cycle, est
  fidèle. Contexte relevé : `vover=3 nStartHBL=34 STScreenStartHorizLine=0 OVERSCAN_TOP=29
  nScanLine=29 skip=5`. ⚠ **Conséquence** : `closure` reste `ref_kind: snapshot`
  **définitivement** (27 px d'écart oracle irréductibles, minimum cherché sur 70 trames
  voisines : 27 ou 43, jamais 0).
- ✅ **GPIP bits 6 (RI) et 3 (GPU_DONE) au repos — verdict MATÉRIEL (2026-10-20)** :
  `$FFFA01` au repos (moniteur couleur, CTS/DCD actives comme chez Hatari) vaut **$F9** sur ST
  sans blitter et **$F1** avec blitter (Mega ST/STE/Mega STE) ; Hatari rend **$B1**.
  `Mfp::gpipInput` : bit 6 = 1 tant que RI n'est pas assertée (`riLine_ = false` au repos),
  bit 3 = 1 sans blitter (`hasBlitter_`, posé par `Machine` via `machineHasBlitter`), 0 au repos
  avec blitter et 1 pendant un blit. Les deux bits SUIVENT toujours leur ligne (détecteur de
  front de `gpipSetLine` intact). **Sources** : *STe Field Service Manual* (C302481-001, §2.12.5)
  — CTS/DCD/RI entrent au MFP via un récepteur 1489 (U207) ; fiche TI MC1489 (SLLS095D) —
  entrée ouverte → sortie haute ; MAME `rs232.cpp` — RI = 1 sans appareil ; lecture sur STFM
  réel `$E9` (exxosforum t=3992 p.49, 2022 — bits 6 et 3 à 1 ; ses bits 1/2 CTS/DCD à 0 sont
  cohérents avec la convention actuelle) ; agranlund/STBlitter_RevA (README : pull-up R4 de
  l'INT blitter). Le 0 d'Hatari est un **artefact** : `MFP_Reset` pose GPIP = 0 (mfp.c:523)
  et rien ne relève jamais le bit 6 (le bit 3 attend le premier `Blitter_Start`).
  ⚠ **Conséquences** : un diff de trace sur `$FFFA01` contre Hatari est ATTENDU ; la
  convergence vers $B1 tentée auparavant n'avait pas sauvé Closure (`CLOSURE_CHANTIER.md`,
  cycle 3). Le bouton Ultimate Ripper (`PortDevices::pressButton`) asserte désormais RI
  (bit 6 → 0) — comportement attendu du logiciel non vérifié. Validé : selftest MFP
  (pristine $F1/$F9), test S Field Service (ST_Diagnostic v4.4, `--machine st`, loopback)
  Pass, MegaSTE suite Q 11/11. ⚠ Non mesuré : bit 3 au repos AVEC blitter (0 supposé, INT =
  copie de BUSY) — cf. `TODO.md`.

---

## 🔁 2ᵉ passe d'audit approfondi (2026-06-15, workflow 7 agents)

Objectif : creuser **plus profond** que la 1ʳᵉ passe (lecture ligne-à-ligne des fonctions clés)
et **vérifier les 4 correctifs mergés**. Verdict global : fidélité **confirmée**, **aucune nouvelle
divergence HAUTE**, les 4 correctifs sont CORRECTS. La passe remonte surtout des cas-limites
**basses**, quelques **moyennes** nouvelles, et **2 bugs nets actionnables**.

### Vérification des correctifs mergés
- ✅ **BL1** (Blitter 65536) — CORRECT : X (bouclage 16 bits) et Y (`int 0→65536`) = 65536 exact, readback 16 bits fidèle, pas de débordement.
- ✅ **BL2** (rejet accès octet) — CORRECT en **écriture** (liste `$3A-$3D` exacte vs `Blitter_CheckAccess_Byte`) ; **lecture complétée le 2026-10-18** : miroir brut MMIO séparé (BL-R).
- ✅ **S1** (filtre LPF STF) — CORRECT (`applyLpfStf250` = `LowPassFilter`, activation `!STE` = `Config_IsMachineST`). Réserve : condition `nAudioFrequency≥40000` d'Hatari ignorée (sans impact à 48 kHz).
- ✅ **BU1** (miroir PSG) — CORRECT : décodage `addr&3`, plage close à `$FF88FF` (pas de chevauchement `$FF8900`), wait-states inclus.
- ✅ **MIDI** (no-purge + RDR persistant) — CORRECT, mais effet de bord **M-MIDI** (ci-dessous).

### 🔴 Nouveaux — BUGS NETS actionnables — ✅ CORRIGÉS
- **BUS-LEAK ✅ corrigé** *(basse-moyenne, bug réel)* — `Bus::write16`/`write32` branche blitter
  faisaient `return` **sans restaurer `ioAccessWidth_`** → après le 1ᵉʳ blit mot, les bus-errors
  d'accès **octet** ($FF9200/lightpen/FDC) restaient désarmées en permanence. **Fix :**
  `ioAccessWidth_ = saved` ajouté avant le `return` des deux branches blitter (`Bus.cpp`).
- **BL-GPIP3 ✅ corrigé** *(moyenne, bug réel)* — la ligne GPU_DONE (GPIP3) était posée à
  `finishTransfer` mais **jamais ré-armée haute** → blit-done « toujours vrai » dès le 2ᵉ blit.
  **Fix :** `start()` dé-asserte la ligne (`setBlitterLine(false)`) au (re)démarrage de chaque blit
  (`Blitter.cpp`), comme Hatari `Blitter_Start` (blitter.c:895) ; `finishTransfer` la rabaisse.

### 🟠 Nouveaux — MOYENNES (fidélité)
- **MFP ✅ porté (2026-09-02)** — ~~pas de dispatch des timers échus (`MFP_UpdateTimers`) avant
  lecture des registres d'IRQ~~ : `Mfp::updateTimers` → `Scheduler::runMfpTimersTo` en tête de
  `Mfp::read8`/`write8` (`Mfp.cpp:63-70, 324, 418`), `NEOST_MFP_UPDTIMERS` ON par défaut ;
  détail → l'entrée MFP de l'inventaire ci-dessous.
- **SON S3 ✅ corrigé (2026-07-07)** — gain LMC ½-amplitude : table DAC pleine + `outScale_=0.5` **sans le ×2** que Hatari
  met dans `left/right_gain` → le YM STE ressort **~6 dB trop bas** relativement quand le LMC est à
  plein volume ; ratio YM:DMA aussi décalé.
- **FDC ✅ porté (2026-08-25, `D-PSG`)** — ~~changement lecteur/face « **pull** » au lieu de
  « push »~~ : `Machine::setPortASink` appelle `Fdc::refreshDriveSide()` à chaque écriture du
  port A du PSG (`Machine.cpp:211-225` ≙ `psg.c:419-420` → `FDC_SetDriveSide`).
- **MIDI M-MIDI ✅ corrigé** — l'ACIA MIDI a désormais un `rdrf_` **distinct** de `!rx_.empty()`
  (`MidiAcia.cpp/.hpp`) : le master reset l'efface (SR → TDRE seul, conforme `ACIA_MasterReset`)
  **sans purger la file** → l'octet reste relisible via RDR, et RDRF retombe correctement. Le
  bouclage « M MIDI » reste fonctionnel (file vide au reset du diagnostic) — mais il faut
  désormais **brancher la fiche** (`--loopback`) : depuis le 2026-08-21 le câble OUT→IN est
  débranché par défaut, comme chez Hatari et sur un vrai ST (cf. CASE_STUDIES § Cubase Lite).

### 🟡 Nouveaux — BASSES (cas-limites/cosmétiques)
- **Vidéo** : filtre « écriture redondante » absent (freq/res rejouées même inchangées) ;
  ~~`$FF8260` bits 2-7 non forcés à 1 sur ST~~ ✅ **corrigé (2026-07-09)** + ~~alias shifter `$FF8261`
  non géré~~ ✅ **corrigé** (lecture) : `$FF8260`/`$FF8261` = port de `Video_Res_ReadByte`
  (video.c:5281) — STF/Mega ST forcent bits 2-7 à 1, STE à 0 ; `$8261` renvoyait 0xFF (void) → mode
  (`Shifter.cpp` read8). ✅ **Écriture `$8261` portée le 2026-10-20** (`Shifter::writeShifterRes`,
  port de `Video_WriteToShifterRes` video.c:1888-2006) : un `move.b` vers `$FF8261` change la
  résolution du SHIFTER seul (ni timings ni bordures Glue, ioMemTabST.c:71 / ioMemTabSTE.c:120) ;
  un `move.w` vers `$FF8260` donne l'octet haut aux deux, l'octet bas est ignoré (le Shifter ne
  lit que D8-D9) ; les lectures `$FF8260/61` rendent la valeur Shifter. **res 3** = Shifter arrêté :
  stop posé au cycle-ligne ≥ 64 (DE actif), retour ≥ 72 et ≤ 32 cycles après → lignes suivantes
  décalées de 0/4/8/12 px, compteur −0/−2/−4/−6 octets en fin de ligne (`vcStopInc_`) ; ligne
  dont toute la fenêtre DE est couverte par un arrêt = aire active noire, bordures intactes
  (extension NeoST, cf. § *Vidéo* ; avant le 2026-10-20 : ligne arrêtée au commit, ce qui
  noircissait la ligne précédente). glue-selftest +36 vérifs ;
  aucun logiciel réel connu ne l'exerce. ⚠ Incertitudes (commentaires du code) : noir franc vs
  couleur 0, lecture OCTET de `$FF8261` (alias complet conservé), trick sur STE (modèle STF
  appliqué, comme Hatari), persistance du décalage aux trames suivantes (TODO video.c:1915 —
  NeoST garde la trame courante, comme Hatari). Non modélisés : pixels noirs pendant un stop
  bref, résolution Shifter ≠ Glue (0/1/2) au rendu, géométrie de trame à res 3 via `$FF8260` ;
  chemin `PrevSize` partiel ; quirks démos `$FF8205/07/09` (E605/Tekila).
- **FDC** : `dmaResetFifo` ne remet pas `bufPos_`/`dmaBytesToTransfer_` ; recalcul densité superflu
  à chaque lecture statut ; borne parseur STX (chemin secteurs complet) ; pas d'`indexCheckUpdate`
  avant la commande.
- **Son** : ~~compteur bruit testé `>=` dans la garde 125 kHz (vs 250)~~ ✅ (5ᵉ passe, bc15a67) ;
  ~~pas de HPF sous-sonique sur le canal DMA~~ ✅ 2026-08-07 (HPF sur le MIX STE,
  `DmaSound::applyHpfStereo`, appelé `AudioMix.cpp:38`) ; ~~`mode_` non masqué `&0x8f` (relecture)~~ **déjà fait** (masqué à l'écriture,
  `DmaSound.cpp:589` → relecture masquée) ; ~~masque adresse DMA `$3fffff` non appliqué~~ ✅ déjà
  porté pour les profils RAM ≤4 Mo (`v & 0x3F` sur l'octet haut, `DmaSound.cpp:574, 577` ≙
  `DMA_MaskAddressHigh`).
- **Blitter** : ~~**BL-R**~~ ✅ (2026-10-18, miroir brut `ioRaw_`) read8 ne rejetait pas l'accès octet aux registres mot (rend la valeur vive
  vs IoMem rance) ; **BL-MST** ✖ faux positif (5ᵉ passe) `$FF8A3E/3F` dé-fauté sur Mega ST (void seulement sur STE — carte
  de bus-faults, non touchée).
- **Bus** : ~~wait-state 4 cyc des registres FDC/DMA non facturé~~ ✅ 2026-10-18 (`Fdc::registerWait`) ; ~~`$FF860E/0F` densité routé sur STE
  simple~~ ✅ 2026-10-18 (Mega STE seul) ; ~~`$FF8A3E/3F`→0x00 au lieu de 0xFF~~ ✅ **corrigé (2026-07-09)** : `Blitter::read8`/`write8`
  traitent `$3E/3F` en void (lit 0xFF, écritures ignorées ; ioMemTabSTE.c:199) ; ~~trou MMU STF
  bank0=128K/bank1=2048K non émulé~~ ✅ porté le 2026-08-13 (`mmuTranslate`).

### Conclusion
La 1ʳᵉ passe avait capté l'essentiel ; les 4 correctifs sont validés. **Corrigés à la 2ᵉ passe
(✅)** : BUS-LEAK, BL-GPIP3, MIDI M-MIDI — validés `glue-selftest` 19/0 + boots ST/STE/MegaSTE
pixel-identiques. **Différés** (validation impossible ici) : **S3 gain LMC** (audio non vérifiable
sans WAV oracle ni écoute — risque de déséquilibre/clip), ~~**FDC drive/side push**~~ → **CORRIGÉ le
2026-08-25 (`D-PSG`)** : ce n'était pas un raffinement cycle-exact mais un **chemin manquant**.
`Machine::setPortASink` appelle désormais `Fdc::refreshDriveSide()` à chaque écriture du port A
du PSG (port de `psg.c:419-420` → `FDC_SetDriveSide`). Sans lui, un programme qui écrivait sa
commande FDC AVANT de sélectionner le lecteur restait à `driveSel_ = -1` pour toujours (Stardust
STE : gel noir, `drv=-1`, INTRQ jamais levé). Après : `drv=0`, 374 294 lignes FDC au lieu de
4714, intro jouée, puis recherche de la disquette 2 dans le lecteur B — comme l'oracle.
**MFP UpdateTimers** : ✅ **PORTÉ le 2026-09-02** — le `runTo` ciblé sur les sources `TIMER_*`
prescrit ici est en place et actif par défaut (`Mfp::updateTimers` → `Scheduler::runMfpTimersTo`,
`Mfp.cpp:63-70`, appelé `Mfp.cpp:324, 418` ; verrou `NEOST_MFP_UPDTIMERS=0`). La conclusion
« NÉGATIVE / patch RETIRÉ » écrite le matin même était un artefact de mesure (prise avant la
correction de l'échéance ARRONDIE du compteur vivant) : une fois cette base saine, le port est
neutre sur `mfp_poll` (0 px) — cf. l'entrée MFP de l'inventaire ci-dessus. Ce qui reste vrai de l'ancien texte :
le motif de report « risque de réentrance » était bien CADUC (`Scheduler::runTo` est ré-entrant
depuis BL4, garde RAII `FiringGuard`), et un `syncTo` nu reste à proscrire — il réactiverait le
modèle **sync-driven RÉFUTÉ** (deadlock Enchanted Land, `Cpu68k.cpp`, `IMPLEMENTED.md`), à
~1590 dispatches mid-instruction par trame. Et les basses
**cycle-exactes** (vidéo) / niche (bus N2-N5, blitter BL-R/BL-MST). À reprendre quand l'oracle
Hatari headless (`extern/hatari/build/src/hatari`) est bâti.

---

## 🧩 3ᵉ passe — sous-systèmes périphériques (2026-06-15, workflow 5 agents)

Couvre les sous-systèmes **NON audités** par les passes 1-2 : **RTC RP5C15, ACSI/DMA HD,
GEMDOS HD, FPU MC68881, SCU**. Verdict : portages globalement **très fidèles** ; aucune des
divergences ne casse le boot. Trouvailles actionnables ci-dessous (terrain neuf → vrais bugs).

### 🔴 Actionnables (par sévérité)

> **Statut des corrections (2026-06-15).** ✅ **CORRIGÉS** (validés : build, `glue-selftest` 19/0,
> boots ST/STE/MegaSTE pixel-identiques, **FPU test ROM 9/9**) : **SCU reset** (`Scu::reset(cold)`
> + appels `Machine::reset/hardReset`) · **FPU propagation NaN** (renvoie l'opérande NaN quiété) ·
> **FPU SNaN→SNAN** (flag softfloat `flag_signaling` distinct → `EXC_SNAN`) · **FPU masques
> FPCR/FPSR** (`&0xFFF0` / `&0x0FFFFFF8`) · **FPU FSGLMUL** (entrées tronquées 24 bits) ·
> **ACSI INQUIRY `buf[4]`** (valeur fixe 31) · **GEMDOS `.`/`..` en sous-répertoire** (paramètre
> `subdir`) · **FPU FSCALE ∞/NaN** (NaN→propagation, ∞→OPERR+NaN, plus d'UB) · **FPU octet AEXC**
> (UNFL accumulé seulement si INEXACT ; INEX sur INEX2 OU OVFL) · **GEMDOS `only_invalid`** (passe
> « caractères invalides » séparée de la troncature, `?` ne matche que les caractères invalides) ·
> **FPU FMOVECR INEX2+arrondi** (table `fpp_cr` portée : `inex` + `rnd[4]` par mode RN/RZ/RM/RP,
> arme `EXC_INEX2`/`AEXC_INEX`). ⏸️ **DIFFÉRÉS** (table de données / plateforme / plomberie / non
> vérifiables headless) : ACSI délai IRQ post-transfert · FPU arrondi de précision
> FMOVE/FABS/FNEG (plomberie softfloat) · FPU packed decimal bit-exact.
> ✅ **Sortis de cette liste le 2026-09-02** : GEMDOS recomposition Unicode macOS (portée et
> gardée à deux niveaux) et les arrondis de conversion SORTANTE du FPU (L/W/B/S/D).
> ✅ **Sortis le 2026-10-18** : précision des unaires FPU et décimal empaqueté,
> comparés aux véritables fonctions Hatari compilées (détails ci-dessous).

- ✅ **SCU — jamais réinitialisé au reset** *(HAUTE — CORRIGÉ)* : `Scu::reset(bool cold)` existe
  (`Scu.hpp:87-92` : masques/états à 0, GPR1=0x01, GPR2 seulement à froid) et `Machine::reset()`
  comme `hardReset()` l'appellent (`Machine.hpp:103` et `:125`). *Avant :* aucun reset du SCU, donc
  `SysIntMask`/`VmeIntMask` **persistants** au reset doux (Hatari `SCU_Reset` les met à 0).
- ✅ **FPU — propagation des NaN perd le payload** *(ÉLEVÉE — CORRIGÉ)* : `propagateNaN` /
  `propagateNaN1` renvoient l'opérande NaN RÉEL (signe + payload) quiété, port du chemin
  SOFTFLOAT_68K (`SoftFloatX80.hpp:82-94` vs `softfloat-specialize.h:321-365`). *Avant :*
  default-NaN `0x7FFF8000…` systématique.
- ✅ **FPU — SNaN lève OPERR au lieu de SNAN** *(ÉLEVÉE — CORRIGÉ)* : softfloat lève désormais un
  `flag_signaling` distinct (`SoftFloatX80.hpp:83,89`) que `Fpu::sfFold` replie en
  `EXC_SNAN`/vecteur 54 (`Fpu.cpp:129`), `flag_invalid` restant sur OPERR/vecteur 52
  (`Fpu.cpp:130`, vs `fpp.c:88-89`).
- ✅ **ACSI — INQUIRY `buf[4]` erroné** *(MOYENNE — CORRIGÉ)* : `buf[4]` (« Additional Length »)
  garde la valeur fixe **31** de `inquiry_bytes[4]`, simplement recopiée (`Acsi.cpp:252-260` vs
  `hdc.c:218-246`). *Avant :* écrasé par `count()-5` (variable) → pilote HD trompé.
- ✅ **ACSI — taille au montage (2026-10-19)** : images dépassant
  UINT32_MAX secteurs refusées avant conversion au lieu d’une taille tronquée.
  Maximum NeoST : 2 Tio moins 512 octets ; limitation des compteurs actuels,
  sans revendication de limite du matériel. Slot UltraSatan conservé sans carte.
- ✅ **ACSI — borne signée grande capacité (2026-10-19)** : compte
  READ/WRITE comparé aux secteurs restants sans conversion signée. Début
  d’une image de plus de 1 Tio accessible ; fin stricte et compteurs inchangés.
  Banc sparse POSIX, saut explicite sous Windows ou FS non sparse.
- ✅ **ACSI — destination WRITE différé (2026-10-19)** : cible et secteur
  figés avant ouverture DMA, comme le FILE engagé par Hatari. Nouvelle
  sélection pendant le paquet suivant sans rediriger l’écriture vers une
  autre image ; phase conservée en sauvegarde v45. Anciens v44 refusés.
- ✅ **ACSI — délai d'IRQ post-DMA (2026-10-18)** : `acsiDmaTransfer` efface
  la source HDC puis programme `Scheduler::ACSI_IRQ` après 1 000 cycles CPU, comme
  `ACSI_TRANSFER_MIN_CYCLES` dans `hdc.c` : 1 000 cycles bus à 8 MHz, 500 sur MegaSTE
  à 16 MHz. Une bascule de vitesse conserve les cycles CPU restants. Les données et l'adresse DMA sont mises
  à jour immédiatement. L'acquittement des octets reste immédiat, y compris quand
  le dernier octet déclenche déjà le DMA : ce sont deux chemins distincts chez
  Hatari. Le reset annule l'échéance, le prochain DMA la remplace ; une nouvelle
  commande seule ne l'annule pas. Tests MMIO + Scheduler et restauration d'état.
  **Limite** : délai fixe de l'oracle, pas une mesure matérielle ni un test Idris OS.
- **GEMDOS — `Dsetpath` retire un masque de fichier final** *(DIVERGENCE VOLONTAIRE, 2026-07-08)* :
  le bureau TOS ouvre un dossier via `Dsetpath("C:\DOSSIER\*.*")` (chemin + masque). Hatari
  laisse `access("…/DOSSIER/*.*")` échouer → EPTHNF → alerte « Impossible de définir le dossier
  par défaut ». NeoST retire un dernier composant contenant `*`/`?` avant le test (Dsetpath ne
  définit qu'un RÉPERTOIRE ; vrai TOS ignore la partie fichier). Débloque entrer/écrire/exécuter
  dans les dossiers au bureau. Dossier inexistant reste -34. (`GemdosHd.cpp` gemChDir)
- ✅ **GEMDOS — points en sous-dossier (2026-10-19)** : `Fsfirst`/`Fsnext`
  exposent désormais `.` et `..` quand les attributs et le masque les incluent,
  comme Hatari/FAT ; noms DTA exacts, sans transformer `..` en `+.`. À la
  racine, aucune entrée point ; autres fichiers cachés `.nom` toujours filtrés.
  L’ancienne divergence volontaire de présentation est remplacée par le contrat
  de recherche attendu par les programmes GEMDOS.
- ✅ **GEMDOS — erreur de recherche (2026-10-19)** : `ENOENT` reste une entrée
  à sauter, mais les autres erreurs `stat` remontent en `EINTRN` ; elles ne sont
  plus confondues avec une fin normale `ENMFIL`.
- ✅ **GEMDOS — matching « caractères invalides » (`only_invalid`)** *(MOYENNE)* : `addPathComponent`
  fait désormais DEUX passes distinctes comme Hatari — troncature (`*`, `onlyInvalid=false`) puis
  caractères invalides (`+`→`?`, `onlyInvalid=true`) — et `fsfirst_match` ne fait matcher un `?`
  « invalide » qu'un caractère réellement invalide pour Atari (`filenameInvalidChar`, port de
  `Str_Filename_Invalid_Char`). Plus de risque d'ouvrir le mauvais fichier hôte.
  (`GemdosHd.cpp` vs `gemdos.c:1374-1396`)
- ✅ **GEMDOS — recomposition Unicode NFD→NFC macOS** *(MOYENNE, macOS — CORRIGÉ le 2026-09-02)* :
  `Str_DecomposedToPrecomposedUtf8` (str.c:726) porté en `neost::hostpath::precomposeUtf8`
  (`HostPath.cpp`), table des 53 couples reprise TELLE QUELLE, et branché sur les deux mêmes
  sites que Hatari — `matchHostDirEntry` (≙ gemdos.c:1201/1214) et le listing Fsfirst
  (≙ gemdos.c:3182). `determineMaxPartitions` n'en a pas besoin (Hatari n'y convertit pas
  non plus : seule la première lettre y est lue).
  **Le bug était RÉEL et il est mesuré** : sans la recomposition, un fichier créé avec les
  octets NFD (« cafe » + U+0301, ce que macOS rend à `readdir`) est INTROUVABLE quand on le
  cherche sous sa forme précomposée — vérifié par mutation sur APFS, qui préserve bien les
  octets tels qu'écrits (`gemdos-selftest` : 14 OK → 13 OK / 1 FAIL).
  ⚠ **Ce que le port apporte exactement** : ni NeoST ni Hatari ne convertissent le JEU DE
  CARACTÈRES sur ce chemin (comparaison octet à octet, « conversion charset off » des deux
  côtés) — le gain n'est donc pas un affichage correct des accents côté TOS, mais la
  COHÉRENCE macOS ↔ Linux : le même dossier hôte rend désormais les mêmes noms, de la même
  longueur, donc la même troncature 8.3 et le même aller-retour listage → ouverture.
  Gardé à DEUX niveaux : `selftest_logic` exerce la fonction pure (13 cas, dont l'accent en
  fin de chaîne, la combinaison hors table et la marque tronquée) depuis n'importe quelle
  plateforme — même discipline que `Style` pour les chemins Windows — et `gemdos-selftest`
  exerce le câblage de bout en bout avec un vrai fichier sur disque.
- ✅ **FPU — précision et décimal empaqueté (2026-10-18)** : FSGLMUL/FSGLDIV,
  FSCALE, FMOD et octet AEXC étaient déjà portés. FMOVE/FABS/FNEG appliquent
  maintenant la précision FPCR avec sa plage d'exposant ; FMOVECR garde l'exposant
  étendu et porte aussi les motifs `fpp_cr_undef`, ajustements et CC particuliers.
  Le décimal P utilise `SoftFloatDecimal.hpp`, sans libc flottante : conversion
  dans les deux sens, k ≤ 0, INEX1 entrant et INEX2 sortant. `completeInput`
  conserve les exceptions de conversion et recalcule leur priorité après l'opération.
  Bancs reproductibles : `tools/test_fpu_unary_oracle.py` (1 443 cas) et
  `tools/check_fmovecr_oracle.py` (2 048 cas), véritables fonctions Hatari compilées.
  `tools/test_fpu_decimal_oracle.py --sanitize` ajoute 161 348 contrôles
  décimaux (comparaisons au C original et gardes sur les encodages invalides),
  sans échec ni diagnostic UBSan.
  Les offsets $40–$7F de ce dernier exercent la fonction ROM par CIR ; le décodeur
  CPU d'Hatari les refuse en amont par F-line et n'est pas simulé par ce banc.
  **Garde volontaire** : sortie P d'un étendu non canonique → OPERR+NaN, pour
  éviter les décalages indéfinis du source décimal amont. Le k dynamique reste
  fixé à 17, le registre Dn étant inaccessible par le dialogue périphérique actuel.
- ✅ **FPU FINTRZ — exceptions d’arrondi (2026-10-18)** : le statut local qui force
  le mode vers zéro est maintenant fusionné dans le statut principal, avant FPSR
  et livraison CIR. INEX2 et SNAN ne sont plus perdus. Banc reproductible
  `tools/test_fpu_next_oracle.py --check-mutation` : 1156 cas FINT/FINTRZ,
  zéro échec ; 354 avec le défaut rétabli. Mini-ROM : groupes 53 et 54 dédiés.
- ✅ **FPU — masques FPCR/FPSR non appliqués** *(BASSE — CORRIGÉ)* : les bits réservés sont forcés
  à 0 au chargement depuis `<ea>` — `fpcr_ & 0xFFF0`, `fpsr_ & 0x0FFFFFF8` (`Fpu.cpp:590-591`,
  cf. Hatari `get_features`).

### Choix intentionnels / NeoST plus correct que Hatari (NE PAS « corriger »)
- **RTC TEST/RESET en écriture seule** — Ricoh EK-086-9908 p.7 réserve
  Eh/Fh à l’écriture. NeoST ne relit plus les commandes mémorisées : zéro
  selon MAME, sans affirmation sur les bits flottants du bus réel. Actions
  RESET conservées ; anciens octets d’archive gardés pour le layout v44.
- **RTC temps émulé** (cycles) vs `localtime()` hôte — déterminisme headless (assumé).
  Le rattrapage conserve désormais les durées au-delà de 90 000 secondes,
  sans boucle seconde par seconde ni changement de source temporelle.
- **RTC Mega-only** — le RP5C15 n'existe **physiquement que sur Mega** (NeoST > simplification Hatari
  qui l'expose sur toute ST/STE).
- **RTC banques et contrôles** — NeoST suit Ricoh EK-086-9908 p.6–7 :
  deux banques, alarmes indépendantes, RESET, ADJ, 12/24 h et compteur
  bissextile. Les chiffres sont conservés sans conversion implicite lors
  d’une écriture du sélecteur ; API DateTime en 24 h. Sonde TOS aux adresses
  2/3 conservée. Sorties CLKOUT/ALARM et modes d’inspection TEST non émulés.
  TIMER EN bas conserve une retenue du diviseur, délivrée à la reprise sans
  rattrapage multiple, avec phase sous-seconde continue (Ricoh p.7).
  Annulation par RESET du diviseur/ADJ déduite du reset des étages, non mesurée.
- **MIDI reprise BREAK** — phase issue du dernier changement CR0..1,
  selon le timer TX ACIA générique d’Hatari ; prochain front conservé après
  snapshot v38. Le backend MIDI d’Hatari est simplifié. Ceci ne porte pas
  les bits physiques. Les départs TDR ordinaires suivent désormais cette même
  phase CR ; changer le diviseur recadence les fronts TSR restants sans modifier
  le format ni les données mémorisés. Archive MIDI inchangée.
- **IKBD phase TX ordinaire (2026-10-19)** — même phase CR pour les départs
  /64 8N1, les reprises BREAK et les TDR suivants. Un changement de diviseur
  recale le TDR en attente ; un TSR engagé garde son stop mémorisé. Autres
  formats/diviseurs et phase SCI exacte toujours hors du modèle. Champs v39 inchangés.
- **SCU encodage bit IRQ1 logicielle** — NeoST utilise le bit 1 (correct) vs bit 0 chez Hatari.
- **FPU transcendantes** : ✅ calculées depuis le 2026-10-20 par le port de `softfloat_fpsp.c`
  (`src/io/SoftFloatFpsp.hpp`), bit à bit comme Hatari (1 050 564 comparaisons, 0 écart). Limites
  connues, communes aux deux : algorithmes FPSP du 68040 et non microcode CORDIC du 68881 (dernier
  bit non garanti sur silicium) ; « zéro » non normalisé après underflow en précision simple ou
  double (`3F81:0…0`, traité Z), probablement un vrai ±0 sur 68881. Socket vide → « not found » —
  correct/hors périmètre.

### Confirmé FIDÈLE 1:1 (scruté cette passe)
ACSI : réception paquet (A1/ICD/LUN), **toutes** les commandes SCSI (TUR/REQ SENSE/RD-WR 6-10/
INQUIRY/READ CAPACITY/MODE SENSE 00-04-3f/SEEK/FORMAT/SHIP/REPORT LUNS), MODE SENSE géométrie,
table de partitions DOS+Atari, incrément/masque adresse DMA, court-circuit FIFO. · GEMDOS : table
d'interception (23 opcodes), Fopen/Fcreate/Fread/Fwrite/Fseek/Fattrib/Fdatime/Frename/Fdelete,
Dcreate/Ddelete/Dsetpath/Dgetpath/Dfree, handles+aliasing, Pexec/LoadAndReloc/reloc, conversion
chemins, codes d'erreur, multi-partitions C..Z. · FPU : cœur softfloat add/sub/mul/div/sqrt/rem/mod/
roundToInt, vecteurs d'exception+priorités, prédicats FBcc/FScc, **valeurs** des constantes FMOVECR,
FCMP/FTST, octet quotient FMOD/FREM, FSAVE/FRESTORE (idle $1F18/null). · RTC : 16 registres, bank
AM/PM, mode, adresses impaires, init heure hôte. · SCU : gating IRQ (masques Sys/Vme), MFP6/SCC5 sur
VmeIntMask, niveaux 4/2/1, IRQ niveau 5 vectorisée, décodage adresses impaires.

**Bilan 3ᵉ passe** : 1 HAUTE (SCU reset) + 2 ÉLEVÉES FPU (NaN/SNaN) + ~8 moyennes — tous **bornés et
corrigeables sans oracle**, et **tous corrigés le jour même** (cf. le chapeau ci-dessus ; seuls
le délai d'IRQ ACSI et le décimal empaqueté sont portés le 2026-10-18, l'Unicode macOS est
corrigé le 2026-09-02, les arrondis de conversion sortante FPU aussi) (logique pure, pas de cycle-exactness). C'est la passe la plus productive
en correctifs actionnables, justifiant le ciblage des sous-systèmes vierges plutôt qu'un re-balayage.

---

## 🧷 4ᵉ passe — couches d'intégration (2026-06-15, workflow 2 agents)

Couvre les **dernières couches jamais auditées** : le **wrapper CPU/Moira** (`Cpu68k`, câblage
exceptions/IPL/bus-error/IACK/reset) et les **E/S** (joypads/paddles `StePads`, `JoystickInput`,
Centronics). Verdict : **très fidèles**. **2 divergences réelles — toutes deux corrigées** (SCC
No-Vector ; support imprimante Centronics) + des points cycle-exacts déjà cadrés.

> ⚠️ **Oracle Hatari non bâtissable ici** : SDL2 absent du conteneur → les divergences
> **cycle-exactes** (phase CPU↔faisceau, échantillonnage IPL au cycle, latence d'exception, jitter
> E-Clock à l'IACK) restent **bloquées dans cet environnement** ; elles nécessitent une session avec
> SDL2 pour construire `extern/hatari/build`. Déjà cadrées dans `TODO.md §beam-sync`.

### Trouvailles
- **CPU — SCC No-Vector → mauvais vecteur** ✅ **corrigé** *(moyenne)* : sur IACK niveau 5 avec WR9
  NV armé (`Scc::processIack()` = -1), NeoST renvoyait l'auto-vecteur **29** (`24+level`, $74) au
  lieu du vecteur **spurious 24** ($60) d'Hatari (`iack_cycle` : `vector<0 → 24`). La branche MFP
  était déjà conforme. **Fix** : `Cpu68k.cpp` renvoie 24 sur `v<0` pour le SCC. (Validé : build,
  glue 19/0, boots ST/STE/MegaSTE pixel-identiques.)
- **Centronics — support imprimante** ✅ **corrigé** *(moyenne)* : le `printerSink_` du PSG est
  désormais câblé (`Machine`) → sur chaque front de strobe, l'octet du port B est **capturé dans un
  fichier hôte** (`Machine::setPrinterFile`, option headless `--printer FILE`) et la ligne BUSY
  (GPIP0) est assertée bas, port fidèle de `psg.c:388-390` (`Printer_TransferByteTo` +
  `MFP_GPIP_Set_Line_Input LINE0 LOW`). Sans fichier imprimante : no-op (défaut inchangé). Validé :
  mini-ROM imprimant « NeoST\n » via le protocole Centronics → fichier capturé identique.
- **PSG port A/B en entrée (joysticks parallèles) absent** *(très basse)* : Hatari recompose R14
  bit5 / R15 depuis les joysticks « parallel port » à la lecture (`psg.c:289-312`) — périphérique de
  niche, non émulé par NeoST.

### Confirmé FIDÈLE 1:1
**CPU** : trame bus-error/address-error groupe 0, double-bus-fault→HALT (2ᵉ faute + SSP impair),
vectorisation IACK MFP niveau 6, auto-vecteurs VBL/HBL + clear pending, reset SSP/PC via overlay ROM,
gating IPL (priorité MFP>VBL>HBL ; SCU sur MegaSTE), STOP, wait-states PSG/MFP/ACIA/bus + E-Clock,
MegaSTE 16 MHz (créneau bus + cache). **E/S** : StePads (multiplexage $FF9202, boutons/directions
pads A/B, DIP MegaSTE 0xBF, paddles REALSTICK, lightpen, bus-errors octet/mot $FF9200-23),
JoystickInput (mapping, ports), suivi du front de strobe Centronics.

**Bilan 4ᵉ passe** : couche d'intégration **solide** ; SCC NV **corrigé** ; support imprimante
Centronics **ajouté** (capture `--printer` + BUSY). Le terrain LOGIQUE est désormais **épuisé** — il
ne reste que le **cycle-exact**, qui exige l'oracle (SDL2 absent ici).

---

## 🔬 5ᵉ passe — rafraîchissement complet des statuts (2026-07-07, 4 agents)

**Contexte.** Passe déclenchée après l'audit blitter du même jour : (1) mettre tous les statuts
en phase avec le code réel (le commit `bc15a67` du 2026-07-03, au titre trompeur « fix(cpu):
improve STOP handling », contient EN RÉALITÉ l'intégralité du bug hunt 39-findings : 23 fichiers,
+750/−193 — `seedResetVectors`, `gpipSetLine`, Timer B evt=0, `Fdc::reset(bool)`, `Shifter::reset`,
`Ikbd::bootRom`, `$FFFC01/03` void, `peek8/16`, `busFaultN` par banque, DmaSound wrap 2²⁴,
etc.) ; (2) préparer les deux chantiers suivants : **lignes raster transitoires de Super Hang-On**
(in-game) et **son YM « différent » en STE** (Rick Dangerous). 4 agents : vidéo/raster (priorité),
MFP, périphériques (FDC/son-statuts/bus/SCC/ACIA), son approfondi (cœur YM + chemin de rendu).

### Statuts corrigés par cette passe (le détail ↑ dans les sections historiques est PÉRIMÉ)

**Passés à ✅ corrigé (tous dans `bc15a67` sauf mention) :**
- **M1 fronts GPIP** — `gpipSetLine`/`gpipUpdateInterrupt` (`Mfp.hpp:547-560`, `Mfp.cpp:1101-1116`)
  = port exact de `MFP_GPIP_Set_Line_Input`/`_Update_Interrupt` (mfp.c:1143-1219) : DDR=entrée,
  GPIP^AER, front actif si `GPIP_new == AER`. Appelants convertis : Ikbd.cpp:1226-1227, MidiAcia.cpp:248-249
  (wire-OR ACIA ≙ `MFP_Main_Compute_GPIP_LINE_ACIA`), Fdc.cpp:1100, Blitter.cpp:285/415.
  L'écriture AER re-déclenche les fronts (Mfp.cpp:426-429 ≙ mfp.c:2787-2790).
- **Timer B event-count** — wrap uint8 0→255, garde `tbCounter_==0` supprimée (`Mfp.cpp:834-850`
  = `MFP_TimerB_EventCount` mfp.c:1297-1320), antidatage `firingDue()`.
- **FDC INTRQ** — plus de `raise(SRC_FDC)` doublé : `setFdcLine` seul (Fdc.cpp:1100).
- **$FF8264 STE** — relit la valeur brute écrite (`Shifter.cpp:1305` en lecture, `:1397-1399` à l'écriture ≙
  `Video_HorScroll_Read_8264`).
- **VoidRead 0x00 vs 0xFF** — l'entrée était mal cadrée : Hatari n'utilise `VoidRead_00` QUE sur
  `$FF820B/$FF8262-63/$FF8266-7F` STE (ioMemTabSTE.c:98,121,124) ; NeoST rend 0x00 exactement là
  (`Shifter.cpp:1317-1320`), 0xFF ailleurs — couverture exacte (commit 88ec84a).
- **read32/zone RAM void** — mécanisme `cpuDb` (`Bus.cpp:385-387`) ≙ `VoidMem_wget/lget` (la zone
  vide rend la dernière valeur du bus), latch `Cpu68k.cpp:496-499`.
- **Trou MMU STF bank 128K/2048K** — ⚠ l'entrée précédente (« commit 6df9432 ») était
  FAUSSE : ce hash n'existe pas et aucune révision de `Bus.cpp` n'a jamais porté le trou —
  seuls `mmuXlatSTF/STE` + `ConfToBank` l'étaient. RÉELLEMENT porté le 2026-08-13 (3ᵉ
  passe de bug hunt) : `mmuTranslate` rend void `$40000-$7FFFF` quand bank0 = 128 Ko et
  bank1 = 2 Mo (≙ `memory_map_Standard_RAM`, cpu/memory.c — quirk mesuré sur STF).
- **Son : bruit ≥/250 kHz** (incrément 125 kHz, comparaison 250 kHz, plancher per=1 retiré —
  `YM2149.cpp:187-190,248-258` = sound.c:1050-1058) et **`mode_ &= 0x8F`** (`DmaSound.cpp:589`).
- **Filtre « écriture redondante » freq/res** — PRÉSENT (`recordSyncWrite`, `VideoGlue.cpp:278-361`,
  persistant inter-trames ≙ `ShifterFrame.Freq/Res`) — l'entrée « absent » de la 2ᵉ passe est fausse.
- **V3 partiel : restart du compteur vidéo PORTÉ** (2026-07-02) — `restartVideoCounter`
  (`VideoCounter.cpp:177-180`, consommé au `beginFrame` `Shifter.cpp:302-304`) + événement `VC_RESTART`
  (`Machine.cpp:385-389,454-461`, ligne 310/260 cycle 56 STF / 60 STE, check freq live) ≙
  `Video_RestartVideoCounter` + HBL intermédiaire video.c:3262-3286.

**Reclassés FAUX POSITIFS (Hatari fait pareil — NE PAS « corriger ») :**
- **D4 6268/6250** — le `FDC_TRACK_BYTES_STANDARD` actif d'Hatari vaut 6268 (formule 6250 commentée).
- **BL-MST** — `IoMem_FixVoidAccessForMegaST` (ioMem.c:172-196) dé-faute `$FF8A3E/3F` sur Mega ST
  AUSSI : la carte de fautes NeoST est identique. Le résidu valeur/latch est **résolu** : `$3E/$3F`
  lisent 0xFF (`Blitter.cpp:134`) et les écritures y sont ignorées (`Blitter.cpp:159`), comme
  `IoMem_VoidRead`/`IoMem_VoidWrite`.
- **Cartouche vide 0xFF** — Hatari `memset 0xFF` (cart.c:117) = NeoST.
- **MIDI bits SR (OVRN/DCD/CTS/FE/PE)** — midi.c:39-41 ne les modélise pas non plus : parité exacte
  avec l'oracle.

### Divergences OUVERTES à jour (par sous-système, sévérité décroissante)

**Vidéo/Glue** — cœur STF = très élevée (glue-selftest 19/0, datations read −6 / write +2 / align
−25 actives par défaut et validées Cuddly 250/250, EL 40/40, spec512 10/10) :
- **[moyenne, systémique] Hybride WS1/WS3 ✅ TRANCHÉ : WS3 complet (2026-07-08)** — NeoST adopte
  le wakestate par défaut de l'oracle (`VIDEO_TIMING_DEFAULT = WS3`, video.c:624), qui partage
  avec le STE l'IRQ HBL à cpl. Implémentation (cf. `glue::` dans `VideoGlue.hpp`) : positions
  horizontales Glue **+1** (`kWsInc`, fenêtres de tricks + DE stockés + Remove*_Pos 503 + HSync
  −49/−9 + canal Hbl_Pos 512/508/224), **IRQ HBL à cpl** (`kHblOff` défaut 0), VBL 64 (déjà bon).
  Séparation clef découverte dans le code Hatari : `Video_CalculateAddress` (compteur $FF8205+),
  `spec512.c` et la copie écran utilisent les constantes **FIXES** `LINE_START/END_CYCLE_*`
  (video.h:91-95), HORS table wakestate — les ancres de rendu NeoST (56/376) restent donc fixes,
  les DE stockés (table WS) sont re-normalisés −inc au rendu, et les datations read −6 / write +2 /
  spec512 −25 sont **inchangées** (fidèles-théoriques, WS-indépendantes — aucune recalibration
  nécessaire). Idem Timer B par défaut (`Video_TimerB_GetDefaultPos` = constantes fixes → 400).
  `NEOST_WS=1..4` pour A/B (WS1 remet HBL cpl−4 + VBL 60). **Validé** : glue-selftest 19/19
  (stimulus LEFT_OFF ajusté @8 — l'ancien @6, 4 cyc d'écart impossible sur HW, retombait
  fidèlement dans la fenêtre de restauration WS3), étalons TOUS OK, boot STF 50 Hz tos102fr
  **0 px** vs oracle, menu Cuddly **pixel-identique à la baseline HEAD** (190/250 trames à 0 px
  vs oracle sur les mêmes trames — les 60 restantes = phase d'animation des drapeaux due à la
  latence de la touche fifo côté oracle, identique baseline), flicker spec512 inchangé (résidu
  838→839 préexistant, invariant WS), son STE : cloche bit-identique, fetch FIFO déplacé de
  +4 cyc avec le HBL (0,003 % de l'étalon DMA — voulu). Le terrain est prêt pour V1/V2 (les
  valeurs de la branche STE d'Hatari se porteront telles quelles).
- **[moyenne] V1 branche STE Glue ✅ PORTÉ (2026-07-08)** — table STE dans `glue::Timing`
  (Preload_Start 0/36/40 : le GST MCU teste les positions de PRELOAD du MMU ; Line_Set_Pal 56,
  HSync −52/−12, Remove 500, HBL à cpl, inc=0 — pas de wakestate) + phase 1 STE de
  `updateGlueState` (video.c:2444-2651) : fenêtres preload, restauration LEFT_OFF stricte `<` 4
  avec variante **LEFT_OFF_2_STE** pile à 4 (+20 octets, DE_start 16, écran −8 px,
  `BORDERBYTES_LEFT_2_STE`), ligne NO_DE freq dans (36,40], latch res **sans** le −1 STF
  (video.c:2224 « not the case for the STE GST MCU »). Phases 2/3 communes (elles lisent la
  table machine). **Validé** : selftest 22/22 sur STE (3 cas LEFT_OFF_2_STE ajoutés), étalons
  TOUS OK, chemin ST strictement inchangé (Cuddly-ST 0 px vs oracle) ; le menu Cuddly sur
  NeoST-STE **casse désormais comme le vrai STE** — couleurs faussées/hachures/scroller
  corrompu, **196/250 trames pixel-identiques à l'oracle Hatari-STE** (Δ=−10, zone mur+robot ;
  le reste = glissements de phase du clignotement bistable de la casse, sensible au cycle exact
  de la touche fifo — la « contrepartie » documentée du 2026-07-03 est résorbée : NeoST-STE
  n'est plus « trop propre »).
  **[moyenne] V2 tricks résolution ✅ PORTÉ (cœur, 2026-07-08)** — `updateGlueRes`
  (`VideoGlue.cpp`) = port de `Video_WriteToGlueRes` (video.c:1637-1753) : **overscan MED-RES**
  (LEFT_OFF + med@20/28/36 → `OVERSCAN_MED_RES` + champ décalage source bits 20-23 ; hi→med ≤20
  → `LEFT_OFF_MED` +26 o ; variante STE `LEFT_OFF_2_STE_MED` +20 o/−16 px), **stab med**
  (hi/med/lo@16 → retrait gauche low propre) et **scrolls « hardware » droite 13/9/5/1 px**
  (les deux familles de fenêtres, lo ≤32 après hi/med et lo ≤40 après hi/lo/med). Rendu
  **multi-résolution PAR LIGNE** (port `Video_StoreResolution`) : lignes med décodées en
  2 plans (2 px/cyc) au sein d'une trame basse rés, base de décodage décalée de (2−champ)
  OCTETS (⚠ pas en pixels : stride 186 ≢ 0 mod 4 → l'appariement des mots de plans dépend de
  l'origine octet), émission = MOYENNE des 2 px med par colonne (même réduction que l'oracle
  2×, les DEUX phases vérifiées), calage émission −4 px med (1er mot med 2 cyc après DE_start).
  **VALIDÉ : écran greetings No Cooper 0 px vs oracle Hatari** (étalon `nocooper_greetings`,
  RÉFÉRENCÉ SUR L'ORACLE, max_diff 0 — chaque ligne : hi@0/lo@12/med@20 + 60/50@376/384 + stab
  hi/lo@444/456) ; selftest +4 scénarios V2 (31 ST / 34 STE) ; étalons TOUS OK, spectrum/Cuddly
  inchangés. L'écran principal (`nocooper`) : nappe/logo/bordures déjà 0 px, texte scrollé 2 px/
  trame à phase de touche près — l'« écart 891 px » d'hier était un artefact de phase (mesuré :
  0 px à sa phase). **Restent hors périmètre, documentés** : hardscroll 4 px plein écran de
  Paulo Simoes (med@84 → lo@92-104, ajuste le pointeur vidéo par ligne — étalon requis), hacks
  « TEMP » Closure/DOLB (reniflage PC/opcode chez Hatari — non portables tels quels), ~~alias
  $FF8261~~ (porté le 2026-10-20), rendu med 640 natif en trame mixte (buffer 416 : réduction ×2 documentée).
- **[basse→moyenne] Rendu par-ligne daté à la frontière d'instruction** : `renderLine(y)` part
  APRÈS l'instruction qui enjambe DE_end (376) — une écriture palette dans `[376, 376+carry)` est
  vue par la ligne y (Hatari : y+1). Intermittent par nature (cf. candidats SHO).
- **[basse] ✅ corrigé le 2026-07-12** — ~~Seuil spec512 `kSpec512Threshold=512`~~ : seuil
  ramené à 1 comme Hatari (`Shifter.cpp:108`, test `Shifter.cpp:662` ≙ spec512.c:140-143) ;
  plus de bascule de CHEMIN de rendu trame à trame.
- **[basse] `lineSnap_` seulement sur trames à écritures freq/res** — une trame sans trick mais
  en course avec le faisceau est relue en fin de trame (Hatari copie TOUJOURS par-ligne).
- **[basses]** ~~$FF8260 lu sans `|0xFC` sur ST (video.c:5281-5298)~~ ✅ 2026-07-09 (`Shifter::read8`) ; quirks E605/Tekila
  $FF8205/07/09 pendant DE : exceptions par opcodes FIXME/partielles d’Hatari,
  pas un défaut matériel démontré dans NeoST (audit 2026-10-19), port différé
  jusqu’à reproduction E605/Tekila. VBlank et remise à ON au VSYNC
  portés le 2026-10-18 (video.c:3443-3487) ; ~~attribution replayGlue à longueur fixe hors
  `NEOST_LINELEN_ATTR`~~ ✅ **verrou ON par défaut depuis le 2026-09-01** (V3 clos,
  `ShifterInternal.hpp:31` ; historique : verrou dédié OFF depuis A16 le 2026-08-27, segfault
  `--glue-selftest` corrigé le 2026-08-28) ;
  ~~CyclesPerVBL±4 si la dernière ligne change de freq~~ **RETIRÉ le 2026-08-25, faux positif
  prouvé** (cf. V3) ;
  STOP réveil granularité 2 cyc (Moira) vs quantum 4 (Hatari) → phase E-clock d'IACK mod 4.

**MFP** — chaîne IRQ/timers/GPIP quasi 1:1 (NeoST fait même le spurious $60 que Hatari laisse en
TODO) :
- **[✅ PORTÉ le 2026-09-02 — et l'entrée qui disait « RÉFUTÉ » était FAUSSE, cf. ⚠ ci-dessous]
  `MFP_UpdateTimers` avant tout accès registre du MFP.**
  ⚠ **CORRECTION D'UN VERDICT.** Une première rédaction de cette entrée, le matin même,
  concluait « correctif prescrit RÉFUTÉ À LA MESURE, ne pas re-tenter » : le port faisait
  passer l'étalon `mfp_poll` de 80 à 88 px contre l'oracle. **Ce verdict était un artefact** :
  il était mesuré AVANT la correction de l'échéance arrondie de `readTimerData` (le vrai
  défaut de cet étalon, corrigé le même jour). Une fois cette base saine, le port est
  **neutre sur `mfp_poll` — 0 px contre l'oracle** — et il n'y avait donc jamais eu de
  raison de le rejeter. Leçon : juger un correctif contre une référence elle-même fausse
  produit un verdict faux, et un verdict faux écrit dans l'inventaire coûte plus cher
  qu'une case vide.
  **Ce que le port corrige, et Hatari le nomme** (mfp.c:135, entrée du 2022/01/27) :
  « *fix the game Super Hang On, where `bclr #0,$fffffa0f` to clear Timer B ISR sometimes
  happens at the same time that Timer C expires, which used the wrong ISR value and gave
  **flickering raster colors*** ». Sans le dispatch, l'écriture qui acquitte l'ISR travaille
  sur un ISR qui n'inclut pas encore l'expiration concurrente : l'acquittement porte sur la
  mauvaise valeur, une interruption raster est perdue ou rejouée, et la ligne sort avec la
  palette de sa voisine — des bandes de couleur pleine largeur, à hauteur arbitraire, par
  intermittence. C'est le symptôme rapporté par l'utilisateur sur ce jeu.
  **Portée mesurée** : 125 890 accès registre sur 1,4 M (≈ 9 %) trouvent effectivement un
  timer échu à servir — le chemin travaille en permanence, seule sa conséquence visible est
  rare. Verrou d'A/B : `NEOST_MFP_UPDTIMERS=0`.
  **Ce qui reste vrai de l'ancienne rédaction** : le dispatch doit être CIBLÉ sur les
  sources `TIMER_*` et surtout pas un `syncTo` nu, qui réactiverait le modèle sync-driven
  réfuté (deadlock Enchanted Land).

- **Les DEUX autres pistes « Super Hang-On » d'Hatari, instruites le 2026-09-02.** Le nom
  du jeu apparaît QUATRE fois dans les sources d'Hatari ; après `MFP_UpdateTimers`, les
  trois restantes ont été lues et confrontées à NeoST :
  - ✅ **Délai de 4 cycles sur la montée d'IRQ** (mfp.c:107, 2013/03/01, « fix […] Super
    Hang On, Super Monaco GP, Bolo ») — **DÉJÀ PORTÉ** : `kIrqDelayToCpu`, `irqTime_` et
    la source `Scheduler::MFP_IRQ` armée à `irqTime_ + 4`, avec le cas « délai déjà
    écoulé » (≙ `MFP_ProcessIRQ`, verrou `NEOST_MFP_EXACT` bit1).
  - ✅ **Bit pending posé DEUX FOIS avant l'IACK** (video.c:320, 2013/05/03, « correct fix
    for Super Monaco GP, Super Hang On […] ») — **DÉJÀ CORRECT PAR CONSTRUCTION** : NeoST
    modélise HBL/VBL par des BOOLÉENS (`hblPending`/`vblPending`, `Cpu68k.cpp:58-59`,
    commentés « ≙ pendingInterrupts = 0 »), effacés à l'acquittement et lus comme un
    NIVEAU pour l'IPL. Poser le bit deux fois s'y confond donc naturellement en une seule
    interruption — ce qu'Hatari obtient par un code dédié.
  - ✅ **Cycles d'écriture dans `MFP_IRQ_Time`** (mfp.c:113, 2013/03/14, « properly fix
    Super Hang On ») — **MANQUAIT, porté le 2026-09-02**. Hatari date une écriture MMIO à
    `currcycle + 4` (`Cycles_GetInternalCycleOnWriteAccess`), « the number of cycles when
    the write will be **completed** ». NeoST datait ses IRQ nées d'une écriture registre à
    `liveNow()`, or sa propre convention veut que la fin d'accès soit à **+2** (Moira a
    déjà facturé le SYNC de tête — cf. `Cpu68k::cyclesIntoInstr`, convention que le
    Shifter applique déjà à ses écritures palette). Les IRQ étaient donc antidatées de
    2 cycles, et comme la visibilité CPU court depuis cet instant (`irqTime_ + 4`),
    l'exception partait 2 cycles trop tôt. Corrigé (`Mfp::writeEventTime`, 9 sites).
    ⚠ **Aucun effet pixel DÉMONTRÉ à la valeur juste** : sur Super Hang-On en jeu, `+0` et
    `+2` rendent la même image. Ce qui EST démontré, c'est que le chemin pilote bien
    l'émulation — `NEOST_MFP_WRITE_END=40` déplace 24 773 px sur la même trame. C'est donc
    un port de FIDÉLITÉ, adopté sur la lettre d'Hatari et non sur une image, et c'est dit
    ainsi. ⚠ `mfp_poll` ne peut pas juger ce point : son programme masque les IRQ
    (`SR=$2700`), il est donc insensible même à un décalage de 40 cycles.

- **[historique] ~~L'écart de latence IPR/ISR~~** Ce qui était écrit
  ici depuis le 2026-08-25 (« un timer expirant PENDANT l'instruction qui polle est vu en retard,
  jusqu'à 157 cycles ») décrivait un état dépassé. Confronté à l'oracle sur l'étalon `mfp_poll`,
  bâti exprès pour l'exhiber : **IPRA est IDENTIQUE à Hatari sur les 100 lignes**. Le modèle BLOC
  préempte déjà le timeslice CPU à chaque échéance de timer (`Scheduler::schedule` → `endSlice_`),
  et BL4/D3 ont fermé le reste ; il ne restait plus rien à rattraper.
  ⚠ **Caduc — le port est EN PLACE** (cf. l'entrée ✅ ci-dessus, `Mfp.cpp:63-70, 324, 418`) ;
  la consigne qui suit est conservée comme trace du verdict erroné du matin du 2026-09-02.
  ~~**Ne pas re-tenter le port.**~~ `MFP_UpdateTimers` (dispatch ciblé des sources `TIMER_*` en tête
  de `Mfp::read8`/`write8`, à l'horloge live) a été ÉCRIT et MESURÉ le 2026-09-02 : il ne ferme
  rien et **dégrade l'étalon de 80 à 88 px** (il fait recharger le timer avant la lecture de TADR,
  ajoutant une ligne divergente). Balayage de l'instant de dispatch sur ±12 cycles : aucun offset
  n'atteint 0 px, et le meilleur (−12) ne fait que reproduire l'image NON corrigée. Le patch a été
  retiré. La borne de 157 cycles reste vraie comme MÉTRIQUE de `Scheduler::timerMaxLate` (elle est
  un maximum sur toute la trace, boot compris), mais elle ne mesure aucun écart de rendu.
- **[moyenne ✅ corrigé le 2026-09-02] Compteur vivant des timers lu sur l'échéance ARRONDIE** —
  le VRAI résidu de cet étalon, et il n'était écrit nulle part. `readTimerData` reconstruisait le
  compteur depuis l'échéance vue par le `Scheduler`, qui n'en est que le **plafond entier**
  (`scheduleTimerAt` : `next = (nextSub + 255) >> 8`) : le reste était surestimé de presque un
  cycle CPU, et comme le compte est un `ceil` (≙ `MFP_CYCLE_TO_REG`, mfp.c:393), TADR sortait **un
  cran trop haut** chaque fois que le reste tombait sur un multiple du prescaler. Mesuré : 6 lignes
  sur 100 (période 19), **toutes NeoST = Hatari+1**, 80 px / 114816. Hatari ne peut pas avoir le
  défaut — son `InterruptHandlers[].Cycles` EST la valeur fractionnaire (unités internes CPU<<8,
  `CYCINT_SHIFT`) et `CycInt_FindCyclesRemaining` la soustrait telle quelle de l'horloge live.
  **Correctif** : partir de l'échéance SOUS-CYCLIQUE que NeoST tenait déjà (`Mfp::timerDueSub_`,
  8 bits de fraction, au save-state depuis v11) et ne lâcher la fraction qu'à la conversion
  CPU→MFP. Résultat : **0 px contre l'oracle**, les 100 octets IPRA ET les 100 octets TADR
  identiques ; `mfp_poll` promu `ref_kind: snapshot` → **`oracle`** (le corpus oracle de la CI
  passe de 7 à 8). Garde vérifiée par mutation : revenir au plafond entier rend 80 px.
  ⚠ `storeStoppedCounter` a reçu le MÊME changement par cohérence (chez Hatari les deux chemins
  sont le seul `MFP_ReadTimer_AB/CD`, avec `TimerIsStopping`) — mais **aucun test ne le couvre** :
  le muter seul laisse l'étalon vert. C'est un port raisonné, pas une correction mesurée.
- **[moyenne-basse] ✅ corrigé (2026-08-14)** — conversion MFP→CPU et grille périodique
  conservées en unités ×256 comme `cycInt` : Timer C 200 Hz = 40106,238 cycles sans
  perdre la fraction à chaque recharge. Échéances `timerDueSub_` incluses au save-state v11.
- ✅ **AER bit3 mid-ligne corrigé (2026-10-19)** : anciens fronts échus servis
  avant mutation, puis recalage immédiat sur le prochain front strictement futur
  de la ligne courante, sinon suivante (`Video_AddInterruptTimerB`). Grille Glue,
  masquage vertical et dates IRQ conservés. Vérifié par banc synthétique 50/60/mono ;
  cela ne vaut pas validation du jeu Seven Gates of Jambala.
- **[basses]** GPIP0 BUSY est désormais corrigé via `gpipSetLine`
  (2026-10-19) ; XSINT respecte déjà DDR7 dans le périmètre revérifié. RS232 CTS/DCD/RI
  `raise()` direct (volontaire, fixture) ; ✅ **élection chronologique par lot (`MFP_UpdateNeeded`) — portée le 2026-09-02, lot
  inter-callbacks fermé le 2026-09-03 (A42).** La règle d'Hatari : une ENTRÉE d'interruption ne déclenche pas l'élection,
  elle pose son bit et sa date et marque « à faire » ; l'élection a lieu plus tard, une
  fois toutes les entrées reçues, pour « choisir la date de la PLUS ANCIENNE »
  (mfp.c:1084-1087). C'est ce qui rend opérante la règle `Pending_Time <= Pending_Time_Min`
  de `MFP_InterruptRequest` — sans elle, une entrée plus RÉCENTE mais plus prioritaire
  l'emporte, là où le 68901 sert la plus ancienne (Hatari 2013/04/21, « fix Fuzion CD
  Menus 77, 78, 84 »). NeoST élisait immédiatement à CHAQUE entrée : `pendingTimeMin_`
  était donc consommé avant l'arrivée de la suivante et ne pouvait jamais départager.
  **Ce qui est porté** : `Mfp::raiseAt` ne fait plus qu'armer `irqUpdateNeeded_` ;
  l'élection est faite par `flushIrqUpdate()`, appelé au calcul d'IPL
  (`neostUpdateIpl`) et en fin de `Mfp::updateTimers` (≙ mfp.c:689). Les paires d'entrées
  d'une MÊME fonction — `TXERR`→`TXEMPTY`, `RXERR`→`RXFULL` — sont désormais départagées
  correctement. Verrou d'A/B : `NEOST_MFP_BATCH=0`.
  ✅ **Fermé le 2026-09-03 (A42)** : `Scheduler::setDispatchHooks` borne le lot ; `Machine`
  suspend l'élection pendant le dispatch (`Mfp::setInDispatch`, garde `Mfp.cpp:1154`) et la
  tranche une fois en fin de lot avant de relire l'IPL (`Machine.cpp:250-262`, verrou
  `NEOST_IPL_BATCH=0`). Mesuré : 123 entrées groupées sur 1 000 123 (Super Hang-On).
  *Historique :* ~~CE QUI RESTE~~ : chez Hatari le flush est PAR INSTRUCTION (boucle
  CPU, newcpu.c:3005/5509) ; chez NeoST il est par CALLBACK, parce que `Machine` fait
  suivre chaque callback d'ordonnanceur d'un `cpu.updateIpl()` (18 sites). Deux timers MFP
  échus dans le MÊME `runTo` sont donc encore élus SÉPARÉMENT. Mesuré sur Super Hang-On :
  **0 groupement sur 1 000 000 d'entrées** — la fenêtre se referme aussitôt. Le port
  actuel ne change donc AUCUNE image (A/B à 0 px sur Super Hang-On, `mfp_poll`,
  `blitter_timer`, `trace_odd`) ; il rend la règle opérante là où elle peut l'être, sans
  prétendre couvrir le cas inter-callbacks. Le fermer demande de sortir `updateIpl()` des
  callbacks pour un unique appel en fin de dispatch — une refonte du pilotage de l'IPL,
  à faire séparément et avec son propre filet. **[volontaires]** jitter Lethal Xcess (hack PC codé en dur, résolu
  par le vrai beam-sync) et « PATCH TIMER D » (hack de performance inutile ici) non portés.

**Son** — générateurs YM = port exact (tons/bruit LFSR 17 bits/enveloppes/mixeur/masques/
read-latch/PWM/LPF C10/HPF/resampler 16.16 : tous vérifiés 1:1 contre sound.c/psg.c) :
- **[moyenne, audible] S3 gain LMC ×2 manquant ✅ corrigé (2026-07-07 soir)** — `kLmcMakeup = 2.0`
  appliqué aux gains LMC (`DmaSound.cpp:442-464`, ≙ ×2 de `left/right_gain` dmaSnd.c:1152-1153,
  1460-1461) et `kDmaGain` passé de 0,7 à 0,375 (= ¾ × ½). *Avant :* `outScale_=0.5` STE ×
  gains plafonnés à 1,0 → **YM STE −6 dB** vs Hatari-STE et vs NeoST-ST, ratio YM:DMA décalé
  (candidat n°1 « Rick Dangerous »). Validé : cloche GEM ST vs STE, ratio RMS **1.000**.
- **[moyenne] S4 table DAC ✅ corrigé (2026-07-07 soir)** — la table MESURÉE est le DÉFAUT, comme
  le `YM_TABLE_MIXING` d'Hatari (configuration.c:807) : mesures Paulo Simoes 16³ vendorisées
  (`ym2149_fixed_vol.h`) et interpolées en 32³ (port de `interpolate_volumetable`,
  sound.c:505-543), cf. `YM2149.cpp:89-159` (`dacTable`, interpolation `:113`). *Avant :* seul le modèle circuit
  (≙ `YM2149_BuildModelVolumeTable`, qu'Hatari ne prend qu'avec `--ym-mixing model`) → timbre et
  balance différents sur accords 2-3 voies, ST ET STE. Le modèle reste accessible par
  `NEOST_YM_MIXING=model` (A/B).
- **[moyenne] S2 FIFO 8 octets + avance HBL DMA ✅ corrigé (2026-07-07 soir)** — port complet du
  modèle Hatari : FIFO anneau 8 octets fetchée par MOTS (`fifoRefill` ≙ dmaSnd.c:342), entretenue
  à chaque HBL (`DmaSound::onHbl` appelé de `Machine::onHbl` ≙ `DmaSnd_STE_HBL_Update`
  video.c:3322), consommation DAC datée au reste fractionnaire (`updateDac` ≙ Sound_Update +
  frameCounter_float) qui **capture les octets au fetch** dans un anneau fixe consommé par le
  rendu audio (mixSegment/mix ne relisent PLUS la RAM en fin de trame — un programme qui modifie
  le tampon pendant la lecture est maintenant fidèle : cas Mental Hangover / Power Up Plus). Fin
  de trame détectée **au fetch** (XSINT/Timer A en avance ≤ 8 octets, quantifiée HBL, ≙ vrai HW) ;
  `Scheduler::DMASND` n'est plus armé. Compteur $FF8909+ = adresse de FETCH après synchronisation
  (≙ `DmaSnd_GetFrameCount`). Réalignement mono→stéréo (`fifoSetStereo` ≙ dmaSnd.c:419) + événement
  MODE daté pour la cadence du rendu. Piège débusqué au passage : la dette de rééchantillonnage du
  rendu hôte doit être PLAFONNÉE à 1 octet en sous-alimentation, sinon le rattrapage saute des
  octets en rafale (l'étalon montrait 21 % d'octets B au lieu de 33 %). **Validation** : étalon
  dédié `tools/make_dmasnd_test.py` (tampon modifié mid-trame par le handler VBL, fenêtre B
  transitoire jamais visible à la frontière de trame) → NeoST capture 33,3 % de mots B, oracle
  Hatari (bâti DANS ce conteneur, SDL2 2.30 présent — AVI + trace `--trace dmasound`) 33,2 % ;
  `NEOST_DMASND_TRACE=1` émet le refill au format Hatari pour diff direct. Étalons 19/0 + 8 OK,
  WAV cloche GEM / Rick Dangerous II bit-identiques (YM intact).
- **[basses]** ~~Horloge YM figée 250 000 Hz~~ **✅ 10ᵉ passe : 250 663 Hz** (= MCLK/2/2/4/8,
  MÊME MCLK ST et STE chez Hatari — le « 250 332 STE » de cette liste était erroné) ;
  ~~écritures YM rejouées au grain ~48 kHz vs frontière 250 kHz~~ ✅ **CORRIGÉ le
  2026-09-02, et ce n'était PAS un « jitter »** : `synthesizeFrame` datait chaque écriture
  registre en ÉCHANTILLONS HÔTE, donc toutes celles tombant dans le même échantillon
  étaient ÉCRASÉES sauf la dernière. Sur un flux lent c'est bien le jitter ≤ 21 µs décrit
  ici ; sur un DIGIDRUM c'est une **DÉCIMATION**. Mesuré sur Super Hang-On (rapport
  utilisateur « le PSG grésille ») : **897 instants sonores par trame — 44,9 kHz — dont
  350 seulement résolus par la grille hôte, soit 61 % du flux perdu**, d'où le repliement
  entendu. Les écritures sont désormais posées sur la grille **250 kHz**, comme Hatari
  (`Sound_Update` avant `Sound_WriteReg`, psg.c:346) : 561 instants résolus, et le
  rééchantillonneur pondéré filtre au lieu de décimer.
  **Vérifié à l'oracle** (spectre sur la même fenêtre de 4 s, AVI audio d'Hatari) :
  | | <1k | 1-3k | 3-6k | 6-10k | 10-16k |
  |---|---|---|---|---|---|
  | NeoST avant | 92,5 % | 3,1 % | 2,3 % | 1,4 % | **0,5 %** |
  | NeoST après | 93,5 % | 2,8 % | 2,0 % | 1,2 % | **0,3 %** |
  | Hatari | 93,8 % | 2,5 % | 2,0 % | 1,2 % | **0,3 %** |
  NeoST rejoint l'oracle exactement dans les bandes où tombait le repliement.
  Instrument conservé : `NEOST_YMEV_DIAG=1`. ⚠ Les 37,5 % d'écritures qui tombent encore
  dans un pas déjà occupé sont R9 et R10, du MÊME instant que le R8 qui les précède de 8 et
  28 cycles : les séparer synthétiserait des transitoires de 1-3,5 µs qui ne sont pas du
  signal mais l'ordre d'écriture du 68000 — et que le passe-bas C10 du STF efface. Ce n'est
  donc pas un reliquat à corriger ; ~~HPF appliqué au YM seul vs
  au MIX YM+DMA en STE~~ **✅ 10ᵉ passe : HPF déplacé sur le mix** (`applyHpfStereo/Mono`) ;
  `mixing≠1`+DMA arrêté ne mute pas le YM (volontaire, `DmaSound.hpp:59-60`) ;
  ~~signe DMA non inversé~~ **✅ 10ᵉ passe : `kDmaGain=−0.375`** ; garde
  `nAudioFrequency≥40000` du LPF STF non portée (sans effet à 48 kHz).

**FDC/DMA (actualisé le 2026-10-18)** — stall FIFO et ancrage anti-dérive
étaient déjà portés. Wait states ajoutés selon Hatari : 4 cycles CPU à l’écriture
$8604/$8606 et à la lecture $8604 hors SCREG ; aucune attente sur lecture $8606,
adresses. Le registre de densité Mega STE facture aussi 4 cycles CPU
(approximation Hatari restant à mesurer sur matériel). Accès octet fautif rejeté avant attente ; observation hors
exécution CPU sans effet d’horloge. La conversion Mega STE 16 MHz suit la source
Hatari (2 cycles bus), sans nouvelle mesure physique. Push PSG, reset FIFO et
UpdateAll sont portés ; restent notamment les choix STX D1/D2.

**Bus (actualisé le 2026-10-18)** — $FF860E/0F : densité active sur Mega STE
uniquement ; les accès autorisés sur les autres profils lisent FF et ignorent les
écritures. $FF8A3E/3F étaient déjà void FF ; régression ajoutée.

**Blitter** — à jour au 2026-07-07 (audit + port mid-word + fenêtre CPU 64 accès réels, § dédié
plus haut). ~~Exécution CPU parallèle pendant la tranche~~ ✅ 2026-10-19 (`Cpu68k.cpp:577-621`) ;
~~cache MegaSTE 16 MHz compté comme accès bus~~ faux positif (Hatari idem) ; RMW/TAS atomique
dans le modèle courant (BL5).

**ACIA/IKBD (actualisé le 2026-10-18)** — délais des interrogations horloge,
joystick, souris absolue, rapports de configuration et réponse Froggies portés.
L’attente est globale comme `SCI_TX_Delay`, quantifiée à 1024 cycles par bit ; les
plages aléatoires Hatari sont remplacées par leur valeur centrale pour conserver
le déterminisme. Une réponse en attente remplace le délai ; l’octet déjà en vol
termine sa transmission. Pause, tampon plein et état sauvegardé sont couverts.
La phase sous-octet du SCI n’est pas simulée ; reset $F1, autosend VBL, taux $17
et transit CPU→IKBD restent hors de ce lot.
~~[NOUVEAU, basse] MIDI : TDRE ne tombe jamais sans TIE (vs midi.c:271,188 —
un poll du SR MIDI voit un émetteur infiniment rapide)~~ ✅ corrigé le 2026-10-18 : TDR et TSR distincts, TDRE tombe à l'écriture TDR et remonte au chargement du TSR, indépendamment de TIE (`MidiAcia.cpp:134-143`).

**SCC** — inchangé depuis 2026-06-15 (SC1/SC1b tranchés).

### Chantier « lignes transitoires Super Hang-On » — candidats classés (agent vidéo + MFP)

> **✅ RÉSOLU (10ᵉ passe, 2026-08-06)** : aucun des candidats ci-dessous — la cause était
> l'**IACK MFP vectorisé 4 cyc trop court** (12 → 16, mesuré à l'oracle instrumenté).
> Cf. § 10ᵉ passe. La section est conservée pour l'historique de la méthode.

Contexte : per-HBL, handler « Timer B → table → `stop #$2100` → HBL écrit couleurs 2+3 » ;
bande d'horizon fixe déjà corrigée (STOP niveau-sensible). Les lignes brèves à position
aléatoire demandent soit un changement de chemin de rendu, soit une écriture attribuée à la
mauvaise ligne, soit un handler très en retard :
1. **Bascule du seuil spec512 trame à trame** (`kSpec512Threshold=512` ; SHO ≈ 420 accès
   palette/trame, juste sous le seuil) → le rendu ALTERNE par-ligne ↔ spec512 (palette roulante
   + relecture RAM). *Test : logguer `paletteAccesses_`/`spec512Active_` par trame pendant
   l'artefact ; forcer le seuil très haut et voir si ça disparaît.*
2. **Écriture palette dans `[376, 376+carry)`** (handler retardé par préemption Timer C/IKBD) →
   ligne y chez NeoST, y+1 chez Hatari. *Test : `NEOST_PAL_TRACE`, chercher cyc ∈ [340..512],
   corréler avec la position de l'artefact ; oracle `--trace video_color`.*
3. **HBL coalescé** (handler+préemption > 1 ligne → `hblPending` absorbe un HBL → le dégradé
   glisse d'une ligne jusqu'à la resynchro Timer B). Hatari a le même latch mais le LOGGE
   (video.c:3292-3305). *Test : compteur « pending déjà levé » dans raiseHbl vs log Hatari.*
4. **Amplificateurs** : phase de réveil STOP mod 2 vs mod 4 (E-clock IACK mod 10) ; HBL à 508
   (WS1) vs oracle 512 (WS3) — *A/B `NEOST_HBL_OFF=0` sur la repro in-game* ; dérive de
   troncature MFP→CPU (si un timer en mode délai est impliqué).
5. *(écartés)* : Timer B position/fenêtre (convergents), V2/LINELEN (SHO ne change pas de freq
   in-game — vérifier `syncWrites_` vide), MFP UpdateTimers (l'IRQ n'est pas affectée).

Oracle : repro in-game headless (DESKTOP.INF #Z + `--keys-at`/`--mouse-at`, cf. mémoire),
`NEOST_PAL_TRACE` par trame, `hatari --trace video_color,video_hbl` fastfdc des deux côtés,
diff des (ligne, cyc) des écritures couleurs 2/3 sur ~200 trames.

### Chantier « Rick Dangerous sonne différent en STE » — candidats classés (agent son)

**✅ RÉSOLU (2026-07-07 soir)** — les deux candidats principaux sont CORRIGÉS :
1. **S3** : `kLmcMakeup=2.0` (port du ×2 de dmaSnd.c:1152) + `kDmaGain` 0.7→0.375 — le YM STE
   ressort à pleine amplitude comme le ST (cloche GEM : ratio RMS STE/ST = **1.000** mesuré) et
   le DMA à ¾ exactement comme Hatari. 2. **S4** : table DAC mesurée par défaut
   (`src/core/ym2149_fixed_vol.h` vendorisé, port exact d'`interpolate_volumetable`), l'ancien
   modèle reste sous `NEOST_YM_MIXING=model` pour A/B. Outillage créé : **`--sound-dump F.wav`**
   headless (48 kHz stéréo s16, chaîne GUI complète YM+DMA+LMC) — validé keyclick/cloche GEM +
   musique Rick Dangerous II STE (profil RMS par seconde, pas d'écrêtage). Reste à comparer À
   L'OREILLE contre le vrai Hatari (machine STE des deux côtés !) — les [basses] (pitch −2,3
   cents, jitter 21 µs, HPF mix) sont documentées si un écart subsiste.

### État des mécanismes CPU « gated » (Cpu68k.cpp, référence rapide)

ON par défaut : `NEOST_LINELEN` (par-ligne, 2026-07-08), `NEOST_IACK` + `NEOST_IACK_AT` (E-clock + bloc au point d'IACK réel ≙
iack_cycle), **`NEOST_IACK_SYNC`** (dispatch des événements échus AU point d'IACK ≙ CycInt_Process,
2026-08-07 — rebase de quantum inclus), `NEOST_IACK_MFP=16` (bloc IACK MFP mesuré à l'oracle),
`NEOST_RAM_SLOT` (créneau bus RAM 4 cyc ≙ wait_cpu_cycle_*), `NEOST_RAISE_COMMIT=3`
(commit IPL au dispatch HBL+VBL), dispatch BLOC. OFF par défaut : `NEOST_RAISE_WINDOW`
(différé ipl_fetch à la frontière — mesuré : Hatari CE committe sans différé), `NEOST_ECLOCK_ON` (legacy),
`NEOST_IPLDELAY` (crude), `NEOST_IPLFETCH` (fidèle ipl_fetch_next dans Moira — candidat de
refonte non validé in-game), `NEOST_PIN_ARM` (réfuté), `NEOST_VC_WAIT` (redondant avec
RAM_SLOT), `NEOST_LINELEN=0` (désactive le canal par-ligne, **ON par défaut** depuis 2026-07-08),
`NEOST_V2` (ne gate plus que le raccourcissement HBL, `Machine.cpp:158-163`), `NEOST_SYNC_DISPATCH` (réfuté).
*Ajout 2026-10-20 (hors Cpu68k, ON par défaut)* : `NEOST_LINELEN_ATTR` (`ShifterInternal.hpp:31`,
depuis 2026-09-01), `NEOST_MFP_UPDTIMERS` (`Mfp.cpp:28`), `NEOST_MFP_BATCH` (`Mfp.cpp:60`),
`NEOST_IPL_BATCH` (`Machine.cpp:256`).

Sondes (pas des bascules — inertes si la variable n'est pas posée) : `NEOST_IACK_DISP=1`
(fréquence des IACK où un événement était réellement échu) et **`NEOST_QDELTA_DIAG=<seuil>`**
(sonde de non-régression de **BL3** : imprime, à chaque entrée de `Cpu68k::run`, l'écart
`busOfClock(horloge CPU) − sched.now()` s'il atteint `<seuil>`, plus un récap tous les
100000 runs). ⚠ Ce delta vaut **40** en régime normal — décalage de RESET (`Moira::reset`
lit SSP/PC avant que l'ordonnanceur ne démarre), CONSTANT, absorbé au 1ᵉʳ IACK, **sans
rapport avec le blitter**. Ce qu'on traque est un **escalier** (136, 272, … 1088 avant le
correctif BL3) : `=1` montre tout ce qui est non nul, `=100` ne montre que les blits.

**Bilan 5ᵉ passe** : sur 26 entrées re-vérifiées — 9 corrigées (5 par bc15a67), 4 faux positifs,
11 ouvertes (majorité basses), 2 assumées. Fidélité « très élevée » partout ; les deux fronts
utiles sont désormais **ciblés et testables** : raster SHO (candidats 1-3 + traces) et son STE
(S3/S4 + oracle WAV).

## 6ᵉ passe — bug hunt pré-release (2026-07-12, agents parallèles)

Audit ciblé des composants MODIFIÉS depuis la 5ᵉ passe (Ikbd, Mfp, DmaSound, ACSI,
MidiAcia, Fpu, Shifter) contre `extern/hatari/src`. **Corrigés dans la foulée** :

- **ACSI statut DMA inversé dans `writeAcsi`** (jumeau du bug d'`acsiDmaTransfer` corrigé
  au hunt précédent) : bit0 de $FF8606 rapportait « erreur » après chaque octet de
  commande accepté, y compris juste après un transfert réussi → `Fdc.cpp` (H1).
- **IKBD $0D** : le feu du joystick 1 compte désormais comme bouton droit quand la souris
  est active (même ligne physique, cf. `IKBD_DuplicateMouseFireButtons`) → `Ikbd.cpp` (M1).
- **FSCALE** : n extrait de l'étendu, exact jusqu'à ±131071 puis saturation −$6001/$E000
  (port exact de `floatx80_scale` ; l'ancien clamp ±32768 faussait la bande étroite
  |n| ∈ (32768, 131071]) → `Fpu.cpp` (M2).
- **±inf décodé S/D** : mantisse 0 (`floatx80_default_infinity_low`, forme canonique
  68881) au lieu de bit 63 posé → `Fpu.cpp` (M4).

**Ouvertes (basses, consignées sans correction)** :

⚠ Numérotation par **SÉVÉRITÉ** (`M` moyenne, `B` basse), indépendante des identifiants par
sous-système du tableau des priorités — cf. l'avertissement en tête de celui-ci. Le `B4`
ci-dessous est un point **Ikbd** ; le `BL4` du tableau des priorités est le Blitter.

| # | Où | Divergence |
|---|----|------------|
| M3 ✅ | `SoftFloatX80.hpp:189-197` | **CORRIGÉ 2026-07-31 (`7a9e03b`)** — `normalizeSubnormal` passé en convention 68881 (`−sc`, bit entier EXPLICITE) : c'est la branche `#ifdef SOFTFLOAT_68K` de `softfloat.c:1061-1065` qu'il fallait porter, pas celle du x87. Supprime le ×2 systématique sur FADD/FSUB/FMUL/FDIV/FSQRT/FREM/FMOD/FGETEXP/FGETMAN/FSCALE. Vérifié par fuzz différentiel contre `softfloat.c` d'Hatari : **0 écart** (plusieurs milliers avant). |
| B1 | `Ikbd.cpp` flush $80,$01 | avale l'octet « en vol » (Hatari : l'octet tiré vers TDR/TSR survit). |
| B2 | `Ikbd.cpp` bootRom | ne remet pas `mDeltaX_/Y_` (Hatari : Delta remis à 0). |
| B4 | `Ikbd.cpp` master reset TIE | Hatari force SR=TDRE immédiatement ; NeoST à l'échéance TX. |
| B6 ✅ | `Fdc.cpp` DMA ACSI hors plage | Statut READ ERROR et drapeau DMA corrigés sans modifier le sense disque. Clause ROM WRITE non applicable aux configurations supportées : RAM ≤4 Mo, masque DMA $3FFFFE. Sonde MMIO ST/STE/MegaSTE : 12 contrôles ASan/UBSan. |
| B7 | `Acsi.cpp` `dmaWrite_` | remis à false à chaque commande ; Hatari le fait persister jusqu'au transfert (refus de sens contradictoire). |
| B8 | `Fdc.cpp`/`Acsi.cpp` | sans image ACSI, Hatari ignore l'octet ($FF8604=0) ; NeoST pose status=2 (probe TOS identique). |
| B9 ✅ | `MidiAcia.hpp:84-101` | **CORRIGÉ 2026-07-31 (`e499eba`)** — `MidiAcia::reset()` ajouté et appelé par les resets CHAUD et FROID de `Machine`, comme `reset.c:111` (`ACIA_Reset`) + `:124` (`Midi_Reset`). |
| B10 | `DmaSound.cpp` $FF8922 | octet compagnon : Hatari lit 0 (réécrit à chaque shift) ; NeoST combine avec le dernier mot latché. |
| B11 | `Fpu.cpp` SNaN double | NeoST livre SNAN (plus fidèle au 68881 réel) ; Hatari lève OPERR à la conversion — assumé façon SC2. |
| B14 | `Shifter.cpp` | capture `lineSnap_`/comptage palette actifs en mono (Hatari : skip hi-res) — surcoût pur, rendu inchangé. |
| B15 ✅ | `Shifter.hpp` (`kVideoAddrMask`), `VideoCounter.cpp`, `Shifter.cpp` | **CORRIGÉ 2026-10-20** — compteur vidéo replié sur 22 bits (`kVideoAddrMask` = $3FFFFF, ST/Mega ST/STE/Mega STE, = `Video_GetAddrMask`/`DMA_MaskAddressHigh` $3F d'Hatari) à l'avance de ligne (`endVideoLine`), au restart, en lecture/écriture `$FF8205/07/09`, à la capture par ligne et sur chaque mot fetché (`decodeLineIndices`/`decodeWindowIndices`). glue-selftest « b15 ». |

Divergences **assumées** confirmées cette passe : symlinks suivis dans un montage GEMDOS
(même modèle de confiance que Hatari) ; VME/FPU MegaSTE « not found » correct.

## 7ᵉ passe — bug hunt post-perf (2026-08-06, 4 agents parallèles)

Chasse déclenchée après le commit de performance `65b1bb9` (chemins chauds inlinés + caches
MMU/Scheduler) et le port `MSA_WriteDisk` (`0d404cc`). Quatre agents : commit perf
(Bus/Scheduler/Shifter), FDC/STX/MSA, CPU/MFP/reset/save-state, Ikbd/DmaSound/YM/Shifter-rendu.
**Corrigés dans la foulée** (tous validés : selftests glue 36 / spec512 15 / bus 12 / mfp 16 /
msa 51, boots ST/STE/MegaSTE pixel-identiques, save-state déterministe) :

- **[MOYENNE] `Bus::read16` chemin rapide ROM — OOB hôte** : `off + 1 < rom.size()` débordait
  à `addr == romBase-1` (`off = 0xFFFFFFFF` → `off+1 = 0`, `0 < rom.size()` vrai → `rom[0xFFFFFFFF]`,
  lecture hôte ~4 Go hors tampon). Atteignable par un programme invité : `GemdosHd::trap` lit
  `read16` à une adresse prise dans la USP. Garde `off < rom.size()` ajoutée avant l'addition.
- **[MOYENNE] `Shifter` save-state — OOB pile scroll STE** : `hwScrollCount`/`newHwScrollCount_`
  restaurés sans borne (les 2 seuls champs oubliés par la campagne de durcissement). Compteur
  4 bits servant d'offset `idx[c + scroll]` dans `renderLine` (tampon 660 o) → un `.state` forgé
  (CRC valide) à `hwScrollCount=255` lit ~234 o hors pile. Gardes `ar.check` ajoutées.
- **[MOYENNE-BASSE] `Fdc::encodeMsa` — `.msa` 87 pistes non persistée** : borne `tracks > 86`
  asymétrique de `decodeMsa` (piste de fin ≤ 86, 0-based → 87 pistes). Une `.msa` de 87 pistes se
  montait inscriptible mais aucune sauvegarde n'était persistée (perdue au remontage). Borne
  rendue symétrique (`> 87`) + cas 87 pistes ajouté au `msa-selftest`.
- **[BASSE] `DmaSound::liveCounter`** : `fifoRefill()` de trop (≙ `DmaSnd_GetFrameCount` n'appelle
  que `Sound_Update`) → poll serré du compteur $FF8909 voyait le fetch avancer en continu au lieu
  de sauts HBL. **[BASSE] `DmaSound::startNewFrame`** : `phase_`/`haveCur_` remis à chaque relatch
  repeat au lieu du seul front PLAY 0→1 (≙ `DmaInitSample`) → click/dérive mono. **[BASSE]
  `Shifter::videoCounter`** : extrapolation créditant `line-offset+prefetch` aux lignes NO_DE,
  incohérente avec le commit `endVideoLine` (`bpl>0`) → compteur reculant entre 2 lectures $8205.
- **[BASSE] Écritures secteur `.st`/`.dim`** : `fstream::write` jamais vérifié (disque plein →
  secteur déchiré silencieux). Avertissement ajouté (le `.msa` est déjà atomique).
- **[BASSE, latents durcis] `Bus` cache MMU** : `machine` ajouté à la clé de revalidation (le
  décodage banque 1 en dépend). **`Scheduler`** : `schedule(kInactive)` traité comme cancel (ne
  corrompt plus `nextDue_`) ; `nextDue_` recalculé au chargement de save-state (comme `armed_`).

**Vérifiés FIDÈLES sans régression** : `MFP_Reset_All` (d410048), instruction RESET (7af3a7f),
STOP/liveNow + double-faute de bus (f755c76/6d45a5f), chemins DMA FDC/ACSI, parseur STX (OOB
« latent » fermé, cf. ligne ~180), Ikbd (table commandes + souris + handlers 6301 = port 1:1),
YM2149 (générateurs + table DAC mesurée), et les chemins rapides read8/write8/busFaultN du commit
perf (whitelist bus-error préservée, `write8` rapide ≡ lent sur le préfixe identité).

## 8ᵉ passe — workflow (6 chasseurs + vérif adversariale, 2026-08-06)

Orchestration multi-agents : 6 agents chasseurs (GEMDOS/sécurité, Bus/MMIO, Blitter, FPU, série
ACIA/SCC, CPU/save-state) → dédup → **réfutation adversariale** de chaque finding par un second
agent. 7 findings uniques, 5 CONFIRMÉS (2 faux positifs écartés : Bus/MMIO et un doublon).
**Corrigés** (mêmes validations que la 7ᵉ passe + montage GEMDOS symlinké/direct) :

- **[MOYENNE] GEMDOS montage symlinké** — `hdEmuDir` restait LEXICAL (`makeAbsoluteName`) alors
  que tout `host` produit par `clampToSandbox` est `physicalCanon()` (realpath, liens résolus).
  Sur un montage traversant un lien symbolique (macOS `/tmp→/private/tmp`, `~`/`​/var` sous Linux)
  les deux espaces de noms divergeaient : `gemChDir` (`:861`) rejetait TOUT `Dsetpath` en EPTHNF ;
  `gemSFirst("C:\")` (`:1221`) calculait `rootLen` sur la longueur brute → scan du dossier PARENT
  du bac à sable. Fix racine : `physicalCanon(hdEmuDir)` une fois au montage (`initDrives`, dossier
  confirmé existant) → toutes les comparaisons `host↔hdEmuDir` partagent l'espace canonique.
  Idempotent ; montage normal inchangé.
- **[BASSE, save-state — trous résiduels de durcissement] `Shifter::glueLineStart_`** : restauré
  sans check de taille, indexé sans garde de lecture dans `liveGlueCatchUp` (ex-`advanceGlueLive`, `VideoGlue.cpp:116`, `:123`, `:160`) → lecture hors-tas d'un `int64_t` (invariant `== glueLines_.size()` ajouté).
  **`Machine::curLineLen_`** : consommé par `advanceLine` (`lineCarry_ += cpl_ - curLineLen_`) →
  forgé, désancre `lineCarry_` après son propre check (borné `]0,4096]`). **`CpuState::cpuMul`** (il s'appelait g_cpuMul avant le regroupement d'état d'A33) :
  multiplie l'horloge Moira (`addBusWaitCycles`) → forgé énorme = bond d'horloge (validé ∈ {1,2}).

**Vérifiés FIDÈLES (adversarial)** : Bus/MMIO whitelist bus-error, Blitter (données + arbitration),
FPU (NaN/exceptions/décodage), ACIA/SCC série.

## 9ᵉ passe — workflow zones peu couvertes (2026-08-06)

6 chasseurs sur les zones les moins auditées (balayage save-state exhaustif, Shifter tricks/
spec512, STX en angle image malformée, IKBD 6301, audio/WASM lock-free, config/E-S fichier) +
réfutation adversariale. 6 findings uniques, **2 CONFIRMÉS** (tous deux save-state), 4 faux
positifs écartés. **Corrigés** :

- **[BASSE] Enums IKBD `joyMode_`/`customRead_`/`customWrite_`** : sérialisés par `ar()` brut →
  un `.state` forgé les matérialise hors domaine → UB (comparaisons/switch de dispatch, UBSan
  enum). Même classe que `mouseMode_` déjà durci. `customWrite_` avait un `ar.check` mais qui
  lisait l'enum déjà hors-domaine. Fix : transit par `std::underlying_type_t` + validation avant
  cast (Ikbd.hpp). Format de save-state inchangé.
- **[BASSE] `Bus::cart` save-state** : `ar.vec(cart)` sans le plafond 128 Ko de `loadCart`.
  `read8Slow` décode la cartouche avant le MMIO (`Bus.cpp:369`, cartouche `:435-436`) → un cart forgé > 128 Ko
  masque les registres `$FF8xxx`. Garde `ar.check(cart.size() <= CART_END-CART_BASE)` ajoutée.

**Vérifiés FIDÈLES (adversarial, 4 faux positifs)** : Shifter tricks/spec512 (rendu multi-rés,
palette roulante), parseur STX (chemins malformés — OOB « simple tronqué » toujours fermé),
audio lock-free/WASM (ring, points d'entrée JS), config/E-S fichier (parsing .cfg/ROM/disque).

**Bilan des 3 passes de la session (7ᵉ-9ᵉ)** : 17 correctifs — 3 moyens à impact réel (OOB hôte
`read16`, OOB pile save-state Shifter, franchissement/blocage GEMDOS symlink), 1 perte de données
(`.msa` 87 pistes), le reste en fidélité et durcissement save-state (9 champs/enums bornés au
total). Aucune valeur émulée ne change sur les chemins normaux ; selftests + boots pixel-identiques
à chaque étape. Le terrain save-state a été balayé exhaustivement ; les faux positifs en hausse
(4/6 à la 9ᵉ passe) signalent un rendement décroissant — la fidélité est très élevée.

## 10ᵉ passe — cycle-exact ciblée à l'oracle instrumenté : raster SHO + son STE (2026-08-06/07)

### Chantier « lignes transitoires Super Hang-On » — ✅ CAUSE TROUVÉE ET CORRIGÉE

**Cause racine : l'IACK MFP vectorisé était 4 cycles trop court** (`g_iackMfp` 12 → **16**,
`Cpu68k.cpp`). Aucun des candidats classés de la 5ᵉ passe (seuil spec512, attribution ligne
376+carry, HBL coalescé, troncature MFP) n'était la cause.

**Banc forgé (réutilisable)** : repro in-game SHO headless — script souris daté (`--mouse-at
1300`, clics titre, tenue accélérateur 700 trames/pas de classe, nitro = token `3` = 2 boutons,
SPACE ×4 pour « no music », accélérateur tenu en course) → course AFRICA déterministe,
~2 500 trames tracées `NEOST_PAL_TRACE_ALL=1` (nouveau : mode cumulatif « frame N » de
NEOST_PAL_TRACE, `Shifter.cpp`). Oracle : `--cmd-fifo` temps réel + événements **`leftdown`/
`leftup` AJOUTÉS à `control.c`** + `--trace video_color` en course + **[HEXC] étendu**
(position vidéo `hbl=`/`lc=` + `pc=`, vecteurs MFP inclus — `newcpu.c`, gate
`NEOST_HAT_IPLDIAG`). Diff : `compare.py` (scratchpad) — activations (paires idx 2+3, pc 1b30)
par ordinal.

**Mécanique élucidée (jeu, per-HBL)** : Timer B (fin de DE, ligne n−1) interrompt la boucle
principale à lc 404-416 → handler `$1ad6` : push ×3, `clr.b $fffffa1b`, lit `$FF8244.l`
(pc 1ae2), **`stop #$2100`** ($1ae8) → l'HBL de la ligne n, PENDANTE depuis lc 0 (masquée
IPL 6 pendant le handler), est prise À LA FRONTIÈRE DU STOP (cas niveau-sensible, pc0=$1aec)
→ handler HBL `$1b30` écrit couleurs 2+3 (1re écriture de paire, lc ~104-124) ; le stop
suivant ($1b10) est réveillé par l'HBL de la ligne n+1 à lc 0 (2e écriture, lc 68-76).

**Mesures (oracle instrumenté, 2 runs in-game ~20 000 exceptions)** :
- réveil STOP : **EXACT avant comme après** — écritures {68 : 40 %, 72 : 40 %, 76 : 20 %}
  identiques au point près des deux côtés ; exceptions à lc 0 pile.
- frontières d'entrée Timer B : identiques (lc {404, 408, 412, 416} aux mêmes poids) → le
  modèle « raise+commit à la frontière, sans différé ipl_fetch » est CONFIRMÉ fidèle
  (`NEOST_RAISE_WINDOW`, nouveau mécanisme opt-in de différé, mesuré : K>0 SUR-diffère —
  défaut 0).
- chaîne FIXE « exception TB → handler → stop → prise HBL » : **Hatari 144 cyc, NeoST 140**
  → +4 sur l'IACK (le `CPU_IACK_CYCLES_MFP_CE=12` d'Hatari — « not measured » — ne couvre
  pas le cycle bus d'IACK lui-même).
- après correction : histogramme 1re écriture {104 : 16,7/16,0 ; 108 : 24,5/25,9 ;
  112 : 23,9/23,8 ; 116 : 18,8/17,7 ; 120 : 10,4/10,8 ; 124 : 2,4/1,8} (NeoST/Hatari) —
  **verrouillé à ±1 pt partout** sur du code in-game vivant.

Effet de bord assumé : le boot glisse d'une trame (Timer C ×milliers d'IACK) → étalon
`nocooper` RECALÉ 6801→6802 par la méthode documentée dans sa note (0 px bit-identique à
l'oracle à la trame voisine, rendu intact). Étalons full tier verts.

### Son STE — restes [basses] portés (chaîne de sortie, ✅)

1. **Signe DMA ×−1** (`kDmaGain = −0.375`) : le LMC1992 inverse le canal DMA
   (dmaSnd.c:520-535) — phase relative YM↔DMA désormais fidèle.
2. **HPF sous-sonique déplacé sur le MIX en STE** : ST = HPF dans la chaîne YM
   (sound.c:1744) ; STE = YM **brut** dans le mix (`YM2149::setHpfBypass`), HPF appliqué au
   mélange YM+DMA (DC du DMA compris) dans `DmaSound::applyHpfStereo/Mono`
   (≙ dmaSnd.c:699,706), branché GUI + `--sound-dump` + WASM. États non sérialisés
   (format save-state inchangé).
3. **Correcteur LMC1992 en plateaux 1er ordre Savinkoff** : port exact de
   `DmaSnd_Bass_Shelf`/`DmaSnd_Treble_Shelf` (dmaSnd.c:1366-1404), coupures mesurées
   **118.2763 / 8438.756 Hz** — remplace le modèle RBJ 2e ordre 200/8000 Hz (audible dès que
   basses/aigus ≠ 0 dB). À 0 dB : identité exacte (bypass conservé).
4. **Horloge YM réelle 250 663 Hz** (`YM_250_HZ`) = MCLK 32084988 ÷2÷2÷4÷8 — Hatari utilise
   le MÊME MCLK sur ST et STE (clocks_timings.c ; l'ancien « 250 332 STE » de la 5ᵉ passe
   était erroné). L'ancien 250 000 rond jouait ~4,6 cents trop bas partout.

Validation : selftests 39/15/12/16/51, tier full vert, étalon `make_dmasnd_test` : fenêtre B
mid-trame toujours capturée (l'écrêtage sur ce signal synthétique extrême est FIDÈLE — Hatari
écrête pareil après son HPF de mix, dmaSnd.c:700-711). B10 ($FF8922) reste consignée ouverte.

### Bug hunt de la 10ᵉ passe (workflow 6 chasseurs + vérif adversariale, 2026-08-07)

16 trouvailles brutes (3 doublons inter-chasseurs), **12 corrigées le jour même** :

- **[MOYENNE] `kDmaGain` −0.375 → −0.1875** : le ÷4 d'Hatari (dmaSnd.c:520-535) couvre la
  demi-table STE **ET** la pré-compensation du ×2 `kLmcMakeup` — appliqué ensuite au mix
  ENTIER, DMA compris. L'ancien module ne retranchait que la demi-table : **canal DMA +6 dB
  vs Hatari** (0.744 fs au lieu de 0.372 à 0 dB LMC), écrêtage accru sur mix riche. La
  « validation S3 ratio DMA 0.75 » de la 5ᵉ passe mesurait NeoST contre sa propre lecture
  (erronée) du commentaire « 3/4 level », pas contre un WAV oracle.
- **[MOYENNE] `Machine::reconfigure` ne reposait pas `setHpfBypass`** : bascule STE→ST à
  chaud = YM sans recentrage DC (offset permanent, clics) ; ST→STE = YM filtré deux fois.
- **[MOYENNE] WASM : chaîne LMC non gatée par la machine** (le fix natif « gate LMC/ST »
  n'avait pas été porté au web) : sur `?machine=st`, gain ×2 sur un YM déjà pleine échelle
  → +6 dB + écrêtage. Gate `machineHasDmaSound` posé (HPF + gains + tonalité).
- **[MOYENNE] WASM : `adjustMachineForTos` absent** (boot ET menu ROM du shell) : un TOS
  ≤ 1.04 sur STE/MegaSTE haltait le CPU, écran figé sans message. Garde posée aux deux
  endroits (reconfigure du profil comme GUI/headless).
- **[MOYENNE] save-state : bit `LOOPING` de Moira forgeable** → `(this->*loop[ird])` sur
  pointeur-membre NUL en Release (l'assert saute). `ar.check` ajouté (jamais posé sur 68000).
- **[MOYENNE] `renderGlueFrame` appliquait le scroll fin STE de FIN de trame à toutes les
  lignes** (échantillonné une fois) : un split $FF8264/65 mi-trame + une seule écriture
  palette (seuil spec512 = 1) re-rendait la moitié haute avec le mauvais décalage. Corrigé
  par capture PAR LIGNE (`lineScrollSnap_`, même datation que `lineSnap_`) → **save-state v9**.
- **[BASSE] $FFFA31-$FFFA3F (impairs)** : void chez Hatari (ioMemTabST.c:143-150) — NeoST
  en faisait 8 octets de RAM relisible AVEC wait-state 4 cyc. Lecture 0xFF, écriture
  absorbée, sans wait (le dernier registre câblé est l'UDR $FFFA2F).
- ✅ **MMIO requalifié (2026-10-19)** : pairs MFP fautifs en byte, filler $FF
  en mot/long partiel et impairs $31–$3F void déjà traités dans le Bus. Ombres
  PSG impaires byte/movep déjà actives, impairs mot/long ignorés. Les anciens
  candidats 23/26 reposaient sur une lecture de la puce sans son routage Bus.
- ✅ **Snapshot scratch DMA son (2026-10-19)** : $FF8920 désormais sérialisé
  en v42 ; un état non nul retrouve sa valeur dans la même instance ou une
  instance neuve. Reset composant chaud/froid conserve l’octet, comme avant.
- **[BASSE] $FF8900 octet pair lit 0x00** (registre MOT, ioMemTabSTE.c:142) et **$FF8920 =
  octet scratch relisible** (non intercepté chez Hatari) — l'ancien 0xFF figé faisait
  échouer un `move.w $FF8900,d0` de diagnostic.
- **[BASSE] `NEOST_PAL_TRACE(_ALL)` : 1re ouverture du run en "w"** — un fichier survivant
  concaténait les sessions (« frame 0.. » dupliqués, diff désynchronisé).
- **[BASSE] headless : `--load-state` écrasait `--joy`** (hostJoy_/stePads restaurés) —
  le joystick est re-posé après restauration, avec log.
- **[BASSE] `parseMachine`/`parseRamBytes` : valeur inconnue → défaut ANNONCÉ** sur stderr
  (« mega-ste » silencieusement remplacé par STE faisait diffier contre le mauvais profil).

- **[BASSE→MOYENNE] ✅ Dispatch des événements échus AU point d'IACK** (`NEOST_IACK_SYNC`,
  défaut ON) — port du « `CycInt_Process()` + `MFP_UpdateIRQ_All` juste avant la séquence
  d'IACK » d'Hatari (newcpu.c:2938-2946 branche MFP, :2998-3003 branche vidéo). En mode bloc,
  les SYNC de l'entrée d'exception ne dispatchaient rien : un timer dont l'échéance tombait
  dans la fenêtre « frontière d'instruction → IACK » (~10-26 cyc) n'avait pas posé son bit IPR
  quand `Mfp::iack` élisait le vecteur. **Mesuré : un événement est réellement échu à 5,7-7 %
  des IACK** (banc SHO in-game, `NEOST_IACK_DISP=1`) — deux ordres de grandeur au-dessus de
  l'estimation « ~26/40106 » du rapport de chasse, qui ne considérait que le Timer C.
  ⚠ **PIÈGE (régression attrapée par le banc)** : le `syncTo` doit être précédé du **rebase du
  quantum** (`Cpu68k::rebaseQuantumAndSync`), exactement comme le saut d'attente STOP — sinon
  le temps couru depuis le début du quantum est facturé une DEUXIÈME fois par le
  `runTo(now + ran)` de `Machine`, et tout le raster glisse de ~16 cycles (mesuré : écritures
  palette {104..128} → {120..156}, réveil STOP {68,72,76} → {80..96}). C'est le troisième
  incident de cette classe : **tout dispatch déclenché depuis un hook Moira doit rebaser**.
  Validation : banc SHO inchangé (±0,1 pt vs oracle, réveil STOP 40/40/20), A/B `NEOST_IACK_SYNC`
  = comportement bien modifié (un handler démarre une ligne plus tôt), tier full vert.

**Vérifiés/écartés** : rien d'autre — 0 réfutation sur 13 uniques, taux inhabituel signalant
des chasseurs bien ciblés (code frais de la session) plus que des vérificateurs laxistes :
chaque correctif ci-dessus a été RE-vérifié à la main contre les sources Hatari avant d'être
appliqué.

## 11ᵉ passe — deux résidus MESURÉS sur les étalons à disque généré (2026-08-19), TRANCHÉS le 2026-08-29 : un artefact de mesure, un écart réel

Découverts en migrant les étalons vers EmuTOS (découplage juridique) : leurs références
sont des **self-captures** (`ref_kind: snapshot`), donc elles gardaient la non-régression
de NeoST contre lui-même **sans jamais confronter ces étalons à l'oracle**. Confrontation
faite (Hatari v2.6.1-devel bâti le jour même, même disque, même ROM, même trame) :

| Étalon | Écart annoncé (2026-08-19) | Verdict (2026-08-29) |
|--------|-----------------------------|----------------------|
| `trace_odd` | 22 px / 114816 | **ARTEFACT DE MESURE — soldé à 0 px**, promu `ref_kind: oracle` |
| `overscan_top` | 194 px / 114816 | **ÉCART DE RENDU RÉEL de 144 px** (les 50 autres étaient la LED de Hatari), localisé et nommé — reste `snapshot` |

### `trace_odd` : ce n'était pas du rendu (soldé)

Hatari incruste une **LED disquette** dans ses captures AVI ; `compare_screenshot.py` la
masque depuis toujours. Le masque valait `(403, 3, 10, 5)` — la taille de la LED **à
l'œil**. Or l'oracle capture en **2×** puis sous-échantillonne : il subsiste un **liseré
d'un pixel tout autour**, mêlé au fond. Sur fond noir, du noir mêlé de noir ne se voit
pas ; sur le fond **vert** de cet étalon, si. Les « 22 px » étaient ce liseré, rien
d'autre. Masque corrigé en `(402, 2, 12, 6)` → **0 px**.

La preuve qui a permis de trancher **ne dépend pas du masque** : les 72 pixels concernés
portent des teintes que le Shifter **ne peut pas produire**. `Shifter::stColorToArgb`
construit chaque octet par `v |= v << 4`, donc à nibbles **égaux**, et sur ST le bit 3 du
nibble n'existe pas : les seuls octets atteignables sont `00 22 44 66 88 AA CC EE`.
Les teintes en cause — `#00B200`, `#007700`, `#E00000` — n'en sont pas. Aucun réglage de
palette, aucun bug de rendu ne les produit : elles ne peuvent venir que de l'incrustation.

⛔ L'hypothèse de 2026-08-19 — « même famille que le résidu d'`overscan_top`, les deux sont
sur les premières lignes de trame » — est **RÉFUTÉE**. Les deux cas n'ont rien de commun :
l'un est une incrustation de l'oracle, l'autre est du rendu NeoST. Ne pas la rouvrir.

### `overscan_top` : écart réel, 144 px, localisé (ouvert)

Mesuré **stable sur les 61 trames** de la fenêtre de balayage : aucune dépendance de phase,
c'est structurel. Les 144 pixels sont sur les **5 premières lignes de la trame** —
exactement celles qu'**ouvre le retrait de bordure haute**. Au-delà (`y ≥ 5`) les deux
images sont **identiques au pixel** : le retrait de bordure haute lui-même est donc
**conforme**, ce n'est pas lui qui est en cause.

Ce qui diffère est le traitement de la **bordure GAUCHE** sur ces lignes de transition —
relevé de la ligne `y=0`, plages de couleur :

| x | 0..31 | 32..43 | 44..47 | 48..351 | 352..363 | 364..367 | 368..415 |
|---|---|---|---|---|---|---|---|
| NeoST  | noir | noir | blanc | blanc | blanc | noir | noir |
| Hatari | noir | `$333` | `$333` | blanc | `$555` | `$555` | noir |

soit une fenêtre de **320 px décalée de −4** côté NeoST (blanc en `44..363`) contre une
fenêtre de **336 px** côté Hatari (contenu en `32..367`).

NeoST applique le décalage `LEFT_OFF` standard de **−4 px** et garde une fenêtre de
**320 px** ; Hatari rend une fenêtre de **336 px** (`x=32..367`) dont les 16 px de tête et
les 16 px de queue portent des index de palette autres que 0 et 15. Les deux teintes
(`#666666` = `$333`, `#AAAAAA` = `$555`) sont des **coloris ST légaux** au sens du test
ci-dessus : c'est bien du rendu.

Piste, non instruite : `glue::LEFT_OFF` / `LEFT_OFF_MED` et la table `shEff` de
`Shifter.cpp` (le `default: −4`, « left-off standard calibré »). ⚠ Ce chemin est calibré à
0 px contre plusieurs démos (No Cooper, Closure, Cuddly) : **aucun réglage sur la foi de ce
seul étalon**. La référence reste une **self-capture** — elle garde la non-régression, elle
ne prouve pas la conformité sur ces 5 lignes.

**Contre-épreuve utile** : `scroll_8264` et `scroll_8265`, capturés dans les mêmes conditions
(STE, disque généré), sont à **0 px** de l'oracle — leurs références ont été promues en
`ref_kind: oracle` dès le 2026-08-19. Le rendu STE fin n'est pas en cause.

**Ce que le contrôle croisé de 2026-08-19 établissait déjà, et qui tient toujours** :
l'écart d'`overscan_top` est **indépendant de la ROM** (identique sous `etos192fr` et
`tos102uk`, l'oracle étant lui-même byte-identique entre les deux — ces disques sont des
secteurs de boot autonomes, le TOS ne fait que les charger) et **indépendant du numéro de
trame** (motif statique).

**Bilan de corpus** : 10 étalons machine sur 16 sont désormais référencés à l'oracle Hatari
(9 avant), un seul écart oracle mesuré reste ouvert.

## Extensions NeoST sans équivalent Hatari (divergences délibérées, hors fidélité)

Ces fonctionnalités **n'existent pas dans Hatari** et ne sont donc pas des écarts à
corriger : ce sont des extensions NeoST, **inactives par défaut** et **sans effet sur
les étalons de fidélité** (réseau OFF pendant `run_all.py --tier fast/full`).

- **[EXTENSION] UltraSatan sur le bus ACSI** (`src/io/UltraSatan.*`, `Acsi::attachUltraSatan`,
  2026-08-21). Hatari n'émule qu'un disque ACSI générique ; NeoST ajoute, SUR DEMANDE, les
  2 slots, l'INQUIRY `JOOKIE  UltraSatan`, l'état « slot vide = NOT READY » et les paquets
  ICD `$20 'US…'` du firmware v1.20 (source : atarijookie/ce-atari). Garde-fou : ces paquets ne
  sont routés que sur les cibles UltraSatan ; une cible ordinaire reste byte-identique à `hdc.c`.
  Spec : `docs/EXTENSIONS.md` § UltraSatan.
- **[EXTENSION] NetUSBee — ISP1160 USB sur le port cartouche** (`src/io/Isp1160.*`, `Bus::read8Slow`,
  2026-08-21). La moitié NE2000 est l'EtherNEC existant ; l'ISP1160 est décodé aux adresses du
  pilote FreeMiNT. Hub racine vide (aucun périphérique USB). ⚠ La fenêtre LSB `$FA0000-$FA01FF`
  est partagée avec le registre CR de la NE2000 : sans schéma, NeoST laisse les DEUX puces voir
  l'accès — à corriger si le matériel gate l'une d'elles. Spec : `docs/EXTENSIONS.md` § NetUSBee.
