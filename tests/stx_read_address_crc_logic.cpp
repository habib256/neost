// READ ADDRESS STX : le CRC ID invalide doit rester visible après le transfert.
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
        std::fprintf(stderr, "[stx-address-crc] FAIL %s: got %lld, expected %lld\n",
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

bool writeImage(const std::filesystem::path& path) {
    std::vector<uint8_t> image = {'R','S','Y',0,3,0,0,0,0,0,2,0,0,0,0,0};
    for (int side = 0; side < 2; ++side) {
        const bool bad = side == 0;
        put32(image, 32 + (bad ? 0 : 512));
        put32(image, 0);
        put16(image, 1);
        put16(image, 1);
        put16(image, 6272);
        image.push_back(uint8_t(side << 7));
        image.push_back(0);
        put32(image, 0);
        put16(image, 800);
        put16(image, 0);
        image.insert(image.end(), {0x27, uint8_t(side), 0x19, 2});
        const uint8_t id[] = {0xA1,0xA1,0xA1,0xFE,0x27,uint8_t(side),0x19,2};
        uint16_t crc = checksum(id, sizeof(id));
        if (bad) crc ^= 1;
        image.push_back(uint8_t(crc >> 8));
        image.push_back(uint8_t(crc));
        // RNF+CRC : mauvais CRC ID ; CRC seul : CRC des données, sans
        // incidence sur READ ADDRESS quand l'ID lui-même est valide.
        image.push_back(bad ? 0x18 : 0x08);
        image.push_back(0);
        if (!bad) image.insert(image.end(), 512, 0xB6);
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(image.data()), image.size());
    return bool(file);
}
std::vector<uint8_t> idBytes(int side) {
    const uint8_t header[] = {0xA1,0xA1,0xA1,0xFE,0x27,uint8_t(side),0x19,2};
    uint16_t crc = checksum(header, sizeof(header));
    if (side == 0) crc ^= 1;
    return {0x27,uint8_t(side),0x19,2,uint8_t(crc >> 8),uint8_t(crc)};
}
void readAddress(Checks& checks, Rig& rig, int side) {
    rig.select(side == 0 ? 5 : 4);
    rig.reg(0, 0xC8);
    checks.equal("READ ADDRESS completion", rig.finish(), true);
    checks.equal("READ ADDRESS CRC without RNF", rig.reg(0) & 0x1D, side == 0 ? 8 : 0);
    checks.equal("ID track copied to sector register", rig.reg(4), 0x27);
}
} // namespace
int testStxReadAddressCrc() {
    Checks checks;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path()
            / ("neost-stx-address-crc-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    const auto path = work / "ids.stx";
    checks.equal("fixture saved", writeImage(path), true);
    Rig rig;
    checks.equal("mount", rig.fdc.loadImage(path.string()), true);
    std::fill_n(rig.bus.ram.begin() + 0x2000, 512, 0xCC);
    rig.mode(0x90);
    rig.data(1);
    rig.fdc.write8(0xFF860D, 0);
    rig.fdc.write8(0xFF860B, 0x20);
    rig.fdc.write8(0xFF8609, 0);
    std::vector<uint8_t> expected;
    for (int side : {0, 0, 1}) {
        readAddress(checks, rig, side);
        const auto id = idBytes(side);
        expected.insert(expected.end(), id.begin(), id.end());
    }
    // Trois champs de six octets : un seul FIFO de 16 octets a atteint la RAM.
    checks.equal("six bytes transferred even with bad ID CRC", std::equal(
        expected.begin(), expected.begin() + 16, rig.bus.ram.begin() + 0x2000), true);
    checks.equal("exact FIFO DMA advance", rig.address(), 0x2010);
    checks.equal("RAM beyond completed FIFO untouched", std::all_of(
        rig.bus.ram.begin() + 0x2010, rig.bus.ram.begin() + 0x2200,
        [](uint8_t value) { return value == 0xCC; }), true);
    readAddress(checks, rig, 0); // CRC redevient visible après une commande valide
    std::fprintf(stderr, "[stx-address-crc] %d checks, %d FAIL\n", checks.count, checks.failures);
    return checks.failures;
}
#ifdef NEOST_STX_READ_ADDRESS_CRC_TEST_MAIN
int main() { return testStxReadAddressCrc() ? 1 : 0; }
#endif
