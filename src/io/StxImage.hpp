// =============================================================================
//  StxImage.hpp — Conteneur STX (Pasti) : port d'extern/hatari/src/floppies/stx.c.
//
//  Une image .ST est « logique » (secteur = offset linéaire). Une STX décrit la
//  piste au niveau MFM réel, secteur par secteur, avec : champ ID véritable
//  (piste/face/secteur/taille/CRC, éventuellement NON standard), statut FDC par
//  secteur (RNF, CRC, record-type, lost-data), bits « fuzzy » (données différentes
//  à chaque lecture) et timing variable (vitesse de lecture par bloc de 16 o).
//  C'est ce qui permet d'émuler les PROTECTIONS (Dungeon Master, etc.).
//
//  Ce module ne fait QUE parser le conteneur en structures mémoire + retrouver une
//  piste/un secteur. Les opérations FDC (remplir le tampon de transfert avec fuzzy
//  + timing, statut) vivent dans Fdc.cpp, qui dispatche vers la STX quand l'image
//  montée en est une. Vérité matérielle : floppies/stx.c.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class StxImage {
public:
    // Drapeaux du statut FDC par secteur (bits 3/4/5 = registre de statut WD1772).
    enum : uint8_t {
        FLAG_VARIABLE_TIME = 1u << 0,   // timing variable (bloc de 16 o)
        FLAG_CRC           = 1u << 3,   // erreur CRC (avec RNF) ou record-type bas
        FLAG_LOST_DATA     = 1u << 3,
        FLAG_RNF           = 1u << 4,   // secteur sans données (Record Not Found)
        FLAG_RECORD_TYPE   = 1u << 5,   // « deleted data »
        FLAG_FUZZY         = 1u << 7,   // bits fuzzy (masque aléatoire)
    };

    // Un secteur (bloc 16 o de l'STX + variables internes). Les pointeurs visent les
    // octets bruts conservés dans buf_ (stable après parse) ou une table statique.
    struct Sector {
        uint32_t dataOffset  = 0;
        uint16_t bitPosition = 0;       // position angulaire (en BITS depuis l'index)
        uint16_t readTime    = 0;       // ms (0 = standard)
        uint8_t  idTrack = 0, idHead = 0, idSector = 0, idSize = 0;
        uint16_t idCrc = 0;
        uint8_t  fdcStatus = 0;
        uint16_t sectorSize = 0;        // octets (128 << (idSize & 3))
        const uint8_t* pData   = nullptr;  // données du secteur ou null si RNF
        const uint8_t* pFuzzy  = nullptr;  // masque fuzzy ou null
        const uint8_t* pTiming = nullptr;  // table de timing (2 o / 16 o) ou null
        int      saveIndex = -1;        // index dans saveSectors (overlay d'écriture) ou -1
    };

    // Une piste (bloc 16 o de l'STX + variables internes).
    struct Track {
        uint32_t blockSize = 0, fuzzySize = 0;
        uint16_t sectorsCount = 0, flags = 0, mfmSize = 0;
        uint8_t  trackNumber = 0, recordType = 0;   // trackNumber : bits0-6 piste, bit7 face
        std::vector<Sector> sectors;
        const uint8_t* pFuzzy        = nullptr;
        const uint8_t* pTrackData    = nullptr;
        uint16_t trackImageSyncPos = 0, trackImageSize = 0;
        const uint8_t* pTrackImage   = nullptr;     // image MFM brute de la piste (READ TRACK)
        const uint8_t* pSectorsImage = nullptr;
        const uint8_t* pTiming       = nullptr;
        uint16_t timingFlags = 0, timingSize = 0;
        const uint8_t* pTimingData   = nullptr;
        int saveTrackIndex = -1;        // index dans saveTracks (WRITE TRACK) ou -1
        // Durée RÉELLE d'un tour en octets MFM DD (32 µs), quand elle est connue
        // indépendamment du dump READ TRACK : pistes issues d'une IPF, dont le flux
        // de cellules donne le temps exact (densité variable comprise). 0 = déduite
        // du dump / de MFMSize comme pour une STX Pasti.
        uint16_t revolutionBytes = 0;

        // Réinterprétation d'une piste réécrite par WRITE TRACK : le flux brut
        // (saveTracks[saveTrackIndex].data) est PARSÉ en secteurs (IDAM $FE → champ
        // ID, DAM $FB/$F8 → données) pour que les LECTURES voient le nouveau contenu
        // et non plus l'original. C'est ce que Hatari laisse en TODO (« convert
        // pDataWrite into pDataRead »). writeData possède les octets ; chaque
        // writeSectors[i].pData y pointe (stable : tracks_ n'est jamais redimensionné).
        std::vector<Sector>               writeSectors;
        std::vector<std::vector<uint8_t>> writeData;
        uint16_t writeMfmSize       = 0;       // longueur du flux écrit (octets MFM)
        bool     writeReinterpreted = false;   // true ⇔ writeSectors fait foi

        // Vue ACTIVE des secteurs : la piste réinterprétée si présente, sinon ceux
        // d'origine. Le chemin FDC indexe TOUJOURS via ces accesseurs.
        std::vector<Sector>&       sectorsView()       { return writeReinterpreted ? writeSectors : sectors; }
        const std::vector<Sector>& sectorsView() const { return writeReinterpreted ? writeSectors : sectors; }
        int  sectorsCountView() const { return writeReinterpreted ? int(writeSectors.size()) : int(sectorsCount); }
    };

    // Drapeaux de piste.
    enum : uint16_t { TRACK_FLAG_SECTOR_BLOCK = 1u << 0,
                      TRACK_FLAG_IMAGE        = 1u << 6,
                      TRACK_FLAG_IMAGE_SYNC   = 1u << 7 };

    // Parse les octets bruts (DÉPLACÉS dans l'objet). Renvoie false si non valide.
    bool parse(std::vector<uint8_t> raw);
    bool valid() const { return valid_; }

    int  revision() const { return revision_; }
    int  tracksCount() const { return int(tracks_.size()); }
    int  sides() const;                 // 1 ou 2 (max face + 1)
    int  tracksPerSide() const;         // nombre de pistes par face
    Track* findTrack(int track, int side);

    // Overlay d'écriture (cf. Hatari STX_SaveStruct) : une STX est en lecture seule ;
    // les 'write sector' et 'write track' vont dans ces structures, relues en
    // priorité, et PERSISTÉES dans un fichier compagnon « .wd1772 » (format
    // compatible avec le conteneur Hatari : blocs SECT/TRCK, multi-octets BE,
    // trailer NDAM optionnel pour la DAM (ignoré et perdu si réécrit par Hatari)).
    struct SaveSector {
        uint8_t  track = 0, side = 0;
        uint16_t bitPos = 0;
        uint8_t  idTrack = 0, idHead = 0, idSector = 0, idSize = 0;  // champ ID (bloc SECT)
        uint16_t idCrc = 0;
        bool     used = true;           // false = invalidé par un WRITE TRACK ultérieur
        uint8_t  dataMark = 0xFB;       // DAM du WRITE SECTOR ; ancien SECT sans NDAM = FB
        std::vector<uint8_t> data;
    };
    // Piste réécrite par WRITE TRACK : flux BRUT écrit par le programme (timings
    // ignorés), conservé tel quel pour la persistance .wd1772. Contrairement à
    // Hatari (qui laisse la ré-interprétation en TODO), NeoST PARSE ce flux en
    // secteurs lisibles (Track::writeSectors) → les lectures voient le nouveau
    // contenu. Le flux brut reste la source de vérité persistée.
    struct SaveTrack {
        uint8_t  track = 0, side = 0;
        std::vector<uint8_t> data;
    };
    std::vector<SaveSector> saveSectors;
    std::vector<SaveTrack>  saveTracks;

    // Persistance .wd1772 (cf. Hatari STX_WriteDisk / STX_LoadSaveFile). save :
    // n'écrit RIEN s'il n'y a aucun overlay ; load : associe chaque bloc SECT à son
    // secteur (par piste/face/bitPosition) et chaque TRCK à sa piste.
    bool saveWd1772(const std::string& path) const;
    bool loadWd1772(const std::string& path);
    Sector* findSectorByPosition(int track, int side, uint16_t bitPos);

    // Réinterprète le flux WRITE TRACK associé à `t` (saveTracks[t.saveTrackIndex])
    // en secteurs lisibles (cf. Track::writeSectors). Appelé juste après un WRITE
    // TRACK et à chaque chargement d'un fichier compagnon .wd1772.
    void reinterpretSaveTrack(Track& t);

private:
    std::vector<uint8_t> buf_;          // octets bruts (les pointeurs ci-dessus pointent dedans)
    std::vector<Track>   tracks_;
    uint16_t version_ = 0, imagingTool_ = 0;
    uint8_t  tracksCountHdr_ = 0, revision_ = 0;
    bool     valid_ = false;

    void buildSectorsSimple(Track& trk, const uint8_t* p);  // piste = N×512 o sans bloc secteur
};
