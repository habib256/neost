// =============================================================================
//  DriveSoundMix.cpp — cf. DriveSoundMix.hpp.
//
//  Format : [B] Microsoft « Multimedia Programming Interface and Data Specifications
//  1.0 », RIFF WAVE, bloc « fmt » (PCM = 1) et bloc « data » ; 8 bits non signé,
//  16 bits signé petit-boutiste.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/DriveSoundMix.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace neost {
namespace {

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

constexpr std::size_t kMaxVoices = 8;
// Gains des one-shots, ceux du GUI (audio/DriveSound.cpp) : unité, sauf le tic d'index.
constexpr float kIndexGain = 0.18f;

}  // namespace

bool DriveSoundMix::readWav(const std::string& path, uint32_t rate, std::vector<float>& out) {
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::string(d.begin(), d.begin() + 4) != "RIFF" ||
        std::string(d.begin() + 8, d.begin() + 12) != "WAVE")
        return false;
    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t srcRate = 0;
    const uint8_t* data = nullptr;
    std::size_t dataLen = 0;
    for (std::size_t p = 12; p + 8 <= d.size();) {
        const std::string id(d.begin() + std::ptrdiff_t(p), d.begin() + std::ptrdiff_t(p + 4));
        const uint32_t len = le32(d.data() + p + 4);
        const std::size_t body = p + 8;
        if (body + len > d.size()) break;
        if (id == "fmt " && len >= 16) {
            fmt = le16(d.data() + body);
            channels = le16(d.data() + body + 2);
            srcRate = le32(d.data() + body + 4);
            bits = le16(d.data() + body + 14);
        } else if (id == "data") {
            data = d.data() + body;
            dataLen = len;
        }
        p = body + len + (len & 1);
    }
    if (fmt != 1 || !data || channels < 1 || channels > 2 || (bits != 8 && bits != 16) || !srcRate || !rate)
        return false;
    const std::size_t bytes = bits / 8u * channels;
    const std::size_t n = dataLen / bytes;
    std::vector<float> mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        float acc = 0.0f;
        for (uint16_t c = 0; c < channels; ++c) {
            const uint8_t* s = data + i * bytes + c * (bits / 8u);
            acc += bits == 8 ? (float(s[0]) - 128.0f) / 128.0f : float(int16_t(le16(s))) / 32768.0f;
        }
        mono[i] = acc / float(channels);
    }
    // Rééchantillonnage linéaire au débit de sortie (bruits mécaniques : suffisant).
    const double step = double(srcRate) / double(rate);
    const std::size_t m = std::size_t(double(n) / step);
    out.resize(m);
    for (std::size_t i = 0; i < m; ++i) {
        const double x = double(i) * step;
        const std::size_t k = std::size_t(x);
        const float t = float(x - double(k));
        const float a = mono[std::min(k, n - 1)], b = mono[std::min(k + 1, n - 1)];
        out[i] = a + (b - a) * t;
    }
    return !out.empty();
}

bool DriveSoundMix::load(const std::string& dir, uint32_t rate) {
    reset();
    readWav(dir + "/drive_spin.wav", rate, spin_);
    readWav(dir + "/drive_startup.wav", rate, startup_);
    readWav(dir + "/drive_seek.wav", rate, seek_);
    readWav(dir + "/drive_click.wav", rate, click_);
    return loaded();
}

void DriveSoundMix::reset() {
    voices_.clear();
    motor_ = false;
    spinPos_ = 0;
    motorLevel_ = 0.0f;
}

void DriveSoundMix::onEvent(FdcSound e) {
    const std::vector<float>* pcm = nullptr;
    float gain = 1.0f;
    switch (e) {
    case FdcSound::Step: pcm = &click_; break;
    case FdcSound::Seek: pcm = &seek_; break;
    case FdcSound::Index: pcm = &click_; gain = kIndexGain; break;
    case FdcSound::MotorOn: case FdcSound::MotorOff: return;   // le moteur suit setMotor
    }
    if (!pcm || pcm->empty()) return;
    if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());   // le plus ancien cède
    voices_.push_back({pcm, 0, gain});
}

void DriveSoundMix::setMotor(bool on) {
    if (on && !motor_ && !startup_.empty()) {
        if (voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());
        voices_.push_back({&startup_, 0, 1.0f});
    }
    motor_ = on;
}

void DriveSoundMix::mix(float* stereo, int frames, float gain) {
    if (frames <= 0) return;
    // Rampe de ~10 ms à 48 kHz sur l'entrée et la sortie du ronron.
    constexpr float kRamp = 1.0f / 480.0f;
    for (int i = 0; i < frames; ++i) {
        float s = 0.0f;
        const float target = motor_ ? 1.0f : 0.0f;
        motorLevel_ += std::clamp(target - motorLevel_, -kRamp, kRamp);
        if (motorLevel_ > 0.0f && !spin_.empty()) {
            s += spin_[spinPos_] * motorLevel_;
            if (++spinPos_ >= spin_.size()) spinPos_ = 0;
        }
        for (Voice& v : voices_)
            if (v.pos < v.pcm->size()) s += (*v.pcm)[v.pos++] * v.gain;
        s *= gain;
        stereo[2 * i] += s;
        stereo[2 * i + 1] += s;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.pos >= v.pcm->size(); }),
                  voices_.end());
}

}  // namespace neost
