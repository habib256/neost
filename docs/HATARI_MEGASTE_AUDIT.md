# Blitter et IRQ MegaSTE : contrôle des domaines d'horloge

État du 2026-10-19. Le banc `run_megaste_blitter_checks.py` vérifie les données,
les IRQ, la répétabilité et une borne inférieure de durée. Il ne remplace pas
les comparaisons temporelles strictes STE ou CPU/cache MegaSTE et ne produit
pas de référence temporelle à partir des échecs d'Hatari.

## Reproduire

```sh
python3 tools/run_megaste_blitter_checks.py
python3 tools/run_megaste_blitter_checks.py --oracle-audit
```

Le premier mode contrôle NeoST et appartient au palier `full`. Le second rejoue
aussi Hatari et **retourne un échec** si celui-ci viole le protocole ou la borne.
Les résultats restent dans `tests/out/megaste_blitter/` : programmes, deux
captures par cible, journaux et `checks.json` / `oracle_audit.json`.

Hatari : pin `f0736b24b32b0439300b52107ba6ab434469ec3c`, binaire canonique,
configuration et HOME isolés, graine 1, `--cpu-exact on --compatible on`,
EmuTOS 256 Ko FR libre. Les empreintes du programme, des sources et de
`clocks_timings.c` sont conservées dans le rapport. Aucun changement de
l'émulation Hatari n'a servi aux mesures de la table ci-dessous.

## Protocole

Douze configurations : HOG/partagé × code ROM/RAM ST × CPU 8 MHz, 16 MHz
sans cache ou 16 MHz avec cache. Le programme tourne avant TOS, sous 128 Ko,
programme le SCU et copie 16×8 mots par HOP=2/LOP=3, masques pleins.
Chaque exécution effectue 72 copies, dont huit échauffements et 64 mesures.

Pour chaque copie, le programme remet la destination à zéro, lance le blitter,
puis autorise IPL5. Il attend BUSY=0 **et** le passage réel dans le handler.
Le handler vérifie d'abord BUSY : une IRQ reçue pendant une copie active
rejoint `irq_while_busy` et fait échouer le banc immédiatement, y compris
pendant l'échauffement. Cette règle vient du manuel Atari, section
[Bus accesses / Busy](https://github-wiki-see.page/m/tattlemuss/atari_st_wiki/wiki/blitter_user_manual) :
la ligne d'interruption reflète l'état BUSY. Le handler incrémente ensuite
D6 et remet le SR empilé à IPL7 avant RTE. La copie suivante
ouvre explicitement sa nouvelle fenêtre d'IRQ après son lancement. D6 doit
valoir exactement un et les 128 mots doivent tous valoir `$5AA5` ; sinon le
programme rejoint `failure`. Le contrôleur hôte exige 72 armements, 72 IRQ et
72 fins, l'ordre armement < IRQ ≤ fin, une horloge monotone et le succès invité.
Aucune IRQ n'est retirée de la trace ou réaffectée par un filtre. Le rapport
identifie ce protocole par `megaste_blitter_irq_clocks_v3`. La sonde invitée
reste celle de v2 ; v3 contrôle aussi les 72 blocs IACK CPU dans chaque journal
NeoST, sans les confondre avec les horodatages en cycles bus.

Chaque cible est exécutée deux fois ; toutes les mesures et le travail CPU
doivent se reproduire. Le rapport conserve les 64 observations individuelles :
une anomalie tardive n'est pas masquée par les premières copies ou la période
dominante.

## Borne de durée

La table d'horloges du pin (`clocks_timings.c`, branche MegaSTE) donne BUS et
BLITTER à CLK8, CPU à CLK16. Les constantes de `blitter.c` donnent quatre
cycles par lecture/écriture et une arbitration MegaSTE de huit cycles à la
prise, quatre à la restitution. Un hit CPU coûte quatre cycles CPU, comme les
accès FAST (`m68000.c`, fonctions `mem_access_delay_*_megaste_16`).

Pour N mots, s=1 en HOG, sinon s=ceil(2N/64), la borne en cycles bus est :

`8*N + 12*s + 64*(s-1)*(4 à 8 MHz, 2 à 16 MHz)`.

Le bug matériel des 63 accès, les misses, les attentes et les délais IRQ ne
peuvent qu'ajouter du temps à cette borne. Pour 128 mots : HOG ≥1036 cycles
bus, partagé ≥1840 à 8 MHz ou ≥1456 à 16 MHz, avec ou sans cache.

Les dates NeoST sont en cycles bus. Les intervalles Hatari à 16 MHz sont
divisés par deux ; aucune différence de date ne traverse la bascule de
fréquence. La borne s'applique à chacune des 64 durées armement→fin et
armement→entrée du handler, sans tolérance ni décalage de compensation.

## Résultats observés au pin

Les douze configurations NeoST satisfont le protocole et les bornes. L'audit
du pin Hatari ne constitue pas une référence temporelle globale valable pour
cette matrice. Exemples de minima mesurés, en cycles bus :

| Configuration | Borne | NeoST fin / IRQ | Hatari fin / IRQ |
|---|---:|---:|---:|
| HOG, CPU 8 MHz, code ROM | 1036 | 1242 / 1124 | 1238 / 1122 |
| HOG, CPU 16 MHz, code ROM | 1036 | 1173 / 1100 | 645 / 573 |
| Partagé, CPU 8 MHz, code ROM | 1840 | 2406 / 2288 | IRQ pendant BUSY : échec |
| Partagé, CPU 16 MHz, code ROM | 1456 | 1789 / 1716 | IRQ pendant BUSY : échec |
| HOG, CPU 16 MHz + cache, code RAM | 1036 | 1232 / 1124 | 696 / 588 |

Les six cas partagés Hatari rejoignent directement le piège invité
`irq_while_busy`. Les quatre cas HOG à 16 MHz terminent trop tôt ; seuls les
deux HOG à 8 MHz passent. Les douze cas NeoST passent sans ce piège.
La garde ISR ajoute du travail CPU après l'entrée d'IRQ, donc les durées
armement→fin de v2 ne sont pas celles de la sonde initiale. Aucun ancien
chiffre n'est utilisé comme référence de v2.

## Audit indépendant et expériences causales

Deux défauts distincts du pin ont été reproduits dans une copie temporaire
pendant l'investigation initiale (sonde v1, avant la garde BUSY dans l'ISR) :

- `blitter.c:340`, `Blitter_AddCycles` ajoute des cycles DMA 8 MHz au compteur
  CPU sans conversion. Remplacer `cycles + WaitStateCycles` par
  `(cycles << nCpuFreqShift) + WaitStateCycles` rétablit les bornes des huit
  configurations à 16 MHz.
- `m68000.c:752`, `M68000_Update_intlev` n'appelle pas `doint()` si
  `pendingInterrupts == 0`. `regs.ipl_pin` peut rester à 6. Dans le cas partagé
  ROM à 8 MHz, la première entrée prématurée (copie 13) acquitte au cycle
  140376 alors que `IRQ=0`, `IRQ_CPU=0` et `IPRB=0` ; GPU_DONE n'arrive qu'à
  142324. Les copies suivantes consomment alors l'IRQ de la copie précédente.
  Appeler aussi `doint()` quand aucun interrupt n'est pending supprime cette
  cascade : les 64 IRQ mesurées valent 2286 cycles en ROM et 2372 en RAM.

Ainsi, une IRQ par fenêtre et D6=1 ne prouvent pas à eux seuls l'association
IRQ↔copie. La sonde v2 vérifie donc BUSY directement dans l'ISR et refuse ce cas sans
attendre de compléter la série.
L'anomalie n'est pas un front GPU_DONE prématuré.

Avec les deux corrections expérimentales, les douze configurations Hatari
respectent les bornes. Exemples à 16 MHz, en cycles bus : HOG ROM fin/IRQ
1145/1089, partagé ROM 1753/1697. Des écarts de l'ordre de 12 à 24 cycles
subsistent face à NeoST : ces expériences ne constituent toujours pas un
oracle temporel strict. Le patch diagnostique
[`hatari_megaste_audit.patch`](../tools/hatari_megaste_audit.patch) conserve
uniquement les deux changements fonctionnels ; il n'est appliqué ni par le
setup Hatari ni par les bancs. Les sources et le binaire canoniques restent
inchangés. Les contre-exemples canoniques continuent de faire échouer l'audit.

## Correctif NeoST indépendant

PRE_START vaut quatre cycles bus après la **fin** de l'écriture de contrôle.
Au callback médian Moira, il reste deux cycles CPU : deux cycles bus à 8 MHz,
un seul à 16 MHz. Le délai armé depuis ce callback vaut donc six ou cinq
cycles bus. L'ancienne soustraction `8 - moitié déjà écoulée` donnait sept
cycles à 16 MHz : deux cycles de retard.

Le test exécute une vraie écriture CPU de `$FF8A3C` et mesure la prise du bus,
en HOG/partagé, CPU 8/16 MHz et cache demandé ou non. Quatre échecs avant
correction, zéro après ; les 127 contrôles CPU/blitter passent sous ASan/UBSan.

Ces gardes bornent des comportements vérifiés. Elles n'établissent ni une
égalité temporelle globale avec Hatari à 16 MHz, ni une validation sur matériel
réel. Les contre-exemples du pin restent des échecs explicites de l'audit.

## Cohérence BUSY / GPU_DONE dans NeoST

Un événement servi pendant les quatre cycles de restitution pouvait lire
BUSY=0 alors que GPU_DONE indiquait encore une copie active. `runSlice`
effaçait BUSY dès la fin des données ; `finishTransfer` ne changeait la
ligne MFP qu'après l'arbitrage. Le statut est désormais maintenu pendant
la restitution, puis BUSY et GPU_DONE changent ensemble dans `finishTransfer`.
La date de l'IRQ et les coûts DMA restent inchangés.

Quatre cas CPU 8/16 MHz × HOG/partagé reproduisent le défaut avant correction,
puis passent après : 155 contrôles CPU/blitter au total sous ASan/UBSan.
L'événement témoin vérifie aussi le dernier mot et l'état GPIP3 pendant
l'arbitrage, puis la fin et les 20 cycles bus d'une copie d'un mot MegaSTE
(prise 8 + lecture/écriture 8 + restitution 4), indépendamment du CPU.

## Horloge des cycles internes après IACK

Le [manuel Motorola MC68000](https://www.nxp.jp/docs/en/reference-manual/MC68000UM.pdf),
section 8 (notamment introduction et §8.12), exprime les opérations internes
et les temps d'exception en périodes CLK du processeur, auxquelles s'ajoutent
les attentes mémoire/périphérique. Le wrapper mélangeait ces domaines : il
multipliait aussi les quatre cycles internes après IACK par le rapport CPU/bus.

Le bloc MFP conservé vaut désormais `16 * cpuMul + 4` cycles CPU, au lieu de
`(16 + 4) * cpuMul` : 20 à 8 MHz, 36 à 16 MHz au lieu de 40. La calibration
périphérique de 16 cycles bus est conservée ; ce correctif d'unités ne constitue
pas une mesure matérielle de sa valeur exacte. La vidéo applique le même
principe : dix cycles bus après l'attente E-clock, puis quatre cycles CPU.
Le réglage diagnostique `NEOST_IACK_VIDEO=0` conserve un bloc nul.

Avant correction : les huit configurations MFP à 16 MHz échouent au contrôle
IACK ; les tests HBL/VBL mesurent aussi deux cycles bus de trop. Après :
199 contrôles CPU/blitter passent sous ASan/UBSan, dont les dix phases
E-clock pour les deux niveaux vidéo et les deux fréquences CPU. Les écritures
de pile des tests vidéo vont dans une zone void sans attente ni bus error,
pour observer l'IACK sans le masquer par le créneau RAM ; aucun RTE n'est
exécuté dans ce montage de mesure. Les 21 tests des comparateurs passent.

Dans les douze copies MegaSTE ci-dessus, le créneau RAM absorbe la différence
après IACK : les dates d'entrée du handler restent identiques. Les écarts
historiques de 12–24 cycles face à Hatari modifié ne sont donc pas déclarés
clos par ce correctif. Les composants MFP, E-clock et PRE_START du banc
expérimental restent à qualifier globalement, puis à confronter au matériel.

## Décomposition de l'écart global (2026-10-19)

**L'écart de 12–24 cycles n'est pas une dérive du transfert DMA.** Il désigne
la fin observée par la boucle CPU. L'entrée du handler diffère de 11–24 cycles
bus selon la configuration. La comparaison ci-dessous utilise le même programme
v3 et le Hatari temporaire avec les deux corrections initiales ; le pin
canonique reste inchangé et ne devient pas une référence valide.

Les marqueurs `BLSTART`, `BLEND`, `BLDONE`, entrée d'exception et IACK séparent :
armement → première prise → DMA/arbitrages et fenêtres CPU → GPU_DONE →
exception → IACK → première instruction du handler → fin observée en polling.
Chaque cible est capturée deux fois ; les 64 décompositions doivent se
reproduire et leur somme doit restituer exactement le total. Les 72 copies
restent soumises aux vérifications invitées. Le régime mesuré exclut les huit
échauffements, notamment le changement d'origine de compteur du premier IACK.

### Mesures avant expériences supplémentaires

Delta = NeoST moins Hatari diagnostique, en cycles bus :

| CPU 16 MHz | Premier départ | DMA + arbitrages | Fenêtres CPU | IACK → handler | Total IRQ | Total polling |
|---|---:|---:|---:|---:|---:|---:|
| HOG, ROM | +2 | 0 | 0 | +11 | +11 | +12 |
| HOG, RAM | +2 | 0 | 0 | +12 | +16 | +16 |
| HOG, RAM + cache | +2 | 0 | 0 | +12 | +20 | +20 |
| Partagé, ROM | +2 | 0 | +4 | +11 | +19 | +20 |
| Partagé, RAM | +2 | 0 | +8 | +12 | +20 | +20 |
| Partagé, RAM + cache | +2 | 0 | +11 | +12 | +24 | +24 |

Les colonnes ne s'additionnent pas seules : les phases GPU_DONE→exception et
exception→IACK, présentes dans le rapport, peuvent ajouter ou retrancher du
temps suivant le polling et le créneau RAM. Exemple HOG ROM :
`2 + 0 + 0 - 4 + 2 + 11 = 11`, puis un cycle de plus jusqu'à la fin observée.

Le transfert proprement dit coûte **1036 cycles bus en HOG** sur les deux
cibles : 128×8 + 8 + 4. En partagé : **1084**, soit 128×8 + 5×(8+4).
Il y a cinq tranches, car le comptage à 63 accès laisse une tranche résiduelle.
Ces coûts coïncident dans toutes les configurations, aux deux fréquences.

### Expérience PRE_START dans Hatari temporaire

Sans changer NeoST, convertir les seuls délais de demande du bus :

```c
/* démarrage : fin de l'accès CPU, puis quatre cycles bus */
Blitter_CyclesBeforeStart = 4 + (4 << nCpuFreqShift);
/* reprise : quatre cycles bus */
Blitter_CyclesBeforeStart = 4 << nCpuFreqShift;
```

L'écart du premier départ passe de +2 à **0** dans les huit configurations
à 16 MHz. Les fenêtres CPU du partagé deviennent identiques en ROM, en RAM
sans cache et en ROM avec cache. Le cas RAM/cache conserve quatre cycles
sur sa **première** fenêtre CPU : 162 contre 158 ; les trois suivantes valent
156 sur les deux cibles. Les résultats à 8 MHz sont inchangés.

Cette expérience isole une différence de domaine dans les délais du modèle
Hatari ; elle ne justifie aucun raccourcissement de PRE_START dans NeoST.

### Expérience de préfetch dans une copie temporaire de Moira

La première différence RAM/cache apparaît au retour de la première tranche :
`MOVE.W #$2500,SR` commence à +278 sur les deux cibles, mais le BTST suivant
commence à +294 chez NeoST contre +292. À l'instruction suivante, le décalage
atteint quatre cycles puis reste constant dans cette fenêtre CPU.

Le code `Moira::execMoveToSr` lit fictivement `reg.pc+2`, puis préfetche encore
cette adresse. Le code CE généré d'Hatari relit d'abord `reg.pc`, puis
`reg.pc+2`. Changer **uniquement** l'adresse de la lecture fictive dans un
binaire NeoST temporaire supprime les quatre cycles de la fenêtre partagée
RAM/cache, sur les 64 copies et deux captures. Le coût DMA reste identique.
Le binaire normal et les sources vendorisées ne sont pas modifiés.

Ce contre-exemple prouve l'origine logicielle de cette différence de cache ;
il ne prouve pas quelle séquence de préfetch reproduit le vrai 68000.
Le cas HOG RAM/cache conserve alors un décalage de reconnaissance d'IRQ de
zéro ou quatre cycles selon les copies : le total IRQ vaut +12 ou +16,
tandis que le segment IACK→handler reste toujours à +12.

### Correction du préfetch appuyée par le microcode matériel

La transcription du silicium publiée par Olivier Galibert permet maintenant
de trancher la séquence 68000 indépendamment d'Hatari :
[all.txt](https://og.kervella.org/m68k/all.txt), entrées `46c0` et `46fc`.
`301 rstw1` recule AU de deux octets ; `321 stiw4` mène à `1e1 malw3`,
qui relit le prochain opcode et charge IRC. `363 b`/`34c mmrw3` lit ensuite
le mot suivant et transfère le nouvel opcode vers IR/IRD. La provenance
est la [reconstruction du circuit à partir des couches du die](https://og.kervella.org/m68k/readme).
Le [code microcodé MAME](https://github.com/mamedev/mame/blob/master/src/devices/cpu/m68000/m68000-sdf.cpp)
rend cette même séquence explicite ; il constitue un recoupement.

NeoST utilise donc désormais `fullPrefetch<C68000, POLL>` après l'écriture
du SR. Une simple lecture fictive de `pc` serait encore incomplète : sa
valeur doit remplacer l'ancien opcode. Le test modifie le NOP préfetché en
MOVEQ avant MOVE D0,SR ; les deux fréquences échouent avec l'ancien code
et avec la variante fictive, et passent avec le rechargement complet.

Les douze comparaisons, deux captures chacune, sont conservées dans
`tests/out/megaste_gap/corrected-prefetch/`. Les fenêtres CPU partagées
à 16 MHz coïncident maintenant, cache inclus. Les 203 contrôles
CPU/blitter passent. Cela clôt le défaut de préfetch identifié ; cela ne
qualifie pas encore la latence périphérique d'IACK du MegaSTE.

### Partie dominante : IACK → handler

Le delta de ce segment vaut **+11 en ROM, +12 en RAM**, avec ou sans cache,
en HOG comme en partagé. Il reste inchangé après les expériences ci-dessus.

Une différence de modèle en explique dix cycles : NeoST conserve un bloc
MFP de 16 cycles bus plus quatre cycles CPU internes (18 cycles bus à
16 MHz). Hatari exécute `CPU_IACK_CYCLES_MFP_CE=12` puis quatre cycles
internes dans son domaine CPU 16 MHz, soit huit cycles bus. Les différences
de créneau des écritures de pile et des fetches ajoutent le résidu du segment.
À 8 MHz, les écarts déjà connus restent +2 en ROM et +4 en RAM.

La valeur 12 est explicitement **non mesurée sur STF réel** dans
`m68000.h` d'Hatari ; la valeur 16 de NeoST vient d'une calibration à 8 MHz.
On ne peut donc choisir la bonne latence MegaSTE à partir de leur seule
concordance. La suite matérielle doit isoler l'IACK MFP et la séquence de
préfetch après écriture du SR, plutôt que retirer 12 ou 24 cycles au blitter.

### Reproduction et preuves

```sh
python3 tools/analyze_megaste_blitter_gap.py \
  --hatari /chemin/hatari-instrumente \
  --hatari-source /chemin/sources-instrumentees \
  --out tests/out/megaste_gap/experience
```

Le binaire diagnostique doit émettre les marqueurs du diff conservé dans
`hatari-source.patch`. `--neost` et `--neost-moira-source` permettent de
comparer un binaire temporaire en conservant ses empreintes. Aucune sortie
n'est écrite dans `tests/reference/`.

Les trois rapports de cette analyse sont conservés localement dans
`tests/out/megaste_gap/{baseline,prestart,prefetch-matrix}/phases.json`, avec
empreintes binaires/sources/programmes et diffs expérimentaux. Les captures
complètes restent dans `/tmp/neost-global-gap-*`. Le parseur refuse aussi un
marqueur manquant ou un cycle artificiellement déplacé dans une tranche.

Après correction du préfetch, le palier `full` passe sur les médias
disponibles (GUI incluse, Closure et NoCooper à zéro pixel d'écart), ainsi
que les 21 tests des comparateurs. Restent non exécutés : Cubase Lite,
diagnostic MegaSTE et quatre étalons pixel propriétaires. Le résidu à
16 MHz est de 11 cycles bus en ROM et 12 en RAM entre IACK et handler ;
L'ancien résidu de reconnaissance d'IRQ HOG RAM/cache à 0 ou +4 cycles
est isolé par l'expérience de cohérence cache ci-dessous. Supprimer le
résidu IACK dans NeoST ne serait pas une correction démontrée.

Pour trancher l'IACK matériel, relever sur MegaSTE CLK CPU, CLK MFP,
AS/DS, IACK, DTACK et IPL avec le même programme, aux deux fréquences,
cache désactivé puis activé. Séparer le début de l'IACK, la réponse DTACK
et les cycles internes suivants ; comparer ensuite les accès de pile et
les fetches du handler. Le manuel NXP nommé `MC68901UM.pdf` décrit en
réalité le **MC68HC901** : ses chronogrammes ne suffisent pas à fixer
arbitrairement la latence du MK68901 et de la logique du MegaSTE.

### Remplissage du cache après invalidation DMA (2026-10-19)

Dans Hatari, `mem_access_delay_wordi_read_megaste_16` appelle
`wait_cpu_cycle_read_megaste_16`, puis `MegaSTE_Cache_Update`. L'attente peut
exécuter le DMA et vider le cache sur BGACK avant de revenir. Le remplissage
qui suit réintroduit alors le mot prélevé **avant** cette invalidation.
NeoST remplit son cache avant cette attente et le DMA invalide ensuite ce mot.
Le cas HOG RAM/cache alterne ainsi dans Hatari des chemins de préfetch
différents et une reconnaissance d'IRQ décalée de quatre cycles.

Le patch expérimental
[`hatari_megaste_cache_audit.patch`](../tools/hatari_megaste_cache_audit.patch)
incrémente une génération à chaque flush et refuse le remplissage d'une
lecture traversant une invalidation. Il couvre les lectures données et
instructions, octet et mot ; la génération est relevée après l'attente
préalable d'accès au bus. Ce patch est distinct des deux corrections
initiales ; il n'est jamais appliqué automatiquement au pin canonique.

Avec cette garde, les douze configurations, deux captures chacune et
64 copies retenues par capture, concordent sur toutes les phases avant
IACK. Le résidu HOG RAM/cache disparaît sans modification de NeoST.
Rapport local, empreintes et diff complet :
`tests/out/megaste_gap/cache-generation-final/`. Le segment IACK→handler
reste +11 cycles bus en ROM et +12 en RAM à 16 MHz (+2/+4 à 8 MHz).
Ce résultat identifie le mécanisme logiciel du résidu de cache ; ce n'est
pas une mesure de la latence matérielle MegaSTE ni une nouvelle référence.

Le [manuel SGS-Thomson MK68901 de décembre 1988](https://www.tvsat.com.pl/pdf/m/mk68901_stm.pdf),
figures 23–24, décrit la synchronisation IACK/IEI et la réponse DTACK par
rapport à CLK du MFP. Il ne suffit pas à fixer la phase du MFP par rapport
au CPU et aux attentes de la carte MegaSTE : le relevé matériel décrit
ci-dessus reste nécessaire.

### Expérience IACK en domaine bus (2026-10-19)

Après les corrections expérimentales de pré-départ, d'IPL et de cache,
un seul changement supplémentaire est appliqué dans la copie temporaire :
`CPU_IACK_CYCLES_MFP_CE << nCpuFreqShift` au lieu de la constante CPU 12.
Le [patch conservé](../tools/hatari_megaste_iack_domain_audit.patch) est une
**hypothèse diagnostique**, jamais appliquée automatiquement. Aucun réglage
ni code d'exécution NeoST n'est modifié dans cette expérience.

Les douze cas sont recapturés deux fois, avec 64 transferts retenus chacun.
Résultats, delta NeoST moins Hatari expérimental en cycles bus :

| Segment IACK → handler | Avant conversion | Après conversion |
|---|---:|---:|
| 8 MHz, code ROM | +2 | +2 |
| 8 MHz, code RAM | +4 | +4 |
| 16 MHz, code ROM, cache demandé ou non | +11 | +3 |
| 16 MHz, code RAM, cache demandé ou non | +12 | +4 |

HOG et partagé donnent les mêmes résultats. Toutes les phases antérieures
à IACK concordent. Le passage 12 cycles CPU → 12 cycles bus ajoute six
cycles bus à l'attente à 16 MHz ; le créneau de pile qui suit en ajoute
deux dans ces cas, d'où une réduction de huit du segment observé. Il ne
faut pas attribuer ces huit cycles au DMA ni les retrancher du blitter.
Rapport, empreintes et diff complet :
`tests/out/megaste_gap/iack-bus-domain/phases.json`.

Cette expérience rapproche les domaines d'horloge et localise le résidu ;
elle ne tranche pas la constante exacte. La sonde autonome
[`make_mfp_iack_probe.py`](../tools/make_mfp_iack_probe.py) produit désormais
un PRG relogeable et une disquette AUTO pour le mesurer sur MegaSTE.
Le [protocole matériel](MFP_IACK_MEASUREMENT.md) décrit les signaux à capturer.
Les contrôles cartouche, AUTO MegaSTE et refus ST/STE passent dans NeoST ;
aucune capture sur un Atari réel n'est encore disponible.
