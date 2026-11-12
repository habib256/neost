# Divergences NeoST ↔ Hatari — inventaire par sous-système

**But.** Inventaire des écarts entre NeoST et la **référence comparative Hatari**, rangé par
sous-système : ce qui diffère aujourd'hui, pourquoi, et ce qui le tranche. Le comportement
du vrai Atari prime : un écart peut venir d'un défaut d'Hatari, et l'égalité avec Hatari ne
prouve pas la fidélité (`CLAUDE.md`, `docs/REFERENCE_LADDER.md`). Le récit des audits qui ont
produit ces entrées (onze passes, de juin à août 2026) est au `CHANGELOG.md`, à leur date ;
ici ne vit que l'état.

## Mode d'emploi

**Une entrée = un écart**, sous son sous-système, une seule fois. Quand un écart change de
statut, on réécrit son entrée ; on n'en ajoute pas une seconde. L'entrée la plus récente
fait foi ; le détail de l'enquête va au `CHANGELOG.md`.

Forme d'une entrée :

```
- **[ID] Titre court** · statut · sévérité · date — ce qui diffère, ce qui le tranche.
  NeoST : `Classe::méthode` (`Fichier.cpp:ligne`). Hatari : `fonction` `fichier.c:ligne @ révision`.
  Preuve : [grade] source. Garde : étalon, test ou banc.
```

Chaque sous-système a la même charpente, pour qu'un ajout soit évident :

1. une ligne **Concordant** : ce qui a été comparé et trouvé identique ;
2. **Écarts ouverts** : un défaut ou une incertitude de NeoST, non tranché ;
3. **Écarts voulus** : NeoST suit une source de rang supérieur à Hatari (fiche, document
   Atari, ROM, netlist, RTL) ou fait un choix de modèle assumé — **ne pas réaligner** ;
4. **Refermés** : écarts qui ont existé et que NeoST a fermés, une ligne chacun, pour qu'on
   ne les redécouvre pas ;
5. **Faux positifs** : écarts annoncés puis réfutés — ne pas rouvrir.

Une rubrique vide se garde avec « — ».

**Statuts.**

| Statut | Sens |
|---|---|
| **ouvert** | défaut ou incertitude de NeoST, à trancher |
| **voulu [B]**, **[C]**, **[D-RTL]** | NeoST suit une fiche, un document Atari, une ROM ou une netlist/un RTL contre Hatari |
| **assumé** | choix de modèle sans source supérieure, documenté ; un [F] dit ce qui le trancherait |
| **extension** | comportement que ni le matériel ni Hatari ne définissent (confort, médias) |
| **refermé** | NeoST aligné ou corrigé, à la date dite |
| **faux positif** | écart réfuté |

**Sévérité** : **haute** = casse un logiciel ou un boot · **moyenne** = fidélité visible ou
audible · **basse** = cas-limite, cosmétique, ou sans logiciel connu qui l'exerce.

**Identifiants.** Le préfixe nomme le sous-système : `M`, `MC` MFP, `V`, `WS` vidéo, `D` FDC,
`S` son, `BU` bus, `BL` blitter, `SC` SCC, `CPU` 68000. Les entrées Blitter s'appelaient
`B1`/`B3`/`B4` jusqu'au 2026-08-25 (commits antérieurs). La 6ᵉ passe (2026-07-12) avait
numéroté ses restes par **sévérité** (`M3`, `B1`…`B15`, sans rapport avec `BL1`…) : ces
numéros sont conservés, suffixés « (6ᵉ p.) ».

**Ancres.** Les `fichier:ligne` NeoST sont des **ancres** : c'est le **symbole** cité qui fait
foi (`grep`, pas `sed -n`) ; `tools/check_doc_anchors.py` (palier `fast`) vérifie que chaque
`Classe::méthode` cité existe. Lignes NeoST revérifiées le **2026-11-12**. Côté Hatari, une
citation `@ f0736b24` (épingle de `tools/setup_hatari.sh`, v2.6.1-devel du 2026-08-18) a été
lue à cette révision ; une citation sans révision date des passes de juin à août 2026
(arbres `c9906f1` puis `981f291`) et peut avoir glissé de quelques lignes.

**Hors périmètre** (non comptés comme écarts) : moteur FDC `_MFM`/DPLL (SCP/KFS ; l'IPF est
lue par un décodeur libre, § FDC), TT/Falcon, VME, FPU « not found » sur Mega STE (correct :
Hatari n'émule pas le VME), USART relayée vers un tty hôte, son en mode headless. Le Mega STE
suit Hatari faute de netlist de son décodage.

Les deux documents qui précédaient celui-ci, `docs/IKBD_HATARI_DIFF.md` et
`docs/SOUND_HATARI_DIFF.md`, décrivaient un code plus ancien et listaient comme absents des
comportements implémentés depuis ; ils ont été supprimés (verdicts repris en § 6 et § 5).

## Bilan par sous-système

| § | Sous-système | NeoST | Hatari | Fidélité | Ouverts (moyenne / basse) |
|---|---|---|---|---|---|
| 1 | MFP 68901, USART | `Mfp.cpp` | `mfp.c`, `rs232.c` | très élevée | 0 / 3 |
| 2 | Vidéo (GLUE, MMU, Shifter) | `Shifter.cpp`, `VideoGlue.cpp`, `VideoCounter.cpp` | `video.c`, `spec512.c` | très élevée | 1 / 10 |
| 3 | FDC WD1772, DMA, STX, IPF | `Fdc.cpp`, `StxImage.cpp`, `IpfImage.cpp` | `fdc.c`, `floppies/*.c` | très élevée | 0 / 3 (+ D1/D2 assumés) |
| 4 | ACSI | `Acsi.cpp`, `Fdc.cpp` | `hdc.c` | élevée | 0 / 2 |
| 5 | Son (YM2149, DMA STE, LMC1992) | `YM2149.cpp`, `DmaSound.cpp` | `psg.c`, `sound.c`, `dmaSnd.c` | très élevée | 0 / 3 |
| 6 | ACIA 6850, IKBD HD6301, MIDI | `Ikbd.cpp`, `MidiAcia.cpp` | `acia.c`, `ikbd.c`, `midi.c` | très élevée | 0 / 7 |
| 7 | Bus, mémoire, bus error | `Bus.cpp` | `ioMem*.c`, `memory.c`, `stMemory.c` | très élevée | 0 / 0 |
| 8 | Blitter | `Blitter.cpp` | `blitter.c` | très élevée (données byte-identiques) | 1 / 0 |
| 9 | SCC Z85C30, SCU (Mega STE) | `Scc.cpp`, `Scu.hpp` | `scc.c`, `scu_vme.c` | élevée | 0 / 3 |
| 10 | RTC RP5C15 | `Rtc.cpp` | `rtc.c` | élevée | 0 / 0 |
| 11 | FPU 68881 | `Fpu.cpp`, `SoftFloat*.hpp` | `fpp.c`, `softfloat/` | très élevée | 0 / 0 |
| 12 | CPU 68000 et intégration | `Cpu68k.cpp`, `extern/moira` | `cpu/newcpu.c`, `gencpu.c` | très élevée | 0 / 1 |
| 13 | GEMDOS HD | `GemdosHd.cpp` | `gemdos.c`, `inffile.c` | élevée (écarts voulus nombreux) | 0 / 0 |
| 14 | TOS et configuration | `Machine.cpp` | `tos.c` | — | 0 / 1 |
| 15 | E/S : imprimante, pads, joysticks | `Machine.cpp`, `StePads.hpp` | `psg.c`, `printer.c` | élevée | 0 / 1 |
| 16 | Extensions NeoST | `UltraSatan.cpp`, `Isp1160.cpp` | — | hors fidélité | — |

Les décomptes suivent les rubriques « Écarts ouverts » ci-dessous ; un écart voulu n'est pas
compté. Aucune divergence connue ne casse un boot EmuTOS (ST, STE, Mega STE) ni un boot
disquette `.ST` normal.

---
## 1. MFP 68901 — `Mfp.cpp` ↔ `mfp.c`, `rs232.c`

**Concordant** : chaîne d'interruption (IER/IPR/IMR/ISR/VR, délai de 4 cycles vers le CPU
`kIrqDelayToCpu` ≙ `MFP_IRQ_DELAY_TO_CPU`, IACK, fin d'interruption automatique ou logicielle,
antidatage), timers datés, compteur vivant, comptage d'événements Timer A/B. NeoST rend le
vecteur parasite `$60` qu'Hatari laisse en TODO.

### Écarts ouverts

- **Retombée de l'IRQ immédiate** · ouvert, suivi · basse — identique à Hatari aujourd'hui ;
  Hatari porte un TODO pour un délai mesuré. NeoST : `Mfp::updateIrq` (`Mfp.cpp:1426`).
  Hatari : `mfp.c:789`.
- **Phase de l'horloge des timers à la mise sous tension** · ouvert [F] · basse — front 0 au
  cycle 0 (cf. « Départ des timers » ci-dessous). Tranché par : une mesure de la phase
  XTAL/CPU sur machine.
- **Lignes RS-232 CTS/DCD/RI levées directement** · assumé · basse — `raise()` direct,
  voulu pour la fiche de bouclage du diagnostic. NeoST : `Mfp::setRs232Cts` (`Mfp.hpp:446`).

### Écarts voulus — ne pas réaligner

- **BREAK reçu par l'USART** · voulu [B] · basse · 2026-11-12 — Hatari ne reçoit pas de BREAK ; NeoST
  pose B et l'IRQ de fin de BREAK en bouclage ([B] MC68901 fig. 6-3, § 6.2.3, `Mfp::receiveSerialBreak`).

- **Quartz maître au choix** · extension · basse · 2026-11-12 — NeoST permet le quartz STF NTSC
  (32 042 400 Hz) ou STE NTSC (32 215 905 Hz) ; Hatari définit ces valeurs
  (`clocks_timings.c:138,144 @ f0736b24`) mais garde le PAL (`:178`). Au défaut, aucun écart. Le YM
  du STE reste sur l'horloge bus chez NeoST, sur SCLK chez Hatari (−2,3 cents, TODO « YM du STE sur
  SCLK ? »).

- **Lecture du compteur dans le dernier cycle MFP fractionnaire** · voulu [B] · basse ·
  2026-11-12 — `Mfp::liveTimerCount` (`Mfp.cpp:885`) rend `ceil(reste / prescaler)` sur le
  reste EXACT (sous-cycles CPU), plafonné au compte de départ de la période. [B] MC68901
  janv. 1984 § 5.1.1 p. 5-1 : chaque impulsion décrémente, à 01 la suivante recharge — une
  donnée non nulle ne se relit jamais $00. Tronquer d'abord le reste en cycles MFP entiers
  (ce que faisait NeoST, comme `MFP_CYCLE_TO_REG` `mfp.c:393 @ f0736b24` sur un reste déjà
  entier) rendait un cran trop bas pendant la fraction de cycle MFP qui précède chaque
  décompte, et $00 juste avant l'échéance. Le plafond couvre la fraction de cycle MFP d'un
  départ frais, avant que le prescaler n'attaque. `mfp_poll` (snapshot) recalé : 352 px sur
  25 lignes, toutes TADR = ancien + 1, IPRA inchangé, dont les deux lignes qui relisaient $00
  (y = 51 et 100) et relisent $01. Test : `testMfpTimerLiveRead` (selftest).
- **Arrêt dans le dernier intervalle de prescaler** · voulu [B] · basse · 2026-11-10 —
  `Mfp::storeStoppedCounter` (`Mfp.cpp:841`) conserve 1 à l'arrêt au lieu de recharger.
  [B] *MC68901 Advance Information*, janv. 1984, § 5.1.1 p. 5-1 et Fig. 5-3 p. 5-6/5-7
  (`MC68901_Jan84.pdf`, SHA-256 `5441a092…`) : le compteur reste à 01 jusqu'à l'impulsion
  suivante ; l'arrêt inhibe le comptage et perd seulement le résidu du prescaler. Hatari
  `mfp.c:1563-1577,1606-1620 @ f0736b24` force la recharge. Garde :
  `tests/mfp_pulse_logic.cpp`, quatre timers, quatre dates avant expiration, lecture, arrêt,
  attente arrêté et reprise (64 assertions ; arrêt et reprise échouaient avant). Pas une
  capture sur machine ; la phase électrique reste hors de ce test de puce nue.
- **Élection par priorité fixe** · voulu [B] · moyenne · 2026-11-10 — MC68901 janv. 1984
  § 3.1 et § 3.1.1 p. 3-1, MK68901 p. 9 : seule la priorité fixe choisit le canal.
  `Mfp::checkPendingInterrupts` (`Mfp.cpp:1465`) la suit ; le front IRQ reste daté de la plus
  ancienne requête éligible (`Mfp::earliestEligible`, `Mfp.cpp:1483`), d'où l'élection
  groupée `Mfp::flushIrqUpdate` (`Mfp.cpp:1413`), gardée pour cette date. Hatari
  `mfp.c:998 @ f0736b24` n'élit que la requête la plus ancienne d'une même instruction
  (règle de 2013 pour les Fuzion CD Menus 77, 78 et 84). Les trois menus tournent sous NeoST :
  `docs/CASE_STUDIES.md` § Fuzion.
- **Impulsion de comptage au changement d'AER en event-count** · voulu [B] · basse ·
  2026-11-10 — MC68901 janv. 1984 § 5.1.3 p. 5-4 (« changing the state of the edge bit while
  the timer is in the event count mode may produce a count pulse ») et § 4.2.2 note p. 4-2
  (détecteur = XOR du bit de front et du tampon d'entrée) : l'impulsion naît quand l'entrée
  vaut déjà le nouveau bit. NeoST la compte pour le Timer A (TAI, AER bit 4, `Mfp::write8`,
  `Mfp.cpp:446`) et le Timer B (DE, AER bit 3, `Machine::retargetTimerB`, `Machine.cpp:578`),
  datée à l'écriture. Hatari `mfp.c:2742-2810 @ f0736b24` déplace seulement le front du
  Timer B. Gardes : 5 assertions Timer A (`tests/mfp_pulse_logic.cpp`), 6 Timer B
  (`tests/machine_timer_b_aer_logic.cpp`). Aucun logiciel du corpus ne l'exerce (56 étalons
  identiques à l'octet par `cpu_swap_gate.py`, 19 parcours verts) ; un titre réel qui
  divergerait primerait sur la fiche, qui dit « may ».
- **Départ des timers sur l'horloge des timers** · voulu [B] · moyenne · 2026-11-10 —
  MC68901 janv. 1984 § 5.1.1 p. 5-1 : le prescaler compte l'horloge XTAL (2,4576 MHz),
  asynchrone du CPU ; un départ frais attend le front suivant, son délai varie d'une fraction
  de cycle MFP selon la phase de l'écriture (`Mfp::scheduleTimerAt`, `Mfp.cpp:969`). Hatari
  démarre au cycle CPU exact et n'imite la gigue que par un aléa de ± 2 cycles MFP réservé à
  Lethal Xcess (`mfp.c:1386-1392 @ f0736b24`). Effets : Lethal Xcess STE garde sa bordure haute
  en partie (`docs/CASE_STUDIES.md`) ; `mfp_poll` et `blitter_hog` passent de `hatari` à
  `snapshot` ; `dmasnd_poll` reposé ; `nocooper` capture une trame plus tard, à 0 px.
- **Reset du MFP** · voulu [B] · basse · 2026-11-04 — TADR…TDDR et les compteurs restent ;
  SCR, UCR, RSR s'effacent et l'USART s'arrête. [B] MC68901 § 2.2.3 p. 2-8, MK68901 p. 13,
  MC68HC901 § 3.3 p. 3-3. Hatari `mfp.c:538-546 @ f0736b24` fait l'inverse sur les deux points.
  NeoST : `Mfp::reset` (`Mfp.cpp:127`).
- **Modes pulse des timers A/B** · voulu [B] · basse · 2026-11-03 — NeoST suit MC68901
  § 5.1.2 ; Hatari les traite en mode délai (`MFP_StartTimer_AB`, `mfp.c:1358-1371
  @ f0736b24`, qui se dit lui-même incomplet).
- **GPIP bits 6 (RI) et 3 (GPU_DONE) au repos** · voulu [A]/[B] · basse · 2026-10-20 —
  `$FFFA01` au repos (moniteur couleur, CTS/DCD actives) vaut **$F9** sur ST sans blitter et
  **$F1** avec blitter (Mega ST, STE, Mega STE) ; Hatari rend **$B1**. `Mfp::gpipInput`
  (`Mfp.cpp:1304`) : bit 6 = 1 tant que RI n'est pas assertée (`riLine_` faux au repos),
  bit 3 = 1 sans blitter (`hasBlitter_`, posé par `Machine` via `machineHasBlitter`), 0 au
  repos avec blitter et 1 pendant un blit ; les deux bits suivent leur ligne (détecteur de
  front de `Mfp::gpipSetLine` intact). Sources : *STe Field Service Manual* (C302481-001,
  § 2.12.5 — CTS/DCD/RI entrent par un récepteur 1489, U207) ; fiche TI MC1489 (SLLS095D —
  entrée ouverte, sortie haute) ; MAME `rs232.cpp` (RI = 1 sans appareil) ; lecture sur STFM
  réel `$E9` (exxosforum t=3992 p. 49, 2022 — bits 6 et 3 à 1, CTS/DCD à 0 cohérents) ;
  agranlund/STBlitter_RevA (pull-up R4 de l'INT blitter). Le 0 d'Hatari est un artefact :
  `MFP_Reset` pose GPIP = 0 (`mfp.c:523`) et rien ne relève le bit 6 (le bit 3 attend le
  premier `Blitter_Start`). Conséquences : un diff de trace sur `$FFFA01` contre Hatari est
  attendu ; la convergence vers $B1, tentée, n'avait pas sauvé Closure
  (`docs/CLOSURE_CHANTIER.md`, cycle 3). Le bouton Ultimate Ripper
  (`PortDevices::pressButton`) asserte RI (bit 6 → 0), effet sur le logiciel non vérifié.
  Validé : selftest MFP ($F1/$F9), test S Field Service (ST_Diagnostic v4.4, `--machine st`,
  loopback), Mega STE suite Q 11/11. [F] bit 3 au repos AVEC blitter (0 supposé, INT = copie
  de BUSY) : non mesuré.
- **Registre VR, bits réservés** · voulu [B] · basse · 2026-10-19 — bits 2-0 lus à zéro et
  ignorés en écriture, selon Motorola MC68HC901UM § 4.1.3 et MAME `REGISTER_VR`. Base
  vectorielle et bit S inchangés ; anciennes valeurs sauvegardées masquées à la lecture (pas de
  changement de format v44). Hatari garde les bits.
- **Jitter Lethal Xcess et « PATCH TIMER D »** · assumé · basse — non portés : le premier
  est un aléa codé sur le PC du jeu (le départ sur l'horloge des timers en donne la
  physique, ci-dessus), le second un raccourci de performance inutile ici.

### Écarts voulus — USART (`Mfp.cpp` ↔ `rs232.c`, MAME `mc68901.cpp`)

Hatari tient l'USART en puits instantané ; NeoST suit le manuel MC68901UM et MAME.

- **TX asynchrone** · voulu [B] · 2026-10-18 — TDR/TSR, Buffer Empty au chargement du TSR,
  émission et bouclage en fin de caractère. La désactivation laisse finir le TSR ; underrun et
  EOT distingués. Cadence selon UCR, phase rationnelle sauvegardée avec SERIAL_TX. Référence :
  MAME `mc68901.cpp:958-1015,1387-1432`. Phase du premier front TC, framing synchrone et forme
  d'onde BREAK approximatifs ; le puits hôte n'a pas d'horodatage.
- **Bouclage interne** · voulu [B] · 2026-10-19 — TSR H/L = 11 relie TX à RX sans fiche
  externe ; SI/RC hôte déconnectés, même si TE = 0. SO reste observable, RE qualifie la
  réception, la prise externe ne double pas la livraison. Motorola § 2.10.1/§ 2.10.3/§ 7.3.2,
  MAME `rc_w/tc_w`. Commutation évaluée au stop abstrait du mode courant, sans phase de bit ni
  reconstruction des trames qui traversent une bascule H/L.
- **Overrun RX différé** · voulu [B] · 2026-10-19 — un octet non lu reste ; le suivant est
  perdu, mais OE et RXERR attendent la lecture d'UDR. Lire RSR avant UDR n'acquitte pas
  l'erreur en attente ; une fois OE publié, les données sont ignorées jusqu'à la lecture de
  RSR. RE = 0 et reset annulent les deux étapes, sans effacer une IRQ déjà pendante.
  MC68HC901UM § 7.2.2 p. 7-5, MAME `REGISTER_UDR` / `rx_async_frame_complete`. Remplace le
  modèle du 2026-10-18 qui signalait RXERR immédiatement. Étape en attente sauvée en v44.
- **Durée et phase RX** · voulu [B] · 2026-10-18 — format asynchrone d'UCR, retenue
  rationnelle et échéance ancrée sans dérive ; configuration en vol conservée jusqu'au
  caractère suivant ; RXFULL daté au stop même si le callback est servi en retard ; RXERR
  d'overrun daté de la lecture d'UDR qui publie OE. UCR7 choisit /1 ou /16 sans arrondi tty ni
  plancher (MC68901UM § 7.1.3) ; le rapport Timer D/UCR exact cadence TX et RX, même aux
  débits non entiers, fraction conservée aux reconfigurations ; diviseur effectif sauvegardé
  en v30. Le débit entier exposé reste informatif ; le synchrone retombe en 8N1 /16. Hatari
  `rs232.c:329-365`.
- **RX désactivé, UDR persistant** · voulu [B] · 2026-10-18 — RE = 0 efface RSR, Buffer Full
  et overrun, sans effacer la donnée ni les IRQ pendantes ; UDR lit toujours le registre RX,
  jamais le dernier octet TX (MAME `REGISTER_RSR/UDR`). Réacquisition après RE bas puis haut
  au milieu d'un caractère approximative, faute de bits de câble.
- **Underrun mémorisé** · voulu [B] · 2026-10-18 — UE reste posé lors des écritures et
  rechargements d'UDR ; les fins de caractère suivantes ne redemandent pas TXERR ; lecture de
  TSR et TE = 0 le réarment. Délai minimal d'une horloge TX avant effacement par lecture porté
  (seuil sur la phase TX engagée). Délais de trame et d'UE sauvegardés en v36 (MAME
  `tx_clock/REGISTER_UDR`).
- **END et auto-turnaround au repos** · voulu [B] · 2026-10-18 — TE coupé sans caractère en
  vol pose END immédiatement ; AT active RE puis se désarme ; en vol, effet à la fin du
  caractère. IRQ END sur nouveau front seulement, datée à la fin d'accès MMIO ou au stop
  théorique (MC68901UM § 7.3.2, MAME `tx_clock`).
- **Droits d'écriture de RSR** · voulu [B] · 2026-10-18 — seuls RE/SS, plus F/S en
  synchrone ; les écritures ne créent ni n'effacent les bits de statut (MC68901UM § 7.2.2,
  MAME `REGISTER_RSR`).
- **Mots courts en RX** · voulu [B] · 2026-10-18 — octet hôte aligné à droite et masqué à
  5..8 bits ; largeur mémorisée au début du caractère (état v31). Aucune erreur de parité ou
  de framing synthétisée depuis un octet hôte (MAME `rx_frame_start/rx_clock`).
- **BREAK selon le mode** · voulu [B] · 2026-10-19 — B n'agit qu'en asynchrone (MC68901UM
  § 7.3.2, MAME `tx_clock`) ; en repli synchrone 8N1 il ne bloque ni TDR ni UE, et un
  changement d'UCR libère le TDR retenu. BREAK asynchrone cadencé par caractère (MC68HC901
  p. 7-8), TSR préservé, BE antérieur conservé ; TE coupé finit à la frontière sans stop ;
  reprise avec un stop d'une période TX (durée déduite), puis TDR ou UE. Pas de protocole
  synchrone complet, ni de phase TC initiale, ni d'onde physique.

### Refermés

- **[M1] Fronts des lignes GPIP** · moyenne · `bc15a67`, 2026-07-03 — machine de fronts
  centralisée, AER/DDR respectés, l'écriture d'AER re-déclenche les fronts : `Mfp::gpipSetLine`
  (`Mfp.hpp:343`), `Mfp::gpipUpdateInterrupt` (`Mfp.cpp:1354`) ≙ `MFP_GPIP_Set_Line_Input` /
  `_Update_Interrupt` (`mfp.c:1143-1219`, AER `mfp.c:2787-2790`). Tous les appelants passent
  par elle : IKBD et MIDI en OU câblé (≙ `MFP_Main_Compute_GPIP_LINE_ACIA`), FDC
  (`Mfp::setFdcLine`), blitter. GPIP0 BUSY y passe depuis le 2026-10-19 (`Mfp::setBusyLine`) ;
  XSINT respecte DDR7 ; GPIP7 composé (XSINT et bouton Multiface) détecte son front après le
  calcul complet du niveau (2026-10-19, plus d'IRQ parasite sous un bouton tenu).
- **[MC] Conversion MFP→CPU sans reste accumulé** · moyenne-basse · 2026-08-14 — échéances
  absolues en 1/256 de cycle CPU (`Mfp::timerPeriodSubCycles`, `Mfp.cpp:771` ;
  `timerDueSub_`), même `CYCINT_SHIFT = 8` qu'Hatari (`cycInt.c:26-45`) ; plafond appliqué à
  la seule frontière du Scheduler, fraction gardée pour le rechargement suivant ; Timer C
  200 Hz = 40 106,238 cycles sans dérive ; phase en save-state v11.
- **Timer B en comptage d'événements** · basse · `bc15a67` — garde `tbCounter_ == 0`
  supprimée, bouclage 0 → 255, antidatage `firingDue()` : `Mfp::hblankAt` (`Mfp.cpp:1049`) ≙
  `MFP_TimerB_EventCount` (`mfp.c:1297-1320`).
- **`MFP_UpdateTimers` avant tout accès registre** · moyenne · 2026-09-02 — `Mfp::updateTimers`
  (`Mfp.cpp:64`) → `Scheduler::runMfpTimersTo` en tête de `Mfp::read8` / `Mfp::write8` ;
  verrou `NEOST_MFP_UPDTIMERS` (défaut ON). Cas nommé par Hatari (`mfp.c:135`, Super Hang-On :
  un `bclr #0,$fffffa0f` concurrent d'une expiration de Timer C acquittait une valeur
  périmée, rasters clignotants). ≈ 9 % des accès registre trouvent un timer échu. Le dispatch
  est ciblé sur les sources `TIMER_*` : un `syncTo` nu réactiverait le modèle sync-driven
  réfuté (deadlock Enchanted Land). Un verdict « réfuté » écrit le matin même était un
  artefact de mesure (`CHANGELOG.md`, 2026-09-02).
- **Compteur vivant lu sur l'échéance arrondie** · moyenne · 2026-09-02 — `Mfp::readTimerData`
  (`Mfp.cpp:898`) partait du plafond entier vu par le Scheduler : TADR sortait un cran trop
  haut quand le reste tombait sur un multiple du prescaler (6 lignes sur 100 de `mfp_poll`,
  80 px). Il part de l'échéance sous-cyclique `timerDueSub_` ; `mfp_poll` passa alors à 0 px
  (100 octets IPRA et TADR identiques) et fut promu `hatari`, avant de redevenir `snapshot`
  le 2026-11-10 (départ sur l'horloge des timers, ci-dessus). `Mfp::storeStoppedCounter` a
  reçu le même changement par cohérence, sans test qui le couvre à l'époque.
- **Élection chronologique par lot** · basse · 2026-09-02, lot inter-callbacks fermé le
  2026-09-03 (A42) — `Mfp::raiseAt` (`Mfp.cpp:1387`) arme `irqUpdateNeeded_` ;
  `Mfp::flushIrqUpdate` élit au calcul d'IPL et en fin de `Mfp::updateTimers` (≙ `mfp.c:689`,
  `mfp.c:1084-1087`) ; `Scheduler::setDispatchHooks` borne le lot, `Mfp::setInDispatch`
  suspend l'élection pendant le dispatch (`Machine.cpp:273-278`, verrous `NEOST_MFP_BATCH`,
  `NEOST_IPL_BATCH`). Mesuré : 123 entrées groupées sur 1 000 123 (Super Hang-On). Depuis le
  2026-11-10 le lot ne sert plus qu'à dater le front (élection par priorité fixe, ci-dessus).
- **Cycles d'écriture dans `MFP_IRQ_Time`** · basse · 2026-09-02 — une IRQ née d'une écriture
  registre est datée à la fin de l'accès (+2, `Mfp::writeEventTime`, `Mfp.cpp:54`), comme
  `Cycles_GetInternalCycleOnWriteAccess` (`mfp.c:113`, « properly fix Super Hang On »). Aucun
  effet pixel démontré à la valeur juste (`+0` et `+2` identiques sur Super Hang-On) ; le
  chemin pilote l'émulation (`NEOST_MFP_WRITE_END=40` déplace 24 773 px).
- **Délai de 4 cycles sur la montée d'IRQ** · déjà porté — `kIrqDelayToCpu`, source
  `Scheduler::MFP_IRQ` armée à `irqTime_ + 4`, cas « délai déjà écoulé » (≙ `MFP_ProcessIRQ`,
  `mfp.c:107`).
- **Bit pending posé deux fois avant l'IACK** · correct par construction — HBL/VBL sont des
  booléens (`hblPending`/`vblPending`, `Cpu68k.cpp:64-65`) lus comme un niveau ; Hatari y
  arrive par un code dédié (`video.c:320`).
- **Repositionnement du Timer B à l'écriture d'AER bit 3 en cours de ligne** · 2026-10-19 —
  fronts déjà échus servis avant la mutation, puis recalage sur le prochain front strictement
  futur de la ligne courante, sinon de la suivante (`Machine::retargetTimerB` ≙
  `Video_AddInterruptTimerB`). Banc synthétique 50/60/mono ; ne vaut pas validation de Seven
  Gates of Jambala.
- **Timer B événementiel lu pendant une instruction** · 2026-11-04 — les accès MFP servent le
  front DE déjà échu avant de lire le compteur (l'exclusion de `TIMER_B` dans
  `runMfpTimersTo` rendait la valeur précédente). Le flux des 25 491 instructions du délai
  vidéo initial de TOS 2.06 concorde avec Hatari (`docs/CASE_STUDIES.md`, cas TOS 2.06).

### Faux positifs

- **Wait-states des accès MFP** — appliqués par le `Bus` (`Bus::rdMfp`, `Bus.cpp:1036` ;
  `Bus::wrMfp`, `Bus.cpp:1058`, qui appellent `Cpu68k::addMfpWaitCycles`), pas par `Mfp.cpp`.

---
## 2. Vidéo (GLUE, MMU, Shifter) — `Shifter.cpp`, `VideoGlue.cpp`, `VideoCounter.cpp` ↔ `video.c`, `spec512.c`

**Concordant** : cœur GLUE du STF transcrit presque ligne à ligne
(`VideoGlue::updateGlueState`, `VideoGlue.cpp:586` ≙ `Video_Update_Glue_State`, branche STF ;
branche STE depuis V1), constantes de wakestate (WS3 par défaut), compteur vidéo, bordures
haute, basse, gauche et droite à 50/60 Hz, Spectrum 512 (seuil 1, `kSpec512Threshold`,
`Shifter.cpp:108` ≙ `spec512.c:140-143`), scroll fin du STE, masquage de palette par
machine, restart du compteur vidéo (`Shifter::restartVideoCounter`, `VideoCounter.cpp:179`
≙ `Video_RestartVideoCounter`, événement `VC_RESTART`), filtre des écritures freq/res
redondantes (`Shifter::recordSyncWrite`, `VideoGlue.cpp:329` ≙ `ShifterFrame.Freq/Res`).

### Écarts ouverts

- **STE : écriture 60 Hz aux cycles 32-36** · ouvert · moyenne · 2026-10-22 —
  `blankline_blank_ste` : 0 px à la phase 28, mais aux phases 32/36 du cycle de 5 trames
  NeoST rend une image (10 714 px, signature d'un décalage d'une ligne) qu'aucune des 61
  trames de l'oracle ne produit (`--oracle-check`). Préexistant, masqué par la phase ; l'étalon
  est recalé sur la phase 28. À analyser : branche 60 Hz `<= Preload_Start_Low_60` (36) et
  fenêtre NO_DE (36,40] de `VideoGlue.cpp` contre `video.c` et la netlist. (Le décalage de
  4 px d'une ligne `left+2` à ces phases, lui, est un écart voulu : « Lignes 60 Hz isolées »
  ci-dessous.)
- **Résidu STOP_MIDDLE de trois pixels** · voulu [D-RTL] · basse · 2026-10-19, tranché le 2026-11-12 — le
  bord de fin de NeoST concorde au modèle GSTMCU au pixel (`run_stcpu_raster`, cas `hi_96_460`) ; les captures ST/STE
  de `blankline_stop_*` diffèrent d'Hatari de trois pixels au dernier groupe de plans, premier
  écart `(156,65)` du buffer 416×276 ; chacune des 61 captures Hatari 350-410 est identique.
  `Video_CopyScreenLineColor` (`video.c @ f0736b24`) recopie 54 octets puis efface la suite du
  buffer planaire, laissant décoder un dernier groupe partiel ; NeoST limite les pixels par la
  fin DE à 164 cycles (normalisée). Les autres lignes et l'adresse accumulée concordent. Les
  deux sondes restent en `snapshot`, jamais en référence Hatari tolérée ; la capture Hatari est
  conservée (`tests/reference/blankline_stop_oracle.png`, ST et STE identiques à l'octet).
  Reproduction : `python3 tools/make_blankline_test.py /tmp/blankline_stop.st`, puis
  `bash tools/hatari_oracle.sh roms/etos192fr.img /tmp/blankline_stop.st 435 380
  /tmp/blankline_stop.png st fastfdc 0`. Ni [Paulo Simoes](https://alive.atari.org/alive11/oscan2b.php)
  (groupes de quatre plans, modèle simplifié) ni les [notes Tattersall/Troed](https://github.com/tattlemuss/atari_st_wiki/wiki/StevesVideoNotes)
  ne tranchent ces trois pixels : une mesure du bord sur Atari est requise.
- **Rendu par ligne daté à la frontière d'instruction** · ouvert · basse→moyenne · 5ᵉ passe —
  `Shifter::renderLine` (`Shifter.cpp:528`) part après l'instruction qui enjambe DE_end
  (376) : une écriture palette dans `[376, 376+carry)` est vue par la ligne y (Hatari : y+1).
  Intermittent par nature.
- **`lineSnap_` capturé seulement sur les trames à écritures freq/res** · ouvert · basse —
  une trame sans trick mais en course avec le faisceau est relue en fin de trame ; Hatari copie
  toujours ligne par ligne.
- **Spectrum 512 : pas d'échelonnement des écritures `move.l`/`movem`** · ouvert · basse — ni
  stagger de +4 cycles des écritures au même cycle, ni bouclage de fin de ligne
  (`Spec512_StoreCyclePalette`, `spec512.c:185-199`). 0 px revendiqué sur le diaporama
  Spectrum 512.
- **Datation des écritures couleur Spectrum 512** · ouvert · basse — offset calibré (−25)
  distinct de la datation freq/res (+16) : fonctionne par calibration séparée, fragile.
- **Quirks E605/Tekila sur `$FF8205/07/09` pendant DE** · ouvert, différé · basse — exceptions
  par opcode (FIXME, partielles) d'Hatari, pas un défaut matériel démontré dans NeoST (audit
  du 2026-10-19) ; port différé jusqu'à reproduction sur E605 ou Tekila.
- **V2, restes** · ouvert · basse — hacks
  « TEMP » Closure/DOLB d'Hatari (`video.c:1791-1834`, reniflage de PC/opcode, non portables) ;
  rendu med 640 natif dans une trame mixte (buffer 416, réduction ×2 documentée).
- **Shifter arrêté (res 3), incertitudes** · ouvert [F] · basse · 2026-10-20 — noir franc ou
  couleur 0 ; lecture OCTET de `$FF8261` (alias complet conservé) ; trick sur STE (modèle STF
  appliqué, comme Hatari) ; persistance du décalage aux trames suivantes (TODO
  `video.c:1915` ; NeoST garde la trame courante, comme Hatari). Non modélisés : pixels noirs
  pendant un arrêt bref, résolution Shifter ≠ GLUE (0/1/2) au rendu, géométrie de trame à
  res 3 via `$FF8260`, chemin `PrevSize` partiel. Aucun logiciel réel connu ne l'exerce.
- **V3, réserve** · ouvert · basse — le cas décisif (la DERNIÈRE ligne `lpf_-1` change de
  fréquence) n'est établi que par lecture des gardes, jamais mesuré ; la ligne raccourcie
  observée est la 34.
- **[B14 (6ᵉ p.)] Capture `lineSnap_` et comptage palette actifs en monochrome** · ouvert ·
  basse — Hatari saute la haute résolution ; surcoût pur, rendu inchangé.

### Écarts voulus — ne pas réaligner

- **Hardscroll 4 px LO/MED/LO de Paulo Simoes** · voulu [D-RTL] · basse · 2026-11-12 — porté selon le
  modèle du Shifter (`shifter_video.v @ 4a6db7cc`) : phase −D px modulo 16, plans tournés, compteur
  vidéo intact, jusqu'à la fin de la trame. Hatari (`video.c:1772-1803 @ f0736b24`) rend
  +13/+9/+5/+1 px, recule `pVideoRaster` et décale toute la ligne de bascule ; le modèle et le seul
  témoin tiers (ljbk, atari-forum t=24855 : 0/4/8/12 px) le contredisent. Aucun titre de la
  collection. Garde : étalon `rtl_lomedlo`, cas fixes de `tests/reference/stcpu_raster_cases.json`.
- **`$FF8264/65` écrit en plein DE (STE)** · voulu [D-RTL] · basse · 2026-11-12 — NeoST décale aussitôt
  le reste de la ligne, comme le modèle (banc `--kind scroll` 60/64) ; Hatari attend la ligne
  suivante (`video.c:5876 @ f0736b24`). `$FF8207` reste à la ligne suivante (TODO).

- **NOSCROLL du STE, sans fenêtre de temps** · voulu [B] · basse · 2026-11-12 —
  `$FF8264/65` sont deux registres dans deux puces : le décalage (Shifter) suit toute
  écriture ; NOSCROLL (GST MCU, prefetch) ne suit que les accès qui activent LDS (`$FF8265` en
  octet, `$FF8264` en mot) et vaut « octet bas nul ». [B] ST-4081 feuille 3 (P3051 :
  SCRLSEL = `$FF8264-5` ET ILDS) et feuille 10 (latch PA017) ; [D] `gstmcu.v:233,340
  @ 4a6db7cc`. Hatari (`Video_HorScroll_Write`, `video.c:5876 @ f0736b24`) ne pose le latch
  336 px que si `$8264=0` suit `$8265>0` de 40 cycles au plus dans la même VBL, et rend un octet
  `$8264>0` toujours sans prefetch. Banc fx68k + GSTMCU, écart de 48 et 100 cycles : 62 913 px
  faux avant, une ligne après. A/B de 11 titres STE (Obsession v1.0 et v1.1, Pacemaker et
  Pacemaker 1 Mo, Just Musix 2, More or Less Zero, Cernit Trandafir, Double Rez Trouble, E605,
  Stardust, Eliminator ; 452 captures sur 12 000 trames) : 0 image changée ; Obsession et
  Pacemaker écrivent `move.w #1,$FF8264` puis `clr.b $FF8264` 16 à 20 cycles plus tard.
  `scroll_336_late` passe à `rtl`. [F] Une lecture ouvre aussi SCRLSEL : non modélisé.
- **Instruction RESET sur STE** · voulu [B] · basse · 2026-11-12 — RESB efface aussi
  `$FF820F`, la base vidéo, et remet NOSCROLL à 1. [B] ST-4081 feuilles 10 et 12 (PA016/PA017,
  PC075…, PC085…, PC094…, PC102…) ; [D] `gstmcu.v:340,401-405 @ 4a6db7cc`. Hatari
  (`customreset`, `cpu/hatari-glue.c:54 @ f0736b24`) ne touche qu'à `$FF820A/60`. TOS 1.62 et
  2.06 ne réécrivent ni `$FF820F` ni `$FF8264/65` à l'amorçage : sans ce reset, une largeur de
  ligne laissée par un programme survivait au RESET. Le son DMA n'est PAS coupé (§ 5).
  Décalage du Shifter gardé ([F]).
- **Relecture de `$FF820A` sur STE, bit 0** · voulu [B] · basse — la synchro externe est
  mémorisée et relue, comme chez Hatari. [B] ST-4081 feuille 10 : bascule EXTS (PA034) relue
  sur ID8 par PA037. `gstmcu.v:375-380 @ 4a6db7cc` relit `{pal, 0}` : MiSTery omet la synchro
  externe, ne pas s'aligner sur le RTL.
- **Prise des HBL et VBL sur STE** · voulu [D-RTL] · basse · 2026-11-04 — broche levée au
  cycle exact et vue par `POLL_IPL`, au lieu du commit au dispatch (`intlev_load →
  ipl_fetch_now`). [D] fx68k @ `0602ee46` + gstmcu @ `4a6db7cc`, `tools/run_stcpu_bench.py` :
  MULU 256/256 contre 63/256, boucle courte 256/256 contre 216/256. Sonde `timer_b_event` à
  14 cycles de première IRQ, consigné. ST inchangé, faute de modèle.
- **GLUE du STE, trois seuils** · voulu [D-RTL] · basse · 2026-11-04 — `Line_Set_Pal` à 58,
  haute résolution à 4 sans retrait gauche, haute résolution dans (40, 48] retirée à 56 sans
  `NO_DE`. [D] gstmcu @ `4a6db7cc`, bloc « fort », `tools/run_glue_sweep.py` : 1654
  impulsions sur 1664 en accord. Hatari `video.c @ f0736b24` garde 56, ≤ 4 et `NO_DE`.
  → `docs/GLUE_SWEEP.md`.
- **STE : impulsion hi-res tardive (40,56] = « no DE » + ligne 224** · voulu [D-netlist] ·
  basse · 2026-10-22 — Hatari (`video.c:2503`) borne la détection no-DE à
  `Preload_Start_Low_50` = 40 sur STE et rend STOP_MIDDLE + ligne 512 entre 40 et 56 ; la
  netlist SYNC.PIN (4081P26 SYM 18-31 : longueur de ligne = f(MDE1) au rechargement du
  compteur HSC, bord au cycle ~56 ; 4081P29 SYM 38-40 : HDE jamais posé si MDE1 = 1 à ~44 puis
  MDE1B = 0 à ~56) donne no DE + 224 jusqu'à 56, comme le STF. `VideoGlue.cpp`, branche 71 Hz :
  `<= HDE_On_Low_50`. Étalon `blankline_node_late_ste` en snapshot. Réserves : cycle 56 à ±4 ;
  fenêtre (40,44] (DE précoce prédit à ~48) non portée ; aucune mesure STE.
- **Lignes 60 Hz isolées `left+2 / right-2` (A40)** · voulu (défaut d'Hatari) · basse ·
  2026-08-30, revu le 2026-10-22 — même état GLUE des deux côtés (trace Hatari
  `video_border_h` : `detect left+2 / right-2 60Hz 53<->373` ; `H_DE 53<->377 border=2`, DE
  52→376), mais `Video_CopyScreenLineColor` (`video.c:4041-4056`) recopie 160 octets à un
  offset fixe et range les 2 octets left+2 dans le plan 3 du groupe x = 32-47 : 2 octets en
  basse résolution ne sont pas 4 pixels mais un mot de plan sur 16 pixels, et le groupe à
  cheval sort avec des plans mixtes. NeoST émet depuis DE = 52, plans alignés, décalés de
  4 px — compteur de pixels du Shifter réarmé au premier LOAD après DE (decap Cwik,
  `shifter_video.v`), principe qu'Hatari applique lui-même à LEFT_OFF
  (`DisplayPixelShift = -4`, `video.c:256`). Mesuré sur `overscan_top` : tête = plans 0-2 à
  zéro + plan 3 à $FFFF → index 8 ($333), queue (memset `right-2`) = plans 0-2 à $FFFF +
  plan 3 à zéro → index 7 ($555) (palette relevée par `--dump-at 380 FF8240 32`) ; l'oracle
  montre 16 px de $333 en x = 32..47 et 16 px de $555 en x = 352..367 ; 4 lignes `bm=00a` ×
  32 px + 1 ligne `bm=002` × 16 px = **144 px**, tout l'écart. Relevé de la ligne y = 0 :

  | x | 0..31 | 32..43 | 44..47 | 48..351 | 352..363 | 364..367 | 368..415 |
  |---|---|---|---|---|---|---|---|
  | NeoST | noir | noir | blanc | blanc | blanc | noir | noir |
  | Hatari | noir | `$333` | `$333` | blanc | `$555` | `$555` | noir |

  L'écart est stable sur les 61 trames du balayage, limité aux 5 premières lignes (celles
  qu'ouvre le retrait de bordure haute, lui conforme ; au-delà, `y ≥ 5`, images identiques),
  indépendant de la ROM (`etos192fr` et `tos102uk`, oracle identique à l'octet entre les deux :
  ces disques sont des secteurs de boot autonomes) et du numéro de trame. `#666666` ($333) et
  `#AAAAAA` ($555) sont des coloris ST légaux : c'est bien du rendu, pas une incrustation. Contre-épreuve : `scroll_8264` et `scroll_8265`, mêmes
  conditions, à 0 px. Conséquences : `overscan_top` reste `snapshot` **définitivement** ;
  `blankline_blank_st` en snapshot à la phase 32 (bit-identique à la phase 28), remonté en
  `hatari` le 2026-11-10 (le programme se verrouille sur le compteur vidéo et écrit au
  cycle 28) ; `blankline_blank_ste` (STE, phases 32/36 : 204 px sur la ligne buffer 65)
  capture la phase 40.
- **Première ligne quand la bordure haute est retirée (A41)** · voulu (défaut d'Hatari) ·
  basse · 2026-09-01 — Hatari amorce sa palette avec des écritures pas encore faites :
  `Spec512_StartFrame` (`spec512.c:233`) fait `nScanLine += OVERSCAN_TOP` sous
  `V_OVERSCAN_NO_TOP`, les `CyclePalettes` des scanlines 0 à 28 ne sont jamais rejouées, et
  l'amorce vient de `pHBLPalettes[]`, que le `pHBLPalettes -= OVERSCAN_TOP` de `video.c:3429`
  (« FIXME useless ? ») a garni d'écritures postérieures. Mesuré sur Closure : l'amorce vaut
  `000 100 200 210 310 310 320 420 430 531 442 541 552 652 652 763`, exactement le bloc que la
  démo écrit aux cycles 438-508 de la ligne 34, 380 cycles après les pixels qu'il colore
  (contexte : `vover=3 nStartHBL=34 STScreenStartHorizLine=0 OVERSCAN_TOP=29 nScanLine=29
  skip=5`). NeoST applique les écritures à leur cycle. `closure` reste `snapshot`
  **définitivement** (27 px irréductibles, minimum sur 70 trames voisines : 27 ou 43).
- **Shifter arrêté (res 3) : arrêt et retour sur la MÊME ligne** · assumé · basse ·
  2026-10-20 — `Shifter::writeShifterRes` (`VideoGlue.cpp:404`). Hatari ne compare que des
  cycles-ligne (`ResPosStop_Shifter.LineCycles`) : un arrêt posé ligne N et levé ligne N+1
  passe son « ≤ 32 » ; sur une durée négative il réécrit `DisplayPixelShift = 0` aux lignes
  suivantes, sur une durée positive qui chevauche le HBL il arme un décalage. NeoST n'arme rien
  dans les deux cas (avant le correctif, la normalisation modulo 16 donnait −4/−8/−12 px et
  −2/−4/−6 octets par ligne). Le trick Troed/Sync décrit un arrêt de 8 à 32 cycles ; un arrêt
  plus long n'est pas documenté.
- **Ligne noire « Shifter arrêté »** · extension · basse · 2026-10-20 — une ligne est noircie
  (aire active seulement) si un intervalle res 3, en cours ou le dernier terminé, couvre toute
  sa fenêtre DE (`Shifter::lineStoppedOver`, `VideoGlue.cpp:490`). Hatari n'affiche pas de
  noir. Un arrêt partiel (8 à 32 cycles) ne noircit rien, comme Hatari (TODO `video.c:1914`).
  Noir franc ou couleur 0 : non mesuré.
- **Origine de la VBL du témoin Hatari** · voulu (ne pas porter) · basse · 2026-11-04 —
  `video.c:4984-4993 @ f0736b24` soustrait un dépassement mesuré sur l'horloge immédiate à
  l'horloge globale sans les cycles internes du CPU : les traces TOS 2.06 montrent des périodes
  PAL raccourcies de 12 cycles sans changement de mode. Une contre-épreuve dans une copie
  temporaire d'Hatari restaure les origines et périodes de NeoST. À phases d'index imposées
  identiques, 60 cycles d'écart restent à AUTO ; la collision de pile TOS 2.06 n'est pas fermée
  (§ 14, `docs/CASE_STUDIES.md`).
- **Couleur des lignes masquées** · assumé [F] — palette 0 pour NO_DE/BLANK/STOP_MIDDLE est une
  convention d'émulateur ; la couleur physique du blanking attend une mesure.

### Refermés

- **[WS] Hybride WS1/WS3** · moyenne · 2026-07-08 — tranché WS3 complet (≙
  `VIDEO_TIMING_DEFAULT = WS3`, `video.c:624`) : positions GLUE +1 (`glue::kWsInc`,
  `VideoGlue.hpp:91`), IRQ HBL à cpl (512/508/224), VBL 64. Ancres de rendu, compteur et
  spec512 FIXES 56/376 (≙ `LINE_START/END_CYCLE_*`, `video.h:91-95`, hors table WS) ; DE
  stockés renormalisés −inc au rendu ; datations read −6 / write +2 / spec512 −25 inchangées
  (indépendantes du WS). `NEOST_WS=1..4` pour l'A/B.
- **[V1] Branche STE de la GLUE** · moyenne · 2026-07-08 — table STE de `glue::Timing`
  (preload MMU 0/36/40, `Line_Set_Pal` 56, HSync −52/−12, Remove 500, HBL à cpl, inc 0),
  `LEFT_OFF_2_STE` (+20 o, −8 px), ligne NO_DE freq dans (36,40], latch res sans le −1 STF
  (`video.c:2224`) ; ≙ `Video_Update_Glue_State` branche STE (`video.c:2442-2652`). Le menu
  Cuddly casse sur NeoST-STE comme sur un vrai STE (196/250 trames = oracle Hatari-STE) : un
  « menu cassé en STE » est fidèle, vérifier la machine avant de conclure. Latch 336 px
  (`bSteBorderFlag`, `steBorderFlag_`) porté le 2026-10-18 (cinq étalons `scroll_336*`).
- **[V2] Tricks par changement de résolution** · moyenne · 2026-07-08 —
  `VideoGlue::updateGlueRes` (`VideoGlue.cpp:896`) ≙ détections `video.c:1637-1753` de
  `Video_WriteToGlueRes` : overscan med-res, `LEFT_OFF_MED` (+26 o), variante STE
  `LEFT_OFF_2_STE_MED` (+20 o, −16 px), stab med, scrolls hardware 13/9/5/1 px, rendu
  multi-résolution par ligne (≙ `Video_StoreResolution`). No Cooper greetings 0 px. Écriture de
  `$FF8261` et Shifter arrêté (res 3) portés le 2026-10-20 (`Shifter::writeShifterRes` ≙
  `Video_WriteToShifterRes`, `video.c:1888-2006`) : un `move.b` vers `$FF8261` change la
  résolution du Shifter seul (`ioMemTabST.c:71`, `ioMemTabSTE.c:120`) ; un `move.w` vers
  `$FF8260` donne l'octet haut aux deux ; res 3 posée au cycle-ligne ≥ 64, retour ≥ 72 et
  ≤ 32 cycles après → lignes suivantes décalées de 0/4/8/12 px, compteur −0/−2/−4/−6 octets
  (`vc_.stopInc`). `NEOST_V2` ne gate plus que le raccourcissement HBL (opt-in). Restes :
  rubrique « ouverts ».
- **[V3] Attribution de ligne à la grille réelle** · basse · clos le 2026-09-01 —
  `NEOST_LINELEN_ATTR` (ON, `ShifterInternal.hpp:31`) et `NEOST_LINELEN` (ON, `Shifter.cpp:93`)
  ; longueur de ligne persistante (≙ `nCyclesPerLine`) ; exhibiteur `freq_switch` : 18/18
  écritures freq à la même ligne et au même cycle qu'Hatari, `=0` fait rougir l'étalon.
  Closure n'exerce pas V3 (bit-identique avec et sans, 2026-08-30). Réserve : rubrique
  « ouverts ».
- **NO_DE / BLANK / STOP_MIDDLE sans palette** · moyenne · 2026-10-19 — `Shifter::finishFrame`
  (`Shifter.cpp:609`) ne lançait `Shifter::renderGlueFrame` que sur bordure élargie, écriture
  palette ou VBlank : les masques restaient invisibles (`blankline_node_st` : 157 px sur la
  ligne masquée). Rendu désormais déclenché aussi par NO_DE/BLANK/STOP_MIDDLE ;
  `bordersTrick_` garde son sens de cadrage. ≙ `Video_CopyScreenLineColor @ f0736b24` : NO_DE
  saute la lecture et l'avance RAM, BLANK efface après lecture, STOP_MIDDLE raccourcit.
  `BORDERMASK_EMPTY_LINE` et `BORDERMASK_BLANK_LINE` ne sont plus produits à ce pin : aucun
  port de leurs branches mortes. Gardes : onze contrôles GLUE, sondes `blankline_*`.
- **NO_SYNC / SYNC_HIGH** · gardes ajoutées le 2026-10-19 — les branches correspondaient déjà
  à `Video_Update_Glue_State @ f0736b24` (impulsion hi-res avant HSYNC ou pendant, masque
  NO_DE/NO_COUNT sur la ligne suivante, extension d'une ligne de la fenêtre basse ; retour
  avant le comparateur = annulation ; NO_COUNT protège NO_DE au retour SYNC_HIGH). Générateur
  `make_blankline_test.py` (quatre séquences, ST et STE) ; 52 contrôles GLUE (masques
  live/replay, pas de tic Timer B sur la ligne vide, stride nul puis normal, extension basse,
  32 000 octets consommés). Ne démontre pas la forme d'onde HSYNC/VDE réelle.
- **Longueur de la ligne qui commence = registres LIVE** · 2026-10-22 — `Machine::onHbl`
  (`Machine.cpp:675`) replanifiait sur la longueur verrouillée à `beginFrame` (`cpl_`) alors
  que le Timer B suivait la GLUE réelle (Super Hang-On : HBL à 508 contre lignes de 512, −4
  cycles par ligne, première VBL 1 048 cycles trop tôt). Désormais `Shifter::liveLineLength`
  (`Shifter.hpp:371`) à chaque HBL (≙ `Video_StartHBL` + `Video_HBL_GetDefaultPos`,
  `video.c:3541/3134` ; mux du compteur `hsc`, `gstmcu.v:779-801`) — fréquence seule (512/508)
  ; les lignes 224 restent décidées par le verrou de trame et les Freq_match 71 Hz. Restart du
  compteur reposé sur la grille réelle quand des lignes sont raccourcies. Effet : phase E-clock
  tournée d'un cran, étalons `spec512_*`, `cuddly_demos`, `nocooper`, `dmasnd_poll`,
  `blankline_blank_st/ste` recalés sur une trame bit-identique.
- **Événement échu servi à la frontière d'instruction** · 2026-10-22 — un `MFP_IRQ` posé « à
  now » après la boucle de `runTo` laissait `Machine::runFrame` exécuter une instruction de
  plus (tic Timer B dans un `bra.s` : exception 4 cycles après Hatari). Servi désormais avant
  toute instruction (une passe, anti-livelock), comme la boucle CPU d'Hatari.
- **Fin de trame et dernière ligne** · 2026-10-22 — `Machine::syncFrameEnd_`
  (`Machine.hpp:468`) recalcule la fin de trame dès que la longueur de la ligne en cours est
  connue (trame de reset 60 Hz passée à 50 Hz : 4 cycles trop courte ; sonde `timer_b_event`
  Δ = 6 au lieu de 2). ≙ VBL posée depuis le dernier HBL réel (`video.c:3370`).
- **Fin de trame sur ligne raccourcie** · 2026-10-22 — `lineCarry_` cumulé sans raccourcir
  `frameEnd_` : VBL servie avec le retard du raccourcissement (224 cycles après une ligne
  « no DE » hi-res tardive). `blankline_node_late_st` : 10 714 → 0 px.
- **STE : écarts refermés par les démos DHS** · 2026-11-03 — exhibés par Double Rez Trouble et
  Sea of Colour (`doublerez_ste`, `doublerez_tall_ste`) : `pVideoRasterDelayed` manquant
  (compteur écrit en bordure droite), trame démarrée en moyenne résolution décodée entière en
  moyenne, placement des lignes `LEFT_OFF_2_STE` et `LEFT_OFF_2_STE_MED`, fenêtre verticale
  nominale au lieu de `nStartHBL`/`nEndHBL`, attente du CPU facturée aux accès du blitter.
  Hatari fait foi ici au grade D seulement : aucun relevé sur machine.
- **Datation res 3 sur la grille réelle** · 2026-10-20 — `Shifter::realLinePos`
  (`VideoGlue.cpp:463`) date sur `glueLineStart_` (≙ `Video_ConvertPosition`, `video.c:1224`) ;
  l'ancienne grille nominale `fc/cpl` dérivait de 4 cycles par ligne de 508. Avec
  `NEOST_LINELEN_ATTR=0`, retour à la grille nominale.
- **`$FF8260`/`$FF8261` en lecture** · 2026-07-09 — port de `Video_Res_ReadByte`
  (`video.c:5281`) : bits 2-7 forcés à 1 sur STF/Mega ST, à 0 sur STE ; `$8261` rendait `$FF`
  (void). `Shifter::read8` (`Shifter.cpp:1379`).
- **[B15 (6ᵉ p.)] Compteur vidéo sur 22 bits** · 2026-10-20 — `kVideoAddrMask` = $3FFFFF
  (`Shifter.hpp:591` ≙ `Video_GetAddrMask` / `DMA_MaskAddressHigh`) à l'avance de ligne, au
  restart, en lecture/écriture `$FF8205/07/09`, à la capture et sur chaque mot fetché ;
  glue-selftest « b15 ».
- **`$FF8264` du STE relit la valeur écrite** · `bc15a67` — ≙ `Video_HorScroll_Read_8264`
  (`ioMemTabSTE.c:122`).
- **`VoidRead_00`** · commit `88ec84a` — Hatari ne rend 0x00 que sur `$FF820B`,
  `$FF8262-63`, `$FF8266-7F` du STE (`ioMemTabSTE.c:98,121,124`) ; NeoST exactement là, 0xFF
  ailleurs.
- **Scroll fin du STE par ligne au re-rendu** · 2026-08-07 — `Shifter::renderGlueFrame`
  appliquait le scroll de fin de trame à toutes les lignes ; capture par ligne
  (`lineScrollSnap_`, même datation que `lineSnap_`), save-state v9.
- **Compteur vidéo qui reculait entre deux lectures** · 2026-08-06 — `Shifter::videoCounter`
  (`VideoCounter.cpp:203`) créditait `line-offset+prefetch` aux lignes NO_DE, incohérent avec
  le commit `endVideoLine`.
- **Seuil Spectrum 512 à 512 accès** · 2026-07-12 — ramené à 1 comme Hatari : plus de
  bascule de chemin de rendu d'une trame à l'autre.
- **VBlank** · 2026-10-18 — comparateurs 50/60 Hz et remise à ON au VSYNC
  (`video.c:3443-3487`) ; lignes masquées à la palette 0 sans avancer la base vidéo ; étalons
  `vblank_on/off` (640 → 0 px). Le compteur intra-ligne garde le calcul d'adresse d'Hatari.

### Faux positifs

- **« CyclesPerVBL ±4 »** (2026-08-25) — l'affectation pleine de `CyclesPerVBL` n'existe chez
  Hatari que dans la branche VDI (`video.c:4937-4942`) ; en mode normal la VBL vient de la
  chaîne `ShifterLines[].StartCycle` (`video.c:3354, 3370-3374`), que `lineCarry_` /
  `beginFrame_` reproduisent. Mesuré sur No Cooper : longueurs de trame {160256 : 6788,
  160252 : 10, 133604 : 3}, 160252 = 313×512−4.
- **Filtre « écriture redondante » absent** (2ᵉ passe) — présent : `Shifter::recordSyncWrite`.
- **« Bordures en basse résolution seulement »** — l'overscan med-res est porté depuis V2.
- **`CopyScreenLineMono` « sans GLUE »** (cartographie 2026-07-08) — la fonction d'Hatari n'a
  aucune gestion de tricks non plus.

---
## 3. FDC WD1772, DMA, STX, IPF — `Fdc.cpp`, `StxImage.cpp`, `WrittenTrack.cpp`, `IpfImage.cpp` ↔ `fdc.c`, `floppies/stx.c`, `msa.c`, `dim.c`, `ipf.c`

**Concordant (dans le périmètre décrit)** : machines à états type I-IV, bits de statut par
type, timings (spin-up, head-load, index), `NextSectorID`, FIFO DMA de 16 octets et stall CPU
de 32 cycles bus par flush (`Fdc::billDmaCycles`, `Fdc.cpp:1368` ≙ `fdc.c:1340,1396`), wait
states de 4 cycles CPU à l'écriture de `$8604/$8606` et à la lecture de `$8604` hors SCREG,
aucun sur la lecture de `$8606` ni les adresses (`Fdc::registerWait`, `Fdc.cpp:1498`, depuis le
2026-10-18), densité `$FF860E` (Mega STE seulement ; 4 cycles CPU facturés comme Hatari, à
mesurer), fuzzy bits et timings STX, persistance `.wd1772` (conteneur lisible par Hatari,
extensions NeoST qualifiées ci-dessous), sélection lecteur/face poussée à chaque écriture du
port A du PSG (`Fdc::refreshDriveSide`, `Fdc.cpp:1186` ≙ `psg.c:419-420` → `FDC_SetDriveSide`),
FORCE INTERRUPT et IRQ forcée, reset FIFO DMA par bascule du bit 8 (statut d'erreur conservé,
octets restants annulés ; seul le reset matériel efface l'erreur, ≙ `FDC_Reset`/`FDC_ResetDMA`),
retenue des bits 7/15 de `$FF860B/0D` sur ST et Mega ST puis masque d'adresse (STE/Mega STE
sans retenue ; garde `tests/fdc_register_logic.cpp`). La conversion Mega STE 16 MHz suit
Hatari (2 cycles bus), sans mesure physique.

### Écarts ouverts

- **Secteur sans données (`pData == NULL`) sans RNF** · ouvert · basse — lu 512 × `0x00` avec
  statut OK (image de bord). NeoST : `Fdc::readSectorStx` (`Fdc.cpp:1967`,
  `sec.pData ? … : 0`).
- **Signal Disk Change → GPIP4** · ouvert · basse — non émulé (TT seulement chez Hatari) ;
  le modèle WPRT est distinct.
- **Face omise d'un fichier `.st`** · ouvert [F] · basse · 2026-11-10 — Hatari
  `fdc.c:5184-5205 @ f0736b24` synthétise des ID selon la géométrie du lecteur, puis la lecture
  de données échoue ; NeoST garde cette convention. Le fichier ne dit pas si la face d'origine
  était vierge ou formatée : tranché par un dump de cette face ou une convention de
  représentation, sans confondre ce manque avec le délai du 1772.

### Écarts voulus — ne pas réaligner

- **[D1] WRITE TRACK STX réinterprété** · assumé · moyenne — NeoST relit le flux MFM écrit et
  le rend aux lectures (`StxImage::reinterpretSaveTrack`, `StxImage.cpp:345` ;
  `Fdc::writeTrackStx`, `Fdc.cpp:2067` : `$FE` → ID, `$FB/$F8` → données). Depuis le 2026-11-11 l'analyse est celle de la puce
  (`wdtrack::parse`, `src/io/WrittenTrack.cpp`) : marques derrière trois `$F5`, CRC relus,
  drapeaux Pasti posés d'après les défauts (CRC d'ID → RNF+CRC, ID sans données → RNF, CRC de
  données → CRC, `$F8` → RECORD TYPE) ; une piste formatée avec des CRC faux les garde.
  L'ancien « CRC régénéré, statut neutralisé » ne vaut plus. Hatari ne relit pas
  (`FDC_WriteTrack_STX`, `stx.c:2027-2134`, `pDataRead = NULL`, TODO « convert pDataWrite
  into pDataRead »).
  Impact : utilisable par les formateurs ordinaires ; à arbitrer pour les protections
  « formate puis relit » (Copylock).
- **[D2] READ TRACK STX rend la piste réécrite** · assumé · moyenne — conséquence de D1
  (`Fdc::readTrackStx`, `Fdc.cpp:2234` ; Hatari `FDC_ReadTrack_STX`, `stx.c:1863`). C'est une
  reconstruction, sans promesse de flux brut exact.
- **[D3] Cadence disque indépendante du vol de bus CPU** · voulu [B] · 2026-11-04 — les 32
  cycles par bloc DMA sont facturés au CPU et au Scheduler, mais retirés du réarmement FDC :
  4 096 cycles pour 16 octets MFM au lieu de 4 128 (`Fdc::billDmaCycles`, `Fdc::onFdcEvent`,
  `Fdc.cpp:3191`). [B] WD177X-00 p. 1-1, 1-5, 1-13 : flux série continu, indépendant du bus
  RAM. Hatari (`fdc.c:2332,2388 @ f0736b24`) conserve le stall après compensation du retard.
  L'attente d'index, calculée depuis l'horloge live, est réarmée depuis cette même horloge. Le
  banc vérifie séparément cadence, travail CPU et fin de commande ; les captures Hatari sont
  intactes. (Avant le 2026-11-04, « due + stall + delay » prouvait seulement l'accord avec
  Hatari.)
- **WD1772, six points de fiche** · voulu [B] · 2026-11-04 — WD177X-00 : Restore pose DR à 0
  et le sens vers la piste 0 (p. 1-7) ; Seek Error après 255 pas seulement si V = 1 (p. 1-8) ;
  STEP sortant en piste 0 : TR passe à 0 (p. 1-7) ; au repos, `$Dx` efface RNF et CRC
  (p. 1-16) ; CRC d'ID faux : bit CRC posé, RNF+CRC au final (p. 1-10, p. 1-18), et il survit
  à la lecture du statut en type I (p. 1-8). Hatari fait autrement sur tous ces points. 80
  disques joués au clavier et au joystick, images identiques avant et après.
- **WD1772, écriture** · voulu [B] · 2026-11-04 — INTRQ 24 µs après le dernier CRC d'un Write
  Sector, et `$F7` compté pour deux octets avec une seule DRQ en Write Track (WD177X-00
  p. 1-12, 1-13, 1-15). Hatari finit ~1 µs après le CRC (`fdc_write`, écart accepté de 184
  cycles dans `run_timing_oracle.py`) et compte `$F7` pour un octet.
- **ID sans champ de données** · voulu [B] · 2026-11-11 — WD177X-00 p. 1-9 (Read Sector) :
  sans marque de données dans les 43 octets qui suivent le CRC d'ID, la puce recherche l'ID à
  nouveau, RNF après cinq tours ; un doublon porteur de données est donc lu au passage. Write
  Sector ne cherche pas de marque : il écrit la sienne 22 octets après le CRC d'ID (p. 1-12).
  Hatari (`stx.c:1626-1627, 1731-1732 @ f0736b24`) rend RNF aussitôt, en lecture comme en
  écriture. NeoST relance la recherche (`Fdc::foundIdHasNoData`, `Fdc.cpp:1765`,
  `RUN_RS_TRANSFER_START`) ; Write Sector donne au secteur un champ neuf, persisté dans le
  `.wd1772`. Même règle sur une piste `.ST` réécrite (`FloppyDisk::stOverride`). A/B du
  2026-11-11, binaire avant contre après, captures toutes les 250 trames (19 STX de
  `disks/stx`, Ivanhoe et The Union Demo en ST, TOS 1.04 UK, 4 000 trames ; Lethal Xcess en jeu
  sur 23 000 trames) : tout identique ; seul ID sans données de la collection : Lethal Xcess
  disque 1, piste 79 face 1, secteur 1, non lu par la recette — effet attendu : RNF au
  cinquième index (~1 s) au lieu d'immédiat.
- **Marques d'une piste écrite (`.ST`)** · voulu [B] · 2026-11-11 — WD177X-00 p. 1-13 : en
  MFM, seul `$F5` écrit un A1 de synchro ; `$FE`, `$FB`, `$F8` ne sont des marques que derrière
  trois A1 (p. 1-18 ; [D] MAME `wd_fdc.cpp:1853-1880 @ ce391499d37d`) ; `$F7` écrit le CRC.
  `Fdc::parseWrittenTrack` (`Fdc.cpp:1738`) relit la piste comme la puce : CRC d'ID faux → ID
  ignoré (RNF+CRC), CRC de données faux → S3, marque hors fenêtre → ID sans données. Seule une
  piste entièrement saine va dans le `.st`, le reste en mémoire. `Flopfmt` + `Floprd` vérifiés
  sous TOS 1.04 et EmuTOS (pistes 0-2 relues en `$E5`). [F] CRC préchargé au premier `$F5`
  d'une suite seulement (la fiche dit « chaque ») : tranché par un format à 1 ou 2 `$F5` relu
  sur un vrai 1772.
- **Registre de données relu** · voulu [B] · 2026-11-11 — WD177X-00 p. 1-3 : chaque octet
  transféré passe par le DR ; relu en `$FF8604` (A1A0 = 11), il rend le dernier octet lu ou
  écrit. NeoST (comme Hatari) le laissait à la dernière valeur écrite par le CPU.
- **Piste sans ID : cinq index avant RNF** · conforme [B] · 2026-11-10 — l'organigramme
  type II de WD177X-00 p. 1-10 (SHA-256 du PDF
  `a2b267094ec9af10c0f4d68fbc5c6ea94ec873897ad9b129a2ff607ad7fe8388`) exige cinq impulsions d'index
  (la prose p. 1-9 dit « four revolutions ») ; `testFdcTrackIdLocation` formate une piste en
  `$4E` seulement et vérifie READ et WRITE SECTOR : aucune IRQ avant cinq index, RNF seul,
  aucun octet DMA (178 contrôles) ; les impulsions sont observées par le puits public
  `FdcSound::Index`, sans accès aux états privés. Le cœur satisfaisait déjà ce cas ; aucun
  timing modifié. Pas une mesure physique du lecteur ni de la phase exacte d'INTRQ.
- **READ ADDRESS : CRC d'ID après le sixième octet** · voulu [B] · 2026-10-19 — selon WD1772 et
  MAME ; Hatari partageait l'omission. CRC de données sans effet ici.
- **WPRT après la montée en régime** · voulu [B] · 2026-10-20 — WRITE SECTOR teste la
  protection dans `RUN_WS_MOTORON`, après le spin-up (6 index si h = 0) et le délai E, avant la
  recherche d'ID, et à chaque secteur d'un multi-secteurs ; WRITE TRACK dans `RUN_WT_MOTORON`,
  avant l'attente de l'index. Organigrammes type II/III du WD177x, MAME `wd_fdc.cpp`
  (`SETTLE_DONE` → `wpt_r`). Hatari faute WRITE SECTOR dès le premier événement et WRITE TRACK
  après l'index (jusqu'à un tour trop tard). Test : `tests/fdc_media_change_logic.cpp`.
- **Densité HD/ED du STX** · voulu (NeoST plus cohérent) · basse — `BitPosition` recadré sur
  la densité (`Fdc::densityFactor`, `Fdc.cpp:1113`), identique en DD ; depuis le 2026-10-20 la
  durée de lecture standard d'un secteur (`ReadTime` nul) vaut 32 µs/octet ÷ `densityFactor()`
  (16 µs en HD, 8 en ED), cohérente avec `Fdc::nextSectorIDStx`. Hatari garde la constante DD :
  un secteur HD y dure deux secteurs et le suivant coûte un tour.
- **CRC de données STX réinscriptible** · voulu [B] · 2026-10-20 — un WRITE SECTOR sur un
  secteur à CRC de DONNÉES faux (statut Pasti CRC sans RNF) écrit un champ neuf au CRC juste ;
  la relecture ne signale plus de CRC. Le WD1772 ne contrôle que le CRC d'ID (MAME
  `wd_fdc.cpp` `write_sector`). Hatari refuse ce cas (`stx.c:1735`, contre son propre
  commentaire « ID field CRC » ligne 1704) et annonce un succès sans rien écrire. Un CRC d'ID
  faux reste refusé. Test : `tests/stx_data_crc_write_logic.cpp`.
- **Compteur de secteurs DMA appliqué à l'ACSI** · voulu [C] · 2026-10-20 — transfert borné à
  count × 512, rien à compte nul (données en attente jusqu'à un compte programmé et un retour
  en mode DMA), décompte par bloc de 512 octets (`$8606` bit 1 = 0 après un transfert complet ;
  une queue < 512 le laisse). Même puce DMA que la disquette ; le diagnostic Field Service
  vérifie ce bit. Hatari (`hdc.c`, `Acsi_DmaTransfer`) ignore le compteur. Test :
  `tests/fdc_acsi_logic.cpp`.
- **Compteur DMA en écriture** · assumé [F] — décrémenté au 33ᵉ rechargement de 16 octets
  ([D] Hatari `fdc.c:1421-1426 @ f0736b24`) quand MiSTery décompte au 32ᵉ
  (`atarist/dma.v:342-387 @ a00deb5b`) ; aucune source primaire de la puce DMA du ST.
  Commentaire à `Fdc::fifoPull` (`Fdc.cpp:1441`).
- **STX : piste absente formatée** · voulu [B] · 2026-11-12 — un WRITE TRACK sur une piste
  que l'image ne décrit pas (au-delà de la dernière, 80-82 compris ; face 1 d'une STX simple
  face) crée la piste (`StxImage::addTrack`, `StxImage.cpp:282`) puis suit D1/D2. [B] WD177X-00
  p. 1-13 : formater = placer la tête et lancer Write Track, LOST DATA ne signale qu'un
  registre non servi. Hatari rend LOST DATA et perd l'écriture (`stx.c:2036-2042 @ f0736b24`).
  Persistance par blocs `NTRK`/`NSCT` (dispositions de TRCK/SECT, hors compteurs d'en-tête),
  parce qu'Hatari rejette le `.wd1772` entier quand un TRCK ou un SECT ne trouve pas sa piste
  (`stx.c:677-685`, `648-657`) et saute les blocs inconnus (`stx.c:692-699`). Test :
  `tests/stx_absent_track_logic.cpp`.
- **`.wd1772` : secteurs d'une piste réécrite** · assumé · basse · 2026-11-12 — Hatari ne
  réinterprète pas un TRCK (« TODO : compute interpreted track », `stx.c:771 @ f0736b24`) et ne
  place un SECT que sur un secteur d'origine (`STX_FindSector_By_Position`, `stx.c:1377-1396`) :
  un SECT écrit sur une piste réécrite faisait rejeter tout le fichier. NeoST écrit ces secteurs
  en `NSCT`, qu'Hatari saute ; Hatari relit donc les secteurs d'origine de ces pistes :
  divergence de contenu assumée, plus de rejet. Test : `tests/stx_sector_dam_logic.cpp`.
- **WRITE TRACK `.ST`, emplacement des ID** · voulu [B] · 2026-10-19, révisé le 2026-10-22 et
  le 2026-10-24 — un flux qui colle à la géométrie de l'image (C/H = tête et face, numéros
  1..spt uniques, 512 o) est écrit dans le `.st` ; un flux non représentable n'est plus rejeté
  par LOST DATA (LOST DATA = DRQ non servie, `fdc1772.v:885`) : la piste est gardée en mémoire
  (`FloppyDisk::stOverride`, volatile, hors `.st` et hors save-state) et relue selon ses ID
  (READ SECTOR/ADDRESS/TRACK, WRITE SECTOR, recherche d'ID) ; un WRITE TRACK standard la rend
  au `.st`. Hatari refuse tout WRITE TRACK ST (LOST DATA, TODO). Corrige le test F de
  STE_Test v1.9 (secteurs 10-18 sur la piste 79, « Disk A: DS / Disk B: DS », Pass). Les ID
  n'actionnent ni STEP ni SIDE. Le 2026-10-24 : READ/WRITE SECTOR et READ ADDRESS servent le
  champ ID trouvé sous la tête (`Fdc::ovSecIdx_`) et non le premier du même numéro (pistes à
  ID dupliqués) ; READ ADDRESS ne compare plus l'octet de taille brut à ses bits 0-1 ; les
  positions angulaires comptent `$F7` pour deux octets (`fdc_track_id_location_logic.cpp`).
- **STX : WRITE SECTOR A8/A9, F8 persistant** · extension · 2026-10-19 — bit 0 choisit FB
  normal ou F8 supprimé ; statut RECTYPE et CRC du READ TRACK reconstruit cohérents. F8
  persiste dans un supplément SECT `NDAM` + marque ; un SECT ancien sans métadonnée vaut FB.
  Hatari lit les données mais ignore la métadonnée ; une réécriture par Hatari ou un ancien
  NeoST peut la perdre. WRITE TRACK conserve aussi la marque supprimée (RECTYPE à la lecture,
  READ TRACK reconstruit), CRC régénéré sur la marque choisie ; Hatari rend le flag en READ
  SECTOR mais son READ TRACK reconstruit encore FB. ST/MSA/DIM sans DAM persistante.
- **STX : READ TRACK sur image décodée** · extension · 2026-10-19 — fusion locale des
  overlays SECT dans une copie du dump, y compris les champs coupés par l'index : parcours
  circulaire strictement inférieur à un tour, synchronisation zéro, ID aligné dans le dump,
  données non fuzzy et CRC concordants ; seuls DAM/données/CRC remplacés, longueur, timings et
  octets extérieurs conservés ; collisions, ambiguïtés, champs incomplets et traversée d'une
  fin de dump écrêtée refusés. Aucune reconstruction de secours ni mutation du dump.
- **STX : piste effacée** · extension · 2026-10-19 — un WRITE TRACK sans secteur exploitable
  remplace la vue des secteurs par une vue vide ; les anciennes données ne réapparaissent pas,
  même après remontage du `.wd1772`.
- **IPF sans libcapsimage** · assumé · 2026-10-22 — Hatari délègue l'IPF à libcapsimage
  (licence SPS non libre, chargée dynamiquement) et la lit par son moteur `_MFM`/DPLL. NeoST
  décode le conteneur (adaptation de MAME `ipf_dsk.cpp`, BSD-3), lit le flux de cellules une
  fois au montage avec un séparateur idéal (règles MAME `wd_fdc.cpp`) et monte le résultat sur
  le chemin STX. Identique pour les protections « secteurs » (ID non standard, CRC voulus,
  chevauchements, pistes longues, densité variable, weak bits sur les données) ; divergent pour
  ce qui exige un DPLL vivant (perte de synchro aléatoire, ID instables d'un tour à l'autre).
  Non comparable à l'oracle local (bâti sans capsimage).
- **MSA/DIM en écriture directe** · assumé · 2026-08-01 — NeoST écrit à chaque secteur
  (`.ST` et `.DIM` in situ, en-tête 32 o préservé ≙ `dim.c:134-149` ; `.MSA` ré-encodé en entier
  et atomiquement, tmp + rename, `encodeMsa` ≙ `MSA_WriteDisk`/`MSA_FindRunOfBytes`
  `msa.c:275-420`) ; Hatari n'écrit qu'à l'éjection (une coupure y perd la sauvegarde).

### Refermés

- **[D0] `.MSA`/`.DIM` montées en lecture seule, WPRT présenté au programme** · moyenne ·
  2026-08-01 — Hatari ne dérive WPRT que du réglage et de `stat()`, jamais du format
  (`floppy.c:205-225`, `Floppy_IsWriteProtected`) ; chez NeoST le drapeau pilotait le statut
  du WD1772 (`Fdc::updateWriteSectors`, `Fdc::updateWriteTrack`, type I) : sauvegardes,
  high-scores, écritures du bureau et protections « écrit puis relit » échouaient. Dispatch de
  `Fdc::writeBack` (`Fdc.cpp:1791`) par format ; `writeProtect` ne vient plus que de `stat()` ;
  `!raw` ne subsiste que là où l'on ne sait pas ré-encoder (STX, en-tête reconnu mais
  indécodable). Garde : `neost-headless --msa-selftest` (étalon `msa_selftest`, palier
  `fast`), 44 cas (6 géométries × 7 motifs, `$E5` isolé, runs longs, piste incompressible,
  montage → écriture → remontage `.msa` et `.dim`). L'ancienne mention « conformes (vérifiés
  ligne à ligne) » était fausse et avait masqué l'écart.
- **Sélection lecteur/face poussée par le PSG (`D-PSG`)** · moyenne · 2026-08-25 — chemin
  manquant : un programme qui écrivait sa commande avant de sélectionner le lecteur restait à
  `driveSel_ = -1` (Stardust STE : gel noir). Cf. « Concordant ».
- **Phase INDEX des médias** · 2026-10-18 — éjecter le lecteur actif coupe l'INDEX ; insérer un
  média moteur lancé amorce une nouvelle phase ; le lecteur non sélectionné ne change pas la
  phase active (≙ `FDC_EjectFloppy/FDC_InsertFloppy`). Les bits « ready » ne correspondent à
  aucune entrée WD1772 câblée sur le ST et ne produisent pas de transition chez Hatari.
- **Sélection live et secteurs** · 2026-10-18 — les primitives ST/MSA/DIM vérifient secteur
  non nul et ≤ secteurs/piste sur le lecteur réellement actif ; un changement de lecteur entre
  ID et données ou pendant le DMA rend RNF si le secteur manque (≙
  `Floppy_ReadSectors/Floppy_WriteSectors`). STX garde sa délégation.
- **Changements rapides de média** · 2026-10-19 — première action puis dernière action
  opposée, la répétition de la première annulant la seconde ; remplacement direct = éjection
  puis insertion, comme `Floppy_DriveTransitionSetState` (deux phases, v41) ; 18 VBL réels par
  phase (fenêtre historique de 18 durées de trame), même lors d'un changement PAL/NTSC/mono
  (compteur et fins VBL en v43). Le compteur survit au reset FDC ; aucune équivalence du reset
  global d'Hatari (`Video_Reset` remet `nVBLs` à zéro) revendiquée. Remplace l'ancien écart
  « hot-swap en 1 phase contre 2 » (`floppy.c:438-514`).
- **Secteur long et sélection STX→ST** · 2026-10-19 — un WRITE SECTOR engagé sur un ID STX de
  1 024 octets puis terminé sur un lecteur ST est limité à 512 octets (≙ `FDC_WriteSector_ST` /
  `Floppy_WriteSectors`, un secteur logique par appel).
- **INTRQ doublée** · vérifié sans défaut, 2026-07-09 — le `raise(SRC_FDC)` doublé a disparu
  à la refonte M1 ; chemin unique `Fdc::fdcSetIrq` (gardé par front) → `Fdc::setIntrqLine` →
  `Mfp::setFdcLine` → `Mfp::gpipSetLine` (`if (line == active) return` + front AER) ; une INTRQ
  maintenue ne régénère aucune IRQ, aucun chemin ne contourne `gpipSetLine`.
- **Bornes du parseur STX** · vérifié sans lecture hors bornes, 2026-08-06 —
  `StxImage::parse` (`StxImage.cpp:74`) rejette une image tronquée qu'Hatari monterait ; une
  piste simple tronquée pose `sectorsCount = 0` (le contrôle de taille `dataBytes` échoue avant
  `buildSectorsSimple`), les quatre
  consommateurs bornent sur `sectorsCountView()` (RNF après 5 tours) ; clamp de
  `TrackImageSize` (`7fe14bb`) et borne `stxNextSector_` 0..255 au save-state.
- **Reste de la 2ᵉ passe (2026-06-15)** — `dmaResetFifo` ne remettait pas
  `bufPos_`/`dmaBytesToTransfer_` : couvert par le reset FIFO du 2026-10-18 (octets restants
  annulés) ; recalcul de densité à chaque lecture du statut : coût sans effet ; absence
  d'`indexCheckUpdate` avant la commande : l'équivalent existe aux points de consommation
  (`Fdc::indexCheckUpdate` en tête de chaque événement et à la lecture de STR).

### Faux positifs

- **[D4] Piste de 6 268 contre 6 250 octets** (revérifié le 2026-10-18) — le
  `FDC_TRACK_BYTES_STANDARD` actif d'Hatari vaut 6 268 (`fdc.c:~410`, la formule 6 250 y est
  commentée) : identique.
- **`FDC_UpdateAll` avant chaque accès registre** (cartographie 2026-07-08) — équivalent aux
  points de consommation ; moteur arrêté = sans effet.

---

## 4. ACSI — `Acsi.cpp`, `Fdc.cpp` ↔ `hdc.c`

**Concordant** : réception des paquets (A1/ICD/LUN) ; toutes les commandes SCSI (TEST UNIT
READY, REQUEST SENSE, READ/WRITE 6 et 10, INQUIRY, READ CAPACITY, MODE SENSE 00/04/3F, SEEK,
FORMAT, SHIP, REPORT LUNS) ; géométrie de MODE SENSE ; table de partitions DOS et Atari ;
incrément et masque de l'adresse DMA ; court-circuit FIFO.

### Écarts ouverts

- **[B7 (6ᵉ p.)] `dmaWrite_` remis à faux à chaque commande** · ouvert · basse — Hatari le
  garde jusqu'au transfert (refus d'un sens contradictoire). NeoST : `Acsi.cpp`.
- **[B8 (6ᵉ p.)] Octet ACSI sans image** · ouvert · basse — Hatari ignore l'octet
  (`$FF8604` = 0) ; NeoST pose status = 2 (la sonde du TOS est identique).

### Écarts voulus — ne pas réaligner

- **Délai d'IRQ après le DMA** · conforme à l'oracle, non mesuré · 2026-10-18 —
  `Fdc::acsiDmaTransfer` (`Fdc.cpp:3570`) efface la source HDC puis programme
  `Scheduler::ACSI_IRQ` après 1 000 cycles CPU (`Fdc::onAcsiIrq`, `Fdc.cpp:3553`), comme
  `ACSI_TRANSFER_MIN_CYCLES` (`hdc.c`) : 1 000 cycles bus à 8 MHz, 500 à 16 MHz ; une bascule de
  vitesse garde les cycles CPU restants. Données et adresse DMA mises à jour immédiatement ;
  l'acquittement des octets reste immédiat. Le reset annule l'échéance, le DMA suivant la
  remplace. Délai de l'oracle, pas une mesure ni un test Idris OS.
- **Taille au montage** · assumé · 2026-10-19 — images de plus de UINT32_MAX secteurs
  refusées avant conversion (maximum NeoST 2 Tio − 512 o, limite des compteurs, pas du
  matériel). Slot UltraSatan conservé sans carte.
- **Borne signée grande capacité** · 2026-10-19 — compte READ/WRITE comparé aux secteurs
  restants sans conversion signée : le début d'une image de plus de 1 Tio est accessible (banc
  sparse POSIX, sauté sous Windows ou FS non sparse).
- **Destination d'un WRITE différé** · 2026-10-19 — cible et secteur figés avant l'ouverture
  du DMA, comme le FILE engagé par Hatari ; une nouvelle sélection pendant le paquet suivant ne
  redirige pas l'écriture (v45 ; v44 refusés).
- **Compteur de secteurs DMA** — § 3, « Compteur de secteurs DMA appliqué à l'ACSI ».
- **Coexistence GEMDOS / ACSI** · 2026-10-19 — dossier simple placé après le nombre de
  partitions ACSI, réévalué au montage et au reset ; lettres explicites C..Z inchangées ;
  ancien montage et handles conservés après refus d'un nouveau dossier ; table vidée au reset
  si aucune lettre libre, sans perdre la configuration ; retrait ACSI puis reset remappe le
  dossier ; l'interface n'annonce plus une collision C: systématique (masque GEMDOS réel).
  Limite commune à Hatari : comptage des racines DOS/Atari, pas d'exploration XGM.

### Refermés

- **INQUIRY `buf[4]`** · moyenne · 2026-06-15 — « Additional Length » garde la valeur fixe 31
  de `inquiry_bytes[4]` (`hdc.c:218-246`) ; il était écrasé par `count()-5`.
- **Statut DMA inversé dans `writeAcsi` (H1, 6ᵉ p.)** · haute · 2026-07-12 — bit 0 de `$FF8606`
  rapportait « erreur » après chaque octet de commande accepté (`Fdc::writeAcsi`,
  `Fdc.cpp:3479`).
- **[B6 (6ᵉ p.)] DMA ACSI hors plage** — statut READ ERROR et drapeau DMA corrigés sans
  toucher au sense disque ; la clause ROM WRITE ne s'applique pas aux configurations
  supportées (RAM ≤ 4 Mo, masque DMA $3FFFFE) ; sonde MMIO ST/STE/Mega STE, 12 contrôles
  ASan/UBSan.

### Faux positifs

—

---
## 5. Son (YM2149, DMA STE, Microwire/LMC1992) — `YM2149.cpp`, `DmaSound.cpp` ↔ `psg.c`, `sound.c`, `dmaSnd.c`

**Concordant** : générateurs YM (tons, bruit LFSR 17 bits, enveloppes, mixeur, masques,
latch de lecture), moteur 250 kHz, table DAC 32³, ET logique ton/bruit, demi-amplitude STE,
PWM + HPF, passe-bas C10 du STF (`YM2149::setStfLowPass`, appelé pour `!STE` ≙
`Config_IsMachineST`), rééchantillonneur 16.16, FIR anti-repliement du DMA, Microwire/LMC
(gains, panoramique), XSINT → GPIP7 + Timer A, resets froid et chaud, reset du port A.
L'ancien `docs/SOUND_HATARI_DIFF.md` (table DAC, ET ton/bruit, filtres, FIFO, start == end,
resets, `Mfp::reset`) a été supprimé : tout y était implémenté.

### Écarts ouverts

- **[B10 (6ᵉ p.)] `$FF8922`, octet compagnon** · ouvert · basse — Hatari lit 0 (réécrit à
  chaque décalage) ; NeoST combine avec le dernier mot latché (`DmaSound::read8`,
  `DmaSound.cpp:531`, case `0x22`).
- **Passe-bas du STF sans la garde `nAudioFrequency ≥ 40000`** · ouvert · basse — sans effet
  à 48 kHz.
- **DMA à l'arrêt hors mixage 1** · assumé · basse — `mixing ≠ 1` et DMA arrêté ne coupent
  pas le YM ; le dernier échantillon DMA n'est pas tenu en continu (Hatari, `dmaSnd.c:547-574,
  150-151`, tient l'offset et coupe le YM après une trame one-shot). Choix documenté à
  `DmaSound::mix` (`DmaSound.hpp:62-63`). À trancher avant tout diff WAV au bit près.

### Écarts voulus — ne pas réaligner

- **RESET sur STE et son DMA** · voulu [B] · basse · 2026-11-12 — NeoST suit la netlist ST-4081
  (f. 13, 14, 17) : début, fin écrite et compteur à 0, contrôle gardé (`DmaSound::resbReset`).
  Hatari (customreset) ne touche pas au son DMA.
- **Fin de trame du son DMA sur STE** · voulu [B] · basse · 2026-11-12 — NeoST date XSINT et le
  Timer A au fetch du dernier mot, hors DE ([B] ST-4081 f. 20, `DmaSound::armFrameEnd`) ;
  Hatari au HBL (`dmaSnd.c:727-741 @ f0736b24`), jusqu'à 136 cycles plus tard. Profondeur de FIFO
  [F] (TODO). Mega STE au HBL des deux côtés.

- **`$FF8921` relu sous le masque `0x83`** · voulu [B] · basse · 2026-11-11 — Hatari garde
  `0x8F` ; le manuel matériel du TT (Atari, « 8920 RW 0000 0000 a000 00bb ») et la
  transcription du STE Developer Addendum mettent les bits 2-6 à zéro, comme MAME et MiSTery.
  NeoST : `DmaSound::write8` (`DmaSound.cpp:582`), case `0x21`. (Avant : `mode_ &= 0x8F`,
  masqué à l'écriture.)
- **`$FF8920`, octet pair relu `0x00`, écriture ignorée** · voulu [B] · basse · 2026-11-12 —
  Hatari (`ioMemTabSTE.c:162 @ f0736b24`, non intercepté) en fait un octet scratch relisible,
  que NeoST suivait depuis le 2026-10-19. [B] *Atari TT030 Hardware Reference Manual*, page
  « DMA SOUND SUBSYSTEM » (p. 26 du PDF, SHA-256 `93c17bee…`) : « 8920 RW 0000 0000 a000 00bb »,
  zéros explicites là où les registres voisins portent « ---- ---- » ; [D] MiSTery
  `gstshifter.v:96,125 @ 4a6db7c` ne pose que l'octet bas. Page du TT, bloc son DMA identique
  au STE : tranché pour le STE par une lecture de `$FF8920` sur le fer. Le champ v42 reste dans
  le flux de save-state, inerte.
- **Compteur du son DMA sur 22 bits** · voulu [D-RTL] · basse · 2026-11-11 — il boucle de
  `$3FFFFE` à `$000000` et ne lit que la RAM, [D] `sndcnt.v:8-9` et `gstmcu.v:137 @ 4a6db7cc`.
  Hatari (`dmaSnd.c:353-361 @ f0736b24`) compte sur 32 bits par `STMemory_DMA_ReadByte` : une
  fin inférieure au début y traverse ROM et cartouche.
- **Compteur du son DMA sur STE, hors DE** · voulu [B] · basse · 2026-11-04 — il avance hors
  de DE, mot par mot, pas seulement au HBL (`DmaSnd_STE_HBL_Update`). [B] ST-4081 feuille 20,
  [D] `mcucontrol.v @ 4a6db7cc`, `tools/make_stcpu_snd.py`. `dmasnd_poll` reposé.
- **Son DMA du STE d'après la netlist** · voulu [B] · basse · 2026-10-22 — `$FF8901` relu sur
  4 bits (`REGISTER.PIN`) ; fin `$FF890F-13` relue via l'étage SFT, figé à chaque début de
  trame pendant la lecture (4081P14, `DmaSound::read8`). Mega STE inchangé
  (`DmaSound::steNetlist`, posé par le Bus, `Bus.cpp:1030`).
- **`$FF8920-3F` au créneau de la RAM, sur STE** · voulu [B] · basse · 2026-11-04 — [B]
  ST-4081 feuilles 19 et 21, CMAP par SMAPB. Hatari n'aligne que couleurs, résolution et
  `$FF8264/65`. Écart de 2 cycles au plus, seulement à une lecture ou écriture en phase 4n + 2.
- **Son DMA non coupé par l'instruction RESET** · voulu [B] · 2026-11-12 — [B] ST-4081
  feuille 13 : SNDON (PD105) ne se remet qu'à PORB, ITESTB et STOFF ; `gstmcu.v:438` le coupe
  sur resb, écart du RTL non suivi (cf. § 2, « Instruction RESET sur STE »).
- **Filtre C10 du STF** · voulu (défaut d'Hatari) · moyenne · 2026-11-04 — NeoST coupe à 7,7
  et 2,1 kHz, les valeurs du circuit que cite Hatari lui-même (`sound.c:439-440 @ f0736b24`) ;
  Hatari applique à 250 kHz des coefficients calculés pour 44,1 kHz, soit 43 et 11 kHz.
  NeoST : `YM2149::applyLpfStf250` (`YM2149.cpp:262`).
- **Bruit du YM à R6 = 0** · voulu (défaut d'Hatari) · basse · 2026-11-04 — NeoST rend R6 = 0
  égal à R6 = 1, comme Hatari dit l'avoir mesuré (`sound.c:1046 @ f0736b24`) ; son code tire le
  LFSR deux fois trop vite. Concordant avec le relevé de N. Pomarède sur STF (`per` = 0 ≡ 1
  pour le bruit et l'enveloppe).
- **Granularité du rechargement de la FIFO DMA** · voulu (Hatari non reproductible) · basse ·
  2026-09-02 — sur un poll serré de `$FF890B/0D`, NeoST rend le découpage par HBL idéal du
  débit : à 50 066 Hz stéréo, 100 132 o/s ÷ 15 650 lignes/s = 6,398 o/ligne, donc des sauts de
  6 avec 19,9 % de sauts de 8 (mesuré : 12 sauts de 8 pour 47 de 6, 20,3 %). Hatari jitte sur
  4/6/8/12 autour du même débit moyen, parce que sa consommation DAC passe par le
  rééchantillonnage vers le taux hôte, dont l'accumulateur court depuis le démarrage et dépend
  donc de la durée du boot, tirée au sort : deux runs Hatari identiques diffèrent de 664 à
  1 432 px (`--sound off` comme `--sound 50066`), et l'ancrage VBL n'y peut rien. Aucun oracle
  dérivable : `dmasnd_poll` en `snapshot`, premier étalon refusé à l'oracle pour
  non-reproductibilité d'Hatari. Confronté : sur la fenêtre des 100 polls, les deux avancent le
  compteur de 382 octets exactement.
- **Écritures R9/R10 au même pas 250 kHz que R8** · voulu · basse — 37,5 % des écritures d'un
  digidrum tombent dans un pas déjà occupé (R9 et R10, 8 et 28 cycles après R8) : les séparer
  synthétiserait des transitoires de 1 à 3,5 µs qui sont l'ordre d'écriture du 68000, que le
  passe-bas C10 efface. Pas un reliquat.

### Refermés

- **[S1] Passe-bas STF jamais activé** · moyenne · 2026-06-15 — `setStfLowPass(!STE)` appelé
  par `Machine` ; ST et Mega ST passent par `YM2149::applyLpfStf250` (≙ `LowPassFilter`,
  `sound.c:1946-1951`) au lieu de rester en PWM.
- **[S2] FIFO de 8 octets et avance au HBL du DMA STE** · moyenne · 2026-07-07 — FIFO en anneau
  fetchée par mots (`DmaSound::fifoRefill`, `DmaSound.cpp:105` ≙ `dmaSnd.c:342`) à chaque HBL
  (`DmaSound::onHbl`, `DmaSound.cpp:221`, appelé par `Machine::onHbl` ≙
  `DmaSnd_STE_HBL_Update`, `video.c:3322`) ; consommation datée au reste fractionnaire
  (`DmaSound::updateDac`, `DmaSound.cpp:191`) qui capture les octets au fetch (plus de
  relecture RAM en fin de trame : Mental Hangover, Power Up Plus) ; fin de trame au fetch
  (XSINT/Timer A en avance, quantifiée HBL) ; compteur `$FF8909` = adresse de fetch (≙
  `DmaSnd_GetFrameCount`) ; réalignement mono → stéréo (`DmaSound::fifoSetStereo` ≙
  `dmaSnd.c:419`). Étalon `tools/make_dmasnd_test.py` : 33,3 % d'octets B, oracle 33,2 %. Le
  2026-08-06, `DmaSound::liveCounter` (`DmaSound.cpp:516`) perd un `fifoRefill` de trop (≙
  `DmaSnd_GetFrameCount` n'appelle que `Sound_Update`), et `DmaSound::startNewFrame` ne remet
  plus `phase_`/`haveCur_` qu'au front PLAY 0 → 1 (≙ `DmaInitSample`).
- **[S3] Gain LMC ×2 manquant (YM STE −6 dB)** · moyenne · 2026-07-07 — `kLmcMakeup = 2.0`
  (`DmaSound.cpp:458` ≙ ×2 de `left/right_gain`, `dmaSnd.c:1152-1153, 1460-1461`) ; cloche GEM
  ST contre STE : rapport RMS 1,000. Le niveau du canal DMA a été corrigé ensuite (ligne
  suivante).
- **Niveau et signe du canal DMA** · moyenne · 2026-08-07 — `kDmaGain = −0.1875`
  (`DmaSound.cpp:38`) : le ÷4 d'Hatari (`dmaSnd.c:520-535`) couvre la demi-table STE et la
  pré-compensation du ×2 de `kLmcMakeup`, appliqué ensuite au mix entier. L'étape du
  2026-07-07 (0,375, « 3/4 level ») laissait le DMA +6 dB au-dessus d'Hatari ; sa validation
  mesurait NeoST contre sa propre lecture du commentaire, pas contre un WAV oracle. Le signe
  ×−1 (le LMC1992 inverse le canal DMA) date du 2026-08-07.
- **[S4] Table DAC** · moyenne · 2026-07-07 — la table MESURÉE est le défaut, comme
  `YM_TABLE_MIXING` (`configuration.c:807`) : mesures de Paulo Simoes 16³ vendorisées
  (`ym2149_fixed_vol.h`) et interpolées en 32³ (≙ `interpolate_volumetable`,
  `sound.c:505-543`) dans `YM2149::dacTable` (`YM2149.cpp:111`). Modèle de circuit sous
  `NEOST_YM_MIXING=model`.
- **HPF sous-sonique sur le mix STE** · 2026-08-07 — ST : HPF dans la chaîne YM
  (`sound.c:1744`) ; STE : YM brut dans le mix (`YM2149::setHpfBypass`), HPF sur YM + DMA
  (`DmaSound::applyHpfStereo`, `DmaSound.cpp:431`, et `applyHpfMono` ≙ `dmaSnd.c:699,706`),
  branché GUI, `--sound-dump` et WASM ; états non sérialisés. `Machine::reconfigure` repose
  `setHpfBypass` à chaud (2026-08-07) ; la chaîne LMC est gatée par la machine côté WASM.
- **Correcteur LMC1992 en plateaux Savinkoff** · 2026-08-07 — port exact de
  `DmaSnd_Bass_Shelf`/`DmaSnd_Treble_Shelf` (`dmaSnd.c:1366-1404`), coupures 118,2763 et
  8 438,756 Hz du 1ᵉʳ ordre, au lieu d'un RBJ 2ᵉ ordre 200/8000 Hz ; identité exacte à 0 dB.
- **Horloge YM 250 663 Hz** · 2026-08-07 — `YM_250_HZ` (`YM2149.hpp:367`) = MCLK
  32 084 988 ÷ 2 ÷ 2 ÷ 4 ÷ 8, même MCLK sur ST et STE chez Hatari (`clocks_timings.c`) ;
  l'ancien 250 000 jouait ~4,6 cents trop bas, et le « 250 332 STE » de la 5ᵉ passe était faux.
- **Écritures YM rejouées sur la grille 250 kHz** · moyenne · 2026-09-02 — `synthesizeFrame`
  (`YM2149.cpp:410`) datait chaque écriture en échantillons hôte, d'où une décimation des
  digidrums (Super Hang-On : 897 instants sonores par trame, 61 % perdus). Grille 250 kHz comme
  Hatari (`Sound_Update` avant `Sound_WriteReg`, `psg.c:346`). Spectre sur 4 s contre l'AVI
  d'Hatari, bande 10-16 kHz : 0,5 % avant, 0,3 % après, 0,3 % Hatari. Instrument :
  `NEOST_YMEV_DIAG=1`.
- **Bruit : incrément 125 kHz, comparaison 250 kHz** · `bc15a67` — plancher `per = 1` retiré
  (≙ `sound.c:1050-1058`).
- **Masque haut `$3F` de l'adresse DMA** — porté pour les profils RAM ≤ 4 Mo (≙
  `DMA_MaskAddressHigh`, `dmaSnd.c:867,909,948`) ; requalifié le 2026-10-19.
- **`$FF8900`, octet pair** · 2026-08-07 — lit 0x00 (registre MOT, `ioMemTabSTE.c:142`) ;
  l'ancien 0xFF faisait échouer un `move.w $FF8900,d0`.
- **Microwire, octet DATA** · 2026-10-19 — fusion avec la valeur visible après décalage, sans
  réutiliser l'autre octet d'une commande ancienne ; déclenchements haut/bas isolés, staging
  mot/long, rejet occupé et état v45 conservés.
- **Microwire, retard** · 2026-10-19 — décalages dus rattrapés sur la grille de huit cycles,
  au plus seize fronts (principe `PendingCyclesOver` d'Hatari) ; Data/Mask actualisés avant le
  MMIO ; nouveau départ à `liveNow + 8`.
- **Chemin mono du DMA** · 2026-10-18 — la queue après STOP était drainée ; l'ancienne API mono
  perdait des demi-échantillons à la moyenne L/R entière (moyenne flottante désormais).
- **Reset du registre R7 du PSG** · 2026-10-19 — remis à zéro selon Hatari et MAME (l'ancien
  `$FF` est retiré) ; la déviation documentée de la lecture de `$FF8802` est inchangée.
- **Méthode : aucun son en headless** · levé — `--sound-dump F` écrit un WAV 48 kHz stéréo
  s16 de la même chaîne que le GUI ; `NEOST_DMASND_TRACE=1` émet le rechargement FIFO au format
  « DMA snd fifo refill » d'Hatari.

### Faux positifs

- **`mode_` non masqué à la relecture** — masqué à l'écriture (puis `0x83`, ci-dessus).
- **« YM nu dès PLAY = 0 »**, **« kDmaGain 0,7 contre 0,75 »** (cartographie 2026-07-08) —
  périmés : drainage porté, gain repris le 2026-08-07.

---

## 6. ACIA 6850, IKBD HD6301, MIDI — `Ikbd.cpp`, `MidiAcia.cpp` ↔ `acia.c`, `ikbd.c`, `midi.c`

**Concordant** : modèle TIE, master reset du 6850, overrun et cadence série, fenêtre critique
de reset, tampon borné, pause `$13`, commandes usuelles et rapports `$07-$9A`, les six
programmes 6301 propres (Froggies, Transbeauce 2, Dragonnels, Chaos A.D., Audio Sculpture),
duplication feu/boutons (`IKBD_DuplicateMouseFireButtons` : le feu du joystick 1 compte comme
bouton droit en `$0D`, 2026-07-12). L'ancien `docs/IKBD_HATARI_DIFF.md` (TIE, master reset,
`$08`/`$1A`/`$19`/`$20`/`$21`/`$87-$9A`, pause, overrun, cadence, quirks de reset) a été
supprimé : tout y était implémenté.

### Écarts ouverts

- **RDRF tenu jusqu'à l'acquittement de l'overrun** · ouvert · basse · 2026-11-10 — l'essai
  du 2026-11-04 bloque cinq jeux : il faut un relevé.
- **Octet MIDI en vol au master reset ; format 7 bits + parité** · ouvert · basse ·
  2026-11-10.
- **Phase SCI sortante sous l'octet ; cadence pour les autres diviseurs** · ouvert · basse —
  approximative ; le modèle est borné au clavier /64 8N1 : la livraison IKBD → ACIA reste à
  10 240 cycles par octet (`kAciaRxByteCycles`, `Ikbd::armRx`, `Ikbd.cpp:1500`) quel que soit le
  diviseur de l'ACIA, là où Hatari la dérive de `Clock_Divider` (`acia.c:800-804`,
  `ikbd.c:1027-1032`) ; la garde d'initialisation, elle, est portée (2026-10-19). Le break
  suspend les départs sans simuler la ligne physique basse ni les erreurs SCI.
- **[B1 (6ᵉ p.)] Purge `$80,$01`** · ouvert · basse — avale l'octet « en vol » (Hatari :
  l'octet tiré vers TDR/TSR survit). Non requalifié depuis la refonte ROM du 2026-10-22.
- **[B2 (6ᵉ p.)] `Ikbd::bootRom` ne remet pas `mDeltaX_/Y_`** · ouvert · basse — Hatari les
  remet à 0. Non requalifié depuis le 2026-10-22.
- **[B4 (6ᵉ p.)] Master reset et TIE** · ouvert · basse — Hatari force SR = TDRE
  immédiatement, NeoST à l'échéance TX ; à relire avec « TDRE sous master reset » ci-dessous.
- **`IKBD_CheckForDoubleClicks` et joystick → barre d'espace** · ouvert · basse — absents ;
  confort d'avance rapide, non matériel.

### Écarts voulus — ne pas réaligner

- **IKBD `$19` selon la ROM** · voulu [C] · basse · 2026-11-12 — Hatari scrute toutes les ~1 ms avec des
  dixièmes de 100 ms et sans anti-rebond ; NeoST suit la ROM 6301 : dixièmes de 101 ms, prédiviseur par
  axe phasé à l'appui, anti-rebond de 4 ms (directions) et 9 ms (feu), bascule VX sans émission si TX=0,
  directions opposées ignorées (`Ikbd::sendKeycodeJoystick`).
- **Liaison ACIA→SCI** · voulu [B]/[C] · basse · 2026-11-12 — Hatari émet toujours en 8N1 et ignore
  l'erreur de trame ; NeoST émet le format du CR ([B] MC6850 § Word Select) et applique le gestionnaire
  ORFE de la ROM ($FF40) et le reset par BREAK ($FE29/$F00B) (`Ikbd::sciFramingError`).

- **Octets impairs `$FFFC01/03/05/07` sur STE** · voulu [D-netlist] · basse · 2026-11-11 —
  Hatari les tient « void » (`ioMemTabSTE.c`, `IoMem_VoidRead/Write`, sans attente). Le GST MCU
  sélectionne l'ACIA sans UDS/LDS et pose VPA sur tout `$FFFC00-$FFFDFF` ([D]
  `gstmcu.v:175,181 @ 4a6db7cc`) ; le 68000 recopie l'octet écrit sur D8-D15 ([D]
  `fx68k.sv:1783-1786 @ 0602ee46`). Un octet impair est donc un cycle de la puce : l'écriture
  atteint le registre pair (`move.b #3,$FFFC01` = master reset), la lecture a son effet (RDR
  consommé) mais rend `$FF` (D0-D7 non pilotés), avec l'attente E-clock ; le vide
  `$FFFC08-$FFFDFF` paie aussi l'E-clock. Câblage des MC6850 sur D8-D15, RS = A1, confirmé par
  le schéma du Sparrow (feuille 2/5). ST, Mega ST et Mega STE inchangés (pas de netlist).
  Test : `tests/acia_odd_byte_logic.cpp`.
- **Récepteur tenu en master reset** · voulu [B] · basse · 2026-11-11 — tant que CR1-0 = 11,
  l'ACIA (clavier et MIDI) n'assemble aucun caractère ; l'octet arrivé entre-temps est perdu,
  sans RDRF ni overrun. [B] MC6850 § Counter Divide Select Bits p. 8 et § Interrupt Request
  p. 7 (`extern/datasheets/MC6850.pdf`). Hatari reçoit quand même (`acia.c:1038 @ f0736b24`).
  Auto-tests `ikbd-acia-init` et `midi-rx` mis à l'attendu de la fiche.
- **TDRE sous master reset ; reset à chaud du MIDI** · voulu [B] · basse · 2026-11-10 —
  MC6850 § Interrupt Request p. 6 (TDRE reflète le registre d'émission « except when inhibited
  by […] the ACIA being maintained in the Reset condition ») et § Counter Divide Select Bits
  p. 8 (le master reset « clears the Status Register ») : `Ikbd::read8` (`Ikbd.cpp:179`) et
  `MidiAcia::read8` (`MidiAcia.cpp:43`) lisent TDRE à 0 tant que CR1-0 = 11, à 1 dès la sortie ;
  Hatari `acia.c:681 @ f0736b24` le pose pendant le reset. Pin Assignment p. 1 : pas de broche
  de reset ; le reset à chaud de la machine ne touche plus l'ACIA MIDI
  (`MidiAcia::warmReset`), comme l'ACIA clavier (`Ikbd::resetHw`) ; un octet en émission part.
  Hatari `midi.c:110-122 @ f0736b24` efface tout. La mise sous tension (`Machine::hardReset`)
  garde l'état initial d'Hatari (la fiche ne le définit qu'après un master reset, que le TOS
  fait au démarrage).
- **Master reset : IRQ tenue haute** · voulu [B] · basse · 2026-11-04 — MC6850 § Master Reset
  p. 5, [D] `gen_uart.v:347-348 @ a00deb5b`. Hatari (`acia.c:727-730 @ f0736b24`) la lève sous
  `CR=%x01xxx11`.
- **IKBD calé sur la ROM 6301 désassemblée** · voulu [C] · 2026-10-22 — MiSTery
  `ikbd/rom/IKBD.ASM` (révision tardive du clavier, dont la logique des commandes ne dépend a
  priori pas). `Ikbd.cpp` :
  - **Octets de mode** : `$C9` = souris activée (bit 7, `mouseOn_`), port 0 au joystick
    (bit 1, `port0Joy_`, posé par `$14/$15/$17/$18/$19` via `$FA47`, effacé par toute commande
    souris `$FBAF`), mode souris (bits 6-5) ; `$CA` = mode joystick et activation (bit 5,
    `joysticksEnabled_`). `$92` rapporte le seul bit 7 ; `$1A` n'efface que le bit 5 (`$94`
    répond toujours `$14`) ; une commande souris arrête `$17/$18/$19`.
  - **Fenêtre de reset** (`$F010 sei … $F139 cli`) : un seul octet reçu pendant l'init attend
    dans le RDR et s'exécute après ; un second déborde et tout est perdu (`$FF40`). Remplace
    les quatre quirks d'Hatari (Barbarian `$08+$14`, Hammerfist `$12+$14`, `$12+$1A`, `$13`
    ignoré pour Just Buggin') : restent les défauts (souris relative + joystick 1 en
    événement ; jamais le joystick 0). Just Buggin' envoie `$80 $01 $11 $13` d'un trait, `$11`
    est retenu puis perdu avec `$13` ; Barbarian (Psygnosis) envoie
    `$80 $01 $08 $0B 03 03 $14`, tout est perdu, souris et joystick 1 répondent ensemble
    (`docs/CASE_STUDIES.md`).
  - **Tampon de sortie de 21 octets** (`$D9-$ED` ; Hatari 1 024) ; les réponses d'état sont
    admises appel par appel (`$FCD7`) et peuvent être tronquées.
  - **PAUSE `$13`** (`$F9AF`) : les octets déjà en file sortent encore ; seules les commandes
    qui sortent par `$F91B` lèvent la pause (pas les rapports d'état, ni `$0D` hors mode
    absolu, `$16` refusé, `$80 xx`) ; rapports d'état ignorés pendant la pause et pendant
    `$17/$18` ; décompte de `$17` gelé (`$FEAC`).
  - **Souris absolue** : `$09` ne remet PAS la position à zéro (`$FACB` — Hatari avait raison,
    la doc du protocole a tort) ; position mise à jour en mode absolu seulement ; `$0D`
    (`$FB39`) ne répond qu'en mode absolu, avec les événements boutons CUMULÉS (`$C2`) qu'il
    efface ; `$07` bits 0-1 envoient les événements du passage ; `$0E` (`$FB5F`) passe en mode
    absolu ; échelle par défaut 1, 0 fige l'axe ; bornes 0/0 après boot.
  - **`$07` bit 2** en relatif : un changement de bouton seul ne produit plus de `$F8`.
  - **`$16`** refusé si joysticks désactivés ou hors modes événement/interrogation ; joystick 0
    lu à 0 si le port 0 sert la souris.
  - **Émission « dès que la ligne est libre »** (`$F54A`) : paquets `$F8` envoyés quand la ligne
    se libère et que le cumul atteint le seuil, ou sur changement de bouton, ou au-delà de
    ±127 ; le cumul sous le seuil est gardé (Hatari scrute toutes les 150 000 cycles et abandonne
    le reliquat). Même règle pour les flèches `$0A` (une paire par axe, cumul effacé) et pour
    `$17` à taux 0.
  - **`$18`** (`$F8A2`) : un échantillon du feu du joystick 1 par tour de boucle, 177 µs
    enfoncé / 180 µs relâché, +15 µs au 8ᵉ (octet écrit dans le TDR), attente 57 µs ligne
    occupée et 34 µs sous pause — cycles HD6301 comptés sur la ROM, hors interruptions du
    timer ; clavier non scruté. **`$19`** (joystick 0 en flèches, RX/TX/VX en dixièmes de
    seconde, feu `$74/$F4`) scruté toutes les 1 ms ; `$94` rapporte `$19` et ses paramètres.
    Hatari reconnaît `$18`/`$19` sans les émuler.
  - **Valeurs extrêmes** : taux `$17` à 0, seuil `$0B` à 0 et pas `$0A` à 0 = flux continus au
    débit de la ligne ; mois 0 = horloge figée ; `$1B` recharge le compteur de ms.
  Conservés (la ROM les confirme) : `$88` complété à 8 octets avec le dernier mode choisi,
  `$9A` piloté par `$1A` seul.
- **Émission MIDI à la fin du caractère** · voulu · 2026-10-18 — Hatari `midi.c:277` écrit au
  backend dès l'accès CPU ; NeoST attend la fin du caractère (TDR/TSR, `MidiAcia::advanceTx`,
  `MidiAcia.cpp:175`, débit selon `midi.c`, transfert selon `acia.c::ACIA_Clock_TX`). TX suit
  les huit formats CR2..4 et /1, /16, /64 (caractère masqué à 7/8 bits, durée fixée au
  chargement du TSR) ; BREAK retient le TDR, laisse finir le TSR et reprend au front du
  diviseur. RX hôte à 31 250 bauds 8N1 ; aucune erreur physique synthétisée.
- **MIDI RX, registre unique** · voulu (référence `acia.c`) · 2026-10-18 — RDRF protège un seul
  caractère achevé ; les suivants sont perdus et posent RX_Overrun ; OVRN apparaît à la lecture
  de RDR puis s'acquitte par SR → RDR, comme le cœur ACIA d'Hatari. L'IRQ suit RDRF et
  l'overrun pendant, en OU câblé avec le clavier. Le module MIDI simplifié d'Hatari ne modélise
  pas ce statut (bits OVRN/DCD/CTS/FE/PE absents de `midi.c:39-41`).
- **Reprise BREAK et phase TX** · voulu · 2026-10-18/19 — MIDI : phase issue du dernier
  changement CR0..1, selon le timer TX générique d'Hatari ; prochain front conservé (v38) ; les
  départs TDR ordinaires suivent la même phase ; changer le diviseur recadence les fronts TSR
  restants. Clavier : même phase CR pour les départs /64 8N1, les reprises BREAK et les TDR
  suivants ; un changement de diviseur recale le TDR en attente, un TSR engagé garde son stop.
- **Monitoring `$17` « entier ou rien »** · voulu (NeoST plus robuste) · basse — Hatari est le
  seul émetteur sans `CheckFreeCount` et peut déchirer un paquet à 1 octet libre
  (`ikbd.c:1484`). Observable seulement tampon plein.
- **Horloge IKBD amorcée sur l'heure hôte** · assumé · basse — Hatari l'efface au démarrage à
  froid (`ikbd.c:539`) ; confort de bureau, `--keys`/`setClock` couvrent le déterminisme. Idem :
  état des programmes 6301 propres réinitialisé à chaque reconnaissance (les `static`
  d'Hatari casseraient un second lancement).

### Refermés

- **Empreinte des programmes 6301 propres** · haute · 2026-10-20 — NeoST calculait un vrai
  CRC32, alors que la fonction d'Hatari (`utils.c:crc32_add_byte`) teste
  `(c & 0x80) ^ (crc & 0x80000000)` ; les valeurs de la table venant d'Hatari, aucun programme
  réel n'était reconnu (Froggies, Transbeauce 2, Dragonnels, Chaos A.D., Audio Sculpture).
  Clé d'identification, pas du matériel : la fonction d'Hatari est reprise à l'identique,
  vérifiée contre son `utils.c` compilé.
- **Datation de l'IRQ clavier** · 2026-10-19 — front GPIP4 daté au transfert TDR → TSR ou au
  stop RX, au lieu du dispatch ; le rattrapage avant écriture CR/TDR publie le front au START ;
  propagation MFP de quatre cycles depuis ce front ; banc de 77 contrôles (dix échecs avant).
- **Date du front IRQ MIDI** · 2026-10-19 — RX hôte cadencé, transfert TDR → TSR et retour au
  stop de bouclage transmettent l'échéance série au détecteur GPIP4.
- **Phase MIDI RX** · 2026-10-18 — initialisée sur `liveNow`, puis échéance précédente + 2 560
  cycles ; resets et remplacement de source gardent une échéance active.
- **Délais des réponses IKBD et Froggies** · 2026-10-18 — attente firmware globale, comme
  `SCI_TX_Delay`, quantifiée à 1 024 cycles par bit ; valeur centrale déterministe des plages
  aléatoires d'Hatari ; une réponse en attente remplace le délai, l'octet en vol finit.
- **Transit CPU → IKBD** · 2026-10-18 — un TDR et un TSR, TDRE au chargement du TSR sur la
  grille de 1 024 cycles, commande reçue au stop ; une écriture CPU prématurée remplace le TDR ;
  TIE ne change que l'IRQ ; resets et phase sérialisés.
- **Autosend normal temporisé ; monitoring `$17`** · 2026-10-18 — timer libre de 150 000
  cycles ancré sur son échéance précédente ; `$17` : taux 0 ramené à 1, période en centièmes de
  seconde sur la base fixe 8 021 247 d'Hatari, phase sauvegardée ; MouseAction et mise à jour
  absolue précèdent le paquet (≙ `IKBD_SendAutoKeyboardCommands`, `ikbd.c:1688-1696`).
- **Break de l'émission clavier** · 2026-10-18 — CR bits 5-6 = 11 empêchent le départ d'un
  nouveau TSR sans tronquer celui engagé ; le TDR reste plein et repart au front suivant.
- **Capacité de sortie IKBD** · 2026-10-18 — les 1 024 octets ne comptent ni le SCI engagé ni
  le RDR ACIA (≙ `ikbd.h:44-45`, `IKBD_OutputBuffer_CheckFreeCount`, `IKBD_Check_New_TDR`) ;
  archives au-delà refusées. (Depuis le 2026-10-22, le tampon du firmware fait 21 octets,
  ci-dessus.)
- **PAUSE `$13`** · 2026-10-18 — seul le prochain départ SCI est bloqué ; l'octet engagé finit
  (≙ `IKBD_SCI_Set_Line_TX`, `ikbd.c:835-866` ; `IKBD_Check_New_TDR`, `:917-929`) ; v32. L'ancienne
  mention « deux octets » était fausse : Hatari ne charge TDR qu'en SCI IDLE.
- **Initialisation de l'ACIA clavier** · 2026-10-19 — sorties firmware rejetées avant
  l'initialisation du diviseur (≙ `IKBD_Send_Byte_Delay`) ; latch conservé aux resets, v46.
- **Registres ACIA et transit au reset matériel** · 2026-10-19 — CR/SR/RDR, erreurs, TDR/TSR et
  causes IRQ conservés (≙ `ACIA_Reset` sans broche) ; SCI sortant annulé par `IKBD_Reset`, puis
  boot ROM ; ligne IRQ republiée après le reset du MFP ; progression des bits ACIA conservée,
  SCI remis en attente de start.
- **[B9 (6ᵉ p.)] Reset de l'ACIA MIDI** · 2026-07-31 (`e499eba`) — `MidiAcia::reset` appelé
  par les resets chaud et froid, comme `reset.c:111` / `:124`. (Le reset à chaud ne touche plus
  l'ACIA MIDI depuis le 2026-11-10, ci-dessus.)
- **M-MIDI : RDRF distinct de la file** · 2ᵉ passe, 2026-06-15 — le master reset l'efface (SR
  → TDRE seul, ≙ `ACIA_MasterReset`) sans purger la file : l'octet reste relisible. Le câble
  OUT → IN est débranché par défaut depuis le 2026-08-21 (`--loopback` pour le diagnostic).
- **TDRE qui ne tombait jamais sans TIE (MIDI et clavier)** · 2026-10-18 — TDR et TSR
  distincts, TDRE tombe à l'écriture TDR et remonte au chargement du TSR, indépendamment de TIE.
- **Limites `$18`/`$19` du 2026-10-18** — périmées : émulées depuis le 2026-10-22 (ci-dessus).
- **`Blitter::reset` relâche GPU_DONE** · 2026-10-22 (même lot) — pas de blit fantôme après un
  reset à chaud en plein blit. Le bouchon RS-232 (`Mfp::loopback_`) n'est plus dans l'état
  depuis la v51 : réglage du frontal, comme les bouchons MIDI et SCC.

### Faux positifs

- **Bits SR du MIDI (OVRN/DCD/CTS/FE/PE)** (5ᵉ passe) — `midi.c:39-41` ne les modélise pas
  non plus.

---
## 7. Bus, mémoire, bus error — `Bus.cpp` ↔ `ioMem*.c`, `cpu/memory.c`, `stMemory.c`

**Concordant** : MMU et banques ST contre STE (`ConfToBank` : banque 1 calquée sur la
banque 0 au STE), repliement RAS/CAS, trou MMU du STF (banque 0 = 128 Ko et banque 1 = 2 Mo :
`$40000-$7FFFF` void, `Bus::mmuTranslate`, `Bus.cpp:310` ≙ `memory_map_Standard_RAM`, quirk
mesuré sur STF), liste blanche des bus errors par modèle (octet par octet, patches Mega ST et
Mega STE, octets impairs du SCU ; un accès mot/long ne faute que si tous ses octets fautent),
fenêtres ROM, accès DMA (lecture 0, écriture ignorée en zone fautive), protection superviseur
`< $800`, zone RAM vide qui rend la dernière valeur du bus (`cpuDb`, ≙ `VoidMem_*`), zone
cartouche vide lue `$FF` (≙ `memset 0xFF`, `cart.c:117`), `$FF8001` remis à 0 au reset à froid
(`Machine::hardReset` ≙ `stMemory.c:93 @ f0736b24`, `bCold`). Décodage MMIO du MFP : octets pairs fautifs en accès octet, remplis
`$FF` dans les accès mot/long partiels, impairs `$FFFA31-3F` void (lus `$FF`, écriture absorbée,
sans wait-state ; ≙ `ioMemTabST.c:143-150`, `Bus::rdMfp`, 2026-08-07, requalifié le
2026-10-19). Ombres impaires du PSG en octet et `movep`, impairs mot/long ignorés
(`Bus::wrPsg`, `Bus.cpp:1003`, requalifié le 2026-10-19). `$FC0000-$FEFFFF` faute avec un TOS
en `$E00000` (`memory.c:1784`, cas IK+ dans `docs/CASE_STUDIES.md`).

### Écarts ouverts

—

### Écarts voulus — ne pas réaligner

- **Décodage du STE d'après la netlist du GST MCU** · voulu [B] · 2026-10-22 — la netlist est
  le câblage du circuit. Sources : netlist texte Atari du 4081 (archive ASIC de Christian
  Zietz, chzsoft.de/asic-web, `ASIC/4081_COMBO/4081/PIN/4081.PIN` et `REGISTER.PIN`), sa
  transcription `gstmcu.v` (cœurs MiSTer/MiSTery). Le MCU ne donne DTACK, en lecture seulement,
  qu'à `$E00000-$EBFFFF` (/ROM2-/ROM0), `$D00000-$D7FFFF` (/ROM6, /ROM5), `$FE0000-$FE1FFF`
  (/ROMP) et au port cartouche (4081P2, porte AF8 `ROMXB`) ; le reste time-oute (`berr_cnt`,
  64 cycles à 8 MHz). NeoST, STE seulement (`Bus::romWindowSize`, `Bus.hpp:234` ;
  `Bus::steRomSelectVoid`, `Bus.hpp:247` ; `Bus::steCartWindow`, `Bus.hpp:259` ;
  `mmuBank0Size`, `Bus.cpp:260` ; `Bus::mmioRead8`, `Bus.cpp:1300`) :
  - `$EC0000-$EFFFFF` faute (Hatari : fenêtre ROM de 1 Mo, lit 0) ;
  - /ROM5, /ROM6 et /ROMP lisent le bus ouvert (Hatari : `BusErrMem_bank`) ; le
    « `irom6 | irom6` » de `gstmcu.v` (sans /ROM5) est une coquille de transcription,
    présente depuis son premier commit ;
  - registre « gamecart » `$FF9000` (4081P10 : bascule `GAMECART`, D = ID8, effacée par
    RESB/PORB) : bit 0 de l'octet pair relu (bit 1 à 0, bits 2-7 flottants rendus à 1) ; à 1,
    /ROM4 et /ROM3 passent en `$D80000-$DBFFFF` et `$DC0000-$DFFFFF` (fenêtres de 64 Ko
    répétées) et `$FA0000-$FBFFFF` faute. Les extensions NeoST du port cartouche (NE2000,
    ISP1160, clés) ne suivent pas le déménagement, et la cartouche système du HD GEMDOS
    (absente d'un vrai STE) ne déménage pas : son trap #1 reste en `$FA002A`
    (`steCartRelocated`) ;
  - `$FF8001` relu sur 4 bits (`REGISTER.PIN` : latches ID0-3, IDB4-7 à VSS) ; banque 0
    configurée « 11 » = 2 Mo (4081P18 : RS3 = 1 prend le chemin 2 Mo quel que soit RS2). Confirme
    le modèle Hatari « banque 1 calquée sur la banque 0 » : RS0/RS1 ne pilotent rien ;
  - pads `$FF9200-01`, `$FF9210-17`, `$FF9220-23` : acquittés en LECTURE seule
    (BUTR/PADR/PENR contiennent IRWZ) → toute écriture fait bus error (`Bus::wrStePads`,
    `Bus.cpp:1172`) ;
  - sélection des pads : bascule JOYWE (`gstmcu.v:352-360`) mise à 1 au reset et par toute
    LECTURE de l'octet bas `$FF9203`, remise à 0 par son ÉCRITURE ; inactive, aucune rangée
    n'est sélectionnée et tout se lit au repos (`StePads::sel`, `StePads.hpp:84`). Hatari
    garde la sélection écrite indéfiniment ;
  - chien de garde `/BERR` (4081P11, compteur 7 bits à 8 MHz remis à zéro hors /AS) : une bus
    error arrive ~64 cycles après /AS, soit 60 cycles d'attente en plus d'un accès normal
    (`kBerrTimeoutWait`, `Cpu68k.cpp:30`). Hatari n'en compte aucun. Le délai de 64 cycles est
    aussi appliqué au ST et au Mega ST d'après un document Atari (relevés de tiers du
    2026-11-11, `TODO.md` § 5) ;
  - son DMA : § 5, « Son DMA du STE d'après la netlist ».
  Mega STE inchangé (aucune netlist de son décodage). Garde : `--bus-selftest` sur STE
  (24 contrôles).
- **Vecteurs reset `$0-$7` servis par la ROM** · voulu [D-netlist] · basse · 2026-11-11 — toute
  lecture CPU de ces 8 octets rend la ROM, [D] `gstmcu.v:189-195 @ 4a6db7cc` (`rvec` → /ROM2,
  `iramc` exclut la RAM). Hatari (`cpu/memory.c @ f0736b24`, `SysMem_*get`) lit la copie RAM,
  qu'un alias MMU ou un DMA peut réécrire. Vidéo et son DMA (cycles RAM) lisent toujours cette
  copie (`Bus::ramCycleRead8`, `Bus.hpp:174` ; `Bus::ramCycleRead16`, `Bus.hpp:179`).
- **DMA disque et ACSI en cycle RAM** · voulu [D-netlist] · basse · 2026-11-11 — tout cycle DMA
  sélectionne la RAM, [D] `gstmcu.v:226 @ 4a6db7cc` (`iram = … | ~ixdmab`, `rom`/`rvec` exigent
  `ixdmab`). `Bus::dmaRead8` (`Bus.cpp:853`) et `Bus::dmaWrite8` (`Bus.cpp:865`) passent par
  les cycles RAM : jamais la ROM ni la cartouche, et `$0-$7` est la copie RAM, lue et écrite
  par le DMA. Hatari (`stMemory.c STMemory_DMA_ReadByte/WriteByte @ f0736b24`) lit le plan CPU
  (ROM lisible) et perd une écriture en `$0-$7`. Sans effet jusqu'à 4 Mo hors de `$0-$7` : le
  compteur DMA y tient sur 22 bits.
- **Blitter : accès mot vers le MFP** · voulu [B] · basse · 2026-11-11 — faute seulement si
  les deux octets fautent, comme pour le CPU ; [B] MC68901 § 2.1.3.2 et 2.1.3.4 (DS sur LDS,
  DTACK dès CS), [D] `gstmcu.v:176 @ 4a6db7cc`. Hatari juge l'octet pair seul
  (`stMemory.c:729 @ f0736b24`) : un blit vers `$FFFA00` y lit `$0000` et n'écrit rien.
- **`$FF8001` prérempli à la configuration** · assumé · basse — au constructeur et à
  `Machine::reconfigure`, NeoST pose `memConfigForBytes(ramBytes)` alors qu'Hatari laisse 0 et
  compte sur le test mémoire du TOS. Visible seulement pour du code exécuté avant le TOS (sonde
  MMIO : NeoST `$04`, Hatari `$00` sur 1 Mo) ; le TOS l'écrase au boot. Le reset à froid, lui,
  le remet à 0 comme Hatari.
- **RAM de 2 176 et 2 560 Ko (banques mixtes)** · conforme à Hatari, contre MAME · 2026-10-20 —
  `ramBanks` (`Bus.cpp:265`) découpe 2048+128 et 2048+512 Ko comme `STMemory_RAM_SetBankSize` ;
  `memConfigForBytes` (`MachineType.hpp:174`) pose `$FF8001` = $08/$09 ; tailles `2176k` et
  `2560k` (alias `2.5m`) acceptées par `--mem`, `neost.cfg`, la configuration, le shell web et
  le contrôle de save-state. phystop = Hatari : ST/Mega ST sous TOS 1.02/1.04/EmuTOS →
  $220000/$280000 ; STE/Mega STE sous TOS 2.06 → $400000 ; EmuTOS 256 → taille exacte. Modèle
  Hatari gardé sur STE car seul cohérent avec les témoignages : FAQ comp.sys.atari.st § 3.1.1
  (« STEs will not recognize a 2.5Mb configuration without silkboot2e or simmfix ») et
  l'existence de SIMMFIX2 ; MAME (`stmmu.cpp`, banques indépendantes) les contredit. [F] À
  confirmer sur vrai STE : lecture `$FF8001` bits 0-1, décodage d'une banque 1 de 128 Ko.

### Refermés

- **[BU1] Miroir du PSG `$FF8804-$FF88FF`** · moyenne · 2026-06-15 — routé vers le YM2149
  (plage `+0x100`, décodage `addr & 3`, wait-states inclus, plage close à `$FF88FF`), comme
  l'ombre d'Hatari (`ioMem.c:386-393`).
- **BUS-LEAK : largeur d'accès non restaurée après un blit** · basse-moyenne · 2026-06-15 —
  `Bus::write16`/`write32`, branche blitter, faisaient `return` sans restaurer
  `ioAccessWidth_` : après le premier blit mot, les bus errors d'accès octet (`$FF9200`, crayon
  optique, FDC) restaient désarmées.
- **Écriture en `$0-$7` par un maître non CPU** · basse · 2026-07-07 — `Blitter::writeWord`
  (`Blitter.cpp:142`) ignore `addr < 8` (≙ `SysMem_wput`, `memory.c:758`) ; un blit visant la
  table des vecteurs corrompait la RAM.
- **API Bus, miroir blitter atomique** · 2026-10-19 — adresse masquée sur 24 bits avant les
  raccourcis `write16`/`write32` ; écriture contrôle/skew atomique, largeur MMIO restaurée,
  rebouclage en RAM conservé. Défaut de l'API directe et des outils ; Moira normalise déjà ses
  cycles.
- **`$FF860E/0F`, densité** · 2026-10-18 — active sur Mega STE seulement ; ailleurs, les accès
  autorisés lisent `$FF` et ignorent l'écriture.
- **`$FF8A3E/3F`** · 2026-07-09 — void (lit `$FF`, écritures ignorées, `ioMemTabSTE.c:199`),
  au lieu de `$00` ; régression ajoutée le 2026-10-18.
- **`read32` en zone RAM vide** — assemblé par `cpuDb`, équivalent à `VoidMem_lget`.

### Faux positifs

- **Distinction `IoMem_VoidRead` (`$FF`) / `_00` (`$00`) « non générale »** — Hatari ne
  l'emploie que sur quelques registres vidéo du STE, que NeoST couvre exactement (§ 2).
- **Zone cartouche vide lue `$FF` au lieu du tampon ROM** — Hatari fait `memset 0xFF`
  (`cart.c:117`) : identique.
- **`$FF8264` du STE lu comme void** — relit la valeur écrite depuis `bc15a67` (§ 2).
- **Atomicité mot des écritures MMIO** (cartographie 2026-07-08) — les trois cas concrets
  (palette fusionnée, octet bas du Microwire, compteurs en `SIZE_BYTE` chez Hatari même) sont
  garantis.

---

## 8. Blitter — `Blitter.cpp` ↔ `blitter.c`

**Concordant** : logique de données (HOP/LOP 16 cas, tables `need_src`/`need_dst`,
FXSR/NFSR, smudge, halftone, masques de bord, défaut « 63 accès », IRQ GPIP3 de fin de blit,
arbitrage ST/STE/Mega STE) — **byte-identique à l'oracle** (mesuré le 2026-08-26). Base de temps
unifiée et facturation par accès (BL3, BL4), PRE_START commun HOG/partagé, recouvrement des
cycles CPU internes (BL5). Accès bus du blitter par le modèle DMA (`Blitter::readWord`,
`Blitter.cpp:128` ≙ `STMemory_DMA_ReadWord`, `stMemory.c:724` : zone fautive lue `$0000`
(`DMA_READ_WORD_BUS_ERR`), écriture absorbée ; corrigé le 2026-07-07, un blit sur zone
fautive lisait `$FF` par le dispatch CPU). Cycle RMW/TAS atomique dans le modèle courant (aucun dispatch Scheduler
entre lecture et écriture CPU ; banc du 2026-10-19, propriété à conserver si le dispatch
devient interne à l'instruction).

### Écarts ouverts

- **Blitter et IRQ du Mega STE à 16 MHz** · ouvert · moyenne · 2026-10-19 — la comparaison
  globale contre Hatari reste ouverte. Qualification CPU/cache du 2026-10-19 : alignement RAM
  au début du cycle, attente supplémentaire passée par `sync`, décision hit/miss avant les
  cycles, valeur du hit conservée pendant un DMA, cache invalidé avant les huit cycles
  d'arbitrage (≙ `blitter.c:395-421`). Onze sondes CPU/cache sans IRQ ni DMA : égalité exacte
  contre deux captures Hatari identiques (intervalles 16 MHz convertis en cycles bus 8 MHz) ;
  mutants rejetés. Le banc global (douze configurations) a révélé deux défauts de l'épingle
  Hatari (cycles DMA sans conversion à 16 MHz, IPL CPU périmé sans interrupt pendant) ; les
  deux corrections expérimentales rétablissent les bornes physiques, avec des écarts résiduels
  d'environ 12 à 24 cycles. Aucune référence issue des échecs. Le résidu attend la capture IACK
  (`docs/HATARI_MEGASTE_AUDIT.md`, `docs/MFP_IACK_MEASUREMENT.md`). Ne pas compenser par les
  délais du blitter.

### Écarts voulus — ne pas réaligner

- **Registres d'après la netlist 4051/4082** · voulu [B] · 2026-10-22 — registre mot chargé
  sur écriture mot seule (strobe WW = UDS & LDS), chaque mot indépendamment (adresses : deux
  mots) ; lecture octet du registre VIVANT (`Blitter::read8`, `Blitter.cpp:179` ;
  `Blitter::writeRegisters`, `Blitter.cpp:215`) — le tampon IoMem d'Hatari (octets rejetés
  relus ou recombinés) n'existe pas dans le circuit, et le miroir brut que NeoST lui avait
  emprunté le 2026-10-18 (BL-R) est retiré (format v53). `$FF8A3D` : bits 4-5 sans verrou,
  relus 0 (masque `0xCF`). Reset à chaud : seuls BUSY et HOG ont une remise à zéro
  (`Blitter::reset`, `Blitter.cpp:77`).
- **Ligne INT (GPIP3) = BUSY** · voulu [D-RTL], confirmé par la netlist · basse · 2026-10-22 —
  MiSTery `stBlitter.sv:185-190` : `INTn = rBusy & !ctrlWr`, câblée sans inversion sur GPIP3
  (`atarist_sdram.sv:623`). NeoST (`Blitter::controlWritten`, `Blitter.cpp:282` ;
  `Blitter::start`, `Blitter.cpp:293` ; `Blitter::pauseTransfer`, `Blitter.cpp:446`) : la ligne
  retombe à la pause (BUSY écrit à 0) et remonte à la reprise ; toute écriture CPU de l'octet
  `$FF8A3C` pendant un blit la fait passer 1 → 0 → 1 (relance `bset #7` du non-hog), ce qui
  peut lever le canal 3 s'il est armé. Hatari la laisse haute pendant la pause, sans impulsion.
  BUSY est effacé au même instant que GPU_DONE (lien décrit par le manuel Atari ; avant le
  2026-10-19, un événement pouvait voir BUSY = 0 pendant les quatre cycles de restitution alors
  que GPIP3 restait haut). Non mesuré sur matériel.
- **Mode partagé : fenêtre de comptage de 2 cycles** · voulu [B] · basse · 2026-11-04 — au lieu
  de 4, et l'accès CPU retenu compte parmi les 64 du CPU. [B] ST-4082 feuille 1 (compteur
  cadencé par AS) et [D] `stBlitter.sv:238-250 @ a00deb5b` avec fx68k @ `0602ee46` ; Hatari
  `blitter.c:69-79 @ f0736b24` annonce 4 cycles, « my guess ». Une boucle d'EXG donne 64 accès
  par tranche chez NeoST et le RTL, 63 chez Hatari. Écarts : `blitter_timer` à 122 px, tous dans
  la zone x 48-55, y 32-128 du buffer (`tolerated_zones`, 0 px hors d'elle), sondes
  `blitter_shared_large` et `blitter_shared_ram`. → `docs/STBLITTER_BENCH.md`.
- **SMUDGE en HOP 0** · voulu [B] · basse · 2026-11-04 — la source se lit ([B] ST-4082
  feuille 1, HOP<1> OU SMUDGE). Hatari ne la lit pas ; même résultat, deux accès de plus par mot.
- **Adresse source relue en plein mot** · voulu [D-RTL] · basse · 2026-11-04 — elle avance
  avec la lecture ([D] `stBlitter.sv` `bltCounters @ a00deb5b`). Hatari l'avance en fin de mot.
- **Accès mot vers le MFP** — § 7.

### Refermés

- **[BL1] Compteur X/Y écrit à 0 = 65 536** · haute · 2026-06-15, en deux passes — X par
  bouclage 16 bits, Y par `yLatch_` converti à l'écriture de `$FF8A38` (≙
  `Blitter_LinesPerBitblock_WriteWord`, `blitter.c:1356`). La première passe (0 → 65 536 relu
  dans `runSlice`) relançait un blit de 65 536 lignes au redémarrage du pilote TOS (`bset #7`
  sur un compte Y résiduel nul) : bureau TOS 1.06 STE brouillé. Modèle complet (`c96311c`) :
  démarrage avec résiduel 0 → BUSY et HOG effacés, aucun blit (`Blitter_Control_WriteByte`,
  `blitter.c:1433-1437`) ; écrire 0 puis démarrer → 65 536 lignes.
- **[BL2] Accès octet aux registres mot** · moyenne · 2026-06-15 — `Blitter::write8`
  (`Blitter.cpp:189`) ignore l'accès octet (`off < 0x3A`) ; seuls HOP, LOP, contrôle et skew
  restent accessibles en octet (≙ `Blitter_CheckAccess_Byte`, `blitter.c:972`). La lecture
  rend le registre vivant (ci-dessus).
- **[BL3] Cycles volés hors de l'horloge de l'ordonnanceur** · haute · 2026-08-25 —
  `Blitter::onSlice` (`Blitter.cpp:369`, callback de l'échéance `Scheduler::BLITTER`) facturait
  des cycles ni dans `ran` ni dans `sched.now()`, perdus puis résorbés par le `syncTo` de
  `Cpu68k::rebaseQuantumAndSync` : escalier 136 → 1 088 cycles bus sur Lethal Xcess, 2 tics de
  Timer A mangés, TADR `$3C`, garde `ILLEGAL` du jeu (cassait `megast`, `ste`, `megaste`).
  Porté par `Blitter::billCycles` (`Blitter.cpp:158`) → `Scheduler::addStolenCycles`
  (`Scheduler.hpp:306`) ≙ `Blitter_AddCycles` (`blitter.c:342-354`), discriminé par
  `Cpu68k::inRun` (en HOG, `ran` capte déjà le stall). Pas de crochet générique dans
  `Cpu68k::addBusWaitCycles` : une lecture d'observation hors machine (`bus.read8(0xFFFA01)`
  de `serialLoopbackSelfTest`) ne doit pas avancer l'horloge. Sonde : `NEOST_QDELTA_DIAG`
  (§ 12).
- **[BL4] Facturation par accès** · 2026-08-25 — `Blitter::billCycles` (≙ `Blitter_AddCycles`
  + `Blitter_FlushCycles`, `blitter.c:356-374`, appel de `CycInt_Process` en `:366`) est appelé
  par `readWord`/`writeWord` et à chaque arbitrage, à sa position réelle ;
  `Scheduler::addStolenCycles` dispatche (`syncTo`) au lieu d'avancer `now_` en silence.
  `Scheduler::runTo` rendu ré-entrant par un garde RAII (`FiringGuard`) sur `firingDue_` (seul
  état non local), comme `CycInt_Process` (`cycInt.h:85-88`, `while (ActiveInt <= now)
  CallActiveHandler()`, sans masque anti-relance, ré-entré depuis le handler
  `INTERRUPT_BLITTER`) ; le membre `inDispatch_` introduit par BL3 est retiré. Mesuré : retard max d'IRQ timer 265 → 132 (Lethal Xcess, `megast`),
  170 → 132 (`ste`/`megaste`), 265 → 161 au boot EmuTOS `megast` nu ; 26 000 trames et
  ~1,6 M préemptions sous assertions actives (`-UNDEBUG`) ;
  `machine=st` bit-identique.
- **[BL5] Datation des blits** · clos le 2026-10-19 — reprise PRE_START pendant
  l'instruction, callbacks de début/fin de cycle bus, exclusion de l'accès initiateur,
  recouvrement des cycles CPU internes jusqu'au prochain accès (hooks Moira par cycle bus, ≙
  `Blitter_Check_Simultaneous_CPU`, `blitter.c:1641` ; crédit CPU sérialisé en v49) ; GPU_DONE
  émis après restitution du bus ; alignement RAM avant `SYNC(2)` sur l'adresse réelle de chaque
  mot. Dix sondes blitter au pin (ROM/RAM, HOG/partagé, tailles, MULU, TAS), 64 copies après
  huit échauffements : fin +4 cycles, IRQ +2 en ROM / +4 en RAM, compte CPU identique ; dix-sept
  sondes sans exception KNOWN ni décalage incorporé aux références. L'écart synthétique partagé
  « +60 cycles / IRQ +42 / trois tours CPU » est corrigé (quatre échecs reproduits avant, 155
  contrôles CPU/blitter après sous ASan/UBSan) ; la garde ISR Mega STE v2 refuse une IRQ
  pendant BUSY ; le rebase préserve le dispatch DMA par accès sans double facturation ; le
  verrou TAS couvre le read-modify-write et se libère avant le préfetch ; les traces portent les
  instructions réelles, et les continuations d'IRQ/RTE ne comptent plus comme travail CPU ;
  crédit CPU sérialisé en v49, avec test de reprise TAS D0 → BRA dans les deux modes. Les
  68 derniers pixels venaient de l'alignement RAM calculé au callback médian, après une
  éventuelle tranche DMA ; le crochet Moira reçoit l'adresse réelle de chaque mot et aligne le
  début de l'accès avant `SYNC(2)`, sans nouvelle constante. 100 relevés TADR et données
  copiées identiques ; `blitter_timer` : 68 → 0 px (0/114 816), deux oracles identiques,
  `blitter_timer.png` produit par `--oracle`, promu en oracle Hatari reproductible. HOG passe
  par le PRE_START et le recouvrement communs au lieu de tourner dans l'écriture du registre ;
  les anciens essais qui ajoutaient un simple délai au chemin synchrone ne réfutent pas ce port. Les estimations et réfutations du 2026-08-26 sont historiques (`CHANGELOG.md`).
- **BL-GPIP3 : GPU_DONE jamais réarmée haute** · moyenne · 2026-06-15 — `Blitter::start`
  dé-asserte la ligne à chaque (re)démarrage (≙ `Blitter_Start`, `blitter.c:895`),
  `Blitter::finishTransfer` (`Blitter.cpp:461`) la rabaisse.
- **Masquage des incréments et adresses à l'écriture** · basse · 2026-07-07 — `&0xFFFE`
  (`blitter.c:1229`) et `&0x00FFFFFE` (`Blitter_SourceAddr_WriteLong`, `blitter.c:1254`) :
  `regWriteMask` masque les octets faibles `$FF8A21/23/2F/31/27/35` et les octets forts
  `$FF8A24/32`. Un incrément impair dérivait.
- **Défaut « 63 accès » remis à zéro à chaque PRE_START** · basse · 2026-07-07 —
  `busCountError_` (≙ `Blitter_HOG_CPU_BusCountError = 0`, `blitter.c:1457`).
- **Suspension au milieu d'un mot** · basse · 2026-07-07 — la tranche non-hog rend le bus au
  64ᵉ accès même entre deux accès d'un mot (≙ `BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED`),
  état du mot (`haveSrc_`/`haveDst_`/`fetchSrc_`/`dstWord_` ≙ `BlitterState`) persistant entre
  tranches. Sans sauver les drapeaux, la reprise refaisait la
  lecture source (6 px verts au bord des icônes GEM, `--walk-mouse` tos106fr STE).
- **Part CPU non-hog = 64 accès bus réels** · basse · 2026-07-07 — comptés par les callbacks
  mémoire de Moira (`bus_.blitterCountCpu` → `Blitter::noteCpuBusAccess`, `Blitter.cpp:415`),
  le 64ᵉ arme PRE_START et date la tranche à +4 cycles (≙ `BLITTER_PHASE_COUNT_CPU_BUS` /
  `Blitter_HOG_CPU_mem_access_after`, modèle CE de l'oracle) ; l'ancien forfait de 256 cycles
  facturait le CPU même sans accès.
- **Reset à chaud** · 2026-10-19 — conserve la RAM halftone et le pipeline source (≙
  `Blitter_Reset`), annule le transfert, remet les commandes à zéro ; initialisation froide
  déterministe NeoST sans mesure de la valeur matérielle à la mise sous tension.
- **Attente du CPU facturée aux accès du blitter** · 2026-11-03 — lot DHS (§ 2).

### Faux positifs

- **BL-MST** (5ᵉ passe) — `IoMem_FixVoidAccessForMegaST` (`ioMem.c:172-196`) dé-faute
  `$FF8A3E/3F` sur Mega ST aussi : carte de fautes identique.
- **Hits du cache Mega STE comptés comme accès bus** — Hatari les compte aussi
  (`m68000.c:1444-1676`).
- **PRE_START « 4 + 4 contre 4 »** (cartographie 2026-07-08) — artefact de point de référence :
  les deux chemins d'Hatari convergent vers fin d'accès + 4 ; NeoST rend fin d'accès + 2 sur
  les deux, sans asymétrie.
- **HOP/LOP et bit 4 du contrôle masqués à l'exécution seulement** — masqués à l'écriture.

---
## 9. SCC Z85C30 et SCU (Mega STE) — `Scc.cpp`, `Scu.hpp` ↔ `scc.c`, `scu_vme.c`

**Concordant** : cœur registre et IRQ (RR/WR, `updateRR0/RR2/RR3`, priorité et IUS, IACK
VIS/NV, resets matériel et canal, IRQ niveau 5 gatée par le SCU). SCU : gating des IRQ
(masques Sys/Vme), MFP6/SCC5 sur VmeIntMask, niveaux 4/2/1, IRQ niveau 5 vectorisée, décodage
des adresses impaires. Référence matérielle : Zilog UM0109 (UM010902-0609) ; MAME
`z80scc.cpp` en appui.

### Écarts ouverts

- **[SC4] Accès aux masques SCU (`$FF8E01`/`$FF8E0D`)** · assumé · basse — chez Hatari, lire
  ou écrire un masque purge toutes les IRQ SCU pendantes (`scu_vme.c:202-221,351-369`) ; dans
  NeoST le modèle « sources vivantes » (`Scu::syncState` repeuple les pendings à chaque
  `updateIpl`, `Cpu68k.cpp:937`) les fait survivre. Observable seulement si un programme
  scrute `$FF8E01` avec une VBL/HBL en attente ; à retrancher si un étalon Mega STE l'expose.
- **Routage LAN / Serial 2 par le bit 7 du port A du PSG** · ouvert · basse — non modélisé
  (`psg.c:423-426` → `scc.c:402-416`, cartographie du 2026-07-08) ; le port A ne pilote dans
  NeoST que la sélection FDC et les lignes RS-232 du MFP.
- **DPLL, FIFO SDLC, série hôte réelle** · ouvert · basse — non portés ; TRxC garde la
  fréquence indicative d'Hatari. Hunt, synchronisation, détection d'abort physique, auto-clear
  X21 et autres fonctions de WR7' hors périmètre.

### Écarts voulus — ne pas réaligner

- **Caractère nul de fin de BREAK** · voulu [B] · basse · 2026-11-12 — à la fin d'un BREAK bouclé, un
  caractère nul entre dans la FIFO ([B] Zilog UM0109 p. 58 et 98) ; absent chez Hatari.

- **All Sent toujours à 1 en synchrone et SDLC** · voulu [B] · basse · 2026-11-10 — UM010902-0609
  RR1 D0 p. 180 (« This bit is always set in synchronous and SDLC modes ») : `Scc::read8`
  (`Scc.cpp:1060`) force ce bit quand WR4 D3-D2 = 00 ; en asynchrone il suit l'émetteur. Hatari
  `scc.c:1969,2003 @ f0736b24` le fait retomber dans tous les modes. Test :
  `tests/scc_async_logic.cpp`. WR4 D7-D6 en External Sync choisissent une variante de
  synchronisation, pas un multiplicateur (p. 150-151) : non modélisé, [F].
- **Pointeur de registre** · voulu [B] · basse · 2026-11-04 — seule une lecture du port de
  contrôle le remet à zéro (UM010902-0609 p. 28 et 30). Hatari (`scc.c:1644 @ f0736b24`) le
  remet à toute lecture.
- **[SC2] RR9 = image de RR13** · voulu [B] · basse · 2026-07-12 — NeoST rend `WR[13]`
  (`Scc.cpp:832`) ; Hatari rend `WR[9]` (`scc.c:1606-1610`, dont le commentaire « also returns
  RR13 » contredit le code). Un diff SCC contre l'oracle diffère à l'octet si un programme lit
  RR9.
- **[SC5] Miroirs RR6/RR7** · voulu [B] · basse · 2026-10-18 — choix FIFO par WR15 du canal
  adressé ; hors FIFO, RR6B lit RR2B et RR7B rend RR3B (= 0), sans dépendre du canal A ; vecteur
  actualisé, INTACK logiciel par le chemin RR2. UM0109 table Z85X30 p. 31-32, MAME
  `scc_register_read` (remappe RR6 avant `do_sccreg_rr2` FIFO désactivée). Hatari partageait le
  défaut. La FIFO SDLC reste non implémentée.
- **Priorité INTACK** · voulu [B] · 2026-10-19 — l'acquittement respecte la chaîne des IUS, y
  compris par lecture de RR2 (INTACK logiciel) : une source moins prioritaire déjà bloquée ne
  reçoit plus d'IUS fantôme, une source plus prioritaire reste acquittable (UM0109 p. 41-42) ;
  lecture vectorielle de polling inchangée. MAME `softRR2` partage le raccourci corrigé.
- **MIE et IUS** · voulu [B] · 2026-10-19 — désarmer WR9 D3 masque l'IRQ sans effacer les
  interruptions en service ; un acquittement pendant MIE = 0 ne crée aucun IUS ; Reset Highest
  IUS garde son effet (UM0109 p. 161, MAME). Corrige un raccourci encore présent chez Hatari.
- **Reset de canal : IP/IUS du seul canal visé** · voulu [B] · 2026-10-19 — RR3A conserve les
  IP de B lors d'un reset A ; les IUS du canal réinitialisé cessent de bloquer les autres ;
  FIFO, vecteur partagé et IUS de l'autre canal conservés (UM0109 p. 160, MAME
  `reset_interrupts`). Hatari a l'ancien raccourci RR3/IUS.
- **TX de 1 à 8 bits** · voulu [B] · 2026-10-18 — WR5 « cinq ou moins » décode la longueur
  depuis les bits hauts du TDR ; seuls les bits de données bas sont émis ; 6/7 bits masqués
  (UM0109 p. 153, tableau WR5 ; chargement TSR p. 114 ; v31). Hatari garde cinq bits fixes.
- **Sync Character Load Inhibit (WR3)** · voulu [B] · 2026-10-18 — l'octet égal à WR6 est
  retiré avant FIFO, overrun et IRQ, y compris en asynchrone ; SDLC exclu, ignoré en Bisync
  12 bits (WR10 D0) ; comparaison sur les huit bits du caractère (UM0109 p. 149). Hatari ne
  porte pas ce filtre ; MAME journalise seulement le bit.
- **[SC1] Caractères TX/RX cadencés** · voulu [B] · 2026-10-18 — WR4/WR3/WR5 font format et
  longueur, WR11 les horloges, le BRG le débit ; TDR et TSR distincts (IRQ TX au transfert,
  RR1 All Sent après vidage) ; réception hôte en file (256 octets, dernier rejeté si pleine),
  un caractère par échéance ; bouclages livrés à la fin TX ; FIFO RX matérielle de trois octets
  (UM0109 et MAME : erreurs par emplacement, débordement remplaçant le plus récent, modes
  premier/tous/spécial, verrouillage sur erreur, Error Reset) — au-delà du modèle mono-octet
  d'Hatari. Fractions directes (RTxC/TRxC, rapport CPU/horloge, v34) et BRG rationnelles
  (rapport exact PCLK/RTxC, WR12/13, WR4, sans arrondi du baud ; UM0109 p. 167-168, 172) ;
  source changée effective au caractère suivant, fraction réinitialisée, nouveau flux après
  silence ancré sur l'horloge vive ; gardes v35 élargies pour PCLK et les longues suspensions
  BREAK. NeoST : `Scc::serialPeriod` (`Scc.cpp:583`), `Scc::serialWriteByte` (`Scc.cpp:469`) ;
  Hatari `SCC_WriteDataReg`/`SCC_Process_TX`, qui tronque `CPUFreq/baud` avant le caractère.
  Horloge du bus indépendante du 8/16 MHz CPU. Phase sous-bit non modélisée, précision sous-bit
  de BREAK absente (reste en cycles).
- **[SC1b] Local Loopback et multiplexage** · voulu [B] — `WR14 = $30` au reset sélectionne le
  bouclage interne (Zilog) ; TX livré à RX interne et au puits représentant TxD, RxD externe
  ignoré (UM0109 p. 69, MAME `write_rx`) ; bascule en cours de réception : caractère hôte
  interrompu invalidé, FIFO déjà complétée conservée ; la prise externe occupe le puits sans
  doubler la réception ; priorité Auto Echo préservée.
- **BRG et Zero Count** · voulu [B] · 2026-10-18 — `Scc::updateBrg` (`Scc.cpp:149`) et
  `Scc::onBrgEvent` (`Scc.cpp:175`) : sources PCLK/RTxC du Mega STE, TC WR12/13 et WR14 ; Zero
  Count précède le /2 et WR4 : (TC + 2)/source, retenue rationnelle, phase propre ; deux
  événements Scheduler ancrés ; RR0 ZC = pulse d'un cycle source, non latchée, forcée à zéro si
  WR15 ZC IE est coupé ; TC effectif au rechargement suivant (UM0109 p. 57/71/177, MAME
  `update_baudrate_timer`). Choix explicite : WR9 reset matériel et reset machine annulent les
  timers quand WR14 désactive le BRG (l'amont laisse un événement fantôme). **Horloge
  indépendante du CPU** : période sur la base bus 8 MHz ; Hatari utilise `CPU_Freq` (16 MHz fixe
  sur Mega STE) même quand `CPU_Freq_Emul` vaut 8 MHz, ce qui ralentit le BRG d'un facteur deux.
- **Réglages en vol sans casser l'écho RX** · voulu [B] · 2026-10-19 — WR14 D1..0 (source et
  enable du BRG), WR12/13 et la seule source TX (WR11 D4..3) ne corrompent plus l'écho RX qui
  utilise RTxC/TRxC directs ; les autres changements de WR11/WR14 et RX BRG/DPLL invalident
  (UM0109 p. 166-168, 172 ; multiplexeurs TX/RX séparés, DPLL sélectionnable côté TX sans moteur
  ajouté). Après arrêt et reprise du BRG, une trame déjà invalidée le reste jusqu'à son retrait ;
  le caractère suivant repart neuf. Durée abstraite de reprise, sans reconstruction bit à bit ;
  cadence BRG et Zero Count inchangées.
- **Stop RX indépendant de TX** · voulu [B] · 2026-10-19 — WR4 D3..2 ne choisit que les stops
  émis ; RX en vérifie toujours un (UM0109 p. 93, 151). Auto Echo huit bits sans parité accepte
  les trois choix de stops TX.
- **Priorité Auto Echo** · voulu [B] · 2026-10-19 — WR14 D3 déconnecte la sortie TSR avant les
  bouclages et le puits, le pipeline et l'IRQ fonctionnant (UM0109 p. 69-70, 172) ; D3 + D4
  garde l'exception DCD de p. 70 ; écho hôte huit bits sans parité livré après un stop RX
  abstrait ; trois états de trame sauvegardés en v40.
- **Auto Enables (WR3)** · voulu [B] · 2026-10-18 — CTS/DCD qualifient TX/RX, sauf en
  bouclage local et Auto Echo ; le TSR courant finit avant l'attente CTS ; DCD perd le seul
  caractère en cours ; RTS asynchrone actif jusqu'au dernier stop du TSR et du TDR (UM0109
  p. 95, 106, 149, 154 ; MAME `cts_w/tra_complete`) ; sortie physique sauvegardée en v29.
- **First / Error Reset, WR1 et verrou d'erreur, FIFO vide** · voulu [B] · 2026-10-19 —
  retirer le premier caractère en erreur efface son IP ; changer entre First/Special et
  Special Only conserve un verrou acquis ; IP/IRQ effacées au retrait du dernier caractère même
  si RR1 garde une erreur collante (UM0109 p. 47-48, 142, 146, 179 ; MAME).
- **Lectures étendues Z85C30** · voulu [B] · 2026-10-19 — WR7' D6 expose WR3/4/5/7'/10 via
  RR9/4/5/14/11 sans consommer la FIFO RX ni l'IRQ (UM0109 p. 157-158 ; l'en-tête de la table
  générale Z85X30 est ambigu sur WR15 D2, sans mesure ici) ; WR15 D0 ne sélectionne que les
  écritures WR7'.
- **RR8 et parité des formats courts** · voulu [B] · 2026-10-18 — bits inutilisés forcés à 1
  selon WR3 en asynchrone, à l'entrée de la FIFO (UM010904 p. 148) ; la parité reçue occupe le
  bit qui suit les données pour RX 5/6/7 bits, mémorisée au chargement du TSR et transmise par
  les boucles (WR4 p. 152). Aucune parité ni erreur inventée depuis un octet hôte.
- **External/Status, statuts selon le mode, fronts BREAK** · voulu [B] · 2026-10-18/19 —
  verrous WR15 indépendants du masque maître WR1 D0 (p. 56-57) ; Sync/Hunt forcé à zéro quand
  /SYNC sert à l'oscillateur ou en External Sync, BREAK nul en synchrone hors SDLC
  (p. 58-59) ; assertion et fin de BREAK conservées sous une autre cause External/Status
  (p. 58, v39 ; WR0 sert les polarités mémorisées, cycles répétés coalescés, sans compteur
  d'événements) ; Reset Tx Underrun/EOM (WR0 D7-D6), indépendant des commandes et pointeurs,
  efface aussi le statut brut en asynchrone (p. 176), image externe verrouillée conservée, sans
  front IRQ descendant ; pas de moteur CRC physique. Désactiver WR1 efface l'IP sans ouvrir les
  verrous ; le réactiver retrouve une condition mémorisée, y compris un Zero Count expiré.
- **SEND BREAK et TX Enable (WR5)** · voulu [B] · 2026-10-18 — suspension du TSR, TDR retenu,
  reprise du temps restant, aucun octet intact émis pour une trame interrompue (UM0109 p. 153,
  MAME `tra_callback/tra_complete`, v33) ; les caractères déjà finis avant une écriture BREAK
  tardive sont rattrapés ; indication RR0 BREAK bouclée immédiate ; WR5 bit 3 à 0 laisse finir
  la trame engagée, un TDR écrit pendant l'arrêt ne remplace pas le TSR actif et attend TX
  Enable et CTS (v32). Résolution au caractère, sans phase exacte du front de bit, ni erreur RX
  ni symbole corrompu inventés.
- **[SC3] `$FF8E07` (VME Interrupter)** · voulu (défaut d'Hatari) · basse — NeoST rend
  `vmeInterrupter` (`Scu.hpp:34`) ; Hatari rend `SCU.SysInterrupter` (copier-coller,
  `scu_vme.c:282-286`).
- **SCU : IRQ1 logicielle sur le bit 1** · voulu · basse — Hatari utilise le bit 0.

### Refermés

- **No-Vector à l'IACK** · moyenne · 2026-06-15 — sur IACK niveau 5 avec WR9 NV armé
  (`Scc::processIack`, `Scc.cpp:422`, rend −1), NeoST rendait l'auto-vecteur 29 au lieu du
  vecteur parasite 24 (`$60`) d'Hatari (`iack_cycle` : `vector < 0 → 24`) ; `Cpu68k.cpp` rend
  24. La branche MFP était déjà conforme.
- **SCU jamais réinitialisé** · haute · 2026-06-15 — `Scu::reset` (`Scu.hpp:87` : masques et
  états à 0, GPR1 = $01, GPR2 à froid seulement) appelé par `Machine::reset` et
  `Machine::hardReset` (≙ `SCU_Reset`).

### Faux positifs

—

---

## 10. RTC RP5C15 (Mega ST, Mega STE) — `Rtc.cpp` ↔ `rtc.c`

**Concordant** : 16 registres, banque AM/PM, mode, adresses impaires, sonde du TOS aux
adresses 2/3. Référence : Ricoh EK-086-9908.

### Écarts ouverts

—

### Écarts voulus — ne pas réaligner

- **RP5C15 sur Mega seulement** · voulu · basse — la puce n'existe que sur Mega ; Hatari
  l'expose sur toute ST/STE.
- **TEST/RESET en écriture seule** · voulu [B] · basse — Ricoh EK-086-9908 p. 7 réserve Eh/Fh à
  l'écriture : les commandes ne sont plus relues (zéro selon MAME, sans affirmation sur les
  bits flottants du bus réel) ; actions RESET conservées ; anciens octets d'archive gardés
  (layout v44).
- **Banques et contrôles** · voulu [B] — deux banques, alarmes indépendantes, RESET, ADJ,
  12/24 h et compteur bissextile (p. 6-7) ; chiffres conservés sans conversion implicite à
  l'écriture du sélecteur ; API DateTime en 24 h. TIMER EN bas garde une retenue du diviseur,
  délivrée à la reprise sans rattrapage multiple (phase sous-seconde continue, p. 7). Annulation
  du diviseur/ADJ par RESET déduite du reset des étages, non mesurée [F]. CLKOUT/ALARM et modes
  TEST non émulés.
- **Temps émulé (cycles) plutôt que `localtime()`** · assumé — déterminisme headless ; le
  rattrapage conserve les durées au-delà de 90 000 s, sans boucle seconde par seconde.

### Refermés

—

### Faux positifs

—

---

## 11. FPU MC68881 (Mega STE) — `Fpu.cpp`, `SoftFloatX80.hpp`, `SoftFloatFpsp.hpp`, `SoftFloatDecimal.hpp` ↔ `fpp.c`, `softfloat/`

**Concordant** : cœur softfloat add/sub/mul/div/sqrt/rem/mod/roundToInt, vecteurs
d'exception et priorités, prédicats FBcc/FScc, valeurs des constantes FMOVECR, FCMP/FTST,
octet quotient de FMOD/FREM, FSAVE/FRESTORE (idle `$1F18`/null), FSGLMUL/FSGLDIV, FSCALE,
FMOD, octet AEXC. Transcendantes calculées par le port de `softfloat_fpsp.c`
(`src/io/SoftFloatFpsp.hpp`, 2026-10-20) bit à bit comme Hatari (1 050 564 comparaisons, 0
écart). Bancs : `tools/test_fpu_unary_oracle.py` (1 443 cas), `tools/check_fmovecr_oracle.py`
(2 048), `tools/test_fpu_decimal_oracle.py --sanitize` (161 348 contrôles),
`tools/test_fpu_next_oracle.py --check-mutation` (1 156 cas FINT/FINTRZ), contre les vraies
fonctions d'Hatari compilées. Socket vide → « not found » : hors périmètre.

### Écarts ouverts

—

### Écarts voulus — ne pas réaligner

- **[B11 (6ᵉ p.)] SNaN double** · voulu · basse — NeoST livre SNAN (plus proche du 68881) ;
  Hatari lève OPERR à la conversion.
- **Sortie P d'un étendu non canonique** · assumé · basse — OPERR + NaN, pour éviter les
  décalages indéfinis du source décimal amont. Le k dynamique reste fixé à 17 (Dn inaccessible
  par le dialogue périphérique actuel). Les offsets $40-$7F du banc décimal exercent la
  fonction ROM par CIR ; le décodeur CPU d'Hatari les refuse en F-line, non simulé.
- **Limites communes aux deux** · [F] — algorithmes FPSP du 68040, pas le microcode CORDIC du
  68881 (dernier bit non garanti sur silicium) ; « zéro » non normalisé après underflow en
  simple ou double (`3F81:0…0`, traité Z), probablement un vrai ±0 sur 68881.

### Refermés

- **Propagation des NaN** · élevée · 2026-06-15 — `propagateNaN`/`propagateNaN1`
  (`SoftFloatX80.hpp:82-94`) rendent l'opérande NaN réel quiété (≙ `SOFTFLOAT_68K`,
  `softfloat-specialize.h:321-365`), au lieu d'un NaN par défaut.
- **SNaN levait OPERR au lieu de SNAN** · élevée · 2026-06-15 — drapeau `flag_signaling`
  distinct replié par `Fpu::sfFold` (`Fpu.cpp:131`) en `EXC_SNAN`/vecteur 54 ; `flag_invalid`
  reste OPERR/vecteur 52 (≙ `fpp.c:88-89`).
- **Masques FPCR/FPSR** · basse · 2026-06-15 — bits réservés forcés à 0 au chargement
  (`fpcr_ & 0xFFF0`, `fpsr_ & 0x0FFFFFF8`, `Fpu.cpp:594`).
- **FSGLMUL, FSCALE ∞/NaN, octet AEXC, FMOVECR INEX2 et arrondi** · 2026-06-15 — entrées
  tronquées à 24 bits ; FSCALE : NaN propagé, ∞ → OPERR + NaN ; UNFL accumulé seulement si
  INEXACT, INEX sur INEX2 ou OVFL ; table `fpp_cr` portée (`inex`, `rnd[4]` par mode).
- **[M3 (6ᵉ p.)] `normalizeSubnormal`** · 2026-07-31 (`7a9e03b`) — convention 68881 (`−sc`,
  bit entier explicite, branche `SOFTFLOAT_68K` de `softfloat.c:1061-1065`) au lieu du x87 :
  supprime le ×2 sur FADD/FSUB/FMUL/FDIV/FSQRT/FREM/FMOD/FGETEXP/FGETMAN/FSCALE ; fuzz
  différentiel : 0 écart.
- **FSCALE, extraction de n** · 2026-07-12 — exact jusqu'à ±131 071 puis saturation
  −$6001/$E000 (≙ `floatx80_scale`).
- **±∞ décodé en S/D** · 2026-07-12 — mantisse 0 (`floatx80_default_infinity_low`).
- **Précision des unaires et décimal empaqueté** · 2026-10-18 — FMOVE/FABS/FNEG appliquent la
  précision du FPCR avec sa plage d'exposant ; FMOVECR garde l'exposant étendu et porte
  `fpp_cr_undef`, ajustements et CC ; décimal P par `SoftFloatDecimal.hpp` sans libc flottante,
  dans les deux sens, k ≤ 0, INEX1 entrant et INEX2 sortant ; `completeInput` conserve les
  exceptions de conversion. Arrondis de conversion sortante (L/W/B/S/D) le 2026-09-02.
- **FINTRZ, exceptions d'arrondi** · 2026-10-18 — le statut local qui force l'arrondi vers zéro
  est fusionné dans le statut principal avant FPSR et CIR ; INEX2 et SNAN ne se perdent plus
  (354 échecs avec le défaut rétabli ; mini-ROM : groupes 53 et 54).

### Faux positifs

—

---

## 12. CPU 68000 et intégration — `Cpu68k.cpp`, `extern/moira` ↔ `cpu/newcpu.c`, `gencpu.c`, `hatari-glue.c`

**Concordant** : trame de bus error et d'address error (groupe 0), double faute → HALT
(seconde faute, SSP impair), vectorisation de l'IACK MFP niveau 6, auto-vecteurs VBL/HBL et
effacement du pending, reset SSP/PC par la ROM, gating IPL (priorité MFP > VBL > HBL ; SCU sur
Mega STE), STOP, wait-states PSG/MFP/ACIA/bus et E-clock, Mega STE 16 MHz (créneau bus et
cache), instruction RESET (≙ `customreset`, `hatari-glue.c:49-77`, par le crochet Moira ;
`Bus::peripheralReset`, `Bus.cpp:116`). Patches vendorisés audités le 2026-08-13 contre l'amont
(`extern/moira/NEOST_VENDOR.md` : diff intégral, 5 fichiers modifiés, tous les hunks
documentés, aucun écart accidentel).

### Écarts ouverts

- **[CPU1] STOP en avance rapide par-dessus une exception trace ou privilège** · ouvert ·
  basse — la garde `isStopped() && !irqDeliverable()` saute une exception en attente (`stop`
  sous bit T, `stop #imm` qui efface S) : elle part avec jusqu'à un écart d'événement de retard.
  Motifs exotiques (moniteurs pas à pas). Ne pas toucher sans revalider Closure (chemin
  critique). Attend l'échange du cœur.

### Écarts voulus — ne pas réaligner

- **STOP : IRQ pendante prise à STOP+8, STOP en 4 cycles** · voulu [D-RTL] · 2026-10-22,
  2026-11-03 — le 68000 teste l'interruption AVANT d'écrire le masque et ré-itère STOP 4 cycles
  plus tard (fx68k `rInterrupt`/`pswI` au même T3, nanomots 0x3A2/0x327 ; Hatari gencpu
  `checkint()` avant `regs.sr = sr`) ; patch vendor `Moira::stopGraceUntil`. Plus de cycle de
  préfetch pendant STOP (4(0/0)), prise quantifiée sur l'itération de 4 cycles. Les 4 cycles
  perdus au retrait du préfetch (2026-10-22) sont rétablis le 2026-11-03 : IRQ pendante au même
  cycle qu'Hatari et fx68k. Sonde cartouche sur la chaîne Timer B → handler → stop → HBL de
  Super Hang-On (3 997 chaînes) : NeoST = Hatari segment par segment.
- **Durée de l'IACK MFP : 12 cycles** · historique — montée de 12 à 16 le 2026-08-06 pour
  égaler Hatari sur Super Hang-On (chaîne fixe Hatari 144 cycles, NeoST 140), elle compensait
  en fait le défaut STOP+4 de Moira ; revenue à 12 le 2026-10-22 (`g_iackMfp`,
  `Cpu68k.cpp:170`). Les étalons `blankline_blank_st`, `blankline_node_late_st`, `nocooper` et
  les sondes `fdc_read/write`, verts avec 16, ne mesuraient pas l'IACK : ils masquaient la
  fenêtre de blank GLUE (§ 2) et une fin de commande WD1772 à −6 cycles (deux artefacts
  d'Hatari, tolérance 8).
- **Dispatch des événements échus au point d'IACK** · conforme (`NEOST_IACK_SYNC`, défaut ON)
  · 2026-08-07 — port du `CycInt_Process()` + `MFP_UpdateIRQ_All` d'Hatari juste avant la
  séquence d'IACK (`newcpu.c:2938-2946` branche MFP, `:2998-3003` branche vidéo) ; un événement
  est échu à 5,7-7 % des IACK. Le `syncTo` est précédé du rebase du quantum
  (`Cpu68k::rebaseQuantumAndSync`, `Cpu68k.cpp:1008`) : sans lui, le temps du quantum est
  facturé deux fois (raster décalé de ~16 cycles). Règle : tout dispatch déclenché depuis un
  crochet Moira rebase.

### Refermés

- **[CPU2] Phase mi-accès à 16 MHz** · requalifié le 2026-11-12 — `chipWait16` n'avait pas reçu
  la correction de phase mi-accès `(c − 2)` appliquée à `chipWait8` le 2026-07-02 ; il n'existe plus :
  l'accès Mega STE 16 MHz passe par `readMste16Mhz`/`writeMste16Mhz`, dont la qualification du
  2026-10-19 (alignement RAM au début du cycle, onze sondes CPU/cache à égalité exacte avec
  Hatari, § 8) couvre la phase. Le résidu 16 MHz est celui du blitter et des IRQ (§ 8).
- **[CPU3] Échantillon IPL différé non sérialisé** · 2026-10-20 — en mode `g_iplFetch`
  (`NEOST_IPLFETCH=1`, opt-in), l'échantillon armé au save était perdu au load (IRQ reconnue
  une frontière trop tard) ; `iplDeferred` dans l'état CPU (après `iplDelay2`), refusé au chargement hors [-1,7] ; format v50.
- **[CPU4] DIVU/DIVS : préfetch après les cycles internes** · 2026-10-20 — jusqu'à 4 cycles
  d'écart par DIV en RAM sous `NEOST_RAM_SLOT` ; correctif du fork (`e4da365`) perdu à la
  vendorisation, rétabli par le port de l'amont `f90fc50` (≙ `op_80c0_13_ff`) ; sonde
  `cpu_div_ram` : 0 cycle sur 46 intervalles.
- **[CPU5] `#,SR`, `#,CCR` et MOVE to CCR** · 2026-10-20 — ANDI/ORI/EORI #,SR et #,CCR et MOVE to
  CCR rechargent les deux mots de préfetch après l'écriture de SR/CCR (`fullPrefetch`, comme
  MOVE to SR le 2026-10-19), en 68000 seulement (microcode `003c`/`023c`/`0a3c`,
  `007c`/`027c`/`0a7c`, `44c0..44fc` ; ≙ `op_027c_13_ff`) ; accès pc0+4, pc0+4, pc0+6, relecture
  en espace PROGRAMME après le changement de SR (un `ANDI #$DFFF,SR` en `$7FA` prend la bus
  error sur `$7FE` en mode utilisateur) ; durées inchangées.
- **Faute levée hors instruction** · 2026-10-20 — `Cpu68k::triggerBusError`
  (`Cpu68k.cpp:1588`) appelé hors de `moira->execute()` (`--dump-at`, débogueur) ne lève plus
  `moira::BusError` et ne pose ni `inBusError` ni `faultValid` ; seuils `NEOST_IPLFETCH4/2`
  recalculés au chargement. Le crash « BusError hors execute() via GEMDOS HD » a été corrigé le
  2026-08-13 (primitives non fautives).
- **Réveil de STOP au grain 2 contre 4 cycles** · 2026-10-22 — quantifié (ci-dessus).

### Verrous du cœur : verdicts

L'inventaire des verrous `NEOST_*` est `tools/env_locks.json` (garde
`tools/check_env_locks.py`). Verdicts face à Hatari, à ne pas refaire :
`NEOST_RAISE_WINDOW` (différé d'`ipl_fetch` à la frontière) — mesuré : Hatari CE committe
sans différé, K > 0 sur-diffère, défaut 0 ; `NEOST_PIN_ARM` réfuté ; `NEOST_VC_WAIT` redondant
avec `NEOST_RAM_SLOT` ; `NEOST_IPLFETCH` (≙ `ipl_fetch_next`) candidat de refonte non validé
en jeu ; `NEOST_SYNC_DISPATCH` (modèle sync-driven) supprimé le 2026-08-28 (deadlock Enchanted
Land, `blitter_timer` à 245 px). Sondes inertes sans variable : `NEOST_IACK_DISP=1` (fréquence
des IACK où un événement était échu), `NEOST_QDELTA_DIAG=<seuil>` (non-régression de BL3 :
écart `busOfClock(horloge CPU) − sched.now()` à chaque entrée de `Cpu68k::run` ; il vaut 40 en
régime normal — décalage de RESET, `Moira::reset` lit SSP/PC avant le démarrage de
l'ordonnanceur, constant —, ce qu'on traque est un escalier 136, 272, … 1 088).

### Faux positifs

—

---

## 13. GEMDOS HD — `GemdosHd.cpp` ↔ `gemdos.c`, `inffile.c`

**Concordant** : table d'interception (23 opcodes), Fopen/Fcreate/Fread/Fwrite/Fseek/Fattrib/
Fdatime/Frename/Fdelete, Dcreate/Ddelete/Dsetpath/Dgetpath/Dfree, handles et alias,
Pexec/LoadAndReloc/relocation, conversion des chemins, codes d'erreur et table errno → GEMDOS
(portage 1:1, audit du 2026-10-20), replis sur le TOS, partitions C..Z. Les symlinks d'un
montage sont suivis, même modèle de confiance qu'Hatari (2026-07-12).

### Écarts ouverts

—

### Écarts voulus — ne pas réaligner

Tranchés par le vrai TOS exécuté (banc `tools/run_gemdos_ab.py`, TOS 1.04, 2.06, EmuTOS) et
[C] EmuTOS `proc.c @ 5679730f` ; Hatari [D] `gemdos.c`.

- **Fforce vers un handle du TOS sous redirection hôte** · voulu [C] · basse · 2026-11-12 — NeoST
  fait trancher le TOS (la cartouche lui relance l'appel, comme pour Pexec) et ne défait la
  redirection que s'il accepte ; un EIHNDL la laisse en place. Hatari `gemdos.c @ f0736b24` la
  défait avant la réponse du TOS. Preuve : [C] EmuTOS `fshand.c` ixforce @ 5679730f. Garde :
  `run_gemdos_ab.py` cas 0x130-0x13B.
- **Date des dossiers hôtes** · voulu [C] · basse · 2026-11-12 — Dcreate date le dossier à
  l'horloge du GEMDOS émulé ; la date du dossier contenant est rétablie après
  Fcreate/Dcreate/Ddelete/Fdelete/Frename, comme sur la FAT (la date d'un dossier est celle de son
  entrée dans le parent). Hatari laisse l'heure de l'hôte. Preuve : [C] EmuTOS `fsdir.c` xmkdir
  @ 5679730f. Garde : `run_gemdos_clock.py` 16/16. [F] Windows : `utime` de MinGW sur un dossier
  non vérifié.

- **Dossier courant par processus** · voulu [C] · 2026-11-11 — rangé par basepage, hérité du
  parent, oublié à Pterm ; Hatari le tient global (un enfant qui fait Dsetpath le change pour
  son parent). Lecteur courant lu dans `p_curdrv` (basepage + `$37`). « X:NOM » et « X: »
  partent du dossier courant de X ; Hatari part de la racine.
- **Redirections Fforce par processus** · voulu [C] · 2026-11-11 — tenues avec les dossiers de
  chaque basepage, recopiées à la naissance de l'enfant ([C] EmuTOS `proc.c init_pd_files`) ;
  Fforce(std, handle du TOS ou CON) et Fclose(std) les défont pour le seul appelant. Hatari :
  table globale. Héritage figé à la création : un enfant de Pexec 3 garde le dossier et les
  redirections de son parent à sa naissance (instantané dans `pexecBpCreated`, ou par les
  descripteurs mémoire pour un enfant chargé par le TOS).
- **Fdelete / Fcreate d'un fichier ouvert** · voulu [C] — par un autre processus → EACCDN,
  rien n'est détruit ; par le processus qui l'a ouvert → détruit, et son Fclose rend EINTRN
  (−65) ([C] EmuTOS `fsopnclo.c ixdel`). Hatari détruit et rend 0.
- **Fopen et Fdelete d'un dossier → EFILNF ; Frename entre lecteurs → ENSAME (−48)** · voulu
  [C] — ([C] EmuTOS `fsdir.c xrename`) ; Hatari rend EACCDN, l'errno de `rename` ou laisse le
  TOS. Fwrite sur un fichier ouvert en lecture refusé à l'ouverture.
- **Codes propres à EmuTOS (ROM « ETOS »)** · voulu [C] — Frename vers une destination
  existante → EACCDN avant tout contrôle de la source, Fattrib dans un dossier absent → EPTHNF,
  Fread de taille négative → 0 : le GEMDOS hôte parle comme la ROM qu'il remplace.
- **Dsetpath avec un joker → EPTHNF** · conforme [C] · 2026-11-11 — comme Hatari et le vrai
  GEMDOS (TOS 1.04, 1.62, 2.06 et EmuTOS ; `SUBD\*.*` comme `*.*`). Le retrait du masque ajouté
  le 2026-07-08 pour le bureau TOS 1.62 (`GemdosHd::gemChDir`, `GemdosHd.cpp:1607`) est retiré :
  le bureau ouvre sans alerte les dossiers de A: et, lancé d'un sous-dossier, passe
  `C:\SUBD\`.
- **Attributs caché, système, archive persistés** · extension — attributs natifs sous
  Windows, attribut étendu `user.neost.tosattr` sous macOS, Linux et Android, table de session
  sur le web. Hatari ne les garde pas.
- **Alias 8.3 `BASE~N.EXT`** · extension — quand deux noms hôtes se tronquent pareil ; Hatari
  masque le second.
- **Handles hôte en 88..127** · voulu [C] · 2026-11-11 — Hatari les place en 64..127, dans la
  plage des handles du TOS (0..80, [C] EmuTOS `bdos/fs.h`) ; NeoST commence au-dessus.
- **Pexec 0 sous TOS < 1.04** · voulu [C] — basepage et environnement donnés à l'enfant, comme
  le chargeur du TOS ; la cartouche rend l'environnement si le chargement échoue.
- **Date d'un fichier créé** · voulu [C] · 2026-11-12 — celle que tient le GEMDOS de la ROM
  (ses deux mots de date et d'heure en RAM, retrouvés par ses routines Tgetdate/Tgettime ;
  [C] désassemblage TOS 1.00 à 2.06 et EmuTOS, `GemdosHd::romGemdosClock`,
  `GemdosHd.cpp:755`) : date de la ROM sur un ST sans horloge, année « 2054 » de TOS 2.06 sur
  ST, Tsetdate/Tsettime de l'invité. Hatari laisse l'horloge de l'hôte. Banc
  `tools/run_gemdos_clock.py` (16 configurations ST/Mega/STE/Mega STE).
- **NeoST plus sûr qu'Hatari** · voulu · audit du 2026-10-20 — Fwrite de taille négative →
  ERANGE (Hatari : `STmem_check` déborde en uint32, `fwrite` d'environ 4 Go,
  `cpu/memory.c:604`) ; Pexec d'un PRG plus court que son en-tête, ou d'une table de relocation
  à 3 octets de la fin → EPLFMT (Hatari lit hors tampon, `gemdos.c:4422`, `:4479`) ; effacement
  du tas avec `p_hitpa` < fin du BSS borné (Hatari boucle ~4 Go, `:4466`) ; somme
  text+data+BSS en 64 bits (Hatari 32 bits, `:4410`). Restent propres aux émulateurs les
  contrôles ERANGE/EINTRN de leurs tampons.
- **Sauvegardes (A13)** · voulu (plus strict) · 2026-10-19 — contexte invité sérialisé en v48
  (handles, position, mode, basepage, Fforce, chemins, caches DTA et Pexec), selon la surface de
  `GemDOS_MemorySnapShot_Capture` ; fichier ouvert absent, modifié (taille, horodatage) ou
  inaccessible → refus du chargement complet, plutôt qu'avertissement ou handle perdu ; `wb+`
  jamais rejoué ; racines identiques et confinement vérifiés ; données hôtes non rembobinées.
- **Autostart `--auto`** · conforme, défaut d'Hatari non reproduit · 2026-10-24 — port
  d'`inffile.c` (`GemdosHd::setAutoStart`, `GemdosHd.cpp:2718` ;
  `GemdosHd::createInfOverride`, `GemdosHd.cpp:2769`) : INF virtuel servi au Fopen du bureau,
  cartouche système installée même sans dossier (`AUTOSTART_INTERCEPT`). `get_inf_file` d'Hatari
  garde `size = sizeof(INF intégré)` alors que `get_builtin_inf` vient d'insérer la ligne `#W`
  du lecteur de boot, et `write_inf_file` (`size-offset-1`, `inffile.c:998`) tronque la fin de
  l'INF de la longueur de cette ligne ; NeoST l'écrit en entier. Écarts assumés : nom de l'INF
  comparé sans lettre de lecteur (EmuTOS le préfixe du lecteur de boot, que NeoST ne force pas) ;
  pas de surcharge de résolution (`--tos-res`), la ligne `#E` recopiée telle quelle.
- **Au banc, propres au montage** · assumé — ordre de listage, lecteur de boot et carte des
  lecteurs, Dfree, étiquette de volume, octets après le NUL d'un nom ; sous EmuTOS, Dgetpath
  d'un dossier courant détruit (pointeur pendant dans `xrmdir`, non reproduit).

### Refermés

- **Tampon ou nom dans le port cartouche** · 2026-10-20 — `$FA0000-$FBFFFF` était refusé
  (ERANGE ou repli TOS) ; Hatari l'accepte (banque ABFLAG_ROM). `GemdosHd::checkArea`
  (`GemdosHd.cpp:495`) l'admet en lecture, jamais en écriture ; quatre contrôles dans
  `--gemdos-selftest`.
- **`.` et `..` en sous-dossier** · 2026-10-19 — Fsfirst/Fsnext les exposent quand attributs et
  masque les incluent, comme Hatari et FAT ; aucun à la racine ; autres fichiers `.nom` filtrés.
- **Erreur de recherche** · 2026-10-19 — ENOENT reste une entrée à sauter, les autres erreurs
  `stat` remontent en EINTRN au lieu d'ENMFIL.
- **Correspondance « caractères invalides » (`only_invalid`)** · moyenne · 2026-06-15 —
  `GemdosHd::addPathComponent` (`GemdosHd.cpp:1271`) fait deux passes comme Hatari (troncature
  `*`, puis caractères invalides `+` → `?`), un `?` « invalide » ne correspond qu'à un caractère
  invalide (`filenameInvalidChar` ≙ `Str_Filename_Invalid_Char`, `gemdos.c:1374-1396`).
- **Recomposition Unicode NFD → NFC (macOS)** · moyenne · 2026-09-02 —
  `Str_DecomposedToPrecomposedUtf8` (`str.c:726`) porté en `hostpath::precomposeUtf8`
  (`HostPath.cpp:160`, table des 53 couples), branché aux mêmes sites qu'Hatari (recherche
  d'entrée ≙ `gemdos.c:1201/1214`, listage Fsfirst ≙ `:3182`). Sans lui, un fichier NFD était
  introuvable sous sa forme précomposée (mutation sur APFS). Ni NeoST ni Hatari ne convertissent
  le jeu de caractères : le gain est la cohérence macOS ↔ Linux. Gardes : `selftest_logic`
  (13 cas) et `gemdos-selftest` (fichier réel).

### Faux positifs

—

---

## 14. TOS et configuration — `Machine.cpp` ↔ `tos.c`

### Écarts ouverts

- **Collision de pile au démarrage de TOS 2.06** · ouvert · basse — les traces concordent sur
  1 190 instructions depuis `$E01240`, puis divergent à la VBL avant le changement de pile.
  Indépendante de Vroom. `docs/CASE_STUDIES.md`, cas TOS 2.06.

### Écarts voulus — ne pas réaligner

- **Cartouche ROM : lecture courte refusée** · assumé · basse · 2026-11-12 — une image lue
  incomplètement est refusée, la cartouche précédente reste montée ; Hatari `cart.c @ f0736b24`
  juge la taille réellement lue. Son minimum de 40 octets ([D], arbitraire) n'est pas porté.
  Garde : selftest du port générique.

- **ROM TOS exécutée intacte** · voulu [C] · 2026-11-04 — les patchs de checksum, de boot DMA
  et les variables RAM préremplies d'Hatari ne sont pas portés (audit
  `tools/audit_tos_patches.py`, lecture par famille dans `HATARI_AUTOMATION.md` § « Patchs
  TOS »). Contrôle de somme de TOS 2.06 : Hatari écrit un BRA en `$E007FA` (`tos.c:483
  @ f0736b24`, `TP_ALWAYS`) ; NeoST exécute la boucle ([C] TOS 2.06 UK, `$E015CC`-`$E015E2`),
  environ 170 trames de plus au démarrage en 4 Mo, mêmes périodes des deux côtés pour le test
  mémoire ; une comparaison datée vise la fin du test (`boot_tos206uk`).
- **TOS 2.05 sur un profil ST** · voulu [C] · 2026-11-04 — Hatari `tos.c:462-478,791
  @ f0736b24` modifie la ROM pour supprimer ses accès STE ; NeoST garde la ROM et
  `Machine::adjustMachineForTos` (`Machine.cpp:35`) choisit STE si le profil demandé est ST ou
  Mega ST (profils STE/Mega STE conservés). [C] TOS 2.05 FR `$E00096` : le son DMA est touché
  avant les vecteurs d'exception. Boot à 2 000 trames : bureau identique à Hatari STE sans
  patch, 0 px. (Rétrogradation en ST d'un TOS ≤ 1.04 sur STE/Mega STE, comme Hatari, aussi côté
  WASM depuis le 2026-08-07.)

### Refermés

—

### Faux positifs

—

---

## 15. E/S : imprimante, joypads, joysticks — `Machine.cpp`, `StePads.hpp`, `JoystickInput.hpp` ↔ `psg.c`, `printer.c`

**Concordant** : StePads (multiplexage `$FF9202`, boutons et directions des pads A/B, DIP du
Mega STE `$BF`, paddles REALSTICK, crayon optique, bus errors octet/mot `$FF9200-23` ; sélection
JOYWE et écriture en bus error d'après la netlist, § 7), JoystickInput (mapping, ports), suivi
du front de strobe Centronics.

### Écarts ouverts

- **Joysticks « parallel port » sur les ports A/B du PSG** · ouvert · très basse — Hatari
  recompose R14 bit 5 / R15 depuis ces joysticks à la lecture (`psg.c:289-312`) ;
  périphérique de niche, non émulé.

### Écarts voulus — ne pas réaligner

—

### Refermés

- **Imprimante Centronics** · moyenne · 2026-06-15 — sur chaque front de strobe, l'octet du
  port B est capturé dans un fichier hôte (`Machine::setPrinterFile`, `Machine.hpp:95` ;
  `--printer FILE`) et BUSY (GPIP0) est asserté bas (≙ `psg.c:388-390`,
  `Printer_TransferByteTo` + `MFP_GPIP_Set_Line_Input LINE0 LOW`) ; sans fichier, rien.

### Faux positifs

—

---

## 16. Extensions NeoST sans équivalent Hatari

Inactives par défaut et sans effet sur les étalons (réseau OFF pendant
`run_all.py --tier fast/full`) : ce ne sont pas des écarts à corriger.
→ [`docs/EXTENSIONS.md`](EXTENSIONS.md)

- **UltraSatan sur le bus ACSI** · extension · 2026-08-21 — `src/io/UltraSatan.*`,
  `Acsi::attachUltraSatan` (`Acsi.cpp:138`). Hatari n'émule qu'un disque ACSI générique ; NeoST
  ajoute sur demande les 2 slots, l'INQUIRY `JOOKIE  UltraSatan`, l'état « slot vide = NOT
  READY » et les paquets ICD `$20 'US…'` du firmware v1.20 (atarijookie/ce-atari). Ces paquets
  ne sont routés que vers les cibles UltraSatan ; une cible ordinaire reste identique à
  `hdc.c`.
- **NetUSBee : ISP1160 USB sur le port cartouche** · extension · 2026-08-21 —
  `src/io/Isp1160.*`, `Bus::read8Slow` (`Bus.cpp:439`). La moitié NE2000 est l'EtherNEC ;
  l'ISP1160 est décodé aux adresses du pilote FreeMiNT. Depuis le 2026-11-10 : deux ports
  (clavier/souris HID, disque SCSI/Bulk-Only sur image), horloge USB de 1 ms et transferts ATL,
  validés avec USB4TOS `098f002a`. La fenêtre LSB `$FA0000-$FA01FF` partage une lecture du CR
  Ethernet sans le modifier ; le document de conception de 2006 place ce latch ailleurs, la
  révision reste à mesurer sur une vraie carte. ITL/isochrone et timing électrique non
  exécutés. → [`docs/NETUSBEE.md`](NETUSBEE.md)

---

## Annexe — où sont passées les anciennes sections

Ce document était rangé par passe d'audit jusqu'au 2026-11-12. Les renvois d'autres documents
et des messages de commit vers ses anciennes sections se lisent ainsi :

| Ancienne section | Où lire désormais |
|---|---|
| Priorités — divergences HAUTE et MOYENNE (tableau `BL1`…`D4`) | entrée de même identifiant dans son sous-système |
| Détail par sous-système | §§ 1 à 13 |
| Cas où NeoST améliore Hatari | rubriques « Écarts voulus » de chaque sous-système |
| 2ᵉ passe (2026-06-15), dont *Nouveaux — BASSES* (Vidéo), BUS-LEAK, BL-GPIP3, M-MIDI | § 2 (V2, `$FF8261`), § 7, § 8, § 6 ; récit : `CHANGELOG.md`, 2026-06-15 |
| 3ᵉ passe (RTC, ACSI, GEMDOS, FPU, SCU) | §§ 10, 4, 13, 11, 9 ; récit : `CHANGELOG.md`, 2026-06-15 |
| 4ᵉ passe (CPU, E/S, Centronics) | §§ 12, 15, 9 ; récit : `CHANGELOG.md`, 2026-06-15 |
| 5ᵉ passe (2026-07-07) et ses chantiers Super Hang-On et Rick Dangerous | sous-systèmes ; récit : `CHANGELOG.md`, 2026-07-07 |
| État des mécanismes CPU « gated » | § 12, « Verrous du cœur » ; `tools/env_locks.json` |
| 6ᵉ passe (2026-07-12), restes `M3`, `B1`…`B15` | numéros suffixés « (6ᵉ p.) » ; récit : `CHANGELOG.md`, 2026-07-12 |
| 7ᵉ, 8ᵉ, 9ᵉ passes (2026-08-06) | sous-systèmes ; défauts sans rapport avec Hatari : `CHANGELOG.md`, 2026-08-06 |
| 10ᵉ passe (2026-08-06/07) | § 12 (IACK), § 5, § 2 ; banc et mesures : `CHANGELOG.md`, 2026-08-06/07 |
| 11ᵉ passe (`trace_odd`, `overscan_top`) | § 2, « Lignes 60 Hz isolées (A40) » ; récit : `CHANGELOG.md`, 2026-08-29 |
| Extensions NeoST sans équivalent Hatari | § 16 |
| Sections datées du 2026-11-03 au 2026-11-12 (MFP, SCC, ACIA, WD1772, STE/DHS, TOS) | sous-système de la puce |
| Documents internes périmés supprimés | en-tête |
