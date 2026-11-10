# Échelle des références

(c) 2026 VERHILLE Arnaud. **Contre quoi chaque verdict du harnais est-il vrai ?** Les grades
de `CLAUDE.md` qualifient une affirmation dans le code ; cette échelle qualifie le verdict
d'un test. Elle a remplacé le 2026-11-03 le couple `oracle`/`snapshot`, qui présentait un
accord avec Hatari comme un verdict de vérité.

**Un vert ne vaut que ce que vaut son échelon.** Un étalon `hatari` vert veut dire
« d'accord avec Hatari », et un `snapshot` vert veut dire « n'a pas bougé ».

## Les échelons

Du plus fort au plus faible. Les chiffres sont recomptés par `tools/check_doc_claims.py`.

| `ref_kind` | Grade | Ce qu'est la référence | Pixel |
|---|---|---|---|
| `hardware` | A | Relevé sur machine réelle, propre ou publié par un tiers nommé | 0 étalons |
| `primary` | B | Attendu calculé depuis une fiche, un schéma, une netlist Atari | 0 étalons |
| `rom` | C | Verdict d'un programme ou d'une ROM Atari | 0 étalons |
| `rtl` | D | Simulation d'un RTL dérivé du silicium ou d'un schéma | 3 étalons |
| `hatari` | D | Capture de l'Hatari épinglé | 40 étalons |
| `snapshot` | aucun | Self-capture NeoST : non-régression, aucune preuve | 13 étalons |

## Les verdicts hors étalons pixel

| Verdict | Échelon | Note |
|---|---|---|
| `run_hatari_suite.py`, `buserror-*` (8) | `hardware` | [A] tiers : relevés de `tests/buserror/results` @ `f0736b24`. Machine et révision des puces non consignées. |
| `run_hatari_suite.py`, `cycles`, `blitter`, `cpu`, `gemdos`, `serial` (MFP, MIDI, SCC) (7) | `hatari` | Aucun readme ne donne la provenance de l'attendu. |
| `run_spec512_truth.py` (13 images, ST et STE) | `rom` | De vraies images Spectrum 512 par le vrai visualiseur, contre la formule du format, sans aucun émulateur : 0 pixel faux. Juge la date des écritures palette, à 4 pixels près. |
| `run_wsdetect.py` (7 cas, ST WS1 à WS4, Mega ST, STE, Mega STE) | `rom` | Le détecteur de Troed/SYNC compose une ligne de 4 bascules et lit ce qu'elle consomme : 162, 54, 206, 204 et 180 octets, sans aucun émulateur. Juge la grille de la GLUE, les dates d'écriture et le compteur. Pas de relevé de ce disque sur machine réelle au dépôt. |
| `run_sting_boot.py` | `rom` | Le vrai pilote `ENEC.STX` charge et active le port sur la NE2000 de NeoST, sans réseau. |
| `run_megaste_diag.py` (suite Q, 12/12) | `rom` | Le diagnostic Field Service prouve le contrat attendu par le logiciel, pas le silicium. |
| `run_timing_oracle.py`, `run_megaste_bus_oracle.py` | `hatari` | |
| `run_game_replay.py` | `hatari` | Des parties jouées au joystick, mêmes masques des deux côtés, à l'image près. Hors paliers : Pillow et disques privés requis, à lancer à la main. |
| `run_gstmcu_bench.py` | `rtl` | Valide le banc contre un écran calculé, pose les références de `rtl_low_*`, contrôle la latence de DTACK par zone. |
| `run_fx68k_bench.py` (43 variantes, 34 en accord) | `rtl` | Moira nu contre fx68k, cycle bus par cycle bus. Verdicts posés dans `tests/reference/fx68k_bench.json` ; un écart localise, ne tranche pas. → [`FX68K_BENCH.md`](FX68K_BENCH.md) |
| `run_stcpu_bench.py` (48 000 lectures, 1 écart) | `rtl` | Le CPU lit le compteur vidéo, en boucle, en HBL et en VBL, sur fx68k + GSTMCU et dans NeoST. → [`STCPU_BENCH.md`](STCPU_BENCH.md) |
| `run_glue_sweep.py` (2560 impulsions à 50 Hz et autant à 60 Hz, 14 et 42 écarts inscrits) | `rtl` | Machine GLUE du STE contre le modèle GSTMCU : DE et octets, écriture par écriture. → [`GLUE_SWEEP.md`](GLUE_SWEEP.md) |
| `run_stblitter_bench.py` (78 programmes, tous en accord) | `rtl` | Blitter de NeoST contre fx68k + `stBlitter.sv`, accès, données et dates. → [`STBLITTER_BENCH.md`](STBLITTER_BENCH.md) |
| `run_mfp_iack_probe.py` | aucun | Contrôle la sonde, sans verdict matériel. |
| `neost-selftest`, `--*-selftest` | par test | `tools/classify_selftests.py` donne l'échelon que chaque test déclare dans ses commentaires, sans relire ses attendus → `TODO.md` § 4. |

## L'échelon `rtl`

Ce grade reste D : un RTL est un autre modèle, jamais validé ici sur du silicium. Il obtient
un échelon distinct parce qu'il vient d'ailleurs qu'Hatari, et certains blocs décrivent le
circuit plutôt que son comportement.

Épingles : `tools/setup_silicon_refs.sh` (fx68k `0602ee46`, MiSTery `a00deb5b`, gstmcu
`4a6db7cc`). Les citations s'écrivent `gstshifter.v:121 @ 4a6db7cc`.

| Bloc | Origine déclarée | Poids |
|---|---|---|
| `gstmcu/hdl` GLUE + MMU STE | Schémas relevés par Christian Zietz sur les ASIC | Fort ; à confronter à `ST4081S.PDF`, qui est [B] |
| `fx68k.sv` | Cœur « cycle exact » de Jorge Cwik | Fort |
| `stBlitter.sv` | Jorge Cwik | Fort |
| `gstshifter.v` | Schémas issus de rétro-ingénierie, « mostly cycle accurate » | Moyen |
| `mfp.v`, `dma.v`, `fdc1772.v` | Modèles comportementaux | **Pas l'échelon `rtl`** : un témoin de grade D ordinaire |

Ce classement « fort / moyen » repose sur ce que les auteurs déclarent, pas sur une
vérification de notre part. `[F]` Le MegaSTE et le ST n'ont aucun bloc équivalent : gstmcu
décrit le STE.

## Les documents primaires

Rapatriés et vérifiés par empreinte (`tools/setup_silicon_refs.sh`) dans `extern/datasheets`,
gitignoré car sous copyright.

| Puce | Document | Limite |
|---|---|---|
| GLUE + MMU STE | `asic/…/4081_COMBO/4081/DWG/pdf/ST4081S.PDF`, 49 p. | Le README de Zietz dit « seems to be the latest » |
| Blitter | `asic/…/4051/DWG/pdf/4051.pdf`, `4082.pdf` | Plusieurs révisions, sans repère de celle du ST |
| MFP 68901 | `MC68901_Jan84.pdf`, `MK68901.pdf` | Motorola en « Advance Information » ; Mostek est une seconde source |
| 68000 | `M68000UM_Rev8_1993.pdf` | |
| 68000, microcode | `m68k_microcode_all.txt` | Transcription tierce, pas un document Motorola. Grade D : elle donne l'ordre des accès, et fx68k en dérive, donc elle ne le confirme pas de façon indépendante |
| 68881 | `MC68881_MC68882_UM_1987.pdf` | |
| YM2149 | `YM2149_199209.pdf` | |
| WD1772 | `WD177X-00.pdf` | Scan médiocre, OCR à revérifier sur l'image |
| ACIA 6850 | `MC6850.pdf` | Copie hébergée par un tiers |
| SCC 85C30 | `Zilog_SCC_ESCC_UM.pdf`, `Zilog_PS0117.pdf` | |

**Non trouvés** : RP5C15 (RTC), HD6301 (IKBD), schéma du GLUE/MMU du ST et du Mega STE, schéma
du Shifter ST.

⚠ **`4118_GAME_SHIFTER` n'est pas le Shifter du ST** : le README de Zietz le donne pour le
« 68K Game Shifter », probablement celui de la console Panther. Il ne prouve rien sur le ST
sans autre source.

## Ce que le modèle confirme déjà

Modèle GSTMCU (STE, 2026-11-03) contre `kSte` et `vblankAtLine`. Cycle = tick de 32 MHz ÷ 4 − 52 :
l'origine est imposée par HSync, qui commence à −52, et toutes les autres valeurs en découlent.
Lignes = ligne depuis VSYNC − 2.

| Valeur | Modèle 50 / 60 Hz | NeoST 50 / 60 Hz |
|---|---|---|
| Cycles par ligne | 512 / 508 | 512 / 508 |
| Lignes par trame | 313 / 263 | 313 / 263 |
| HSync, relatif à la fin de ligne | −52 → −12 | −52 → −12 |
| Fin de BLANK horizontal | 28 / 24 | 28 / 24 |
| DE | 56 → 376 / 52 → 372 | 56 → 376 / 52 → 372 |
| Lignes actives | 63 → 262 / 34 → 233 | 63 → 262 / 34 → 233 |
| Lignes hors VBLANK | 25 → 307 / 16 → 257 | 25 → 307 / 16 → 257 |

Le pixel (`rtl_low_*`) : 0 pixel actif faux, au même emplacement du buffer. Bordures égales en
PAL, sauf les lignes sous BLANK : noires dans le modèle, couleur 0 chez NeoST et Hatari. NeoST et
Hatari sont identiques entre eux sur ces écrans, à 0 pixel sur tout le buffer.

⚠ Écran statique : ces étalons ne disent rien des écritures en cours de ligne. Ces lignes
comparent des modèles entre eux, pas au silicium, et NeoST a pris ces valeurs chez Hatari.

## Les règles d'arbitrage

1. **Un échelon inférieur ne l'emporte jamais sur un supérieur.** `--oracle` refuse d'écraser
   une référence `hardware`, `primary`, `rom` ou `rtl`.
2. **Hatari et un RTL en désaccord :** ne s'aligner sur aucun des deux d'emblée. Remonter à
   la netlist, à la fiche ou à une ROM. Sans source plus haute, la question porte sur :
   - un signal ou un cycle d'un bloc « fort » : le RTL prévaut, à titre provisoire ;
   - le reste : la question reste `[F]`, avec ce qui la trancherait.
3. **Une référence se déclare à son vrai échelon.** Une capture Hatari reste `hatari` même
   si tout le monde la croit juste, et un relevé tiers nomme sa source.
4. **Promouvoir un étalon** (`hatari` → `rtl`) n'efface pas l'ancienne référence : elle reste
   à côté, et l'écart est consigné dans `HATARI_DIVERGENCES.md`.

## Quand le RTL se trompe (2026-11-04)

Trois bancs ont confronté le modèle GSTMCU au logiciel réel, et deux fois le logiciel a contredit le RTL, pourtant bloc « fort » pour le GLUE :

- **Palette** : le Shifter du modèle change la couleur 4 pixels avant ce que 13 images Spectrum 512 exigent (`run_spec512_truth.py`, 0 pixel faux pour NeoST, de 10 à 1 118 selon l'image avec ce décalage).
- **`$FF820A`** : le modèle réagit 3 cycles avant la date d'Hatari. Le suivre casse DHS More or Less Zero et Double Rez Trouble.

Une explication commune reste à établir : le modèle prendrait les écritures quelques cycles trop tôt, par ses verrous transparents. **Règle** : un écart de date d'écriture contre le RTL ne se porte pas sans passer par des démos ou des images réelles. La lecture du compteur, les interruptions et la logique des fenêtres, elles, concordent et ont été portées.

## Quand une fiche lue à la lettre bloque le logiciel réel (2026-11-04)

MC6850 § Receiver Overrun p. 8-9 : « The RDRF bit remains set until the Overrun is reset ». Appliqué à la lettre, Enchanted Land, Lemmings, Dynamite Dux, Lethal Xcess et Magic Pockets restent bloqués sur leurs premiers écrans. Ces programmes tournent sur la machine réelle : ou la phrase ne dit pas ce qu'elle semble dire, ou leur overrun n'y arrive pas à ce moment. NeoST garde donc le comportement d'Hatari, marqué `⚠ [B]` dans le code, et le relevé d'un vrai 6850 tranchera. Même cas sur le WD1772, `$D8` et Super Hang-On (`src/io/Fdc.cpp`). **Règle** : une correction de fiche se joue sur les disques (A/B au pixel) avant d'être gardée. Si elle ne change aucune image, la fiche prime. Si elle en change, on regarde lesquelles.

## Sans machine réelle

Le projet n'a pas d'accès à un vrai ST. Le sommet atteignable passe donc, dans l'ordre, par :

1. **Les relevés de tiers** — déjà les bus errors ; à chercher : ST-STE Hardware Test (Troed),
   notes de Steven Tattersall. Un relevé compte en `hardware` s'il nomme sa machine et son
   protocole.
2. **Les documents primaires** — ci-dessus, à lire avant le RTL.
3. **Les ROM** — TOS et diagnostics exécutés sous NeoST.
4. **Le RTL simulé** — `gstmcu/tb` fournit un banc Verilator ; fx68k a le sien depuis le
   2026-11-03 (`tools/run_fx68k_bench.py`, [`FX68K_BENCH.md`](FX68K_BENCH.md)) ; `stBlitter.sv` depuis le
   2026-11-04 (`tools/run_stblitter_bench.py`, [`STBLITTER_BENCH.md`](STBLITTER_BENCH.md)).

Ce qui exige vraiment une machine reste inscrit comme tel au `TODO.md`, statut « ⏸ matériel ».
