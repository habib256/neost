// ACSI/DMA : protocole des registres et datation de l'IRQ, sans ROM ni CPU.
// Référence : Hatari hdc.c Acsi_DmaTransfer / HDC_ACSI_InterruptHandler_Update.
#include "core/Bus.hpp"
#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"
#include "core/YM2149.hpp"
#include "io/Fdc.hpp"
#include "io/Mfp.hpp"
#include "io/UltraSatan.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void expect(const char* label, int64_t actual, int64_t expected) {
        ++count;
        if (actual == expected) return;
        ++failures;
        std::printf("  FAIL ACSI %-43s = %lld (attendu %lld)\n", label,
                    (long long)actual, (long long)expected);
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
        fdc.setScheduler(&sched);
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        sched.setCallback(Scheduler::ACSI_IRQ, [this] { fdc.onAcsiIrq(); });
        sched.setLiveClock([this] { return live; });
        reset();
    }
    void reset() { fdc.reset(true); mfp.reset(); sched.reset(); live = 0; }
    void advance(int64_t cycle) { live = cycle; sched.runTo(cycle); }
    void mode(uint16_t v) {
        fdc.write8(0xFF8606, uint8_t(v >> 8)); fdc.write8(0xFF8607, uint8_t(v));
    }
    void data(uint8_t v) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, v); }
    bool irq() { return !(mfp.read8(0xFFFA01) & 0x20); }
    void ack() { mode(0x80); (void)fdc.read8(0xFF8605); }
    void prepare(uint8_t opcode = 8, bool write = false, bool dmaEnabled = false) {
        mode(write ? 0x190 : 0x90); data(1);
        fdc.write8(0xFF8609, 0); fdc.write8(0xFF860B, 0x20); fdc.write8(0xFF860D, 0);
        const uint16_t commandMode = (write ? 0x108 : 8) | (dmaEnabled ? 0 : 0x80);
        mode(commandMode); data(opcode);
        mode(commandMode | 2);
        for (uint8_t v : {uint8_t(0), uint8_t(0), uint8_t(1), uint8_t(1), uint8_t(0)}) data(v);
    }
    void start(bool write = false) { mode(write ? 0x100 : 0); }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
};
}

// Phase WRITE retenue pendant qu'un autre CDB commence : la sélection de
// commande et la destination du DMA sont deux états distincts (Hatari FILE*).
int testAcsiWriteDestination() {
    Checks c;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-acsi-write-destination-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    const std::filesystem::path paths[] = {work / "a.img", work / "b.img"};
    for (int original : {0, 1}) for (bool snapshot : {false, true}) {
        std::vector<uint8_t> images[2];
        for (int disk = 0; disk < 2; ++disk) {
            images[disk].resize(2048);
            for (int sector = 0; sector < 4; ++sector)
                std::fill_n(images[disk].begin() + sector * 512, 512, uint8_t(0x30 + disk * 16 + sector));
            FILE* file = std::fopen(paths[disk].string().c_str(), "wb");
            if (!file) return 1;
            const bool saved = std::fwrite(images[disk].data(), 1, 2048, file) == 2048;
            std::fclose(file);
            if (!saved) return 1;
        }
        Rig r;
        for (int disk = 0; disk < 2; ++disk)
            c.expect("montage petite image", r.fdc.mountAcsi(paths[disk].string(), disk), true);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, 0xCC);
        r.mode(0x190); r.data(1);
        r.fdc.write8(0xFF860D, 0);
        r.fdc.write8(0xFF860B, 0x20);
        r.fdc.write8(0xFF8609, 0);
        r.mode(0x188); r.data(uint8_t((original << 5) | 0x0A));
        r.mode(0x18A);
        for (uint8_t byte : {0, 0, 2, 1, 0}) r.data(byte); // WRITE(6), LBA2, un secteur
        c.expect("DMA fermé conserve adresse", r.address(), 0x2000);
        // TEST UNIT READY incomplet vers l'autre cible : aucun nouveau CDB
        // complet ne remplace la phase précédente, mais target_ a déjà changé.
        r.mode(0x188); r.data(uint8_t((1 - original) << 5));
        if (snapshot) {
            std::vector<uint8_t> bytes;
            auto save = StateArchive::saver(bytes);
            r.sched.serialize(save); r.fdc.serialize(save);
            r.fdc.reset(true);
            auto load = StateArchive::loader(bytes.data(), bytes.size());
            r.sched.serialize(load); r.fdc.serialize(load);
            c.expect("archive phase différée", load.ok(), true);
        }
        r.start(true);
        c.expect("DMA transfert entier", r.address(), 0x2200);
        c.expect("IRQ fin différée", r.irq(), false);
        r.advance(999); c.expect("IRQ avant délai", r.irq(), false);
        r.advance(1000); c.expect("IRQ délai exact", r.irq(), true);
        c.expect("DMA sans erreur", r.fdc.read8(0xFF8607) & 1, 1);
        std::fill_n(images[original].begin() + 1024, 512, 0xCC);
        for (int disk = 0; disk < 2; ++disk) {
            FILE* file = std::fopen(paths[disk].string().c_str(), "rb");
            if (!file) return 1;
            std::vector<uint8_t> actual(2048);
            const size_t read = std::fread(actual.data(), 1, actual.size(), file);
            const int tail = std::fgetc(file);
            std::fclose(file);
            c.expect("image reste bornée", read == 2048 && tail == EOF, true);
            c.expect("destination et voisins exacts", actual == images[disk], true);
        }
    }
    // Même phase propriétaire pour une commande USWr, même si le prochain
    // octet sélectionne une cible qui n'appartient pas au boîtier UltraSatan.
    for (bool snapshot : {false, true}) {
        Rig r;
        UltraSatan device;
        r.fdc.attachUltraSatan(&device, 0);
        c.expect("montage cible hors boîtier", r.fdc.mountAcsi(paths[1].string(), 3), true);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, 0);
        const std::string name = "Retained";
        std::copy(name.begin(), name.end(), r.bus.ram.begin() + 0x2000);
        r.mode(0x190); r.data(1);
        r.fdc.write8(0xFF860D, 0);
        r.fdc.write8(0xFF860B, 0x20);
        r.fdc.write8(0xFF8609, 0);
        r.mode(0x188); r.data(0x1F);
        r.mode(0x18A);
        for (uint8_t byte : std::vector<uint8_t>{0x20, 'U', 'S', 'W', 'r', 'I', 'N', 'Q', 'R', 'N'}) r.data(byte);
        r.mode(0x188); r.data(0x60); // autre cible3, nouveau CDB incomplet
        if (snapshot) {
            std::vector<uint8_t> bytes;
            auto save = StateArchive::saver(bytes);
            r.sched.serialize(save); r.fdc.serialize(save); device.serialize(save);
            r.fdc.reset(true);
            auto load = StateArchive::loader(bytes.data(), bytes.size());
            r.sched.serialize(load); r.fdc.serialize(load); device.serialize(load);
            c.expect("archive phase US différée", load.ok(), true);
        }
        r.start(true);
        c.expect("nom écrit sur boîtier engagé", device.inquiryName() == name, true);
        c.expect("DMA US longueur secteur", r.address(), 0x2200);
        c.expect("IRQ US différée", r.irq(), false);
        r.advance(1000);
        c.expect("IRQ US finale", r.irq(), true);
    }
    std::fprintf(stderr, "[acsi-write-destination] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}
#ifdef NEOST_ACSI_WRITE_DESTINATION_TEST_MAIN
int main() { return testAcsiWriteDestination() ? 1 : 0; }
#endif
