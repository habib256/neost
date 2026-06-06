# Rapport de débogage — Sous-système SON de NeoST (analyse comparative Hatari oracle vs NeoST)

## 1. Résumé exécutif

Le son de NeoST est **fonctionnel et globalement crédible** pour le PSG mono-voix : la table de volume 5 bits (`kVolume`, `YM2149.cpp:11-20`) est portée à l'identique de `ymout1c5bit` de Hatari, le mapping 4→5 bits est correct, le LFSR de bruit utilise le **même polynôme primitif** que le YM2149 (vérifié bit-pour-bit), le réarmement d'enveloppe sur R13 est conforme, et le câblage XSINT → GPIP7 (XOR moniteur) + TAI Timer A reproduit fidèlement Hatari.

En revanche, **trois familles de divergences pèsent réellement sur le rendu audio** :

1. **Modèle de synthèse « pull » dans le thread audio, sans horloge d'émulation ni anneau** (`Audio.cpp:33-44`, `YM2149.cpp:57-126`). Toutes les écritures registres sont appliquées en bloc au prochain callback, sans horodatage cycle-CPU. C'est la **cause racine n°1** : digidrums, sync-buzzer/syncsquare et arpèges très rapides sont aplatis ou muets (corrobore le symptôme « musique muette sur la majorité des titres » du TODO).

2. **Mixage des voies linéaire au lieu de la table DAC non linéaire 32×32×32**, et **combinaison ton+bruit par moyenne arithmétique au lieu d'un ET logique** (`YM2149.cpp:107-125`). Le DAC réel du ST (3 sorties sur résistance 1k commune) compresse la somme ; NeoST sur-amplifie les passages polyphoniques et rend mal les bruitages ton+bruit (explosions, moteurs, percussions).

3. **Absence de tout filtrage de sortie YM** (passe-bas C10/PWM + passe-haut sous-sonique anti-DC), `YM2149.cpp:125`. Son plus dur, aliasing sur les notes aiguës, offset DC résiduel.

Côté **son DMA STE / LMC1992**, deux vrais défauts comportementaux : le **registre de mixage LMC1992 (reg 0) est décodé mais jamais appliqué** (le YM passe toujours, même en mode « DMA only ») et l'**équilibre d'amplitude YM/DMA sur STE est faux** (pas de demi-amplitude YM → écrêtage dur). Les défauts de timing (compteur de trame non live, FIFO 8 octets absente, start==end) sont réels mais de plus faible impact.

Côté **intégration**, un défaut transverse de fiabilité : le **MFP n'est jamais réinitialisé au reset** (pas de `Mfp::reset()`), ce qui peut laisser des IRQ Timer A / GPIP7 fantômes survivre à un reset chaud et faire dériver une musique chip après reset.

**Faux positifs écartés** (déjà vérifiés conformes, à ne PAS réinvestiguer) : polynôme LFSR (dualité Fibonacci/Galois du même polynôme), volume LMC appliqué à tout le mix (conforme HW), bruits de lecteur hors chaîne LMC (fonctionnalité d'immersion non-matérielle), câblage XSINT/GPIP7/TAI, réarmement enveloppe R13.

---

## 2. Tableau des bugs CONFIRMÉS (`isRealDivergence = true`)

Trié par impact audible décroissant.

| Sous-système | Symptôme audible | Sév. | Cause racine | Correctif à porter (réf. Hatari) | Déjà connu ? |
|---|---|---|---|---|---|
| **Backend / horloge** (`callback-thread-synthesis-no-ringbuffer`) | Digidrums/sync-buzzer/arpèges rapides en escalier, désynchro ou muets ; underruns/craquements sous charge | **high** | Synthèse « pull » entièrement dans le thread audio (`Audio.cpp:33-44`) lisant `regs_` en direct ; aucune génération sur l'horloge CPU ni anneau ; écritures appliquées en bloc au callback | Modèle « push » : `Sound_Update(Cycles_GetClockCounterOnWriteAccess())` AVANT chaque write (`psg.c:339`), `YM2149_Run`/`YM2149_DoSamples_250` 250 kHz (`sound.c:1241-1256, 1022-1133`), anneau `AudioMixBuffer[]` recopié par le callback (`audio.c:75-104`) | **Oui** (TODO.md:196-200) |
| **PSG synthèse** (`linear-vs-nonlinear-mix`) | Chiptunes 3 voies sur-amplifiées/saturées, balance/timbre du DAC ST faussés | **medium** | Somme linéaire `sample += v*vol; out=sample/3` (`YM2149.cpp:107-125`) = équivalent du mode non-défaut `YM_LINEAR_MIXING` | Table 3D mesurée/modélisée `ymout5[32][32][32]` indexée `YM_MERGE_VOICE(C,B,A)` ; `YM2149_BuildModelVolumeTable()` (`sound.c:615-678`, autonome) ou `interpolate_volumetable()` ; défaut Hatari = `YM_TABLE_MIXING` (`sound.c:286`) | Non |
| **PSG synthèse / mixage** (`tone-noise-and-vs-average` = `tone-noise-mix-additive-vs-AND`) | Bruitages ton+bruit (explosions, moteurs, percussions) adoucis au lieu d'être hachés par la porteuse ; niveau du bruit faussé | **medium** | `v=square; v+=noise; if both v*=0.5` (`YM2149.cpp:119-123`) = moyenne bipolaire, pas un gating | ET logique par voie `bt=(Tone\|mixerT)&(Noise\|mixerN)` → 0 ou 0x1f, volume appliqué seulement si voie « haute » (`sound.c:1098-1111`) | Non |
| **DMA STE** (`ste-ym-no-half-amplitude`) | DMA trop fort/faible vs YM sur STE ; écrêtage dur quand YM+DMA jouent fort ensemble (distorsion) | **medium** | YM pleine échelle sur toutes machines (`YM2149.cpp:125`), DMA additionné à gain fixe 0.7 (`DmaSound.cpp:19,252`), clamp dur [-1,1] (`Audio.cpp:42-43`) ; aucune conscience ST/STE | Sur STE/TT : YM demi-amplitude `YM_OUTPUT_LEVEL>>1` (`sound.c:780-784`), DMA à 3/4 (`dmaSnd.c:558-566`), gains LMC doublés (`dmaSnd.c:1153-1166`) | Non |
| **DMA STE / LMC** (`mixing-reg0-never-applied`) | En mode « DMA only » (reg0=0/2/3), la musique YM reste audible alors qu'elle devrait être coupée | medium→low | `mwMixing_` décodé/stocké (`DmaSound.cpp:87`) mais **jamais lu** ; mix toujours additif (`DmaSound.cpp:252`) | `switch(microwire.mixing)` : `==1` DMA+YM, défaut `0/2/3` DMA seul écrase le YM (`dmaSnd.c:555-568`) | **Oui** (TODO.md:194) |
| **PSG synthèse** (`no-output-filters` = `missing-ym-lowpass-and-dc-hpf`) | Son YM plus dur/agressif, aliasing sur notes aiguës, plops/offset DC | medium→low | Sortie brute `out=sample/3`, aucun filtre (`YM2149.cpp:125`) ; rien en aval (`Audio.cpp:33-44`) | PWMaliasFilter (défaut) ou LowPassFilter STF (`sound.c:451-492`) par échantillon + Subsonic_IIR_HPF anti-DC (`sound.c:382-394`), appliqués par défaut (`sound.c:288-290`) | **Partiel** (TODO.md:185-186 ne cite que le LPF, pas le HPF) |
| **PSG timing** (`host-rate-vs-250k-resample` / `envelope-restart-phase`) | Sync-buzzer/syncsquare déformés, léger aliasing notes aiguës, transitions de période moins nettes, réarmements d'enveloppe perdus | medium | Accumulateurs de phase flottants à la fréquence hôte (`YM2149.cpp:94,103,109`) ; réarmement enveloppe quantifié au bloc audio (`YM2149.cpp:82-88`), drapeau booléen unique | Compteurs entiers 250 kHz + rééchantillonnage `Resample_Weighted_Average_N` (`sound.c:1041-1122, 1347-1383`) ; `Env_pos=0;Env_count=0` au cycle exact (`sound.c:1507-1513`) | **Oui** (TODO.md:196-200) |
| **Intégration / reset** (`mfp-no-reset`) | Après reset chaud : musique chip/Timer A qui continue ou démarre sur IRQ fantôme (notes parasites, tempo emballé) ou silence si canal resté masqué | medium | Aucun `Mfp::reset()` ; `Machine::reset/hardReset` (`Machine.hpp:60,64`) ne reset que PSG/DMA/CPU ; état MFP (IERA/IPRA/ISRA/IMRA/AER/compteurs) survit | `MFP_Reset` (mfp.c:519-569) appelé à chaque reset cold ET warm via `MFP_Reset_All` (`reset.c:74`) | Non |
| **DMA STE** (`dma-no-fifo-no-antialias-direct-ram`) | Aliasing sur samples 50 kHz→48 kHz ; grésillement sur demos qui réécrivent le buffer (Mental Hangover) ; clics (course RAM CPU/audio) | medium | Lecture RAM directe `sampleAt(curAddr_)` côté thread audio (`DmaSound.cpp:169-175,245-268`), pas de FIFO, pas d'anti-repliement, zero-order hold | FIFO 8 octets remplie sur HBL (`DmaSnd_FIFO_Refill/PullByte`, `dmaSnd.c:343-410`) + FIR 3-taps anti-repliement (`dmaSnd.c:1316-1349`) + décodage sur horloge émulation | **Partiel** (TODO.md:196-200 : thread/horloge, pas FIFO/anti-repliement) |
| **DMA STE / interruptions** (`framestart-equals-frameend` / `dmasnd-start-eq-end`) | Demos start==end (Amberstar cracktro, A Little Bit Insane) : son coupé/absent, IRQ fin-de-trame manquante ; GPIP7 figé HAUT (détection moniteur faussée) | medium→low | start==end non traité au démarrage : `scheduleFrameEnd` annule juste DMASND (`DmaSound.cpp:26-34`) sans baisser XSINT ni effacer play ; `setXsint(true)` déjà monté | `DmaSnd_StartNewFrame` : si start==end & repeat off → efface PLAY et `return` AVANT de monter XSINT (`dmaSnd.c:471-480`) ; repeat on → compteur avance, ~2^24 octets, pas d'IRQ | **Oui** (TODO.md:190-191) |
| **DMA STE** (`frame-counter-audio-thread-only` / `dmasnd-framecount-idle`) | Effets synchronisés sur la position de lecture ($FF8909/0B/0D) : clics, sauts ; à l'arrêt renvoie endAddr au lieu de startAddr ; figé en headless | low | `read8` renvoie toujours `curAddr_` (`DmaSound.cpp:183-185`) ; `curAddr_` avancé seulement dans `mix()` (thread audio) | `DmaSnd_GetFrameCount` : `Sound_Update(cycle)` puis startAddr si !PLAY (`dmaSnd.c:749-762`) ; `frameCounterAddr += 2` sur HBL (`dmaSnd.c:362`) | **Oui** (TODO.md:196-198) |
| **DMA STE / LMC** (`tone-control-freqs-and-design`) | Réglages basses/aigus : timbre légèrement décalé ; codes 13-15 donnent +14/+16/+18 dB au lieu de saturer à +12 dB | low | Biquads RBJ 200 Hz/8 kHz (`DmaSound.cpp:114-146`) ; `(data-6)*2` dB sans table de saturation (`DmaSound.cpp:142-143`) | Shelfs 1er ordre Savinkoff fusionnés, fc 118.276/8438.756 Hz (`dmaSnd.c:1369-1420`), table `LMC1992_Bass_Treble_Table[16]={0..12,12,12,12}` (`dmaSnd.c:211-214`) | Non (RBJ noté CHANGELOG.md:325, pas l'écart) |
| **PSG I/O** (`select-masked-4bits` / `psg-select-mask-8bit`) | Très marginal : RMW sur registres masqués relus différemment ; pas de « désactivation PSG » via select≥16 (European Demo) | low | `selected_ = v & 0x0F` (`YM2149.hpp:35`) ; pas de garde select≥16 ; pas de read-latch 8 bits | Select 8 bits non masqué (`psg.c:252-258`), data ignorée + lecture 0xFF si select≥16 (`psg.c:283-284, 335-336`), latch `PSGRegisterReadData` (`psg.c:343`) | **Oui** (TODO.md:187) |
| **PSG I/O** (`no-write-masking`) | RMW/relecture sur R6/R8-10/R13 : bits parasites relus (cas Mindbomb/BBC, Murders In Venice) | low | Valeur brute stockée à l'écriture (`YM2149.hpp:37`), masquée seulement à la synthèse | Masquage à l'écriture &0x0f (R1/3/5/13) / &0x1f (R6/8/9/10) (`psg.c:349-358`) + latch non masqué (`psg.c:343`) | **Oui** (TODO.md:187) |
| **PSG I/O / reset** (`reset-porta-value` / `psg-reset-porta`) | Pas d'effet sonore direct ; effet de bord : sélection lecteur/face erronée au boot (face B, lecteur A) | low | `regs_.fill(0)` met R14=0 (`YM2149.hpp:65`) ; port A actif bas → toutes lignes assertées | `memset 0` puis `PSGRegisters[PORTA]=0xff` (`psg.c:222-223`) | Non |
| **PSG I/O** (`read-shadow-not-ff`) | Très rare : sonde $FF8801/03 pour 0xFF (détection HW) reçoit autre chose | low | `read8` ignore l'adresse, renvoie `regs_[selected_]` pour les 4 adresses (`YM2149.hpp:26-32`) | $FF8800 = donnée, $FF8801/02/03 = 0xff (`psg.c:524-542`) — $FF8802 relisible est un choix délibéré NeoST (CHANGELOG.md:369) | **Partiel** ($FF8801/03 non documenté) |
| **PSG I/O** (`porta-sink-only-rs232`) | Aucun symptôme sonore PSG ; strobe Centronics/GPIP0 BUSY absent, notif drive/face lazy | low | `write8` R14 ne notifie que RS232 si loopback (`Machine.cpp:85-93`) ; pas de Printer ni FDC_SetDriveSide eager | `PSG_Set_DataRegister` PORTA : strobe bit5 + Printer_TransferByteTo + GPIP0, FDC_SetDriveSide eager (`psg.c:367-460`) | **Oui** (TODO.md:183-184, 234-235) |

---

## 3. Plan de portage ORDONNÉ (regroupé par fichier NeoST)

### Phase A — Quick wins low-risk, gain audible/fiabilité immédiat

Ces correctifs sont localisés, sans refonte d'architecture, et corrigent des défauts réels.

**`src/core/YM2149.cpp` (boucle `synthesize`, lignes 107-125) — remplacer le mixage**
- **Combinaison ton+bruit par ET logique** (au lieu de la moyenne `v*=0.5`). Calculer un état binaire par voie : `toneBit = toneOn ? (phase<0.5) : 1`, `noiseBit = noiseOn ? (lfsr&1) : 1`, voie active si `toneBit & noiseBit`. Réf. `sound.c:1098-1111`. **Gain audible direct** sur tous les bruitages.
- (Idéalement combiner avec la table 3D ci-dessous, mais le passage à l'ET logique seul est déjà un net progrès.)

**`src/core/YM2149.cpp` + `YM2149.hpp` — table DAC non linéaire**
- Précalculer au reset/init `ymout5[32][32][32]` via `YM2149_BuildModelVolumeTable()` (`sound.c:615-678`, **autonome**, ne dépend que de MaxVol/WARP/FOURTH2 — le plus simple et le plus précis à porter). Fusionner les index 5 bits en `idx = (idxC<<10)|(idxB<<5)|idxA`, sortir `out[i] = ymout5[idx]` normalisé. Réf. `sound.c:238, 1111`. À faire **conjointement** avec l'ET logique (même boucle).

**`src/core/YM2149.cpp` — filtres de sortie + `YM2149.hpp` (état persistant x1_/y0_)**
- Passe-haut sous-sonique anti-DC `Subsonic_IIR_HPF` (`sound.c:382-394`, fc~13-15 Hz) en sortie de `synthesize`. Recalculer les coefficients pour float/48 kHz.
- Passe-bas `PWMaliasFilter` (défaut Hatari, `sound.c:479-492`) ou `LowPassFilter` STF (`sound.c:451-464`).

**`src/core/YM2149.hpp` (`reset`, ligne 65) — port A au repos**
- Ajouter `regs_[14] = 0xFF;` après `regs_.fill(0)`. Réf. `psg.c:223`. Corrige l'état initial sélection lecteur/face (faible risque, aucun effet son).

**`src/core/DmaSound.cpp` (`mix`, lignes 251-252) — appliquer le registre de mixage LMC**
- Lire `mwMixing_` : si `!= 1` (0/2/3 = DMA seul) → écraser (`out[i] = dma`) ; si `== 1` → additionner (comportement actuel). Réf. `dmaSnd.c:555-568`.

**`src/core/DmaSound.cpp` (`read8` cases 0x09/0x0B/0x0D, lignes 183-185) — adresse à l'arrêt**
- Si `!playing_` → renvoyer `startAddr_` ; sinon `curAddr_`. Réf. `dmaSnd.c:756-759`. (Volet 1 ; le volet « live cycle-exact » relève de la Phase C.)

**`src/core/DmaSound.cpp` (`reset`, ligne 69) — défauts de volume cold/warm**
- Ajouter un paramètre `bool cold` ; n'initialiser les volumes (à -80/-40/-40 dB = index 0) que si `cold` ; au warm reset, préserver `mwMaster_/Left_/Right_/Bass_/Treble_/Mixing_`. Réf. `dmaSnd.c:261-270`. Propager depuis `Machine::reset()` (warm) et `hardReset()/reconfigure()` (cold).

**`src/core/DmaSound.cpp` (`scheduleFrameEnd`/démarrage trame, lignes 26-34, 203-208) — cas start==end**
- Factoriser `startNewFrame()` : si `endAddr_<=startAddr_ && !(ctrl_&0x02)` (repeat off) → `playing_=false`, `ctrl_&=~0x01`, **ne PAS** monter XSINT, `cancel(DMASND)`, return. Réf. `dmaSnd.c:471-480`. Corrige le GPIP7 figé HAUT.

### Phase B — Équilibre d'amplitude STE (medium, touche plusieurs fichiers)

- Donner au sous-système audio la connaissance de la machine (flag `isSTE` de `Machine` → `YM2149`/`Audio`).
- **`src/core/YM2149.cpp` (sortie ligne 125)** : sur STE/MegaSTE/TT, multiplier la sortie YM par 0.5 (`YM_OUTPUT_LEVEL>>1`, `sound.c:780-784`) ; pleine échelle sur ST.
- **`src/core/DmaSound.cpp` (`kDmaGain`, `mix`)** : garder le DMA à ~3/4 du niveau YM et **doubler** le gain effectif appliqué au DMA pour compenser la demi-amplitude YM. Réf. `dmaSnd.c:558-566, 1153-1166`. Évite l'écrêtage dur du clamp `Audio.cpp:42-43`.

### Phase C — Refonte architecture audio (high, item de fond)

C'est le **gain audible le plus important** (digidrums/sync-buzzer/musiques muettes) mais le plus lourd. Déjà listé TODO.md:196-200.

- **Modèle « push » horodaté** : ajouter une file d'événements registres horodatés (cpuCycle, reg, value) remplie par `YM2149::write8` (`YM2149.hpp:33-41`) et `DmaSound::write8`, sur le thread émulation. Port conceptuel de `Sound_Update(Cycles_GetClockCounterOnWriteAccess())` AVANT le write (`psg.c:339`).
- **Synthèse interne 250 kHz + rééchantillonnage** : remplacer les accumulateurs de phase flottants par des compteurs entiers (`YM2149_DoSamples_250`, `sound.c:1041-1122`) puis `Resample_Weighted_Average_N` (`sound.c:1347-1383`). Le réarmement R13 devient `Env_pos=0;Env_count=0` au cycle exact (`sound.c:1507-1513`) ; remplace le drapeau `envReload_`.
- **Anneau émulation → audio** : réduire `Audio::render` (`Audio.cpp:33-44`) à une recopie d'anneau + gestion underrun (modèle `audio.c:75-104`).
- **DMA sur horloge émulation** : FIFO 8 octets remplie sur HBL (`DmaSnd_FIFO_Refill/PullByte`, `dmaSnd.c:343-410`), FIR anti-repliement (`dmaSnd.c:1316-1349`), compteur de trame avancé côté CPU + `Sound_Update` en tête de `read8` ($FF8909/0B/0D) — supprime aussi la course RAM CPU/audio et règle le compteur figé en headless.

### Phase D — Intégration reset (medium, fiabilité)

- **`src/io/Mfp.hpp`/`Mfp.cpp`** : ajouter `Mfp::reset()` (port de `MFP_Reset`, `mfp.c:523-568`) : gpip=0xFF, aer/ddr/iera/ierb/ipra/iprb/isra/isrb/imra/imrb/vr=0, vider `timer_[]`, compteurs Timer A/B, tai_/xsint_=false, annuler les échéances Scheduler TIMERA/B/C/D. Ne PAS toucher `colorMonitor_`/`hasDmaSound_`.
- **`src/core/Machine.hpp` (lignes 60, 64)** : appeler `mfp.reset()` dans `reset()` ET `hardReset()`, **avant** `cpu.reset()` (ordre Hatari `reset.c:74` avant `M68000_Reset`). Re-synchroniser ensuite XSINT/tai_ depuis `dmasnd`.

### Phase E — Détails I/O PSG (low, fidélité fine)

- **`src/core/YM2149.hpp`** : modèle 2 états de `psg.c` — select 8 bits non masqué + garde `≥16`, read-latch `regReadData_`, masquage à l'écriture, $FF8801/03 → 0xff (attention au choix délibéré $FF8802 relisible, CHANGELOG.md:369 — revalider les cartouches de diagnostic en headless `--cart --keys` sur les 2 cœurs avant de toucher $FF8802). Réf. `psg.c:252-358`.
- **Tone control LMC** : table de saturation `LMC1992_Bass_Treble_Table` + shelfs Savinkoff + fc 118.276/8438.756 Hz (`dmaSnd.c:211-214, 1369-1420`).
- **Centronics/strobe + FDC_SetDriveSide eager** : si on veut combler les gaps I/O (TODO.md:183-184, 234-235) — sans impact son.

---

## 4. Non-divergences / vérifié conforme (à ne PAS réinvestiguer)

- **Polynôme du LFSR de bruit** (`lfsr-polynomial`) : **FAUX POSITIF**. NeoST (Fibonacci 17 bits, rétroaction bit0^bit3) et Hatari (Galois `RndRack>>1 ^ 0x12000`) sont les représentations **duales du MÊME polynôme primitif degré 17**. Vérifié : suites identiques bit-pour-bit sur toute la période 131071 (décalage de phase -16). Grain/spectre du bruit identiques.
- **Volume LMC1992 appliqué à tout le mix** (`lmc-volume-applied-to-whole-mix`) : **FAUX POSITIF**. Dans Hatari, `DmaSnd_Apply_LMC` opère sur `AudioMixBuffer` qui contient **déjà** le YM mélangé (YM = « input 1 » du LMC1992, en-tête `dmaSnd.c:47-51`). Atténuer YM+DMA ensemble (`Audio.cpp:37-38`) est conforme au matériel.
- **Bruits de lecteur hors chaîne LMC** (`drivesound-...`) : **conforme**. Fonctionnalité d'immersion propre à NeoST (reprise de STeem SSE), **absente du matériel et non émulée par Hatari** — aucune référence comportementale, le critère cycle-accurate est sans objet. Améliorations possibles purement cosmétiques (gain propre, marge anti-clip).
- **Câblage XSINT → GPIP7 (XOR moniteur) + TAI Timer A** (`xsint-order-and-xor-conform`) : **conforme**. Même ordre (GPIP7 puis Timer A), XOR restreint à STE via `hasDmaSound_`, même règle de front, même séquence fin-de-trame LOW→HIGH, reset XSINT→BAS. Réserve : le cas start==end (Phase A) peut figer XSINT à tort, mais c'est un sujet distinct du câblage.
- **Réarmement enveloppe sur écriture R13** (`env-reset-via-flag-ok`) : **conforme** sémantiquement (réarmement à chaque écriture, sens initial Attack correct). Seule nuance = la granularité au bloc audio, couverte par la Phase C (timing cycle-exact).

---

## 5. Limites de validation et recommandation oracle

**Deux obstacles empêchent aujourd'hui une validation oracle bout-à-bout du son :**

1. **`neost-headless` n'émet aucun audio.** `Audio` n'est jamais instancié en mode headless (`main_headless.cpp` : zéro occurrence d'`Audio`/`render`/`mix`), donc `YM2149::synthesize` et `DmaSound::mix` ne sont **jamais appelés**. Conséquence directe : `curAddr_` (compteur de trame DMA) reste **figé** à `startAddr_`, et aucune forme d'onde n'est produite pour comparaison.

2. **Hatari headless ne boote pas les disquettes de test overscan/boot-sector** (cf. mémoire projet `hatari-headless-no-floppy-boot`) : il retombe sur le bureau. Les titres qui exercent le son (digidrums, sync-buzzer, samples STE) ne peuvent donc pas être lancés à l'identique des deux côtés pour une comparaison end-to-end.

**Recommandation : ajouter un trace de registres/échantillons son des DEUX côtés.**

- **Côté NeoST headless** : ajouter une option (p.ex. `--sound-trace`) qui, à chaque écriture PSG ($FF8800/02) et DMA/Microwire ($FF89xx), journalise `(cycleCPU, registre, valeur)` ; et, en pilotant `YM2149::synthesize`/`DmaSound::mix` de façon déterministe sur l'horloge d'émulation (pas le thread audio), dumpe un flux d'échantillons PCM (.wav/.raw) reproductible. Cela rend le son **observable et déterministe** en headless — prérequis indispensable.
- **Côté Hatari** : Hatari sait déjà dumper le flux YM 250 kHz (`YM_250_DEBUG` / `YM2149_DoSamples_250_Debug`, visible dans `sound.c`) et enregistrer la sortie audio en WAV. Activer ces sorties sur un même programme de test (cartouche de diagnostic bootant des deux côtés, ou un petit .prg PSG déterministe) permet une **comparaison oracle des traces de registres puis des échantillons**.
- **Pivot pratique** : commencer par comparer les **traces de registres horodatées** (indépendantes du backend audio) — c'est le moyen le plus fiable de valider la Phase C (modèle push horodaté) avant même de comparer les échantillons. Pour la géométrie/timing, continuer d'utiliser un glue-selftest comme noté en mémoire projet.

Sans ces traces, la validation des correctifs Phase A/B (mixage, filtres, amplitude) restera **auditive/subjective** sur la cible GUI ; les ajouter débloque une validation reproductible et chiffrable.
