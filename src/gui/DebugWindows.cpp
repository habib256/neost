// =============================================================================
//  DebugWindows.cpp — cf. DebugWindows.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/DebugWindows.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "core/BitmapView.hpp"
#include "core/Machine.hpp"
#include "gui/App.hpp"
#include "gui/JoyMap.hpp"
#include "gui/StScreenView.hpp"
#include "gui/UiCommon.hpp"
#include "io/JoystickInput.hpp"

void drawHexViewer(App& A, Bus& bus) {
    static int base = 0;
    // `p_open` : c'est lui qui donne la CROIX de fermeture. Ces deux fenêtres
    // d'inspection étaient les seules du projet à l'omettre — Joystick, CRT,
    // Debugger, Floppies, Configuration et Keyboard le passent toutes —, donc les
    // seules qu'on ne pouvait fermer que par le menu Windows. L'état est déjà
    // persisté (cfg.showHex), la croix se comporte donc comme l'entrée de menu.
    ImGui::Begin("Memory (hex)", &A.showHex);
    ImGui::InputInt("Base address", &base, 16, 256,
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    // Le champ ne doit pas confisquer durablement le clavier du ST : dès qu'il
    // perd l'édition (Entrée/Échap/clic ailleurs), on relâche le focus fenêtre
    // pour que WantCaptureKeyboard retombe et que les touches (espace inclus)
    // repartent vers le ST. Sinon le clavier ST « se déconnecte » tant que ce
    // champ garde le focus.
    if (ImGui::IsItemDeactivated())
        ImGui::SetWindowFocus(nullptr);
    if (base < 0) base = 0;
    const auto& mem = bus.ram;
    // Clamp HAUT aussi : saisir $7FFFFFFF ferait déborder base + row*16 (UB signé,
    // adresse négative qui repasse la garde `< mem.size()` → lecture hors bornes).
    if (base > (int)mem.size()) base = (int)mem.size();
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

// reqReset passe à true si le bouton RESET est cliqué.
void drawCpuState(App& A, Cpu68k& cpu, bool& reqReset) {
    ImGui::Begin("CPU 68000", &A.showCpu);   // croix de fermeture, cf. drawHexViewer
    if (IconButton(ICON_FA_POWER_OFF, "Reset (hardware RESET)")) reqReset = true;
    ImGui::Separator();
    // A42 (2026-08-30) — un CPU HALTÉ se dit. Le 68000 assère /HALT sur une double
    // faute de bus/adresse et ne redémarre QUE sur un reset : plus une instruction,
    // écran figé, son coupé. Le headless l'écrivait déjà ; l'interface, elle, ne
    // montrait qu'un gel — d'où des rapports « ça plante » sans diagnostic, alors que
    // la cause est souvent une config (No Cooper en Mega ST : double faute FIDÈLE,
    // cf. docs/CASE_STUDIES.md). Les registres qui suivent sont ceux du GEL, figés.
    if (cpu.halted()) {
        const Cpu68k::Fault f = cpu.lastFault();
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f),
                           ICON_FA_WARNING " CPU HALTED - double bus/address error");
        if (f.valid)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                               "last fault: %s at $%08X, PC=$%06X (vector %d)",
                               f.write ? "writing" : "reading", f.addr, f.pc, f.vector);
        ImGui::TextDisabled("Frozen until reset. Check the machine profile first"
                            " (some demos halt on Mega ST/STE - faithfully).");
        ImGui::Separator();
    }
    ImGui::Text("PC = %08X    SR = %04X", cpu.pc(), cpu.sr());
    ImGui::Separator();
    for (int i = 0; i < 8; ++i)
        ImGui::Text("D%d=%08X   A%d=%08X", i, cpu.reg(i), i, cpu.reg(i + 8));
    ImGui::End();
}

// Fenêtre Joystick : visualisation LIVE de ce que voit l'hôte et de ce qui est
// réellement envoyé au ST. Affiche, pour chaque manette présente, le nom, si elle
// est reconnue « gamepad » (mapping SDL), ses axes (bruts + gamepad) sous forme de
// barres avec la zone morte, ses boutons et son hat ; puis l'octet ST composé pour
// chaque port avec les 5 bits décodés. Inclut les réglages (émulation clavier,
// port, zone morte) modifiables ici. lastJoy0/1 = ce qui a été posé sur l'IKBD.
void drawJoystickAxisBar(const char* label, float v, float dz) {
    // v ∈ [-1,1] → barre [0,1] ; coloration si |v| dépasse la zone morte.
    const float frac = (v + 1.0f) * 0.5f;
    const bool active = (v < -dz) || (v > dz);
    if (active) ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.80f, 0.30f, 1.0f));
    char buf[32]; std::snprintf(buf, sizeof buf, "%+.2f", v);
    ImGui::ProgressBar(frac, ImVec2(140.0f, 0.0f), buf);
    if (active) ImGui::PopStyleColor();
    ImGui::SameLine(); ImGui::TextUnformatted(label);
}

void drawJoyDirLed(const char* label, bool on) {
    const ImVec4 col = on ? ImVec4(0.20f, 0.85f, 0.30f, 1.0f) : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    ImGui::TextColored(col, "%s", label);
    ImGui::SameLine();
}

void drawJoystickWindow(App& A, GLFWwindow* win, uint8_t lastJoy0, uint8_t lastJoy1) {
    ImGui::Begin("Joystick", &A.showJoy);

    // --- Réglages (modifient les globals ; resauve via A.joyCfgDirty) -----------
    if (ImGui::Checkbox("Keyboard emulation (arrows + Ctrl)", &A.kbdJoy)) A.joyCfgDirty = true;
    ImGui::SameLine(); ImGui::TextDisabled("(F11)");
    ImGui::Text("Emulated port:"); ImGui::SameLine();
    if (ImGui::RadioButton("1 (games)", A.kbdJoyPort == 1)) { A.kbdJoyPort = 1; A.joyCfgDirty = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("0 (mouse)", A.kbdJoyPort == 0)) { A.kbdJoyPort = 0; A.joyCfgDirty = true; }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Dead zone", &A.joyDeadzone, 0.0f, 0.95f, "%.2f")) {
        if (A.joyDeadzone < 0.0f) A.joyDeadzone = 0.0f;
        if (A.joyDeadzone > 0.95f) A.joyDeadzone = 0.95f;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) A.joyCfgDirty = true;

    ImGui::Separator();

    // --- Sortie réellement envoyée au ST (le plus important) --------------------
    auto decodeRow = [](const char* who, uint8_t v) {
        ImGui::Text("%s  $%02X :", who, v); ImGui::SameLine();
        drawJoyDirLed("UP",    v & stjoy::UP);
        drawJoyDirLed("DOWN",  v & stjoy::DOWN);
        drawJoyDirLed("LEFT",  v & stjoy::LEFT);
        drawJoyDirLed("RIGHT", v & stjoy::RIGHT);
        drawJoyDirLed("FIRE",  v & stjoy::FIRE);
        ImGui::NewLine();
    };
    ImGui::TextDisabled("→ Sent to the IKBD (ST):");
    decodeRow("Port 0", lastJoy0);
    decodeRow("Port 1", lastJoy1);

    ImGui::Separator();

    // --- État brut de chaque manette présente -----------------------------------
    // Port affiché = affectation EFFECTIVE (joymap + AUTO), comme la page kiosque —
    // l'ordre d'énumération mentait dès qu'un rôle était épinglé (PORT 0/OFF).
    int8_t joyRoles[GLFW_JOYSTICK_LAST + 1];
    joyResolveRoles(A, joyRoles);
    int8_t joyAssign[GLFW_JOYSTICK_LAST + 1];
    stjoy::resolveAssign(joyRoles, joyAssign, A.port0Auto);
    int nPresent = 0;
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickPresent(jid)) continue;
        ++nPresent;
        const char* nm = glfwGetJoystickName(jid);
        const int stPort = joyAssign[jid];
        ImGui::Text("Pad %d: %s", jid, nm ? nm : "?");
        if (stPort >= 0) { ImGui::SameLine(); ImGui::TextDisabled("→ ST port %d", stPort); }
        else             { ImGui::SameLine(); ImGui::TextDisabled("(off)"); }

        GLFWgamepadstate gs;
        if (glfwGetGamepadState(jid, &gs)) {
            ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f), "  recognized as a gamepad (SDL mapping)");
            ImGui::Indent(8.0f);
            drawJoystickAxisBar("LX", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_X],  A.joyDeadzone);
            drawJoystickAxisBar("LY", gs.axes[GLFW_GAMEPAD_AXIS_LEFT_Y],  A.joyDeadzone);
            drawJoystickAxisBar("RX", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], A.joyDeadzone);
            drawJoystickAxisBar("RY", gs.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], A.joyDeadzone);
            ImGui::Unindent(8.0f);
        } else {
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f), "  NOT recognized as a gamepad → raw read");
        }

        // Axes bruts (toujours affichés : révèlent un axe non centré au repos).
        int axN = 0, btN = 0, hatN = 0;
        const float*         ax  = glfwGetJoystickAxes(jid, &axN);
        const unsigned char* bt  = glfwGetJoystickButtons(jid, &btN);
        const unsigned char* hat = glfwGetJoystickHats(jid, &hatN);
        ImGui::Text("  Raw axes (%d):", axN);
        for (int i = 0; i < axN && ax; ++i) {
            char lbl[24]; std::snprintf(lbl, sizeof lbl, "a%d%s", i,
                                        (i == 0 ? " (X?)" : i == 1 ? " (Y?)" : ""));
            ImGui::Indent(8.0f); drawJoystickAxisBar(lbl, ax[i], A.joyDeadzone); ImGui::Unindent(8.0f);
        }
        ImGui::Text("  Buttons (%d):", btN); ImGui::SameLine();
        for (int i = 0; i < btN && bt; ++i)
            if (bt[i]) { ImGui::SameLine(); ImGui::Text("%d", i); }
        if (hat && hatN >= 1)
            ImGui::Text("  Hat0 : %s%s%s%s", (hat[0]&GLFW_HAT_UP)?"U":"", (hat[0]&GLFW_HAT_DOWN)?"D":"",
                        (hat[0]&GLFW_HAT_LEFT)?"L":"", (hat[0]&GLFW_HAT_RIGHT)?"R":"");
        // Décomposition analogique / numérique + effet du filtre anti-bloqué.
        const float thr = (A.joyDeadzone < 0.0f) ? 0.0f : (A.joyDeadzone > 0.95f ? 0.95f : A.joyDeadzone);
        uint8_t an = 0, dg = 0; stjoy::readStickRaw(jid, thr, an, dg);
        const uint8_t fin = stjoy::readStick(jid, A.joyDeadzone);
        ImGui::Text("  analog $%02X | raw digital $%02X", an, dg);
        if ((dg & ~fin) & ~an)
            ImGui::TextColored(ImVec4(1.0f,0.7f,0.3f,1.0f),
                               "  stuck-input filter: jammed digital bits ignored ($%02X)",
                               uint8_t((dg & ~fin) & ~an));
        ImGui::Text("  → ST byte sent: $%02X", fin);
        ImGui::Separator();
    }
    if (nPresent == 0) ImGui::TextDisabled("No pad detected. (Keyboard: enable the emulation above.)");
    (void)win;
    ImGui::End();
}

// Fenêtre « Débogueur » (fenêtré) : breakpoints PC + pause/continue/step-frame +
// désassemblage autour du PC. Le moteur de breakpoints vit dans Cpu68k (conteneur
// Guards de Moira) ; ici on ne fait qu'AFFICHER/piloter. Le gel effectif de
// l'émulation (A.dbgPaused) et le pas-à-pas trame (A.dbgStepFrame) sont traités
// dans la boucle principale. Les registres/mémoire ont déjà leurs propres fenêtres.
// Adresse hexadécimale saisie → valeur 32 bits. Plus de 8 chiffres est REFUSÉ : strtoul
// les lisait sur 64 bits puis la conversion en uint32_t tronquait en silence, et le point
// d'arrêt tombait sur une autre adresse que celle tapée.
static bool parseBpAddr(const char* txt, uint32_t& out) {
    while (*txt == ' ') ++txt;
    if (*txt == '$') ++txt;
    std::size_t n = 0;
    while (txt[n] && txt[n] != ' ') ++n;
    if (n == 0 || n > 8) return false;
    out = (uint32_t)std::strtoul(txt, nullptr, 16);
    return true;
}

void drawDebugger(App& A, Machine& machine) {
    Cpu68k& cpu = machine.cpu;
    static int addrTooLong = 0;               // dernier ajout refusé (> 8 chiffres) : 1 = bp, 2 = wp
    // Étiquette symbolique « <nom+off> » d'une adresse (vide si aucun symbole).
    auto symLabel = [&A](uint32_t a) -> std::string {
        uint32_t off = 0;
        const std::string n = A.symbols.nameFor(a, &off);
        if (n.empty()) return {};
        char b[160]; std::snprintf(b, sizeof b, " <%s+%u>", n.c_str(), off);
        return b;
    };
    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_BUG " Debugger", &A.showDbg);

    // --- État + transport -----------------------------------------------------
    if (A.dbgPaused) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           ICON_FA_PAUSE " PAUSED  \xe2\x80\x94  PC=$%06X%s",
                           cpu.pc(), symLabel(cpu.pc()).c_str());
        if (cpu.breakpointHit() && cpu.breakpointHitIsWatch())
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "  watchpoint: access $%06X%s",
                               cpu.breakpointHitAddr(), symLabel(cpu.breakpointHitAddr()).c_str());
        if (ImGui::Button(ICON_FA_PLAY " Continue")) {
            cpu.clearBreakpointHit();   // arme le skip-once de l'adresse courante
            A.dbgPaused = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STEP_FORWARD " Step (1 instr)")) A.dbgStepInstr = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STEP_FORWARD " Step (1 frame)")) A.dbgStepFrame = true;
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), ICON_FA_PLAY " Running");
        if (ImGui::Button(ICON_FA_PAUSE " Pause")) A.dbgPaused = true;
    }
    ImGui::Separator();

    // --- Symboles : chargement (.sym nm-style ou exécutable TOS) + bp par nom --
    ImGui::Text("Symbols (%zu)", A.symbols.count());
    static char symPath[512] = "";
    static char symBaseBuf[16] = "";
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##sympath", ".sym or .TOS path", symPath, sizeof symPath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputTextWithHint("##symbase", "hex base", symBaseBuf, sizeof symBaseBuf,
                             ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Load") && symPath[0]) {
        const uint32_t base = (uint32_t)std::strtoul(symBaseBuf, nullptr, 16);
        A.symbols.load(symPath, base);   // auto-détecte nm-style vs exécutable TOS
    }
    // Breakpoint par symbole (nom → adresse via la table).
    static char symBp[64] = "";
    ImGui::SetNextItemWidth(220.0f);
    const bool symEnter = ImGui::InputTextWithHint("##symbp", "symbol name", symBp, sizeof symBp,
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Symbol BP") || symEnter) && symBp[0]) {
        uint32_t a = 0;
        if (A.symbols.lookup(symBp, a)) { cpu.setBreakpoint(a); symBp[0] = '\0'; }
    }
    ImGui::Separator();

    // --- Breakpoints : ajout + liste ------------------------------------------
    ImGui::Text("Breakpoints (%d)", cpu.breakpointCount());
    static char bpBuf[16] = "";
    ImGui::SetNextItemWidth(120.0f);
    const bool entered = ImGui::InputText("##bpaddr", bpBuf, sizeof bpBuf,
                                          ImGuiInputTextFlags_CharsHexadecimal |
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add") || entered) && bpBuf[0]) {
        uint32_t a = 0;
        addrTooLong = parseBpAddr(bpBuf, a) ? 0 : 1;
        if (!addrTooLong) { cpu.setBreakpoint(a); bpBuf[0] = '\0'; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) cpu.clearAllBreakpoints();
    if (addrTooLong == 1)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Address too long: 8 hex digits at most.");

    if (ImGui::BeginChild("##bplist", ImVec2(0, 120), true)) {
        for (int i = 0; i < cpu.breakpointCount(); ++i) {
            uint32_t a = 0;
            if (!cpu.breakpointByIndex(i, a)) continue;
            ImGui::PushID(i);
            if (ImGui::SmallButton(ICON_FA_TIMES)) {   // retirer (les indices bougent → on sort)
                cpu.clearBreakpoint(a);
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            char dis[256]; cpu.disassemble(dis, a);
            ImGui::Text("$%06X%s  %s", a, symLabel(a).c_str(), dis);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();

    // --- Watchpoints mémoire : arrêt à l'accès (lecture OU écriture) d'une adresse
    ImGui::Text("Watchpoints (%d)", cpu.watchpointCount());
    static char wpBuf[16] = "";
    ImGui::SetNextItemWidth(120.0f);
    const bool wpEnter = ImGui::InputTextWithHint("##wpaddr", "hex address", wpBuf, sizeof wpBuf,
                                                  ImGuiInputTextFlags_CharsHexadecimal |
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add##wp") || wpEnter) && wpBuf[0]) {
        uint32_t a = 0;
        addrTooLong = parseBpAddr(wpBuf, a) ? 0 : 2;
        if (!addrTooLong) { cpu.setWatchpoint(a); wpBuf[0] = '\0'; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all##wp")) cpu.clearAllWatchpoints();
    if (addrTooLong == 2)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Address too long: 8 hex digits at most.");
    if (ImGui::BeginChild("##wplist", ImVec2(0, 80), true)) {
        for (int i = 0; i < cpu.watchpointCount(); ++i) {
            uint32_t a = 0;
            if (!cpu.watchpointByIndex(i, a)) continue;
            ImGui::PushID(1000 + i);
            if (ImGui::SmallButton(ICON_FA_TIMES)) { cpu.clearWatchpoint(a); ImGui::PopID(); break; }
            ImGui::SameLine();
            ImGui::Text("$%06X%s", a, symLabel(a).c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();

    // --- Désassemblage autour du PC (clic sur une ligne = toggle breakpoint) ---
    ImGui::TextDisabled("Disassembly (click = toggle a breakpoint)");
    if (ImGui::BeginChild("##disasm", ImVec2(0, 0), true)) {
        const uint32_t pc = cpu.pc();
        uint32_t addr = pc;
        for (int i = 0; i < 24; ++i) {
            char dis[256];
            int len = cpu.disassemble(dis, addr);
            if (len <= 0) len = 2;
            const bool isPc  = (addr == pc);
            const bool hasBp = cpu.hasBreakpoint(addr);
            char line[300];
            std::snprintf(line, sizeof line, "%s %s $%06X%s  %s",
                          hasBp ? ICON_FA_TIMES : "  ", isPc ? ">" : " ", addr,
                          symLabel(addr).c_str(), dis);
            if (hasBp) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.4f, 1.0f));
            else if (isPc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.5f, 1.0f));
            if (ImGui::Selectable(line, isPc)) {
                if (hasBp) cpu.clearBreakpoint(addr); else cpu.setBreakpoint(addr);
            }
            if (hasBp || isPc) ImGui::PopStyleColor();
            addr += (uint32_t)len;
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// =============================================================================
//  Explorateur de bitmaps — la mémoire en plans ST, avec la palette courante.
//
//  Modèle : la DebugWindow de l'ASE (adresse de départ, nombre de plans, largeur
//  en octets, palette, zoom, défilement), absente de la machine de développement :
//  conçu ici sur ce cahier des charges. Le décodage vit dans le cœur
//  (core/BitmapView) ; la fenêtre lit la mémoire SANS effet de bord
//  (Bus::peekMapped8 : la RAM telle que la vidéo la voit, puis ROM et cartouche)
//  et pose le résultat dans une texture. Rien n'est écrit dans la machine.
//
//  « Follow screen » recopie à chaque trame la base vidéo, la résolution et la
//  largeur de ligne STE : la vue montre alors la RAM de l'écran, SANS bordure, scroll
//  fin ni changement de palette en cours de trame — la mémoire, pas l'image.
//  Décochée, l'adresse se déplace à la molette (8 lignes, une page avec Maj), aux
//  boutons, ou se saisit.
// =============================================================================
namespace {
struct BitmapExplorerState {
    int  addr    = 0;           // adresse de départ (24 bits)
    int  planes  = 4;           // 1, 2 ou 4
    int  stride  = 160;         // largeur d'une ligne en octets
    int  height  = 200;         // lignes affichées
    int  zoom    = 2;
    int  palette = 0;           // 0 = palette courante, 1 = gris
    bool follow  = true;        // suit la base vidéo et la résolution
    GlScreen tex;
    std::vector<uint8_t>  bytes, valid;
    std::vector<uint32_t> argb;
};
}  // namespace

void drawBitmapExplorer(App& A, Machine& machine) {
    static BitmapExplorerState S;
    constexpr int kMaxStride = 1024, kMaxHeight = 1024;
    Shifter& sh = machine.shifter;

    ImGui::SetNextWindowSize(ImVec2(560, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_PALETTE " Bitmap explorer", &A.showBitmap)) { ImGui::End(); return; }

    // --- Suivi de l'écran : base vidéo, plans selon la résolution, ligne STE ------
    ImGui::Checkbox("Follow screen", &S.follow);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Track the video base address, resolution and STE line width.\n"
                          "Shows the screen RAM - no border, fine scroll or raster.");
    if (S.follow) {
        S.addr   = int(sh.videoBase & 0xFFFFFEu);
        S.planes = sh.mode == Shifter::Mode::High ? 1 : sh.mode == Shifter::Mode::Medium ? 2 : 4;
        S.stride = 160 + 2 * int(sh.lineWidth);
        S.height = sh.mode == Shifter::Mode::High ? 400 : 200;
    }

    // --- Géométrie -----------------------------------------------------------
    ImGui::BeginDisabled(S.follow);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Address", &S.addr, 0, 0,
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivated()) ImGui::SetWindowFocus(nullptr);   // rend le clavier au ST
    ImGui::SameLine();
    ImGui::TextDisabled("$%06X", unsigned(S.addr) & 0xFFFFFFu);
    ImGui::TextUnformatted("Planes:"); ImGui::SameLine();
    if (ImGui::RadioButton("1 (high)", S.planes == 1))   S.planes = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("2 (medium)", S.planes == 2)) S.planes = 2;
    ImGui::SameLine();
    if (ImGui::RadioButton("4 (low)", S.planes == 4))    S.planes = 4;
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Width (bytes)", &S.stride, 2 * S.planes, 16 * S.planes);
    if (ImGui::IsItemDeactivated()) ImGui::SetWindowFocus(nullptr);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Lines", &S.height, 8, 64);
    if (ImGui::IsItemDeactivated()) ImGui::SetWindowFocus(nullptr);
    ImGui::EndDisabled();

    S.planes = neost::bitmapPlanes(S.planes);
    S.stride = std::clamp(S.stride, 2 * S.planes, kMaxStride);
    S.height = std::clamp(S.height, 1, kMaxHeight);

    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("Palette", &S.palette, "Current (shifter)\0Grayscale\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Zoom", &S.zoom, 1, 8, "x%d");

    // --- Défilement de l'adresse : boutons ; la molette agit sur l'image ------
    const int page = S.stride * S.height;
    ImGui::BeginDisabled(S.follow);
    ImGui::TextUnformatted("Scroll:");
    auto nudge = [&](const char* label, int delta) {
        ImGui::SameLine();
        if (ImGui::Button(label)) S.addr += delta;
    };
    nudge("-page", -page); nudge("-line", -S.stride); nudge("-word", -2);
    nudge("+word", 2);     nudge("+line", S.stride);  nudge("+page", page);
    ImGui::EndDisabled();
    S.addr = std::clamp(S.addr, 0, 0xFFFFFF);

    // --- Lecture sans effet de bord, puis décodage dans le cœur ---------------
    neost::BitmapLayout lay;
    lay.planes = S.planes; lay.strideBytes = S.stride; lay.height = S.height;
    const int w = neost::bitmapWidthPx(lay);
    const std::size_t n = neost::bitmapBytes(lay);
    S.bytes.assign(n, 0);
    S.valid.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const uint32_t a = (uint32_t(S.addr) + uint32_t(i)) & 0xFFFFFFu;
        uint8_t v = 0;
        if (machine.bus.peekMapped8(a, v)) { S.bytes[i] = v; S.valid[i] = 1; }
    }
    uint32_t pal[16];
    // Haute résolution = moniteur monochrome : un plan, polarité du registre 0 seule
    // (cf. neost::bitmapPalette). Un plan en basse ou moyenne → registres 0 et 1.
    const bool mono = (sh.mode == Shifter::Mode::High) && S.planes == 1;
    neost::bitmapPalette(sh.palette.data(), S.planes, mono, S.palette == 1, pal);
    S.argb.assign(std::size_t(w) * std::size_t(S.height), 0xFF000000u);
    // Trou du plan mémoire (banque vide, MMIO) : une teinte sombre distincte du noir.
    neost::decodeBitmap(lay, S.bytes.data(), S.valid.data(), pal, 0xFF402040u, S.argb.data());

    const uint32_t endAddr = (uint32_t(S.addr) + uint32_t(n) - 1u) & 0xFFFFFFu;
    ImGui::Text("$%06X-$%06X  %d x %d px", unsigned(S.addr), unsigned(endAddr), w, S.height);

    // --- Palette appliquée (pastilles) ----------------------------------------
    const int colors = 1 << S.planes;
    for (int i = 0; i < colors; ++i) {
        const uint32_t c = pal[i];
        const ImVec4 col(float((c >> 16) & 0xFF) / 255.0f, float((c >> 8) & 0xFF) / 255.0f,
                         float(c & 0xFF) / 255.0f, 1.0f);
        if (i) ImGui::SameLine(0.0f, 2.0f);
        ImGui::PushID(i);
        ImGui::ColorButton("##pal", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(16.0f, 16.0f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Color %d = $%03X", i, unsigned(sh.palette[i]) & 0xFFFu);
        ImGui::PopID();
    }

    if (w <= 0) { ImGui::TextDisabled("Width too small for %d planes.", S.planes); ImGui::End(); return; }
    if (S.tex.tex == 0) S.tex.init();
    S.tex.update(S.argb.data(), w, S.height);

    // --- Image : enfant défilant (barres dès que le zoom dépasse la fenêtre) ----
    // Hors suivi, la molette déplace l'ADRESSE : l'enfant ne doit pas la consommer.
    ImGui::BeginChild("##bitmap", ImVec2(0, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar
                      | (S.follow ? 0 : ImGuiWindowFlags_NoScrollWithMouse));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)S.tex.tex, ImVec2(float(w * S.zoom), float(S.height * S.zoom)));
    if (ImGui::IsItemHovered()) {
        const ImVec2 m = ImGui::GetMousePos();
        const int x = int((m.x - origin.x) / float(S.zoom));
        const int y = int((m.y - origin.y) / float(S.zoom));
        if (x >= 0 && x < w && y >= 0 && y < S.height) {
            const std::size_t off = std::size_t(y) * std::size_t(S.stride)
                                  + std::size_t((x >> 4) * 2 * S.planes);
            const unsigned group = (unsigned(S.addr) + unsigned(off)) & 0xFFFFFFu;
            if (S.valid[off])
                ImGui::SetTooltip("x %d  y %d\nword group $%06X\ncolor %d", x, y, group,
                                  neost::bitmapPixelIndex(S.bytes.data() + std::size_t(y) * std::size_t(S.stride),
                                                          S.planes, x));
            else
                ImGui::SetTooltip("x %d  y %d\n$%06X: unmapped", x, y, group);
        }
        // Molette : 8 lignes, une page avec Maj. Seulement hors suivi de l'écran.
        const float wheel = ImGui::GetIO().MouseWheel;
        if (!S.follow && wheel != 0.0f) {
            const int step = ImGui::GetIO().KeyShift ? page : 8 * S.stride;
            S.addr = std::clamp(S.addr - int(wheel) * step, 0, 0xFFFFFF);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
