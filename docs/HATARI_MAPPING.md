# Cartographie Hatari ↔ NeoST — audit de correspondance

**But** : inventaire maître de la correspondance code-à-code entre `extern/hatari/src`
(référence comparative) et NeoST, pour **préparer les travaux de précision** :
chaque mécanisme Hatari est classé porté / partiel / absent / divergent-volontaire, avec
références `fichier:ligne` des deux côtés. Complète [`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md)
(inventaire des écarts) par la vue « où vit quoi ».

**État au 2026-07-08**, sur l'**arbre de travail** (refactor dmaSnd + tranchage WS3
**non commités** — plusieurs références de lignes diffèrent de HEAD, notamment
`DmaSound.cpp`, `Shifter.cpp:102-149`, `Machine.cpp:49-63`). **Références NeoST resynchronisées le 2026-10-20** (§1-4 au moins) : vidéo éclatée en `VideoGlue.cpp`/`VideoGlue.hpp`/`VideoCounter.cpp`/`ShifterInternal.hpp`, dispatch MMIO par table dans `Bus.cpp` ; les références du sondage ci-dessous restent celles du 2026-07-08.

**Méthode** : 10 agents de cartographie (un par sous-système) + **vérification
adversariale** de chaque écart et candidat (relecture indépendante des deux sources).
Exception : le sous-système 10 (GEMDOS/cartouche/RTC/SCU/joysticks) a perdu son agent
deux fois (erreur API puis plafond de dépense) et a été **cartographié manuellement**
le 2026-07-08 par relecture directe des deux côtés, **sans contre-vérification**.
Légende des marqueurs :

- **⚠** = écart **CONFIRMÉ** par la vérification adversariale (les deux côtés relus).
- **✗** = affirmation **RÉFUTÉE** (avec la raison — précieux contre les faux positifs récurrents).
- Sans marqueur = cartographié mais non soumis à contre-vérification.

**Sondage préalable** (références tirées au hasard, toutes confirmées) : `Bus.cpp:297`
(buildIoFault), `Mfp.cpp:268` (×31333/9600), `fdc.c:2370` (−PendingCyclesOver),
`Blitter.cpp:86` (read8 vif), `Shifter.cpp:102` (WS3 tranché), `Machine.cpp:266`
(VC_RESTART). **Non confirmé** : le verdict sur `FDC_DiskControllerStatus_ReadWord`
(fdc.c:4760) n'a pas été rendu (agent de vérification perdu) — l'entrée reste « partiel »
sans ⚠. Nota : de nombreuses références NeoST dérivent de ±5-40 lignes après éditions
(signalé au cas par cas par la vérification) ; les mécanismes désignés restent exacts.

**Bilan chiffré** : 254 correspondances par agents — **165 portées, 48 partielles,
14 absentes, 27 divergences volontaires** — plus **15 manuelles** (§10). 51 candidats
précision proposés, **43 confirmés** (36 uniques après déduplication), **8 réfutés**.

---

## 1. Bus & mémoire (ioMem.c, cpu/memory.c, stMemory.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| ioMem.c:123-134, 277-394 (IoMem_Init + SetBusErrorRegion) | Bus.cpp:472-558 (spans ST_OK/STE_OK + buildIoFault) | porté | Même modèle : tout $FF8000+ faute par défaut, whitelist par machine ; tables Hatari condensées en spans. |
| ioMemTabST.c:42-195 (IoMemTable_ST) | Bus.cpp:482-490 + 514-533 | porté | Vérifié octet par octet ; blitter absent sur ST simple = fidèle (ioMem.c:367-370). |
| ioMemTabSTE.c:81-291 (IoMemTable_STE) | Bus.cpp:492-502 + 514-533 | porté | $FF8000-800F, scroll fin, FDC 12 o, DMA-son, blitter, joypads, etc. |
| ioMem.c:156-160 (FixVoidAccessForST) | Bus.cpp:536-537 | porté | Ricoh : $FF820F/$FF860F void. |
| ioMem.c:172-196 (FixVoidAccessForMegaST) | Bus.cpp:538-542 | porté | Chipset IMP ; reste la VALEUR $8A3E (0x00 vs 0xFF), déjà consigné 5ᵉ passe. |
| ioMem.c:202-236 (FixAccessForMegaSTE) | Bus.cpp:543-554 | porté | SCU impairs, cache $8E20-23, SCC, densité ; extension FPU $FFFA40 documentée (Bus.hpp:395-401). |
| ioMem.c:386-393 (shadow PSG) | Bus.cpp:532-533, 847-863 | porté | Miroir + dispatch réel + wait-state (ex-BU1, corrigé). |
| ioMem.c:373-380 (RTC void non-Mega) | Bus.cpp:529, 942-943 + Bus.hpp:460 (claimsRtc), Bus.cpp:1086-1090 | porté | Dispatch gaté machineIsMega, sinon 0xFF. |
| ioMem.c:442-802, 835-867 (bget/wget + nBusErrorAccesses) | Bus.hpp:228-236 + Bus.cpp:629-664 (busFaultN/busFaultNSlow) + Cpu68k.cpp:526-529 | porté | Whitelist « tous les octets fautent », étage superviseur, banque de départ. Filler $FF des pairs MFP pris en charge dans le routage Bus ; banc de qualification ajouté le 2026-10-19, candidat 23 clos. |
| ioMem.c:53 (nIoMemAccessSize) | Bus.hpp:246-248, 554 + Bus.cpp:778-828 (ioAccessWidth_) | porté | Largeur exposée aux puces (FDC, joypads) ; fuite blitter corrigée (BUS-LEAK). |
| ioMem.c:61-84 (IoAccessInstrCount) | — | volontaire | Heuristique du mode non-CE Hatari ; Moira est cycle-exact. |
| ioMem.c:719-728 (buffer écrit AVANT handlers, atomicité mot) | Bus.cpp:778-828 (dispatch octet par octet) | volontaire ✗ | Candidat « audit atomicité » **RÉFUTÉ** : les 3 cas concrets sont déjà garantis — palette fusionnée en 1 ColorWrite/mot (Shifter.cpp:645-657), microwire déclenché sur l'octet BAS après pose du haut (DmaSound.cpp:598-628), compteurs $8205/07/09 = SIZE_BYTE chez Hatari même (ioMemTabSTE.c:92-96). Blitter atomique (Bus.cpp:803-811, 820-824). |
| memory.c:494-547, 1749-1832 (BusErrMem_bank, map_banks) | Bus.cpp:560-584 (busFault hors-IO) | porté | Trous $400000-$F9FFFF, $FF0000-$FF7FFF ; cartouche jamais. |
| memory.c:676-896 (SysMem_*) | Bus.cpp:632-648 + Blitter.cpp:98-103 | porté | Écriture $0-$7 et user <$800 fautent (CPU) ; blitter ignore sans fauter. |
| memory.c:899-926 (VoidMem → regs.db) | Bus.hpp:327-333 + Bus.cpp:380-387 + Cpu68k.cpp:498-499 (cpuDb) | porté | Latch CPU seulement (fetches vidéo/DMA exclus). |
| memory.c:1022-1076 + cart.c:117 (ROMmem, fenêtres) | Bus.hpp:191-199 + Bus.cpp:389-394, 439-444, 638-642 | porté | Cartouche vide = 0xFF exactement cart.c:117 (le faux positif de la 2ᵉ passe est clos). |
| memory.c:1622-1631 (carve-out void $40000-$7FFFF si MMU 128K/2048K) | Bus.cpp (mmuTranslate) | porté | Présent depuis le 2026-08-13 ; ancienne mention « absent » périmée, revérifiée le 2026-10-18. ST/Mega ST uniquement, STE ignore les bits de banque 1. Garde : tests/mmu_logic.cpp. |
| stMemory.c:851-900 (MMU_ConfToBank + MMU_Size) | Bus.cpp:215-218, 263-271 | porté | bank1 bits 0-1 sur ST/Mega ST seulement, conf 11 → 0. |
| stMemory.c:1052-1308 (Translate_Addr STF/STE) | Bus.cpp:233-257 + 263-298 | porté | Masques RAS/CAS vérifiés terme à terme (9 cas STF). |
| stMemory.c:1005-1031 (RAM_SetBankSize) | Bus.cpp:222-235 (ramBanks) + `memConfigForBytes` (MachineType.hpp) | **complet** (2026-10-20) | 2176 (2M+128K, $FF8001=$08) et 2560 Ko (2M+512K, $09) ajoutés, exposés par `parseRamBytes` (`2176k`, `2560k`/`2.5m`). ⚠ STE/Mega STE : banque 1 calquée sur la banque 0 comme Hatari (TOS 2.06 → phystop $400000, config non reconnue — cohérent avec FAQ comp.sys.atari.st §3.1.1 / SIMMFIX ; MAME `stmmu.cpp` contredit) ; $FF8001 bits 0-1 et banque 1 de 128 Ko à confirmer sur vrai STE. |
| stMemory.c:908-940 ($FF8001 R/W) | MmuGlue.hpp:26,32 lu live par mmuTranslate | porté | Pas de remap : relu à chaque accès, effet identique. |
| stMemory.c:93-102 (STMemory_Reset : $FF8001=0 à froid) | Machine.hpp (hardReset) | porté au reset | hardReset remet 0 ; reset chaud préserve. Constructeur/reconfigure préremplissent encore la configuration RAM (divergence assumée avant le TOS). Vérifié le 2026-10-18 ; tests/mmu_logic.cpp couvre chaud/froid sur les quatre profils. |
| stMemory.c:558-571, 724-768 (DMA Read/Write + CheckAddr) | Bus.cpp:730-753 (dmaRead8/dmaWrite8) | porté | Zone fautive lit 0x00, écriture perdue, jamais d'exception. |
| stMemory.c:532-592 (STAddrToPointer/CheckAreaType) | Bus.cpp:351-361 (hostRamPtr) | porté | Test de contiguïté plus strict, repli octet par octet. |
| stMemory.c:250-252 (miroir vecteurs ROM $0/$4) | Bus.cpp:772-775 (seedResetVectors) | porté | Le reste de SetDefaultConfig = fastboot, volontairement non porté. |
| cpu/debug get_iword_debug | Bus.cpp:757-768 + Bus.hpp:182 (peek8/16) | porté | MMIO/trous → 0xFF sans dispatch ni wait state. |
| m68000.c (MegaSTE_Cache_*) + ioMemTabSTE.c:39-52 | `Bus::megaSteCache*`, `NeostMoira::willAccessBus/readMste16Mhz` | porté | 8192 lignes tag+mot ; décision hit/miss avant les cycles, alignement RAM au début et donnée du hit conservée pendant DMA. Onze sondes CPU/cache strictes, global blitter/IRQ à 16 MHz encore ouvert. |
| fdc.c:4688+ (M68000_WaitState(4) sur $FF8604/06) | Fdc.cpp:1262-1269 (`Fdc::registerWait`), appelé Fdc.cpp:2782, 2851, 2882 | porté | Ex-écart confirmé, désormais porté : 4 cycles CPU (2 à 16 MHz) sur la lecture $8604 hors SCREG, l'écriture $8604 et l'écriture $8606 (densité $860E/F Mega STE comprise). Asymétrie Hatari respectée : rien sur read $8606 ni $8609/0B/0D. PSG/MFP/ACIA facturés à Bus.cpp:848/896/916. |
| ioMem.c:523-527, 590-594 (garde wrap $FFFFFE) | Bus.cpp:778-828 (wrap via ADDR_MASK) | volontaire | Cas dégénéré sans logiciel connu. |

## 2. Vidéo / Shifter / Glue (video.c, spec512.c, conv_st.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| video.c:810/904 (Video_Reset + Reset_Glue) | Shifter.cpp:156-215 (reset) | porté | Palette non touchée, différés annulés — conforme. |
| video.c (InterruptHandler_VBL → ClearOnVBL → ResetShifterTimings) | Machine::onVbl / scheduleFrameEvents + Shifter::frameTimingChanged | porté | Géométrie relue au signal VBL (X=64/68) si elle change depuis X=0 ; évite une fausse trame mono sur une impulsion de bordure Cuddly Fullscreen. Les événements futurs et frameEnd sont réarmés, sans doubler la VBL. Géométrie mid-trame = canal LINELEN (ON par défaut). |
| video.c:2197-2438, 2667-2991 (Update_Glue_State, branche STF) | VideoGlue.cpp:442-719 (updateGlueState) | porté | Quasi ligne-à-ligne, glueSelfTest (VideoGlue.cpp:1011). Branche STE désormais PORTÉE (VideoGlue.cpp:542-616) : fenêtres Preload_Start_*, LEFT_OFF_2_STE, table kSte (VideoGlue.hpp:131-147). |
| video.c:3523 (StartHBL) | VideoGlue.cpp:409-434 (startHBL) | porté | Défauts par res/freq, DisplayStartCycle pré-positionné respecté. |
| video.c:3398/3882 (EndHBL + CopyScreenLineColor) | VideoGlue.cpp:807-1006 + Shifter.cpp:762-1074 (replayGlue/renderGlueFrame) | volontaire | Architecture différente : rendu live par-ligne + rejeu Glue fin de trame. BORDERMASK STE (LEFT_OFF_2_STE, Shifter.cpp:861 ; VideoGlue.cpp:263), lignes med par ligne (Shifter.cpp:873-878, 1031-1050) et bSteBorderFlag (steBorderFlag_, Shifter.cpp:859) désormais rendus. |
| video.c (`Video_CopyScreenLineColor`, masques NO_DE/BLANK/STOP_MIDDLE) | `Shifter::finishFrame` → `renderGlueFrame` | porté | Correction 2026-10-19 : le replay est aussi déclenché sans palette ni extension de bordure. Onze contrôles Glue et huit sondes ST/STE ; EMPTY_LINE/BLANK_LINE ne sont plus produits au pin. Palette 0 comparative, couleur physique du blanking non mesurée. |
| video.c (`Video_Update_Glue_State`, NO_SYNC/SYNC_HIGH et annulations, `BlankLines`) | `Shifter::updateGlueState`, `startHBL`, `glueLineBytes`, `liveLineDisplayed`, `renderGlueFrame` | porté et gardé | Qualification comparative 2026-10-19 : huit sondes ST/STE et 52 contrôles Glue ; ligne NO_DE/NO_COUNT, pas d'avance vidéo ni de tic Timer B, extension basse et annulation. Aucune modification d'exécution ; signaux électriques non mesurés. |
| video.c:3798 (CopyScreenLineMono) | Shifter.cpp (renderLine hi + early-return) | porté **✗** | Le « partiel : pas de Glue/bordure en mono » est **RÉFUTÉ** : CopyScreenLineMono d'Hatari n'a AUCUNE gestion de tricks non plus (« no overscan », video.c:3497-3501) ; NeoST porte tout ce qu'elle fait réellement (avance +2 mono, LineWidth/HSCROLL différés) — iso-comportement. |
| video.c:3238-3387 (InterruptHandler_HBL) | Machine.cpp:570-603 (onHbl) + scheduleFrameEvents | porté | HBL/ligne, raise niveau 2, DmaSnd HBL, broche pré-armée. Position : **tranché WS3 dans l'arbre** (kHblOff défaut 0 → 512). |
| video.c:2231-2232, 2849-2877, 3336 (HBL_Pos/nCyclesPerLine chaîné) | VideoGlue.cpp:478-549, 143 + Machine.cpp:177-189, 587-592, 699 | porté | Canal COMPLET porté, deux verrous ON par défaut : `NEOST_LINELEN` (Machine, Shifter.cpp:92-95) et `NEOST_LINELEN_ATTR` (attribution à la grille réelle côté Shifter, ShifterInternal.hpp:30-33 ; V3 clos le 2026-09-01). Historique : Le verrou est en DEUX moitiés de défaut différent : Machine (reprogrammation HBL + cumul `lineCarry_`) est **ON par défaut**, les 3 sites Shifter (attribution des écritures + lecture du compteur sur la grille réelle) sont **OFF par défaut** — c'est cet hybride qui est validé au pixel. `NEOST_LINELEN=0` désactive les deux, `=1` les active les deux (les sites Shifter testaient la seule PRÉSENCE de la variable jusqu'au 2026-07-29 : `=0` en activait donc la moitié Shifter, et l'A/B mesurait un hybride inverse). L'activation complète par défaut, jadis réfutée comme candidat (voir ✗ ci-dessous), est effective depuis le 2026-09-01. |
| video.c:3595/3195/4850 + 2884-2888 (EndLine/TimerB_GetPosFromDE) | Machine.cpp:510-568 (onTimerB) + VideoGlue.cpp:45-82 (timerBFrameCycleForLine/timerBPosForLine) + Shifter.hpp:221-228 | porté | Ex-candidat n°8 : le tic est repositionné au DE réel de la ligne Glue (displayStart/EndCycle + 24, port de TimerB_GetPosFromDE) ; onTimerB se replanifie si la cible a reculé. Repli sur timerBLinePos hors trame à tricks. |
| video.c:4926/4875 (InterruptHandler_VBL/StartInterrupts) | Machine.cpp:605-637 (onVbl) + runFrame | porté | Offsets par wakestate cohérents (Machine.cpp:440-446). |
| video.c:1379-1600 (Video_CalculateAddress) | VideoCounter.cpp:202-417 (videoCounter) | porté | Reconstruction ds/CurSize, prefetch −16/+8, restart, bSteBorderFlag (VideoCounter.cpp:345-365, 436). Manquent les lignes med (hors LEFT_OFF_2_STE_MED). |
| video.c:5108 (ScreenCounter_ReadByte) | Shifter.cpp:1244-1281 | porté | vcDelayedOffset_ reflété ; wait défaut 0 (RAM_SLOT fournit le +2). |
| video.c:5144-5262 (ScreenCounter_WriteByte) | `VideoCounter.cpp` (`Shifter::writeVideoCounterByte`) | exceptions logicielles non portées | E605/Tekila : ajustements conditionnés aux opcodes, explicitement FIXME/partiels chez Hatari (+6 « or -2? », +2 avec cas 256→0). Audit 2026-10-19 : aucun défaut matériel NeoST démontré ; port différé jusqu’à reproduction des programmes. |
| video.c:5080/5252 (ScreenBase R/W) | Shifter.cpp:1329-1352, 1361-1363 + read8 (1235-1240) | porté | STE : haut/milieu effacent $FF820D, bit0 ignoré. |
| video.c:4608 + 3262-3286 (RestartVideoCounter) | VideoCounter.cpp:177-180 + Machine.cpp:385-390, 454-460 (VC_RESTART) | porté | Ligne 310/260, freq relue live. Remise à ON de VBlank au même point portée dans vblankAtLine (2026-10-18). |
| video.c:3037/5241 (Sync_WriteByte/ReadByte) | Shifter.cpp:1354 + recordSyncWrite (VideoGlue.cpp:278-361) + read8 (Shifter.cpp:1285) | porté | Filtre « même Freq » inter-trames, wait 4 cyc, datation +2. |
| video.c:1618-1860 (WriteToGlueRes, détections) | VideoGlue.cpp:278-361 (recordSyncWrite) + 731-805 (updateGlueRes) | partiel | Filtre/latch −1 cyc/pixelShift −4 portés ; détections 1637-1753 PORTÉES dans `updateGlueRes` (med-res overscan, LEFT_OFF_MED, variante STE, stab med, scrolls hw 13/9/5/1 px), appliquées au rejeu de fin de trame sans verrou (VideoGlue.cpp:893), pas par la Glue live. Restent non portés : hacks Closure/DOLB (1791-1834) et hardscroll 4 px (1755-1789). `NEOST_V2` ne gate plus que le raccourcissement HBL. |
| video.c:1888-2006 (WriteToShifterRes) + ioMemTabST.c:71 / ioMemTabSTE.c:120 ($FF8261) | `Shifter::writeShifterRes` (VideoGlue.cpp) + Shifter.cpp write8/read8 ($FF8260/61) | **porté** (2026-10-20) | Écriture octet $FF8261 = résolution Shifter seule (Glue intacte) ; `move.w` vers $FF8260 : octet bas ignoré ; lectures = valeur Shifter. res 3 : stop ≥ 64, retour ≥ 72 et ≤ 32 cycles → lignes suivantes décalées 0/4/8/12 px, compteur −0/−2/−4/−6 o ; ligne restée stoppée = noire. Incertitudes : noir vs couleur 0, lecture octet $FF8261, STE, persistance inter-trame. Non modélisés : pixels noirs d'un stop bref, res Shifter≠Glue (0/1/2) au rendu, géométrie de trame à res 3 via $FF8260. Whitelist à Bus.cpp:514. |
| video.c:5373 (ColorReg_WriteWord) + conv_st.c | Shifter.cpp:1415-1434 + stColorToArgb (Shifter.cpp:217-233) | porté | Masque $777/$FFF, expansion 4→8 bits (demi-marche STE). |
| m68000.c (M68000_SyncCpuBus) | Shifter.cpp:670-676 (syncCpuBus) | porté | Alignement frontière 4 cyc de la fin d'accès — base du 512 cyc/ligne spec512. |
| spec512.c:152-226 (StoreCyclePalette) | Shifter.cpp:627-663 (recordColorWrite) | volontaire | Datation live Moira au cycle. Seuil spec512 aligné sur Hatari : 1 (kSpec512Threshold, Shifter.cpp:108), ex-candidat n°11 clos. |
| spec512.c:133-335 (StartVBL/StartScanLine/UpdatePaletteSpan) | Shifter.cpp:612-663, 762-1074 + kSpec512AlignCyc=−25 (Shifter.cpp:36) | porté | Modèle spans 4 cyc exact ; validé 10/10 images diapo à 0 px. |
| video.c:3676-3760 (HBLPalettes/StoreResolution) | renderLine + colorWrites_ rejoués | porté | Seuil 1 comme Hatari (Shifter.cpp:108) : toute écriture palette déclenche le re-rendu au cycle ; une trame sans écriture garde sa palette constante. |
| video.c:5826-5916 + 5813 (HorScroll) | Shifter.cpp:1388-1412, 1305-1310, 422-439 | partiel | Immédiat/différé, prefetch, octet brut relu : portés. Mise à jour 2026-10-18 : latch 336 px porté dans Shifter/VideoCounter/VideoGlue, cinq étalons oracle scroll_336*. |
| video.c:5326 (LineWidth_WriteByte) | Shifter.cpp:1368-1376 + endVideoLine (VideoCounter.cpp:52-134) | porté | Immédiat si DE pas fini, sinon différé ; stride partout. |
| video.c:1299/1283/1204 (GetCyclesSinceVbl/ConvertPosition) | Machine.cpp:124-127 + offsets read −6 / write +2 (ShifterInternal.hpp:37-41) | porté | Paire d'offsets fidèles §8 — ne bouger QUE par paire. ConvertPosition à longueur réelle = NEOST_LINELEN_ATTR, ON par défaut depuis le 2026-09-01 (VideoCounter.cpp:249). |
| video.c:598-600, 3443-3487 (signal VBlank/VSync) | VideoGlue.cpp (vblankAtLine) | porté | Comparateurs 50/60 Hz, VSYNC, palette 0 et suppression du stride ; deux étalons oracle (2026-10-18). |
| video.c:927-1074, 624 (VideoTimings WS1-4) | VideoGlue.hpp:65-147 (glue::wakestate, kWsInc, kStf/kSte) + Machine.cpp:87-92, 441-442 | porté **✗** | L'« hybride WS1/WS3 à trancher » est **RÉFUTÉ/PÉRIMÉ** : tranché WS3 dans l'arbre de travail (2026-07-08) — wakestate() défaut 3, kWsInc +1, kHblOff défaut 0 (HBL 512), VBL 64/60 cohérent. HATARI_DIVERGENCES.md est à jour (ligne « WS ✅ »). |
| (esprit CopyScreenLineColor : copie au fil du faisceau) | VideoCounter.cpp:86-101 (lineSnap_) + Shifter.cpp:1000-1006 | partiel | Capture datée au faisceau sur les trames à écritures freq/res OU palette (seuil spec512 = 1) ; une trame sans aucune de ces écritures relit la RAM de fin de trame. Hatari copie TOUJOURS par ligne. |

**✗ Réfutés (vidéo)** :
- ~~*Activer NEOST_LINELEN par défaut*~~ : réfutation **caduque**, candidat **FAIT**. L'argument
  d'origine (Cuddly résolu 250/250 LINELEN OFF, hypothèse « lignes 508 » éliminée —
  MOIRA_WINUAE_CONVERGENCE.md) valait tant que le verrou était un choix optionnel ; le canal
  complet est actif par défaut depuis le tranchage WS3 (`NEOST_LINELEN`, Shifter.cpp:92-95) et
  `NEOST_LINELEN_ATTR` depuis le 2026-09-01 (V3 clos, ShifterInternal.hpp:31).
- *Trancher l'hybride WS1/WS3* : déjà tranché WS3 dans l'arbre (voir table).

## 3. MFP 68901 (mfp.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| mfp.c:519-569 (MFP_Reset) | Mfp.cpp:80-112 (resetChip) + 114-160 (reset) + Machine.hpp:102 | porté | GPIP = 0x00 comme Hatari, mais entrées recalculées à la lecture : repos $F9 (ST) / $F1 (avec blitter) contre $B1 chez Hatari — bits 6 (RI) et 3 (GPU_DONE) tirés haut sur le matériel (HATARI_DIVERGENCES § Cas où NeoST améliore Hatari). |
| mfp.c:390-393, 431-451 + cycInt.c:26-45 (conversions, unités internes) | Mfp.cpp:629-650, 769-801 | porté (2026-08-14) | Période et échéance en 1/256 cycle CPU (`timerPeriodSubCycles`, `timerDueSub_`), comme CYCINT_SHIFT=8 d'Hatari. Le rechargement repart de l'échéance fractionnaire. Candidat n°4 clos. |
| mfp.c:681-692 (MFP_UpdateTimers avant CHAQUE accès registre) | `Mfp::updateTimers` Mfp.cpp:63-70 (en tête de read8 :324 et write8 :418) + `Scheduler::runMfpTimersTo` | porté (timers délai) | Défaut ON (NEOST_MFP_UPDTIMERS). TIMER_B event-count est exclu à dessein (Scheduler.hpp). Le repli modulo reste dans readTimerData (Mfp.cpp:735-741). Candidat n°5 clos pour A/B/C/D en mode délai. |
| mfp.c:736-799 (GetIRQ_CPU + délai 4 cyc) | Mfp.cpp:1234-1238, 1176-1192 | porté | Front visible à irqTime_+4 via événement MFP_IRQ. |
| mfp.c:893-913 (ProcessIRQ frontière) | Mfp.cpp:20-21, 1181-1189 (g_mfpExact bit1) | porté | Défaut NEOST_MFP_EXACT=3. |
| mfp.c:946-983 (MFP_UpdateIRQ) | Mfp.cpp:1165-1193 (updateIrq) | porté | Antidatage timers, retombée si bloquée — 1:1. |
| mfp.c:993-1071 (InterruptRequest/CheckPending) | Mfp.cpp:1202-1229 | porté | Balayage 15..0, chronologie, in-service. |
| mfp.c:1088-1126 (InputOnChannel + élection différée) | Mfp.cpp:1123-1157 (raise/raiseAt, flushIrqUpdate) | porté | Élection groupée : raiseAt ne fait que marquer `irqUpdateNeeded_` ; flushIrqUpdate et updateTimers élisent ensuite (défaut NEOST_MFP_BATCH=1). Candidat n°12 clos. |
| mfp.c:812-857 (ProcessIACK) | Mfp.cpp:1244-1254 + Cpu68k readIrqUserVector | porté | NeoST plus fidèle (spurious $60 sur vecteur <0 — TODO chez Hatari). |
| mfp.c:1142-1170 (GPIP_Update_Interrupt) | Mfp.cpp:1101-1116 | porté | Corrigé bc15a67 (M1). |
| mfp.c:1180-1219 + 1884-1935 (Set_Line_Input, GPIP7, ACIA wire-OR) | Mfp.hpp:555-560 + Mfp.cpp:1062-1094 | porté | Appelants convertis (Ikbd, MidiAcia, Fdc, Blitter). |
| psg.c:388-390 (BUSY Centronics → GPIP0) | `Mfp::setBusyLine/gpipSetLine` | porté | Détection du front GPIP0 qualifiée AER/DDR/IER, niveau identique sans effet ; IRQ et état de ligne persistés. |
| dmaSnd.c:308 (XSINT → LINE7), mfp.c:1201 | `Mfp::setXsintLine` | déjà porté, revérifié | DDR7 qualifie l’IRQ GPIP7, XOR avec le moniteur couleur préservé. TAI reste indépendant du DDR GPIO. |
| RS232 CTS/DCD/RI → GPIP 2/1/6 | Mfp.hpp:387-389 | volontaire | Fixture de bouclage ; RI au repos DÉSASSERTÉE (bit 6 = 1, MC1489 entrée ouverte), CTS/DCD actives. |
| mfp.c:1228-1261 (TimerA_Set_Line_Input) | Mfp.cpp:852-869 | porté | TACR==8 strict, wrap 0→255, piloté par XSINT STE. |
| mfp.c:1269-1291 (TimerA_EventCount) | — | volontaire | Falcon uniquement, hors périmètre. |
| mfp.c:1297-1322 + video.c:3659 (TimerB_EventCount) | Mfp.cpp:829-850 + Machine.cpp:510-568 | porté | Antidatation du tic ; ligne AFFICHÉE via Glue live. |
| mfp.c:1352-1545 (StartTimer, PendingCyclesOver, modulo) | Mfp.cpp:629-650, 758-827 | porté | Ancrage firingDue ≡ AddRelativeInterruptWithOffset. |
| mfp.c:1386-1393 (jitter LX, PC codé en dur) | — | volontaire | NeoST résout LX par le vrai beam-sync. |
| mfp.c:1448-1453 (TimerBEventCountCycleStart) | — (équivalent par construction) | porté | TBCR relu live au dispatch de chaque ligne. |
| mfp.c:1552-1633 (ReadTimer, règle « <1 unité au stop ») | Mfp.cpp:711-756, 679-704 | porté | + repli modulo (Captain Blood). |
| mfp.c:2552-2594 (hack TBDR event-count) | Mfp.cpp:361-366 | **partiel ⚠** | CONFIRMÉ : hack requis seulement si !CE chez Hatari. NeoST garde une fenêtre ≤1 instruction, car TIMER_B event-count est exclu d'updateTimers. |
| mfp.c (M68000_WaitState(4) tous handlers) | Bus.cpp:892-896, 904 + Cpu68k.cpp:1147-1149 | porté | 4 cyc/accès sans dédup, octet impair seulement. |
| mfp.c:1957-2028 (GPIP_ReadByte) | Mfp.cpp:326-336 | porté | Sorties = verrou, entrées calculées live. |
| mfp.c:2700-2731 (GPIP_WriteByte) | Mfp.cpp:420 | porté | Équivalence démontrée en commentaire (jamais de front possible). |
| mfp.c:2742-2812 (ActiveEdge : fronts + reposition Timer B bit3) | `Mfp::write8`, `Machine::retargetTimerB` | porté | Avant mutation AER3, anciens fronts échus servis à leur date ; nouveau front strictement futur de la ligne courante, sinon ligne suivante. Grille Glue et masquage vertical conservés ; test synthétique, jeu Seven Gates non validé ici. |
| mfp.c:2856-3099 (IER/IPR/ISR/IMR) | Mfp.cpp:436-443 | porté | Clear-only, updateIrq daté au cycle d'écriture. |
| mfp.c:3106-3144 (VectorReg) | Mfp.cpp:445-454 | porté | SEI→AEI vide ISR ; nuance bénigne (updateIrq à chaque écriture). |
| mfp.c:3150-3325 (TimerCtrl) | Mfp.cpp:460-463, 658-673 | porté | « PATCH TIMER D » non porté = volontaire. |
| mfp.c:3331-3484 (TimerData) | Mfp.cpp:470-482 | porté | Timer qui court : recharge seule ; TDDR→updateSerialConfig. |
| rs232.c (USART, tty hôte) | Mfp.cpp:165-319, 370-399, 483-552, 557+ | volontaire | Config effective, TX/RX cadencés au débit (SERIAL_TX/SERIAL_RX) + bouclage ; pas de tty hôte (assumé). |
| mfp.c (2ᵉ MFP TT, daisy-chain) | — | volontaire | Pas de TT dans NeoST. |
| ioMemTabST.c:117-148 (impairs, $FFFA31-3F void) | Bus.cpp:892, 903 (+ 527) | porté | Conforme au piège DEV.md. |

**API Bus et miroirs 24 bits (2026-10-19)** : `Bus::write16/write32`
normalisent l’adresse avant le raccourci blitter atomique. CPU Moira normalise
déjà chaque cycle ; correction du contrat Bus direct/outils, sans nouveau défaut
CPU revendiqué. Masques de largeur MMIO et rebouclage en RAM conservés.

## 4. FDC / DMA disque / ACSI (fdc.c, floppy.c, floppies/*, hdc.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| `floppy.c:Floppy_ReadSectors/Floppy_WriteSectors` | `Fdc::readSectorST/writeSectorST` | porté | Validation du secteur sur le média actif, même après changement de lecteur pendant transfert ; RNF sans alias face/piste, délégation STX inchangée. |
| `fdc.c:FDC_InsertFloppy/FDC_EjectFloppy` | `Fdc::loadImage/eject` | porté | Phase INDEX supprimée à l’éjection, réamorcée au montage du lecteur actif ; lecteur non sélectionné sans effet. FORCE INTERRUPT et IRQ forcée revérifiés. |
| fdc.c:2307-2372 (InterruptHandler_Update) | Fdc.cpp (onFdcEvent) | porté | Ancrage firingDue()+stall+delay déjà présent, vérifié le 2026-10-18. Le stall n’est pas absorbé par la compensation du retard. |
| fdc.c:4487+ (ExecuteCommand types I-IV) | Fdc.cpp:2561-2635 | porté | Délais PREPARE identiques. |
| fdc.c (WriteCommandRegister pendant busy) | Fdc.cpp:2866-2873 | porté | Remplacement type-à-type (Overdrive Demos). |
| fdc.c:2563/2732/2908 (Restore/Seek/Step) | Fdc.cpp:2005-2207 | porté | Spin-up 6 IP, butées, VERIFY, densité STX. |
| fdc.c:3066/3311 (Read/WriteSectors) | Fdc.cpp:2212-2378 | porté | Transfert octet-par-octet daté, multi-secteurs, WPRT. |
| fdc.c:3560/3717/3853 (ReadAddress/ReadTrack/WriteTrack) | Fdc.cpp:2383-2556 | porté | Départ à l'index, gate densité $FF860E. |
| MAME `wd_fdc.cpp`, READ_ID_BLOCK_TO_DMA_BYTE ; WD1772 READ ADDRESS | `Fdc::updateReadAddress` | CRC ID corrigé | CRC signalé après les six octets ID, avant completion/IRQ ; CRC de données exclu. Hatari partageait l’omission, correction selon le composant. |
| fdc.c:2519/3991/2415/2447 (moteur/fin de commande) | Fdc.cpp:1977-2000, 1943-1975 | porté | Arrêt 9 IP, spin-up conditionné. |
| fdc.c:2016-2200 (IndexPulse_*) | Fdc.cpp:986-1047 | porté | 1 604 249 cyc/tour, phase PRNG déterministe. |
| fdc.c:5127 (NextSectorID_FdcCycles_ST) | Fdc.cpp:1899-1938 | porté | Mêmes gaps, même retour NO_DRIVE. |
| fdc.c:5240-5470 (Read/WriteSector_ST etc.) | Fdc.cpp:1274-1334 | porté | CRC16 CCITT, writeBack au fil de l'eau. |
| fdc.c:5477 (WriteTrack_ST, TODO Hatari) | Fdc.cpp:1380-1422 | volontaire | NeoST PARSE le flux MFM (mieux que le LOST_DATA d'Hatari), documenté. |
| fdc.c:1311/1370 (FIFO Push/Pull) | Fdc.cpp (billDmaCycles, fifoPush/fifoPull) | porté | Stall 32 cycles bus et report sur l’échéance suivante ; D3 clos depuis 2026-08-26. |
| fdc.c:1233 (ResetDMA) | Fdc.cpp (dmaResetFifo) | porté 2026-10-18 | Préserve le statut d’erreur, annule les octets restants. Les positions de tampon amont sont mortes ; banc tests/fdc_register_logic.cpp. |
| fdc.c:1172 (FDC_Reset) | Fdc.cpp:785-828 | porté | Froid/chaud, porté à la 5ᵉ passe (bc15a67). |
| fdc.c:4760 (DiskControllerStatus_ReadWord) | Fdc.cpp (read8/write8) | porté pour UpdateAll | Index actualisé avant tous les accès WD1772, hors compteur DMA/ACSI (2026-10-18). Garde horloge live avant dispatch dans tests/fdc_selection_logic.cpp ; wait states hors de ce lot. |
| fdc.c:4924/4978 (DmaModeControl/DmaStatus) | Fdc.cpp:2881-2891, 1254-1260 | porté | Toggle bit 8, bits rémanents $8604 (vérifié STF). |
| fdc.c:5026-5067 (ripple-carry adresse DMA ST) | `Fdc::writeDmaAddressByte` | porté 2026-10-18 | Retenue des bits 7/15 sur ST/Mega ST, avant masque d’adresse ; tests/fdc_register_logic.cpp. |
| fdc.c:5090 + m68000.c (DMA_MaskAddressHigh) | Fdc.cpp:43-51, 1197/1216, 1251, 2826-2828 | porté | Bit0 forcé, masque haut selon RAM, adresse relisible. |
| WD1772 WRITE TRACK ; fdc.c FDC_WriteTrack_ST (refus) | `Fdc::writeTrackBuffer` | extension NeoST bornée | Piste ST complète, C/H conformes à tête/face physiques et numéros uniques vérifiés avant copie. Flux non représentable rejeté LOST_DATA sans écraser une autre piste. STX inchangé. |
| fdc.c FDC_WriteSector_ST, floppy.c Floppy_WriteSectors | `Fdc::writeSectorST` | corrigé | Sélection STX→ST en cours de WRITE SECTOR : un ID long ne déborde plus au-delà du secteur logique ST de 512 octets. Taille DMA engagée inchangée. |
| fdc.c:1720 (SetDriveSide PUSH depuis psg.c) | Machine.cpp (setPortASink), Fdc.cpp (refreshDriveSide) | porté | Appelé à chaque écriture R14, indépendamment du loopback série. Ancienne mention pull périmée, revérifiée le 2026-10-18. |
| fdc.c:1881/1932/1100 (densité, TransferByte) | Fdc.cpp:868-938 | porté | DD/HD/ED, porte $FF860E Mega STE, repli DD. |
| fdc.c:2227/2268 (SetIRQ/ClearIRQ) | Fdc.cpp:1092-1125 | porté | IRQ forcée survit au clear, GPIP5 par front. |
| fdc.c:1078-1086 (fastfdc ÷10) | Fdc.cpp:1057-1077 (applyFastFdc) | volontaire | Épargne les délais rotation + neutralisé sur STX (anti-casse protections). |
| floppy.c:839/765 (FindDiskDetails/DoubleCheck) | Fdc.cpp:212-244 | porté | Cracks Xenon 2/Epic/SHO. |
| floppy.c:438/481 + floppy.h:20 (transitions hot-swap) | `Fdc::onVbl/transitionSetState/transitionForceWprt`, `Machine::onVbl` | porté | Première action puis dernière action opposée ; répétition de la première annule la seconde. Depuis le 2026-10-19, 18 vrais VBL par phase, même si PAL/NTSC/mono change. Compteur et échéances VBL sauvegardés v43. Contrat reset FDC conservé, sans équivalence du reset vidéo global revendiquée. |
| floppies/msa.c + dim.c | Fdc.cpp:257-350, 379-388 | porté | Lecture seule, détection par contenu. |
| floppy.c (IsWriteProtected auto) | Fdc.cpp:736-744 | porté | Permissions hôte. |
| floppies/stx.c:989-1377 (BuildStruct etc.) | StxImage.cpp:50-242 (buildSectorsSimple, parse) | porté | Bornes plus strictes qu'Hatari (documenté basse). |
| floppies/stx.c:1466 (NextSectorID_STX) | Fdc.cpp:1529-1566 | volontaire | Port fidèle + CORRECTION densité HD au-delà d'Hatari (NE PAS « corriger »). |
| floppies/stx.c:1606 (ReadSector_STX) | Fdc.cpp:1570-1622 | porté | Fuzzy re-tiré, timing par bloc, overlay écriture. |
| floppies/stx.c:1714 + 364/548 (WriteSector_STX, .wd1772) | Fdc.cpp:1627-1666, 1706-1717 + StxImage.cpp:371/419 | porté | Persistance au fil de l'eau (bénin, documenté). |
| MAME `wd_fdc.cpp`, écriture DAM selon command bit0 | `Fdc::writeSectorStx/readSectorStx/readTrackStx`, `StxImage::SaveSector` | FB/F8 portés pour STX | A8 normal/A9 supprimé, RECTYPE et CRC reconstruite selon DAM. Extension SECT NDAM+marque pour F8 ; anciens SECT = FB. Hatari lit les données mais ignore la métadonnée, perdue à sa réécriture. État v45 inchangé : média hors snapshot, commande déjà sérialisée. Fusion locale sur image de piste décodée qualifiée à la ligne suivante ; DAM persistante ST/MSA/DIM hors périmètre. |
| floppies/stx.c `FDC_ReadTrack_STX`, branche pTrackImageData | `Fdc::readTrackStx` | extension NeoST bornée | Copie du flux décodé, fusion DAM/données/CRC des SECT ; parcours circulaire des champs coupés par index sur moins d’un tour. Ancrage ID exact et aligné dans le dump, synchronisation zéro, données non fuzzy et CRC valides requis. Collisions circulaires, ambiguïtés, champs incomplets et traversée de fin de dump écrêté refusés. Gaps/ID/CRC voisins, taille/timings et source conservés ; aucun moteur MFM général. |
| floppies/stx.c `STX_ReadSector` ; réinterprétation NeoST | `StxImage::reinterpretSaveTrack`, `Fdc::readTrackStx` | extension NeoST corrigée | Marque F8 conservée en RECTYPE et dans READ TRACK reconstruit, CRC régénérée sur la marque choisie. Hatari READ SECTOR rend le flag, mais son READ TRACK reconstruit encore FB ; aucune fidélité physique du flux revendiquée. |
| floppies/stx.c:2027/1863 (Write/ReadTrack_STX) | Fdc.cpp:1674-1701, 1829-1894 + StxImage.cpp:293-344 | volontaire | D1/D2 assumées : flux réinterprété en secteurs lisibles. Vue vide active si aucun secteur exploitable, persistée par TRCK ; pas de résurrection de la piste originale. |
| floppies/stx.c:1823 (ReadAddress_STX) | Fdc.cpp:1720-1733 | porté | Vrai champ ID + CRC réel. |
| hdc.c `HDC_Init/hdSize`, capacité SCSI | `Acsi::mount` | limite NeoST explicite | Compteurs uint32 : maximum UINT32_MAX blocs de 512 octets. Refus avant conversion des images ≥2 Tio, au lieu d’une taille tronquée. Aucun élargissement ni nouvelle limite matérielle revendiquée ; banc sparse POSIX, Windows/FS non sparse sautés. |
| hdc.c `HDC_Cmd_ReadSector/WriteSector` | `Acsi::cmdReadSector/cmdWriteSector` | corrigé | Comparaison non signée du compte et des secteurs restants ; début d’une image de plus de 1 Tio accepté. Borne de fin inchangée, banc sparse POSIX ; pas d’élargissement des compteurs. |
| hdc.c `HDC_Cmd_WriteSector`, `dmawrite_to_fh` | `Acsi::cmdWriteSector/writeToDisk/serialize` | porté | Cible et LBA du WRITE différé figés ; début d’un nouveau paquet sans redirection de l’écriture. Deux champs sauvegardés v45. |
| hdc.c:1034 (WriteCommandPacket) | Acsi.cpp:508-545 | porté | 1:1 (6/10/12 octets, LUN). |
| hdc.c:1170 (Acsi_WriteCommandByte) | `Fdc::writeAcsi/clearHdcIrq` | porté, revérifié | Acquittement de la seule source HDC ; IRQ de réponse via `fdcSetIrq(IRQ_HDC)`. Ancienne mention de pilotage direct périmée. |
| hdc.c:1109/1253 (Acsi_DmaTransfer) | `Fdc::acsiDmaTransfer/onAcsiIrq`, `Acsi::markDmaRangeError` | porté avec limite | IRQ différée 1000 cycles CPU ; hors RAM, drapeau DMA et statut ERROR en READ, sense disque conservé. WRITE conserve son statut ; source ROM inaccessible avec RAM supportée ≤4 Mo (masque DMA $3FFFFE). |
| hdc.c:627, 188-620 (HDC_Cmd_*) | Acsi.cpp:467-503, 222-465 | porté | Jeu SCSI complet, INQUIRY corrigé 3ᵉ passe. |
| hdc.c:742 (PartitionCount) | Acsi.cpp:550-574 | partiel | Manque la détection byte-swap (cosmétique). |
| fdc.c:6360-7500 (flux MFM/DPLL) | — | volontaire | Hors périmètre (exclusion assumée : SCP/KFS). |
| ipf.c (IPF via libcapsimage) | `IpfImage.cpp` (`ipf::decode`/`ipf::toStx`) → chemin STX | réécrit (2026-10-22) | Décodeur libre adapté de MAME `ipf_dsk.cpp` ; lecture WD1772 figée au montage, pas de DPLL. Cf. HATARI_DIVERGENCES § FDC. |
| fdc.c:1569, 4724/4799 (FDC_UpdateAll avant chaque accès) | Fdc.cpp:2644, 2788, 2863 | porté (2026-10-18) | indexCheckUpdate en tête de onFdcEvent et avant tout accès WD1772, en lecture comme en écriture (cf. ligne DiskControllerStatus). |

## 5. PSG / YM2149 (psg.c, sound.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| psg.c:208-230 (PSG_Reset), MAME ay8910_reset_ym | `YM2149::reset` | porté | R7 ramené à zéro le 2026-10-19 ; registres nuls sauf R14=$FF, latch/sélecteur zéro, silence par volumes nuls. Ports et strobe qualifiés, sans callback de reset parasite. |
| psg.c:252-276 (Set_SelectRegister) | YM2149.hpp:47-54 | porté | Sélecteur non masqué, ≥16 → 0xFF. |
| psg.c:283-317 (Get_DataRegister) | YM2149.hpp:36-45 | **partiel ⚠** | CONFIRMÉ : read-latch fidèle ; manque la recomposition R14 bit5/R15 depuis les joysticks port parallèle (inventorié [très basse]). |
| psg.c:324-464 (Set_DataRegister) | YM2149.hpp:55-91 + Machine.cpp:211-225 (sinks) | **partiel ⚠** | Masques, strobe et sinks portés ; FDC_SetDriveSide poussé à chaque écriture du port A (Machine.cpp:225). Manque SCC_Check_Lan_IsEnabled MegaSTE (bit7 port A, psg.c:423-426). |
| psg.c:477-503 (PSG_WaitState) | Cpu68k.cpp:1135-1143 | partiel **✗** | Le cas MOVEM (+4/4 accès) est omis, mais l'impact est **RÉFUTÉ** : divergence volontaire triplement documentée, et les mesures psg.c:143-150 sont des bancs matériels SANS attribution logicielle — tous les loaders réels cités (ventura, ULM, SNY, TCB, X-Out) utilisent movep/move, jamais movem. |
| psg.c:510-545 (ff8800/ff880x_ReadByte) | Bus.cpp:847-850 + YM2149.hpp:30-46 | porté | Exception $FF8802 relisible = volontaire documenté (RMW cartouches diag). |
| psg.c:577-606, 637-666 (ff8801/8803_WriteByte, fix X-Out) | `Bus::wrPsg` | déjà porté, requalifié 2026-10-19 | Ombres impaires normalisées seulement pour byte/movep ; octets impairs des mots/longs ignorés. Décodage de largeur dans le Bus, pas dans YM2149. |
| ioMem.c:386-393 (shadow $FF8804-FF) | `Bus::wrPsg/rdPsg` | porté | Miroirs jusqu’à $FF88FF, y compris ombres impaires byte ; lectures impaires void. |
| psg.c:420 (SetDriveSide push) | Machine.cpp:211-225 (push) → Fdc.cpp:969-981 | porté | Poussée à chaque écriture du port A, comme Hatari ; refreshDriveSide idempotent. Écart du candidat n°19 résolu côté code. |
| sound.c:164-185, 735-760 (YmEnvDef/EnvBuild) | YM2149.cpp:34-44, 161-181 | porté | Identique. |
| sound.c:208 (YmVolume4to5) | YM2149.cpp:18-22 | porté | Table identique. |
| includes/ym2149_fixed_vol.h | src/core/ym2149_fixed_vol.h | porté | 4096 valeurs IDENTIQUES (diff des littéraux), en-tête GPL conservé. |
| sound.c:505-543 (interpolate_volumetable) | YM2149.cpp:113-157 (défaut) | porté | = YM_TABLE_MIXING, le défaut Hatari (S4 ✅). |
| sound.c:617-680 (BuildModelVolumeTable) | YM2149.cpp:94-111 (NEOST_YM_MIXING=model) | porté | Équivalent --ym-mixing model pour A/B. |
| sound.c:554-562 (BuildLinearVolumeTable) | — | absent | Option de confort, sans enjeu de fidélité. |
| sound.c:700-724 (Normalise Level>>1 STE) | YM2149.cpp:150-156 + Machine.cpp:309 | porté | Demi-amplitude par outScale_ + kLmcMakeup ×2 (S3 ✅). |
| sound.c:1411-1518 (Sound_WriteReg) | YM2149.cpp:183-212 (updateFromRegs) | porté | Recalcul par bloc, même résultat. |
| sound.c:1024-1135 (DoSamples_250) | YM2149.cpp:242-289 | porté | 1:1 (bruit 125 kHz, incrémente-puis-compare, Env boucle 32-95). |
| sound.c:969-981 (RndCompute) | YM2149.cpp:54-61 | porté | LFSR 17, taps identiques. |
| sound.c:453-466 + 1945-1952 (LowPassFilter C10) | YM2149.cpp:231-240 + Machine.cpp:312 | porté | LPF STF / PWM STE (S1 ✅). |
| sound.c:481-494 (PWMaliasFilter) | YM2149.cpp:214-224 | porté | — |
| sound.c:384-411 + 1714-1748 (Subsonic HPF, placement) | `YM2149::synthesizeFrame`, `DmaSound::applyHpfStereo` | déjà porté, revérifié | HPF YM sur ST ; YM brut et HPF après mélange YM+DMA sur STE, via configuration Machine. Ancienne mention HPF YM seul périmée. |
| sound.c:864-900, 1243-1258 (horloge YM_Freq/8 exacte) | YM2149.hpp:285-288 (YM_250_HZ = 250 663) + YM2149.cpp:373-396 (rejeu sur la grille 250 kHz) | porté | Horloge MCLK/128 au lieu de 250 000, et écritures datées au pas 250 kHz, plus au grain hôte. Reste une fréquence unique pour toutes les machines. Ex-candidat n°27. |
| sound.c:1349-1385 + 1708 (Resample Weighted_N) | YM2149.cpp:291-318 | porté | 16.16 identique, marge ceil+2. |
| sound.c:938-958 (Ym2149_Reset) | YM2149.hpp:111-134 | porté | Fusionné avec PSG_Reset. |
| psg.c/sound.c MemorySnapShot | `YM2149::serialize` (YM2149.hpp:204) | porté | Save-states en place (format v50). |

## 6. Son DMA STE / Microwire / LMC1992 (dmaSnd.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| dmaSnd.c:240-276 (DmaSnd_Reset) | DmaSound.cpp:216-247 | porté | Fix « Brace » ; défauts LMC 0 dB au cold = divergence assumée commentée. |
| dmaSnd.c:305-322 (XSINT lines) | DmaSound.cpp:51-58 + Mfp.cpp:852-869, 877-882 | porté | GPIP7 + TAI ; délai 74LS164 non émulé des deux côtés. |
| dmaSnd.c:342-368 (FIFO_Refill) | DmaSound.cpp:94-129 | porté | S2 ✅ : anneau 8 o par mots, fin de trame au fetch, cas 2^24. |
| dmaSnd.c:387-409 (FIFO_PullByte) | DmaSound.cpp:131-143 | porté | Refill-sur-vide identique. |
| dmaSnd.c:419-438 (FIFO_SetStereo) | DmaSound.cpp:145-153 | porté | Réalignement mono→stéréo. |
| dmaSnd.c:461-479 (StartNewFrame) | DmaSound.cpp:67-92 | porté | start==end + repeat OFF → arrêt sec (Amberstar). |
| dmaSnd.c:727-741 (STE_HBL_Update) | DmaSound.cpp:209-214 ← Machine.cpp:582 | porté | Même granularité ligne des deux côtés. |
| dmaSnd.c:587-676 (consommation FIFO au rééchantillonnage) | DmaSound.cpp:179-207 + anneau 155-177 | volontaire | Consommation au rythme DAC daté + capture ; architecture assumée (DmaSound.hpp:167-178). |
| dmaSnd.c:748-761, 825-848 (GetFrameCount) | `DmaSound::liveCounter/onHbl/fifoPull` | déjà porté, requalifié 2026-10-19 | Compteur vivant sans refill à la lecture ; FIFO entretenue au HBL ou lorsqu’elle se vide. |
| dmaSnd.c:768-818 (SoundControl R/W) | `DmaSound::read8/write8` | porté | $FF8900 pair vaut zéro. $FF8920 est un octet scratch relisible, pas un zéro constant ; désormais sauvegardé en v42. |
| dmaSnd.c:855-976 + m68000.c:958-971 (FrameStart/End + MaskAddressHigh) | `DmaSound::write8` | déjà porté, requalifié 2026-10-19 | Adresses paires ; octets hauts début/fin masqués à $3F pour les profils exposés avec RAM ≤4 Mo. |
| dmaSnd.c:983-1026 (SoundModeCtrl) | DmaSound.cpp:582-592 | porté | Masque 0x8F, updateDac avant changement. |
| dmaSnd.c:1037-1076 (Microwire shift 16×8 cyc) | `DmaSound::onMicrowireShift/catchUpMicrowire/advanceMicrowire` | corrigé | Retard consommé sans dérive, au plus 16 fronts ; départ liveNow+8. MMIO Data/Mask rattrape les fronts dus avant accès, sans dispatch global ; mot et écritures occupées préservés. |
| dmaSnd.c:1087-1171 (décodage LMC1992) | DmaSound.cpp:249-286 | porté | Scan des runs 1:1, codes bruts équivalents aux tables. |
| dmaSnd.c `DmaSnd_MicrowireData_WriteWord`, IoMem_ReadWord | `DmaSound::write8` | corrigé | Fusion DATA octet depuis la valeur visible après décalage, sans ressusciter l’ancienne commande. Mot/long conserve son octet haut staged ; déclenchement high isolé, rejet occupé et snapshot préservés. |
| dmaSnd.c:1178-1254 (MicrowireData/Mask R/W) | DmaSound.cpp:538-543, 593-631 | porté | Écritures ignorées pendant shift, déclenché sur l'octet bas. |
| dmaSnd.c:538-680 (routage mixing + mix) | `DmaSound::mixSegment`, kDmaGain | déjà porté avec choix | Routage et niveau ¾, signe DMA négatif du LMC déjà présents. DC-hold reste le choix déclaré ; ancienne mention de signe manquant périmée. |
| dmaSnd.c:1315-1348 + 577-580 (LowPass FIR) | DmaSound.cpp:469-477, 642-648 | porté | (1,2,1)/4 exact. |
| dmaSnd.c:689-713, 1262-1462 (Apply_LMC + shelves Savinkoff) | `DmaSound::applyTone/applyToneStereo/applyHpfStereo` | déjà porté, revérifié | Shelves Savinkoff premier ordre, coupures 118.2763/8438.756 Hz et HPF après mélange STE présents. Ancienne entrée RBJ/HPF incorrecte périmée, aucun nouveau port de filtre. |
| dmaSnd.c:282-289 (MemorySnapShot), stMemory.c STMemory_MemorySnapShot_Capture | `DmaSound::serialize` | porté | État runtime et registres sauvegardés ; octet scratch $FF8920 ajouté en v42, comme l’IoMem compris dans le snapshot Hatari. |

## 7. IKBD / ACIA / MIDI (acia.c, ikbd.c, midi.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| acia.c:757-776 (Read_SR) | Ikbd.cpp:142-155 | porté | DCD/CTS à la masse, FE/PE omis (assumé). |
| acia.c:856-881 (Read_RDR) | Ikbd.cpp:157-182 | porté | OVRN posé À LA LECTURE, séquence SR→RDR. |
| acia.c:1103-1126 (Clock_RX STOP_BIT) | Ikbd.cpp:1199-1209 | porté | Overrun : nouvel octet perdu, RDR conservé. |
| acia.c:472-485 (cadence 1024 cyc/bit) | Ikbd.cpp:1147-1177 (10240 cyc/octet) | porté | Granularité octet (écart de phase ≤1024 cyc). |
| acia.c:669-705 (MasterReset) | Ikbd.cpp:260-265 | porté | File 6301 et octet en vol conservés. |
| acia.c:785-842 (Write_CR) | Ikbd.cpp:236-276 | **partiel ⚠** | TIE, master reset, mode break (Ikbd.cpp:243, 1269-1284) et garde « diviseur non initialisé → IKBD muet » (aciaClockInitialized_, Ikbd.cpp:1105, 1128) portés. Clock_Divider reste borné à /64 8N1 (RX 10240 cyc/octet, TX grille 1024 cyc/bit). → candidat n°18. |
| acia.c:893-902 + 913-1027 (Write_TDR + Clock_TX) | Ikbd.cpp (write8, onTxEmpty, receiveSerialByte) | porté pour /64 8N1 | TDR/TSR, TDRE à la grille bit, commande au stop, remplacement du TDR occupé, reset et sauvegarde (2026-10-18). Formats/diviseurs et break restent bornés. |
| acia.c Clock_TX → ikbd.c:755-819, 891 (transit série CPU→IKBD) | `Ikbd::write8/onTxEmpty/receiveSerialByte` | déjà porté pour /64 8N1 | Commande consommée au stop, TDR/TSR séparés et TDRE à la grille bit. Formats/diviseurs hors liaison usuelle restent bornés ; ancien parse immédiat supprimé le 2026-10-18. |
| acia.c:372-389, 715-748, 954-1023 (IRQ, Clock_TX/RX) | `Ikbd::raiseIfReady/onRxDeliver/catchUpCpuTx`, `Mfp::setAciaLineKbdAt` | porté | OU câblé GPIP4 clavier+MIDI ; fronts TDR→TSR et stop RX datés à l’échéance, même au rattrapage MMIO (2026-10-19). Propagation MFP +4 ; accès CR/RDR datés au cycle CPU courant. |
| acia.c:546-561 (AddWaitCycles) | Bus.cpp:916,923,930,937 + Cpu68k.cpp:1151-1166 | porté | 6 cyc + E-Clock. |
| ioMemTabST.c ($FFFC01/03/05/07 void) | Bus.cpp:915,922,929,936 | porté | Un accès mot ne touche l'ACIA qu'une fois. |
| ikbd.c:499-627 + 107 (IKBD_Reset/Boot_ROM, 502000 cyc) | Ikbd.cpp:657-750 + Machine.hpp:102,124 | porté | Delay_Random omis (déterminisme headless, assumé). |
| hatari-glue.c:49-77 (customreset) | `Cpu68k::didExecute`, `Bus::peripheralReset` | déjà porté, revérifié le 2026-10-19 | Instruction RESET reliée à IKBD/Glue/PSG/MFP/FDC, sans réinitialiser CPU ni ordonnanceur. Ancienne entrée « hook absent » périmée ; candidat 14 déjà clos. |
| reset.c:111-124 (ACIA_Reset + Midi_Reset au reset machine) | `Machine::reset/hardReset`, `MidiAcia::reset`, `Ikbd::resetHw` | MIDI porté ; clavier /64 8N1 | MIDI CR/statut/RDR/TDR/TSR remis aux défauts et TX annulé aux deux resets. Phase du câble RX conservée. Clavier : CR/SR/RDR, erreurs, TDR/TSR, initialisation du diviseur et causes IRQ conservés ; SCI sortant annulé séparément, IRQ republiée. Timer TX réancré, réception du fragment reconstruite selon états SCI ; autres formats physiques différés. |
| ikbd.c:218-268 (KeyboardCommands[]) | Ikbd.cpp:185-229, 296-334, 336-655 | porté | Longueurs identiques, NOP inconnus. |
| ikbd.c:2085-2758, 3049 (Delay_Random avant réponses) | `Ikbd::pushRxDelayed/armRx` | déjà porté avec délais déterministes | Valeurs centrales des plages Hatari, quantifiées en bits, puis transit SCI ; interrogations et Froggies couverts depuis le 2026-10-18. Phase SCI exacte hors modèle. |
| ikbd.c:945-959 (OutputBuffer 1024) | Ikbd.cpp:1092-1125 | porté | Testé avant le 1er octet (Downfall/Fokker). |
| ikbd.c (PauseOutput $13/$11) | Ikbd.cpp:359-362, 433-439, 1165, 1189 | porté | Ignoré pendant la fenêtre critique (Just Bugging). |
| ikbd.c:1783-1813, 1672-1729 (AutoSend timer 150000 cyc) | Ikbd.cpp (onAutoSend, sendAutoInput) | porté | Timer indépendant du VBL, phase conservée lors des resets rapprochés (2026-10-18). |
| ikbd.c:2293-2312 (SetJoystickMonitoring, taux) | `Ikbd::onJoystickMonitor/sendAutoInput` | porté (2026-10-18) | Taux respecté, cadence commune conservée après changement de mode ; MouseAction avant monitoring. |
| ikbd.c:1429-1437, 1344, 2219-2228 (joysticks, quirks reset) | Ikbd.cpp:764-788, 485-509, 370-380, 752-762 | porté | Barbarian/Hammerfist/$12+$1A. |
| ikbd.c:1484 + $14 immédiat (Utopos) | Ikbd.cpp:776-788, 501-507 | porté | PrevJoyData même si tampon plein. |
| ikbd.c:1382-1422, 2050-2095 (paquets souris) | Ikbd.cpp:934-1056, 903-915 | porté | Seuil/échelle/axe Y/front sans mouvement (Vroom). |
| ikbd.c:1118+ + 1065-1091 (horloge BCD 6301) | Ikbd.cpp:1058-1090, 122-139 | porté | Année 99→00 (Captain Blood). |
| ikbd.c:386+, 3012+ (code 6301 custom, CRC) | `Ikbd::commonBoot`, `crc32AddByte` | porté (empreinte d'Hatari reprise à l'identique le 2026-10-20 : le vrai CRC32 ne reconnaissait aucun programme réel) | 6 programmes au CRC ; délai 7000 cyc du $83 Froggies porté (pushRxDelayed). |
| ikbd.c:1736-1757 (PressSTKey) | Ikbd.cpp:917-932 | porté | ScanCodeState, filtrage monitoring. |
| keymap.c | gui/InputCallbacks.cpp (onKey) + gui/StKeys | volontaire | Plomberie frontend (GLFW vs SDL). |
| midi.c:143-184 (MIDI_UpdateIRQ + Control_Read) | MidiAcia.cpp:43-64, 241-250 | porté | Wire-OR GPIP4. |
| midi.c:245-288 (TDR/TSR : vide après 1 bit=256 cyc) | MidiAcia.cpp (write8, advanceTx) | porté TX | Huit formats CR2..4 et /1,/16,/64 ; TDR→TSR après un bit, données et durée mémorisées, attente écrasable, IRQ indépendante et horodatages série (2026-10-18). RX hôte reste 31250 8N1. |
| acia.c:800–804,451–454,479–484 (diviseur et timer TX générique) | `MidiAcia::write8`, `txClockPhase_` | porté pour reprise BREAK | Phase ancrée au changement CR0..1 et sauvegardée v38 ; libération BREAK au prochain front de cette phase. Changement diviseur en vol recadence les fronts TSR restants ; TDR au repos attend le prochain front de phase CR ; pas de modèle bit à bit. |
| acia.c Clock_TX/RX ; mfp.c MFP_GPIP_Update_Interrupt/InputOnChannel | `MidiAcia::advanceTx/onRxPace/emitTx`, `Mfp::setAciaLineMidiAt` | datation corrigée | IRQ TDR→TSR au front de chargement, RX hôte à firingDue et bouclage au stop ; AER/DDR, ligne partagée clavier et délai4cycles conservés. CR/RDR et autres setters gardent leur date d’accès. |
| midi.c:294-331 (RX hôte cadencé) + acia.c (RX STOP / Read_RDR) | MidiAcia.cpp (onRxPace, pushRx, read8) | porté avec modèle ACIA | Source hôte à 2560 cycles/caractère ; un RDR, RX_Overrun puis OVRN, acquittement SR→RDR. Bouclage livré au stop TX ; le RSR ne retient pas un second caractère achevé (2026-10-18). |
| `ikbd.c:2324,2351`, table des commandes | `Ikbd::cmdLength/dispatch` | NOP comme Hatari | `$18` reconnu (1 octet), `$19` consommé (7 octets) ; ces deux modes ne sont pas émulés. `$99` reste un alias du rapport `$94/$95`. |

## 8. Blitter (blitter.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| blitter.c:271-307 (Blitter_Reset) | `Blitter::reset` | porté au reset chaud | RAM halftone et pipeline source conservés depuis le 2026-10-19. Reset froid : initialisation déterministe NeoST conservée, aucune valeur power-on matérielle revendiquée. |
| blitter.c:430-453 + stMemory.c:724 (Read/WriteWord DMA) | Blitter.cpp:87-103 | porté | bus_word à chaque accès, sliceBus_ ≙ CountBusBlitter. |
| blitter.c:489-514 (SourceShift/Fetch/Read) | Blitter.cpp:474-479 | porté | Réinjection bus_word en NFSR (bug icônes GEM corrigé). |
| blitter.c:522-528 (halftone + smudge) | Blitter.cpp:480-482 | porté | — |
| blitter.c:533-564 (tables HOP) | Blitter.cpp:483-490 | porté | 4 cas identiques. |
| blitter.c:570-677 (LOP + need_src/dst) | Blitter.cpp:19-20, 525-542 | porté | 16 LOP identiques. |
| blitter.c:682-744 (ProcessWord) | Blitter.cpp:501-552 | porté | FXSR, « weird » NFSR x_count=1, suspension mid-word. |
| blitter.c:777-857 (Step) | Blitter.cpp:492-569 | porté | Endmasks, nfsr latché, halftone_line ±1. |
| blitter.c:463-481 (ContinueNonHog) | Blitter.cpp:327-328, 472 + breaks | porté | 64 accès exacts (63 si busCountError_), état de mot persisté. |
| blitter.c:871-946 (Blitter_Start) | Blitter.cpp:242-310, 318-359, 406-416 | porté | GPIP3 ré-armée, ctrl recomposé, fenêtre CPU armée. |
| blitter.c:342-375 (AddCycles/FlushCycles → CycInt_Process PAR ACCÈS) | `Blitter::billCycles`, `Cpu68k::rebaseQuantumAndSync` | porté | Facturation et dispatch par accès ; rebase du quantum quand la tranche tourne dans le CPU, crédit direct hors quantum. BL4 conservé avec la reprise sous-instruction (2026-10-19). |
| blitter.c:395-421 (BusArbitration + cache MegaSTE) | `Blitter::onSlice` | porté | 4/8 + 4 ; cache invalidé AVANT arbitration, observé par un événement pendant ces huit cycles (2026-10-19). |
| blitter.c:1393-1483 (Control_WriteByte) | Blitter.cpp:148-172, 242-257, 394-399 | porté | Restart TOS, pause/reprise ; écritures mot/long atomiques (Bus.cpp:807/820). |
| blitter.c:1489-1495 (Skew + latches) | Blitter.cpp:431-438 | porté | Relecture en début de tranche = granularité identique. |
| blitter.c:1338-1367 (WordsPerLine/Lines) | Blitter.cpp:207-211, 226-229 | porté | Y 0→65536 à l'écriture (B1). |
| blitter.c:972-989 (CheckAccess_Byte) | Blitter.cpp (read8/writeRegisters) | porté 2026-10-18 | Miroir MMIO brut séparé des registres vivants ; lecture mot/long le rafraîchit, accès octet rejeté conserve le miroir. État v22. |
| blitter.c (masques matériels à l'écriture) | Blitter.cpp:56-61 (regWriteMask) | porté | Relecture fidèle. |
| blitter.c:1574-1593 (bug « 63 accès ») | Blitter.cpp:293-306, 378-389 + Cpu68k.cpp:580, 814-820 | porté | Fenêtre PRE_START datée à l'horloge bus absolue. |
| blitter.c:1603-1626 (fenêtre CPU 64 accès réels) | Blitter.cpp:364-372 via Bus::blitterCountCpu | porté | Modèle CE (callbacks mémoire Moira). |
| blitter.c:954-961, 1641-1659 (Check_Simultaneous_CPU) | `NeostMoira::sync`, `Cpu68k::serialize` | porté HOG/partagé 2026-10-19 | Crédit CPU interne jusqu’au prochain accès bus, conservé en v49 ; sondes tailles différentes, MULU, TAS et RAM ST. |
| blitter.c:1672-1694 (do_cycles_after : PRE_START + cpu_bus_rmw) | `NeostMoira::sync`, `Scheduler::runBlitterTo`, `Moira::execTasEa` | porté 2026-10-19 | Reprise ciblée pendant l’instruction, avant/après chaque cycle bus ; verrou TAS explicite, libéré avant préfetch et sur exception. Aucun dispatch vidéo par le seul helper PRE_START ; dispatch DMA BL4 préservé par accès. |
| blitter.c:943, 1467, 1502-1510 (forfait non-CE 256 cyc) | Blitter.cpp:318 (onSlice) | volontaire | Seul le modèle CE (celui de l'oracle) est gardé. |
| blitter.c:895/916 (GPIP3/GPU_DONE) | Blitter.cpp:281-286, 406-416 + Mfp.hpp:325 | porté | Front AER. |
| Manuel Atari BLiTTER, « Bus accesses / Busy » (lien BUSY / interruption) | `Blitter::runSlice`, `Blitter::finishTransfer` | corrigé 2026-10-19 | BUSY maintenu pendant restitution, puis effacé avec GPU_DONE ; événement témoin 8/16 MHz × HOG/partagé. |
| blitter.c (Falcon/TT-RAM 32 bits, DSP) | — | volontaire | Sans objet. |
| blitter.c:316-334, 1516-1565 (stats/snapshots) | `Blitter::serialize` (Blitter.hpp:67) | snapshots portés ; stats volontaire | Statistiques remplacées par NEOST_BLIT_TRACE. |

**✗ Réfuté (blitter)** : *MegaSTE 16 MHz — hits du cache comptés comme accès CPU* : Hatari compte
LUI AUSSI les hits du cache dans la fenêtre des 64 accès (m68000.c:1444-1676 : mem_access_before/after
appelés inconditionnellement, hit ou miss). Cette concordance ne suffit pas
à valider le matériel : la visibilité des hits pour le compteur du blitter
MegaSTE reste à qualifier par documentation ou mesure matérielle. Le
commentaire de `Blitter.hpp` signale cette approximation, pas une validation.

## 9. CPU / cycles / ordonnanceur (cycInt.c, cycles.c, clocks_timings.c, hatari-glue.c, newcpu.c)

**Domaines IACK MegaSTE, 2026-10-19** : `NeostMoira::iackSyncAfter` sépare
le bloc périphérique (cycles bus) des quatre cycles internes (cycles CPU),
suivant la distinction CLK / attentes du manuel Motorola, section 8.
Les multiplier ensemble doublait l'idle à 16 MHz. Vérifié sur les 72 IACK
MFP par capture et sur les dix phases E-clock HBL/VBL à 8/16 MHz ; aucune
égalité globale avec Hatari modifié déduite de cette correction.

La [sonde IACK matérielle](MFP_IACK_MEASUREMENT.md) produit désormais un PRG
relogeable et une disquette AUTO. Son protocole passe sous NeoST ; aucune
mesure sur Atari réel n'est revendiquée. L'expérience de conversion du bloc
MFP d'Hatari en cycles bus laisse +3/+4 cycles à 16 MHz : la constante reste
à trancher sur la carte, indépendamment des corrections du blitter.

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| cycInt.c (Add*/InsertInt, liste triée) | Scheduler.hpp:128-170, 336-421 | porté | Tableau + cache O(1) ; tie-break par énum documenté. |
| cycInt.c:531-552 + PendingInterruptCount | Machine.cpp:679-687 + Scheduler.hpp:183-186 | porté | Dispatch frontière d'instruction (CE) ; sync-driven mid-instruction RÉFUTÉ (deadlock EL), supprimé (A34). |
| cycInt.c:340-363 (DelayedCycles, anti-dérive) | Scheduler.hpp:406, 423 (firingDue) + Machine.cpp | porté | Vidéo sur grille théorique, MFP et FDC sur firingDue (Fdc.cpp:2719-2721). |
| cycInt.c:26-45 + cycInt.h:53-63 (unités internes ×256) | Mfp.cpp:629-646, 769-801 | porté | Unité 1/256 cycle (CYCINT_SHIFT=8) et reste fractionnaire conservé entre recharges (timerDueSub_). |
| cycInt.c:486-497 (FindCyclesRemaining) | Scheduler.hpp:312-326 | porté | rawCyclesUntil plus fin qu'Hatari. |
| cycles.c:47 (CyclesGlobalClockCounter) | Cpu68k.cpp:233-238, 963-965 | porté | Horloge maîtresse = compteur Moira, continue. |
| cycles.c:315-321 (GetClockCounterImmediate) | Scheduler.hpp:88-95 + Machine.cpp:149 | porté | Explicitement calqué. |
| cycles.c:120-157, 282-289 (OnReadAccess CE) | Cpu68k.cpp:698-701 + read −6 | porté | Table non-CE volontairement non portée. Paire read −6 / write +2 indissociable. |
| cycles.c:167-244, 297-304 (OnWriteAccess) | Cpu68k.hpp:162-168 + write +2 | porté | Même modèle CE. |
| cycles.c:74-111 (compteur vidéo par trame) | Machine.cpp:124-127, 698-699 (liveFrameClock) | porté | frameStart_ = ancre théorique. |
| clocks_timings.c:181-371 (fréquences exactes par machine) | `core/Pacing.hpp:27-36` (`kCpuHz` / `kCpuHzInt`) | **partiel ⚠** | RECOMPTÉ le 2026-08-30 : la moitié « non centralisé » de ce constat est PÉRIMÉE depuis A28 — l'horloge a UN site de définition, et les deux littéraux qui avaient survécu (`Mfp.cpp` débit série, en-tête du dump MIDI headless) y pointent désormais. Ce qui RESTE vrai, et seul : **une seule fréquence pour toutes les machines**, pas de variantes NTSC/MegaSTE. Impact limité à la synchro audio long terme (acté P3). ⚠ Les `fichier:ligne` précédents (Audio.cpp:74, DmaSound.cpp:27, Rtc.hpp:52, Mfp.cpp:226) désignaient du code sans rapport : ils n'avaient pas été revérifiés depuis A28. |
| clocks_timings.c:373-410 (nCpuFreqShift 16 MHz) | Cpu68k.cpp:119-124, 233-238, 1272-1283 | porté | Même invariant, horloge continue à la bascule $FF8E21. |
| clocks_timings.c:498-530 (durée VBL µs → IKBD) | Machine.cpp:630-631 | porté | 8 MHz nominal, écart ~0,27 % négligeable. |
| hatari-glue.c:85-99 (intlev) | Cpu68k.cpp:826-859 + Scu.hpp:59-73 | porté | Y compris soft IRQ1 SCU ; commit à frontière (RAISE_COMMIT=3). |
| hatari-glue.c:49-77 + newcpu.c (customreset) | `Cpu68k::didExecute`, `Bus::peripheralReset` | déjà porté, revérifié le 2026-10-19 | Le hook Moira appelle les resets périphériques et recalcule IPL ; ancienne affirmation « ne resette rien » périmée. |
| hatari-glue.c:195-325 (opcodes cartouche + CpuDoNOP) | Cpu68k.cpp:290-313 | porté | VDI/NatFeats hors-champ documenté. |
| m68000.c:791-861 (WaitState/EClock/SyncCpuBus) | Cpu68k.cpp:1128-1166 + alignRamBus (501-511) | porté | RAM_SLOT étend l'alignement à toute la RAM (assumé, mesuré). |
| newcpu.c:2958-3019 (iack_cycle E-clock) | Cpu68k.cpp:715-792 | porté | Motif mod-20 fidèle, spurious $60. |
| newcpu.c:9234 (STOP CE, boucle 4 cyc) | Cpu68k.cpp:1066-1105 + patch Moira niveau-sensible | volontaire ⚠ | Téléportation à l'événement (assumé) MAIS granularité de réveil 2 cyc vs quantum 4 CONFIRMÉE → phase E-clock d'IACK mod 4 après STOP. → candidat n°10. |
| video.c:624 vs 976-982 (WS1/WS3, datation HBL) | VideoGlue.hpp:69-92 + Machine.cpp:76-92 | porté **✗** | **RÉFUTÉ/RÉSOLU** : tranché WS3 dans l'arbre (2026-07-08), kHblOff défaut 0, VBL par wakestate. Doc à mettre à jour. |
| mfp.c:374 (MFP_IRQ_DELAY_TO_CPU=4) | Scheduler.hpp:47-50 + Cpu68k.cpp:651-661 | porté | Sans le commit frontière, le délai s'additionnerait au pipeline IPL Moira. |
| cycInt.c:140 (CycInt_From_Opcode, rattrapage mid-opcode) | Mfp.cpp:63-70 + Scheduler.hpp:218-248 (runMfpTimersTo) | **partiel ⚠** | MFP_UpdateTimers porté avant chaque accès registre MFP (timers délai seuls, défaut ON) ; le dispatch générique mid-opcode reste absent (modèle bloc). |
| cycles.c/cycInt.c MemorySnapShot | `Scheduler::serialize`, `Cpu68k::serialize` | porté | Save-states en place (format v50). |

---

## 10. GEMDOS HD / cartouche / RTC / SCU / joysticks (gemdos.c, cart.c, rtc.c, scu_vme.c, joy.c)

**Cartographié manuellement le 2026-07-08** (agent perdu deux fois : erreur API puis
plafond de dépense) — relecture directe des deux côtés, **sans vérification adversariale**.

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| gemdos.c:4087 `GemDOS_Trap` (dispatch) | GemdosHd.cpp:1720-1748 (`GemdosHd::trap`) | porté | Même jeu d'appels interceptés (0x00, 0x0E, 0x31, 0x36, 0x39-0x43, 0x46, 0x47, 0x4B, 0x4C, 0x4E, 0x4F, 0x56, 0x57). |
| gemdos.c `GemDOS_MemorySnapShot_Capture`, `save_file_handle_info/restore_file_handle_info` | `GemdosHd::serialize`, `Machine::serializeState/loadState` | porté avec politique stricte, 2026-10-19 | v48 : handles, Fforce, DTA, chemins et Pexec. Aucun rejeu de wb+ ; fichier absent/changé refusé au lieu d'un handle perdu ou d'un simple avertissement. Préparation puis commit atomique ; configuration des racines vérifiée. |
| gemdos.c `GemDOS_SFirst/SNext/PopulateDTA`, str.c `Str_Filename_Host2Atari` | `GemdosHd::gemSFirst/gemSNext/populateDTA` | porté, corrigé 2026-10-19 | Points seulement en sous-dossier selon masque/attributs, `..` préservé ; erreur `stat` différente d’ENOENT → EINTRN, entrée disparue sautée. |
| gemdos.c:1600 `GemDOS_Cconws` + `GemDOS_Super` (0x09/0x20) | — | volontaire | Chemins « mode test sans TOS » (`bUseTos=false`) ; NeoST exécute toujours un TOS (GemdosHd.cpp:6-8). |
| gemdos.c:350/1183 `Str_Filename_Atari2Host/Host2Atari`, configuration.c:654 | `GemdosHd` | défaut conforme, option non portée | Conversion charset désactivée par défaut dans les deux émulateurs. L’option activable d’Hatari reste absente ; ce n’est pas un écart de comportement par défaut. Recomposition NFD/NFC hôte déjà portée séparément. |
| gemdos.c `bGemdosWriteProt` (write-protect) | — | volontaire | Désactivé par défaut, documenté (GemdosHd.cpp:11). |
| gemdos.c:610/2180 `INF_CreateOverride`/`INF_Overriding` (autostart INF) | — | volontaire | INF_* inactifs (GemdosHd.cpp:10) ; recette DESKTOP.INF `#Z` headless en remplacement. |
| gemdos.c `GemDOS_InitDrives` (lecteur après les partitions images) | `GemdosHd::initDrives/reset/sysInit`, Machine | porté pour ACSI, 2026-10-19 | Dossier simple après les partitions ACSI ; sous-dossiers C..Z explicites inchangés. Provider commun aux frontends, réévaluation au boot/reset sans remap en vol. Aucune lettre libre : configuration conservée, table GEMDOS vide au reset ; ancien montage intact si un nouveau montage est refusé. |
| gemdos.c `GemDOS_Pexec` + `GemDOS_LoadAndReloc` | GemdosHd.cpp (opcode 9, `pexecBpCreated`) | porté | Cpu68k.cpp:290-312 (`illegalOpcodeHook`) : opcodes magiques 8/9/10 dans la cartouche → traités en C++ puis NOP. |
| cart.c/cartData.c (cartouche système) | GemdosHd.cpp:97-140 `Cart_data` | divergent-volontaire | Programme info renommé `NEOST.TOS`, texte = raccourcis NeoST, C-BSIZ `$1E7` (2026-07-08) ; en-tête, code 68000 et adresses `$FA0024`/`$FA002A` inchangés. |
| cart_asm.s:37 `VDI_OPCODE` (12, résolutions VDI étendues) | — | volontaire | Pas de mode VDI étendu dans NeoST ; l'opcode `$000C` est dans `Cart_data` mais jamais intercepté (Cpu68k.cpp:304 ne prend que 8-10). |
| cart.c (exclusivité cartouche externe ↔ système) | gui/App.cpp:447-455 (démarrage) + gui/MediaPages.cpp (page Disques durs) | porté | Depuis le 2026-07-08 : monter le GEMDOS éjecte la cartouche et inversement. |
| rtc.c (RP5C15 Mega ST / Mega STE) | Rtc.cpp/.hpp | porté avec extensions Ricoh | Divergence assumée sur la source d'heure : modèle « paresseux déterministe » persisté dans neost.cfg (`rtc=`/`rtc_saved=`) au lieu de l'heure hôte directe. Banque 1 indépendante selon Ricoh EK-086-9908 ; aucune sortie ALARM/CLKOUT. |
| Calendrier RP5C15 et persistance entre sessions | `Rtc::advanceWholeSeconds/advanceDays` | extension déterministe corrigée | Aucun plafond à 90 000 secondes ; calcul borné par mois/cycles de 100 ans, compteur bissextile indépendant et phase conservés. |
| Ricoh EK-086-9908 p.7, note Eh/Fh write-only ; MAME rp5c15 read | `Rtc::read8/write8` | corrigé selon primaire | TEST/RESET ne relisent plus les commandes écrites. Zéro en lecture selon convention MAME, sans mesure du bus flottant réel. Commandes RESET et layout v44 conservés ; TEST inspection toujours hors modèle. |
| Ricoh EK-086-9908 p.7, note TIMER EN | `Rtc::catchUp/write8` | porté | Phase sous-seconde continue ; retenue saturante délivrée dès reprise, sauvegardée en v38. Annulation RESET/ADJ déduite de la remise à zéro des étages. |
| scu_vme.c (SCU MegaSTE/TT, `SysIntMask`/`VmeIntMask`) | Scu.hpp | porté | IPL = (SysIntState & SysIntMask & `$9F`) OU (VmeIntState & VmeIntMask & `$60`) ; MFP (niv. 6) et SCC (niv. 5) gatés par VmeIntMask. Bus VME non émulé (comme Hatari — « VME not found » CORRECT). TT hors périmètre. |
| joy.c `Joy_GetStickData` + pads STE `$FF9200/02` | JoystickInput.hpp (`stjoy::compose`) + StePads.hpp | porté | Manette USB + émulation clavier → IKBD (`$16`/`$14`) et joypads STE (même état). |
| joy.c:671 paddles analogiques `$FF9211-17` (plage `$04-$43`) | StePads.hpp + gui/AppLoop.cpp:329 (`setAnalog`) | porté | Axes bruts du stick gauche, plage grossière STE respectée. |
| joy.c:583 lightpen `$FF9220/22` | StePads.hpp:172-173 | porté | Non supporté → 0, comme Hatari. |

**Candidats précision** : aucun candidat *timing* dans ce sous-système.
Décalage GEMDOS après ACSI porté le 2026-10-19 ; conversion charset des noms
de fichiers encore à proposer comme option (défaut actuel conforme à Hatari).

---

### USART MFP — réception (actualisé le 2026-10-18)

| Référence | NeoST | Statut | Note |
|---|---|---|---|
| rs232.c:329-365 (format UCR), MAME mc68901.cpp USART | Mfp::scheduleSerialRx/onSerialRxEvent | porté pour le transport asynchrone hôte | 5..8 données masquées à droite (bits hauts zéro, largeur mémorisée au départ), parité, 1/1,5/2 stops ; retenue rationnelle, phase entière et reste sauvegardés. Reconfiguration au caractère suivant ; UCR7 /1 ou /16 et rapport Timer D exact, sans arrondis tty ni plancher RX ; repli synchrone 8N1 /16. MC68901UM §7.1.3. |
| MC68HC901 §7.3.2/p.7-8, MAME mc68901 tx_clock | `Mfp::startSerialTx/advanceSerialTx/write8` | porté avec limites | B asynchrone seulement ; intervalles BREAK sans données, TXERR si UE/IPR libres, fin TE à la frontière sans stop. Reprise par stop d’une période TX déduit ; phase TC initiale et onde abstraites. Séquence v40. |
| MAME mc68901.cpp:958-1015,1387-1432 (USART TX) | Mfp::startSerialTx/onSerialTxEvent, SERIAL_TX | porté avec limites | TDR/TSR, UCR, TE et fin différée, Buffer Empty/underrun/EOT, phase sauvegardée. Rapport Timer D exact et fraction conservée aux reconfigurations. Front TC initial, synchro et waveform BREAK restent approximatifs. Hatari émet directement vers le tty. |
| MAME `tx_clock/REGISTER_UDR` | `Mfp::onSerialTxEvent` | porté avec limite | UE mémorisé : TXERR sur nouveau front seulement ; UDR ne l’efface pas. Lecture TSR ou TE=0 réarme ; délai minimal d’une période TX sauvegardé, seuil de trame distinct du seuil UE. |
| MC68901UM §7.3.2, MAME `tx_clock` | `Mfp::finishSerialTx` | porté | END/AT immédiats au repos, différés en vol ; activation RE sans perdre RDR/TDR, IRQ sur nouveau front uniquement. |
| MC68901UM §7.2.2/7.3.2, MAME `REGISTER_RSR/REGISTER_TSR` | `Mfp::write8` | porté | RE/SS contrôlables, F/S en synchrone uniquement ; statuts matériels préservés. Droits TSR revérifiés. |
| MAME mc68901.cpp `REGISTER_RSR/REGISTER_UDR` | `Mfp::write8/read8` | porté avec limites | RE=0 efface RSR/BF/overrun sans effacer les données RX ni IPR ; UDR vide restitue RX. Réacquisition à mi-caractère encore approximative. |

### IKBD — PAUSE et octet SCI engagé (actualisé le 2026-10-18)

| Référence | NeoST | Statut | Note |
|---|---|---|---|
| `ikbd.h:44-45`, `ikbd.c:918-929,945-958` | `Ikbd::rxFree/pushRx/serialize` | porté | 1024 octets firmware en attente, indépendants du SCI et du RDR ACIA. Paquets entiers, garde de capacité au chargement. |
| `ikbd.c:835-866,917-929` | `Ikbd::armRx/onRxDeliver`, `rxShiftData_` | porté | Un seul octet engagé peut finir après PAUSE ; la file firmware attend. Boot ROM le conserve, reset matériel annule, phase sauvegardée. |
| `ikbd.c` IKBD_Send_Byte_Delay ; `acia.c` ACIA_Init/ACIA_Write_CR | `Ikbd::pushRx/pushRxDelayed`, `aciaClockInitialized_` | porté | Sorties firmware rejetées avant initialisation effective du diviseur, sans laisser de délai. CR00 identique initial ne l’arme pas ; master reset conserve le latch. Bool sauvegardé en v46 ; autres formats série non étendus. |
| `ikbd.c` IKBD_SCI_Get_Line_RX ; `acia.c` ACIA_Clock_TX/ACIA_Reset | `Ikbd::resetHw/advanceTx` | porté en /64 8N1 | Progression ACIA conservée, SCI réinitialisé et prochain tick réancré. Reprise sur zéro du fragment, huit données puis validation stop ; MARK au repos et TDR suivant pris en compte. Phases TX/SCI sauvegardées en v47. |

### IKBD — cadence commune et monitoring (actualisé le 2026-10-18)

| Référence | NeoST | Statut | Note |
|---|---|---|---|
| `ikbd.c:1688-1696,2292-2311`, `IKBD_Boot_ROM` | `Ikbd::onJoystickMonitor/sendAutoInput/bootRom` | porté | MouseAction et position absolue avant monitoring. $17 remplace la cadence commune, conservée après changement de mode ; boot rétablit 150000 en préservant l’échéance. Base fixe 8021247 comme Hatari. |

### SCC — générateur de débit (actualisé le 2026-10-18)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| `scc.c` : `SCC_Compute_BaudRate`, `SCC_Start_InterruptHandler_BRG`, `SCC_InterruptHandler_BRG` | `Scc::updateBrg/onBrgEvent`, `Scheduler::SCC_BRG_A/B` | porté | PCLK/RTxC MegaSTE, WR12/13/14, ZC=(TC+2)/source avant /2 et WR4 ; retenue rationnelle, pulse RR0 non latchée, masques et IRQ. Période mémorisée et phase sauvegardées ; caractères TX/RX temporisés par des événements distincts. |
| `scc.c:1660-1679,1986-2040,2139-2173` : TDR/TSR et événements caractère | `Scc::serialPeriod/updateSerial/onSerialEvent`, `Scheduler::SCC_TX_A/B`, `SCC_RX_A/B` | porté | Formats et horloges indépendants, file RX hôte bornée, bouclage à la fin TX. FIFO RX matérielle de trois octets portée selon Zilog/MAME ; bits inutilisés RR8=1 en asynchrone, format figé à la réception ; DPLL ouverte. |
| Zilog UM0109 WR3 p.149, MAME receive_allowed / cts_w / tra_complete | Scc::receiveAllowed/transmitAllowed/updateSerial | porté avec limites | CTS/DCD bruts, exceptions de bouclage, TX engagé terminé, DCD ne purge pas les caractères déjà en FIFO. RTS asynchrone Auto Enables différé jusqu’au dernier stop ; CTS suit la sortie physique (UM0109 p.95/p.154). |
| Zilog UM0109 WR12/13 p.167–168 et WR14 p.172 | `Scc::serialPeriod` | porté pour les caractères BRG | Rapport exact PCLK/RTxC et TC, sans arrondi baud ni seuil1Hz. Fractions TX/RX et longues suspensions sauvegardées ; Zero Count rationnel et indépendant. |
| Zilog UM0109 p.93/p.151, WR4 D3..2 | `Scc::serialPeriod/echoByteAllowed/writeControl` | corrigé selon primaire | RX asynchrone vérifie un stop ; TX conserve 1/1,5/2 stops. Auto Echo huit bits sans parité indépendant du choix des stops TX, aussi lors d’une bascule async→async en vol. Aucune reconstruction de gap/stop source physique. |
| Zilog UM0109 WR4/WR11, MAME clocks séparées | `Scc::serialPeriod` | porté pour RTxC/TRxC directs | Rapport rationnel et fractions TX/RX par canal sauvegardées ; changement source au prochain caractère. Cadence BRG des caractères désormais rationnelle ; Zero Count rationnel et indépendant. |
| Zilog UM0109 WR5 p.153 Send Break, MAME `tra_callback/tra_complete` | `Scc::updateSerial/processTX` | porté avec limites | TSR suspendu, TDR retenu, temps restant sauvegardé ; caractère interrompu non livré comme intact. Phase bit, erreurs RX et X21 non portés. |
| Zilog UM0109 WR5 p.153, Transmit Enable | `Scc::updateSerial/processTX` | porté | Désactivation TX termine la trame déjà engagée ; TDR retenu, CTS et RTS différé respectés. Départ TX sauvegardé. |
| Zilog UM0109 WR5 p.153 et chargement TSR p.114 | `Scc::copyTdrTsr/serialPeriod` | porté | TX 1..8 bits ; encodage cinq bits ou moins, masquage et longueur mémorisée dans TSR. |
| Zilog UM0109 WR3 p.149 | `Scc::deliverByte` | porté | Sync Character Load Inhibit : comparaison WR6 avant FIFO, même en asynchrone ; SDLC exclu et exception Bisync 12 bits. Aucun synchroniseur ajouté ; absent de Hatari. |
| Zilog UM0109 WR4 p.152 | `Scc::copyTdrTsr/serialWriteByte/deliverByte` | porté pour les bouclages de même largeur | RX 5/6/7 bits restitue la parité émise après les données ; format conservé en FIFO, parité du TSR sauvegardée. L’API hôte sans bits série et les formats discordants ne permettent pas de reconstruire parité/framing. |
| Zilog UM0109 p.69, MAME write_rx | `Scc::serialWriteByte/receiveByte/updateSerial` | porté au transport d’octets | Local Loopback : TxD vers sink et RX interne, RxD externe ignoré. Bascule invalide la trame hôte courante, FIFO complétée préservée ; priorité Auto Echo et prise externe sans double RX. Archive v40 inchangée. |
| Zilog UM0109 p.139/p.176, WR0 Reset Tx Underrun/EOM | `Scc::writeControl` | porté | D7–D6 indépendants des commandes/pointeurs ; statut brut effacé aussi en asynchrone, verrou externe conservé jusqu’au reset. Pas de CRC physique. |
| Zilog UM0109 p.69–70/p.172, Auto Echo | `Scc::serialWriteByte/startRxFrame/onSerialEvent` | porté pour octets connus | TSR déconnecté ; octets hôtes RX huit bits sans parité renvoyés après un stop RX abstrait, indépendamment des stops TX et de RXEN/DCD. Reconfiguration invalide l’écho courant ; trois états de trame v40. Pas de moteur de bits physiques. |
| Zilog UM0109 p.58–59, WR11 D7 | `Scc::updateRR0/writeControl` | porté avec limites | Sync/Hunt forcé zéro sans IRQ si /SYNC est utilisée par l’oscillateur ; BREAK nul en synchrone hors SDLC. Aucun moteur Hunt/SDLC ajouté. |
| Zilog UM0109 p.58 BREAK/Abort | `Scc::updateRR0/updateRR0LatchOff` | porté avec limites | Deux polarités BREAK retenues pendant un verrou existant ; répétitions coalescées, service WR0, champs v39. Pas de RX FIFO/framing synthétiques. |
| acia.c Write_CR/Set_Timers/Clock_TX | `Ikbd::write8/advanceTx`, `txClockPhase_` | porté pour reprise BREAK /64 | Phase réancrée au changement diviseur ; événements TX échus servis avant CR/TDR. TX ordinaire /64 8N1 aligné sur la même phase ; autres formats encore limités, phase sauvegardée v39. |
| Zilog UM0109 p.56–57, MAME WR1 | `Scc::updateRR0/onBrgEvent/writeControl` | porté | WR15 arme les verrous indépendamment de WR1 D0 ; ce dernier masque uniquement l’IP. WR0 ouvre les verrous et réarme selon le niveau actuel. |
| Motorola MC68HC901UM §2.10/§7.3.2, MAME rc_w/tc_w | `Mfp::internalSerialLoopback/onSerialRxEvent/onSerialTxEvent` | porté au transport d’octets | TSR H/L=11 route TX→RX, ignore SI/RC hôte ; RE conservé, pas de double livraison avec la fixture. Commutation en pleine trame sans décodage bit. Archive v44 inchangée. |
| Motorola MC68HC901UM §7.2.2 p.7-5, MAME USART RX | `Mfp::onSerialRxEvent/read8` | corrigé selon primaire | RXFULL datée à l’échéance série ; overrun invisible avant UDR, OE/RXERR publiés à cette lecture puis réception bloquée jusqu’à RSR. Étape en attente sauvée v44. |
| Zilog UM0109 p.41–42, priorité pendant INTACK | `Scc::doIACK` | corrigé selon primaire | Un IUS arrête le scan des demandes moins prioritaires, aussi par RR2 Software INTACK ; les sources supérieures restent acquittables. Lecture vectorielle de polling inchangée. |
| Motorola MC68HC901UM §4.1.3, MAME REGISTER_VR | `Mfp::read8/write8` | corrigé selon primaire | VR bits2..0 réservés lus zéro et non mémorisés à l’écriture ; base vectorielle et S conservés. Lecture d’une ancienne archive brute aussi masquée, layout v44 inchangé. |
| Zilog UM0109 p.47/p.142, WR0 Error Reset | `Scc::popRx/writeControl` | corrigé selon primaire | Retrait effectif du premier caractère par Error Reset efface aussi rxFirstPending ; suivant sans IRQ héritée, réarmement explicite conservé. Aucun nouveau champ. |
| Zilog UM0109 p.146, MAME WR1 | `Scc::writeControl` | corrigé selon primaire | Transition First/Special ↔ Special Only conserve le verrou d’erreur et l’IP déjà acquis ; Error Reset/reset et politiques Disabled/All conservés. Aucun nouveau champ. |
| Zilog UM0109 p.47–48/p.179 ; MAME `m_rx_fifo_rp_step` | `Scc::updateRR3` | corrigé selon primaire | RX All/Special : FIFO vide efface IP/IRQ même avec erreur RR1 collante ; prochains caractères restent spéciaux jusqu’à Error Reset. Modes First/Special verrouillés conservés. |
| mfp.c:1180–1219 (`MFP_GPIP_Set_Line_Input`) | `Mfp::setXsintLine/gpipSetLine/gpipInput` | corrigé | XSINT passe par le calcul complet GPIP7, bouton Multiface déjà modélisé inclus ; pas de front si niveau composé inchangé. DDR/AER conservés, aucun nouveau modèle électrique. |
| Zilog UM0109 p.157–158 (85C30), lectures étendues | `Scc::readControl` | corrigé selon primaire | WR7’ D6 seul expose WR3/4/5/7’/10 via RR9/4/5/14/11 ; WR15 D0 sélectionne seulement les écritures WR7’. En-tête de table générale ambigu sur D2 ; description spécifique 85C30 retenue. Aucun autre feature WR7’ ajouté. |
| Zilog UM0109 p.172 WR14 BRG ; p.166 RXmux | `Scc::writeControl/updateSerial` | corrigé selon primaire | D1..0 BRG ne corrompent plus l’écho RX direct ; autres changements WR14 et RX BRG/DPLL restent invalidants. Arrêt/reprise BRG conserve l’invalidation du caractère écho interrompu. |
| Zilog UM0109 p.166–168, WR11 RX et WR12/13 | `Scc::writeControl` | corrigé selon primaire | Régler TC du BRG conserve l’écho si RX utilise RTxC/TRxC directs. Invalidation BRG/DPLL conservée ; cadence BRG et Zero Count inchangées. |
| Zilog UM0109 p.166–167, WR11 TX/RX clock select | `Scc::writeControl` | corrigé selon primaire | Modifier seulement D4..3 (source TX) conserve l’écho RX engagé et sa cadence ; vrais changements RX gardent l’invalidation. Aucun moteur DPLL ajouté. |
| Zilog UM0109 p.161, WR9 D3 ; MAME IACK | `Scc::writeControl/doIACK` | corrigé selon primaire | MIE masque l’IRQ sans effacer les IUS existants ; aucun nouvel IUS créé par IACK matériel ou logiciel quand MIE=0. Reset Highest IUS reste actif. Hatari efface encore IUS en désarmant MIE. |
| `scc.c` : `SCC_ResetChannel/Full`, Zilog UM0109 p.160 | `Scc::resetChannel/resetFull` | porté avec corrections primaires | Reset canal conserve le BRG et les IP/IUS de l’autre canal, purge ses propres IUS (MAME). Hatari efface encore tout RR3A au reset A. Reset matériel annule les événements après désactivation WR14 ; l’amont omet cette annulation. |

## 11. FPU 68881 — opérations et conversions (actualisé le 2026-10-18)

Hatari ne fournit pas le socket périphérique MegaSTE. Son moteur 6888x sert
d'oracle aux opérations, via des sondes qui exercent les registres CIR de NeoST.

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| `softfloat.c` : `floatx80_move/abs/neg` | `Fpu::genOp` | porté | Précision FPCR, plage d'exposant simple/double, NaN/SNaN, zéros signés et exceptions. Banc reproductible `tools/test_fpu_unary_oracle.py`. |
| `fpp.c` : `fpu_get_constant`, `fpp_cr_undef` ; `softfloat.c` : `floatx80_round32/64` | `Fpu::romConstant`, `Fpu::command` | porté | Constantes définies et indéfinies, corrections de mantisse, codes de condition particuliers ; arrondi de mantisse avec exposant étendu. |
| `softfloat_decimal.c` ; `fpp_softfloat.c` : `fp_to_pack/from_pack` | `SoftFloatDecimal.hpp`, `Fpu::decodeFmt/encodeFmt/sfFold/completeInput` | porté | Conversions sans flottant hôte, k signé, INEX1/INEX2 et livraison CIR. Garde supplémentaire OPERR+NaN sur les étendus non canoniques pour éviter l'UB amont ; k dynamique fixé à 17. |

## Docs à corriger

Issues regroupées par fichier (suggestion en italique). Les entrées liées au tranchage
WS3 tiennent compte de l'arbre de travail.

### docs/HATARI_DIVERGENCES.md

| Ligne(s) | Problème | Suggestion |
|---|---|---|
| 33 + 525 + 50 | Ancien audit : le commit cité ne portait pas le trou MMU. | Port réellement présent depuis le 2026-08-13 ; entrée de mapping corrigée le 2026-10-18. |
| 64, 549-556 | L'« hybride WS1/WS3 à trancher » est TRANCHÉ WS3 dans l'arbre (Shifter.cpp:102-149, Machine.cpp:49-63). | *Marquer résolu, décrire la table wakestate (NEOST_WS pour A/B).* |
| 195-199 | La § bus déclare « conforme » sans mentionner le cold reset $FF8001=0 non porté (stMemory.c:93-102). | *Ajouter entrée [basse] « cold reset $FF8001=0 absent ».* |
| 91, 97, 99 | Références périmées : conversion MFP = Mfp.cpp:267-268 (pas :255), retombée IRQ = Mfp.cpp:543-544 (pas :529), wait MFP = Bus.cpp:590 (pas :540). | *Rafraîchir les trois références.* |
| 130 | « Shifter.hpp:334 » pour bordered() — dérive : c'est Shifter.hpp:404-405. | *Corriger.* |
| 148-149 | « Hatari ne touche Status qu'au cold reset (fdc.c:1233) » — inexact : FDC_Reset (fdc.c:1181) met Status=1 froid ET chaud ; fdc.c:1233 = FDC_ResetDMA qui ne touche jamais Status. | *Reformuler.* |
| 160 | Audit corrigé le 2026-10-19 : déjà 18 durées de trame, pas quatre ; deux actions portées puis compteur VBL réel ajouté en v43 pour conserver 18/36 VBL si fréquence changée en vol. | *Clos dans ce périmètre.* |
| 592-598 (liste ouverte FDC) | (1) Dérive −PendingCyclesOver des événements FDC **non inventoriée nulle part** (Fdc.cpp:1855/1899 vs fdc.c:2370) ; (2) omissions : IRQ HDC 1000 cyc, ripple-carry, hot-swap, dmaError_=false au toggle ; pas de rubrique ACSI. | *Ajouter l'entrée [moyenne] anti-dérive + consolider les 4 items + rubrique ACSI ; mettre le décompte :47 en phase.* |
| 587-588 | Audit périmé : les ombres PSG $FF8801/03 étaient déjà gérées par `Bus::wrPsg`, selon la largeur. Requalifiées le 2026-10-19. | *Clos ; banc Bus ajouté.* |
| 589-598 | S3/S4 présentés OUVERTS dans le bloc 5ᵉ passe alors que corrigés 2026-07-07 (lignes 71-72 ✅). | *Annoter « ✅ corrigé » comme pour S2.* |
| 649 | « SCC inchangé » : le routage LAN/Serial par bit7 port A PSG (psg.c:423-426 → scc.c:402-416) est absent et non inventorié. | *Ajouter entrée [basse] section SCC.* |
| 175-177, 528, 623 | Références dmaSnd périmées après le refactor non commité (shelfCoeffs à :285-309, mode_&=0x8F à :495) ; « YM nu dès PLAY=0 » périmé (drainage porté) ; « kDmaGain=0.7 vs 0.75 » contredit S3 corrigé (0.375×2 = 0.75 exact). | *Rafraîchir ; remplacer l'entrée :176 par « pas de DC-hold » ; retirer kDmaGain de :623 (garder le signe ×−1).* |
| 162-179 | Masque haut DMA son et lectures $FF8900/$FF8920 déjà portés, requalifiés le 2026-10-19. Scratch $FF8920 désormais sauvegardé v42. | *Clos pour ce périmètre.* |
| 601-617 | L'entrée S2 ne mentionne pas le reste introduit par le port : fifoRefill à chaque lecture du compteur (DmaSound.cpp:430) vs refill au HBL. | *Documenter sous S2.* |
| 646 | « TDRE ne tombe jamais sans TIE » présenté comme spécifique MIDI — l'ACIA CLAVIER a exactement le même défaut (Ikbd.cpp:202 vs acia.c:899). | *Étendre l'entrée au clavier.* |
| 181-193 | La § ACIA/IKBD n'inventorie ni le transit série CPU→IKBD absent (~10240 cyc/octet), ni customreset (instruction RESET), ni l'absence de Midi_Reset au reset machine. | *Ajouter trois entrées ([moyenne], [moyenne], [basse]).* |
| 216-264 (§ Blitter) | Deux divergences confirmées manquantes : pas d'interfoliage CycInt_Process par accès bus pendant la tranche ; (le « PRE_START 4+4 » signalé par la cartographie est lui RÉFUTÉ — ne pas l'ajouter). | *Ajouter l'entrée interfoliage [basse/moyenne].* |

### docs/CYCLE_ACCURACY.md

| Ligne(s) | Problème | Suggestion |
|---|---|---|
| 111 + 142-147 | Non-hog déjà porté ; atomicité TAS déjà garantie par le modèle CPU actuel (audit 2026-10-19). | *Restant à qualifier séparément : recouvrement CPU/blitter et autres écarts de datation.* |
| 109 + 137-140 | « RestartVideoCounter tenté puis retiré » — porté et actif depuis 2026-07-02 (VC_RESTART). | *Mettre à jour.* |
| 112 + 149-153 | « P2 Son DMA STE compteur live + FIFO » — S2 corrigé 2026-07-07. | *Passer en acquis ; reste = quantification du refill (candidat n°30).* |
| 156-159 | « VBL au cycle exact » : l'offset broche pré-armée est FAIT ; ne reste que la trame à longueur réelle (LINELEN). | *Réduire l'item.* |
| 179-182 | « constantes EMPIRIQUES » — périmé : read −6 / write +2 / −25 sont des valeurs FIDÈLES dérivées (2026-07-03). | *Mettre à jour + règle « par paire ».* |

### DEV.md

| Ligne(s) | Problème | Suggestion |
|---|---|---|
| 29 | « Blitter ST (HOG) » périmé. | *« Blitter ST (données + partage de bus hog/non-hog 64/64 cycle-exact) ».* |
| 50 | « Timer B à 400 » présenté fixe — position DE-dépendante (400/396/184, AER bit3). | *Préciser (aussi dans CYCLE_ACCURACY §3).* |
| 52-53 | « quantum sous la ligne = grand chantier » — acquis ; le chantier est le beam-sync par-ligne/LINELEN. | *Remplacer.* |
| 58-60 | Énumération mmioRead8/Write8 incomplète (blitter, joypads, SCC, SCU, cache MegaSTE, FPU). | *Compléter.* |
| 176, 181-182 | Table de mapping : « screen.c » n'existe pas (→ conv_st.c + spec512.c) ; manquent Scheduler→cycInt.c/cycles.c, StxImage→floppies/stx.c, msa/dim.c, hatari-glue.c/newcpu.c pour Cpu68k. | *Compléter la table.* |

### TODO.md

| Ligne(s) | Problème | Suggestion |
|---|---|---|
| 31 | Captain Blood « erreur clavier » — expliqué 2026-07-03 (détection AZERTY du jeu, pas un bug). | *Marquer résolu, renvoi TEST_SOFTWARE.md.* |
| 68 + 131-133 | S2/S3 et « FIFO + compteur live » listés à faire — corrigés 2026-07-07. | *Remplacer par les restes réels (MaskAddressHigh, $FF8900 pair, signe ×−1, Savinkoff).* |
| 71 | M1 GPIP encore listé — corrigé bc15a67. | *Ne garder que « UpdateTimers avant lecture IPR ».* |
| 96-102 | « read −14 / write −6 / HBL 512 » périmés (actuel : read −6, write +2, HBL par wakestate). | *Annoter.* |
| 121-122 | « Bus/MMU fait et validé » masque wait-state FDC, trou MMU STF (marqué à tort corrigé), cold reset $FF8001. | *Renvoyer vers la section Bus des divergences.* |

### CHANGELOG.md

| Ligne(s) | Problème | Suggestion |
|---|---|---|
| 96-100 + 176-178 | Valeurs beam-sync périmées (read −14/write −6/HBL 512 ; rustine +16) sans annotation. | *Entrée consolidée « datations 2026-07-03 » ou chaîne d'annotations.* |
| 156-168 | « VC wait +2 actif » — défaut réel 0 depuis RAM_SLOT défaut-ON. | *Annoter « superseded ».* |
| 260-267 | kSpec512AlignCyc « −23 » et read « −2 » — actuels : −25 et −6 ; étalon = 10/10 images. | *Entrée corrective.* |
| 461-462 | « GPIP4 câblée sur RDRF » — ancien modèle (actuel : irqActive wire-OR RX+TX clavier+MIDI). | *Reformuler.* |
| section Vidéo | Rien sur les travaux vidéo du 2026-07-03 pourtant committés (lineSnap_, datations fidèles, −25, STOP niveau-sensible). | *Ajouter 3-4 entrées datées.* |
| 904-916 | Ancienne entrée blitter (forfait 256) sans renvoi vers celle du 2026-07-07. | *Renvoi croisé (optionnel).* |

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

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 1 | **Wait states FDC/DMA portés le 2026-10-18** : 4 cycles CPU par handler, lecture SCREG/statut DMA et adresses sans attente | fdc.c (DiskController*_Word, DmaModeControl), m68000.c (WaitState) | Fdc.cpp (registerWait), tests/fdc_timing_logic.cpp | clos |
| 2 | **Ancrage anti-dérive déjà porté**, avec conservation du stall DMA | fdc.c:2310-2316, 2370 | Fdc.cpp (onFdcEvent), vérifié 2026-10-18 | clos |
| 3 | **Stall FIFO DMA déjà porté**, 32 cycles bus au flush de 16 octets | fdc.c:1340,1396 | Fdc.cpp (billDmaCycles), clos 2026-08-26 | clos |
| 4 | ~~**Conversion MFP→CPU avec reste accumulé**~~ **PORTÉE** (2026-08-14) : échéances absolues ×256, plafond vers le Scheduler, phase sérialisée (v11) ; Timer C 200 Hz = 40106,238 cycles sans dérive | cycInt.c:21-47, cycInt.h:53-63, mfp.c:1408-1423 | Mfp.cpp (`timerPeriodSubCycles`, `scheduleTimerAt`) | — |
| 5 ✅ | **Fait le 2026-09-02** (`Mfp::updateTimers` → `Scheduler::runMfpTimersTo`, NEOST_MFP_UPDTIMERS actif par défaut) — **MFP_UpdateTimers avant lecture IPR/ISR/TBDR** (poll vu ≤1 instruction en retard en mode bloc ; runTo ciblé sur les sources TIMER_* au read MMIO $FFFAxx, sans réactiver le sync-driven réfuté) | mfp.c:681-692 (+ ~30 handlers) | Mfp.cpp:63-70, 324, 418 | clos |
| 6 ✅ | **Fait le 2026-08-25 (BL4, dispatch par accès via `Scheduler::addStolenCycles`)** — **Interfoliage des événements pendant le blit** (CycInt_Process par accès bus ; écritures blitter vs capture lineSnap_/événements MFP — divergence pixel directe sur écran dessiné au blitter en cours de trame) | blitter.c:342-375 (366), 417-418, 437-438, 451-452 | `Blitter::billCycles`, `Scheduler::addStolenCycles` | clos |
| 7 ✅ | **Fait le 2026-10-19** : hooks Moira par cycle bus, recouvrement des cycles CPU internes HOG/partagé, sonde MULU identique à Hatari — **Exécution CPU parallèle pendant la tranche blitter** (MULU/DIVU recouvrent le blit ; jusqu'à ~256 cyc/tranche de recouvrement perdu — Relapse s'auto-calibre dessus ; hooks par-cycle Moira) | blitter.c:932-937, 1641-1659, 76-80 | Blitter.cpp, Cpu68k.cpp (hooks cycle bus) | clos |

### P2 — Datation d'événements localisée

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 8 ✅ | **Fait le 2026-08-12** (`Shifter::timerBPosForLine`, DE réel + 24) — **Timer B au DE RÉEL des lignes à tricks** (RIGHT_OFF → tic à 486 vs 400 : tout split raster Timer B sur ligne fullscreen ~86 cyc trop tôt ; la donnée existe déjà : glueLines_[].displayEndCycle) | video.c:2880-2889, 3161-3171 | VideoGlue.cpp:70-81 ; Machine.cpp:510-535 | clos |
| 9 | **Repositionnement Timer B à l’écriture AER3 porté le 2026-10-19** : anciens fronts échus puis nouveau front futur strict, sinon ligne suivante ; banc synthétique 50/60/mono | mfp.c:2742-2812, video.c Video_AddInterruptTimerB | `Machine::retargetTimerB`, `Mfp::write8/hblankAt` | clos |
| 10 | **Réveil STOP quantifié grille 4 cyc** (phase E-clock d'IACK mod 4 après STOP — amplificateur SHO documenté) | newcpu.c:9234 + gencpu.c:7018 | Cpu68k.cpp:1066-1106 (saut d'attente STOP ; réveil mesuré identique à l'oracle, cf. HATARI_DIVERGENCES.md) | S |
| 11 ✅ | **Fait le 2026-07-12** (kSpec512Threshold = 1) — **Supprimer le cliff kSpec512Threshold=512** (Hatari : seuil 1 ; SHO ~420 accès/trame pile sous le seuil → chemin de rendu ALTERNANT par trame, candidat n°1 des lignes raster transitoires) | configuration.c:769 + spec512.c:140-143, 221-224 | Shifter.cpp:108, 662 | clos |
| 12 ✅ | **Fait le 2026-09-02/03** (`irqUpdateNeeded_` + `Mfp::flushIrqUpdate`, NEOST_MFP_BATCH actif par défaut) — **Élection MFP chronologique par LOT** (inputs d'une même instruction ; vecteur IACK peut différer — rarissime, après n°4/5) | mfp.c:1083-1087, 1118-1125, 689-690 | Mfp.cpp:55-69 ; Mfp.hpp:140-151 | clos |
| 13 | **Atomicité TAS déjà garantie par le modèle courant**, requalifiée le 2026-10-19 : aucun dispatch Scheduler dans l’instruction, même avec BLITTER échu entre lecture/écriture | blitter.c:1668-1695 | `Cpu68k::run`, `Blitter::onSlice`, tests/blitter_rmw_logic.cpp | clos pour le modèle courant |

### P3 — Comportement / protocole (IRQ, registres, resets)

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 14 | **Instruction RESET : déjà portée** via le hook Moira | hatari-glue.c:49-77 | Cpu68k.cpp (didExecute), Bus.cpp (peripheralReset) | clos |
| 15 | **Transit CPU→IKBD porté pour /64 8N1 (2026-10-18)** | acia.c:893-902, 954-1027 ; ikbd.c:755-819 | Ikbd.cpp (write8, onTxEmpty, receiveSerialByte) | porté ; formats/diviseurs et break ouverts |
| 16 | **Délais de réponse IKBD portés le 2026-10-18** : attente globale quantifiée en bits, valeur centrale déterministe des plages Hatari ; interrogations et Froggies | ikbd.c (Send_Byte_Delay, SCI_Set_Line_TX) | Ikbd.cpp (pushRxDelayed/armRx), tests/ikbd_response_logic.cpp | clos, phase bit exacte non simulée |
| 17 | **Monitoring $17 porté le 2026-10-18** : cadence commune au taux demandé ; autosend normal à 150000 cycles | ikbd.c (SetJoystickMonitoring, InterruptHandler_AutoSend) | Ikbd.cpp (onJoystickMonitor), Scheduler::IKBD_MONITOR | porté ; phase SCI et formats ACIA non usuels restent approximatifs |
| 18 | **Garde d'initialisation portée le 2026-10-19 (`aciaClockInitialized_`, v46) ; reste la cadence pour les autres diviseurs** — **Cadence ACIA dérivée du Clock_Divider + garde init** (div 1/16 → liaison 64×/4× ; Divider==0 → IKBD muet) | acia.c:800-804 ; ikbd.c:1027-1032 | Ikbd.cpp:1156 (10240 en dur), 682 (repli 8N1/1024 hors /64) | S |
| 19 | **Drive/side push déjà porté** à chaque écriture port A PSG | psg.c:420 → fdc.c:1720-1766 | Machine.cpp (setPortASink), Fdc.cpp (refreshDriveSide) | clos |
| 20 ✅ | **IRQ HDC différée de 1000 cycles CPU post-DMA ACSI** — portée le 2026-10-18 ; acquittements de commande conservés, échéance sauvegardée | hdc.c `Acsi_DmaTransfer`, `HDC_ACSI_InterruptHandler_Update` | Fdc.cpp `acsiDmaTransfer/onAcsiIrq`, Scheduler::ACSI_IRQ | — |
| 21 | **Cold reset : déjà porté** ; constructeur/reconfigure préremplissent encore la configuration RAM | stMemory.c:93-102 ; reset.c:57 | Machine.hpp (hardReset), vérifié le 2026-10-18 | clos pour hardReset |
| 22 | **Trou MMU STF bank0=128K/bank1=2048K : déjà porté** depuis le 2026-08-13 | memory.c:1623-1631 | Bus.cpp (mmuTranslate), vérifié le 2026-10-18 | clos |
| 23 | **Décodage MFP déjà porté**, requalifié le 2026-10-19 : pairs byte fautifs, filler $FF dans les accès partiels mot/long, impairs $31–$3F void. Ricoh STE déjà couvert par le banc MMIO | ioMem.c:835-849, ioMemTabST/STE | `Bus::rdMfp/wrMfp`, tests/mfp_mmio_decode_logic.cpp | clos |
| 24 ✅ | **Porté le 2026-07-08** (`Shifter::updateGlueRes`) ; `Video_WriteToShifterRes` (res=3, écriture $FF8261) porté le 2026-10-20 (`Shifter::writeShifterRes`) ; reste le hardscroll 4 px Paulo Simoes — **V2 — tricks par changement de résolution** (med-res overscan No Cooper/PYM, stab, scrolls hw 13/9/5/1 px, $FF8261/res=3 Troed/Sync — plus gros bloc STF restant de video.c) | video.c:1618-1857, 1871-1961 ; ioMemTabST.c:71-72 | VideoGlue.cpp:731-805 ; rendu Shifter.cpp:925-960 ; NEOST_V2 (A/B) Machine.cpp:158-163 | reste S/M |
| 25 ✅ | **Porté** (branche STE le 2026-07-08, steBorderFlag_ le 2026-10-18) — **V1 — branche STE de la Glue + bSteBorderFlag 336 px** (Cuddly « trop propre » en STE, Obsession/Pacemaker faux ; faisable maintenant que WS3 est tranché) | video.c:2440-2652, 1520/1538/1603, 5826-5916 | Référence historique Shifter.cpp:977-981 ; mise à jour 2026-10-18 : bSteBorderFlag porté (steBorderFlag_), cinq étalons oracle | clos |
| 26 | **Ombres PSG impaires déjà portées**, requalifiées le 2026-10-19 : byte/movep acceptés, impairs mot/long ignorés | psg.c:577-606, 637-666 | `Bus::wrPsg`, tests/psg_odd_write_logic.cpp | clos |

### P4 — Audio (registre-exactitude et chaîne STE)

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 27 ✅ | **Fait** (horloge YM_250_HZ = 250663 le 2026-08-07 ; écritures appliquées au pas 250 kHz le 2026-09-02) — **Horloge YM réelle (YM_Freq/8) + rejeu à la frontière 250 kHz** (pitch −4,6/−2,3 cents, écart ST↔STE absent, phase R13 sync-buzzer au grain 4 µs vs 21 µs) | sound.c:234-235, 864-878, 1243 ; psg.c:342 ; clocks_timings.c:211,243 | YM2149.hpp:288 ; YM2149.cpp:386-397 | clos |
| 28 ✅ | **BUSY via gpipSetLine porté le 2026-10-19** ; XSINT DDR déjà correct, TAI indépendant du DDR GPIO | mfp.c:1180-1219 ; psg.c:390 ; MC68HC901 §5.1 | `Mfp::setBusyLine/gpipSetLine/setXsintLine` | — |
| 29 | **MIDI TDR/TSR porté (2026-10-18)** | midi.c:47,256-271,303-306 ; acia.c Clock_TX | MidiAcia.cpp (advanceTx) | porté TX huit formats, /1,/16,/64 et suspension/reprise BREAK ; RX hôte 31250 8N1, waveform ouverte |
| 30 | **Compteur DMA sans refill à la lecture déjà porté**, requalifié le 2026-10-19 | dmaSnd.c:748-761, 361, 393, 733/740 | `DmaSound::liveCounter/onHbl/fifoPull` | clos |
| 31 | **Masque haut $3F DMA son déjà porté**, requalifié le 2026-10-19 pour les profils RAM ≤4 Mo | dmaSnd.c:867, 909, 948, m68000.c:958-971 | `DmaSound::write8` | clos |
| 32 | **Lecture $FF8900 zéro et scratch $FF8920 déjà portés**, requalifiés le 2026-10-19 ; scratch ajouté au snapshot v42 | dmaSnd.c:768-771, ioMemTabSTE.c:162 | `DmaSound::read8/write8/serialize` | clos |
| 33 ✅ | **Fait le 2026-08-07** (kDmaGain négatif ; HPF du mix STE `DmaSound::applyHpfStereo`) — **Signe ×−1 du canal DMA + HPF subsonique sur le MIX** (phase relative YM/DMA inversée en mixing=1 ; DC du DMA jamais filtré — deux corrections locales pour le diff WAV bit-à-bit vs oracle) | dmaSnd.c:524-534, 557-565 ; 699, 706 | DmaSound.cpp:33, 412-415 ; YM2149.hpp:167 (`setHpfBypass`) | clos |
| 34 ✅ | **Fait le 2026-08-07** (plateaux Savinkoff du 1ᵉʳ ordre) — **Filtre LMC1992 exact (plateaux Savinkoff)** (118.276/8438.756 Hz 1ᵉʳ ordre vs RBJ 200/8000 Hz 2ᵉ ordre — timbre dès bass/treble ≠ 0 dB) | dmaSnd.c:89-92, 1262-1462, 1418-1419 | DmaSound.cpp:331-360 | clos |
| 35 | **DC-hold du dernier échantillon DMA** (offset tenu + mute YM si mixing≠1 après trame one-shot ; à trancher avant tout diff WAV sérieux) | dmaSnd.c:547-574, 150-151 | DmaSound.cpp:656-660, 678, 724 ; choix DmaSound.hpp:58-60 | S |
| 36 | **BL-R : porté le 2026-10-18**, miroir brut des registres mot | blitter.c (CheckAccess_Byte), ioMem.c | Blitter.cpp (ioRaw_) | clos |

### ✗ Candidats réfutés (mémo anti-faux-positifs)

| Candidat | Raison de la réfutation |
|---|---|
| Trancher l'hybride WS1/WS3 | Déjà tranché WS3 dans l'arbre de travail (2026-07-08) : VideoGlue.hpp:66-93 (table wakestate, défaut 3), Machine.cpp:78-92 (kHblOff défaut 0), VBL cohérent. Seule la doc retarde. |
| ~~Activer NEOST_LINELEN par défaut~~ | **Plus une réfutation : FAIT.** Le candidat avait été écarté (Cuddly résolu 250/250 LINELEN OFF, hypothèse « lignes 508 » éliminée — MOIRA_WINUAE_CONVERGENCE.md), puis le canal a été activé par défaut au tranchage WS3 (Shifter.cpp:92-95) et NEOST_LINELEN_ATTR le 2026-09-01 (V3 clos, ShifterInternal.hpp:31). |
| Atomicité MOT des écritures MMIO | Les 3 cas concrets sont déjà garantis (palette fusionnée, microwire octet bas, compteurs = SIZE_BYTE chez Hatari même) ; aucun écart restant établi. |
| FDC_UpdateAll avant chaque accès registre | L'équivalent existe aux points de consommation (indexCheckUpdate en tête de chaque événement + lecture STR) ; moteur arrêté = no-op ; aucune impulsion stale possible. |
| PSG_WaitState cas MOVEM | Omission volontaire documentée ×3 ; les mesures psg.c:143-150 sont des bancs matériels sans logiciel réel — tous les loaders cités utilisent movep, traité identiquement. |
| PRE_START blitter 4+4 vs 4 | Artefact de point de référence : les deux chemins Hatari convergent vers fin d'accès + 4 ; NeoST donne fin d'accès + 2 sur les DEUX chemins (aucune asymétrie start/restart). |
| MegaSTE : hits cache comptés | Hatari compte LUI AUSSI les hits (m68000.c:1444-1676, mem_access_before/after inconditionnels) — NeoST reproduit exactement l'oracle. |
| CopyScreenLineMono « sans Glue » (mapping) | La fonction Hatari n'a AUCUNE gestion de tricks non plus (« no overscan ») — iso-comportement, pas un écart. |
