// [interne] Aperçu de mise en page, aucune affirmation sur le matériel.
// =============================================================================
//  android_menu_preview.cpp — Aperçu BUREAU du menu Android.
//
//  Le menu Android (src/android/AndroidMenu.cpp) ne dépend que de Dear ImGui et
//  de io/MediaScan : ni SDL, ni Android, ni émulateur. On peut donc le dessiner
//  dans une fenêtre de bureau — et c'est le SEUL moyen d'itérer sur sa mise en
//  page sans téléphone sous la main (tailles de rangées, réserve du pied de
//  page, débordements, lisibilité à l'échelle d'un écran dense).
//
//  Ce n'est PAS l'application : rien n'est émulé, les requêtes du menu sont
//  seulement affichées sur la sortie standard.
//
//  Construire et lancer :
//      cmake --build build --target neost-menu-preview
//      ./build/neost-menu-preview [dossier_de_disquettes] [échelle]
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include <cstdio>
#include <string>

#include <GLFW/glfw3.h>

#include "android/AndroidMenu.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

int main(int argc, char** argv) {
    const std::string dir   = (argc > 1) ? argv[1] : "disks/st";
    const float       scale = (argc > 2) ? float(std::atof(argv[2])) : 2.0f;

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    // Format d'un téléphone tenu à l'horizontale (~19,5:9), à l'échelle.
    GLFWwindow* w = glfwCreateWindow(1300, 600, "NeoST — apercu du menu Android", nullptr, nullptr);
    if (!w) { std::fprintf(stderr, "window failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(w);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
    ImGui_ImplGlfw_InitForOpenGL(w, true);
    ImGui_ImplOpenGL2_Init();

    neost::AndroidMenu menu;
    menu.dataDir = dir;
    menu.refresh();
    if (!menu.disks.empty()) menu.mounted = menu.disks[0];   // simule un jeu inséré
    menu.refresh();
    menu.open = true;

    while (!glfwWindowShouldClose(w)) {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        menu.draw(scale);

        // Les requêtes ne sont pas exécutées : on les TRACE, ce qui suffit à
        // vérifier que les rangées renvoient bien ce qu'on croit.
        if (!menu.reqMount.empty()) { std::printf("[preview] INSERT %s\n", menu.reqMount.c_str()); menu.mounted = menu.reqMount; menu.reqMount.clear(); menu.refresh(); menu.open = true; }
        if (menu.reqRestart)  { std::printf("[preview] RESTART\n");  menu.reqRestart = false; menu.open = true; }
        if (menu.reqRescan)   { menu.refresh(); menu.reqRescan = false; }
        if (menu.reqQuit)     { std::printf("[preview] QUIT\n");     menu.reqQuit = false; }
        if (menu.reqKeyPress >= 0) { std::printf("[preview] KEY %s\n", neost::kKeys[menu.reqKeyPress].label); menu.reqKeyPress = -1; }
        if (menu.reqClick)    { std::printf("[preview] CLICK %d\n", menu.reqClick); menu.reqClick = 0; }

        ImGui::Render();
        int fw, fh; glfwGetFramebufferSize(w, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);          // à la place de l'écran ST
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(w);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(w);
    glfwTerminate();
    return 0;
}
