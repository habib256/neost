# Inventaire cycle-exact — travail restant (oracle : Hatari)

> (c) 2026 VERHILLE Arnaud. Inventaire établi en diffant systématiquement
> `extern/hatari/src` (source de vérité) contre le code NeoST, sous-système par
> sous-système. Complète [`CYCLE_ACCURACY.md`](CYCLE_ACCURACY.md) (le plan, phases 1-6
> faites) : ici, **uniquement ce qui manque encore**, trié par priorité.
>
> Affirmations vérifiées dans les sources (pas seulement rapportées) : délai IRQ MFP
> 4 cyc (`mfp.c:374,783,899`), `Pending_Time[]` (`mfp.c:963-1120`), arbitrage blitter
> 64/64 (`blitter.c:251-252,395`), absence d'écriture `$FF8205/07/09` côté NeoST
> (`Shifter.cpp:write8`), `NewHWScrollCount`/`NewLineWidth`/`RestartVideoCounter`
> (`video.c:522,526,541`).

## Vue d'ensemble

Ce qui est DÉJÀ acquis (cf. CHANGELOG/CYCLE_ACCURACY) : Scheduler daté préemptif à
horloge continue, `liveNow()` sous-instruction (Moira), timers MFP A/B/C/D mode délai
+ event-count (Timer B daté au cycle 400 de la ligne, Timer A sur XSINT), machine
GLUE STF complète (bordures H/B/G/D détectées, self-test 19/19), Spec512
pixel-identique, wait states shifter/PSG/MFP/ACIA + E-Clock, FDC rotationnel complet
+ STX, Microwire/LMC1992 datés. **Le gros du squelette est là** ; ce qui reste est
de la précision fine — mais c'est elle que sondent les démos.

| Priorité | Chantier | Effort | Étalon type |
|----------|----------|--------|-------------|
| **P1** | Chaîne IRQ MFP fine (délai 4 cyc, chronologie, IACK) | faible→moyen | jeux qui pollent un flag effacé par IRQ |
| **P1** | Registres vidéo STE différés (HSCROLL/LINEWIDTH/compteur) | moyen | Cuddly scrolling, démos STE |
| **P1** | Rendu live bordure basse + restes bordures | moyen | scroller bas du menu Cuddly |
| **P2** | Blitter non-hog (arbitrage bus 64/64) | élevé | démos CPU+blitter simultanés |
| **P2** | Restes vidéo : ENDLINE, VBL 64/68, restart compteur | moyen | démos fullscreen |
| **P2** | Son DMA : compteur d'adresse live | moyen | STE_Test, sync zik/raster |
| **P3** | Wakeup states WS1-4, jitter HBL/VBL, med-res overscan | élevé | démos « extrêmes » (Closure…) |
| **P3** | Unité interne ×256 (Phase 5), fréquences exactes | faible | dérive long terme |

---

## P1 — Chaîne d'interruption MFP fine (`mfp.c`) — ✅ FAIT (2026-06)

Porté et validé (cf. CHANGELOG § Interruptions) : batteries Z des 3 diagnostics Pass
sur les 2 cœurs, T4 Video Counter inclus.

- [x] **Délai IRQ→CPU de 4 cycles** (`MFP_IRQ_DELAY_TO_CPU`, `mfp.c:374`) — signal
      `irq_`/`irqTime_` daté + événement `Scheduler::MFP_IRQ` à T+4 qui committe l'IPL
      (`Cpu68k::updateIplNow` → `commitIpl` broche + `reg.ipl`). ⚠ Leçon : sans le
      commit, le délai s'ADDITIONNE au pipeline IPL de Moira (poll à l'instruction
      suivante) → +1 instruction de latence et T4 échoue.
- [x] **Chronologie multi-IRQ** (`Pending_Time[]`, `mfp.c:963-1120`) — `pendingTime_`
      par canal + gate `pendingTimeMin_` ; timers antidatés de leur échéance réelle
      (`raiseAt(due)`, port `Interrupt_Delayed_Cycles`).
- [x] **Ré-évaluation du vecteur à l'IACK** (`MFP_ProcessIACK`) — `iack()` recalcule
      le signal au cycle de lecture du vecteur (Moira appelle `readIrqUserVector` au
      bon cycle de l'exception, la latence des 12 cyc est dans le timing d'exception
      du cœur). Canal désactivé → requête effacée (port fidèle).
- [x] **Offset fin d'instruction des timers** — NON-ITEM, vérifié dans les sources :
      en mode CE Hatari date à `clock + currcycle` (`cycles.c:315-321`) = exactement
      `Scheduler::liveNow()` sous Moira. L'item « Phase 6 reste » du plan est clos.
- [~] **Résidu de période** (`PendingCyclesOver % période`, `mfp.c:1421`) :
      `scheduleTimerAt` réaligne déjà sur la grille par modulo quand le retard dépasse
      une période. Reste le cas Timer D USART ultra-court sous RS232 réel (non émulé).
- [ ] **Backing-store du compteur à l'arrêt** (`TA/TB/TC/TD_MAINCOUNTER`,
      `MFP_StartTimer_AB/CD`) : écrire TxCR=0 en plein décompte doit figer le compteur
      courant relisible, et un restart repart de cette valeur. NeoST : remise à zéro
      de l'échéance, pas de gel exact du compteur intermédiaire. Cas rare
      (« Punish Your Machine »). _Effort moyen, valeur basse._
- [ ] **Pulse-width (modes 9-15)** : Hatari lui-même l'approxime en mode délai
      (GPIO3/4 non émulés) → porter la même approximation suffit. _Valeur quasi
      nulle, pour mémoire._

## P1 — Registres vidéo STE à application différée (`video.c`) — ✅ FAIT (2026-06, sauf 2 restes)

Porté via le **compteur vidéo MATÉRIALISÉ** (port `pVideoRaster` : base latchée au
début de trame ≙ `Video_ClearOnVBL`, avance du stride RÉEL à chaque fin de ligne
active — `Shifter::vcLineBase_`/`endVideoLine`). Conséquence fidèle au matériel :
une écriture `$FF8201/03` en cours de trame ne s'applique qu'à la trame suivante.
Validé : Spec512 byte-identique, overscan_top byte-identique, batteries T (lecture
du compteur) Pass 2 cœurs, bureau/jeux propres.

- [x] **Écriture du compteur vidéo `$FF8205/07/09`** (`Video_ScreenCounter_WriteByte`)
      — immédiat hors affichage (incl. bordure droite : notre rendu à DE_end est déjà
      passé, ≙ `pVideoRasterDelayed`), différé pendant le DE (`vcDelayedOffset_`,
      relu par `$FF8205/07/09` comme Hatari). Étalons à tester : Stardust Tunnel STE,
      Braindamage.
- [x] **HSCROLL `$FF8264/65` différé** (`NewHWScrollCount`) — immédiat si cycle ≤
      HDE_On ou faisceau hors zone affichée, sinon appliqué en fin de ligne.
- [x] **LINEWIDTH `$FF820F` différé** (`NewLineWidth`) — immédiat si cycle ≤ DE_end,
      sinon fin de ligne. Le stride par ligne est désormais ACCUMULÉ (lineWidth
      variable par ligne = bump mapping Pacemaker enfin représentable).
- [ ] **`bSteBorderFlag` / mode 336 px** (`video.c:530,1520-1540`) : combo
      `$FF8265>0` puis `$FF8264=0` → 16 px de plus à gauche (prefetch sans scroll).
      Toujours absent. _Effort moyen._
- [ ] **Restart du compteur vidéo ligne 310/260** (`RestartVideoCounter*`) :
      ⚠ TENTÉ PUIS RETIRÉ — les logiciels qui pollent « compteur revenu à la base »
      sortent de leur attente à la ligne 310 et posent leur bascule 50/60 Hz à la
      frontière de trame, là où `beginFrame` VERROUILLE la géométrie → la trame
      entière bascule en 263 lignes (étalon overscan_top : 0 détection de bordure).
      Hatari n'a pas ce problème (machine d'état continue). À reporter APRÈS le
      chantier « bascule 50/60 Hz en cours de trame / géométrie par ligne ».
      Étalon : ULM Dark Side of the Spoon. _Dépendance structurelle._

## P1 — Restes bordures / rendu live (suite de TODO.md § Vidéo)

Déjà tracés dans TODO.md, rappelés ici pour l'ordonnancement :

- [ ] **Rendu live du retrait BAS** (le scroller de bordure basse du menu Cuddly
      n'est pas rendu) — le retrait est détecté par la glue mais le rendu de la
      timeline live ne suit pas. _Effort moyen._
- [ ] **Scrolling Cuddly qui saute** quand le robot bouge — à diff'er à l'oracle ;
      suspects : écritures différées ci-dessus, base vidéo `$FF8201/03` mid-frame,
      datation `$FF8209`. _Diagnostic d'abord._
- [ ] **Lignes EMPTY / BLANK / NO_DE au rendu** : détectées par la glue
      (`glue::NO_COUNT/BLANK/NO_DE`), mais le rendu live ne noircit/saute pas
      systématiquement ces lignes hors `renderGlueFrame`. _Effort faible._
- [ ] **Pixel-perfect gauche/droite end-to-end** : dépend de la contention bus
      résiduelle ; re-valider `make_overscan_lr.py` après chaque chantier P1.

## P2 — Blitter non-hog (`blitter.c`)

- [ ] **Arbitrage bus 64/64** (`Blitter_BusArbitration`, `blitter.c:395-470` ;
      `BLITTER_NONHOG_BUS_BLITTER/CPU = 64`, lignes 251-252) : en mode non-hog le
      blitter prend 64 accès bus, rend la main 64 cycles au CPU, et ainsi de suite ;
      chaque mot lu/écrit compte (`CountBusBlitter`, +1 accès CPU compté blitter —
      bug matériel reproduit, `blitter.c:886-888`). NeoST : HOG pur, transfert
      instantané hors temps CPU. Indispensable aux démos qui calculent PENDANT un
      blit et aux raster-effects blitter. Nécessite de voler des cycles au CPU
      (même mécanique que `addBusWaitCycles`). _Effort élevé — c'est le plus gros
      morceau structurant restant._
- [ ] **Datation de l'IRQ blitter et du bit BUSY** pendant le transfert étalé dans
      le temps (suit l'arbitrage ci-dessus).

## P2 — Restes vidéo « plan » (Phase 2b/3, déjà notés dans CYCLE_ACCURACY)

- [ ] **`VIDEO_ENDLINE`** (événement de fin de ligne distinct du HBL).
- [ ] **Position cycle-exacte du VBL** : vraie fin de trame + offset STF 64 / STE 68
      (`VBL_VIDEO_CYCLE_OFFSET_*`, `video.h:119-120`).
- [ ] **Phase exacte du tic Timer C** (au cycle de programmation, pas sur la grille).
- [ ] **Lecture compteur vidéo à cheval sur deux lignes** (`Video_CalculateAddress`
      considère la ligne précédente si la lecture traverse le HBL ; NeoST ne décode
      que la ligne courante). _Effort faible, cas marginal._

## P2 — Son DMA STE (`dmaSnd.c`)

- [ ] **Avance live du compteur d'adresse** (`Sound_Update(CyclesGlobalClockCounter)`,
      `dmaSnd.c:737+` ; `DmaSnd_GetFrameCount`, `dmaSnd.c:748-759`) : `$FF8909/0B/0D`
      doit refléter la position au CYCLE de la lecture ; NeoST n'avance le compteur
      qu'au rythme de la synthèse audio. Étalon : STE_Test (poll du compteur), démos
      qui synchronisent raster sur la position sample. Déjà « Phase C reste » au
      TODO. _Effort moyen._
- [ ] **Vol de cycles du DMA son sur le bus** : NON modélisé par Hatari non plus →
      même décision que la contention vidéo MAME : **ne pas faire** (divergerait de
      l'oracle).

## P3 — Précision « démos extrêmes » (faible priorité, effort élevé)

- [ ] **Wakeup states WS1-4** (`VIDEO_TIMING[STF_WS1..WS4]`, `video.c:626-680`) :
      4 jeux de timings ±1 cyc sélectionnables (NeoST = WS3 figé, choix déjà acté
      pour le mode cycle-exact Moira). Étalon : Closure (Sync), tests Troed/SYNC.
      Structure à prévoir : table de timings au lieu de constantes `glue::`.
- [ ] **Jitter HBL/VBL** (`HblJitterArray/VblJitterArray`, `video.h:162-169`,
      `InterruptAddJitter` côté cœur UAE) : motif déterministe de 5 valeurs (0/4/8 cyc)
      ajouté à la latence des autovecteurs. NeoST : alignement fixe.
- [ ] **Overscan med-res** (`BORDERMASK_OVERSCAN_MED_RES`, `LEFT_OFF_MED`,
      `video.c:1637-1731`) + **224 octets STE en med-res** : détection des
      enchaînements hi/med/lo et rendu med à largeur variable. Étalon : No Cooper
      Greetings. NeoST : rendu med-res basique, pas de détection.
- [ ] **NO_SYNC / SYNC_HIGH** (`video.c:2553-2592`) : lignes vides injectées par
      manipulation du sync hi-res ; masques présents côté glue NeoST mais
      `glueBlankLines_` n'influence pas le rendu.
- [ ] **Quirk miroir écriture octet palette** (`Video_ColorReg_WriteWord`) — déjà
      au TODO (risque élevé).

## P3 — Horloges, cœurs, divers

- [ ] **Unité interne ×256** (`CYCINT_SHIFT=8`, `cycInt.h:53-63`) = Phase 5 du plan :
      NeoST tronque la conversion 31333/9600 à l'entier par échéance ; l'unité
      interne supprime la dérive résiduelle long-terme. _Effort faible, valeur
      faible (dérive aujourd'hui négligeable car replanification ancrée)._
- [ ] **Fréquences exactes centralisées** (`clocks_timings.c` : CPU 8021248 Hz PAL,
      VBL ≈ 50,05 Hz) : NeoST raisonne en cycles purs (50 fps bridés au GUI) ; seul
      impact réel = synchro audio long-terme. _Pour mémoire._
- [ ] **Datation sous-instruction sous MUSASHI** : Musashi débite ses cycles en fin
      d'opcode → tous les mécanismes ci-dessus (délai 4 cyc, IACK, écritures datées)
      n'auront leur pleine précision que sous **Moira** (cœur recommandé, choix déjà
      acté). Les `add*WaitCycles` restent no-op sous Musashi : assumé, à documenter
      par étalon plutôt qu'à « corriger » (porter le pairing Hatari 49×49 +
      `BusCyclePenalty` dans Musashi serait un gros effort pour un cœur de
      compatibilité). _Décision, pas un chantier._
- [ ] **Offsets de datation Moira vs Hatari** : les constantes empiriques
      `kVideoCounterReadOffsetCyc=-2` et `kSpec512AlignCyc=-23` compensent la
      convention de datation de Moira (vs -8/-28 chez Hatari). Validées
      pixel-identiques, mais chaque nouveau mécanisme daté devra choisir son offset
      avec la même méthode (sweep vs oracle). _Méthodologie à garder en tête._
- [ ] **MegaSTE 8/16 MHz + cache 16 Ko** — déjà au TODO (§ CPU), change tous les
      débits de cycles (`clocks_timings.c`, `m68000.c:MegaSTE_CPU_Cache_Update`).

## Ce qu'on ne fera PAS (décisions actées)

- **Contention DMA vidéo générale sur la RAM** (le shifter vole des cycles au CPU
  pendant le DE) : modèle MAME (`stmmu.cpp::bus_contention`), **pas Hatari** →
  l'ajouter ferait diverger NeoST de l'oracle qui valide nos étalons au pixel.
- **Vol de cycles DMA son/FDC sur le bus** : idem, Hatari ne le modélise pas.
- **Pairing d'instructions côté Musashi** : Moira couvre le besoin cycle-exact.

## Méthode de validation (rappel)

Chaque chantier : (1) porter depuis `extern/hatari/src`, (2) re-tester les étalons
(`tools/run_etalons.py`, `--glue-selftest`, Spec512 byte-identique, batteries Z des
cartouches de diag sur les DEUX cœurs), (3) diff à l'oracle
(`tools/hatari_oracle.sh`, `tools/trace_diff.py`). Les étalons logiciels par
sous-système sont catalogués dans [`TEST_SOFTWARE.md`](TEST_SOFTWARE.md).
