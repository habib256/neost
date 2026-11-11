#include "io/UsbDevice.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | (uint32_t(le16(p + 2)) << 16); }
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
void put32(uint8_t* p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
// [B] HID 1.11, annexe B : rapports boot clavier (8 octets) et souris (3).
const std::vector<uint8_t> keyboardReport = {
    0x05,1, 0x09,6, 0xA1,1, 0x05,7, 0x19,0xE0, 0x29,0xE7,
    0x15,0, 0x25,1, 0x75,1, 0x95,8, 0x81,2, 0x95,1, 0x75,8, 0x81,1,
    0x95,5, 0x75,1, 0x05,8, 0x19,1, 0x29,5, 0x91,2,
    // Logical/Usage Maximum à $7F et non $65 : Help ($75) et Undo ($7A) sont émis par
    // Machine et tombaient hors de la plage déclarée — [B] HID 1.11 § 6.2.2.7 : une
    // valeur de tableau hors de Logical Min/Max est ignorée par l'hôte.
    0x95,1, 0x75,3, 0x91,1, 0x95,6, 0x75,8, 0x15,0, 0x25,0x7F,
    0x05,7, 0x19,0, 0x29,0x7F, 0x81,0, 0xC0
};
const std::vector<uint8_t> mouseReport = {
    0x05,1, 0x09,2, 0xA1,1, 0x09,1, 0xA1,0, 0x05,9,
    0x19,1, 0x29,3, 0x15,0, 0x25,1, 0x95,3, 0x75,1, 0x81,2,
    0x95,1, 0x75,5, 0x81,1, 0x05,1, 0x09,0x30, 0x09,0x31,
    0x15,0x81, 0x25,0x7F, 0x75,8, 0x95,2, 0x81,6, 0xC0,0xC0
};
}

UsbDevice::~UsbDevice() { if (disk_) std::fclose(disk_); }

bool UsbDevice::parseSpec(const std::string& spec, Kind& kind, std::string& image, bool& readOnly) {
    image.clear(); readOnly = false;
    if (spec.empty() || spec == "none") kind = Kind::None;
    else if (spec == "keyboard") kind = Kind::Keyboard;
    else if (spec == "mouse") kind = Kind::Mouse;
    else if (spec.rfind("disk:",0) == 0 || spec.rfind("disk-ro:",0) == 0) {
        kind = Kind::Storage; readOnly = spec.rfind("disk-ro:",0) == 0;
        image = spec.substr(readOnly ? 8 : 5); if (image.empty()) return false;
    } else return false;
    return true;
}

bool UsbDevice::attach(Kind kind, const std::string& image, bool readOnly) {
    FILE* fp = nullptr;
    uint32_t blocks = 0;
    if (kind == Kind::Storage) {
        if (image.empty()) return false;
        if (!readOnly) fp = std::fopen(image.c_str(), "r+b");
        if (!fp) { fp = std::fopen(image.c_str(), "rb"); readOnly = true; }
        if (!fp) return false;
        const bool seekOk = fseeko(fp, 0, SEEK_END) == 0;
        const auto size = ftello(fp);
        // Borne explicite : offsets byte 32 bits, secteurs logiques de 512 octets.
        if (!seekOk || size <= 0 || size % 512 || uint64_t(size) > 0xFFFFFE00u) {
            std::fclose(fp); return false;
        }
        blocks = uint32_t(size / 512);
        std::rewind(fp);
    }
    if (disk_) std::fclose(disk_);
    disk_ = fp; kind_ = kind; blocks_ = blocks;
    image_ = kind == Kind::Storage ? image : std::string();
    readOnly_ = kind == Kind::Storage && readOnly;
    reset();
    return true;
}

void UsbDevice::reset() {
    address_ = configuration_ = pendingAddress_ = pendingConfig_ = idle_ = leds_ = 0;
    protocol_ = 1; setAddress_ = setConfig_ = controlStall_ = false;
    std::fill(std::begin(setup_), std::end(setup_), uint8_t(0));
    reply_.clear(); replyOffset_ = 0; halt_ = 0;
    std::fill(std::begin(keys_), std::end(keys_), false);
    buttons_ = 0; mouseX_ = mouseY_ = 0; reportDirty_ = true; idleElapsed_ = 0;
    bot_ = Bot::Cbw; tag_ = residue_ = dataLeft_ = diskOffset_ = 0;
    cswStatus_ = senseKey_ = senseAsc_ = 0; diskTransfer_ = false;
    scsiReply_.clear(); scsiOffset_ = 0;
}

std::vector<uint8_t> UsbDevice::descriptor(unsigned type, unsigned index) const {
    const bool keyboard = kind_ == Kind::Keyboard;
    const bool hid = keyboard || kind_ == Kind::Mouse;
    const auto& desc = keyboard ? keyboardReport : mouseReport;
    const std::vector<uint8_t> hd = {9,0x21,0x11,0x01,0,1,0x22,uint8_t(desc.size()),0};
    if (type == 1 && index == 0) {
        // Identité virtuelle explicite ; aucun VID commercial emprunté.
        return {18,1,0x10,1,0,0,0,8, 0,0, uint8_t(kind_),0, 0,1,1,2,0,1};
    }
    if (type == 2 && index == 0) {
        std::vector<uint8_t> c = {9,2,uint8_t(hid ? 34 : 32),0,1,1,0,0x80,25,
            9,4,0,0,uint8_t(hid ? 1 : 2),uint8_t(hid ? 3 : 8),uint8_t(hid ? 1 : 6),
            uint8_t(hid ? (keyboard ? 1 : 2) : 0x50),0};
        if (hid) c.insert(c.end(), hd.begin(), hd.end());
        const uint8_t ep[] = {7,5,0x81,uint8_t(hid ? 3 : 2),uint8_t(hid ? 8 : 64),0,uint8_t(hid ? 10 : 0)};
        c.insert(c.end(), std::begin(ep), std::end(ep));
        if (!hid) c.insert(c.end(), {7,5,2,2,64,0,0});
        return c;
    }
    if (hid && type == 0x21 && index == 0) return hd;
    if (hid && type == 0x22 && index == 0) return desc;
    if (type == 3) {
        if (index == 0) return {4,3,9,4};
        const char* s = index == 1 ? "NeoST" : index == 2 ?
            (keyboard ? "USB Keyboard" : hid ? "USB Mouse" : "USB Disk") : nullptr;
        if (!s) return {};
        std::vector<uint8_t> result = {uint8_t(2 + 2 * std::strlen(s)),3};
        while (*s) { result.push_back(uint8_t(*s++)); result.push_back(0); }
        return result;
    }
    return {};
}

void UsbDevice::setup() {
    const uint8_t rt = setup_[0], request = setup_[1];
    const uint16_t value = le16(setup_ + 2), index = le16(setup_ + 4), length = le16(setup_ + 6);
    reply_.clear(); replyOffset_ = 0; controlStall_ = false; setAddress_ = setConfig_ = false;
    const bool hid = kind_ == Kind::Keyboard || kind_ == Kind::Mouse;
    if (request == 6 && (rt == 0x80 || (hid && rt == 0x81 && index == 0))) {
        reply_ = descriptor(value >> 8, value & 255);
        controlStall_ = reply_.empty();
    } else if (rt == 0 && request == 5 && !index && !length && value < 128) {
        pendingAddress_ = uint8_t(value); setAddress_ = true;
    } else if (rt == 0 && request == 9 && !index && !length && value <= 1) {
        pendingConfig_ = uint8_t(value); setConfig_ = true;
    } else if (rt == 0x80 && request == 8 && !value && !index) reply_ = {configuration_};
    else if (request == 0 && !value && ((rt == 0x80 && !index) || (rt == 0x81 && !index) ||
             (rt == 0x82 && (index == 0x81 || index == 2)))) {
        const uint8_t mask = index == 0x81 ? 1 : 2;
        reply_ = {uint8_t(rt == 0x82 && (halt_ & mask) ? 1 : 0),0};
    } else if ((rt == 2) && (request == 1 || request == 3) && value == 0 && !length &&
               (index == 0x81 || (kind_ == Kind::Storage && index == 2))) {
        const uint8_t mask = index == 0x81 ? 1 : 2;
        if (request == 1) halt_ &= uint8_t(~mask); else halt_ |= mask;
    } else if (rt == 0x81 && request == 10 && !value && !index) reply_ = {0};
    else if (rt == 1 && request == 11 && !value && !index && !length) {}
    else if (hid && !index && rt == 0xA1 && request == 1 && (value >> 8) == 1 && !(value & 255)) reply_ = report(false);
    else if (hid && !index && rt == 0xA1 && request == 2 && !value) reply_ = {idle_};
    else if (hid && !index && rt == 0xA1 && request == 3 && !value) reply_ = {protocol_};
    else if (hid && !index && rt == 0x21 && request == 10 && !length && !(value & 255)) { idle_ = uint8_t(value >> 8); idleElapsed_ = 0; }
    else if (hid && !index && rt == 0x21 && request == 11 && !length && value <= 1) protocol_ = uint8_t(value);
    else if (kind_ == Kind::Keyboard && !index && rt == 0x21 && request == 9 && value == 0x200 && length == 1) {}
    else if (kind_ == Kind::Storage && !index && rt == 0xA1 && request == 0xFE && !value && length == 1) reply_ = {0};
    else if (kind_ == Kind::Storage && !index && rt == 0x21 && request == 0xFF && !value && !length) {
        bot_ = Bot::Cbw; scsiReply_.clear(); scsiOffset_ = dataLeft_ = 0; diskTransfer_ = false;
    } else controlStall_ = true;
    if (reply_.size() > length) reply_.resize(length);
}

UsbDevice::Result UsbDevice::control(unsigned pid, uint8_t* data, unsigned length, unsigned& actual) {
    if (pid == 0) {
        if (length != 8) return Result::Stall;
        std::copy_n(data, 8, setup_); setup(); actual = 8;
        return Result::Ack;                 // une requête non supportée STALL à la phase suivante
    }
    if (controlStall_) return Result::Stall;
    if (pid == 2) {
        if (!(setup_[0] & 0x80)) {          // status IN : applique SET_ADDRESS après l'ACK
            if (length) return Result::Stall;
            if (setAddress_) { address_ = pendingAddress_; setAddress_ = false; }
            if (setConfig_) { configuration_ = pendingConfig_; setConfig_ = false; halt_ = 0; }
        } else {
            actual = std::min<unsigned>(length, unsigned(reply_.size()) - replyOffset_);
            if (actual) std::copy_n(reply_.data() + replyOffset_, actual, data);
            replyOffset_ += actual;
        }
        return Result::Ack;
    }
    if (pid == 1) {
        if (setup_[0] & 0x80) return length ? Result::Stall : Result::Ack;
        if (kind_ == Kind::Keyboard && setup_[0] == 0x21 && setup_[1] == 9 && length == 1) {
            leds_ = data[0]; actual = 1; return Result::Ack;
        }
        return length ? Result::Stall : Result::Ack;
    }
    return Result::Stall;
}

void UsbDevice::keyEvent(uint8_t usage, bool down) {
    if (kind_ != Kind::Keyboard || !usage || keys_[usage] == down) return;
    keys_[usage] = down; reportDirty_ = true;
}
void UsbDevice::mouseEvent(int dx, int dy, bool left, bool right) {
    if (kind_ != Kind::Mouse) return;
    const uint8_t buttons = uint8_t((left ? 1 : 0) | (right ? 2 : 0));
    if (dx || dy || buttons != buttons_) reportDirty_ = true;
    mouseX_ = int32_t(std::clamp<int64_t>(int64_t(mouseX_) + dx, -32767, 32767));
    mouseY_ = int32_t(std::clamp<int64_t>(int64_t(mouseY_) + dy, -32767, 32767));
    buttons_ = buttons;
}
void UsbDevice::tick() { if (idle_ && idleElapsed_ < 4u * idle_) ++idleElapsed_; }

std::vector<uint8_t> UsbDevice::report(bool consume) {
    std::vector<uint8_t> r(kind_ == Kind::Keyboard ? 8u : 3u, 0);
    if (kind_ == Kind::Keyboard) {
        for (unsigned i = 0; i < 8; ++i) if (keys_[0xE0 + i]) r[0] |= uint8_t(1u << i);
        unsigned n = 2;
        for (unsigned i = 1; i < 0xE0; ++i) if (keys_[i]) {
            if (n == 8) { std::fill(r.begin() + 2, r.end(), uint8_t(1)); break; } // ErrorRollOver
            r[n++] = uint8_t(i);
        }
    } else {
        const int x = std::clamp(mouseX_, -127, 127), y = std::clamp(mouseY_, -127, 127);
        r = {buttons_,uint8_t(x),uint8_t(y)};
        if (consume) { mouseX_ -= x; mouseY_ -= y; }
    }
    if (consume) { reportDirty_ = mouseX_ || mouseY_; idleElapsed_ = 0; }
    return r;
}

UsbDevice::Result UsbDevice::transfer(unsigned ep, unsigned pid, uint8_t* data, unsigned length,
                                     unsigned& actual, unsigned /*maxPacket*/) {
    actual = 0;
    if (!ep) return control(pid, data, length, actual);
    if (!configuration_) return Result::Stall;
    if ((ep == 1 && pid == 2 && (halt_ & 1)) || (ep == 2 && pid == 1 && (halt_ & 2))) return Result::Stall;
    if (kind_ == Kind::Storage) return bulk(ep, pid, data, length, actual);
    if (ep != 1 || pid != 2) return Result::Stall;
    if (!reportDirty_ && (!idle_ || idleElapsed_ < 4u * idle_)) return Result::Nak;
    const unsigned need = kind_ == Kind::Keyboard ? 8u : 3u;
    if (length < need) return Result::Stall;
    auto r = report(true); actual = unsigned(r.size()); std::copy(r.begin(), r.end(), data);
    return Result::Ack;
}

// [B] Bulk-Only 1.0 §6 : signature/tag/résidu little-endian, CDB SCSI big-endian.
void UsbDevice::scsi(const uint8_t* cdb, unsigned cdbLength, uint32_t expected, bool in) {
    scsiReply_.clear(); scsiOffset_ = 0; diskTransfer_ = false; dataLeft_ = 0; cswStatus_ = 0;
    auto fail = [&](uint8_t key, uint8_t asc) { senseKey_ = key; senseAsc_ = asc; cswStatus_ = 1; };
    const uint8_t op = cdb[0];
    if (cdbLength < (op < 0x20 ? 6u : op < 0xA0 ? 10u : 12u)) fail(5,0x24);
    else switch (op) {
    case 0x00: case 0x1B: case 0x1E: break; // ready/start-stop/prevent-allow
    case 0x35: if (std::fflush(disk_) != 0) fail(3,0x0C); break;
    case 0x12: {
        if (cdb[1] & 1) { fail(5,0x24); break; }
        scsiReply_.assign(36,0); scsiReply_[1] = 0x80; scsiReply_[2] = 2; scsiReply_[3] = 2; scsiReply_[4] = 31;
        std::memcpy(scsiReply_.data() + 8, "NeoST   USB Disk        1.00", 28);
        scsiReply_.resize(std::min<unsigned>(36, cdb[4])); break;
    }
    case 0x03:
        scsiReply_.assign(18,0); scsiReply_[0] = 0x70; scsiReply_[2] = senseKey_; scsiReply_[7] = 10; scsiReply_[12] = senseAsc_;
        scsiReply_.resize(std::min<unsigned>(18,cdb[4])); senseKey_ = senseAsc_ = 0; break;
    case 0x1A:
        scsiReply_ = {3,0,uint8_t(readOnly_ ? 0x80 : 0),0};
        scsiReply_.resize(std::min<unsigned>(4,cdb[4])); break;
    case 0x25: {
        scsiReply_.assign(8,0); const uint32_t last = blocks_ - 1;
        for (unsigned i = 0; i < 4; ++i) scsiReply_[i] = uint8_t(last >> (24 - 8 * i));
        scsiReply_[6] = 2; break;
    }
    case 0x08: case 0x0A: case 0x28: case 0x2A: {
        const bool six = op < 0x20, write = op == 0x0A || op == 0x2A;
        const uint32_t lba = six ? ((uint32_t(cdb[1] & 31) << 16) | (uint32_t(cdb[2]) << 8) | cdb[3]) : be32(cdb + 2);
        const uint32_t count = six ? (cdb[4] ? cdb[4] : 256u) : uint32_t((cdb[7] << 8) | cdb[8]);
        if (uint64_t(lba) + count > blocks_) { fail(5,0x21); break; }
        if (write && readOnly_) { fail(7,0x27); break; }
        if (in == write || expected != count * 512u) { cswStatus_ = 2; break; }
        diskOffset_ = lba * 512u; dataLeft_ = count * 512u; diskTransfer_ = true;
        break;
    }
    default: fail(5,0x20); break;
    }
    if (!scsiReply_.empty()) {
        if (!in) { scsiReply_.clear(); cswStatus_ = 2; }
        else dataLeft_ = std::min<uint32_t>(expected,uint32_t(scsiReply_.size()));
    }
    // [B] BOT §6.7 : les transferts en trop restent dans le résidu. Une erreur
    // SCSI OUT est drainée sans écrire ; l'état final est communiqué dans le CSW.
    if (!in && !diskTransfer_) dataLeft_ = expected;
    bot_ = (dataLeft_ || (in && expected)) ? (in ? Bot::DataIn : Bot::DataOut) : Bot::Csw;
}

UsbDevice::Result UsbDevice::bulk(unsigned ep, unsigned pid, uint8_t* data, unsigned length, unsigned& actual) {
    if (bot_ == Bot::Invalid) return Result::Stall;
    if (ep == 2 && pid == 1 && bot_ == Bot::Cbw) {
        if (length != 31 || le32(data) != 0x43425355 || (data[12] & 0x7F) || data[13] ||
            !data[14] || data[14] > 16) { bot_ = Bot::Invalid; halt_ |= 3; return Result::Stall; }
        tag_ = le32(data + 4); residue_ = le32(data + 8);
        scsi(data + 15, data[14], residue_, (data[12] & 0x80) != 0);
        actual = 31; return Result::Ack;
    }
    const bool in = bot_ == Bot::DataIn;
    if ((in && ep == 1 && pid == 2) || (bot_ == Bot::DataOut && ep == 2 && pid == 1)) {
        actual = std::min(length, dataLeft_);
        if (diskTransfer_) {
            bool ok = fseeko(disk_, static_cast<off_t>(diskOffset_), SEEK_SET) == 0;
            if (ok) ok = in ? std::fread(data,1,actual,disk_) == actual : std::fwrite(data,1,actual,disk_) == actual;
            if (!ok) { senseKey_ = 3; senseAsc_ = in ? 0x11 : 0x0C; cswStatus_ = 1; actual = 0; dataLeft_ = 0; bot_ = Bot::Csw; return Result::Ack; }
            diskOffset_ += actual;
        } else if (in) {
            if (actual) std::copy_n(scsiReply_.data() + scsiOffset_, actual, data);
            scsiOffset_ += actual;
        }
        dataLeft_ -= actual;
        if (diskTransfer_ || in) residue_ -= actual;
        if (!dataLeft_) {
            if (diskTransfer_ && !in && std::fflush(disk_) != 0) { senseKey_ = 3; senseAsc_ = 0x0C; cswStatus_ = 1; }
            bot_ = Bot::Csw;
        }
        return Result::Ack;
    }
    if (ep == 1 && pid == 2 && bot_ == Bot::Csw) {
        if (length < 13) return Result::Stall;
        put32(data,0x53425355); put32(data + 4,tag_); put32(data + 8,residue_); data[12] = cswStatus_;
        actual = 13; bot_ = Bot::Cbw; return Result::Ack;
    }
    return Result::Stall;
}

void UsbDevice::serialize(StateArchive& ar) {
    // La configuration et les fichiers ne proviennent jamais d'un save-state.
    // Comme l'ACSI, les secteurs vivent dans l'image hôte ; seul le protocole
    // en vol est restauré. Un autre média/type est refusé avant toute écriture.
    auto kind = kind_; auto blocks = blocks_; auto ro = readOnly_;
    std::vector<uint8_t> path(image_.begin(), image_.end());
    ar(kind); ar(blocks); ar(ro); ar.vec(path);
    ar.check(kind == kind_ && blocks == blocks_ && ro == readOnly_ &&
             std::string(path.begin(),path.end()) == image_, "USB device/media mismatch");
    ar(address_); ar(configuration_); ar(protocol_); ar(idle_); ar(leds_);
    ar(pendingAddress_); ar(pendingConfig_); ar(setAddress_); ar(setConfig_); ar(controlStall_);
    ar.arr(setup_); ar.vec(reply_); ar(replyOffset_);
    for (bool& key : keys_) ar(key);
    ar(buttons_); ar(mouseX_); ar(mouseY_); ar(reportDirty_); ar(idleElapsed_); ar(halt_);
    ar(bot_); ar(tag_); ar(residue_); ar(dataLeft_); ar(diskOffset_);
    ar(cswStatus_); ar(senseKey_); ar(senseAsc_); ar(diskTransfer_); ar.vec(scsiReply_); ar(scsiOffset_);
    ar.check(address_ < 128 && configuration_ <= 1 && protocol_ <= 1 && pendingAddress_ < 128 && pendingConfig_ <= 1, "USB address/configuration");
    ar.check(reply_.size() <= 65535 && replyOffset_ <= reply_.size(), "USB control offset");
    ar.check(scsiOffset_ <= scsiReply_.size() && scsiReply_.size() <= 65535 && uint8_t(bot_) <= uint8_t(Bot::Invalid), "USB BOT state");
    ar.check(!diskTransfer_ || (disk_ && uint64_t(diskOffset_) + dataLeft_ <= uint64_t(blocks_) * 512), "USB disk transfer bounds");
    ar.check(!(diskTransfer_ && bot_ == Bot::DataOut && readOnly_), "USB write to read-only image");
    ar.check(bot_ != Bot::DataIn || diskTransfer_ || uint64_t(scsiOffset_) + dataLeft_ <= scsiReply_.size(), "USB BOT reply bounds");
}
