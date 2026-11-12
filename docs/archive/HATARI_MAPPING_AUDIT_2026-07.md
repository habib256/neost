# Audit de cartographie Hatari ↔ NeoST — tables du 2026-07-08 (archive)

> **Archive.** Extrait de [`../HATARI_MAPPING.md`](../HATARI_MAPPING.md) le 2026-11-10. Les `fichier:ligne`
> sont ceux de l'époque et ont dérivé ; les statuts « clos » ont été ajoutés au fil des passes.
> Rien ici ne fait foi : l'ouvert vit dans [`../../TODO.md`](../../TODO.md), l'état par puce dans
> [`../IMPLEMENTED.md`](../IMPLEMENTED.md). **Recontrôle du 2026-11-12** : chaque rangée des tables
> « Docs à corriger » et « Prochains travaux » porte son verdict en dernière colonne, établi contre
> l'état des docs et du code ce jour-là. « Appliqué » = corrigé ce jour dans le document visé ;
> « Déjà appliqué / clos » = vrai avant ; « Sans objet » = la phrase visée n'existe plus ; « Non
> appliqué » = le `CHANGELOG.md` est un registre daté, on n'y réécrit pas une entrée.

## Docs à corriger

Issues regroupées par fichier (suggestion en italique). Les entrées liées au tranchage
WS3 tiennent compte de l'arbre de travail.

### docs/HATARI_DIVERGENCES.md

| Ligne(s) | Problème | Suggestion | Verdict (2026-11-12) |
|---|---|---|---|
| 33 + 525 + 50 | Ancien audit : le commit cité ne portait pas le trou MMU. | Port réellement présent depuis le 2026-08-13 ; entrée de mapping corrigée le 2026-10-18. | Déjà clos ; § 7 de l'inventaire (trou MMU porté le 2026-08-13). |
| 64, 549-556 | L'« hybride WS1/WS3 à trancher » est TRANCHÉ WS3 dans l'arbre (Shifter.cpp:102-149, Machine.cpp:49-63). | *Marquer résolu, décrire la table wakestate (NEOST_WS pour A/B).* | Appliqué : § 2, refermé [WS] (WS3, `NEOST_WS` pour l'A/B). |
| 195-199 | La § bus déclare « conforme » sans mentionner le cold reset $FF8001=0 non porté (stMemory.c:93-102). | *Ajouter entrée [basse] « cold reset $FF8001=0 absent ».* | Appliqué : `$FF8001` remis à 0 au froid (`Machine::hardReset`) en « Concordant » § 7 ; le préremplissage à la configuration, lui, est inscrit en écart assumé. |
| 91, 97, 99 | Références périmées : conversion MFP = Mfp.cpp:267-268 (pas :255), retombée IRQ = Mfp.cpp:543-544 (pas :529), wait MFP = Bus.cpp:590 (pas :540). | *Rafraîchir les trois références.* | Appliqué : références remplacées par symbole et ligne revérifiés (§ 1). |
| 130 | « Shifter.hpp:334 » pour bordered() — dérive : c'est Shifter.hpp:404-405. | *Corriger.* | Sans objet : la référence a disparu de l'inventaire. |
| 148-149 | « Hatari ne touche Status qu'au cold reset (fdc.c:1233) » — inexact : FDC_Reset (fdc.c:1181) met Status=1 froid ET chaud ; fdc.c:1233 = FDC_ResetDMA qui ne touche jamais Status. | *Reformuler.* | Sans objet : la phrase a disparu de l'inventaire. |
| 160 | Audit corrigé le 2026-10-19 : déjà 18 durées de trame, pas quatre ; deux actions portées puis compteur VBL réel ajouté en v43 pour conserver 18/36 VBL si fréquence changée en vol. | *Clos dans ce périmètre.* | Déjà clos ; § 3, « Changements rapides de média ». |
| 592-598 (liste ouverte FDC) | (1) Dérive −PendingCyclesOver des événements FDC **non inventoriée nulle part** (Fdc.cpp:1855/1899 vs fdc.c:2370) ; (2) omissions : IRQ HDC 1000 cyc, ripple-carry, hot-swap, dmaError_=false au toggle ; pas de rubrique ACSI. | *Ajouter l'entrée [moyenne] anti-dérive + consolider les 4 items + rubrique ACSI ; mettre le décompte :47 en phase.* | Appliqué : anti-dérive dans [D3] (réarmement sur l'horloge live) ; IRQ HDC, retenue des bits 7/15, hot-swap et statut d'erreur au reset FIFO dans § 3 et § 4 ; rubrique ACSI créée (§ 4). |
| 587-588 | Audit périmé : les ombres PSG $FF8801/03 étaient déjà gérées par `Bus::wrPsg`, selon la largeur. Requalifiées le 2026-10-19. | *Clos ; banc Bus ajouté.* | Déjà clos ; « Concordant » § 7. |
| 589-598 | S3/S4 présentés OUVERTS dans le bloc 5ᵉ passe alors que corrigés 2026-07-07 (lignes 71-72 ✅). | *Annoter « ✅ corrigé » comme pour S2.* | Appliqué : S3/S4 en « Refermés » § 5, une entrée chacun. |
| 649 | « SCC inchangé » : le routage LAN/Serial par bit7 port A PSG (psg.c:423-426 → scc.c:402-416) est absent et non inventorié. | *Ajouter entrée [basse] section SCC.* | Appliqué : entrée ouverte « Routage LAN / Serial 2 » § 9 (toujours non modélisé). |
| 175-177, 528, 623 | Références dmaSnd périmées après le refactor non commité (shelfCoeffs à :285-309, mode_&=0x8F à :495) ; « YM nu dès PLAY=0 » périmé (drainage porté) ; « kDmaGain=0.7 vs 0.75 » contredit S3 corrigé (0.375×2 = 0.75 exact). | *Rafraîchir ; remplacer l'entrée :176 par « pas de DC-hold » ; retirer kDmaGain de :623 (garder le signe ×−1).* | Appliqué : « pas de DC-hold » en écart assumé § 5 ; YM nu et kDmaGain en faux positifs ; références rafraîchies. |
| 162-179 | Masque haut DMA son et lectures $FF8900/$FF8920 déjà portés, requalifiés le 2026-10-19. Scratch $FF8920 désormais sauvegardé v42. | *Clos pour ce périmètre.* | Déjà clos ; § 5 (et `$FF8920` revu le 2026-11-12 : octet relu 0). |
| 601-617 | L'entrée S2 ne mentionne pas le reste introduit par le port : fifoRefill à chaque lecture du compteur (DmaSound.cpp:430) vs refill au HBL. | *Documenter sous S2.* | Appliqué : le reste (refill à la lecture du compteur) a été corrigé le 2026-08-06, noté sous [S2] § 5. |
| 646 | « TDRE ne tombe jamais sans TIE » présenté comme spécifique MIDI — l'ACIA CLAVIER a exactement le même défaut (Ikbd.cpp:202 vs acia.c:899). | *Étendre l'entrée au clavier.* | Appliqué : les deux ACIA corrigés le 2026-10-18, entrée « TDRE qui ne tombait jamais sans TIE (MIDI et clavier) » § 6. |
| 181-193 | La § ACIA/IKBD n'inventorie ni le transit série CPU→IKBD absent (~10240 cyc/octet), ni customreset (instruction RESET), ni l'absence de Midi_Reset au reset machine. | *Ajouter trois entrées ([moyenne], [moyenne], [basse]).* | Déjà clos : transit CPU → IKBD (2026-10-18), instruction RESET (§ 12, Concordant), reset MIDI ([B9]) ; inscrits § 6 et § 12. |
| 216-264 (§ Blitter) | Deux divergences confirmées manquantes : pas d'interfoliage CycInt_Process par accès bus pendant la tranche ; (le « PRE_START 4+4 » signalé par la cartographie est lui RÉFUTÉ — ne pas l'ajouter). | *Ajouter l'entrée interfoliage [basse/moyenne].* | Déjà clos : BL4 (2026-08-25), § 8. |

### docs/CYCLE_ACCURACY.md

| Ligne(s) | Problème | Suggestion | Verdict (2026-11-12) |
|---|---|---|---|
| 111 + 142-147 | Non-hog déjà porté ; atomicité TAS déjà garantie par le modèle CPU actuel (audit 2026-10-19). | *Restant à qualifier séparément : recouvrement CPU/blitter et autres écarts de datation.* | Déjà appliqué (§ 4, P2 Blitter non-hog, TAS). |
| 109 + 137-140 | « RestartVideoCounter tenté puis retiré » — porté et actif depuis 2026-07-02 (VC_RESTART). | *Mettre à jour.* | Déjà appliqué (§ 4, `RestartVideoCounter` porté le 2026-07-02). |
| 112 + 149-153 | « P2 Son DMA STE compteur live + FIFO » — S2 corrigé 2026-07-07. | *Passer en acquis ; reste = quantification du refill (candidat n°30).* | Appliqué le 2026-11-12 : lignes `DmaSound.cpp` rafraîchies ; le « reste » remplacé par la confrontation `dmasnd_poll` du 2026-09-02. |
| 156-159 | « VBL au cycle exact » : l'offset broche pré-armée est FAIT ; ne reste que la trame à longueur réelle (LINELEN). | *Réduire l'item.* | Appliqué en partie : la longueur de ligne live et la fin de trame réelle (2026-10-22) sont notées ; « VBL au cycle exact » reste ouvert, renvoyé au TODO. |
| 179-182 | « constantes EMPIRIQUES » — périmé : read −6 / write +2 / −25 sont des valeurs FIDÈLES dérivées (2026-07-03). | *Mettre à jour + règle « par paire ».* | Déjà appliqué (§ 4, P3 « Offsets de datation », règle par paire). |

### DEV.md

| Ligne(s) | Problème | Suggestion | Verdict (2026-11-12) |
|---|---|---|---|
| 29 | « Blitter ST (HOG) » périmé. | *« Blitter ST (données + partage de bus hog/non-hog 64/64 cycle-exact) ».* | Déjà appliqué (arborescence : « données + partage de bus hog ET non-hog »). |
| 50 | « Timer B à 400 » présenté fixe — position DE-dépendante (400/396/184, AER bit3). | *Préciser (aussi dans CYCLE_ACCURACY §3).* | Déjà appliqué (« Timer B à une position DÉRIVÉE »). |
| 52-53 | « quantum sous la ligne = grand chantier » — acquis ; le chantier est le beam-sync par-ligne/LINELEN. | *Remplacer.* | Déjà appliqué (« Le quantum sous la ligne est ACQUIS »). |
| 58-60 | Énumération mmioRead8/Write8 incomplète (blitter, joypads, SCC, SCU, cache MegaSTE, FPU). | *Compléter.* | Appliqué le 2026-11-12 : liste complétée d'après `Bus::mmioTable` (blitter, SCC, SCU, cache Mega STE, joypads, FPU, MIDI). |
| 176, 181-182 | Table de mapping : « screen.c » n'existe pas (→ conv_st.c + spec512.c) ; manquent Scheduler→cycInt.c/cycles.c, StxImage→floppies/stx.c, msa/dim.c, hatari-glue.c/newcpu.c pour Cpu68k. | *Compléter la table.* | Déjà appliqué (table de correspondance : `conv_st.c`, `cycInt.c`/`cycles.c`, `floppies/stx.c`, `msa.c`/`dim.c`, `hatari-glue.c`/`newcpu.c`). |

### TODO.md

| Ligne(s) | Problème | Suggestion | Verdict (2026-11-12) |
|---|---|---|---|
| 31 | Captain Blood « erreur clavier » — expliqué 2026-07-03 (détection AZERTY du jeu, pas un bug). | *Marquer résolu, renvoi TEST_SOFTWARE.md.* | Sans objet : le TODO a été réécrit ; Captain Blood est un cas tranché (`TODO.md` § 4). |
| 68 + 131-133 | S2/S3 et « FIFO + compteur live » listés à faire — corrigés 2026-07-07. | *Remplacer par les restes réels (MaskAddressHigh, $FF8900 pair, signe ×−1, Savinkoff).* | Sans objet : TODO réécrit, ces items n'y figurent plus. |
| 71 | M1 GPIP encore listé — corrigé bc15a67. | *Ne garder que « UpdateTimers avant lecture IPR ».* | Sans objet : M1 n'y figure plus. |
| 96-102 | « read −14 / write −6 / HBL 512 » périmés (actuel : read −6, write +2, HBL par wakestate). | *Annoter.* | Sans objet : ces valeurs n'y figurent plus. |
| 121-122 | « Bus/MMU fait et validé » masque wait-state FDC, trou MMU STF (marqué à tort corrigé), cold reset $FF8001. | *Renvoyer vers la section Bus des divergences.* | Sans objet : TODO réécrit ; l'état du bus est dans l'inventaire § 7. |

### CHANGELOG.md

| Ligne(s) | Problème | Suggestion | Verdict (2026-11-12) |
|---|---|---|---|
| 96-100 + 176-178 | Valeurs beam-sync périmées (read −14/write −6/HBL 512 ; rustine +16) sans annotation. | *Entrée consolidée « datations 2026-07-03 » ou chaîne d'annotations.* | Non appliqué : registre daté, une entrée ancienne ne se réécrit pas ; les valeurs courantes vivent dans `CYCLE_ACCURACY.md` § 4 et `DEV.md`. |
| 156-168 | « VC wait +2 actif » — défaut réel 0 depuis RAM_SLOT défaut-ON. | *Annoter « superseded ».* | Non appliqué : registre daté. |
| 260-267 | kSpec512AlignCyc « −23 » et read « −2 » — actuels : −25 et −6 ; étalon = 10/10 images. | *Entrée corrective.* | Non appliqué : registre daté ; valeurs courantes dans `CYCLE_ACCURACY.md` § 4. |
| 461-462 | « GPIP4 câblée sur RDRF » — ancien modèle (actuel : irqActive wire-OR RX+TX clavier+MIDI). | *Reformuler.* | Non appliqué : registre daté ; le modèle courant (OU câblé RX+TX clavier et MIDI) est dans l'inventaire § 6. |
| section Vidéo | Rien sur les travaux vidéo du 2026-07-03 pourtant committés (lineSnap_, datations fidèles, −25, STOP niveau-sensible). | *Ajouter 3-4 entrées datées.* | Couvert en partie : le récit de la 5ᵉ passe (2026-07-07), déplacé au `CHANGELOG.md` le 2026-11-12, cite les datations −6/+2/−25 et le commit `bc15a67` du 2026-07-03 ; pas d'entrée dédiée, faute de source détaillée. |
| 904-916 | Ancienne entrée blitter (forfait 256) sans renvoi vers celle du 2026-07-07. | *Renvoi croisé (optionnel).* | Non appliqué : renvoi optionnel dans un registre daté. |

### Commentaires code (signalés au passage)

✅ **Cinq des six sont TRAITÉS (passe de cohérence du 2026-08-19)** ; le sixième
(`Machine.cpp:298-301`) n'en était plus un : le commentaire de `kHblOff` avait déjà été
réécrit après le tranchage WS3 (défaut 0, `−4` seulement en A/B WS1). Les `fichier:ligne`
de la colonne 1 sont ceux de 2026-07-08 et ont dérivé.

| Fichier:ligne (2026-07-08) | Problème | Suggestion |
|---|---|---|
| src/core/Machine.cpp:94-96 (+ Scheduler.hpp:107, Cpu68k.cpp:608-609) | « Préemption DORMANTE » — FAUX en mode bloc défaut (beginRun appelé à chaque bloc, la préemption est ACTIVE). | *Corriger les trois commentaires.* |
| src/core/Machine.cpp:298-301 | Contradiction avec le défaut kHblOff réel (à revalider après le tranchage WS3 de l'arbre). | *Aligner.* |
| src/core/Scheduler.hpp:10-13 | En-tête « Phase 1 : 3 sources, quantum ligne » — 19 sources, quantum à l'événement. | *Réécrire.* |
| src/core/Bus.cpp:270-271 | « blitter volontairement ABSENT : NeoST ne l'émule pas » — périmé (émulé, dé-fauté selon machineHasBlitter). | *Reformuler.* |
| src/io/Mfp.hpp:6-9 | « strict nécessaire : Timer C … » — le MFP est quasi 1:1 avec mfp.c. | *Réécrire l'intro.* |
| src/core/YM2149.hpp:112 | Laisse croire que R7 relisible = 0xFF chez Hatari aussi (en réalité PSGRegisters[7]=0). | *Préciser la micro-divergence.* |

---

## Prochains travaux de précision

**Candidats CONFIRMÉS uniquement** (43 verdicts positifs, 36 uniques), priorisés impact
timing d'abord. Effort : S (< ½ journée, localisé) / M (chantier borné) / L (gros bloc).

### P1 — Timing systémique (dérives d'horloge, phase CPU↔faisceau)

| # | Candidat | Hatari | NeoST | Effort | Verdict (2026-11-12) |
|---|---|---|---|---|---|
| 1 | **Wait states FDC/DMA portés le 2026-10-18** : 4 cycles CPU par handler, lecture SCREG/statut DMA et adresses sans attente | fdc.c (DiskController*_Word, DmaModeControl), m68000.c (WaitState) | Fdc.cpp (registerWait), tests/fdc_timing_logic.cpp | clos | Clos. |
| 2 | **Ancrage anti-dérive corrigé (2026-11-04)** : coût CPU DMA conservé, flux disque non ralenti | fdc.c:2310-2316, 2370 | Fdc.cpp (onFdcEvent), vérifié 2026-10-18 | clos | Clos. |
| 3 | **Stall FIFO DMA déjà porté**, 32 cycles bus au flush de 16 octets | fdc.c:1340,1396 | Fdc.cpp (billDmaCycles), clos 2026-08-26 | clos | Clos. |
| 4 | ~~**Conversion MFP→CPU avec reste accumulé**~~ **PORTÉE** (2026-08-14) : échéances absolues ×256, plafond vers le Scheduler, phase sérialisée (v11) ; Timer C 200 Hz = 40106,238 cycles sans dérive | cycInt.c:21-47, cycInt.h:53-63, mfp.c:1408-1423 | Mfp.cpp (`timerPeriodSubCycles`, `scheduleTimerAt`) | — | Clos. |
| 5 ✅ | **Fait le 2026-09-02** (`Mfp::updateTimers` → `Scheduler::runMfpTimersTo`, NEOST_MFP_UPDTIMERS actif par défaut) — **MFP_UpdateTimers avant lecture IPR/ISR/TBDR** (poll vu ≤1 instruction en retard en mode bloc ; runTo ciblé sur les sources TIMER_* au read MMIO $FFFAxx, sans réactiver le sync-driven réfuté) | mfp.c:681-692 (+ ~30 handlers) | Mfp.cpp:63-70, 324, 418 | clos | Clos. |
| 6 ✅ | **Fait le 2026-08-25 (BL4, dispatch par accès via `Scheduler::addStolenCycles`)** — **Interfoliage des événements pendant le blit** (CycInt_Process par accès bus ; écritures blitter vs capture lineSnap_/événements MFP — divergence pixel directe sur écran dessiné au blitter en cours de trame) | blitter.c:342-375 (366), 417-418, 437-438, 451-452 | `Blitter::billCycles`, `Scheduler::addStolenCycles` | clos | Clos. |
| 7 ✅ | **Fait le 2026-10-19** : hooks Moira par cycle bus, recouvrement des cycles CPU internes HOG/partagé, sonde MULU identique à Hatari — **Exécution CPU parallèle pendant la tranche blitter** (MULU/DIVU recouvrent le blit ; jusqu'à ~256 cyc/tranche de recouvrement perdu — Relapse s'auto-calibre dessus ; hooks par-cycle Moira) | blitter.c:932-937, 1641-1659, 76-80 | Blitter.cpp, Cpu68k.cpp (hooks cycle bus) | clos | Clos. |

### P2 — Datation d'événements localisée

| # | Candidat | Hatari | NeoST | Effort | Verdict (2026-11-12) |
|---|---|---|---|---|---|
| 8 ✅ | **Fait le 2026-08-12** (`Shifter::timerBPosForLine`, DE réel + 24) — **Timer B au DE RÉEL des lignes à tricks** (RIGHT_OFF → tic à 486 vs 400 : tout split raster Timer B sur ligne fullscreen ~86 cyc trop tôt ; la donnée existe déjà : glueLines_[].displayEndCycle) | video.c:2880-2889, 3161-3171 | VideoGlue.cpp:70-81 ; Machine.cpp:510-535 | clos | Clos. |
| 9 | **Repositionnement Timer B à l’écriture AER3 porté le 2026-10-19** : anciens fronts échus puis nouveau front futur strict, sinon ligne suivante ; banc synthétique 50/60/mono | mfp.c:2742-2812, video.c Video_AddInterruptTimerB | `Machine::retargetTimerB`, `Mfp::write8/hblankAt` | clos | Clos. |
| 10 | **Réveil STOP quantifié grille 4 cyc** (phase E-clock d'IACK mod 4 après STOP — amplificateur SHO documenté) | newcpu.c:9234 + gencpu.c:7018 | Cpu68k.cpp:1066-1106 (saut d'attente STOP ; réveil mesuré identique à l'oracle, cf. HATARI_DIVERGENCES.md) | S | Clos le 2026-10-22 (STOP 4(0/0), prise quantifiée sur 4 cycles ; inventaire § 12). |
| 11 ✅ | **Fait le 2026-07-12** (kSpec512Threshold = 1) — **Supprimer le cliff kSpec512Threshold=512** (Hatari : seuil 1 ; SHO ~420 accès/trame pile sous le seuil → chemin de rendu ALTERNANT par trame, candidat n°1 des lignes raster transitoires) | configuration.c:769 + spec512.c:140-143, 221-224 | Shifter.cpp:108, 662 | clos | Clos. |
| 12 ✅ | **Fait le 2026-09-02/03** (`irqUpdateNeeded_` + `Mfp::flushIrqUpdate`, NEOST_MFP_BATCH actif par défaut) — **Élection MFP chronologique par LOT** (inputs d'une même instruction ; vecteur IACK peut différer — rarissime, après n°4/5) | mfp.c:1083-1087, 1118-1125, 689-690 | Mfp.cpp:55-69 ; Mfp.hpp:140-151 | clos | Clos ; remplacé le 2026-11-10 par l'élection à priorité fixe (inventaire § 1). |
| 13 | **Atomicité TAS déjà garantie par le modèle courant**, requalifiée le 2026-10-19 : aucun dispatch Scheduler dans l’instruction, même avec BLITTER échu entre lecture/écriture | blitter.c:1668-1695 | `Cpu68k::run`, `Blitter::onSlice`, tests/blitter_rmw_logic.cpp | clos pour le modèle courant | Clos. |

### P3 — Comportement / protocole (IRQ, registres, resets)

| # | Candidat | Hatari | NeoST | Effort | Verdict (2026-11-12) |
|---|---|---|---|---|---|
| 14 | **Instruction RESET : déjà portée** via le hook Moira | hatari-glue.c:49-77 | Cpu68k.cpp (didExecute), Bus.cpp (peripheralReset) | clos | Clos. |
| 15 | **Transit CPU→IKBD porté pour /64 8N1 (2026-10-18)** | acia.c:893-902, 954-1027 ; ikbd.c:755-819 | Ikbd.cpp (write8, onTxEmpty, receiveSerialByte) | porté ; formats/diviseurs et break ouverts | Porté ; formats et diviseurs : inventaire § 6. |
| 16 | **Délais de réponse IKBD portés le 2026-10-18** : attente globale quantifiée en bits, valeur centrale déterministe des plages Hatari ; interrogations et Froggies | ikbd.c (Send_Byte_Delay, SCI_Set_Line_TX) | Ikbd.cpp (pushRxDelayed/armRx), tests/ikbd_response_logic.cpp | clos, phase bit exacte non simulée | Clos. |
| 17 | **Monitoring $17 porté le 2026-10-18** : cadence commune au taux demandé ; autosend normal à 150000 cycles | ikbd.c (SetJoystickMonitoring, InterruptHandler_AutoSend) | Ikbd.cpp (onJoystickMonitor), Scheduler::IKBD_MONITOR | porté ; phase SCI et formats ACIA non usuels restent approximatifs | Porté. |
| 18 | **Garde d'initialisation portée le 2026-10-19 (`aciaClockInitialized_`, v46) ; reste la cadence pour les autres diviseurs** — **Cadence ACIA dérivée du Clock_Divider + garde init** (div 1/16 → liaison 64×/4× ; Divider==0 → IKBD muet) | acia.c:800-804 ; ikbd.c:1027-1032 | Ikbd.cpp:1156 (10240 en dur), 682 (repli 8N1/1024 hors /64) | S | Ouvert : cadence en dur à 10 240 cycles (`Ikbd::armRx`), inventaire § 6. |
| 19 | **Drive/side push déjà porté** à chaque écriture port A PSG | psg.c:420 → fdc.c:1720-1766 | Machine.cpp (setPortASink), Fdc.cpp (refreshDriveSide) | clos | Clos. |
| 20 ✅ | **IRQ HDC différée de 1000 cycles CPU post-DMA ACSI** — portée le 2026-10-18 ; acquittements de commande conservés, échéance sauvegardée | hdc.c `Acsi_DmaTransfer`, `HDC_ACSI_InterruptHandler_Update` | Fdc.cpp `acsiDmaTransfer/onAcsiIrq`, Scheduler::ACSI_IRQ | — | Clos. |
| 21 | **Cold reset : déjà porté** ; constructeur/reconfigure préremplissent encore la configuration RAM | stMemory.c:93-102 ; reset.c:57 | Machine.hpp (hardReset), vérifié le 2026-10-18 | clos pour hardReset | Clos ; préremplissage à la configuration inscrit en écart assumé (inventaire § 7). |
| 22 | **Trou MMU STF bank0=128K/bank1=2048K : déjà porté** depuis le 2026-08-13 | memory.c:1623-1631 | Bus.cpp (mmuTranslate), vérifié le 2026-10-18 | clos | Clos. |
| 23 | **Décodage MFP déjà porté**, requalifié le 2026-10-19 : pairs byte fautifs, filler $FF dans les accès partiels mot/long, impairs $31–$3F void. Ricoh STE déjà couvert par le banc MMIO | ioMem.c:835-849, ioMemTabST/STE | `Bus::rdMfp/wrMfp`, tests/mfp_mmio_decode_logic.cpp | clos | Clos. |
| 24 ✅ | **Porté le 2026-07-08** (`Shifter::updateGlueRes`) ; `Video_WriteToShifterRes` (res=3, écriture $FF8261) porté le 2026-10-20 (`Shifter::writeShifterRes`) ; reste le hardscroll 4 px Paulo Simoes — **V2 — tricks par changement de résolution** (med-res overscan No Cooper/PYM, stab, scrolls hw 13/9/5/1 px, $FF8261/res=3 Troed/Sync — plus gros bloc STF restant de video.c) | video.c:1618-1857, 1871-1961 ; ioMemTabST.c:71-72 | VideoGlue.cpp:731-805 ; rendu Shifter.cpp:925-960 ; NEOST_V2 (A/B) Machine.cpp:158-163 | reste S/M | Reste ouvert : hardscroll 4 px (inventaire § 2, « V2, restes »). |
| 25 ✅ | **Porté** (branche STE le 2026-07-08, steBorderFlag_ le 2026-10-18) — **V1 — branche STE de la Glue + bSteBorderFlag 336 px** (Cuddly « trop propre » en STE, Obsession/Pacemaker faux ; faisable maintenant que WS3 est tranché) | video.c:2440-2652, 1520/1538/1603, 5826-5916 | Référence historique Shifter.cpp:977-981 ; mise à jour 2026-10-18 : bSteBorderFlag porté (steBorderFlag_), cinq étalons oracle | clos | Clos. |
| 26 | **Ombres PSG impaires déjà portées**, requalifiées le 2026-10-19 : byte/movep acceptés, impairs mot/long ignorés | psg.c:577-606, 637-666 | `Bus::wrPsg`, tests/psg_odd_write_logic.cpp | clos | Clos. |

### P4 — Audio (registre-exactitude et chaîne STE)

| # | Candidat | Hatari | NeoST | Effort | Verdict (2026-11-12) |
|---|---|---|---|---|---|
| 27 ✅ | **Fait** (horloge YM_250_HZ = 250663 le 2026-08-07 ; écritures appliquées au pas 250 kHz le 2026-09-02) — **Horloge YM réelle (YM_Freq/8) + rejeu à la frontière 250 kHz** (pitch −4,6/−2,3 cents, écart ST↔STE absent, phase R13 sync-buzzer au grain 4 µs vs 21 µs) | sound.c:234-235, 864-878, 1243 ; psg.c:342 ; clocks_timings.c:211,243 | YM2149.hpp:288 ; YM2149.cpp:386-397 | clos | Clos. |
| 28 ✅ | **BUSY via gpipSetLine porté le 2026-10-19** ; XSINT DDR déjà correct, TAI indépendant du DDR GPIO | mfp.c:1180-1219 ; psg.c:390 ; MC68HC901 §5.1 | `Mfp::setBusyLine/gpipSetLine/setXsintLine` | — | Clos. |
| 29 | **MIDI TDR/TSR porté (2026-10-18)** | midi.c:47,256-271,303-306 ; acia.c Clock_TX | MidiAcia.cpp (advanceTx) | porté TX huit formats, /1,/16,/64 et suspension/reprise BREAK ; RX hôte 31250 8N1, waveform ouverte | Porté ; RX hôte 8N1 (inventaire § 6). |
| 30 | **Compteur DMA sans refill à la lecture déjà porté**, requalifié le 2026-10-19 | dmaSnd.c:748-761, 361, 393, 733/740 | `DmaSound::liveCounter/onHbl/fifoPull` | clos | Clos ; confronté le 2026-09-02 (`dmasnd_poll`). |
| 31 | **Masque haut $3F DMA son déjà porté**, requalifié le 2026-10-19 pour les profils RAM ≤4 Mo | dmaSnd.c:867, 909, 948, m68000.c:958-971 | `DmaSound::write8` | clos | Clos. |
| 32 | **Lecture $FF8900 zéro et scratch $FF8920 déjà portés**, requalifiés le 2026-10-19 ; scratch ajouté au snapshot v42 | dmaSnd.c:768-771, ioMemTabSTE.c:162 | `DmaSound::read8/write8/serialize` | clos | Clos ; `$FF8920` revu le 2026-11-12 (octet relu 0). |
| 33 ✅ | **Fait le 2026-08-07** (kDmaGain négatif ; HPF du mix STE `DmaSound::applyHpfStereo`) — **Signe ×−1 du canal DMA + HPF subsonique sur le MIX** (phase relative YM/DMA inversée en mixing=1 ; DC du DMA jamais filtré — deux corrections locales pour le diff WAV bit-à-bit vs oracle) | dmaSnd.c:524-534, 557-565 ; 699, 706 | DmaSound.cpp:33, 412-415 ; YM2149.hpp:167 (`setHpfBypass`) | clos | Clos. |
| 34 ✅ | **Fait le 2026-08-07** (plateaux Savinkoff du 1ᵉʳ ordre) — **Filtre LMC1992 exact (plateaux Savinkoff)** (118.276/8438.756 Hz 1ᵉʳ ordre vs RBJ 200/8000 Hz 2ᵉ ordre — timbre dès bass/treble ≠ 0 dB) | dmaSnd.c:89-92, 1262-1462, 1418-1419 | DmaSound.cpp:331-360 | clos | Clos. |
| 35 | **DC-hold du dernier échantillon DMA** (offset tenu + mute YM si mixing≠1 après trame one-shot ; à trancher avant tout diff WAV sérieux) | dmaSnd.c:547-574, 150-151 | DmaSound.cpp:656-660, 678, 724 ; choix DmaSound.hpp:58-60 | S | Ouvert : écart assumé « DMA à l'arrêt hors mixage 1 », inventaire § 5. |
| 36 | **BL-R : porté le 2026-10-18**, miroir brut des registres mot | blitter.c (CheckAccess_Byte), ioMem.c | Blitter.cpp (ioRaw_) | clos | Clos ; le miroir brut a été retiré le 2026-10-22 (registre vivant, netlist ST-4082). |

### ✗ Candidats réfutés (mémo anti-faux-positifs)

| Candidat | Raison de la réfutation | Verdict (2026-11-12) |
|---|---|---|
| Trancher l'hybride WS1/WS3 | Déjà tranché WS3 dans l'arbre de travail (2026-07-08) : VideoGlue.hpp:66-93 (table wakestate, défaut 3), Machine.cpp:78-92 (kHblOff défaut 0), VBL cohérent. Seule la doc retarde. | Toujours réfuté (inventaire, faux positifs). |
| ~~Activer NEOST_LINELEN par défaut~~ | **Plus une réfutation : FAIT.** Le candidat avait été écarté (Cuddly résolu 250/250 LINELEN OFF, hypothèse « lignes 508 » éliminée — MOIRA_WINUAE_CONVERGENCE.md), puis le canal a été activé par défaut au tranchage WS3 (Shifter.cpp:92-95) et NEOST_LINELEN_ATTR le 2026-09-01 (V3 clos, ShifterInternal.hpp:31). | Fait (cf. ligne). |
| Atomicité MOT des écritures MMIO | Les 3 cas concrets sont déjà garantis (palette fusionnée, microwire octet bas, compteurs = SIZE_BYTE chez Hatari même) ; aucun écart restant établi. | Toujours réfuté (inventaire, faux positifs). |
| FDC_UpdateAll avant chaque accès registre | L'équivalent existe aux points de consommation (indexCheckUpdate en tête de chaque événement + lecture STR) ; moteur arrêté = no-op ; aucune impulsion stale possible. | Toujours réfuté (inventaire, faux positifs). |
| PSG_WaitState cas MOVEM | Omission volontaire documentée ×3 ; les mesures psg.c:143-150 sont des bancs matériels sans logiciel réel — tous les loaders cités utilisent movep, traité identiquement. | Toujours réfuté (inventaire, faux positifs). |
| PRE_START blitter 4+4 vs 4 | Artefact de point de référence : les deux chemins Hatari convergent vers fin d'accès + 4 ; NeoST donne fin d'accès + 2 sur les DEUX chemins (aucune asymétrie start/restart). | Toujours réfuté (inventaire, faux positifs). |
| MegaSTE : hits cache comptés | Hatari compte LUI AUSSI les hits (m68000.c:1444-1676, mem_access_before/after inconditionnels) — NeoST reproduit exactement l'oracle. | Toujours réfuté (inventaire, faux positifs). |
| CopyScreenLineMono « sans Glue » (mapping) | La fonction Hatari n'a AUCUNE gestion de tricks non plus (« no overscan ») — iso-comportement, pas un écart. | Toujours réfuté (inventaire, faux positifs). |
