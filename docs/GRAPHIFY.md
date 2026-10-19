# Carte du code NeoST avec Graphify

Graphify indexe localement les fonctions, classes et relations du code avec un
parseur syntaxique (AST). Aucun modèle ni appel API n'a servi à cette génération.
Le graphe aide à explorer les dépendances et préparer une modification ; il ne
valide ni l'émulation ni l'exhaustivité des appels résolus.

## Consulter

- `../graphify-out/graph.html` : carte interactive (vue regroupée si le graphe est volumineux).
- `../graphify-out/GRAPH_REPORT.md` : rapport d'ensemble.
- `../graphify-out/wiki/index.md` : entrée du wiki par communautés.
- `../graphify-out/graph.json` : données interrogeables.

Depuis la racine du dépôt :

```sh
graphify query "Machine Bus Mfp" --budget 1500
graphify explain "src/core/Bus.hpp::Bus"
graphify path src_core_machine_machine src_core_bus_bus
graphify update .
graphify export wiki
```

Si la commande n'est pas dans le PATH, elle est installée dans
`/Users/gistair/.local/bin/graphify`.

## Périmètre et limites

Le code du projet, ses tests, ses outils et le cœur Moira modifié sont inclus.
Les autres bibliothèques tierces, `dev/`, les disques, ROM, images et polices sont
exclus. Les règles `.gitignore` sont respectées. Les exclusions supplémentaires
sont enregistrées dans `graphify-out/.graphify_build.json` et réutilisées lors
des mises à jour.

Les vues courantes et cette configuration sont versionnées. Les caches AST,
les sauvegardes datées et les métadonnées locales d'exécution sont ignorés
par `graphify-out/.gitignore`.

Les documents et images n'ont pas fait l'objet d'une extraction sémantique.
Le parseur a signalé six fichiers avec erreurs de syntaxe, notamment certains
en-têtes Moira : leurs symboles ou relations peuvent être incomplets. Certains
fichiers de données ou déclarations ne produisent aucun symbole. Les relations
inférées doivent être vérifiées dans les sources avant une décision technique.

Après une modification du code, lancer `graphify update .` (AST local), puis
`graphify export wiki` pour rafraîchir le wiki. Les fichiers générés ne sont pas
une nouvelle source de vérité matérielle : les références et méthodes de
`CLAUDE.md` et `DEV.md` restent applicables.
