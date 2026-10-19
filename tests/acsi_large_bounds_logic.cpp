// Borne ACSI non signée : image sparse de 2^31+4 secteurs, sans allocation 1 Tio.
// Hatari hdc.c conserve hdSize/LBA non signés ; NeoST garde sa borne de fin stricte.
#include "io/Acsi.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
struct Checks {
    int count = 0, failures = 0;
    void equal(const char* label, uint64_t got, uint64_t expected) {
        ++count;
        if (got == expected) return;
        ++failures;
        std::fprintf(stderr, "[acsi-large-bounds] FAIL %s: %llu != %llu\n", label,
                     static_cast<unsigned long long>(got), static_cast<unsigned long long>(expected));
    }
};
void command(Acsi& acsi, bool ten, bool write, uint32_t lba, unsigned count) {
    std::vector<uint8_t> cdb(ten ? 10 : 6, 0);
    cdb[0] = ten ? (write ? 0x2A : 0x28) : (write ? 0x0A : 0x08);
    if (ten) {
        cdb[2] = uint8_t(lba >> 24); cdb[3] = uint8_t(lba >> 16);
        cdb[4] = uint8_t(lba >> 8); cdb[5] = uint8_t(lba);
        cdb[7] = uint8_t(count >> 8); cdb[8] = uint8_t(count);
    } else {
        cdb[1] = uint8_t((lba >> 16) & 31);
        cdb[2] = uint8_t(lba >> 8); cdb[3] = uint8_t(lba); cdb[4] = uint8_t(count);
    }
    acsi.selectTarget(0);
    for (uint8_t byte : cdb) acsi.feedByte(byte);
}
uint8_t sense(Acsi& acsi) {
    acsi.selectTarget(0);
    for (uint8_t byte : {3, 0, 0, 0, 4, 0}) acsi.feedByte(byte);
    return acsi.readBuffer()[0] & 0x7F;
}
}

int testAcsiLargeBounds() {
#ifdef _WIN32
    // resize_file seul ne garantit pas un fichier sparse sur Windows. Ne jamais
    // risquer une allocation physique géante sans FSCTL_SET_SPARSE.
    std::fprintf(stderr, "[acsi-large-bounds] SKIP sparse POSIX fixture unavailable\n");
    return 0;
#else
    Checks check;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-acsi-large-bounds-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    constexpr uint32_t largeSectors = 0x80000004u;
    constexpr uint64_t allocationLimit = 4 * 1024 * 1024;
    for (uint32_t sectors : {uint32_t(4), largeSectors}) {
        const auto path = work / "disk.img";
        FILE* file = std::fopen(path.string().c_str(), "wb+");
        if (!file) return 1;
        const uint64_t logicalSize = uint64_t(sectors) * 512;
        if (sectors == largeSectors) {
            // Sonder d'abord un trou de 1 Mio : un FS qui matérialise ftruncate
            // est exclu AVANT toute demande de taille logique géante.
            struct stat probe{};
            if (ftruncate(fileno(file), 1024 * 1024) != 0 ||
                fstat(fileno(file), &probe) != 0) { std::fclose(file); return 1; }
            if (probe.st_blocks != 0) {
                std::fclose(file);
                std::fprintf(stderr, "[acsi-large-bounds] SKIP filesystem does not preserve sparse holes\n");
                return check.failures;
            }
        }
        if (ftruncate(fileno(file), off_t(logicalSize)) != 0) { std::fclose(file); return 1; }
        struct stat before{};
        if (fstat(fileno(file), &before) != 0) { std::fclose(file); return 1; }
        check.equal("taille logique exacte", uint64_t(before.st_size), logicalSize);
        check.equal("sparse initial physiquement borné", uint64_t(before.st_blocks) * 512 <= allocationLimit, true);
        std::fclose(file);
        Acsi acsi;
        check.equal("montage", acsi.mount(0, path.string()), true);
        for (bool ten : {false, true}) for (bool write : {false, true}) {
            command(acsi, ten, write, 0, 1);
            check.equal("commande début acceptée", acsi.status(), 0);
            check.equal("longueur un secteur", acsi.dataLen(), 512);
            if (write && acsi.status() == 0) {
                std::vector<uint8_t> payload(512, 0xC5);
                acsi.writeToDisk(payload.data(), int(payload.size()));
            }
            if (write) check.equal("écriture début réussie", acsi.status(), 0);
        }
        command(acsi, true, false, 0, 1);
        check.equal("relecture donnée début", acsi.status() == 0 && acsi.readBuffer()[0] == 0xC5, true);
        for (bool write : {false, true}) {
            command(acsi, true, write, sectors - 1, 1);
            check.equal("dernier secteur accepté", acsi.status(), 0);
            check.equal("dernier secteur longueur", acsi.dataLen(), 512);
            if (write && acsi.status() == 0) {
                std::vector<uint8_t> payload(512, 0xD6);
                acsi.writeToDisk(payload.data(), int(payload.size()));
                check.equal("écriture fin réussie", acsi.status(), 0);
            }
            command(acsi, true, write, sectors - 1, 2);
            check.equal("dépassement fin rejeté", acsi.status(), 2);
            check.equal("dépassement sans phase partielle", acsi.dataLen(), 0);
            check.equal("sense dépassement INVADDR", sense(acsi), 0x21);
            command(acsi, true, write, sectors, 1);
            check.equal("LBA hors image rejeté", acsi.status(), 2);
            check.equal("sense LBA INVADDR", sense(acsi), 0x21);
            command(acsi, true, write, 0, 0);
            // Politique historique conservée : READ10 zéro réussit, WRITE10 zéro échoue.
            check.equal("count10 zéro inchangé", acsi.status(), write ? 2 : 0);
            check.equal("count10 zéro sans données", acsi.dataLen(), 0);
            if (sectors == largeSectors) {
                command(acsi, false, write, 0, 0);
                check.equal("count6 zéro vaut256", acsi.status(), 0);
                check.equal("count6 zéro longueur", acsi.dataLen(), 256 * 512);
                command(acsi, true, write, 0, 256);
                check.equal("count10 256 accepté", acsi.status(), 0);
                check.equal("count10 256 longueur", acsi.dataLen(), 256 * 512);
                for (uint32_t lba : {uint32_t(4), uint32_t(5)}) {
                    command(acsi, true, write, lba, 1);
                    check.equal("frontière INT_MAX restante", acsi.status(), 0);
                }
            } else {
                command(acsi, false, write, 0, 0);
                check.equal("count6 256 dépasse petite image", acsi.status(), 2);
                check.equal("count6 petite image INVADDR", sense(acsi), 0x21);
            }
        }
        command(acsi, true, false, sectors - 1, 1);
        check.equal("relecture donnée fin", acsi.status() == 0 && acsi.readBuffer()[0] == 0xD6, true);
        struct stat after{};
        if (stat(path.string().c_str(), &after) != 0) return 1;
        check.equal("image jamais agrandie", uint64_t(after.st_size), logicalSize);
        check.equal("allocation physique finale bornée", uint64_t(after.st_blocks) * 512 <= allocationLimit, true);
        std::fprintf(stderr, "[acsi-large-bounds] logical=%llu physical=%llu bytes\n",
                     static_cast<unsigned long long>(logicalSize),
                     static_cast<unsigned long long>(uint64_t(after.st_blocks) * 512));
    }
    std::fprintf(stderr, "[acsi-large-bounds] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
#endif
}
#ifdef NEOST_ACSI_LARGE_BOUNDS_TEST_MAIN
int main() { return testAcsiLargeBounds() ? 1 : 0; }
#endif
