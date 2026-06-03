// =============================================================================
//  StxImage.cpp — parseur du conteneur STX (Pasti). Port de STX_BuildStruct
//  (extern/hatari/src/floppies/stx.c). Voir StxImage.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/StxImage.hpp"

#include <cstring>

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
        sec.bitPosition = uint16_t(bytePos * 8);
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
    auto inBuf = [&](const uint8_t* q, std::size_t n) { return q >= base && q + n <= end; };

    // En-tête (16 o).
    if (std::memcmp(base, "RSY\0", 4) != 0) return false;
    version_         = rd16le(base + 4);
    imagingTool_     = rd16le(base + 6);
    tracksCountHdr_  = base[10];
    revision_        = base[11];

    const uint8_t* p = base + 16;
    tracks_.reserve(tracksCountHdr_);

    for (int t = 0; t < tracksCountHdr_; ++t) {
        if (!inBuf(p, 16)) return false;
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

        // Borne du bloc de piste (p_cur + BlockSize) ; on s'y recale à la fin.
        const uint8_t* next = p_cur + trk.blockSize;
        if (trk.blockSize < 16 || !inBuf(p_cur, trk.blockSize)) return false;

        bool simple = false;
        if (trk.sectorsCount > 0 && (trk.flags & TRACK_FLAG_SECTOR_BLOCK) == 0) {
            // Piste = SectorsCount secteurs de 512 o, données juste après l'en-tête.
            if (inBuf(p, std::size_t(trk.sectorsCount) * 512u))
                buildSectorsSimple(trk, p);
            simple = true;
        }

        if (!simple) {
            // Zones optionnelles fuzzy / image de piste.
            trk.pFuzzy     = p + std::size_t(trk.sectorsCount) * 16u;   // après les blocs secteur
            trk.pTrackData = trk.pFuzzy + trk.fuzzySize;

            if ((trk.flags & TRACK_FLAG_IMAGE) == 0) {
                trk.pTrackImage   = nullptr;
                trk.pSectorsImage = trk.pTrackData;
            } else if ((trk.flags & TRACK_FLAG_IMAGE_SYNC) == 0) {
                if (inBuf(trk.pTrackData, 2)) {
                    trk.trackImageSize = rd16le(trk.pTrackData);
                    trk.pTrackImage    = trk.pTrackData + 2;
                    trk.pSectorsImage  = trk.pTrackImage + trk.trackImageSize;
                }
            } else {
                if (inBuf(trk.pTrackData, 4)) {
                    trk.trackImageSyncPos = rd16le(trk.pTrackData);
                    trk.trackImageSize    = rd16le(trk.pTrackData + 2);
                    trk.pTrackImage       = trk.pTrackData + 4;
                    trk.pSectorsImage     = trk.pTrackImage + trk.trackImageSize;
                }
            }

            if (trk.sectorsCount > 0) {
                trk.sectors.resize(trk.sectorsCount);
                const uint8_t* pFuzzy = trk.pFuzzy;
                bool variableTimings = false;
                uint32_t maxOffsetEnd = 0;

                for (int s = 0; s < trk.sectorsCount; ++s) {
                    if (!inBuf(p, 16)) return false;
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
                        const uint8_t* d = trk.pTrackData + sec.dataOffset;
                        if (inBuf(d, sec.sectorSize)) sec.pData = d;
                        if (sec.fdcStatus & FLAG_FUZZY) {
                            if (inBuf(pFuzzy, sec.sectorSize)) sec.pFuzzy = pFuzzy;
                            pFuzzy += sec.sectorSize;
                        }
                        if (sec.dataOffset + sec.sectorSize > maxOffsetEnd)
                            maxOffsetEnd = sec.dataOffset + sec.sectorSize;
                        if (sec.fdcStatus & FLAG_VARIABLE_TIME) variableTimings = true;
                    }
                }

                // Table de timing (après l'image des secteurs).
                trk.pTiming = trk.pTrackData + maxOffsetEnd;
                if (trk.pTiming < trk.pSectorsImage) trk.pTiming = trk.pSectorsImage;

                if (variableTimings) {
                    if (revision_ == 2 && inBuf(trk.pTiming, 4)) {
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
                                if (pTim && inBuf(pTim, (sec.sectorSize / 16) * 2)) sec.pTiming = pTim;
                                if (pTim) pTim += (sec.sectorSize / 16) * 2;
                            } else {
                                sec.pTiming = TimingDataDefault;   // table fixe révision 0
                            }
                        }
                    }
                }
            }
        }

        if (!inBuf(next, 0)) return false;
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
