// READ TRACK STX : fusion bornée des overlays dans le dump décodé original.
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
        std::fprintf(stderr, "[stx-raw-overlay] FAIL %s: got %lld, expected %lld\n",
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
uint16_t checksum(const uint8_t* bytes, size_t size) {
    uint16_t value = 0xFFFF;
    for (size_t n = 0; n < size; ++n) {
        value ^= uint16_t(bytes[n]) << 8;
        for (int b = 0; b < 8; ++b)
            value = uint16_t((value << 1) ^ ((value & 0x8000) ? 0x1021 : 0));
    }
    return value;
}
void put16(std::vector<uint8_t>& out, unsigned value) {
    out.push_back(uint8_t(value)); out.push_back(uint8_t(value >> 8));
}
void put32(std::vector<uint8_t>& out, unsigned value) {
    put16(out, value); put16(out, value >> 16);
}
std::vector<uint8_t> dumpTrack() {
    std::vector<uint8_t> dump(6272, 0x4E);
    for (int sector = 0; sector < 2; ++sector) {
        const size_t id = 100 + sector * 1000;
        const uint8_t header[] = {0xA1,0xA1,0xA1,0xFE,0,0,uint8_t(sector+1),2};
        std::copy(std::begin(header), std::end(header), dump.begin()+id-4);
        uint16_t crc = checksum(dump.data()+id-4, 8);
        dump[id+4] = uint8_t(crc >> 8); dump[id+5] = uint8_t(crc);
        const size_t dam = id + 40;
        dump[dam-3] = dump[dam-2] = dump[dam-1] = 0xA1; dump[dam] = 0xFB;
        std::fill_n(dump.begin()+dam+1, 512, uint8_t(0x31+sector));
        crc = checksum(dump.data()+dam-3, 516);
        dump[dam+513] = uint8_t(crc >> 8); dump[dam+514] = uint8_t(crc);
    }
    return dump;
}
void writeImage(const std::filesystem::path& path, const std::vector<uint8_t>& dump,
                int variant) {
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,1,0,0,0,0,0};
    const bool sync = variant == 1;
    put32(image, 16+32+2+(sync?2:0)+dump.size()+1024);
    put32(image, 0); put16(image, 2); put16(image, 0x41|(sync?0x80:0));
    put16(image, 6272); image.push_back(0); image.push_back(0);
    for (int sector = 0; sector < 2; ++sector) {
        put32(image, 2+(sync?2:0)+dump.size()+sector*512);
        unsigned bit = unsigned(100+sector*1000)*8;
        if (sector==0 && variant==2) ++bit;
        put16(image, bit); put16(image, 0);
        image.insert(image.end(), {0,0,uint8_t(sector+1),2});
        image.push_back(dump[100+sector*1000+4] ^ ((variant==6 && sector==0)?1:0));
        image.push_back(dump[100+sector*1000+5]);
        image.push_back(0); image.push_back(0);
    }
    if (sync) put16(image, 1);
    put16(image, dump.size()); image.insert(image.end(), dump.begin(), dump.end());
    image.insert(image.end(), 512, 0x31); image.insert(image.end(), 512, 0x32);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(image.data()), image.size());
}
void writeSector(Checks& checks, Rig& rig, int sector, uint8_t command, uint8_t value) {
    std::fill_n(rig.bus.ram.begin()+0x2000, 512, value);
    rig.reg(4, uint8_t(sector)); rig.mode(0x190); rig.data(1);
    rig.fdc.write8(0xFF860D,0); rig.fdc.write8(0xFF860B,0x20); rig.fdc.write8(0xFF8609,0);
    rig.mode(0x180); rig.data(command);
    checks.equal("WRITE SECTOR completion", rig.finish(), true);
    checks.equal("WRITE SECTOR status", rig.reg(0)&0x5D, 0);
}
void checkDump(Checks& checks, Rig& rig, const std::vector<uint8_t>& expected) {
    rig.read(1, 0xE8, 13);
    checks.equal("READ TRACK completion", rig.finish(), true);
    checks.equal("READ TRACK status", rig.reg(0)&0x5D, 0);
    checks.equal("READ TRACK DMA byte count", rig.address(), 0x2000+expected.size());
    checks.equal("exact decoded dump including gaps and neighbour", std::equal(
        expected.begin(), expected.end(), rig.bus.ram.begin()+0x2000), true);
}
}
int testStxRawTrackOverlay() {
    Checks checks;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path()
            / ("neost-stx-raw-overlay-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path,error); }
    } cleanup{work};
    for (int variant = 0; variant < 7; ++variant) {
        const auto path = work/(std::to_string(variant)+".stx");
        auto expected = dumpTrack();
        // Un secteur ambigu ne doit pas empêcher la fusion du secteur suivant.
        if (variant == 3) {
            expected[120] = expected[121] = expected[122] = 0xA1;
            expected[123] = 0xFB; // deuxième DAM avant le champ de données
        }
        if (variant == 4) expected[141] ^= 1; // payload différent de la description
        if (variant == 5) expected[653] ^= 1; // CRC données d'origine incorrecte
        if (variant == 6) expected[104] ^= 1; // ancre ID non concordante
        writeImage(path, expected, variant);
        Rig rig; rig.fdc.setHostWriteBack(true);
        checks.equal("mount", rig.fdc.loadImage(path.string()), true); rig.select(5);
        checkDump(checks,rig,expected);
        for (int sector=1; sector<=2; ++sector) {
            writeSector(checks,rig,sector,sector==1?0xA9:0xA8,uint8_t(0x71+sector));
            if (variant!=1 && (sector==2 || variant==0)) {
                size_t dam = 140+(sector-1)*1000;
                expected[dam] = sector==1?0xF8:0xFB;
                std::fill_n(expected.begin()+dam+1,512,uint8_t(0x71+sector));
                auto crc=checksum(expected.data()+dam-3,516);
                expected[dam+513]=uint8_t(crc>>8); expected[dam+514]=uint8_t(crc);
            }
            checkDump(checks,rig,expected);
        }
        rig.fdc.eject();
        Rig restored;
        checks.equal("remount",restored.fdc.loadImage(path.string()),true);
        restored.select(5);
        checkDump(checks,restored,expected);
    }
    std::fprintf(stderr,"[stx-raw-overlay] %d checks, %d FAIL\n",checks.count,checks.failures);
    return checks.failures;
}
#ifdef NEOST_STX_RAW_OVERLAY_TEST_MAIN
int main() { return testStxRawTrackOverlay()?1:0; }
#endif
