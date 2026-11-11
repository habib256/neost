// =============================================================================
//  WrittenTrack.cpp — cf. WrittenTrack.hpp.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/WrittenTrack.hpp"
#include <algorithm>

namespace wdtrack {

uint16_t crc16(const uint8_t* buf, int n) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; ++i) {
        crc ^= uint16_t(buf[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

// Relit un flux WRITE TRACK comme le WD1772 relira la piste.
//
// Écriture ([B] WD177X-00 p. 1-13, table MFM) : $F5 → A1 sans horloge (synchro) et
// préchargement du CRC, $F6 → C2 sans horloge, $F7 → deux octets de CRC ; tout autre
// octet, $FE/$FB/$F8 compris, s'écrit avec son horloge normale : ce n'est pas une marque.
// ⚠ [F] La fiche dit que CHAQUE $F5 précharge le CRC ; on ne précharge qu'au premier d'une
// suite, faute de quoi les 3 $F5 du format recommandé donneraient un CRC que la lecture
// (qui le précharge au premier A1, [D] MAME wd_fdc.cpp:1840 @ ce391499d37d, crc = 0x443b
// après un A1) refuserait. Même convention que readTrackST. Tranché par un format à 1 ou
// 2 $F5 relu sur un vrai 1772.
//
// Lecture : une marque d'ID est $FE derrière trois A1 de synchro ([B] p. 1-18 : « 3 bytes
// of A1 must be used in MFM » ; [D] MAME wd_fdc.cpp:1853-1880 @ ce391499d37d exige les trois).
// Le CRC d'ID est comparé à ce qui est sur la piste ([B] p. 1-10). La marque de données
// est cherchée dans les 43 octets qui suivent le dernier octet de CRC d'ID ([B] p. 1-9) :
// la première synchro rencontrée tranche — $FB/$F8 derrière, champ données ; autre chose,
// pas de données (MAME : SEARCH_ADDRESS_MARK_DATA_FAILED, :1983-2000). Un champ données
// coupé par l'index se relit sur le début de la piste, réécrit depuis : CRC faux.
std::vector<Sector> parse(const uint8_t* raw, int n, int trackLen) {
    std::vector<uint8_t> disk, sync;                   // octets relus, 1 = A1 de synchro
    disk.reserve(size_t(std::max(trackLen, 0)));
    uint16_t crc = 0xFFFF;
    bool prevF5 = false;
    auto upd = [&](uint8_t b) {
        crc ^= uint16_t(b) << 8;
        for (int k = 0; k < 8; ++k)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    };
    auto put = [&](uint8_t b, uint8_t s) { disk.push_back(b); sync.push_back(s); };
    for (int i = 0; i < n && int(disk.size()) < trackLen; ++i) {
        const uint8_t b = raw[i];
        if (b == 0xF5) { if (!prevF5) crc = 0xFFFF; upd(0xA1); put(0xA1, 1); prevF5 = true; continue; }
        prevF5 = false;
        if (b == 0xF7) { const uint16_t c = crc; put(uint8_t(c >> 8), 0); put(uint8_t(c), 0); continue; }
        const uint8_t v = (b == 0xF6) ? 0xC2 : b;
        upd(v); put(v, b == 0xF6 ? 2 : 0);
    }
    if (int(disk.size()) > trackLen) { disk.resize(size_t(trackLen)); sync.resize(size_t(trackLen)); }
    const int len = int(disk.size());
    std::vector<Sector> out;
    if (len < 10) return out;
    auto at = [&](int p) { return disk[size_t(p % len)]; };
    auto syncRun = [&](int p) {                        // trois A1 de synchro consécutifs en p
        return p + 3 < len && sync[size_t(p)] == 1 && sync[size_t(p + 1)] == 1 && sync[size_t(p + 2)] == 1;
    };
    constexpr int DAM_WINDOW = 43;                     // [B] p. 1-9, double densité
    for (int p = 0; p + 10 <= len; ++p) {
        if (!syncRun(p) || disk[size_t(p + 3)] != 0xFE) continue;
        Sector s;
        s.tr = disk[size_t(p + 4)]; s.hd = disk[size_t(p + 5)];
        s.sr = disk[size_t(p + 6)]; s.sz = disk[size_t(p + 7)];
        s.idPos = p + 3;
        s.idCrc = uint16_t((disk[size_t(p + 8)] << 8) | disk[size_t(p + 9)]);
        s.idCrcOk = crc16(&disk[size_t(p)], 8) == s.idCrc;
        const int dataLen = 128 << (s.sz & 3);
        s.data.assign(size_t(dataLen), 0);
        s.hasData = false;
        // Marque de données : son octet ($FB/$F8) au plus 43 octets après le CRC d'ID (p+9).
        for (int q = p + 10; q + 3 <= p + 9 + DAM_WINDOW && q + 3 < len; ++q) {
            if (!syncRun(q)) continue;
            const uint8_t mark = disk[size_t(q + 3)];
            if (mark == 0xFB || mark == 0xF8) {
                s.hasData = true;
                s.deleted = mark == 0xF8;
                std::vector<uint8_t> rec(size_t(4 + dataLen));
                for (int k = 0; k < 4 + dataLen; ++k) rec[size_t(k)] = at(q + k);
                std::copy(rec.begin() + 4, rec.end(), s.data.begin());
                const uint16_t stored = uint16_t((at(q + 4 + dataLen) << 8) | at(q + 5 + dataLen));
                s.dataCrcOk = crc16(rec.data(), int(rec.size())) == stored
                           && q + 6 + dataLen <= len;  // coupé par l'index : réécrit, faux
            }
            break;                                     // la première synchro tranche
        }
        out.push_back(std::move(s));
        p += 3;                                        // l'ID suivant ne chevauche pas celui-ci
    }
    return out;
}

} // namespace wdtrack
