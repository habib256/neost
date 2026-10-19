# NeoST

Lire `CLAUDE.md` pour les conventions, les références et la méthode du projet.

## Objectif matériel

NeoST doit reproduire le comportement d'un véritable Atari ST et des variantes
prises en charge. Le matériel réel prime sur Hatari : une erreur confirmée
d'Hatari ne doit pas être portée dans NeoST. Corriger NeoST selon les preuves
matérielles disponibles et documenter la divergence. Une concordance entre
émulateurs ne suffit pas à établir l'exactitude matérielle ; si les preuves
manquent, conserver explicitement l'incertitude.

## Graphify

Une carte locale du code est disponible dans `graphify-out/`.
- Pour une question ciblée, commencer par `graphify query`, `graphify explain`
  ou `graphify path`, puis vérifier les sources indiquées.
- Pour une vue générale, consulter `graphify-out/GRAPH_REPORT.md`.
- Si `graphify-out/wiki/index.md` existe, utiliser cet index pour naviguer.
- Après modification du code, lancer `graphify update .`, puis
  `graphify export wiki` pour garder les vues à jour (sans appel API).
- Ne pas utiliser le graphe comme preuve de sa propre exactitude ou de
  l'exactitude matérielle de l'émulation.

Périmètre et commandes : `docs/GRAPHIFY.md`.
