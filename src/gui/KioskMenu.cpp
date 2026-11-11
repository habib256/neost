// =============================================================================
//  KioskMenu.cpp — cf. KioskMenu.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/KioskMenu.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "imgui.h"

#include "gui/App.hpp"
#include "gui/JoyMap.hpp"
#include "gui/StKeys.hpp"
#include "gui/UiCommon.hpp"
#include "io/JoystickInput.hpp"
#include "io/MediaScan.hpp"

namespace fs = std::filesystem;

// Longueur du préfixe commun (insensible à la casse) entre deux noms de fichier.
// Sert à mesurer la « proximité » : les disquettes d'un même jeu (« Jeu (Disk A) »,
// « Jeu (Disk B) »…) partagent un long préfixe et ne diffèrent qu'au marqueur B/C/D.

// Recense les images montables sous disks/ (+ dossiers ROM additionnels) →
// A.kioskDisks, TRIÉES par proximité au disque courant. Le MODÈLE (scan borné,
// détection des suites, ordre de tri) vit dans io/MediaScan.cpp : il est partagé
// avec le frontend Android, dont le menu reprend cette ludothèque.
void kioskScanDisks(App& A, const std::string& disksDir, const std::string& mounted) {
    std::vector<std::string> dirs{ disksDir };
    for (const auto& d : A.kioskRomDirs) dirs.push_back(d);
    A.kioskDisks = neost::scanDiskImages(dirs, mounted);
}

// Recense les SOUS-DOSSIERS immédiats de `dir` (triés, insensible à la casse) →
// A.browseSubdirs, et remet la sélection en tête. Pour le navigateur « SELECT ROM
// FOLDER » piloté à la manette.
void kioskScanBrowse(App& A, const std::string& dir) {
    A.browseSubdirs.clear();
    A.browseSel = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    // Itération MANUELLE : l'incrément d'un range-for lève filesystem_error (clé USB
    // retirée ou EIO pendant le parcours) et rien ne l'attrapait — la borne tombait.
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) A.browseSubdirs.push_back(it->path().string());
    }
    std::sort(A.browseSubdirs.begin(), A.browseSubdirs.end(),
              [](const std::string& a, const std::string& b) {
                  auto low = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
                  return low(fs::path(a).filename().string()) < low(fs::path(b).filename().string());
              });
}

// Retire de A.kioskRomDirs les dossiers qui n'existent plus (mal détectés / débranchés).
// Renvoie true si la liste a changé → l'appelant re-sauve la config.
bool kioskPruneRomDirs(App& A) {
    bool changed = false;
    for (size_t i = 0; i < A.kioskRomDirs.size(); ) {
        std::error_code ec;
        if (!fs::is_directory(A.kioskRomDirs[i], ec)) {
            A.kioskRomDirs.erase(A.kioskRomDirs.begin() + (long)i);
            changed = true;
        } else ++i;
    }
    return changed;
}

// Calcule les raccourcis du navigateur : racine /, home, puis chaque VOLUME MONTÉ
// (par son nom) sous /run/media/$USER, /media/$USER, /media, /mnt. Chaque libellé
// embarque une icône FA. Appelé à l'ouverture du navigateur.
void kioskComputeShortcuts(App& A) {
    A.browseShortcutPaths.clear();
    A.browseShortcutLabels.clear();
    auto add = [&](const std::string& path, const std::string& label) {
        std::error_code ec;
        if (!fs::is_directory(path, ec)) return;
        if (std::find(A.browseShortcutPaths.begin(), A.browseShortcutPaths.end(), path)
                != A.browseShortcutPaths.end()) return;   // dédup
        A.browseShortcutPaths.push_back(path);
        A.browseShortcutLabels.push_back(label);
    };
    add("/", std::string(ICON_FA_SERVER) + " / (filesystem root)");
    // USERPROFILE en repli : Windows ne définit pas HOME, et sans lui le
    // raccourci « Home » du navigateur borne disparaissait purement et simplement.
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = std::getenv("USERPROFILE");
    if (home && *home)
        add(home, std::string(ICON_FA_FOLDER_OPEN) + " Home  (" + home + ")");
    // Emplacements de montage (portable : le garde is_directory ci-dessous fait que
    // chaque OS n'expose que ceux qui existent, pas besoin de #ifdef) :
    //   · macOS  : /Volumes (tous les volumes montés, par nom)
    //   · Linux  : /run/media/$USER (udisks2), /media/$USER (classique), /mnt (manuel)
    // On évite le /media NU (il listerait des noms d'utilisateurs) — « / » y donne accès.
    const char* user = std::getenv("USER");
    std::vector<std::string> roots;
    roots.push_back("/Volumes");                                  // macOS
    if (user) { roots.push_back(std::string("/run/media/") + user);
                roots.push_back(std::string("/media/") + user); }
    roots.push_back("/mnt");
    for (const auto& r : roots) {
        std::error_code ec;
        if (!fs::is_directory(r, ec)) continue;
        for (fs::directory_iterator it(r, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {     // itération manuelle, cf. kioskScanBrowse
            std::error_code e2;
            if (it->is_directory(e2))
                add(it->path().string(),
                    std::string(ICON_FA_HDD) + " " + it->path().filename().string());
        }
    }
}

// Table de la page « Clavier & souris » du menu kiosk. kind : 0 = touche ST
// (scancode), 1 = clic gauche souris, 2 = clic droit. Disposée en 3 rangées
// (F1-F8, chiffres 1-0, Espace + clics) — cf. KIOSK_KEY_ROWS pour la navigation.
const KioskKey KIOSK_KEYS[] = {
    {"F1",0x3B,0},{"F2",0x3C,0},{"F3",0x3D,0},{"F4",0x3E,0},
    {"F5",0x3F,0},{"F6",0x40,0},{"F7",0x41,0},{"F8",0x42,0},          // rangée 0 (8)
    {"1",0x02,0},{"2",0x03,0},{"3",0x04,0},{"4",0x05,0},{"5",0x06,0},
    {"6",0x07,0},{"7",0x08,0},{"8",0x09,0},{"9",0x0A,0},{"0",0x0B,0}, // rangée 1 (10)
    {"SPACE",0x39,0},{"RETURN",0x1C,0},{"ESCAPE",0x01,0},
    {"T",0x14,0},{"Y",0x15,0},{"N",0x31,0},
    {"CLICK L",0,1},{"CLICK R",0,2},                                  // rangée 2 (8)
};
[[maybe_unused]] static const int KIOSK_KEY_COUNT = (int)(sizeof(KIOSK_KEYS) / sizeof(KIOSK_KEYS[0]));
// Bornes de rangées (indices de début) : [0,8) [8,18) [18,26).
const int KIOSK_KEY_ROWS[][2] = { {0, 8}, {8, 18}, {18, 26} };
const int KIOSK_KEY_ROWN = 3;

// Menu kiosk PLEIN ÉCRAN (rendu par-dessus l'écran ST). Pages selon A.kioskPage.
// La navigation (clavier/manette) est gérée dans la boucle ; ici on AFFICHE.
// `mounted` = chemin monté (marqué « ● »).
void drawKioskDiskMenu(App& A, const std::string& disksDir, const std::string& mounted) {
    const ImGuiIO& io = ImGui::GetIO();
    auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string mname = mounted.empty() ? std::string("(none)")
                                              : fs::path(mounted).filename().string();
    const ImVec4 kGreen (0.30f, 1.0f, 0.40f, 1.0f);
    const ImVec4 kYellow(1.0f, 0.9f, 0.3f, 1.0f);
    const ImVec4 kOrange(1.0f, 0.60f, 0.15f, 1.0f);
    const bool keysPage = (A.kioskPage == KIOSK_PAGE_KEYS);

    // Voile sombre plein écran — UNIQUEMENT pour les pages plein écran (liste,
    // quitter). La page Clavier est un petit bandeau translucide : on VOIT le jeu
    // tourner dessous (et donc qu'on a avancé) quand on lui envoie une touche.
    if (!keysPage) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##kioskveil", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::End();
    }

    // Géométrie selon la page : plein cadre centré pour la liste/quitter, petit
    // bandeau ancré EN BAS (translucide) pour le clavier.
    const ImVec2 fullSz(io.DisplaySize.x * 0.72f, io.DisplaySize.y * 0.82f);
    if (keysPage) {
        const ImVec2 ksz(io.DisplaySize.x * 0.60f, io.DisplaySize.y * 0.30f);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.98f),
                                ImGuiCond_Always, ImVec2(0.5f, 1.0f));   // ancré en bas
        ImGui::SetNextWindowSize(ksz, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
    } else {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(fullSz, ImGuiCond_Always);
    }
    ImGui::Begin("##kioskmenu", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoSavedSettings);

    // ================= PAGE 1 : DEUX menus (intérieur / extérieur) ============
    // Menu INTÉRIEUR = liste des jeux ; menu EXTÉRIEUR = Redémarrer / Clavier /
    // Quitter. On BASCULE de l'un à l'autre avec GAUCHE/DROITE ; haut/bas navigue
    // dans le menu focalisé ; le FEU valide son item surligné. Le menu qui a le focus
    // est vif (cursor vert ▶), l'autre est estompé.
    if (A.kioskPage == KIOSK_PAGE_LIST) {
        const int nd = (int)A.kioskDisks.size();
        const bool zList = (A.kioskZone == KIOSK_ZONE_LIST);
        const bool zAct  = (A.kioskZone == KIOSK_ZONE_ACTIONS);
        const ImVec4 kDim(0.5f, 0.5f, 0.5f, 1.0f);   // item sélectionné du menu NON focalisé
        ImGui::SetWindowFontScale(3.0f);
        ImGui::TextColored(kYellow, ICON_FA_GAMEPAD " MENU");
        ImGui::SameLine(); ImGui::SetWindowFontScale(1.5f);
        ImGui::TextDisabled("  \xe2\x97\x80\xe2\x96\xb6 switch menu   \xc2\xb7   up/down select"
                            "   \xc2\xb7   L1/R1 fast   \xc2\xb7   FIRE confirm   \xc2\xb7   (B) resume");
        ImGui::Separator();

        // --- Menu INTÉRIEUR : liste des jeux (valider = INSÉRER à chaud) -------
        const std::string mrefL = lower(mname);
        // Réserve pour le 2ᵉ menu (4 actions @2.3 + « Roms found » @1.3) calée sur son
        // CONTENU réel → aucun espace vide en bas : la liste des jeux prend tout le reste,
        // le 2ᵉ menu vient flush contre le bas de la fenêtre.
        ImGui::SetWindowFontScale(1.0f);
        const float sp = ImGui::GetStyle().ItemSpacing.y;
        const float bf = ImGui::GetFontSize();               // taille de police de base
        const float footer = 6.0f * (bf * 2.3f + sp)         // 6 rangées d'action @2.3
                           + (bf * 1.3f + sp)                // ligne « Roms found » @1.3
                           + (bf * 1.3f + sp)                // disquette de compilation reconnue @1.3
                           + (sp + 6.0f);                    // séparateur + petite marge
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(zList ? kYellow : kDim,
                           zList ? "\xe2\x96\xb6 " ICON_FA_COMPACT_DISC " GAMES"
                                 : "  " ICON_FA_COMPACT_DISC " GAMES");
        ImGui::BeginChild("##kdlist", ImVec2(0, ImGui::GetContentRegionAvail().y - footer), true);
        ImGui::SetWindowFontScale(2.7f);   // noms de fichiers TRÈS gros
        if (A.kioskDisks.empty())
            ImGui::TextDisabled("(no image in %s/)", disksDir.c_str());
        for (int i = 0; i < nd; ++i) {
            const bool sel = (i == A.kioskDiskSel);
            const bool cur = (A.kioskDisks[i] == mounted);
            const std::string fn = fs::path(A.kioskDisks[i]).filename().string();
            const bool sibling = !mrefL.empty() && !cur && neost::areSiblingImages(lower(fn), mrefL);
            // Cursor vert seulement si le menu JEUX a le focus ; sinon item courant estompé.
            if (sel)          ImGui::PushStyleColor(ImGuiCol_Text, zList ? kGreen : kDim);
            else if (sibling) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.6f, 1.0f));
            ImGui::Text("%s %s%s", (sel && zList) ? "\xe2\x96\xb6" : "   ", cur ? ICON_FA_COMPACT_DISC " " : "", fn.c_str());
            if (sel || sibling) ImGui::PopStyleColor();
            if (sel && zList) ImGui::SetScrollHereY(0.5f);
        }
        ImGui::EndChild();

        // --- Menu EXTÉRIEUR : boutons d'action --------------------------------
        ImGui::SetWindowFontScale(2.3f);
        auto actionRow = [&](int idx, const ImVec4& col, const char* label) {
            const bool sel = (A.kioskActSel == idx);
            // Vif + cursor vert si le menu ACTIONS a le focus et cet item est choisi.
            ImGui::PushStyleColor(ImGuiCol_Text, (sel && zAct) ? kGreen : (zAct ? col : kDim));
            ImGui::Text("%s %s", (sel && zAct) ? "\xe2\x96\xb6" : "  ", label);
            ImGui::PopStyleColor();
        };
        actionRow(0, kOrange,                          ICON_FA_REDO " RESTART MACHINE");
        actionRow(1, ImVec4(0.55f, 0.8f, 1.0f, 1.0f),  ICON_FA_KEYBOARD " KEYBOARD & MOUSE");
        actionRow(2, ImVec4(0.8f, 0.7f, 1.0f, 1.0f),   ICON_FA_GAMEPAD " JOYSTICKS");
        actionRow(3, ImVec4(0.6f, 0.95f, 0.6f, 1.0f),  ICON_FA_FOLDER_OPEN " ROM FOLDERS");
        actionRow(4, ImVec4(0.75f, 0.85f, 1.0f, 1.0f), ICON_FA_CLONE " DESKTOP MODE");
        actionRow(5, ImVec4(1.0f, 0.5f, 0.4f, 1.0f),   ICON_FA_SIGN_OUT_ALT " QUIT NEOST");
        ImGui::SetWindowFontScale(1.0f);

        // Bas de fenêtre : nombre de ROMs trouvées (les DOSSIERS se voient dans « ROM FOLDERS »).
        ImGui::Separator();
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextColored(kYellow, ICON_FA_COMPACT_DISC " Roms found: %d", (int)A.kioskDisks.size());
        // Disquette de compilation sous le curseur (data/compdisks) : équipe, numéro, jeux.
        const neost::CompDiskInfo* cd = (A.kioskDiskSel >= 0 && A.kioskDiskSel < (int)A.kioskDisks.size())
                                      ? A.compDisks.find(A.kioskDisks[A.kioskDiskSel]) : nullptr;
        if (cd) ImGui::TextColored(kDim, "%s: %s", cd->name.c_str(), cd->games.c_str());
        else    ImGui::TextUnformatted(" ");
        ImGui::SetWindowFontScale(1.0f);
    }

    // ================= PAGE 2 : Clavier & souris (petit bandeau) ==============
    // Jeu NON en pause : la touche/clic est envoyée au jeu qui tourne dessous.
    else if (A.kioskPage == KIOSK_PAGE_KEYS) {
        ImGui::SetWindowFontScale(1.5f);
        ImGui::TextColored(kYellow, ICON_FA_KEYBOARD " KEYBOARD & MOUSE");
        ImGui::SameLine();
        ImGui::TextDisabled(" (A) press  \xc2\xb7  (B) close");
        ImGui::Separator();

        for (int r = 0; r < KIOSK_KEY_ROWN; ++r) {
            ImGui::SetWindowFontScale(2.4f);
            for (int i = KIOSK_KEY_ROWS[r][0]; i < KIOSK_KEY_ROWS[r][1]; ++i) {
                const bool sel = (i == A.kioskKeySel);
                if (i > KIOSK_KEY_ROWS[r][0]) ImGui::SameLine();
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                char cell[24];
                std::snprintf(cell, sizeof cell, sel ? "[%s]" : " %s ", KIOSK_KEYS[i].label);
                ImGui::TextUnformatted(cell);
                if (sel) ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0, 4));
        }
        ImGui::SetWindowFontScale(1.0f);
    }

    // ================= PAGE 3 : confirmation de sortie =======================
    else if (A.kioskPage == KIOSK_PAGE_QUIT) {
        ImGui::SetWindowFontScale(3.1f);
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), ICON_FA_SIGN_OUT_ALT " QUIT NEOST?");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::SetWindowFontScale(2.4f);
        ImGui::TextDisabled("The kiosk will close.");
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::Separator();
        ImGui::SetWindowFontScale(2.0f);
        ImGui::TextColored(kGreen, ICON_FA_POWER_OFF " (A) Yes, quit");
        ImGui::SetWindowFontScale(1.8f);
        ImGui::TextDisabled("(B) No, back to menu");
        ImGui::SetWindowFontScale(1.0f);
    }

    // ============ PAGE 4 : navigateur « SELECT ROM FOLDER » (plein écran) ======
    // Liste : [0] valider CE dossier, [1] .. (parent), [2..] sous-dossiers. La
    // navigation/validation est gérée dans la boucle ; ici on AFFICHE.
    else if (A.kioskPage == KIOSK_PAGE_BROWSE) {
        ImGui::SetWindowFontScale(2.6f);
        ImGui::TextColored(kYellow, ICON_FA_FOLDER_OPEN " SELECT ROM FOLDER");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextDisabled("up/down move   \xc2\xb7   (A) enter / select   \xc2\xb7   (B) cancel");
        ImGui::Separator();
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", A.browseDir.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##kbrowse", ImVec2(0, 0), true);
        ImGui::SetWindowFontScale(2.2f);
        // Ordre des lignes : [0] valider · [1] .. · [2..2+S) raccourcis · [reste] sous-dossiers.
        const int nShort = (int)A.browseShortcutPaths.size();
        const int total  = 2 + nShort + (int)A.browseSubdirs.size();
        for (int i = 0; i < total; ++i) {
            const bool sel = (i == A.browseSel);
            const char* cur = sel ? "\xe2\x96\xb6" : "   ";
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            if (i == 0)
                ImGui::Text("%s " ICON_FA_STAR " [ USE THIS FOLDER ]", cur);
            else if (i == 1)
                ImGui::Text("%s " ICON_FA_FOLDER_OPEN " ..", cur);
            else if (i < 2 + nShort)
                ImGui::Text("%s %s", cur, A.browseShortcutLabels[i - 2].c_str());
            else
                ImGui::Text("%s " ICON_FA_FOLDER_OPEN " %s", cur,
                            fs::path(A.browseSubdirs[i - 2 - nShort]).filename().string().c_str());
            if (sel) { ImGui::PopStyleColor(); ImGui::SetScrollHereY(0.5f); }
        }
        ImGui::EndChild();
        ImGui::SetWindowFontScale(1.0f);
    }

    // ============ PAGE 5 : gestion des dossiers ROM (ajouter / retirer) ========
    // [0] « + ADD A FOLDER » (→ navigateur) ; [1..N] dossiers configurés, chacun avec
    // une croix rouge (FEU = retirer). Auto-prune des dossiers disparus fait à l'ouverture.
    else if (A.kioskPage == KIOSK_PAGE_JOY) {
        // Affectation des manettes hôte aux ports joystick ST. Une ligne par
        // manette PRÉSENTE : nom + rôle (AUTO avec le port effectif résolu, PORT 1,
        // PORT 0, OFF) + pastille verte si la manette émet (pour identifier
        // physiquement laquelle est laquelle : bouger le stick l'allume).
        ImGui::SetWindowFontScale(2.6f);
        ImGui::TextColored(kYellow, ICON_FA_GAMEPAD " JOYSTICKS");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextDisabled("up/down move   \xc2\xb7   (A) cycle port   \xc2\xb7   (B) back"
                            "   \xc2\xb7   move a stick to spot it \xe2\x97\x8f");
        ImGui::Separator();
        ImGui::BeginChild("##kjoy", ImVec2(0, 0), true);
        ImGui::SetWindowFontScale(2.2f);
        int8_t roles[GLFW_JOYSTICK_LAST + 1];
        joyResolveRoles(A, roles);
        int8_t assign[GLFW_JOYSTICK_LAST + 1];
        stjoy::resolveAssign(roles, assign, A.port0Auto);
        int row = 0;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (!glfwJoystickPresent(jid)) continue;
            const bool sel = (row == A.kioskJoySel);
            const char* nm = glfwGetJoystickName(jid);
            char role[32];
            if      (roles[jid] == stjoy::ROLE_PORT1) std::snprintf(role, sizeof role, "PORT 1");
            else if (roles[jid] == stjoy::ROLE_PORT0) std::snprintf(role, sizeof role, "PORT 0");
            else if (roles[jid] == stjoy::ROLE_OFF)   std::snprintf(role, sizeof role, "OFF");
            else if (assign[jid] >= 0) std::snprintf(role, sizeof role, "AUTO (PORT %d)", assign[jid]);
            else                       std::snprintf(role, sizeof role, "AUTO (unused)");
            const bool active = stjoy::readStick(jid, A.joyDeadzone) != 0;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
            ImGui::Text("%s %s \xe2\x80\x94 %s", sel ? "\xe2\x96\xb6" : "  ",
                        nm ? nm : "(unnamed)", role);
            if (sel) ImGui::PopStyleColor();
            if (active) {
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "\xe2\x97\x8f");
            }
            if (sel) ImGui::SetScrollHereY(0.5f);
            ++row;
        }
        if (row == 0)
            ImGui::TextDisabled("   (no joystick detected \xe2\x80\x94 plug one in, "
                                "the list is live)");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Separator();
        ImGui::TextDisabled("PORT 1 = games port. Buttons: A/B = fire, X = SPACE, Y = RETURN.");
        ImGui::EndChild();
        ImGui::SetWindowFontScale(1.0f);
    }

    else if (A.kioskPage == KIOSK_PAGE_ROMDIRS) {
        ImGui::SetWindowFontScale(2.6f);
        ImGui::TextColored(kYellow, ICON_FA_FOLDER_OPEN " ROM FOLDERS");
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextDisabled("up/down move   \xc2\xb7   (A) add / remove   \xc2\xb7   (B) back");
        ImGui::Separator();
        ImGui::BeginChild("##kromdirs", ImVec2(0, 0), true);
        ImGui::SetWindowFontScale(2.2f);
        const int total = 1 + (int)A.kioskRomDirs.size();
        for (int i = 0; i < total; ++i) {
            const bool sel = (i == A.romDirSel);
            const char* cur = sel ? "\xe2\x96\xb6" : "   ";
            if (i == 0) {
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                ImGui::Text("%s " ICON_FA_PLUS " [ ADD A FOLDER ]", cur);
                if (sel) ImGui::PopStyleColor();
            } else {
                ImGui::Text("%s", cur); ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), " " ICON_FA_TIMES " ");
                ImGui::SameLine(0.0f, 0.0f);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
                ImGui::Text("%s", A.kioskRomDirs[i - 1].c_str());
                if (sel) ImGui::PopStyleColor();
            }
            if (sel) ImGui::SetScrollHereY(0.5f);
        }
        if (A.kioskRomDirs.empty()) {
            ImGui::SetWindowFontScale(1.3f);
            ImGui::TextDisabled("   (no extra folder \xe2\x80\x94 only disks/ is scanned)");
        }
        ImGui::EndChild();
        ImGui::SetWindowFontScale(1.0f);
    }

    ImGui::End();
}
