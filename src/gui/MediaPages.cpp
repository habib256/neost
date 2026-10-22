// =============================================================================
//  MediaPages.cpp — cf. MediaPages.hpp.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/MediaPages.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "gui/UiCommon.hpp"
#include "io/MediaScan.hpp"

namespace fs = std::filesystem;

// Champs de saisie LIBRE des pages Disque dur (chemin tapé à la main). Ils vivaient
// dans main.cpp, présentés comme « globaux pour que le sous-menu Profils les
// resynchronise » — mais ils y étaient dans un namespace ANONYME, donc à liaison
// interne, et plus personne ne les touchait ailleurs. Ils suivent donc leurs pages.
char g_gdBuf[512] = {0}, g_hdBuf[512] = {0};

void drawFloppyPage(const std::string& disksDir,
                    const std::string& mountedA, const std::string& mountedB,
                    std::string& reqMountA, std::string& reqMountB,
                    bool& reqEjectA, bool& reqEjectB) {
    auto driveRow = [](const char* letter, const std::string& mounted, bool& reqEject) {
        if (!mounted.empty()) {
            ImGui::PushID(letter);
            if (IconButton(ICON_FA_EJECT, "Eject")) reqEject = true;
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("%s: %s", letter, fs::path(mounted).filename().string().c_str());
        } else {
            ImGui::Text("%s: ", letter); ImGui::SameLine();
            ImGui::TextDisabled("(empty)");
        }
    };
    driveRow("A", mountedA, reqEjectA);
    driveRow("B", mountedB, reqEjectB);
    ImGui::Separator();
    ImGui::TextDisabled("Images in %s/", disksDir.c_str());

    std::error_code ec;
    if (!fs::is_directory(disksDir, ec)) {
        ImGui::TextDisabled("(disks/ folder not found)");
        return;
    }
    const fs::path base(disksDir);
    // Récolte RÉCURSIVE des images .st/.msa/.dim/.stx/.ipf, triées par ordre alphabétique de
    // DOSSIER puis de FICHIER (insensible à la casse) sur le chemin relatif à disks/.
    //
    // ⚠ MISE EN CACHE OBLIGATOIRE. Ce scan tournait à CHAQUE trame, et sa clé de tri
    // appelait fs::relative() — un weakly_canonical(), donc un readlink par composant —
    // DEPUIS le comparateur de std::sort, soit O(n log n) fois. Mesuré : 21 ms par trame
    // (le budget PAL entier) sur les 77 images du dépôt, et l'émulateur tombait de 50 à
    // 0,9 trame/s sur une ludothèque de 3000 images, la fenêtre étant ouverte par défaut.
    // Le chemin relatif est ici purement lexical : lexically_relative() ne touche pas le
    // disque. On calcule la clé UNE fois par image (décorer-trier-dévorer).
    struct Entry { std::string path, rel, key; };
    static std::vector<Entry> cache;
    static std::string  cacheDir;
    static double       cacheTime = -1.0;
    const double now = ImGui::GetTime();
    bool refresh = (cacheDir != disksDir) || cacheTime < 0.0 || (now - cacheTime) > 2.0;
    if (ImGui::SmallButton("Refresh")) refresh = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu images)", cache.size());
    if (refresh) {
        cache.clear();
        cacheDir  = disksDir;
        cacheTime = now;
        // Itération manuelle : le range-for lancerait filesystem_error sur un
        // sous-dossier/symlink illisible.
        fs::recursive_directory_iterator dit(base, fs::directory_options::skip_permission_denied, ec), dend;
        while (!ec && dit != dend) {
            const fs::directory_entry& e = *dit;
            std::error_code ec2;
            if (e.is_regular_file(ec2)) {
                std::string ext = e.path().extension().string();
                for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".st" || ext == ".msa" || ext == ".dim" || ext == ".stx" || ext == ".ipf") {
                    Entry en;
                    en.path = e.path().string();
                    en.rel  = e.path().lexically_relative(base).generic_string();
                    en.key  = en.rel;
                    for (auto& ch : en.key) ch = (char)std::tolower((unsigned char)ch);
                    cache.push_back(std::move(en));
                }
            }
            dit.increment(ec);
        }
        ec.clear();
        std::sort(cache.begin(), cache.end(),
                  [](const Entry& a, const Entry& b) { return a.key < b.key; });
    }

    for (const auto& en : cache) {
        ImGui::PushID(en.path.c_str());
        // Chemin COMPLET et non nom de fichier : le scan est récursif, et deux
        // dumps homonymes dans deux sous-dossiers (« Xenon/DISK1.ST », « Gods/
        // DISK1.ST » — nommage très courant) étaient tous deux marqués « montée »,
        // aucun n'offrant plus le bouton Monter : le second devenait inaccessible.
        const bool inA = !mountedA.empty() && en.path == mountedA;
        const bool inB = !mountedB.empty() && en.path == mountedB;
        if (inA)      ImGui::TextDisabled("●A");
        else if (ImGui::SmallButton("A")) reqMountA = en.path;
        ImGui::SameLine(0.0f, 4.0f);
        if (inB)      ImGui::TextDisabled("●B");
        else if (ImGui::SmallButton("B")) reqMountB = en.path;
        ImGui::SameLine();
        ImGui::TextUnformatted(en.rel.c_str());        // affiché (montre le dossier)
        ImGui::PopID();
    }
}

// Page « Cartouche » : images de carts/ branchées sur le port $FA0000. Un reset
// reste nécessaire pour que le TOS relise le magic de boot.
void drawCartPage(const std::string& cartsDir, const std::string& mounted,
                  std::string& reqMount, bool& reqEject) {
    if (!mounted.empty()) {
        if (IconButton(ICON_FA_EJECT, "Eject")) reqEject = true;
        ImGui::SameLine();
        ImGui::Text("plugged: %s", fs::path(mounted).filename().string().c_str());
    } else {
        ImGui::TextDisabled("(cartridge port empty)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Images in %s/", cartsDir.c_str());

    std::error_code ec;
    if (fs::is_directory(cartsDir, ec)) {
        const std::string mountedName = mounted.empty() ? "" : fs::path(mounted).filename().string();
        // Itération MANUELLE, comme la page Disquettes : l'error_code passé au constructeur
        // ne couvre QUE la construction — l'incrément du range-for, lui, lève
        // filesystem_error si le dossier devient illisible en cours de parcours (carts/
        // sur une clé USB retirée), et personne ne l'attrape ici → std::terminate.
        fs::directory_iterator it(cartsDir, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end) {
            const fs::directory_entry& e = *it;
            // is_regular_file() SANS error_code LANCE sur un symlink dont la cible est devenue
            // illisible (clé USB débranchée) : rien ne l'attrape ici → std::terminate.
            std::error_code ec2;
            if (e.is_regular_file(ec2)) {
                std::string ext = e.path().extension().string();
                for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".bin" || ext == ".img" || ext == ".rom") {
                    const std::string name = e.path().filename().string();
                    ImGui::PushID(name.c_str());
                    if (name == mountedName) {
                        ImGui::TextDisabled("●");          // branchée
                    } else if (ImGui::SmallButton("Plug in")) {
                        reqMount = e.path().string();
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::PopID();
                }
            }
            it.increment(ec);
        }
    } else {
        ImGui::TextDisabled("(carts/ folder not found)");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Plugging in / ejecting restarts the machine to re-detect the cart.");
    ImGui::TextDisabled("Exclusive with the GEMDOS drive: both occupy $FA0000.");
}

// Page « Disques durs » : les deux chemins d'accès (HD GEMDOS = dossier hôte monté en
// C:, image ACSI = disque dur émulé sur la cible 0). Ils demandaient jusqu'ici de TAPER
// un chemin absolu dans un menu ; on liste ici les candidats trouvés sur disque — un
// clic monte, comme pour une disquette — le champ libre restant pour ce qui vit
// ailleurs (le glisser-déposer sur la fenêtre couvre le même besoin).
//
// Convention de rangement, volontairement simple et documentée dans hd/README.md :
//   · un DOSSIER dans hd/ (et le dossier gemdos/ du dépôt) = un lecteur GEMDOS ;
//   · un FICHIER image à plat dans hd/ = une image ACSI.
// Le scan des images ACSI n'est donc PAS récursif : sans ça, les .img rangés DANS un
// lecteur GEMDOS (hd/JEUX/DEMOS/truc.img) seraient proposés comme disques durs.
// Champ de saisie LIBRE d'un chemin, avec VERDICT IMMÉDIAT. Il y avait là deux façons
// de croire l'interface cassée : ce que l'on tape ne va au champ que si la souris n'est
// PAS capturée (main.cpp:854 — sinon les touches partent au clavier ST), et le bouton
// refuse SILENCIEUSEMENT un champ vide ; quant au message d'échec du montage, il
// s'affiche en bas de la fenêtre PRINCIPALE, invisible quand la Configuration est
// détachée dans sa propre fenêtre. On répond donc sur place : le verdict s'affiche sous
// le champ pendant la frappe, et Entrée vaut clic.
//
// Un seul stat() par trame sur une chaîne courte : sans commune mesure avec le scan de
// dossier que cette page met en cache (cf. la note de drawFloppyPage).
static bool pathEntryRow(const char* id, const char* hint, char* buf, std::size_t bufSz,
                         bool wantDir) {
    ImGui::SetNextItemWidth(-70.0f);
    const bool entered = ImGui::InputTextWithHint(id, hint, buf, bufSz,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    char btn[64];
    std::snprintf(btn, sizeof btn, "Mount##%s", id);
    const bool clicked = ImGui::Button(btn);

    // Messages COURTS : le panneau est étroit (nav de gauche + fenêtre parfois
    // détachée), et une phrase complète y était coupée en plein milieu. Le détail
    // part en infobulle, qui, elle, n'est pas contrainte par la largeur.
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", wantDir
            ? "Absolute path to a host folder, e.g. /home/you/atari-stuff\n"
              "Simple folders use C:, or the drive after ACSI partitions.\n"
              "Enter or Mount validates.\n"
              "Tip: a folder dropped in hd/ is listed above, no typing needed.\n"
              "Note: typing only reaches this field when the mouse is NOT captured\n"
              "(middle-click or Ctrl+Alt+G releases it)."
            : "Absolute path to a hard disk image (.img .hd .acsi .vhd .raw)\n"
              "Enter or Mount validates.\n"
              "Tip: an image dropped in hd/ is listed above, no typing needed.");

    if (buf[0]) {
        std::error_code ec;
        const bool ok = wantDir ? fs::is_directory(buf, ec) : fs::is_regular_file(buf, ec);
        if (ok) ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.f),
                                   wantDir ? "folder OK — Mount or Enter" : "file OK — Mount or Enter");
        else    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f),
                                   wantDir ? "no such folder" : "no such file");
        if (!ok) return false;                      // on ne propose pas un montage voué à l'échec
    } else {
        ImGui::TextDisabled("absolute path, or drop it in hd/");
        return false;                               // champ vide : le bouton ne fait rien, on le DIT
    }
    return entered || clicked;
}

std::string hardDiskDriveLetters(uint32_t mask) {
    std::string letters;
    for (int drive = 2; drive < 26; ++drive) {
        if (!(mask & (uint32_t(1) << drive))) continue;
        const int first = drive;
        while (drive + 1 < 26 && (mask & (uint32_t(1) << (drive + 1)))) ++drive;
        if (!letters.empty()) letters += ", ";
        letters += char('A' + first);
        letters += ':';
        if (drive != first) {
            letters += "–";
            letters += char('A' + drive);
            letters += ':';
        }
    }
    return letters;
}

void drawHardDiskPage(const std::string& hdDir, const std::string& gemdosDefault,
                      const std::string& curGemdos, bool gemdosActive, uint32_t gemdosDrives,
                      const std::string& curAcsi, bool acsiActive, int acsiParts,
                      bool usatanOn, const std::string& curSd2,
                      std::string& reqMountGemdos, bool& reqEjectGemdos,
                      std::string& reqMountAcsi,  bool& reqEjectAcsi,
                      int& reqUltraSatan, std::string& reqMountSd2, bool& reqEjectSd2) {
    // Scan mis en cache 2 s, comme la page Disquettes : la fenêtre peut rester ouverte
    // et un parcours de dossier par trame coûte le budget PAL entier sur une grosse
    // ludothèque (cf. la note de drawFloppyPage).
    struct Cand { std::string path, label; };
    static std::vector<Cand> gemCands, acsiCands;
    static std::string cacheDir;
    static double      cacheTime = -1.0;
    const double now = ImGui::GetTime();
    bool refresh = (cacheDir != hdDir) || cacheTime < 0.0 || (now - cacheTime) > 2.0;
    if (refresh) {
        gemCands.clear(); acsiCands.clear();
        cacheDir = hdDir; cacheTime = now;
        std::error_code ec;
        // Le gemdos/ du dépôt est un lecteur à part entière : le proposer en tête.
        if (fs::is_directory(gemdosDefault, ec))
            gemCands.push_back({ gemdosDefault, gemdosDefault + "/" });
        ec.clear();
        if (fs::is_directory(hdDir, ec)) {
            fs::directory_iterator it(hdDir, fs::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end) {
                const fs::directory_entry& e = *it;
                std::error_code ec2;
                if (e.is_directory(ec2)) {
                    gemCands.push_back({ e.path().string(),
                                         e.path().lexically_relative(fs::path(hdDir).parent_path())
                                             .generic_string() + "/" });
                } else if (e.is_regular_file(ec2)) {
                    std::string ext = e.path().extension().string();
                    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                    if (ext == ".img" || ext == ".hd" || ext == ".acsi" ||
                        ext == ".vhd" || ext == ".raw") {
                        acsiCands.push_back({ e.path().string(),
                                              e.path().filename().string() });
                    }
                }
                it.increment(ec);
            }
        }
        auto byLabel = [](const Cand& a, const Cand& b) { return a.label < b.label; };
        std::sort(gemCands.begin(),  gemCands.end(),  byLabel);
        std::sort(acsiCands.begin(), acsiCands.end(), byLabel);
    }

    // Le chemin monté vient de la config (« gemdos ») et le candidat du scan
    // (« build/../gemdos ») : la comparaison textuelle rate. fs::equivalent tranche
    // sur l'inode — le coût est négligeable, ces listes tiennent en quelques entrées.
    auto samePath = [](const std::string& a, const std::string& b) {
        if (a.empty() || b.empty()) return false;
        if (a == b) return true;
        std::error_code ec;
        return fs::equivalent(a, b, ec) && !ec;
    };

    // ── GEMDOS ────────────────────────────────────────────────────────────
    ImGui::TextDisabled(ICON_FA_FOLDER_OPEN " GEMDOS — host folder");
    if (gemdosActive) {
        // PushID OBLIGATOIRE : IconButton dérive son ID du seul texte de l'icône, et
        // cette page affiche jusqu'à TROIS ICON_FA_EJECT (GEMDOS, ACSI, slot 2). Deux
        // montés en même temps = deux widgets de même ID → ImGui ouvre son avertissement
        // « conflicting ID » et les clics partent sur le mauvais bouton. driveRow(), page
        // Disquettes, applique déjà cette discipline ; ici elle manquait.
        ImGui::PushID("ejectGemdos");
        if (IconButton(ICON_FA_EJECT, "Eject the GEMDOS drive")) reqEjectGemdos = true;
        ImGui::PopID();
        ImGui::SameLine();
        const std::string letters = hardDiskDriveLetters(gemdosDrives);
        if (!letters.empty()) ImGui::Text("%s %s", letters.c_str(), curGemdos.c_str());
        else {
            ImGui::TextUnformatted(curGemdos.c_str());
            ImGui::TextDisabled("(No drive letters available for this folder.)");
        }
        ImGui::TextDisabled("(occupies cartridge port $FA0000 — exclusive with a cartridge)");
    } else {
        ImGui::TextDisabled("(no GEMDOS drive mounted)");
    }
    for (const auto& c : gemCands) {
        ImGui::PushID(c.path.c_str());
        if (gemdosActive && samePath(c.path, curGemdos)) ImGui::TextDisabled("●");
        else if (ImGui::SmallButton("Mount")) reqMountGemdos = c.path;
        ImGui::SameLine();
        ImGui::TextUnformatted(c.label.c_str());
        ImGui::PopID();
    }
    if (pathEntryRow("##gdPath", "path to a host folder…", g_gdBuf, sizeof g_gdBuf, true))
        reqMountGemdos = g_gdBuf;

    ImGui::Separator();

    // ── ACSI ──────────────────────────────────────────────────────────────
    ImGui::TextDisabled(ICON_FA_HDD " ACSI — hard disk image (target 0)");
    // acsiActive vient de acsiActive() = anyEnabled(), qui balaie TOUTES les cibles :
    // l'UltraSatan seul, avec son slot 2 (ID 1) peuplé, suffit à le rendre vrai alors
    // que la cible 0 — la seule dont parle cette section — est vide. On s'appuie donc
    // sur curAcsi, qui EST le chemin de la cible 0. Sans lui, la ligne affichait
    // « mounted:  — 0 partition(s) » : un nom vide, et un décompte de partitions qui
    // sommait les autres cibles, donc appartenait au slot 2.
    if (acsiActive && !curAcsi.empty()) {
        ImGui::PushID("ejectAcsi");
        if (IconButton(ICON_FA_EJECT, "Eject the ACSI image")) reqEjectAcsi = true;
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Text("mounted: %s — %d partition(s)",
                    fs::path(curAcsi).filename().string().c_str(), acsiParts);
    } else if (acsiActive) {
        // Pas d'Eject ici : il n'y a pas de carte à retirer, et le bouton lançait
        // quand même un hard reset.
        ImGui::TextDisabled("(empty slot)");
    } else {
        ImGui::TextDisabled("(no ACSI image mounted)");
    }
    for (const auto& c : acsiCands) {
        ImGui::PushID(c.path.c_str());
        if (acsiActive && samePath(c.path, curAcsi)) ImGui::TextDisabled("●");
        else if (ImGui::SmallButton("Mount")) reqMountAcsi = c.path;
        ImGui::SameLine();
        ImGui::TextUnformatted(c.label.c_str());
        ImGui::PopID();
    }
    if (pathEntryRow("##hdPath", "path to a hard disk image…", g_hdBuf, sizeof g_hdBuf, false))
        reqMountAcsi = g_hdBuf;

    if (gemCands.empty() && acsiCands.empty())
        ImGui::TextDisabled("(nothing in %s/ — drop a folder or an image there)", hdDir.c_str());

    // UltraSatan (extension NeoST) : l'interface SD de Jookie sur les IDs 0-1. Le
    // slot 1 EST l'image ACSI ci-dessus (ID 0) ; le slot 2 (ID 1) se monte ici.
    // Sans carte, un slot répond quand même (INQUIRY « JOOKIE  UltraSatan »).
    ImGui::Separator();
    bool us = usatanOn;
    if (ImGui::Checkbox("UltraSatan (2 SD slots on ACSI IDs 0-1, JOOKIE INQUIRY, RTC, 'US' commands)", &us))
        reqUltraSatan = us ? 1 : 0;
    if (usatanOn) {
        ImGui::TextDisabled("slot 1 = the ACSI image above (ID 0) — slot 2 (ID 1):");
        if (!curSd2.empty()) {
            ImGui::PushID("ejectSd2");
            if (IconButton(ICON_FA_EJECT, "Eject the slot 2 card")) reqEjectSd2 = true;
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("slot 2: %s", fs::path(curSd2).filename().string().c_str());
        } else {
            ImGui::TextDisabled("(slot 2 empty)");
        }
        for (const auto& c : acsiCands) {
            ImGui::PushID(("sd2" + c.path).c_str());
            if (!curSd2.empty() && samePath(c.path, curSd2)) ImGui::TextDisabled("●");
            else if (ImGui::SmallButton("Slot 2")) reqMountSd2 = c.path;
            ImGui::SameLine();
            ImGui::TextUnformatted(c.label.c_str());
            ImGui::PopID();
        }
        static char g_sd2Buf[512] = "";
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputTextWithHint("##sd2Path", "path to a raw SD image for slot 2…", g_sd2Buf, sizeof g_sd2Buf);
        ImGui::SameLine();
        if (ImGui::Button("Mount##sd2Free") && g_sd2Buf[0]) reqMountSd2 = g_sd2Buf;
    }

    uint32_t acsiDrives = 0;
    if (acsiActive)
        for (int drive = 2; drive < 26 && drive - 2 < acsiParts; ++drive)
            acsiDrives |= uint32_t(1) << drive;
    const uint32_t sharedDrives = gemdosActive ? gemdosDrives & acsiDrives : 0;
    if (sharedDrives) {
        const std::string letters = hardDiskDriveLetters(sharedDrives);
        ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f),
                           "GEMDOS and ACSI both use %s", letters.c_str());
    }
    ImGui::Separator();
    ImGui::TextDisabled("Folder = GEMDOS drive, file = ACSI image. Mounting restarts");
    ImGui::TextDisabled("the machine (TOS only probes disks at boot).");
}

// Page « Network » : les extensions réseau de NeoST (cf. docs/EXTENSIONS.md).
// Discipline habituelle : données en entrée, requêtes en sortie, AUCUNE E/S ici.
void drawNetworkPage(bool modemOn, bool etherOn,
                     bool netusbeeOn, bool cartMounted,
                     bool slirpOn, bool slirpAvail,
                     int& reqModem, int& reqEther, int& reqNetUsbee, int& reqSlirp) {
    ImGui::TextDisabled("Network extensions (NeoST)");
    ImGui::TextWrapped("Hardware that really existed on the ST, emulated here: a Hayes "
                       "modem on the serial port, and NE2000-based cartridge-port "
                       "adapters for the period TCP/IP stacks (STinG, MiNTnet).");
    ImGui::Separator();

    // Modem Hayes : le logiciel d'époque — terminaux, BBS, STinG/STiK en SLIP —
    // parle à « un modem sur le port série ».
    bool mdm = modemOn;
    if (ImGui::Checkbox("Hayes modem on RS-232 (AT commands \xe2\x86\x92 TCP bridge)", &mdm))
        reqModem = mdm ? 1 : 0;

    // EtherNEC : NE2000 sur le port cartouche → pilotes STinG/MiNTnet historiques.
    // NetUSBee : la MÊME NE2000 + un hôte USB ISP1160 — les deux sont exclusifs
    // (un seul montage sur le port cartouche).
    bool eth = etherOn && !netusbeeOn;
    bool nub = netusbeeOn;
    if (cartMounted && !etherOn && !netusbeeOn) {
        ImGui::TextDisabled("EtherNEC / NetUSBee: free the cartridge port to enable");
    } else {
        if (netusbeeOn) ImGui::TextDisabled("EtherNEC (NE2000): included in the NetUSBee below");
        else if (ImGui::Checkbox("EtherNEC (NE2000 on the cartridge port)", &eth))
            reqEther = eth ? 1 : 0;
        if (ImGui::Checkbox("NetUSBee (NE2000 + ISP1160 USB host on the cartridge port)", &nub))
            reqNetUsbee = nub ? 1 : 0;
    }

    // SLIRP : le « câble » de la NE2000 — boucle locale (défaut) ou Internet réel
    // (NAT mode utilisateur). Bascule à chaud : la carte vue par le pilote ST ne
    // change pas. Sans effet tant qu'aucun adaptateur ci-dessus n'est monté.
    ImGui::Separator();
    if (!slirpAvail) {
        ImGui::TextDisabled("Real Internet (libslirp): not available in this build");
    } else {
        bool sl = slirpOn;
        if (ImGui::Checkbox("Real Internet for the NE2000 (user-mode NAT, libslirp)", &sl))
            reqSlirp = sl ? 1 : 0;
        ImGui::TextDisabled("ST 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3 \xe2\x80\x94 needs an ST-side"
                            " TCP/IP stack (STinG ENEC.STX, MiNTnet)");
    }
}
