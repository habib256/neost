// =============================================================================
//  CompDisks.hpp — Nommer une disquette de compilation de la scène (menus).
//
//  POURQUOI. La ludothèque et la borne listent des noms de fichiers : « fuzcd077.msa »
//  ne dit pas qu'il s'agit du disque 77 de Fuzion, avec Darkman et Puzzmania. Le
//  catalogue CompDisks (data/compdisks/CompDisks.json, MIT, 4 515 disquettes) donne
//  pour chacune l'équipe, le nom et les jeux ; ce module le lit et retrouve une image.
//
//  COMMENT. Le JSON est un objet de disquettes, chacune un objet de chaînes (Games,
//  File, Team…) : un lecteur limité à cette forme suffit, sans dépendance. La
//  correspondance se fait en deux temps :
//    1. le nom de fichier exact du catalogue, sans casse ni extension (PP_001) ;
//    2. sinon le même NUMÉRO de disque, avec un préfixe alphabétique compatible
//       (« fuz » ⊂ « fuzcd ») ou le nom de l'équipe (« automation123 »).
//  Plusieurs candidats → aucun : mieux vaut ne rien afficher qu'une étiquette fausse.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace neost {

struct CompDiskInfo {
    std::string name;    // « Fuzion Disk 77 »
    std::string team;    // « Fuzion »
    std::string games;   // « Darkman, International Championship Athletics, Puzzmania »
};

class CompDiskCatalog {
public:
    // Charge un catalogue depuis un fichier ; faux s'il est absent ou illisible.
    bool load(const std::string& path);
    // Charge depuis un texte JSON (tests) ; faux si la forme attendue manque.
    bool loadFromText(const std::string& json);
    // L'entrée qui correspond à une image (chemin ou nom de fichier), ou nullptr.
    const CompDiskInfo* find(const std::string& imagePath) const;
    std::size_t size() const { return entries_.size(); }

private:
    struct Key { std::string letters; int number; };
    std::vector<CompDiskInfo> entries_;
    std::unordered_map<std::string, std::size_t> byStem_;   // nom exact (minuscules, sans extension)
    std::vector<Key> fileKeys_, teamKeys_;                  // par entrée : préfixe et numéro
    std::unordered_map<int, std::vector<std::size_t>> byNumber_;
};

// Décompose « fuzcd077 » en lettres « fuzcd » et numéro 77 (dernier groupe de chiffres) ;
// numéro −1 s'il n'y en a pas. Exposé pour les tests.
void splitDiskName(const std::string& stem, std::string& letters, int& number);

}  // namespace neost
