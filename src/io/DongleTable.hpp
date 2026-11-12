// =============================================================================
//  DongleTable — quelle clé brancher pour quelle disquette (disks/dongles.txt).
// =============================================================================
//
//  La ludothèque n'a pas de manifeste : c'est un scan de fichiers. Pour que la borne
//  (et l'utilisateur pressé) n'ait pas à ouvrir la page Dongles avant Leader Board,
//  une table texte associe un MOTIF (sous-chaîne du nom d'image, insensible à la
//  casse) à un branchement :
//
//      # motif = port:périphérique     (ports : joy0 joy1 rs232 printer cartbutton)
//      # motif = cart:modèle           (modèles : cubase2 cubase3 auto notator)
//      leader board = joy1:leaderboard
//      notator      = cart:notator
//
//  Politique : on ne remplit que les emplacements VIDES — un réglage explicite de
//  l'utilisateur n'est jamais écrasé — et on le dit (message d'état). Logique pure,
//  testée par neost-selftest ; l'application des branchements reste au frontend.
// =============================================================================
#pragma once
#include <string>
#include <vector>
#include "io/PortDevices.hpp"
#include "io/CartridgeKey.hpp"

namespace neost {

struct DongleRule {
    std::string pattern;                       // minuscules
    bool cart = false;                         // cart:… (sinon port:…)
    PortDevices::Port   port = PortDevices::Port::Joy0;
    PortDevices::Device dev  = PortDevices::Device::None;
    CartridgeKey::Model key  = CartridgeKey::Model::None;
};

// Analyse le contenu de la table (lignes `motif = cible`, `#` commentaire). Les lignes
// invalides sont ignorées ; `bad` (optionnel) reçoit leur nombre.
std::vector<DongleRule> parseDongleTable(const std::string& text, int* bad = nullptr);

// Règles dont le motif apparaît dans le NOM DE FICHIER de `imagePath` (insensible à la casse).
std::vector<DongleRule> matchDongleRules(const std::vector<DongleRule>& rules, const std::string& imagePath);

// Table livrée : les titres à clé connus (cf. docs/EXTENSIONS.md). Écrite dans
// disks/dongles.txt si le fichier n'existe pas.
const char* defaultDongleTable();

// --- Branchements choisis à la main (page Dongles des frontends) ---------------
// Le bureau les range dans neost.cfg (dongle=, joy0=…) ; le web et Android n'ont pas
// de neost.cfg : ils partagent ce petit format, une ligne `emplacement=identifiant`
// avec les MÊMES identifiants que neost.cfg et dongles.txt :
//
//      cart=cubase3
//      joy1=leaderboard
//
// Emplacement absent = vide. Un identifiant inconnu, ou un périphérique qui n'entre
// pas dans ce port, est ignoré (jamais d'erreur : un fichier abîmé ne doit pas
// empêcher la machine de démarrer).
struct DongleSetup {
    CartridgeKey::Model key = CartridgeKey::Model::None;
    PortDevices::Device dev[std::size_t(PortDevices::Port::Count)] = {};
};
std::string formatDongleSetup(const DongleSetup& s);
DongleSetup parseDongleSetup(const std::string& text);

// Identifiants et libellés des clés cartouche, dans l'ordre de CartridgeKey::Model
// (identifiants de neost.cfg `dongle=` et de dongles.txt `cart:`).
constexpr int kCartKeyModelCount = 5;
const char* cartKeyId(CartridgeKey::Model m);          // "" pour None
const char* cartKeyLabel(CartridgeKey::Model m);
CartridgeKey::Model cartKeyFromId(const std::string& id);   // None si inconnu

}  // namespace neost
