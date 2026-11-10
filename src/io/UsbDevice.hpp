// Périphériques USB virtuels du NetUSBee. Aucun accès USB brut de l'hôte.
// [B] USB 2.0 (27/04/2000), chap. 9 ; HID 1.11 (27/06/2001), §7 et annexe B ;
// Mass Storage Bulk-Only 1.0 (31/08/1999), §5-6. Références : docs/NETUSBEE.md.
#pragma once
#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include "core/StateArchive.hpp"

class UsbDevice {
public:
    enum class Kind : uint8_t { None, Keyboard, Mouse, Storage };
    enum class Result { Ack, Nak, Stall };
    ~UsbDevice();
    UsbDevice() = default;
    UsbDevice(const UsbDevice&) = delete;
    UsbDevice& operator=(const UsbDevice&) = delete;

    // Le montage est transactionnel ; une erreur conserve le périphérique courant.
    bool attach(Kind kind, const std::string& image = {}, bool readOnly = false);
    void reset();                         // reset du bus USB, conserve le média
    Kind kind() const { return kind_; }
    uint8_t address() const { return address_; }
    const std::string& image() const { return image_; }
    bool readOnly() const { return readOnly_; }
    static bool parseSpec(const std::string& spec, Kind& kind, std::string& image, bool& readOnly);
    Result transfer(unsigned endpoint, unsigned pid, uint8_t* data, unsigned length,
                    unsigned& actual, unsigned maxPacket);
    void keyEvent(uint8_t usage, bool down);
    void mouseEvent(int dx, int dy, bool left, bool right);
    void tick();                          // 1 ms USB, pour SET_IDLE
    void serialize(StateArchive& ar);

private:
    Kind kind_ = Kind::None;
    FILE* disk_ = nullptr;
    std::string image_;
    uint32_t blocks_ = 0;
    bool readOnly_ = false;
    uint8_t address_ = 0, configuration_ = 0, protocol_ = 1, idle_ = 0, leds_ = 0;
    uint8_t pendingAddress_ = 0, pendingConfig_ = 0;
    bool setAddress_ = false, setConfig_ = false, controlStall_ = false;
    uint8_t setup_[8] = {};
    std::vector<uint8_t> reply_;
    uint32_t replyOffset_ = 0;
    bool keys_[256] = {};
    uint8_t buttons_ = 0;
    int32_t mouseX_ = 0, mouseY_ = 0;
    bool reportDirty_ = true;
    uint16_t idleElapsed_ = 0;
    uint8_t halt_ = 0;                    // EP1 IN / EP2 OUT

    // BOT : CBW -> DataIn/DataOut -> CSW ; Invalid attend le reset de classe.
    enum class Bot : uint8_t { Cbw, DataIn, DataOut, Csw, Invalid };
    Bot bot_ = Bot::Cbw;
    uint32_t tag_ = 0, residue_ = 0, dataLeft_ = 0, diskOffset_ = 0;
    uint8_t cswStatus_ = 0, senseKey_ = 0, senseAsc_ = 0;
    bool diskTransfer_ = false;
    std::vector<uint8_t> scsiReply_;
    uint32_t scsiOffset_ = 0;

    void setup();
    std::vector<uint8_t> descriptor(unsigned type, unsigned index) const;
    std::vector<uint8_t> report(bool consume);
    Result control(unsigned pid, uint8_t* data, unsigned length, unsigned& actual);
    Result bulk(unsigned endpoint, unsigned pid, uint8_t* data, unsigned length, unsigned& actual);
    void scsi(const uint8_t* cdb, unsigned cdbLength, uint32_t expected, bool in);
};
