# NAT intégré de NeoST

Ce NAT appartient à NeoST. Il est issu de POM2 (même auteur) et en reprend
les correctifs à la demande.

Origine : https://github.com/habib256/POM2
Dernière reprise : `4625ec82547118918bfafc795f6b7128cdb616ae` (2026-11-11).
Licence : GPL-3.0-or-later, © 2026 VERHILLE Arnaud (`LICENSE`).
Les références au dessin Casso @ `2d899adf` (MIT) sont conservées dans les sources.

Espaces de noms : `nat` (ex-`pom2` amont) et `nat::detail` (ex-`pom2::nat`).
Une reprise de correctifs POM2 applique ce renommage (`pom2::` → `nat::`,
`nat::` interne → `detail::`) avant la fusion à trois voies.

Les fichiers de protocole, le résolveur et leurs tests sont importés sans
autre réécriture. Le raccord NeoST vit dans `src/net/NatBackend.*` ; il appelle
`nicReset()` à la mise sous tension de la NE2000 (`Ne2000::powerCycle`), pas
au port reset du DP8390, comme POM2 pour le registre MR du W5100.
Les commentaires anglais de cette copie amont sont conservés pour faciliter
les comparaisons et les mises à jour.

Adaptations locales :

- `Pom2Build.h` suit `NEOST_WITH_NET`, Android et Emscripten.
- `BuiltinNatBackend.cpp` marque l'ouverture invalide dès qu'une publication
  demandée ne peut pas prendre son port hôte. NeoST refuse alors le backend
  entier ; il ne prétend pas avoir appliqué une liste partielle.
- Les tests à base d'assertions sont compilés sans NDEBUG, même en Release.
- `W5100NameResolver` (2026-11-12) : une adresse résolue expire aussi, après
  `kPositiveTtl` = 60 s, la durée que le DNS virtuel annonce à l'invité
  (`NatDns.cpp`, `kTtlSeconds`). En amont, elle ne périmait jamais : l'invité qui
  redemandait après 60 s recevait la première réponse pour toute la session.
  Test : `testSuccessExpires`.
- `BuiltinNat::nicReset` (2026-11-12) vide aussi le cache du résolveur
  (`clearCache`), que la mise sous tension de la carte laissait intact. Test : cas 5b
  de `builtin_nat_test.cpp` (compteur `clears`).

Ces deux défauts existent aussi dans POM2 à la reprise `4625ec82` ; ils sont à
remonter en amont, et une prochaine reprise doit garder ces lignes. Correctif
porté le 2026-11-12 sur la branche locale POM2 `nat-dns-cache-from-neost`
(non poussée, non fusionnée).

- `NatAllowlist.*` (2026-11-12, propre à NeoST) : liste blanche de domaines,
  OFF par défaut (`BuiltinNatOptions::allowedDomains`). `DnsProxy::setGate`
  répond REFUSED hors liste et apprend les adresses A/AAAA rendues ;
  `BuiltinNat::routePeer` refuse (`Route::Gated`) toute destination que le DNS
  n'a pas rendue pour un nom listé ; `nicReset` oublie ces adresses. Test : cas
  15 de `builtin_nat_test.cpp`.
- `builtin_nat_test.cpp` cas 13 (2026-11-12) : le port publié n'est retenu que si
  TCP ET UDP l'ont pris (`isValid()`). Le port libre venait d'un écouteur TCP seul ;
  sous Winsock, l'UDP du même numéro était tenu ailleurs (run CI 43225255791 :
  « could not bind host port 61684 », 31 s d'attente de « udp A »).

Le port conserve les horloges hôte et les injections de tests ; il ne change
aucun cycle du ST. Le trafic réel est extérieur au contrat de déterminisme
et aux sauvegardes de la machine.

Validation distincte : les trames forgées et les sockets loopback ne prouvent
pas la compatibilité de toutes les piles Atari. STinG/ENEC, MiNTnet et un
échange entre deux ordinateurs doivent être qualifiés séparément.
