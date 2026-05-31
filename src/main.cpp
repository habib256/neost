// =============================================================================
//  main.cpp — Boucle d'horloge matérielle de NeoST.
//
//  Modèle temporel (PAL, basse résolution) :
//    - CPU 68000 cadencé à ~8 MHz.
//    - 1 ligne écran = 512 cycles CPU (64 µs).
//    - 1 trame = 313 lignes → ~50 Hz. 200 lignes visibles, le reste = bordure/VBlank.
//  La boucle exécute X cycles CPU PUIS rattrape le Shifter ligne par ligne :
//  c'est le coeur du "couplage matériel" que NeoST veut rendre transparent.
//
//  UI façon POM1/POM2 : GLFW3 + OpenGL (immediate mode) + Dear ImGui en
//  superposition (hexa mémoire + registres CPU) + reset physique virtuel (F12).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <GLFW/glfw3.h>
#include <cstdio>

#include "core/Bus.hpp"
#include "core/Cpu68k.hpp"
#include "core/Shifter.hpp"
#include "core/YM2149.hpp"
#include "core/Glue.hpp"

#if defined(NEOST_WITH_IMGUI)
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#endif

namespace {
// --- Constantes d'horloge (cf. en-tête) -------------------------------------
constexpr int CYCLES_PER_LINE = 512;
constexpr int LINES_PER_FRAME = 313;   // PAL
constexpr int VISIBLE_LINES   = 200;
constexpr int IRQ_HBL         = 2;     // niveau auto-vectorisé ligne (HBLANK)
constexpr int IRQ_VBL         = 4;     // niveau auto-vectorisé trame (VBLANK)

constexpr int WIN_SCALE = 3;           // 320x200 → fenêtre 960x600

void onGlfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW erreur %d : %s\n", code, desc);
}

#if defined(NEOST_WITH_IMGUI)
// Visualiseur hexadécimal minimal de la mémoire (RAM).
void drawHexViewer(Bus& bus) {
    static int base = 0;
    ImGui::Begin("Mémoire (hex)");
    ImGui::InputInt("Adresse base", &base, 16, 256, ImGuiInputTextFlags_CharsHexadecimal);
    if (base < 0) base = 0;
    const auto& mem = bus.ram;
    for (int row = 0; row < 16; ++row) {
        const int addr = base + row * 16;
        char line[128];
        int n = std::snprintf(line, sizeof line, "%06X:", addr);
        for (int col = 0; col < 16 && (addr + col) < (int)mem.size(); ++col)
            n += std::snprintf(line + n, sizeof line - n, " %02X", mem[addr + col]);
        ImGui::TextUnformatted(line);
    }
    ImGui::End();
}

// Visualiseur des registres CPU.
void drawCpuState(Cpu68k& cpu) {
    ImGui::Begin("CPU 68000");
    ImGui::Text("PC = %08X    SR = %04X", cpu.pc(), cpu.sr());
    ImGui::Separator();
    for (int i = 0; i < 8; ++i)
        ImGui::Text("D%d=%08X   A%d=%08X", i, cpu.reg(i), i, cpu.reg(i + 8));
    ImGui::End();
}
#endif // NEOST_WITH_IMGUI
} // namespace

int main(int argc, char** argv) {
    const char* tosPath = (argc > 1) ? argv[1] : "tos.img";

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;

    // Pas de hint de profil → contexte legacy compatible (GL 2.1) : immediate
    // mode disponible sur Linux comme sur macOS Silicon, sans loader.
    GLFWwindow* window = glfwCreateWindow(
        Shifter::WIDTH * WIN_SCALE, Shifter::HEIGHT * WIN_SCALE,
        "NeoST — Atari ST", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);                    // VSync : cadence la boucle (~50/60 Hz)

    // --- Assemblage de la "carte mère" --------------------------------------
    Bus bus(512u * 1024u);                  // 512 Ko de RAM ST par défaut
    if (!bus.loadTos(tosPath))
        std::fprintf(stderr, "[main] Démarrage sans TOS (le CPU tournera à vide).\n");

    Shifter shifter(bus);                   // crée sa texture GL (contexte courant requis)
    YM2149  psg;
    Glue    glue;
    bus.shifter = &shifter;
    bus.psg     = &psg;
    bus.glue    = &glue;

    Cpu68k cpu(bus);
    cpu.reset();

#if defined(NEOST_WITH_IMGUI)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();
#endif

    bool prevReset = false;                 // détection de front pour F12
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Entrées : reset physique virtuel (F12), quitter (Échap) --------
        const bool resetNow = glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;
        if (resetNow && !prevReset) cpu.reset();   // front montant = bouton RESET
        prevReset = resetNow;
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, 1);
        // TODO : mapper le clavier vers l'IKBD (clavier ST) via l'ACIA.

        // --- Une trame, ligne par ligne (couplage CPU ↔ Shifter) ------------
        for (int line = 0; line < LINES_PER_FRAME; ++line) {
            cpu.run(CYCLES_PER_LINE);          // on exécute les cycles de la ligne
            if (line < VISIBLE_LINES) {
                shifter.renderScanline(line);  // puis le Shifter "rattrape" cette ligne
                cpu.setIrq(IRQ_HBL);           // interruption horizontale (HBL)
            }
            if (line == VISIBLE_LINES)
                cpu.setIrq(IRQ_VBL);           // début du VBlank → interruption trame
        }

        // --- Rendu : framebuffer ST plein écran puis superposition debug ----
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        shifter.present();                     // quad texturé plein écran

#if defined(NEOST_WITH_IMGUI)
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawHexViewer(bus);
        drawCpuState(cpu);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
#endif
        glfwSwapBuffers(window);
    }

#if defined(NEOST_WITH_IMGUI)
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
