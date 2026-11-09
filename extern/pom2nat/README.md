# NAT intégré issu de POM2

Source : https://github.com/habib256/POM2
Révision : `b07f3a0a5ac699989be0e61c5f36369f5c1287a2` (2026-11-09).
Licence : GPL-3.0-or-later, © 2026 VERHILLE Arnaud (`LICENSE`).
Les références au dessin Casso @ `2d899adf` (MIT) sont conservées dans les sources.

Les fichiers de protocole, le résolveur et leurs tests sont importés sans
réécriture. Le raccord NeoST vit dans `src/net/NatBackend.*`.
Les commentaires anglais de cette copie amont sont conservés pour faciliter
les comparaisons et les mises à jour.

Adaptations locales :

- `Pom2Build.h` suit `NEOST_WITH_NET`, Android et Emscripten.
- `BuiltinNatBackend.cpp` marque l'ouverture invalide dès qu'une publication
  demandée ne peut pas prendre son port hôte. NeoST refuse alors le backend
  entier ; il ne prétend pas avoir appliqué une liste partielle.
- Les tests à base d'assertions sont compilés sans NDEBUG, même en Release.

Le port conserve les horloges hôte et les injections de tests ; il ne change
aucun cycle du ST. Le trafic réel est extérieur au contrat de déterminisme
et aux sauvegardes de la machine.

Validation distincte : les trames forgées et les sockets loopback ne prouvent
pas la compatibilité de toutes les piles Atari. STinG/ENEC, MiNTnet et un
échange entre deux ordinateurs doivent être qualifiés séparément.
