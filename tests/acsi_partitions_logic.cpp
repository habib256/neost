// Hatari hdc.c HDC_PartitionCount : quatre racines DOS/Atari, sans parcours XGM.
#include "io/Acsi.hpp"
#include "io/UltraSatan.hpp"
#include <cstdio>
#include <filesystem>
#include <vector>

int testAcsiPartitions() {
    int checks = 0, failures = 0;
    auto equal = [&](const char* label, int got, int wanted) {
        ++checks;
        if (got == wanted) return;
        ++failures;
        std::fprintf(stderr, "[acsi-partitions] FAIL %s: %d != %d\n", label, got, wanted);
    };
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-acsi-partitions-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    auto image = [&](const char* name, bool dos, int count) {
        const auto path = work / name;
        std::vector<uint8_t> bytes(512, 0);
        if (dos) { bytes[510] = 0x55; bytes[511] = 0xAA; }
        for (int i = 0; i < count; ++i) {
            if (dos) bytes[0x1BE + 16*i + 4] = 0x06;
            else {
                const int offset = 0x1C6 + 12*i;
                bytes[offset] = 1;
                bytes[offset+1] = 'X'; bytes[offset+2] = 'G'; bytes[offset+3] = 'M';
            }
        }
        FILE* file = std::fopen(path.string().c_str(), "wb");
        if (!file) return std::filesystem::path{};
        const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        std::fclose(file);
        return written ? path : std::filesystem::path{};
    };
    const auto dos = image("dos.img", true, 4);
    const auto atari = image("atari.img", false, 2);
    const auto empty = image("unpartitioned.img", false, 0);
    const auto shortImage = image("truncated.img", true, 4);
    if (dos.empty() || atari.empty() || empty.empty() || shortImage.empty()) return 1;

    Acsi acsi;
    equal("aucune cible", acsi.partitionCount(), 0);
    for (int target = 0; target < Acsi::MAX_DEVS; ++target) {
        equal("montage DOS", acsi.mount(target, dos.string()), true);
        equal("agrégation huit cibles", acsi.partitionCount(), 4*(target+1));
    }
    equal("remontage Atari XGM", acsi.mount(0, atari.string()), true);
    equal("XGM compte racine sans explorer", acsi.partitionCount(), 30);
    equal("remontage sans partition", acsi.mount(7, empty.string()), true);
    equal("racines vides ne comptent pas", acsi.partitionCount(), 26);
    equal("image absente refusée", acsi.mount(0, (work/"absent.img").string()), false);
    equal("ancien montage invalide retiré", acsi.partitionCount(), 24);
    acsi.unmountAll();
    equal("démontage toutes cibles", acsi.partitionCount(), 0);

    UltraSatan usatan;
    acsi.attachUltraSatan(6, &usatan);
    equal("slots peuplés sans carte", acsi.partitionCount(), 0);
    equal("carte dans slot", acsi.mount(7, atari.string()), true);
    equal("slot vide ne crée pas partition", acsi.partitionCount(), 2);
    acsi.unmountAll();
    equal("retrait carte garde cible sans partition", acsi.partitionCount(), 0);
    acsi.detachUltraSatan();

    equal("montage avant EOF", acsi.mount(0, shortImage.string()), true);
    std::filesystem::resize_file(shortImage, 0);
    equal("secteur racine court compte zéro", acsi.partitionCount(), 0);
    equal("EOF répété sans partition fantôme", acsi.partitionCount(), 0);
    std::fprintf(stderr, "[acsi-partitions] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_ACSI_PARTITIONS_TEST_MAIN
int main() { return testAcsiPartitions() ? 1 : 0; }
#endif
