// WD1772 : marques et CRC relus d'une piste écrite (WRITE TRACK sur .ST), et ID sans
// champ de données (piste .ST réécrite, puis STX) — recherche relancée jusqu'à 5 index.
// [B] WD177X-00 p. 1-9 (Read Sector : fenêtre de 43 octets, cinq tours), p. 1-12 (Write
// Sector écrit sa marque), p. 1-13 (table MFM $F5/$F6/$F7), p. 1-18 (trois A1).
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[fdc-written-marks] FAIL %s: got %lld, expected %lld\n",
                     label, static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    int64_t live = 0;
    int indexes = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        fdc.setSoundSink([this](FdcSound sound) { if (sound == FdcSound::Index) ++indexes; });
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80);
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) {
        fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value));
    }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t addressBits) { mode(0x80 | addressBits); return fdc.read8(0xFF8605); }
    void reg(uint16_t addressBits, uint8_t value) { mode(0x80 | addressBits); data(value); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    bool next() {
        const int64_t due = sched.nextDue();
        if (due < 0) return false;
        live = due; sched.runTo(due); return true;
    }
    bool finish() {
        for (unsigned n = 0; n < 200000 && !irq(); ++n) if (!next()) return false;
        return irq();
    }
    void dma(bool write, uint8_t count) {
        mode(write ? 0x090 : 0x190); mode(write ? 0x190 : 0x090);   // bascule : reset FIFO
        mode(write ? 0x190 : 0x090); data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
    }
    uint8_t command(bool write, uint8_t sector, uint8_t cmd, uint8_t count = 1) {
        dma(write, count);
        reg(write ? 0x104 : 0x004, sector);
        reg(write ? 0x100 : 0x000, cmd);
        finish();
        return reg(0);
    }
};

uint16_t crc16(const std::vector<uint8_t>& bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : bytes) {
        crc ^= uint16_t(b) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

// Un champ ID et (si dataMark) un champ données au format recommandé (p. 1-18).
void field(std::vector<uint8_t>& t, uint8_t sector, uint8_t dataMark, uint8_t fill,
           bool syncedId = true, bool idCrcByF7 = true, uint16_t literalIdCrc = 0,
           bool dataCrcByF7 = true) {
    t.insert(t.end(), 12, 0x00);
    if (syncedId) t.insert(t.end(), {0xF5, 0xF5, 0xF5});
    else          t.insert(t.end(), {0xA1, 0xA1, 0xA1});           // A1 AVEC horloge : pas une synchro
    t.insert(t.end(), {0xFE, 0, 0, sector, 2});
    if (idCrcByF7) t.push_back(0xF7);
    else { t.push_back(uint8_t(literalIdCrc >> 8)); t.push_back(uint8_t(literalIdCrc)); }
    t.insert(t.end(), 22, 0x4E);
    if (dataMark) {
        t.insert(t.end(), 12, 0x00);
        t.insert(t.end(), {0xF5, 0xF5, 0xF5, dataMark});
        t.insert(t.end(), 512, fill);
        if (dataCrcByF7) t.push_back(0xF7);
        else             t.insert(t.end(), {0x12, 0x34});           // CRC en clair, faux
    }
    t.insert(t.end(), 40, 0x4E);
}
} // namespace

int testFdcWrittenMarks() {
    Checks c;

    // --- 1. Relecture pure d'un flux ------------------------------------------
    {
        std::vector<uint8_t> t(60, 0x4E);
        const uint16_t goodCrc = crc16({0xA1, 0xA1, 0xA1, 0xFE, 0, 0, 4, 2});
        field(t, 1, 0xFB, 0x11);                                   // sain
        field(t, 2, 0xFB, 0x22, false);                            // A1 en clair : invisible
        field(t, 3, 0xFB, 0x33, true, false, goodCrc ^ 0x0101);    // CRC d'ID en clair, faux
        field(t, 4, 0xFB, 0x44, true, false, uint16_t(crc16({0xA1, 0xA1, 0xA1, 0xFE, 0, 0, 4, 2})));
        field(t, 5, 0, 0);                                         // ID sans données
        field(t, 6, 0xF8, 0x66, true, true, 0, false);             // données à CRC faux, effacées
        t.resize(6268, 0x4E);
        const auto s = Fdc::parseWrittenTrack(t.data(), int(t.size()), 6250);
        c.equal("flux : cinq IDs synchronisés", int(s.size()), 5);
        if (s.size() == 5) {
            c.equal("sain : CRC d'ID", s[0].idCrcOk, true);
            c.equal("sain : données", s[0].hasData && s[0].dataCrcOk && !s[0].deleted, true);
            c.equal("sain : contenu", s[0].data[511], 0x11);
            c.equal("A1 en clair : ID suivant = 3", s[1].sr, 3);
            c.equal("CRC d'ID en clair faux", s[1].idCrcOk, false);
            c.equal("CRC d'ID en clair juste", s[2].idCrcOk, true);
            c.equal("ID sans données", s[3].hasData, false);
            c.equal("CRC données faux", s[4].hasData && !s[4].dataCrcOk && s[4].deleted, true);
        }
        // Deux $F5 seulement : pas de marque ([B] p. 1-18, « exactly 3 bytes of A1 »).
        std::vector<uint8_t> two = {0x4E, 0xF5, 0xF5, 0xFE, 0, 0, 1, 2, 0xF7};
        two.resize(200, 0x4E);
        c.equal("deux A1 : pas d'ID", int(Fdc::parseWrittenTrack(two.data(), int(two.size()), 6250).size()), 0);
    }

    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-fdc-written-marks-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return c.failures + 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};

    // --- 2. Piste .ST réécrite : CRC, ID sans données, doublon -----------------
    {
        const auto path = work / "disk.st";
        std::vector<uint8_t> image(80u * 9u * 2u * 512u, 0x39);
        image[0x13] = 0xA0; image[0x14] = 0x05;
        image[0x18] = 9; image[0x1A] = 2;
        FILE* f = std::fopen(path.string().c_str(), "wb");
        if (!f) return c.failures + 1;
        std::fwrite(image.data(), 1, image.size(), f);
        std::fclose(f);
        Rig r;
        c.equal("ST : montage", r.fdc.loadImage(path.string()), true);
        r.select(5);
        std::vector<uint8_t> t(60, 0x4E);
        field(t, 7, 0, 0);                         // 7 : ID sans données…
        field(t, 1, 0xFB, 0x11);
        field(t, 2, 0xFB, 0x22, true, true, 0, false);   // 2 : CRC données faux
        field(t, 3, 0, 0);                         // 3 : ID sans données, seul
        field(t, 7, 0xFB, 0x77);                   // … et son doublon porteur de données
        field(t, 4, 0xFB, 0x44, false);            // 4 : marques sans synchro
        t.resize(6268, 0x4E);
        std::copy(t.begin(), t.end(), r.bus.ram.begin() + 0x2000);
        c.equal("ST : WRITE TRACK sans erreur", r.command(true, 1, 0xF8, 13) & 0x5D, 0);

        c.equal("ST : secteur sain", r.command(false, 1, 0x88) & 0x1C, 0);
        c.equal("ST : données saines", r.bus.ram[0x2000], 0x11);
        // [B] p. 1-3 : le DR garde le dernier octet assemblé.
        c.equal("ST : DR = dernier octet lu", r.reg(0x006), 0x11);
        c.equal("ST : CRC données faux → S3 seul", r.command(false, 2, 0x88) & 0x1C, 0x08);
        c.equal("ST : données servies malgré le CRC", r.bus.ram[0x2000], 0x22);
        c.equal("ST : marques sans synchro → RNF", r.command(false, 4, 0x88) & 0x1C, 0x10);
        r.indexes = 0;
        c.equal("ST : ID sans données → RNF", r.command(false, 3, 0x88) & 0x1C, 0x10);
        c.equal("ST : RNF au cinquième index", r.indexes, 5);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0));
        c.equal("ST : doublon porteur de données lu", r.command(false, 7, 0x88) & 0x1C, 0);
        c.equal("ST : données du doublon", r.bus.ram[0x2000], 0x77);
        // Write Sector n'attend pas de marque : il écrit la sienne.
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0x5C));
        r.bus.ram[0x2000 + 511] = 0x5D;
        c.equal("ST : Write Sector sur ID sans données", r.command(true, 3, 0xA8) & 0x5C, 0);
        c.equal("ST : DR = dernier octet écrit", r.reg(0x006), 0x5D);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0));
        c.equal("ST : relu après écriture", r.command(false, 3, 0x88) & 0x1C, 0);
        c.equal("ST : données écrites", r.bus.ram[0x2000], 0x5C);
        c.equal("ST : dernier octet écrit", r.bus.ram[0x2000 + 511], 0x5D);
    }

    // --- 3. STX : ID sans données (Pasti RNF seul) et doublon --------------------
    {
        const auto path = work / "nodata.stx";
        std::vector<uint8_t> img = {'R','S','Y',0, 3,0, 1,0, 0,0, 1, 2, 0,0,0,0};
        auto le16 = [&](unsigned v) { img.push_back(uint8_t(v)); img.push_back(uint8_t(v >> 8)); };
        auto le32 = [&](unsigned v) { le16(v); le16(v >> 16); };
        struct Sec { unsigned off; unsigned bytePos; uint8_t sector; uint8_t status; };
        const Sec secs[] = { {0, 100, 1, 0x10}, {0, 700, 1, 0x00}, {0, 1300, 2, 0x10} };
        le32(16 + 3 * 16 + 512); le32(0); le16(3); le16(1); le16(6250);
        img.push_back(0); img.push_back(0);                        // piste 0 face 0
        for (const Sec& s : secs) {
            le32(s.off); le16(s.bytePos * 8); le16(0);
            img.insert(img.end(), {0, 0, s.sector, 2});
            const uint16_t crc = crc16({0xA1, 0xA1, 0xA1, 0xFE, 0, 0, s.sector, 2});
            img.push_back(uint8_t(crc >> 8)); img.push_back(uint8_t(crc));
            img.push_back(s.status); img.push_back(0);
        }
        img.insert(img.end(), 512, 0xA7);
        FILE* f = std::fopen(path.string().c_str(), "wb");
        if (!f) return c.failures + 1;
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
        {
            Rig r;
            r.fdc.setHostWriteBack(true);
            c.equal("STX : montage", r.fdc.loadImage(path.string()), true);
            r.select(5);
            std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0));
            c.equal("STX : doublon porteur de données lu", r.command(false, 1, 0x88) & 0x1C, 0);
            c.equal("STX : données du doublon", r.bus.ram[0x2000], 0xA7);
            r.indexes = 0;
            c.equal("STX : ID sans données → RNF", r.command(false, 2, 0x88) & 0x1C, 0x10);
            c.equal("STX : RNF au cinquième index", r.indexes, 5);
            std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0x3D));
            c.equal("STX : Write Sector sur ID sans données", r.command(true, 2, 0xA8) & 0x5C, 0);
            std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0));
            c.equal("STX : relu après écriture", r.command(false, 2, 0x88) & 0x1C, 0);
            c.equal("STX : données écrites", r.bus.ram[0x2000], 0x3D);
        }
        Rig again;                                                 // .wd1772 au remontage
        c.equal("STX : remontage", again.fdc.loadImage(path.string()), true);
        again.select(5);
        std::fill_n(again.bus.ram.begin() + 0x2000, 512, uint8_t(0));
        c.equal("STX : remonté, secteur écrit lisible", again.command(false, 2, 0x88) & 0x1C, 0);
        c.equal("STX : remonté, données", again.bus.ram[0x2000], 0x3D);
    }

    std::fprintf(stderr, "[fdc-written-marks] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_FDC_WRITTEN_MARKS_TEST_MAIN
int main() { return testFdcWrittenMarks() ? 1 : 0; }
#endif
