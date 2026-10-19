# Cartographie Hatari ↔ NeoST — audit de correspondance

**But** : inventaire maître de la correspondance code-à-code entre `extern/hatari/src`
(source de vérité matérielle) et NeoST, pour **préparer les travaux de précision** :
chaque mécanisme Hatari est classé porté / partiel / absent / divergent-volontaire, avec
références `fichier:ligne` des deux côtés. Complète [`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md)
(inventaire des écarts) par la vue « où vit quoi ».

**État au 2026-07-08**, sur l'**arbre de travail** (refactor dmaSnd + tranchage WS3
**non commités** — plusieurs références de lignes diffèrent de HEAD, notamment
`DmaSound.cpp`, `Shifter.cpp:102-149`, `Machine.cpp:49-63`).

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
| ioMem.c:123-134, 277-394 (IoMem_Init + SetBusErrorRegion) | Bus.cpp:297-351 (buildIoFault) | porté | Même modèle : tout $FF8000+ faute par défaut, whitelist par machine ; tables Hatari condensées en spans. |
| ioMemTabST.c:42-195 (IoMemTable_ST) | Bus.cpp:275-283 + 320-327 | porté | Vérifié octet par octet ; blitter absent sur ST simple = fidèle (ioMem.c:367-370). |
| ioMemTabSTE.c:81-291 (IoMemTable_STE) | Bus.cpp:285-294 + 311-316 | porté | $FF8000-800F, scroll fin, FDC 12 o, DMA-son, blitter, joypads, etc. |
| ioMem.c:156-160 (FixVoidAccessForST) | Bus.cpp:329-330 | porté | Ricoh : $FF820F/$FF860F void. |
| ioMem.c:172-196 (FixVoidAccessForMegaST) | Bus.cpp:331-335 | porté | Chipset IMP ; reste la VALEUR $8A3E (0x00 vs 0xFF), déjà consigné 5ᵉ passe. |
| ioMem.c:202-236 (FixAccessForMegaSTE) | Bus.cpp:336-346 | porté | SCU impairs, cache $8E20-23, SCC, densité ; extension FPU $FFFA40 documentée (Bus.hpp:268-274). |
| ioMem.c:386-393 (shadow PSG) | Bus.cpp:325-327, 575-578, 676-680 | porté | Miroir + dispatch réel + wait-state (ex-BU1, corrigé). |
| ioMem.c:373-380 (RTC void non-Mega) | Bus.cpp:322-323, 612-613, 716-718 | porté | Dispatch gaté machineIsMega, sinon 0xFF. |
| ioMem.c:442-802, 835-867 (bget/wget + nBusErrorAccesses) | Bus.cpp:380-414 (busFaultN) + Cpu68k.cpp:294-297 | porté | Whitelist « tous les octets fautent », étage superviseur, banque de départ. Nuance : filler 0xFF des octets fautifs délégué aux puces → voir candidat n°23. |
| ioMem.c:53 (nIoMemAccessSize) | Bus.hpp:160-162 + Bus.cpp:517-563 (ioAccessWidth_) | porté | Largeur exposée aux puces (FDC, joypads) ; fuite blitter corrigée (BUS-LEAK). |
| ioMem.c:61-84 (IoAccessInstrCount) | — | volontaire | Heuristique du mode non-CE Hatari ; Moira est cycle-exact. |
| ioMem.c:719-728 (buffer écrit AVANT handlers, atomicité mot) | Bus.cpp:516-563 (dispatch octet par octet) | volontaire ✗ | Candidat « audit atomicité » **RÉFUTÉ** : les 3 cas concrets sont déjà garantis — palette fusionnée en 1 ColorWrite/mot (Shifter.cpp:800-813), microwire déclenché sur l'octet BAS après pose du haut (DmaSound.cpp:504-515), compteurs $8205/07/09 = SIZE_BYTE chez Hatari même (ioMemTabSTE.c:92-96). Blitter atomique (Bus.cpp:543-547). |
| memory.c:494-547, 1749-1832 (BusErrMem_bank, map_banks) | Bus.cpp:353-378 (busFault hors-IO) | porté | Trous $400000-$F9FFFF, $FF0000-$FF7FFF ; cartouche jamais. |
| memory.c:676-896 (SysMem_*) | Bus.cpp:388-400 + Blitter.cpp:74 | porté | Écriture $0-$7 et user <$800 fautent (CPU) ; blitter ignore sans fauter. |
| memory.c:899-926 (VoidMem → regs.db) | Bus.hpp:200-206 + Bus.cpp:210-216 (cpuDb) | porté | Latch CPU seulement (fetches vidéo/DMA exclus). |
| memory.c:1022-1076 + cart.c:117 (ROMmem, fenêtres) | Bus.hpp:129-136 + Bus.cpp:219-238, 388-392 | porté | Cartouche vide = 0xFF exactement cart.c:117 (le faux positif de la 2ᵉ passe est clos). |
| memory.c:1622-1631 (carve-out void $40000-$7FFFF si MMU 128K/2048K) | Bus.cpp (mmuTranslate) | porté | Présent depuis le 2026-08-13 ; ancienne mention « absent » périmée, revérifiée le 2026-10-18. ST/Mega ST uniquement, STE ignore les bits de banque 1. Garde : tests/mmu_logic.cpp. |
| stMemory.c:851-900 (MMU_ConfToBank + MMU_Size) | Bus.cpp:107-111, 157-164 | porté | bank1 bits 0-1 sur ST/Mega ST seulement, conf 11 → 0. |
| stMemory.c:1052-1308 (Translate_Addr STF/STE) | Bus.cpp:126-150 + 156-176 | porté | Masques RAS/CAS vérifiés terme à terme (9 cas STF). |
| stMemory.c:1005-1031 (RAM_SetBankSize) | Bus.cpp:113-124 (ramBanks) | **partiel ⚠** | 2176 et 2560 Ko manquants (le default les traiterait faux) ; sans impact tant que parseRamBytes n'expose que 256k-4m (MachineType.hpp:40-47). |
| stMemory.c:908-940 ($FF8001 R/W) | MmuGlue.hpp:26,32 lu live par mmuTranslate | porté | Pas de remap : relu à chaque accès, effet identique. |
| stMemory.c:93-102 (STMemory_Reset : $FF8001=0 à froid) | Machine.hpp (hardReset) | porté au reset | hardReset remet 0 ; reset chaud préserve. Constructeur/reconfigure préremplissent encore la configuration RAM (divergence assumée avant le TOS). Vérifié le 2026-10-18 ; tests/mmu_logic.cpp couvre chaud/froid sur les quatre profils. |
| stMemory.c:558-571, 724-768 (DMA Read/Write + CheckAddr) | Bus.cpp:481-491 (dmaRead8/dmaWrite8) | porté | Zone fautive lit 0x00, écriture perdue, jamais d'exception. |
| stMemory.c:532-592 (STAddrToPointer/CheckAreaType) | Bus.cpp:183-193 (hostRamPtr) | porté | Test de contiguïté plus strict, repli octet par octet. |
| stMemory.c:250-252 (miroir vecteurs ROM $0/$4) | Bus.cpp:510-513 (seedResetVectors) | porté | Le reste de SetDefaultConfig = fastboot, volontairement non porté. |
| cpu/debug get_iword_debug | Bus.cpp:495-506 (peek8/16) | porté | MMIO/trous → 0xFF sans dispatch ni wait state. |
| m68000.c (MegaSTE_Cache_*) + ioMemTabSTE.c:39-52 | Bus.cpp:421-473, 648-651, 742-748 | porté | 8192 lignes tag+mot, cache seulement à 16 MHz, invalidations. |
| fdc.c:4688+ (M68000_WaitState(4) sur $FF8604/06) | Bus.cpp:579-580, 681-684 (sans wait) | **absent ⚠** | CONFIRMÉ (triple : bus, FDC, CPU) : seul wait-state MMIO manquant ; PSG/MFP/ACIA sont facturés (Bus.cpp:576/590/600). Asymétrie Hatari respectée : rien sur read $8606 ni $8609/0B/0D. |
| ioMem.c:523-527, 590-594 (garde wrap $FFFFFE) | Bus.cpp:516-535 (wrap via ADDR_MASK) | volontaire | Cas dégénéré sans logiciel connu. |

## 2. Vidéo / Shifter / Glue (video.c, spec512.c, conv_st.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| video.c:810/904 (Video_Reset + Reset_Glue) | Shifter.cpp:148-204 (reset) | porté | Palette non touchée, différés annulés — conforme. |
| video.c:4620/4459/4584 (ClearOnVBL/ResetShifterTimings/InitShifterLines) | Shifter.cpp:250-321 (beginFrame) | porté | Divergence assumée : res/freq verrouillées par trame (géométrie mid-trame = canal LINELEN opt-in). |
| video.c:2197-2438, 2667-2991 (Update_Glue_State, branche STF) | Shifter.cpp:955-1135 (updateGlueState) | porté | Quasi ligne-à-ligne, glueSelfTest 19/19. Branche STE ABSENTE (V1, → candidat n°25). |
| video.c:3523 (StartHBL) | Shifter.cpp:931-948 (startHBL) | porté | Défauts par res/freq, DisplayStartCycle pré-positionné respecté. |
| video.c:3398/3882 (EndHBL + CopyScreenLineColor) | Shifter.cpp:464-593 + 1140-1479 (replayGlue/renderGlueFrame) | volontaire | Architecture différente : rendu live par-ligne + rejeu Glue fin de trame. Manquent : BORDERMASK STE, med-res par-ligne, bSteBorderFlag. |
| video.c:3798 (CopyScreenLineMono) | Shifter.cpp (renderLine hi + early-return) | porté **✗** | Le « partiel : pas de Glue/bordure en mono » est **RÉFUTÉ** : CopyScreenLineMono d'Hatari n'a AUCUNE gestion de tricks non plus (« no overscan », video.c:3497-3501) ; NeoST porte tout ce qu'elle fait réellement (avance +2 mono, LineWidth/HSCROLL différés) — iso-comportement. |
| video.c:3238-3387 (InterruptHandler_HBL) | Machine.cpp:362-392 (onHbl) + scheduleFrameEvents | porté | HBL/ligne, raise niveau 2, DmaSnd HBL, broche pré-armée. Position : **tranché WS3 dans l'arbre** (kHblOff défaut 0 → 512). |
| video.c:2231-2232, 2849-2877, 3336 (HBL_Pos/nCyclesPerLine chaîné) | Shifter.cpp:990-995, 361-396 + Machine.cpp:127-139, 386-389, 444 | **partiel ⚠** | CONFIRMÉ : canal COMPLET porté, gated `NEOST_LINELEN`. Le verrou est en DEUX moitiés de défaut différent : Machine (reprogrammation HBL + cumul `lineCarry_`) est **ON par défaut**, les 3 sites Shifter (attribution des écritures + lecture du compteur sur la grille réelle) sont **OFF par défaut** — c'est cet hybride qui est validé au pixel. `NEOST_LINELEN=0` désactive les deux, `=1` les active les deux (les sites Shifter testaient la seule PRÉSENCE de la variable jusqu'au 2026-07-29 : `=0` en activait donc la moitié Shifter, et l'A/B mesurait un hybride inverse). L'ACTIVATION complète par défaut comme candidat est réfutée (voir ✗ ci-dessous). |
| video.c:3595/3195/4850 + 2884-2888 (EndLine/TimerB_GetPosFromDE) | Machine.cpp:354-368 (onTimerB) + Shifter.hpp:131-139 | **partiel ⚠** | CONFIRMÉ : le tic Timer B n'est JAMAIS repositionné au DE réel des lignes à tricks (RIGHT_OFF → Hatari 486, NeoST 400) ; glueLines_[].displayEndCycle existe mais est inutilisé pour Timer B. → candidat n°8. |
| video.c:4926/4875 (InterruptHandler_VBL/StartInterrupts) | Machine.cpp:394-410 (onVbl) + runFrame | porté | Offsets par wakestate cohérents dans l'arbre (Machine.cpp:320-323). |
| video.c:1379-1600 (Video_CalculateAddress) | Shifter.cpp:1658-1826 (videoCounter) | porté | Reconstruction ds/CurSize, prefetch −16/+8, restart. Manquent bSteBorderFlag et lignes med. |
| video.c:5108 (ScreenCounter_ReadByte) | Shifter.cpp:1581-1618 | porté | vcDelayedOffset_ reflété ; wait défaut 0 (RAM_SLOT fournit le +2). |
| video.c:5144-5250 (ScreenCounter_WriteByte) | Shifter.cpp:1880-1905 (writeVideoCounterByte) | **partiel ⚠** | CONFIRMÉ : quirks E605 (+6 movep, video.c:5217-5220) et Tekila (+2 wrap, 5222-5235) absents de tout src/ (grep opcode 0x01c9ffc3 = 0). |
| video.c:5080/5252 (ScreenBase R/W) | Shifter.cpp:1872-1901 + read8 | porté | STE : haut/milieu effacent $FF820D, bit0 ignoré. |
| video.c:4608 + 3262-3286 (RestartVideoCounter) | Shifter.cpp:602-605 + Machine.cpp:250-254 (VC_RESTART) | porté | Ligne 310/260, freq relue live. Remise à ON de VBlank au même point portée dans vblankAtLine (2026-10-18). |
| video.c:3037/5241 (Sync_WriteByte/ReadByte) | Shifter.cpp:1892 + recordSyncWrite + read8 | porté | Filtre « même Freq » inter-trames, wait 4 cyc, datation +2. |
| video.c:1618-1860 (WriteToGlueRes, détections) | Shifter.cpp:1956-1960 (recordSyncWrite res) | **partiel ⚠** | CONFIRMÉ : filtre/latch −1 cyc/pixelShift −4 portés ; TOUTES les détections 1637-1834 absentes (med-res overscan, stab, scrolls hw 13/9/5/1 px, closure/DOLB). = V2, squelette NEOST_V2 opt-in. → candidat n°24. |
| video.c:1871+ (WriteToShifterRes) + ioMemTabST.c:71 ($FF8261) | — | **absent ⚠** | CONFIRMÉ : res=3 (shifter stoppé, hardscroll 4 px Troed/Sync) non modélisé, $FF8261 lit 0xFF / écrit no-op (seule la whitelist bus-error existe, Bus.cpp:280). |
| video.c:5373 (ColorReg_WriteWord) + conv_st.c | Shifter.cpp:1947-1966 + stColorToArgb | porté | Masque $777/$FFF, expansion 4→8 bits (demi-marche STE). |
| m68000.c (M68000_SyncCpuBus) | Shifter.cpp:799-805 (syncCpuBus) | porté | Alignement frontière 4 cyc de la fin d'accès — base du 512 cyc/ligne spec512. |
| spec512.c:152-226 (StoreCyclePalette) | Shifter.cpp:769-792 (recordColorWrite) | volontaire | Datation live Moira au cycle. ⚠ le SEUIL 512 mots/trame vs 1 chez Hatari est un écart confirmé → candidat n°11. |
| spec512.c:133-335 (StartVBL/StartScanLine/UpdatePaletteSpan) | Shifter.cpp:626-741 + kSpec512AlignCyc=−25 | porté | Modèle spans 4 cyc exact ; validé 10/10 images diapo à 0 px. |
| video.c:3676-3760 (HBLPalettes/StoreResolution) | renderLine + colorWrites_ rejoués | volontaire | Sous le seuil sans trick : palette de fin de ligne (≠ Hatari seuil 1). |
| video.c:5826-5916 + 5813 (HorScroll) | Shifter.cpp:1926-1944, 1632-1637, 422-460 | partiel | Immédiat/différé, prefetch, octet brut relu : portés. Mise à jour 2026-10-18 : latch 336 px porté dans Shifter/VideoCounter/VideoGlue, cinq étalons oracle scroll_336*. |
| video.c:5326 (LineWidth_WriteByte) | Shifter.cpp:1906-1915 + endVideoLine | porté | Immédiat si DE pas fini, sinon différé ; stride partout. |
| video.c:1299/1283/1204 (GetCyclesSinceVbl/ConvertPosition) | Machine.cpp:79-82 + offsets read −6 / write +2 | porté | Paire d'offsets fidèles §8 — ne bouger QUE par paire. ConvertPosition à longueur réelle = LINELEN opt-in. |
| video.c:598-600, 3443-3487 (signal VBlank/VSync) | VideoGlue.cpp (vblankAtLine) | porté | Comparateurs 50/60 Hz, VSYNC, palette 0 et suppression du stride ; deux étalons oracle (2026-10-18). |
| video.c:927-1074, 624 (VideoTimings WS1-4) | Shifter.cpp:102-149 (table wakestate) + Machine.cpp:49-63 | porté **✗** | L'« hybride WS1/WS3 à trancher » est **RÉFUTÉ/PÉRIMÉ** : tranché WS3 dans l'arbre de travail (2026-07-08) — wakestate() défaut 3, kWsInc +1, kHblOff défaut 0 (HBL 512), VBL 64/60 cohérent. Seule la doc (HATARI_DIVERGENCES.md:64, 549-556) reste à mettre à jour. |
| (esprit CopyScreenLineColor : copie au fil du faisceau) | Shifter.cpp:566-574 (lineSnap_) + 1323-1353, 1434-1441 | partiel | Capture datée au faisceau mais seulement sur trames à écritures freq/res ; Hatari copie TOUJOURS par-ligne. |

**✗ Réfutés (vidéo)** :
- *Activer NEOST_LINELEN par défaut* : la justification centrale (« décisif sur Cuddly ») est contredite
  par MOIRA_WINUAE_CONVERGENCE.md — Cuddly résolu 250/250 avec LINELEN OFF (datations read −6/write +2),
  l'hypothèse lignes 508 mesurée et ÉLIMINÉE ; tous les bancs passent LINELEN OFF. Le gate reste un choix
  documenté en attente de validation (Machine.cpp:120-122), pas un écart de précision mesuré.
- *Trancher l'hybride WS1/WS3* : déjà tranché WS3 dans l'arbre (voir table).

## 3. MFP 68901 (mfp.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| mfp.c:519-569 (MFP_Reset) | Mfp.cpp:30-58 + Machine.hpp:87,104 | porté | GPIP 0xFF vs 0 équivalent (entrées recalculées). |
| mfp.c:390-393, 431-451 + cycInt.c:26-45 (conversions, unités internes) | Mfp.cpp:256-269, 311, 342-344 | **partiel ⚠** | CONFIRMÉ : ratio exact 31333/9600 mais TRONQUÉ par période sans reste accumulé (Hatari : unités ×256 sub-cycle) → dérive de phase monotone timer↔faisceau (Timer C 200 Hz : 40106 vs 40106,24). → candidat n°4. |
| mfp.c:681-692 (MFP_UpdateTimers avant CHAQUE accès registre) | — (mode bloc défaut) + repli Mfp.cpp:333-341 | **partiel ⚠** | CONFIRMÉ : poll IPR/ISR/TBDR vu ≤1 instruction en retard en dispatch bloc ; repli modulo couvre les data-regs seulement. → candidat n°5. |
| mfp.c:736-799 (GetIRQ_CPU + délai 4 cyc) | Mfp.cpp:605-609, 548-560 | porté | Front visible à irqTime_+4 via événement MFP_IRQ. |
| mfp.c:893-913 (ProcessIRQ frontière) | Mfp.cpp:19-20, 553-560 (g_mfpExact bit1) | porté | Défaut NEOST_MFP_EXACT=3. |
| mfp.c:946-983 (MFP_UpdateIRQ) | Mfp.cpp:536-564 (updateIrq) | porté | Antidatage timers, retombée si bloquée — 1:1. |
| mfp.c:993-1071 (InterruptRequest/CheckPending) | Mfp.cpp:573-600 | porté | Balayage 15..0, chronologie, in-service. |
| mfp.c:1088-1126 (InputOnChannel + élection différée) | Mfp.cpp:511-528 (raise/raiseAt) | **partiel ⚠** | CONFIRMÉ : élection updateIrq(0) IMMÉDIATE par raise au lieu du LOT en fin d'instruction (MFP_UpdateNeeded) — vecteur IACK peut différer si plusieurs inputs non triés dans la même instruction. → candidat n°12. |
| mfp.c:812-857 (ProcessIACK) | Mfp.cpp:615-625 + Cpu68k readIrqUserVector | porté | NeoST plus fidèle (spurious $60 sur vecteur <0 — TODO chez Hatari). |
| mfp.c:1142-1170 (GPIP_Update_Interrupt) | Mfp.cpp:493-504 | porté | Corrigé bc15a67 (M1). |
| mfp.c:1180-1219 + 1884-1935 (Set_Line_Input, GPIP7, ACIA wire-OR) | Mfp.hpp:303-308 + Mfp.cpp:473-486 | porté | Appelants convertis (Ikbd, MidiAcia, Fdc, Blitter). |
| psg.c:388-390 (BUSY Centronics → GPIP0) | Mfp.hpp:177 (setBusyLine sans front) | **partiel ⚠** | CONFIRMÉ : busyLine_ écrit SANS gpipSetLine — le canal GPIP0 n'est JAMAIS levé sur front BUSY (aucune source SRC_GPIP0). → candidat n°28. |
| dmaSnd.c:308 (XSINT → LINE7) | Mfp.cpp:459-469 (setXsintLine) | **partiel ⚠** | CONFIRMÉ : front AER respecté mais SANS test DDR bit7 (Hatari le teste, mfp.c:1201). → candidat n°28. |
| RS232 CTS/DCD/RI → GPIP 2/1/6 | Mfp.hpp:178-180 | volontaire | Fixture de bouclage uniquement. |
| mfp.c:1228-1261 (TimerA_Set_Line_Input) | Mfp.cpp:434-451 | porté | TACR==8 strict, wrap 0→255, piloté par XSINT STE. |
| mfp.c:1269-1291 (TimerA_EventCount) | — | volontaire | Falcon uniquement, hors périmètre. |
| mfp.c:1297-1322 + video.c:3659 (TimerB_EventCount) | Mfp.cpp:415-432 + Machine.cpp:354-368 | porté | Antidatation du tic ; ligne AFFICHÉE via Glue live. |
| mfp.c:1352-1545 (StartTimer, PendingCyclesOver, modulo) | Mfp.cpp:234, 256-269, 358-413 | porté | Ancrage firingDue ≡ AddRelativeInterruptWithOffset. |
| mfp.c:1386-1393 (jitter LX, PC codé en dur) | — | volontaire | NeoST résout LX par le vrai beam-sync. |
| mfp.c:1448-1453 (TimerBEventCountCycleStart) | — (équivalent par construction) | porté | TBCR relu live au dispatch de chaque ligne. |
| mfp.c:1552-1633 (ReadTimer, règle « <1 unité au stop ») | Mfp.cpp:325-356, 298-318 | porté | + repli modulo (Captain Blood). |
| mfp.c:2552-2594 (hack TBDR event-count) | Mfp.cpp:349-355 | **partiel ⚠** | CONFIRMÉ : hack requis seulement si !CE chez Hatari ; NeoST mode bloc a la fenêtre ≤1 instruction (cf. UpdateTimers), exact en sync-driven. |
| mfp.c (M68000_WaitState(4) tous handlers) | Bus.cpp:586-590 + Cpu68k.cpp:665-667 | porté | 4 cyc/accès sans dédup, octet impair seulement. |
| mfp.c:1957-2028 (GPIP_ReadByte) | Mfp.cpp:62-72 | porté | Sorties = verrou, entrées calculées live. |
| mfp.c:2700-2731 (GPIP_WriteByte) | Mfp.cpp:109 | porté | Équivalence démontrée en commentaire (jamais de front possible). |
| mfp.c:2742-2812 (ActiveEdge : fronts + reposition Timer B bit3) | Mfp.cpp:115-116 | **partiel ⚠** | CONFIRMÉ : fronts AER portés, mais l'échéance TIMER_B de la ligne COURANTE n'est pas repositionnée à l'écriture bit3 (effet à la ligne suivante — Seven Gates of Jambala). → candidat n°9. |
| mfp.c:2856-3099 (IER/IPR/ISR/IMR) | Mfp.cpp:123-131 | porté | Clear-only, updateIrq daté au cycle d'écriture. |
| mfp.c:3106-3144 (VectorReg) | Mfp.cpp:132-143 | porté | SEI→AEI vide ISR ; nuance bénigne (updateIrq à chaque écriture). |
| mfp.c:3150-3325 (TimerCtrl) | Mfp.cpp:147-151, 277-292 | porté | « PATCH TIMER D » non porté = volontaire. |
| mfp.c:3331-3484 (TimerData) | Mfp.cpp:157-168 | porté | Timer qui court : recharge seule ; TDDR→updateSerialConfig. |
| rs232.c (USART, tty hôte) | Mfp.cpp:89-102, 171-189, 202-222 | volontaire | Config effective + bouclage 1 octet ; pas de tty hôte (assumé). |
| mfp.c (2ᵉ MFP TT, daisy-chain) | — | volontaire | Pas de TT dans NeoST. |
| ioMemTabST.c:117-148 (impairs, $FFFA31-3F void) | Bus.cpp:320 | porté | Conforme au piège DEV.md. |

## 4. FDC / DMA disque / ACSI (fdc.c, floppy.c, floppies/*, hdc.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| `floppy.c:Floppy_ReadSectors/Floppy_WriteSectors` | `Fdc::readSectorST/writeSectorST` | porté | Validation du secteur sur le média actif, même après changement de lecteur pendant transfert ; RNF sans alias face/piste, délégation STX inchangée. |
| `fdc.c:FDC_InsertFloppy/FDC_EjectFloppy` | `Fdc::loadImage/eject` | porté | Phase INDEX supprimée à l’éjection, réamorcée au montage du lecteur actif ; lecteur non sélectionné sans effet. FORCE INTERRUPT et IRQ forcée revérifiés. |
| fdc.c:2307-2372 (InterruptHandler_Update) | Fdc.cpp (onFdcEvent) | porté | Ancrage firingDue()+stall+delay déjà présent, vérifié le 2026-10-18. Le stall n’est pas absorbé par la compensation du retard. |
| fdc.c:4487+ (ExecuteCommand types I-IV) | Fdc.cpp:1782-1856 | porté | Délais PREPARE identiques. |
| fdc.c (WriteCommandRegister pendant busy) | Fdc.cpp:2000-2010 | porté | Remplacement type-à-type (Overdrive Demos). |
| fdc.c:2563/2732/2908 (Restore/Seek/Step) | Fdc.cpp:1253-1446 | porté | Spin-up 6 IP, butées, VERIFY, densité STX. |
| fdc.c:3066/3311 (Read/WriteSectors) | Fdc.cpp:1451-1615 | porté | Transfert octet-par-octet daté, multi-secteurs, WPRT. |
| fdc.c:3560/3717/3853 (ReadAddress/ReadTrack/WriteTrack) | Fdc.cpp:1620-1777 | porté | Départ à l'index, gate densité $FF860E. |
| fdc.c:2519/3991/2415/2447 (moteur/fin de commande) | Fdc.cpp:1225-1248, 1191-1222 | porté | Arrêt 9 IP, spin-up conditionné. |
| fdc.c:2016-2200 (IndexPulse_*) | Fdc.cpp:588-638 | porté | 1 604 249 cyc/tour, phase PRNG déterministe. |
| fdc.c:5127 (NextSectorID_FdcCycles_ST) | Fdc.cpp:1153-1186 | porté | Mêmes gaps, même retour NO_DRIVE. |
| fdc.c:5240-5470 (Read/WriteSector_ST etc.) | Fdc.cpp:770-844 | porté | CRC16 CCITT, writeBack au fil de l'eau. |
| fdc.c:5477 (WriteTrack_ST, TODO Hatari) | Fdc.cpp:855-886 | volontaire | NeoST PARSE le flux MFM (mieux que le LOST_DATA d'Hatari), documenté. |
| fdc.c:1311/1370 (FIFO Push/Pull) | Fdc.cpp (billDmaCycles, fifoPush/fifoPull) | porté | Stall 32 cycles bus et report sur l’échéance suivante ; D3 clos depuis 2026-08-26. |
| fdc.c:1233 (ResetDMA) | Fdc.cpp (dmaResetFifo) | porté 2026-10-18 | Préserve le statut d’erreur, annule les octets restants. Les positions de tampon amont sont mortes ; banc tests/fdc_register_logic.cpp. |
| fdc.c:1172 (FDC_Reset) | Fdc.cpp:413-451 | porté | Froid/chaud, porté à la 5ᵉ passe (bc15a67). |
| fdc.c:4760 (DiskControllerStatus_ReadWord) | Fdc.cpp (read8/write8) | porté pour UpdateAll | Index actualisé avant tous les accès WD1772, hors compteur DMA/ACSI (2026-10-18). Garde horloge live avant dispatch dans tests/fdc_selection_logic.cpp ; wait states hors de ce lot. |
| fdc.c:4924/4978 (DmaModeControl/DmaStatus) | Fdc.cpp:2016-2026, 759-765 | porté | Toggle bit 8, bits rémanents $8604 (vérifié STF). |
| fdc.c:5026-5067 (ripple-carry adresse DMA ST) | Fdc.cpp (write8) | porté 2026-10-18 | Retenue des bits 7/15 sur ST/Mega ST, avant masque d’adresse ; tests/fdc_register_logic.cpp. |
| fdc.c:5090 + m68000.c (DMA_MaskAddressHigh) | Fdc.cpp:40-48, 723/740, 2028-2037 | porté | Bit0 forcé, masque haut selon RAM, adresse relisible. |
| fdc.c:1720 (SetDriveSide PUSH depuis psg.c) | Machine.cpp (setPortASink), Fdc.cpp (refreshDriveSide) | porté | Appelé à chaque écriture R14, indépendamment du loopback série. Ancienne mention pull périmée, revérifiée le 2026-10-18. |
| fdc.c:1881/1932/1100 (densité, TransferByte) | Fdc.cpp:492-544 | porté | DD/HD/ED, porte $FF860E Mega STE, repli DD. |
| fdc.c:2227/2268 (SetIRQ/ClearIRQ) | Fdc.cpp:683-709 | porté | IRQ forcée survit au clear, GPIP5 par front. |
| fdc.c:1078-1086 (fastfdc ÷10) | Fdc.cpp:648-668 (applyFastFdc) | volontaire | Épargne les délais rotation + neutralisé sur STX (anti-casse protections). |
| floppy.c:839/765 (FindDiskDetails/DoubleCheck) | Fdc.cpp:195-227 | porté | Cracks Xenon 2/Epic/SHO. |
| floppy.c:438/481 + floppy.h:20 (transitions hot-swap) | Fdc.cpp:313-316, 367-404, 1906-1914 | partiel | WPRT pendant éjection porté ; manquent l'enchaînement 2 phases et la durée (4 trames vs 18 VBL par phase = fenêtre Mediach 4-9× plus courte). |
| floppies/msa.c + dim.c | Fdc.cpp:237-284 | porté | Lecture seule, détection par contenu. |
| floppy.c (IsWriteProtected auto) | Fdc.cpp:375-380 | porté | Permissions hôte. |
| floppies/stx.c:989-1377 (BuildStruct etc.) | StxImage.cpp:69/48/229/238 | porté | Bornes plus strictes qu'Hatari (documenté basse). |
| floppies/stx.c:1466 (NextSectorID_STX) | Fdc.cpp:914-951 | volontaire | Port fidèle + CORRECTION densité HD au-delà d'Hatari (NE PAS « corriger »). |
| floppies/stx.c:1606 (ReadSector_STX) | Fdc.cpp:955-1002 | porté | Fuzzy re-tiré, timing par bloc, overlay écriture. |
| floppies/stx.c:1714 + 364/548 (WriteSector_STX, .wd1772) | Fdc.cpp:1007-1030 + StxImage.cpp:329/372 | porté | Persistance au fil de l'eau (bénin, documenté). |
| floppies/stx.c:2027/1863 (Write/ReadTrack_STX) | Fdc.cpp:1038-1148 + StxImage.cpp:259 | volontaire | D1/D2 assumées : flux réinterprété en secteurs lisibles. |
| floppies/stx.c:1823 (ReadAddress_STX) | Fdc.cpp:1078-1091 | porté | Vrai champ ID + CRC réel. |
| hdc.c:1034 (WriteCommandPacket) | Acsi.cpp:340-366 | porté | 1:1 (6/10/12 octets, LUN). |
| hdc.c:1170 (Acsi_WriteCommandByte) | Fdc.cpp:2054-2076 | partiel | Porté sauf : INTRQ pilotée en direct sans irqSignal_/IRQ_HDC (suivi de source divergent en FDC+HDC mêlés). |
| hdc.c:1109/1253 (Acsi_DmaTransfer + 1000 cyc) | Fdc.cpp:2079-2097 | **partiel ⚠** | CONFIRMÉ : le délai de 1000 cycles avant l'IRQ HDC post-DMA (« Idris OS », hdc.c:1162-1163) manque — IRQ levée immédiatement. → candidat n°20. |
| hdc.c:627, 188-620 (HDC_Cmd_*) | Acsi.cpp:303-330, 122-301 | porté | Jeu SCSI complet, INQUIRY corrigé 3ᵉ passe. |
| hdc.c:742 (PartitionCount) | Acsi.cpp:371-395 | partiel | Manque la détection byte-swap (cosmétique). |
| fdc.c:6360-7500 + ipf.c (flux MFM/DPLL, IPF) | — | volontaire | Hors périmètre (exclusion assumée). |
| fdc.c:1569, 4724/4799 (FDC_UpdateAll avant chaque accès) | Fdc.cpp:1865, 1941-1942 | partiel **✗** | Le candidat « UpdateAll avant executeCommand/TR/SR/DR » est **RÉFUTÉ** : l'équivalent existe aux points de CONSOMMATION — indexCheckUpdate en tête de chaque événement FDC et avant lecture STR ; tous les resets/comparaisons d'indexCounter_ moteur tournant sont DANS onFdcEvent ; moteur arrêté, indexCheckUpdate est un no-op. Aucune impulsion stale possible, le fix ne changerait rien. |

## 5. PSG / YM2149 (psg.c, sound.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| psg.c:208-230 (PSG_Reset) | YM2149.hpp:110-131 | porté | Micro-écart : regs_[7]=0xFF relisible vs PSGRegisters[7]=0 chez Hatari (impact quasi nul, commentaire à préciser). |
| psg.c:252-276 (Set_SelectRegister) | YM2149.hpp:48-52 | porté | Sélecteur non masqué, ≥16 → 0xFF. |
| psg.c:283-317 (Get_DataRegister) | YM2149.hpp:37-41 | **partiel ⚠** | CONFIRMÉ : read-latch fidèle ; manque la recomposition R14 bit5/R15 depuis les joysticks port parallèle (inventorié [très basse]). |
| psg.c:324-464 (Set_DataRegister) | YM2149.hpp:54-90 + Machine.cpp (sinks) | **partiel ⚠** | CONFIRMÉ : masques/strobe/sinks portés ; manquent SCC_Check_Lan_IsEnabled MegaSTE (bit7 port A, psg.c:423-426) et FDC_SetDriveSide immédiat (→ candidat n°19). |
| psg.c:477-503 (PSG_WaitState) | Cpu68k.cpp:656-661 | partiel **✗** | Le cas MOVEM (+4/4 accès) est omis, mais l'impact est **RÉFUTÉ** : divergence volontaire triplement documentée, et les mesures psg.c:143-150 sont des bancs matériels SANS attribution logicielle — tous les loaders réels cités (ventura, ULM, SNY, TCB, X-Out) utilisent movep/move, jamais movem. |
| psg.c:510-545 (ff8800/ff880x_ReadByte) | Bus.cpp:572-578 + YM2149.hpp:35-44 | porté | Exception $FF8802 relisible = volontaire documenté (RMW cartouches diag). |
| psg.c:577-606, 637-666 (ff8801/8803_WriteByte, fix X-Out) | — | **absent ⚠** | CONFIRMÉ : Hatari accepte les écritures OCTET/movep sur les ombres impaires ; YM2149::write8 default ignore toujours addr&3∈{1,3} sans tester ioAccessWidth_. movep.w $FF8801 (X-Out) = no-op. → candidat n°26. |
| ioMem.c:386-393 (shadow $FF8804-FF) | Bus.cpp:572-578, 675-680 | porté | Hérite de l'absence ci-dessus pour les impaires. |
| psg.c:420 (SetDriveSide push) | Fdc.cpp:571-583 (pull) | volontaire ⚠ | Classé volontaire mais écart CONFIRMÉ avec impact (index ré-ancré tard) — voir candidat n°19. |
| sound.c:164-185, 735-760 (YmEnvDef/EnvBuild) | YM2149.cpp:34-43, 160-180 | porté | Identique. |
| sound.c:208 (YmVolume4to5) | YM2149.cpp:19-21 | porté | Table identique. |
| includes/ym2149_fixed_vol.h | src/core/ym2149_fixed_vol.h (nouveau, non commité) | porté | 4096 valeurs IDENTIQUES (diff des littéraux), en-tête GPL conservé. |
| sound.c:505-543 (interpolate_volumetable) | YM2149.cpp:114-154 (défaut) | porté | = YM_TABLE_MIXING, le défaut Hatari (S4 ✅). |
| sound.c:617-680 (BuildModelVolumeTable) | YM2149.cpp:93-109 (NEOST_YM_MIXING=model) | porté | Équivalent --ym-mixing model pour A/B. |
| sound.c:554-562 (BuildLinearVolumeTable) | — | absent | Option de confort, sans enjeu de fidélité. |
| sound.c:700-724 (Normalise Level>>1 STE) | YM2149.cpp:153-154 + Machine.cpp:194 | porté | Demi-amplitude par outScale_ + kLmcMakeup ×2 (S3 ✅). |
| sound.c:1411-1518 (Sound_WriteReg) | YM2149.cpp:182-211 (updateFromRegs) | porté | Recalcul par bloc, même résultat. |
| sound.c:1024-1135 (DoSamples_250) | YM2149.cpp:241-288 | porté | 1:1 (bruit 125 kHz, incrémente-puis-compare, Env boucle 32-95). |
| sound.c:969-981 (RndCompute) | YM2149.cpp:54-61 | porté | LFSR 17, taps identiques. |
| sound.c:453-466 + 1945-1952 (LowPassFilter C10) | YM2149.cpp:230-239 + Machine.cpp:197 | porté | LPF STF / PWM STE (S1 ✅). |
| sound.c:481-494 (PWMaliasFilter) | YM2149.cpp:213-223 | porté | — |
| sound.c:384-411 + 1714-1748 (Subsonic HPF, placement) | YM2149.cpp:26, 332-335 | **partiel ⚠** | CONFIRMÉ : coefficient identique mais Hatari n'applique PAS le HPF au YM en STE (il est dans dmaSnd sur le MIX) ; NeoST filtre le YM seul → DC du DMA non filtré. → candidat n°33. |
| sound.c:864-900, 1243-1258 (horloge YM_Freq/8 exacte) | YM2149.cpp:343-355 (250 000 Hz figé, rejeu au grain 48 kHz) | volontaire ⚠ | Écart CONFIRMÉ : pitch −4,6/−2,3 cents, écart ST↔STE absent, jitter écritures ≤21 µs (sync-buzzer). → candidat n°27. |
| sound.c:1349-1385 + 1708 (Resample Weighted_N) | YM2149.cpp:290-317 | porté | 16.16 identique, marge ceil+2. |
| sound.c:938-958 (Ym2149_Reset) | YM2149.hpp:110-131 | porté | Fusionné avec PSG_Reset. |
| psg.c/sound.c MemorySnapShot | — | absent | Pas de save-states (hors périmètre). |

## 6. Son DMA STE / Microwire / LMC1992 (dmaSnd.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| dmaSnd.c:240-276 (DmaSnd_Reset) | DmaSound.cpp:192-218 | porté | Fix « Brace » ; défauts LMC 0 dB au cold = divergence assumée commentée. |
| dmaSnd.c:305-322 (XSINT lines) | DmaSound.cpp:42-49 + Mfp.cpp:434, 459-476 | porté | GPIP7 + TAI ; délai 74LS164 non émulé des deux côtés. |
| dmaSnd.c:342-368 (FIFO_Refill) | DmaSound.cpp:81-111 | porté | S2 ✅ : anneau 8 o par mots, fin de trame au fetch, cas 2^24. |
| dmaSnd.c:387-409 (FIFO_PullByte) | DmaSound.cpp:115-124 | porté | Refill-sur-vide identique. |
| dmaSnd.c:419-438 (FIFO_SetStereo) | DmaSound.cpp:129-134 | porté | Réalignement mono→stéréo. |
| dmaSnd.c:461-479 (StartNewFrame) | DmaSound.cpp:58-72 | porté | start==end + repeat OFF → arrêt sec (Amberstar). |
| dmaSnd.c:727-741 (STE_HBL_Update) | DmaSound.cpp:185-190 ← Machine.cpp:374 | porté | Même granularité ligne des deux côtés. |
| dmaSnd.c:587-676 (consommation FIFO au rééchantillonnage) | DmaSound.cpp:163-178 + anneau 139-156 | volontaire | Consommation au rythme DAC daté + capture ; architecture assumée (DmaSound.hpp:104-121). |
| dmaSnd.c:748-761, 825-848 (GetFrameCount) | DmaSound.cpp:429-433 (liveCounter) | **partiel ⚠** | CONFIRMÉ : liveCounter fait un fifoRefill() à CHAQUE lecture (Hatari : au HBL ou FIFO vide) → compteur mid-ligne en avance jusqu'à +8 octets, grain 2 o au lieu de quanta FIFO. → candidat n°30. |
| dmaSnd.c:768-818 (SoundControl R/W) | DmaSound.cpp:437, 464-479 | **partiel ⚠** | CONFIRMÉ : écriture portée ; lecture de l'octet PAIR $FF8900 rend 0xFF vs 0x00 (mot posé chez Hatari), idem $FF8920. → candidat n°32. |
| dmaSnd.c:855-976 + m68000.c:958-971 (FrameStart/End + MaskAddressHigh) | DmaSound.cpp:482-487 | **partiel ⚠** | CONFIRMÉ : bit0 forcé porté ; masque $3F/$7F des octets HAUTS $FF8903/09/0F absent (stocké/relu 8 bits pleins, fetch >22 bits possible) — le masque existe pourtant côté FDC et vidéo. → candidat n°31. |
| dmaSnd.c:983-1026 (SoundModeCtrl) | DmaSound.cpp:451, 493-499 | porté | Masque 0x8F, updateDac avant changement. |
| dmaSnd.c:1037-1076 (Microwire shift 16×8 cyc) | DmaSound.cpp:272-281 + Scheduler::MICROWIRE | porté | Chaque pas daté exactement (pas besoin du rattrapage Hatari). |
| dmaSnd.c:1087-1171 (décodage LMC1992) | DmaSound.cpp:224-256 | porté | Scan des runs 1:1, codes bruts équivalents aux tables. |
| dmaSnd.c:1178-1254 (MicrowireData/Mask R/W) | DmaSound.cpp:452-457, 504-518 | porté | Écritures ignorées pendant shift, déclenché sur l'octet bas. |
| dmaSnd.c:538-680 (routage mixing + mix) | DmaSound.cpp:537-576, 582-626, 631-654 | **partiel ⚠** | CONFIRMÉ : routage et niveau ¾ portés ; manquent le signe ×−1 du LMC (phase relative YM/DMA inversée en mixing=1) et le DC-hold (choix documenté). → candidats n°33/35. |
| dmaSnd.c:1315-1348 + 577-580 (LowPass FIR) | DmaSound.cpp:529-535, 398-403 | porté | (1,2,1)/4 exact. |
| dmaSnd.c:689-713, 1262-1462 (Apply_LMC + shelves Savinkoff) | DmaSound.cpp:285-363, 373-394 | **partiel ⚠** | CONFIRMÉ : gains/saturation/×2 portés ; le filtre diffère (RBJ 2ᵉ ordre 200/8000 Hz vs Savinkoff 1ᵉʳ ordre 118.276/8438.756 Hz) et le HPF est au mauvais étage. → candidats n°33/34. |
| dmaSnd.c:282-289 (MemorySnapShot) | — | absent ⚠ | Vérifié : aucun save-state (hors périmètre comportemental, seul snapshotRtc existe). |

## 7. IKBD / ACIA / MIDI (acia.c, ikbd.c, midi.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| acia.c:757-776 (Read_SR) | Ikbd.cpp:90-105 | porté | DCD/CTS à la masse, FE/PE omis (assumé). |
| acia.c:856-881 (Read_RDR) | Ikbd.cpp:106-131 | porté | OVRN posé À LA LECTURE, séquence SR→RDR. |
| acia.c:1103-1126 (Clock_RX STOP_BIT) | Ikbd.cpp:931-949 | porté | Overrun : nouvel octet perdu, RDR conservé. |
| acia.c:472-485 (cadence 1024 cyc/bit) | Ikbd.cpp:912-929 (10240 cyc/octet) | porté | Granularité octet (écart de phase ≤1024 cyc). |
| acia.c:669-705 (MasterReset) | Ikbd.cpp:190-193 | porté | File 6301 et octet en vol conservés. |
| acia.c:785-842 (Write_CR) | Ikbd.cpp:174-195 | **partiel ⚠** | CONFIRMÉ : TIE + master reset portés ; manquent Clock_Divider (cadence en dur : RX 10240, TX 7200), mode break, et la garde « Divider==0 → IKBD muet » (ikbd.c:1027-1032). → candidat n°18. |
| acia.c:893-902 + 913-1027 (Write_TDR + Clock_TX) | Ikbd.cpp (write8, onTxEmpty, receiveSerialByte) | porté pour /64 8N1 | TDR/TSR, TDRE à la grille bit, commande au stop, remplacement du TDR occupé, reset et sauvegarde (2026-10-18). Formats/diviseurs et break restent bornés. |
| acia.c Clock_TX → ikbd.c:755-819, 891 (transit série CPU→IKBD) | Ikbd.cpp:210-241 (parse immédiat) | **absent ⚠** | CONFIRMÉ : chaque octet de commande prend effet ~10240 cyc trop tôt, multi-octets sans pacing (commentaire NeoST l'assume). Plus gros écart de timing du sous-système. → candidat n°15. |
| acia.c:715-748 (UpdateIRQ) | Ikbd.cpp:951-966 | porté | Wire-OR GPIP4 clavier+MIDI. |
| acia.c:546-561 (AddWaitCycles) | Bus.cpp:600,607 + Cpu68k.cpp:673-684 | porté | 6 cyc + E-Clock. |
| ioMemTabST.c ($FFFC01/03/05/07 void) | Bus.cpp:599,606,703,710 | porté | Un accès mot ne touche l'ACIA qu'une fois. |
| ikbd.c:499-627 + 107 (IKBD_Reset/Boot_ROM, 502000 cyc) | Ikbd.cpp:539-592 + Machine.hpp:91,108 | porté | Delay_Random omis (déterminisme headless, assumé). |
| hatari-glue.c:54-66 (customreset : RESET → IKBD/PSG/Glue) | — | **absent ⚠** | CONFIRMÉ (double : ikbd, cpu) : Moira execReset = 128 cyc sans hook ; le hook didExecute est pourtant ARMÉ pour Instr::RESET (MoiraConfig.h:94) mais jamais surchargé → fix trivial. Hatari resette aussi MFP et FDC (portée sous-estimée). → candidat n°14. |
| reset.c:111-124 (ACIA_Reset + Midi_Reset au reset machine) | Machine.hpp:86-115 (bootRom seul) | **partiel ⚠** | TIENT pour le MIDI (aucune méthode reset : control_/file survivent au reboot, non documenté). ✗ La sous-note ACIA clavier est un FAUX POSITIF : ACIA_Reset d'Hatari (acia.c:334-347) ne touche NI CR NI SR — la survie de control_/tdre_ est conforme. |
| ikbd.c:218-268 (KeyboardCommands[]) | Ikbd.cpp:133-172, 174-242, 244-537 | porté | Longueurs identiques, NOP inconnus. |
| ikbd.c:2085-2758, 3049 (Delay_Random 7000-10800 avant réponses) | Ikbd.cpp:289-531 (pushRx immédiat) | **partiel ⚠** | CONFIRMÉ : contenu octet pour octet, mais réponses ~7000-10800 cyc trop tôt (le délai Hatari est ADDITIF au transfert). → candidat n°16. |
| ikbd.c:945-959 (OutputBuffer 1024) | Ikbd.cpp:888-910 | porté | Testé avant le 1er octet (Downfall/Fokker). |
| ikbd.c (PauseOutput $13/$11) | Ikbd.cpp:256-262, 318-329, 920 | porté | Ignoré pendant la fenêtre critique (Just Bugging). |
| ikbd.c:1783-1813, 1672-1729 (AutoSend timer 150000 cyc) | Ikbd.cpp (onAutoSend, sendAutoInput) | porté | Timer indépendant du VBL, phase conservée lors des resets rapprochés (2026-10-18). |
| ikbd.c:2293-2312 (SetJoystickMonitoring, taux) | `Ikbd::onJoystickMonitor/sendAutoInput` | porté (2026-10-18) | Taux respecté, cadence commune conservée après changement de mode ; MouseAction avant monitoring. |
| ikbd.c:1429-1437, 1344, 2219-2228 (joysticks, quirks reset) | Ikbd.cpp:606-616, 665-672, 375-399, 594-604 | porté | Barbarian/Hammerfist/$12+$1A. |
| ikbd.c:1484 + $14 immédiat (Utopos) | Ikbd.cpp:618-636, 391-397 | porté | PrevJoyData même si tampon plein. |
| ikbd.c:1382-1422, 2050-2095 (paquets souris) | Ikbd.cpp:745-852, 706-718, 808-823 | porté | Seuil/échelle/axe Y/front sans mouvement (Vroom). |
| ikbd.c:1118+ + 1065-1091 (horloge BCD 6301) | Ikbd.cpp:854-886, 59-69 | porté | Année 99→00 (Captain Blood). |
| ikbd.c:386+, 3012+ (code 6301 custom, CRC) | Ikbd.cpp:982-1141, 30-45 | porté | 6 programmes au CRC ; seul le délai 7000 cyc du $83 Froggies manque (→ couvert par candidat n°16). |
| ikbd.c:1736-1757 (PressSTKey) | Ikbd.cpp:720-730 | porté | ScanCodeState, filtrage monitoring. |
| keymap.c | gui/InputCallbacks.cpp (onKey) + gui/StKeys | volontaire | Plomberie frontend (GLFW vs SDL). |
| midi.c:143-184 (MIDI_UpdateIRQ + Control_Read) | MidiAcia.cpp:21-40, 81-89 | porté | Wire-OR GPIP4. |
| midi.c:245-288 (TDR/TSR : vide après 1 bit=256 cyc) | MidiAcia.cpp (write8, advanceTx) | porté TX | Huit formats CR2..4 et /1,/16,/64 ; TDR→TSR après un bit, données et durée mémorisées, attente écrasable, IRQ indépendante et horodatages série (2026-10-18). RX hôte reste 31250 8N1. |
| acia.c:800–804,451–454,479–484 (diviseur et timer TX générique) | `MidiAcia::write8`, `txClockPhase_` | porté pour reprise BREAK | Phase ancrée au changement CR0..1 et sauvegardée v38 ; libération BREAK au prochain front de cette phase. Changement diviseur en vol recadence les fronts TSR restants ; TDR au repos now+bit reste approximatif ; pas de modèle bit à bit. |
| midi.c:294-331 (RX hôte cadencé) + acia.c (RX STOP / Read_RDR) | MidiAcia.cpp (onRxPace, pushRx, read8) | porté avec modèle ACIA | Source hôte à 2560 cycles/caractère ; un RDR, RX_Overrun puis OVRN, acquittement SR→RDR. Bouclage livré au stop TX ; le RSR ne retient pas un second caractère achevé (2026-10-18). |
| `ikbd.c:2324,2351`, table des commandes | `Ikbd::cmdLength/dispatch` | NOP comme Hatari | `$18` reconnu (1 octet), `$19` consommé (7 octets) ; ces deux modes ne sont pas émulés. `$99` reste un alias du rapport `$94/$95`. |

## 8. Blitter (blitter.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| blitter.c:271-307 (Blitter_Reset) | Blitter.cpp:49-58 | porté | NeoST remet EN PLUS la halftone RAM (négligeable). |
| blitter.c:430-453 + stMemory.c:724 (Read/WriteWord DMA) | Blitter.cpp:65-76 | porté | bus_word à chaque accès, sliceBus_ ≙ CountBusBlitter. |
| blitter.c:489-514 (SourceShift/Fetch/Read) | Blitter.cpp:370-375 | porté | Réinjection bus_word en NFSR (bug icônes GEM corrigé). |
| blitter.c:522-528 (halftone + smudge) | Blitter.cpp:376-378 | porté | — |
| blitter.c:533-564 (tables HOP) | Blitter.cpp:379-386 | porté | 4 cas identiques. |
| blitter.c:570-677 (LOP + need_src/dst) | Blitter.cpp:18-19, 421-438 | porté | 16 LOP identiques. |
| blitter.c:682-744 (ProcessWord) | Blitter.cpp:399-448 | porté | FXSR, « weird » NFSR x_count=1, suspension mid-word. |
| blitter.c:777-857 (Step) | Blitter.cpp:388-465 | porté | Endmasks, nfsr latché, halftone_line ±1. |
| blitter.c:463-481 (ContinueNonHog) | Blitter.cpp:367-368 + breaks | porté | 64 accès exacts (63 si busCountError_), état de mot persisté. |
| blitter.c:871-946 (Blitter_Start) | Blitter.cpp:160-231, 240-258, 480-491 | porté | GPIP3 ré-armée, ctrl recomposé, fenêtre CPU armée. |
| blitter.c:342-375 (AddCycles/FlushCycles → CycInt_Process PAR ACCÈS) | Blitter.cpp:81-84 (stall en bloc) | **partiel ⚠** | CONFIRMÉ : Hatari interfolie HBL/Timer B/vidéo au cycle exact PENDANT le blit ; NeoST exécute la tranche instantanément puis stalle — les écritures du blit tombent toutes du même côté d'une capture lineSnap_. Non listé dans HATARI_DIVERGENCES § Blitter. → candidat n°6. |
| blitter.c:395-421 (BusArbitration + cache MegaSTE) | Blitter.cpp:206-213, 242-248, 321 | porté | 4/8 + 4 identique. |
| blitter.c:1393-1483 (Control_WriteByte) | Blitter.cpp:88-153, 160-231, 294-299 | porté | Restart TOS, pause/reprise ; écritures mot/long atomiques (Bus.cpp:543/555). |
| blitter.c:1489-1495 (Skew + latches) | Blitter.cpp:327-334 | porté | Relecture en début de tranche = granularité identique. |
| blitter.c:1338-1367 (WordsPerLine/Lines) | Blitter.cpp:116-127, 140-148 | porté | Y 0→65536 à l'écriture (B1). |
| blitter.c:972-989 (CheckAccess_Byte) | Blitter.cpp (read8/writeRegisters) | porté 2026-10-18 | Miroir MMIO brut séparé des registres vivants ; lecture mot/long le rafraîchit, accès octet rejeté conserve le miroir. État v22. |
| blitter.c (masques matériels à l'écriture) | Blitter.cpp:41-46 (regWriteMask) | porté | Relecture fidèle. |
| blitter.c:1574-1593 (bug « 63 accès ») | Blitter.cpp:278-289 + Cpu68k.cpp:294-297, 454-468 | porté | Fenêtre PRE_START datée à l'horloge bus absolue. |
| blitter.c:1603-1626 (fenêtre CPU 64 accès réels) | Blitter.cpp:264-272 via Bus::blitterCountCpu | porté | Modèle CE (callbacks mémoire Moira). |
| blitter.c:954-961, 1641-1659 (Check_Simultaneous_CPU) | — | **absent ⚠** | CONFIRMÉ : exécution CPU parallèle pendant la tranche absente (stall en bloc) ; étalon Hatari : Relapse. → candidat n°7. |
| blitter.c:1672-1694 (do_cycles_after : PRE_START + cpu_bus_rmw) | Blitter.cpp:221-230, 269-271 | **partiel ⚠/✗** | ⚠ garde cpu_bus_rmw CONFIRMÉE absente (TAS ne retient pas la prise de bus) → candidat n°13. ✗ Le « 4+4 vs 4 au start initial » est **RÉFUTÉ** : artefact de point de référence — les deux chemins Hatari convergent vers fin d'accès + 4, et NeoST (callback à mi-accès, +4 uniforme) donne fin d'accès + 2 sur les DEUX chemins ; aucune asymétrie start/restart. BlitterStartDuringBusAccess compense un ordre interne d'Hatari qui ne peut pas se produire dans NeoST. |
| blitter.c:943, 1467, 1502-1510 (forfait non-CE 256 cyc) | Blitter.cpp:240 (onSlice) | volontaire | Seul le modèle CE (celui de l'oracle) est gardé. |
| blitter.c:895/916 (GPIP3/GPU_DONE) | Blitter.cpp:203, 306-309 + Mfp.hpp:132 | porté | Front AER. |
| blitter.c (Falcon/TT-RAM 32 bits, DSP) | — | volontaire | Sans objet. |
| blitter.c:316-334, 1516-1565 (stats/snapshots) | — | volontaire | Plomberie ; NEOST_BLIT_TRACE en remplacement. |

**✗ Réfuté (blitter)** : *MegaSTE 16 MHz — hits du cache comptés comme accès CPU* : Hatari compte
LUI AUSSI les hits du cache dans la fenêtre des 64 accès (m68000.c:1444-1676 : mem_access_before/after
appelés inconditionnellement, hit ou miss). NeoST reproduit exactement la source de vérité ; le
commentaire Blitter.hpp:27-28 documente un écart vs le VRAI matériel, pas vs l'oracle.

## 9. CPU / cycles / ordonnanceur (cycInt.c, cycles.c, clocks_timings.c, hatari-glue.c, newcpu.c)

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| cycInt.c (Add*/InsertInt, liste triée) | Scheduler.hpp:98-118, 200-205 | porté | Tableau + cache O(1) ; tie-break par énum documenté. |
| cycInt.c:531-552 + PendingInterruptCount | Machine.cpp:457-468 + Scheduler.hpp:163-180 | porté | Dispatch frontière d'instruction (CE) ; sync-driven mid-instruction RÉFUTÉ (deadlock EL), opt-in. |
| cycInt.c:340-363 (DelayedCycles, anti-dérive) | Scheduler.hpp:173-186 (firingDue) + Machine.cpp | porté | Vidéo sur grille théorique, MFP sur firingDue. ⚠ le FDC ne l'utilise pas (→ candidat n°2). |
| cycInt.c:26-45 + cycInt.h:53-63 (unités internes ×256) | Mfp.cpp:268, 311, 342 | **partiel ⚠** | CONFIRMÉ (doublon MFP) : troncature par période sans reste. → candidat n°4. |
| cycInt.c:486-497 (FindCyclesRemaining) | Scheduler.hpp:140-154 | porté | rawCyclesUntil plus fin qu'Hatari. |
| cycles.c:47 (CyclesGlobalClockCounter) | Cpu68k.cpp:149-151, 546-548 | porté | Horloge maîtresse = compteur Moira, continue. |
| cycles.c:315-321 (GetClockCounterImmediate) | Scheduler.hpp:70-77 + Machine.cpp:90 | porté | Explicitement calqué. |
| cycles.c:120-157, 282-289 (OnReadAccess CE) | Cpu68k.cpp:698-701 + read −6 | porté | Table non-CE volontairement non portée. Paire read −6 / write +2 indissociable. |
| cycles.c:167-244, 297-304 (OnWriteAccess) | Cpu68k.hpp:118-124 + write +2 | porté | Même modèle CE. |
| cycles.c:74-111 (compteur vidéo par trame) | Machine.cpp:79-82 (liveFrameClock) | porté | frameStart_ = ancre théorique. |
| clocks_timings.c:181-371 (fréquences exactes par machine) | `core/Pacing.hpp:27-36` (`kCpuHz` / `kCpuHzInt`) | **partiel ⚠** | RECOMPTÉ le 2026-08-30 : la moitié « non centralisé » de ce constat est PÉRIMÉE depuis A28 — l'horloge a UN site de définition, et les deux littéraux qui avaient survécu (`Mfp.cpp` débit série, en-tête du dump MIDI headless) y pointent désormais. Ce qui RESTE vrai, et seul : **une seule fréquence pour toutes les machines**, pas de variantes NTSC/MegaSTE. Impact limité à la synchro audio long terme (acté P3). ⚠ Les `fichier:ligne` précédents (Audio.cpp:74, DmaSound.cpp:27, Rtc.hpp:52, Mfp.cpp:226) désignaient du code sans rapport : ils n'avaient pas été revérifiés depuis A28. |
| clocks_timings.c:373-410 (nCpuFreqShift 16 MHz) | Cpu68k.cpp:53-54, 149-154, 706-717 | porté | Même invariant, horloge continue à la bascule $FF8E21. |
| clocks_timings.c:498-530 (durée VBL µs → IKBD) | Machine.cpp:405-409 | porté | 8 MHz nominal, écart ~0,27 % négligeable. |
| hatari-glue.c:85-99 (intlev) | Cpu68k.cpp:475-500 + Scu.hpp:59-73 | porté | Y compris soft IRQ1 SCU ; commit à frontière (RAISE_COMMIT=3). |
| hatari-glue.c:54-77 + newcpu.c:9168-9231 (customreset) | — | **absent ⚠** | CONFIRMÉ (cf. § IKBD) : un `reset` logiciel ne resette RIEN (timers MFP, YM, Glue, FDC conservés) — divergence de COMPORTEMENT, pas que de timing. → candidat n°14. |
| hatari-glue.c:195-325 (opcodes cartouche + CpuDoNOP) | Cpu68k.cpp:573-589 | porté | VDI/NatFeats hors-champ documenté. |
| m68000.c:791-861 (WaitState/EClock/SyncCpuBus) | Cpu68k.cpp:646-684 + chipWait8 | porté | RAM_SLOT étend l'alignement à toute la RAM (assumé, mesuré). |
| newcpu.c:2958-3019 (iack_cycle E-clock) | Cpu68k.cpp:412-448 | porté | Motif mod-20 fidèle, spurious $60. |
| newcpu.c:9234 (STOP CE, boucle 4 cyc) | Cpu68k.cpp:618-637 + patch Moira niveau-sensible | volontaire ⚠ | Téléportation à l'événement (assumé) MAIS granularité de réveil 2 cyc vs quantum 4 CONFIRMÉE → phase E-clock d'IACK mod 4 après STOP. → candidat n°10. |
| video.c:624 vs 976-982 (WS1/WS3, datation HBL) | Shifter.cpp:102-149 + Machine.cpp:49-63 | porté **✗** | **RÉFUTÉ/RÉSOLU** : tranché WS3 dans l'arbre (2026-07-08), kHblOff défaut 0, VBL par wakestate. Doc à mettre à jour. |
| mfp.c:374 (MFP_IRQ_DELAY_TO_CPU=4) | Scheduler.hpp:45-48 + Cpu68k.cpp:729-731 | porté | Sans le commit frontière, le délai s'additionnerait au pipeline IPL Moira. |
| cycInt.c:140 (CycInt_From_Opcode, rattrapage mid-opcode) | — (bloc défaut) + Mfp.cpp:337-341 | **partiel ⚠** | CONFIRMÉ (doublon MFP UpdateTimers). → candidat n°5. |
| cycles.c/cycInt.c MemorySnapShot | — | volontaire | Pas de save-states. |

---

## 10. GEMDOS HD / cartouche / RTC / SCU / joysticks (gemdos.c, cart.c, rtc.c, scu_vme.c, joy.c)

**Cartographié manuellement le 2026-07-08** (agent perdu deux fois : erreur API puis
plafond de dépense) — relecture directe des deux côtés, **sans vérification adversariale**.

| Hatari | NeoST | Statut | Note |
|---|---|---|---|
| gemdos.c:4087 `GemDOS_Trap` (dispatch) | GemdosHd.cpp:1381-1406 | porté | Même jeu d'appels interceptés (0x00, 0x0E, 0x31, 0x36, 0x39-0x43, 0x46, 0x47, 0x4B, 0x4C, 0x4E, 0x4F, 0x56, 0x57). |
| gemdos.c:1600 `GemDOS_Cconws` + `GemDOS_Super` (0x09/0x20) | — | volontaire | Chemins « mode test sans TOS » (`bUseTos=false`) ; NeoST exécute toujours un TOS (GemdosHd.cpp:6-8). |
| gemdos.c:350/1183 `Str_Filename_Atari2Host/Host2Atari` | — | partiel | Conversion de jeu de caractères désactivée par défaut (GemdosHd.cpp:10) : noms accentués hôte mal restitués côté Atari. Confort, pas timing. |
| gemdos.c `bGemdosWriteProt` (write-protect) | — | volontaire | Désactivé par défaut, documenté (GemdosHd.cpp:10). |
| gemdos.c:610/2180 `INF_CreateOverride`/`INF_Overriding` (autostart INF) | — | volontaire | INF_* inactifs (GemdosHd.cpp:9) ; recette DESKTOP.INF `#Z` headless en remplacement. |
| gemdos.c `GemDOS_InitDrives` (lecteur GEMDOS **décalé après** les partitions ACSI/IDE) | GemdosHd.cpp:393-421 `initDrives` (lecteurs dès C:) | **absent** | NeoST monte toujours dès C: → collision si une image ACSI est montée en même temps (l'UI « Disque dur » avertit depuis le 2026-07-08). Comportement, pas timing. |
| gemdos.c `GemDOS_Pexec` + `GemDOS_LoadAndReloc` | GemdosHd.cpp (opcode 9, `pexecBpCreated`) | porté | Cpu68k.cpp:574-590 : opcodes magiques 8/9/10 dans la cartouche → traités en C++ puis NOP. |
| cart.c/cartData.c (cartouche système) | GemdosHd.cpp:37-45 `Cart_data` | divergent-volontaire | Programme info renommé `NEOST.TOS`, texte = raccourcis NeoST, C-BSIZ `$1E7` (2026-07-08) ; en-tête, code 68000 et adresses `$FA0024`/`$FA002A` inchangés. |
| cart_asm.s:37 `VDI_OPCODE` (12, résolutions VDI étendues) | — | volontaire | Pas de mode VDI étendu dans NeoST ; l'opcode `$000C` est dans `Cart_data` mais jamais intercepté (Cpu68k.cpp:585 ne prend que 8-10). |
| cart.c (exclusivité cartouche externe ↔ système) | main.cpp (démarrage + menu Disque dur) | porté | Depuis le 2026-07-08 : monter le GEMDOS éjecte la cartouche et inversement. |
| rtc.c (RP5C15 Mega ST / Mega STE) | Rtc.cpp/.hpp | porté avec extensions Ricoh | Divergence assumée sur la source d'heure : modèle « paresseux déterministe » persisté dans neost.cfg (`rtc=`/`rtc_saved=`) au lieu de l'heure hôte directe. Banque 1 indépendante selon Ricoh EK-086-9908 ; aucune sortie ALARM/CLKOUT. |
| scu_vme.c (SCU MegaSTE/TT, `SysIntMask`/`VmeIntMask`) | Scu.hpp | porté | IPL = (SysIntState & SysIntMask & `$9F`) OU (VmeIntState & VmeIntMask & `$60`) ; MFP (niv. 6) et SCC (niv. 5) gatés par VmeIntMask. Bus VME non émulé (comme Hatari — « VME not found » CORRECT). TT hors périmètre. |
| joy.c `Joy_GetStickData` + pads STE `$FF9200/02` | JoystickInput.hpp (`stjoy::compose`) + StePads.hpp | porté | Manette USB + émulation clavier → IKBD (`$16`/`$14`) et joypads STE (même état). |
| joy.c:671 paddles analogiques `$FF9211-17` (plage `$04-$43`) | StePads.hpp + main.cpp (`setAnalog`) | porté | Axes bruts du stick gauche, plage grossière STE respectée. |
| joy.c:583 lightpen `$FF9220/22` | StePads.hpp:166 | porté | Non supporté → 0, comme Hatari. |

**Candidats précision** : aucun candidat *timing* dans ce sous-système. Deux candidats
*comportement* : décalage du lecteur GEMDOS derrière les partitions ACSI (S — permettrait
GEMDOS + VHD montés ensemble) ; conversion charset des noms de fichiers (S — confort).

---

### USART MFP — réception (actualisé le 2026-10-18)

| Référence | NeoST | Statut | Note |
|---|---|---|---|
| rs232.c:329-365 (format UCR), MAME mc68901.cpp USART | Mfp::scheduleSerialRx/onSerialRxEvent | porté pour le transport asynchrone hôte | 5..8 données masquées à droite (bits hauts zéro, largeur mémorisée au départ), parité, 1/1,5/2 stops ; retenue rationnelle, phase entière et reste sauvegardés. Reconfiguration au caractère suivant ; UCR7 /1 ou /16 et rapport Timer D exact, sans arrondis tty ni plancher RX ; repli synchrone 8N1 /16. MC68901UM §7.1.3. |
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
| Zilog UM0109 WR4/WR11, MAME clocks séparées | `Scc::serialPeriod` | porté pour RTxC/TRxC directs | Rapport rationnel et fractions TX/RX par canal sauvegardées ; changement source au prochain caractère. Cadence BRG des caractères désormais rationnelle ; Zero Count rationnel et indépendant. |
| Zilog UM0109 WR5 p.153 Send Break, MAME `tra_callback/tra_complete` | `Scc::updateSerial/processTX` | porté avec limites | TSR suspendu, TDR retenu, temps restant sauvegardé ; caractère interrompu non livré comme intact. Phase bit, erreurs RX et X21 non portés. |
| Zilog UM0109 WR5 p.153, Transmit Enable | `Scc::updateSerial/processTX` | porté | Désactivation TX termine la trame déjà engagée ; TDR retenu, CTS et RTS différé respectés. Départ TX sauvegardé. |
| Zilog UM0109 WR5 p.153 et chargement TSR p.114 | `Scc::copyTdrTsr/serialPeriod` | porté | TX 1..8 bits ; encodage cinq bits ou moins, masquage et longueur mémorisée dans TSR. |
| Zilog UM0109 WR3 p.149 | `Scc::deliverByte` | porté | Sync Character Load Inhibit : comparaison WR6 avant FIFO, même en asynchrone ; SDLC exclu et exception Bisync 12 bits. Aucun synchroniseur ajouté ; absent de Hatari. |
| Zilog UM0109 WR4 p.152 | `Scc::copyTdrTsr/serialWriteByte/deliverByte` | porté pour les bouclages de même largeur | RX 5/6/7 bits restitue la parité émise après les données ; format conservé en FIFO, parité du TSR sauvegardée. L’API hôte sans bits série et les formats discordants ne permettent pas de reconstruire parité/framing. |
| Zilog UM0109 p.58 BREAK/Abort | `Scc::updateRR0/updateRR0LatchOff` | porté avec limites | Deux polarités BREAK retenues pendant un verrou existant ; répétitions coalescées, service WR0, champs v39. Pas de RX FIFO/framing synthétiques. |
| acia.c Write_CR/Set_Timers/Clock_TX | `Ikbd::write8/advanceTx`, `txClockPhase_` | porté pour reprise BREAK /64 | Phase réancrée au changement diviseur ; événements TX échus servis avant CR/TDR. TX ordinaire et formats non usuels restent approximatifs ; phase sauvegardée v39. |
| Zilog UM0109 p.56–57, MAME WR1 | `Scc::updateRR0/onBrgEvent/writeControl` | porté | WR15 arme les verrous indépendamment de WR1 D0 ; ce dernier masque uniquement l’IP. WR0 ouvre les verrous et réarme selon le niveau actuel. |
| Ricoh EK-086-9908 p.7, note TIMER EN | `Rtc::catchUp/write8` | porté | Phase sous-seconde continue ; retenue saturante délivrée dès reprise, sauvegardée en v38. Annulation RESET/ADJ déduite de la remise à zéro des étages. |
| `scc.c` : `SCC_ResetChannel/Full` | `Scc::resetChannel/resetFull` | porté avec garde | Reset canal conserve le BRG. Reset matériel annule les événements après désactivation WR14 ; l'amont omet cette annulation. |

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
| 160 | L'entrée hot-swap omet l'écart de DURÉE : ~4 trames NeoST vs 18 VBL/phase Hatari (floppy.h:20) = fenêtre Mediach 4-9× plus courte. | *Compléter avec les durées.* |
| 592-598 (liste ouverte FDC) | (1) Dérive −PendingCyclesOver des événements FDC **non inventoriée nulle part** (Fdc.cpp:1855/1899 vs fdc.c:2370) ; (2) omissions : IRQ HDC 1000 cyc, ripple-carry, hot-swap, dmaError_=false au toggle ; pas de rubrique ACSI. | *Ajouter l'entrée [moyenne] anti-dérive + consolider les 4 items + rubrique ACSI ; mettre le décompte :47 en phase.* |
| 587-588 | « générateurs YM vérifiés 1:1 » — l'acceptation des écritures octet/movep sur $FF8801/03 (fix X-Out, psg.c:577-666) n'est portée nulle part et n'apparaît dans AUCUN inventaire. | *Ajouter entrée [basse/moyenne] ; mettre à jour le « ~6 » de la ligne :48.* |
| 589-598 | S3/S4 présentés OUVERTS dans le bloc 5ᵉ passe alors que corrigés 2026-07-07 (lignes 71-72 ✅). | *Annoter « ✅ corrigé » comme pour S2.* |
| 649 | « SCC inchangé » : le routage LAN/Serial par bit7 port A PSG (psg.c:423-426 → scc.c:402-416) est absent et non inventorié. | *Ajouter entrée [basse] section SCC.* |
| 175-177, 528, 623 | Références dmaSnd périmées après le refactor non commité (shelfCoeffs à :285-309, mode_&=0x8F à :495) ; « YM nu dès PLAY=0 » périmé (drainage porté) ; « kDmaGain=0.7 vs 0.75 » contredit S3 corrigé (0.375×2 = 0.75 exact). | *Rafraîchir ; remplacer l'entrée :176 par « pas de DC-hold » ; retirer kDmaGain de :623 (garder le signe ×−1).* |
| 162-179 | Deux absences non inventoriées : masque DMA_MaskAddressHigh sur $FF8903/09/0F ; lecture mot $FF8900/$FF8920 (octet pair 0xFF vs 0x00). | *Ajouter les deux entrées.* |
| 601-617 | L'entrée S2 ne mentionne pas le reste introduit par le port : fifoRefill à chaque lecture du compteur (DmaSound.cpp:430) vs refill au HBL. | *Documenter sous S2.* |
| 646 | « TDRE ne tombe jamais sans TIE » présenté comme spécifique MIDI — l'ACIA CLAVIER a exactement le même défaut (Ikbd.cpp:202 vs acia.c:899). | *Étendre l'entrée au clavier.* |
| 181-193 | La § ACIA/IKBD n'inventorie ni le transit série CPU→IKBD absent (~10240 cyc/octet), ni customreset (instruction RESET), ni l'absence de Midi_Reset au reset machine. | *Ajouter trois entrées ([moyenne], [moyenne], [basse]).* |
| 216-264 (§ Blitter) | Deux divergences confirmées manquantes : pas d'interfoliage CycInt_Process par accès bus pendant la tranche ; (le « PRE_START 4+4 » signalé par la cartographie est lui RÉFUTÉ — ne pas l'ajouter). | *Ajouter l'entrée interfoliage [basse/moyenne].* |

### docs/CYCLE_ACCURACY.md

| Ligne(s) | Problème | Suggestion |
|---|---|---|
| 111 + 142-147 | « P2 Blitter non-hog … NeoST = HOG pur » — FAUX depuis 2026-07-07 (tranches 64/64, mid-word, busCountError, pause/reprise). | *Passer en acquis ; restant réel = CPU parallèle, interfoliage, cpu_bus_rmw.* |
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
| 5 | **MFP_UpdateTimers avant lecture IPR/ISR/TBDR** (poll vu ≤1 instruction en retard en mode bloc ; runTo ciblé sur les sources TIMER_* au read MMIO $FFFAxx, sans réactiver le sync-driven réfuté) | mfp.c:681-692 (+ ~30 handlers) | Mfp.cpp:62-104 ; Bus.cpp:586-593 | M |
| 6 | **Interfoliage des événements pendant le blit** (CycInt_Process par accès bus ; écritures blitter vs capture lineSnap_/événements MFP — divergence pixel directe sur écran dessiné au blitter en cours de trame) | blitter.c:342-375 (366), 417-418, 437-438, 451-452 | Blitter.cpp:81-84, 210-214, 246-248 ; Shifter.cpp:594-599 | M |
| 7 | **Exécution CPU parallèle pendant la tranche blitter** (MULU/DIVU recouvrent le blit ; jusqu'à ~256 cyc/tranche de recouvrement perdu — Relapse s'auto-calibre dessus ; hooks par-cycle Moira) | blitter.c:932-937, 1641-1659, 76-80 | Blitter.cpp:81-84 (stall en bloc) ; Blitter.hpp:24-28 | L |

### P2 — Datation d'événements localisée

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 8 | **Timer B au DE RÉEL des lignes à tricks** (RIGHT_OFF → tic à 486 vs 400 : tout split raster Timer B sur ligne fullscreen ~86 cyc trop tôt ; la donnée existe déjà : glueLines_[].displayEndCycle) | video.c:2880-2889, 3161-3171 | Machine.hpp:182 + Shifter.hpp:131-139 (DE nominal seulement) | S |
| 9 | **Repositionnement Timer B à l'écriture AER bit3 mid-ligne** (Seven Gates of Jambala) | mfp.c:2742-2812 | Mfp.cpp:115-116 ; Machine.cpp:366-367 | S |
| 10 | **Réveil STOP quantifié grille 4 cyc** (phase E-clock d'IACK mod 4 après STOP — amplificateur SHO documenté) | newcpu.c:9234 + gencpu.c:7018 | Cpu68k.cpp:618-637 (granularité 2 cyc Moira) | S |
| 11 | **Supprimer le cliff kSpec512Threshold=512** (Hatari : seuil 1 ; SHO ~420 accès/trame pile sous le seuil → chemin de rendu ALTERNANT par trame, candidat n°1 des lignes raster transitoires) | configuration.c:769 + spec512.c:140-143, 221-224 | Shifter.cpp:43, 817, finishFrame:695 | M |
| 12 | **Élection MFP chronologique par LOT** (inputs d'une même instruction ; vecteur IACK peut différer — rarissime, après n°4/5) | mfp.c:1083-1087, 1118-1125, 689-690 | Mfp.cpp:527, 546 | M |
| 13 | **Garde cpu_bus_rmw** (TAS retient la prise de bus blitter — rare mais franc) | blitter.c:1668-1679 | Blitter.cpp:240 (onSlice sans garde) | S |

### P3 — Comportement / protocole (IRQ, registres, resets)

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 14 | **Instruction RESET : déjà portée** via le hook Moira | hatari-glue.c:49-77 | Cpu68k.cpp (didExecute), Bus.cpp (peripheralReset) | clos |
| 15 | **Transit CPU→IKBD porté pour /64 8N1 (2026-10-18)** | acia.c:893-902, 954-1027 ; ikbd.c:755-819 | Ikbd.cpp (write8, onTxEmpty, receiveSerialByte) | porté ; formats/diviseurs et break ouverts |
| 16 | **Délais de réponse IKBD portés le 2026-10-18** : attente globale quantifiée en bits, valeur centrale déterministe des plages Hatari ; interrogations et Froggies | ikbd.c (Send_Byte_Delay, SCI_Set_Line_TX) | Ikbd.cpp (pushRxDelayed/armRx), tests/ikbd_response_logic.cpp | clos, phase bit exacte non simulée |
| 17 | **Monitoring $17 porté le 2026-10-18** : cadence commune au taux demandé ; autosend normal à 150000 cycles | ikbd.c (SetJoystickMonitoring, InterruptHandler_AutoSend) | Ikbd.cpp (onJoystickMonitor), Scheduler::IKBD_MONITOR | porté ; phase SCI et formats ACIA non usuels restent approximatifs |
| 18 | **Cadence ACIA dérivée du Clock_Divider + garde init** (div 1/16 → liaison 64×/4× ; Divider==0 → IKBD muet) | acia.c:800-804 ; ikbd.c:1027-1032 | Ikbd.cpp:922 (10240 en dur), 898-910 | S |
| 19 | **Drive/side push déjà porté** à chaque écriture port A PSG | psg.c:420 → fdc.c:1720-1766 | Machine.cpp (setPortASink), Fdc.cpp (refreshDriveSide) | clos |
| 20 ✅ | **IRQ HDC différée de 1000 cycles CPU post-DMA ACSI** — portée le 2026-10-18 ; acquittements de commande conservés, échéance sauvegardée | hdc.c `Acsi_DmaTransfer`, `HDC_ACSI_InterruptHandler_Update` | Fdc.cpp `acsiDmaTransfer/onAcsiIrq`, Scheduler::ACSI_IRQ | — |
| 21 | **Cold reset : déjà porté** ; constructeur/reconfigure préremplissent encore la configuration RAM | stMemory.c:93-102 ; reset.c:57 | Machine.hpp (hardReset), vérifié le 2026-10-18 | clos pour hardReset |
| 22 | **Trou MMU STF bank0=128K/bank1=2048K : déjà porté** depuis le 2026-08-13 | memory.c:1623-1631 | Bus.cpp (mmuTranslate), vérifié le 2026-10-18 | clos |
| 23 | **Filler 0xFF des octets fautifs en accès mot partiel** (⚠ exemple phare $FF8204 FAUX — Shifter rend déjà 0xFF ; l'écart réel = octets PAIRS du MFP : move.w $FFFA00 rend 0x00xx vs 0xFFxx, et pairs latchés relisibles ; idem $FF860E/0F Ricoh. Correctif ciblé Mfp::read8/write8 plutôt que filler générique) | ioMem.c:835-849 ; ioMemTabST.c:117-141 | Mfp.cpp:103, 190 ; Fdc.cpp:1980-1981 | S |
| 24 | **V2 — tricks par changement de résolution** (med-res overscan No Cooper/PYM, stab, scrolls hw 13/9/5/1 px, $FF8261/res=3 Troed/Sync — plus gros bloc STF restant de video.c) | video.c:1618-1857, 1871-1961 ; ioMemTabST.c:71-72 | Shifter.cpp:1956-1960 seul ; squelette NEOST_V2 Machine.cpp:118-150 | L |
| 25 | **V1 — branche STE de la Glue + bSteBorderFlag 336 px** (Cuddly « trop propre » en STE, Obsession/Pacemaker faux ; faisable maintenant que WS3 est tranché) | video.c:2440-2652, 1520/1538/1603, 5826-5916 | Référence historique Shifter.cpp:977-981 ; mise à jour 2026-10-18 : bSteBorderFlag porté (steBorderFlag_), cinq étalons oracle | L |
| 26 | **Écritures ombres $FF8801/03 en accès octet/movep** (fix X-Out : musique muette ; ioAccessWidth_ déjà disponible) | psg.c:577-606, 637-666, 39-59 | YM2149.hpp:92 (default ignore) ; Bus.cpp:676-679 | S |

### P4 — Audio (registre-exactitude et chaîne STE)

| # | Candidat | Hatari | NeoST | Effort |
|---|---|---|---|---|
| 27 | **Horloge YM réelle (YM_Freq/8) + rejeu à la frontière 250 kHz** (pitch −4,6/−2,3 cents, écart ST↔STE absent, phase R13 sync-buzzer au grain 4 µs vs 21 µs) | sound.c:234-235, 864-878, 1243 ; psg.c:342 ; clocks_timings.c:211,243 | YM2149.hpp:212 (250000 figé) ; YM2149.cpp:343-355 | M |
| 28 | **XSINT et BUSY via gpipSetLine** (DDR respecté ; GPIP0 jamais levé sur front BUSY — fix trivial, le port centralisé existe) | mfp.c:1180-1219 (DDR :1201) ; psg.c:390 ; dmaSnd.c:308 | Mfp.cpp:459-469 ; Mfp.hpp:177 | S |
| 29 | **MIDI TDR/TSR porté (2026-10-18)** | midi.c:47,256-271,303-306 ; acia.c Clock_TX | MidiAcia.cpp (advanceTx) | porté TX huit formats, /1,/16,/64 et suspension/reprise BREAK ; RX hôte 31250 8N1, waveform ouverte |
| 30 | **Quantifier le refill au HBL dans liveCounter** ($FF8909 pollé mid-ligne : figé/quanta FIFO chez Hatari vs top-up immédiat — LA divergence résiduelle du port S2 ; retirer le fifoRefill de liveCounter suffit, fifoPull couvre le cas vide) | dmaSnd.c:748-761, 361, 393, 733/740 | DmaSound.cpp:429-433 | S |
| 31 | **DMA_MaskAddressHigh sur $FF8903/$FF890F** (readback ET fetch registre-exacts ; convention déjà en place côté FDC et vidéo) | dmaSnd.c:867, 909, 948 ; m68000.c:958-971 | DmaSound.cpp:482, 485 ; read8:438, 448 | S |
| 32 | **Lecture mot $FF8900 (pair = 0) et $FF8920 (= 0)** (move.w $FF8900 = 0xFF0x vs 0x000x — deux cases de switch) | dmaSnd.c:768-771 ; ioMemTabSTE.c:142, 162 | DmaSound.cpp:435-459 (default 0xFF) | S |
| 33 | **Signe ×−1 du canal DMA + HPF subsonique sur le MIX** (phase relative YM/DMA inversée en mixing=1 ; DC du DMA jamais filtré — deux corrections locales pour le diff WAV bit-à-bit vs oracle) | dmaSnd.c:524-534, 557-565 ; 699, 706 | DmaSound.cpp:24, 556-557, 601-603 ; YM2149.cpp:332-334 | S |
| 34 | **Filtre LMC1992 exact (plateaux Savinkoff)** (118.276/8438.756 Hz 1ᵉʳ ordre vs RBJ 200/8000 Hz 2ᵉ ordre — timbre dès bass/treble ≠ 0 dB) | dmaSnd.c:89-92, 1262-1462, 1418-1419 | DmaSound.cpp:285-363 | M |
| 35 | **DC-hold du dernier échantillon DMA** (offset tenu + mute YM si mixing≠1 après trame one-shot ; à trancher avant tout diff WAV sérieux) | dmaSnd.c:547-574, 150-151 | DmaSound.cpp:583-584, 608-612 ; choix DmaSound.hpp:53-58 | S |
| 36 | **BL-R : porté le 2026-10-18**, miroir brut des registres mot | blitter.c (CheckAccess_Byte), ioMem.c | Blitter.cpp (ioRaw_) | clos |

### ✗ Candidats réfutés (mémo anti-faux-positifs)

| Candidat | Raison de la réfutation |
|---|---|
| Trancher l'hybride WS1/WS3 | Déjà tranché WS3 dans l'arbre de travail (2026-07-08) : Shifter.cpp:102-149 (table wakestate, défaut 3), Machine.cpp:49-63 (kHblOff défaut 0), VBL cohérent. Seule la doc retarde. |
| Activer NEOST_LINELEN par défaut | Justification centrale contredite par le dépôt : Cuddly résolu 250/250 LINELEN OFF, hypothèse lignes 508 mesurée et éliminée (MOIRA_WINUAE_CONVERGENCE.md) ; tous les bancs passent OFF. |
| Atomicité MOT des écritures MMIO | Les 3 cas concrets sont déjà garantis (palette fusionnée, microwire octet bas, compteurs = SIZE_BYTE chez Hatari même) ; aucun écart restant établi. |
| FDC_UpdateAll avant chaque accès registre | L'équivalent existe aux points de consommation (indexCheckUpdate en tête de chaque événement + lecture STR) ; moteur arrêté = no-op ; aucune impulsion stale possible. |
| PSG_WaitState cas MOVEM | Omission volontaire documentée ×3 ; les mesures psg.c:143-150 sont des bancs matériels sans logiciel réel — tous les loaders cités utilisent movep, traité identiquement. |
| PRE_START blitter 4+4 vs 4 | Artefact de point de référence : les deux chemins Hatari convergent vers fin d'accès + 4 ; NeoST donne fin d'accès + 2 sur les DEUX chemins (aucune asymétrie start/restart). |
| MegaSTE : hits cache comptés | Hatari compte LUI AUSSI les hits (m68000.c:1444-1676, mem_access_before/after inconditionnels) — NeoST reproduit exactement l'oracle. |
| CopyScreenLineMono « sans Glue » (mapping) | La fonction Hatari n'a AUCUNE gestion de tricks non plus (« no overscan ») — iso-comportement, pas un écart. |
