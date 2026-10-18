// FDC/DMA : retenue d'adresse STF et reset du FIFO, par les registres publics.
// Référence : Hatari fdc.c FDC_DmaAddress_WriteByte / FDC_ResetDMA.
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
    void equal(const char* label, uint32_t got, uint32_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::printf("  FAIL FDC register %-42s = $%06X (attendu $%06X)\n", label, got, expected);
    }
};
struct Rig {
    Bus bus{512 * 1024};
    YM2149 psg;
    Mfp mfp;
    Scheduler sched;
    Fdc fdc{bus, psg, mfp};
    Rig() {
        fdc.setScheduler(&sched);
        sched.setCallback(Scheduler::FDC, [this] { fdc.onFdcEvent(); });
        fdc.setHostWriteBack(false);
        fdc.reset(true);
        psg.write8(0xFF8800, 14); psg.write8(0xFF8802, 5); // A, face0
    }
    void mode(uint16_t v) {
        fdc.write8(0xFF8606, uint8_t(v >> 8)); fdc.write8(0xFF8607, uint8_t(v));
    }
    void data(uint8_t v) { fdc.write8(0xFF8604, 0); fdc.write8(0xFF8605, v); }
    void address(uint32_t v) { // ordre matériel sûr : bas, milieu, haut
        fdc.write8(0xFF860D, uint8_t(v)); fdc.write8(0xFF860B, uint8_t(v >> 8));
        fdc.write8(0xFF8609, uint8_t(v >> 16));
    }
    uint32_t address() {
        return (uint32_t(fdc.read8(0xFF8609)) << 16)
             | (uint32_t(fdc.read8(0xFF860B)) << 8) | fdc.read8(0xFF860D);
    }
    uint16_t dmaStatus() {
        return uint16_t((fdc.read8(0xFF8606) << 8) | fdc.read8(0xFF8607));
    }
    bool busy() { return fdc.read8(0xFF8605) & 1; } // mode FDC statut requis
    bool finish(int64_t budget = 16'000'000) {
        const int64_t limit = sched.now() + budget;
        unsigned events = 0;
        while (busy()) {
            const int64_t due = sched.nextDue();
            if (due < 0 || due > limit || ++events > 100000) return false;
            sched.runTo(due);
        }
        return true;
    }
    void command(uint8_t cmd, uint8_t count, bool write = false) {
        mode(write ? 0x190 : 0x90); data(count);
        mode(write ? 0x184 : 0x84); data(1); // secteur1
        mode(write ? 0x180 : 0x80); data(cmd);
    }
};
}

int testFdcRegisters() {
    Checks c;
    // Config_IsMachineST() désigne ST ET Mega ST dans Hatari. Les IMP STE
    // n'ont pas cette retenue, même si le même octet passe de $80 à $00.
    const struct { uint32_t old; uint8_t offset, value; uint32_t st, ste; } cases[] = {
        {0x123480, 0x0D, 0x00, 0x123500, 0x123400},
        {0x12FF80, 0x0D, 0x00, 0x130000, 0x12FF00},
        {0x12FEFE, 0x0D, 0x7F, 0x12FF7E, 0x12FE7E},
        {0x123400, 0x0D, 0xFE, 0x1234FE, 0x1234FE},
        {0x128002, 0x0B, 0x00, 0x130002, 0x120002},
        {0x120002, 0x0B, 0x80, 0x128002, 0x128002},
        {0x3FFF80, 0x0D, 0x00, 0x000000, 0x3FFF00},
        {0x3F8002, 0x0B, 0x00, 0x000002, 0x3F0002},
        {0x123480, 0x09, 0xFF, 0x3F3480, 0x3F3480},
    };
    for (MachineType machine : {MachineType::St, MachineType::MegaSt,
                                MachineType::Ste, MachineType::MegaSte}) {
        Rig r; r.bus.machine = machine;
        const bool st = machine == MachineType::St || machine == MachineType::MegaSt;
        for (const auto& t : cases) {
            r.address(t.old);
            r.fdc.write8(0xFF8600 + t.offset, t.value);
            c.equal("retenue, masque haut et alignement", r.address(), st ? t.st : t.ste);
        }
        r.address(0x123480); r.address(0x123400);
        c.equal("ordre bas/milieu/haut réécrit la retenue", r.address(), 0x123400);
        r.address(0x123480);
        r.fdc.write8(0xFF8609, 0x12); r.fdc.write8(0xFF860B, 0x34); r.fdc.write8(0xFF860D, 0);
        c.equal("ordre haut/milieu/bas expose la retenue", r.address(), st ? 0x123500 : 0x123400);
        r.fdc.write8(0xFF860D, 0);
        c.equal("pas de retenue sans nouveau front", r.address(), st ? 0x123500 : 0x123400);
    }

    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-fdc-registers-" + std::to_string(n));
        std::error_code ec; made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    c.equal("répertoire temporaire exclusif", made, true);
    if (!made) return c.failures;
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    } cleanup{work};
    // Une seule piste/face/secteur : secteur lisible, piste1 absente pour READ TRACK.
    std::vector<uint8_t> image(512, 0xA5);
    image[0x13] = 1; image[0x14] = 0;
    image[0x18] = 1; image[0x19] = 0; image[0x1A] = 1; image[0x1B] = 0;
    const auto path = work / "tiny.st";
    FILE* f = std::fopen(path.string().c_str(), "wb");
    c.equal("création image", f != nullptr, true);
    if (!f) return c.failures;
    const bool written = std::fwrite(image.data(), 1, image.size(), f) == image.size();
    const bool closed = std::fclose(f) == 0;
    c.equal("image complète", written && closed, true);
    if (!written || !closed) return c.failures;
    {
        Rig r;
        c.equal("montage pour statut DMA", r.fdc.loadImage(path.string()), true);
        r.address(0x2000); r.command(0x88, 0); // READ avec DMA coupée → erreur DMA
        c.equal("lecture sans compteur terminée", r.finish(), true);
        c.equal("compteur nul : bit no-error effacé", r.dmaStatus() & 1, 0);
        c.equal("lecture statut WD1772 dans la rémanence", r.dmaStatus() & 0xFFF8, 0x80);
        r.mode(0x90); r.data(3);
        c.equal("SCREG : compteur non nul", r.dmaStatus() & 3, 2);
        r.mode(0x90);
        c.equal("sans toggle : compteur et erreur conservés", r.dmaStatus() & 3, 2);
        r.mode(0x190);
        c.equal("toggle vers écriture : erreur conservée", r.dmaStatus() & 3, 0);
        c.equal("toggle conserve la rémanence", r.dmaStatus() & 0xFFF8, 0x80);
        c.equal("toggle conserve l'adresse", r.address(), 0x2000);
        r.mode(0x90);
        c.equal("toggle vers lecture : erreur conservée", r.dmaStatus() & 3, 0);
        r.command(0x88, 1);
        c.equal("nouvelle lecture DMA terminée", r.finish(), true);
        c.equal("transfert réussi efface l'erreur", r.dmaStatus() & 3, 1);
        c.equal("données du transfert réussi", r.bus.ram[0x21FF], 0xA5);
        for (bool cold : {false, true}) {
            r.command(0x88, 0);
            c.equal("erreur avant reset matériel : lecture finie", r.finish(), true);
            c.equal("erreur active avant reset matériel", r.dmaStatus() & 1, 0);
            r.fdc.reset(cold);
            c.equal(cold ? "reset froid efface erreur et rémanence"
                         : "reset chaud efface l'erreur",
                    cold ? r.dmaStatus() : (r.dmaStatus() & 3), 1);
        }
    }
    {
        Rig r;
        c.equal("montage pour écriture interrompue", r.fdc.loadImage(path.string()), true);
        std::fill_n(r.bus.ram.begin() + 0x2000, 512, uint8_t(0x6A));
        r.address(0x2000); r.command(0xA8, 1, true);
        unsigned events = 0;
        while (r.address() == 0x2000 && r.busy() && ++events < 100000)
            r.sched.runTo(r.sched.nextDue());
        c.equal("écriture a commencé le transfert FIFO", r.address(), 0x2010);
        r.mode(0x80); // toggle WRBIT pendant TRANSFER_LOOP
        c.equal("reset DMA termine les octets restants", r.finish(4000), true);
    }
    {
        Rig r;
        c.equal("montage pour piste absente", r.fdc.loadImage(path.string()), true);
        r.mode(0x80); r.data(0x58); // STEP-IN, TR mis à jour : tête sur piste1 absente
        c.equal("positionnement piste absente terminé", r.finish(), true);
        r.address(0x4000); r.command(0xE8, 13);
        c.equal("READ TRACK piste absente terminé", r.finish(), true);
        // 6268 octets, dont 6256 dans les rafales FIFO complètes de16. Une
        // constante6250 ne transférerait que6240 : cette assertion la distingue.
        c.equal("repli piste 6268, FIFO complète 6256", r.address() - 0x4000, 6256);
    }
    std::printf("[selftest-fdc-registers] %d checks, %d FAIL\n", c.count, c.failures);
    return c.failures;
}

#ifdef NEOST_FDC_REGISTERS_TEST_MAIN
int main() { return testFdcRegisters() ? 1 : 0; }
#endif
