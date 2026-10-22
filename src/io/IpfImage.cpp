// =============================================================================
//  IpfImage.cpp — décodeur IPF (SPS/CAPS) et conversion vers le modèle STX.
//  Voir IpfImage.hpp pour l'architecture et les limites.
//
//  ATTRIBUTION. L'étage 1 (blocs → flux de cellules : descriptions de blocs,
//  gaps de types 0 à 3, pistes à densité variable de types 3 à 9, rotation sur
//  l'index) est une ADAPTATION du décodeur IPF de MAME :
//      src/lib/formats/ipf_dsk.cpp — copyright-holders: Olivier Galibert
//      licence BSD-3-Clause (compatible GPL 3), github.com/mamedev/mame
//  Le texte de la licence BSD-3-Clause figure dans packaging/licenses/THIRD-PARTY.txt.
//  Écarts volontaires avec l'original : bornes vérifiées sur TOUS les accès au
//  conteneur (MAME lit les descriptions de gap sans borne au second passage),
//  modulo signé pour les motifs de gap « à rebours » (MAME s'appuie sur
//  l'enroulement uint32, juste seulement pour les motifs de 2^k bits), pas de
//  marquage du raccord d'écriture (sans effet sur la lecture : il tombe dans un
//  gap), sortie en tableaux niveau/durée au lieu de floppy_image.
//
//  L'étage 2 (lecteur WD1772 → STX) est propre à NeoST ; ses règles de synchro
//  suivent MAME src/devices/machine/wd_fdc.cpp (live_run : SEARCH_ADDRESS_MARK_*,
//  READ_TRACK_DATA), lui-même calé sur les fiches WD177x.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST (GPL 3).
// =============================================================================
#include "io/IpfImage.hpp"
#include "io/StxImage.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace ipf {

// --- CRC ------------------------------------------------------------------
uint32_t crc32(const uint8_t* p, std::size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int j = 0; j < 8; ++j) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

uint16_t crc16Ccitt(const uint8_t* p, std::size_t n, uint16_t crc) {
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= uint16_t(p[i]) << 8;
        for (int k = 0; k < 8; ++k)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

uint64_t CellTrack::totalNs() const {
    uint64_t s = 0;
    for (uint16_t t : timeNs) s += t;
    return s;
}

bool isIpf(const uint8_t* p, std::size_t n) {
    static const uint8_t refh[12] = { 0x43, 0x41, 0x50, 0x53, 0x00, 0x00, 0x00, 0x0c,
                                      0x1c, 0xd5, 0x73, 0xba };
    return n >= 12 && std::memcmp(p, refh, 12) == 0;
}

namespace {

// Bornes mémoire (un fichier de 8 Mo pourrait sinon réclamer des Go de cellules) :
// une piste ED fait ~400 000 cellules ; 84 cylindres × 2 faces en HD ≈ 34 M.
constexpr uint32_t kMaxTrackCells = 600000;
constexpr uint64_t kMaxTotalCells = 34000000;

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// =============================================================================
//  Étage 1 — conteneur IPF → flux de cellules (adapté de MAME ipf_dsk.cpp).
// =============================================================================
struct TrackInfo {
    uint32_t cylinder = 0, head = 0, type = 0, sigtype = 0, process = 0;
    uint32_t sizeCells = 0, indexCells = 0, blockCount = 0;
    bool infoSet = false;
    const uint8_t* data = nullptr;   // descripteurs de blocs + flux (après l'en-tête DATA)
    uint32_t dataSize = 0;
    bool hasData = false;
};

struct Decoder {
    std::map<uint32_t, TrackInfo> tinfos;      // clé = dataKey (IMGE +64 / DATA +24)
    Info info;
    std::string err;

    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    // ----- conteneur ------------------------------------------------------
    bool scanAll(const std::vector<uint8_t>& raw) {
        const uint8_t* base = raw.data();
        const std::size_t size = raw.size();
        std::size_t pos = 0;
        bool sawCaps = false, sawInfo = false;
        while (pos != size) {
            if (size - pos < 12) return fail("truncated record header");
            const uint8_t* tag = base + pos;
            const uint32_t tsize = be32(tag + 4);
            if (tsize < 12) return fail("record smaller than its header");
            if (size - pos < tsize) return fail("truncated record");
            // CRC32 du bloc avec son propre champ CRC lu comme zéro.
            uint8_t hdr[12];
            std::memcpy(hdr, tag, 12);
            hdr[8] = hdr[9] = hdr[10] = hdr[11] = 0;
            uint32_t c = 0xFFFFFFFFu;
            auto feed = [&c](const uint8_t* p, std::size_t n) {
                for (std::size_t i = 0; i < n; ++i) {
                    c ^= p[i];
                    for (int j = 0; j < 8; ++j) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
                }
            };
            feed(hdr, 12);
            feed(tag + 12, tsize - 12);
            if (~c != be32(tag + 8)) return fail("record CRC mismatch");
            pos += tsize;

            const uint32_t id = be32(tag);
            if (!sawCaps && id != 0x43415053) return fail("missing CAPS header");
            switch (id) {
            case 0x43415053:                                   // CAPS
                if (tsize != 12) return fail("bad CAPS record");
                sawCaps = true;
                break;
            case 0x494e464f:                                   // INFO
                if (tsize != 96 || !parseInfo(tag)) return fail(err.empty() ? "bad INFO record" : err.c_str());
                sawInfo = true;
                break;
            case 0x494d4745:                                   // IMGE
                if (!sawInfo) return fail("IMGE before INFO");
                if (tsize != 80 || !parseImge(tag)) return fail(err.empty() ? "bad IMGE record" : err.c_str());
                break;
            case 0x44415441: {                                 // DATA (+ zone de données)
                if (tsize != 28) return fail("bad DATA record");
                TrackInfo& t = tinfos[be32(tag + 24)];
                const uint32_t dsz = be32(tag + 12);
                if (dsz > size - pos) return fail("DATA area overruns the file");
                t.data = base + pos;
                t.dataSize = dsz;
                t.hasData = true;
                if (crc32(t.data, dsz) != be32(tag + 20)) return fail("DATA area CRC mismatch");
                pos += dsz;
                break;
            }
            default:                                           // TRCK, CTEI, CTEX… : rien d'utile
                break;
            }
        }
        if (!sawInfo) return fail("missing INFO record");
        return true;
    }

    bool parseInfo(const uint8_t* p) {
        if (be32(p + 12) != 1) return fail("not a floppy disk image (INFO media type)");
        info.encoder = be32(p + 16);
        if (info.encoder != 1 && info.encoder != 2) return fail("unknown IPF encoder");
        if (be32(p + 20) != 1) return fail("unsupported IPF encoder revision");
        info.release     = be32(p + 24);
        info.revision    = be32(p + 28);
        info.minCylinder = be32(p + 36);
        info.maxCylinder = be32(p + 40);
        info.minHead     = be32(p + 44);
        info.maxHead     = be32(p + 48);
        return true;
    }

    bool parseImge(const uint8_t* p) {
        TrackInfo& t = tinfos[be32(p + 64)];
        if (t.infoSet) return fail("duplicate IMGE record");
        t.infoSet    = true;
        t.cylinder   = be32(p + 12);
        t.head       = be32(p + 16);
        if (t.cylinder < info.minCylinder || t.cylinder > info.maxCylinder
            || t.head < info.minHead || t.head > info.maxHead || t.cylinder > 127 || t.head > 1)
            return fail("IMGE track outside the INFO geometry");
        t.type       = be32(p + 20);
        t.sigtype    = be32(p + 24);
        if (t.sigtype != 1) return fail("unsupported signal type (only 2 us cells)");
        t.indexCells = be32(p + 36);
        t.sizeCells  = be32(p + 48);
        t.blockCount = be32(p + 52);
        t.process    = be32(p + 56);
        if (t.process != 0) return fail("unsupported encoder process");
        return true;
    }

    // ----- génération d'une piste ----------------------------------------
    // Curseur d'écriture dans la piste en cours.
    struct Out {
        std::vector<uint8_t>& cells;
        std::size_t pos;
    };

    static void writeRaw(Out& o, const uint8_t* data, uint32_t n, bool& ctx) {
        for (uint32_t i = 0; i != n; ++i)
            o.cells[o.pos++] = (data[i >> 3] & (0x80 >> (i & 7))) ? CELL_1 : CELL_0;
        if (n) ctx = o.cells[o.pos - 1] == CELL_1;
    }
    // Motif MFM répété (données de `patBits` bits), démarré au demi-bit `start`
    // (modulo signé : un départ négatif aligne la FIN du motif sur la fin du gap).
    static void writeMfm(Out& o, const uint8_t* data, int64_t start, uint32_t patBits,
                         uint32_t n, bool& ctx) {
        const int64_t patlen = int64_t(patBits) * 2;
        for (uint32_t i = 0; i != n; ++i) {
            int64_t p = (int64_t(i) + start) % patlen;
            if (p < 0) p += patlen;
            const bool bit = data[p >> 4] & (0x80 >> ((p >> 1) & 7));
            if (p & 1) { o.cells[o.pos++] = bit ? CELL_1 : CELL_0; ctx = bit; }
            else         o.cells[o.pos++] = (ctx || bit) ? CELL_0 : CELL_1;
        }
    }
    static void writeWeak(Out& o, uint32_t n) {
        for (uint32_t i = 0; i != n; ++i) o.cells[o.pos++] = CELL_WEAK;
    }

    // Lit un paramètre de `count` octets (0..7) big-endian, borné.
    static bool rb(const uint8_t*& p, const uint8_t* lim, int count, uint32_t& v) {
        if (count > lim - p) return false;
        v = 0;
        for (int i = 0; i < count; ++i) v = (v << 8) | *p++;
        return true;
    }

    bool blockData(const uint8_t* data, const uint8_t* lim, Out o, std::size_t tlimit,
                   bool dmb, bool raw, bool& ctx) {
        for (;;) {
            if (data >= lim) return false;
            const uint8_t val = *data++;
            uint32_t param;
            if (!rb(data, lim, val >> 5, param)) return false;
            const uint64_t tleft = tlimit - o.pos;
            uint64_t bitcount = dmb ? uint64_t(param) : uint64_t(param) * 8;
            uint64_t bytecount = (bitcount + 7) / 8;
            const uint64_t avail = uint64_t(lim - data);
            switch (val & 0x1f) {
            case 0: return tleft == 0;                         // fin de description
            case 1:                                            // synchro : cellules brutes
                if (raw || bitcount > tleft || bytecount > avail) return false;
                writeRaw(o, data, uint32_t(bitcount), ctx);
                data += bytecount;
                break;
            case 2:                                            // octets de données
            case 3:                                            // octets de gap
                if (raw || 2 * bitcount > tleft || bytecount > avail) return false;
                writeMfm(o, data, 0, uint32_t(bitcount), uint32_t(2 * bitcount), ctx);
                data += bytecount;
                break;
            case 4:                                            // cellules brutes (encodeur raw)
                if (!raw) return false;
                bitcount = uint64_t(param) * 8;
                bytecount = param;
                if (bitcount > tleft || bytecount > avail) return false;
                writeRaw(o, data, uint32_t(bitcount), ctx);
                data += bytecount;
                break;
            case 5:                                            // octets faibles (weak bits)
                if (raw || 2 * bitcount > tleft) return false;
                writeWeak(o, uint32_t(2 * bitcount));
                ctx = false;
                break;
            default:
                return false;
            }
        }
    }

    bool gapReservedSize(const uint8_t*& data, const uint8_t* lim, uint32_t& res) {
        res = 0;
        for (;;) {
            if (data >= lim) return false;
            const uint8_t val = *data++;
            uint32_t param;
            if (!rb(data, lim, val >> 5, param)) return false;
            switch (val & 0x1f) {
            case 0: return true;
            case 1: res += param * 2; break;
            case 2:
                if ((uint64_t(param) + 7) / 8 > uint64_t(lim - data)) return false;
                data += (param + 7) / 8;
                break;
            default: return false;
            }
        }
    }

    bool gapFromDescription(const uint8_t*& data, const uint8_t* lim, Out o, uint32_t size,
                            bool pre, bool& ctx) {
        const uint8_t* d1 = data;
        uint32_t resSize;
        if (!gapReservedSize(d1, lim, resSize) || resSize > size) return false;
        uint8_t pattern[16] = {};
        uint32_t patternSize = 0, pos = 0, blockSize = 0;
        for (;;) {
            if (data >= lim) return false;
            const uint8_t val = *data++;
            uint32_t param;
            if (!rb(data, lim, val >> 5, param)) return false;
            switch (val & 0x1f) {
            case 0:
                return size == pos;
            case 1:
                if (blockSize) return false;
                blockSize = param * 2;
                patternSize = 0;
                break;
            case 2: {
                patternSize = param;
                if (patternSize == 0 || patternSize > sizeof(pattern) * 8) return false;
                const uint32_t nb = (patternSize + 7) / 8;
                if (nb > uint32_t(lim - data)) return false;
                std::memcpy(pattern, data, nb);
                data += nb;
                if (pre) {
                    // Le dernier échantillon d'un gap « en avant » boucle jusqu'au bout.
                    if (data < lim && !(*data & 0x1f)) blockSize = size - pos;
                    else if (!blockSize) blockSize = patternSize;
                    if (uint64_t(pos) + blockSize > size) return false;
                    writeMfm(o, pattern, 0, patternSize, blockSize, ctx);
                } else {
                    if (pos == 0 && blockSize && resSize != size) blockSize = size - (resSize - blockSize);
                    if (!blockSize) blockSize = size - resSize;
                    if (uint64_t(pos) + blockSize > size) return false;
                    writeMfm(o, pattern, -int64_t(blockSize), patternSize, blockSize, ctx);
                }
                pos += blockSize;
                blockSize = 0;
                break;
            }
            default:
                return false;
            }
        }
    }

    bool gap0(uint32_t gapCells, uint8_t pattern, int64_t ipos, Out& o, bool& ctx) {
        const uint32_t spos = (ipos >= 16 && ipos + 16 <= int64_t(gapCells)) ? uint32_t(ipos) : gapCells >> 1;
        writeMfm(o, &pattern, 0, 8, spos, ctx);
        uint32_t delta = 0;
        if (gapCells & 1) { o.cells[o.pos++] = CELL_0; delta = 1; }
        writeMfm(o, &pattern, int64_t(spos) + delta - gapCells, 8, gapCells - spos - delta, ctx);
        return true;
    }

    bool gap3(uint32_t gapCells, int64_t ipos, const uint8_t* data, const uint8_t* lim,
              Out o, bool& ctx) {
        uint32_t presize, postsize;
        const uint8_t* d1 = data;
        if (!gapReservedSize(d1, lim, presize) || !gapReservedSize(d1, lim, postsize)) return false;
        const uint32_t delta = gapCells & 1;
        const uint32_t usable = gapCells - delta;
        if (uint64_t(presize) + postsize > usable) {
            // Rognage symétrique des deux flux, comme le décodeur de référence.
            const uint32_t rem = presize + postsize - usable;
            uint32_t rem0 = rem >> 1, rem1 = rem - rem0;
            if (presize < rem0) { rem1 += rem0 - presize; rem0 = presize; }
            if (postsize < rem1) { rem0 += rem1 - postsize; rem1 = postsize; }
            presize -= rem0;
            postsize -= rem1;
        }
        uint32_t spos;
        if (ipos >= 16 && ipos < int64_t(gapCells) - 16) {
            spos = uint32_t(ipos);
            if (spos < presize) spos = presize;
            if (spos > usable - postsize) spos = usable - postsize;
        } else {
            spos = presize + (usable - presize - postsize) / 2;
        }
        const std::size_t at = o.pos;
        if (!gapFromDescription(data, lim, o, spos, true, ctx)) return false;
        if (delta) o.cells[at + spos] = CELL_0;
        Out o2{o.cells, at + spos + delta};
        return gapFromDescription(data, lim, o2, gapCells - spos - delta, false, ctx);
    }

    bool block(const TrackInfo& t, uint32_t idx, int64_t ipos, std::vector<uint8_t>& cells,
               uint32_t& pos, uint32_t& dpos, uint32_t& gpos, bool& ctx) {
        const uint8_t* data = t.data;
        const uint8_t* end = t.data + t.dataSize;
        const uint8_t* th = data + 32 * std::size_t(idx);
        const uint32_t dataCells = be32(th);
        uint32_t gapCells = be32(th + 4);
        if (gapCells < 8) gapCells = 0;
        // +8 : offset de la description du gap ; +16 : encodeur (1 MFM, 2 brut) ;
        // +20 : drapeaux (bits 0-1 type de gap, bit 2 taille en bits) ; +24 : motif
        // du gap de type 0 ; +28 : offset de la description des données.
        const uint32_t encoder = be32(th + 16);
        if (encoder != 1 && encoder != 2) return false;
        const bool raw = encoder == 2;
        const uint32_t flags = info.encoder == 1 ? 0 : be32(th + 20);
        const uint32_t gapType = flags & 3;
        const bool dmb = (flags & 4) != 0;

        dpos = pos;
        if (uint64_t(dpos) + dataCells + gapCells > t.sizeCells) return false;
        gpos = dpos + dataCells;
        pos = gpos + gapCells;

        const uint32_t dOff = be32(th + 28), gOff = be32(th + 8);
        if (dOff >= t.dataSize) return false;
        if (!blockData(data + dOff, end, Out{cells, dpos}, gpos, dmb, raw, ctx)) return false;

        const int64_t ip = ipos > int64_t(gpos) ? ipos - int64_t(gpos) : 0;
        Out g{cells, gpos};
        switch (gapType) {
        case 0:
            return gap0(gapCells, uint8_t(be32(th + 24)), ip, g, ctx);
        case 1:
        case 2: {
            if (gOff >= t.dataSize) return false;
            const uint8_t* gd = data + gOff;
            return gapFromDescription(gd, end, g, gapCells, gapType == 1, ctx);
        }
        case 3:
            if (gOff >= t.dataSize) return false;
            return gap3(gapCells, ip, data + gOff, end, g, ctx);
        default:
            return false;
        }
    }

    static void timingSet(std::vector<uint16_t>& tm, uint32_t a, uint32_t b, uint16_t v) {
        for (uint32_t i = a; i < b && i < tm.size(); ++i) tm[i] = v;
    }

    bool timings(const TrackInfo& t, std::vector<uint16_t>& tm, const std::vector<uint32_t>& dp,
                 const std::vector<uint32_t>& gp) {
        const uint32_t bc = t.blockCount;
        switch (t.type) {
        case 2: break;                                   // densité nominale
        case 3:                                          // Copylock (ancien)
            if (bc >= 5) { timingSet(tm, gp[3], dp[4], 1890); timingSet(tm, dp[4], gp[4], 1890); }
            if (bc >= 6) { timingSet(tm, gp[4], dp[5], 1990); timingSet(tm, dp[5], gp[5], 1990); }
            if (bc >= 7) { timingSet(tm, gp[5], dp[6], 2090); timingSet(tm, dp[6], gp[6], 2090); }
            break;
        case 4:                                          // Copylock
            timingSet(tm, gp[bc - 1], t.sizeCells, 1890);
            timingSet(tm, dp[0], gp[0], 1890);
            if (bc >= 2) { timingSet(tm, gp[0], dp[1], 1990); timingSet(tm, dp[1], gp[1], 1990); }
            if (bc >= 3) { timingSet(tm, gp[1], dp[2], 2090); timingSet(tm, dp[2], gp[2], 2090); }
            break;
        case 5: if (bc >= 6) timingSet(tm, dp[5], gp[5], 2100); break;
        case 6:
            if (bc >= 2) timingSet(tm, dp[1], gp[1], 2200);
            if (bc >= 3) timingSet(tm, dp[2], gp[2], 1800);
            break;
        case 7: if (bc >= 2) timingSet(tm, dp[1], gp[1], 2100); break;
        case 8:
            if (bc >= 2) timingSet(tm, dp[1], gp[1], 2200);
            if (bc >= 3) timingSet(tm, dp[2], gp[2], 2100);
            if (bc >= 5) timingSet(tm, dp[4], gp[4], 1900);
            if (bc >= 6) timingSet(tm, dp[5], gp[5], 1800);
            if (bc >= 7) timingSet(tm, dp[6], gp[6], 1700);
            break;
        case 9: {                                        // clé de vitesse (motif du bloc 0)
            const uint32_t mask = be32(t.data + 24);
            for (uint32_t i = 1; i < bc; ++i)
                timingSet(tm, dp[i], gp[i], (i - 1 < 32 && (mask & (1u << (i - 1)))) ? 1900 : 2100);
            break;
        }
        default:
            return false;
        }
        return true;
    }

    bool track(TrackInfo& t, CellTrack& out) {
        out.cylinder = int(t.cylinder);
        out.head = int(t.head);
        if (t.type == 1) {                               // bruit / non formatée
            // Taille lue telle quelle dans l'IMGE : bornée comme une piste formatée
            // (sinon un champ forgé réclamerait 4 G cellules).
            if (t.sizeCells > kMaxTrackCells) return fail("implausible track size");
            out.noise = true;
            out.cells.assign(t.sizeCells, CELL_WEAK);
            out.timeNs.assign(t.sizeCells, 2000);
            return true;
        }
        if (t.blockCount == 0 || t.blockCount > 4096) return fail("bad block count");
        if (t.dataSize < 32ull * t.blockCount) return fail("block descriptors truncated");
        // Les gaps trop petits sont ignorés et changent la taille réelle (MAME).
        uint64_t real = 0;
        for (uint32_t i = 0; i != t.blockCount; ++i) {
            const uint8_t* th = t.data + 32 * std::size_t(i);
            const uint32_t g = be32(th + 4);
            real += uint64_t(be32(th)) + (g < 8 ? 0 : g);
        }
        // Une piste DD fait ~100 000 cellules, une ED ~400 000.
        if (real == 0 || real > kMaxTrackCells) return fail("implausible track size");
        t.sizeCells = uint32_t(real);
        if (t.indexCells >= t.sizeCells) return fail("index offset beyond track end");

        std::vector<uint8_t> cells(t.sizeCells, CELL_0);
        std::vector<uint32_t> dp(t.blockCount + 1), gp(t.blockCount);
        bool ctx = false;
        uint32_t pos = 0;
        for (uint32_t i = 0; i != t.blockCount; ++i) {
            const int64_t ipos = (i == t.blockCount - 1) ? int64_t(t.sizeCells) - t.indexCells
                                                         : int64_t(0xFFFFFFFFll);
            if (!block(t, i, ipos, cells, pos, dp[i], gp[i], ctx))
                return fail("malformed block description");
        }
        if (pos != t.sizeCells) return fail("blocks do not fill the track");
        dp[t.blockCount] = pos;

        std::vector<uint16_t> tm(t.sizeCells, 2000);
        if (!timings(t, tm, dp, gp)) return fail("unknown track density type");

        // Rotation : la cellule 0 devient l'impulsion d'index (bloc 0 à indexCells).
        out.cells.resize(t.sizeCells);
        out.timeNs.resize(t.sizeCells);
        const uint32_t off = t.sizeCells - t.indexCells;
        for (uint32_t i = 0; i < t.sizeCells; ++i) {
            const uint32_t src = (i + off) % t.sizeCells;
            out.cells[i] = cells[src];
            out.timeNs[i] = tm[src];
        }
        return true;
    }
};

// =============================================================================
//  Étage 2 — lecteur WD1772 sur le flux de cellules → piste STX.
// =============================================================================
struct Stream {
    const CellTrack& t;
    int64_t n;
    std::vector<uint64_t> start;                    // start[i] = ns cumulés avant la cellule i

    explicit Stream(const CellTrack& tr) : t(tr), n(int64_t(tr.cells.size())) {
        start.resize(std::size_t(n) + 1);
        uint64_t s = 0;
        for (int64_t i = 0; i < n; ++i) { start[std::size_t(i)] = s; s += tr.timeNs[std::size_t(i)]; }
        start[std::size_t(n)] = s;
    }
    int64_t wrap(int64_t i) const { i %= n; return i < 0 ? i + n : i; }
    // Une cellule faible ne porte pas de transition fiable : 0 pour la recherche
    // de synchro ; signalée à part pour les octets de données (masque fuzzy).
    int cell(int64_t i) const { return t.cells[std::size_t(wrap(i))] == CELL_1; }
    bool weak(int64_t i) const { return t.cells[std::size_t(wrap(i))] == CELL_WEAK; }
    uint16_t raw16(int64_t first) const {
        uint16_t v = 0;
        for (int k = 0; k < 16; ++k) v = uint16_t((v << 1) | cell(first + k));
        return v;
    }
    // Octet MFM dont la 1re cellule (horloge) est `first` ; `stable` reçoit les
    // bits dont ni la cellule de donnée ni l'horloge qui la précède n'est faible.
    uint8_t byte(int64_t first, uint8_t* stable = nullptr) const {
        uint8_t v = 0, s = 0;
        for (int k = 0; k < 8; ++k) {
            const int64_t c = first + 2 * k;
            v = uint8_t((v << 1) | cell(c + 1));
            s = uint8_t((s << 1) | ((weak(c) || weak(c + 1)) ? 0 : 1));
        }
        if (stable) *stable = s;
        return v;
    }
    // Instant (ns depuis l'index) du DÉBUT de la cellule i (i peut dépasser n).
    uint64_t timeAt(int64_t i) const {
        const int64_t w = wrap(i);
        const int64_t turns = (i - w) / n;
        return start[std::size_t(w)] + uint64_t(turns) * start[std::size_t(n)];
    }
};

struct FoundSector {
    uint16_t bitPos = 0;
    uint8_t  id[4] = {};
    uint16_t idCrc = 0;
    uint8_t  status = 0;
    std::vector<uint8_t> data, fuzzy, timing;     // timing : 2 o BE par bloc de 16 o
    bool     hasFuzzy = false, hasTiming = false;
    uint16_t readTimeUs = 0;
};

constexpr int kSyncA1 = 0x4489, kSyncC2 = 0x5224;

// Champ données associé à l'ID dont le dernier octet de CRC finit à la cellule
// `idEnd` : fenêtre de synchro 28..43 octets (MAME wd_fdc SEARCH_ADDRESS_MARK_DATA,
// fiche WD1772 : DAM dans les 43 octets MFM qui suivent le CRC de l'ID).
void readDataField(const Stream& s, int64_t idEnd, FoundSector& fs, StxStats* st) {
    uint16_t sr = 0;
    int64_t sync = -1;
    for (int64_t k = 1; ; ++k) {
        sr = uint16_t((sr << 1) | s.cell(idEnd + k));
        if (k > 43 * 16) break;
        if (k >= 28 * 16 && sr == kSyncA1) { sync = idEnd + k; break; }
    }
    if (sync < 0 || s.raw16(sync + 1) != kSyncA1 || s.raw16(sync + 17) != kSyncA1) {
        fs.status |= StxImage::FLAG_RNF;
        if (st) ++st->noData;
        return;
    }
    const uint8_t mark = s.byte(sync + 33);
    if ((mark & 0xfe) != 0xfa && (mark & 0xfe) != 0xf8) {
        fs.status |= StxImage::FLAG_RNF;
        if (st) ++st->noData;
        return;
    }
    if ((mark & 0xfe) == 0xf8) { fs.status |= StxImage::FLAG_RECORD_TYPE; if (st) ++st->deleted; }

    const int size = 128 << (fs.id[3] & 3);
    const int64_t d0 = sync + 49;
    fs.data.resize(std::size_t(size));
    fs.fuzzy.resize(std::size_t(size));
    uint8_t hdr[4] = { 0xA1, 0xA1, 0xA1, mark };
    uint16_t crc = crc16Ccitt(hdr, 4);
    for (int i = 0; i < size; ++i) {
        uint8_t stable;
        fs.data[std::size_t(i)] = s.byte(d0 + 16 * i, &stable);
        fs.fuzzy[std::size_t(i)] = stable;
        if (stable != 0xFF) fs.hasFuzzy = true;
    }
    crc = crc16Ccitt(fs.data.data(), fs.data.size(), crc);
    uint8_t cb[2] = { s.byte(d0 + 16 * size), s.byte(d0 + 16 * (size + 1)) };
    crc = crc16Ccitt(cb, 2, crc);
    // Un bit faible rend le CRC aléatoire à chaque lecture : avec les cellules
    // faibles lues à 0 il tombe faux, ce qui est aussi l'issue quasi certaine sur
    // le vrai matériel.
    if (crc != 0) { fs.status |= StxImage::FLAG_CRC; if (st) ++st->dataCrcErrors; }
    if (fs.hasFuzzy) { fs.status |= StxImage::FLAG_FUZZY; if (st) ++st->fuzzySectors; }

    // Durée réelle des octets lus. Nominale (2 µs/cellule) → timing standard.
    bool nominal = true;
    for (int64_t c = d0; c < d0 + 16 * int64_t(size) && nominal; ++c)
        nominal = s.t.timeNs[std::size_t(s.wrap(c))] == 2000;
    if (!nominal) {
        fs.hasTiming = true;
        fs.status |= StxImage::FLAG_VARIABLE_TIME;
        if (st) ++st->timedSectors;
        const uint64_t total = s.timeAt(d0 + 16 * int64_t(size)) - s.timeAt(d0);
        fs.readTimeUs = uint16_t(std::min<uint64_t>((total + 500) / 1000, 65535));
        // Convention Pasti : bloc de 16 o = tv × 32 + 28 cycles FDC (8 MHz).
        for (int b = 0; b < size / 16; ++b) {
            const uint64_t ns = s.timeAt(d0 + 256 * int64_t(b + 1)) - s.timeAt(d0 + 256 * int64_t(b));
            const double cycles = double(ns) * 8.0 / 1000.0;
            const long tv = std::max(0L, std::lround((cycles - 28.0) / 32.0));
            fs.timing.push_back(uint8_t(tv >> 8));
            fs.timing.push_back(uint8_t(tv));
        }
    }
}

// Dump READ TRACK : MAME wd_fdc READ_TRACK_DATA — un octet toutes les 16
// cellules, resynchro sur $4489/$5224 (octet émis seulement si > 8 cellules
// s'étaient accumulées). Les cellules faibles reçoivent une valeur pseudo-
// aléatoire figée (le dump STX est, lui aussi, une lecture figée).
std::vector<uint8_t> readTrackDump(const Stream& s) {
    std::vector<uint8_t> out;
    out.reserve(std::size_t(s.n / 16 + 1));
    uint16_t sr = 0;
    uint8_t data = 0;
    int counter = 0;
    bool phase = false;
    uint32_t lfsr = 0xACE1u ^ uint32_t(s.t.cylinder * 2 + s.t.head);
    for (int64_t i = 0; i < s.n; ++i) {
        int bit;
        if (s.weak(i)) { lfsr = lfsr * 1103515245u + 12345u; bit = (lfsr >> 16) & 1; }
        else bit = s.cell(i);
        sr = uint16_t((sr << 1) | bit);
        ++counter;
        if (phase) data = uint8_t((data << 1) | bit);
        phase = !phase;
        if (counter != 16 && sr != kSyncA1 && sr != kSyncC2) continue;
        const bool emit = counter > 8;
        phase = false;
        counter = 0;
        // shift_reg_data() : bits de donnée des 16 dernières cellules.
        uint8_t d = 0;
        for (int k = 7; k >= 0; --k) d = uint8_t((d << 1) | ((sr >> (2 * k)) & 1));
        data = d;
        if (emit) out.push_back(data);
    }
    if (out.size() > 0xFFFF) out.resize(0xFFFF);
    return out;
}

std::vector<FoundSector> findSectors(const Stream& s, StxStats* st) {
    std::vector<FoundSector> res;
    if (s.n < 64) return res;
    // Le registre à décalage démarre plein des cellules de fin de piste (le
    // disque tourne : un IDAM à cheval sur l'index reste visible).
    uint16_t sr = s.raw16(-16);
    for (int64_t i = 0; i < s.n; ++i) {
        sr = uint16_t((sr << 1) | s.cell(i));
        if (sr != kSyncA1) continue;
        // 3 × $A1 consécutifs (le 1er vient d'être vu), puis l'IDAM $FE (ou $FF).
        if (s.raw16(i + 1) != kSyncA1) { i += 16; sr = s.raw16(i - 15); continue; }
        if (s.raw16(i + 17) != kSyncA1) { i += 32; sr = s.raw16(i - 15); continue; }
        const uint8_t mark = s.byte(i + 33);
        if (mark != 0xFE && mark != 0xFF) { i += 48; sr = s.raw16(i - 15); continue; }

        FoundSector fs;
        uint8_t f[10] = { 0xA1, 0xA1, 0xA1, mark };
        for (int k = 0; k < 6; ++k) f[4 + k] = s.byte(i + 49 + 16 * k);
        std::memcpy(fs.id, f + 4, 4);
        fs.idCrc = uint16_t((f[8] << 8) | f[9]);
        // BitPosition (convention Pasti) : 1er octet du champ ID, en bits DD de 4 µs.
        const int64_t idStart = i + 49;
        const uint64_t ns = s.timeAt(s.wrap(idStart));
        fs.bitPos = uint16_t(std::min<uint64_t>((ns + 2000) / 4000, 0xFFFF));
        if (crc16Ccitt(f, 10) != 0) {
            fs.status = StxImage::FLAG_RNF | StxImage::FLAG_CRC;    // CRC d'ID faux (Pasti)
            if (st) ++st->idCrcErrors;
        } else {
            readDataField(s, i + 48 + 96, fs, st);
        }
        res.push_back(std::move(fs));
        i += 48 + 96;                                    // reprise après le CRC de l'ID
        sr = s.raw16(i - 15);
    }
    std::stable_sort(res.begin(), res.end(),
                     [](const FoundSector& a, const FoundSector& b) { return a.bitPos < b.bitPos; });
    return res;
}

void put16le(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32le(std::vector<uint8_t>& v, uint32_t x) { put16le(v, x & 0xFFFF); put16le(v, x >> 16); }
void set32le(std::vector<uint8_t>& v, std::size_t at, uint32_t x) {
    for (int k = 0; k < 4; ++k) v[at + std::size_t(k)] = uint8_t(x >> (8 * k));
}

}  // namespace

bool decode(const std::vector<uint8_t>& raw, std::vector<CellTrack>& out, Info* info,
            std::string* err) {
    out.clear();
    Decoder d;
    if (!isIpf(raw.data(), raw.size())) { if (err) *err = "not an IPF image"; return false; }
    bool ok = d.scanAll(raw);
    if (ok) {
        uint64_t total = 0;
        for (auto& kv : d.tinfos) {
            TrackInfo& t = kv.second;
            if (!t.infoSet || !t.hasData) { ok = d.fail("IMGE/DATA records do not pair up"); break; }
            if (t.sizeCells == 0) continue;              // piste absente (non formatée)
            CellTrack ct;
            if (!d.track(t, ct)) { ok = false; break; }
            total += ct.cells.size();
            if (total > kMaxTotalCells) { ok = d.fail("image too large"); break; }
            out.push_back(std::move(ct));
        }
    }
    if (!ok) { out.clear(); if (err) *err = d.err; return false; }
    std::sort(out.begin(), out.end(), [](const CellTrack& a, const CellTrack& b) {
        return a.cylinder != b.cylinder ? a.cylinder < b.cylinder : a.head < b.head; });
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i].cylinder == out[i - 1].cylinder && out[i].head == out[i - 1].head) {
            out.clear(); if (err) *err = "duplicate track"; return false;
        }
    if (info) *info = d.info;
    return true;
}

bool toStx(const std::vector<CellTrack>& tracks, std::vector<uint8_t>& stx,
           std::vector<TrackRev>* revs, StxStats* stats) {
    stx.clear();
    if (revs) revs->clear();
    if (stats) *stats = StxStats{};
    if (tracks.empty() || tracks.size() > 255) return false;

    // En-tête STX (16 o) : « RSY\0 », version 3, outil, piste count, révision 2.
    stx = { 'R', 'S', 'Y', 0 };
    put16le(stx, 3);            // version
    put16le(stx, 0x00CC);       // outil d'imagerie : arbitraire (non lu)
    put16le(stx, 0);
    stx.push_back(uint8_t(tracks.size()));
    stx.push_back(2);           // révision 2 : tables de timing par secteur
    put32le(stx, 0);

    for (const CellTrack& ct : tracks) {
        if (ct.cells.size() != ct.timeNs.size() || ct.cylinder < 0 || ct.cylinder > 127
            || ct.head < 0 || ct.head > 1) return false;
        Stream s(ct);
        const uint64_t revNs = s.start.back();
        const uint32_t revBytes = uint32_t(std::min<uint64_t>((revNs + 16000) / 32000, 0xFFFF));
        if (revs) revs->push_back({ ct.cylinder, ct.head, revBytes });
        if (stats) ++stats->tracks;

        std::vector<FoundSector> secs;
        std::vector<uint8_t> image;
        if (!ct.noise && s.n >= 64) {
            secs = findSectors(s, stats);
            image = readTrackDump(s);
        }
        if (secs.size() > 255) secs.resize(255);
        if (stats) stats->sectors += int(secs.size());

        const std::size_t trkStart = stx.size();
        const bool withImage = !image.empty();
        uint32_t fuzzyBytes = 0;
        for (const auto& f : secs)
            if (!(f.status & StxImage::FLAG_RNF) && f.hasFuzzy) fuzzyBytes += uint32_t(f.data.size());

        put32le(stx, 0);                                         // blockSize (complété plus bas)
        put32le(stx, fuzzyBytes);
        put16le(stx, uint32_t(secs.size()));
        put16le(stx, StxImage::TRACK_FLAG_SECTOR_BLOCK | (withImage ? StxImage::TRACK_FLAG_IMAGE : 0));
        put16le(stx, std::min<uint32_t>(withImage ? uint32_t(image.size()) : revBytes, 0xFFFF));
        stx.push_back(uint8_t((ct.cylinder & 0x7f) | (ct.head << 7)));
        stx.push_back(0);

        // Descripteurs de secteur (offsets de données relatifs à pTrackData).
        uint32_t dataOff = withImage ? 2u + uint32_t(image.size()) : 0u;
        for (const auto& f : secs) {
            const bool hasData = !(f.status & StxImage::FLAG_RNF);
            put32le(stx, hasData ? dataOff : 0);
            put16le(stx, f.bitPos);
            put16le(stx, f.hasTiming ? f.readTimeUs : 0);
            stx.insert(stx.end(), f.id, f.id + 4);
            stx.push_back(uint8_t(f.idCrc >> 8));                // ID_CRC en big-endian
            stx.push_back(uint8_t(f.idCrc));
            stx.push_back(f.status);
            stx.push_back(0);
            if (hasData) dataOff += uint32_t(f.data.size());
        }
        for (const auto& f : secs)                               // masques fuzzy
            if (!(f.status & StxImage::FLAG_RNF) && f.hasFuzzy)
                stx.insert(stx.end(), f.fuzzy.begin(), f.fuzzy.end());
        if (withImage) {                                         // pTrackData : dump READ TRACK
            put16le(stx, uint32_t(image.size()));
            stx.insert(stx.end(), image.begin(), image.end());
        }
        bool anyTiming = false;
        for (const auto& f : secs)
            if (!(f.status & StxImage::FLAG_RNF)) {
                stx.insert(stx.end(), f.data.begin(), f.data.end());
                anyTiming |= f.hasTiming;
            }
        if (anyTiming) {                                         // tables de timing (rév. 2)
            std::size_t sz = 0;
            for (const auto& f : secs) if (!(f.status & StxImage::FLAG_RNF)) sz += f.timing.size();
            put16le(stx, 0x0005);
            put16le(stx, uint32_t(sz + 4));
            for (const auto& f : secs)
                if (!(f.status & StxImage::FLAG_RNF) && f.hasTiming)
                    stx.insert(stx.end(), f.timing.begin(), f.timing.end());
        }
        set32le(stx, trkStart, uint32_t(stx.size() - trkStart));
    }
    return true;
}

}  // namespace ipf
