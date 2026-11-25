// =============================================================================
//  neost_libretro.cpp — Le cœur libretro de NeoST : un cinquième frontend.
//
//  N'est qu'un ADAPTATEUR : il traduit l'API libretro vers `Machine` et ne touche
//  à rien d'intérieur au cœur (docs/LIBRETRO.md, « Principe »). Les règles qu'il
//  suit sont celles de docs/LIBRETRO.md, « Règles de l'adaptateur » et « L'image :
//  zoom adaptatif et shaders » ; ce fichier les cite par leur titre.
//
//  Ce que l'utilisateur voit :
//   · le zoom adaptatif de la borne, par rognage de la trame et SET_GEOMETRY ;
//   · un TOS choisi seul (TosChoice) parmi ceux du dossier système `neost/` ;
//   · un chargement de disquette accéléré tant que le moteur du lecteur tourne
//     (trames émulées en plus, toutes fidèles : seul le temps de l'hôte change) ;
//   · la souris du ST au stick droit, ses boutons sur L2/R2, la disquette suivante
//     d'une liste sur R3 ;
//   · des disquettes jamais écrites à leur place : une copie privée par jeu dans le
//     dossier des sauvegardes de RetroArch.
//
//  Interface et journaux en anglais ; commentaires en français (CLAUDE.md).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "libretro/libretro.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "core/AudioMix.hpp"
#include "core/Framing.hpp"
#include "core/Machine.hpp"
#include "core/MachineType.hpp"
#include "core/Pacing.hpp"
#include "core/TosChoice.hpp"
#include "io/FloppyCatalog.hpp"
#include "io/MediaScan.hpp"
#include "io/StJoyBits.hpp"
#include "util/Log.hpp"

#ifndef NEOST_VERSION_STR
#define NEOST_VERSION_STR "dev"
#endif

namespace fs = std::filesystem;

namespace {

// --- Rappels du frontend ------------------------------------------------------------
retro_environment_t        g_env = nullptr;
retro_video_refresh_t      g_video = nullptr;
retro_audio_sample_batch_t g_audioBatch = nullptr;
retro_input_poll_t         g_inputPoll = nullptr;
retro_input_state_t        g_inputState = nullptr;
retro_log_printf_t         g_log = nullptr;
retro_set_led_state_t      g_led = nullptr;
bool                       g_canDupe = false;

constexpr uint32_t kSampleRate = 48000;
// Le plus grand tampon du Shifter (geometryFor : mono 640 × 400 ; basse résolution
// bordée 416 × 276 ; moyenne 640 × 200). Une géométrie hors de ce cadre forcerait
// SET_SYSTEM_AV_INFO, qui réinitialise le pilote vidéo (docs/LIBRETRO.md, « Zoom
// adaptatif : ce que libretro permet »).
constexpr unsigned kMaxWidth = 640, kMaxHeight = 400;
// Trames émulées au plus par retro_run quand le lecteur tourne (« Fast floppy »).
constexpr int kFastFloppyFrames = 8;

void logf(retro_log_level lvl, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_log) g_log(lvl, "[NeoST] %s", buf);
    else std::fprintf(stderr, "[NeoST] %s", buf);
}

// Journaux du cœur → retro_log_printf (règle 4). Les lignes du cœur portent déjà leur
// préfixe ([fdc], [state]…) et leur fin de ligne.
void coreLogSink(const char* text, void*) {
    if (g_log) g_log(RETRO_LOG_INFO, "%s", text);
    else std::fputs(text, stderr);
}

void notify(const std::string& msg, unsigned ms = 3000, retro_log_level lvl = RETRO_LOG_INFO) {
    logf(lvl, "%s\n", msg.c_str());
    if (!g_env) return;
    unsigned ver = 0;
    if (g_env(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &ver) && ver >= 1) {
        retro_message_ext m{};
        m.msg = msg.c_str();
        m.duration = ms;
        m.priority = 1;
        m.level = lvl;
        m.target = RETRO_MESSAGE_TARGET_OSD;
        m.type = RETRO_MESSAGE_TYPE_NOTIFICATION;
        m.progress = -1;
        g_env(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &m);
    } else {
        retro_message m{msg.c_str(), ms * 60 / 1000};
        g_env(RETRO_ENVIRONMENT_SET_MESSAGE, &m);
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extOf(const std::string& path) {
    return lower(fs::path(path).extension().string());
}

// =============================================================================
//  Options du cœur (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2)
// =============================================================================
// Les fichiers TOS du dossier système `neost/`, relevés une fois : l'option « TOS »
// les propose par leur nom. Chaînes à durée de vie du processus (l'API garde les
// pointeurs).
std::vector<std::string> g_tosFiles;
std::string g_systemDir, g_saveDir;

std::string systemNeostDir() { return g_systemDir.empty() ? std::string() : (fs::path(g_systemDir) / "neost").string(); }

void scanTosFiles() {
    g_tosFiles.clear();
    const std::string dir = systemNeostDir();
    if (dir.empty()) return;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string ext = extOf(e.path().string());
        if (ext != ".img" && ext != ".rom" && ext != ".tos") continue;
        neost::TosInfo info;
        if (!neost::readTosInfo(e.path().string(), info)) continue;
        g_tosFiles.push_back(e.path().filename().string());
    }
    std::sort(g_tosFiles.begin(), g_tosFiles.end());
    // RETRO_NUM_CORE_OPTION_VALUES_MAX (128) moins « Automatic » et le terminateur.
    if (g_tosFiles.size() > 120) g_tosFiles.resize(120);
}

retro_core_option_v2_category g_categories[] = {
    {"system", "System", "Machine, operating system and memory. Changes apply at the next restart."},
    {"video", "Video", "Framing of the Atari picture."},
    {"input", "Input", "Joypad, mouse and keyboard."},
    {"media", "Media", "Floppy disks."},
    {nullptr, nullptr, nullptr},
};

std::vector<retro_core_option_v2_definition> g_defs;

void buildOptions() {
    g_defs.clear();
    auto def = [](const char* key, const char* desc, const char* info, const char* cat,
                  std::initializer_list<retro_core_option_value> vals, const char* dflt) {
        retro_core_option_v2_definition d{};
        d.key = key; d.desc = desc; d.desc_categorized = nullptr;
        d.info = info; d.info_categorized = nullptr; d.category_key = cat;
        std::size_t i = 0;
        for (const auto& v : vals) { if (i + 1 < RETRO_NUM_CORE_OPTION_VALUES_MAX) d.values[i++] = v; }
        d.values[i] = {nullptr, nullptr};
        d.default_value = dflt;
        return d;
    };
    g_defs.push_back(def("neost_machine", "Machine (restart)",
        "Automatic: an STE for content tagged (STE), otherwise the model the chosen TOS runs, an ST by default.",
        "system",
        {{"auto", "Automatic"}, {"st", "520/1040 ST"}, {"megast", "Mega ST"},
         {"ste", "STE"}, {"megaste", "Mega STE"}}, "auto"));
    {
        retro_core_option_v2_definition d = def("neost_tos", "TOS (restart)",
            "Images found in the system folder, subfolder neost. Automatic: the TOS the machine shipped with when present "
            "(PAL before NTSC), otherwise EmuTOS.",
            "system", {{"auto", "Automatic"}}, "auto");
        std::size_t i = 1;
        for (const auto& f : g_tosFiles) {
            if (i + 1 >= RETRO_NUM_CORE_OPTION_VALUES_MAX) break;
            d.values[i++] = {f.c_str(), nullptr};
        }
        d.values[i] = {nullptr, nullptr};
        g_defs.push_back(d);
    }
    g_defs.push_back(def("neost_ram", "Memory (restart)", "ST-RAM size.", "system",
        {{"512k", "512 KB"}, {"1m", "1 MB"}, {"2m", "2 MB"}, {"4m", "4 MB"}}, "1m"));
    g_defs.push_back(def("neost_monitor", "Monitor (restart)",
        "Colour (SC1224) for games, monochrome (SM124) for high-resolution software.", "system",
        {{"color", "Colour"}, {"mono", "Monochrome"}}, "color"));
    g_defs.push_back(def("neost_clock", "Clock (restart)",
        "Host time, or 1 January 2026 at noon for runs that must repeat exactly.", "system",
        {{"host", "Host time"}, {"fixed", "Fixed"}}, "host"));
    g_defs.push_back(def("neost_framing", "Framing",
        "Adaptive: the active screen fills the picture, and opens to the borders when a demo draws in them. "
        "Active area: never the borders. Full frame: always the borders.",
        "video", {{"adaptive", "Adaptive"}, {"active", "Active area"}, {"full", "Full frame"}}, "adaptive"));
    g_defs.push_back(def("neost_pad_a", "Button A",
        "Many games jump with the joystick pushed up.", "input",
        {{"up", "Joystick up (jump)"}, {"space", "Space"}, {"fire", "Fire"}}, "up"));
    g_defs.push_back(def("neost_mouse_stick", "Mouse on the right stick",
        "The right analog stick moves the Atari mouse; L2 and R2 are its buttons.", "input",
        {{"enabled", "Enabled"}, {"disabled", "Disabled"}}, "enabled"));
    g_defs.push_back(def("neost_mouse_speed", "Mouse speed", nullptr, "input",
        {{"50", "50 %"}, {"75", "75 %"}, {"100", "100 %"}, {"150", "150 %"}, {"200", "200 %"}, {"300", "300 %"}},
        "100"));
    g_defs.push_back(def("neost_autorun", "Start the program (restart)",
        "A floppy that does not boot by itself but holds one game program: start it from the desktop, "
        "as a double-click would.",
        "media", {{"enabled", "Enabled"}, {"disabled", "Disabled"}}, "enabled"));
    g_defs.push_back(def("neost_fast_floppy", "Fast floppy loading",
        "While the drive motor turns, run up to 8 Atari frames per host frame. The emulation stays exact: "
        "only the waiting is shorter.",
        "media", {{"enabled", "Enabled"}, {"disabled", "Disabled"}}, "enabled"));
    retro_core_option_v2_definition end{};
    g_defs.push_back(end);
}

std::string optionValue(const char* key, const char* dflt) {
    retro_variable v{key, nullptr};
    if (g_env && g_env(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) return v.value;
    return dflt;
}

struct RuntimeOptions {
    enum class Framing { Adaptive, Active, Full } framing = Framing::Adaptive;
    enum class PadA { Up, Space, Fire } padA = PadA::Up;
    bool mouseStick = true;
    int mouseSpeed = 100;
    bool fastFloppy = true;
} g_opt;

void readRuntimeOptions() {
    const std::string f = optionValue("neost_framing", "adaptive");
    g_opt.framing = f == "active" ? RuntimeOptions::Framing::Active
                  : f == "full"   ? RuntimeOptions::Framing::Full
                                  : RuntimeOptions::Framing::Adaptive;
    const std::string a = optionValue("neost_pad_a", "up");
    g_opt.padA = a == "space" ? RuntimeOptions::PadA::Space
               : a == "fire"  ? RuntimeOptions::PadA::Fire
                              : RuntimeOptions::PadA::Up;
    g_opt.mouseStick = optionValue("neost_mouse_stick", "enabled") != "disabled";
    g_opt.mouseSpeed = std::max(25, std::min(400, std::atoi(optionValue("neost_mouse_speed", "100").c_str())));
    g_opt.fastFloppy = optionValue("neost_fast_floppy", "enabled") != "disabled";
}

// =============================================================================
//  La session
// =============================================================================
struct Disk { std::string path; std::string label; };

struct Session {
    std::unique_ptr<Machine> machine;
    neost::FramingLatch latch;
    neost::pacing::AvTracker av;
    neost::pacing::AudioPacer pacer;
    neost::FrameMixBuffers mix;
    std::vector<int16_t> pcm;
    float aspect = 4.0f / 3.0f;            // dernier rapport d'aspect annoncé
    int rx = 0, ry = 0, rw = 0, rh = 0;    // dernier cadre envoyé (dans le tampon)
    std::vector<Disk> playlist;
    unsigned diskIndex = 0;
    bool ejected = false;
    std::string tosPath;
    std::vector<uint8_t> state;            // tampon de sérialisation réutilisé
    std::size_t stateBound = 0;            // taille annoncée par retro_serialize_size
    uint32_t padPrev[2] = {0, 0};          // boutons de la trame précédente (fronts)
    bool mouseL = false, mouseR = false;
    double mouseFracX = 0, mouseFracY = 0;
    bool ledOn = false;
    bool loaded = false;
    // Cadence annoncée au frontend (cycles par trame). Au démarrage, la Glue tourne à
    // 60 Hz jusqu'à ce que le TOS écrive $FF820A : annoncer ce passage réinitialiserait
    // deux fois le pilote vidéo. Les changements de cadence attendent donc la fin d'une
    // grâce (trames montrées), puis seule la cadence établie est annoncée.
    int64_t announcedFc = 0;
    int timingGrace = 0;
    // Machine automatique : le contenu, et la machine imposée après un arrêt du 68000 sur
    // un périphérique absent (cf. checkMissingHardware). Un seul redémarrage par contenu.
    std::string content;
    uint32_t faultSeenAddr = 0, faultSeenPc = 0;   // dernière faute examinée
    bool machineForced = false;
    MachineType forcedMachine = MachineType::St;
} g_s;
constexpr int kTimingGraceFrames = 120;

// Copie privée d'une disquette dans le dossier des sauvegardes : l'original n'est
// jamais écrit (docs/LIBRETRO.md, « Le reste du métier »). Les écritures STX/IPF vont
// dans le `.wd1772` voisin de la copie. Sans dossier de sauvegardes, l'original est
// monté et la recopie vers le fichier hôte coupée.
std::string privateCopy(const std::string& original) {
    if (g_saveDir.empty()) return std::string();
    std::error_code ec;
    const fs::path dir = fs::path(g_saveDir) / "neost" / "disks";
    fs::create_directories(dir, ec);
    const fs::path dst = dir / fs::path(original).filename();
    if (!fs::exists(dst, ec)) {
        fs::copy_file(original, dst, ec);
        if (ec) { logf(RETRO_LOG_WARN, "cannot copy %s to %s: %s\n", original.c_str(), dst.string().c_str(),
                       ec.message().c_str()); return std::string(); }
    }
    return dst.string();
}

bool mountDisk(int drive, const std::string& original) {
    Machine& m = *g_s.machine;
    const std::string copy = privateCopy(original);
    m.fdc.setHostWriteBack(!copy.empty());
    const std::string& path = copy.empty() ? original : copy;
    if (!m.fdc.loadImage(path, drive)) {
        notify("Cannot open floppy " + fs::path(original).filename().string(), 4000, RETRO_LOG_ERROR);
        return false;
    }
    return true;
}

std::string diskLabel(unsigned i) {
    if (i >= g_s.playlist.size()) return std::string();
    const Disk& d = g_s.playlist[i];
    return d.label.empty() ? fs::path(d.path).filename().string() : d.label;
}

bool isFloppyExt(const std::string& e) {
    return e == ".st" || e == ".msa" || e == ".dim" || e == ".stx" || e == ".ipf";
}
bool isProgramExt(const std::string& e) {
    return e == ".prg" || e == ".tos" || e == ".ttp" || e == ".app" || e == ".gtp";
}

// Liste .m3u : un chemin par ligne, relatif au dossier de la liste ; « chemin|nom » donne
// un libellé (comme POM2) ; # commente.
// Les autres disquettes du même jeu, dans le même dossier (« Disk 1 of 3 », faces a/b :
// MediaScan::areSiblingImages), triées par nom : R3 et Disc Control les trouvent sans
// liste .m3u. La disquette chargée garde sa place dans l'ordre.
std::vector<Disk> siblingDisks(const std::string& path, unsigned& index) {
    std::vector<std::string> names;
    const fs::path p(path);
    const std::string self = lower(p.filename().string());
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(p.parent_path(), ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = lower(e.path().filename().string());
        if (!isFloppyExt(extOf(n))) continue;
        if (n == self || neost::areSiblingImages(self, n)) names.push_back(e.path().string());
        if (names.size() > 16) break;
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });
    std::vector<Disk> out;
    index = 0;
    for (const auto& n : names) {
        if (lower(fs::path(n).filename().string()) == self) index = unsigned(out.size());
        out.push_back(Disk{n, std::string()});
    }
    if (out.empty()) out.push_back(Disk{path, std::string()});
    return out;
}

std::vector<Disk> readM3u(const std::string& path) {
    std::vector<Disk> out;
    std::ifstream in(path);
    std::string line;
    const fs::path base = fs::path(path).parent_path();
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        Disk d;
        const auto bar = line.find('|');
        d.path = line.substr(0, bar);
        if (bar != std::string::npos) d.label = line.substr(bar + 1);
        fs::path p(d.path);
        if (p.is_relative()) p = base / p;
        d.path = p.lexically_normal().string();
        out.push_back(d);
    }
    return out;
}

// --- Choix de la machine et du TOS -------------------------------------------------------
const char* machineLabel(MachineType t);
bool parseMachine(const std::string& v, MachineType& out) {
    if (v == "st") { out = MachineType::St; return true; }
    if (v == "megast") { out = MachineType::MegaSt; return true; }
    if (v == "ste") { out = MachineType::Ste; return true; }
    if (v == "megaste") { out = MachineType::MegaSte; return true; }
    return false;
}

// Les noms TOSEC marquent les titres STE : « (STe) », « [STE] ».
bool contentWantsSte(const std::string& path) {
    const std::string n = lower(fs::path(path).filename().string());
    return n.find("(ste)") != std::string::npos || n.find("[ste]") != std::string::npos
        || n.find("(ste ") != std::string::npos || n.find("ste only") != std::string::npos;
}

std::size_t parseRam(const std::string& v) {
    if (v == "512k") return 512u * 1024u;
    if (v == "2m") return 2u * 1024u * 1024u;
    if (v == "4m") return 4u * 1024u * 1024u;
    return 1024u * 1024u;
}

// TOS et machine de la session. Faux (avec un message) si aucun TOS n'est utilisable.
bool chooseTos(const std::string& contentPath, std::string& tosPath, MachineType& machine) {
    const std::string dir = systemNeostDir();
    std::vector<std::string> files;
    std::vector<neost::TosInfo> infos;
    for (const auto& f : g_tosFiles) {
        neost::TosInfo t;
        const std::string p = (fs::path(dir) / f).string();
        if (neost::readTosInfo(p, t)) { files.push_back(p); infos.push_back(t); }
    }
    if (files.empty()) {
        notify("No TOS found: put EmuTOS (etos192us.img, etos256us.img…) in the system folder, subfolder neost",
               8000, RETRO_LOG_ERROR);
        return false;
    }
    const std::string mOpt = optionValue("neost_machine", "auto");
    const std::string tOpt = optionValue("neost_tos", "auto");
    bool autoMachine = !parseMachine(mOpt, machine);
    if (autoMachine && g_s.machineForced) { machine = g_s.forcedMachine; autoMachine = false; }

    if (tOpt != "auto") {
        const auto it = std::find(g_tosFiles.begin(), g_tosFiles.end(), tOpt);
        if (it != g_tosFiles.end()) {
            tosPath = (fs::path(dir) / tOpt).string();
            neost::TosInfo t;
            if (autoMachine && neost::readTosInfo(tosPath, t))
                machine = neost::pickMachineForTos(contentWantsSte(contentPath) ? MachineType::Ste
                                                                                 : MachineType::St, t);
            return true;
        }
        notify("TOS " + tOpt + " not found, choosing one", 4000, RETRO_LOG_WARN);
    }
    if (autoMachine) machine = contentWantsSte(contentPath) ? MachineType::Ste : MachineType::St;
    // Un TOS Atari PAL de préférence : la plupart des jeux ST visent le 50 Hz. Le pays
    // reste libre (country = -1).
    neost::TosInfo want;
    want.emuTos = false;
    want.ntsc = false;
    int i = neost::pickTosForMachine(machine, &want, infos);
    if (i < 0 && autoMachine) {
        // Rien pour un STE : un ST plutôt que rien (le titre le dira s'il exige un STE).
        machine = MachineType::St;
        i = neost::pickTosForMachine(machine, &want, infos);
    }
    if (i < 0) {
        notify(std::string("No TOS in the neost folder runs an ") + machineLabel(machine), 8000, RETRO_LOG_ERROR);
        return false;
    }
    tosPath = files[std::size_t(i)];
    return true;
}

const char* machineLabel(MachineType t) {
    switch (t) {
    case MachineType::St: return "Atari ST";
    case MachineType::MegaSt: return "Mega ST";
    case MachineType::Ste: return "Atari STE";
    case MachineType::MegaSte: return "Mega STE";
    }
    return "Atari ST";
}

// « TOS 1.04 UK, 50 Hz », « EmuTOS FR, 50 Hz » : ce qui décide du clavier et de la cadence.
std::string tosLabel(const std::string& path) {
    neost::TosInfo t;
    if (!neost::readTosInfo(path, t)) return fs::path(path).filename().string();
    static const char* const kCountry[] = {"US", "DE", "FR", "UK", "ES", "IT", "SE", "CH", "CH", "TR",
                                           "FI", "NO", "DK", "SA", "NL", "CZ", "HU"};
    std::string s;
    if (t.emuTos) s = "EmuTOS";
    else {
        char buf[32];
        std::snprintf(buf, sizeof buf, "TOS %X.%02X", t.ver >> 8, t.ver & 0xFF);
        s = buf;
    }
    if (t.country >= 0 && t.country < int(sizeof kCountry / sizeof kCountry[0])) s += std::string(" ") + kCountry[t.country];
    s += t.ntsc ? ", 60 Hz" : ", 50 Hz";
    return s;
}

// =============================================================================
//  Image : cadre, géométrie, aspect
// =============================================================================
struct Rect { int x, y, w, h; };

// Cadre de la trame selon l'option « Framing » (docs/LIBRETRO.md, « Zoom adaptatif ».
// L'hystérésis de stContentRegion se compte en trames : on ne l'appelle QUE sur les
// trames montrées (`shown`).
Rect frameRect(bool shown) {
    const Shifter& sh = g_s.machine->shifter;
    const int w = sh.width(), h = sh.height();
    Rect r{0, 0, w, h};
    switch (g_opt.framing) {
    case RuntimeOptions::Framing::Full:
        if (shown) { int t, ch, cw; neost::stContentRegion(sh, g_s.latch, t, ch, cw); }   // latch tenu à jour
        break;
    case RuntimeOptions::Framing::Active:
        if (shown) { int t, ch, cw; neost::stContentRegion(sh, g_s.latch, t, ch, cw); }
        r = {(w - sh.activeWidth()) / 2, sh.activeTop(), sh.activeWidth(), sh.activeHeight()};
        break;
    case RuntimeOptions::Framing::Adaptive: {
        if (!shown && g_s.rw > 0) { r = {g_s.rx, g_s.ry, g_s.rw, g_s.rh}; break; }
        int cTop = 0, cH = h, cW = w;
        neost::stContentRegion(sh, g_s.latch, cTop, cH, cW);
        r = {(w - cW) / 2, cTop, cW, cH};
        break;
    }
    }
    // Bornage défensif : la région vient du Glue LIVE, une trame de transition peut la
    // donner hors du tampon courant (changement de résolution) — même garde que le bureau.
    r.w = std::max(1, std::min(r.w, w));
    r.h = std::max(1, std::min(r.h, h));
    r.x = std::max(0, std::min(r.x, w - r.w));
    r.y = std::max(0, std::min(r.y, h - r.h));
    return r;
}

// Aspect « moniteur » du cadre, la règle du bureau (src/gui/StScreenView.cpp) : un
// pixel de basse résolution vaut 2 × 2 pixels mono. Zone active basse, moyenne et mono
// donnent alors le même 1,6 — un passage du bureau GEM à un jeu ne fait pas sauter
// l'image. [F] Simplification : l'aspect d'un téléviseur diffère à 50 et 60 Hz
// (TODO « Rapport d'aspect selon la cadence »).
float aspectFor(const Rect& r) {
    const Shifter& sh = g_s.machine->shifter;
    const float sx = sh.width() <= 480 ? 2.0f : 1.0f;
    const float sy = sh.height() <= 300 ? 2.0f : 1.0f;
    return (float(r.w) * sx) / (float(r.h) * sy);
}

void fillAvInfo(retro_system_av_info* info, const Rect& r) {
    info->geometry.base_width = unsigned(r.w);
    info->geometry.base_height = unsigned(r.h);
    info->geometry.max_width = kMaxWidth;
    info->geometry.max_height = kMaxHeight;
    info->geometry.aspect_ratio = aspectFor(r);
    const int64_t fc = g_s.machine->frameCycles();
    info->timing.fps = fc > 0 ? neost::pacing::cpuHz() / double(fc) : 50.0;
    info->timing.sample_rate = double(kSampleRate);
}

void presentVideo(bool shown) {
    Machine& m = *g_s.machine;
    const Rect r = frameRect(shown);
    if (!shown) {
        if (g_canDupe) g_video(nullptr, unsigned(r.w), unsigned(r.h), 0);
        else g_video(m.shifter.pixels() + std::size_t(r.y) * m.shifter.width() + r.x, unsigned(r.w),
                     unsigned(r.h), std::size_t(m.shifter.width()) * 4);
        return;
    }
    g_s.rx = r.x; g_s.ry = r.y; g_s.rw = r.w; g_s.rh = r.h;
    // Annonce après trois trames identiques (AvTracker) : un cadre franc par scène.
    // Un changement de cadence (50, 60, 71 Hz) réinitialise le pilote ; un changement de
    // cadre ne fait que recalculer la fenêtre de RetroArch.
    const unsigned change = g_s.av.onFrame(r.w, r.h, m.frameCycles());
    if (g_s.timingGrace > 0) --g_s.timingGrace;
    const int64_t settledFc = g_s.av.announced().frameCycles;
    if (g_s.timingGrace == 0 && settledFc > 0 && settledFc != g_s.announcedFc) {
        retro_system_av_info info{};
        fillAvInfo(&info, r);
        info.timing.fps = neost::pacing::cpuHz() / double(settledFc);
        g_s.aspect = info.geometry.aspect_ratio;
        g_s.announcedFc = settledFc;
        g_env(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
    } else if (change & neost::pacing::AvTracker::Geometry) {
        retro_game_geometry g{unsigned(r.w), unsigned(r.h), kMaxWidth, kMaxHeight, aspectFor(r)};
        g_s.aspect = g.aspect_ratio;
        g_env(RETRO_ENVIRONMENT_SET_GEOMETRY, &g);
    }
    // Sans copie : pointeur dans le tampon du Shifter, pitch du tampon entier.
    g_video(m.shifter.pixels() + std::size_t(r.y) * m.shifter.width() + r.x, unsigned(r.w), unsigned(r.h),
            std::size_t(m.shifter.width()) * 4);
}

// =============================================================================
//  Son
// =============================================================================
void drainAudio() {
    Machine& m = *g_s.machine;
    neost::mixEmulatedFrame(m.psg, &m.dmasnd, false, 0, 0, m.frameCycles(), g_s.mix);
}

void presentAudio(bool heard) {
    Machine& m = *g_s.machine;
    const int64_t fc = m.frameCycles();
    // Débit EXACT, sans asservissement : RetroArch rééchantillonne lui-même
    // (docs/LIBRETRO.md : AudioPacer avec coussin et file à 0).
    const int n = g_s.pacer.samplesForFrame(fc, kSampleRate, 0, 0);
    if (n <= 0) { drainAudio(); return; }
    float* st = neost::mixEmulatedFrame(m.psg, &m.dmasnd, machineHasDmaSound(m.bus.machine),
                                        uint32_t(n), kSampleRate, fc, g_s.mix);
    if (!st || !heard) return;
    g_s.pcm.resize(std::size_t(n) * 2);
    for (std::size_t i = 0; i < g_s.pcm.size(); ++i) {
        const float v = std::max(-1.0f, std::min(1.0f, st[i]));
        g_s.pcm[i] = int16_t(std::lrint(v * 32767.0f));
    }
    for (std::size_t done = 0; done < std::size_t(n);)
        done += g_audioBatch(g_s.pcm.data() + done * 2, std::size_t(n) - done);
}

// =============================================================================
//  Entrées
// =============================================================================
// RETROK (positions d'un clavier US, comme les rend RetroArch) → scancode ST. Même
// table que le GUI (src/gui/StKeys.cpp) ; les touches propres au ST suivent Hatari
// (sdl/keymap.c) : Help, Undo et les parenthèses du pavé.
uint8_t scancodeFor(unsigned k) {
    switch (k) {
    case RETROK_ESCAPE: return 0x01;
    case RETROK_1: return 0x02; case RETROK_2: return 0x03; case RETROK_3: return 0x04;
    case RETROK_4: return 0x05; case RETROK_5: return 0x06; case RETROK_6: return 0x07;
    case RETROK_7: return 0x08; case RETROK_8: return 0x09; case RETROK_9: return 0x0A;
    case RETROK_0: return 0x0B; case RETROK_MINUS: return 0x0C; case RETROK_EQUALS: return 0x0D;
    case RETROK_BACKSPACE: return 0x0E; case RETROK_TAB: return 0x0F;
    case RETROK_q: return 0x10; case RETROK_w: return 0x11; case RETROK_e: return 0x12;
    case RETROK_r: return 0x13; case RETROK_t: return 0x14; case RETROK_y: return 0x15;
    case RETROK_u: return 0x16; case RETROK_i: return 0x17; case RETROK_o: return 0x18;
    case RETROK_p: return 0x19; case RETROK_LEFTBRACKET: return 0x1A; case RETROK_RIGHTBRACKET: return 0x1B;
    case RETROK_RETURN: return 0x1C; case RETROK_LCTRL: case RETROK_RCTRL: return 0x1D;
    case RETROK_a: return 0x1E; case RETROK_s: return 0x1F; case RETROK_d: return 0x20;
    case RETROK_f: return 0x21; case RETROK_g: return 0x22; case RETROK_h: return 0x23;
    case RETROK_j: return 0x24; case RETROK_k: return 0x25; case RETROK_l: return 0x26;
    case RETROK_SEMICOLON: return 0x27; case RETROK_QUOTE: return 0x28; case RETROK_BACKQUOTE: return 0x29;
    case RETROK_LSHIFT: return 0x2A; case RETROK_BACKSLASH: return 0x2B;
    case RETROK_z: return 0x2C; case RETROK_x: return 0x2D; case RETROK_c: return 0x2E;
    case RETROK_v: return 0x2F; case RETROK_b: return 0x30; case RETROK_n: return 0x31;
    case RETROK_m: return 0x32; case RETROK_COMMA: return 0x33; case RETROK_PERIOD: return 0x34;
    case RETROK_SLASH: return 0x35; case RETROK_RSHIFT: return 0x36;
    case RETROK_LALT: case RETROK_RALT: return 0x38; case RETROK_SPACE: return 0x39;
    case RETROK_CAPSLOCK: return 0x3A;
    case RETROK_F1: return 0x3B; case RETROK_F2: return 0x3C; case RETROK_F3: return 0x3D;
    case RETROK_F4: return 0x3E; case RETROK_F5: return 0x3F; case RETROK_F6: return 0x40;
    case RETROK_F7: return 0x41; case RETROK_F8: return 0x42; case RETROK_F9: return 0x43;
    case RETROK_F10: return 0x44;
    case RETROK_HOME: return 0x47; case RETROK_UP: return 0x48; case RETROK_LEFT: return 0x4B;
    case RETROK_RIGHT: return 0x4D; case RETROK_DOWN: return 0x50;
    case RETROK_INSERT: return 0x52; case RETROK_DELETE: return 0x53;
    case RETROK_OEM_102: case RETROK_LESS: return 0x60;          // touche ISO < >
    case RETROK_HELP: case RETROK_PRINT: case RETROK_F11: return 0x62;   // Help
    case RETROK_UNDO: case RETROK_END: case RETROK_F12: case RETROK_KP_EQUALS: return 0x61;   // Undo
    case RETROK_PAGEUP: return 0x63; case RETROK_PAGEDOWN: return 0x64;   // ( et ) du pavé ST
    case RETROK_KP0: return 0x70; case RETROK_KP1: return 0x6D; case RETROK_KP2: return 0x6E;
    case RETROK_KP3: return 0x6F; case RETROK_KP4: return 0x6A; case RETROK_KP5: return 0x6B;
    case RETROK_KP6: return 0x6C; case RETROK_KP7: return 0x67; case RETROK_KP8: return 0x68;
    case RETROK_KP9: return 0x69; case RETROK_KP_PERIOD: return 0x71; case RETROK_KP_DIVIDE: return 0x65;
    case RETROK_KP_MULTIPLY: return 0x66; case RETROK_KP_MINUS: return 0x4A; case RETROK_KP_PLUS: return 0x4E;
    case RETROK_KP_ENTER: return 0x72;
    default: return 0;
    }
}

void RETRO_CALLCONV onKeyboard(bool down, unsigned keycode, uint32_t, uint16_t) {
    if (!g_s.machine) return;
    const uint8_t sc = scancodeFor(keycode);
    if (sc) g_s.machine->keyEvent(sc, down);
}

constexpr uint32_t bit(unsigned id) { return 1u << id; }

uint32_t readPad(unsigned port) {
    if (!g_inputState) return 0;
    if (g_env && g_env(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, nullptr))
        return uint32_t(g_inputState(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK)) & 0xFFFFu;
    uint32_t m = 0;
    for (unsigned id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; ++id)
        if (g_inputState(port, RETRO_DEVICE_JOYPAD, 0, id)) m |= bit(id);
    return m;
}

uint8_t joyBits(uint32_t pad, bool aIsUp, bool aIsFire) {
    uint8_t j = 0;
    if (pad & bit(RETRO_DEVICE_ID_JOYPAD_UP)) j |= stjoy::UP;
    if (pad & bit(RETRO_DEVICE_ID_JOYPAD_DOWN)) j |= stjoy::DOWN;
    if (pad & bit(RETRO_DEVICE_ID_JOYPAD_LEFT)) j |= stjoy::LEFT;
    if (pad & bit(RETRO_DEVICE_ID_JOYPAD_RIGHT)) j |= stjoy::RIGHT;
    if (pad & bit(RETRO_DEVICE_ID_JOYPAD_B)) j |= stjoy::FIRE;
    if ((pad & bit(RETRO_DEVICE_ID_JOYPAD_A)) && aIsUp) j |= stjoy::UP;
    if ((pad & bit(RETRO_DEVICE_ID_JOYPAD_A)) && aIsFire) j |= stjoy::FIRE;
    // Haut et bas ensemble n'existent pas sur un manche ST : le haut l'emporte.
    if ((j & stjoy::UP) && (j & stjoy::DOWN)) j &= uint8_t(~stjoy::DOWN);
    return j;
}

void insertNextDisk();

bool loadContent(const char* path);

// Machine automatique : un programme qui lit un périphérique absent du profil (son DMA
// du STE sur un ST…) prend une bus error, que le TOS 1.x double en arrêt du 68000 —
// fidèle, mais un écran noir (cas Stardust, docs/CASE_STUDIES.md). La faute dit ce qui
// manquait (mmioNeedsBetterMachine) : on redémarre une fois sur la machine qui le porte.
// Seulement sur un ARRÊT : un programme qui sonde le STE et rattrape la faute sur un ST
// est dans son droit.
bool checkMissingHardware() {
    Machine& m = *g_s.machine;
    if (g_s.machineForced) return false;
    const Cpu68k::Fault f = m.cpu.lastFault();
    if (!f.valid || (f.addr == g_s.faultSeenAddr && f.pc == g_s.faultSeenPc && !m.cpu.halted())) return false;
    g_s.faultSeenAddr = f.addr;
    g_s.faultSeenPc = f.pc;
    MissingHw miss{};
    if (!mmioNeedsBetterMachine(f.addr, m.machineType(), miss)) return false;
    // Deux signatures d'un programme qui ne survivra pas : le 68000 arrêté (double faute,
    // TOS Atari), ou une bus error prise par une instruction en RAM alors que le vecteur 2
    // pointe encore dans la ROM — le programme n'a pas posé de gestionnaire, la faute va
    // au TOS (EmuTOS ne s'arrête pas, il affiche son écran d'erreur). Les sondes du TOS
    // (PC en ROM) et celles d'un programme qui rattrape la faute (vecteur en RAM) passent.
    const auto& ram = m.bus.ram;
    const uint32_t vec2 = ram.size() > 11 ? (uint32_t(ram[8]) << 24 | uint32_t(ram[9]) << 16 |
                                             uint32_t(ram[10]) << 8 | ram[11]) & 0xFFFFFFu : 0;
    const bool inRom = [](uint32_t a) { return a >= 0xE00000u && a < 0xFF0000u; }(vec2);
    // `vector` n'est renseigné que pour la faute qui a doublé (Cpu68k::Fault) : 0 ou 2
    // désignent ici une bus error, 3 une erreur d'adresse, qu'aucune machine ne corrige.
    const bool unhandled = f.vector != 3 && (f.pc & 0xFFFFFFu) < ram.size() && inRom;
    logf(RETRO_LOG_DEBUG, "fault on the %s at $%06X, PC=$%06X, vector %d, handler $%06X%s\n", miss.chip,
         unsigned(f.addr & 0xFFFFFFu), unsigned(f.pc & 0xFFFFFFu), f.vector, unsigned(vec2),
         m.cpu.halted() ? ", 68000 halted" : "");
    if (!m.cpu.halted() && !unhandled) return false;
    const bool autoMachine = optionValue("neost_machine", "auto") == "auto";
    MachineType want;
    const std::string needs = miss.needs;
    if (needs == "ste") want = MachineType::Ste;
    else if (needs == "megaste") want = MachineType::MegaSte;
    else if (needs == "megast or ste") want = MachineType::MegaSt;   // même TOS que le ST
    else return false;                                               // FPU absent : rien à choisir
    char where[96];
    std::snprintf(where, sizeof where, "%s at $%06X", miss.chip, unsigned(f.addr & 0xFFFFFFu));
    if (!autoMachine) {
        notify(std::string("68000 halted: the program needs the ") + where + ". Set Machine to " + machineLabel(want),
               10000, RETRO_LOG_WARN);
        g_s.machineForced = true;   // dit une fois
        return false;
    }
    logf(RETRO_LOG_INFO, "%s on the %s: restarting as %s\n",
         m.cpu.halted() ? "68000 halted" : "unhandled bus error", where, machineLabel(want));
    g_s.machineForced = true;
    g_s.forcedMachine = want;
    const std::string content = g_s.content;
    if (!loadContent(content.empty() ? nullptr : content.c_str())) { g_s.machine.reset(); return false; }
    notify(std::string("This program needs the ") + miss.chip + ": restarted as " + machineLabel(want), 6000);
    return true;
}

// Boutons de la manette du joueur 1 qui tapent une touche du ST.
struct PadKey { unsigned id; uint8_t scancode; };
constexpr PadKey kPadKeys[] = {
    {RETRO_DEVICE_ID_JOYPAD_Y, 0x39},        // Espace
    {RETRO_DEVICE_ID_JOYPAD_X, 0x1C},        // Return
    {RETRO_DEVICE_ID_JOYPAD_START, 0x39},    // Espace : « press space to start »
    {RETRO_DEVICE_ID_JOYPAD_SELECT, 0x01},   // Esc
    {RETRO_DEVICE_ID_JOYPAD_L, 0x3B},        // F1
    {RETRO_DEVICE_ID_JOYPAD_R, 0x3C},        // F2
};

void applyInput() {
    Machine& m = *g_s.machine;
    if (g_inputPoll) g_inputPoll();
    const uint32_t p0 = readPad(0), p1 = readPad(1);
    const bool aUp = g_opt.padA == RuntimeOptions::PadA::Up;
    const bool aFire = g_opt.padA == RuntimeOptions::PadA::Fire;
    // Joueur 1 → port 1 du ST (celui des jeux) ; joueur 2 → port 0 (celui de la souris).
    m.setJoysticks(joyBits(p1, aUp, aFire), joyBits(p0, aUp, aFire));

    // Touches tapées par la manette : fronts seulement, pour ne pas écraser le clavier.
    const uint32_t changed = p0 ^ g_s.padPrev[0];
    bool spaceWas = false, spaceNow = false;
    for (const PadKey& k : kPadKeys) {
        if (k.scancode == 0x39) {   // Espace a trois sources (Y, Start, A) : OU logique
            spaceWas |= (g_s.padPrev[0] & bit(k.id)) != 0;
            spaceNow |= (p0 & bit(k.id)) != 0;
            continue;
        }
        if (changed & bit(k.id)) m.keyEvent(k.scancode, (p0 & bit(k.id)) != 0);
    }
    if (g_opt.padA == RuntimeOptions::PadA::Space) {
        spaceWas |= (g_s.padPrev[0] & bit(RETRO_DEVICE_ID_JOYPAD_A)) != 0;
        spaceNow |= (p0 & bit(RETRO_DEVICE_ID_JOYPAD_A)) != 0;
    }
    if (spaceWas != spaceNow) m.keyEvent(0x39, spaceNow);
    if ((changed & bit(RETRO_DEVICE_ID_JOYPAD_R3)) && (p0 & bit(RETRO_DEVICE_ID_JOYPAD_R3))) insertNextDisk();
    g_s.padPrev[0] = p0;
    g_s.padPrev[1] = p1;

    // Souris : celle de l'hôte, plus le stick droit, boutons gauche/droit ou L2/R2.
    double dx = g_inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    double dy = g_inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    bool l = g_inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
    bool r = g_inputState(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
    if (g_opt.mouseStick) {
        auto axis = [](int v) {
            const double a = double(v) / 32768.0;
            const double dead = 0.15;
            if (std::fabs(a) < dead) return 0.0;
            const double s = (std::fabs(a) - dead) / (1.0 - dead);
            return (a < 0 ? -1.0 : 1.0) * s * s * 8.0;   // ≤ 8 px par trame, courbe douce
        };
        dx += axis(g_inputState(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X));
        dy += axis(g_inputState(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y));
        l = l || (p0 & bit(RETRO_DEVICE_ID_JOYPAD_L2));
        r = r || (p0 & bit(RETRO_DEVICE_ID_JOYPAD_R2));
    }
    const double k = g_opt.mouseSpeed / 100.0;
    g_s.mouseFracX += dx * k;
    g_s.mouseFracY += dy * k;
    const int ix = int(g_s.mouseFracX), iy = int(g_s.mouseFracY);
    g_s.mouseFracX -= ix;
    g_s.mouseFracY -= iy;
    if (ix || iy || l != g_s.mouseL || r != g_s.mouseR) {
        m.mouseEvent(ix, iy, l, r);
        g_s.mouseL = l;
        g_s.mouseR = r;
    }
}

void setInputDescriptors() {
    static const retro_input_descriptor desc[] = {
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Joystick left"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Joystick up"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Joystick down"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick right"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Fire"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Jump (up) / option"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Space"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Return"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Space"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Esc"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "F1"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "F2"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Mouse left button"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Mouse right button"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "Next disk"},
        {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Mouse X"},
        {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Mouse Y"},
        {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Joystick 0 left"},
        {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Joystick 0 up"},
        {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Joystick 0 down"},
        {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Joystick 0 right"},
        {1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Joystick 0 fire"},
        {0, 0, 0, 0, nullptr},
    };
    g_env(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, const_cast<retro_input_descriptor*>(desc));
}

// =============================================================================
//  Disquettes : interface de changement (RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT)
// =============================================================================
bool RETRO_CALLCONV diskSetEject(bool ejected) {
    if (!g_s.machine) return false;
    if (ejected == g_s.ejected) return true;
    if (ejected) g_s.machine->fdc.eject(0);
    else if (g_s.diskIndex < g_s.playlist.size() && !mountDisk(0, g_s.playlist[g_s.diskIndex].path)) return false;
    g_s.ejected = ejected;
    return true;
}
bool RETRO_CALLCONV diskGetEject() { return g_s.ejected; }
unsigned RETRO_CALLCONV diskGetIndex() { return g_s.diskIndex; }
bool RETRO_CALLCONV diskSetIndex(unsigned i) {
    if (!g_s.ejected) return false;
    if (i > g_s.playlist.size()) return false;   // == taille : « pas de disque »
    g_s.diskIndex = i;
    return true;
}
unsigned RETRO_CALLCONV diskCount() { return unsigned(g_s.playlist.size()); }
bool RETRO_CALLCONV diskReplace(unsigned i, const retro_game_info* info) {
    if (i >= g_s.playlist.size()) return false;
    if (!info || !info->path) {
        g_s.playlist.erase(g_s.playlist.begin() + i);
        if (g_s.diskIndex > i || g_s.diskIndex >= g_s.playlist.size())
            g_s.diskIndex = g_s.diskIndex > 0 ? g_s.diskIndex - 1 : 0;
        return true;
    }
    g_s.playlist[i] = Disk{info->path, std::string()};
    return true;
}
bool RETRO_CALLCONV diskAdd() { g_s.playlist.push_back(Disk{}); return true; }
bool RETRO_CALLCONV diskSetInitial(unsigned, const char*) { return false; }
bool RETRO_CALLCONV diskGetPath(unsigned i, char* out, size_t len) {
    if (i >= g_s.playlist.size() || !out || !len) return false;
    std::snprintf(out, len, "%s", g_s.playlist[i].path.c_str());
    return true;
}
bool RETRO_CALLCONV diskGetLabel(unsigned i, char* out, size_t len) {
    if (i >= g_s.playlist.size() || !out || !len) return false;
    std::snprintf(out, len, "%s", diskLabel(i).c_str());
    return true;
}

void registerDiskControl() {
    static retro_disk_control_ext_callback ext{diskSetEject, diskGetEject, diskGetIndex, diskSetIndex, diskCount,
                                               diskReplace, diskAdd, diskSetInitial, diskGetPath, diskGetLabel};
    unsigned ver = 0;
    if (g_env(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &ver) && ver >= 1
        && g_env(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &ext))
        return;
    static retro_disk_control_callback basic{diskSetEject, diskGetEject, diskGetIndex, diskSetIndex, diskCount,
                                             diskReplace, diskAdd};
    g_env(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &basic);
}

// R3 : la disquette suivante de la liste, sans passer par le menu (comme sur la borne,
// sans reboot : on glisse une autre disquette dans le lecteur).
void insertNextDisk() {
    if (g_s.playlist.size() < 2) return;
    const unsigned next = (g_s.diskIndex + 1) % unsigned(g_s.playlist.size());
    diskSetEject(true);
    g_s.diskIndex = next;
    if (diskSetEject(false))
        notify("Drive A: " + diskLabel(next) + " (" + std::to_string(next + 1) + "/" +
               std::to_string(g_s.playlist.size()) + ")");
}

// =============================================================================
//  Chargement d'un contenu
// =============================================================================
bool loadContent(const char* path) {
    readRuntimeOptions();
    const std::string content = path ? path : "";
    const std::string ext = extOf(content);

    MachineType machine = MachineType::St;
    std::string tos;
    if (!chooseTos(content, tos, machine)) return false;
    machine = Machine::adjustMachineForTos(machine, tos);

    // Règle 1 : quartz process-global AVANT la construction, puis horloges de session.
    neost::pacing::Quartz quartz = neost::pacing::Quartz::Pal;
    if (!Machine::resolveQuartz("auto", machine, tos, quartz)) quartz = neost::pacing::Quartz::Pal;
    neost::pacing::setQuartzGlobal(quartz);
    g_s.machine = std::make_unique<Machine>(parseRam(optionValue("neost_ram", "1m")), CpuCore::Moira, machine);
    Machine& m = *g_s.machine;
    m.setClocks(quartz, 0);
    if (!m.switchTos(tos, machine)) {
        notify("Cannot load TOS " + fs::path(tos).filename().string(), 6000, RETRO_LOG_ERROR);
        g_s.machine.reset();
        return false;
    }
    m.mfp.setColorMonitor(optionValue("neost_monitor", "color") != "mono");
    m.armAudioClocks();
    if (optionValue("neost_clock", "host") == "fixed") m.freezeHostClocks();

    g_s.playlist.clear();
    g_s.diskIndex = 0;
    g_s.ejected = true;
    std::string what;
    if (ext == ".m3u") {
        g_s.playlist = readM3u(content);
        if (g_s.playlist.empty()) { notify("Empty playlist", 4000, RETRO_LOG_ERROR); return false; }
    } else if (isFloppyExt(ext)) {
        g_s.playlist = siblingDisks(content, g_s.diskIndex);
    } else if (isProgramExt(ext)) {
        // Programme : son dossier devient C: (GEMDOS) et il démarre seul.
        const fs::path p(content);
        if (!m.gemdos.setDirectory(p.parent_path().string()) || !m.gemdos.setAutoStart(p.filename().string())) {
            notify("Cannot start " + p.filename().string(), 5000, RETRO_LOG_ERROR);
            return false;
        }
        what = p.filename().string() + " on C:";
    } else if (ext == ".stc") {
        if (!m.loadCart(content)) { notify("Cannot load the cartridge", 5000, RETRO_LOG_ERROR); return false; }
        what = "cartridge " + fs::path(content).filename().string();
    } else if (!content.empty()) {
        notify("Unknown content type " + ext, 5000, RETRO_LOG_ERROR);
        return false;
    }
    if (!g_s.playlist.empty()) {
        const unsigned i = g_s.diskIndex;
        if (!mountDisk(0, g_s.playlist[i].path)) return false;
        g_s.ejected = false;
        what = "A: " + diskLabel(i);
        if (g_s.playlist.size() > 1)
            what += " (" + std::to_string(i + 1) + "/" + std::to_string(g_s.playlist.size()) + ", R3: next)";
        // Disquette qui ne s'amorce pas mais porte UN programme de jeu : le lancer depuis
        // le bureau, comme un double-clic (io/FloppyCatalog). TOS ≥ 1.04 ou EmuTOS.
        if (optionValue("neost_autorun", "enabled") != "disabled") {
            neost::FloppyCatalog cat;
            const auto reader = [&m](int lsn, uint8_t* out) { return m.fdc.readLogicalSector(0, lsn, out); };
            const bool read = neost::readFloppyCatalog(reader, cat);
            std::string list;
            for (const auto& e : cat.root) list += " " + e.name + ((e.attr & 0x10) ? "\\" : "");
            for (const auto& e : cat.autoFolder) list += " AUTO\\" + e.name;
            logf(RETRO_LOG_DEBUG, "A: %s, %s, root:%s, AUTO: %zu entries\n", read ? "catalogue read" : "no catalogue",
                 cat.bootable ? "boot sector executable" : "boot sector not executable", list.c_str(),
                 cat.autoFolder.size());
            if (read) {
                const std::string prg = neost::pickAutoStartProgram(cat, fs::path(g_s.playlist[i].path).stem().string());
                if (!prg.empty() && m.gemdos.setAutoStart(prg)) what += " · starts " + prg;
            }
        }
    }

    m.reset();
    g_s.latch.reset();
    g_s.av.reset();
    g_s.pacer.reset();
    g_s.rw = 0;
    g_s.padPrev[0] = g_s.padPrev[1] = 0;
    g_s.mouseL = g_s.mouseR = false;
    g_s.stateBound = 0;
    g_s.tosPath = tos;
    g_s.timingGrace = kTimingGraceFrames;
    g_s.faultSeenAddr = g_s.faultSeenPc = 0;
    g_s.loaded = true;

    std::string head = std::string(machineLabel(m.machineType())) + " · " + tosLabel(tos) + " · " +
                       std::to_string(m.bus.ram.size() / 1024) + " KB";
    notify(what.empty() ? head : head + " · " + what, 5000);
    return true;
}

bool loadContentOrDrop(const char* path) {
    g_s.content = path ? path : "";
    if (loadContent(path)) return true;
    g_s.machine.reset();
    g_s.playlist.clear();
    return false;
}

}  // namespace

// =============================================================================
//  API libretro
// =============================================================================
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb) {
    g_env = cb;
    bool noGame = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);
    const char* dir = nullptr;
    if (cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir) g_systemDir = dir;
    scanTosFiles();
    buildOptions();
    unsigned ver = 0;
    if (cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &ver) && ver >= 2) {
        static retro_core_options_v2 opts;
        opts.categories = g_categories;
        opts.definitions = g_defs.data();
        cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &opts);
    } else {
        // Frontend ancien : les mêmes options, à plat (RETRO_ENVIRONMENT_SET_VARIABLES).
        static std::vector<std::string> texts;
        static std::vector<retro_variable> vars;
        texts.clear(); vars.clear();
        for (const auto& d : g_defs) {
            if (!d.key) break;
            std::string t = std::string(d.desc) + "; ";
            std::string first = d.default_value ? d.default_value : "";
            t += first;
            for (int i = 0; d.values[i].value; ++i)
                if (first != d.values[i].value) t += std::string("|") + d.values[i].value;
            texts.push_back(t);
        }
        for (std::size_t i = 0; i < texts.size(); ++i) vars.push_back({g_defs[i].key, texts[i].c_str()});
        vars.push_back({nullptr, nullptr});
        cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars.data());
    }
    static const retro_controller_description pads[] = {
        {"Atari joystick", RETRO_DEVICE_JOYPAD},
    };
    static const retro_controller_info ports[] = {
        {pads, 1}, {pads, 1}, {nullptr, 0},
    };
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info*>(ports));
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { g_video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { g_audioBatch = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { g_inputPoll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { g_inputState = cb; }

RETRO_API void retro_init(void) {
    retro_log_callback lc{};
    if (g_env && g_env(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lc)) g_log = lc.log;
    neost::log::setSink(coreLogSink, nullptr);   // règle 4 : avant toute Machine
    const char* dir = nullptr;
    if (g_env && g_env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir) g_saveDir = dir;
    if (g_env && g_systemDir.empty() && g_env(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir) {
        g_systemDir = dir;
        scanTosFiles();
    }
    retro_led_interface led{};
    if (g_env && g_env(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &led)) g_led = led.set_led_state;
    g_canDupe = false;
    if (g_env) g_env(RETRO_ENVIRONMENT_GET_CAN_DUPE, &g_canDupe);
    if (g_env) {
        static retro_keyboard_callback kb{onKeyboard};
        g_env(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
        registerDiskControl();
    }
}

RETRO_API void retro_deinit(void) {
    g_s = Session{};
    neost::log::setSink(nullptr, nullptr);
    g_log = nullptr;
    g_led = nullptr;
}

RETRO_API void retro_get_system_info(retro_system_info* info) {
    std::memset(info, 0, sizeof *info);
    info->library_name = "NeoST";
    info->library_version = NEOST_VERSION_STR;
    info->valid_extensions = "st|msa|dim|stx|ipf|m3u|prg|tos|ttp|app|gtp|stc";
    info->need_fullpath = true;
    info->block_extract = false;   // RetroArch ouvre les .zip pour nous
}

RETRO_API void retro_get_system_av_info(retro_system_av_info* info) {
    std::memset(info, 0, sizeof *info);
    if (!g_s.machine) {
        info->geometry = {320, 200, kMaxWidth, kMaxHeight, 1.6f};
        info->timing = {50.0, double(kSampleRate)};
        return;
    }
    const Rect r = frameRect(false);
    fillAvInfo(info, r);
    // La cadence que le TOS va établir (son en-tête dit 50 ou 60 Hz), plutôt que celle
    // de la Glue à la mise sous tension : l'annonce initiale est alors la bonne.
    // Trames couleur (VideoGeometry, Shifter.hpp) : 263 lignes de 508 cycles à 60 Hz,
    // 313 de 512 à 50 Hz ; la mono garde la cadence de la Glue.
    int64_t fc = g_s.machine->frameCycles();
    neost::TosInfo t;
    if (g_s.machine->mfp.colorMonitor() && neost::readTosInfo(g_s.tosPath, t))
        fc = t.ntsc ? int64_t(263) * 508 : int64_t(313) * 512;
    info->timing.fps = neost::pacing::cpuHz() / double(fc);
    g_s.announcedFc = fc;
    g_s.aspect = info->geometry.aspect_ratio;
    g_s.av.onFrame(r.w, r.h, g_s.machine->frameCycles());   // amorce : déjà annoncé
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API void retro_reset(void) {
    if (!g_s.machine) return;
    // « Restart » de RetroArch : une remise sous tension, comme l'interrupteur.
    g_s.machine->hardReset();
    g_s.latch.reset();
}

RETRO_API bool retro_load_game(const retro_game_info* game) {
    retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!g_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        logf(RETRO_LOG_ERROR, "XRGB8888 is not supported by the frontend\n");
        return false;
    }
    setInputDescriptors();
    return loadContentOrDrop(game ? game->path : nullptr);
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info*, size_t) { return false; }

RETRO_API void retro_unload_game(void) {
    g_s.machine.reset();
    g_s.machineForced = false;
    g_s.playlist.clear();
    g_s.loaded = false;
}

RETRO_API unsigned retro_get_region(void) {
    return g_s.machine && g_s.machine->frameCycles() < 150000 ? RETRO_REGION_NTSC : RETRO_REGION_PAL;
}

RETRO_API void retro_run(void) {
    if (!g_s.machine) return;
    bool updated = false;
    if (g_env(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) readRuntimeOptions();
    Machine& m = *g_s.machine;
    applyInput();
    // Règle 2 : une fois les entrées posées, l'IPL une fois, puis la trame.
    m.cpu.updateIpl();
    m.runFrame();
    // Lecteur en marche : des trames de plus, son drainé, image de la dernière seule.
    // Le nombre de trames dépend de l'état émulé (moteur), donc rejouable à l'identique
    // (anticipation, jeu en réseau).
    if (g_opt.fastFloppy) {
        for (int i = 1; i < kFastFloppyFrames && m.fdc.motorOn(); ++i) {
            drainAudio();
            m.runFrame();
        }
    }
    // Redémarrée sur une autre machine : une trame de la nouvelle avant de montrer quoi
    // que ce soit (son tampon d'image n'existe qu'après sa première trame).
    if (checkMissingHardware()) { g_s.machine->cpu.updateIpl(); g_s.machine->runFrame(); }
    if (!g_s.machine) { if (g_canDupe) g_video(nullptr, 320, 200, 0); return; }
    int av = 3;
    const bool known = g_env(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &av);
    const bool shown = !known || (av & 1);
    const bool heard = !known || (av & 2);
    presentVideo(shown);
    presentAudio(heard);
    const bool led = g_s.machine->fdc.motorOn();
    if (g_led && led != g_s.ledOn) { g_led(0, led ? 1 : 0); g_s.ledOn = led; }
}

// --- États (règle 3) -----------------------------------------------------------------
RETRO_API size_t retro_serialize_size(void) {
    if (!g_s.machine) return 0;
    if (g_s.machine->saveStateWithMedia(g_s.state)) {
        // Borne fixe pour la session : la taille d'un état plus une marge (GEMDOS grandit
        // au boot, une disquette plus grande peut être insérée). Ne fait que croître.
        const std::size_t want = g_s.state.size() + 1024u * 1024u;
        if (want > g_s.stateBound) g_s.stateBound = want;
    }
    return g_s.stateBound;
}

RETRO_API bool retro_serialize(void* data, size_t size) {
    if (!g_s.machine || !g_s.machine->saveStateWithMedia(g_s.state)) return false;   // blit HOG garé : trame sautée
    if (g_s.state.size() > size) {
        logf(RETRO_LOG_WARN, "state of %zu bytes does not fit in %zu\n", g_s.state.size(), size);
        return false;
    }
    std::memcpy(data, g_s.state.data(), g_s.state.size());
    std::memset(static_cast<uint8_t*>(data) + g_s.state.size(), 0, size - g_s.state.size());
    return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t size) {
    if (!g_s.machine) return false;
    if (!g_s.machine->loadStateWithMedia(static_cast<const uint8_t*>(data), size)) {
        logf(RETRO_LOG_WARN, "state refused: %s\n", g_s.machine->lastStateError().c_str());
        return false;
    }
    return true;
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}

RETRO_API void* retro_get_memory_data(unsigned id) {
    if (!g_s.machine || id != RETRO_MEMORY_SYSTEM_RAM) return nullptr;
    return g_s.machine->bus.ram.data();
}

RETRO_API size_t retro_get_memory_size(unsigned id) {
    if (!g_s.machine || id != RETRO_MEMORY_SYSTEM_RAM) return 0;
    return g_s.machine->bus.ram.size();
}
