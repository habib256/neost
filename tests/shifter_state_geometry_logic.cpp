// Save-state du Shifter : la géométrie du tampon est LIÉE à la résolution verrouillée.
// Un .state forgé (curW_ = 1024, frame_ assorti) passait les bornes lâches et
// renderLine décodait 928 px dans son tampon de pile idx[660] (SIGABRT).
#include "core/Bus.hpp"
#include "core/Shifter.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

int testShifterStateGeometry() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "[shifter-state-geometry] FAIL: %s\n", label); }
    };
    Bus bus;
    Shifter sh(bus);
    std::vector<uint8_t> bytes;
    { auto ar = StateArchive::saver(bytes); sh.serialize(ar); }
    {
        auto ar = StateArchive::loader(bytes.data(), bytes.size()); sh.serialize(ar);
        check(ar.ok(), "untouched state reloads");
    }
    // Géométrie basse rés bordée telle que sérialisée : w, h, ah, x, y (int32 LE).
    const int32_t geo[5] = { 416, 276, 200, 48, 29 };
    uint8_t pat[sizeof geo]; std::memcpy(pat, geo, sizeof geo);
    size_t at = SIZE_MAX;
    for (size_t i = 0; i + sizeof pat <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, pat, sizeof pat) == 0) { at = i; break; }
    check(at != SIZE_MAX, "bordered low-res geometry found in stream");
    if (at == SIZE_MAX) return failures;
    // Forge : curW_ 416 → 1024, frame_ grossi pour passer le contrôle de taille.
    const size_t cntAt = at + sizeof pat + 2;              // + frameMode_ + frameSync_
    uint32_t count = 0; std::memcpy(&count, bytes.data() + cntAt, 4);
    check(count == 416u * 276u, "framebuffer size follows geometry");
    if (count != 416u * 276u) return failures;
    const int32_t wide = 1024;
    std::vector<uint8_t> forged(bytes.begin(), bytes.begin() + cntAt);
    std::memcpy(forged.data() + at, &wide, 4);
    const uint32_t newCount = 1024u * 276u;
    forged.insert(forged.end(), reinterpret_cast<const uint8_t*>(&newCount),
                  reinterpret_cast<const uint8_t*>(&newCount) + 4);
    forged.insert(forged.end(), size_t(newCount) * 4, 0);
    forged.insert(forged.end(), bytes.begin() + cntAt + 4 + size_t(count) * 4, bytes.end());
    {
        auto ar = StateArchive::loader(forged.data(), forged.size()); sh.serialize(ar);
        check(!ar.ok(), "geometry wider than the locked resolution is rejected");
    }
    std::printf("[shifter-state-geometry] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
