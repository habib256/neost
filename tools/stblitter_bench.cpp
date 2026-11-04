// =============================================================================
//  stblitter_bench.cpp — Pilote Verilator de fx68k + stBlitter sur une RAM plate.
//
//  POURQUOI. Pendant RTL de tools/blitter_bare_bench.cpp : le même programme fait travailler
//  le blitter, et on relève chaque accès de celui-ci, plus les écritures du CPU dans la
//  fenêtre témoin. tools/run_stblitter_bench.py compare les deux traces. Grade D.
//
//  CONVENTION. Chaque accès est daté au front descendant d'AS, en cycles CPU. Une période de
//  `clk` par phase (enPhi1 puis enPhi2), soit un cycle CPU pour deux périodes, comme
//  tools/fx68k_bench.cpp. Par défaut, DTACK posé dès AS : aucun état d'attente. Avec --slot, un
//  accès mémoire de l'un ou l'autre maître attend le créneau de 4 cycles de la MMU : S0 + attente
//  multiple de 4, l'attente comptée en cycles entiers. C'est le modèle de NEOST_RAM_SLOT, pas un
//  relevé : [F] la phase du créneau par rapport au reset est libre ; elle ne change que la date
//  absolue, une fois le premier accès aligné. Un accès en attente est daté à la fin de l'attente,
//  sa date utile, et non à AS, qu'un maître peut poser un cycle avant le créneau.
//
//  Usage : Vstblitter_bench <image.bin> <trace.txt> [--limit N] [--slot]
//  Trace : « t M R|W adresse donnée », M = B (blitter) ou C (CPU, fenêtre témoin seule), puis
//  « END t=<cycles> reason=<sentinel|halt|limit> ».
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test. Les RTL restent GPL-3 et ne sont
//  jamais liés au binaire NeoST : ce pilote est un exécutable à part.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <verilated.h>
#include "Vstblitter_bench_top.h"

namespace {
constexpr uint32_t WITNESS = 0x3000, WITNESS_END = 0x3300, SENTINEL = 0x32FE;

Vstblitter_bench_top* top = nullptr;
std::vector<uint8_t> ram(1u << 24);
int64_t cyc = 0;
FILE* out = nullptr;
bool done = false, armed = false, asLow = false, logged = false;
std::string reason = "limit";
int64_t tAs = 0;
bool masterBlit = false;
bool useSlot = false;
int64_t waits = 0;

uint16_t ramWord(uint32_t a) { a &= 0xFFFFFE; return uint16_t(ram[a] << 8 | ram[a + 1]); }

void observe() {
    const bool as = !top->as_n;
    const uint32_t a = (uint32_t(top->addr) << 1) & 0xFFFFFE;
    const bool uds = !top->uds_n, lds = !top->lds_n, strobe = uds || lds, read = top->rw_n;
    if (!as) armed = true;
    if (!armed) return;
    if (as && !asLow) {
        asLow = true; logged = false; tAs = cyc; masterBlit = top->blit_has_bus;
        waits = useSlot && !top->blit_sel ? (4 - ((tAs - 1) & 3)) & 3 : 0;     // AS = S0 + 1
    }
    if (as) {
        top->mem_data = ramWord(a);
        top->mem_dtack_n = (top->blit_sel || cyc - tAs < waits) ? 1 : 0;
        if (strobe && !read && !logged && !top->blit_sel) {
            const uint16_t w = top->wdata;
            if (uds) ram[a] = uint8_t(w >> 8);
            if (lds) ram[a + 1] = uint8_t(w);
            logged = true;
            if (masterBlit || (a >= WITNESS && a < WITNESS_END))
                std::fprintf(out, "%lld %c W %06X %04X\n", (long long)(tAs + waits), masterBlit ? 'B' : 'C', a, w);
            if (!masterBlit && a == SENTINEL) { done = true; reason = "sentinel"; }
        } else if (strobe && read && !logged && masterBlit) {
            logged = true;
            std::fprintf(out, "%lld B R %06X %04X\n", (long long)(tAs + waits), a, ramWord(a));
        }
    } else {
        top->mem_dtack_n = 1;
        asLow = false;
    }
}

void halfCycle(bool phi1) {
    top->enPhi1 = phi1; top->enPhi2 = !phi1;
    top->clk = 1; top->eval();
    observe();    top->eval();
    top->clk = 0; top->eval();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s image.bin trace.txt [--limit N] [--slot]\n", argv[0]); return 2; }
    int64_t limit = 2000000;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--limit") && i + 1 < argc) limit = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--slot")) useSlot = true;
        else { std::fprintf(stderr, "argument inconnu : %s\n", argv[i]); return 2; }
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "image illisible : %s\n", argv[1]); return 2; }
    const size_t n = std::fread(ram.data(), 1, ram.size(), f);
    std::fclose(f);
    if (!n) { std::fprintf(stderr, "image vide : %s\n", argv[1]); return 2; }
    out = std::fopen(argv[2], "w");
    if (!out) { std::fprintf(stderr, "trace inécrivable : %s\n", argv[2]); return 2; }

    Verilated::commandArgs(argc, argv);
    top = new Vstblitter_bench_top;
    top->mem_dtack_n = 1; top->mem_data = 0;
    top->pwrUp = 1; top->extReset = 1;
    for (int i = 0; i < 32; ++i) { halfCycle(true); halfCycle(false); }
    top->pwrUp = 0; top->extReset = 0;
    cyc = 0;
    while (!done && cyc < limit) {
        halfCycle(true); halfCycle(false); ++cyc;
        if (!top->halted_n) { reason = "halt"; break; }
    }
    std::fprintf(out, "END t=%lld reason=%s\n", (long long)cyc, reason.c_str());
    std::fclose(out);
    top->final();
    delete top;
    return 0;
}
