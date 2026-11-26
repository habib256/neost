// =============================================================================
//  Archive.hpp — Lire un .zip, un .7z et un .gz, sans bibliothèque système.
//
//  Les collections de jeux ST circulent zippées (TOSEC, sites de la scène), parfois
//  en .st.gz ou en .7z (productions récentes) ; un jeu GEMDOS (programme et fichiers)
//  presque toujours en .zip. Les frontends (cœur libretro, GUI) les ouvrent comme leur
//  contenu : ce module ne fait que lire — lister les membres d'une archive, en extraire
//  un, décompresser un gzip — et vérifie chaque CRC.
//
//  Décompression deflate : celle de stb_image (extern/stb, domaine public), compilée
//  en statique dans Archive.cpp pour ne pas heurter l'unité du GUI (StbImage.cpp).
//  Décodage 7z : le décodeur C du SDK LZMA (extern/lzma, 7-Zip 24.09, domaine public).
//  Logique pure, testée par neost-selftest.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
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
bool is7z(const std::vector<uint8_t>& data);

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

// Une archive à membres, zip ou 7z, derrière une seule interface. Un 7z se décode par
// bloc « solide » (plusieurs fichiers compressés d'un seul flux) : le lecteur garde le
// dernier bloc décodé, si bien qu'extraire tous les membres dans l'ordre ne décode
// chaque bloc qu'une fois. `data` doit survivre au lecteur. Un 7z chiffré, ou dont le
// contenu dépasse kMaxUnpacked, est refusé à l'ouverture.
struct Member {
    std::string name;            // chemin dans l'archive, séparateur '/'
    uint64_t    size = 0;
    bool        directory = false;
};

class Reader {
public:
    static constexpr uint64_t kMaxUnpacked = 256ull << 20;

    Reader();
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // Zip ou 7z, reconnu par sa signature. Faux si ni l'un ni l'autre, ou illisible.
    bool open(const std::vector<uint8_t>& data, std::string* err = nullptr);
    const std::vector<Member>& members() const { return members_; }
    // Contenu du membre `i` (CRC vérifié quand l'archive le porte).
    bool extract(std::size_t i, std::vector<uint8_t>& out, std::string* err = nullptr);

private:
    struct SevenZip;
    const std::vector<uint8_t>* data_ = nullptr;
    std::vector<Member> members_;
    std::vector<ZipMember> zip_;
    std::unique_ptr<SevenZip> sz_;
};

// Pour les autotests : deflate brut (sans en-tête zlib) → octets.
bool inflateRaw(const uint8_t* src, std::size_t n, std::vector<uint8_t>& out, std::size_t expected);

}  // namespace neost::archive
