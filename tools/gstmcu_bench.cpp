// =============================================================================
//  gstmcu_bench.cpp — Pilote Verilator du modèle STE GSTMCU + Shifter de MiSTery.
//
//  POURQUOI. Le banc amont (gstmcu/tb/ste_tb.cpp) peint VSYNC, HSYNC et BLANK par-dessus
//  la sortie vidéo : il cache ce qu'on veut lire. Ce pilote charge un écran connu et
//  enregistre, à chaque période de 32 MHz, R, G, B et les signaux SÉPARÉS.
//  Le décodage et le verdict sont dans tools/run_gstmcu_bench.py.
//
//  Cycles de bus adaptés de ste_tb.cpp @ 4a6db7cc, avec une correction de write_reg :
//  [B] M68000UM Rev 8 § 4.1.2 (PDF p. 50), S7 : AS retombe au front descendant, R/W ne
//      remonte qu'au front montant de fin de S7, soit 2 périodes de 32 MHz plus tard.
//      Le banc amont remonte R/W avec AS : gstshifter.v:136 @ 4a6db7cc (write = CS_d &
//      ~CS & ~RW) perd alors toute écriture de $FF8260, et MCU et Shifter divergent.
//
//  Usage : Vste_tb <vram.bin> <sortie.bin> <mde 0|1> <ntsc 0|1> <via_ntsc 0|1> <16 couleurs hexa>
//  via_ntsc : trois trames en 60 Hz avant le mode voulu.
//  [F] Sans ce passage, la basse résolution PAL issue du reset garde une phase de plans
//      décalée, sur toutes les trames observées. Modèle ou silicium ? Tranché par : un
//      relevé sur STE réel, le schéma du Shifter ST n'étant pas disponible.
//  Sortie : une trame à partir d'un front descendant de VSYNC, mots 16 bits natifs :
//    bits 11-0 R,G,B · bit 12 BLANK_N · bit 13 HSYNC_N · bit 14 VSYNC_N · bit 15 DE
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <verilated.h>
#include "Vste_tb.h"
#include "Vste_tb___024root.h"

static Vste_tb* tb;
static unsigned char ram[4 * 1024 * 1024];
static constexpr uint32_t kVideoBase = 0x1F8000;

static void tick(int c) {
    tb->clk32 = c;
    tb->eval();
    if (c && !(tb->RAS0_N && tb->RAS1_N) && tb->ram_a < 0x200000) {
        if (!tb->we_n) {
            ram[tb->ram_a << 1] = (tb->mdout & 0xff00) >> 8;
            ram[(tb->ram_a << 1) + 1] = tb->mdout & 0xff;
        }
        tb->mdin = (ram[tb->ram_a << 1] * 256) + ram[(tb->ram_a << 1) + 1];
    }
}

static void period() { tick(1); tick(0); }
static void waitEn1() { while (!tb->MHZ8_EN1) period(); }
static void waitEn2() { while (!tb->MHZ8_EN2) period(); }

static void writeReg(uint32_t addr, uint16_t data) {
    waitEn1();
    waitEn2();
    tb->A = addr >> 1;
    while (!tb->bus_free) period();
    tb->RW = 1;
    waitEn1();
    tb->AS_N = 0;
    tb->RW = 0;
    waitEn2();
    tb->DIN = data;
    waitEn1();
    tb->UDS_N = 0;
    tb->LDS_N = 0;
    do { period(); } while (!(tb->MHZ8_EN2 && !(tb->DTACK_N && tb->BERR_N)));
    waitEn1();
    waitEn2();
    tb->AS_N = 1;
    tb->UDS_N = 1;
    tb->LDS_N = 1;
    period();
    period();
    tb->RW = 1;
    period();
}

static void waitVsyncFall() {
    int prev = tb->VSYNC_N;
    for (int guard = 0; guard < 4000000; ++guard) {
        period();
        if (prev && !tb->VSYNC_N) return;
        prev = tb->VSYNC_N;
    }
    std::fprintf(stderr, "VSYNC absent\n");
    std::exit(3);
}

int main(int argc, char** argv) {
    if (argc != 22) {
        std::fprintf(stderr, "usage: %s vram sortie mde ntsc via_ntsc c0..c15\n", argv[0]);
        return 2;
    }
    FILE* in = std::fopen(argv[1], "rb");
    if (!in) { std::perror(argv[1]); return 2; }
    size_t got = std::fread(ram + kVideoBase, 1, sizeof(ram) - kVideoBase, in);
    std::fclose(in);
    if (got != 32000) { std::fprintf(stderr, "vram : %zu octets\n", got); return 2; }

    Verilated::commandArgs(argc, argv);
    tb = new Vste_tb;

    // Séquence de reset de ste_tb.cpp @ 4a6db7cc.
    tb->resb = 0; tb->porb = 0;
    period();
    tb->turbo = 0;
    tb->AS_N = 1; tb->UDS_N = 1; tb->LDS_N = 1; tb->RW = 1;
    tb->VMA_N = 1; tb->MFPINT_N = 1; tb->BR_N = 1;
    tb->FC0 = 0; tb->FC1 = 1; tb->FC2 = 1;
    tb->resb = 1; tb->porb = 1;
    period();
    tb->resb = 0;
    period();
    tb->resb = 1;

    writeReg(0xff8000, 0x0c);                       // configuration mémoire
    writeReg(0xff8200, (kVideoBase >> 16) & 0xff);  // base vidéo haute
    writeReg(0xff8202, (kVideoBase >> 8) & 0xff);   // base vidéo moyenne
    writeReg(0xff820c, kVideoBase & 0xff);          // base vidéo basse
    for (int i = 0; i < 16; ++i)
        writeReg(0xff8240 + 2 * i, uint16_t(std::strtoul(argv[6 + i], nullptr, 16)));
    if (std::atoi(argv[5])) {
        writeReg(0xff820a, 0);
        writeReg(0xff8260, uint16_t(std::atoi(argv[3]) << 8));
        for (int i = 0; i < 3; ++i) waitVsyncFall();
    }
    writeReg(0xff820a, std::atoi(argv[4]) ? 0 : 0x200);
    writeReg(0xff8260, uint16_t(std::atoi(argv[3]) << 8));

    // Deux trames pour laisser le changement de mode et de fréquence se poser.
    waitVsyncFall();
    waitVsyncFall();

    std::vector<uint16_t> frame;
    frame.reserve(700000);
    int prev = 0;
    for (size_t i = 0; i < 2000000; ++i) {
        const auto* r = tb->rootp;
        frame.push_back(uint16_t((tb->R << 8) | (tb->G << 4) | tb->B
                                 | (tb->BLANK_N ? 1u << 12 : 0)
                                 | (tb->HSYNC_N ? 1u << 13 : 0)
                                 | (tb->VSYNC_N ? 1u << 14 : 0)
                                 | (r->ste_tb__DOT__de ? 1u << 15 : 0)));
        period();
        if (prev && !tb->VSYNC_N) break;
        prev = tb->VSYNC_N;
    }
    FILE* out = std::fopen(argv[2], "wb");
    if (!out) { std::perror(argv[2]); return 2; }
    std::fwrite(frame.data(), sizeof(uint16_t), frame.size(), out);
    std::fclose(out);
    delete tb;
    return 0;
}
