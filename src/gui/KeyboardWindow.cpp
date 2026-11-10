// =============================================================================
//  KeyboardWindow.cpp — cf. KeyboardWindow.hpp.
//
//  La table des touches est exprimée en coordonnées NORMALISÉES de la photo
//  (x/largeur, y/hauteur), relevées sur pic/Black_Keyboard_AtariST.jpeg : elle
//  suit donc n'importe quelle taille de fenêtre. Scancodes = clavier ST US
//  (ikbd.c de Hatari / doc IKBD) ; les touches mal libellées de la photo sont
//  mappées à leur POSITION sur un vrai ST (grande touche à gauche de Z = Shift
//  gauche, « Splift » = Shift droit, « Ne » = Insert, « Rel » = Clr/Home, « PgDn »
//  = Delete, touche vierge sous Tab = `~, pavé « Num Lock / × ÷ » = ( ) / *).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/KeyboardWindow.hpp"

#include <GLFW/glfw3.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <cstdio>
#include <vector>

#include "imgui.h"
#include "core/Machine.hpp"
#include "stb_image.h"

namespace {
struct Key { float x0, y0, x1, y1; uint8_t sc; const char* name; bool sticky; };

// Repères de la photo (2000 × 655 une fois réduite ; la table est normalisée).
constexpr float W = 2000.f, H = 655.f;
#define K(x0, y0, x1, y1, sc, name)  { (x0) / W, (y0) / H, (x1) / W, (y1) / H, sc, name, false }
#define KS(x0, y0, x1, y1, sc, name) { (x0) / W, (y0) / H, (x1) / W, (y1) / H, sc, name, true }
const Key kKeys[] = {
    // Rangée des fonctions (libellés de la photo : F1-F6, F9, F10 — pris tels quels)
    K(  62, 45, 185, 140, 0x3B, "F1"), K( 185, 45, 308, 140, 0x3C, "F2"), K( 308, 45, 431, 140, 0x3D, "F3"),
    K( 431, 45, 554, 140, 0x3E, "F4"), K( 554, 45, 677, 140, 0x3F, "F5"), K( 677, 45, 800, 140, 0x40, "F6"),
    K( 800, 45, 923, 140, 0x43, "F9"), K( 923, 45,1046, 140, 0x44, "F10"),
    // Rangée 1
    K(  45,200, 108,275, 0x62, "Help"), K( 118,200, 180,275, 0x61, "Undo"),
    K( 200,200, 262,275, 0x02, "1"), K( 283,200, 345,275, 0x03, "2"), K( 366,200, 428,275, 0x04, "3"),
    K( 449,200, 511,275, 0x05, "4"), K( 532,200, 594,275, 0x06, "5"), K( 615,200, 677,275, 0x07, "6"),
    K( 698,200, 760,275, 0x08, "7"), K( 781,200, 843,275, 0x09, "8"), K( 864,200, 926,275, 0x0A, "9"),
    K( 947,200,1009,275, 0x0B, "0"), K(1030,200,1092,275, 0x0C, "-"), K(1113,200,1175,275, 0x0D, "="),
    K(1196,200,1310,275, 0x0E, "Backspace"),
    K(1360,200,1505,275, 0x01, "Esc"), K(1525,200,1590,275, 0x52, "Insert"),
    K(1635,200,1700,275, 0x63, "( (keypad)"), K(1718,200,1783,275, 0x64, ") (keypad)"),
    K(1801,200,1866,275, 0x65, "/ (keypad)"), K(1884,200,1950,275, 0x66, "* (keypad)"),
    // Rangée 2
    K(  45,285, 225,355, 0x0F, "Tab"),
    K( 243,285, 305,355, 0x10, "Q"), K( 326,285, 388,355, 0x11, "W"), K( 409,285, 471,355, 0x12, "E"),
    K( 492,285, 554,355, 0x13, "R"), K( 575,285, 637,355, 0x14, "T"), K( 658,285, 720,355, 0x15, "Y"),
    K( 741,285, 803,355, 0x16, "U"), K( 824,285, 886,355, 0x17, "I"), K( 907,285, 969,355, 0x18, "O"),
    K( 990,285,1052,355, 0x19, "P"), K(1073,285,1135,355, 0x1A, "["), K(1156,285,1218,355, 0x1B, "]"),
    K(1232,285,1310,440, 0x1C, "Return"),
    K(1360,285,1423,355, 0x47, "Clr/Home"), K(1442,285,1505,355, 0x48, "Up"), K(1525,285,1590,355, 0x53, "Delete"),
    K(1635,285,1700,355, 0x67, "7 (keypad)"), K(1718,285,1783,355, 0x68, "8 (keypad)"),
    K(1801,285,1866,355, 0x69, "9 (keypad)"), K(1884,285,1950,355, 0x4A, "- (keypad)"),
    // Rangée 3
    K(  45,368, 190,440, 0x3A, "CapsLock"), K( 200,368, 240,440, 0x29, "`~"),   // CapsLock : bascule au make côté TOS → touche momentanée
    K( 257,368, 319,440, 0x1E, "A"), K( 340,368, 402,440, 0x1F, "S"), K( 423,368, 485,440, 0x20, "D"),
    K( 506,368, 568,440, 0x21, "F"), K( 589,368, 651,440, 0x22, "G"), K( 672,368, 734,440, 0x23, "H"),
    K( 755,368, 817,440, 0x24, "J"), K( 838,368, 900,440, 0x25, "K"), K( 921,368, 983,440, 0x26, "L"),
    K(1004,368,1066,440, 0x27, ";"), K(1087,368,1149,440, 0x28, "'"),
    K(1360,368,1423,440, 0x4B, "Left"), K(1442,368,1505,440, 0x50, "Down"), K(1525,368,1590,440, 0x4D, "Right"),
    K(1635,368,1700,440, 0x6A, "4 (keypad)"), K(1718,368,1783,440, 0x6B, "5 (keypad)"),
    K(1801,368,1866,440, 0x6C, "6 (keypad)"), K(1884,368,1950,440, 0x4E, "+ (keypad)"),
    // Rangée 4
    KS(  45,452, 285,525, 0x2A, "Shift (left)"),
    K( 295,452, 357,525, 0x2C, "Z"), K( 378,452, 440,525, 0x2D, "X"), K( 461,452, 523,525, 0x2E, "C"),
    K( 544,452, 606,525, 0x2F, "V"), K( 627,452, 689,525, 0x30, "B"), K( 710,452, 772,525, 0x31, "N"),
    K( 793,452, 855,525, 0x32, "M"), K( 876,452, 938,525, 0x33, ","), K( 959,452,1021,525, 0x34, "."),
    K(1042,452,1104,525, 0x35, "/"), KS(1127,452,1245,525, 0x36, "Shift (right)"),
    K(1635,452,1700,525, 0x6D, "1 (keypad)"), K(1718,452,1783,525, 0x6E, "2 (keypad)"),
    K(1801,452,1866,525, 0x6F, "3 (keypad)"), K(1884,452,1950,610, 0x72, "Enter (keypad)"),
    // Rangée 5
    KS( 200,538, 300,610, 0x1D, "Control"), KS( 322,538, 423,610, 0x38, "Alternate"),
    K( 445,538,1070,610, 0x39, "Space"), K(1085,538,1190,610, 0x39, "Space"),
    K(1635,538,1783,610, 0x70, "0 (keypad)"), K(1801,538,1866,610, 0x71, ". (keypad)"),
};
#undef K
#undef KS

GLuint g_tex = 0; int g_texW = 0, g_texH = 0; bool g_texTried = false;
bool g_sticky[256] = {false};      // modificateurs armés (par scancode)
int  g_heldKey = -1;               // index de la touche enfoncée à la souris

void loadTexture(const std::string& path) {
    g_texTried = true;
    int w, h, n;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) { std::fprintf(stderr, "[keyboard] cannot load %s\n", path.c_str()); return; }
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    g_texW = w; g_texH = h;
}

void releaseSticky(Machine& machine) {
    for (const Key& k : kKeys)
        if (k.sticky && g_sticky[k.sc]) { machine.keyEvent(k.sc, false); g_sticky[k.sc] = false; }
}
} // namespace

void keyboardWindowReleaseAll(Machine& machine) {
    if (g_heldKey >= 0) { machine.keyEvent(kKeys[g_heldKey].sc, false); g_heldKey = -1; }
    releaseSticky(machine);
}

void drawKeyboardWindow(bool* open, Machine& machine, const std::string& imagePath) {
    if (!g_texTried) loadTexture(imagePath);
    ImGui::SetNextWindowSize(ImVec2(900, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard", open) || !*open) {   // repliée ou fermée : rien ne reste enfoncé
        keyboardWindowReleaseAll(machine);
        ImGui::End();
        return;
    }
    if (!g_tex) {
        ImGui::TextWrapped("Keyboard photo not found: %s", imagePath.c_str());
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Click a key (held while the mouse button is). Shift/Control/Alternate are "
                        "sticky: click to arm, released after the next clicked key (or a second click).");
    // Image à la largeur disponible, ratio conservé.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale = avail.x / float(g_texW);
    const ImVec2 size(avail.x, float(g_texH) * scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)g_tex, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < int(sizeof(kKeys) / sizeof(kKeys[0])); ++i) {
        const Key& k = kKeys[i];
        const ImVec2 p0(origin.x + k.x0 * size.x, origin.y + k.y0 * size.y);
        const ImVec2 p1(origin.x + k.x1 * size.x, origin.y + k.y1 * size.y);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(i);
        ImGui::InvisibleButton("k", ImVec2(p1.x - p0.x, p1.y - p0.y));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated()) {
            if (k.sticky) {
                g_sticky[k.sc] = !g_sticky[k.sc];
                machine.keyEvent(k.sc, g_sticky[k.sc]);
            } else {
                machine.keyEvent(k.sc, true);
                g_heldKey = i;
            }
        }
        if (ImGui::IsItemDeactivated() && !k.sticky && g_heldKey == i) {
            machine.keyEvent(k.sc, false);
            g_heldKey = -1;
            releaseSticky(machine);                 // les modificateurs retombent après la touche
        }
        ImGui::PopID();
        const bool lit = (k.sticky && g_sticky[k.sc]) || g_heldKey == i;
        if (lit)     dl->AddRectFilled(p0, p1, IM_COL32(80, 200, 255, 90), 4.f);
        if (hovered) dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 200), 4.f, 0, 2.f);
        if (hovered) ImGui::SetTooltip("%s  ($%02X)", k.name, k.sc);
    }
    ImGui::End();
}
