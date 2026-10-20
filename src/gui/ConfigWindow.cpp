// =============================================================================
//  ConfigWindow.cpp — cf. ConfigWindow.hpp.
//
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "gui/ConfigWindow.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "imgui.h"

#include "audio/MidiDeviceProfiles.hpp"
#include "audio/MidiInHost.hpp"
#include "audio/Mt32Synth.hpp"
#include "audio/GmSynth.hpp"
#include "core/Machine.hpp"
#include "gui/App.hpp"
#include "gui/CrtUi.hpp"
#include "gui/JoyMap.hpp"
#include "gui/MediaPages.hpp"
#include "gui/UiCommon.hpp"
#include "io/JoystickInput.hpp"
#include "io/MediaScan.hpp"
#include "net/SlirpBackend.hpp"

namespace fs = std::filesystem;
using namespace neost::appconfig;   // profileFileName / listProfiles, comme dans main.cpp



// Fenêtre autonome : les disquettes sont des supports manipulés en jouant, pas un
// réglage matériel. Les requêtes restent consommées par la boucle principale.
void drawFloppyWindow(App& A, ConfigUi& ui) {
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_SAVE " Floppies", &A.showFloppy,
                      ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::End();
        return;
    }
    drawFloppyPage(ui.disksDir,
                   ui.machine->fdc.mountedPath(0), ui.machine->fdc.mountedPath(1),
                   ui.reqMountA, ui.reqMountB, ui.reqEjectA, ui.reqEjectB);
    ImGui::End();
}

// Suffixe pays d'une ROM → fréquence de balayage. C'est LA cause d'écran « déchiré »
// la plus fréquente sur les démos européennes (images Spectrum 512 calculées pour le
// 50 Hz, jouées en 60 Hz) et rien ne la montrait dans l'interface. Cf. CLAUDE.md.
bool romIsNtsc(const std::string& filename) {
    const std::string stem = fs::path(filename).stem().string();
    return stem.size() >= 2 && stem.compare(stem.size() - 2, 2, "us") == 0;
}

void drawConfigWindow(App& A, ConfigUi& ui) {
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_COG " Configuration", &A.showCfg)) { ImGui::End(); return; }

    const Config& cfg = *ui.cfg;
    // Semis des champs « en attente » : à la première ouverture, et après chaque
    // application (l'appelant remet pendInit à false).
    if (!ui.pendInit) {
        ui.pendMachine = cfg.machine; ui.pendMem = cfg.mem;
        ui.pendRom = cfg.rom;         ui.pendFpu = cfg.fpu;
        ui.pendInit = true;
    }

    // ── Préréglages : la config matérielle complète en un clic. Codés en dur et
    // limités au MATÉRIEL (ils ne font que garnir les champs « en attente »). Les
    // configurations de l'utilisateur, elles, vivent dans la page « Profiles ».
    // Chaque préréglage nomme le TOS d'ORIGINE de la machine, PUIS des replis EmuTOS.
    // Sans repli, un préréglage pointait sur une ROM absente du paquet livré : « Mega STE »
    // demande tos206fr, qui n'a JAMAIS été empaquetée, et « 520 ST »/« 1040 STE » perdent
    // la leur dès qu'un paquet est construit sans les TOS Atari
    // (NEOST_PACKAGE_NO_ATARI_TOS=1, cf. packaging/stage_free_data.sh). Le repli garde le
    // pays/la fréquence quand il le peut : tos102uk et tos162uk sont PAL → etos*fr (PAL).
    struct Profil { const char* label; const char* machine; const char* mem;
                    const char* rom; const char* rom2; const char* rom3; };
    static const Profil kProfils[] = {
        { "520 ST",   "st",      "512k", "roms/tos102uk.img", "roms/etos192fr.img", "roms/etos192us.img" },
        { "1040 STE", "ste",     "1m",   "roms/tos162uk.img", "roms/etos256fr.img", "roms/etos256us.img" },
        { "Mega STE", "megaste", "4m",   "roms/tos206fr.img", "roms/etos256fr.img", "roms/etos256us.img" },
    };
    // Premier candidat PRÉSENT dans roms/ ; à défaut le premier (message d'erreur explicite
    // au chargement plutôt qu'un chemin silencieusement faux).
    auto pickPresetRom = [&](const Profil& p) {
        std::error_code ec;
        for (const char* cand : { p.rom, p.rom2, p.rom3 })
            if (fs::exists(fs::path(ui.romsDir) / fs::path(cand).filename(), ec)) return std::string(cand);
        return std::string(p.rom);
    };
    ImGui::TextDisabled("Presets:");
    for (const auto& p : kProfils) {
        ImGui::SameLine();
        const std::string rom = pickPresetRom(p);
        const bool cur = ui.pendMachine == p.machine && ui.pendMem == p.mem
                      && fs::path(ui.pendRom).filename() == fs::path(rom).filename();
        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(p.label)) {
            ui.pendMachine = p.machine; ui.pendMem = p.mem; ui.pendRom = rom;
            ui.pendFpu = false;
        }
        if (cur) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s, %s, %s%s", p.machine, p.mem, rom.c_str(),
                              rom == p.rom ? "" : "\n(original TOS not installed - using EmuTOS)");
    }
    ImGui::Separator();

    // ── Colonne de navigation + page ──────────────────────────────────────
    static const char* kPageNames[kCfgCount] = {
        ICON_FA_MICROCHIP " Machine",  ICON_FA_MEMORY " Memory",
        ICON_FA_SAVE " ROM / TOS",     ICON_FA_HDD " Hard disks",
        ICON_FA_COMPACT_DISC " Cartridge",
        ICON_FA_WIFI " Network",
        ICON_FA_KEY " Dongles",
        ICON_FA_DESKTOP " Screen",     ICON_FA_VOLUME_UP " Sound",
        ICON_FA_MUSIC " MIDI",
        ICON_FA_GAMEPAD " Input",      ICON_FA_BOLT " Emulation",
        ICON_FA_STAR " Profiles",      ICON_FA_DESKTOP " Kiosk",
    };
    // Hauteur réservée au pied de page : le message « à jour » tient sur deux lignes
    // dans une fenêtre étroite — sans la deuxième, il déborde sous le bord.
    const float footH = ImGui::GetFrameHeightWithSpacing() + 2.0f * ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##cfgNav", ImVec2(165, -footH), true);
    for (int i = 0; i < kCfgCount; ++i)
        if (ImGui::Selectable(kPageNames[i], A.cfgPage == i)) A.cfgPage = i;
    ImGui::EndChild();
    ImGui::SameLine();
    // Barre de défilement HORIZONTALE : les noms de dumps sont longs par nature
    // (« st/Blood Money (1989)(Psygnosis)[cr Delight][m Superior][t].st ») et la
    // fenêtre est étroite quand elle est ancrée sur le côté — sans elle, la moitié
    // du nom est simplement invisible, sans aucun moyen d'aller la voir.
    ImGui::BeginChild("##cfgPage", ImVec2(0, -footH), true, ImGuiWindowFlags_HorizontalScrollbar);

    switch (A.cfgPage) {
    case kCfgMachine: {
        ImGui::TextDisabled("Machine model");
        static const char* labels[] = { "ST", "Mega ST", "STE", "Mega STE" };
        static const char* ids[]    = { "st", "megast", "ste", "megaste" };
        for (int i = 0; i < 4; ++i)
            if (ImGui::RadioButton(labels[i], ui.pendMachine == ids[i])) ui.pendMachine = ids[i];
        ImGui::Separator();
        // Le socket MC68881 n'existe QUE sur Mega STE (cf. Fpu.hpp) ; ailleurs il n'y a
        // rien à peupler et « not found » est le comportement fidèle.
        ImGui::BeginDisabled(ui.pendMachine != "megaste");
        ImGui::Checkbox("Populate the MC68881 FPU socket", &ui.pendFpu);
        ImGui::EndDisabled();
        if (ui.pendMachine != "megaste")
            ImGui::TextDisabled("(FPU socket: Mega STE only)");
        ImGui::Separator();
        ImGui::TextWrapped("A TOS ≤ 1.04 booted on an STE/Mega STE switches NeoST to ST "
                           "mode (like Hatari): those TOS versions know nothing of the "
                           "extra hardware.");
        break;
    }
    case kCfgMem: {
        ImGui::TextDisabled("ST-RAM");
        // 2176/2560 KB : banque 0 de 2 Mo + banque 1 de 128/512 Ko (combinaisons MMU réelles).
        static const char* mlabels[] = { "256 KB", "512 KB", "1 MB", "2 MB",
                                         "2176 KB (2 MB + 128 KB)", "2.5 MB (2 MB + 512 KB)", "4 MB" };
        static const char* mids[]    = { "256k", "512k", "1m", "2m", "2176k", "2560k", "4m" };
        for (int i = 0; i < 7; ++i)
            if (ImGui::RadioButton(mlabels[i], ui.pendMem == mids[i])) ui.pendMem = mids[i];
        ImGui::Separator();
        ImGui::TextWrapped("512 KB = the 1985 machine. Many games from 1989 on, and most "
                           "cracks/depackers, require 1 MB: with 512 KB they give no "
                           "warning, they just go haywire (black screen frozen after "
                           "the intro).");
        break;
    }
    case kCfgRom: {
        ImGui::TextDisabled("TOS images in %s/", ui.romsDir.c_str());
        std::error_code ec;
        if (fs::is_directory(ui.romsDir, ec)) {
            const std::string curName = fs::path(ui.pendRom).filename().string();
            std::vector<fs::path> roms;
            fs::directory_iterator it(ui.romsDir, fs::directory_options::skip_permission_denied, ec), end;
            while (!ec && it != end) {
                std::error_code ec2;
                if (it->is_regular_file(ec2)) {
                    std::string ext = it->path().extension().string();
                    for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                    if (ext == ".img" || ext == ".rom") roms.push_back(it->path());
                }
                it.increment(ec);
            }
            auto key = [](const fs::path& p) {
                std::string n = p.filename().string();
                for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
                return n;
            };
            std::sort(roms.begin(), roms.end(),
                      [&](const fs::path& a, const fs::path& b) { return key(a) < key(b); });
            for (const auto& p : roms) {
                const std::string name = p.filename().string();
                if (ImGui::RadioButton(name.c_str(), name == curName)) ui.pendRom = p.string();
                ImGui::SameLine();
                if (romIsNtsc(name)) ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), "60 Hz NTSC");
                else                 ImGui::TextDisabled("50 Hz PAL");
            }
        } else {
            ImGui::TextDisabled("(roms/ folder not found)");
        }
        ImGui::Separator();
        ImGui::TextWrapped("The ROM sets the scan rate: an \"us\" suffix = 60 Hz NTSC, "
                           "\"uk/fr/de/es\" = 50 Hz PAL. European demos come out TORN "
                           "at 60 Hz — faithfully so, that also happens on real "
                           "hardware.");
        break;
    }
    case kCfgHd:
        drawHardDiskPage(ui.hdDir, ui.gemdosDir,
                         ui.curGemdos, ui.machine->gemdos.active(), ui.machine->gemdos.driveMask(),
                         ui.curAcsi, ui.machine->fdc.acsiActive(),
                         ui.machine->fdc.acsiPartitionCount(),
                         ui.machine->ultraSatanEnabled(), ui.curSd2,
                         ui.reqMountGemdos, ui.reqEjectGemdos,
                         ui.reqMountAcsi,   ui.reqEjectAcsi,
                         ui.reqUltraSatan, ui.reqMountSd2, ui.reqEjectSd2);
        break;
    case kCfgCart:
        drawCartPage(ui.cartsDir, ui.machine->bus.mountedCartPath(),
                     ui.reqMountCart, ui.reqEjectCart);
        break;
    case kCfgNet:
        drawNetworkPage(ui.cfg && ui.cfg->modem, ui.machine->ne2000.enabled(),
                        ui.machine->netUsbeeEnabled(),
                        !ui.machine->bus.mountedCartPath().empty(),
                        ui.cfg && ui.cfg->slirp, SlirpBackend::available(),
                        ui.reqModem, ui.reqEther, ui.reqNetUsbee, ui.reqSlirp);
        break;
    // ── Dongles ───────────────────────────────────────────────────────────────
    // Tout ce qui se branchait sur un port pour qu'un logiciel le sonde : les clés du
    // port cartouche (io/CartridgeKey.hpp) et un périphérique par autre port
    // (io/PortDevices.hpp) — modèle physique : ils coexistent.
    case kCfgDongle: {
        ImGui::TextDisabled("CARTRIDGE PORT KEY (/ROM3 $FB0000, invisible to TOS)");
        int dk = cfg.dongle == "cubase2" ? 1 : cfg.dongle == "cubase3" ? 2 : cfg.dongle == "auto" ? 3
               : cfg.dongle == "notator" ? 4 : 0;
        bool dkCh = false;
        dkCh |= ImGui::RadioButton("None##key", &dk, 0); ImGui::SameLine();
        dkCh |= ImGui::RadioButton("Red key (Cubase 3.1 / Score / Audio)", &dk, 2);
        dkCh |= ImGui::RadioButton("Black key (Cubase 2.01)", &dk, 1); ImGui::SameLine();
        dkCh |= ImGui::RadioButton("Auto (red/black)", &dk, 3);
        dkCh |= ImGui::RadioButton("C-Lab key (Notator / Creator, Unitor-N)", &dk, 4);
        if (dkCh) ui.reqDongle = dk;
        ImGui::TextDisabled("  PAL16R8 / 5C060 / EP600 state machines (MiSTery + TPH equations).");
        ImGui::TextDisabled("  Cubase Lite needs none. Black key: clocked by every CPU bus cycle - best effort.");
        // Observabilité : ce que la clé a vu. Le jour où un logiciel dit « dongle not
        // found », c'est ici qu'on regarde d'abord (puis --key-log / --key-replay).
        if (ui.machine->dongle.attached()) {
            const auto& k = ui.machine->dongle;
            ImGui::Text("  Probes: %u   last byte: $%02X   state: $%04X%s",
                        unsigned(k.probes()), unsigned(k.lastByte()), unsigned(k.state()),
                        k.model() == CartridgeKey::Model::Notator ? (k.armed() ? "   armed" : "   not armed") : "");
        }
        ImGui::Separator();

        ImGui::TextDisabled("ONE DEVICE PER PORT (they coexist, like real hardware)");
        static const char* const portNames[] = { "Joystick 0 (mouse port)", "Joystick 1", "RS-232", "Printer", "Cartridge button" };
        for (int pi = 0; pi < int(PortDevices::Port::Count); ++pi) {
            const auto port = PortDevices::Port(pi);
            const PortDevices::Device cur = ui.machine->ports.at(port);
            ImGui::SetNextItemWidth(260);
            if (ImGui::BeginCombo(portNames[pi], PortDevices::label(cur))) {
                for (int di = 0; di < int(PortDevices::Device::Count); ++di) {
                    const auto d = PortDevices::Device(di);
                    if (!PortDevices::fits(port, d)) continue;
                    const bool home = d == PortDevices::Device::None || PortDevices::defaultPort(d) == port;
                    char lab[96];
                    std::snprintf(lab, sizeof lab, "%s%s", PortDevices::label(d), home ? "" : "  (wrong port for this game)");
                    if (ImGui::Selectable(lab, cur == d)) { ui.reqPlugPort = pi; ui.reqPlugDev = di; }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Spacing();
        if (ui.machine->ports.hasButton()) {
            const bool mf = ui.machine->ports.at(PortDevices::Port::CartButton) == PortDevices::Device::Multiface;
            if (ImGui::Button(mf ? "Press FREEZE button" : "Press RIPPER button")) ui.reqPortButton = true;
            ImGui::SameLine();
            ImGui::TextDisabled(mf ? "pulls GPIP7 (monitor) low until next VBL - load the ROM as a cartridge"
                                   : "pulls RI (GPIP6) until next VBL - load the ROM as a cartridge");
        }
        ImGui::TextWrapped("Joystick keys override the directions the IKBD reports (Leader Board: up+down "
                           "at once; Cricket & co: an oscillator) - a game only looks at one port, plug the "
                           "key where it expects it. Serial keys drive CTS/DCD from RTS/DTR. Pro Sound "
                           "Designer is not a key: an 8-bit DAC on the printer port (Wings of Death / "
                           "Lethal Xcess on an STF), with its own fader on the Sound page. disks/dongles.txt "
                           "plugs keys automatically when a matching disk is mounted. Protocols from Steem "
                           "SSE and WinUAE; not emulated (no public dump): Log 3, Pro-24, Avalon, Zodiac.");
        break;
    }

    case kCfgScreen: {
        ImGui::TextDisabled("Atari monitor");
        if (ImGui::RadioButton("Color (low/medium res)", ui.color))  ui.reqMonitor = 1;
        if (ImGui::RadioButton("Mono (high res)",        !ui.color))  ui.reqMonitor = 0;
        ImGui::Separator();
        // Même cadrage adaptatif que la borne : l'écran cale sa zone de contenu sur la
        // hauteur disponible, les bordures inutilisées sortent du cadre, et une
        // ouverture de bordure (démo overscan) rend le cadre entier.
        if (ImGui::Checkbox("Auto zoom (adaptive framing)", &A.autoZoom)) ui.cfgDirty = true;
        ImGui::Separator();
        ImGui::TextDisabled("CRT look");
        bool crtChanged = false;
        drawCrtControls(A, crtChanged);
        if (crtChanged) ui.cfgDirty = true;
        break;
    }
    case kCfgSound: {
        ImGui::TextDisabled("Master volume (host output, independent of the emulated LMC1992)");
        int pct = int(ui.volume * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::SliderInt("##vol", &pct, 0, 100, "%d %%")) ui.reqVolume = float(pct) / 100.0f;
        if (ImGui::IsItemDeactivatedAfterEdit()) ui.volumeDone = true;
        ImGui::SameLine();
        if (ImGui::SmallButton(ui.volume <= 0.0f ? "Unmute" : "Mute")) {
            ui.reqVolume = (ui.volume <= 0.0f) ? 1.0f : 0.0f;
            ui.volumeDone = true;
        }
        ImGui::Separator();
        // Réglage MÉMORISÉ (drivesound=) — il ne l'était pas : la case se cochait, se
        // décochait, et repartait à « on » au lancement suivant.
        ImGui::BeginDisabled(!ui.driveSoundAvail);
        if (ImGui::Checkbox("Floppy drive sound", &ui.driveSound)) ui.cfgDirty = true;
        ImGui::EndDisabled();
        if (!ui.driveSoundAvail)
            ImGui::TextDisabled("(samples not found in roms/drivesound/)");
        ImGui::Separator();
        ImGui::TextDisabled("Audio latency is set at launch (--audio-latency MS, stored");
        ImGui::TextDisabled("in neost.cfg): changing it live would rebuild the audio");
        ImGui::TextDisabled("ring mid-playback.");
        ImGui::Text("Target cushion: %d ms", cfg.audioLatencyMs);
        // --- Mixeur : un fader par source de NeoST ---------------------------------
        ImGui::Separator();
        ImGui::TextDisabled("Mixer (per-source gains, 100 %% = as on the hardware)");
        {
            auto fader = [&](const char* label, float& v, bool enabled, const char* why) {
                int pct = int(v * 100.0f + 0.5f);
                ImGui::BeginDisabled(!enabled);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderInt(label, &pct, 0, 200, "%d %%")) { v = float(pct) / 100.0f; ui.mixDirty = true; }
                if (ImGui::IsItemDeactivatedAfterEdit()) ui.mixDone = true;
                ImGui::EndDisabled();
                if (!enabled && why) { ImGui::SameLine(); ImGui::TextDisabled("%s", why); }
            };
            const bool isSte = ui.machine && (ui.machine->machineType() == MachineType::Ste
                                               || ui.machine->machineType() == MachineType::MegaSte);
            fader("YM2149 (PSG)", ui.mixYm, true, nullptr);
            fader("DMA sound (STE)", ui.mixDma, isSte, "(ST: no DMA sound)");
            fader("Floppy drive", ui.mixDrive, ui.driveSoundAvail, "(no samples)");
            fader("Roland MT-32 / CM-32L", ui.mixMt32, Mt32Synth::available(), "(no libmt32emu)");
            // Le DLSMusicDevice macOS sort HORS mixeur (AUGraph → sortie système) :
            // le fader ne vaut que pour le chemin TinySoundFont, mixé ici.
            if (!MidiOutHost::synthAvailable())
                fader("Built-in GM synth", ui.mixGm, GmSynth::available(), "(no SoundFont)");
            fader("Pro Sound DAC (printer port)", ui.mixDac, ui.machine && ui.machine->ports.usesPortBDac(), "(none plugged - Dongles page)");
            if (ImGui::SmallButton("Reset mixer")) {
                ui.mixYm = ui.mixDma = ui.mixDrive = ui.mixMt32 = ui.mixDac = ui.mixGm = 1.0f; ui.mixDirty = ui.mixDone = true;
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("MIDI OUT has its own page (MIDI).");
        break;
    }

    // ── MIDI ──────────────────────────────────────────────────────────────────
    // Page à part : la sortie MIDI n'est pas un réglage de volume, c'est un CÂBLAGE
    // vers l'extérieur — et elle est le meilleur moyen d'entendre correctement du
    // General MIDI, que le MT-32 ne sait pas jouer (cf. la note sur les cartes).
    case kCfgMidi: {
        ImGui::TextDisabled("MIDI OUT of the ST (ACIA 6850)");
        ImGui::Separator();

        // Liste des appareils À AFFICHER : ce qui est branché MAINTENANT, PLUS ce que
        // la config mémorise mais qui est absent. Un appareil débranché ne doit pas
        // disparaître de l'écran — sinon son réglage semble s'être évaporé.
        //
        // `cfgIdx` relie chaque ligne à SON entrée de configuration via l'appariement
        // (identifiant d'abord, nom ensuite, jamais deux fois le même point) : c'est ce
        // qui permet à deux appareils du même modèle — donc de même NOM — d'avoir
        // chacun sa ligne et son réglage.
        struct DevRow { std::string name, uid, label; bool plugged; int cfgIdx; };
        auto buildRows = [](const std::vector<neost::midi::Endpoint>& have,
                            const std::vector<neost::midi::Wanted>& conf) {
            const std::vector<int> pick = neost::midi::matchEndpoints(conf, have);
            std::vector<DevRow> rows;
            for (std::size_t e = 0; e < have.size(); ++e) {
                int owner = -1;
                for (std::size_t w = 0; w < conf.size(); ++w)
                    if (pick[w] == int(e)) { owner = int(w); break; }
                rows.push_back({have[e].name, have[e].uid,
                                neost::midi::displayLabel(have, e), true, owner});
            }
            // Mémorisés mais introuvables : ils gardent leur ligne, marquée.
            for (std::size_t w = 0; w < conf.size(); ++w)
                if (pick[w] < 0) rows.push_back({conf[w].name, conf[w].uid, conf[w].name, false, int(w)});
            return rows;
        };

        // ⚠ L'identifiant ImGui d'une ligne est son INDEX, jamais son nom : deux lignes
        // homonymes partageraient sinon le même ID et leurs cases se piloteraient l'une
        // l'autre. Deux appareils du même MODÈLE donnent exactement ce cas.
        int rowId = 0;

        // Rangée de 16 canaux. Boutons plutôt que cases à cocher : à cette densité
        // c'est le NUMÉRO qu'on cherche des yeux, pas la case.
        auto channelRow = [](uint16_t& mask) {
            bool changed = false;
            for (int ch = 0; ch < 16; ++ch) {
                ImGui::PushID(ch);
                const bool on = ((mask >> ch) & 1) != 0;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.28f, 1.f));
                char lbl[8]; std::snprintf(lbl, sizeof lbl, "%d", ch + 1);
                if (ImGui::Button(lbl, ImVec2(26, 0))) {
                    mask = uint16_t(mask ^ (1u << ch)); changed = true;
                }
                if (on) ImGui::PopStyleColor();
                ImGui::PopID();
                if (ch < 15) ImGui::SameLine(0.f, 2.f);
            }
            ImGui::SameLine(0.f, 10.f);
            if (ImGui::SmallButton("all"))  { mask = 0xFFFF; changed = true; }
            ImGui::SameLine(0.f, 4.f);
            if (ImGui::SmallButton("none")) { mask = 0;      changed = true; }
            return changed;
        };

        // (a bis) DESTINATIONS MATÉRIELLES — un AIGUILLAGE. Le port virtuel ci-dessus
        // est PASSIF : il attend qu'un logiciel s'y abonne, et un expandeur ne
        // s'abonne à rien. Ici, chaque appareil reçoit les canaux qu'on lui donne :
        // « instrument 1 de Cubase vers le piano logiciel, instrument 2 vers la
        // groovebox », sans toucher au réglage des appareils eux-mêmes.
        // ⚠ Gardé par destinationsAvailable(), PAS par portAvailable() : Windows n'a
        // pas de port virtuel (il y faut un pilote tiers) mais pilote parfaitement
        // les appareils branchés. L'ancienne garde cachait, sous Windows, la seule
        // moitié qui marche — la page MIDI y était vide de bout en bout.
        if (MidiOutHost::destinationsAvailable()) {
            ImGui::TextDisabled("Hardware devices - which channels go where");
            std::vector<neost::midi::Wanted> conf;
            for (const auto& d : cfg.midiOutDevices) conf.push_back({d.name, d.uid});
            std::vector<Config::MidiOutDev> next;      // l'état voulu, reconstruit
            bool changed = false;
            for (const auto& r : buildRows(ui.midiOutDevs, conf)) {
                const bool on = r.cfgIdx >= 0;
                uint16_t mask = on ? cfg.midiOutDevices[std::size_t(r.cfgIdx)].channels : uint16_t(0xFFFF);
                bool live = false;
                for (const auto& d : ui.midiOutOpen) if (d.name == r.name && d.uid == r.uid) { live = true; break; }

                ImGui::PushID(rowId++);
                bool want = on;
                if (ImGui::Checkbox("##on", &want)) changed = true;
                ImGui::SameLine();
                if (on && !live && r.plugged)  ImGui::TextDisabled("%s  (opening)", r.label.c_str());
                else if (on && !r.plugged)     ImGui::TextDisabled("%s  (not connected)", r.label.c_str());
                else                           ImGui::TextUnformatted(r.label.c_str());
                if (want) {
                    ImGui::Indent(24.f);
                    if (channelRow(mask)) changed = true;
                    // Profil d'appareil CONNU : pose le masque d'un clic au lieu de
                    // seize. L'infobulle donne le plan complet — pour un Circuit
                    // Tracks, savoir que les 4 drums partagent le canal 10 et se
                    // distinguent par la NOTE est ce qui manque au moment de
                    // séquencer, pas au moment de câbler.
                    if (const auto* prof = neost::midi::profileFor(r.name)) {
                        if (ImGui::SmallButton(prof->label)) { mask = prof->channels; changed = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", prof->detail);
                    }
                    ImGui::Unindent(24.f);
                    next.push_back({r.name, mask, r.uid});
                }
                ImGui::PopID();
            }
            if (changed) { ui.reqMidiOut = next; ui.reqMidiIn = cfg.midiInDevices; ui.midiDevsDirty = true; }
            if (ui.midiOutDevs.empty() && cfg.midiOutDevices.empty())
                ImGui::TextDisabled("  Nothing plugged in right now.");
            else
                ImGui::TextDisabled("  System messages (clock, SysEx) always go to all.");
            // Windows n'a AUCUNE API pour créer un port MIDI que les autres
            // applications voient : ni winmm, ni WinRT MIDI 1.0. Le dire, et dire
            // quoi installer, vaut mieux qu'une case grisée sans explication — le
            // port d'un pilote loopMIDI apparaît ensuite dans la liste ci-dessus
            // comme n'importe quel appareil, et en source côté MIDI IN.
            if (!MidiOutHost::portAvailable())
                ImGui::TextDisabled("  No virtual port on Windows: install loopMIDI,\n"
                                    "  its port then shows up in this list (and in MIDI IN).");
        }
        ImGui::Separator();

        // (b) Synthé GM intégré : DLSMusicDevice sous macOS, TinySoundFont ailleurs
        // (banque .sf2 — TimGM6mb livrée dans roms/gm/, ou une banque système).
        {
            bool gm = cfg.midiOutGm;
            if (ImGui::Checkbox("Built-in General MIDI synth", &gm)) ui.reqMidiOutGm = gm ? 1 : 0;
            if (MidiOutHost::synthAvailable())
                ImGui::TextDisabled("  Apple DLSMusicDevice, nothing to install.");
            else
                ImGui::TextDisabled("  SoundFont: %s  -  %s", cfg.gmSoundFont.c_str(),
                                    ui.gmStatus.empty() ? "(off)" : ui.gmStatus.c_str());
        }
        ImGui::Separator();

        // (c) MT-32 / CM-32L.
        if (Mt32Synth::available()) {
            bool mt = cfg.midiOutMt32;
            if (ImGui::Checkbox("Roland MT-32 / CM-32L (Munt, emulated)", &mt))
                ui.reqMidiOutMt32 = mt ? 1 : 0;
            const int cur = cfg.mt32Model == "mt32" ? 1 : cfg.mt32Model == "cm32l" ? 2 : 0;
            ImGui::TextDisabled("  Model:"); ImGui::SameLine();
            if (ImGui::RadioButton("Auto", cur == 0))   ui.reqMt32Model = 0; ImGui::SameLine();
            if (ImGui::RadioButton("MT-32", cur == 1))  ui.reqMt32Model = 1; ImGui::SameLine();
            if (ImGui::RadioButton("CM-32L", cur == 2)) ui.reqMt32Model = 2;
            ImGui::TextDisabled("  ROMs: %s  -  %s", cfg.mt32Roms.c_str(),
                                ui.mt32Status.empty() ? "(off)" : ui.mt32Status.c_str());
            ImGui::TextDisabled("  Auto = CM-32L if its ROMs are there.");
            ImGui::TextDisabled("  For era patches (Cubase .ALL, 1991).");
            ImGui::TextColored(ImVec4(1.f, .7f, .35f, 1.f),
                               "  GM files play WRONG here: LA map != GM map.");
        } else {
            ImGui::TextDisabled("Roland MT-32 / CM-32L: built without libmt32emu.");
        }
        ImGui::Separator();

        // MIDI IN : la source matérielle, puis la fiche de bouclage (jusqu'ici cachée
        // dans le menu Machine).
        // Avance de livraison : l'arbitrage gigue/latence, rendu à l'utilisateur.
        // Le témoin d'octets en retard est ce qui rend le réglage utilisable — sans
        // lui, on baisse à l'aveugle jusqu'à ce que « ça sonne bizarre ».
        {
            int lead = cfg.midiLeadMs;
            ImGui::SetNextItemWidth(220.f);
            if (ImGui::SliderInt("Output lead (ms)", &lead, 0, 100)) ui.reqMidiLead = lead;
            if (ui.midiLateBytes)
                ImGui::TextColored(ImVec4(1.f, .7f, .35f, 1.f),
                                   "  %llu bytes sent late - raise the lead.",
                                   (unsigned long long)ui.midiLateBytes);
            else
                ImGui::TextDisabled("  Lower = more direct to play, less slack for a"
                                    " GUI hiccup.");
        }
        ImGui::Separator();

        ImGui::TextDisabled("MIDI IN of the ST (ACIA 6850)");
        // FUSION. Le ST n'a qu'UNE prise MIDI IN : réunir plusieurs claviers dessus
        // est le rôle d'un boîtier de fusion, et NeoST en tient lieu (entrelacement
        // aux frontières de MESSAGES, cf. MidiInHost). Le canal forcé est ce qui rend
        // l'enregistrement multipiste possible : deux claviers émettant tous deux sur
        // le canal 1 seraient inséparables pour le séquenceur.
        if (MidiInHost::available()) {
            ImGui::TextDisabled("Hardware devices - merged into the one MIDI IN");
            std::vector<neost::midi::Wanted> conf;
            for (const auto& d : cfg.midiInDevices) conf.push_back({d.name, d.uid});
            std::vector<Config::MidiInDev> next;
            bool changed = false;
            for (const auto& r : buildRows(ui.midiInDevs, conf)) {
                const bool on = r.cfgIdx >= 0;
                int chan = on ? cfg.midiInDevices[std::size_t(r.cfgIdx)].channel : 0;
                bool live = false;
                for (const auto& n : ui.midiInOpen) if (n == r.name) { live = true; break; }

                ImGui::PushID(rowId++);
                bool want = on;
                if (ImGui::Checkbox("##on", &want)) changed = true;
                ImGui::SameLine();
                if (on && !live && r.plugged)  ImGui::TextDisabled("%s  (opening)", r.label.c_str());
                else if (on && !r.plugged)     ImGui::TextDisabled("%s  (not connected)", r.label.c_str());
                else                           ImGui::TextUnformatted(r.label.c_str());
                if (want) {
                    ImGui::SameLine();
                    char cur[24];
                    if (chan) std::snprintf(cur, sizeof cur, "channel %d", chan);
                    else      std::snprintf(cur, sizeof cur, "as-is");
                    ImGui::SetNextItemWidth(130.f);
                    if (ImGui::BeginCombo("##ch", cur)) {
                        if (ImGui::Selectable("as-is", chan == 0)) { chan = 0; changed = true; }
                        for (int c = 1; c <= 16; ++c) {
                            char lbl[24]; std::snprintf(lbl, sizeof lbl, "channel %d", c);
                            if (ImGui::Selectable(lbl, chan == c)) { chan = c; changed = true; }
                        }
                        ImGui::EndCombo();
                    }
                    next.push_back({r.name, chan, r.uid});
                }
                ImGui::PopID();
            }
            if (changed) { ui.reqMidiIn = next; ui.reqMidiOut = cfg.midiOutDevices; ui.midiDevsDirty = true; }
            if (ui.midiInDevs.empty() && cfg.midiInDevices.empty())
                ImGui::TextDisabled("  Nothing plugged in right now.");
            else if (!ui.midiInOpen.empty())
                // Preuve de vie : sans ce compteur, « rien ne se passe » ne dit pas si
                // le câble est muet ou si c'est le programme ST qui n'écoute pas.
                ImGui::TextDisabled("  %llu bytes into the ST so far. Force a channel per"
                                    " device to record them on separate tracks.",
                                    (unsigned long long)ui.midiInBytes);
            else
                ImGui::TextDisabled("  Play them: the ST sees one merged MIDI cable.");
        }
        ImGui::Spacing();
        bool loop = cfg.midiLoopback;   // cfg est CONST ici : on passe par une requête
        if (ImGui::Checkbox("Loopback cable OUT->IN", &loop)) ui.reqMidiLoopback = loop ? 1 : 0;
        ImGui::TextDisabled("  A real ST has none. Cubase MIDI Thru = feedback.");
        ImGui::Separator();

        ImGui::TextDisabled("Steinberg key (Cubase 2/3): see the Dongles page.");
        ImGui::Separator();

        // Panique : indispensable dès qu'on coupe une sortie en plein accord.
        if (ImGui::Button("Panic - all notes off")) ui.reqMidiPanic = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(CC 120/121/123 on the 16 channels)");
        ImGui::TextWrapped("A synth never releases a note by itself: stop a program "
                           "mid-chord and the notes hang until told otherwise.");
        break;
    }
    case kCfgInput: {
        // Modèle PHYSIQUE des deux ports DE-9 : on choisit ce qu'on y BRANCHE. Port 0 =
        // port souris (la souris par défaut ; un joystick l'y remplace, comme sur un vrai
        // ST) ; port 1 = port jeux (la 1re manette par défaut). Les choix s'expriment
        // sur le mécanisme existant (rôles par GUID — joymap —, émulation clavier,
        // port0=) : la page Joystick et le menu borne voient la même chose.
        ImGui::TextDisabled("WHAT IS PLUGGED INTO THE TWO JOYSTICK PORTS");
        int8_t roles[GLFW_JOYSTICK_LAST + 1]; joyResolveRoles(A, roles);
        int8_t assign[GLFW_JOYSTICK_LAST + 1]; stjoy::resolveAssign(roles, assign, A.port0Auto);
        auto padName = [](int jid) { const char* n = glfwGetGamepadName(jid); if (!n) n = glfwGetJoystickName(jid); return n ? n : "?"; };
        auto unpin = [&](int port) {   // toute manette épinglée sur `port` repasse en AUTO
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid)
                if (glfwJoystickPresent(jid) && roles[jid] == port) A.joyRoleByGuid.erase(joyGuid(jid));
        };
        for (int port = 0; port < 2; ++port) {
            // Valeur courante : manette ÉPINGLÉE > clavier > auto/souris.
            int pinnedJid = -1, autoJid = -1;
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                if (!glfwJoystickPresent(jid)) continue;
                if (roles[jid] == port) pinnedJid = jid;
                else if (assign[jid] == port && roles[jid] == stjoy::ROLE_AUTO) autoJid = jid;
            }
            const bool kbdHere = A.kbdJoy && A.kbdJoyPort == port;
            char cur[160];
            if (pinnedJid >= 0)      std::snprintf(cur, sizeof cur, "Pad: %s", padName(pinnedJid));
            else if (kbdHere)        std::snprintf(cur, sizeof cur, "Keyboard joystick (arrows + right Ctrl)");
            else if (port == 0)      std::snprintf(cur, sizeof cur, A.port0Auto ? (autoJid >= 0 ? "Auto: 2nd pad (%s)" : "Auto: 2nd pad (none yet)") : "Mouse", autoJid >= 0 ? padName(autoJid) : "");
            else                     std::snprintf(cur, sizeof cur, autoJid >= 0 ? "Auto: first pad (%s)" : "Auto: first pad (none detected)", autoJid >= 0 ? padName(autoJid) : "");
            ImGui::SetNextItemWidth(320);
            if (ImGui::BeginCombo(port == 0 ? "Port 0 (mouse port)" : "Port 1 (joystick port)", cur)) {
                if (port == 0) {
                    if (ImGui::Selectable("Mouse", pinnedJid < 0 && !kbdHere && !A.port0Auto)) {
                        unpin(0); A.port0Auto = false; if (kbdHere) A.kbdJoyPort = 1; A.joyCfgDirty = true;
                    }
                    if (ImGui::Selectable("Auto: 2nd pad takes the mouse port", pinnedJid < 0 && !kbdHere && A.port0Auto)) {
                        unpin(0); A.port0Auto = true; if (kbdHere) A.kbdJoyPort = 1; A.joyCfgDirty = true;
                    }
                } else {
                    if (ImGui::Selectable("Auto: first free pad", pinnedJid < 0 && !kbdHere)) {
                        unpin(1); if (kbdHere) A.kbdJoyPort = 0; A.joyCfgDirty = true;
                    }
                }
                if (ImGui::Selectable("Keyboard joystick (arrows + right Ctrl)", kbdHere && pinnedJid < 0)) {
                    unpin(port); A.kbdJoy = true; A.kbdJoyPort = port; A.joyCfgDirty = true;
                }
                for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                    if (!glfwJoystickPresent(jid)) continue;
                    char lab[200]; std::snprintf(lab, sizeof lab, "Pad: %s##%d", padName(jid), jid);
                    if (ImGui::Selectable(lab, pinnedJid == jid)) {
                        unpin(port);                                  // un seul épinglé par port
                        A.joyRoleByGuid[joyGuid(jid)] = int8_t(port); // cette manette, ici
                        if (kbdHere) A.kbdJoyPort = 1 - port;         // le clavier cède la place
                        A.joyCfgDirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            // Clé de protection branchée sur ce port (page Dongles) : elle s'ajoute.
            const auto key = ui.machine->ports.at(port == 0 ? PortDevices::Port::Joy0 : PortDevices::Port::Joy1);
            if (key != PortDevices::Device::None) { ImGui::SameLine(); ImGui::TextDisabled("+ %s (Dongles page)", PortDevices::label(key)); }
        }
        if (A.port0Joystick) ImGui::TextDisabled("  A joystick occupies port 0: the host mouse is unplugged from the ST.");
        ImGui::TextDisabled("  Two players: put a pad on port 0 (games disable the mouse themselves).");
        ImGui::Separator();
        if (ImGui::Checkbox("Keyboard joystick emulation active", &A.kbdJoy)) A.joyCfgDirty = true;
        ImGui::SameLine(); ImGui::TextDisabled("(F11 - session only: it swallows the arrow keys)");
        ImGui::Separator();
        // Zone morte centrale des sticks analogiques (anti-drift). Le D-pad numérique
        // n'est pas concerné. Mémorisée à la validation du slider.
        ImGui::TextDisabled("Analog stick dead zone");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("##deadzone", &A.joyDeadzone, 0.0f, 0.95f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            A.joyDeadzone = A.joyDeadzone < 0.0f ? 0.0f : (A.joyDeadzone > 0.95f ? 0.95f : A.joyDeadzone);
            A.joyCfgDirty = true;
        }
        ImGui::Separator();
        // Sensibilité de la souris ÉMULÉE. La souris du ST est mécanique (~200 points
        // par pouce) et le TOS n'offre aucun réglage d'accélération utilisable : une
        // souris moderne à 1600, 3200 ou 8000 dpi envoie jusqu'à 40 fois plus de pas
        // pour le même geste, et le pointeur GEM traverse l'écran au moindre mouvement.
        // Ce curseur divise le delta hôte AVANT l'IKBD (cf. App::mouseSpeed).
        if (ImGui::Checkbox("Desktop mouse (GEM, no capture)", &A.desktopMouse)) {
            A.desktopPointer.reset();
            A.machine->ikbd.mouseEvent(0, 0, false, false);
            ui.cfgDirty = true;
        }
        ImGui::TextDisabled("Point inside the ST image. Middle-click still toggles relative capture.");
        ImGui::TextDisabled("Desktop mode needs an initialized GEM mouse driver; games use capture.");
        ImGui::TextDisabled("Emulated mouse speed (relative capture)");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("##mousespeed", &A.mouseSpeed, 0.05f, 4.0f, "%.2f x",
                           ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            A.mouseSpeed = A.mouseSpeed < 0.05f ? 0.05f : (A.mouseSpeed > 4.0f ? 4.0f : A.mouseSpeed);
            ui.cfgDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##mousespeed")) { A.mouseSpeed = 1.0f; ui.cfgDirty = true; }
        ImGui::TextDisabled("1.00 x = one host step per ST step. Lower it for a high-dpi mouse.");
        ImGui::Separator();
        ImGui::TextDisabled("USB pads detected (effective assignment):");
        int nPad = 0;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (!glfwJoystickPresent(jid)) continue;
            const char* how = roles[jid] == stjoy::ROLE_OFF ? " (off)" : roles[jid] == stjoy::ROLE_AUTO ? " (auto)" : " (pinned)";
            if (assign[jid] >= 0) ImGui::BulletText("Port %d: %s%s", assign[jid], padName(jid), how);
            else                  ImGui::BulletText("Unused: %s%s", padName(jid), how);
            ++nPad;
        }
        if (nPad == 0) ImGui::BulletText("(none)");
        break;
    }
    case kCfgEmul: {
        ImGui::TextDisabled("Floppy access speed");
        bool fast = cfg.fastfdc;
        if (ImGui::Checkbox("Fast FDC (delays ÷10)", &fast)) ui.reqFastFdc = fast ? 1 : 0;
        ImGui::TextDisabled("Same as --fastfdc: loading runs at accelerated speed.");
        ImGui::TextDisabled("TURN IT OFF to compare a trace with the Hatari oracle: the");
        ImGui::TextDisabled("frame numbers no longer match between the two.");
        ImGui::Separator();
        ImGui::TextDisabled("Machine state (save-state)");
        if (ImGui::Button(ICON_FA_SAVE " Save state (F5)"))   ui.reqSaveState = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load (F7)"))  ui.reqLoadState = true;
        ImGui::TextDisabled("A state carries a fingerprint of the config: one taken on");
        ImGui::TextDisabled("another machine/ROM is refused rather than applied.");
        if (ui.machine && ui.machine->gemdos.active()) {
            ImGui::TextDisabled("Open host files must stay unchanged until restore.");
            ImGui::TextDisabled("Their contents are not rolled back by a state.");
        }
        break;
    }
    case kCfgProfiles: {
        // Un profil = une PHOTO NOMMÉE des réglages en vigueur. neost.cfg reste la
        // configuration courante (écrite toute seule à chaque changement) ; les profils
        // servent à revenir en un clic sur un attelage machine + ROM + support connu.
        // Lignes COURTES pré-découpées et boutons sur leur propre ligne, comme les
        // autres pages : ancrée sur le côté, la fenêtre est étroite — un TextWrapped y
        // débordait (le défilement horizontal de la page repousse le point de coupure)
        // et un bouton mis en SameLine sortait tout simplement du cadre.
        ImGui::TextDisabled("A named snapshot of the settings in effect:");
        ImGui::TextDisabled("machine, RAM, FPU, ROM, media, monitor, CRT,");
        ImGui::TextDisabled("sound, input. Loading one restarts the machine");
        ImGui::TextDisabled("(same single restart as \"Apply and restart\").");
        ImGui::Separator();
        // Le mode borne n'écrit RIEN sur le disque (« la borne repart identique ») :
        // les profils y sont consultables mais ni créés ni supprimés.
        const bool frozen = A.kiosk || A.kioskLaunched;
        static char nameBuf[80] = "";
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##profname", "profile name", nameBuf, sizeof nameBuf);
        const std::string clean = profileFileName(nameBuf);
        ImGui::BeginDisabled(clean.empty() || frozen);
        if (ImGui::Button(ICON_FA_SAVE " Save current settings")) {
            ui.reqSaveProfile = clean;
            nameBuf[0] = '\0';
        }
        ImGui::EndDisabled();
        if (frozen) ImGui::TextDisabled("(kiosk: frozen configuration, nothing is written)");
        ImGui::Separator();

        // Scan du dossier MIS EN CACHE : la fenêtre est redessinée à chaque trame, et
        // la page Disquettes a déjà payé le prix d'un parcours de dossier par trame
        // (21 ms, le budget PAL entier). Rafraîchi toutes les 2 s, et immédiatement
        // après une écriture ou une suppression (A.profilesDirty).
        struct ProfCache { std::vector<std::string> names; std::string dir; double t = -1.0; };
        static ProfCache pc;
        const double nowP = ImGui::GetTime();
        if (A.profilesDirty || pc.dir != ui.profDir || pc.t < 0.0 || (nowP - pc.t) > 2.0) {
            pc.names = listProfiles(ui.profDir);
            pc.dir   = ui.profDir;
            pc.t     = nowP;
            A.profilesDirty = false;
        }
        if (pc.names.empty()) ImGui::TextDisabled("(no profile saved yet)");
        // Suppression en DEUX temps : un profil est le fruit d'un réglage patient, et
        // les boutons sont côte à côte dans une fenêtre étroite.
        static std::string confirmDel;
        for (const std::string& n : pc.names) {
            ImGui::PushID(n.c_str());
            if (ImGui::SmallButton("Load")) ui.reqLoadProfile = n;
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::BeginDisabled(frozen);
            if (confirmDel == n) {
                if (ImGui::SmallButton("Delete?")) { ui.reqDeleteProfile = n; confirmDel.clear(); }
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::SmallButton("Cancel")) confirmDel.clear();
            } else {
                if (ImGui::SmallButton("Overwrite")) ui.reqSaveProfile = n;
                ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::SmallButton("Delete")) confirmDel = n;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted(n.c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Files: profiles/*.cfg, next to neost.cfg.");
        ImGui::TextDisabled("Debug windows / docking layout are NOT saved.");
        ImGui::TextDisabled("Audio latency only applies at the next launch.");
        break;
    }
    case kCfgKiosk: {
        ImGui::TextWrapped("Kiosk mode goes full screen with no chrome, freezes the "
                           "configuration (nothing more is written to neost.cfg) and "
                           "enables keyboard joystick emulation. Leave it with Alt+Enter.");
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DESKTOP " Switch to kiosk mode (Ctrl+Alt+F)")) ui.reqKiosk = true;
        break;
    }
    default: break;
    }
    ImGui::EndChild();

    // ── Pied de page : réglages matériels en attente ──────────────────────
    int pending = 0;
    if (ui.pendMachine != cfg.machine) ++pending;
    if (ui.pendMem     != cfg.mem)     ++pending;
    if (ui.pendFpu     != cfg.fpu)     ++pending;
    if (fs::path(ui.pendRom).filename() != fs::path(cfg.rom).filename()) ++pending;
    if (pending > 0) {
        // Texte PUIS boutons sur leur propre ligne : ancrée sur le côté, la fenêtre
        // est étroite et « Appliquer et redémarrer » sortait du cadre.
        ImGui::TextColored(ImVec4(1.f, .6f, .2f, 1.f), ICON_FA_REDO " %d pending hardware setting%s",
                           pending, pending > 1 ? "s" : "");
        if (ImGui::Button("Cancel")) ui.pendInit = false;       // resème depuis cfg
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_POWER_OFF " Apply and restart")) ui.reqApply = true;
    } else {
        ImGui::TextDisabled("Machine up to date. Model, RAM, FPU and ROM are applied");
        ImGui::TextDisabled("together, by a single restart.");
    }
    ImGui::End();
}
