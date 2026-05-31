// =============================================================================
//  main_headless.cpp — NeoST sans interface : exécution déterministe + traces.
//
//  But : produire des journaux d'exécution très précis (trace d'instructions
//  façon MAME, registres, interruptions) pour diff avec une trace MAME, et
//  pouvoir tourner en CI / sans serveur graphique. Aucune dépendance GL/GLFW.
//
//  Exemples :
//    neost-headless --frames 50 --trace trace.txt
//    neost-headless --frames 50 --trace trace.txt --regs --irq
//    neost-headless --until-pc FC0030 --trace -        (trace vers stdout)
//    neost-headless --frames 50 --screenshot screen.ppm
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/Machine.hpp"
#include "core/Tracer.hpp"

namespace {
void usage() {
    std::printf(
        "Usage: neost-headless [options] [rom]\n"
        "  --frames N        nombre de trames à exécuter (défaut 200, ~4 s ST)\n"
        "  --trace FILE      écrit la trace d'instructions ('-' = stdout)\n"
        "  --regs            ajoute l'état des registres à chaque instruction\n"
        "  --irq             trace aussi les interruptions prises\n"
        "  --until-pc HEX    arrête dès que PC atteint cette adresse (hex)\n"
        "  --walk-mouse      après le boot, injecte un mouvement souris + clic (diag)\n"
        "  --screenshot PPM  dump du framebuffer final au format PPM\n"
        "  rom               image TOS (défaut rom/etos192fr.img)\n");
}

// Dump du framebuffer décodé en PPM binaire (P6) — comparable visuellement.
bool writePpm(const char* path, const uint32_t* px, int w, int h) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) {
        const uint32_t c = px[i];                 // ARGB8888
        const unsigned char rgb[3] = {
            static_cast<unsigned char>((c >> 16) & 0xFF),
            static_cast<unsigned char>((c >> 8)  & 0xFF),
            static_cast<unsigned char>( c        & 0xFF) };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    int         frames     = 200;
    std::string tracePath;
    std::string shotPath;
    std::string romPath    = "rom/etos192fr.img";
    bool        regs       = false;
    bool        irq        = false;
    bool        haveUntil  = false;
    uint32_t    untilPc    = 0;
    bool        walkMouse  = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s attend un argument\n", opt); std::exit(2); }
            return argv[++i];
        };
        if      (!std::strcmp(a, "--frames"))     frames    = std::atoi(next(a));
        else if (!std::strcmp(a, "--trace"))      tracePath = next(a);
        else if (!std::strcmp(a, "--regs"))       regs      = true;
        else if (!std::strcmp(a, "--irq"))        irq       = true;
        else if (!std::strcmp(a, "--screenshot")) shotPath  = next(a);
        else if (!std::strcmp(a, "--walk-mouse")) walkMouse = true;
        else if (!std::strcmp(a, "--until-pc"))   { untilPc = (uint32_t)std::strtoul(next(a), nullptr, 16); haveUntil = true; }
        else if (!std::strcmp(a, "-h") || !std::strcmp(a, "--help")) { usage(); return 0; }
        else if (a[0] == '-')                     { std::fprintf(stderr, "option inconnue: %s\n", a); usage(); return 2; }
        else                                      romPath   = a;
    }

    Machine machine;
    if (!machine.loadTos(romPath)) {
        std::fprintf(stderr, "[headless] impossible de charger %s\n", romPath.c_str());
        return 1;
    }

    Tracer tracer;
    if (!tracePath.empty()) {
        if (!tracer.open(tracePath)) {
            std::fprintf(stderr, "[headless] impossible d'ouvrir la trace %s\n", tracePath.c_str());
            return 1;
        }
        tracer.setLogRegs(regs);
        tracer.setLogInterrupts(irq);
        machine.cpu.setTracer(&tracer);    // active le hook d'instruction
    }

    machine.reset();

    // Exécution déterministe : nombre fixe de trames (pas de Date/random/sleep).
    // Note : --until-pc s'évalue par trame (granularité d'une trame), suffisant
    // pour borner une capture autour d'un point d'intérêt.
    for (int frame = 0; frame < frames; ++frame) {
        machine.runFrame();
        if (haveUntil && machine.cpu.pc() == untilPc) {
            std::fprintf(stderr, "[headless] PC=$%06X atteint à la trame %d\n", untilPc, frame);
            break;
        }
    }

    // Diagnostic souris : après boot, on déplace le pointeur en diagonale et on
    // clique au milieu du parcours, pour voir si le curseur GEM apparaît/bouge.
    if (walkMouse) {
        auto idle    = [&](int frames) { for (int i = 0; i < frames; ++i) machine.runFrame(); };
        auto packet  = [&](int dx, int dy, bool l) {
            machine.ikbd.mouseEvent(dx, dy, l, false);
            machine.cpu.updateIpl();
            machine.runFrame();
        };
        // 1) Curseur (centre 320,200) → icône Disque A (~haut-gauche).
        for (int i = 0; i < 58; ++i) packet(-5, -3, false);
        idle(5);
        // 2) Double-clic réaliste : on n'envoie un paquet QUE sur transition.
        packet(0, 0, true);  idle(2);   // clic 1 bas
        packet(0, 0, false); idle(3);   // clic 1 haut
        packet(0, 0, true);  idle(2);   // clic 2 bas
        packet(0, 0, false); idle(40);  // clic 2 haut, puis on laisse réagir
        std::fprintf(stderr, "[headless] séquence : déplacement + double-clic Disque A\n");
    }

    std::fprintf(stderr, "[headless] %llu instructions tracées\n",
                 (unsigned long long)tracer.instructionCount());

    if (!shotPath.empty()) {
        if (writePpm(shotPath.c_str(), machine.shifter.pixels(),
                     machine.shifter.width(), machine.shifter.height()))
            std::fprintf(stderr, "[headless] capture écran → %s (%dx%d)\n",
                         shotPath.c_str(), machine.shifter.width(), machine.shifter.height());
    }

    tracer.close();
    return 0;
}
