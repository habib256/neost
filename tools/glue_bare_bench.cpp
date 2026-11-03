// =============================================================================
//  glue_bare_bench.cpp — La machine GLUE de NeoST seule, sur des écritures datées.
//
//  POURQUOI. Pendant de « Vste_tb --glue » : le même plan d'écritures de $FF820A et $FF8260,
//  rejoué dans Shifter::replayGlue sans CPU. tools/run_glue_sweep.py compare ligne à ligne.
//
//  Entrée (stdin) : une expérience par ligne, « id L n l1 c1 r1 v1 ... ». Sortie : « id ligne début
//  fin octets masque début_de_ligne » pour les lignes L à L + 2. Machine : st ou ste en argument.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
// =============================================================================
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/Bus.hpp"
#include "core/Shifter.hpp"

int main(int argc, char** argv) {
    Bus bus;
    bus.machine = (argc > 1 && !std::strcmp(argv[1], "st")) ? MachineType::St : MachineType::Ste;
    Shifter shifter(bus);
    int id, line, n;
    while (std::scanf("%d %d %d", &id, &line, &n) == 3) {
        std::vector<std::array<int, 4>> writes(static_cast<std::size_t>(n));
        for (auto& w : writes)
            if (std::scanf("%d %d %d %d", &w[0], &w[1], &w[2], &w[3]) != 4) return 2;
        shifter.glueBenchReplay(writes);
        for (int l = line; l <= line + 2; ++l) {
            const Shifter::GlueProbe p = shifter.glueBenchLine(l);
            std::printf("%d %d %d %d %d %x %lld\n", id, l, p.start, p.end, p.bytes, p.mask, (long long)p.lineStart);
        }
    }
    return 0;
}
