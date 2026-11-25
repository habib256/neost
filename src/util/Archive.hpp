// =============================================================================
//  Archive.hpp — Lire un .zip et un .gz, sans bibliothèque.
//
//  Les collections de jeux ST circulent zippées (TOSEC, sites de la scène), parfois
//  en .st.gz ; un jeu GEMDOS (programme et fichiers) presque toujours en .zip. Les
//  frontends (cœur libretro, GUI) les ouvrent comme leur contenu : ce module ne fait
//  que lire — lister les membres d'un zip, en extraire un, décompresser un gzip — et
//  vérifie chaque CRC.
//
//  Décompression deflate : celle de stb_image (extern/stb, domaine public), compilée
//  en statique dans Archive.cpp pour ne pas heurter l'unité du GUI (StbImage.cpp).
//  Logique pure, testée par neost-selftest.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neost::archive {

struct ZipMember {
    std::string name;            // chemin dans l'archive, séparateur '/'
    uint16_t    method = 0;      // 0 stocké, 8 deflate (les seuls lus)
    uint32_t    crc = 0;
    uint32_t    compressedSize = 0, size = 0;
    uint32_t    localHeader = 0; // position de l'en-tête local
    bool        directory() const { return !name.empty() && name.back() == '/'; }
};

bool isZip(const std::vector<uint8_t>& data);
bool isGzip(const std::vector<uint8_t>& data);

// Membres d'un zip, par son répertoire central. Faux si l'archive est illisible
// (zip64, multi-volumes : refusés). `err` dit pourquoi, en anglais.
bool listZip(const std::vector<uint8_t>& data, std::vector<ZipMember>& out, std::string* err = nullptr);

// Contenu d'un membre (stocké ou deflate), CRC vérifié.
bool extractZip(const std::vector<uint8_t>& data, const ZipMember& m, std::vector<uint8_t>& out,
                std::string* err = nullptr);

// Contenu d'un gzip (un seul membre), CRC et taille vérifiés ; `name` reçoit le nom
// d'origine si l'en-tête le porte (FNAME), sinon vide.
bool gunzip(const std::vector<uint8_t>& data, std::vector<uint8_t>& out, std::string* name = nullptr,
            std::string* err = nullptr);

// Un chemin de membre sans danger à écrire sous un dossier : relatif, sans « .. »,
// sans lecteur ni '\'. Vide s'il faut le refuser.
std::string safeMemberPath(const std::string& name);

// Pour les autotests : deflate brut (sans en-tête zlib) → octets.
bool inflateRaw(const uint8_t* src, std::size_t n, std::vector<uint8_t>& out, std::size_t expected);

}  // namespace neost::archive
