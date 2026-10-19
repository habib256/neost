// Hatari gemdos.c fsfirst_dirname/SFirst et str.c Str_Filename_Host2Atari.
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "io/GemdosHd.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, int64_t got, int64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        if (failures < 16)
            std::printf("  FAIL GEMDOS entries %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(expected));
    }
};
struct Rig {
    Bus bus;
    Cpu68k cpu{bus};
    GemdosHd gemdos{bus, cpu};
    bool intercepted = false;
    explicit Rig(int partitions) {
        bus.cpu = &cpu;
        gemdos.setPartitionCountProvider([partitions] { return partitions; });
        cpu.setSr(0x2700);
        cpu.setReg(15, 0x1000);
        bus.write32(0, 0x5000); // act_pd non initialisé par boot : pointe ici vers notre basepage
        bus.write32(0x5020, 0x6000); // DTA de processus, réellement lu par Fsfirst/Fsnext
    }
    int trap(uint16_t call) {
        bus.write16(0x1000, 0x2700);
        bus.write32(0x1002, 0x2000);
        bus.write16(0x1006, call);
        cpu.setSr(0x2700);
        gemdos.handleOpcode(8);
        intercepted = (cpu.sr() & 4) != 0;
        return int32_t(cpu.reg(0));
    }
    std::map<std::string, uint8_t> list(const std::string& pattern, uint16_t attributes = 0x10) {
        for (size_t i = 0; i <= pattern.size(); ++i)
            bus.write8(0x3000 + uint32_t(i), i < pattern.size() ? uint8_t(pattern[i]) : 0);
        bus.write32(0x1008, 0x3000);
        bus.write16(0x100C, attributes);
        std::map<std::string, uint8_t> found;
        int result = trap(0x4E);
        for (int n = 0; result == 0 && intercepted && n < 100; ++n) {
            std::string name;
            for (int i = 0; i < 14 && bus.read8(0x601E + i); ++i)
                name += char(bus.read8(0x601E + i));
            found[name] = bus.read8(0x6015);
            result = trap(0x4F);
        }
        return found;
    }
};
}

int testGemdosDirectoryEntries() {
    namespace fs = std::filesystem;
    Checks check;
    fs::path work;
    bool made = false;
    for (int i = 0; i < 1000 && !made; ++i) {
        work = fs::temp_directory_path() / ("neost-gemdos-entries-" + std::to_string(i));
        std::error_code error;
        made = fs::create_directory(work, error);
        if (error) return 1;
    }
    if (!made) return 1;
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{work};
    const fs::path root = work / "drive";
    fs::create_directories(root / "SUB" / "CHILD");
    for (const auto& name : {"A", "AB", "FILE.TXT", ".hidden"})
        std::ofstream(root / "SUB" / name) << "x";
    std::ofstream(root / "ROOT.TXT") << "root";
    std::ofstream(root / ".secret") << "hidden";
    std::ofstream(work / "OUTSIDE.TXT") << "outside";

    for (int partitions : {0, 2}) {
        Rig rig(partitions);
        check.equal("montage", rig.gemdos.setDirectory(root.string()), true);
        const std::string drive = std::string(1, char('C' + partitions)) + ":\\";
        auto entries = rig.list(drive + "*.*");
        check.equal("racine sans points ni cachés", entries.size(), 2);
        check.equal("fichier racine", entries.count("ROOT.TXT"), 1);
        check.equal("dossier racine", entries.count("SUB"), 1);

        entries = rig.list(drive + "SUB\\*.*");
        check.equal("sous-dossier six entrées", entries.size(), 6);
        check.equal("nom point exact", entries.count("."), 1);
        check.equal("nom parent exact", entries.count(".."), 1);
        check.equal("attribut point", entries.count(".") ? entries.at(".") : -1, 0x10);
        check.equal("attribut parent", entries.count("..") ? entries.at("..") : -1, 0x10);
        check.equal("caché ignoré", entries.count(".HIDDEN"), 0);
        check.equal("conversion parent non altérée", entries.count("+."), 0);
        entries = rig.list(drive + "SUB\\*.*", 0);
        check.equal("attribut filtre dossiers et points", entries.size(), 3);
        entries = rig.list(drive + "SUB\\*");
        check.equal("étoile seule sans extension", entries.size(), 3);
        check.equal("étoile seule garde dossier", entries.count("CHILD"), 1);
        entries = rig.list(drive + "SUB\\?");
        check.equal("question un caractère", entries.size(), 2);
        check.equal("question garde point", entries.count("."), 1);
        entries = rig.list(drive + "SUB\\??");
        check.equal("deux questions deux caractères", entries.size(), 2);
        check.equal("deux questions gardent parent", entries.count(".."), 1);
        entries = rig.list(drive + "SUB\\CHILD\\..\\*.*");
        check.equal("navigation parent reste sous-dossier", entries.count(".."), 1);
        entries = rig.list(drive + "SUB\\..\\..\\*.*");
        check.equal("parent borné à racine", entries.size(), 2);
        check.equal("aucun fichier hors montage", entries.count("OUTSIDE.TXT"), 0);
        entries = rig.list(drive);
        check.equal("racine nue ne scanne pas parent", entries.empty(), true);
    }
    std::printf("[gemdos-directory-entries] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_GEMDOS_DIRECTORY_ENTRIES_TEST_MAIN
int main() { return testGemdosDirectoryEntries() ? 1 : 0; }
#endif
