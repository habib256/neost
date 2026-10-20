// STX : WRITE SECTOR sur un secteur à erreur CRC DONNÉES (CRC sans RNF) et durée
// standard d'un secteur sur piste HD. Registres WD1772 publics + Scheduler.
// Le WD1772 ne contrôle que le CRC du champ ID avant d'écrire un champ données neuf
// (MAME wd_fdc.cpp write_sector) : l'écriture réussit et la relecture est saine.
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
        std::fprintf(stderr, "[stx-data-crc-write] FAIL %s: got %lld, expected %lld\n",
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
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) {
        fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value));
    }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t addressBits) { mode(0x80 | addressBits); return fdc.read8(0xFF8605); }
    void reg(uint16_t addressBits, uint8_t value) { mode(0x80 | addressBits); data(value); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    void advance(int64_t at) { live = at; sched.runTo(at); }
    bool finish() {
        for (unsigned n = 0; n < 200000 && !irq(); ++n) {
            const int64_t due = sched.nextDue();
            if (due < 0) return false;
            advance(due);
        }
        return irq();
    }
    void command(uint8_t sector, uint8_t cmd, uint8_t count, bool write) {
        // SR d'abord : reg() repasse en mode $80, et basculer le bit 8 (sens DMA)
        // après le compte remettrait celui-ci à zéro.
        reg(4, sector);
        mode(write ? 0x190 : 0x90); data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
        mode(write ? 0x180 : 0x80); data(cmd);
    }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
};

uint16_t crc16(std::initializer_list<uint8_t> bytes) {
    uint16_t crc = 0xFFFF;
    for (uint8_t b : bytes) {
        crc ^= uint16_t(b) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

// Une piste 0 face 0 à blocs secteur : `positions` donne la BitPosition (cellules
// DD) de chaque secteur 1..n, `status` le statut FDC Pasti du secteur 1.
bool writeStx(const std::filesystem::path& path, uint16_t mfmSize,
              const std::vector<uint16_t>& positions, uint8_t status) {
    const unsigned n = unsigned(positions.size());
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    auto le16 = [&](unsigned v) { image.push_back(uint8_t(v)); image.push_back(uint8_t(v >> 8)); };
    auto le32 = [&](unsigned v) { le16(v); le16(v >> 16); };
    le32(16 + n * 16 + n * 512); le32(0); le16(n); le16(1); le16(mfmSize);
    image.push_back(0); image.push_back(0);
    for (unsigned s = 0; s < n; ++s) {
        le32(s * 512); le16(positions[s]); le16(0);
        const uint8_t id = uint8_t(s + 1);
        image.insert(image.end(), {0, 0, id, 2});
        const uint16_t crc = crc16({0xA1, 0xA1, 0xA1, 0xFE, 0, 0, id, 2});
        image.push_back(uint8_t(crc >> 8)); image.push_back(uint8_t(crc));
        image.push_back(s == 0 ? status : 0); image.push_back(0);
    }
    for (unsigned s = 0; s < n; ++s) image.insert(image.end(), 512, uint8_t(0xA5 + s));
    FILE* f = std::fopen(path.string().c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(image.data(), 1, image.size(), f) == image.size();
    return std::fclose(f) == 0 && ok;
}
} // namespace

int testStxDataCrcWrite() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-stx-data-crc-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};

    {
        // Secteur 1 : CRC données faux (0x08 sans RNF), ID correct.
        const auto path = work / "crc.stx";
        if (!writeStx(path, 6268, {100 * 8}, 0x08)) return 1;
        Rig r;
        c.equal("montage STX CRC", r.fdc.loadImage(path.string()), true);
        r.select(5);
        r.command(1, 0x88, 1, false);
        c.equal("READ d'origine termine", r.finish(), true);
        c.equal("READ d'origine : CRC données", r.reg(0) & 0x18, 0x08);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0xD7));
        r.command(1, 0xA8, 1, true);
        c.equal("WRITE SECTOR termine", r.finish(), true);
        c.equal("WRITE SECTOR sans erreur", r.reg(0) & 0x5C, 0);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0));
        r.command(1, 0x88, 1, false);
        c.equal("relecture termine", r.finish(), true);
        c.equal("relecture : champ données neuf, CRC juste", r.reg(0) & 0x18, 0);
        c.equal("relecture : données écrites", r.bus.ram[0x2000], 0xD7);
        c.equal("relecture : dernier octet", r.bus.ram[0x21FF], 0xD7);
    }
    {
        // Piste HD (12 536 o) : secteurs consécutifs espacés de 614 o HD comme un
        // formatage 18 spt. Un secteur standard dure 512×16 µs ; à 32 µs/o (DD),
        // l'ID suivant est déjà passé et chaque secteur coûte un tour de plus.
        const auto path = work / "hd.stx";
        std::vector<uint16_t> positions;
        for (int s = 0; s < 3; ++s) positions.push_back(uint16_t((200 + s * 614) * 8));
        if (!writeStx(path, 12536, positions, 0)) return 1;
        Rig r;
        c.equal("montage STX HD", r.fdc.loadImage(path.string()), true);
        r.select(5);
        r.command(1, 0x98, 3, false);                    // READ multi-secteurs
        // Date à laquelle l'adresse DMA franchit la fin de chaque secteur.
        int64_t end1 = -1, end2 = -1;
        for (unsigned n = 0; n < 200000 && !r.irq() && end2 < 0; ++n) {
            const int64_t due = r.sched.nextDue();
            if (due < 0) break;
            r.advance(due);
            const uint32_t at = r.address();
            if (end1 < 0 && at >= 0x2200) end1 = r.live;
            if (end2 < 0 && at >= 0x2400) end2 = r.live;
        }
        c.equal("READ HD : deux secteurs transférés", end1 >= 0 && end2 >= 0, true);
        // Écart attendu ≈ 614 o HD × 128 cyc = 78 592 cyc ; à 32 µs/o, un tour de plus.
        c.equal("READ HD : secteur suivant sans tour perdu", end2 - end1 < 200000, true);
        c.equal("READ HD : écart au moins la durée d'un secteur HD", end2 - end1 >= 512 * 128, true);
    }
    std::fprintf(stderr, "[stx-data-crc-write] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_STX_DATA_CRC_WRITE_TEST_MAIN
int main() { return testStxDataCrcWrite() ? 1 : 0; }
#endif
