# Mesurer l'IACK sur un MegaSTE

État du 2026-10-19 : programme et protocole contrôlés dans NeoST ; **aucune
capture matérielle n'a encore été faite**. Le programme ne choisit aucune
valeur « correcte » à partir d'Hatari.

## Préparer la sonde

```sh
python3 tools/make_mfp_iack_probe.py
python3 tools/run_mfp_iack_probe.py
```

Sorties dans `tests/out/mfp_iack_probe/` :

- `IACKTEST.PRG` : exécutable TOS relogeable, à lancer sur MegaSTE ;
- `IACKTEST.ST` : disquette FAT12 avec `AUTO/IACKTEST.PRG` ;
- `iack-control.bin` : cartouche de contrôle pour les émulateurs ;
- `manifest.json` : SHA-256 des médias, paramètres, offsets du PRG et adresses
  de la cartouche ; `checks.json` : contrôles d'exécution, sans verdict matériel.

Le PRG vérifie `_MCH=$00010010` avant tout accès MegaSTE. Sur ST/STE/TT/Falcon,
ou sans cookie de machine, il revient au TOS en indiquant le refus. Sur
MegaSTE, il prend la main, masque les autres sources, remplace le vecteur
Timer A et utilise sa propre pile. **Reset pour quitter**, même à la fin.
Il ne restaure pas les services TOS et ne touche à aucun fichier invité.

## Ce qui est exécuté

Trois phases successives, même code et même pile en RAM :

| Phase | `$FF8E21` | Palette 0 au début des marqueurs |
|---|---:|---:|
| CPU 8 MHz, cache désactivé | 0 | `$0700` (rouge) |
| CPU 16 MHz, cache désactivé | 2 | `$0070` (vert) |
| CPU 16 MHz, cache activé | 3 | `$0007` (bleu) |

Timer A utilise le prédiviseur /4 et un compteur de 192. Son horloge XTAL
est indépendante de la fréquence CPU. Chaque phase sert 72 interruptions,
dont les huit premières sont à exclure des mesures de régime établi.
`--samples N` permet d'allonger les captures (9 à 32767 interruptions par phase).

Le CPU attend en `STOP #$2500`. La première instruction du handler est
`MOVE.W D6,$FFFF8240.W`, puis la couleur est alternée par XOR `$0111`, le
compteur d'IRQ est incrémenté et `RTE` reprend l'attente. Palette blanche
fixe en fin de programme. La couleur est un repère pratique sur moniteur
couleur ; le décodage de l'écriture sur le bus reste le repère de mesure.

Le programme conserve trois compteurs d'IRQ servis, puis `$BEEF`. Le runner
exige exactement un enregistrement `72,72,72,$BEEF`, sans halt. Il vérifie
aussi un témoin écrit par le chemin de refus sur ST et STE : un programme
jamais exécuté ne doit pas produire un faux succès. La cartouche et le
chargement AUTO du PRG sont contrôlés séparément.

## Signaux à enregistrer

Conserver la référence de carte, les marquages GSTMCU/MFP/PAL, le TOS, le
moniteur, les empreintes des médias et la fréquence d'échantillonnage.
Utiliser le schéma correspondant à la révision de carte et le brochage du
boîtier MFP présent ; ne pas confondre DPIP48 et PLCC52.

Pour isoler le bloc périphérique, capturer ensemble : CLK CPU, **CLK MFP**,
INTR MFP, IACK MFP, DTACK MFP et le DTACK reçu par le CPU. CLK du MFP et
XTAL des timers sont deux entrées distinctes. Capturer aussi AS et les
strobes ; si le bus d'adresses/données est disponible, identifier le
vecteur `$4D`, les accès de pile, le premier fetch du handler et l'écriture
de `$FF8240`. Le DTACK de la puce et celui reçu par le CPU permettent de
séparer la réponse du MFP de la synchronisation par les PAL.

Mesurer séparément, avec les phases des deux CLK :

1. INTR → début IACK : propagation, réveil STOP et début de l'exception ;
2. IACK → DTACK MFP → DTACK CPU : périphérique et synchronisation de carte ;
3. DTACK CPU → premier fetch du handler → écriture palette : fin de
   l'exception, créneaux mémoire et première instruction.

Conserver les captures brutes et les 64 échantillons retenus par phase,
en secondes et en périodes CLK réellement observées. Une couleur changée
ou un compteur juste ne prouve aucune latence. Les huit échauffements
exclus servent notamment à ne pas mélanger cache froid et régime établi.

## Confronter sans forcer l'égalité

Les figures 23–24 de la [fiche SGS-Thomson MK68901 de décembre 1988](https://www.tvsat.com.pl/pdf/m/mk68901_stm.pdf)
décrivent IACK/IEI et DTACK par rapport à **CLK du MFP**. Le manuel de
service MegaSTE, §2.2.8.3, décrit la synchronisation des signaux DTACK et
BERR par les PAL. Ces sources justifient de séparer les deux horloges et
les deux réponses DTACK ; elles ne fournissent pas à elles seules une
latence globale mesurée de la carte.

L'[audit logiciel](HATARI_MEGASTE_AUDIT.md) conserve les hypothèses testées.
La conversion expérimentale de l'attente IACK d'Hatari en cycles bus
réduit le résidu à 3/4 cycles à 16 MHz, mais ne prouve ni sa constante 12,
ni la calibration 16 de NeoST. Seule la capture ci-dessus permettra de
choisir ou de remplacer cette calibration.
