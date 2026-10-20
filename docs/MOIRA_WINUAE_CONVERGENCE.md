# Convergence Moira ↔ WinUAE — chantier « full timing-engine » (beam-sync)

**But.** Rendre le timing de Moira (cœur 68000 de NeoST) **cycle-identique à WinUAE** (= le cœur
CPU qu'utilise Hatari, `extern/hatari/src/cpu/`), puis **retirer les hacks empiriques** de datation
vidéo de NeoST. C'est la cause racine commune des bugs beam-sync (Lethal Xcess, Enchanted Land,
Cuddly, Super Hang-On). Décision utilisateur (2026-06-16) : **garder le sync-driven (PT=true)** +
**full convergence** (pas un graft chirurgical).

> 🧭 **Cadre.** Ce doc est le **front actif** de la précision cycle. Le cadre général (modèle
> Hatari, phases acquises, inventaire priorisé du restant) est dans
> [`CYCLE_ACCURACY.md`](CYCLE_ACCURACY.md) ; les écarts logiques bornés dans
> [`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md).
>
> ⚠️ Ce doc CORRIGE plusieurs notes mémoire optimistes/contradictoires. Lire d'abord §3 « Vérités
> mesurées » avant de rouvrir une piste.
>
> ⚠️ **Sondes : état revérifié le 2026-10-20.** `NEOST_EXC_DIAG` (constatée absente le
> 2026-08-19) est de nouveau dans le Moira vendorisé (`[PIN]` dans `Moira.cpp`, `[EXC]` dans
> `MoiraExceptions_cpp.h`, `[JTV]` dans `MoiraDataflow_cpp.h`, cf. `extern/moira/NEOST_VENDOR.md`).
> `NEOST_HAT_IPLDIAG` est le gate d'un Hatari PATCHÉ, jamais dans `extern/hatari` tel quel : le
> patch est `tools/hatari_neost_oracle.patch`. `NEOST_SYNC_DISPATCH` a été **supprimée** (A34,
> 2026-08-28, cf. `tools/env_locks.json`). Toutes les autres `NEOST_*` de ce doc sont vivantes
> dans `src/`.

---

## ⭐ ÉTAT COURANT (posé le 2026-08-27 — lire CECI avant tout le reste)

**Le chantier beam-sync est CLOS** (2026-07-09, complété 2026-08-06). Ce document est un
**journal par accrétion** : les couches anciennes énoncent des valeurs et des plans
**supplantés depuis** sans être barrés — l'audit du 2026-08-27 a constaté qu'il fallait
le lire chronologiquement en entier pour connaître l'état. Ce bloc s'en charge :

- **Verdict final** : « beam-sync EL CONVERGÉ, transitoire d'entrée INCLUS — aucun
  résidu NeoST » (re-mesure oracle du 2026-07-09 : datation re-arm 438/442/446, σ 3,0,
  meilleure que la cible Hatari ~444 ±8 ; diff `$8209` d'entrée byte-identique ;
  retrait haut `start=34` sur 249/249 trames). EL 12402/12402, Cuddly 250/250,
  Super Hang-On résolu (IACK MFP vectorisé 12→16 cyc, 2026-08-06).
- **Valeurs de datation EN VIGUEUR** (celles du code, 2026-07-03) :
  `kVideoCounterReadOffsetCyc = −6`, `kSyncWriteOffsetCyc = +2`,
  `kSpec512AlignCyc = −25`. ⚠ Les couches du 2026-07-02 ci-dessous citent encore
  read −14 / write −6 : c'était l'étape intermédiaire, supplantée le lendemain.
  Read et write se déplacent PAR PAIRE — bouger l'un seul casse Enchanted Land.
- **Le « plan d'attaque pour la prochaine passe » du §8 est EXÉCUTÉ** — le futur de
  ce paragraphe est un futur de juillet 2026.
- **Ce qui reste d'ouvert n'est PAS ici** : inventaire à rendement décroissant →
  `CYCLE_ACCURACY.md` §4 (V3/attribution de ligne — bloquée sur le segfault
  `NEOST_LINELEN_ATTR` — son segfault est corrigé le 2026-08-28 (A16b), et le verrou est
  **ON par défaut depuis le 2026-09-01** (V3 clos : prouvé à l'oracle sur l'étalon généré
  `freq_switch`, cf. `TODO.md` § restes) —, retrait bas live, interfoliage blitter, WS1-4).

Le reste du document est conservé tel quel comme MÉMOIRE du chantier (mesures,
réfutations, recettes de bancs) — précieux pour ne pas rejouer une piste morte, mais
plus normatif nulle part où ce bloc dit autre chose.

---

**État au 2026-07-02 (résumé de l'époque).** Tout est sur `main`, **build vert** (`run_etalons` 19/0 + TOUS OK).
- 🎯🎯 **REFONTE COORDONNÉE §8 EXÉCUTÉE (2026-07-02, oracle Hatari 2.6.1-devel bâti dans
  `extern/hatari/build`, Ubuntu).** Le résidu de phase **+24 est ATTRIBUÉ et CORRIGÉ** — c'était
  un CUMUL de 4 biais, tous mesurés :
  1. **+8 IACK sur-compté** : l'ancien `willInterrupt` ajoutait E-wait+14 PAR-DESSUS les
     `SYNC(4)+SYNC(4)` stock de Moira. → hooks `iackSyncBefore/After` AU point d'IACK réel
     (Moira.h + MoiraExceptions_cpp.h vendorisés, `NEOST_IACK_AT` défaut ON). Fait émerger le
     motif **mod-20** des positions d'IRQ (= Hatari).
  2. **+2 skew d'alignement** : `chipWait8` alignait le point-MILIEU de l'accès Moira au lieu du
     DÉBUT (WinUAE) → fin d'accès ≡2 mod 4 vs ≡0 Hatari. → `slot=(c+bias−2)&3`.
  3. **−8 origine d'horloge trame** (NeoST lit +8 vs les coordonnées ligne Hatari), mesuré 2×
     indépendamment : read fidèle empirique **−14** (banc poll + variante `lsr#3` exposant le
     bit 3 : l'ancien +4 était FAUX de +8 octets, invisible en palette) ; write fidèle **−6**
     (diff du calibrateur loader EL, beam-syncé : positions 456/452/448/444 = Hatari EXACT).
     → `kVideoCounterReadOffsetCyc=−14`, `kSyncWriteOffsetCyc=−6`, `syncCpuBus` aligne fc+2.
  4. **HBL à la frontière de ligne** (512, `HBL_VIDEO_CYCLE_OFFSET=0`) au lieu de cpl−4
     (calibration périmée d'avant l'IACK fidèle). `NEOST_HBL_OFF` pour A/B.
  - **+ 2 bugs structurels débusqués par l'oracle** : (a) le **commit du compteur à DE_end** —
    une écriture 60 Hz datée 376 (scan de calibration du loader EL, ouverture bordure droite)
    arrivait APRÈS le commit → $8209 figé → **loader bloqué à $ee78 pour toujours** ; fix =
    commit PARESSEUX (`while (vcLineY_ < y) endVideoLine()` en tête de renderLine, ≙
    `Video_EndHBL`). (b) **`Video_RestartVideoCounter` NON porté** (ligne 310/260, cycle 56) :
    la base était latchée à la ligne 0, AVANT le handler VBL du jeu → le stabilisateur d'EL
    lisait l'ANCIEN buffer toute la trame (aveugle) ; fix = event `VC_RESTART` (Scheduler) +
    latch figé (`vcRestartBase_`). L'étalon `overscan_top` re-baseliné : les 24 px de diff
    étaient FAUX dans l'ancienne référence (vérifié pixel-à-pixel contre Hatari).
  - **RÉSULTATS** : poll-bench **180/180 byte-identique** Hatari (2 variantes) ; loader EL
    **répare** ; flicker EL **38-40/40** (base 34/40) ; **LX titre 0,00 % de churn** (était
    ~1,5 %) ; EL en jeu : paysage NET, moteur fullscreen verrouillé **72 %** (53 % avant HBL@512)
    vs Hatari 100 % ; étalons **19/0 + TOUS OK**. `NEOST_IPLFETCH=1` casse le loader EL même en
    config fidèle → reste OFF.
  - **RESTE (pièce vidéo, plus CPU)** : le moteur par-ligne d'EL verrouille à **−16 (freq) /
    +4 (res)** vs l'oracle in-game (Hatari : freq 376/384, res 4/12 & 444/456, sd=0) → ses
    impulsions freq ratent la fenêtre bordure-droite (372,376] → retraits G/D sporadiques au
    lieu de chaque-ligne → micro-sauts de scroll résiduels. Balayé : `NEOST_ECLOCK_PHASE`
    (déplace le TAUX 57-75 %, jamais la POSITION), offsets read (cassent le loader, verrouillé
    par l'oracle). Piste suivante : instrumenter l'entrée du handler HBL in-game (+4 constant
    sur les sites res à pc IDENTIQUES ; le +(−20) du site freq vient du nop-slide auto-patché).
    Oracle : cmd-fifo réel ~100 s (`hatari-event keydown 57` / `keyup`, `hatari-option --trace
    video_sync,video_res` — PAS `hatari-trace`, et la fifo est créée PAR Hatari).
  - 🔬 **BANC DÉDIÉ AU RÉSIDU : `tools/make_poll_entry_test.py`** (poll-test + délai ~254 cyc →
    lecture $8209 EN plein DE = la couleur encode la PHASE D'ENTRÉE du handler HBL au cycle près —
    le poll-test de base lit pendant le blank, compteur FIGÉ, donc INSENSIBLE à l'entrée : il ne
    validait PAS la latence d'exception). Mesures 2026-07-02 :
    * **Structure de quantification IDENTIQUE** Hatari↔NeoST : entrées par période de 5 lignes
      {x, x+4, x+8}, ratio 2:2:1, deltas/ligne {508,512,520} mêmes proportions.
    * ✅ **« Latence d'exception −12 » = ARTEFACT DE TRACE, RÉSOLU (2026-07-02, 2ᵉ passe).** Le
      Tracer NeoST logue l'instruction AVANT le check d'IRQ → quand l'exception préempte, la
      dernière ligne tracée est un **FANTÔME** (loggée, jamais exécutée) : le Δ mesuré en
      soustrayait 12 à tort. Instrumenté au vrai chemin (`NEOST_EXC_DIAG` → [EXC]/[JTV]/[PIN]) :
      **latence d'exception NeoST = Hatari EXACTEMENT** ({56,60}, idle+PClo=12, iack=E+14,
      SR/PChi=8-10, jumpToVector=20).
    * 🎯 **VRAIE DIVERGENCE TROUVÉE ET CORRIGÉE : la BROCHE IPL montait au DISPATCH de bloc**
      (frontière = fin de la dernière instruction du bloc, 0..24 cyc APRÈS l'instant vrai, avec
      jitter = le dépassement) au lieu du cycle exact — Hatari la lève DANS do_cycles, en cours
      d'instruction, et l'instruction qui ENJAMBE l'événement la voit à son échantillon IPL
      (fin−4). **Fix : broches PRÉ-ARMÉES** (`Cpu68k::armHblPinAt/armVblPinAt` posées à la
      PLANIFICATION, appliquées par le hook `sync()` au cycle bus près, mid-instruction — le
      dispatch BLOC est conservé, seul la broche est exacte). `NEOST_PIN_ARM` (bit0 HBL, bit1
      VBL) — **défaut 1 = HBL seule** : la broche VBL exacte casse le loader d'Enchanted Land
      (chargement infini — couplage VBL↔IKBD/FDC à investiguer).
    * ✅ **Position de l'INT HBL = cpl−4 = 508 CONFIRMÉE fidèle** (`Hbl_Int_Pos_Low_50 =
      CYCLES_PER_LINE_50HZ − 4`, video.c:978 — le commentaire « HBL_VIDEO_CYCLE_OFFSET=0 » de
      video.h est TROMPEUR). Le passage à 512 de la 1ʳᵉ passe ne gagnait que parce qu'il
      compensait la broche en retard → **revenu à 508** (défaut `NEOST_HBL_OFF=-4`).
    * ⚠ **PIÈGE DU BANC : la phase E-clock PRÉCESSE de −4 cyc PAR TRAME** (160256 ≡ 16 mod 20,
      période 5 trames) → comparer une capture NeoST à **5 trames consécutives** de l'AVI Hatari
      (le motif tourne). Et les constantes ajoutées au bloc IACK sont ABSORBÉES par la boucle
      main 12 cyc (équilibre auto-verrouillé : `IACK_VIDEO` 18/22/26 → images byte-identiques).
    * ✅ **LE −16 DU MOTEUR EL = MODÈLE VIDÉO, TROUVÉ ET CORRIGÉ (2026-07-02, 3ᵉ passe).**
      `Video_CalculateAddress` (video.c:1508-1565) NE lit PAS DisplayStart/EndCycle de la Glue
      pour l'intra-ligne : il reconstruit **ds** (LEFT_OFF → LINE_START_CYCLE_71 = **0**, pas le
      HDE_On_Hi=4 de la Glue ; LEFT_PLUS_2 → 52) et la **taille** = CurSize×2 par la table de
      bordures (fullscreen = 230, pas (End−Start)/2 = 229). L'erreur de 2-4 octets sur lignes à
      tricks faussait l'estimation faisceau du stabilisateur d'EL → impulsions à −16. **Après le
      port fidèle : le moteur verrouille à 376/384 = HATARI BYTE-EXACT** (res à −4, une case) ;
      trames verrouillées churn ~20 % vs 17 % Hatari (le scroll du paysage est LÉGITIME —
      churn Hatari in-game = 17 % constant, mesuré à l'AVI).
    * ◑ **RESTE (dernier verrou) : le TAUX de lock (55-66 % vs Hatari 100 %)** = la
      micro-structure de reconnaissance IPL. Implémenté (gated `NEOST_IPLFETCH`, défaut OFF) :
      la règle `ipl_fetch_next` COMPLÈTE — seuil `cdp` sur le changement PRÉCÉDENT
      (`ipl_pin_change_evt_p`), report différé ≙ `regs.ipl[1]` appliqué à la frontière suivante
      (rotation run-loop). Insuffisant seul (poll-entry 54/180) : la parité byte exige (a) l'audit
      des POINTS d'échantillonnage PAR INSTRUCTION (Moira POLL_IPL vs WinUAE fin−4 uniforme +
      re-sample « changement à pre+2 mid-accès », newcpu.c:5030), (b) la **broche MFP (niv 6)
      exacte** (chaîne TIMER_B→MFP_IRQ encore dispatch-late) — c'est elle qui fait que
      `IPLFETCH=1` casse le loader EL (boucle FDC sur IRQ MFP : broche tardive + report = un
      octet raté).
    * 🚨 **4ᵉ PASSE (régression SHO) — pré-armement RETIRÉ, remplacé par le COMMIT au dispatch.**
      Le pré-armement (broche mi-instruction) créait une DOUBLE-PRISE HBL (exception dans le
      bloc → IACK efface → le callback re-lève → 2ᵉ HBL, période libre ~427 cyc) → course
      Super Hang-On injouable. Oracle INSTRUMENTÉ (fprintf [HPIN]/[HFETCH]/[HEXC] dans
      extern/hatari, gated NEOST_HAT_IPLDIAG) : **Hatari CE traite les INT vidéo À LA
      FRONTIÈRE d'instruction** (CycInt au boundary, currcycle flushé par instruction) et
      `intlev_load → ipl_fetch_now` commit l'IPL SANS délai → **exc−broche = 0** (12709/12711).
      La règle `ipl_fetch_next` 4/2-cyc ne concerne que les changements mid-accès (style Amiga).
      ⇒ modèle fidèle = **commit immédiat au dispatch** : `NEOST_RAISE_COMMIT` défaut 1 (HBL ;
      le commit VBL casse le loader EL — couplage VBL↔IKBD/FDC, même signature que le pré-arm
      VBL, À CREUSER), `NEOST_PIN_ARM` défaut 0. Résultat : cadence d'exception = Hatari EXACT
      ({508,512,520} @ 2:2:1, motif par-5-lignes {x+8,x+4,x+4,x,x} identique, E-clock {2,4}
      identique) ; **flicker EL 40/40 — top-trick PARFAIT, une première** ; positions moteur
      376/384 = Hatari ; taux de lock ~47-66 % = dernier verrou (+ bancs poll à re-fermer).
    * ⚠ **PIÈGE : les bancs poll/poll2 ne sont PAS comparables mono-trame** (précession
      E-clock, 5 contenus distincts, frames AVI dupliquées ~2×) ; et leur ancien « 180/180 »
      était CO-CALIBRÉ avec l'entrée d'avant (broche au dispatch) — depuis broche-exacte/HBL 508
      ils ne matchent plus AUCUNE phase (les valeurs bougent par ligne, pas d'un offset
      constant). À re-fermer AVEC le front reconnaissance.
    * 🎯🎯 **5ᵉ PASSE (2026-07-02) — LE DERNIER VERROU EST TOMBÉ : lock moteur EL
      100 % (12402/12402).** La cause n'était PAS la reconnaissance IPL : c'était un
      **DOUBLE COMPTAGE du saut STOP** dans la comptabilité de quantum (`Cpu68k::run`,
      chemin STOP) — `setClock(jumpTo)` + `syncTo(jumpTo)` avançaient `sched.now_`,
      mais `ran` (et `cyclesRunInQuantum`) mesuraient toujours depuis l'ANCIEN début de
      quantum → le `runTo(now+ran)` de Machine recomptait le saut → `sched.now()`/
      `liveNow()` prenait une avance **δ = jumpTo − quantumStart ∈ {4..26}** sur
      l'horloge CPU, STABLE jusqu'au STOP suivant. Toute la datation vidéo (beamClock,
      compteur $8209, écritures freq/res) était donc décalée de δ vs les créneaux bus —
      impossible sur le vrai matériel (même horloge). Quand δ ≡ 2 (mod 4), le
      calibrateur beam-sync d'EL (double lecture $8209 à la ligne 65, pc 8db6/8dba,
      détection du gel du compteur à DE-end) percevait une géométrie décalée d'un
      demi-créneau → patch faux → trame déverrouillée. **Corrélation mesurée parfaite** :
      sweep à phase ≡0 mod 4 → 546/546 trames lock ; ≡2 → 69/69 unlock. Fix = REBASE de
      `quantumStartBus_/Clock_` après le saut. **Chaîne d'attribution** : lock 46,9 % →
      instrumentation `into=` (VC trace) → phase X flip à `into` constant → [BUS] diag :
      CPU épinglé m4=2 PARTOUT → c'est `liveNow`, pas le CPU → drift = δ vs frameStart
      théorique → le chemin STOP. **Les 3 « mystères » avaient la MÊME cause** (tous
      re-testés verts après fix) : (a) taux de lock 47-66 % → **100 %** ; (b) « le commit
      VBL casse le loader EL » → passe, `NEOST_RAISE_COMMIT` **défaut 3 = HBL+VBL**
      (modèle fidèle complet) ; (c) « IPLFETCH=1 casse le loader » → passe (reste opt-in,
      non requis). En bonus, port fidèle de la **broche MFP exacte** (`NEOST_MFP_EXACT`,
      défaut 3) : bit0 = anti-datation du tic Timer B event-count (≙ Delayed_Cycles de
      `MFP_TimerB_EventCount`, l'IRQ court depuis l'échéance TIMER_B servie, pas la
      frontière de bloc) ; bit1 = prise à la frontière courante quand le délai de 4 cyc
      est écoulé (≙ `MFP_ProcessIRQ`) — fidèle mais NON nécessaire au lock (A/B : 100 %
      sans). **Validé** : étalons 19/0 TOUS OK (défauts finaux), loader EL OK, LX titre
      churn 0,00 %, SHO titre/menus propres + période HBL régulière (pas de signature
      double-prise ~427). **Reste** : re-fermer les bancs poll contre un oracle Hatari
      FRAIS (les fenêtres des anciens AVI ne sont plus reconstructibles) ; re-mesurer le
      flicker bordure haute EL avec la fenêtre de mesure d'origine (l'étalon
      `overscan_top` byte-exact couvre déjà la mécanique).
      * 🎯🎯 **RE-MESURE FLICKER EL + DIFF `$8209` D'ENTRÉE À L'ORACLE = FAITS (2026-07-09).
        VERDICT FINAL : beam-sync EL CONVERGÉ, transitoire d'entrée INCLUS — aucun résidu NeoST.**
        (⚠ Deux verdicts intermédiaires FAUX en chemin, tous deux des artefacts de segmentation ;
        seul le diff `$8209` cycle-exact ci-dessous a tranché — le noter pour ne pas rejouer.)
        Repro NeoST `NEOST_VC_TRACE=1 NEOST_GLUE_STAT=1 … tos102fr … --keys-at 3500 " " --frames
        4200` (SANS `--fastfdc`). Oracle Hatari : `--cmd-fifo` + SPACE tenu (scancode 57,
        keydown/keyup) à l'intro (~70 s temps réel), `--trace video_addr,video_sync,video_border_v`.
        Scripts scratchpad : `el_oracle.sh`, `el_oracle_vc.sh`, `parse_el*.py`.
        - **(a) Datation re-arm write** (VERROU racine, `freq val=00 line=33 pc=0036ee`) : cyc
          **438/442/446 (mean 441.2, sd 3.0, spread 8)**, histo {438:99, 442:100, 446:50} ≈ 2:2:1
          (quantif E-clock). **BAT la cible Hatari** (~444 ±8) ; l'ancien tueur était 432-508
          sd≈18 spread 76. **Régime établi : retrait haut `start=34` sur 249/249 frames.**
        - **(b) DIFF `$8209` D'ENTRÉE = BYTE-IDENTIQUE.** Le stabilisateur EL spin-poll `$FF8209`
          à `pc=$ee78/$ee80` (~7965 lectures/frame) en attendant que le compteur commence à
          avancer (078000→078006 = détection DE-start). NeoST et Hatari détectent le DE-start au
          **MÊME point** : **ligne 63, X≈60-68, val 2-6, ~7965 lectures** des DEUX côtés (parseurs
          `parse_el_vc.py` sur les deux traces). ⇒ **la datation read `$8209` est fidèle en
          transitoire aussi** (rien à corriger).
        - **(c) LE TRANSITOIRE D'ENTRÉE EXISTE DES DEUX CÔTÉS, DURÉE COMPARABLE.** EL calibre son
          overscan à l'entrée : freq toggle à la ligne **63 (VDE_On nominale)** pendant ~17-26
          frames, PUIS bascule ligne **32** (retrait haut) et verrouille. NeoST : ~19 frames à
          la ligne 63 (`start=63`) puis lock. Hatari (séquence brute des 60 Hz-open, chrono) :
          `33 33 | 63 71 79…223 (sweep de calibration) | 63 64 63 64 ×~26 | 32 33 35 36…` — soit
          ~17-26 frames ligne 63/64 avant lock ligne 32 (run VC : 17 frames poll consécutives
          TOUTES DE-start ligne 63, pas encore verrouillé). **⇒ NeoST reproduit fidèlement le
          transitoire de convergence propre au stabilisateur d'EL ; ce n'est PAS une divergence.**
        - **(d) ⚠ ARTEFACTS RÉFUTÉS (ne pas rejouer).** (1) « transitoire = propriété démo, Hatari
          settle pareil » — juste par chance au 1ᵉʳ tour mais non prouvé. (2) « NeoST rate 17
          frames vs Hatari 1 » — **FAUX** : la segmentation par `detect remove top` + wrap `nHBL`
          des écritures sync FUSIONNE les frames transitoires de Hatari (leurs écritures restent
          à nHBL 63-64, jamais >150 → aucun wrap détecté → tout le transitoire compté comme 1
          « frame de 819 écritures »). La segmentation FIABLE = via les lectures poll `video_addr`
          (balayent nHBL 0→312/frame). Leçon : pour compter des frames en transitoire beam-sync,
          segmenter sur le POLL (couvre toute la trame), jamais sur les écritures (clusterisées).
      ⚠ Les diagnostics de cette passe restent branchés (gated) : `NEOST_FRAME_DIAG`
      (phase de l'ancre de trame), `NEOST_BUS_DIAG=<page-pc-hex>` (séquence bus + mod 4
      par accès), champ `into=` dans `NEOST_VC_TRACE`.
    * ✅ **Canal longueurs de ligne PAR-LIGNE porté** (`HBL_Pos`/`nCyclesPerLine`, commits
      `c680e1a`/`10c72b8`, gated `NEOST_LINELEN`). Le menu robot restaure 512 finaux
      (paires freq/res du menu) — **fidèle, ne corrige PAS le clignotement**. ⇒ les
      longueurs de ligne ne sont **plus** le suspect du menu Cuddly (mesuré 2026-07-02).
    * ✅✅ **MENU ROBOT CUDDLY RÉSOLU (2026-07-03) — clignotement 10-47 % → 0 (250/250
      trames verrouillées), commit `125388b`.** La cause n'était AUCUNE des pistes de la
      fenêtre verticale « 3 lignes » ci-dessous : c'était les **datations lecture/écriture
      du compteur vidéo**, co-calibrées autour d'une « origine −8 » devenue artefact après
      le fix STOP de la 5ᵉ passe. Mesure décisive (oracle `video_addr` + `cpu_disasm`,
      landmarks f02e/f200/f264) : le synchroniseur de la démo (pc=f264, paire 60/50 par
      ligne, sortie sur octet bas de $8209 > $40 **SIGNÉ**) a un chemin CPU et une ancre
      VBL IDENTIQUES à Hatari (f200 = VBL+16548 des deux côtés), mais lisait des valeurs
      4-6 octets plus petites → sortie L34 (marge Hatari +6..10) glissant à L36 → paire
      une ligne trop tard → retrait haut raté. **Fix : read −14→−6 et write −6→+2
      ENSEMBLE (+8 chacun) = les valeurs fidèles théoriques de la table §8** (cibles #1
      et #2 — la refonte coordonnée est ACHEVÉE : plus aucune rustine d'origine).
      Validé aux défauts : Cuddly 250/250 (mur régulier = Hatari), EL loader + top-trick
      40/40, LX titre propre, SHO byte-identique, étalons 19/19 + TOUS OK.
      ÉLIMINÉ en route (mesures) : lignes 508 pendant le poll (l'oracle montre
      cycles_line=512, le canal LINELEN est fidèle) ; VDE_On 34/35 ; restart compteur.
      Bonus gated `NEOST_LINELEN` : videoCounter() mappe désormais la lecture sur la
      grille réelle glueLineStart_ (port Video_ConvertPosition), défaut OFF.
      ⚠ Balayages d'offsets : read seul (+8) verrouille Cuddly mais CASSE EL (0/40) —
      la co-calibration read/write est réelle, ne bouger que PAR PAIRE.
    * 🚧 **(archive — diagnostic antérieur, cause réelle ci-dessus) menu robot Cuddly.**
      Diagnostic raffiné (2026-07-02 fin de session, traces `video_border_h` +
      `NEOST_GLUE_DIAG`, cf. TODO.md) :
      **Chaîne causale mesurée :**
      1. ~**47 %** des trames ratent le retrait bordure haute → fenêtre oscille
         **34..310** (haut+bas ouverts) ↔ **63..263** (nominale) = le clignotement.
      2. Trames ratées : 1ʳᵉ paire 60/50 émise à **L34** au lieu de **L33** → rampe
         démo une ligne trop tard.
      3. Poll son départ **`pc=f264`** (échantillonne L34–36) lit **`$8209`** : l'avancement
         du compteur dépend du retrait haut **de la trame courante** — trame verrouillée →
         compteur avance dès L34 ; trame ratée → gelé (**Δ=−160/ligne**, mesuré).
      4. **Boucle bistable** NeoST (oscille entre les deux états) vs Hatari qui converge
         vers l'attracteur « toujours verrouillé ». Écart initial = **±160 octets** (une
         ligne) dans la valeur `$8209` vue par le poll.
      **Éliminé (avec mesures) :** longueurs de ligne (canal ci-dessus) ; sortie fenêtre
      du retrait haut (paire reste 448–456 ≪ 502, démo compense la précession E-clock) ;
      masques Glue par ligne (strictement identiques trames stables vs sautées).
      **Objectif restant :** fenêtre verticale **3 lignes** byte-exactes — VDE_On (34 vs 35),
      comptage lignes bordure haute ouverte, ou `Video_RestartVideoCounter`.
      **Prochaine sonde :** oracle Hatari `--trace video_addr` sur le menu → comparer
      `$8209` à **L35**, état verrouillé vs NeoST ; l'écart ±160 dira le levier exact.
      **Plan d'implémentation résiduel** (port masques/DE Glue = FIDÈLE, ec40677 ; reste
      V_DE live + datation compteur par trame, cf. video.c 2246-2438 / 2849-2877) :
      **(a)** VDE_On live + comptage lignes bordure haute (sticky 34, jamais remonter) ;
      **(b)** `Video_RestartVideoCounter` / latch base compteur (cf. fix EL VC_RESTART) ;
      **(c)** datation beamClock par-ligne (fc/cpl, dernier étage). Gate : `NEOST_LINELEN`
      (défaut OFF ; EL lock 100 % + LX verts — re-valider avec le flag ON).
      Bancs : menu robot (clignotement 47 % → 0 ; mur pleine largeur), étalons 19/19,
      poll-entry. Repro headless : `--fastfdc --keys-at 3000 " "`, ≈ trame 6000.
      Outils : `NEOST_VARLINE_TRACE`, `NEOST_GLUE_DIAG`, `NEOST_BORDER_TRACE`, oracle AVI.
- 🎯 **PERCÉE (2026-06-17) — `NEOST_RAM_SLOT`+`NEOST_IACK` désormais DÉFAUT ON.** Les DEUX flags
  ENSEMBLE font FONCTIONNER le mécanisme d'overscan beam-sync : sans eux le handler HBL d'EL est
  ~88 cyc trop rapide → l'impulsion res se VERROUILLE (trick=0, zéro overscan) ; avec eux la dérive
  du faisceau = **+78/ligne = Hatari** → l'impulsion balaye, les tricks se déclenchent (§3, §7).
  CORRIGE la conclusion « RAM_SLOT/E-clock = pas d'impact jeu » (testés en isolation, jamais ensemble).
  `NEOST_RAM_SLOT=0`/`NEOST_IACK=0` désactivent (A/B). Coût : `overscan_top` re-baseliné (les 56 px
  diffèrent UNIQUEMENT en bordure haute overscan ; zone active byte-identique Hatari, ON et OFF).
  **Validation jeux (A/B) : Super Hang-On titre byte-identique ; Enchanted Land logo propre (diff =
  phase anim) ; Lethal Xcess titre PROPRE on vs CORROMPU off → les flags RÉPARENT l'overscan LX.
  Aucun double-comptage avec les hacks empiriques observé.** [[ramslot-iack-enable-overscan]]
- ✅ **Convergence INSTRUCTION = COMPLÈTE** : `NEOST_RAM_SLOT` (align créneau bus) + fix DIV (fork Moira) →
  datation cycle de NeoST = WinUAE sur tout le jeu courant, validé au différentiel (§1, §5).
- ✅ **Deadlock Enchanted Land = RÉSOLU** : dispatch BLOC par défaut (le sync-driven mid-instruction était
  net-négatif) ; intro/écrans statiques propres (§6).
- ◑ **Overscan VERTICAL (haut/bas) EN JEU = NON RÉSOLU** : la dérive MOYENNE correspond mais la PHASE
  ABSOLUE par-ligne diffère (impulsion NeoST culmine cyc ~476-492 vs Hatari 500-508 → pas de
  « straddle » res=00 sur la ligne suivante → le retrait haut ne TIENT pas). Fermeture = tracking
  cycle-exact du handler PAR LIGNE (alternance 76/80 de Hatari), pas un offset constant (§7).
- ⛔ **Clignotement bordure haute EL EN JEU — datation par-instruction RÉFUTÉE (2026-06-18, §7).** Mesuré :
  (a) `liveFrameClock` au `write8` est DÉJÀ live → datation faithful Hatari-CE = `fcRaw+2` CONSTANT (pas
  par-instruction) ; corrige le jeu (0/130) mais casse le loader (intro noire), write hard-borné **≥+14**.
  (b) Le re-arm vit dans un **stabilisateur nop-slide auto-modifiant** (lit `$8209`) qui NE verrouille PAS
  (jitter sd≈18 vs Hatari ±8) car le videoCounter read est daté **+10 vs Hatari** (source `Video_CalculateAddress`).
  (c) Hacks write(+16)/read(+4) **jointement calibrés** + résidu phase **+24** (re-arm 468 vs Hatari 444).
  ⇒ Fix = **refonte coordonnée** (write+2, read−6, alignement bus, phase), pas un levier unique. [[el-ingame-oracle-vcwait]]
- **Outils** : harnais différentiel de cycles (`NEOST_TRACE_CYC` + `tools/trace_diff.py --periods`),
  bancs `make_cycle_bench.py` / `make_respulse_test.py` (oracle Hatari `--trace video_res`), diag
  `NEOST_RENDER_ALL` ; comparaison rendu via profil par-ligne PIL (bbox/per-row).
  `tools/beamsync_diff.sh <tos> <disk|-> <vbls> [machine]` = diff cycle-exact de la **phase
  CPU↔faisceau** (cycle/ligne où chaque IRQ/exception est prise + cycle où le CPU échantillonne
  `$FF8205/07/09`) NeoST vs oracle Hatari. Datation MMIO : `Cpu68k::cyclesIntoInstr()` (cycles écoulés
  dans l'instr courante, ≈ position sous-cycle de l'accès) + `NEOST_WRITE_DIAG` (Shifter, trace fcRaw/into
  de chaque écriture freq/res).

---

## 1. L'outil n°1 — harnais différentiel de cycles (Moira ↔ WinUAE)

Le SEUL métrique fiable de convergence : comparer **les cycles par itération de boucle** entre les
deux cœurs sur du code identique. Construit cette session (Tracer.cpp opt-in + trace_diff.py).

```sh
python3 tools/make_poll_test.py /tmp/poll.st
# Moira (NeoST) — colonne cycle absolue (opt-in, défaut trace inchangé)
NEOST_TRACE_CYC=1 ./build/neost-headless roms/tos102uk.img --disk /tmp/poll.st \
    --machine st --mem 512k --frames 150 --trace /tmp/neost.txt 2>/dev/null >/dev/null
# WinUAE (Hatari) — colonne cycle absolue (CyclesGlobalClockCounter)
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home; mkdir -p /tmp/hatari_home
/opt/homebrew/bin/hatari --machine st --tos roms/tos102uk.img --monitor rgb --disk-a /tmp/poll.st \
    --sound off --fast-forward on --confirm-quit off --statusbar off --frameskips 0 \
    --alert-level fatal --run-vbls 150 --trace cpu_disasm,cpu_video_cycles \
    --trace-file /tmp/hatari.txt >/dev/null 2>&1
# Différentiel : périodes par PC-landmark (robuste au split de flot)
python3 tools/trace_diff.py /tmp/neost.txt /tmp/hatari.txt --periods 173C 1742
```
- `NEOST_TRACE_CYC=1` → préfixe `cyc=<clock absolu>` (= `busClockNow()`, analogue de Hatari
  `CyclesGlobalClockCounter`). Trace par défaut **byte-identique** sans l'env.
- `--periods PC…` → pour chaque PC, la **période dominante** (delta d'horloge entre visites) côté
  NeoST et côté Hatari + verdict `OK`/`DIFF`. Robuste au split de flot (aligne sur landmarks).
- Landmarks du poll-test : `173C`=`bra.s self`, `1742`=lecture `$8209` du handler HBL.

---

## 2. Carte des divergences (source-grounded, 3 agents — file:line des DEUX côtés)

| # | Mécanisme | Moira | WinUAE/Hatari | État |
|---|---|---|---|---|
| **A** | **Alignement créneau bus 4 cyc sur la RAM (CHIP16)** : avant un accès RAM, WinUAE attend `(4 - clock&3)` cyc (`wait_cpu_cycle_read`, custom.c:148-153). Moira NE l'a PAS. ROM/cart/**IO** = FAST (pas d'alignement, mesuré STF, memory.c:1798). | absent | custom.c:148-153 ; memory.c:1548/1798 | ✅ **PORTÉ** (`NEOST_RAM_SLOT`) |
| **B** | **Délai de reconnaissance IPL** : Moira reconnaît l'IPL immédiatement (POLL_IPL≡`reg.ipl=ipl`, MoiraMacros.h:64). WinUAE diffère d'1 instr si le pin a changé <4 (ou prend l'ancien si <2) cyc avant l'échantillon (`ipl_fetch_next`, newcpu.c:4982-4997). | MoiraMacros.h:64-67 ; Moira.cpp:901-941 (`setIPL`/`pollIpl`) | newcpu.c:4982-4997, 5672-5673 | ✅ **PORTÉ FIDÈLE `NEOST_IPLFETCH`** (cf. note ↓) |
| **C** | **E-clock + bloc occupé à l'IACK** : WinUAE applique l'attente E-clock (0..8, motif [0 8 6 4 2]) PUIS `CPU_IACK_CYCLES_VIDEO_CE(10)+idle(4)` au cycle d'IACK (`iack_cycle`, newcpu.c:2958-3019). Moira n'a ni l'un ni l'autre ; NeoST plaçait l'E-clock dans `willInterrupt` (≈14 cyc trop tôt). | MoiraExceptions_cpp.h:503/543-549 (`execInterrupt`) | newcpu.c:2958-3019 ; m68000.c:810 | ✅ **PORTÉ** : hooks `iackSyncBefore/After` au point d'IACK réel, `NEOST_IACK_AT` défaut ON (2026-07-02, cf. ÉTAT ci-dessus) |

**Fait structurel clé (persistance du fork).** Le superprojet **COPIE** `extern/moira/Moira/*` dans
`build/generated/moira/` et **réécrit `MoiraConfig.h`** (force `PRECISE_TIMING=true`,
**`MIMIC_MUSASHI=false`**, `EMULATE_ADDRESS_ERROR=true`) — cf. CMakeLists.txt:221-255. Donc (a)
`MIMIC_MUSASHI` est **false** à la compilation (la note mémoire « true » est trompeuse), (b) éditer
`extern/moira` directement est CLOBBERÉ au `submodule update` (historique : Moira est
vendorisé depuis le 2026-06-25, cf. `extern/moira/NEOST_VENDOR.md`). **Surface de fork = la sous-classe
`NeostMoira` dans `Cpu68k.cpp`** (overrides `read*/write*/sync/willInterrupt/readIrqUserVector`).
Si un edit Moira interne devient nécessaire → **vendoriser** (dé-submoduliser).

> 🔧 **`NEOST_IPLFETCH` — port FIDÈLE de `ipl_fetch_next` (2026-06-18, mécanisme B).** Réalisé
> DANS Moira (≠ l'ancien `NEOST_IPLDELAY` qui retardait la broche de l'extérieur, crude) :
> `setIPL` historise la broche (≙ `update_ipl` : `iplPrev`/`iplChangeClock`/`...Prev`), et
> `POLL_IPL`→`pollIpl()` applique la règle 3-cas (`Moira.cpp`) : pin stable ≥4 cyc → nouvelle
> valeur ; changée 2-4 cyc avant → **ancienne** valeur ; <2 cyc → différée. Seuils posés par
> `setIplDelay(4×mul, 2×mul)` (`Cpu68k.cpp` constructeur + `setMegaSteSpeed`). **DÉFAUT OFF**
> (`iplDelay4=0` → `pollIpl` ≡ `reg.ipl=ipl`, **étalons 19/0 byte-identiques vérifiés**).
> ⚠ **Edits dans le sous-module** (`Moira.h`/`Moira.cpp`/`MoiraMacros.h`) → à committer DANS
> le sous-module ou vendoriser, sinon clobbérés au `submodule update`.
>
> 🔎 **Résultat EL (diagnostique) : `NEOST_IPLFETCH=1` EMPIRE le clignotement (6→20/40).** Le
> délai IPL RETARDE la reconnaissance → handler/écriture EL encore plus TARD. Or EL est DÉJÀ
> +20 cyc tard vs Hatari (écriture brute ~464 vs ~444). EL a besoin de l'écriture plus TÔT, pas
> plus tard → **le délai IPL est fidèle mais hors-sujet pour EL** (il rendrait WinUAE plus tôt
> sans-délai ; NeoST sans-délai est déjà trop tard). **Preuve que le +20 d'EL = latence de
> BASE** (Moira exécute le handler HBL d'EL ~4,5 % plus lentement que WinUAE sur ~448 cyc), donc
> **convergence du timing INSTRUCTION cumulé**, pas la reconnaissance IPL ni le bloc IACK
> (réduire `IACK_VIDEO` casse l'overscan = load-bearing). C'est le cœur restant. [[el-ingame-oracle-vcwait]]
>
> 🎯 **ACQUIS (2026-06-18, diff cycle-exact + oracle CE).** Outil ajouté : `NEOST_HTRACE` (Cpu68k.cpp,
> gated, dump cycle-exact en jeu). DEUX points SOLIDES + 1 piste :
> **(a) Moira ne mal-date AUCUNE instruction** — `movem.l` 10-reg = **88** (store (An)) / **92** (store
> (d16,An) + load) = Hatari EXACTEMENT ; `dbra`/`bra` PRIS = **12** = Hatari (datasheet dit 10, mais WinUAE
> CE facture 12 : prefetch réaliste — NOTRE 12 était déjà correct, cf. [[beamsync-busalign-falsified]]).
> **La piste « instruction mal-datée » est RÉFUTÉE.**
> **(b) NeoST exécute le MÊME moteur fullscreen qu'Hatari** — prouvé par DUMP des octets en jeu : `$36ea` =
> `4238 820a` (`clr.b $820a`, 60Hz-open) et `$36f6` = `11fc 0002 820a` (`move.b #2,$820a`, 50Hz-close),
> IDENTIQUES à Hatari. ⚠ **Une étape intermédiaire concluait à tort « EL tourne du movem / code différent /
> `$f6f6` » — FAUX** : `$f6f6` était un typo oracle pour `$36f6`, et le `movem` vu par HTRACE à `$36dc`
> venait de l'**AUTO-MODIFICATION** du code (phase transitoire lue ; vrais octets = `nop`-ramp + `clr.b`).
> Les `movem` d'EL écrivent la PALETTE ($8240/$8840), jamais `$820a`. **Pas de divergence de code/contrôle.**
> **(c) RACINE CONFIRMÉE (capture Hatari pilotée directement) : NeoST utilise le MÊME mécanisme remove-top
> qu'Hatari, CORRECT quand il marche, mais INSTABLE (rate ~15% des trames = le clignotement).** Hatari
> retire la bordure haute d'EL à CHAQUE trame, STABLE : 1ʳᵉ ligne non-noire = 64/64/64 (3 screenshots),
> `detect remove top` = 76 (≈1×/trame). [⚠ un oracle « remove-top=0 » était FAUX.] Capture 832×588 → ligne
> 64 = ligne 32 en coords NeoST (×2) = les trames « ouvertes » de NeoST. Mécanisme du raté : EL pose 60Hz(L32,
> arme glueStartHBL_=34)→50Hz(L33, annule→63)→60Hz(L33 cyc~464, ré-arme→34). Le ré-arme doit tomber L33
> (frameCycle<512) ; datation GLUE NeoST = liveFrameClock+`kSyncWriteOffsetCyc(16)` ≈ **480** (vs Hatari
> ~444) → marge à 512 = 32, et le JITTER trame-à-trame franchit 512 → ré-arme glisse L34 → trick perdu.
> Hatari ~444 (marge 68) + jitter ±8 → jamais. **Tueur = jitter (×3) + moyenne tardive (480), PAS le
> mécanisme.** Levier = datation de la lecture `$8209` du beam-sync poll (videoCounter) ; balayé : VC_OFF
> best=+6→**2/40**, jamais 0 (chaotique). `+16` write-offset non réductible (loader EL casse). ⛔ **La piste
> « datation d'accès bus PAR-INSTRUCTION » est RÉFUTÉE (2026-06-18, §7) : `liveFrameClock` est DÉJÀ live →
> faithful Hatari-CE = `fcRaw+2` CONSTANT (pas par-instruction), corrige le jeu mais casse le loader (couplage
> glue↔lecture). Le vrai tueur = le JITTER de phase d'entrée d'IRQ (sd≈18 vs ±8), pas la datation.**

**En PRECISE_TIMING=true, les tables `CYCLES_*` de MoiraExec_cpp.h sont MORTES** (le timing vient
des `SYNC()` dans les accès/prefetch). Éditer ces tables ne fait RIEN. La convergence se fait par
DATATION (instants des accès), pas par comptage d'instruction.

---

## 3. Vérités MESURÉES cette session (corrigent la mémoire)

- ✅ **A (RAM_SLOT) est un vrai gain de convergence INSTRUCTION** : `bra.s self` en RAM passe de
  **10 (Moira) → 12 (WinUAE) = OK** au différentiel ; le **pas du beat** poll passe de 2 → **4**
  (= Hatari). **Zone active pixel-exacte vs oracle Hatari** (overscan_top crop=active = 0 px ON/OFF).
  ⚠️ L'ancien « chipWait8 FALSIFIÉ » (mémoire) ajoutait un **+4 parasite** (miroir erroné du 16 MHz) ;
  la version FIDÈLE est **align-only** (pas de +4 à 8 MHz, Moira facture déjà l'accès). Gated, sûr.
- ❌ **L'E-clock NE converge PAS en isolation.** Le « 56 % → 34 % » de [[eclock-convergence-validated]]
  est une **FRAME CHERRY-PICKED** (frame 390, phase 2 — PAS phase 8). Sur la **moyenne 5 frames**,
  l'E-clock fait **56 % → 70 %** (PIRE) : il ajoute du **jitter de phase trame-à-trame mal calé** sur
  Hatari. Le baseline 56 % est **stable** (offset constant), pas du jitter.
- ❌ **Aucun offset constant** (`NEOST_VC_OFF` -12..+12) ne descend la moyenne sous ~55 %. Le 56 %
  est **structurel** (motif de barres différent), pas un simple décalage → le **poll-screenshot est
  un métrique saturé/peu fiable**. Ne plus l'utiliser comme cible principale.
- 🔗 **Chicken-and-egg établi** : le jitter E-clock dépend de la phase d'horloge absolue, qui dépend
  de TOUT le timing d'instruction. ⇒ **converger le timing INSTRUCTION d'abord** (différentiel → 0),
  PUIS la phase d'IRQ tombe juste. L'E-clock ne se calibre PAS isolément.
- 🏁 **JALON — convergence INSTRUCTION complète** (banc `tools/make_cycle_bench.py` + workflow 6
  classes, cf. §5) : **NEOST_RAM_SLOT converge 14/14 boucles** (vs 2/14 sans) ; le Δ+2 sur quasi
  toutes (off) = le créneau bus manquant (WinUAE arrondit chaque période à un multiple de 4). Seul
  DIV résiduait (Δ+4) → corrigé (fork Moira). ⇒ **convergence cycle d'instruction = ATTEINTE**.
- ⚠️ **RAM_SLOT NÉCESSAIRE mais PAS SUFFISANT pour les JEUX, et le dé-deadlock vient d'AILLEURS.**
  EL deadlockait (noir dès frame 1200) — cause = le **modèle de DISPATCH sync-driven**, PAS le timing
  d'instruction → **RÉSOLU** en repassant au dispatch BLOC tout en gardant PT+RAM_SLOT (§6). Reste,
  après le dé-deadlock, la **corruption EL EN JEU (scroll)** = chantier vidéo **V3 multi-couches**
  (§7), distincte du timing CPU. (L'ancienne hypothèse « E-clock @ IACK / phase d'IRQ » pour les jeux
  est rétrogradée en RAFFINEMENT : elle n'a bougé ni le poll-screenshot ni les jeux — §7.)
- 🎯 **CORRECTION (2026-06-17) — RAM_SLOT+IACK ENSEMBLE ONT un impact JEU décisif.** La ligne
  ci-dessus (« E-clock @ IACK n'a pas bougé les jeux ») valait pour l'IACK SEUL. Mesure au banc
  `make_respulse_test.py` (oracle `video_res`) : c'est exactement le **chicken-and-egg résolu dans le
  bon sens** — RAM_SLOT (timing instruction) PUIS IACK (phase IRQ) APPLIQUÉS ENSEMBLE font tomber la
  dérive du faisceau sur Hatari (+78/ligne) et DÉCLENCHENT l'overscan beam-sync (trick 0→1). Séparément :
  RAM_SLOT seul = dérive +64 (insuffisant), IACK seul = verrouillé (inutile). ⇒ **les deux DÉFAUT ON**.
  Reste l'overscan VERTICAL (phase absolue par-ligne, alternance 76/80) — §7. [[ramslot-iack-enable-overscan]]

---

## 4. Implémenté (RAM_SLOT+IACK DÉFAUT ON depuis 2026-06-17 ; toggle via `=0`)

`src/core/Cpu68k.cpp` (sous-classe `NeostMoira`) :
- `NEOST_RAM_SLOT` (+`_PHASE`) → `chipWait8()` align-only sur RAM <$400000 dans read8/16/write8/16.
  **DÉFAUT ON** (`NEOST_RAM_SLOT=0` désactive).
- `NEOST_IACK` (+`_VIDEO`/`_MFP`/`_LEAD`) → E-clock @ IACK (via `willInterrupt`+lead-in 14) + bloc
  occupé (supplanté le 2026-07-02 par `NEOST_IACK_AT`, défaut ON : hooks au point d'IACK réel ;
  le chemin `willInterrupt` ne sert plus qu'avec `NEOST_IACK_AT=0`). **DÉFAUT ON** (`NEOST_IACK=0` désactive). (Le bloc constant est ABSORBÉ par l'ordonnanceur
  beam-anchoré pour le beam-sync en boucle d'attente ; il compte pour le code de jeu non-spinnant.)
- `NEOST_IPLDELAY` (préexistant) → retard pin 4 cyc (approx `ipl_fetch_next`). Reste défaut OFF.

`src/core/Tracer.cpp` : `NEOST_TRACE_CYC=1` → colonne `cyc=` (harnais). `tools/trace_diff.py` :
mode `--periods`.

**SÛRETÉ vérifiée (défaut ON)** : `run_etalons` 19/0 + TOUS OK (zone active byte-identique Hatari ;
`overscan_top` re-baseliné — 56 px en bordure haute overscan SEULEMENT, zone active 0 px). A/B intact :
`NEOST_RAM_SLOT=0 NEOST_IACK=0` reproduit l'ancien comportement (banc respulse : trick=1→0).

---

## 5. ✅ CONVERGENCE INSTRUCTION — COMPLÈTE (validée par workflow 6 classes)

Le différentiel a été piloté vers 0 sur **tout le jeu d'instructions courant** (workflow 6 agents,
banc `tools/make_cycle_bench.py` par classe). Verdict **avec NEOST_RAM_SLOT=1** :
- ✅ **5/6 classes 100 % OK** : modes d'adressage (toutes variantes src/dest), ALU & comparaison,
  branches & flot (Bcc/BRA/BSR/JMP/JSR/RTS/DBcc), décalages/rotations/bits, unaires & divers (y
  compris **écritures MMIO shifter `$8240/$8260/$820A`** — le `syncCpuBus` empirique de NeoST datait
  DÉJÀ comme WinUAE `M68000_SyncCpuBus`). MUL/MOVEM/move.l : OK.
- ✅ **DIV** (seule divergence résiduelle, Δ+4) : CORRIGÉE. Cause root-causée : Moira faisait
  `writeD; prefetch; SYNC(idle)` vs WinUAE `idle; store; prefetch` → le prefetch du DIV était aligné
  à la phase PRÉ-idle au lieu de POST-idle (DIFF ssi idle%4==2). **Fix** : reorder dans
  `execDivsMoira`/`execDivuMoira` (MoiraExec_cpp.h, fork Moira committé e4da365). Neutre sans
  RAM_SLOT (étalons inchangés), converge avec.
  ⚠ **Perdu à la vendorisation du 2026-06-25, rétabli le 2026-10-20** par le port du
  correctif amont équivalent `f90fc50`, désormais gardé par la sonde `cpu_div_ram`
  (cf. `extern/moira/NEOST_VENDOR.md`).

⇒ **La datation cycle d'instruction de NeoST = WinUAE, cycle pour cycle, sur tout le jeu courant.**
C'est le « full WinUAE timing convergence » au niveau INSTRUCTION. (Garder NEOST_RAM_SLOT opt-in tant
que la phase IRQ n'est pas faite : seul, il décale la phase de trame des hacks empiriques sans les
remplacer — cf. §6.)

## 6. ✅ FONDATION CORRIGÉE — dispatch BLOC (Enchanted Land DÉBLOQUÉ)

**Le DEADLOCK EL N'ÉTAIT PAS la convergence ni PT — c'était le MODÈLE DE DISPATCH sync-driven.**
A/B décisif (réponse au « reconsidérer la fondation ») : repasser au **dispatch BLOC** (CPU borné à
l'événement suivant + dispatch à la frontière via `runTo`, modèle pré-sync-driven) **tout en gardant
PT=true + RAM_SLOT** → **EL DÉ-DEADLOCKÉ** : l'INTRO (logo Thalion + pluie) rend **PROPREMENT** (vérifié
visuellement ; était 0 %/NOIR dès la trame ~1200 sous sync-driven). Le sync-driven (dispatch
mid-instruction `do_cycles` WinUAE) deadlockait la boucle beam-sync `$EE78` d'EL SANS corriger le jitter
(déjà falsifié) = **net-négatif, RÉFUTÉ**. La convergence d'instruction est **indépendante du dispatch**
(PT=true suffit). **FAIT (défaut, commit ff3ab25)** : bloc par défaut, sync-driven en opt-in
`NEOST_SYNC_DISPATCH`. Validé : étalons 19/0 + TOUS OK, différentiel 14/14 (RAM_SLOT), LX inchangé,
`NEOST_SYNC_DISPATCH=1` reproduit le deadlock (A/B intact).

> ⚠️ **NUANCE (vérifiée à l'image)** : le bloc DÉ-DEADLOCKE EL et rend l'INTRO propre, mais le
> niveau EN JEU (recette `--joy-at 3100 0x80` → frame 13000) **SCRAMBLE encore** (garbage plein écran).
> C'est le **résidu EL d'origine** (tricks fullscreen hi-res per-ligne, V2 res-switch / beam-sync), qui
> PRÉ-DATE le sync-driven (lequel l'avait juste enterré sous un deadlock pire). ⇒ le bloc RESTAURE l'état
> pré-sync-driven (intro propre, jeu scramble), il NE corrige PAS le scramble. C'est un VRAI gain de
> fondation (sync-driven était strictement pire) mais EL n'est PAS « réparé » en jeu.
>
> ⚠️ Le dé-deadlock = le DISPATCH BLOC, PAS RAM_SLOT (EL identique avec/sans). RAM_SLOT reste la
> convergence d'INSTRUCTION (fidélité WinUAE), sans impact jeu prouvé → garder opt-in (décale les
> réf-étalons SELF de 56 px en bordure, zone active intacte).

## 7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)

Avec la fondation bloc+PT, plus de deadlock ; EL boote/intro propre. Reste, par valeur décroissante :
1. **[HAUTE] EL corruption EN JEU (scroll) — ✅ LARGEMENT RÉSOLUE (2026-06-18, oracle EL réel).**
   > 🎯 **FIX = `NEOST_VC_WAIT` défaut 2→0** (`Shifter.cpp` lecture `$FF8205/07/09`). Diff datation à
   > l'oracle EL en jeu (poll fullscreen) : le wait-state +2 par-lecture **double-comptait** le +2 que
   > `NEOST_RAM_SLOT` (défaut-ON) fournit déjà → les boucles de poll dérivaient de **+4 cyc/itér** :
   > `$ee78` (sync-scroll) 24 vs Hatari **20** ; `$3700` (double lecture) 40 vs **36**. Avec VC_WAIT=0 :
   > **les deux = Hatari exactement**, étalons **byte-identiques** (spec512/overscan_top/scroll/glue 19-0),
   > et **EL en jeu passe de garbage scramblé à paysage rocheux NET**. L'ancien commentaire « +2 requis »
   > datait d'avant RAM_SLOT défaut-ON. **Reste un résidu** de corruption (bande droite). ⚠ Testé
   > (2026-06-18) : ce n'est **PAS** la datation des écritures `kSyncWriteOffsetCyc=+16` — la réduire
   > (`NEOST_SYNC_OFF<0`) **CASSE la progression d'EL** (reste à l'intro : l'offset alimente la glue live
   > que les reads `$FF8209` consultent → load-bearing, **pas** redondant comme VC_WAIT).
   >
   > **RÉSIDU = 2 symptômes (utilisateur), ROOT-CAUSÉ 2026-06-18 contre l'oracle Hatari EL en jeu — ce
   > N'EST PAS la géométrie par-ligne, c'est un JITTER de phase CPU↔faisceau du beam-sync d'EL :**
   > 1. **Ligne du haut qui clignote** : le trick top-border (toggle `$FF820A` 60/50 Hz, PAS la res) rate
   >    **~6/40 trames** (`glueStartHBL_` bascule 34↔63). MÉCANISME : EL pose 60 Hz(ligne 32)→50 Hz(L33,
   >    *annule*)→60 Hz(L33 cyc~464, *ré-arme*). Le ré-arme DOIT tomber sur L33 (cyc < 512). Quand la phase
   >    dérive tard, il glisse sur L34 cyc~8 → condition `line==Top_Pos-1` fausse → trick perdu.
   > 2. **Scroll qui saute** : la boucle de sync-scroll d'EL (`pc=$3700`, poll `$FF8209` tous les ~36 cyc,
   >    `addr`+18 o/poll) sort à des points variables → 32-52 % px changent/trame (joueur immobile).
   >
   > **ORACLE HATARI (cmd-fifo+SPACE, 601 trames de jeu) : top-border retiré 601/601 = 100 % STABLE.** Donc
   > le flicker NeoST est un BUG NeoST. Cibles mesurées (trace `video_sync`) : l'écriture 60 Hz tombe cyc
   > **~444 (440/444/448), jitter ±8** chez Hatari. NeoST : exécution brute cyc **~464 (+20), jitter ±26**
   > (436-488, queue à ~504) ; PUIS `kSyncWriteOffsetCyc=+16` → la GLUE voit **480** (vs 444 Hatari, +36).
   > Marge à la frontière (512) = 32 < jitter → franchit ~15 %. **Avec le jitter de Hatari (±8) même à 480 :
   > 488 < 512 → stable.** Donc c'est le JITTER (×3) le tueur, pas la moyenne.
   >
   > **CE N'EST PAS** : la géométrie par-ligne (banc : dérive fixe-vs-variable EL = -8..-24 cyc seulement,
   > négligeable ; `NEOST_V2` EMPIRE → 9 fails) ; ni un offset statique (balayés VC_OFF/LEAD/ECLOCK_PHASE/
   > SYNC_OFF/IACK + 2D : **MIN = 2 fails, jamais 0**, comportement chaotique = instabilité de feedback).
   > `IACK=0`→40/40 fails (le bloc E-clock@IACK est nécessaire mais insuffisant). `SYNC_OFF<0` casse le
   > LOADER (EL reste à l'intro, dominante bleue) → le `+16` est load-bearing → pas réductible globalement.
   >
   > ⛔ **DATATION PAR-INSTRUCTION = RÉFUTÉE PAR MESURE (2026-06-18, session datation).** L'hypothèse « le
   > `+16` fixe sur-date différemment selon l'instruction → fix = datation sous-cycle par-instruction » est
   > **FAUSSE**. Outils ajoutés : `Cpu68k::cyclesIntoInstr()` + `NEOST_WRITE_DIAG` (Shifter.cpp). Faits mesurés :
   > 1. **`liveFrameClock` au `write8` est DÉJÀ LIVE** : `cyclesIntoInstr` (cycles écoulés DANS l'instr au
   >    callback) = **into=2..16 selon l'instruction** (clr.b $820a into=14, move.b #imm into=10, move.b
   >    dn,(an) into=2), PAS 0. Donc fcRaw capture DÉJÀ la position sous-instruction `P`. Le calcul faithful
   >    Hatari-CE (`currcycle+4` = FIN d'accès) = **fcRaw + 2, CONSTANT pour TOUTES les instructions** (la
   >    position variable est déjà dans fcRaw). ⚠ `clr.b (xxx).w` = **16 cyc** (pas 12 comme supposé) → into=14
   >    → fcRaw+2 = instr_start+16 = fin = Hatari clr-family ; move.b #imm into=10 → fcRaw+2 = instr_start+12 =
   >    length−4 = Hatari move-family. **Le +2 reproduit Hatari pour les deux familles — il n'y a PAS d'offset
   >    par-instruction à porter.**
   > 2. **Le faithful (+2) corrige le JEU mais casse le LOADER.** Re-arm clr.b L33 : fcRaw 432-508 (mesuré,
   >    n=130) → +2 = max 510 **< 512** ⇒ **0/130 fail**. MAIS net+2 (= `SYNC_OFF=-14`) rend l'**intro NOIRE**
   >    (capture vérifiée). Le `+16` est load-bearing via le couplage **écriture→glue VDE→lecture videoCounter
   >    →sync loader** (PAS la datation). Borne MESURÉE : offset écriture **≥ +14** (intro noire à +12 ;
   >    +14 → intro OK + jeu **36/40** ; +16 → 34/40 ; +10/+12 → intro cassée).
   > 3. **Le re-arm vit dans un STABILISATEUR NOP-SLIDE AUTO-MODIFIANT, pas un handler HBL.** Dump runtime
   >    (2 dumps même trame 0x36c0/0x36e0 DIFFÈRENT de 2 octets = auto-modif confirmée) : `move.w #$60,d2` →
   >    boucle `move.b (a0),d0 / cmp.b #$40 / bgt / dbf d6` (poll `$8209` via a0, ≤8×) → `sub.w d0,d2 / lsl /
   >    écriture dans un nop-slide` = stabilisateur ST classique qui LIT le faisceau et corrige sa propre
   >    position. **Le jitter (sd≈18, spread 76, 432-508, série QUASI-ALÉATOIRE) = le stabilisateur NE
   >    VERROUILLE PAS** (Hatari ±8) — il est NOURRI par un videoCounter inexact.
   > 4. **DISCREPANCE READ source-groundée (+10) :** Hatari `Video_CalculateAddress` (video.c:1396) =
   >    `Video_GetCyclesSinceVbl_OnReadAccess() − 8` = `[since_vbl + (currcycle+4)] − 8`. En repère Moira
   >    (read8 tire à `access_start+2` après le `SYNC(2)` de tête) ⇒ Hatari date l'adresse à **`beamClock − 6`**.
   >    NeoST `videoCounter()` = **`beamClock + 4`** (kVideoCounterReadOffsetCyc) → **+10 cyc trop tard**. Mais
   >    le ramener vers le faithful (−6) EMPIRE le jeu (VC_OFF=−4 → 0/40) : le `+4` est jointement calibré avec
   >    le `+16` write.
   > 5. **Les hacks write/read sont JOINTEMENT CALIBRÉS, pas séparables.** Le write est hard-borné **≥+14
   >    INDÉPENDAMMENT du read** (testé write+2 × read{−6,−2,+2} → intro NOIRE dans les 3 → le loader casse par
   >    le write seul, le read ne le sauve pas). Read `VC_OFF` CHAOTIQUE (+2→38/40, +4→4/40, +6→0/40).
   >    `RAM_SLOT_PHASE` sans effet (mean→475) ; `IPLFETCH` sd→107 PIRE ; `IACK=0` casse EL. `RAM_SLOT`
   >    pèse **+37 sur la MOYENNE** (468 vs 431 sans) mais **+4 sur le jitter** (sd 18.5 vs 14.6).
   > ⇒ **FIX EL = COORDINATED REFACTOR** : converger ENSEMBLE write dating (+2), read dating (−6), alignement
   > bus RAM_SLOT (mesurer son écart vs WinUAE), ET le résidu de phase ~+24 (re-arm NeoST 468 vs Hatari 444).
   > Chaque pièce SEULE casse (hacks co-calibrés + phase résiduelle) → c'est le chicken-and-egg, pas un levier
   > unique. La datation d'écriture par-instruction (l'hypothèse de départ) est **réfutée**. (`+14` au lieu de
   > `+16` = +2 trames marginal mais risque l'étalon `overscan_top` ; non shippé.) [[el-ingame-oracle-vcwait]]
   > 🎯 **RECETTE IN-GAME FIABLE (2026-06-18, remplace l'ancienne périmée) :**
   > ```sh
   > ./build/neost-headless roms/tos102fr.img --disk "disks/st/Enchanted Land (1990)(Thalion).st" \
   >     --machine st --mem 512k --keys-at 3500 " " --frames 4200 --shot-every 1 /tmp/el_
   > ```
   > **CLÉ : PAS de `--fastfdc`** — il casse le loader EL (→ écran noir, ce qui faisait croire l'ancienne
   > recette `--joy-at 3100 0x80` « périmée »). **SPACE démarre EL** (clavier, = joystick 0 px diff) et
   > l'intro propre est à ~3000, le **JEU SCRAMBLÉ à ~4000** (terrain rocheux + crédits). Corruption
   > objectivée : **32-52 % des pixels changent trame-à-trame** (joueur immobile). Intro byte-propre = Hatari.

   Synthèse des passes (plusieurs hypothèses falsifiées en chemin) :

   **Ce qui est FIDÈLE (ne pas y toucher).** `updateGlueState` (aujourd'hui VideoGlue.cpp:442-719) est un PORT FIDÈLE
   de `Video_Update_Glue_State` (video.c:2244-2438) — constantes identiques, branches right-border
   freq=60 (video.c:2782-2800) et res hi-res fin-de-ligne (2683-2800) présentes ; glue-selftest 19/19.
   **AUCUNE branche Glue manquante** (hypothèse réfutée). **Les bordures G/D d'EL S'OUVRENT** (terrain
   rendu bord-à-bord) et les **écrans statiques (logo, crédits) sont PROPRES** ; la corruption est
   spécifique au **SCROLL ACTIF**. `endVideoLine` (aujourd'hui VideoCounter.cpp:52-134) avance déjà `vcLineBase_` du
   stride réel via `glueLineBytes`.

   **Banc de repro + validation (la pièce qui débloque, `tools/make_respulse_test.py`).** Handler HBL
   faisant `res=02`/`res=00` en fin de ligne par ligne (mécanisme fullscreen d'EL) ; **boote dans NeoST
   ET Hatari SANS input** → contourne le blocage oracle. RÉSULTAT : screenshot NeoST≠Hatari **40-49 %**.
   > ✅ **ORACLE EL EN JEU DÉBLOQUÉ (2026-06-18, contredit le « BLOQUÉ » précédent).** EL démarre à la
   > **touche SPACE** (pas seulement au feu joystick) → injectable dans Hatari via `--cmd-fifo` +
   > `hatari-event keypress 57`. ⚠ `--cmd-fifo` désactive le fast-forward → run TEMPS RÉEL (~72 s pour
   > atteindre l'intro vbl ~3600, puis SPACE) ; `--avirecord --avi-vcodec png` capture la scène. Hatari
   > rend alors le **niveau PROPRE** (paysage rocheux + crédits) = la référence du scramble NeoST. Le banc
   > synthétique `make_respulse_test.py` reste utile (rapide, sans input) mais est un **proxy imparfait**
   > (Hatari n'y retire les bordures que par intermittence). L'oracle EL réel est désormais la cible.

   **CAUSE RACINE RÉELLE (root-causée 2026-06-17, banc + oracle `video_res`) — c'est la PHASE
   CPU↔FAISCEAU, PAS la largeur d'affichage.** L'ancienne conclusion « divergence DOMINANTE = largeur
   d'affichage RIGHT_OFF / NeoST ouvre plus large » était FAUSSE (artefact de bbox : le profil par-ligne
   PIL montre une largeur ~normale des DEUX côtés). Mesure réelle au banc N=38 :
   - **Sans flags : l'impulsion res se VERROUILLE à cyc ~480 sur CHAQUE ligne** (handler ~88 cyc trop
     rapide) → ne balaye JAMAIS les fenêtres de retrait → `trick=0`, ZÉRO overscan (jamais, à tout N).
   - **`NEOST_RAM_SLOT`+`NEOST_IACK` ENSEMBLE** (accès RAM de la boucle dbra + bloc E-clock@IACK) →
     dérive **+78/ligne = Hatari** → l'impulsion balaye → tricks détectés, overscan rendu. ⇒ **DÉFAUT ON**.
     (RAM_SLOT seul = +64 ; IACK seul = verrouillé ; il faut les DEUX.) Cf. [[ramslot-iack-enable-overscan]].

   **Résidu NON résolu = overscan VERTICAL (haut/bas).** Banc : Hatari rend les lignes 2..273
   (haut+bas retirés), NeoST 29..228 (`start=63 end=263` inchangé). La dérive MOYENNE correspond (+78)
   mais la PHASE ABSOLUE par-ligne diffère : pour que le retrait HAUT TIENNE, la paire `res=02`/`res=00`
   doit STRADDLE une frontière de ligne (sinon le `res=00` 50 Hz RÉAJOUTE la bordure haute sur la même
   ligne précoce). Hatari culmine cyc **500-508** → `res=00` déborde sur la ligne suivante (straddle) ;
   NeoST culmine **476-492** et garde la paire sur la même ligne (ordonnancement HBL grille-fixe
   `N*cpl+508` → positions QUANTIFIÉES qui ratent [496,508]). La dérive de Hatari **ALTERNE 76/80**
   (écritures res alignées sur la grille bus 4 cyc) ; NeoST est steady +78 (écritures IO `$8260` = FAST).

   **PROCHAIN PAS** : tracking CYCLE-EXACT du handler PAR LIGNE (reproduire l'alternance 76/80 de Hatari →
   straddle atteint → retrait haut/bas TIENT). PAS un offset constant (`RAM_SLOT_PHASE`/`IACK_LEAD` swept :
   ne bougent que le bas 228→259, jamais le haut ; un offset constant casse la généralité = hack
   test-spécifique). C'est de la convergence boot/HBL fine. ⚠ NE PAS ajouter de branche Glue (fidèle :
   `updateGlueState` = port FIDÈLE, glue-selftest 19/19). Cf. [[enchanted-land-glue-live]],
   [[v2-resswitch-validated]], [[video-geometry-50-60-71]].
2. **LX jitter de titre** (~1.5 %, subtil ; LX rend déjà) ; **Cuddly menu robot** (clignotement
   bistable ±160 octets / 3 lignes — cf. chantier ci-dessus, oracle `$8209` L35) / **SHO course**
   (inatteignables headless → navigation requise).
3. **E-clock @ IACK** (poll-beat période-3 vs Hatari période-5) — RAFFINEMENT de phase ; n'a PAS
   amélioré le screenshot ni les jeux → faible priorité. Si repris : éditer `execInterrupt<C68000>`.
4. **Refonte beam-sync coordonnée** (retirer les hacks de datation co-calibrés + converger à l'oracle) :
   le plan ordonné, les cibles fidèles source-groundées et les impasses réfutées sont en **§8**. (RAM_SLOT est
   déjà défaut-ON depuis 2026-06-17.)

### (archive) Sous-problèmes d'entrée d'IRQ — désormais RAFFINEMENTS, plus des blocages

1. **E-clock @ IACK ne compose pas encore avec RAM_SLOT** : poll-beat reste période-3 `{0,4,8}` vs
   Hatari période-5 `{0,4,8,12,16}` (= E-clock mod-10 × créneau mod-4 = mod-20). En NeoST, RAM_SLOT
   ABSORBE l'E-clock (appliqué dans willInterrupt, trop tôt). Le fix faisable maintenant (fork Moira
   committable) = éditer `execInterrupt<C68000>` (MoiraExceptions_cpp.h:543-549 aujourd'hui, fait depuis via `iackSyncBefore/After`) pour insérer le wait
   E-clock + bloc occupé `CPU_IACK_CYCLES_VIDEO_CE(10)+idle(4)` AU point d'IACK (entre write PClo et
   read vecteur), comme Hatari `iack_cycle` → les accès suivants (SR/PChi/vecteur/prefetch) s'alignent
   à la phase POST-E-clock → mod-20 émerge. (Même leçon que le fix DIV : l'ORDRE idle↔prefetch fait la
   phase du créneau.) ⚠ Le setClock depuis readIrqUserVector est PERDU (mid-accès) → passer par un
   hook dédié dans execInterrupt, pas readIrqUserVector.
2. **Datation dispatch sync-driven (✅ RÉSOLU par le dispatch bloc, §6)** : le sync-driven faisait
   atteindre à EL sa boucle beam-sync `$EE78` ~50 lignes trop tard (spin infini → deadlock noir).
   C'était le modèle de dispatch mid-instruction, pas le timing CPU → réglé en repassant au dispatch
   bloc (§6). Gardé ici comme repère : le deadlock EL ≠ la corruption en jeu (V3, item 1).

Une fois le V3 fait : **retirer les hacks** (`NEOST_VC_WAIT=2`, `kSyncWriteOffsetCyc=+16`) devenus
redondants et **recalibrer à l'oracle**. ⇒ **Plan d'attaque ordonné en §8** (cette session a prouvé que
ces hacks sont co-calibrés autour d'un résidu de phase — la refonte doit être COORDONNÉE, pas pièce par pièce).

Cf. [[beamsync-busalign-falsified]], [[eclock-convergence-validated]] (CORRIGÉE ici),
[[sync-driven-scheduler-falsified]], [[v2-resswitch-validated]].

---

## 8. 🎯 PLAN D'ATTAQUE COORDONNÉ (refonte beam-sync) — pour la prochaine passe

**Thèse unifiante (établie 2026-06-18).** Le clignotement EL, l'overscan vertical EL, et le jitter de titre
LX sont le MÊME bug : la phase CPU↔faisceau de NeoST diffère de WinUAE d'un petit résidu (**~+24 cyc sur le
re-arm EL : 468 vs Hatari 444**). Les « hacks » de datation (`kSyncWriteOffsetCyc=+16`, read offset `+4`,
`NEOST_VC_WAIT`, le couple `RAM_SLOT`+`IACK`) sont des rustines **localement calibrées AUTOUR de ce résidu**.
Preuve qu'ils sont co-calibrés et inséparables : bouger UNE pièce vers sa valeur fidèle casse autre chose
(write+2 → loader noir ; read−6 → jeu pire ; RAM_SLOT=0 → overscan perdu). ⇒ **la convergence doit être
COORDONNÉE** : tout fidéliser ENSEMBLE, puis retirer les rustines, puis recalibrer à l'oracle.

**Cibles fidèles, source-groundées (À PORTER ENSEMBLE) :**
| # | Quantité | NeoST actuel | Fidèle Hatari (source) | Écart |
|---|---|---|---|---|
| 1 | **Datation read videoCounter** | `beamClock + 4` (Shifter `kVideoCounterReadOffsetCyc`) | `beamClock − 6` = `Video_CalculateAddress` (video.c:1396) = `[sinceVbl + currcycle+4] − 8`, repère Moira read8 = `access_start+2` | **+10** |
| 2 | **Datation write freq/res** | `+16` (Shifter `kSyncWriteOffsetCyc`) | `+2` = `fcRaw+2` (fcRaw LIVE capte déjà la pos sous-instr ; `Cycles_GetInternalCycleOnWriteAccess` CE = `currcycle+4`) | **+14** (⚠ borné ≥+14 par le loader tant que 1+3 pas faits) |
| 3 | **Alignement bus RAM_SLOT** | `chipWait8` appelé APRÈS le `SYNC(2)` de tête (align au MILIEU de l'accès) ; +37 sur la moyenne du re-arm | `wait_cpu_cycle_read/write` (custom.c) align AVANT les 4 cyc d'accès | à quantifier (suspect : sur-compte le résidu +24) |
| 4 | **Résidu de phase** | re-arm @ 468 | @ 444 | **+24** — à ATTRIBUER en premier (cf. étape 1) |

**Ordre d'attaque (chaque étape débloque la suivante) :**
1. **ATTRIBUER le +24 d'abord** (rien ne verrouille le stabilisateur tant que la moyenne est +24 trop tard).
   Diff cycle-exact NeoST↔Hatari sur le chemin jusqu'au re-arm. Oracle : soit l'**oracle EL in-game**
   (cmd-fifo + SPACE scancode 57, temps réel ~95 s, `--trace cpu_disasm,cpu_video_cycles,video_sync`), soit
   l'**oracle de phase contrôlé `tools/make_poll_test.py`** (boot sans input, handler HBL lit `$8209`→palette ;
   diffable au pixel, isole la datation read pure). Question : le +24 vient-il de RAM_SLOT (cible #3 : comparer
   le coût d'alignement PAR ACCÈS vs `wait_cpu_cycle_*` en mode CE), du bloc IACK, ou d'un cumul ?
2. **Porter la datation read fidèle (#1, `beamClock−6`)** — sensé SEULEMENT après #4 (le `+4` est calibré sur
   le résidu). Valider sur `make_poll_test` (motif de barres = Hatari) + spec512 (flicker) + EL loader.
3. **Porter la datation write fidèle (#2, `+2`)** + comprendre le couplage loader : le loader exige `+16`
   parce que le `write→glue VDE→read $8209→sync loader` est décalé ; une fois #1+#4 fidèles, le loader
   devrait tourner à `+2`. Test décisif : intro NON noire avec write+2.
4. **Retirer les rustines** (`NEOST_VC_WAIT`, `kSyncWriteOffsetCyc`, l'offset read) et **recalibrer à
   l'oracle** : garde-fou `run_etalons` byte-identique + EL (0/40, recette ci-dessus §7) + LX titre + Cuddly + SHO.

**Métrique de feedback (le tableau de bord de la refonte) :**
- Flicker EL : `NEOST_GLUE_STAT=1 … --keys-at 3500 " " --frames 4080 | grep -oE "start=[0-9]+ end=" | tail -40` → viser **40× start=34, 0× start=63** (baseline 34/6).
- Jitter du re-arm : `NEOST_WRITE_DIAG=1 … | grep "val=00" | grep "pc=0036ee" | grep "line=33"` → cyc actuel
  432-508 (sd≈18) ; cible Hatari ±8 (≈440-448). C'est le verrou : tant que sd>10, le stabilisateur rate ~10 %.

**⛔ Impasses RÉFUTÉES — NE PAS refaire** (toutes mesurées) : datation write PAR-INSTRUCTION (fidèle = `+2`
CONSTANT, pas par-instruction) ; tuning d'offset write OU read SEUL (chaotique/borné/casse loader-étalons) ;
`RAM_SLOT_PHASE` (sans effet sur le +37) ; `NEOST_IPLFETCH` (jitter sd→107, PIRE) ; `IACK=0` (casse EL) ;
géométrie par-ligne / `NEOST_V2` (pire) ; ajouter une branche Glue (`updateGlueState` = port fidèle, selftest
19/19). [[el-ingame-oracle-vcwait]] [[beamsync-busalign-falsified]] [[v2-resswitch-validated]]
