// =============================================================================
//  FloppyCatalog.hpp — Ce qu'une disquette ST contient, et quoi y lancer.
//
//  Beaucoup de disquettes de jeux ne s'amorcent pas : le jeu est un programme que
//  l'on double-clique sur le bureau GEM. À la souris c'est un geste ; à la manette
//  d'un frontend (libretro, borne, téléphone), une corvée. Ce module lit le secteur
//  de boot et le catalogue FAT12 d'une image montée et dit s'il faut lancer un
//  programme, et lequel — ou rien, quand le choix n'est pas sûr.
//
//  Logique PURE : les secteurs viennent d'un lecteur fourni par l'appelant
//  (Fdc::readLogicalSector), si bien que les règles se testent dans neost-selftest
//  sur une image synthétique.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace neost {

struct FloppyEntry {
    std::string name;     // « GAME.PRG » (8.3, majuscules, sans espaces de remplissage)
    uint8_t     attr = 0; // attributs FAT ($10 = dossier, $08 = étiquette de volume)
    uint32_t    size = 0;
};

struct FloppyCatalog {
    bool bootable = false;               // secteur de boot exécutable (somme $1234)
    std::vector<FloppyEntry> root;       // répertoire racine
    std::vector<FloppyEntry> autoFolder; // contenu de \AUTO (vide sans dossier AUTO)
    // Dossiers de la racine (hors AUTO) et leur contenu, 16 au plus.
    std::vector<std::pair<std::string, std::vector<FloppyEntry>>> folders;
};

// Lit un secteur LOGIQUE de 512 octets ; faux s'il manque.
using SectorReader = std::function<bool(int lsn, uint8_t* out512)>;

// Faux si le secteur de boot manque ou si son BPB est incohérent (pas une disquette
// GEMDOS) ; `bootable` est alors tout de même renseigné quand le secteur a été lu.
bool readFloppyCatalog(const SectorReader& read, FloppyCatalog& out);

// Le programme à lancer depuis A:, « A:\GAME.PRG », ou vide. Le démarrage automatique
// part du BUREAU : un secteur de boot ou un programme de \AUTO qui lance le jeu ne rend
// jamais la main, et rien n'est lancé en double ; celui qui la rend (signature de
// cracker, anti-virus, pilote) laisse venir le bureau, qui lance alors le jeu. Ni le
// boot exécutable ni \AUTO n'empêchent donc le choix (corpus du 2026-11-25 : Pirates,
// Stormlords, Simulcra… portent un boot de cracker qui rend la main).
//  · les programmes de la racine (.PRG, .TOS, .APP ; pas .TTP, qui attend des
//    paramètres), utilitaires écartés (INSTALL, SETUP, README…) : s'il en reste UN, lui ;
//    plusieurs : celui dont le nom commence comme `hint` (nom de l'image), sinon vide ;
//  · aucun à la racine : l'unique programme d'un unique dossier qui en porte
//    (« A:\RICK_DAN.GER\RICKLOAD.TOS »).
std::string pickAutoStartProgram(const FloppyCatalog& cat, const std::string& hint = std::string());

}  // namespace neost
