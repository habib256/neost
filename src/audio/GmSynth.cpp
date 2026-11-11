// =============================================================================
//  GmSynth.cpp — cf. GmSynth.hpp. Seule unité qui instancie TinySoundFont.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/GmSynth.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#define TSF_IMPLEMENTATION
#include "tsf.h"

GmSynth::GmSynth() = default;
GmSynth::~GmSynth() { close(); }

bool GmSynth::available() { return true; }

namespace {
// Où chercher une banque quand `sf` n'aboutit pas : les emplacements standards des
// distributions (Arch/CachyOS puis Debian/Ubuntu). `default.sf2` est le lien que
// pose le paquet soundfont d'Arch — priorité au choix de l'utilisateur système.
const char* const kSystemDirs[] = {
    "/usr/share/soundfonts", "/usr/share/sounds/sf2", "/usr/local/share/soundfonts",
};

bool isSf2(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e == ".sf2";
}

// Les .sf2 d'un dossier, par ordre de nom (déterministe : le même dossier donne
// toujours la même banque).
std::vector<std::filesystem::path> sf2In(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && isSf2(e.path())) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

bool GmSynth::open(const std::string& sf, uint32_t outputRate) {
    close();
    rate_ = outputRate;
    namespace fs = std::filesystem;
    std::error_code ec;

    std::vector<fs::path> candidates;
    if (!sf.empty()) {
        if (fs::is_directory(sf, ec)) for (auto& p : sf2In(sf)) candidates.push_back(p);
        else candidates.push_back(sf);
    }
    for (const char* d : kSystemDirs) {
        if (fs::is_regular_file(fs::path(d) / "default.sf2", ec)) candidates.push_back(fs::path(d) / "default.sf2");
        if (fs::is_directory(d, ec)) for (auto& p : sf2In(d)) candidates.push_back(p);
    }

    tsf* f = nullptr;
    fs::path loaded;
    for (const fs::path& p : candidates)
        if ((f = tsf_load_filename(p.string().c_str())) != nullptr) { loaded = p; break; }
    if (!f) {
        error_ = "no usable SoundFont (.sf2) — looked in \"" + sf +
                 "\" then the system folders; put TimGM6mb.sf2 (or any GM bank) in roms/gm/";
        return false;
    }
    tsf_set_output(f, TSF_STEREO_INTERLEAVED, int(outputRate), 0.0f);
    // Canal 10 = percussions (banque SF2 128). tsf_channel_set_presetnumber le
    // retrouvera aussi à chaque Program Change grâce au drapeau flag_mididrums.
    tsf_channel_set_bank_preset(f, 9, 128, 0);
    synth_ = f;
    sfName_ = loaded.filename().string();
    std::fprintf(stderr, "[gm] General MIDI synth started (TinySoundFont, %s, %d presets)\n",
                 loaded.string().c_str(), tsf_get_presetcount(f));
    return true;
}

void GmSynth::close() {
    if (synth_) { tsf_close(static_cast<tsf*>(synth_)); synth_ = nullptr; }
    pending_.clear(); sfName_.clear();
    parser_.reset();
}

// -----------------------------------------------------------------------------
//  Octets → événements datés (attente du rendu de la trame)
// -----------------------------------------------------------------------------
namespace { constexpr std::size_t kMaxPending = 8192; }   // même filet que Mt32Synth

void GmSynth::byteAt(uint8_t b, int64_t cycle) {
    if (!synth_) return;
    curCycle_ = cycle;
    parser_.byte(b, [this](const uint8_t* msg, int len) {
        if (msg[0] >= 0xF0 || len > 3) return;           // système/SysEx : rien pour un synthé GM
        if (pending_.size() >= kMaxPending) return;
        Event ev; ev.cycle = curCycle_; ev.len = len;
        for (int i = 0; i < len; ++i) ev.msg[i] = msg[i];
        pending_.push_back(ev);
    });
}

void GmSynth::clearEvents() { pending_.clear(); }

void GmSynth::panic() {
    if (!synth_) return;
    tsf* f = static_cast<tsf*>(synth_);
    // Les événements datés de la trame en cours sont joués d'abord par render ; la
    // panique arrive entre deux trames (requête de l'interface), la file est vide.
    for (int ch = 0; ch < 16; ++ch) {
        tsf_channel_sounds_off_all(f, ch);              // CC 120 All Sound Off : coupe net
        tsf_channel_midi_control(f, ch, 121, 0);        // CC 121 Reset All Controllers (pédale comprise)
        tsf_channel_note_off_all(f, ch);                // CC 123 All Notes Off
    }
}

void GmSynth::apply(const uint8_t* msg, int len) {
    tsf* f = static_cast<tsf*>(synth_);
    const int ch = msg[0] & 0x0F;
    switch (msg[0] & 0xF0) {
    case 0x80: tsf_channel_note_off(f, ch, msg[1]); break;
    case 0x90:
        if (len >= 3 && msg[2]) tsf_channel_note_on(f, ch, msg[1], float(msg[2]) / 127.0f);
        else tsf_channel_note_off(f, ch, msg[1]);        // vélocité 0 = note off (usage courant)
        break;
    case 0xB0: if (len >= 3) tsf_channel_midi_control(f, ch, msg[1], msg[2]); break;
    case 0xC0: tsf_channel_set_presetnumber(f, ch, msg[1], ch == 9); break;
    case 0xE0: if (len >= 3) tsf_channel_set_pitchwheel(f, ch, (int(msg[2]) << 7) | msg[1]); break;
    default: break;                                      // $A0/$D0 : TSF ne module pas la pression
    }
}

// -----------------------------------------------------------------------------
//  Rendu d'une trame : le bloc est DÉCOUPÉ aux dates des messages
// -----------------------------------------------------------------------------
void GmSynth::render(float* lr, int frames, int64_t frameStartCycle, int64_t frameCycles) {
    if (!synth_ || frames <= 0) { pending_.clear(); return; }
    tsf* f = static_cast<tsf*>(synth_);
    if (int(scratch_.size()) < 2 * frames) scratch_.resize(std::size_t(2 * frames));
    std::fill(scratch_.begin(), scratch_.begin() + 2 * frames, 0.0f);
    int done = 0;
    for (const Event& ev : pending_) {
        double off = frameCycles > 0 ? double(ev.cycle - frameStartCycle) / double(frameCycles) * frames : 0.0;
        const int at = std::max(done, std::min(frames - 1, int(off)));
        if (at > done) { tsf_render_float(f, scratch_.data() + 2 * done, at - done, 0); done = at; }
        apply(ev.msg, ev.len);
    }
    pending_.clear();
    if (done < frames) tsf_render_float(f, scratch_.data() + 2 * done, frames - done, 0);
    for (int i = 0; i < 2 * frames; ++i) lr[i] += scratch_[std::size_t(i)] * gain_;
}
