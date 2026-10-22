// =============================================================================
//  ipf_image_logic.cpp — autotest du décodeur IPF (io/IpfImage) SANS image
//  commerciale : on FORGE une IPF SPS (blocs CAPS/INFO/IMGE/DATA, CRC32, flux
//  de blocs MFM/sync/weak, gap de type 0) puis on vérifie, étage par étage :
//    1. le conteneur : CRC32 des blocs et de la zone DATA, troncature, rejets ;
//    2. le flux de cellules : taille, rotation sur l'index, densité variable ;
//    3. la lecture « WD1772 » → STX : IDs, positions angulaires, données, CRC
//       d'ID / de données, marque F8, champ données absent, weak bits → fuzzy,
//       table de timing, dump READ TRACK ;
//    4. le montage par Fdc::loadImage et un READ SECTOR réel par le DMA.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST (GPL 3).
// =============================================================================
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/IpfImage.hpp"
#include "io/Mfp.hpp"
#include "io/StxImage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, long long got, long long expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[ipf] FAIL %s: got %lld, expected %lld\n", label, got, expected);
    }
};

void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int k = 3; k >= 0; --k) v.push_back(uint8_t(x >> (8 * k)));
}
void setBe32(std::vector<uint8_t>& v, std::size_t at, uint32_t x) {
    for (int k = 0; k < 4; ++k) v[at + std::size_t(k)] = uint8_t(x >> (8 * (3 - k)));
}
// Bloc IPF : identifiant, taille, CRC32 (calculé avec le champ CRC à zéro).
void record(std::vector<uint8_t>& out, const char* id, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> r(id, id + 4);
    be32(r, uint32_t(12 + body.size()));
    be32(r, 0);
    r.insert(r.end(), body.begin(), body.end());
    setBe32(r, 8, ipf::crc32(r.data(), r.size()));
    out.insert(out.end(), r.begin(), r.end());
}

// Description de bloc (flux de données) au format SPS, tailles en OCTETS (dmb=0).
struct Stream {
    std::vector<uint8_t> d;
    uint32_t cells = 0;
    void elem(uint8_t code, uint32_t param) {
        d.push_back(uint8_t((4u << 5) | code));
        be32(d, param);
    }
    void sync3() {                                   // 3 × $4489 en cellules brutes
        elem(1, 6);
        for (int i = 0; i < 3; ++i) { d.push_back(0x44); d.push_back(0x89); }
        cells += 48;
    }
    void bytes(const std::vector<uint8_t>& b, uint8_t code = 2) {
        elem(code, uint32_t(b.size()));
        d.insert(d.end(), b.begin(), b.end());
        cells += uint32_t(16 * b.size());
    }
    void fill(uint8_t v, int n, uint8_t code = 3) { bytes(std::vector<uint8_t>(std::size_t(n), v), code); }
    void weak(int n) { elem(5, uint32_t(n)); cells += uint32_t(16 * n); }
    void end() { d.push_back(0); }
};

enum Kind { NORMAL, BAD_DATA_CRC, BAD_ID_CRC, DELETED, NO_DAM, WEAK };

uint8_t pattern(int sector, int i) { return uint8_t(sector * 37 + i * 5); }

Stream sectorStream(int track, int sector, Kind kind) {
    Stream s;
    s.fill(0x00, 12);
    s.sync3();
    const uint8_t idh[8] = { 0xA1, 0xA1, 0xA1, 0xFE, uint8_t(track), 0, uint8_t(sector), 2 };
    uint16_t crc = ipf::crc16Ccitt(idh, 8);
    if (kind == BAD_ID_CRC) crc ^= 0x0101;
    s.bytes({ 0xFE, uint8_t(track), 0, uint8_t(sector), 2, uint8_t(crc >> 8), uint8_t(crc) });
    s.fill(0x4E, 22);
    s.fill(0x00, 12);
    if (kind == NO_DAM) {                           // pas de champ données : gap à la place
        s.fill(0x4E, 3 + 1 + 512 + 2, 2);
        s.end();
        return s;
    }
    s.sync3();
    const uint8_t mark = kind == DELETED ? 0xF8 : 0xFB;
    std::vector<uint8_t> data(512);
    for (int i = 0; i < 512; ++i) data[std::size_t(i)] = pattern(sector, i);
    uint8_t hdr[4] = { 0xA1, 0xA1, 0xA1, mark };
    uint16_t dcrc = ipf::crc16Ccitt(data.data(), data.size(), ipf::crc16Ccitt(hdr, 4));
    if (kind == BAD_DATA_CRC) dcrc ^= 0x8000;
    if (kind == WEAK) {                             // octets 100..103 faibles
        s.bytes({ mark });
        s.bytes(std::vector<uint8_t>(data.begin(), data.begin() + 100));
        s.weak(4);
        s.bytes(std::vector<uint8_t>(data.begin() + 104, data.end()));
        s.bytes({ uint8_t(dcrc >> 8), uint8_t(dcrc) });
    } else {
        std::vector<uint8_t> all = { mark };
        all.insert(all.end(), data.begin(), data.end());
        all.push_back(uint8_t(dcrc >> 8));
        all.push_back(uint8_t(dcrc));
        s.bytes(all);
    }
    s.end();
    return s;
}

constexpr uint32_t kGapCells = 40 * 16;
constexpr uint32_t kIndexCells = 60 * 16;

// IPF d'une piste (cylindre 0, face 0) de `kinds.size()` secteurs de 512 o.
std::vector<uint8_t> buildIpf(const std::vector<Kind>& kinds, uint32_t trackType = 2,
                              uint32_t* sizeCellsOut = nullptr) {
    std::vector<uint8_t> out;
    record(out, "CAPS", {});
    std::vector<uint8_t> info;
    for (uint32_t v : { 1u, 2u, 1u, 1234u, 1u, 0u, 0u, 0u, 0u, 0u, 20260101u, 0u,
                        2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }) be32(info, v);
    record(out, "INFO", info);

    std::vector<Stream> streams;
    for (std::size_t i = 0; i < kinds.size(); ++i)
        streams.push_back(sectorStream(0, int(i + 1), kinds[i]));
    const uint32_t bc = uint32_t(streams.size());
    std::vector<uint8_t> area(32 * bc);
    uint32_t size = 0;
    for (uint32_t i = 0; i < bc; ++i) {
        const uint32_t off = uint32_t(area.size());
        area.insert(area.end(), streams[i].d.begin(), streams[i].d.end());
        std::vector<uint8_t> desc;
        for (uint32_t v : { streams[i].cells, kGapCells, 0u, 1u, 1u, 0u, 0x4Eu, off }) be32(desc, v);
        std::copy(desc.begin(), desc.end(), area.begin() + 32 * i);
        size += streams[i].cells + kGapCells;
    }
    if (sizeCellsOut) *sizeCellsOut = size;
    std::vector<uint8_t> imge;
    for (uint32_t v : { 0u, 0u, trackType, 1u, size / 16, kIndexCells / 16, kIndexCells, 0u, 0u,
                        size, bc, 0u, 0u, 1u, 0u, 0u, 0u }) be32(imge, v);
    record(out, "IMGE", imge);
    std::vector<uint8_t> data;
    for (uint32_t v : { uint32_t(area.size()), uint32_t(area.size() * 8), ipf::crc32(area.data(), area.size()), 1u })
        be32(data, v);
    record(out, "DATA", data);
    out.insert(out.end(), area.begin(), area.end());
    return out;
}

// --- Banc FDC minimal (même câblage que Machine::Machine) ---------------------
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) { fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value)); }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t a) { mode(0x80 | a); return fdc.read8(0xFF8605); }
    void reg(uint16_t a, uint8_t v) { mode(0x80 | a); data(v); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    bool finish() {
        for (unsigned n = 0; n < 200000 && !irq(); ++n) {
            const int64_t due = sched.nextDue();
            if (due < 0) return false;
            live = due; sched.runTo(due);
        }
        return irq();
    }
    void readSector(uint8_t sector) {
        mode(0x90); mode(0x190); mode(0x90); data(1);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
        reg(4, sector); reg(0, 0x80);
    }
};
}  // namespace

int testIpfImage() {
    Checks c;
    const std::vector<Kind> kinds = { NORMAL, BAD_DATA_CRC, BAD_ID_CRC, DELETED, NO_DAM, WEAK, NORMAL };
    uint32_t sizeCells = 0;
    const std::vector<uint8_t> file = buildIpf(kinds, 2, &sizeCells);

    // ---- 1. conteneur ----------------------------------------------------
    std::vector<ipf::CellTrack> tracks;
    ipf::Info info;
    std::string err;
    c.equal("signature", ipf::isIpf(file.data(), file.size()), 1);
    c.equal("decode", ipf::decode(file, tracks, &info, &err), 1);
    if (!err.empty()) std::fprintf(stderr, "[ipf] decode error: %s\n", err.c_str());
    c.equal("encoder SPS", info.encoder, 2);
    {
        std::vector<uint8_t> bad = file;
        bad[40] ^= 1;                                       // octet du bloc INFO
        c.equal("record CRC32 checked", ipf::decode(bad, tracks, nullptr, nullptr), 0);
        bad = file;
        bad[bad.size() - 3] ^= 0x10;                        // zone DATA
        c.equal("DATA area CRC32 checked", ipf::decode(bad, tracks, nullptr, nullptr), 0);
        bad.assign(file.begin(), file.end() - 1);
        c.equal("truncated file rejected", ipf::decode(bad, tracks, nullptr, nullptr), 0);
        bad = file;
        bad[0] = 'X';
        c.equal("non-IPF rejected", ipf::decode(bad, tracks, nullptr, nullptr), 0);
    }

    // ---- 2. flux de cellules ----------------------------------------------
    c.equal("decode again", ipf::decode(file, tracks, nullptr, nullptr), 1);
    c.equal("one track", long(tracks.size()), 1);
    if (tracks.size() != 1) return c.failures;
    const ipf::CellTrack& t = tracks[0];
    c.equal("track cells", long(t.cells.size()), sizeCells);
    c.equal("nominal timing", long(t.totalNs()), 2000LL * sizeCells);
    // Bloc 0 commence à kIndexCells : 12 octets $00 puis la 1re synchro $4489.
    uint16_t sr = 0;
    for (uint32_t i = 0; i < 16; ++i) sr = uint16_t((sr << 1) | t.cells[kIndexCells + 12 * 16 + i]);
    c.equal("first sync after index offset", sr, 0x4489);

    // ---- 3. lecture WD1772 → STX ------------------------------------------
    std::vector<uint8_t> blob;
    std::vector<ipf::TrackRev> revs;
    ipf::StxStats st;
    c.equal("toStx", ipf::toStx(tracks, blob, &revs, &st), 1);
    c.equal("revolution bytes", revs.size() == 1 ? long(revs[0].bytes) : -1, long((sizeCells + 8) / 16));
    c.equal("stats ID CRC", st.idCrcErrors, 1);
    c.equal("stats data CRC (bad + weak)", st.dataCrcErrors, 2);
    c.equal("stats no data", st.noData, 1);
    c.equal("stats deleted", st.deleted, 1);
    c.equal("stats weak", st.fuzzySectors, 1);
    StxImage stx;
    c.equal("STX parse", stx.parse(blob), 1);
    StxImage::Track* tr = stx.findTrack(0, 0);
    c.equal("STX track", tr != nullptr, 1);
    if (!tr) return c.failures;
    c.equal("sector count", tr->sectorsCount, long(kinds.size()));
    const uint32_t sectorCells = sectorStream(0, 1, NORMAL).cells + kGapCells;
    for (std::size_t i = 0; i < tr->sectors.size() && i < kinds.size(); ++i) {
        const StxImage::Sector& s = tr->sectors[i];
        const uint32_t idCell = kIndexCells + uint32_t(i) * sectorCells + (12 + 4) * 16;
        c.equal("ID sector number", s.idSector, long(i + 1));
        c.equal("bit position = ID cell / 2", s.bitPosition, idCell / 2);
        const Kind k = kinds[i];
        const uint8_t want = k == BAD_ID_CRC ? 0x18 : k == NO_DAM ? 0x10 : k == BAD_DATA_CRC ? 0x08
                           : k == DELETED ? 0x20 : k == WEAK ? 0x88 : 0x00;
        c.equal("FDC status", s.fdcStatus, want);
        if (k == NORMAL || k == DELETED || k == BAD_DATA_CRC) {
            bool same = s.pData != nullptr;
            for (int b = 0; same && b < 512; ++b) same = s.pData[b] == pattern(int(i + 1), b);
            c.equal("sector data", same, 1);
        }
        if (k == WEAK) {
            c.equal("fuzzy mask present", s.pFuzzy != nullptr, 1);
            if (s.pFuzzy) {
                c.equal("fuzzy stable byte", s.pFuzzy[99], 0xFF);
                c.equal("fuzzy weak byte", s.pFuzzy[101], 0x00);
                c.equal("fuzzy after weak run", s.pFuzzy[105], 0xFF);
            }
        }
    }
    // Dump READ TRACK. Le 1er $A1 d'une synchro est mangé par la fausse synchro
    // $5224 que forme la transition $00 → $4489 (MAME wd_fdc : « 00.00.14.a1 »,
    // « ff.fe.c2.a1 »…) ; les deux suivants et l'IDAM restent lisibles.
    bool dumpOk = false, firstSyncEaten = true;
    if (tr->pTrackImage)
        for (int i = 1; i + 6 <= tr->trackImageSize && !dumpOk; ++i)
            if (std::memcmp(tr->pTrackImage + i, "\xA1\xA1\xFE\x00\x00\x01", 6) == 0) {
                dumpOk = true;
                firstSyncEaten = tr->pTrackImage[i - 1] != 0xA1;
            }
    c.equal("READ TRACK dump keeps address marks", dumpOk, 1);
    c.equal("READ TRACK first sync byte not clean (WD1772 resync)", firstSyncEaten, 1);
    c.equal("READ TRACK dump about one revolution", tr->trackImageSize > int(sizeCells / 16) - 32
                                                    && tr->trackImageSize <= int(sizeCells / 16), 1);

    // Densité variable (type 7 : bloc 1 à 2100 ns/cellule) → table de timing.
    {
        std::vector<ipf::CellTrack> vt;
        c.equal("variable density decode", ipf::decode(buildIpf({ NORMAL, NORMAL, NORMAL }, 7), vt, nullptr, nullptr), 1);
        std::vector<uint8_t> vb;
        ipf::StxStats vs;
        c.equal("variable density toStx", ipf::toStx(vt, vb, nullptr, &vs), 1);
        c.equal("one timed sector", vs.timedSectors, 1);
        StxImage vx;
        c.equal("variable density parse", vx.parse(vb), 1);
        StxImage::Track* vtr = vx.findTrack(0, 0);
        if (vtr && vtr->sectors.size() == 3) {
            const StxImage::Sector& s = vtr->sectors[1];
            c.equal("timing flag", s.fdcStatus & StxImage::FLAG_VARIABLE_TIME, 1);
            // 16 o à 2,1 µs/cellule = 4300,8 cycles → (4300,8 − 28) / 32 = 133,5 → 134.
            c.equal("timing table value", s.pTiming ? (s.pTiming[0] << 8 | s.pTiming[1]) : -1, 134);
            c.equal("nominal neighbour untouched", vtr->sectors[0].pTiming == nullptr, 1);
        } else {
            c.equal("variable density sectors", 0, 1);
        }
    }

    // ---- 4. montage + READ SECTOR par le DMA -------------------------------
    std::filesystem::path dir;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        dir = std::filesystem::temp_directory_path() / ("neost-ipf-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(dir, ec);
        if (ec) break;
    }
    if (!made) { c.equal("temp dir", 0, 1); return c.failures; }
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cl{dir};
    const auto path = dir / "synthetic.ipf";
    { std::ofstream o(path, std::ios::binary); o.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size())); }
    Rig rig;
    c.equal("mount", rig.fdc.loadImage(path.string()), 1);
    rig.select(5);
    std::fill_n(rig.bus.ram.begin() + 0x2000, 512, 0xCC);
    rig.readSector(4);                                       // secteur F8 (record type)
    c.equal("READ SECTOR completes", rig.finish(), 1);
    c.equal("READ SECTOR status = record type", rig.reg(0) & 0x3C, 0x20);
    bool same = true;
    for (int b = 0; b < 512 && same; ++b) same = rig.bus.ram[0x2000 + std::size_t(b)] == pattern(4, b);
    c.equal("READ SECTOR data through DMA", same, 1);
    rig.readSector(3);                                       // CRC d'ID faux → RNF
    c.equal("READ SECTOR bad ID completes", rig.finish(), 1);
    c.equal("READ SECTOR bad ID = RNF", rig.reg(0) & 0x10, 0x10);

    std::fprintf(stderr, "[ipf] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}

#ifdef NEOST_IPF_TEST_MAIN
int main() { return testIpfImage() ? 1 : 0; }
#endif
