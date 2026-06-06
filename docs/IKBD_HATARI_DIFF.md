# Rapport de débogage — Sous-système IKBD / ACIA 6850 de NeoST (analyse comparative Hatari oracle vs NeoST)

## 1. Résumé exécutif

L'IKBD/ACIA de NeoST est **fonctionnel pour le chemin clavier/souris/joystick nominal**
(boot EmuTOS, bureau, jeux courants) : le parseur de commandes multi-octets est calqué sur
`IKBD_RunKeyboardCommand`, la réponse de reset `$F1` est correctement **différée** de 502000
cycles (port de `IKBD_RESET_CYCLES`), l'horloge interne BCD (`$1B`/`$1C`) reproduit la logique
DAA du HD6301, et la ligne ACIA → GPIP4 → canal 6 du MFP est câblée. La plupart des « bugs »
listés ici **n'affectent PAS** la connexion clavier/souris standard : ils ont été reclassés
après vérification.

**Méthode imposée** (cf. `CLAUDE.md`) : `extern/hatari/src` (`acia.c`, `ikbd.c`) est la source
de vérité matérielle. Avant tout désassemblage, on **porte ce qui manque** depuis Hatari puis on
RETESTE. Ce document inventorie les divergences CONFIRMÉES (code lu des DEUX côtés, ancres
exactes) et propose un plan de portage phasé par sévérité de **déconnexion** clavier/souris.

**Cause racine architecturale n°1.** NeoST a remplacé le couple **registre 1-octet RDR + flag
overrun** du 6850 par une **FIFO logicielle illimitée et instantanée** (`std::deque<uint8_t> rx_`,
`Ikbd.hpp:105`). Le statut `$FFFC00` est **synthétisé** à chaque lecture (`RDRF = !rx_.empty()`,
`IRQ = RDRF && RIE`, `TDRE` câblé à 1 ; `Ikbd.cpp:55-71`). Il en découle TROIS familles de
lacunes : (a) **aucune cause d'IRQ d'émission (TIE)** ni overrun/DCD — seule `RDRF & RIE` arme la
ligne ; (b) **aucun master reset 6850** (`(CR&0x03)==0x03` est un no-op côté clavier, alors que
l'ACIA MIDI le gère) ; (c) **aucune fenêtre critique de reset** ni gating de sortie (pause `$13`,
garde `nVBLs>20`, discard paquet-entier).

**Risque #1 de déconnexion réelle (le plus crédible) : l'absence d'IRQ d'émission (TIE).** Un
jeu qui arme l'IRQ « transmetteur prêt » (`CR=$b6` au lieu de `$96`, ex. **Hades Nebula**,
documenté `ikbd.c:47-48`) pilote l'envoi de ses commandes IKBD depuis le handler d'IRQ ACIA :
comme `CTS=GND` et `TDRE=1` au repos, il attend une IRQ immédiate à l'armement. NeoST ne tire
**jamais** GPIP4 pour une cause TX → le handler n'est jamais appelé → la séquence d'init souris/
joystick ne démarre pas → **clavier/souris muets** pour ce titre. C'est le seul mode de panne où
l'écart Hatari↔NeoST produit une vraie déconnexion fonctionnelle sur un logiciel réel et nommé.

**Faux positifs sur la sévérité (écartés comme vecteurs de déconnexion).** Toute la direction
« FIFO illimitée / pas d'overrun / pas de cadence baud » a été **rétrogradée à low** : NeoST ne
**perd jamais** d'octet et préserve le cadrage des paquets (jamais de demi-paquet), donc il est
**plus robuste** que le matériel, pas moins. Aucun pilote ST connu ne dépend de PERDRE des octets
ni de lire le bit `OVRN` pour se resynchroniser. Les seuls effets réels de ces écarts sont de la
**latence/backlog** sous flood (Downfall/Fokker), une **croissance mémoire non bornée** du deque,
et une **densité d'IRQ** trop élevée — des défauts de fidélité/ressource, pas des déconnexions.

---

## 2. Tableau des divergences CONFIRMÉES (`isReal = true`)

Trié par sévérité de **déconnexion** décroissante (verdicts vérifiés des deux côtés). Sév. =
sévérité de déconnexion révisée (souvent < à la sévérité brute alléguée).

| Domaine | NeoST | Hatari | Mécanisme de déconnexion | Sév. | Ancres (NeoST → Hatari) |
|---|---|---|---|---|---|
| **IRQ émission (TIE)** | `control_` traité comme drapeau RIE 1-bit ; bits 5-6 (Transmitter Control) jamais décodés ; aucune IRQ TX malgré `TDRE` câblé à 1 | `ACIA_Write_CR` pose `TX_EnableInt` pour CR bits6-5=01 ; `ACIA_UpdateIRQ` tire la ligne si `TX_EnableInt && TDRE && !CTS` | Jeu armant `CR=$b6` (Hades Nebula) pilote l'envoi IKBD via IRQ TX ; NeoST ne lève jamais GPIP4 par cause TX → init souris/joy jamais amorcée → **muet** | **medium** | `Ikbd.cpp:55-64,101-106,503-510` → `acia.c:812-823,727-730,838-842` |
| **Bit IRQ du SR** | `0x80` posé uniquement sur `!rx_.empty() && RIE` ; aucune voie TX/OVRN/DCD | `ACIA_UpdateIRQ` agrège cause RX (`RDRF\|DCD\|OVRN`) ET cause TX (`TIE && TDRE && !CTS`) | Handler lisant SR pour distinguer RX/TX voit toujours « cause RX » ; boucle d'émission dirigée par IRQ ne progresse pas (corollaire du TIE) | **medium** | `Ikbd.cpp:55-64` → `acia.c:715-748` |
| **Réévaluation IRQ à l'écriture CR** | `write8 $FFFC00` n'appelle `raiseIfReady()` que pour la cause RX | `ACIA_Write_CR` finit par `ACIA_UpdateIRQ` (RX **et** TX) ; armer TIE lève la ligne immédiatement | Pilote armant TIE quand `TDRE=1` attend l'IRQ initiale « transmetteur prêt » pour amorcer ; elle n'arrive jamais → init bloquée | **medium** | `Ikbd.cpp:101-107,503-510` → `acia.c:838-842,727-748` |
| **Master reset 6850** | `(CR&0x03)==0x03` ignoré ; SR/RDRF/ligne non réinit ; l'ACIA MIDI, elle, gère le reset (`MidiAcia.cpp:38`) | `ACIA_Write_CR` détecte `COUNTER_DIVIDE==0x03` → `ACIA_MasterReset` : SR=TDRE, RDRF/OVRN effacés, `Set_Line_IRQ(1)` | Loader (Transbeauce 2) reset puis lit SR en attendant `0x02` ; un octet résiduel laisse `RDRF=1` (`0x03`) → parseur déphasé | **medium** | `Ikbd.cpp:101-107` → `acia.c:794-797,669-705` |
| **Fenêtre critique de reset** | Aucun `bDuringResetCriticalTime` ; pushRx non gardé pendant les ~502000 cyc avant `$F1` | `IKBD_Boot_ROM` pose le flag ; `IKBD_Send_Byte_Delay` teste le flag EN PREMIER et `return` (rien émis avant `$F1`) | Reset à chaud (Lotus/Dragonnels/Barbarian) avec touche/manette tenue : NeoST émet un octet parasite AVANT `$F1` attendu → « keyboard not responding » | **medium** | `Ikbd.cpp:139-167,322-325,310-320` → `ikbd.c:594,635-649,1019-1031` |
| **Cmd `$19` (longueur 7)** | absente de `cmdLength` → traitée mono-octet → 6 octets de paramètre reparsés comme opcodes | `{0x19,7,...}` : accumule 7 octets puis exécute (handler = log) ; parseur synchronisé | Octet de paramètre = `$12`/`$14`/`$80`… → change mode souris/joy ou reset parasite → perte de rapports | **medium** | `Ikbd.cpp:73-99,112-137,284-288` → `ikbd.c:243,1853-1886,2351-2354` |
| **Cmd `$20` (LoadMemory)** | absente de `cmdLength` → mono-octet ; pas d'état de chargement → en-tête + NUM octets de code 6301 reparsés comme opcodes | `{0x20,4,...}` arme `MemoryLoadNbBytesLeft` ; `IKBD_Process_RDR` avale les NUM octets sans les interpréter | Code 6301 contient `$12` (souris OFF) / `$80,$01` (reset) → souris « morte » / reset à chaud parasite | **medium** | `Ikbd.cpp:73-99,112-137,284-288` → `ikbd.c:247,2460-2468,890-907,2828-2838` |
| **Cmd `$08` (RelMouseMode)** | longueur OK par accident mais **aucun case** → `mouseMode_` non forcé à REL | `{0x08,1,...}` pose `MouseMode=MOUSEREL` + quirk Barbarian (`bMouseEnabledDuringReset`) | Jeu en ABS/CURSOR/OFF envoie `$08` pour revenir en relatif → souris reste muette / émet des flèches | **medium** | `Ikbd.cpp:73-99,284-288` → `ikbd.c:226,1951-1963` |
| **Cmd `$1A` (DisableJoysticks)** | longueur OK (`=1`) mais **aucun case** → `joyMode_` reste JOY_AUTO, report `$FE/$FF` continue | `{0x1A,1,...}` pose `JoystickMode=OFF` + `IKBD_CheckResetDisableBug` | Programme coupe les joy via `$1A`, NeoST continue d'émettre des paquets non sollicités (mais bien encadrés) | **low** | `Ikbd.cpp:93,284-288,310-320` → `ikbd.c:244,2363-2371` |
| **Cmd `$21` (ReadMemory)** | absente de `cmdLength` → mono-octet → 2 octets d'adresse reparsés ; aucune réponse `$F6` | `{0x21,3,...}` renvoie `$F6 $20`+6 octets ; parseur synchronisé | Diagnostic envoie `$21` et attend `$F6` → boucle ; en plus désync si octet d'adresse = opcode | **low** | `Ikbd.cpp:73-99,284-288` → `ikbd.c:248,2487-2503` |
| **Cmd report `$87`–`$9A`** | aucun opcode dans `cmdLength` → mono-octet → aucune réponse poussée | entrées longueur 1 ; chaque handler renvoie un paquet `$F6`+7 octets | Programme atypique attend `$F6` en boucle → time-out (jamais émis par TOS/EmuTOS) | **low** | `Ikbd.cpp:73-99,284-288` → `ikbd.c:252-264,2540-2574` |
| **Cmd `$13`/`$11` (Pause/Resume)** | absentes → no-op ; aucun `PauseOutput` ; NeoST émet toujours | `$13`→`PauseOutput=true` (sauf reset critique) gèle le drain ; `$11`/toute cmd valide→false | Programme `$13` hors reset attend le silence ; NeoST sur-émet (paquets valides surnuméraires) | **low** | `Ikbd.cpp:73-99,284-289,498-501` → `ikbd.c:2156-2160,2186-2197,917-928,1871-1875` |
| **Garde `nVBLs>20`** | `onVbl` appelle `sendAutoJoysticks` dès la 1re trame, sans garde de boot | `IKBD_InterruptHandler_AutoSend` n'émet que si `nVBLs>20` (« avoid TOS confused during boot ») | Manette tenue au boot → paquet `$FE` pendant l'init ACIA de TOS → cadrage décalé (conditionnel) | **low** | `Ikbd.cpp:310-320,292-308` → `ikbd.c:1805-1811` |
| **Quirk reset disable** | pas de fenêtre critique, `$1A` no-op → `$12`+`$1A` ne réactive pas souris+joy | `IKBD_CheckResetDisableBug` réactive MOUSEREL+JOY si les deux désactivés pendant reset | Jeux (Barbarian/Psygnosis) voulant souris+joy simultanés ne reçoivent pas les deux flux | **low** | `Ikbd.cpp:73-99,139-290` → `ikbd.c:1821-1838,2363-2371` |
| **`$17` JoystickMonitoring** | retient `JOY_MONITOR` mais `onVbl` ne traite que JOY_AUTO → **rien émis** ; commentaire `Ikbd.cpp:262-263` mensonger | reprogramme `AutoSendCycles`, émet 2 octets/tick (`IKBD_SendAutoJoysticksMonitoring`) + force `MouseMode=OFF` | Jeu attendant le flux monitoring le voit muet (mais `$17` quasi inutilisé) | **low** | `Ikbd.cpp:260-265,310-320` → `ikbd.c:2292-2311,1483-1497,1693-1697` |
| **Pas d'overrun ACIA / OVRN** | `rx_` non bornée ; aucun bit `OVRN`/`FE`/`PE` ; jamais de perte ni de signalement | `ACIA_Clock_RX` pose `RX_Overrun` (octet perdu) ; `OVRN` exposé à la lecture RDR, compte comme IRQ | Direction inverse : NeoST ne perd jamais d'octet → plus robuste, pas de déconnexion | **low** | `Ikbd.cpp:55-71` → `acia.c:1103-1137,869-873` |
| **Pas de cadence série (baud)** | octets disponibles INSTANTANÉMENT ; aucun timer inter-octet (seul `$F1` différé) | timer ~1024 cyc/bit, ~10240 cyc/octet ; RDR chargé bit par bit | Livraison plus rapide mais 1 octet/IRQ : pas de perte ni de désync → pas de déconnexion | **low** | `Ikbd.cpp:498-510` → `acia.c:472-515` |
| **Re-raise IRQ par octet** | `raiseIfReady()` après CHAQUE pushRx ; rafale entière → IRQ re-posées au même cycle | `ACIA_UpdateIRQ` ne (re)génère l'IRQ qu'à la fin d'un octet (cadence baud) ; GPIP4 piloté sur front | Densité d'IRQ trop élevée (fidélité/perf) ; aucun octet perdu → pas de déconnexion | **low** | `Ikbd.cpp:498-510` → `acia.c:715-748,1111-1117,497-515` |
| **Output buffer non borné** | `rx_` sans limite ; aucun `CheckFreeCount`, aucun discard paquet-entier | ring buffer 1024 octets ; rejet du **paquet entier** si plein (jamais de demi-paquet) | Flood (Downfall/Fokker) → backlog/latence + croissance mémoire ; cadrage préservé → pas de déconnexion | **low** | `Ikbd.cpp:498-501;Ikbd.hpp:105` → `ikbd.c:944-958,1037-1048;ikbd.h:44` |
| **Souris émise par event hôte** | `mouseEvent` pousse `$F8` synchroniquement par event hôte (pas d'accumulateur drainé au VBL) | callback accumule `dx/dy` ; paquet `$F8` émis 1×/AutoSend (~VBL) | Dérive sémantique (sur-mouvement, double-clics) ; aucun octet perdu → pas de déconnexion | **low** | `Ikbd.cpp:327-392;main.cpp:758-768` → `main.c:566-567;ikbd.c:1196-1224,1782-1811` |
| **Reset ne vide pas `rx_`** | `$80,$01` ne touche pas `rx_` ; octets pré-reset survivent et précèdent `$F1` | `IKBD_Boot_ROM` vide l'output buffer (`BufferHead=Tail=0`) au reset | Pilote supposant `$F1` en 1er octet sans boucler lit un octet périmé (rare ; rx_ vide au boot) | **low** | `Ikbd.cpp:149-167,498-501` → `ikbd.c:531-626,578-581` |

> Note d'agrégation : plusieurs entrées du tableau couvrent le **même mécanisme physique** vu sous
> des angles différents (le verdict d'analyse a confirmé les doublons). Les groupes : **(G1)
> master reset 6850** = `kbd-acia-no-master-reset-detect`, `kbd-acia-no-master-reset`,
> `no-master-reset-clears-rdrf-ovrn`, `no-rdrf-clear-on-reset` ; **(G2) IRQ TX/causes du bit IRQ**
> = `no-tx-interrupt-tie`, `irq-bit-only-rdrf-not-causes`, `rie-change-no-tx-reeval` ; **(G3)
> fenêtre critique de reset** = `reset-no-critical-window-byte-discard`,
> `no-during-reset-critical-time` ; **(G4) overrun/cadence/output-buffer/flood** =
> `no-overrun-no-cadence-irq`, `no-acia-overrun`, `no-serial-cadence-timer`,
> `no-ikbd-output-buffer-discard`, `rx-buffer-unbounded-no-checkfreecount`,
> `no-baud-rate-drain-instant-consume`, `no-serial-baud-gating-overrun`,
> `irq-rearm-per-byte-flood-iack`, `no-output-buffer-bound`, `mouse-emit-per-host-event-not-vbl` ;
> **(G5) flush au reset** = `reset-no-flush-rx-and-input-buffer`, `reset-no-buffer-flush`. Le plan
> de portage ci-dessous est organisé par mécanisme, pas par entrée.

---

## 3. Plan de portage PHASÉ (par sévérité de déconnexion)

### Phase A — Fort impact déconnexion : statut ACIA / IRQ TX / causes du bit IRQ

C'est la **seule phase qui adresse une déconnexion réelle et documentée** (Hades Nebula). Tout
tourne autour d'un point : NeoST réduit le bit IRQ du 6850 à `RDRF & RIE`, sans la **moitié
émission** (TIE). À porter ensemble, dans le même fichier.

**`src/io/Ikbd.cpp` + `src/io/Ikbd.hpp` — décoder le Transmitter Control et ajouter la cause TX**
- Décoder `TX_EnableInt` à l'écriture de `$FFFC00` : `(control_>>5)&3 == 0x01` → IRQ d'émission
  armée (gérer aussi RTS pour `0x02`, break pour `0x03` si souhaité). Ajouter un membre
  `bool txEnableInt_`. Réf. `acia.c:812-823` (`ACIA_Write_CR`).
- Étendre `raiseIfReady()` (`Ikbd.cpp:503-510`) pour activer la ligne sur l'**UNION** des causes,
  comme `ACIA_UpdateIRQ` (`acia.c:715-748,727-730`) :
  `active = (RIE && !rx_.empty()) || (txEnableInt_ && TDRE && !CTS)`. Comme `TDRE` est câblé à 1 et
  `CTS=0` (GND) sur ST, la branche TX se réduit à `if (txEnableInt_) active = true;` → le handler
  d'émission du jeu se ré-arme tout seul.
- Inclure `ACIA_IRQ (0x80)` dans le statut lu en `$FFFC00` (`read8`, `Ikbd.cpp:55-64`) quand la
  cause TX est active (pas seulement RX), pour qu'un handler distinguant RX/TX voie la bonne
  source. Réf. `acia.c:715-748`.
- Ré-évaluer la ligne après chaque écriture du registre de contrôle (déjà fait) **et** après
  chaque écriture de donnée vers l'IKBD (`$FFFC02`), comme Hatari rappelle `UpdateIRQ` après
  `Write_CR` (`acia.c:841`) et `Write_TDR` (`acia.c:901`).

**Validation Phase A.** Tester avec une cartouche/PRG qui arme `CR=$b6` puis pilote l'IKBD via
IRQ ACIA. En headless : `--cart`/`--keys` + `--irq` pour tracer GPIP4/canal 6 ; comparer aux DEUX
cœurs (`--cpu musashi|moira`). L'IRQ « transmetteur prêt » doit apparaître dès l'armement.

### Phase B — Robustesse : master reset, fenêtre critique de reset, flush au reset

Ces correctifs préviennent les déconnexions **conditionnelles** (loaders/démos qui reset l'ACIA/
l'IKBD en cours de route, reset à chaud avec entrée tenue). Aucun n'affecte le boot EmuTOS normal.

**`src/io/Ikbd.cpp` (`write8 $FFFC00`, `Ikbd.cpp:101-107`) — master reset 6850 (G1)**
- Détecter `(v & 0x03) == 0x03` et reproduire `ACIA_MasterReset` (`acia.c:669-705`) **sans purger
  `rx_`**. ⚠ Hatari ne vide PAS les octets en transit au master reset (`ikbd.c:37-40`,
  « don't clear bytes in transit » — fix Froggies/Overdrive) ; le `rx_.clear()` de `MidiAcia.cpp:38`
  est en fait **trop agressif** au regard de Hatari.
- Effacer la **visibilité `RDRF`** du seul octet « dans le RDR » : poser un flag transitoire
  `rdrCleared_` au master reset, consommé à la prochaine lecture de SR/donnée, pris en compte dans
  `read8` pour masquer `RDRF`/`IRQ` une fois → la lecture immédiate de `$FFFC00` rend `0x02`
  (contrat Transbeauce 2, `ikbd.c:22-24`), l'octet restant refaisant remonter `RDRF` ensuite.
- Forcer la désassertion de ligne (`mfp_.setAciaLineKbd(false)`) au master reset pour reproduire
  `Set_Line_IRQ(1)` indépendamment de RIE. Aligner `MidiAcia.cpp` sur la même logique (retirer le
  `rx_.clear()`).

**`src/io/Ikbd.cpp` + `src/io/Ikbd.hpp` — fenêtre critique de reset (G3)**
- Ajouter `bool duringResetCriticalTime_`, mis à `true` dans `dispatchCommand` case `0x80`
  (branche `inBuf_[1]==0x01`) au moment de planifier `$F1` (`Ikbd.cpp:164`). Réf. `ikbd.c:594`.
- Vider la file de sortie au reset : `rx_.clear()` (équivalent `BufferHead=BufferTail=0` de
  `IKBD_Boot_ROM`, `ikbd.c:578-579`) — fusionne aussi le groupe **G5** (`$80,$01` ne vide pas
  `rx_`). Par cohérence : `inBufLen_=0; cmdExpected_=0;`.
- Gater `pushRx()` (donc `keyEvent`/`mouseEvent`/`sendAutoJoysticks`/`sendCursorKeys`) :
  `return` immédiat tant que `duringResetCriticalTime_` est vrai, comme `IKBD_Send_Byte_Delay`
  (`ikbd.c:1019-1024`). Attention : seul le `$F1` doit passer.
- Effacer `duringResetCriticalTime_` dans le callback `onResetResponse()` (`Ikbd.hpp:37`) JUSTE
  AVANT `pushRx(0xF1)`, comme `IKBD_InterruptHandler_ResetTimer` (`ikbd.c:644-648`).

**`src/io/Ikbd.cpp` (`onVbl`, `Ikbd.cpp:310-320`) — garde de boot `nVBLs>20`**
- Incrémenter un `uint32_t vblCount_` et conditionner le report auto :
  `if (joyMode_==JOY_AUTO && vblCount_>20 && !duringResetCriticalTime_) sendAutoJoysticks();`.
  Réf. `ikbd.c:1805-1811`.

### Phase C — Fidélité fine : couverture des commandes + cadence/output buffer

Aucun de ces items n'est un vecteur de déconnexion crédible (verdicts → `low`), mais ils comblent
des lacunes de fidélité réelles et suivent la méthode imposée (porter ce qui manque de Hatari).

**`src/io/Ikbd.cpp` (`cmdLength` + `dispatchCommand`) — framing & couverture des commandes**
- `cmdLength` : ajouter les longueurs manquantes pour synchroniser le parseur :
  `case 0x08: return 1;` (lisibilité), `case 0x19: return 7;`, `case 0x20: return 4;`,
  `case 0x21: return 3;`, `case 0x22: return 3;`, `case 0x11/0x13: return 1;`,
  `case 0x87..0x9A: return 1;`. Réf. table `KeyboardCommands[]` (`ikbd.c:226,243,247,248,252-264`).
- `dispatchCommand` : `case 0x08` → `mouseMode_=REL` (`ikbd.c:1951-1963`) ; `case 0x1A` →
  `joyMode_=JOY_OFF` (calqué sur le `case 0x15` existant ; `ikbd.c:2363-2371`) ; `case 0x21` →
  émettre `$F6 $20` + 6×`$00` (`ikbd.c:2487-2503`) ; `case 0x87..0x9A` → paquets de status `$F6`+7
  octets selon le handler Hatari correspondant (`ikbd.c:2540-2574`).
- `case 0x20` (LoadMemory) : lire `NUM=inBuf_[3]`, armer `memoryLoadBytesLeft_` ; en tête de
  `write8 $FFFC02`, si `memoryLoadBytesLeft_>0`, **avaler** l'octet (décrémenter) au lieu de le
  parser. Réf. `ikbd.c:902-906,2828-2838`. (Le pilotage réel des menus Froggies/Chaos AD/Audio
  Sculpture exigerait en plus les `CustomCodeDefinitions[]` — hors scope.)
- `case 0x17` (JoystickMonitoring) : forcer `mouseMode_=OFF` (`ikbd.c:2300`), émettre les 2 octets
  monitoring au taux échantillonné via `onVbl` (`ikbd.c:1483-1497`), supprimer le report clavier/
  souris en JOY_MONITOR, **et corriger le commentaire mensonger** `Ikbd.cpp:262-263`.

**`src/io/Ikbd.cpp` + `src/io/Ikbd.hpp` — output buffer borné + discard paquet-entier (G4)**
- Borner `rx_` à `SIZE_KEYBOARD_BUFFER=1024` (`ikbd.h:44`). Ajouter une fonction style
  `IKBD_OutputBuffer_CheckFreeCount(Nb)` (`ikbd.c:944-958`) appelée AVANT chaque paquet complet
  (`$FD/$FE/$FF/$F7/$F8/$FC` = 3/2/2/6/3/7 octets…) ; rejeter le **paquet entier** si la place
  manque (jamais de demi-paquet, cadrage préservé). Au niveau octet (`pushRx`), re-tester pour 1
  et jeter si plein (équivalent du `LOG_ERROR` de `IKBD_Send_Byte_Delay`, `ikbd.c:1037-1048`).
- **Pause `$13`/`$11`** : ajouter `bool pauseOutput_` ; `$13` → true SAUF en fenêtre critique
  (réutiliser `duringResetCriticalTime_` de la Phase B — préserve le hack « Just Bugging »),
  `$11` → false, toute commande valide → false (`ikbd.c:1871-1875`). Gater le drain
  (`read8 $FFFC02` + `raiseIfReady`) sur `!pauseOutput_` ou, plus simple, tamponner dans une file
  interne déversée vers `rx_` seulement quand `!pauseOutput_`. Réf. `ikbd.c:2156-2197,917-928`.

**`src/io/Ikbd.cpp` — overrun ACIA, cadence baud, souris agrégée au VBL (fidélité, non urgent)**
- **OVRN/FE/PE** : ajouter `ACIA_OVRN=0x20`/`FE=0x10`/`PE=0x40` à l'enum de statut + un flag
  `rxOverrun_` ; modèle 1-RDR : si l'octet précédent n'a pas été lu, marquer overrun et jeter le
  nouvel octet sans écraser RDR (`acia.c:1103-1137`) ; exposer OVRN à la lecture de RDR
  (`acia.c:869-873`). Inclure `RX_Overrun` comme cause d'IRQ RX.
- **Souris agrégée au VBL** : en mode REL, ne PAS pushRx dans `mouseEvent` — accumuler
  `dx_+=dx; dy_+=dy` (équivalent `KeyboardProcessor.Mouse.dx/dy`, `main.c:566-567`) et déplacer la
  paquetisation `$F8` dans `onVbl` (cadence AutoSend ~1 VBL, `ikbd.c:1196-1224,1782-1811`).
- **Cadence série** (lourd, optionnel) : cadencer la livraison `rx_`→CPU au baud (~10240 cyc/octet)
  via le Scheduler plutôt que des octets instantanés, en s'inspirant de
  `ACIA_InterruptHandler_IKBD`/`IKBD_Check_New_TDR` (`acia.c:497-515`, `ikbd.c:917-929`). Utile
  uniquement pour des loaders mesurant le timing inter-octets ; **non requis** pour la déconnexion.

---

## 4. Non-divergences / sévérité écartée (à ne PAS traiter comme déconnexion)

Après vérification du code des deux côtés, les écarts suivants sont **réels en fidélité** mais
**ne provoquent PAS** de déconnexion clavier/souris — direction du risque inverse ou cas
théorique sans logiciel ST connu. Ne pas les prioriser haut.

- **Pas d'overrun / FIFO illimitée** : NeoST ne perd jamais d'octet et n'injecte jamais de désync
  de trame → **plus robuste** que le matériel. Aucun pilote ST ne dépend de PERDRE des octets ni
  de lire le bit `OVRN` pour se resynchroniser (resync sur en-têtes `$F6..$FF`). Seul effet :
  latence/backlog sous flood, croissance mémoire non bornée.
- **Pas de cadence série** : la livraison est **instantanée mais 1 octet/IRQ** (`read8` re-arme
  l'IRQ tant que `rx_` non vide) ; le CPU draine aussi vite qu'il s'emplit. Pas d'IRQ perdue, pas
  d'octet perdu. Le drain-loop `_int_acia` d'EmuTOS VEUT consommer vite. Aucune déconnexion.
- **Output buffer non borné** : le rejet Hatari modélise une limite **matérielle** (1024 octets) et
  préserve l'intégrité paquet ; NeoST préserve aussi le cadrage (jamais de demi-paquet) en gardant
  tout. Conséquence = backlog/mémoire, pas une corruption de trame.
- **`$13`/`$11` Pause** : commandes obscures, quasi inutilisées ; le seul logiciel réel (« Just
  Bugging ») les émet PENDANT le reset — cas que Hatari IGNORE et que NeoST (no-op) gère par hasard
  correctement.
- **Master reset — purge de `rx_`** : ⚠ ne PAS copier le `rx_.clear()` de `MidiAcia.cpp`. Hatari
  garde les octets en transit (`ikbd.c:37-40`, fix Froggies/Overdrive) ; seul `RDRF`/la ligne
  doivent être réinit.

---

## 5. Limites de validation et recommandation oracle

**Obstacle principal (cf. mémoire projet `hatari-headless-no-floppy-boot`).** Hatari headless ne
boote pas les disquettes de test overscan/boot-sector (retombe sur le bureau) ; les titres qui
exercent les chemins divergents (Hades Nebula via IRQ TX, loaders Transbeauce 2/Froggies,
Lotus/Dragonnels au reset à chaud) **ne peuvent pas être lancés à l'identique des deux côtés**
pour une comparaison end-to-end. De plus, en headless déterministe, `--keys` laisse tourner le
boot (idle) AVANT d'injecter → la fenêtre critique de reset (Phase B) **ne se déclenche pas** en
oracle ; ces bugs sont surtout atteignables en **usage interactif réel** (reset à chaud avec
touche/manette tenue).

**Recommandation : tracer la ligne ACIA et le SR des DEUX côtés.**

- **Côté NeoST headless** : utiliser `--irq` (indispensable pour les bugs d'IRQ) pour tracer
  GPIP4/canal 6 du MFP à chaque écriture/lecture `$FFFC00`/`$FFFC02`. Pour la Phase A, le test
  décisif est : armer `CR=$b6` et vérifier que la ligne ACIA passe basse IMMÉDIATEMENT (cause TX)
  — observable dans la trace `--irq` sans dépendre du boot disquette.
- **Pivot pratique** : valider via une **cartouche de diagnostic** (`--cart` + `--keys`, rapport
  sur port série, `--loopback` branché APRÈS `--keys`) qui programme l'ACIA clavier et lit le SR —
  c'est le moyen le plus fiable de valider Phase A (TIE) et Phase B (master reset → SR=`0x02`)
  indépendamment d'un titre commercial. Tester les DEUX cœurs (`--cpu musashi|moira`).
- **Faux positif à connaître** : « VME/FPU MegaSTE not found » est CORRECT (cf. `CLAUDE.md`).
