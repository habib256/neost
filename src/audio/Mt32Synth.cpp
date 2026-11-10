// =============================================================================
//  Mt32Synth.cpp — cf. Mt32Synth.hpp.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/Mt32Synth.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef NEOST_WITH_MT32
#include <mt32emu/mt32emu.h>

namespace {
// Munt écrit ses messages sur stderr SANS préfixe (Synth.cpp, printDebug par défaut) :
// ils se mêlaient au journal de NeoST sans qu'on sache d'où ils venaient — visible dès
// que le MT-32 est passé actif par défaut. On les récupère pour les préfixer.
//
// Cas particulier du « Header not intended for this device manufacturer » : un SysEx
// adressé à un AUTRE fabricant que Roland ($41) n'est pas une anomalie — c'est le
// fonctionnement normal d'un anneau MIDI où le ST parle à plusieurs appareils. On le
// signale UNE fois, pour informer, puis on se tait plutôt que de noyer le journal.
class NeostReport : public MT32Emu::ReportHandler {
public:
    void printDebug(const char* fmt, va_list list) override {
        char buf[512];
        std::vsnprintf(buf, sizeof buf, fmt, list);
        // Anti-répétition GÉNÉRAL. Un fichier General MIDI joué sur un MT-32 déclenche
        // « Rhythm: Attempted to play unmapped key NN » à CHAQUE coup de percussion :
        // des centaines de lignes identiques qui noient tout le reste du journal.
        if (std::strncmp(buf, last_, sizeof last_ - 1) == 0) { ++repeats_; return; }
        if (repeats_) {
            std::fprintf(stderr, "[mt32] (message précédent répété %u fois)\n", repeats_);
            repeats_ = 0;
        }
        std::snprintf(last_, sizeof last_, "%s", buf);

        if (std::strstr(buf, "not intended for this device manufacturer")) {
            if (foreignSysexSeen_) return;
            foreignSysexSeen_ = true;
            std::fprintf(stderr, "[mt32] %s (SysEx destiné à un autre fabricant — "
                                 "ignoré ; répétitions tues)\n", buf);
            return;
        }
        std::fprintf(stderr, "[mt32] %s\n", buf);
    }
    void showLCDMessage(const char* message) override {
        std::fprintf(stderr, "[mt32] LCD: %s\n", message);
    }
private:
    bool foreignSysexSeen_ = false;
    char last_[512] = {0};
    unsigned repeats_ = 0;
};

NeostReport& report() { static NeostReport r; return r; }
}  // namespace
#endif

Mt32Synth::Mt32Synth() { sysex_.reserve(256); }
Mt32Synth::~Mt32Synth() { close(); }

bool Mt32Synth::available() {
#ifdef NEOST_WITH_MT32
    return true;
#else
    return false;
#endif
}

#ifdef NEOST_WITH_MT32
namespace {
// Charge un fichier ROM ; renvoie l'image (à libérer) et son type, ou nullptr.
const MT32Emu::ROMImage* loadRom(const std::filesystem::path& p, std::string& name, bool& isControl) {
    MT32Emu::FileStream* f = new MT32Emu::FileStream();   // possédé par l'image
    if (!f->open(p.string().c_str())) { delete f; return nullptr; }
    const MT32Emu::ROMImage* img = MT32Emu::ROMImage::makeROMImage(f);
    if (!img) { delete f; return nullptr; }
    const MT32Emu::ROMInfo* info = img->getROMInfo();
    if (!info) { MT32Emu::ROMImage::freeROMImage(img); delete f; return nullptr; }
    // ROM COMPLÈTES seulement : les moitiés (ic27/ic26 et PCM ic21/ic22 des lots MAME,
    // roland_mt32.cpp:397-418, 432-433 @ ce391499d37d ; pcm_mt32_l/_h chez munt) ont
    // un pairType ≠ Full et ne s'ouvrent pas seules.
    if (info->pairType != MT32Emu::ROMInfo::Full) { MT32Emu::ROMImage::freeROMImage(img); delete f; return nullptr; }
    name = info->shortName ? info->shortName : "";
    isControl = info->type == MT32Emu::ROMInfo::Control;
    return img;
}
} // namespace
#endif

#ifdef NEOST_WITH_MT32
namespace {
// freeROMImage ne libère PAS un File fourni par l'utilisateur (isFileUserProvided) :
// à nous de le supprimer, sinon chaque changement de modèle fuit 64 Ko à 1 Mo par ROM.
void freeRom(const MT32Emu::ROMImage* img) {
    if (!img) return;
    MT32Emu::File* f = img->isFileUserProvided() ? img->getFile() : nullptr;
    MT32Emu::ROMImage::freeROMImage(img);
    delete f;
}
} // namespace
#endif

bool Mt32Synth::open(const std::string& romDir, uint32_t outputRate, const std::string& model) {
#ifdef NEOST_WITH_MT32
    close();
    outputRate_ = outputRate;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(romDir, ec)) { error_ = "no ROM folder: " + romDir; return false; }
    // Toutes les ROM reconnues du dossier, classées ; CM-32L préféré (sur-ensemble).
    struct Found { const MT32Emu::ROMImage* img; std::string name; bool control; };
    std::vector<Found> found;
    for (const auto& e : fs::directory_iterator(romDir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string name; bool ctl = false;
        if (const MT32Emu::ROMImage* img = loadRom(e.path(), name, ctl)) found.push_back({img, name, ctl});
    }
    // Version la plus récente d'abord (« ctrl_cm32l_1_02 » avant « …_1_00 »).
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.name > b.name; });
    // Noms courts Munt : « ctrl_cm32l_1_02 » / « pcm_cm32l », « ctrl_mt32_1_07 » / « pcm_mt32 » —
    // préfixe EXACT de famille (« cm32l_ » ne doit pas matcher la variante « cm32ln »).
    auto pick = [&](bool control, const char* family) -> const MT32Emu::ROMImage* {
        const std::string want = control ? std::string("ctrl_") + family + "_" : std::string("pcm_") + family;
        for (const Found& f : found)
            if (f.control == control && (control ? f.name.rfind(want, 0) == 0 : f.name == want)) return f.img;
        return nullptr;
    };
    const MT32Emu::ROMImage* ctl = nullptr; const MT32Emu::ROMImage* pcm = nullptr;
    std::vector<const char*> families;
    if (model == "mt32") families = {"mt32"};
    else if (model == "cm32l") families = {"cm32l"};
    else families = {"cm32l", "mt32"};
    for (const char* family : families) {
        ctl = pick(true, family); pcm = pick(false, family);
        if (ctl && pcm) { model_ = std::string(family) == "cm32l" ? "CM-32L" : "MT-32"; break; }
    }
    if (!ctl || !pcm) {
        for (const Found& f : found) freeRom(f.img);
        error_ = (model == "mt32" ? "no complete MT-32 ROM pair (MT32_CONTROL + MT32_PCM) in "
                : model == "cm32l" ? "no complete CM-32L ROM pair (CM32L_CONTROL + CM32L_PCM) in "
                : "no complete MT-32/CM-32L ROM pair in ") + romDir;
        return false;
    }
    for (const Found& f : found) if (f.img != ctl && f.img != pcm) freeRom(f.img);
    auto* synth = new MT32Emu::Synth(&report());   // journal préfixé [mt32]
    if (!synth->open(*ctl, *pcm)) {
        delete synth;
        freeRom(ctl); freeRom(pcm);
        error_ = "libmt32emu refused the ROMs";
        return false;
    }
    synth->setOutputGain(1.0f);
    synth_ = synth; romControl_ = ctl; romPcm_ = pcm;
    src_ = new MT32Emu::SampleRateConverter(*synth, double(outputRate),
                                            MT32Emu::SamplerateConversionQuality_GOOD);
    std::fprintf(stderr, "[mt32] %s emulation started (libmt32emu, control ROM %s, PCM ROM %s)\n",
                 model_.c_str(), ctl->getROMInfo()->shortName, pcm->getROMInfo()->shortName);
    return true;
#else
    (void)romDir; (void)outputRate; (void)model;
    error_ = "this build has no libmt32emu (NEOST_WITH_MT32=OFF)";
    return false;
#endif
}

void Mt32Synth::close() {
#ifdef NEOST_WITH_MT32
    if (src_) { delete static_cast<MT32Emu::SampleRateConverter*>(src_); src_ = nullptr; }
    if (synth_) {
        auto* s = static_cast<MT32Emu::Synth*>(synth_);
        s->close(); delete s; synth_ = nullptr;
    }
    if (romControl_) { freeRom(static_cast<const MT32Emu::ROMImage*>(romControl_)); romControl_ = nullptr; }
    if (romPcm_)     { freeRom(static_cast<const MT32Emu::ROMImage*>(romPcm_)); romPcm_ = nullptr; }
#endif
    pending_.clear(); model_.clear();
}

// -----------------------------------------------------------------------------
//  Parseur : octets → événements datés (attente du rendu de la trame)
// -----------------------------------------------------------------------------
namespace {
int dataBytesFor(uint8_t st) {
    switch (st & 0xF0) {
    case 0xC0: case 0xD0: return 1;
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    default: break;
    }
    switch (st) { case 0xF1: case 0xF3: return 1; case 0xF2: return 2; default: return 0; }
}
} // namespace

void Mt32Synth::byteAt(uint8_t b, int64_t cycle) { if (synth_) parse(b, cycle); }

void Mt32Synth::clearEvents() { pending_.clear(); }

namespace { constexpr std::size_t kMaxPending = 8192; }   // ~3 min de notes : jamais atteint en marche normale

void Mt32Synth::parse(uint8_t b, int64_t cycle) {
    if (b >= 0xF8) return;                           // temps réel : rien pour un MT-32
    if (inSysex_) {
        // kMaxPending : filet. Un appelant qui oublie de rendre la trame (périphérique
        // audio perdu) ferait autrement croître la file SANS FIN, et au retour du son
        // tout le retard partirait d'un coup dans une seule trame.
        if (b == 0xF7) {
            sysex_.push_back(b);
            if (pending_.size() < kMaxPending) pending_.push_back({cycle, 0, sysex_});
            sysex_.clear(); inSysex_ = false;
        }
        else if (b & 0x80) { sysex_.clear(); inSysex_ = false; parse(b, cycle); }
        else if (sysex_.size() < 4096) sysex_.push_back(b);
        return;
    }
    if (b & 0x80) {
        if (b == 0xF0) { inSysex_ = true; sysex_.assign(1, b); status_ = 0; return; }
        status_ = b; needed_ = dataBytesFor(b); got_ = 0;
        if (needed_ == 0) status_ = 0;               // système commun sans donnée : ignoré
        return;
    }
    if (!status_) return;
    data_[got_++] = b;
    if (got_ >= needed_) {
        if (status_ < 0xF0)
            if (pending_.size() < kMaxPending)
                pending_.push_back({cycle, uint32_t(status_) | (uint32_t(data_[0]) << 8) | (uint32_t(data_[1]) << 16), {}});
        got_ = 0;
        if (status_ >= 0xF0) status_ = 0;
    }
}

// -----------------------------------------------------------------------------
//  Rendu d'une trame : événements datés → Munt, puis conversion de fréquence + mix
// -----------------------------------------------------------------------------
void Mt32Synth::render(float* lr, int frames, int64_t frameStartCycle, int64_t frameCycles) {
#ifdef NEOST_WITH_MT32
    if (!synth_ || frames <= 0) { pending_.clear(); return; }
    auto* synth = static_cast<MT32Emu::Synth*>(synth_);
    auto* src = static_cast<MT32Emu::SampleRateConverter*>(src_);
    const uint32_t base = synth->getInternalRenderedSampleCount();
    for (const Event& ev : pending_) {
        double off = frameCycles > 0 ? double(ev.cycle - frameStartCycle) / double(frameCycles) * frames : 0.0;
        off = std::max(0.0, std::min(double(frames - 1), off));
        const uint32_t ts = base + uint32_t(src->convertOutputToSynthTimestamp(off));
        if (ev.sysex.empty()) synth->playMsg(ev.msg, ts);
        else synth->playSysex(ev.sysex.data(), uint32_t(ev.sysex.size()), ts);
    }
    pending_.clear();
    if (int(scratch_.size()) < 2 * frames) scratch_.assign(std::size_t(2 * frames), 0.0f);
    src->getOutputSamples(scratch_.data(), unsigned(frames));
    for (int i = 0; i < 2 * frames; ++i) lr[i] += scratch_[std::size_t(i)] * gain_;
#else
    (void)lr; (void)frames; (void)frameStartCycle; (void)frameCycles;
    pending_.clear();
#endif
}
