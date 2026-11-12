# Extensions NeoST — matériel d'époque que l'ST n'avait pas d'origine

> **Statut commun.** Toutes **inactives par défaut**, sans effet sur les étalons (aucune
> n'ouvre de socket depuis `tools/run_all.py`), et consignées comme divergences délibérées
> dans `docs/HATARI_DIVERGENCES.md` § 16, Extensions — Hatari n'a aucun équivalent. Le cœur
> (`neost_core`) ne fait que *signaler* ; l'I/O réelle vit dans `neost_net` (frontends).
>
> Toutes reproduisent du matériel que les Atariens ont réellement branché :
>
> | Extension | Nature | Réalité matérielle |
> |-----------|--------|--------------------|
> | [UltraSatan](#ultrasatan--interface-sdmmc-sur-le-bus-acsi---ultrasatan-gui-hard-disks) | stockage SD sur ACSI | **matériel réel** (Jookie) |
> | [NetUSBee](#netusbee--ne2000--hôte-usb-isp1160-sur-le-port-cartouche---netusbee-gui-network) | Ethernet + USB, port cartouche | **matériel réel** |
> | [EtherNEC](#ethernec--ne2000-sur-le-port-cartouche---ethernec-gui-network) | NE2000, port cartouche | **matériel réel** (montage T. Redelberger) |
> | [Modem Hayes](#modem-hayes-sur-rs-232---modem-gui-network) | pont RS-232 → TCP | équivalent des modems WiFi ESP8266 vendus pour ST |
> | [Appareils MIDI hôtes](#appareils-midi-de-lhôte--le-câble-din-du-st-vers-du-vrai-matériel) | expandeur/clavier branché sur la machine | **c'est le câble DIN du ST** |
> | [Anneau MIDI](#anneau-midi-réseau--midimaze-en-ligne---midi-net-hpl) | MIDIMaze sur UDP | transpose un câblage MIDI réel |
> | [Clé Steinberg](#clé-steinberg--dongle-cubase-sur-le-port-cartouche---dongle-gui-dongles) | PAL16R8 / EPLD sur /ROM3 | **matériel réel** (clés noire et rouge de Cubase) |
> | [Périphériques des ports](#périphériques-des-ports--un-par-port--clés-joystick--série-dac-pro-sound-boutons---plug-gui-dongles) | clés joystick/RS-232, DAC parallèle, boutons Multiface/URC | **matériel réel** (11 adaptateurs, inventaire Steem SSE) |

# Les extensions, une par une

Philosophie commune : backend hôte hors du cœur, OFF par défaut, `NEOST_WITH_NET` pour
celles qui ouvrent des sockets. **UltraSatan** pour le stockage, **NetUSBee**/**EtherNEC**
pour le réseau, **modem Hayes** et **anneau MIDI** pour le reste.

## Modem Hayes sur RS-232 (`--modem`, GUI Network)

Un modem émulé sur l'USART MFP : les commandes `AT` ouvrent de vraies connexions TCP
(`ATDT hôte:port` → `CONNECT`, pont transparent octets ↔ socket ; `+++`/`ATH` pour
raccrocher ; `DCD` suit la porteuse). C'est le grand débloqueur du logiciel d'époque —
terminaux, BBS, et les piles TCP/IP historiques **STiK/STinG en SLIP/PPP**. Repose sur
`Mfp::receiveByte` (injection RX **cadencée** au débit série via `Scheduler::SERIAL_RX`,
IRQ RxFull par octet — un pilote qui compte sur le rythme du fil ne perd aucun octet).
Vérifié : `MODMTEST.TOS` ↔ serveur TCP local (`CONNECT 9600`, bannière reçue), et
**bout-en-bout sur Internet** (2026-08-22) — `tools/make_net_test.py` génère une disquette
dont l'`AUTO\NETTEST.PRG` compose `ATDT theoldnet.com:80`, envoie une requête HTTP à la
main et affiche la réponse : en-têtes (`Content-Length`, `X-Powered-By: Express`) puis le
HTML. C'est le chemin qui marche **sans pile TCP/IP côté ST** (le modem est un pont
transparent octets ↔ socket) ; interactivement, `UNITERM.PRG` fait la même chose.

**Numérotation asynchrone** (2026-11-11) : résolution DNS et connexion (délai 5 s) tournent
sur un fil hôte ; `CONNECT` ou `NO CARRIER` arrive à la trame qui suit l'issue, sans geler
le fil d'émulation ni le GUI. Tout caractère reçu pendant la numérotation l'abandonne
(`NO CARRIER`, ITU-T V.250), sauf le LF d'une ligne terminée en CRLF : un `ATH` ou un `+++`
annulent donc par leur premier octet. **DTR** (PSG port A bit 4, actif bas) est suivi par
`AT&Dn` sur un **front** active → inactive : `&D0` ignore, `&D1` repasse en mode commande,
`&D2` (défaut) raccroche, `&D3` réinitialise. Le **reset du ST** remet le port A du PSG au
repos : DTR retombe et un appel en cours est raccroché sous `&D2`, comme sur le fer — le
modem lui-même n'a pas de ligne de reset. Un logiciel qui ne touche jamais à DTR garde un
modem utilisable. **Échappement `+++` avec temps de garde** (2026-11-12) : silence, trois
`+`, silence, chacun de 50 trames émulées (S12 par défaut, 1 s à 50 Hz, ~0,83 s à 60 Hz) ;
`OK` arrive à la fin du silence final. Avant, trois `+` consécutifs suffisaient : tout
`+++` d'un transfert binaire (ZMODEM, archive) avalait ses trois octets et passait en mode
commande au milieu du flux. Les `+` d'un échappement avorté repartent comme données, dans
l'ordre. Les lignes `AT` enchaînent plusieurs commandes (`AT&FE0&D2`) ; `Sn?`
répond `000`. Grades : [E] pour le jeu Hayes et V.250 (aucun texte dans
`extern/datasheets`), [F] pour le défaut `&D2`. Contrôle local : `tests/hayes_modem_logic.cpp`
(cible `neost-modem-test`, palier `fast`, écouteur TCP sur 127.0.0.1).

```sh
python3 tools/make_net_test.py /tmp/nettest.st theoldnet.com /
./build/neost-headless roms/etos192us.img --machine megast --mem 1m --mono \
    --modem --disk /tmp/nettest.st --frames 2500 --screenshot out.ppm
```

Deux pièges appris en l'écrivant, consignés dans le générateur : le compteur 200 Hz du TOS
(`$4BA`) vit en mémoire BASSE, **protégée** — y toucher depuis un PRG (mode utilisateur)
lève un bus error, d'où `Super(0)` ; et la console ST **n'enroule pas** les lignes longues,
elle écrase la 80ᵉ colonne (une ligne HTML n'y laissait voir que son dernier caractère,
une colonne de « > »), d'où le repli à 78 colonnes et le CR ajouté sur les LF seuls.
`src/net/HayesModem.cpp`.

Le NetUSBee/EtherNEC utilise le **NAT intégré de NeoST** (`extern/nat`), issu de
POM2 ; dernière reprise des correctifs amont : `4625ec82547118918bfafc795f6b7128cdb616ae`.
`NatBackend` (`src/net/NatBackend.cpp`) adapte les trames à `NetBackend` ;
aucune dépendance libslirp/GLib. Le NAT est construit avec `NEOST_WITH_NET`
(par défaut sur bureau, coupé sur Android et WASM). La case **Real Internet
for the NE2000** de la page **Network**, ou `--nat`, branche le câble à chaud.
Le pilote ST garde la même carte : ST `10.0.2.15/24`, passerelle `10.0.2.2`,
DNS `10.0.2.3`. `--slirp`, `--slirp-restricted` et `--slirp-selftest` restent
des alias ; les anciens profils `slirp=1` sont relus, les nouveaux écrivent `nat=1`.

Le backend fournit ARP, DHCP, ping des services virtuels, relais TCP/UDP,
fragmentation/réassemblage IPv4/IPv6 bornés, sondes TCP de fenêtre nulle et DNS
A/AAAA/MX/TXT/PTR/SRV. IPv6 : préfixe `fd00:2::/64`, routeur `fe80::2`,
passerelle `fd00:2::2`, DNS `fd00:2::3` (ND et SLAAC). Les DNS A passent par
le résolveur hôte ; les autres types par les API DNS natives, sur des tâches
bornées hors du thread d'émulation. Les réponses A sont gardées en cache 60 s (le TTL
annoncé à l'invité), les échecs 30 s ; la mise sous tension de la carte vide ce cache
(2026-11-12 — une adresse résolue ne périmait jamais). Le réseau réel reste hors déterminisme
et hors save-state. Aucun autre ICMP n'est relayé sur Internet.

Les options `--nat-restricted` et `--nat-allow-loopback` correspondent à
`nat_restricted=` et `nat_allow_loopback=`. Le mode restreint conserve les
services virtuels mais refuse les relais et les publications. L'accès aux
services loopback de l'hôte est désactivé par défaut.

**Liste blanche de domaines** (2026-11-12, OFF par défaut) : `--net-allow-domains L`,
clé `net_allow_domains=` (rejouée par `--from-cfg`), champ **Allowed domains** de la page
Network. Elle vaut pour les deux backends sortants : le NAT et le modem Hayes.
`L` liste des domaines ou des adresses IPv4, séparés par virgules, points-virgules ou
blancs (64 au plus) ; `example.org` couvre `example.org` et `www.example.org`, pas
`badexample.org` ; `*.example.org` s'écrit aussi. Une liste invalide fait échouer
l'ouverture du NAT (et `neost-headless` sort en erreur), sans liste partielle.

- **NAT** : le DNS virtuel répond `REFUSED` (RCODE 5, [B] RFC 1035 § 4.1.1) à tout nom
  hors liste, en A comme en natif, sans interroger l'hôte. Le trafic TCP/UDP que l'invité
  engage ne sort que vers une adresse que ce DNS a rendue pour un nom de la liste, depuis
  la dernière mise sous tension de la carte, ou vers une adresse IPv4 listée. Une adresse
  écrite en dur par l'invité est refusée : RST en TCP, datagramme jeté en UDP. Les services
  virtuels, les publications de ports (trafic entrant) et le loopback de l'hôte, qui a son
  propre interrupteur (`nat_allow_loopback`), ne sont pas filtrés.
- **Modem** : `ATD` vers un hôte hors liste répond `NO CARRIER` sans résolution ni
  connexion.
- **Journal** (stderr, en anglais) : `DNS query for "x" refused — not in the domain
  allowlist` pour les 16 premiers noms distincts, `guest TCP to a.b.c.d:p refused — not an
  address of a domain in the allowlist` une fois par session, `modem dial to x refused` à
  chaque numérotation. Le nom du backend annonce `domain allowlist of N`.
- **Limite** : une adresse partagée (CDN, hébergement mutualisé) ouvre tous les sites
  qu'elle sert ; NeoST ne lit ni l'en-tête HTTP `Host` ni le SNI TLS. La liste filtre des
  noms et des adresses, pas des contenus.

Code : `extern/nat/NatAllowlist.*` (adaptation locale, cf. `extern/nat/README.md`).
Tests : `neost-nat-test` cas [15] (analyse, appariement, DNS refusé, adresse apprise,
adresse en dur refusée en UDP et TCP, oubli au reset de la carte, loopback),
`neost-modem-test` cas 6.

Publication explicite de services, également réglable dans la page Network :

```sh
./build/neost-headless roms/etos256us.img --ethernec --nat \
  --nat-forwards 'tcp:0.0.0.0:6502:23;udp:0.0.0.0:6503:6502' --frames 1500
```

Format : `tcp|udp:adresse-hôte:port-hôte:port-ST`, règles séparées par `;`,
32 maximum, avec adresse de publication IPv4. `127.0.0.1` réserve l'accès à
l'hôte ; `0.0.0.0` publie sur le LAN.
La clé `nat_forwards=` est rejouée par `--from-cfg`, avec les autres options NAT.
Une liste invalide ou un port indisponible fait échouer l'ouverture, sans liste
partiellement appliquée. Deux ST peuvent garder `10.0.2.15` : chacun contacte
l'adresse LAN de l'autre ordinateur et son **port hôte**. Ce n'est pas un pont
Ethernet : les broadcasts et protocoles non-IP ne traversent pas les NAT.

`--nat-selftest` pilote la NE2000 au niveau fil : ARP, OFFER DHCP, compteurs
et aller-retour UDP sur une socket loopback locale. Les tests `neost-nat-test`,
`neost-resolver-test` et `neost-nat-adapter-test` couvrent aussi le protocole,
les fragments, IPv6, les redirections entre deux instances et les erreurs de bind.
Ils sont lancés par les paliers `fast`/`full`, sans dépendance à Internet.
`tools/run_nat_sting.py` fait aussi résoudre `localhost` à STinG/ENEC, puis
exécute un GET HTTP vers un serveur local éphémère via `10.0.2.2` : la réponse
est vérifiée sur RS-232 et la requête côté serveur. Le domaine de recherche
STinG est vidé dans une copie temporaire du disque pour éviter un nom externe.
`NEOST_SLIRP_ONLINE=1` garde son nom historique : requête DNS réelle opt-in ;
`NEOST_SLIRP_DNS=a.b.c.d[:port]` choisit sa cible. Ces contrôles ne qualifient
pas toutes les piles Atari ni un LAN entre deux ordinateurs physiques.

Côté ST, le banc bout-en-bout est **`tools/make_sting_test.py`** : il génère une
disquette de boot STinG 1.26 + `ENEC.STX` (URLs des archives freeware/GPL dans son
en-tête) dont l'`AUTO` embarque un PRG maison qui configure le port « EtherNet » par
l'API STinG (cookie `STiK` — le rôle que joue `STNGPORT.CPX` en GEM), résout un nom et
fait un GET HTTP, verdict recopié sur RS-232 :

```sh
python3 tools/make_sting_test.py /tmp/sting.st <dossier sting126> <dossier ENEC.STX>
./build/neost-headless roms/etos256us.img --ethernec --nat --disk /tmp/sting.st \
    --frames 3500 --serial-dump /tmp/ser.txt     # attendu : DNS=a.b.c.d puis HTTP/1.x
```

Sans réseau, le 2026-11-04, le pilote charge et le port s'active sous TOS 1.02 et 1.04 sur ST, TOS 1.62 sur STE, TOS 2.06 sur Mega STE et EmuTOS. TOS 2.06 attend au démarrage, sur plus de 4000 trames : SPACE passe cette attente, et les étapes sortent en moins de 2500 trames.

Rejoué le 2026-11-04 avec `disks/sting_ethernec.st` : `DNS=138.197.157.224`, puis `HTTP/1.1 200 OK` depuis theoldnet.com, sur 4 runs de suite. Le premier run du jour avait échoué en DNS (`err -17`), sans que la cause soit identifiée ni l'échec reproduit.

**Trace du fil** (2026-11-12) : `NEOST_SLIRP_TRACE=1` journalise sur stderr, au passage
entre la NE2000 et le NAT, chaque requête DNS de l'invité (`id`, nom, type), sa réponse
(RCODE, nombre de réponses, délai en ms) et les SYN/RST TCP. Avant, la variable ne traçait
que la trame de `--nat-selftest`, rien du trafic d'un vrai STinG. Le même jour, quatre
premiers runs (un processus neuf chacun, `--frames 3500`) : DNS bon les quatre fois, 581 ms
au premier (cache de l'hôte froid), puis 12, 2 et 2 ms ; l'échec `err -17` n'est pas revenu.
STinG rend `err -17` pour toute réponse inutilisable : un `REFUSED` de la liste blanche
donne exactement ce code. Un prochain `err -17` se lira donc à la trace : RCODE 2 (SERVFAIL
à l'échéance de 8 s ou faute de jeton), 3 (NXDOMAIN de l'hôte), ou aucune réponse. Ces
runs ont aussi montré que la suite TCP dépend de l'hôte : quatre fois, le SYN vers
`138.197.157.224:80` n'a reçu aucun SYN-ACK (le `connect` du binaire fraîchement compilé
n'aboutissait pas — filtre applicatif de l'hôte, sans preuve), puis il a abouti au cinquième.
Et theoldnet.com répond désormais `403 Forbidden` à une requête sans `User-Agent` (celle de
`STGTEST.PRG` ; `curl -H "User-Agent:"` aussi), là où il rendait `200 OK` le 2026-11-04. C'est ce banc qui a démasqué l'inversion des fenêtres ROM3/ROM4 (cf. EtherNEC ci-dessous). Restent le
navigateur (CAB) en GUI et la consignation dans `docs/CASE_STUDIES.md` — cf. `TODO.md`.

**Livré dans les paquets (2026-11-11)** : `disks/sting_ethernec.st`, la disquette de boot STinG 1.26
(freeware, P. Rottengatter et R. Andersson) + `ENEC.STX` (GPL-2, T. Redelberger), avec ses
licences dans `licenses/THIRD-PARTY.txt`. Brancher l'EtherNEC et le NAT (page **Network**), insérer
ce disque et redémarrer : STinG se charge en AUTO, `STGTEST.PRG` configure le port et prouve la pile
(DNS puis GET HTTP, affichés à l'écran et recopiés sur RS-232).

## EtherNEC — NE2000 sur le port cartouche (`--ethernec`, GUI Network)

Émulation de la carte réseau NE2000 vue par le port ROM (montage
[EtherNEC](https://github.com/EmmanuelKasper/ethernec) de Dr. Thomas Redelberger), pour
faire tourner les pilotes libres **STinG (`.STX`), MiNTnet (`.XIF`) et MagiCNet SANS
modification**. Le port cartouche étant en lecture seule et sans ligne A0, tout est
encodé dans l'adresse :

* **lire** le registre `n` : lecture à `$FA0000 + n*512` (/ROM4) ;
* **écrire** `d` dans `n` : *fausse lecture* à `$FB0000 + n*512 + d*2` (/ROM3).

⚠ Ces deux fenêtres ont été **inversées** dans NeoST jusqu'au 2026-08-27 : les selftests
passaient (mêmes constantes des deux côtés du test), mais le vrai `ENEC.STX` ne pouvait
ni lire ni écrire la carte — découvert à la première session STinG réelle, tranché sur
`BUSENEC.I` du pilote de l'auteur du montage (`getBUS` lit via `rom4=$FA0000`, `putBUS`
écrit via `rom3=$FB0000`).

Décodé dans `Bus::read8Slow` (`src/io/Ne2000.cpp`). Modèle DP8390 classique (pages 0/1,
anneau de réception avec en-tête 4 octets, Remote DMA, filtrage MAC/broadcast). Backend
physique = interface `NetBackend` (boucle locale, ou NAT intégré avec `--nat`e ; pcap = point d'extension). **Exclusif d'une cartouche montée** (conflit de
fenêtre $FA0000 — le montage est refusé avec message). Auto-test : `--enec-selftest`
(palier `fast`, qui vérifie aussi la PROM et sa lecture seule).

**Fidélité à la fiche DP8390D** (2026-11-12, [B] National, juillet 1995, § 10) :

* **Un accès mot est un seul cycle /ROM.** L'effet de bord (écriture registre, avance du
  Remote DMA, `TXP`) joue une fois ; la carte ne pilote que D8-D15, l'octet impair rend
  `$FF`. Avant, chaque octet rejouait l'accès : un `move.w` au port données consommait
  deux octets, un `move.w` de `CR=$26` émettait la trame deux fois.
* **Lectures de la page 0** : `TSR` (PTX après émission), `NCR` = 0, `RSR` (PRX, PHY pour
  broadcast/multicast, MPA, DIS en mode moniteur), compteurs `CNTR0-2` effacés à la
  lecture. Ces adresses rendaient `TBCR0` et `RCR/TCR/DCR/IMR`.
* **Page 2** : elle relit `PSTART/PSTOP/TPSR/RCR/TCR/DCR/IMR` ; ses écritures sont
  ignorées. Elles tombaient dans la page 0 (`CR=$A2` puis registre 3 écrivait `BNRY`).
* **Réception** : rien n'entre carte stoppée (`STP`, état de mise sous tension) ; `TXP`
  sous `STP` n'émet rien. `RCR` est honoré : broadcast si `AB`, multicast si `AM` et que le
  hachage CRC désigne un bit posé des `MAR`, adresse physique si `PRO` ou égale aux `PAR` ;
  mode moniteur (`MON`) : rien n'est rangé, `CNTR2` compte. Un pilote doit donc démarrer
  la carte et poser `AB` pour recevoir ARP et DHCP, comme sur le fer.

**La puce est une RTL8019AS** (2026-11-12, [B] Realtek, spécification LS003.8 du
2000-08-20, rapatriée par `tools/setup_silicon_refs.sh`) — celle du NetUSBee et des cartes
NE2000 les plus courantes sur l'EtherNEC. Là où elle s'écarte de la DP8390D, elle l'emporte :

* **Identifiants** : `$0A/$0B` de la page 0 se lisent `$50`/`$70` (« P », « p »).
* **`CLDA0/1`** (page 0, `$01/$02`) : fin de la trame lue après une émission
  (`TPSR·256 + TBCR`), `CURR·256 + 4` après une réception (la DMA locale finit par
  l'en-tête), `CURR·256` après un rejet.
* **Page 2** en lecture seule : les pointeurs internes de la DP8390D (Remote/Local Next
  Packet Pointer, Address Counter) n'existent pas sur la RTL8019AS (`$03`, `$05-$0B`
  réservés). `RCR`, `TCR` et `DCR` s'y relisent avec leurs bits « Always 1 » ; `DCR.LAS`
  vaut 1 à la mise sous tension.
* **Paquets courts (`AR`)** : une trame de moins de 64 octets (FCS compris) est rejetée
  sauf `AR`. L'émetteur bourre : le NAT, STinG et MiNTnet le font ; seul un bouclage
  d'une trame trop courte en produit une. `SEP` n'a pas de cas (aucune trame erronée).
* **Page 3** : `9346CR` (modes normal, auto-load, programmation, écriture de
  configuration), `BPAGE`, `CONFIG0-4`, `CSNSAV` = 0, `INTR`. Les `CONFIG` ne s'écrivent
  qu'en mode `EEM=11`, sur les seuls bits que la fiche ouvre (`VERID`, `IRQEN`,
  `PL/BSELB`, `SLEEP/PWRDN`) ; l'auto-load (`EEM=01`) les recharge et remet `CR` à `$21` ;
  le port reset ne les touche pas. `PWRDN` coupe émission et réception ; `EEM=10` coupe
  la DMA. Mode RT jumperless : le readme du NetUSBee fait couper la piste de `JP` une
  fois l'EEPROM programmée.
* Restent [F] : le contenu de la 9346 (IRQ, base d'E/S, média, duplex : 0), la 93C46
  elle-même (`EEDO` lu 0, le programmateur d'EEPROM ne peut pas tourner), la FIFO, la
  valeur lue des registres réservés, la durée de l'auto-load (instantané, « about 2ms »
  selon la fiche), le mode horloge arrêtée de `HLTCLK`, et les bits « Always 1 » de
  `TSR`/`RSR` (le texte p. 14 les dit à 1, la table p. 10 à 0 : gardés à 0).

**PROM d'adresse station** (depuis le 2026-10-20) : 32 octets visibles à RSAR=0 par Remote
DMA. La MAC y est doublée (octets 0-11) et la signature `WW` ($57) occupe les octets
28-31, celle que teste `ne.c`. Une PROM 8 bits câblée sur les deux moitiés du bus donne
cette disposition, la même que QEMU. Elle est en lecture seule : une écriture Remote DMA
n'y change rien. Comme sur une vraie NE2000, la puce ne charge pas les PAR depuis la PROM :
c'est le pilote qui la lit et recopie la MAC. **MAC par défaut `02:4E:53:54:00:01`** :
adresse localement administrée (bit 1 du premier octet, « NST » en ASCII), fixe pour que
les runs restent déterministes. Aucun OUI de fabricant n'est documenté pour l'EtherNEC, qui
accepte n'importe quelle carte ISA NE2000, ni pour le NetUSBee ; une adresse inventée ne
doit pas usurper un OUI réel.

## Appareils MIDI de l'hôte — le câble DIN du ST vers du vrai matériel

Le port virtuel « NeoST MIDI OUT » (CoreMIDI / séquenceur ALSA) est une **source
passive** : un FluidSynth ou un DAW s'y abonne, mais un expandeur, une groovebox ou un
clavier maître ne s'abonne à rien. Il fallait donc un patchbay tiers pour relier NeoST à
du matériel. NeoST **choisit désormais lui-même** ses deux extrémités :

Trois backends, un seul comportement visible — `src/audio/MidiWinmm.hpp` porte les
particularités de Windows :

| | macOS | Linux | Windows |
|---|---|---|---|
| Backend | CoreMIDI | séquenceur ALSA | **winmm** |
| Destinations matérielles | ✅ | ✅ | ✅ |
| Sources matérielles | ✅ | ✅ | ✅ |
| Identifiant unique | `kMIDIPropertyUniqueID` | ❌ (nom seul) | **chemin d'interface USB** |
| Port virtuel « NeoST MIDI OUT » | ✅ | ✅ | ❌ — **pilote tiers** (loopMIDI) |
| Ouverture concurrente d'un port | multiplexée | multiplexée | **EXCLUSIVE** |

Deux différences de Windows méritent d'être connues avant de câbler un studio :

- **Aucun port virtuel n'est possible en natif.** Ni winmm ni WinRT MIDI 1.0 ne savent
  créer un port que les autres applications voient — il y faut un **pilote** (loopMIDI,
  LoopBe1). Une fois installé, son port apparaît dans les listes ci-dessous comme
  n'importe quel appareil, **en destination et en source** : le câblage vers un DAW est
  alors complet, sans que NeoST ait à s'en mêler. (Windows MIDI Services, la pile MIDI
  2.0 de Windows 11, sait le faire nativement mais s'installe à part et exigerait une
  pile WinRT/COM que le zip autonome ne peut pas emporter.)
- **Un port MIDI est exclusif.** Là où ALSA et CoreMIDI multiplexent, un DAW ouvert sur
  le clavier empêche NeoST de l'ouvrir : `MMSYSERR_ALLOCATED`, journalisé en clair
  (`device busy - another application holds it`).

L'**identifiant unique**, lui, est meilleur que sous Linux : `DRV_QUERYDEVICEINTERFACE`
rend le chemin d'interface du périphérique
(`\\?\usb#vid_1235&pid_0139&mi_00#6&33d600c&0&0000#{...}`), dont la partie médiane est
le chemin **physique** — hub et prise. Deux appareils du même modèle branchés ensemble
sont donc discernables, comme sous CoreMIDI. ⚠ Corollaire : **changer de prise USB change
l'identifiant** ; le repli par nom rattrape le cas, avec son avertissement au journal.
Les synthés logiciels du système (« Microsoft GS Wavetable Synth ») ne sont pas des
périphériques et rendent une chaîne vide : identifiant vide, repli par nom — le régime
dans lequel ALSA vit tout entier.

| Réglage | `neost.cfg` (UNE ligne, comme toute autre clé) | Effet |
|---|---|---|
| Destinations | `midi_out_devices=nom\|1,2,10-12;autre\|3` | chaque appareil reçoit les canaux nommés |
| Sources | `midi_in_devices=nom\|1;autre\|2` | chaque appareil entre dans le MIDI IN, forcé sur ce canal (0 = tel quel) |

Enregistrements séparés par `;`, champs par `|`, et ces caractères sont échappés par
`\` dans les noms. ⚠ Le format **répétable** de la première version (`midi_out_device=`
une fois par appareil) est encore LU, jamais écrit : `parseConfigLine` est partagé avec
les **profils nommés**, qui s'appliquent *par-dessus* la config courante — là où une clé
scalaire remplace, une clé répétable AJOUTAIT, si bien que charger un profil dupliquait
chaque appareil. Une clé, une ligne, une affectation.

**La sortie est un AIGUILLAGE, pas un Thru box.** Chaque destination porte le masque des
canaux qu'elle reçoit : « instrument 1 de Cubase vers le piano logiciel, instrument 2
vers la groovebox » se règle chez NeoST, sans toucher au réglage des appareils. Un même
canal peut partir vers plusieurs destinations (superposition). ⚠ Les messages **système**
(horloge, start/stop, SysEx) n'ont pas de canal et vont à **toutes** les destinations :
les filtrer désynchroniserait le studio.

**L'entrée est un boîtier de FUSION.** Le ST n'a qu'une prise MIDI IN ; plusieurs
appareils y sont réunis. Un tel boîtier ne mélange pas des octets, il entrelace des
**messages** : deux claviers joués ensemble émettent `90 3C 40` et `90 40 40` au même
instant, et entrelacés octet par octet ils donneraient `90 90 3C 40 40 40`, du charabia.
D'où un décodeur par source (`MidiMessageParser`, partagé avec la sortie). Le statut
n'est ré-émis dans le flux fusionné que s'il a **changé** : une source seule garde son
running status, deux sources qui alternent le voient correctement réinséré.

**Canalisation — sans elle, pas d'enregistrement multipiste.** Deux claviers émettent
tous deux sur le canal 1 par défaut : un séquenceur ne peut alors pas les séparer et
tout atterrit sur la même piste. Forcés sur 1 et 2, ils deviennent enregistrables
simultanément sur deux pistes. Ce que le séquenceur ST en fait le regarde — les Cubase
complets et Notator savent enregistrer plusieurs canaux sur plusieurs pistes, Cubase
Lite non.

GUI : Configuration → **MIDI**. La sortie est une **matrice** appareils × 16 canaux (un
clic par affectation, `all` / `none` par ligne) ; l'entrée, une liste d'appareils avec le
canal forcé de chacun. Headless : `--midi-list` énumère les entrées,
`--midi-in-device NAME` (**répétable**, fusionnées) et `--midi-in-channel N` (s'applique
à l'appareil précédent). Il n'y a pas de `--midi-out-device` côté headless :
`--midi-dump` capture déjà ce que le ST émet, sans dépendre du matériel branché — c'est
ce qu'un test veut.

Quatre choix de conception, chacun payé par un piège réel :

- **Désignation par NOM + IDENTIFIANT UNIQUE, jamais par index.** Débrancher un
  périphérique renumérote tous les autres : une config mémorisée en index se serait mise
  à piloter le mauvais appareil au branchement suivant. Mais le nom seul ne suffit pas
  non plus — deux machines du **même modèle** portent exactement le même nom. La config
  mémorise donc `kMIDIPropertyUniqueID` (CoreMIDI) à côté du nom, et l'appariement fait
  deux passes : identifiant d'abord, nom ensuite, en n'attribuant **jamais deux fois le
  même point de terminaison**. Deux claviers identiques s'ouvrent chacun sur le sien,
  y compris sous ALSA (pas d'identifiant stable, mais la non-réattribution suffit quand
  les deux sont branchés). L'identifiant est **appris** à la première ouverture, donc
  une config qui ne connaît qu'un nom devient sûre d'elle-même. Interface : suffixe
  « #1 / #2 » sur les homonymes. Logique d'appariement : `src/audio/MidiEndpoint.hpp`,
  fonction PURE — c'est ce qui la rend testable sans deux appareils identiques sous la
  main.
- **L'absence n'efface pas le réglage.** Un appareil débranché n'est pas une erreur de
  configuration : le nom reste dans `neost.cfg`, la page affiche *(not connected)*, et
  la boucle **re-tente à 1 Hz** — rebrancher le câble USB suffit, sans rouvrir la
  configuration. (C'est la leçon du `midi_out_port` retombé à 0 sans un mot.)
- **Panique avant de couper.** Un synthé ne relâche JAMAIS une note tout seul : fermer
  la destination en plein accord la laisserait tenue indéfiniment dans l'appareil.
  `closeDestination()` envoie donc CC 120/121/123 sur les 16 canaux **avant** de fermer.
- **C'est l'ACIA qui tire, à 31 250 bauds.** CoreMIDI livre ses paquets sur son propre
  thread quand ça lui chante ; `MidiInHost` accumule, et l'ACIA vient prendre un octet
  toutes les 2 560 cycles (`Scheduler::MIDI_RX`, le pendant de `MIDI_TX` et le même
  patron qu'`IKBD_RX` pour l'ACIA clavier). La première version poussait les octets une
  fois par TRAME, ce qui plafonnait l'entrée à 2 octets/trame — mesuré 1,76, soit
  ~143 o/s contre 3 125 o/s sur un câble : un accord de dix notes mettait 0,2 s à entrer.
  Après correctif, **40,4 octets/trame mesurés en temps réel (~2 885 o/s, 92 % du câble)**.
  Le débordement redevient celui du **matériel** : si le ST ne lit pas assez vite, le
  6850 perd l'octet **neuf** (garder l'ancien préserve le début des messages entamés).
  Éprouvé par `neost-selftest` sur le chemin complet ACIA + Scheduler, sans appareil.
  ⚠ Cette mesure n'est possible que dans le **GUI** : le headless émule ~19 fois plus
  vite que le temps réel, donc une source MIDI réelle y est toujours le facteur limitant.

**Profils d'appareil.** Cocher seize cases pour une machine dont le plan de canaux est
public est du travail perdu : `src/audio/MidiDeviceProfiles.hpp` pose le masque d'un clic
pour les appareils connus. Première entrée, le **Novation Circuit Tracks** (*Programmer's
Reference Guide* v3) :

| Piste | Canal | Notes |
|---|---|---|
| Synth 1 / Synth 2 | 1 / 2 | |
| MIDI 1 / MIDI 2 | 3 / 4 | pistes MIDI externes |
| **Drums 1-4** | **10** (les quatre) | 60 = Drum 1, 62 = Drum 2, 64 = Drum 3, 65 = Drum 4 |
| — | 16 | réservé au Project Control |

Les quatre percussions partagent donc **un seul canal** et se distinguent par la **note** :
pour les séquencer depuis le ST, une piste sur le canal 10 avec ces quatre hauteurs.
⚠ Défauts d'usine, réassignables dans le Setup View de la machine — la table du dépôt
n'accepte qu'une source constructeur **citée**, jamais une supposition.

**Avance de livraison** (`midi_lead_ms=`, curseur 0-100 ms). Chaque octet est reposé à
l'heure hôte correspondant à son cycle 68000, plus cette avance : c'est elle qui tue la
gigue de trame (σ 0,4-1,7 ms mesurée sur l'étalon Cubase), et c'est elle qu'on paie en
latence quand on joue au clavier à travers le ST. Mesuré : 190 ms d'écart de livraison
pour 200 ms commandés. ⚠ Elle ne protège **pas** du cas courant — l'émulation d'une trame
prend moins de temps réel qu'une trame, donc à 0 ms aucun octet n'est en retard sur un run
sain — mais du **rattrapage** quand la boucle GUI décroche (drag de fenêtre, rafale
disque). Le compteur d'octets « sent late » de la page MIDI est le témoin : on baisse
jusqu'à ce qu'il bouge.

Sous Linux, les destinations matérielles sont **adressées explicitement**
(`snd_seq_ev_set_dest`) et NON abonnées comme le ferait `aconnect` : un abonné recevrait
le flux entier, ce qui interdirait le filtrage par canal — c'est-à-dire tout l'intérêt de
l'aiguillage. Choisir une destination fait tout de même exister le port « NeoST MIDI OUT »
même si sa case est décochée : sans port source, il n'y aurait rien d'où émettre.

Aiguillage et fusion vérifiés le 2026-08-29 (macOS, deux destinations virtuelles) : la
panique de fermeture diffuse des CC sur les 16 canaux, et chaque destination n'a reçu que
les siens (`B0 78 00…` pour celle du canal 1, `B1 78 00…` pour celle du canal 2, 9 octets
chacune sur 48 messages), tandis que le SysEx de démarrage de MROS est arrivé aux **deux**.

Vérifié le 2026-08-29 (macOS, Novation Circuit Tracks + appareils virtuels de test) :
255 octets entrés dans l'ACIA depuis une source hôte (0 perdu, contre 2 octets pour un
appareil qu'on ne touche pas), et le SysEx de démarrage de MROS reçu par une destination
matérielle **port virtuel coupé** — donc portée par la destination seule. Le backend
ALSA est écrit mais n'a **pas** été exécuté (pas de machine Linux dans la boucle).
`src/audio/MidiInHost.cpp`, `src/audio/MidiOutHost.cpp`.

Vérifié le 2026-08-31 (**Windows 11, matériel réel, Novation Circuit Tracks en USB**) :
`neost-headless --midi-list` énumère l'appareil avec son identifiant d'interface ;
`--midi-in-device "Circuit Tracks"` fait entrer **35 octets d'horloge MIDI dans l'ACIA
en 1,3 s** (0 perdu) — l'appareil émet son horloge en continu, ce qui donne une source
de test déterministe, sans les mains ; le GUI ouvre les deux extrémités d'un coup
(destination + source canalisée), **apprend l'identifiant** et livre **0 octet en
retard**. Gamme, SysEx, running status et panique éprouvés sur l'appareil réel par un
harnais pilotant `MidiOutHost` directement.

**Résolution du minuteur système** (`timeBeginPeriod(1)`, posée tant qu'une sortie est
ouverte) : sans elle Windows réveille le thread de livraison par tranches de ~15,6 ms, et
tout le mécanisme d'horodatage (ancre + `midi_lead_ms`) serait arrondi à cette tranche.
Mesuré A/B sur 40 échéances de 50 ms vers le Circuit Tracks :

| | intervalle moyen | σ | écart max |
|---|---|---|---|
| minuteur 1 ms **posé** | 50,00 ms | **0,30 ms** | 0,90 ms |
| minuteur 1 ms absent | 50,09 ms | 5,34 ms | 13,10 ms |

## Anneau MIDI réseau — MIDIMaze en ligne (`--midi-net H:P[:L]`)

MIDI Maze relie les ST en **anneau** par câbles MIDI (OUT de l’un → IN du suivant).
`MidiAcia::setMidiSink` transmet les octets achevés vers le pair aval : ceux d'une trame
partent ensemble dans **un** datagramme, au `poll()` de fin de trame (ou dès 512 octets).

**Format v1** (2026-11-11) : `'N' 'M' 'R' 0x01`, session (u32 gros-boutiste, tirée à
l'ouverture), numéro de séquence (u32, +1 par datagramme), puis 1 à 512 octets MIDI. Le
récepteur se verrouille sur la première source valide (adresse + port) et ignore les
autres tant qu'elle parle (relève après 5 s de silence) ; il jette doublons et
datagrammes en retard, compte les trous, et se resynchronise quand la session de sa
source change (pair relancé) ; un datagramme tardif de la session qu'il vient de quitter
est jeté comme doublon au lieu de la rétablir (2026-11-12). L'ancien format — un octet brut par datagramme, sans
en-tête, jamais documenté comme protocole — est **refusé** avec un avertissement : les
deux bouts d'un anneau doivent être à jour. Un datagramme hors ordre est perdu, pas
recousu : un câble MIDI ne réordonne rien.

Les datagrammes amont sont collectés une fois par trame dans le tampon de gigue de `MidiRing` ;
`tryPop` alimente ensuite la source série à **un octet toutes les 2560 cycles**.
Il n’y a pas d’appel UDP par octet. Le 6850 n’a qu’un seul RDR : si le CPU ne le lit
pas, le caractère suivant est perdu et l’overrun matériel est signalé. Le tampon
réseau ne constitue pas une extension du RDR.

En headless, un anneau ouvert prend la place des appareils MIDI IN hôtes sur cette
entrée DIN ; si les deux ont été demandés, le choix est signalé dans le journal.
Le contrôle local `tests/midi_ring_logic.cpp` (cible `neost-midi-ring-test`, lancée
par le palier `fast` quand `NEOST_WITH_NET` est actif) reçoit 20 octets dans l’ordre en
51200 cycles, portés par un seul datagramme (127.0.0.1 uniquement), et rejette doublon,
datagramme réordonné, source étrangère et ancien format brut. Le test historique
`MIDITEST.TOS` (`dev/netdemo/`) avait aussi validé le round-trip OUT→UDP→IN→ACIA.
Implémentation : `src/net/MidiRing.cpp`.

## UltraSatan — interface SD/MMC sur le bus ACSI (`--ultrasatan`, GUI Hard disks)

C'est **la** réponse historique de l'écosystème ST au stockage moderne (Jookie, 2009) :
un boîtier ACSI à **deux slots SD/MMC** — chaque slot est une cible ACSI (IDs *n* et
*n+1*, usine 0-1) — et une **horloge temps réel** sauvegardée par pile. Les pilotes
d'époque (HDDRIVER, ICD PRO, AHDI, **EmuTOS sans pilote**) le voient comme deux disques
SCSI ordinaires ; l'outil `US_CONF.TOS` lui parle par des **paquets ICD propres** :

```
octet 0 : (ID << 5) | $1F         marqueur ICD
octet 1 : $20                     groupe 1 (10 octets) — jamais un opcode SCSI utile
octets 2-3 : 'U','S'              signature UltraSatan
octets 4-7 : code ASCII           CurntFW · RdCl/WrCl · RdINQRN/WrINQRN · RdSt/WrSt · RdLog · RdFW/WrFW
octets 8-10 : 3 paramètres        magie 'RTC' (WrCl), $83 $03 $17 (WrSt)
puis UN secteur (512 octets) par DMA, dans le sens de la commande
```

| Élément | NeoST (`src/io/UltraSatan.*`, `Acsi`) | Source de vérité |
|---------|----------------------------------------|------------------|
| INQUIRY | `JOOKIE  ` + nom (10 car., défaut `UltraSatan`) + n° de slot `'1'`/`'2'` + `1.20` + date, bit **RMB** | `scsi6.c SCSI_Inquiry` |
| Slot sans carte | TEST UNIT READY / READ CAPACITY / READ / WRITE → CHECK CONDITION, clé **NOT READY**, ASC `$3A` (medium not present) ; INQUIRY répond quand même | `ReturnStatusAccordingToIsInit` |
| `USCurntFW` | secteur = `UltraSatan v1.20 (NeoST emulation)`, 0-terminé | `special.c` |
| `USRdClRTC` / `USWrClRTC` | `'R','T','C'` + `{année−2000, mois, jour, h, min, s}` binaires ; écriture = magie `RTC` dans le **paquet ET le secteur** | `special.c`, `rtc.c` |
| `USRdINQRN` / `USWrINQRN` | nom INQUIRY (10 octets ; `$00`/`$FF` en tête = nom d'usine) | `special.c` |
| `USRdSt` / `USWrSt` | page de réglages 512 octets (opaque, octet 1 = firmware amorcé) ; écriture **refusée sans la magie** `$83 $03 $17` | `special.c` |
| `USRdLog` | journal de commandes (vide) | `special.c` |
| `USRdFW` / `USWrFW` | **refusés** (CHECK CONDITION) : NeoST n'émule pas la dataflash — impossible de briquer l'appareil | — |
| Horloge | pile propre, **indépendante** du RP5C15 Mega : seedée sur l'hôte, avance avec les **cycles émulés** (comme `Rtc`) | `rtc.c` (année base 2000) |

Sources : firmware v1.20 ([atarijookie/ce-atari `ultrasatan/`](https://github.com/atarijookie/ce-atari/tree/master/ultrasatan)).
Hatari n'a aucun équivalent (disque ACSI générique « Hatari Emulated Harddisk »). Toute
cible qui n'est pas un slot UltraSatan reste **byte-identique** au port de `hdc.c`.

| Où | Comment |
|----|---------|
| Headless | `--ultrasatan` (IDs 0-1), `--ultrasatan-id N`, `--sd1 IMG`, `--sd2 IMG` (`--acsi IMG` = slot 1 quand l'ID est 0) |
| GUI | Configuration → **Hard disks** : case UltraSatan, slot 1 = l'image ACSI, slot 2 à monter ; `neost.cfg` : `ultrasatan=`, `sd2=` (rejoués par `--from-cfg` depuis le 2026-10-20 : cibles 0-1, slot 2 lu seulement si la case est cochée) |
| Code | `machine.enableUltraSatan(firstTarget); machine.fdc.mountAcsi(path, firstTarget + slot);` |

Image de carte SD : n'importe quel dump brut partitionné (Atari ou DOS). `tools/make_usatan_hd.py`
en génère une minimale (16 Mo, une partition GEM FAT16) qu'EmuTOS monte en **C:** sans pilote.

## NetUSBee — NE2000 + hôte USB ISP1160 sur le port cartouche (`--netusbee`, GUI Network)

Le [NetUSBee](https://hardware.atari.org/netusbee/netus.htm) = une **RTL8019AS câblée
exactement comme l'EtherNEC** (pilotes rétro-compatibles — NeoST réutilise `io/Ne2000`
tel quel) + un **ISP1160** (hôte USB 1.1, deux ports) sur le même port cartouche. Le port
étant en lecture seule, 16 bits, sans A0, le pilote FreeMiNT (`sys/usb/src.km/ucd/netusbee/
isp116x.h`) encode tout dans l'adresse :

```
$FA0000 + (b << 1)   LSB_WRITE      : verrouille l'octet b
$FB8000 + (b << 1)   MSB_DATA_WRITE : écrit le MOT (b << 8) | latch dans le port DONNÉES
$FBC000              MSB_CMD_WRITE  : écrit le MOT latch dans le port COMMANDE (index | $80 = écriture)
$FA8000              DATA_READ      : lit un mot 16 bits du port DONNÉES (registres 32 bits : bas puis haut)
```

`src/io/Isp1160.*` exécute les transferts ATL et fournit deux ports USB : clavier
HID, souris HID ou disque SCSI/Bulk-Only sur image. L'horloge USB est de 1 ms,
indépendante de la vidéo ; puissance, connexion, reset et changements de statut
sont visibles dans le hub racine. Le branchement se fait à chaud dans la page
**Network**, ou avec `--usb1` / `--usb2` (`keyboard`, `mouse`, `disk:IMAGE`,
`disk-ro:IMAGE`, `none`). Les pilotes USB4TOS 68000 sont vérifiés en exécution.
L'IRQ reste non câblée (polling), ITL/isochrone et passthrough USB hôte non exécutés.

Le latch `$FA0000` partage une **lecture** du CR Ethernet, sans le modifier.
L'ancienne mention d'une écriture partagée du CR était erronée. La différence de
latch entre le document de conception de 2006 et le pilote FreeMiNT reste consignée.

| Où | Comment |
|----|---------|
| Headless | `--netusbee` ; `--usb1 keyboard --usb2 mouse` ou `--usb2 disk:IMAGE` |
| GUI | Configuration → **Network** : NetUSBee puis les deux ports USB |
| Configuration | `netusbee=1`, `usb1=keyboard`, `usb2=disk:IMAGE` (profils et `--from-cfg`) |
| Code | `machine.enableNetUsbee()` puis `machine.isp1160.attach(port, kind, image, ro)` |

Protocoles, références primaires, limites et banc USB4TOS : **[NETUSBEE.md](NETUSBEE.md)**.

## Tests UltraSatan + NetUSBee

* `neost-headless --usatan-selftest` — 15 vérifications au niveau **fil** ($FF8604/06, séquence
  **LongRW** de `US_CONF` : A1 bas/haut, bascule R/W + compteur de secteurs AVANT le dernier
  octet, statut après transfert) : INQUIRY des deux slots, slot vide (NOT READY), slot avec
  carte (TEST UNIT READY, READ CAPACITY, READ(6)), `CurntFW`, `RdCl`/`WrCl` (horloge figée),
  `RdINQRN`/`WrINQRN`, `RdSt`/`WrSt`, refus flash/inconnu, **verrouillage** du paquet `'US'`
  aux cibles UltraSatan. Palier `fast` (`tools/etalons.json`, type `usatan_selftest`).
* `neost-headless --netusbee-selftest` — 39 vérifications : les primitives **exactes** du pilote
  FreeMiNT (`raw_read/write_data16/32`, lectures MOT), ID de puce, scratch, reset logiciel,
  registres 32 bits, ATL/EOT, IRQ, coexistence NE2000, énumération, rapports HID, disque
  BOT/SCSI, erreurs et lecture seule, save-state, cadence de 1 ms.
  Palier `fast` (type `netusbee_selftest`).
* `tools/selftests.json` → **`usatan_netusbee`** (palier `fast`, verdict série) : une carte SD
  générée (`tools/make_usatan_hd.py`, 16 Mo, partition GEM FAT16) qu'EmuTOS monte en **C:** sans
  pilote et dont il lance **`AUTO\USTEST.PRG`** — le programme de test 68000 RELOGEABLE de
  `tools/make_usatan_test.py` (table de relocation TOS générée, `Super()` puis `Pterm0`), qui parle
  à l'UltraSatan **comme `US_CONF`** (séquence LongRW, attente IRQ GPIP5) et au NetUSBee **comme le
  pilote FreeMiNT** (lectures MOT, primitives raw), et vérifie `_drvbits` bit 2. Verdicts `usfw
  usinq usrtc uscdrv nubid nubscr nubnic`. Trois règles EmuTOS apprises en construisant ce test
  (`bios/blkdev.c`, `bios/disk.c`) : dès qu'un disque dur existe, la **disquette n'est plus
  amorcée** ; un **secteur racine exécutable n'est lancé que sur un disque SANS partition
  reconnue** (`disk_try_dmaboot`) ; et le type de FAT suit la **règle Microsoft** (≤ 4084 clusters
  = FAT12 — une partition de 2 Mo y était lue en FAT12, d'où 16 Mo), là où Atari TOS suppose un
  FAT16 sur tout disque dur. `make_usatan_test.py OUT.st` produit la **disquette** équivalente
  (`A:\AUTO`) pour tester SANS carte SD : `uscdrv` échoue alors, c'est attendu. **Contrôle
  négatif** : la même carte sous Hatari (`--acsi`, `--rs232-out`) donne `uscdrv PASS` et les six
  verdicts matériels **FAIL** — le test ne passe pas par construction.
* Save-states : **v12** (UltraSatan + ISP1160 sérialisés) ; drapeaux d'en-tête (v13) bit2 = UltraSatan,
  bit3 = NetUSBee — les configs save/load doivent concorder). Les images ACSI (cartes SD
  UltraSatan comprises) sont vérifiées aussi : bit5 = au moins une image montée, et une
  empreinte par cible (chemin + taille) dans l'en-tête ; un état repris sans l'image, avec une
  autre ou sur une autre cible est refusé avec son motif (`lastStateError`).

## Clé Steinberg — dongle Cubase sur le port cartouche (`--dongle`, GUI Dongles)

La protection de Cubase ST : une clé dans le port cartouche, lue en `$FB0000-$FBFFFF`
(/ROM3 — le TOS ne sonde que /ROM4 `$FA0000`, la clé lui est invisible et cohabite avec
le HD GEMDOS). Pas d'écriture possible sur ce port : le **défi** voyage sur les lignes
d'adresse (A1-A8) d'une lecture « fantôme », la **réponse** revient sur D8-D15.

| Clé | Puce | Logiciels | Horloge | Émulation |
|-----|------|-----------|---------|-----------|
| **rouge** (`cubase3`) | EPLD Intel 5C060, 16 bascules T, 1 entrée (A8), 1 sortie (D8) | Cubase 3.10, Cubase Score 2.0x, Cubase Audio Falcon | front montant de **/ROM3** : seuls les accès `$FBxxxx` la font avancer | fidèle (équations du JED décapsulé, décompilé par troed) |
| **noire** (`cubase2`) | PAL16R8, 8 bascules D, A1-A8 → D8-D15 | Cubase 2.01 | **chaque** front montant de /UDS, où que lise le CPU — fetchs compris | « au mieux » : exige le motif bus d'un vrai 68000 (Moira modélise le prefetch) ; crochet `udsDone` dans `NeostMoira` |
| `auto` | — | — | — | heuristique MiSTery : premier accès `$FBxxxx` avec A7..A1 = 0 → rouge, sinon noire |
| **C-Lab** (`notator`) | EP600 (= 5C060), 8 bascules D actives bas + 1 bascule d'armement | Notator, Creator (clé seule ou intégrée à l'Unitor-N) | **armement** : `FEEDB1 := STER` sur /ROM4 (STER = A8..A1 = `$75`, soit `$FA00EA`) ; **données** : désarmée → UDS de chaque cycle (comme la noire), armée → **descente** de /ROM3 (le CPU lit l'état *après* le coup d'horloge) ; resets asynchrones D9 (A4·A2) et D8 (A3·A1) pendant l'accès | fidèle aux équations TPH (JED EP600 décapsulé par Zippy, relevé Unnamed Villain, publié 10/2025 ; transcription C du firmware SidecarTridge `md-notator`) ; tout terme contient STER → l'armement remet les 8 bascules à 0, le motif bus exact ne compte donc pas |

### Oracle : trace de référence et rejeu (`--key-log`, `--key-replay`)

Aucun logiciel à clé n'est dans le dépôt : « fidèle aux équations » est une promesse
tant que rien ne la mesure. L'oracle est une **trace de référence** — une ligne par
signal du port cartouche, telle qu'un analyseur logique ou un SidecarTridge (`md-notator`
voit /ROM3, /ROM4 et A1-A8) peut la produire sur une vraie machine :

```
R3 <A8..A1 hex> <octet D15..D8 lu>   lecture sous /ROM3 ($FBxxxx)
R4 <A8..A1 hex>                      lecture sous /ROM4 ($FAxxxx) — armement Notator
U  <A8..A1 hex>                      cycle /UDS hors fenêtre cartouche (clés cadencées par UDS)
#  commentaire
```

`neost-headless --dongle MODEL --key-log FILE` écrit cette trace pendant une session ;
`neost-headless --dongle MODEL --key-replay FILE` rejoue n'importe quelle trace (capture
matérielle ou journal NeoST) contre la machine d'état **sans machine** et sort 0 si tous
les `R3` concordent, 1 avec la première ligne en écart. C'est le test qui tranchera les
incertitudes de chronologie (front de /ROM4, ordre UDS↔/ROM4 à l'armement) le jour où une
capture existe — et il vaut pour les trois clés. ⚠ Une clé cadencée par UDS (noire,
Notator) journalise **chaque** cycle bus : ~30 000 lignes par trame. Un auto-test rejoue
une trace écrite par NeoST (0 écart) et une trace altérée (1 écart localisé). La page
Dongles affiche en direct le nombre de sondages, le dernier octet rendu, l'état et
l'armement.

### Recette de capture matérielle (à faire quand une clé et un logiciel sont réunis)

1. Sonder A1-A8, D8-D15, /ROM3, /ROM4, /UDS sur le port cartouche (analyseur logique ≥ 50 MHz,
   ou firmware SidecarTridge dérivé de `md-notator` qui journalise au lieu de répondre).
2. Échantillonner au front montant de /ROM3 (données valides) → `R3`, au front montant de
   /ROM4 → `R4`, au front montant de /UDS hors $FA/$FB → `U`.
3. `neost-headless roms/<tos>.img --dongle notator --key-replay capture.trace`.

Pour la clé C-Lab, les accès `$FAxxxx` du **GEMDOS HD** de NeoST (qui vit sur /ROM4)
**désarment** la clé (`FEEDB1 := 0`) — Notator le tolère s'il réarme à chaque contrôle,
comme le ferait n'importe quel accès cartouche du TOS sur une vraie machine. Un accès
**octet impair** à `$FA00EB` (seul /LDS descend) arme la clé dans le même cycle, sans
cadencer les données : FEEDB1 suit /ROM4, pas /UDS (corrigé le 2026-11-11 ; avant, l'armement
attendait le /UDS du cycle suivant). Le format de trace ne note pas A0 : un `R4 75` rejoué
est toujours un accès pair.

**Reset à chaud** : les clés gardent leurs bascules. [E] Le port cartouche n'a pas de ligne
/RESET — ses 40 broches sont toutes prises (+5 V ×2, D0-D15, A1-A15, /ROM3, /ROM4, /UDS,
/LDS, masse ×3 ; START vol. 1 n° 3, hiver 1986, d'après Abacus « ST Internals » ; ST-Log
n° 27, janv. 1989). Seule la mise sous tension les remet à 0. Tranché au grade B par un
schéma Atari de la carte mère.

Source des équations : cœur FPGA **MiSTery** (`atarist/cubase2_dongle.v`,
`cubase3_dongle.v`, gyurco) — clé noire relevée par force brute sur une clé réelle
(MasterOfGizmo, 2022), clé rouge depuis le JED de la puce décapsulée. Transcription en
C++ dans `src/io/CartridgeKey.cpp` (classe `CartridgeKey`, abonnée au port cartouche via
`core/CartDevice.hpp`), épinglée par `neost-selftest` (propriétés : registres
à 0 à la mise sous tension, motif de reset logiciel `%11011000` → 0 sur la noire, /UDS sans effet sur
la rouge). Une lecture renvoie l'état **courant** (les bascules basculent en fin de
cycle) ; octet faible `$FF`. Sérialisé dans le save-state (v14, drapeau bit 4).

⚠ **Non validée sur logiciel** : aucun Cubase à clé n'est dans le dépôt (Cubase Lite n'en
a pas besoin). Recette quand on en aura un : `neost-headless roms/tos104fr.img --dongle
cubase3 --disk cubase310.st --frames 3000 --screenshot s.ppm` ; si bombes ou « dongle
not found », tracer `--trace` autour des lectures `$FB0000` (la rouge lit avec A7..A1 = 0,
A8 = bit de défi).

---

## Périphériques des ports — un par port : clés joystick / série, DAC Pro Sound, boutons (`--plug`, GUI Dongles)

Tout ce qui se branchait sur un port **autre que la cartouche** pour qu'un logiciel le
sonde. Classe `PortDevices` (`src/io/PortDevices.{hpp,cpp}`) : le modèle est **physique**,
un périphérique optionnel par port — joystick 0, joystick 1, RS-232, imprimante, bouton de
cartouche — et ils **coexistent** (Leader Board dans le joystick 1 + DAC Pro Sound sur
l'imprimante + clé Cubase dans la cartouche, comme sur une vraie machine). On peut aussi se
tromper de port, comme avec l'objet : une clé joystick entre dans les deux DE-9, le jeu
n'en sonde qu'un (le GUI signale « wrong port for this game »). `--plug PORT=DEVICE`
(répétable) ou `--adapter DEVICE` (port par défaut) en headless ; `joy0=`, `joy1=`,
`rs232=`, `printer=`, `cartbutton=` dans `neost.cfg` (l'ancien `adapter=` est relu) ; page
**Dongles**. `--from-cfg` rejoue ces cinq clés ainsi que `dongle=` et `auto_dongle=` par
les mêmes chemins que `--plug` et `--dongle` (depuis le 2026-10-20). Un identifiant inconnu
est ignoré avec un avertissement, comme le GUI l'oublie. **OFF par défaut**, sans effet sur les étalons. **Sérialisé** dans le
save-state (v16) : l'oscillateur de Cricket et la date de Music Master font partie du
déterminisme, et les périphériques branchés sont restaurés avec l'état.

**Branchement automatique** : `disks/dongles.txt` (créé avec les titres connus s'il manque,
éditable) associe un motif du nom d'image à un branchement (`leader board = joy1:leaderboard`,
`notator = cart:notator`). Au montage d'une disquette (GUI, borne, `--disk`), NeoST remplit
les emplacements **vides** seulement — un réglage explicite prime — et l'annonce. `auto_dongle=0`
ou `--no-auto-dongle` pour désactiver. Logique pure dans `io/DongleTable`, auto-testée.

| `DEVICE` | Logiciel | Port sondé | Ce que fait la clé | Source |
|----------|----------|------|--------------------|--------|
| `leaderboard`, `10thframe` | Leader Board, 10th Frame (Access) | joystick 1 | cavalier HAUT+BAS : le jeu lit les deux bits à 1, impossible avec un vrai joystick | Steem `ikbd.cpp`, WinUAE `JOY1DAT == 0x0101` |
| `cricket`, `soccer` | Cricket Captain, Multi Player Soccer Manager (D&H) | joystick 0 | oscillateur : nibble direction `%1100` ↔ `%1101` à chaque sonde IKBD (« must continuously change state ») | Steem, WinUAE |
| `rugby` | Rugby Coach (D&H) | joystick 1 | idem | Steem |
| `bat2` | B.A.T. II (Ubi Soft) | RS-232 | CTS (GPIP2) lu à 0 en permanence (sur ST ; l'Amiga exige une impulsion DTR). ⚠ NeoST et Hatari lisent déjà CTS à 0 au repos : la clé est redondante, gardée pour la cohérence avec Steem | Steem `ior.cpp` |
| `musicmaster` | Music Master (Computer's Dream) | RS-232 | DTR recopié sur DCD (GPIP1) avec **~200 cycles** de retard : la première lecture après le basculement rend encore l'ancienne valeur | Steem « inspired by WinUAE » |
| `jeannedarc` | Jeanne d'Arc (Chip) | RS-232 | DCD assertée quand le mot (RTS\|DTR) **décroît sans s'annuler** : `DCD = !(new && new < old)` | Steem `iow.cpp` |
| `prosound` | Wings of Death, Lethal Xcess (Thalion) | parallèle | **pas une clé** : DAC 8 bits R-2R Pro Sound Designer (Eidersoft) sur le port imprimante — samples sur STF sans son DMA. R15 horodaté et rejoué par le YM2149 avec son propre bloc DC (amorcé au branchement, continu au chargement d'un état : pas de clic) et son **fader** page Sound (`mix_dac=`) | Steem `SSE_DONGLE_PROSOUND` |
| `multiface` | Multiface ST (Romantic Robot) | cartouche + câble moniteur | bouton **freeze** : GPIP7 (détection moniteur) tiré à 0 le temps de l'appui → IRQ niveau 7 prise par la ROM (`--cart`) ; relâché à la VBL suivante. Bouton : page Dongles, `--button-at N` | Steem `run.cpp`/`options.cpp` |
| `urc` | Ultimate Ripper (Gotcha) | cartouche + port série | même idée sur la ligne RI (GPIP6) : l'appui ASSERTE RI (bit 6 → 0, repos à 1 depuis le 2026-10-20), relâchée à la VBL suivante — réaction attendue du logiciel non vérifiée | Steem |

Câblage NeoST : joystick → `Ikbd::setJoystickProbe` (par-dessus l'état hôte) ; série →
abonné port A du PSG (`Machine`) + crochet de lecture `$FFFA01` (`Mfp::setGpipReadHook`,
B.A.T. II / Music Master sont **sondés**, pas armés en IRQ) ou ligne MFP avec front AER
(Jeanne d'Arc, boutons) ; parallèle → `YM2149::setPortBDac`. `Machine::portsApply` remet
les lignes au repos quand on débranche (DCD repos actif, RI, GPIP7). Auto-tests de logique
pure dans `neost-selftest` (un par protocole, plus connecteurs et coexistence).

⚠ **Validation** : les protocoles sont transcrits de Steem/WinUAE, aucun des jeux à clé
n'est dans le dépôt. Wings of Death (présent, `disks/stx`) prouve seulement que le DAC
atteint le mixeur (`--sound-dump` diffère d'un échelon continu filtré) — l'option « Pro
Sound » du menu du jeu reste à exercer.

### Inventaire des dongles ST connus (recherche 2026-08-23) et pourquoi le reste n'est pas émulé

| Logiciel | Port | Matériel | Statut |
|----------|------|----------|--------|
| Cubase 2.01, Avalon 2.1, Synthworks Wavestation | cartouche | **clé noire** PAL/GAL16V8 (routine « A » du forum exxos) | Cubase 2 émulé (équations MiSTery) ; Avalon/Synthworks : même famille, **équations distinctes non relevées** |
| Cubase 3.10, Score 2.0x, Audio Falcon | cartouche | **clé rouge** EPLD 5C060 | émulé |
| Notator / Creator (C-Lab), Unitor-N | cartouche | EP600 ; JED extrait par décapsulation (Zippy), équations publiées par TPH en octobre 2025 (atari-forum t=43078, 1er message), transcrites en C dans le firmware SidecarTridge [`md-notator`](https://github.com/MrYoIt/md-notator) (2026) | **émulé** (`--dongle notator`, voir plus haut) |
| Log 3 (Emagic : Notator Logic) | cartouche | EP600 + EP330 (2 puces) ; seule la partie EP600 est publiée | non émulé (EP330 non relevée) |
| Pro-24 / Twenty Four, Proscore | cartouche | GAL16V8 | aucun relevé public (Steem : « failed like an old dog ») ; Pro 24 v2.1 tourne sans clé |
| Music Master (Computer's Dream) | série | — | émulé |
| Zero-X, Virtuoso, SY77 SWS, X-Analyzer, SoundPool (clé + fichier licence) | cartouche | inconnu | aucun relevé |
| Zodiac (astrologie) | joystick 1 | LED clignotante | protocole inconnu |
| Dames Grand-Maître, Italy '90, Logistix, Scala, Striker Manager, Football Director 2 | joystick (Amiga) | résistances/condensateurs sur POTX/POTY | **Amiga seulement** (ports analogiques) — WinUAE |
| Robocop 3 | — | — | sur ST : protection disque + codes du manuel, pas de clé |
| NeoN Grafix 3D | LAN Falcon | boucle TX→RX dans un boîtier de starter | hors périmètre (Falcon) |
| DynaBlaster, Jeanne d'Arc, B.A.T. II, Leader Board, 10th Frame, Cricket Captain, Rugby Coach | joystick / série | voir ci-dessus | émulés sauf DynaBlaster (protocole inconnu) |

Sources : Steem SSE (`steem/headers/stports.h`, `ior.cpp`, `iow.cpp`, `ikbd.cpp`, `run.cpp`,
miroir github.com/mattiasgustavsson/steem-crt), WinUAE `dongle.cpp`, AtariForumWiki
« Dongle protections », atari-forum « Dongle Protections » (t=14437), « Notator Dongle Dump »
(t=43078, équations Notator dans le 1er message), « Cartridge keys and emulation »
(exxosforum t=2781), MiSTery `cubase*_dongle.v`, firmware `md-notator` (`rp/src/notator_dongle.c`).
