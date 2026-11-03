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
//          Vste_tb --glue <plan.txt> <sortie.txt> : écritures datées de $FF820A et $FF8260 (plan :
//          « trame ligne cycle adresse mot » ; cycles comptés depuis HSYNC, -52 à 459), puis les
//          fronts de DE, datés depuis VSYNC, avec le compteur vidéo. Cf. tools/run_glue_sweep.py.
//          Vste_tb --dtack <vram.bin> : latence de DTACK par zone, DE actif ou non, son DMA actif ou
//          non, à des phases variées. Une ligne « zone sens de son latence effectif » par
//          combinaison, latence en cycles de 8 MHz entre la chute d'AS et celle de DTACK.
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
#include <cstring>
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

static void (*g_onTick)() = nullptr;
static void period() { tick(1); tick(0); if (g_onTick) g_onTick(); }
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

// Un cycle bus du CPU, données comprises : rend le nombre de périodes de 32 MHz entre AS et DTACK.
static int probeAccess(uint32_t addr, bool write) {
    waitEn1();
    tb->A = addr >> 1;
    tb->RW = write ? 0 : 1;
    tb->AS_N = 0;
    if (!write) { tb->UDS_N = 0; tb->LDS_N = 0; }
    else tb->DIN = 0;
    int ticks = 0;
    while (tb->DTACK_N && tb->BERR_N && ticks < 800) {
        period();
        ++ticks;
        if (write && ticks == 4) { tb->UDS_N = 0; tb->LDS_N = 0; }
    }
    waitEn1();
    waitEn2();
    tb->AS_N = 1; tb->UDS_N = 1; tb->LDS_N = 1;
    period();
    period();
    tb->RW = 1;
    period();
    return ticks;
}

static void setupProbe() {
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
    writeReg(0xff8000, 0x0c);
    writeReg(0xff8200, (kVideoBase >> 16) & 0xff);
    writeReg(0xff8202, (kVideoBase >> 8) & 0xff);
    writeReg(0xff820c, kVideoBase & 0xff);
    writeReg(0xff820a, 0x200);
    waitVsyncFall();
    waitVsyncFall();
}

static int dtackMain() {
    tb = new Vste_tb;
    setupProbe();
    struct Zone { const char* name; uint32_t addr; bool write; };
    static const Zone zones[] = {
        {"RAM", 0x010000, false}, {"RAM", 0x010000, true}, {"ROM", 0xE00000, false},
        {"cartouche", 0xFA0000, false}, {"MMU_ff8001", 0xFF8000, false}, {"video_ff8200", 0xFF8200, false},
        {"palette_ff8240", 0xFF8240, false}, {"palette_ff8240", 0xFF8240, true},
        {"res_ff8260", 0xFF8260, false}, {"dma_ff8604", 0xFF8604, false},
        {"son_ff8900", 0xFF8900, false}, {"son_ff8900", 0xFF8900, true},
        {"microwire_ff8922", 0xFF8922, false}, {"microwire_ff8924", 0xFF8924, false},
        {"son_ff8930", 0xFF8930, false}, {"son_ff8938", 0xFF8938, false},
        {"sync_ff820a", 0xFF820A, false}, {"sync_ff820a", 0xFF820A, true},
        {"scroll_ff8264", 0xFF8264, false}, {"scroll_ff827e", 0xFF827E, false},
    };
    unsigned phase = 1;
    for (int sound = 0; sound < 2; ++sound) {
        if (sound) {                                // boucle de son sur 64 Ko de RAM
            writeReg(0xff8902, 0x01); writeReg(0xff8904, 0x00); writeReg(0xff8906, 0x00);
            writeReg(0xff890e, 0x02); writeReg(0xff8910, 0x00); writeReg(0xff8912, 0x00);
            writeReg(0xff8920, 0x03);               // 50 kHz, stéréo : la plus forte charge
            writeReg(0xff8900, 0x03);
        }
        for (const Zone& z : zones) {
            int hist[2][64] = {};
            for (int i = 0; i < 6000; ++i) {
                phase = phase * 1103515245u + 12345u;
                for (unsigned k = (phase >> 16) & 7; k; --k) period();   // phase libre
                const int de = tb->rootp->ste_tb__DOT__de ? 1 : 0;
                const int ticks = probeAccess(z.addr, z.write);
                ++hist[de][ticks / 4 < 63 ? ticks / 4 : 63];
            }
            for (int de = 0; de < 2; ++de)
                for (int l = 0; l < 64; ++l)
                    if (hist[de][l])
                        std::printf("%s %c de=%d son=%d lat=%d n=%d\n", z.name, z.write ? 'W' : 'R', de, sound, l, hist[de][l]);
        }
    }
    delete tb;
    return 0;
}

// --- Balayage des écritures GLUE -------------------------------------------------------------
namespace sweep {
struct Event { int frame, line, cycle; uint32_t addr; uint16_t word; };
int frame = -1, hs = -1, th = 0, prevHs = 1, prevVs = 1, prevDe = 0;
long tAbs = 0;
FILE* out = nullptr;

uint32_t vidNow() { return tb->rootp->ste_tb__DOT__gstmcu__DOT__vid_reg; }

// Dates absolues depuis VSYNC : une impulsion peut déplacer HSYNC sans déplacer DE.
void onTick() {
    const int hsNow = tb->HSYNC_N, vsNow = tb->VSYNC_N, deNow = tb->rootp->ste_tb__DOT__de;
    ++th; ++tAbs;
    if (prevDe != deNow) {
        if (out) std::fprintf(out, "E %d %ld %d %u\n", frame, tAbs, deNow ? 1 : 0, vidNow());
        prevDe = deNow;
    }
    if (prevHs && !hsNow) {                          // front descendant de HSYNC
        // Une impulsion haute résolution vers le cycle 28 fait tomber HSYNC une fois de plus sans
        // commencer de ligne : la DE reste en place. Le plus court vrai intervalle vaut 224 cycles.
        if (th >= 600) {
            ++hs; th = 0;
            if (out) std::fprintf(out, "H %d %d %ld\n", frame, hs, tAbs);
        } else if (out) {
            std::fprintf(out, "S %d %d %ld\n", frame, hs, tAbs);
        }
    }
    if (prevVs && !vsNow) { ++frame; hs = -1; th = 0; tAbs = 0; }
    prevHs = hsNow; prevVs = vsNow;
}

// Écriture mot, depuis un front EN1. Rend le tick d'AS relatif à HSYNC.
int write(uint32_t addr, uint16_t word) {
    tb->A = addr >> 1;
    tb->RW = 1;
    waitEn1();
    const int asTick = th;
    tb->AS_N = 0;
    tb->RW = 0;
    waitEn2();
    tb->DIN = word;
    waitEn1();
    tb->UDS_N = 0;
    tb->LDS_N = 0;
    do { period(); } while (!(tb->MHZ8_EN2 && !(tb->DTACK_N && tb->BERR_N)));
    waitEn1();
    waitEn2();
    tb->AS_N = 1; tb->UDS_N = 1; tb->LDS_N = 1;
    period();
    period();
    tb->RW = 1;
    period();
    return asTick;
}
}  // namespace sweep

static int glueMain(const char* planPath, const char* outPath) {
    using namespace sweep;
    std::vector<Event> plan;
    FILE* in = std::fopen(planPath, "r");
    if (!in) { std::perror(planPath); return 2; }
    Event e;
    unsigned addr, word;
    int maxFrame = 0;
    while (std::fscanf(in, "%d %d %d %x %x", &e.frame, &e.line, &e.cycle, &addr, &word) == 5) {
        e.addr = addr; e.word = uint16_t(word);
        plan.push_back(e);
        if (e.frame > maxFrame) maxFrame = e.frame;
    }
    std::fclose(in);
    tb = new Vste_tb;
    setupProbe();
    g_onTick = onTick;
    out = std::fopen(outPath, "w");
    if (!out) { std::perror(outPath); return 2; }
    waitVsyncFall();
    frame = 0; hs = -1; th = 0;
    size_t next = 0;
    while (frame <= maxFrame) {
        period();
        while (next < plan.size() && plan[next].frame <= frame
               && (plan[next].frame < frame || plan[next].line < hs
                   || (plan[next].line == hs && th >= (plan[next].cycle + 52) * 4 - 4))) {
            const Event& ev = plan[next];
            const int f = frame, h = hs;
            const int as = write(ev.addr, ev.word);
            std::fprintf(out, "W %d %d %d %d %06X %04X\n", ev.frame, ev.line, ev.cycle, (f == frame && h == hs) ? as : -99999, ev.addr, ev.word);
            ++next;
        }
    }
    std::fclose(out);
    delete tb;
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--glue") == 0) {
        Verilated::commandArgs(argc, argv);
        return glueMain(argv[2], argv[3]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--dtack") == 0) {
        Verilated::commandArgs(argc, argv);
        return dtackMain();
    }
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
