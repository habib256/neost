// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// NetworkBackend — the host-side transport that carries raw Ethernet
// frames for POM2's Ethernet cards. Shaped after AppleWin's
// `source/Tfe/NetworkBackend.h` (GPL2+), which serves the same role for
// its Uthernet I/II.
//
// Who needs it
// ------------
//   UthernetCard   (CS8900A)  — ALWAYS. The chip is a plain NIC: the
//                               Apple-side software (IP65, Contiki,
//                               ADTPro-ethernet) carries its own TCP/IP
//                               stack and hands whole Ethernet frames to
//                               the card.
//   UthernetIICard (W5100)    — ONLY for MACRAW / IPRAW sockets. Its
//                               TCP and UDP modes are a *hardware* stack,
//                               so POM2 maps those straight onto host BSD
//                               sockets and never builds a frame. That is
//                               why Uthernet II works with no backend at
//                               all (`NullNetworkBackend`) as long as the
//                               guest sticks to TCP/UDP — which every
//                               period IRC / telnet / FTP client does.
//
// Implementations
// ---------------
//   NullNetworkBackend     — always present. `isValid()` is false; frames
//                            written to it are dropped and nothing is ever
//                            received. Keeps the cards pluggable (and
//                            software-detectable) on a build with no
//                            host networking at all.
//   LoopbackNetworkBackend — transmit() feeds receive(). Used by the
//                            pinned smoke tests so they never touch a
//                            real network, and usable from the UI as a
//                            "cable unplugged, talk to yourself" mode.
//   SlirpNetworkBackend    — libslirp user-mode NAT (see
//                            SlirpNetworkBackend.h). Optional build dep.
//   BuiltinNatBackend      — POM2's own user-mode NAT, no library (see
//                            BuiltinNatBackend.h). What a build without
//                            libslirp — Windows — uses instead.
//
// Threading: backends are driven from the CPU thread under
// EmulationController's stateMutex, via SlotPeripheral::advanceCycles.
// They must not block — `receive()` returns <= 0 when nothing is pending
// and `poll()` is expected to use a zero timeout.

#ifndef POM2_NETWORK_BACKEND_H
#define POM2_NETWORK_BACKEND_H

#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace pom2 {

/// 6-byte Ethernet hardware address.
struct MacAddress {
    uint8_t b[6] = { 0, 0, 0, 0, 0, 0 };
};

/// Host transport bounds. The CS8900A itself accepts a three-byte TxLength
/// and normally pads it to 60 data bytes before handing it to this backend.
inline constexpr int kMaxEthFrame = 1518;
inline constexpr int kMinEthFrame = 4;

class NetworkBackend
{
public:
    virtual ~NetworkBackend() = default;

    /// False when the backend cannot actually move packets (no libslirp
    /// in the build, interface open failed, …). Cards still work — they
    /// just never see traffic — so the UI can report the reason instead
    /// of the card vanishing.
    virtual bool isValid() const = 0;

    /// Short human-readable identity for the UI ("libslirp 10.0.2.15",
    /// "loopback", "none").
    virtual std::string_view name() const = 0;

    /// Hand one complete Ethernet frame (dest MAC first, no preamble, no
    /// FCS) to the host side.
    virtual void transmit(const uint8_t* frame, int len) = 0;

    /// Pop one pending frame into `buf` (capacity `cap`). Returns the
    /// frame length, or <= 0 when nothing is queued. Never blocks.
    virtual int receive(uint8_t* buf, int cap) = 0;

    /// Pump the backend's own I/O. Called periodically from the card's
    /// cycle hook; must use a zero timeout.
    virtual void poll() {}

    /// The NIC was reset — the slot's RESET line or a power cycle. Every
    /// socket, flow and connection the guest had through its stack is gone
    /// on the guest side; a backend that relays them for this one guest
    /// (the built-in NAT) drops its side too, instead of holding host
    /// sockets and table slots for connections nobody will ever finish.
    /// Port forwards and configuration survive.
    virtual void nicReset() {}

    /// Resolve an IPv4 address (network byte order, as it sits in the
    /// W5100 registers) to the MAC a frame for it should be addressed to.
    /// Returns false when unknown — the caller then falls back to
    /// broadcast, which is what the real Uthernet II does while its ARP
    /// is outstanding. Only IPRAW sockets need this.
    virtual bool resolveMac(uint32_t /*ipv4NetworkOrder*/, MacAddress& /*out*/)
    {
        return false;
    }

    /// Frames handed to transmit() since construction / last reset.
    uint64_t framesSent() const { return framesSent_; }
    /// Frames handed out by receive().
    uint64_t framesReceived() const { return framesReceived_; }
    /// Frames dropped because a queue was full.
    uint64_t framesDropped() const { return framesDropped_; }

protected:
    uint64_t framesSent_     = 0;
    uint64_t framesReceived_ = 0;
    uint64_t framesDropped_  = 0;
};

/// No host networking. Everything written is dropped; nothing arrives.
class NullNetworkBackend final : public NetworkBackend
{
public:
    bool isValid() const override { return false; }
    std::string_view name() const override { return "none"; }
    void transmit(const uint8_t*, int) override { ++framesDropped_; }
    int  receive(uint8_t*, int) override { return 0; }
};

/// Everything transmitted comes straight back on receive(). Lets the
/// smoke tests exercise the full CS8900A TX→RX path (and the W5100's
/// MACRAW path) with zero host network access.
class LoopbackNetworkBackend final : public NetworkBackend
{
public:
    /// Cap on queued frames — same backstop MAME puts on its CS8900A
    /// inbound queue (`machine/cs8900a.cpp:39`), so a guest that
    /// transmits without ever draining can't grow the heap without bound.
    static constexpr size_t kMaxQueued = 4096;

    bool isValid() const override { return true; }
    std::string_view name() const override { return "loopback"; }

    void transmit(const uint8_t* frame, int len) override
    {
        if (len < kMinEthFrame || len > kMaxEthFrame) { ++framesDropped_; return; }
        if (queue_.size() >= kMaxQueued) { queue_.pop_front(); ++framesDropped_; }
        queue_.emplace_back(frame, frame + len);
        ++framesSent_;
    }

    int receive(uint8_t* buf, int cap) override
    {
        if (queue_.empty()) return 0;
        const std::vector<uint8_t>& f = queue_.front();
        const int n = static_cast<int>(f.size());
        if (n > cap) { queue_.pop_front(); ++framesDropped_; return 0; }
        std::memcpy(buf, f.data(), static_cast<size_t>(n));
        queue_.pop_front();
        ++framesReceived_;
        return n;
    }

    /// Loopback answers every ARP with the locally-administered MAC
    /// 02:00:<ip>, so IPRAW frames built against it are well-formed and
    /// round-trip through transmit()/receive() unchanged.
    bool resolveMac(uint32_t ipv4NetworkOrder, MacAddress& out) override
    {
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&ipv4NetworkOrder);
        out.b[0] = 0x02; out.b[1] = 0x00;
        out.b[2] = ip[0]; out.b[3] = ip[1]; out.b[4] = ip[2]; out.b[5] = ip[3];
        return true;
    }

    size_t queued() const { return queue_.size(); }

private:
    std::deque<std::vector<uint8_t>> queue_;
};

} // namespace pom2

#endif // POM2_NETWORK_BACKEND_H
