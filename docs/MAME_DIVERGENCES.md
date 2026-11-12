# Divergences NeoST ↔ MAME — plan mémoire, bus errors, DMA, SCC

**But.** Inventaire des écarts entre NeoST et le pilote Atari ST de **MAME**, pour les
sous-systèmes où MAME apporte un second regard indépendant d'Hatari : plan mémoire et
MMU, bus errors, FIFO FDC/DMA, blitter, SCC Z85C30 ; en survol, MFP 68901, RTC RP5C15 et
YM2149. Item `TODO.md` « Comparaison MAME ↔ NeoST ».

**Référence et grade.** MAME est épinglé à **`ce391499d37d`**
(`MAME_PIN=ce391499d37dc21a4cf8fba85e4b774698ff2945`, `tools/setup_silicon_refs.sh`) ;
clone partiel, gitignoré, sous `extern/mame/src`. Il est de **grade D**, au même rang
qu'Hatari (`CLAUDE.md`, « Les grades de preuve ») : un écart n'appelle une correction de
NeoST que si une source de rang supérieur (A mesure, B fiche ou document Atari, C ROM)
le tranche. L'égalité avec MAME ne prouve rien de plus que l'égalité avec Hatari.

**Conventions.** Chemins MAME abrégés : `atarist.cpp`, `stmmu.cpp/.h` =
`src/mame/atari/` ; `wd_fdc.cpp`, `z80scc.cpp/.h`, `mc68901.cpp` = `src/devices/machine/` ;
`ay8910.cpp` = `src/devices/sound/`. Toute citation de la colonne MAME vaut
`@ ce391499d37d` et a été relue à cette révision. Les `fichier:ligne` NeoST sont des
**ancres** (relevées le 2026-11-12) : le **symbole** fait foi. Sévérité, du point de vue
de l'émulation : **haute** = casse un logiciel ou un boot · **moyenne** = fidélité visible ·
**basse** = cas-limite. Netlist du GST MCU (4081) : citée d'après `Bus.hpp` et
`HATARI_DIVERGENCES.md` § 7 « Bus, mémoire, bus error », pas relue pour ce document ; de
même, les citations RTL (`gstmcu.v`, `fx68k.sv`) sont reprises du code NeoST qui les porte.

**Ajouter une entrée.** Identifiant `MA-n` suivant, une ligne de table dans la section du
sous-système, citation vérifiée des deux côtés, arbitrage nommant la source la plus haute
connue (ou `[F]` avec ce qui le trancherait). Si l'écart est tranché, le reporter dans la
section finale.

**Pourquoi un document séparé.** `HATARI_DIVERGENCES.md` reste l'**inventaire maître**
des écarts NeoST ↔ Hatari. Un rôle par document : ici une autre référence, d'autres
fichiers et une autre révision ; mélanger les deux brouillerait les ancres et les dates de
relecture. Quand un écart MAME est aussi un écart Hatari, la ligne le dit et renvoie au
document maître.

**Ce que le clone ne permet pas de comparer.** L'implémentation du blitter
(`st_blitter_device`, `ataristb.h` inclus par `atarist.cpp:5`), la vidéo (`stvideo.h`,
`atarist_v.h`, l. 6 et 9) et le cœur `m68000` de MAME sont absents du clone. Le **Mega STE
de MAME n'a pas de carte mémoire** : `megaste_super_map` est vide (`atarist.cpp:944-946`)
et remplace celle du STE (`atarist.cpp:1548`) ; son SCC et son RTC sont instanciés sans
adresse (l. 1549-1550), le registre cache `$FF8E21` n'est pas câblé (l. 250-251). Tout ce
qui est propre au Mega STE n'est donc comparable qu'au niveau de la puce.

---

## Plan mémoire et MMU

| # | Écart | Sév. | NeoST | MAME @ ce391499d37d | Arbitrage |
|---|---|---|---|---|---|
| MA-1 | Remappage RAS/CAS du ST quand `$FF8001` déclare une banque d'une autre taille que la puce. Sur les six combinaisons, quatre diffèrent (rapport mot à mot sur la banque 0) : config 128 Ko/puce 512 Ko et config 512 Ko/puce 2 Mo, la moitié des mots ; config 128 Ko/puce 2 Mo, les trois quarts ; config 2 Mo/puce 128 Ko, la moitié (MAME perd le bit d'adresse 18 : `$40000` → `$0`, NeoST → `$10000`). Identiques : 512 Ko/128 Ko et 2 Mo/512 Ko. | moyenne | `mmuXlatSTF`, `Bus.cpp:280-296` (port d'Hatari) | `remap_*`, `stmmu.cpp:353-381` | [F]. Hatari attribue sa table à la rétro-ingénierie du MMU par C. Zietz et à des programmes de test sur ST réel (`stMemory.c`, bloc « Description of the MMU used in STF/STE », révision non relue ici) : meilleur appui connu, non reproduit. Tranché par : motifs écrits puis relus avec `$FF8001` sur-déclaré, sur un vrai STF (puces 256 Kbit). |
| MA-2 | Config 128 Ko ou 512 Ko sur puce de 2 Mo : MAME n'écrit pas où il lit. L'écriture passe par `remap_512_128`, la lecture par `remap_128_2048`/`remap_512_2048`. | moyenne | Un seul `Bus::mmuTranslate` sert lecture et écriture (`Bus.cpp:547`, `:879`) | lecture `stmmu.cpp:408,410,436,438` ; écriture `:409,411,437,439` | **MAME a tort** : une DRAM relit ce qu'on vient d'y écrire ; défaut de recopie évident. |
| MA-3 | STE : MAME applique le remappage du ST. Son propre commentaire dit les lignes entrelacées et le décodage linéaire sur STE. | moyenne | `mmuXlatSTE`, `Bus.cpp:298-304`, choisi `:357-358` | un seul `configure_ram` pour toutes les machines, `stmmu.cpp:475-506` ; commentaire `:350-351` | **MAME incohérent** avec son commentaire ; NeoST suit le modèle STE d'Hatari. |
| MA-4 | Banque 1 du STE : MAME honore les bits 0-1 de `$FF8001` ; NeoST calque la banque 1 sur la banque 0. | basse | `Bus::mmuTranslate`, `Bus.cpp:339-341` (arbitrage `:317-338`) | `stmmu.cpp:487-488` | [F]. Témoignages d'usage (FAQ comp.sys.atari.st § 3.1.1, existence de SIMMFIX2) en faveur de NeoST, pas de mesure. Déjà dans `HATARI_DIVERGENCES.md` (RAM 2176/2560 Ko). |
| MA-5 | Banque 0 configurée « 11 » sur STE : 0 Ko chez MAME (`bank_sizes[3] = 0`, et `ram_mapping` n'a pas de cas 0 : rien n'est installé ni retiré), 2 Mo chez NeoST. Sur ST, les deux la traitent comme vide. | basse | `mmuBank0Size`, `Bus.cpp:260-263` | `stmmu.cpp:486-487`, `:387-472` | **MAME a tort sur STE** : [B] netlist 4081P18 (RS3 = 1 prend le chemin 2 Mo quel que soit RS2). |
| MA-6 | `$FF8001` relu sur STE : 8 bits chez MAME, 4 chez NeoST. | basse | `Bus::mmioRead8`, `Bus.cpp:1306` | `memcfg_r`, `stmmu.cpp:322-325` | **MAME a tort sur STE** : [B] netlist 4081 `REGISTER.PIN` (latches ID0-3, IDB4-7 à VSS). |
| MA-7 | Tailles de RAM : MAME ne connaît que 256 Ko, 512 Ko, 1, 2 et 4 Mo ; NeoST ajoute 128, 640, 2176 et 2560 Ko. | basse | `ramBanks`, `Bus.cpp:265-278` | `configure_ram`, `stmmu.cpp:478-484` | MAME en retard (configurations réelles mais rares). |
| MA-8 | Lecture hors banque peuplée (au-delà de la config, banque vide) : `$FF` chez MAME, dernier mot du bus chez NeoST. | basse | `Bus::ramSpaceRead8`, `Bus.cpp:878-888` | `nop_readwrite`, `stmmu.cpp:498-502` + `unmap_value_high`, `atarist.cpp:806` | [F], déjà déclaré dans `Bus::read8Slow` (`Bus.cpp:461-468`). Tranché par : lecture d'une banque vide sur un vrai ST. |
| MA-9 | Fenêtres ROM du STE. MAME : ROM `$E00000-$E3FFFF`, faute sur `$D00000-$EFFFFF` hors ROM, `$FC0000-$FEFFFF` non câblé (lit `$FF`, sans faute). NeoST : `$E00000-$EBFFFF` lu, `$EC0000-$EFFFFF` faute, `$D00000-$D7FFFF` et `$FE0000-$FE1FFF` bus ouvert, reste de `$FC0000-$FEFFFF` faute. | haute | `romWindowSize`, `Bus.hpp:234-237` ; `steRomSelectVoid`, `Bus.hpp:247-251` ; `Bus::busFault`, `Bus.cpp:680-684` | `ste_super_map`, `atarist.cpp:900,902` | **MAME a tort** : [B] netlist 4081P2, porte AF8 `ROMXB` (DTACK de /ROM0-6 et /ROMP seulement). Cas réel : IK+ sur STE fait une bus error à `$FC000C` comme le matériel (`CASE_STUDIES.md`). |
| MA-10 | Registre « gamecart » `$FF9000` du STE (déménagement du port cartouche en `$D80000`) : absent de MAME. | basse | `Bus::mmioRead8`/`mmioWrite8`, `Bus.cpp:1309,1318` ; `steCartWindow`, `Bus.hpp:259-261` | non câblé (`ste_super_map`, `atarist.cpp:894-937`) | MAME en retard ; [B] netlist 4081P10. |
| MA-11 | Mode utilisateur sur STE : MAME réutilise la carte utilisateur du ST, où la ROM n'est lisible qu'en `$FC0000-$FEFFFF` ; `$E00000` y fait une bus error. NeoST ne protège que `$0-$7FF` et l'espace IO. | basse | `Bus::busFaultNSlow`, `Bus.cpp:769-771` | `atarist.cpp:1344` (toutes machines), `:840-841` | **MAME incohérent** avec sa carte superviseur STE (`atarist.cpp:902`). |
| MA-12 | Mega ST : carte superviseur sans aucune bus error (ni écriture `$0-$7`, ni trous `$400000-$F9FFFF`, ni IO non décodé). | moyenne | mêmes règles que le ST, `Bus::busFault`, `Bus.cpp:654-685` | `megast_super_map`, `atarist.cpp:851-887` | MAME en retard (sa carte ST, l. 803-833, en a). |
| MA-13 | Mega ST : `$FFFA40-$FFFA57` rend `$0802` à toute lecture, rustine déclarée « HACK » pour une cartouche de diagnostic. NeoST : bus error (pas de coprocesseur hors Mega STE). | basse | `Bus::buildIoFault`, `Bus.cpp:647` | `fpu_r`, `atarist.cpp:290-294`, câblé `:883` | MAME simplifié, de son propre aveu. |

**Concordant.** Lecture CPU de `$0-$7` servie par la ROM, RAM installée à partir de `$8`
(`atarist.cpp:807`, `stmmu.cpp:492-495` ↔ `Bus::read8Slow`, `Bus.cpp:453`) ; écriture
`$0-$7` fautive (`atarist.cpp:808` ↔ `Bus.cpp:759`) ; ROM ST de 192 Ko à `$FC0000`,
écriture fautive (`atarist.cpp:810-811` ↔ `romWindowSize`) ; port cartouche vide sans
faute, lu `$FF` ; `$FF8001` conservé par un reset à chaud (`stmmu.cpp:62-63` ↔
`Machine::hardReset` ne le remet à 0 qu'à froid, `Machine.hpp:160`) ; espace RAM de 4 Mo.

---

## Bus errors et décodage IO

| # | Écart | Sév. | NeoST | MAME @ ce391499d37d | Arbitrage |
|---|---|---|---|---|---|
| MA-14 | ST : octets IO non décodés qui fautent chez NeoST mais lisent `$FF` chez MAME : `$FF8000`, `$FF8002-$FF80FF`, les octets non câblés de `$FF8200-$FF82FF`, `$FF8600-$FF8603`, `$FF8608/0A/0C/0E`, `$FF8610-$FF86FF`, les octets pairs `$FFFA00-$FFFA3E` et `$FFFA40-$FFFAFF`. MAME ne déclare la faute que sur des plages entières de 256 octets ou plus. | moyenne | `Bus::buildIoFault`, liste `ST_OK`, `Bus.cpp:576-584`, MFP `:621`, chipset ST `:630-631` | `st_super_map`, `atarist.cpp:826-832` | [F] : pas de netlist du GLUE (`Bus.cpp:1084-1086`). NeoST suit la carte octet par octet d'Hatari, dont les sondes de diagnostic dépendent. Tranché par : la netlist du GLUE ou un balayage `move.b` sur un vrai ST. |
| MA-15 | ST : `$FFFD00-$FFFDFF` fautif chez MAME, non fautif chez NeoST. | basse | `Bus.cpp:624` | `atarist.cpp:832` | [F] sur ST. Sur STE, [D] `gstmcu.v:175 @ 4a6db7cc` (VPA sur tout `$FFFC00-$FFFDFF`, `Bus.cpp:1290-1291`) soutient NeoST. |
| MA-16 | STE : aucune bus error dans `$FF0000-$FFFFFF` chez MAME ; le matériel absent (SCC, SCU, FPU) ne faute jamais, et les sondes par bus error ne détectent rien. | haute | liste `STE_OK`, `Bus.cpp:586-595`, `:611-617` | `ste_super_map`, `atarist.cpp:894-937` (seulement `$0-$7` et `$400000-$F9FFFF`, l. 899-900) | **MAME a tort** : [B] netlist 4081 (DTACK des seuls registres décodés, chien de garde `/BERR` 4081P11). |
| MA-17 | Accès octet à un registre mot : `$FF8604-$FF8607` et `$FF9200`, `$FF9220-$FF9223` fautent chez NeoST ; MAME les sert. | basse | `Fdc::read8`/`write8`, `Fdc.cpp:3306-3311`, `:3401-3406` ; `Bus::rdStePads`, `Bus.cpp:1145-1149` | handlers 16 bits, `stmmu.cpp:32-33` ; `portr`, `atarist.cpp:926,932-933` | [F] : Hatari ([D]) contre MAME ([D]). Déjà le modèle d'Hatari. |
| MA-18 | Pads STE : MAME n'a aucun handler d'écriture (ni faute, ni sélection de rangée : les ports sont lus tels quels). NeoST : sélection par `$FF9202/03` (bascule JOYWE), toute autre écriture fait bus error. | moyenne | `Bus::wrStePads`, `Bus.cpp:1172-1193` ; `rdStePads`, `:1140-1171` | `atarist.cpp:926-933` | **MAME a tort** : [B] netlist 4081 (`BUTR`/`PADR`/`PENR` contiennent IRWZ); JOYWE détaillé dans `HATARI_DIVERGENCES.md` § Bus. |
| MA-19 | ACIA, octet impair sur STE : MAME masque la voie basse (`umask16(0xff00)`), l'accès n'atteint pas la puce. NeoST : vrai cycle de la puce (un `move.b #3,$FFFC01` remet l'ACIA clavier à zéro). | basse | `Bus::aciaOddByteIsChipCycle`, `Bus.cpp:1068-1090` | `atarist.cpp:935-936` | NeoST soutenu : [D] `gstmcu.v:175,181 @ 4a6db7cc` (RTL dérivé du schéma, arbitre provisoire) et [B] schéma Sparrow f. 2/5 (câblage analogue des MC6850), cités `Bus.cpp:1070-1083`. Sur ST, NeoST et MAME concordent. |

**Concordant.** Bus error après 64 cycles (`before_delay` de 64, `atarist.cpp:808,809,826-832`
↔ `kBerrTimeoutWait` = 60 + 4, `Cpu68k.cpp:463-472`, [B] « Atari MegaST Bus » § 3.11 ; la
sémantique exacte de `before_delay` dans le cœur `m68000` n'est pas relue, hors clone) ;
trous `$400000-$F9FFFF` et `$FF0000-$FF7FFF` fautifs sur ST ; mode utilisateur : `$0-$7FF`
et `$FF0000+` fautifs (`atarist.cpp:839,843` ↔ `Bus.cpp:769-771`), ROM lisible ; ST sans
blitter ni son DMA : `$FF8900-$FF8AFF` fautif (`atarist.cpp:819,830` ↔ `Bus.cpp:612,617`).

---

## FDC WD1772 et DMA

| # | Écart | Sév. | NeoST | MAME @ ce391499d37d | Arbitrage |
|---|---|---|---|---|---|
| MA-20 | Sens écriture (RAM → disque) : MAME précharge une FIFO double de 32 octets et décompte le secteur au 32ᵉ bloc ; NeoST recharge 16 octets et décompte au 33ᵉ (modèle Hatari). | basse | `Fdc::fifoPull`, `Fdc.cpp:1441-1463` | `fifo_schedule_block_transfer_from_ram`, `stmmu.cpp:274-319` | [F], déjà déclaré `Fdc.cpp:1437-1440` (puce DMA C025913 absente de l'archive ASIC). MAME rejoint MiSTery `dma.v`. Tranché par : `$FF8606` bit 1 et compteur relus pendant un Write Sector de deux secteurs. |
| MA-21 | `$FF8606` bits 3-15 : 0 chez MAME, dernier mot passé par `$FF8604` chez NeoST. | basse | `Fdc::dmaStatusWord`, `Fdc.cpp:1490-1496` | `dma_status_r`, `stmmu.cpp:149-152` | [F] ici : Hatari ([D]) le dit vérifié sur STF réel, mesure non citée. |
| MA-22 | `$FF8606` bit 2 : DRQ du FDC chez MAME, toujours 0 chez NeoST. | basse | `Fdc.cpp:1491-1495` (masque `0xfff8`) | `stmmu.cpp:151` | [F]. Tranché par : la documentation Atari de la puce DMA, ou `$FF8606` lu pendant un transfert. |
| MA-23 | Compteur de secteurs relu (mode SCREG) : `$55` fixe chez MAME, mot rémanent de `$FF8604` chez NeoST. Les deux s'accordent : le compteur ne se relit pas. | basse | `Fdc::read8`, `Fdc.cpp:3319-3322` | `data_r`, `stmmu.cpp:113-115` | [F]. |
| MA-24 | Écrire 0 dans le compteur : erreur posée tout de suite chez MAME ; chez NeoST, seulement au premier octet transféré à compte nul. | basse | `Fdc.cpp:3414` ; `fifoPush`/`fifoPull`, `:1415`, `:1442` | `data_w`, `stmmu.cpp:135-136` | [F]. |
| MA-25 | Bascule du bit 8 de `$FF8606` : MAME remet aussi le statut à « pas d'erreur » ; NeoST conserve l'erreur (modèle Hatari `FDC_ResetDMA`). | basse | `Fdc::dmaResetFifo`, `Fdc.cpp:1465-1473`, appel `:3452` | `fifo_flush`, `stmmu.cpp:206-212`, appel `:156-157` | [F]. |
| MA-26 | Adresse DMA : MAME garde 24 bits relisibles, bit 0 compris, et ne masque (`& $3FFFFE`) qu'à l'accès RAM ; NeoST borne l'octet haut à `$3F` (≤ 4 Mo), force le bit 0 à 0 et propage la retenue du compteur STF à l'écriture. | basse | `dmaAddressMask`, `Fdc.cpp:59-71` ; `Fdc::writeDmaAddressByte`, `:1475-1488` | `stmmu.cpp:174-203`, `:261` | [F] : Hatari ([D]) contre MAME ([D]). |
| MA-27 | Le DMA de MAME écrit la RAM physique sans passer par le décodage des banques ; celui de NeoST passe par le MMU (alias compris). | basse | `Bus::dmaWrite8`/`dmaRead8`, `Bus.cpp:853-874` ; `Fdc.cpp:1421,1450` | `m_ram[(m_dma_address & 0x3ffffe) >> 1]`, `stmmu.cpp:261,283,302` | **MAME incohérent** : son compteur DMA vit dans le MMU (`stmmu.h:13`), qui décode les banques. Visible seulement avec `$FF8001` ≠ RAM posée. |
| MA-28 | Durée des transferts : synchrones chez MAME, sans vol de cycles ; NeoST vole 32 cycles par bloc de 16 octets. | moyenne | `Fdc.cpp:1353-1359`, `:1425` | « Synchronous for now », `stmmu.cpp:259,279` ; TODO « floppy DMA transfer timer », `atarist.cpp:37` | MAME en retard, de son propre aveu. |
| MA-29 | ACSI (disque dur) : absent de MAME. | moyenne | `Fdc::writeAcsi`, `acsiDmaTransfer` (`Fdc.cpp:3416`, `:3455-3456`) | `hdc_transfer`, `stmmu.cpp:88-91` ; `data_r`/`data_w`, `:117-121`, `:140-141` | MAME en retard. |
| MA-30 | Attente sur `$FF8604/06` : 4 cycles chez NeoST (port Hatari), aucune chez MAME. | basse | `Fdc::registerWait`, `Fdc.cpp:1498-1505` | `atarist.cpp:814` | [F] : [D] contre [D]. |
| MA-31 | Instruction RESET : MAME ne réinitialise pas la partie DMA du MMU (mode, adresse, FIFO) ; NeoST la remet au repos. | basse | `Bus::peripheralReset`, `Bus.cpp:133` → `Fdc::reset`, `Fdc.cpp:1011-1014` | `reset_w`, `atarist.cpp:307-320` | [F]. |
| MA-32 | WD1772, arrêt moteur : MAME coupe au 8ᵉ index, alors que son commentaire dit « 9 revs according to spec » ; NeoST attend 9 tours. | basse | `IP_MOTOR_OFF`, `Fdc.cpp:168` ; `Fdc::updateMotorStop`, `:2421` | `index_callback`, `wd_fdc.cpp:1538-1546` | **MAME a tort** : [B] WD177X-00 p. 1-7 (« remains idle for 9 revolutions »). |

**Concordant.** Pas de pas du WD1772 6/12/2/3 ms (`wd_fdc.cpp:3163`, `clock_ratio` = 4
l. 2559, horloge 8 MHz `atarist.cpp:1360`) ; sélection de face et de lecteur par le port A
du YM, face inversée (`atarist.cpp:1138-1149`) ; INTRQ → GPIP5 inversé (`atarist.cpp:1361`) ;
FIFO de lecture vidée en RAM tous les 16 octets (`stmmu.cpp:227-228` ↔ `Fdc::fifoPush`) ;
bascule du bit 8 qui vide la FIFO et le compteur ; registres WD1772 choisis par A1/A0 de
`$FF8606`. Plusieurs comportements du WD1772 sont déjà cités comme concordants dans
`Fdc.cpp` (l. 2013, 2791, 2937, 3022). Non comparé : `$FF860E` (Mega STE).

---

## Blitter

**Non comparé.** L'implémentation (`st_blitter_device`) est dans `ataristb.cpp`, absent du
clone (`atarist.cpp:5` n'en inclut que l'en-tête). Seul le câblage se compare, et il est
**concordant** : registres `$FF8A00-$FF8A3D` sur Mega ST et STE (`atarist.cpp:870-881`,
`:914-925`), absents du ST (`:819`, plage fautive `:830`) ↔ `Bus::buildIoFault`
(`Bus.cpp:617`) ; horloge 8 MHz (`atarist.cpp:1468,1509`) ; fin de blit sur GPIP3 du MFP
(`:1470,1511`). Pour confronter le blitter : `docs/STBLITTER_BENCH.md` (RTL et netlist
ST-4082, de rang supérieur à MAME).

---

## SCC Z85C30 (Mega STE)

| # | Écart | Sév. | NeoST | MAME @ ce391499d37d | Arbitrage |
|---|---|---|---|---|---|
| MA-33 | Le SCC de MAME n'a ni adresse ni IRQ (carte Mega STE vide) ; NeoST le câble en `$FF8C80-$FF8C87`, IRQ de niveau 5. | — | `Bus::mmioTable`, `Bus.cpp:1251` ; `Bus.cpp:643` | `atarist.cpp:1550`, `:944-946` | Non comparable au niveau système. |
| MA-34 | Variante : MAME instancie un SCC **NMOS** (`SCC8530`) ; NeoST modélise le **Z85C30** CMOS. Conséquence visible : l'acquittement logiciel (WR9 D5) n'existe que sur CMOS/ESCC, et MAME le réserve à ces variantes, donc l'ignore sur son instance Atari. | basse | `Scc.hpp:2`, `:15` ([D] Hatari `scc.c:31 @ f0736b24`) | `atarist.cpp:1550` ; `z80scc.h:419,425` ; `z80scc.cpp:1527` | [F] sur la puce posée : [B] UM0109 p. 44-45 (« Software Interrupt Acknowledge (CMOS/ESCC) ») ne dit que la conséquence. Tranché par : la référence de la puce sur le schéma du Mega STE. |
| MA-35 | Valeurs de reset : MAME garde WR3 bit 0 (réception autorisée) et efface le reste, met WR4 = `$04` et WR5 = `$00` au reset de canal, et garde WR9 bits 2-5 au reset matériel en effaçant VIS et NV. | basse | `Scc::resetChannel`, `Scc.cpp:64-113` (WR3 `&= $FE`, WR4 `\|= $04`, WR5 `&= $61`, WR9 `&= $03 \| $C0`, l. 71 et 80) | `z80scc.cpp:1153-1156` ; `:509-510` | **MAME a tort** : [B] UM0109 p. 35, « Z85X30 Register Reset Value » (WR3 `XXXXXXX0`, WR4 `XXXXX1XX`, WR5 `0XX0000X`, WR9 matériel `110000XX`). NeoST conforme. |

**Concordant.** PCLK à 8 MHz (`atarist.cpp:1550` ↔ `Scc.cpp:157-160`) ; FIFO de réception de
3 octets sur NMOS/CMOS (`z80scc.cpp:1061-1063`) ; WR14 = `$30` au reset matériel, donc
bouclage local actif, honoré par les deux (`z80scc.cpp:515-518`, `:1217`, `:2063` ↔
`Scc.cpp:81`, `:476-491`), contre Hatari qui ne le lit pas ; RR2B sans IP = statut `011`
(`z80scc.cpp:1513-1515` ↔ `Scc.cpp:340`) ; table de modification du vecteur
(`z80scc.cpp:688-720` ↔ `Scc::updateRR2`, `Scc.cpp:344-355`) ; BRG à TC + 2
(`z80scc.cpp:2246` ↔ `Scc.cpp:141`). NeoST emprunte déjà à MAME l'overrun, le BREAK et le
repli RR6/RR7 (`Scc.cpp:520`, `:667-668`, `:809-810`).

---

## MFP 68901, RTC RP5C15, YM2149 — survol

| # | Écart | Sév. | NeoST | MAME @ ce391499d37d | Arbitrage |
|---|---|---|---|---|---|
| MA-36 | MFP en fin d'interruption logicielle (bit S de VR) : MAME ignore l'in-service. IRQ = IPR & IMR, et le vecteur part au plus prioritaire en attente, même sous un in-service plus prioritaire. | moyenne | `Mfp::checkPendingInterrupts`, `Mfp.cpp:1465-1478` ; `Mfp::updateIrq`, `:1426-1435` | `check_interrupts`, `mc68901.cpp:182-192` ; `get_vector`, `:1020-1045` | **MAME a tort** : [B] MC68901 § 3.4.3 p. 3-6 (« no lower priority channel may request interrupt service nor pass its vector »). |
| MA-37 | Attente MFP : 4 cycles sur ST chez MAME, aucune sur STE ; NeoST : 4 partout. | basse | `Bus::rdMfp`/`wrMfp`, `Bus.cpp:1053,1063` ; `Cpu68k::addMfpWaitCycles`, `Cpu68k.cpp:1267-1269` | `atarist.cpp:821` contre `:934` | **MAME incohérent** entre ses deux cartes. |
| MA-38 | MFP `$FFFA31-$FFFA3F` : 0 chez MAME, `$FF` chez NeoST. | basse | `Bus::rdMfp`, `Bus.cpp:1045` | `mc68901_device::read`, `mc68901.cpp:557` | [F] (valeur du bus ouvert). |
| MA-39 | YM2149, bits inutilisés : MAME relit les registres tels qu'écrits et dit le YM2149 « tested and confirmed on hardware … no anomaly » ; NeoST masque à l'écriture (R1/3/5/13 sur 4 bits, R6/8/9/10 sur 5). | basse | `YM2149::write8`, `YM2149.hpp:90-91` | `ay8910.cpp:1431-1451` | [F], déjà déclaré `YM2149.hpp:60-62`. L'affirmation de MAME ne cite pas sa mesure : piste, pas preuve. Tranché par : écrire `$FF` dans R1 puis relire, sur un vrai ST. |
| MA-40 | YM2149, écriture octet en `$FF8801`/`$FF8803` : ignorée chez MAME (handler sur l'octet pair seul) ; ombre du registre pair chez NeoST. | moyenne | `Bus::wrPsg`, `Bus.cpp:1003-1015` | `atarist.cpp:816-817`, `:905-906` | NeoST soutenu : logiciel réel (musique de X-Out muette sans ce comportement, correctif d'Hatari) et [D] `fx68k.sv:1783-1786 @ 0602ee46` (un cycle octet recopie la donnée sur D8-D15, cité `Bus.cpp:1075-1076`). |
| MA-41 | `$FF8802` en lecture sur Mega ST : `$FF` chez MAME (écriture seule), le registre sur sa carte ST ; NeoST relit le registre partout. | basse | `YM2149::read8`, `YM2149.hpp:64-65` | `atarist.cpp:869` contre `:817` | **MAME incohérent** entre ses cartes. |

**Concordant.** Horloge des timers MFP à 2,4576 MHz, TDO relié à TC et RC
(`atarist.cpp:1373-1376`), GPIP en entrée/sortie selon DDR (`mc68901.cpp:497`) ; USART et
prédiviseurs déjà alignés sur MAME (`Mfp.cpp`, citations l. 188-628). Vecteur MFP au
niveau 6, autovecteurs pour HBL (2) et VBL (4) avec synchro E-clock, et l'IACK efface la
requête (`atarist.cpp:790-796`). Sur ST, `$FF8802` relit le registre (`atarist.cpp:817`),
comme NeoST et contrairement à Hatari. RTC du Mega ST en `$FFFC21-$FFFC3F`, octets impairs
(`atarist.cpp:886` ↔ `Bus.cpp:1259`) ; RP5C15 déjà recoupé avec MAME (`Rtc.cpp:92,100`).

---

## Écarts qu'une source de rang supérieur tranche

**NeoST à corriger (source A, B ou C contre NeoST).** Aucun. Sur ce périmètre, aucune
fiche, netlist ou ROM consultée ne donne raison à MAME contre NeoST.

**MAME a tort (source B, ou incohérence interne démontrable) — NeoST à garder tel quel.**

- MA-5, MA-6, MA-9, MA-16, MA-18 : [B] netlist du GST MCU 4081 (4081P18, `REGISTER.PIN`,
  4081P2 `ROMXB`, 4081P11, `BUTR`/`PADR`/`PENR`) ; MA-9 aussi illustré par IK+.
- MA-32 : [B] WD177X-00 p. 1-7 (moteur coupé après 9 tours).
- MA-35 : [B] Zilog UM0109 p. 35 (valeurs de reset).
- MA-36 : [B] MC68901 § 3.4.3 p. 3-6 (masquage par l'in-service).
- Incohérences de MAME avec lui-même : MA-2 (lecture ≠ écriture), MA-3, MA-11, MA-27,
  MA-37, MA-41.

**Non tranchés ([F]), à garder ouverts.** MA-1, MA-4, MA-8, MA-14, MA-15 (ST), MA-17,
MA-20 à MA-26, MA-30, MA-31, MA-34, MA-38, MA-39. Les plus utiles à trancher : MA-1
(remappage du MMU, utilisé par les tests de RAM) et MA-20 (FIFO du DMA en écriture, que
MAME et MiSTery modélisent autrement qu'Hatari et NeoST).

**MAME en retard ou simplifié, sans enjeu d'arbitrage.** MA-7, MA-10, MA-12, MA-13, MA-28,
MA-29 ; MA-19 et MA-40 penchent pour NeoST sur des sources [D] et du logiciel réel.
