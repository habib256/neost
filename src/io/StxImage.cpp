// =============================================================================
//  StxImage.cpp — parseur du conteneur STX (Pasti). Port de STX_BuildStruct
//  (extern/hatari/src/floppies/stx.c). Voir StxImage.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <filesystem>
#include "io/StxImage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

// --- Lectures little-endian (le conteneur STX est LE ; seul l'ID_CRC est BE) ----
static inline uint16_t rd16le(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
static inline uint32_t rd32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// --- Constantes de disposition de piste (cf. Hatari fdc.h) ---------------------
static constexpr int GAP1 = 60, GAP2 = 12, GAP3a = 22, GAP3b = 12, GAP4 = 40;
static constexpr int RAW_SECTOR_512 = GAP2 + 3 + 1 + 6 + GAP3a + GAP3b + 3 + 1 + 512 + 2 + GAP4; // 614
static constexpr uint8_t SECTOR_SIZE_512 = 2;
static constexpr uint8_t SECTOR_SIZE_MASK = 0x03;

// Table de timing fixe pour les secteurs « variable » des STX révision 0 (64 o, soit
// 2 o par bloc de 16 o sur un secteur de 512 o). Cf. stx.c:TimingDataDefault.
static const uint8_t TimingDataDefault[] = {
    0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,
    0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,0x00,0x85,
    0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,0x00,0x79,
    0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f,0x00,0x7f
};

// CRC16-CCITT (poly 0x1021, init 0xFFFF) du WD1772, pour les champs ID synthétisés.
static uint16_t crc16(std::initializer_list<uint8_t> bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : bytes) {
        crc ^= uint16_t(b) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

// Piste « simple » : SectorsCount secteurs de 512 o sans bloc d'info (drapeau
// SECTOR_BLOCK absent) → on synthétise des champs ID standard et des positions.
// Cf. STX_BuildSectorsSimple. `p` pointe le début des données (N×512 o).
void StxImage::buildSectorsSimple(Track& trk, const uint8_t* p) {
    int bytePos = GAP1 + GAP2 + 4;   // Pasti pointe juste après 3×$A1 + IDAM $FE
    trk.sectors.resize(trk.sectorsCount);
    for (int s = 0; s < trk.sectorsCount; ++s) {
        Sector& sec = trk.sectors[s];
        sec.saveIndex   = -1;
        sec.dataOffset  = 0;
        // Écrêté (pas de wrap uint16) : une piste HD/ED simple dépasse 65535 bits —
        // le wrap inversait l'ordre angulaire (nextSectorIDStx suppose croissant).
        sec.bitPosition = uint16_t(std::min(bytePos * 8, 0xFFFF));
        sec.readTime    = 0;
        sec.idTrack  = uint8_t(trk.trackNumber & 0x7f);
        sec.idHead   = uint8_t((trk.trackNumber >> 7) & 0x01);
        sec.idSector = uint8_t(s + 1);
        sec.idSize   = SECTOR_SIZE_512;
        sec.idCrc    = crc16({ 0xa1, 0xa1, 0xa1, 0xfe, sec.idTrack, sec.idHead, sec.idSector, sec.idSize });
        sec.fdcStatus  = 0;
        sec.sectorSize = uint16_t(128 << sec.idSize);
        sec.pData      = p + s * 512;
        bytePos += RAW_SECTOR_512;
    }
}

bool StxImage::parse(std::vector<uint8_t> raw) {
    buf_ = std::move(raw);
    valid_ = false;
    tracks_.clear();
    if (buf_.size() < 16) return false;

    const uint8_t* const base = buf_.data();
    const uint8_t* const end  = base + buf_.size();
    // Plafonne une taille d'image de piste annoncée par le fichier sur ce qui reste
    // du tampon à partir de `img` (cf. les deux branches TRACK_FLAG_IMAGE plus bas).
    auto clampImage = [](uint16_t sz, const uint8_t* img, const uint8_t* e) -> uint16_t {
        const std::size_t avail = (img <= e) ? std::size_t(e - img) : 0u;
        return uint16_t(avail < std::size_t(sz) ? avail : std::size_t(sz));
    };

    // En-tête (16 o).
    if (std::memcmp(base, "RSY\0", 4) != 0) return false;
    version_         = rd16le(base + 4);
    imagingTool_     = rd16le(base + 6);
    tracksCountHdr_  = base[10];
    revision_        = base[11];

    const uint8_t* p = base + 16;
    tracks_.reserve(tracksCountHdr_);

    for (int t = 0; t < tracksCountHdr_; ++t) {
        if (16u > std::size_t(end - p)) return false;
        const uint8_t* p_cur = p;

        tracks_.emplace_back();
        Track& trk = tracks_.back();
        trk.blockSize    = rd32le(p);      p += 4;
        trk.fuzzySize    = rd32le(p);      p += 4;
        trk.sectorsCount = rd16le(p);      p += 2;
        trk.flags        = rd16le(p);      p += 2;
        trk.mfmSize      = rd16le(p);      p += 2;
        trk.trackNumber  = *p++;
        trk.recordType   = *p++;

        // Valider la taille par soustraction AVANT de former le pointeur de fin :
        // un BlockSize hostile ne doit jamais produire un pointeur hors tableau.
        if (trk.blockSize < 16 || trk.blockSize > std::size_t(end - p_cur)) return false;
        const uint8_t* next = p_cur + trk.blockSize;

        bool simple = false;
        if (trk.sectorsCount > 0 && (trk.flags & TRACK_FLAG_SECTOR_BLOCK) == 0) {
            // Piste = SectorsCount secteurs de 512 o, données juste après l'en-tête.
            // Invariant : « sectorsCount == sectors.size() toujours » — le FDC
            // (nextSectorIDStx/readSectorStx) borne ses parcours sur sectorsCount
            // mais indexe sectors[] : toute désynchronisation = accès hors borne.
            const std::size_t dataBytes = std::size_t(trk.sectorsCount) * 512u;
            if (dataBytes <= std::size_t(next - p))
                buildSectorsSimple(trk, p);       // resize(sectorsCount) → invariant tenu
            else
                trk.sectorsCount = 0;             // image tronquée → piste vide cohérente
            simple = true;
        }

        if (!simple) {
            // Zones optionnelles fuzzy / image de piste.
            const std::size_t sectorInfoBytes = std::size_t(trk.sectorsCount) * 16u;
            if (sectorInfoBytes > std::size_t(next - p)) return false;
            trk.pFuzzy = p + sectorInfoBytes;             // après les blocs secteur
            if (trk.fuzzySize > std::size_t(next - trk.pFuzzy)) return false;
            trk.pTrackData = trk.pFuzzy + trk.fuzzySize;

            if ((trk.flags & TRACK_FLAG_IMAGE) == 0) {
                trk.pTrackImage   = nullptr;
                trk.pSectorsImage = trk.pTrackData;
            // TrackImageSize vient du FICHIER (0..65535) : le plafonner sur ce qui
            // reste réellement du tampon. Sans ça, une .stx tronquée fait lire
            // Fdc::readTrackStx jusqu'à 64 Ko au-delà du tas (la boucle n'est bornée
            // que par trackImageSize). Hatari ne borne pas non plus (stx.c:1095) mais
            // NeoST borne tous les autres champs — c'était le dernier trou. On
            // TRONQUE plutôt qu'on ne rejette (esprit du clamp de msa.c:205) : une
            // image partiellement valide reste exploitable.
            } else if ((trk.flags & TRACK_FLAG_IMAGE_SYNC) == 0) {
                if (2u <= std::size_t(next - trk.pTrackData)) {
                    trk.trackImageSize = clampImage(rd16le(trk.pTrackData), trk.pTrackData + 2, next);
                    trk.pTrackImage    = trk.trackImageSize ? trk.pTrackData + 2 : nullptr;
                    trk.pSectorsImage  = trk.pTrackData + 2 + trk.trackImageSize;
                } else return false;
            } else {
                if (4u <= std::size_t(next - trk.pTrackData)) {
                    trk.trackImageSyncPos = rd16le(trk.pTrackData);
                    trk.trackImageSize    = clampImage(rd16le(trk.pTrackData + 2), trk.pTrackData + 4, next);
                    trk.pTrackImage       = trk.trackImageSize ? trk.pTrackData + 4 : nullptr;
                    trk.pSectorsImage     = trk.pTrackData + 4 + trk.trackImageSize;
                } else return false;
            }

            if (trk.sectorsCount > 0) {
                trk.sectors.resize(trk.sectorsCount);
                const uint8_t* pFuzzy = trk.pFuzzy;
                bool variableTimings = false;
                uint32_t maxOffsetEnd = 0;

                for (int s = 0; s < trk.sectorsCount; ++s) {
                    if (16u > std::size_t(next - p)) return false;
                    Sector& sec = trk.sectors[s];
                    sec.dataOffset  = rd32le(p);            p += 4;
                    sec.bitPosition = rd16le(p);            p += 2;
                    sec.readTime    = rd16le(p);            p += 2;
                    sec.idTrack     = *p++;
                    sec.idHead      = *p++;
                    sec.idSector    = *p++;
                    sec.idSize      = *p++;
                    sec.idCrc       = uint16_t((p[0] << 8) | p[1]); p += 2;   // ID_CRC en BIG-endian
                    sec.fdcStatus   = *p++;
                    /* reserved */    p++;
                    sec.saveIndex   = -1;

                    if ((sec.fdcStatus & FLAG_RNF) == 0) {
                        sec.sectorSize = uint16_t(128 << (sec.idSize & SECTOR_SIZE_MASK));
                        if (sec.dataOffset <= std::size_t(next - trk.pTrackData)
                            && sec.sectorSize <= std::size_t(next - trk.pTrackData) - sec.dataOffset)
                            sec.pData = trk.pTrackData + sec.dataOffset;
                        if (sec.fdcStatus & FLAG_FUZZY) {
                            if (sec.sectorSize > std::size_t(next - pFuzzy)) return false;
                            sec.pFuzzy = pFuzzy;
                            pFuzzy += sec.sectorSize;
                        }
                        if (sec.pData && sec.dataOffset + sec.sectorSize > maxOffsetEnd)
                            maxOffsetEnd = sec.dataOffset + sec.sectorSize;
                        if (sec.fdcStatus & FLAG_VARIABLE_TIME) variableTimings = true;
                    }
                }

                // Table de timing (après l'image des secteurs).
                trk.pTiming = trk.pTrackData + maxOffsetEnd;
                if (trk.pTiming < trk.pSectorsImage) trk.pTiming = trk.pSectorsImage;

                if (variableTimings) {
                    if (revision_ == 2 && 4u <= std::size_t(next - trk.pTiming)) {
                        trk.timingFlags = rd16le(trk.pTiming);
                        trk.timingSize  = rd16le(trk.pTiming + 2);
                        trk.pTimingData = trk.pTiming + 4;
                    }
                    const uint8_t* pTim = trk.pTimingData;
                    for (int s = 0; s < trk.sectorsCount; ++s) {
                        Sector& sec = trk.sectors[s];
                        sec.pTiming = nullptr;
                        if ((sec.fdcStatus & FLAG_RNF) == 0 && (sec.fdcStatus & FLAG_VARIABLE_TIME)) {
                            if (revision_ == 2) {
                                const std::size_t timingBytes = (sec.sectorSize / 16) * 2u;
                                if (pTim && timingBytes <= std::size_t(next - pTim)) {
                                    sec.pTiming = pTim;
                                    pTim += timingBytes;
                                } else {
                                    pTim = nullptr;
                                }
                            } else if (sec.sectorSize <= 512) {
                                // Table fixe révision 0 : 64 octets = 32 blocs de 16 o,
                                // soit un secteur de 512 o max. Un .stx forgé rév. 0 avec
                                // un secteur de 1024 o à timing variable la ferait déborder
                                // (lecture statique hors bornes) → timing uniforme à la place.
                                sec.pTiming = TimingDataDefault;
                            }
                        }
                    }
                }
            }
        }

        p = next;   // bloc de piste suivant
    }

    valid_ = !tracks_.empty();
    return valid_;
}

int StxImage::sides() const {
    int s = 1;
    for (const Track& t : tracks_)
        if ((t.trackNumber >> 7) & 0x01) { s = 2; break; }
    return s;
}

int StxImage::tracksPerSide() const {
    int maxTrack = 0;
    for (const Track& t : tracks_) {
        const int tr = t.trackNumber & 0x7f;
        if (tr > maxTrack) maxTrack = tr;
    }
    return maxTrack + 1;
}

StxImage::Track* StxImage::findTrack(int track, int side) {
    const uint8_t want = uint8_t((track & 0x7f) | (side << 7));
    for (Track& t : tracks_)
        if (t.trackNumber == want) return &t;
    return nullptr;
}

// Retrouve un secteur par sa position angulaire — c'est la clé d'association des
// blocs SECT d'un fichier .wd1772 (cf. Hatari STX_FindSector_By_Position).
StxImage::Sector* StxImage::findSectorByPosition(int track, int side, uint16_t bitPos) {
    Track* t = findTrack(track, side);
    if (!t) return nullptr;
    // sectorsView() et non t->sectors : sur une piste déjà réinterprétée par un
    // WRITE TRACK, les positions angulaires courantes sont celles de writeSectors —
    // chercher dans les secteurs d'origine ne trouvait rien et l'overlay était jeté.
    for (Sector& sec : t->sectorsView())
        if (sec.bitPosition == bitPos) return &sec;
    return nullptr;
}

// =============================================================================
//  Ré-interprétation d'une piste réécrite par WRITE TRACK (au-delà de Hatari, qui
//  laisse « convert pDataWrite into pDataRead » en TODO). Le flux brut écrit par le
//  programme est parcouru pour en extraire les secteurs : on cherche chaque IDAM
//  ($FE) → champ ID (piste/face/secteur/taille), puis la première DAM ($FB normal /
//  $F8 « deleted ») → données (128 << (taille & 3) octets). On tolère un sync $A1
//  comme $F5 (on ne s'appuie que sur $FE/$FB, comme l'extracteur .ST éprouvé).
//
//  Les secteurs ainsi reconstruits remplacent ceux d'origine pour la LECTURE
//  (Track::sectorsView). La position angulaire (BitPosition, en bits DD) vient de
//  l'offset de l'octet ID dans le flux. La CRC est ré-générée ; la marque de
//  données FB/F8 reste représentée par le bit RECORD TYPE du secteur.
// =============================================================================
void StxImage::reinterpretSaveTrack(Track& t) {
    t.writeReinterpreted = false;
    t.writeSectors.clear();
    t.writeData.clear();
    t.writeMfmSize = 0;
    if (t.saveTrackIndex < 0 || t.saveTrackIndex >= int(saveTracks.size())) return;

    const std::vector<uint8_t>& flux = saveTracks[t.saveTrackIndex].data;
    const int n = int(flux.size());
    t.writeMfmSize = uint16_t(n);

    struct Parsed { uint8_t tr, hd, sr, sz; int fePos, dataPos, dataLen; bool deleted; };
    std::vector<Parsed> found;
    for (int i = 0; i + 5 < n; ) {
        if (flux[i] != 0xFE) { ++i; continue; }              // IDAM : champ d'adresse
        const uint8_t tr = flux[i + 1], hd = flux[i + 2], sr = flux[i + 3], sz = flux[i + 4];
        const int dataLen = 128 << (sz & 0x03);
        int k = i + 5;                                       // cherche la marque de données
        while (k < n && flux[k] != 0xFB && flux[k] != 0xF8) ++k;
        if (k >= n || k + 1 + dataLen > n) { ++i; continue; }// DAM/données incomplètes → on passe
        found.push_back({ tr, hd, sr, sz, i, k + 1, dataLen, flux[k] == 0xF8 });
        i = k + 1 + dataLen;
    }
    // Un WRITE TRACK sans ID exploitable a effacé les anciens secteurs.
    // La vue vide doit faire foi aussi au rechargement du bloc TRCK .wd1772,
    // sinon les lectures ressuscitent silencieusement le contenu d'origine.
    t.writeReinterpreted = true;
    if (found.empty()) return;          // piste non formatée, vue active vide

    t.writeData.resize(found.size());
    t.writeSectors.resize(found.size());
    for (std::size_t s = 0; s < found.size(); ++s) {
        const Parsed& f = found[s];
        t.writeData[s].assign(flux.begin() + f.dataPos, flux.begin() + f.dataPos + f.dataLen);
        Sector& sec   = t.writeSectors[s];
        sec.idTrack   = f.tr; sec.idHead = f.hd; sec.idSector = f.sr; sec.idSize = f.sz;
        sec.idCrc     = crc16({ 0xa1, 0xa1, 0xa1, 0xfe, f.tr, f.hd, f.sr, f.sz });
        sec.fdcStatus = f.deleted ? FLAG_RECORD_TYPE : 0;
        sec.sectorSize  = uint16_t(f.dataLen);
        // « Juste après l'IDAM » (octet ID piste). Écrêté : un flux WRITE TRACK
        // HD/ED (~12,5/25 Ko) dépasse 65535 bits — le wrap uint16 cassait l'ordre
        // angulaire croissant supposé par nextSectorIDStx (secteurs masqués/RNF).
        sec.bitPosition = uint16_t(std::min<uint32_t>((f.fePos + 1) * 8, 0xFFFF));
        sec.readTime    = 0;
        sec.dataOffset  = 0;
        sec.pFuzzy = nullptr; sec.pTiming = nullptr;
        sec.saveIndex = -1;
    }
    // 2e passe : pointeurs de données (writeData n'est plus redimensionné ensuite).
    for (std::size_t s = 0; s < found.size(); ++s)
        t.writeSectors[s].pData = t.writeData[s].data();
}

// =============================================================================
//  Persistance .wd1772 — conteneur compatible Hatari (STX_WriteDisk /
//  STX_LoadSaveFile). Extension NDAM optionnelle : les anciens lecteurs gardent
//  les données mais ignorent la DAM, qui peut être perdue lors d'une réécriture.
//
//  En-tête (16 o) : "WD1772" + version(1) + révision(0) + u32BE nbSECT + u32BE nbTRCK.
//  Bloc SECT (20 o + données) : "SECT", u32BE longueur (16+taille), piste, face,
//    u16BE bitPosition, ID piste/face/secteur/taille, u16BE ID_CRC, u16BE taille, données.
//    Extension après les données : "NDAM" + DAM (FB/F8), incluse dans longueur.
//    Absente pour FB à l'écriture ; les anciens SECT sans trailer signifient FB.
//  Bloc TRCK (12 o + données) : "TRCK", u32BE longueur (8+taille), piste, face,
//    u16BE taille du flux écrit, données (flux WRITE TRACK brut).
// =============================================================================
static inline void wr16be(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}
static inline void wr32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}
static inline uint16_t rd16be(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
static inline uint32_t rd32be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool StxImage::saveWd1772(const std::string& path) const {
    uint32_t nbSect = 0;
    for (const SaveSector& ss : saveSectors)
        if (ss.used) nbSect++;
    if (nbSect == 0 && saveTracks.empty()) return true;   // rien à sauver → pas de fichier

    std::vector<uint8_t> out;
    out.insert(out.end(), {'W','D','1','7','7','2'});
    out.push_back(1);                                     // version
    out.push_back(0);                                     // révision
    wr32be(out, nbSect);
    wr32be(out, uint32_t(saveTracks.size()));

    for (const SaveSector& ss : saveSectors) {
        if (!ss.used) continue;                           // invalidé par un WRITE TRACK
        out.insert(out.end(), {'S','E','C','T'});
        const bool deleted = ss.dataMark == 0xF8;
        wr32be(out, uint32_t(16 + ss.data.size() + (deleted ? 5 : 0)));
        out.push_back(ss.track);
        out.push_back(ss.side);
        wr16be(out, ss.bitPos);
        out.push_back(ss.idTrack);
        out.push_back(ss.idHead);
        out.push_back(ss.idSector);
        out.push_back(ss.idSize);
        wr16be(out, ss.idCrc);
        wr16be(out, uint16_t(ss.data.size()));
        out.insert(out.end(), ss.data.begin(), ss.data.end());
        // Extension NeoST v1.0 : Hatari lit SectorSize octets puis saute à
        // BlockLen, donc conserve les données et ignore ce trailer optionnel.
        // Un ancien lecteur qui réécrit le fichier peut perdre cette DAM.
        if (deleted) out.insert(out.end(), {'N','D','A','M', ss.dataMark});
    }
    for (const SaveTrack& st : saveTracks) {
        out.insert(out.end(), {'T','R','C','K'});
        wr32be(out, uint32_t(8 + st.data.size()));
        out.push_back(st.track);
        out.push_back(st.side);
        wr16be(out, uint16_t(st.data.size()));
        out.insert(out.end(), st.data.begin(), st.data.end());
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;                                 // répertoire en lecture seule…
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(f);
}

bool StxImage::loadWd1772(const std::string& path) {
    // ⚠ Ce fichier n'est JAMAIS nommé par l'utilisateur : Fdc::loadImage le déduit du
    // .stx monté (« .stx » → « .wd1772 »). Il doit donc être le plus défensif du lot.
    // Il manquait les deux garde-fous que les cinq autres chargeurs ont déjà :
    //  - refus des non-fichiers : sous Linux, ifstream OUVRE un répertoire et tellg()
    //    rend 2^63−1 → vector géant → std::bad_alloc non rattrapé → l'émulateur entier
    //    mourait au simple montage de la disquette (Hatari, lui, refuse les répertoires
    //    dans File_Exists et se contente d'un message d'erreur) ;
    //  - borne HAUTE de taille : un .wd1772 creux de 200 Go faisait la même fin.
    std::error_code fec;
    if (!std::filesystem::is_regular_file(path, fec)) return false;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;                                 // pas de fichier compagnon : normal
    const std::streamsize n = f.tellg();
    constexpr std::streamsize kMaxWd1772 = 8 * 1024 * 1024;   // overlays d'écriture : quelques dizaines de Ko
    if (n < 16 || n > kMaxWd1772) return false;
    f.seekg(0);
    std::vector<uint8_t> in(static_cast<std::size_t>(n));
    f.read(reinterpret_cast<char*>(in.data()), n);
    if (!f || f.gcount() != n) return false;              // lecture courte : image douteuse

    if (std::memcmp(in.data(), "WD1772", 6) != 0) return false;
    if (in[6] != 1 || in[7] != 0) {                       // version/révision inconnues
        std::fprintf(stderr, "[STX] %s: unsupported .wd1772 version %d.%d\n",
                     path.c_str(), in[6], in[7]);
        return false;
    }
    // Les compteurs de l'en-tête (+8/+12) ne servent qu'aux allocations chez
    // Hatari ; on parse les blocs jusqu'à la fin du fichier.
    saveSectors.clear();
    saveTracks.clear();

    // DEUX PASSES, les TRCK d'ABORD. Un WRITE SECTOR effectué APRÈS un WRITE TRACK
    // porte la position angulaire de la piste RÉINTERPRÉTÉE ; en lisant les SECT en
    // premier, on la cherchait parmi les secteurs d'ORIGINE, on ne la trouvait pas
    // (« bloc SECT sans secteur »), et l'overlay était jeté — puis le TRCK
    // reconstruisait writeSectors avec saveIndex = -1. Résultat : la sauvegarde
    // écrite par un jeu sur une piste qu'il venait de formater disparaissait au
    // lancement suivant, sans un mot.
    for (int pass = 0; pass < 2; ++pass) {
    const bool wantTrack = (pass == 0);
    std::size_t p = 16;
    while (p + 8 <= in.size()) {
        const uint32_t blockLen = rd32be(&in[p + 4]);
        const std::size_t next = p + 4 + blockLen;        // cf. Hatari : ID(4) + longueur
        if (blockLen < 4 || next > in.size()) break;      // bloc tronqué → on s'arrête là

        if (std::memcmp(&in[p], "SECT", 4) == 0 && blockLen >= 16 && !wantTrack) {
            SaveSector ss;
            const uint8_t* q = &in[p + 8];
            ss.track    = q[0];
            ss.side     = q[1];
            ss.bitPos   = rd16be(q + 2);
            ss.idTrack  = q[4];
            ss.idHead   = q[5];
            ss.idSector = q[6];
            ss.idSize   = q[7];
            ss.idCrc    = rd16be(q + 8);
            const uint16_t size = rd16be(q + 10);
            if (std::size_t(16) + size > blockLen) break; // données tronquées
            ss.data.assign(q + 12, q + 12 + size);
            // Sans extension (anciens SECT), le WRITE SECTOR était normal.
            // Ignorer tout trailer inconnu, incomplet ou non FB/F8, sans lire
            // au-delà de BlockLen ni transformer un octet en flags arbitraires.
            if (std::size_t(blockLen) - 16 - size >= 5) {
                const uint8_t* extra = q + 12 + size;
                if (std::memcmp(extra, "NDAM", 4) == 0 &&
                    (extra[4] == 0xFB || extra[4] == 0xF8)) ss.dataMark = extra[4];
            }
            Sector* sec = findSectorByPosition(ss.track, ss.side, ss.bitPos);
            if (sec && ss.data.size() != sec->sectorSize) {
                // Overlay d'une autre taille que le secteur : les lecteurs bouclent
                // sur sec.sectorSize → un bloc plus court ferait lire hors du vector.
                std::fprintf(stderr, "[STX] %s: SECT block of %zu B for a %u B sector "
                             "(track %d side %d) — ignored\n", path.c_str(),
                             ss.data.size(), sec->sectorSize, ss.track, ss.side);
            } else if (sec) {                             // associe l'overlay à son secteur
                saveSectors.push_back(std::move(ss));
                sec->saveIndex = int(saveSectors.size()) - 1;
            } else {
                std::fprintf(stderr, "[STX] %s: SECT block with no sector (track %d side %d "
                             "bitpos %d) — ignored\n", path.c_str(), ss.track, ss.side, ss.bitPos);
            }
        } else if (std::memcmp(&in[p], "TRCK", 4) == 0 && blockLen >= 8 && wantTrack) {
            SaveTrack st;
            const uint8_t* q = &in[p + 8];
            st.track = q[0];
            st.side  = q[1];
            const uint16_t size = rd16be(q + 2);
            if (std::size_t(8) + size > blockLen) break;
            st.data.assign(q + 4, q + 4 + size);
            Track* t = findTrack(st.track, st.side);
            if (t) {
                saveTracks.push_back(std::move(st));
                t->saveTrackIndex = int(saveTracks.size()) - 1;
                reinterpretSaveTrack(*t);          // flux rechargé → secteurs lisibles
            } else {
                std::fprintf(stderr, "[STX] %s: TRCK block with no track (track %d side %d) "
                             "— ignored\n", path.c_str(), st.track, st.side);
            }
        } else if (wantTrack && std::memcmp(&in[p], "SECT", 4) != 0
                              && std::memcmp(&in[p], "TRCK", 4) != 0) {
            std::fprintf(stderr, "[STX] %s: unknown block \"%.4s\" — ignored\n",
                         path.c_str(), reinterpret_cast<const char*>(&in[p]));
        }
        p = next;
    }
    }
    return !saveSectors.empty() || !saveTracks.empty();
}
