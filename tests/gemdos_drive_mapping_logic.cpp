// Hatari gemdos.c734–824 : dossier simple après partitions images ; lettres explicites préservées.
#include "core/Bus.hpp"
#include "core/Machine.hpp"
#include "core/Cpu68k.hpp"
#include "io/GemdosHd.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
struct Checks {
    int count = 0;
    int failures = 0;
    void equal(const char* label, int64_t got, int64_t wanted) {
        ++count;
        if (got == wanted) return;
        ++failures;
        if (failures < 16)
            std::printf("  FAIL GEMDOS mapping %s: %lld attendu %lld\n", label,
                        static_cast<long long>(got), static_cast<long long>(wanted));
    }
};
struct Rig {
    Bus bus;
    Cpu68k cpu{bus};
    GemdosHd gemdos{bus, cpu};
    int partitions = 0;
    bool intercepted = false;
    Rig() {
        bus.cpu = &cpu;
        gemdos.setPartitionCountProvider([this] { return partitions; });
        cpu.setSr(0x2700);
        cpu.setReg(15, 0x1000);
    }
    int trap(uint16_t call) {
        bus.write16(0x1000, 0x2700); // cadre TRAP superviseur : SR, PC, paramètres
        bus.write32(0x1002, 0x2000);
        bus.write16(0x1006, call);
        cpu.setSr(0x2700);
        gemdos.handleOpcode(8);
        intercepted = (cpu.sr() & 4) != 0;
        return int32_t(cpu.reg(0));
    }
    int open(const std::string& name) {
        for (size_t i = 0; i <= name.size(); ++i)
            bus.write8(0x3000 + uint32_t(i), i < name.size() ? uint8_t(name[i]) : 0);
        bus.write32(0x1008, 0x3000);
        bus.write16(0x100C, 0);
        return trap(0x3D);
    }
    int read(int handle) {
        bus.write16(0x1008, uint16_t(handle));
        bus.write32(0x100A, 1);
        bus.write32(0x100E, 0x4000);
        const int result = trap(0x3F);
        return result == 1 ? bus.read8(0x4000) : -1;
    }
};
}

int testGemdosDriveMapping() {
    Checks check;
    namespace fs = std::filesystem;
    fs::path root;
    bool created = false;
    for (int i = 0; i < 1000 && !created; ++i) {
        root = fs::temp_directory_path() / ("neost-gemdos-mapping-" + std::to_string(i));
        std::error_code error;
        created = fs::create_directory(root, error);
        if (error) return 1;
    }
    if (!created) return 1;
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{root};
    const fs::path simple = root / "simple", multi = root / "multi";
    fs::create_directories(simple);
    fs::create_directories(multi / "C");
    fs::create_directories(multi / "F");
    std::ofstream(simple / "FILE.TXT") << "host";
    std::ofstream(multi / "C" / "FILE.TXT") << "C";
    std::ofstream(multi / "F" / "FILE.TXT") << "F";

    for (int count : {0, 1, 2, 23}) {
        Rig rig;
        rig.partitions = count;
        check.equal("montage simple", rig.gemdos.setDirectory(simple.string()), true);
        check.equal("masque simple décalé", rig.gemdos.driveMask(), uint32_t(1) << (2 + count));
        check.equal("première lettre", rig.gemdos.firstMappedDrive(), 2 + count);
        const std::string name = std::string(1, char('C' + count)) + ":\\FILE.TXT";
        const int handle = rig.open(name);
        check.equal("lecteur hôte intercepté", rig.intercepted, true);
        check.equal("lecture sur bonne lettre", rig.read(handle), 'h');
        if (count) {
            rig.open("C:\\FILE.TXT");
            check.equal("partition physique non interceptée", rig.intercepted, false);
        }
    }
    {
        Rig rig;
        check.equal("montage avant ACSI", rig.gemdos.setDirectory(simple.string()), true);
        rig.partitions = 2;
        const int handle = rig.open("C:\\FILE.TXT");
        check.equal("changement provider sans remap actif", rig.read(handle), 'h');
        rig.gemdos.reset();
        check.equal("reset résout nouvel ACSI", rig.gemdos.driveMask(), 1u << 4);
        rig.open("C:\\FILE.TXT");
        check.equal("C physique après reset", rig.intercepted, false);
        check.equal("E hôte après reset", rig.read(rig.open("E:\\FILE.TXT")), 'h');
        rig.partitions = 3;
        rig.bus.write16(0x446, 2); // _bootdev physique : doit rester C
        rig.bus.write32(0x4C2, 7);
        rig.gemdos.handleOpcode(10);
        check.equal("boot résout ACSI tardif", rig.gemdos.driveMask(), 1u << 5);
        check.equal("drvbits fusionné", rig.bus.read32(0x4C2), 7 | (1u << 5));
        rig.open("FILE.TXT");
        check.equal("bootdev physique non forcé sur hôte", rig.intercepted, false);
        check.equal("F hôte après boot", rig.read(rig.open("F:\\FILE.TXT")), 'h');
        const int retained = rig.open("F:\\FILE.TXT");
        rig.partitions = 24;
        check.equal("montage hors Z refusé", rig.gemdos.setDirectory(simple.string()), false);
        check.equal("ancien masque conservé", rig.gemdos.driveMask(), 1u << 5);
        check.equal("handle conservé sur échec", rig.read(retained), 'h');
        check.equal("chemin absent refusé", rig.gemdos.setDirectory((root / "absent").string()), false);
        check.equal("handle encore vivant", rig.read(retained), 'o');
    }
    for (int count : {24, 32}) {
        Rig rig;
        check.equal("configuration avant saturation", rig.gemdos.setDirectory(simple.string()), true);
        rig.partitions = count;
        rig.gemdos.reset();
        check.equal("reset saturé conserve configuration", rig.gemdos.active(), true);
        check.equal("reset saturé vide lettres", rig.gemdos.driveMask(), 0);
        check.equal("aucune première lettre", rig.gemdos.firstMappedDrive(), -1);
        rig.open("C:\\FILE.TXT");
        check.equal("C physique malgré saturation", rig.intercepted, false);
        rig.partitions = 0;
        rig.gemdos.reset();
        check.equal("retrait ACSI restaure C", rig.gemdos.driveMask(), 1u << 2);
        check.equal("configuration hôte préservée", rig.read(rig.open("C:\\FILE.TXT")), 'h');
    }
    {
        // Câblage partagé de Machine : dossier configuré avant l'image physique,
        // comme dans les frontends ; le reset résout le nombre réel de partitions.
        std::vector<uint8_t> image(512, 0);
        image[510] = 0x55;
        image[511] = 0xAA;
        image[0x1BE + 4] = 6;
        image[0x1BE + 16 + 4] = 6;
        const fs::path imagePath = root / "partitions.img";
        std::ofstream disk(imagePath, std::ios::binary);
        disk.write(reinterpret_cast<const char*>(image.data()), image.size());
        disk.close();
        auto machine = std::make_unique<Machine>(1024u * 1024u, CpuCore::Moira, MachineType::Ste);
        machine->bus.rom.assign(0x30000, 0);
        machine->bus.romBase = 0xE00000;
        const uint8_t vectors[] = {0, 1, 0, 0, 0, 0xE0, 0, 8, 0x60, 0xFE};
        std::copy(std::begin(vectors), std::end(vectors), machine->bus.rom.begin());
        check.equal("Machine monte dossier", machine->gemdos.setDirectory(simple.string()), true);
        check.equal("Machine monte ACSI après", machine->fdc.mountAcsi(imagePath.string()), true);
        check.equal("Machine compte partitions réelles", machine->fdc.acsiPartitionCount(), 2);
        machine->reset();
        check.equal("Machine reset câble E", machine->gemdos.driveMask(), 1u << 4);
        machine->fdc.unmountAcsi();
        machine->reset();
        check.equal("Machine reset après retrait câble C", machine->gemdos.driveMask(), 1u << 2);
    }
    {
        Rig rig;
        rig.partitions = 24;
        check.equal("multilettres explicites", rig.gemdos.setDirectory(multi.string()), true);
        check.equal("masque explicite inchangé", rig.gemdos.driveMask(), (1u << 2) | (1u << 5));
        check.equal("C explicite", rig.read(rig.open("C:\\FILE.TXT")), 'C');
        check.equal("F explicite", rig.read(rig.open("F:\\FILE.TXT")), 'F');
        rig.gemdos.reset();
        check.equal("reset garde lettres explicites", rig.gemdos.driveMask(), (1u << 2) | (1u << 5));
    }
    std::printf("[gemdos-drive-mapping] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
}
#ifdef NEOST_GEMDOS_DRIVE_MAPPING_TEST_MAIN
int main() { return testGemdosDriveMapping() ? 1 : 0; }
#endif
