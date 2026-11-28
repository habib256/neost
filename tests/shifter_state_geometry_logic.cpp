// [interne] Robustesse du chargeur d'état, aucune affirmation sur le matériel.
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
    // Rendu TT-medium. Le motif encode cinq pixels connus et vérifie
    // la banque non nulle, les quatre plans et le passage à la ligne suivante.
    bus.machine = MachineType::Tt;
    bus.ram.assign(2u << 20, 0);
    sh.reset();
    sh.write8(0xFF8201u, 1);
    sh.write8(0xFF8203u, 0x20);
    sh.write8(0xFF820Du, 0x5F);
    const uint32_t base = 0x12058u;
    check(sh.read8(0xFF820Du) == 0x58, "TT video base low byte is eight-byte aligned");
    bus.ram[base] = 0x80;         // plan 0, pixel 0
    bus.ram[base + 2] = 0x40;     // plan 1, pixel 1
    bus.ram[base + 4] = 0x20;     // plan 2, pixel 2
    bus.ram[base + 6] = 0x10;     // plan 3, pixel 3
    bus.ram[base + 320] = 0x80;   // ligne 1, pixel 0
    bus.ram[base + 320 * 480 - 7] = 1; // bit 0 du premier plan dans le dernier groupe
    bus.write16(0xFF8262u, 0x0403);
    bus.write16(0xFF8460u, 0x0FFF); // index 0 dans la banque 3
    bus.write16(0xFF8462u, 0x0F00);
    bus.write16(0xFF8464u, 0x00F0);
    bus.write16(0xFF8468u, 0x000F);
    bus.write16(0xFF8470u, 0x0123);
    sh.renderFrame();
    check(sh.width() == 640 && sh.height() == 480 &&
          sh.activeLeft() == 0 && sh.activeTop() == 0 &&
          sh.activeWidth() == 640 && sh.activeHeight() == 480,
          "TT medium exposes an unbordered 640x480 image");
    if (sh.width() == 640 && sh.height() == 480) {
        const uint32_t* pixels = sh.pixels();
        check(pixels[0] == 0xFFFF0000u && pixels[1] == 0xFF00FF00u &&
              pixels[2] == 0xFF0000FFu && pixels[3] == 0xFF112233u &&
              pixels[4] == 0xFFFFFFFFu,
              "TT four planes, big-endian words and selected 0RGB palette");
        check(pixels[640] == 0xFFFF0000u && pixels[640 * 480 - 1] == 0xFFFF0000u,
              "TT line pitch and final planar word group");
    }
    sh.beginFrame();
    check(sh.height() == 480 && sh.rasterLines() == 200,
          "TT image survives the next scheduled raster");
    sh.write8(0xFF8201u, 0x1F);
    sh.write8(0xFF8203u, 0xFF);
    sh.finishFrame();
    check(sh.width() == 640 && sh.height() == 480 && sh.pixels()[0] == 0xFF000000u,
          "TT incomplete screen span returns a black frame");
    sh.write8(0xFF8201u, 0xC1);
    sh.write8(0xFF8203u, 0x20);
    sh.write8(0xFF820Du, 0x58);
    sh.finishFrame();
    check(sh.read8(0xFF8201u) == 0xC1 && sh.pixels()[0] == 0xFF000000u,
          "TT out-of-range high address does not alias into ST RAM");
    bus.write16(0xFF8262u, 0);
    sh.renderFrame();
    check(sh.height() != 480, "TT mode transition discards the previous image");

    std::printf("[shifter-state-geometry] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
