// WRITE SECTOR normal sur un secteur STX F8 : données et marque doivent être réécrites.
// Registres WD1772 publics + Scheduler, sans accès aux internals du contrôleur.
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
        std::fprintf(stderr, "[stx-sector-rewrite] FAIL %s: got %lld, expected %lld\n",
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
    int pulses = 0;
    Rig() {
        sched.setLiveClock([this] { return live; });
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setScheduler(&sched);
        fdc.setHostWriteBack(false);
        fdc.setSoundSink([this](FdcSound sound) { if (sound == FdcSound::Index) ++pulses; });
        // Même connexion que Machine::Machine ; aucun accès FDC dans select().
        psg.setPortASink([this](uint8_t) { fdc.refreshDriveSide(); });
        psg.reset(); fdc.reset(true); mfp.reset();
        mfp.write8(0xFFFA09, 0x80); // capturer le front GPIP5 dans IPRB, même après clear IRQ
    }
    void select(uint8_t value) { psg.write8(0xFF8800, 14); psg.write8(0xFF8802, value); }
    void mode(uint16_t value) {
        fdc.write8(0xFF8606, uint8_t(value >> 8)); fdc.write8(0xFF8607, uint8_t(value));
    }
    void data(uint8_t value) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, value); }
    uint8_t reg(uint16_t addressBits) { mode(0x80 | addressBits); return fdc.read8(0xFF8605); }
    void reg(uint16_t addressBits, uint8_t value) { mode(0x80 | addressBits); data(value); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    bool pending() { return mfp.read8(0xFFFA0D) & 0x80; }
    void advance(int64_t at) { live = at; sched.runTo(at); }
    bool next() {
        const int64_t due = sched.nextDue();
        if (due < 0) return false;
        advance(due); return true;
    }
    bool finish() {
        // Ne pas lire STR : cela masquerait une sélection seulement PULL.
        for (unsigned n = 0; n < 100000 && !irq(); ++n) if (!next()) return false;
        return irq();
    }
    void read(uint8_t sector = 2, uint8_t command = 0x88, uint8_t count = 1) {
        mode(0x90); data(count);
        fdc.write8(0xFF860D, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF8609, 0);
        reg(4, sector); reg(0, command);
    }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
    int64_t armIndex() {
        select(5); reg(0, 0xD4); next();
        // Le premier handler initialise la phase puis attend précisément l'index.
        return sched.nextDue();
    }
};
}

namespace {
std::vector<uint8_t> formattedTrack(uint8_t mark) {
    std::vector<uint8_t> track(60, 0x4E);
    track.insert(track.end(), 12, 0);
    track.insert(track.end(), {0xA1, 0xA1, 0xA1, 0xFE, 0, 0, 1, 2, 0, 0});
    track.insert(track.end(), 22, 0x4E);
    track.insert(track.end(), 12, 0);
    track.insert(track.end(), {0xA1, 0xA1, 0xA1, mark});
    track.insert(track.end(), 512, 0xC6);
    track.insert(track.end(), 2, 0);
    track.resize(6268, 0x4E);
    return track;
}
uint16_t crc16(const uint8_t* bytes, size_t size) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= uint16_t(bytes[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = uint16_t((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
void verifyTrack(Checks& check, Rig& rig, uint8_t mark) {
    rig.read(1);
    check.equal("READ SECTOR termine", rig.finish(), true);
    check.equal("RECORD TYPE reflète DAM", rig.reg(0) & 0x7D, mark == 0xF8 ? 0x20 : 0);
    check.equal("données secteur intactes", std::all_of(rig.bus.ram.begin() + 0x2000,
            rig.bus.ram.begin() + 0x2200, [](uint8_t byte) { return byte == 0xD7; }), true);
    rig.read(1, 0xE8, 13);
    check.equal("READ TRACK termine", rig.finish(), true);
    check.equal("READ TRACK sans erreur", rig.reg(0) & 0x5D, 0);
    const std::vector<uint8_t> signature = {0xA1, 0xA1, 0xA1, mark};
    const auto begin = rig.bus.ram.begin() + 0x2000;
    const auto end = begin + 6256; // dernier FIFO incomplet non transféré
    const auto found = std::search(begin, end, signature.begin(), signature.end());
    const bool complete = found != end && end - found >= 518;
    check.equal("READ TRACK conserve DAM", complete, true);
    if (complete) {
        const uint16_t stored = uint16_t(found[516] << 8) | found[517];
        check.equal("CRC reconstruite couvre DAM et données", stored, crc16(&*found, 516));
    } else {
        check.equal("CRC reconstruite accessible", false, true);
    }
}
} // namespace

int testStxSectorRewrite() {
    Checks check;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-stx-sector-rewrite-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    for (uint8_t mark : {0xFB, 0xF8}) {
        const auto path = work / (mark == 0xF8 ? "deleted.stx" : "normal.stx");
        std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
        auto le16 = [&](unsigned value) { image.push_back(uint8_t(value)); image.push_back(uint8_t(value >> 8)); };
        auto le32 = [&](unsigned value) { le16(value); le16(value >> 16); };
        le32(16 + 512); le32(0); le16(1); le16(0); le16(6268 * 8);
        image.push_back(0); image.push_back(0);
        image.insert(image.end(), 512, 0xA5);
        FILE* file = std::fopen(path.string().c_str(), "wb");
        if (!file) return 1;
        const bool saved = std::fwrite(image.data(), 1, image.size(), file) == image.size();
        std::fclose(file);
        if (!saved) return 1;
        {
            Rig rig;
            rig.fdc.setHostWriteBack(true);
            check.equal("montage STX", rig.fdc.loadImage(path.string()), true);
            rig.select(5);
            const auto track = formattedTrack(mark);
            std::copy(track.begin(), track.end(), rig.bus.ram.begin() + 0x2000);
            rig.mode(0x190); rig.data(13);
            rig.fdc.write8(0xFF860D, 0);
            rig.fdc.write8(0xFF860B, 0x20);
            rig.fdc.write8(0xFF8609, 0);
            rig.mode(0x180); rig.data(0xF8);
            check.equal("WRITE TRACK termine", rig.finish(), true);
            check.equal("WRITE TRACK sans erreur", rig.reg(0) & 0x5D, 0);
            // Le secteur conserve son ID, mais WRITE SECTOR normal remplace
            // ses données et sa DAM par FB (MAME wd_fdc.cpp WRITE_BYTE_DONE).
            std::fill_n(rig.bus.ram.begin() + 0x2000, 512, 0xD7);
            rig.reg(4, 1);
            rig.mode(0x190); rig.data(1);
            rig.fdc.write8(0xFF860D, 0);
            rig.fdc.write8(0xFF860B, 0x20);
            rig.fdc.write8(0xFF8609, 0);
            rig.mode(0x180); rig.data(0xA8);
            check.equal("WRITE SECTOR termine", rig.finish(), true);
            check.equal("WRITE SECTOR sans erreur", rig.reg(0) & 0x5D, 0);
            std::vector<uint8_t> bytes;
            auto save = StateArchive::saver(bytes);
            rig.sched.serialize(save); rig.fdc.serialize(save);
            const int64_t savedLive = rig.live;
            verifyTrack(check, rig, 0xFB);
            auto load = StateArchive::loader(bytes.data(), bytes.size());
            rig.sched.serialize(load); rig.fdc.serialize(load);
            rig.live = savedLive;
            check.equal("archive piste écrite", load.ok(), true);
            verifyTrack(check, rig, 0xFB);
        }
        Rig remounted;
        check.equal("remontage fichier compagnon", remounted.fdc.loadImage(path.string()), true);
        remounted.select(5);
        verifyTrack(check, remounted, 0xFB);
    }
    std::fprintf(stderr, "[stx-sector-rewrite] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_STX_SECTOR_REWRITE_TEST_MAIN
int main() { return testStxSectorRewrite() ? 1 : 0; }
#endif
