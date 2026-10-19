# Mode borne (kiosk)

Plein écran **sans chrome de bureau** (ni barre de menus, ni fenêtres ImGui),
configuration **figée** — la borne repart toujours identique — mais avec un **menu
in-game** manette/clavier pour changer de jeu, envoyer des touches ou redémarrer sans
jamais quitter le plein écran.

```sh
./build/neost --kiosk roms/tos102uk.img "disks/Jeu.stx"     # plein écran EXCLUSIF
./build/neost --kiosk --kiosk-monitor 1 roms/tos162uk.img   # sur le 2ᵉ écran
```

**Ctrl+Alt+F** fait l'aller-retour bureau ⇄ borne à chaud : la machine traverse la bascule par
instantané, le jeu en cours continue.

| Option | Effet |
|--------|-------|
| `--kiosk` | Vrai plein écran **exclusif** : reste au-dessus de tout (panneaux/dock inclus), garde le focus clavier |
| `--kiosk-monitor N` | Écran cible (0 = principal) |
| `--audio-latency MS` | Coussin audio visé (défaut 85, borné `[20, 250]`, persisté en `audio_latency_ms=`) |

**Latence audio** : la monter à 120-150 sur une machine juste. Un underrun coûte un
**trou audible**, une latence un peu plus haute non. Le diagnostic est dans la sortie
d'erreur : `[Audio] ring underrun … emulation loop: XX real frames/s`.

En borne : la souris est capturée et le curseur masqué ; l'**émulation joystick au
clavier est activée** (flèches + Ctrl droit = feu) pour jouer sans manette. La
**sensibilité de la souris émulée** (`mousespeed=`, page **Input** du GUI) s'applique
aussi en borne — mais la borne ne réécrit pas ce réglage : on le pose depuis le bureau
(ou à la main dans `neost.cfg`) avant de basculer. `neost.cfg`
n'est réécrit que pour les deux réglages que la borne doit mémoriser et qui se règlent
depuis son menu — dossiers ROM additionnels et affectation des manettes. Jamais pour la
ROM, la disquette ou le modèle de machine.

## Zoom adaptatif (défaut, bascule F10)

La **zone active** (l'écran ST « de base », hors bandes overscan) est calée sur la
hauteur en gardant le ratio pixel : un jeu normal remplit l'écran autant qu'une démo,
sans être rapetissé par des bordures unies qu'il n'exploite pas (barres noires
latérales seulement). Dès qu'une démo **ouvre les bordures** (overscan — Enchanted
Land, Lethal Xcess…), la Glue le signale et NeoST montre le **buffer entier**
(hystérésis anti-clignotement). `F10` fige au contraire le cadre complet en permanence.

## Menu in-game (START manette, ou F9)

Plein écran, **jeu en pause**. Libellés en anglais. Deux colonnes qu'on bascule
gauche/droite :

- la **liste des jeux**, triée par proximité — les phases B/C/D du jeu en cours
  remontent en tête ; **L1/R1** défile par page ;
- les **actions** : *Restart machine* · *Keyboard & mouse* · *Joysticks* ·
  *ROM folders* · *Desktop mode* · *Quit*.

Le FEU (A / Entrée) valide. Insérer un jeu **échange la disquette à chaud, sans
reboot** — exactement comme glisser une disquette dans le lecteur : le jeu en cours
continue. Seul *Restart machine* relance.

## Clavier & souris (SELECT manette, ou F12)

Un bandeau en bas **sans mettre le jeu en pause** : on envoie une frappe brève (F1-F8,
chiffres, Espace/Return/Escape, clics souris) au jeu qui tourne dessous. Indispensable
pour les jeux qui réclament une touche que la manette n'a pas.

## Joysticks

Choisir **quelle manette hôte pilote quel port ST**. Une ligne par manette détectée —
bouger un stick allume une pastille ● pour identifier laquelle est laquelle. Le FEU fait
tourner son rôle :

**AUTO** (1ʳᵉ manette → port 1 « jeux » ; la 2ᵉ ne va au port 0 que si `port0=auto`,
sinon le port 0 reste à la souris) → **PORT 1** → **PORT 0** → **OFF** → AUTO.

Plusieurs manettes sur le même port sont OR-ées (deux sticks pilotent le même joueur).
Le choix est **persisté par GUID** dans `neost.cfg` (`joymap=`) : il survit au
rebranchement et au reboot. Dans le GUI, la page **Input** montre la même chose
**par port** : « Port 0 (mouse port) » = souris / auto (2ᵉ manette) / clavier / telle
manette ; « Port 1 » = auto (1ʳᵉ manette) / clavier / telle manette. Un joystick
branché sur le port 0 **débranche la souris hôte** (comme sur un vrai ST) — c'est le
réglage « jouer à deux ».

Boutons en jeu : **A/B et gâchettes = FEU**, **X = ESPACE**, **Y = RETURN** — les jeux
« press SPACE to start » (Enchanted Land…) se jouent entièrement à la manette.

⚠ Les **boutons** de n'importe quelle manette naviguent dans le menu, y compris une
manette mise sur OFF ; le filtre par rôle ne s'applique qu'aux **axes**. Sans cette
asymétrie, un opérateur qui coupe sa seule manette depuis cette page — dont c'est
précisément la fonction — perdrait tout contrôle, et le réglage étant persisté la borne
redémarrerait verrouillée.

## Dossiers ROM

Ajouter d'autres dossiers de jeux/disques, scannés **en plus** de `disks/`. Un
navigateur piloté à la manette parcourt le système de fichiers (chemin absolu →
remontée jusqu'à `/`) avec des raccourcis vers la racine, le *Home* et les **volumes
montés** (`/Volumes` sur macOS, `/run/media` · `/media` · `/mnt` sur Linux).
*USE THIS FOLDER* ajoute ; chaque dossier se retire d'une **croix ×**, et disparaît
automatiquement de la liste s'il n'existe plus. Persisté dans `neost.cfg`
(`kiosk_romdir=`).

⚠ La racine `/` et le dossier personnel sont **refusés** : `kioskScanDisks` les
parcourrait récursivement dans le thread GUI (mesuré : 2,7 M d'entrées pour `/home`),
et la borne se figerait plusieurs minutes à chaque ouverture du menu.

## Raccourcis

| Touche / bouton | Action |
|-----------------|--------|
| Flèches / Ctrl droit | Joystick émulé (direction / feu) |
| **F9** / START | Ouvre/ferme le menu in-game (jeu en pause) |
| **F12** / SELECT | Ouvre/ferme le bandeau Clavier & souris |
| **L1 / R1** (Page↑/↓) | Défilement rapide par page dans la liste des jeux |
| **F5** / **F7** | Sauver / charger l'état (slot `neost.state`) |
| **Alt+Entrée** | Ferme le menu borne et reprend l'Atari ; hors menu, bascule plein écran ⇄ fenêtre |
| **Ctrl+Alt+F** | Bascule borne ⇄ bureau |
| **F10** | (dés)active le zoom adaptatif |
| **F11** | (dés)active l'émulation joystick clavier |
| A·B / Entrée·Échap | Valider / revenir |
| **Alt+F4** | Quitter la borne (immédiat) |
| **Ctrl+Shift+Q** (~0,7 s) | Quitter la borne (chord discret) |

## Démarrage direct sur un jeu (attract)

Monter un dossier GEMDOS (`NEOST_GEMDOS_DIR=…`) dont le `DESKTOP.INF` (TOS 1.x) ou
`NEWDESK.INF` (TOS 2.x) contient une ligne d'autostart :

```
#Z 01 C:\JEU.TOS@
```

## Borne Raspberry Pi

Démarrage direct sur l'émulateur (Pi OS Lite, X nu sans compositeur, build natif
`-mcpu`) : voir [`../packaging/raspberry/README.md`](../packaging/raspberry/README.md).

Le paquet de release `pi400-aarch64` est compilé `-mcpu=cortex-a72` pour Pi 4 / Pi 400 ;
`raspberry-aarch64` est générique et couvre Pi 3 → Pi 5.
