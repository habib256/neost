// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatPacket — the headers POM2's built-in virtual network reads and writes:
// Ethernet II, ARP, IPv4, ICMP echo, UDP and TCP.
//
// The built-in network (BuiltinNatBackend.h) is the far side of the wire for
// a guest that carries its own IP stack. It has to take the guest's frames
// apart and put replies together; this file is that and nothing else — no
// socket, no clock, no policy.
//
// Parsers return false for anything truncated, malformed or failing its
// checksum; the caller drops the frame, as a real host's stack would. Fields
// are big-endian on the wire and HOST byte order in the structures.
//
// [B] RFC 791 (IPv4), RFC 826 (ARP), RFC 792 (ICMP), RFC 768 (UDP),
//     RFC 793 (TCP), RFC 1071 (the Internet checksum), RFC 894 (IP over
//     Ethernet — the 46-byte minimum payload, hence the padding to 60).
// [D] Structure after Casso `Devices/Network/PacketCodec` @ 2d899adf (MIT);
//     rewritten for C++17 and POM2's types. One deliberate difference: an
//     IPv4 header WITH options is accepted and its options skipped (Casso
//     refuses it) — RFC 791 makes options legal on any datagram.

#ifndef POM2_NAT_PACKET_H
#define POM2_NAT_PACKET_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <tuple>

namespace nat {
namespace detail {

using Mac = std::array<uint8_t, 6>;
using Bytes = std::vector<uint8_t>;
using Ipv6 = std::array<uint8_t, 16>;
inline constexpr uint16_t kEtherTypeIpv6 = 0x86DD;
inline constexpr uint8_t kProtoIcmp6 = 58;

inline constexpr Mac kBroadcastMac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

inline constexpr uint16_t kEtherTypeIpv4 = 0x0800;
inline constexpr uint16_t kEtherTypeArp  = 0x0806;
inline constexpr uint16_t kArpRequest    = 1;
inline constexpr uint16_t kArpReply      = 2;
inline constexpr uint8_t  kProtoIcmp     = 1;
inline constexpr uint8_t  kProtoTcp      = 6;
inline constexpr uint8_t  kProtoUdp      = 17;
inline constexpr uint8_t  kIcmpEchoReply   = 0;
inline constexpr uint8_t  kIcmpEchoRequest = 8;

inline constexpr uint8_t kTcpFin = 0x01;
inline constexpr uint8_t kTcpSyn = 0x02;
inline constexpr uint8_t kTcpRst = 0x04;
inline constexpr uint8_t kTcpPsh = 0x08;
inline constexpr uint8_t kTcpAck = 0x10;

inline constexpr std::size_t kEthernetHeader = 14;
inline constexpr std::size_t kMinimumFrame   = 60;    ///< without the FCS
inline constexpr std::size_t kIpv4Header     = 20;
inline constexpr std::size_t kUdpHeader      = 8;
inline constexpr std::size_t kTcpHeader      = 20;
inline constexpr std::size_t kIcmpHeader     = 8;
inline constexpr std::size_t kArpLength      = 28;
inline constexpr uint8_t     kDefaultTtl     = 64;

/// A view into a buffer the caller keeps alive.
struct View {
    const uint8_t* data = nullptr;
    std::size_t    size = 0;
};
struct Ipv6Packet {
    Ipv6 source{}, destination{};
    uint8_t protocol = 0, hopLimit = 0;
    View payload;
};
bool parseIpv6(View packet, Ipv6Packet& out);
uint16_t checksum6(const Ipv6& source, const Ipv6& destination, uint8_t protocol, View data);
Bytes buildIpv6(const Ipv6& source, const Ipv6& destination, uint8_t protocol,
                 const Bytes& payload, uint8_t hopLimit = 64);
std::vector<Bytes> fragmentIpv6(const Ipv6& source, const Ipv6& destination,
                               uint8_t protocol, uint32_t id, const Bytes& payload);
class Ipv6Reassembler {
public:
    Bytes accept(View packet, uint64_t nowMs);
    void expire(uint64_t nowMs);
private:
    struct Assembly {
        uint64_t started = 0;
        uint8_t protocol = 0, hop = 0;
        Bytes data, present;
        std::size_t end = 0, count = 0;
        bool first = false, last = false;
    };
    std::map<std::tuple<Ipv6, Ipv6, uint32_t>, Assembly> pending_;
};

struct EthernetFrame {
    Mac      destination{};
    Mac      source{};
    uint16_t etherType = 0;
    View     payload;
};

struct ArpPacket {
    uint16_t operation = 0;
    Mac      senderMac{};
    uint32_t senderIp = 0;
    Mac      targetMac{};
    uint32_t targetIp = 0;
};

struct Ipv4Packet {
    uint32_t source = 0;
    uint32_t destination = 0;
    uint8_t  protocol = 0;
    View     payload;
};

struct UdpDatagram {
    uint16_t sourcePort = 0;
    uint16_t destinationPort = 0;
    View     payload;
};

/// A TCP segment with its addresses, so the relay can pass one around whole.
/// Only the MSS option is understood; `mss` 0 means the option is absent.
struct TcpSegment {
    uint32_t sourceIp = 0;
    uint32_t destinationIp = 0;
    uint16_t sourcePort = 0;
    uint16_t destinationPort = 0;
    uint32_t sequence = 0;
    uint32_t acknowledgment = 0;
    uint8_t  flags = 0;
    uint16_t window = 0;
    uint16_t mss = 0;
    Bytes    payload;
};

uint16_t rd16(const uint8_t* p);
uint32_t rd32(const uint8_t* p);
void     wr16(uint8_t* p, uint16_t v);
void     wr32(uint8_t* p, uint32_t v);

/// Internet checksum (RFC 1071) of `data`, continuing from a partial
/// one's-complement `sum`; returns the final complemented value. A buffer
/// that already carries a correct checksum sums to 0.
uint16_t checksum(const uint8_t* data, std::size_t size, uint32_t sum = 0);

bool parseEthernet(const uint8_t* frame, std::size_t size, EthernetFrame& out);
bool parseArp(View payload, ArpPacket& out);
/// Whole datagrams only. The backend calls Ipv4Reassembler first.
bool parseIpv4(View payload, Ipv4Packet& out);
/// Bounded IPv4 reassembly; conflicting overlaps discard the entire datagram.
/// [B] RFC 791 § 3.2. Assemblies expire from their first fragment, not activity.
class Ipv4Reassembler {
public:
    static constexpr uint64_t kTimeoutMs = 30000;
    static constexpr std::size_t kMaxPending = 32;
    Bytes accept(View packet, uint64_t nowMs);
    void expire(uint64_t nowMs);
    std::size_t pending() const { return pending_.size(); }
private:
    struct Assembly {
        uint64_t started = 0;
        Bytes data, present;
        std::size_t end = 0, count = 0;
        bool first = false, last = false;
    };
    std::map<std::tuple<uint32_t, uint32_t, uint8_t, uint16_t>, Assembly> pending_;
};
std::vector<Bytes> fragmentIpv4(uint32_t source, uint32_t destination, uint8_t protocol,
                                uint16_t id, const Bytes& payload, std::size_t mtu = 1500);
bool parseUdp(const Ipv4Packet& packet, UdpDatagram& out);
bool parseTcp(const Ipv4Packet& packet, TcpSegment& out);

/// Pads to the 60-byte Ethernet minimum.
Bytes buildEthernet(const Mac& destination, const Mac& source,
                    uint16_t etherType, const Bytes& payload);
Bytes buildArp(const ArpPacket& packet);
Bytes buildIpv4(uint32_t source, uint32_t destination, uint8_t protocol,
                uint16_t identification, const Bytes& payload);
Bytes buildUdp(uint32_t sourceIp, uint32_t destinationIp, uint16_t sourcePort,
               uint16_t destinationPort, const uint8_t* data, std::size_t size);
Bytes buildTcp(const TcpSegment& segment);
/// The reply to an ICMP echo request (`request` is the ICMP message).
Bytes buildIcmpEchoReply(View request);

}  // namespace detail
}  // namespace nat

#endif  // POM2_NAT_PACKET_H
