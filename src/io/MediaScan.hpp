// =============================================================================
//  MediaScan.hpp — Recensement des images montables et TRI PAR PROXIMITÉ.
//
//  Extrait du menu borne (main.cpp), où il vivait sous le nom `kioskScanDisks`,
//  pour être partagé avec le frontend Android : c'est le MODÈLE de la ludothèque
//  (que scanner, dans quel ordre présenter), indépendant de qui le dessine.
//
//  Le tri est ce qui fait la différence à l'usage : les SUITES du jeu monté
//  (face A/B, « Disk 1 of 2 », phases B/C/D) remontent en tête, puis les noms
//  proches, puis l'alphabétique. Sur une borne comme sur un téléphone, on cherche
//  presque toujours la disquette suivante du jeu en cours.
//
//  Le parcours est BORNÉ (profondeur, entrées, temps) : il tourne dans le thread
//  d'interface, et un dossier vaste y figerait l'écran — mesuré à 4,6 s sur un
//  /home de 2,7 M d'entrées, à une touche d'un raccourci du menu borne.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace neost {

// Longueur du préfixe commun, insensible à la casse.
std::size_t commonPrefixLenCI(const std::string& a, const std::string& b);

// Deux noms de fichier sont-ils des SUITES du même jeu ? Vrai si les noms sont
// identiques SAUF un court jeton central (long préfixe commun + long suffixe
// commun, écart au milieu borné). Rejette « Space Harrier » / « Space Crusade ».
// `a` et `b` sont supposés déjà en MINUSCULES.
bool areSiblingImages(const std::string& a, const std::string& b);

// Images montables (.st/.msa/.dim/.stx/.ipf) trouvées récursivement dans `dirs`
// (dédupliquées), triées par proximité au chemin `mounted` (vide = alphabétique).
std::vector<std::string> scanDiskImages(const std::vector<std::string>& dirs,
                                        const std::string& mounted);

}  // namespace neost
