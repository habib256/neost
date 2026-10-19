// WRITE SECTOR STX : bit0 choisit FB/F8, trailer NDAM et commande en vol.
// Registres WD1772 publics + Scheduler, sans accès aux internals du contrôleur.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[stx-sector-dam] FAIL %s: got %lld, expected %lld\n",
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
void verifyTrack(Checks& check, Rig& rig, uint8_t mark, uint8_t payload) {
    rig.read(1);
    check.equal("READ SECTOR termine", rig.finish(), true);
    check.equal("RECORD TYPE reflète DAM", rig.reg(0) & 0x7D, mark == 0xF8 ? 0x20 : 0);
    check.equal("données secteur intactes", std::all_of(rig.bus.ram.begin() + 0x2000,
            rig.bus.ram.begin() + 0x2200, [payload](uint8_t byte) { return byte == payload; }), true);
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

int testStxSectorDam() {
    Checks check;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-stx-sector-dam-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    for (uint8_t initialMark : {0xFB, 0xF8}) {
        const auto path = work / (initialMark == 0xF8 ? "deleted.stx" : "normal.stx");
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
        Rig rig;
        rig.fdc.setHostWriteBack(true);
        check.equal("montage STX", rig.fdc.loadImage(path.string()), true);
        rig.select(5);
        const auto track = formattedTrack(initialMark);
        std::copy(track.begin(), track.end(), rig.bus.ram.begin() + 0x2000);
        rig.mode(0x190); rig.data(13);
        rig.fdc.write8(0xFF860D, 0); rig.fdc.write8(0xFF860B, 0x20); rig.fdc.write8(0xFF8609, 0);
        rig.mode(0x180); rig.data(0xF8);
        check.equal("WRITE TRACK termine", rig.finish(), true);
        check.equal("WRITE TRACK sans erreur", rig.reg(0) & 0x5D, 0);
        uint8_t payload = 0xD7;
        for (uint8_t command : {0xA9, 0xA8, 0xA9}) {
            std::fill_n(rig.bus.ram.begin() + 0x2000, 512, payload);
            rig.reg(4, 1);
            rig.mode(0x190); rig.data(1);
            rig.fdc.write8(0xFF860D, 0); rig.fdc.write8(0xFF860B, 0x20); rig.fdc.write8(0xFF8609, 0);
            rig.mode(0x180); rig.data(command);
            unsigned events = 0;
            while (++events < 10000 && rig.next()) if (rig.address() >= 0x2010) break;
            check.equal("DMA engagé avant snapshot", events < 10000, true);
            check.equal("commande occupée", rig.fdc.read8(0xFF8605) & 1, 1);
            rig.data(command ^ 1); // nouvelle DAM opposée, ignorée pendant le transfert
            // Le média reste monté : seule la commande en vol est sauvegardée,
            // pas le contenu de l'image ni celui des overlays.
            std::vector<uint8_t> bytes;
            auto save = StateArchive::saver(bytes);
            rig.sched.serialize(save); rig.fdc.serialize(save);
            rig.data(0xD0); // aborter sans dispatch : le load doit restaurer CR et la phase
            auto load = StateArchive::loader(bytes.data(), bytes.size());
            rig.sched.serialize(load); rig.fdc.serialize(load);
            check.equal("archive commande en vol", load.ok(), true);
            check.equal("WRITE SECTOR termine", rig.finish(), true);
            check.equal("WRITE SECTOR sans erreur", rig.reg(0) & 0x5D, 0);
            const uint8_t mark = command & 1 ? 0xF8 : 0xFB;
            verifyTrack(check, rig, mark, payload);
            Rig remounted;
            check.equal("remontage compagnon", remounted.fdc.loadImage(path.string()), true);
            remounted.select(5);
            verifyTrack(check, remounted, mark, payload);
            ++payload;
        }
        // Le trailer est optionnel : injecter une extension reconnue ou
        // incomplète dans le SECT réel, en conservant les données et le TRCK.
        const auto companion = work / (initialMark == 0xF8 ? "deleted.wd1772" : "normal.wd1772");
        std::ifstream input(companion, std::ios::binary);
        std::vector<uint8_t> persisted((std::istreambuf_iterator<char>(input)), {});
        input.close();
        const bool headerOk = persisted.size() >= 36 &&
            std::equal(persisted.begin() + 16, persisted.begin() + 20, "SECT");
        check.equal("compagnon SECT standard", headerOk, true);
        if (!headerOk) return check.failures;
        check.equal("version compagnon inchangée", persisted[6] == 1 && persisted[7] == 0, true);
        const uint32_t blockLen = uint32_t(persisted[20]) << 24 | uint32_t(persisted[21]) << 16 |
                                  uint32_t(persisted[22]) << 8 | persisted[23];
        const unsigned dataSize = unsigned(persisted[34]) << 8 | persisted[35];
        const size_t dataEnd = 36 + dataSize;
        const size_t blockEnd = 20 + blockLen;
        if (dataEnd > blockEnd || blockEnd > persisted.size()) return 1;
        const std::vector<std::vector<uint8_t>> tails = {
            {}, {'N','D','A','M'}, {'N','D','A','M',0}, {'X','D','A','M',0xF8},
            {'N','D','A','M',0xFB}, {'N','D','A','M',0xF8}
        };
        for (const auto& tail : tails) {
            auto modified = persisted;
            modified.erase(modified.begin() + dataEnd, modified.begin() + blockEnd);
            modified.insert(modified.begin() + dataEnd, tail.begin(), tail.end());
            const uint32_t newLength = 16 + dataSize + uint32_t(tail.size());
            for (int byte = 0; byte < 4; ++byte) modified[20 + byte] = uint8_t(newLength >> (24 - 8 * byte));
            FILE* output = std::fopen(companion.string().c_str(), "wb");
            if (!output) return 1;
            const bool written = std::fwrite(modified.data(), 1, modified.size(), output) == modified.size();
            std::fclose(output);
            if (!written) return 1;
            Rig restored;
            check.equal("montage ancien/trailer borné", restored.fdc.loadImage(path.string()), true);
            restored.select(5);
            const bool deleted = tail.size() == 5 && tail[0] == 'N' && tail[4] == 0xF8;
            verifyTrack(check, restored, deleted ? 0xF8 : 0xFB, uint8_t(payload - 1));
        }
    }
    std::fprintf(stderr, "[stx-sector-dam] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_STX_SECTOR_DAM_TEST_MAIN
int main() { return testStxSectorDam() ? 1 : 0; }
#endif
