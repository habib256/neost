// Mediach suit les callbacks VBL Machine réels, même lorsque PAL/NTSC/mono changent.
#include "core/Machine.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

namespace {
struct Rig {
    std::unique_ptr<Machine> machine = std::make_unique<Machine>(1024u*1024u, CpuCore::Moira, MachineType::Ste);

    Rig() {
        machine->bus.rom.assign(0x30000, 0);
        machine->bus.romBase = 0xE00000;
        const uint8_t rom[] = {0,1,0,0,0,0xE0,0,8,0x60,0xFE}; // BRA sur place, SR reset masque IRQ
        std::copy(std::begin(rom), std::end(rom), machine->bus.rom.begin());
        machine->cpu.reset();
        video(0);
        machine->stepInstruction();
    }
    void video(int mode) {
        machine->shifter.mode = mode == 2 ? Shifter::Mode::High : Shifter::Mode::Low;
        machine->shifter.sync = mode == 0 ? 2 : 0;
    }
    bool vbl() {
        unsigned steps = 0;
        while (machine->sched.rawCyclesUntil(Scheduler::VBL) == INT64_MIN)
            if (++steps > 1000000) return false;
            else machine->stepInstruction();
        const int64_t due = machine->sched.liveNow() + machine->sched.rawCyclesUntil(Scheduler::VBL);
        while (machine->sched.liveNow() < due)
            if (++steps > 1000000) return false;
            else machine->stepInstruction();
        return true;
    }
    bool wprt() {
        machine->bus.write8(0xFF8800, 14);
        machine->bus.write8(0xFF8802, 5);
        machine->bus.write16(0xFF8606, 0x80);
        machine->bus.write16(0xFF8604, 0xD0);
        return (machine->bus.read16(0xFF8604) & 0x40) != 0;
    }
};
}

int testMachineFdcMediaVbl() {
    int checks = 0, failures = 0;
    auto eq = [&](const char* label, bool got, bool wanted) {
        ++checks;
        if (got == wanted) return;
        ++failures;
        std::fprintf(stderr, "[machine-fdc-media-vbl] FAIL %s: %d != %d\n", label, got, wanted);
    };
    std::filesystem::path work;
    bool made = false;
    for (int n = 0; n < 1000 && !made; ++n) {
        work = std::filesystem::temp_directory_path() / ("neost-machine-media-" + std::to_string(n));
        std::error_code ec;
        made = std::filesystem::create_directory(work, ec);
        if (ec) break;
    }
    if (!made) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{work};
    const auto path = work / "disk.st";
    std::vector<uint8_t> image(2048, 0);
    image[0x13] = 4; image[0x18] = 2; image[0x1A] = 2;
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return 1;
    const bool written = std::fwrite(image.data(), 1, image.size(), file) == image.size();
    std::fclose(file);
    if (!written) return 1;

    for (int initialMode = 0; initialMode < 3; ++initialMode) {
        Rig rig;
        rig.video(initialMode);
        eq("montage initial", rig.machine->fdc.loadImage(path.string()), true);
        for (int n = 0; n < 20; ++n) eq("VBL initiale servie", rig.vbl(), true);
        rig.machine->fdc.eject();
        eq("réinsertion", rig.machine->fdc.loadImage(path.string()), true);
        for (int n = 1; n <= 17; ++n) {
            rig.video((initialMode + n) % 3);
            eq("VBL à fréquence variable", rig.vbl(), true);
            eq("WPRT avant VBL18", rig.wprt(), true);
        }
        eq("VBL18 servie", rig.vbl(), true);
        eq("insertion commence à VBL18", rig.wprt(), false);
        // Une nouvelle action pendant la deuxième phase doit démarrer aussitôt.
        rig.machine->fdc.eject();
        eq("remontage phase2", rig.machine->fdc.loadImage(path.string()), true);
        for (int n = 1; n <= 36; ++n) {
            rig.video((initialMode + n) % 3);
            eq("VBL seconde transition", rig.vbl(), true);
            eq("frontières18/36 indépendantes cycles", rig.wprt(), n < 18);
        }
    }
    std::fprintf(stderr, "[machine-fdc-media-vbl] %d checks, %d FAIL\n", checks, failures);
    return failures;
}
#ifdef NEOST_MACHINE_FDC_MEDIA_VBL_TEST_MAIN
int main() { return testMachineFdcMediaVbl() ? 1 : 0; }
#endif
