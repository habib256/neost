// =============================================================================
//  blitter_bare_bench.cpp — Cpu68k + Blitter de NeoST sur une RAM plate : trace du blitter.
//
//  POURQUOI. Pendant de tools/stblitter_bench.cpp : le code réel de NeoST (Cpu68k, Bus,
//  Blitter, Scheduler), sans Machine, sur le même programme et au même format de trace.
//  NEOST_RAM_SLOT règle les créneaux mémoire, comme l'option --slot du pilote RTL.
//
//  CONVENTION. Date au front descendant d'AS, S0 + 1 cycle. Le blitter observe au début de
//  son accès (S0), date = horloge + 1. Moira rappelle ses écritures à S0 + 2 : date = horloge − 1.
//
//  Usage : neost-blitter-bare <image.bin> <trace.txt> [--limit N]
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/Blitter.hpp"
#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Scheduler.hpp"
#include "io/Mfp.hpp"

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s image.bin trace.txt [--limit N]\n", argv[0]); return 2; }
    int64_t limit = 2000000;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--limit") && i + 1 < argc) limit = std::atoll(argv[++i]);
        else { std::fprintf(stderr, "argument inconnu : %s\n", argv[i]); return 2; }
    }
    constexpr uint32_t WITNESS = 0x3000, SENTINEL = 0x32FE;
    Bus bus(4u * 1024u * 1024u);
    Cpu68k cpu(bus);
    Scheduler scheduler;
    Mfp mfp;
    Blitter blitter(bus);
    bus.cpu = &cpu; bus.mfp = &mfp; bus.blitter = &blitter; bus.machine = MachineType::Ste;
    cpu.setScheduler(&scheduler);
    scheduler.setLiveClock([&] { return cpu.busClockNow(); });
    blitter.setScheduler(&scheduler);
    scheduler.setCallback(Scheduler::BLITTER, [&] { blitter.onSlice(); });

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "image illisible : %s\n", argv[1]); return 2; }
    std::vector<uint8_t> image(1u << 20);
    const size_t n = std::fread(image.data(), 1, image.size(), f);
    std::fclose(f);
    if (!n) { std::fprintf(stderr, "image vide : %s\n", argv[1]); return 2; }
    for (size_t i = 8; i < n; ++i) bus.ram[i] = image[i];
    // $0-$7 lit la ROM sur ST : les deux vecteurs de reset y vivent.
    bus.romBase = 0xFC0000;
    bus.rom.assign(image.begin(), image.begin() + 8192);

    FILE* out = std::fopen(argv[2], "w");
    if (!out) { std::fprintf(stderr, "trace inécrivable : %s\n", argv[2]); return 2; }
    bool done = false;
    uint16_t hi = 0;
    bus.watchBase_ = WITNESS;
    bus.watchHook = [&](uint32_t a, uint8_t v) {
        if (bus.blitterMasterAccess) return;
        if (!(a & 1)) { hi = v; return; }
        std::fprintf(out, "%lld C W %06X %04X\n", (long long)(cpu.busClockNow() - 1), a & ~1u,
                     unsigned(hi << 8 | v));
        if ((a & ~1u) == SENTINEL) done = true;
    };
    blitter.accessObserver = [&](bool write, uint32_t a, uint16_t v) {
        std::fprintf(out, "%lld B %c %06X %04X\n", (long long)(scheduler.liveNow() + 1), write ? 'W' : 'R', a, v);
    };
    cpu.reset();
    std::string reason = "limit";
    while (!done && cpu.busClockNow() < limit) {
        cpu.run(64);
        if (cpu.halted()) { reason = "halt"; break; }
    }
    if (done) reason = "sentinel";
    std::fprintf(out, "END t=%lld reason=%s\n", (long long)cpu.busClockNow(), reason.c_str());
    std::fclose(out);
    return 0;
}
