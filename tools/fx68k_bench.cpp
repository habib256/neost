// =============================================================================
//  fx68k_bench.cpp — Pilote Verilator du cœur fx68k (Jorge Cwik) sur une RAM plate.
//
//  POURQUOI. Le 68000 de NeoST est Moira, et ses cycles n'étaient confrontés qu'à
//  Hatari (grade D, autre émulateur) et aux totaux de la table de Musashi. fx68k est
//  un RTL « cycle exact » dérivé du microcode (échelon `rtl`, bloc fort dans
//  docs/REFERENCE_LADDER.md) : ce pilote en relève chaque cycle bus, pour le comparer
//  à ceux de Moira nu (tools/moira_bare_bench.cpp) sur le même programme
//  (tools/run_fx68k_bench.py). Ce n'est pas une mesure de silicium : grade D.
//
//  COMMENT. RAM plate de 16 Mio, DTACK sans état d'attente, VPA jamais posé (les
//  interruptions sont vectorisées par le périphérique, comme le MFP : le vecteur
//  $40+niveau est servi sur D0-D7 pendant l'IACK, et la broche IPL retombe à
//  l'acquittement). Horloge : une période de `clk` par phase, enPhi1 puis enPhi2 (fx68k.txt
//  § Developer Notes), soit un cycle CPU pour deux périodes.
//
//  CONVENTION DE DATATION. [B] M68000UM Rev 8 § 5.1.1 p. 5-4 : AS tombe au front montant
//  de S2, un cycle d'horloge après S0. Chaque accès est daté au front
//  descendant d'AS, en cycles CPU depuis la fin du reset. Le second transfert d'un cycle
//  lecture-modification-écriture (TAS, AS reste bas) est daté à la retombée de son strobe
//  UDS/LDS moins un cycle : [B] § 5.1.2 p. 5-6, une écriture pose DS à S4, un cycle après
//  AS ; § 5.1.3 p. 5-9, la phase d'écriture du RMW pose DS à S16.
//
//  Usage : Vfx68k_bench <image.bin> <trace.txt> [--limit N] [--irq T:L]... [--vpa]
//    --vpa : l'IACK répond par VPA, sans DTACK, comme HBL et VBL sur ST (autovecteur $18+niveau).
//    image.bin : contenu de la mémoire à partir de $000000 (le reste est à zéro).
//    --irq T:L : poser le niveau L sur IPL au cycle T, compté depuis le 7e accès bus, le
//                premier après la séquence de reset (SSP, PC, deux préfetchs), comme côté
//                Moira. Pas depuis le 1er : les deux cœurs diffèrent de 2 cycles dans cette
//                séquence, et la broche tomberait à une autre phase du programme.
//  Trace : une ligne par cycle bus « t fc R|W taille adresse donnée » (hexadécimal
//  sauf t, fc et taille), puis « END t=<cycles> reason=<sentinel|halt|limit> ».
//  Le programme se termine par une écriture dans $F00000-$F0FFFF (sentinelle).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test. fx68k reste GPL-3 et n'est
//  jamais lié au binaire NeoST : ce pilote est un exécutable à part.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <verilated.h>
#include "Vfx68k.h"

namespace {

Vfx68k* cpu = nullptr;
std::vector<uint8_t> ram(1u << 24);
int64_t cyc = 0;                 // cycles CPU depuis la fin du reset
int nAs = 0;                     // fronts descendants d'AS vus
int64_t tOrigin = -1;            // date du 7e accès bus (origine des --irq)
FILE* out = nullptr;

struct IrqEv { int64_t t; int level; };
std::vector<IrqEv> irqs;
size_t irqIdx = 0;

struct Access {
    int64_t t = 0; int fc = 0; bool read = true; int size = 0;
    uint32_t addr = 0; uint16_t data = 0; bool strobed = false; bool emitted = false;
};
Access cur;
bool asLow = false, strobeLow = false, done = false;
bool armed = false;   // vrai dès qu'AS a été vu haut : les sorties valent 0 avant le reset
bool useVpa = false;  // l'IACK répond par VPA (autovecteur)
std::string reason = "limit";

void setIpl(int level) {
    cpu->IPL0n = !(level & 1);
    cpu->IPL1n = !(level & 2);
    cpu->IPL2n = !(level & 4);
}

uint16_t ramWord(uint32_t a) { a &= 0xFFFFFE; return uint16_t(ram[a] << 8 | ram[a + 1]); }

void emit(const Access& a) {
    std::fprintf(out, "%lld %d %c %d %06X %0*X\n", (long long)a.t, a.fc, a.read ? 'R' : 'W',
                 a.size, a.addr, a.size == 2 ? 4 : 2, a.data);
    if (!a.read && (a.addr & 0xFF0000) == 0xF00000) { done = true; reason = "sentinel"; }
}

// Observé après chaque front montant de clk. Le modèle mémoire répond dans la même
// période : DTACK dès AS, donnée de lecture présentée tant qu'AS est bas.
void observe() {
    const bool as = !cpu->ASn;
    const int fc = cpu->FC0 | cpu->FC1 << 1 | cpu->FC2 << 2;
    const uint32_t wordAddr = (uint32_t(cpu->eab) << 1) & 0xFFFFFE;
    const bool uds = !cpu->UDSn, lds = !cpu->LDSn;
    const bool strobe = uds || lds;

    if (!as) armed = true;
    if (!armed) return;                              // phase de reset : rien à dater
    if (as && !asLow) {                              // front descendant d'AS
        asLow = true;
        cur = Access{};
        cur.t = cyc; cur.fc = fc; cur.read = cpu->eRWn; cur.addr = wordAddr;
        if (++nAs == 7) tOrigin = cyc;
    }
    if (as) {
        if (strobe && !strobeLow) {                  // début d'un transfert
            strobeLow = true;
            if (cur.emitted) {                       // 2e transfert d'un RMW (TAS)
                cur = Access{};
                cur.t = cyc - 1; cur.fc = fc; cur.read = cpu->eRWn; cur.addr = wordAddr;
            }
            cur.size = (uds && lds) ? 2 : 1;
            if (!uds && lds) cur.addr |= 1;
            cur.strobed = true;
        }
        if (fc == 7) {                               // IACK : vecteur utilisateur
            // Consigné comme Moira : octet sur D0-D7, adresse impaire. fx68k pose UDS et
            // LDS pendant l'IACK ; le vecteur n'est lu que sur l'octet bas.
            const int level = (wordAddr >> 1) & 7;
            cpu->iEdb = uint16_t(0x40 + level);
            cur.size = 1; cur.addr = wordAddr | 1; cur.data = uint8_t(0x40 + level);
            if (strobe) setIpl(0);                   // le périphérique est acquitté
        } else if (cpu->eRWn) {
            const uint16_t w = ramWord(wordAddr);
            cpu->iEdb = w;
            // Donnée relevée tant que les strobes sont bas : après leur remontée, uds/lds
            // valent 0 et désigneraient l'octet bas à tort.
            if (strobe && cur.strobed) cur.data = cur.size == 2 ? w : (uds ? w >> 8 : w & 0xFF);
        } else if (strobe && cur.strobed) {          // écriture : latch de la donnée
            const uint16_t w = cpu->oEdb;
            if (uds) ram[wordAddr] = uint8_t(w >> 8);
            if (lds) ram[wordAddr + 1] = uint8_t(w);
            cur.data = cur.size == 2 ? w : (uds ? w >> 8 : w & 0xFF);
        }
        cpu->DTACKn = (useVpa && fc == 7) ? 1 : 0;
        cpu->VPAn = (useVpa && fc == 7) ? 0 : 1;
        if (!strobe && strobeLow) {                  // fin du transfert
            strobeLow = false;
            if (!cur.emitted) { emit(cur); cur.emitted = true; }
        }
    } else {
        cpu->DTACKn = 1;
        cpu->VPAn = 1;
        if (asLow) {
            asLow = false; strobeLow = false;
            if (!cur.emitted) { emit(cur); cur.emitted = true; }
        }
    }
}

void halfCycle(bool phi1) {
    cpu->enPhi1 = phi1;
    cpu->enPhi2 = !phi1;
    cpu->clk = 1; cpu->eval();
    observe();    cpu->eval();
    cpu->clk = 0; cpu->eval();
}

void cpuCycle() {
    if (tOrigin >= 0)
        while (irqIdx < irqs.size() && cyc - tOrigin >= irqs[irqIdx].t) setIpl(irqs[irqIdx++].level);
    halfCycle(true);
    halfCycle(false);
    ++cyc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s image.bin trace.txt [--limit N] [--irq T:L]... [--vpa]\n", argv[0]); return 2; }
    int64_t limit = 400000;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--limit") && i + 1 < argc) limit = std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--irq") && i + 1 < argc) {
            const char* s = argv[++i]; const char* c = std::strchr(s, ':');
            if (!c) { std::fprintf(stderr, "--irq attend T:L\n"); return 2; }
            irqs.push_back({std::atoll(s), std::atoi(c + 1)});
        } else if (!std::strcmp(argv[i], "--vpa")) useVpa = true;
        else { std::fprintf(stderr, "argument inconnu : %s\n", argv[i]); return 2; }
    }
    if (FILE* f = std::fopen(argv[1], "rb")) {
        const size_t n = std::fread(ram.data(), 1, ram.size(), f);
        std::fclose(f);
        if (!n) { std::fprintf(stderr, "image vide : %s\n", argv[1]); return 2; }
    } else { std::fprintf(stderr, "image illisible : %s\n", argv[1]); return 2; }
    out = std::fopen(argv[2], "w");
    if (!out) { std::fprintf(stderr, "trace inécrivable : %s\n", argv[2]); return 2; }

    Verilated::commandArgs(argc, argv);
    cpu = new Vfx68k;
    cpu->HALTn = 1; cpu->DTACKn = 1; cpu->VPAn = 1; cpu->BERRn = 1;
    cpu->BRn = 1; cpu->BGACKn = 1; cpu->iEdb = 0;
    setIpl(0);
    // Reset à froid : pwrUp ET extReset posés ensemble (fx68k.txt), tenus 32 cycles.
    cpu->pwrUp = 1; cpu->extReset = 1;
    for (int i = 0; i < 32; ++i) { halfCycle(true); halfCycle(false); }
    cpu->pwrUp = 0; cpu->extReset = 0;
    cyc = 0;

    while (!done && cyc < limit) {
        cpuCycle();
        if (!cpu->oHALTEDn) { reason = "halt"; break; }
    }
    std::fprintf(out, "END t=%lld reason=%s\n", (long long)cyc, reason.c_str());
    std::fclose(out);
    cpu->final();
    delete cpu;
    return 0;
}
