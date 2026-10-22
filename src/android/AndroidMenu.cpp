// =============================================================================
//  AndroidMenu.cpp — cf. AndroidMenu.hpp.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "android/AndroidMenu.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "imgui.h"
#include "io/MediaScan.hpp"

namespace fs = std::filesystem;

namespace neost {

// Même table que le menu borne (main.cpp, KIOSK_KEYS) : F1-F8, chiffres, puis
// les touches dont les jeux ST ont réellement besoin + les deux clics souris.
const MenuKey kKeys[] = {
    {"F1", 0x3B, 0}, {"F2", 0x3C, 0}, {"F3", 0x3D, 0}, {"F4", 0x3E, 0},
    {"F5", 0x3F, 0}, {"F6", 0x40, 0}, {"F7", 0x41, 0}, {"F8", 0x42, 0},
    {"1", 0x02, 0}, {"2", 0x03, 0}, {"3", 0x04, 0}, {"4", 0x05, 0}, {"5", 0x06, 0},
    {"6", 0x07, 0}, {"7", 0x08, 0}, {"8", 0x09, 0}, {"9", 0x0A, 0}, {"0", 0x0B, 0},
    {"SPACE", 0x39, 0}, {"RETURN", 0x1C, 0}, {"ESC", 0x01, 0},
    {"T", 0x14, 0}, {"Y", 0x15, 0}, {"N", 0x31, 0},
    {"CLICK L", 0, 1}, {"CLICK R", 0, 2},
};
const int  kKeyCount   = int(sizeof(kKeys) / sizeof(kKeys[0]));
const int  kKeyRows[][2] = { {0, 8}, {8, 18}, {18, 26} };
const int  kKeyRowCount  = 3;

namespace {

const ImVec4 kGreen (0.30f, 1.00f, 0.40f, 1.0f);
const ImVec4 kYellow(1.00f, 0.85f, 0.25f, 1.0f);
const ImVec4 kOrange(1.00f, 0.65f, 0.25f, 1.0f);
const ImVec4 kDim   (0.55f, 0.55f, 0.58f, 1.0f);

std::string lowerOf(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// Bouton d'action : sa couleur porte le sens (rouge = on quitte, orange = on
// relance), comme les rangées du menu borne.
bool actionRow(const char* label, const ImVec4& col, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    const bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return hit;
}

}  // namespace

void AndroidMenu::refresh() {
    disks = scanDiskImages({dataDir}, mounted);
    diskSel = 0;
    for (int i = 0; i < int(disks.size()); ++i)
        if (disks[i] == mounted) { diskSel = i; break; }
}

void AndroidMenu::draw(float uiScale) {
    ImGuiIO& io = ImGui::GetIO();

    // --- Menu FERMÉ : un seul bouton, petit, en haut à gauche ----------------
    // Discret mais toujours atteignable : sur un téléphone il n'y a ni touche F9
    // ni bouton START garanti, et un menu qu'on ne sait pas ouvrir n'existe pas.
    if (!open && !keysPage) {
        ImGui::SetNextWindowPos(ImVec2(6.0f * uiScale, 6.0f * uiScale));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("##openbtn", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing);
        const float mh = ImGui::GetTextLineHeight() * 1.7f;
        if (ImGui::Button("MENU", ImVec2(mh * 2.6f, mh))) { open = true; reqRescan = true; }
        ImGui::End();
        return;
    }

    // --- PAGE CLAVIER : bandeau BAS, jeu NON mis en pause --------------------
    // Modèle borne : la frappe part au programme qui tourne dessous, on ne fige
    // donc rien — c'est ce qui permet de répondre à un « PRESS SPACE ».
    if (keysPage) {
        const ImVec2 sz(io.DisplaySize.x * 0.98f, io.DisplaySize.y * 0.42f);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.99f),
                                ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.80f);
        ImGui::Begin("##keys", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(kYellow, "KEYBOARD & MOUSE");
        ImGui::SameLine();
        if (ImGui::Button("CLOSE", ImVec2(ImGui::GetTextLineHeight() * 4.0f, 0))) keysPage = false;
        ImGui::Separator();

        const float kh = ImGui::GetTextLineHeight() * 1.9f;
        for (int r = 0; r < kKeyRowCount; ++r) {
            const int lo = kKeyRows[r][0], hi = kKeyRows[r][1];
            const float avail = ImGui::GetContentRegionAvail().x;
            const float kw = (avail - ImGui::GetStyle().ItemSpacing.x * (hi - lo - 1)) / float(hi - lo);
            for (int i = lo; i < hi; ++i) {
                if (i > lo) ImGui::SameLine();
                ImGui::PushID(i);
                if (ImGui::Button(kKeys[i].label, ImVec2(kw, kh))) {
                    if (kKeys[i].click) reqClick = kKeys[i].click;
                    else                reqKeyPress = i;
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
        return;
    }

    // --- MENU PRINCIPAL : voile + panneau centré -----------------------------
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##veil", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.94f, io.DisplaySize.y * 0.92f),
                             ImGuiCond_Always);
    ImGui::Begin("##menu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings);

    // ⚠ Les tailles se dérivent de la HAUTEUR DE LIGNE, jamais d'un nombre de
    // pixels multiplié par uiScale : la police est DÉJÀ mise à l'échelle
    // (FontGlobalScale), et multiplier une seconde fois donnait des rangées deux
    // fois trop hautes — deux jeux visibles, actions hors cadre. Vu du premier
    // coup d'œil dans neost-menu-preview, jamais sur l'appareil.
    const float line = ImGui::GetTextLineHeight();
    const float rowH = line * 1.9f;           // cible tactile : ~9 mm sur un téléphone

    ImGui::TextColored(kYellow, "MENU");
    ImGui::SameLine();
    ImGui::TextDisabled("  tap a game to insert it  ·  no reboot");
    {
        const float bw = line * 6.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - bw + ImGui::GetCursorPosX());
        if (ImGui::Button("RESUME", ImVec2(bw, rowH * 0.8f))) open = false;
    }
    ImGui::Separator();

    // Les actions sont sur UNE rangée horizontale, contre le bas. La borne les
    // empile verticalement — c'est bon sur un téléviseur piloté à la manette,
    // mais un téléphone tenu en paysage est large et BAS : empilées, elles ne
    // laissaient que deux jeux visibles.
    const float footer = rowH + ImGui::GetTextLineHeightWithSpacing()
                       + ImGui::GetStyle().ItemSpacing.y * 2.0f;

    ImGui::TextColored(kGreen, "GAMES");
    ImGui::BeginChild("##list", ImVec2(0, ImGui::GetContentRegionAvail().y - footer), true);
    if (disks.empty()) {
        ImGui::TextDisabled("(no image in %s)", dataDir.c_str());
        ImGui::TextDisabled("Import .st/.msa/.dim/.stx/.ipf files there.");
    }
    const std::string mrefL = lowerOf(fs::path(mounted).filename().string());
    for (int i = 0; i < int(disks.size()); ++i) {
        const std::string fn = fs::path(disks[i]).filename().string();
        const bool cur     = (disks[i] == mounted);
        const bool sel     = (i == diskSel);
        // Les SUITES du jeu monté (face B, disk 2…) sont teintées : c'est ce
        // qu'on cherche neuf fois sur dix en ouvrant ce menu.
        const bool sibling = !mrefL.empty() && !cur && areSiblingImages(lowerOf(fn), mrefL);
        if (cur)          ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
        else if (sibling) ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
        ImGui::PushID(i);
        char label[512];
        std::snprintf(label, sizeof label, "%s%s", cur ? "> " : "  ", fn.c_str());
        if (ImGui::Selectable(label, sel, 0, ImVec2(0, rowH))) {
            diskSel  = i;
            reqMount = disks[i];      // INSÉRER à chaud, sans redémarrer
            open     = false;
        }
        ImGui::PopID();
        if (cur || sibling) ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    // Quatre actions côte à côte, largeur égale (cf. la réserve `footer`).
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float bw = (ImGui::GetContentRegionAvail().x - 3.0f * sp) / 4.0f;
        if (actionRow("RESTART", kOrange, ImVec2(bw, rowH))) { reqRestart = true; open = false; }
        ImGui::SameLine();
        if (actionRow("KEYBOARD", ImVec4(0.55f, 0.80f, 1.00f, 1.0f), ImVec2(bw, rowH))) {
            keysPage = true; open = false;
        }
        ImGui::SameLine();
        if (actionRow("RESCAN", ImVec4(0.60f, 0.95f, 0.60f, 1.0f), ImVec2(bw, rowH))) reqRescan = true;
        ImGui::SameLine();
        if (actionRow("QUIT", ImVec4(1.00f, 0.50f, 0.40f, 1.0f), ImVec2(bw, rowH)))   reqQuit = true;
    }

    // Les dumps ont des noms à rallonge (« Blood Money (1989)(Psygnosis)[cr
    // Replicants][t].st ») : on tronque, sinon le pied de page sort du cadre.
    std::string inA = mounted.empty() ? std::string("(none)")
                                      : fs::path(mounted).filename().string();
    if (inA.size() > 38) {
        // Couper au BORD d'un point de code : les noms de dumps sont pleins
        // d'accents, et trancher un octet de continuation UTF-8 (10xxxxxx)
        // laisse une séquence invalide en fin de chaîne.
        std::size_t cut = 37;
        while (cut > 0 && (static_cast<unsigned char>(inA[cut]) & 0xC0) == 0x80) --cut;
        inA = inA.substr(0, cut) + "\u2026";
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::Text("Games found: %d   ·   in A: %s", int(disks.size()), inA.c_str());
    ImGui::PopStyleColor();
    ImGui::End();
}

}  // namespace neost
