# Catalogue des disquettes de compilation (CompDisks.json)

Catalogue de **4 515 disquettes de compilation** de la scène Atari ST, par équipe (Automation,
Pompey Pirates, Medway Boys, D-Bug…) : pour chacune, le nom de fichier attendu (`File`), l'équipe,
les jeux qu'elle contient (`Games`) et la clé de sa capture de menu (`Screenshot`, non fournie).

- **Source** : <https://github.com/dimfilhub/Atari-Compilation-Disks>, commit
  `d70d6193e1970d65bd72b3c5dccdfd0028ec02c3`, fichier `CompDisks.json`.
- **SHA-256** : `abdda05eb7440aca414b41e5d986bc4497674a44530440edf25e481feb41427b`.
- **Licence** : MIT, © 2026 Dimitris Filios — texte complet dans `LICENSE`, à joindre à tout
  paquet qui embarquerait ce fichier (et à nommer dans `packaging/licenses/THIRD-PARTY.txt`).
- **Aucune image disque** : seulement des noms et des listes de jeux.

Lu par `src/io/CompDisks.cpp` (2026-11-11) : la page Floppy et la borne affichent l'équipe, le numéro
et les jeux d'une disquette reconnue. Embarqué dans les paquets avec sa licence.
