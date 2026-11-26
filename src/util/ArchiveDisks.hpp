// =============================================================================
//  ArchiveDisks.hpp — Les disquettes d'une archive (.zip, .7z, .gz), extraites une fois.
//
//  Les collections de jeux ST circulent zippées (TOSEC, sites de la scène), parfois en
//  .st.gz ou en .7z. Le lecteur de disquette du cœur ne lit qu'un fichier image, qu'il
//  RÉÉCRIT quand le ST écrit (sauvegardes, scores) : on ne monte donc pas l'archive
//  elle-même, mais ses images, extraites dans un dossier propre à l'archive. Un fichier
//  déjà extrait n'est jamais réécrit : ce que le jeu a écrit survit d'une session à
//  l'autre, et l'archive d'origine n'est jamais modifiée.
//
//  Couche fichiers au-dessus d'util/Archive (qui ne fait que lire en mémoire). Servie
//  aux frontends qui montent par chemin (GUI de bureau, borne). Le cœur libretro garde
//  sa propre politique (listes .m3u, jeux GEMDOS zippés : docs/LIBRETRO.md).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace neost::archive {

// Une archive, à son extension : .zip, .7z, .gz (casse indifférente).
bool isArchiveName(const std::string& path);

// Une archive qui vaut la peine d'être proposée dans une ludothèque sans l'ouvrir :
// tout .zip et tout .7z, mais un .gz seulement s'il emballe une image (« jeu.st.gz ») —
// un .tar.gz ou un journal compressé n'est pas une disquette.
bool isFloppyArchiveName(const std::string& path);

// Dossier d'extraction d'une archive sous `cacheRoot` : « <nom sans extension>-<taille> ».
// La taille sépare deux archives homonymes de deux dossiers (« Disk1.zip » de deux jeux),
// qui sinon se partageraient leurs disquettes extraites ; elle se lit sans ouvrir le
// fichier (la ludothèque en a besoin pour chaque archive, à chaque rafraîchissement).
// Vide si l'archive est illisible.
std::string unpackDirFor(const std::string& archive, const std::string& cacheRoot);

// Extrait les images disquette (.st .msa .dim .stx .ipf) de `archive` sous `dir`, en
// gardant leurs sous-dossiers, et rend leurs chemins triés par nom (« Disk 1 » avant
// « Disk 2 »). Un fichier déjà présent est gardé tel quel. Faux si l'archive est
// illisible ou sans disquette ; `err` dit pourquoi, en anglais.
bool unpackFloppies(const std::string& archive, const std::string& dir,
                    std::vector<std::string>& disks, std::string* err = nullptr);

// Les images déjà extraites sous `dir`, triées comme unpackFloppies, sans relire
// l'archive. Vide si le dossier n'existe pas.
std::vector<std::string> unpackedFloppies(const std::string& dir);

}  // namespace neost::archive
