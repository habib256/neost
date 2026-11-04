// =============================================================================
//  stcpu_bench.cpp — Pilote Verilator de fx68k + GSTMCU : un programme, puis la RAM.
//
//  Usage : Vstcpu_bench <rom.img> <trames> <adresse hexa> <longueur> <sortie.bin>
//  La ROM couvre $E00000-$E3FFFF, et aussi $0-$7 au reset (décodage du GSTMCU). La RAM est plate,
//  commandée par RAS/CAS du modèle. Sortie : les octets demandés après ce nombre de VSYNC.
//  Avec NEOST_STCPU_TRACE=fichier, chaque chute d'AS des trames NEOST_STCPU_FROM à _TO (défaut :
//  l'avant-dernière) : trame, ticks de 32 MHz depuis VSYNC, adresse, sens, donnée écrite. De quoi lire un
//  point précis, pas un verdict.
//  Avec NEOST_STCPU_VIDEO=fichier, la dernière trame, un mot 16 bits natif par période de 32 MHz :
//  bits 11-0 R,G,B · bit 12 BLANK_N · bit 13 HSYNC_N · bit 14 VSYNC_N, comme tools/gstmcu_bench.cpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test. Les RTL ne sont jamais liés à NeoST.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <verilated.h>
#include "Vstcpu_bench_top.h"

static Vstcpu_bench_top* t;
static std::vector<uint8_t> rom(256 * 1024), ram(4 * 1024 * 1024);

static void tick(int c) {
    t->clk32 = c;
    t->eval();
    if (c && !(t->RAS0_N && t->RAS1_N) && t->ram_a < 0x200000) {
        if (!t->we_n) {
            if (t->RAM_UDS) ram[t->ram_a << 1] = (t->mdout & 0xff00) >> 8;
            if (t->RAM_LDS) ram[(t->ram_a << 1) + 1] = t->mdout & 0xff;
        }
        t->mdin = uint16_t(ram[t->ram_a << 1] << 8 | ram[(t->ram_a << 1) + 1]);
    }
    if (c && !t->rom_n) {
        const uint32_t a = (uint32_t(t->cpu_a) << 1) & 0x3FFFE;
        t->rom_data = uint16_t(rom[a] << 8 | rom[a + 1]);
    }
    t->eval();
}

int main(int argc, char** argv) {
    if (argc != 6) { std::fprintf(stderr, "usage: %s rom.img trames adresse longueur sortie\n", argv[0]); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 2; }
    if (std::fread(rom.data(), 1, rom.size(), f) == 0) return 2;
    std::fclose(f);
    const int frames = std::atoi(argv[2]);
    const uint32_t addr = std::strtoul(argv[3], nullptr, 16), len = std::strtoul(argv[4], nullptr, 0);
    Verilated::commandArgs(argc, argv);
    t = new Vstcpu_bench_top;
    t->porb = 0; t->resb = 0; t->pwrUp = 1; t->extReset = 1; t->mdin = 0;
    for (int i = 0; i < 64; ++i) { tick(1); tick(0); }
    t->porb = 1;
    for (int i = 0; i < 64; ++i) { tick(1); tick(0); }
    t->resb = 1; t->pwrUp = 0; t->extReset = 0;
    int seen = 0, prev = 1, prevAs = 1;
    long guard = 0, ticks = 0, pendingAs = -1;
    const int traceFrom = std::getenv("NEOST_STCPU_FROM") ? std::atoi(std::getenv("NEOST_STCPU_FROM")) : frames - 2;
    const int traceTo = std::getenv("NEOST_STCPU_TO") ? std::atoi(std::getenv("NEOST_STCPU_TO")) : traceFrom;
    FILE* trace = nullptr;
    if (const char* tp = std::getenv("NEOST_STCPU_TRACE")) trace = std::fopen(tp, "w");
    std::vector<uint16_t> video;
    const char* videoPath = std::getenv("NEOST_STCPU_VIDEO");
    while (seen < frames && ++guard < 800000000L) {
        tick(1); tick(0);
        ++ticks;
        if (videoPath && seen == frames - 1)
            video.push_back(uint16_t((t->R << 8) | (t->G << 4) | t->B | (t->BLANK_N ? 1u << 12 : 0)
                                     | (t->HSYNC_N ? 1u << 13 : 0) | (t->vsync_n ? 1u << 14 : 0)));
        if (trace && seen >= traceFrom && seen <= traceTo && prevAs && !t->as_n) pendingAs = ticks;
        if (trace && pendingAs >= 0 && !t->as_n && (!t->rw ? (ticks - pendingAs >= 4) : true)) {
            std::fprintf(trace, "%d %ld %06X %c %04X\n", seen, pendingAs, unsigned(t->cpu_a) << 1, t->rw ? 'R' : 'W', t->cpu_dout);
            pendingAs = -1;
        }
        prevAs = t->as_n;
        if (prev && !t->vsync_n) { ++seen; ticks = 0; }
        prev = t->vsync_n;
        if (!t->halted_n) { std::fprintf(stderr, "CPU arrêté\n"); break; }
    }
    if (videoPath) {
        FILE* v = std::fopen(videoPath, "wb");
        if (!v) { std::perror(videoPath); return 2; }
        std::fwrite(video.data(), sizeof(uint16_t), video.size(), v);
        std::fclose(v);
    }
    FILE* o = std::fopen(argv[5], "wb");
    if (!o) { std::perror(argv[5]); return 2; }
    std::fwrite(ram.data() + addr, 1, len, o);
    std::fclose(o);
    std::fprintf(stderr, "VSYNC vus : %d\n", seen);
    delete t;
    return 0;
}
