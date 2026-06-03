# Automatiser Hatari (oracle de référence)

Hatari est la **source de vérité matérielle** de NeoST (cf. `CLAUDE.md`). Au-delà de la
lecture des sources (`extern/hatari/src`), on peut **exécuter** Hatari de façon
déterministe et **sans affichage** pour comparer son comportement à NeoST (boot, écran,
détection HW, IRQ). Ce doc note la recette vérifiée (Hatari v2.6.1, macOS Silicon, juin 2026).

Binaire : `/opt/homebrew/bin/hatari` (Homebrew). ⚠ Sous macOS **pas de `timeout`** — on
s'appuie sur `--run-vbls` qui fait sortir Hatari tout seul.

## Recette headless : boot → image PNG

```sh
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy   # aucune fenêtre / audio (CI, headless)
hatari --machine megaste --tos rom/etos256us.img --monitor rgb \
       --sound off --fast-forward on --confirm-quit off --statusbar off \
       --alert-level fatal \
       --run-vbls 400 \
       --avirecord --avi-vcodec png --avi-file /tmp/h.avi
# Extraire une frame (ffmpeg dispo via Homebrew). N = n° de frame ; ~60 fps dans l'AVI.
ffmpeg -y -i /tmp/h.avi -vf "select=eq(n\,300)" -frames:v 1 -update 1 /tmp/h.png
```

- **`--avirecord` capture l'écran ÉMULÉ** (pas la fenêtre hôte) → marche avec
  `SDL_VIDEODRIVER=dummy`. C'est la clé du headless : pas besoin d'un display ni d'envoyer
  la touche screenshot. `--avi-vcodec png` = frames PNG sans perte dans un conteneur AVI.
- Choisir une frame **du milieu** (pas la dernière : les toutes dernières frames peuvent
  être noires/transition de sortie). L'AVI fait la taille de l'overscan (ex. 832×552) avec
  de larges bordures noires ; le contenu utile est au centre.
- `--alert-level fatal` : **indispensable** — sinon Hatari ouvre des **dialogues GUI
  bloquants** (ex. l'avertissement TOS≤1.4, cf. piège ci-dessous) qui figent l'exécution.
- `--run-vbls N` : exécute N VBL (≈ N/50 s de temps ST en PAL) puis quitte proprement.
  `--fast-forward on` accélère (ne change pas le nombre de VBL émulées).

## Autres signaux (sans image)

- `--conout 2` : redirige la **console EmuTOS/VT-52** vers stdout — utile pour suivre le
  boot (messages, panics) sans image.
- `--trace <flags>` (`--trace help` pour la liste) + `--trace-file FILE` : trace CPU /
  IRQ / vidéo… façon MAME. Comparable aux traces NeoST headless (`trace_diff.py`).
- `--parse FILE` : exécute des commandes du **débogueur** intégré (points d'arrêt, dump
  mémoire/registres après N cycles) → introspection scriptée.
- `--log-file FILE`, `--log-level info|warn|...`.

## Options machine utiles

| Option | Effet |
|--------|-------|
| `--machine st\|megast\|ste\|megaste\|tt\|falcon` | profil matériel |
| `--tos <file>` | image TOS/EmuTOS |
| `--cpulevel <0..>` | type 680x0 (EmuTOS/TOS 2.06 seulement) |
| `--monitor mono\|rgb\|vga\|tv` | type moniteur (mono = haute rés) |
| `--country <x>` | code pays pour EmuTOS multi-langue |
| `--fast-boot on` | patche TOS/memvalid pour booter plus vite |

## Pièges vérifiés

- **TOS ≤ 1.4 → forcé en mode ST.** Hatari lit la version dans l'en-tête TOS ; un EmuTOS
  **192 Ko** (`etos192*.img`) se présente en **« TOS 1.4 / Atari ST »** et Hatari **refuse**
  de le lancer en MegaSTE/TT (« TOS versions <= 1.4 work only in ST mode », bascule auto en
  ST). Pour MegaSTE il faut un **EmuTOS 256 Ko** (`etos256us/fr.img`, qui se présente
  « Atari Mega STe ») ou un TOS 2.05/2.06. C'est ainsi qu'on a tranché la question du SCU
  (cf. `CHANGELOG` : EmuTOS 256K **programme** le SCU comme TOS 2.06).
- **`--avirecord` exige `--avi-file`** ; sans `--avi-vcodec png` le défaut peut être un
  codec moins pratique à décoder.
- Au **premier lancement** Hatari crée `~/Library/Application Support/Hatari/` (config,
  NVRAM). Un `INFO : NVRAM not found` au boot est normal.

## Récupérer un EmuTOS récent (libre, GPL)

```sh
curl -sL -o /tmp/e.zip "https://downloads.sourceforge.net/project/emutos/emutos/1.4/emutos-256k-1.4.zip"
unzip -o /tmp/e.zip -d /tmp/e && cp /tmp/e/emutos-256k-1.4/etos256us.img rom/
```
Le paquet `256k` contient toutes les langues (`etos256us.img`, `etos256fr.img`, …). Le
paquet `192k` est pour ST/STE (TOS 1.x), le `256k` pour Mega ST/STE/TT/Falcon (TOS 2.x/3.x).
