// Limite de représentation des compteurs ACSI uint32, pas une limite du bus SCSI.
// Hatari hdc.c distingue hdSize (nombre de blocs) et READ CAPACITY (dernier LBA).
#include "io/Acsi.hpp"
#include "io/UltraSatan.hpp"
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
    void equal(const char* label, uint64_t actual, uint64_t expected) {
        ++count;
        if (actual == expected) return;
        ++failures;
        std::fprintf(stderr, "[acsi-capacity-limit] FAIL %s: %llu != %llu\n", label,
                     static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
    }
};
void send(Acsi& acsi, int target, std::initializer_list<uint8_t> cdb) {
    acsi.selectTarget(uint8_t(target));
    for (uint8_t byte : cdb) acsi.feedByte(byte);
}
uint32_t read32(const uint8_t* data) {
    return uint32_t(data[0]) << 24 | uint32_t(data[1]) << 16 | uint32_t(data[2]) << 8 | data[3];
}
}

int testAcsiCapacityLimit() {
#ifdef _WIN32
    std::fprintf(stderr, "[acsi-capacity-limit] SKIP sparse POSIX fixture unavailable\n");
    return 0;
#else
    Checks check;
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-acsi-capacity-limit-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    const auto path = work / "sparse.img";
    FILE* file = std::fopen(path.string().c_str(), "wb+");
    if (!file) return 1;
    struct FileClose { FILE* file; ~FileClose() { std::fclose(file); } } close{file};
    struct stat probe{};
    if (ftruncate(fileno(file), 1024 * 1024) != 0 || fstat(fileno(file), &probe) != 0) return 1;
    if (probe.st_blocks != 0) {
        std::fprintf(stderr, "[acsi-capacity-limit] SKIP filesystem does not preserve sparse holes\n");
        return 0;
    }
    constexpr uint64_t maxSectors = UINT32_MAX;
    Acsi acsi;
    for (uint64_t sectors : {maxSectors, maxSectors + 1, maxSectors + 2, uint64_t(4)}) {
        const uint64_t logical = sectors * 512;
        if (ftruncate(fileno(file), off_t(logical)) != 0) return 1;
        struct stat allocation{};
        if (fstat(fileno(file), &allocation) != 0) return 1;
        check.equal("taille logique exacte", uint64_t(allocation.st_size), logical);
        check.equal("allocation physique bornée", uint64_t(allocation.st_blocks) * 512 <= 1024 * 1024, true);
        const bool fits = sectors <= maxSectors;
        check.equal("montage représentable seulement", acsi.mount(0, path.string()), fits);
        acsi.selectTarget(0);
        check.equal("présence cible après montage", acsi.targetEnabled(), fits);
        check.equal("chemin après montage", acsi.mountedPath(0).empty(), !fits);
        if (fits) {
            send(acsi, 0, {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0});
            check.equal("READ CAPACITY statut", acsi.status(), 0);
            check.equal("READ CAPACITY longueur", acsi.dataLen(), 8);
            check.equal("READ CAPACITY dernier LBA", read32(acsi.readBuffer()), sectors - 1);
            check.equal("READ CAPACITY bloc512", read32(acsi.readBuffer() + 4), 512);
            const uint32_t last = uint32_t(sectors - 1);
            send(acsi, 0, {0x28, 0, uint8_t(last >> 24), uint8_t(last >> 16),
                          uint8_t(last >> 8), uint8_t(last), 0, 0, 1, 0});
            check.equal("dernier LBA lisible", acsi.status(), 0);
            check.equal("dernier LBA longueur", acsi.dataLen(), 512);
        }
        std::fprintf(stderr, "[acsi-capacity-limit] logical=%llu physical=%llu bytes\n",
                     static_cast<unsigned long long>(logical),
                     static_cast<unsigned long long>(uint64_t(allocation.st_blocks) * 512));
    }
    // Refuser une carte ne retire pas le boîtier UltraSatan du bus.
    UltraSatan device;
    acsi.attachUltraSatan(2, &device);
    if (ftruncate(fileno(file), off_t((maxSectors + 1) * 512)) != 0) return 1;
    check.equal("carte US trop grande refusée", acsi.mount(2, path.string()), false);
    acsi.selectTarget(2);
    check.equal("slot US reste peuplé", acsi.targetEnabled(), true);
    check.equal("slot US sans ancien chemin", acsi.mountedPath(2).empty(), true);
    send(acsi, 2, {0, 0, 0, 0, 0, 0});
    check.equal("slot US vide NOT READY", acsi.status(), 2);
    send(acsi, 2, {3, 0, 0, 0, 18, 0});
    check.equal("slot US sense NOT READY", acsi.readBuffer()[2] & 15, 2);
    check.equal("slot US sense medium absent", acsi.readBuffer()[12], 0x3A);
    std::fprintf(stderr, "[acsi-capacity-limit] %d checks, %d FAIL\n", check.count, check.failures);
    return check.failures;
#endif
}
#ifdef NEOST_ACSI_CAPACITY_LIMIT_TEST_MAIN
int main() { return testAcsiCapacityLimit() ? 1 : 0; }
#endif
