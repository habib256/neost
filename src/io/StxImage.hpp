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
#include <map>
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
        int      saveTrackIndex = -1;   // index dans saveTracks (overlay WRITE TRACK) ou -1
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

    // -------------------------------------------------------------------------
    //  Overlays d'écriture (WRITE SECTOR / WRITE TRACK). Persistés à l'éjection
    //  dans un fichier compagnon « <image>.wd1772 » (format Hatari, interop) et
    //  rechargés à l'insertion. Cf. STX_SaveStruct / STX_WriteDisk / STX_LoadSaveFile.
    // -------------------------------------------------------------------------

    // Overlay WRITE SECTOR : données réécrites pour un secteur, relues à la place
    // de l'original. Identifie le secteur par (Track, Side, BitPosition), comme
    // STX_FindSector_By_Position. On copie aussi le champ ID pour le bloc SECT du
    // .wd1772 (cf. STX_SAVE_SECTOR_STRUCT). `used` = false ⇒ entrée libérée par un
    // WRITE TRACK ultérieur (StructIsUsed == 0 chez Hatari : non écrite au fichier).
    struct SaveSector {
        uint8_t  track = 0, side = 0;
        uint16_t bitPos = 0;
        uint8_t  idTrack = 0, idHead = 0, idSector = 0, idSize = 0;
        uint16_t idCrc = 0;
        uint16_t sectorSize = 0;
        std::vector<uint8_t> data;
        bool     used = true;
    };
    std::vector<SaveSector> saveSectors;

    // Overlay WRITE TRACK : octets BRUTS de la piste réécrite (tels que la DMA les a
    // fournis, timings ignorés), exactement comme STX_SAVE_TRACK_STRUCT::pDataWrite.
    // C'est CE tampon qui est sérialisé dans le bloc TRCK du .wd1772 (interop Hatari).
    struct SaveTrack {
        uint8_t  track = 0, side = 0;
        std::vector<uint8_t> dataWrite;
    };
    std::vector<SaveTrack> saveTracks;

    // Secteur EXTRAIT du flux WRITE TRACK (champ ID + données 512 o). Hatari laisse
    // « pDataRead » à NULL (TODO non implémenté), donc une piste réécrite y revient
    // aux secteurs d'origine en lecture ; NeoST va plus loin et interprète le flux
    // MFM écrit (IDAM/DAM) pour que les lectures suivantes voient la piste réécrite.
    // Écart documenté p/r à Hatari (qui n'interprète pas pDataWrite).
    struct FormattedSector {
        uint8_t  idTrack = 0, idHead = 0, idSector = 0, idSize = 0;
        uint16_t idCrc = 0;              // CRC du champ ID (relu par READ ADDRESS)
        uint16_t sectorSize = 0;
        std::vector<uint8_t> data;
    };
    // Overlay de piste « formatée » dérivé du WRITE TRACK, clé = (track << 8) | side.
    // Consulté EN PREMIER par les chemins de lecture STX (avant l'overlay write-sector
    // et l'image d'origine). Reconstruit depuis saveTracks (parse du flux) au besoin.
    std::map<uint16_t, std::vector<FormattedSector>> formattedTracks;

    // Y a-t-il eu une écriture (secteur ou piste) ? Conditionne l'écriture du .wd1772.
    bool dirty() const;

    // Clé d'overlay de piste formatée.
    static uint16_t trackKey(int track, int side) { return uint16_t((track << 8) | (side & 1)); }
    // Renvoie l'overlay de piste formatée pour (track, side) ou nullptr.
    const std::vector<FormattedSector>* findFormatted(int track, int side) const;

    // Persistance compagnon « .wd1772 » (format Hatari : en-tête « WD1772 » + blocs
    // SECT/TRCK, champs multi-octets en BIG-endian). Renvoie false en cas d'erreur
    // d'E/S ou de format invalide. saveWd1772 ne crée rien s'il n'y a rien à écrire.
    bool saveWd1772(const std::string& path) const;
    bool loadWd1772(const std::string& path);

    // (Re)construit formattedTracks[key] en interprétant le flux MFM brut d'un
    // WRITE TRACK (IDAM $FE → ID, DAM $FB/$F8 → données). Cf. parse façon FDC .ST.
    // Appelé par le FDC après un WRITE TRACK et par loadWd1772 (bloc TRCK).
    void rebuildFormatted(int track, int side, const std::vector<uint8_t>& mfm) {
        buildFormattedFromWriteTrack(track, side, mfm);
    }

private:
    void buildFormattedFromWriteTrack(int track, int side, const std::vector<uint8_t>& mfm);
    std::vector<uint8_t> buf_;          // octets bruts (les pointeurs ci-dessus pointent dedans)
    std::vector<Track>   tracks_;
    uint16_t version_ = 0, imagingTool_ = 0;
    uint8_t  tracksCountHdr_ = 0, revision_ = 0;
    bool     valid_ = false;

    void buildSectorsSimple(Track& trk, const uint8_t* p);  // piste = N×512 o sans bloc secteur
};
