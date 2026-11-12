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

#include "android/AndroidRoms.hpp"
#include "imgui.h"
#include "io/DongleTable.hpp"
#include "io/MediaScan.hpp"
#include "io/PortDevices.hpp"

namespace fs = std::filesystem;

namespace neost {

static_assert(sizeof(AndroidMenu::portDev) / sizeof(AndroidMenu::portDev[0])
                  == std::size_t(PortDevices::Port::Count),
              "AndroidMenu::portDev must have one slot per PortDevices::Port");

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
    // Les DEUX dossiers : l'interne porte la disquette embarquée, l'externe ce que
    // l'utilisateur a copié depuis un ordinateur.
    std::vector<std::string> dirs{dataDir};
    if (!importDir.empty()) dirs.push_back(importDir);
    disks = scanDiskImages(dirs, mounted);
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
        // Menu refermé par RETOUR, START ou RESUME depuis la page ROM : il se
        // rouvrira sur la page principale, pas sur la liste des ROM.
        romPage = false;
        donglePage = false; dongleSlot = -1;
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

    // --- PAGE ROM : même panneau, la liste des EmuTOS embarquées --------------
    // Grammaire de la ludothèque : rangées énormes et tapables, la ROM chargée en
    // vert. Choisir redémarre À FROID (une autre ROM, c'est un autre OS : rien de
    // la session ne lui survit) — le libellé le dit avant qu'on tape.
    if (romPage) {
        ImGui::TextColored(kYellow, "SYSTEM ROM");
        ImGui::SameLine();
        ImGui::TextDisabled("  tap to switch  ·  cold restart");
        {
            const float bw = line * 6.0f;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - bw + ImGui::GetCursorPosX());
            if (ImGui::Button("BACK", ImVec2(bw, rowH * 0.8f))) romPage = false;
        }
        ImGui::Separator();
        ImGui::BeginChild("##roms", ImVec2(0, 0), true);
        for (int i = 0; i < kAndroidRomCount; ++i) {
            const bool cur = (i == romCur);
            if (cur) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            ImGui::PushID(i);
            char label[128];
            std::snprintf(label, sizeof label, "%s%s", cur ? "> " : "  ", kAndroidRoms[i].label);
            if (ImGui::Selectable(label, cur, 0, ImVec2(0, rowH))) {
                // La ROM déjà chargée ne redémarre rien : on referme la page.
                if (!cur) { reqRom = i; open = false; }
                romPage = false;
            }
            ImGui::PopID();
            if (cur) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("192K = Atari ST, 256K = STE. US = 60 Hz, FR = 50 Hz and French keyboard.");
        ImGui::TextDisabled("The choice is kept for the next launch.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    // --- PAGE DONGLES : même panneau, deux niveaux ----------------------------
    // Vue d'ensemble = une rangée par emplacement (clé cartouche, puis les cinq
    // ports) avec ce qui y est branché ; taper une rangée ouvre la liste de ses
    // choix, grammaire de la page ROM. Brancher ne redémarre rien : le logiciel
    // sonde sa clé quand il le veut, comme sur le fer (la page Dongles du bureau
    // fait de même).
    if (donglePage) {
        using Port = PortDevices::Port;
        using Dev  = PortDevices::Device;
        static const char* const kPortNames[] = {
            "JOYSTICK 0 (MOUSE)", "JOYSTICK 1", "RS-232", "PRINTER", "CARTRIDGE BUTTON" };
        static_assert(sizeof(kPortNames) / sizeof(kPortNames[0]) == std::size_t(Port::Count),
                      "kPortNames must follow PortDevices::Port");
        ImGui::TextColored(kYellow, "DONGLES");
        ImGui::SameLine();
        if (dongleSlot < 0)
            ImGui::TextDisabled("  tap a slot to change it  ·  no reboot");
        else
            ImGui::TextDisabled("  %s  ·  tap to plug  ·  no reboot",
                                dongleSlot == 0 ? "CARTRIDGE KEY" : kPortNames[dongleSlot - 1]);
        {
            const float bw = line * 6.0f;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - bw + ImGui::GetCursorPosX());
            if (ImGui::Button("BACK", ImVec2(bw, rowH * 0.8f))) {
                if (dongleSlot >= 0) dongleSlot = -1; else donglePage = false;
            }
        }
        ImGui::Separator();
        ImGui::BeginChild("##dongles", ImVec2(0, 0), true);
        // Une rangée tapable « libellé ... valeur », la valeur en vert si branchée.
        auto slotRow = [&](int id, const char* name, const char* value, bool plugged) {
            ImGui::PushID(id);
            const ImVec2 p0 = ImGui::GetCursorPos();
            const bool hit = ImGui::Selectable("##slot", false, 0, ImVec2(0, rowH));
            ImGui::SetCursorPos(ImVec2(p0.x, p0.y + (rowH - line) * 0.5f));
            ImGui::TextUnformatted(name);
            ImGui::SameLine(p0.x + line * 14.0f);
            ImGui::TextColored(plugged ? kGreen : kDim, "%s", value);
            ImGui::SetCursorPos(ImVec2(p0.x, p0.y + rowH + ImGui::GetStyle().ItemSpacing.y));
            ImGui::PopID();
            return hit;
        };
        // Une rangée de choix, grammaire de la page ROM (« > » + vert = branché).
        auto choiceRow = [&](int id, const char* label, bool cur) {
            if (cur) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            ImGui::PushID(id);
            char lab[160];
            std::snprintf(lab, sizeof lab, "%s%s", cur ? "> " : "  ", label);
            const bool hit = ImGui::Selectable(lab, cur, 0, ImVec2(0, rowH));
            ImGui::PopID();
            if (cur) ImGui::PopStyleColor();
            return hit;
        };

        if (dongleSlot < 0) {
            const auto key = CartridgeKey::Model(cartKey);
            if (slotRow(0, "CARTRIDGE KEY", cartKeyLabel(key), key != CartridgeKey::Model::None))
                dongleSlot = 0;
            for (int p = 0; p < int(Port::Count); ++p) {
                const Dev d = Dev(portDev[p]);
                if (slotRow(1 + p, kPortNames[p], PortDevices::label(d), d != Dev::None))
                    dongleSlot = 1 + p;
            }
            // Bouton de cartouche branché : le presser referme le menu, la machine
            // reprend et voit l'appui (Multiface : GPIP7, Ripper : RI, jusqu'au VBL).
            const Dev btn = Dev(portDev[int(Port::CartButton)]);
            if (btn != Dev::None) {
                ImGui::Spacing();
                const bool mf = btn == Dev::Multiface;
                ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
                if (ImGui::Selectable(mf ? "  PRESS FREEZE BUTTON" : "  PRESS RIPPER BUTTON",
                                      false, 0, ImVec2(0, rowH))) {
                    reqPortButton = true;
                    open = false;
                }
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ImGui::TextDisabled("A game only probes the port it expects: plug the key there.");
            ImGui::TextDisabled("Pro Sound Designer is an 8-bit DAC, not a key. Kept for the next launch.");
        } else if (dongleSlot == 0) {
            for (int m = 0; m < kCartKeyModelCount; ++m)
                if (choiceRow(m, cartKeyLabel(CartridgeKey::Model(m)), m == cartKey)) {
                    if (m != cartKey) reqCartKey = m;
                    dongleSlot = -1;
                }
            ImGui::Spacing();
            ImGui::TextDisabled("On /ROM3 ($FB0000), invisible to TOS. Cubase Lite needs none.");
        } else {
            const int p = dongleSlot - 1;
            for (int di = 0; di < int(Dev::Count); ++di) {
                const Dev d = Dev(di);
                if (!PortDevices::fits(Port(p), d)) continue;
                const bool home = d == Dev::None || PortDevices::defaultPort(d) == Port(p);
                char lab[128];
                std::snprintf(lab, sizeof lab, "%s%s", PortDevices::label(d),
                              home ? "" : "  (wrong port for this game)");
                if (choiceRow(di, lab, di == portDev[p])) {
                    if (di != portDev[p]) { reqPlugPort = p; reqPlugDev = di; }
                    dongleSlot = -1;
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    ImGui::TextColored(kYellow, "MENU");
    ImGui::SameLine();
    ImGui::TextDisabled("  tap a game to insert it  ·  no reboot");
    {
        // Largeur au moins celle du libellé le plus long : à 6 hauteurs de ligne,
        // « REWIND: OFF » sortait de son bouton (« OF » visible) sur l'émulateur
        // Android 1080×2400 (capture du 2026-11-12) — la police à chasse fixe de
        // ce menu est plus large que la hauteur de ligne ne le laisse supposer.
        const float bw = std::max(line * 6.0f, ImGui::CalcTextSize("REWIND: OFF").x
                                               + 2.0f * ImGui::GetStyle().FramePadding.x);
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 2.0f * bw - sp + ImGui::GetCursorPosX());
        // Bascule du retour arrière : éteint par défaut, comme l'option du bureau.
        if (ImGui::Button(rewindOn ? "REWIND: ON" : "REWIND: OFF", ImVec2(bw, rowH * 0.8f)))
            rewindOn = !rewindOn;
        ImGui::SameLine();
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
    if (disks.empty()) ImGui::TextDisabled("(no floppy image found)");
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
    // Où déposer ses images : le dossier EXTERNE, le seul qu'un utilisateur atteigne
    // (USB/MTP depuis un ordinateur). Afficher le dossier interne invitait à copier
    // là où rien ne peut écrire sans root. Toujours affiché, en fin de liste.
    ImGui::Spacing();
    if (!importDir.empty()) {
        ImGui::TextDisabled("Add games: copy .st/.msa/.dim/.stx/.ipf files over USB to");
        ImGui::TextDisabled("  %s", importDir.c_str());
        ImGui::TextDisabled("then tap RESCAN.");
    } else {
        ImGui::TextDisabled("Shared storage unavailable: games cannot be imported right now.");
    }
    ImGui::EndChild();

    // Sept actions côte à côte, largeur égale (cf. la réserve `footer`).
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float bw = (ImGui::GetContentRegionAvail().x - 6.0f * sp) / 7.0f;
        if (actionRow("RESTART", kOrange, ImVec2(bw, rowH))) { reqRestart = true; open = false; }
        ImGui::SameLine();
        // Le menu se referme : le recul se voit aussitôt, la partie reprend de là.
        if (actionRow("REWIND", kYellow, ImVec2(bw, rowH))) { reqRewind = true; open = false; }
        ImGui::SameLine();
        if (actionRow("KEYBOARD", ImVec4(0.55f, 0.80f, 1.00f, 1.0f), ImVec2(bw, rowH))) {
            keysPage = true; open = false;
        }
        ImGui::SameLine();
        if (actionRow("RESCAN", ImVec4(0.60f, 0.95f, 0.60f, 1.0f), ImVec2(bw, rowH))) reqRescan = true;
        ImGui::SameLine();
        if (actionRow("ROM", ImVec4(0.85f, 0.70f, 1.00f, 1.0f), ImVec2(bw, rowH))) romPage = true;
        ImGui::SameLine();
        if (actionRow("DONGLES", ImVec4(0.95f, 0.85f, 0.55f, 1.0f), ImVec2(bw, rowH))) {
            donglePage = true; dongleSlot = -1;
        }
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
    ImGui::Text("Games found: %d   ·   in A: %s%s%s", int(disks.size()), inA.c_str(),
                status.empty() ? "" : "   ·   ", status.c_str());
    ImGui::PopStyleColor();
    ImGui::End();
}

}  // namespace neost
