# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire — uniquement l'OUVERT.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (tout ce qui a été retiré d'ici) → [`CHANGELOG.md`](CHANGELOG.md)
- Écarts NeoST↔Hatari, `fichier:ligne` des deux côtés → [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md)
- Écarts Moira↔fx68k, cycle bus par cycle bus → [`docs/FX68K_BENCH.md`](docs/FX68K_BENCH.md)
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Logiciels étalons par sous-système → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md)
- Contre quoi chaque verdict est vrai → [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)
- Échange du cœur : porte et contrat → [`docs/CPU_SWAP_GATE.md`](docs/CPU_SWAP_GATE.md)

## Le cap

**Le meilleur émulateur d'Atari ST, STE et Mega STE — puis le TT et le Falcon.** Le prochain
jalon est l'échange du cœur 68000 par le Moira de POM68K. Ce fichier est rangé par rapport
à lui, en trois temps :

1. **Avant l'échange** (§ 1 à 8) — tout ce que le cœur actuel n'empêche pas de fermer :
   les puces autres que le CPU, la logithèque, le harnais, la livraison. Et d'abord **le
   filet** (§ 2) : plus NeoST est mesuré finement avant l'échange, plus l'échange se juge
   vite et sans débat.
2. **L'échange** (§ 9) — ce qui touche l'intérieur du cœur est **gelé** : un patch de plus
   dans `extern/moira` est un patch de plus à retrouver dans POM68K.
3. **Le TT et le Falcon** (§ 10) — rien ne s'intègre avant que l'auteur déclare le cœur
   prêt ; d'ici là, de la préparation seulement.

Quatre exigences ne changent pas d'un temps à l'autre : **le silicium pour référence**
(chaque affirmation porte son grade, [`CLAUDE.md`](CLAUDE.md) ; Hatari et MAME sont des
témoins, jamais la cible), **la logithèque qui tourne comme sur la machine**, **un harnais
dont chaque verdict dit son échelon**, **des paquets livrables sans surprise**.

**État au 2026-11-10** : contrat logiciel Mega STE tenu et gardé (suite Q du diagnostic Field
Service 12/12, grade C) ; les écarts que les fiches MFP, WD1772, ACIA et SCC suffisaient à
trancher sont corrigés ; **banc fx68k : 34 variantes sur 48 en accord** cycle à cycle, bus errors et double faute comprises (§ 9) ;
blitter, GLUE et lecture du faisceau confrontés aux RTL sur STE (§ 5) ; 27 parcours de jeu
contre Hatari, et la collection entière inchangée en A/B après les corrections du jour.
**Pas d'accès à un vrai ST** : les ⏸ matériel passent par les relevés de tiers, les
documents primaires, les ROM et le RTL épinglé.

### Règles de tenue de ce fichier

- Une entrée = **titre · priorité · statut**, puis 1 à 3 lignes et un lien. Le récit
  (mesures, pièges, preuves) va au `CHANGELOG.md` ou dans la doc du sous-système.
- **Ce fichier est la seule liste de l'ouvert.** Une doc de sous-système décrit un modèle,
  un banc ou un cas ; son « reste à faire » tient en un renvoi ici.
- **Priorité** : **P1** prochain chantier · **P2** utile, à planifier · **P3** à rendement
  faible ou « si un jour ». Elle se lit à l'aune du cap : gain de grade, titres touchés,
  verdicts montés d'un échelon, échange rendu plus sûr.
- **Statut** : ⭘ ouvert · ◐ entamé · ⏸ attend une ressource externe (matériel, média,
  schéma, décision, cœur). Dans chaque section, **ce qui est faisable vient d'abord, les ⏸
  ensuite**.
- **Une question sur le silicium vit au § 3, sous sa puce**, avec son grade actuel et ce
  qui la tranche ; ce qui relève de l'intérieur du 68000 vit au § 9. **aligné Hatari**
  marque une décision dont la seule justification était l'égalité avec Hatari : le code ne
  bouge pas avant une source de rang supérieur.
- Un item clos est **supprimé dans le commit qui l'ajoute au `CHANGELOG.md`**, jamais coché
  ni barré sur place.
- Les identifiants `A`*n* (dette d'architecture) sont stables ; un trou de numérotation
  est du travail fait. Les autres docs citent un item **par son titre**, pas par son
  numéro de section.
- *(proposé)* marque un item ajouté lors d'une réorganisation et pas encore validé par
  l'auteur : à garder, reformuler ou supprimer.

---

## 1. 🎯 Plan avant l'échange

Cinq lots, dans l'ordre. Un lot n'attend pas la fin du précédent, mais c'est l'ordre des
arbitrages : le filet passe avant la fidélité, parce qu'il sert aussi à juger chaque
correction de fidélité.

| Lot | But | Items (titre → §) | Prio |
|-----|-----|-------------------|------|
| **0. Jalons de version** | Trois versions sur le cœur actuel, auxquelles tout se compare ensuite | **0.6.3** publiée le 2026-11-10 (`355b6c0`) ; **0.6.4** publiée le 2026-11-11 (`bf5d61f`) ; **0.6.5** juste avant l'intégration du cœur POM68K → § 7 | P1 |
| **1. Filet** | Que l'échange se juge en une commande, sur plus que 57 étalons | Porte élargie, Mega STE 16 MHz, banc fx68k étendu et sort des `.state` (refus explicite) : faits → § 2 | P1 |
| **2. Silicium hors cœur** | Fermer ce que les sources déjà en main tranchent | restes MFP de la fiche et WS3 en attente de source, DMA disque relevé et reporté au § 9 → § 3 ; étalons `rtl` en cours de ligne, relevés de tiers, documents primaires → § 5 | P2 |
| **3. Logithèque** | Plus aucun plantage que le vrai matériel n'aurait pas | TOS 2.06 UK jugé (course réelle de la ROM), parcours étendus (son, STE, parties longues) → § 4 | P2 |
| **4. Livrable** | Ce qui sert l'utilisateur sans toucher à l'émulation | Pilotes EtherNEC, EWeb par le NAT → § 6-7 | P2 |
| **5. Terrain du TT/Falcon** | Préparer sans rien intégrer | Oracle et fiches TT/Falcon → § 10 ; A32 et inventaire faits (`docs/HYPOTHESES_ST.md`) | P2 |

**À ne pas entreprendre avant l'échange** : tout ce qui est au § 9 (prise d'IPL, IACK,
amont Moira, `g_cur`, points chauds du cœur). Le reste est P3, ou attend une ressource
externe (⏸).

---

## 2. 🥅 Le filet de l'échange

Ce qui doit exister **avant** le commit de l'échange pour qu'il se juge sur pièces.
Procédure et contrat → [`docs/CPU_SWAP_GATE.md`](docs/CPU_SWAP_GATE.md). La porte rejoue
**57 étalons, 29 parcours (28 de compatibilité et le diagnostic Magic Pockets), la collection en ST, STE et Mega STE, et 42 sondes Mega STE**
(8, 16 et 16 MHz + cache : blitter/IRQ, bus/cache, DMA disquette, blit sur ses propres registres).

- **Enregistrer la référence** · P1 ⏸ cœur — `cpu_swap_gate.py --record` sur le commit de la
  **0.6.5**, dernier avant l'échange, palier `full` vert. La référence ne se versionne pas.

---

## 3. 🔬 Fidélité au silicium, hors cœur — par puce

Sources citées à leurs épingles : `tools/setup_silicon_refs.sh`. Une fiche ou un schéma se lit
avant le RTL, et un `rtl` ne tranche qu'à titre provisoire (règles →
[`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)). Les écarts de sévérité basse déjà
consignés face à Hatari restent dans [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md) ;
ici ne figure que ce qui demande une décision ou une source. Le 68000 lui-même → § 9.

### 3.1 GLUE, MMU, Shifter — vidéo et bus

- **Plan mémoire : bus ouvert** · P3 ⏸ source · [F] — trois valeurs selon la zone (`$00` fenêtre
  ROM, dernier mot sur banque RAM vide, `$FF` cartouche et octets pairs du MFP) ; MAME rend `$FF`
  partout. L'attente MFP d'un accès du blitter n'est pas facturée au CPU. Tranche : une lecture
  sur un vrai STE. La carte des bus errors en lecture donne 0 écart contre les relevés de vraies
  machines sur les quatre modèles.

- **Restes vidéo au cycle** · P3 ⭘ — la VBL est datée à la fin de trame + 64/68 (`Machine.cpp`), la
  fin de DE a son événement Timer B distinct du HBL, et la lecture du compteur concorde au RTL sur STE
  (47 999 sur 48 000, `docs/STCPU_BENCH.md`). Restent ST et Mega ST, sans netlist, et la tolérance du quartz
  MFP (→ « Tolérance du quartz MFP »). → `docs/CYCLE_ACCURACY.md` § 4
- **Bord de fin STOP_MIDDLE et signaux HSYNC/VDE** · P3 ⭘ · [F] — la fin de DE à 164 et les 54 octets concordent avec le modèle au pixel (`run_stcpu_raster`, cas `hi_96_460`), contre les trois pixels de plus d'Hatari. Le HSYNC parasite vers le cycle 28 vient d'un comparateur du compteur horizontal (`gstmcu.v:803-819`) et arme aussi la requête HBL dans le modèle (`gstmcu.v:320`) : un programme pourrait le voir. Rien n'est porté. → `docs/GLUE_SWEEP.md`
- **STE, impulsions restantes face au modèle** · P3 ⭘ · [D] — DE précoce à 44, 14 impulsions sur 2560 en écart à 50 Hz, 42 sur 2560 à 60 Hz, toutes en haute résolution, et les
  largeurs au-delà de 32 qui enjambent HSYNC. Hors logiciel connu. Cause lue dans le modèle : la décision de DE
  passe par 4 étages en basse résolution, 1 en haute (`gstmcu.v:860-893`) ; non portée. → `docs/GLUE_SWEEP.md`
- **`$FF8207` en plein DE sur STE** · P3 ⭘ · [D] — le modèle lit le reste de la ligne à la nouvelle adresse ;
  NeoST et Hatari attendent la ligne suivante. `$FF8265` est fait (2026-11-12). Il faut changer de source au
  milieu de la capture d'une ligne. Aucun logiciel connu n'en dépend.
- **Hardscroll LO/MED/LO : persistance d'une trame à l'autre** · P3 ⏸ source · [F] — le modèle garde la phase du
  Shifter au-delà de la VBL, NeoST la remet à 0 comme Hatari. Tranché par : beescrn4.prg, ou une capture sur STF.
- **NOSCROLL et décalage du Shifter STE** · P3 ⏸ source · [F] — depuis le 2026-11-12 le latch
  suit tout accès LDS à `$FF8264-5` ([B] ST-4081 f. 3 et 10). Restent : une LECTURE de `$FF8265`
  active-t-elle SCRLSEL (porte sans R/W) ? le GST Shifter efface-t-il son décalage au RESET ?
  Tranche : une mesure sur un STE, ou le schéma du GST Shifter.
- **Offsets de datation dérivés d'Hatari** · P2 ⏸ source — la lecture du compteur est confirmée sur STE (47 999 lectures sur 48 000, `docs/STCPU_BENCH.md`). `kVideoCounterReadOffsetCyc`,
  `kSyncWriteOffsetCyc`, `kSpec512AlignCyc` : fidèles à `Video_CalculateAddress`, donc [D].
  `$FF820A` : le modèle réagit à AS + 5, NeoST et Hatari datent à AS + 8. Avancer casse de vraies démos STE, donc non retenu,
  et la palette, contredite de même par 13 images réelles, va dans ce sens : le modèle prend les écritures trop tôt (`docs/REFERENCE_LADDER.md`). ⚠ read et write bougent par paire. Tranche : la date d'une écriture `$FF820A`/`$FF8240` mesurée sur le fer, ou une netlist du ST pour la lecture.
- **Basse résolution PAL issue du reset** · P2 ⏸ source · [F] — le modèle GSTMCU garde une phase de
  plans décalée jusqu'à un passage par 60 Hz. Artefact du modèle ou effet réel ? Relevé de tiers.
- **Wakestate WS3 par défaut** · P2 ⏸ source · aligné Hatari — le modèle des quatre états est confirmé par le détecteur de Troed (`tools/run_wsdetect.py`, 162, 54, 206, 204 octets, et 180 sur STE). Le choix de WS3 comme défaut a une première source, faible : Troed, sur un STFM, « Cold: WS3 — possible to get into WS2 quite easily with a short power off/on. WS4 very seldom. WS1 almost never. Warm: WS1 » (atari-forum p=254222, 24 avril 2014, relu par web.archive.org) — une machine, de mémoire, sans comptage. Tranche : une statistique de relevés de tiers, ou un tirage au reset en option. Un tirage au reset demande d'abord de sortir le wakestate des constantes figées avant `main()` (`kWsInc`, `VideoGlue.hpp`).
- **Délai de bus error sur Mega STE** · P3 ⏸ source · [F] — ST et Mega ST comptent les 64 cycles depuis
  le 2026-11-11 ([B] Atari, « MegaST Bus », 1991, DOIT_ST § 3.11 : « The ST will automatically generate
  this signal if a bus cycle does not complete within 64 clock cycles ») ; étalons pixel inchangés. Le Mega
  STE reste sans source.
- **Couleur pendant BLANK** · P3 ⏸ source · [F] palette 0 — `gstshifter.v:263-265 @ 4a6db7cc` force le
  noir hors mono, et le banc le confirme (étalons `rtl_low_*` : seul écart, 2 lignes en PAL,
  13 en NTSC). Hatari l'admet (`video.c:4008-4009 @ f0736b24`). Décision de l'auteur
  (2026-11-11) : palette 0 tant qu'aucune mesure ni document ne tranche ; passer au noir
  déplacerait les références `hatari` à bordure visible. Tranche : une capture sur machine.
  ⚠ Le schéma 4118 est le shifter de la Panther ; aucun schéma du Shifter ST n'est disponible.
- **Vidéo — mesures sur Atari** · P2 ⏸ matériel · [F] — comportement électrique de
  NO_SYNC/SYNC_HIGH et confirmation au fer de tout ce § 3.1.
- **Bits 3, 7 et 11 de la palette relue sur ST** · P3 ⏸ matériel · [F] — NeoST les rend à 0. Hatari les tire au hasard (`video.c:5507 @ f0736b24`, « not defined », selon la dernière activité du bus), sauf si le PC est hors RAM. Pirates [cr Delight] relit ainsi sa palette, sans effet à l'image. Tranche : relire ces bits sur le fer, ou modéliser le dernier mot du bus.
- **Octet `$FF8261` relu** · P3 ⏸ source · [F] — `gstshifter.v:121 @ 4a6db7cc` rend $00 en octet bas ; NeoST garde l'alias de
  `$FF8260`, comme Hatari. Bloc « moyen » : le RTL ne l'emporte pas. Tranche : relevé sur STE.
- **Shifter arrêté (res 3 / `$FF8261`)** · P3 ⏸ matériel · [F] — porté sans logiciel réel qui
  l'exerce : noir franc ou couleur 0, comportement sur STE, persistance du décalage.
- **Décodage Mega STE** · P3 ⏸ schéma · [D] — le STE suit la netlist 4081 ; le Mega STE reste au
  modèle Hatari faute de netlist.

### 3.2 MFP 68901

- **Fiche contre code, restes de l'audit** · P2 ⏸ source · [B] contre [D] — marqués `⚠` dans `src/io/Mfp.cpp` et `Mfp.hpp`. TxDR : la fiche fixe la règle (« captured on the last low-to-high transition of the DS pin », MC68901 § 5.2.1 p. 5-5, relu le 2026-11-11) mais pas la date de ce front face à l'horloge où NeoST lit (`liveNow`), qui dépend du placement des états d'attente du MFP dans le cycle : 2 à 6 cycles, de sens non établi, et `mfp_poll` concorde aujourd'hui à l'octet avec Hatari. Prescaler changé en marche : MC68901 (1 à 200 cycles) contre MK68901 (jusqu'à 200 fois la donnée). Tranche : la datation des états S d'un accès MFP sur le fer, ou un relevé de tiers. UE corrigé (`0ce2471`).
- **Jitter des périodes Timer B** · P2 ⏸ cœur · [F] — sonde Super Hang-On : NeoST {512, 516, 508,
  520}, Hatari {512, 520, 504}. Dépend de la prise d'IPL (§ 9) : à rejouer après l'échange.
- **Tolérance du quartz MFP** · P3 ⭘ · [F] — la phase au reset est posée en option (`--mfp-phase-seed`, pas
  d'1/64 de cycle MFP) et ne change rien à la course de TOS 2.06 UK (3/256 sous 7 phases) : elle décale un
  timer d'au plus 3,26 cycles CPU. Ce qui fige le réseau de trajectoires, c'est le rapport exact CPU/MFP ; un
  autre quartz (`--quartz 32042|32216`) donne 0/256. Sur le fer, les deux quartz s'écartent de quelques
  dizaines de ppm (HWTST : STE PAL 8 020 7xx-8 021 2xx Hz), soit ~50 cycles par ppm sur ce trajet. À poser :
  une graine de tolérance du quartz MFP (±ppm), en option (`docs/CASE_STUDIES.md`). Tranché par : la
  dispersion des quartz MFP relevée sur plusieurs machines.
- **USART** · P3 ⭘ — modèle synchrone (SCR, M, F/S), réacquisition et CIP au milieu d'un caractère (récepteur
  bit à bit, § 6.1.1), phase du premier front TC. BREAK reçu en bouclage fait le 2026-11-12 ; [F] sa date de
  détection et le canal de l'IRQ de fin de BREAK : tranché par SO et IPRA relevés sur un 68901.

### 3.3 Blitter

- **Blit HOG long : Windows, Android et Safari non vérifiés** · P3 ⭘ — coroutine compilée seulement en croisé
  pour Windows (fibres) et Android (fil dédié). Web : vérifié sous Chrome 157 et Firefox 160
  (`tools/check_web_jspi.mjs --browser`, WebDriver BiDi) ; cadence de Firefox sans écran non établie (sous
  60 tours rAF/s avant le blit, contrôles sautés). Safari non pilotable sans « Allow remote automation ».
- **DMA externe prioritaire sur le blitter en HOG** · P3 ⭘ · [F] — netlist 4082, DMARQ,
  confiance basse.
- **Résidu blitter/IRQ Mega STE 16 MHz** · P2 ⏸ IACK — comparaison temporelle exacte ; deux
  défauts Hatari isolés, 12-24 cycles résiduels après corrections expérimentales. Attend la
  latence IACK (§ 9). → `docs/HATARI_MEGASTE_AUDIT.md`

### 3.4 Son — YM2149, DMA STE, Microwire

- **YM du STE sur SCLK ?** · P3 ⏸ source · [D] — Hatari `clocks_timings.c:222-223,243 @ f0736b24` tire CLK2
  du YM de l'oscillateur U303 (8 010 613 Hz) sur STE/Mega STE : 250 331 pas/s contre 250 663 chez NeoST,
  −2,3 cents. Tranché par : le schéma STE C300780 (feuille de l'U303) ou une fréquence de sortie mesurée.
- **Microwire : écriture pendant un décalage** · P3 ⏸ source · [F] — la démo Systematic Error
  (validée sur le fer) écrit le masque en plein décalage et exclut seulement le masque pris sans
  arrêt ni relance (`docs/CASE_STUDIES.md`). Ignorer (Hatari, NeoST) et arrêter (MiSTery) restent
  possibles : ils ne diffèrent que par les aigus +12 dB de la commande interrompue. L'écriture de
  la donnée en cours de décalage n'est exercée par aucun logiciel connu. Tranche : la musique de
  la démo enregistrée sur un vrai STE, ou la netlist du Shifter STE.

- **Bits 2-3 de `$FF8921` et relecture de `$FF8920` sur STE** · P3 ⏸ matériel · [B] — `$FF8920`
  relu 0 : [B] STE Developer Information Addendum (25 mai 1989), « FF8920 0000 0000 m000 00rr »,
  sans légende (« 0 » = relu 0 ou écrire 0 ?). Masque de `$FF8921` : 0x83 (addendum, MAME,
  MiSTery) contre 0x8F (Hatari, FAQ STE de Paranoid, sans mesure décrite). Tranche : une lecture
  sur un STE.
- **Profondeur réelle de la FIFO du son DMA** · P3 ⏸ source · [F] — 8 octets (Hatari) contre 7 mots (modèle
  `gstshifter.v:414-419`) ; elle fixe la date du fetch du dernier mot, donc de XSINT et du Timer A (datés au
  fetch hors DE depuis le 2026-11-12). Tranche : netlist du GST Shifter ou relevé sur STE.
- **Tenue du dernier échantillon DMA** · P3 ⏸ décision · [B]/[F] — schéma STE C300780-001 f. 5 : registre
  74F374 chargé par XLD avant le DAC0802, donc le DAC tient son octet ; inconnu : XLD cesse-t-il FIFO vide
  (Shifter sans netlist). Hatari et MiSTery tiennent. Proposition : tenir sur STE ; mute du YM au repos
  (mixing ≠ 1) en attente de la fiche LMC1992 ou de l'addendum STE (code 0 = −12 dB ?). Sources dans
  `src/core/DmaSound.hpp`. À trancher avant toute comparaison de WAV bit à bit.
- **Préchargement DMA en écriture** · P3 ⏸ source · [F] — `dma.v:277 @ a00deb5b`, modèle comportemental :
  simple témoin. ST-4081 n'a que le compteur d'adresse (f. 16) ; la FIFO est dans la puce DMA, absente de
  l'archive ASIC. Tranche : netlist du DMA.
- **WRITE TRACK / READ TRACK sur STX** · P3 ⭘ · [D] D1/D2 — la piste réécrite est
  réinterprétée en secteurs (CRC régénéré, statut neutralisé), READ TRACK rend cette
  reconstruction : seule divergence de sévérité moyenne assumée, sans promesse de flux brut.
  Tranche : relecture sur un vrai lecteur d'une piste écrite par un jeu (sauvegarde), ou IPF.
- **Densité `$FF860E` après XRESET** · P3 ⏸ source · [F] — registre en écriture seule sur Mega
  STE ([B] TOS `bios/sparrow/flop.s`, Pratt 1990), latch dans la PAL22V10 U407 qui reçoit XRESET
  (mise sous tension et bouton, schéma Mega STE Rev 2 f. 2 et 4/11) ; équations non publiées.
  NeoST : DD au reset à froid, gardée à chaud, relue `$FF`. Inobservable par le logiciel, réécrite
  par le TOS avant tout accès. Tranche : un dump JEDEC de U407/UA02.
- **ACSI : essai Idris OS** · P3 ⏸ média — délai de fin de DMA repris d'Hatari pour lui ; jamais tenté. Aucune
  image libre : le dépôt de préservation 9nut/IDRIS-OS-for-Atari-ST n'a pas de licence (vérifié le 2026-11-12).
- **Compteur DMA en écriture** · P3 ⏸ source · [F] — décrémenté au 33ᵉ rechargement de 16 octets
  (Hatari, NeoST) contre le 32ᵉ (MiSTery `dma.v`) ; ni l'archive des ASIC Atari ni le manuel TT ne
  décrivent la puce C025913. L'ID sans données relancé jusqu'au 5ᵉ index n'a pas encore été
  observé sur un titre (seul cas : Lethal Xcess disque 1, piste 79 face 1, jamais lu par la
  recette). Tranche : la fiche ou le schéma du C025913.
- **WD1772, restes de l'audit** · P3 ⭘ · [B] contre [D] — marqués `⚠ [B]` dans `src/io/Fdc.cpp` : INTRQ de $D8 effaçable par toute commande, gardé car la fiche à la lettre fait diverger Super Hang-On ; $Dx annulé si une commande suit à moins de 16 µs. Deux écarts voulus protègent de vrais programmes : TR/SR écrits pendant BUSY (Delirious Demo IV) et commande remplacée pendant BUSY (Overdrive Demos).
- **Face absente d’un fichier ST** · P3 ⭘ · [F] — aucun contenu de la face omise : vierge ou formatée à l’origine ? Les cinq index avant RNF sont vérifiés sur une piste réellement sans ID (WRITE TRACK de $4E). La synthèse d’ID sur une face absente reste une convention héritée d’Hatari, à trancher par un dump ou une convention de format. → `docs/HATARI_DIVERGENCES.md`

### 3.6 Clavier — IKBD HD6301, ACIA 6850, MIDI

- **Liaison série physique** · P3 ⭘ — formats du 6850 émis bit à bit au /64, erreur de trame et BREAK vus par
  la ROM depuis le 2026-11-12. Restent : diviseurs /16 et /1 côté CPU→IKBD (échantillonnage du SCI, 8 × 7812,5
  bauds, HD6301 p. 1247) ; PE/FE de l'ACIA clavier pour les formats non 8N1 ; phase SCI sortante. [F] erreur
  de trame pendant la fenêtre de reset.
- **Cadence du SCI de l'IKBD** · P3 ⏸ source · [D] — 1024 cycles par bit, comme Hatari, qui
  suppose un CPU à 8 MHz. La fiche du HD6301 (table 2-6-1 p. 153) donne 128 µs par bit avec un
  quartz de 4 MHz, soit 1026,7 cycles du CPU PAL : 27 cycles de plus par octet. Tranche : le
  quartz sur le schéma de la carte ST, ou le document Atari du protocole IKBD ; puis A/B de la
  porte et des parcours.
- **Firmware IKBD, restes face à la ROM** · P3 ⭘ — `$19` suit la ROM depuis le 2026-11-12. Restent : code 6301
  custom (`Execute`) sans cœur HD6301 ; délais de `$18` comptés sur la ROM, pas mesurés sur un IKBD ; [F] latence
  du tour de boucle de `$19` et valeur de `$9F` à l'entrée.
- **ACIA MIDI** · P3 ⭘ — source RX hôte au câble 8N1 : le 7 bits + parité (strip D7, PE) est tranché par la fiche
  ([B] MC6850 p. 6 et 9) mais pas modélisé ; le format TX est figé au chargement du TSR (comme Hatari) alors
  que la fiche le rend effectif immédiatement ; BREAK non simulé.
- **ACIA : octets impairs sur ST et Mega STE** · P3 ⏸ source · [F] — vides comme Hatari ; le STE
  suit le GST MCU depuis le 2026-11-11 (cycle de la puce, écriture sur le registre pair, attente
  E-clock sur `$FFFC00-$FFFDFF`). Tranche : la netlist du GLUE du ST, ou de la MCU du Mega STE.

### 3.7 Mega STE — SCC Z85C30, RTC RP5C15, cache

- **SCC, restes** · P3 ⭘ — caractère nul de fin de BREAK fait le 2026-11-12. Restent : délai de la détection BREAK
  et garde WR3 Rx Enable (le test « I SCC » du diagnostic s'en sert) ; parité RX (PE du caractère nul en parité
  impaire) ; autres fonctions de WR7', FIFO SDLC, DPLL, série hôte, X21, Hunt bit à bit, formats discordants.
- **RTC, restes** · P3 ⏸ source — TEST (« LSI inspection », fiche EK-086-9908), relecture de Eh/Fh non définie
  par la fiche, CLKOUT/ALARM sans câblage sourcé. Tranché par : le schéma Mega ST/STE, ou une relecture de
  `$FFFC3D/3F` sur le fer.
- **Matrice DD/HD × cache** · P3 ⭘ — balayage systématique si un titre l'exige.

### 3.8 TOS, GEMDOS, cartouche

- **GEMDOS : conventions du montage** · P3 ⏸ décision — 79 écarts au banc
  (`tools/run_gemdos_ab.py`, TOS 1.04, 2.06, EmuTOS), tous de convention : ordre de listage (FAT
  contre hôte trié), lecteur de boot, carte des lecteurs, Dfree, étiquette de volume, octets
  après le NUL d'un nom. Les E/S sur un fichier détruit encore ouvert rendent EINTRN [F].
- **Tailles de RAM sans source** · P3 ⏸ source · [E] — 256k et 2176k sur STE/Mega STE, Mega STE
  sous 1 Mo : acceptées sans document (barrettes 30 broches, banque minimale probable de 512 Ko).

---

## 4. 🕹️ Logithèque — ce qui doit tourner comme sur la machine

TOS 1.02fr sauf mention ; chemins sous `disks/st/` ou `disks/stx/`. Avant de déclarer un bug :
**vérifier la RAM, puis la ROM** ; les cas tranchés (Captain Blood, Enchanted Land, Cuddly, Rick
Dangerous II, Stardust, Spectrum 512 STE, Blood Money, Arkanoid, Wings of Death…) sont dans
[`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md) — ne pas les rouvrir.

### Bugs ouverts

| Titre | Prio | Symptôme | Piste |
|-------|------|----------|-------|
| **Lemmings** `[cr BBC][a]` | P3 ◐ | Menu, premier niveau terminé avec un Digger, bilan et présentation du niveau 2 gardés (`lemmings_first_level`, `lemmings_level1_complete`, 0,0 %). | Niveaux suivants (Floater au niveau 2). |
| **Vroom** `[cr Elite]`, intro sous TOS 2.06 | P2 ⏸ matériel | Trame rayée transitoire, 20/1 000 trames, reproduite aussi par Hatari ; la sonde GSTMCU retrouve 230/184 octets. | Mesurer latence VBL et fréquence de la séquence sur STE réel avant tout recalage CPU/GLUE (`tools/probe_vroom_intro_glue.py`). |
| **Switchblade** `[cr Replicants][t +2]` | P3 ⏸ relevé | Graine 8 débloquée par la cadence FDC/DMA ; seule la datation émulée change. | Relever la probabilité de collision sur un vrai ST. |
| **Shadow Warriors** (2Hot2Handle) | P3 ⏸ média | Version Ocean jouée sans écart (`shadow_warrior_st`) ; le disque 2Hot2Handle manque. | S'il revient, essayer la touche 1. |

Récit, traces et recettes de chacun : [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md).

### Campagnes à mener

- **Logithèque jouée** · P2 ◐ — `tools/run_game_replay.py` garde 27 parcours de compatibilité
  contre Hatari (médias originaux, changements de disquette, souris et glisser-déposer) ; le
  diagnostic Magic Pockets est exclu par défaut. À étendre : parties plus longues, disques STE.
  Son : première passe du 2026-11-11 (`tools/compare_sound.py`, enveloppe contre Hatari, 60 s après
  le boot, ST et STE) : 21 et 24 titres sonores concordants, niveau médian 0,985 et 1,0. Parmi les
  muets, `--launch` en ajoute 6 et 7, concordants ; 17 et 16 restent muets une fois lancés, 48 et 46
  n'ont pas de programme à la racine (amorçage ou dossier). Dynamite Dux (0,64) est jugé : son
  morceau dépend de l'angle de disquette tiré au démarrage, les mêmes écritures YM se retrouvent des
  deux côtés selon la graine.
  Un script joystick tient chaque commande plus que la période
  d'interrogation du jeu. Chaque parcours ajouté entre aussi dans la porte d'échange (§ 2).
  Essais du 2026-11-10 : Rick Dangerous et Pang joués (lancés du bureau). Bombjack et Xenon 2
  atteignent le jeu mais divergent au joystick générique (chargement décalé de quelques trames) ;
  New Zealand Story affiche des barres noires, identiques chez Hatari ; Giana Sisters passe par un
  menu D-Bug qu'ESPACE quitte ; Onslaught attend ses touches. Rainbow Islands (Ocean, deux STX)
  exige **4 Mo** : en 4 Mo, disque B à 7000, l'entrée en jeu concorde (0,02 % à 13000), puis la
  partie dérive ; en 1 Mo, NeoST et Hatari se séparent dès le chargement. CarVup passe trainer
  (N), niveau (F1) et instructions à l'identique, puis diverge, même avec `--hatari-delay`.
  Super Hang-On, Nitro, Dynamite Dux et Cybernoid II : en jeu, parties divergentes.
  → `docs/CASE_STUDIES.md`, `docs/OPENDST.md`
- **Passe de la collection** · P3 ⭘ — 96 images (`cpu_swap_gate.py --suite collection` : ST, STE et Mega STE) ; A/B du 2026-11-10 identique à
  l'octet sur 185 cas sur 188 ; passe d'angle du 2026-11-04 (5 graines), seul Vroom change de
  sort. À refaire après tout changement de timing. Super Off Road attend son disque de données
  et Chessmaster plante sous TOS 1.62, chez les deux émulateurs.

---

## 5. 🪜 Harnais et preuve

40 étalons `hatari`, 10 `snapshot`, 7 `rtl` ; les 8 verdicts `buserror-*` sont les seuls au
contact du matériel, par des relevés de tiers. Le banc fx68k ajoute 48 variantes `rtl` pour
le cœur. → [`docs/REFERENCE_LADDER.md`](docs/REFERENCE_LADDER.md)

### Monter l'échelle

- **Étendre les étalons `rtl`** · P2 ⏸ source — `rtl_low_pal`, `rtl_low_ntsc` et `rtl_med_pal` posés, ce dernier à 0 pixel. Reste ce qui bouge en cours de ligne (palette, fréquence), là où NeoST et le
  modèle peuvent vraiment diverger. Confronter aussi `ST4081S.PDF`. En attente : le modèle date les écritures 3 à 4 cycles trop tôt face à de vraies images (`docs/REFERENCE_LADDER.md`) ; un étalon en cours de ligne contre lui ne trancherait rien sans relevé.
- **Relevés de tiers** · P2 ◐ — passe du 2026-11-11 (sources relues par web.archive.org) : bus error à
  64 cycles sur ST/Mega ST (document Atari, appliqué), YM2149 (N. Pomarède, STF : `per`=0 ≡ 1 pour
  le bruit et l'enveloppe, bruit deux fois plus lent que le ton, R13 relance forme et phase —
  concordant), wakestate à froid (Troed, WS3), machines des relevés bus error d'Hatari, HWTST
  (atari-forum t=24862) : horloges CPU de huit machines et son DMA à 50 066 Hz concordants, et
  rapport de vitesse du Mega STE à 16 MHz (2,1816 avec cache, 0,9999 sans) reproduit à 0,0003
  près — `tools/run_hwtst_loop.py`, palier fast, verdict `hardware`. À exploiter : la
  bride BLANK du ST (diodes CR1-CR3, transcription KiCad du schéma C070231). Les notes de Steven
  Tattersall ne contiennent aucune mesure. Un relevé entre en `hardware` s'il nomme machine et
  protocole.
- **Documents primaires manquants** · P2 ⏸ source — HD6301 rapatrié le 2026-11-10 (Hitachi, *HD6301/HD6303 Series Handbook*, 1989) ; restent les schémas GLUE/MMU ST et Mega STE,
  schéma du Shifter ST. chzsoft.de consulté le 2026-11-10 : seul le GSTMCU du STE y est.
- **A43 — dette de preuve** · P2 ⏸ — tout commentaire cite ses références (règle : `CLAUDE.md`) ;
  les 312 citations d'Hatari portent grade et révision. MFP, YM2149, WD1772, ACIA et SCC sont
  confrontés à leurs fiches : ce qu'elles tranchaient seules est corrigé (2026-11-10), les `⚠ [B]`
  restants attendent un relevé sur le fer, ou sont gardés parce qu'un jeu contredit la fiche.
  Restent les GLUE/MMU sans schéma, et le SCC en synchrone, SDLC et DMA.
- **`union_demo` à monter en `hatari`** · P3 ⭘ — référence `snapshot`. `compare_titles` en 1 Mo sans `--fastfdc` concorde, 0,04 % au plus à 3000, 3900 et 6000. L'oracle de l'étalon, en 512 Ko, ne trouve aucune trame exacte dans ±90, avec ou sans `--fastfdc`. Tenter 1 Mo ; une égalité exacte manque peut-être à cause de 46 pixels résiduels, situés sur le scroller du bas : sur certaines lignes, quelques pixels prennent la couleur d'un cran voisin, 43 à la trame 3900. Cause non cherchée. Tenté en 1 Mo le 2026-11-11 : aucune trame identique dans ±90, ni avec ni sans `--fastfdc` ; la plus proche diffère de 5 213 px (avec) et 9 077 px (sans), toutes au bord de la fenêtre. Une fenêtre de ±300 (avec `--fastfdc`) ne trouve rien non plus : l'écart n'est pas un décalage de numérotation.
- **Monter les 10 snapshots** · P3 ⭘ — `blankline_stop_ste` et `blankline_node_late_ste` sont montés en `rtl`
  (2026-11-12). `freq_switch` et `blankline_*_st` n'ont pas de modèle ST ; `closure` et `overscan_top`
  s'expliquent déjà face à Hatari (A41, définitif pour les 27 px de la ligne 0).

### Instruments

- **Suite de tests d'Hatari : restes** · P2 ⏸ source — `run_hatari_suite.py` rejoue cycles, blitter,
  bus errors, `cpu`, `gemdos` et `serial`. Reste `screen` (référence PNG au format Hatari) ; `xbios`/`mem_end` hors périmètre. `screen` attend que l'écart de 4 px de `flixfull.prg` soit tranché (§ 5, Instruments, P3 [F]).
- **Banc temporel Hatari** · P2 ⏸ IACK — 20 sondes, dont écriture DMA disquette, lecture STX et
  DIVU/DIVS ; le résidu blitter/IRQ Mega STE attend la capture IACK. → `docs/HATARI_MEGASTE_AUDIT.md`
- **`screen/flixfull.prg`** · P3 ⭘ · [F] — NeoST est 4 px à gauche d'Hatari sur toute la ligne, ST comme STE. Écritures et décisions du GLUE identiques (LEFT_OFF + RIGHT_OFF, DE 5 à 463, 230 octets) :
  l'écart est dans la recopie de la ligne. Aucun témoin réel ne dit qui a raison, et le Shifter du modèle n'est pas fiable à ce grain. Tranche : une capture sur machine.
- **A10 — étalons adossés à une ROM Atari** · P3 ◐ — **45 étalons pixel sur 57** tournent
  sans TOS Atari. Les trois `spectrum512_diapo*` restent des SKIP recensés (migration EmuTOS
  réfutée, ne pas retenter) ; leur motif est rendu par `spec512_view`. `union_demo` → ci-dessus.
- **Écarts MAME [F] à trancher** · P3 ⭘ — MA-1 (remappage RAS/CAS du ST : 4 cas sur 6 diffèrent de MAME, NeoST
  suit Hatari) et MA-20 (FIFO DMA en écriture, 32ᵉ contre 33ᵉ bloc) ; le blitter, la vidéo et le Mega STE de
  MAME ne sont pas dans le clone. → [`docs/MAME_DIVERGENCES.md`](docs/MAME_DIVERGENCES.md)
- **Étalon spec512 en overscan** · P3 ⏸ média — aucune démo libre à bordures ouvertes et palette Spectrum 512
  trouvée (2026-11-12) ; à défaut, un étalon généré à bordures ouvertes, sur le modèle de `make_spec512_test.py`.

> **Les références se bâtissent** : `extern/hatari`, `extern/fx68k`, `extern/mistery`,
> `extern/mame` (les seuls fichiers cités) et `extern/datasheets` sont gitignorés, absents d'une machine fraîche. `tools/setup_hatari.sh` et
> `tools/setup_silicon_refs.sh` les rapatrient à leur épingle.
> → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md)

---

## 6. 🔌 Extensions — matériel réel, OFF par défaut

→ [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md). Tout a existé sur ST ; rien n'agit sur les étalons.

### Réseau, stockage, ports
- **Clés cartouche au reset à chaud** · P3 ⏸ source · [E] — gardées depuis le 2026-11-11 sur la foi
  de brochages publiés (START vol. 1 n° 3, ST-Log n° 27 : 40 broches, pas de /RESET). Passe au
  grade B avec un schéma Atari de la carte mère (C070112 pour le 520ST).
- **EWeb ST derrière AERA-Gateway** · P2 ⭘ — navigateur GEM libre (GPL v2) : `gateway/webgate.py`
  lancé seul sur l'hôte, sans Docker (Python standard, avec `aeragate.py` et `translateclient.py` ;
  essayé le 2026-11-11 en boucle locale, requête au format proxy), EWeb dans NeoST (STE 4 Mo,
  STinG + `ENEC.STX`) par le NAT, port 8280. Un site en HTTP simple se passe de passerelle.
  EWeb 1.0 (`EWEBST.PRG`) a affiché le 2026-11-11 une page HTTP d'un serveur local (titre, styles,
  liste, tableau), sur STE 4 Mo, EmuTOS 256 Ko, monochrome. Recette : image ACSI (`make_hd_image.py
  --no-driver`) portant EWeb, `AUTO`, `STING` et `EMUDESK.INF` avec `#Z` — le `--gemdos` occupe le
  port cartouche de l'EtherNEC, EmuTOS démarre sur C: dès qu'un disque est là, EWeb refuse la basse
  résolution. Restent : un lien cliqué, la passerelle et un site HTTPS, puis un test (EWeb non versionné).
  → <https://yaary.music/software/atari-st/eweb/>
- **NetUSBee : validation MiNT et matériel** · P3 ⭘ — USB4TOS vérifié (HID + disque) ;
  restent FreeMiNT, capture du latch des différentes cartes, timing électrique et ITL/isochrone.
  → [`docs/NETUSBEE.md`](docs/NETUSBEE.md)
- **RTL8019AS : restes sans preuve** · P3 ⏸ source · [F] — contenu de la 93C46 du NetUSBee
  (IRQ/E-S/média/duplex lus 0) et 93C46 non modélisée (EEDO=0 : le programmeur d'EEPROM d'Atari.org
  ne tourne pas), registres réservés lus 0, FIFO, durée de l'auto-load, HLTCLK mode 2, TSR bits 5/1
  et RSR bit 3 (fiche contradictoire p. 10/14). Tranché par : un dump de la page 3 et de la 9346
  d'un vrai NetUSBee. La fiche LS003.8 tranche le reste depuis le 2026-11-12.
- **NAT : correctifs à remonter dans POM2** · P3 ◐ — cache DNS positif à 60 s et vidé au reset de la carte :
  porté sur la branche POM2 `nat-dns-cache-from-neost` (`9e15c67d`, non poussée). Reste à la fusionner dans
  POM2, puis à reprendre la révision au `extern/nat/README`.
- **STinG, premier run en échec DNS** · P3 ⭘ · [F] — `err -17` au tout premier `--slirp` du 2026-11-04, puis 4 succès d'affilée, alors que le `--slirp-selftest` en ligne passait juste après. Le filtre de l'hôte reste la piste la plus simple, sans preuve. À regarder si l'échec revient, avec `NEOST_SLIRP_TRACE=1`, qui trace désormais le DNS (RCODE, délai) et les SYN/RST d'un vrai STinG. Essayé le 2026-11-12 sur quatre premiers runs : non reproduit (581 ms à froid). `err -17` = toute réponse DNS inutilisable, REFUSED compris.
- **Modem / STinG** · P3 ◐ — installation documentée (`docs/TEST_SOFTWARE.md` § 4 quater). Banc SLIP de bout
  en bout bloqué : `SERIAL.STX` absent (archive STinG 1.26), pas de pair SLIP côté hôte (piste : un numéro
  réservé du modem vers le NAT intégré), composeur de STinG.
- **Banc STinG → theoldnet.com** · P3 ⭘ — le site répond 403 à une requête sans User-Agent depuis 2026-11 :
  ajouter un User-Agent à STGTEST ou changer de cible ; les docs qui attendent « 200 » sont à revoir.
- **Passerelle intégrée au NAT** · P3 ⏸ décision — relais TLS côté hôte pour HTTP, IMAP, SMTP et
  FTP, aux ports des clients AERA ; les services lourds (images, scripts, PDF) restent à une
  AERA externe. Lire d'abord le protocole d'EWeb sur le fil, avec la vraie passerelle.

### Station MIDI
Cas visé : le ST mène la séance (Cubase, Notator), un Mac porte les AU/VST. La sortie est au
niveau (gigue σ 0,4-1,7 ms, `run_midi_sequencer.py`).
- **Interfaces multi-ports** · P3 ⏸ source — C-Lab Unitor, Steinberg Midex : la clé Unitor-N est émulée, pas
  les boîtiers. Aucun protocole publié (ni Hatari ni MAME ne les émulent). Débloqué par un schéma [B], un
  relevé de carte et de PAL [A], ou le pilote Unitor/M.ROS capturé en mémoire [C]. → `docs/EXTENSIONS.md`
- **Synchro SMPTE/MTC** · P3 ◐ — MTC tiré du transport du ST vers les sorties hôtes (`midi_mtc=`), éprouvé en
  selftest. Restent l'essai contre un logiciel hôte réel et le SMPTE de l'Unitor (même blocage que ci-dessus).
- **ALSA : identifiants réels** · P3 ⭘ — l'identifiant stable `alsa:<adresse USB>/<port>` est compilé (syntaxe
  vérifiée contre alsa-lib 1.1.3), jamais exécuté : `--midi-list` sur un Linux avec deux claviers identiques.
- **Port virtuel sous Windows** · P3 ⏸ décision — Windows MIDI Services évalué le 2026-11-12 : héberger un
  port exige le SDK C++/WinRT et un runtime séparé, hors MinGW ; ne pas implémenter. Les paires de bouclage
  de *MIDI Settings* (ou loopMIDI) suffisent, à confirmer sur un vrai Windows.
- **Anneau MIDI** · P3 ◐ — réception headless cadencée et saisie du pair dans le GUI (page MIDI, `midi_net=`)
  livrées ; reste un test à deux nœuds (un seul processus, bloqué par A33, § 9).

---

## 7. 📦 Livraison et interface

*Après la 0.6.5 : idées relevées le 2026-11-13 dans [BMAtariST v0.1.1](https://github.com/jhonnaker1/bmatarist/releases/tag/v0.1.1)
(Hatari non modifié, bare metal sur Raspberry Pi avec Circle, GPL-3.0 comme NeoST).*

- **Disquettes `.zip` et `.gz` ouvertes directement** · P2 ⭘ — monter l'image qu'elles contiennent sans
  décompression manuelle (cas Bruce Lee, 2026-11-13). BMAtariST le fait pour tous ses formats ; choisir
  l'image si l'archive en contient plusieurs, réécriture dans l'archive ou à côté, à trancher.
- **Vérifier un état sauvegardé juste après l'écriture** · P3 ⭘ — le relire et contrôler son CRC
  (déjà vérifié au chargement) ; un état raté est supprimé et le menu le dit, comme BMAtariST 0.1.1.
- **Format `.scp` (SuperCard Pro, flux magnétique)** · P3 ⭘ — lu par BMAtariST via Hatari ; NeoST lit
  `.st/.msa/.dim/.stx/.ipf`. Confronter au modèle FDC des `.stx`.
- **Borne Pi : écran calé sur la fréquence du ST** · P3 ⭘ — passer l'écran à 50/60/71 Hz (KMS sous
  Linux) et rythmer l'émulation sur ses rafraîchissements, pour un défilement sans à-coup ;
  `AudioPacer` garde déjà le son en phase. BMAtariST le fait sur Pi 4/5.
- **Borne Pi : joysticks câblés sur le GPIO** · P3 ⭘ — vraies manettes d'arcade sur le connecteur du Pi
  (libgpiod), en plus des manettes USB.
- **Retour arrière : essais à la main** · P3 ⭘ — livré dans le GUI de bureau, le web et
  Android (2026-11-11), jugé par `--rewind-test` ; le GUI réel recule aussi (`--rewind-at N`).
  Le bouton *Back* du web a reculé deux fois dans Chrome sans écran, piloté par CDP (refus
  éteint, anneau vide, puis deux pas). Restent à presser Ctrl+Alt+R au clavier, et `REWIND`
  sur un téléphone : Android n'est que vérifié à la syntaxe, l'APK se construit en CI.
- **Android : clavier physique sur un appareil réel** · P3 ⭘ — clavier (US et FR) et page ROM
  (quatre EmuTOS, choix persistant) exécutés sur l'émulateur arm64 API 36 le 2026-11-12. Reste un
  vrai clavier physique (couche `.kl/.kcm`, disposition AZERTY choisie dans Android), jamais exercé.
- **Paquets Linux sans MIDI ALSA ?** · P2 ⭘ — ni l'image bionic (`Dockerfile.bionic`) ni bookworm
  (`build_in_bookworm_arm64.sh`) n'installent `libasound2-dev` : les AppImage partent sans doute sans MIDI
  hôte. La CI compile ALSA depuis le 2026-11-12 (job GUI) ; ajouter le paquet aux images et vérifier.
- **Borne Raspberry Pi « bare metal » : NeoST sur Circle** · P3 ⏸ A12 + cœur — remplacer Hatari par
  `neost_core` dans le cadre de BMAtariST : sa frontière matérielle tient dans `src/host.h` (image RGB565,
  son 16 bits, touches USB, souris, manettes), Hatari s'y branche par `src/port/`. À lever : `EmuFiber`
  (mode sans fibre du web), `std::filesystem` absent de newlib, et d'abord la VITESSE de NeoST sur un
  Pi 4/5 (A12). Leur simulateur PC permet de mettre la couche au point sur Mac. Après l'échange du cœur.
- **A12 — validation des paquets sur machines hôtes** · P1 ⏸ matériel — macOS arm64 passé (sauf
  le pas visuel en session graphique). Restent les pas 3 à 5 pour **Raspberry Pi** (aucun
  budget temps réel mesuré), **Windows** (jamais lancé hors CI), **APK** (QEMU seul) et
  **Linux**. Ne dit rien de la fidélité. Le budget temps réel sera à remesurer après
  l'échange (§ 9). → [`docs/HW_VALIDATION.md`](docs/HW_VALIDATION.md)
- **Passe d'usage interactif du GUI** · P2 ⏸ session humaine — le GUI n'est couvert que par une
  capture au boot et trois assertions d'arguments. Même famille qu'A12.
- **Notarisation macOS** · P2 ⏸ décision (99 $/an) — `.app` scellé ad hoc ; manque Developer ID.
  Signature du `.zip` Windows écartée (token HSM, SmartScreen = un clic). ⚠ Le JIT de POM68K
  touchera au hardened runtime (`allow-jit`). → `docs/RELEASE.md`

---

## 8. 🧱 Dette d'architecture et de documentation

Issue de l'audit du 2026-08-27 et des revues suivantes ; le soldé est au `CHANGELOG.md`.
Ce qui touche `src/core/Cpu68k.cpp` attend l'échange → § 9.

- **A38 — tenue du registre** · P3 ⭘ — `tools/fast_guards.json` inscrit les étapes du palier
  `fast` ; consigner chaque vrai déclenchement dans le commit qui corrige le défaut.
- **A39 — modules jamais audités** · P3 ◐ — `src/net` relu le 2026-11-11 ; le bac à sable GEMDOS est jugé
  couche par couche depuis le 2026-11-12 (`--gemdos-selftest`, 30 cas : normalisation, parseur, garde
  terminale, liens pendants et intérieurs). Reste à désigner le prochain module jamais audité.

### Garde-fous (à NE PAS faire)
- **Croire que la liste se vide** : auditer coûte une journée, réparer des semaines. Arbitrer
  explicitement entre « ouvrir » et « fermer » à chaque session.
- **Combiner A9 + A32 (ou tout chantier structurel) en un « grand refactor »** : un à la fois,
  filet de test posé AVANT.
- **Refactorer `Cpu68k.cpp` ou patcher `extern/moira` avant l'échange** : c'est le fichier que
  l'échange réécrit et le code qu'il remplace.
- **Supprimer une branche d'exécution sur une intuition** : précédent A34, tranché par la mesure.
- **Compenser pour égaler un oracle** : ni les délais du blitter pour absorber le résidu IACK,
  ni les bords STOP_MIDDLE, ni `closure` passé en `hatari` (A41), **ni Moira ajusté pour égaler
  fx68k** : un écart du banc se tranche à une source plus haute ou reste écrit.
- **Garder un contrôle par un autre contrôle** : A38 ne se traite pas avec un 18ᵉ outil.

---

## 9. 🔁 L'échange du cœur — gelé jusque-là

Le Moira de POM68K (68000/030/040, JIT déterministe, FPU et MMU intégrés, cycle-exact en
68000) remplace `extern/moira`. **Rien ne s'intègre avant que l'auteur le déclare prêt.**
Tout ce qui suit touche l'intérieur du cœur ou son enveloppe `NeostMoira` : y travailler
maintenant, c'est travailler deux fois. Exception : un défaut qui casse un titre se corrige,
et **sa ligne s'ajoute à la table des patches** de `docs/CPU_SWAP_GATE.md`.

### Le jour de l'échange

1. Filet du § 2 en place, **0.6.5 publiée**, référence enregistrée sur son commit.
2. `diff` de `extern/moira/Moira` contre le cœur POM68K en mode 68000 : chaque patch de
   `extern/moira/NEOST_VENDOR.md` s'y retrouve, ou son absence est expliquée.
3. `cpu_swap_gate.py --check`, palier `full`, `run_moira_runner.sh`, parcours de jeu,
   `run_determinism.py`. Un écart se localise par trace et se tranche au banc fx68k : plus
   proche de fx68k, c'est un progrès consigné ; sinon une régression du cœur.
4. Remesurer : budget temps réel par cible (`run_perfbench.py --budget`), profil callgrind.

### Questions du 68000, à requalifier sur le nouveau cœur

Écarts du **cœur Moira nu** contre fx68k @ `0602ee46`, avec Hatari et le microcode pour autres
témoins : tous grade D, Moira non modifié. La chaîne MFP→IPL de NeoST ajoute ses propres délais :
requalifier dans NeoST avant de conclure sur une IRQ réelle. Détail → [`docs/FX68K_BENCH.md`](docs/FX68K_BENCH.md).

- **Les 14 variantes hors accord au banc fx68k** · P1 ⏸ cœur — à rejouer telles quelles sur POM68K :
  le verdict de chacune dit si l'échange est un progrès.
- **Bus error : état laissé par la faute** · P2 ⏸ cœur · [D] ×2 — fx68k et Hatari s'accordent contre
  Moira : An incrémenté ou décrémenté par `(An)+`/`-(An)` fautifs, PC empilé de 2 plus loin sur
  ces modes, PC proche du JMP (et non sa cible) quand le préfetch faute. L'entrée de l'exception
  dure 2 cycles de moins chez Moira (fx68k seul). → `docs/FX68K_BENCH.md` § *Bus error et double faute*
- **Pile impaire à l'empilement d'un TRAP** · P2 ⏸ cœur · [B] + [D] ×2 — Moira écrit la trame à
  l'adresse arrondie et poursuit ; fx68k, Hatari et la fiche (§ 6.3.9) lèvent l'erreur d'adresse
  puis s'arrêtent en double faute. Aucun titre connu n'en dépend ; à vérifier d'abord sur POM68K.
- **Prise d'IPL sur ST et Mega STE** · P2 ⏸ source · [F] — le STE suit fx68k + GSTMCU pour la HBL et pour la VBL pendant un STOP, depuis le 2026-11-04 (`docs/STCPU_BENCH.md`). Hors STOP, sa VBL se prend au dispatch, car l'intro de Vroom `[cr Elite]` se fige sinon ; ST et Mega STE gardent le commit
  d'Hatari, faute de modèle du GLUE. Le 68000 est le même : à trancher par un modèle du GLUE ST, ou un relevé de tiers.
- **Latence IACK MFP** · P1 ⏸ matériel · [D] 12 cycles — capturer CLK CPU/MFP, IACK et les
  DTACK puce/CPU sur un vrai ST et un vrai Mega STE, aux trois régimes CPU/cache. Sonde prête
  (`tools/make_mfp_iack_probe.py`, contrôles verts). Le bloc IACK = 12 cycles bus (valeur
  d'Hatari, « not measured ») ; le 16 du 2026-08-06 compensait le défaut STOP+4 de Moira. Le
  banc Mega STE (`docs/HATARI_MEGASTE_AUDIT.md`) est à refaire avec 12 ; le résidu
  blitter/IRQ 16 MHz en dépend. → [protocole](docs/MFP_IACK_MEASUREMENT.md)
- **Erreur d'adresse : cycle amorcé** · P3 ⭘ · [D] — seul fx68k pose AS sans strobe à l'adresse fautive.
  Un seul témoin, sans effet connu sur un ST.
- **FPU MC68881 : silicium, et qui calcule** · P3 ⏸ cœur · [D] — bit à bit avec Hatari, transcendantes
  FPSP comprises, mais le 68881 réel calcule en CORDIC : dernier bit non garanti, « zéro »
  non normalisé après underflow à vérifier. `k` dynamique fixé à 17 (le dialogue
  périphérique ne transmet pas Dn). POM68K apporte son FPU : décider si `src/io/Fpu.cpp`
  garde son arithmétique pour le 68881 en périphérique du Mega STE, ou la lui délègue.

- **DMA disque : le CPU ne cède que le bus** · P3 ⏸ cœur · [D] — relevé sur fx68k le 2026-11-11
  (`Vfx68k_bench --br`) : BR → BG en 2 cycles, puis le 68000 ne bloque qu'à son prochain accès ; un
  prêt de 32 cycles pendant un MULU ne le retarde pas du tout. NeoST facture 32 cycles par rafale
  sans condition (`Fdc::billDmaCycles`). À porter avec l'arbitrage de bus du nouveau cœur, comme
  le recouvrement du blitter. → `docs/FX68K_BENCH.md` § *Bus prêté à un DMA*

### Dette qui attend le nouveau cœur

- **Suivre l'amont Moira** · P3 ⏸ cœur — 10 commits non portés depuis le pin (68020/68030, vAmiga,
  empreinte mémoire) ; rien pour le 68000 hormis DIVU/DIVS, porté. Sans objet si POM68K les
  a déjà : à vérifier au `diff`. Rejouer `tools/run_moira_runner.sh` et le banc fx68k à
  chaque patch du cœur.
- **A33 — vrai parallélisme CPU** · P3 ⏸ cœur — retirer les accès à `g_cur` (≈ 215 dans
  `src/core/Cpu68k.cpp`). Rien ne le réclame, sauf l'anneau MIDI en un seul processus. À
  faire dans la réécriture de l'enveloppe, pas avant.
- **Points chauds du cœur** · P3 ⏸ cœur — `NeostMoira::sync`, le dispatch de l'ordonnanceur et
  les lectures bus pèsent 39 % du profil ; le JIT change la donne. Refaire le profil après
  l'échange avant d'y toucher. → `docs/PERFORMANCE.md` § 6

---

## 10. 🚀 Le TT et le Falcon

Après l'échange. D'ici là : des inventaires, des fiches et des oracles, aucun code de machine.

- **Puces du TT et du Falcon** · P2 ⏸ cœur — DSP56001, VIDEL, Crossbar, second MFP du TT,
  SCSI NCR5380, IDE, NVRAM MC146818 ; bus 32 bits et cartes MMIO par machine. Fiches à
  rapatrier par `tools/setup_silicon_refs.sh` ; témoins : `extern/hatari/src/falcon`.
- **Fiches et oracle, restes** · P3 ⏸ source — fait le 2026-11-11 : MC68030, DSP56001, NCR5380,
  MC146818A, TT030 Hardware Reference Manual et deux éditions de la doc développeur Falcon épinglés
  (`tools/setup_silicon_refs.sh`) ; oracle Hatari en TT et en Falcon jusqu'au bureau avec EmuTOS
  512 Ko (`docs/HATARI_AUTOMATION.md`). Manquent la carte des registres du VIDEL et une fiche du
  Crossbar (aucune publiée par Atari), et la norme ATA pour l'IDE. Fixer `--cpu-exact`/`--compatible`
  avant toute référence TT/Falcon.
- **Terrain dans le code** → A32 fait (2026-11-11 : `VideoGlue`, `VideoCounter` et le Shifter, le compteur lisant la Glue par `GlueView`). Reste l'interface de puce vidéo que liront les frontends au lieu de `Shifter&` (`docs/HYPOTHESES_ST.md` § 5) ; ce que le TT et le Falcon remettent en cause, chiffré par catégorie : [`docs/HYPOTHESES_ST.md`](docs/HYPOTHESES_ST.md) (le plus structurel : `ADDR_MASK` et la frontière de 4 Mo du `Bus`, l'horloge unique à 8 MHz de l'ordonnanceur, 124 tests de `MachineType`).
- **Suite AERA de yaary.music** · P2 ⏸ Falcon — <https://yaary.music/software/atari-st/>.
  Dix programmes GEM en C, sous GPL v2 : EWeb, EMail, ECal, EContact, EFTP, EPDF, EPlay,
  EHypView, EUpdate, ClaudeST. Sur Falcon, EWeb fait son TLS 1.2 et ses JPEG **sur le DSP**,
  et EPlay y décode le MP3 : de vrais juges du DSP56001. Sur ST, ils passent par AERA-Gateway.
