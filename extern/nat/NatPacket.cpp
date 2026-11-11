// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatPacket — see the header.

#include "NatPacket.h"

#include <algorithm>
#include <cstring>

namespace nat {
namespace detail {

uint16_t rd16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t rd32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |  static_cast<uint32_t>(p[3]);
}

void wr16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

void wr32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

namespace {

uint32_t sumWords(const uint8_t* data, std::size_t size, uint32_t sum)
{
    std::size_t i = 0;
    for (; i + 1 < size; i += 2) sum += rd16(data + i);
    if (i < size) sum += static_cast<uint32_t>(data[i]) << 8;   // odd tail, zero-padded
    return sum;
}

/// The TCP/UDP pseudo-header (RFC 793 § 3.1, RFC 768).
uint32_t sumPseudoHeader(uint32_t source, uint32_t destination, uint8_t protocol,
                         std::size_t length)
{
    uint32_t sum = 0;
    sum += source >> 16;      sum += source & 0xFFFF;
    sum += destination >> 16; sum += destination & 0xFFFF;
    sum += protocol;
    sum += static_cast<uint32_t>(length);
    return sum;
}

uint16_t fold(uint32_t sum)
{
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

}  // namespace

uint16_t checksum(const uint8_t* data, std::size_t size, uint32_t sum)
{
    return fold(sumWords(data, size, sum));
}

bool parseEthernet(const uint8_t* frame, std::size_t size, EthernetFrame& out)
{
    if (!frame || size < kEthernetHeader) return false;
    std::memcpy(out.destination.data(), frame, 6);
    std::memcpy(out.source.data(), frame + 6, 6);
    out.etherType = rd16(frame + 12);
    out.payload   = { frame + kEthernetHeader, size - kEthernetHeader };
    return true;
}

bool parseArp(View p, ArpPacket& out)
{
    if (p.size < kArpLength) return false;
    // Ethernet (1) carrying IPv4 (0x0800), 6-byte and 4-byte addresses.
    if (rd16(p.data) != 1 || rd16(p.data + 2) != kEtherTypeIpv4 ||
        p.data[4] != 6 || p.data[5] != 4)
        return false;
    out.operation = rd16(p.data + 6);
    std::memcpy(out.senderMac.data(), p.data + 8, 6);
    out.senderIp = rd32(p.data + 14);
    std::memcpy(out.targetMac.data(), p.data + 18, 6);
    out.targetIp = rd32(p.data + 24);
    return true;
}

bool parseIpv4(View p, Ipv4Packet& out)
{
    if (p.size < kIpv4Header) return false;
    if ((p.data[0] >> 4) != 4) return false;
    const std::size_t headerLength = static_cast<std::size_t>(p.data[0] & 0x0F) * 4;
    if (headerLength < kIpv4Header || headerLength > p.size) return false;
    const std::size_t totalLength = rd16(p.data + 2);
    // A frame is padded to 60 bytes, so the buffer may be LONGER than the
    // datagram; it may never be shorter.
    if (totalLength < headerLength || totalLength > p.size) return false;
    // More-fragments set, or a non-zero offset: a fragment.
    if ((rd16(p.data + 6) & 0x3FFF) != 0) return false;
    if (checksum(p.data, headerLength) != 0) return false;
    out.protocol    = p.data[9];
    out.source      = rd32(p.data + 12);
    out.destination = rd32(p.data + 16);
    out.payload     = { p.data + headerLength, totalLength - headerLength };
    return true;
}

void Ipv4Reassembler::expire(uint64_t nowMs)
{
    for (auto it = pending_.begin(); it != pending_.end();)
        if (nowMs - it->second.started >= kTimeoutMs) it = pending_.erase(it);
        else ++it;
}

Bytes Ipv4Reassembler::accept(View p, uint64_t nowMs)
{
    expire(nowMs);
    if (p.size < 20 || p.data[0] >> 4 != 4) return {};
    const std::size_t h = (p.data[0] & 15) * 4, total = rd16(p.data + 2);
    if (h < 20 || h > p.size || total < h || total > p.size || checksum(p.data, h)) return {};
    const uint16_t flags = rd16(p.data + 6);
    if (flags & 0x8000) return {};
    if (!(flags & 0x3fff)) return Bytes(p.data, p.data + total);
    const std::size_t offset = (flags & 0x1fff) * 8, size = total - h;
    const bool more = flags & 0x2000;
    const auto key = std::make_tuple(rd32(p.data + 12), rd32(p.data + 16), p.data[9], rd16(p.data + 4));
    if ((flags & 0x4000) || size == 0 || offset + size > 65515 || (more && size % 8)) {
        pending_.erase(key); return {};
    }
    if (!pending_.count(key) && pending_.size() >= kMaxPending) return {};
    auto inserted = pending_.try_emplace(key);
    Assembly& a = inserted.first->second;
    if (inserted.second) a.started = nowMs;
    const auto end = offset + size;
    if ((a.last && (end > a.end || (!more && end != a.end))) ||
        (!more && a.data.size() > end)) { pending_.erase(key); return {}; }
    a.data.resize(std::max(a.data.size(), end));
    a.present.resize(a.data.size());
    for (std::size_t i = 0; i < size; ++i) {
        if (a.present[offset + i] && a.data[offset + i] != p.data[h + i]) {
            pending_.erase(key); return {};
        }
        if (!a.present[offset + i]) { a.present[offset + i] = 1; ++a.count; }
        a.data[offset + i] = p.data[h + i];
    }
    if (offset == 0) a.first = true;
    if (!more) { a.last = true; a.end = end; }
    if (!a.first || !a.last || a.count != a.end) return {};
    Bytes out = buildIpv4(std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key), a.data);
    pending_.erase(key);
    return out;
}

std::vector<Bytes> fragmentIpv4(uint32_t source, uint32_t destination, uint8_t protocol,
                               uint16_t id, const Bytes& payload, std::size_t mtu)
{
    if (mtu < 28 || payload.size() > 65515) return {};
    if (payload.size() + 20 <= mtu) return {buildIpv4(source, destination, protocol, id, payload)};
    const auto chunk = ((mtu - 20) / 8) * 8;
    std::vector<Bytes> out;
    for (std::size_t at = 0; at < payload.size(); at += chunk) {
        const auto size = std::min(chunk, payload.size() - at);
        Bytes p = buildIpv4(source, destination, protocol, id, Bytes(payload.begin() + at, payload.begin() + at + size));
        wr16(p.data() + 6, static_cast<uint16_t>(at / 8 | (at + size < payload.size() ? 0x2000 : 0)));
        wr16(p.data() + 10, 0);
        wr16(p.data() + 10, checksum(p.data(), 20));
        out.push_back(std::move(p));
    }
    return out;
}

bool parseUdp(const Ipv4Packet& packet, UdpDatagram& out)
{
    const View p = packet.payload;
    if (p.size < kUdpHeader) return false;
    const std::size_t length = rd16(p.data + 4);
    if (length < kUdpHeader || length > p.size) return false;
    // A zero checksum means the sender computed none (RFC 768).
    if (rd16(p.data + 6) != 0 &&
        checksum(p.data, length,
                 sumPseudoHeader(packet.source, packet.destination, kProtoUdp, length)) != 0)
        return false;
    out.sourcePort      = rd16(p.data);
    out.destinationPort = rd16(p.data + 2);
    out.payload         = { p.data + kUdpHeader, length - kUdpHeader };
    return true;
}

bool parseTcp(const Ipv4Packet& packet, TcpSegment& out)
{
    const View p = packet.payload;
    if (p.size < kTcpHeader) return false;
    const std::size_t headerLength = static_cast<std::size_t>(p.data[12] >> 4) * 4;
    if (headerLength < kTcpHeader || headerLength > p.size) return false;
    if (checksum(p.data, p.size,
                 sumPseudoHeader(packet.source, packet.destination, kProtoTcp, p.size)) != 0)
        return false;

    out = TcpSegment{};
    out.sourceIp        = packet.source;
    out.destinationIp   = packet.destination;
    out.sourcePort      = rd16(p.data);
    out.destinationPort = rd16(p.data + 2);
    out.sequence        = rd32(p.data + 4);
    out.acknowledgment  = rd32(p.data + 8);
    out.flags           = p.data[13];
    out.window          = rd16(p.data + 14);

    // Options: kind 0 ends the list, 1 is a one-byte pad, anything else
    // carries its own length. Only MSS (kind 2, length 4) is read.
    for (std::size_t i = kTcpHeader; i < headerLength;) {
        const uint8_t kind = p.data[i];
        if (kind == 0) break;
        if (kind == 1) { ++i; continue; }
        if (i + 1 >= headerLength) break;
        const std::size_t optionLength = p.data[i + 1];
        if (optionLength < 2 || i + optionLength > headerLength) break;
        if (kind == 2 && optionLength == 4) out.mss = rd16(p.data + i + 2);
        i += optionLength;
    }
    out.payload.assign(p.data + headerLength, p.data + p.size);
    return true;
}

Bytes buildEthernet(const Mac& destination, const Mac& source, uint16_t etherType,
                    const Bytes& payload)
{
    Bytes frame(std::max(kEthernetHeader + payload.size(), kMinimumFrame), 0);
    std::memcpy(frame.data(), destination.data(), 6);
    std::memcpy(frame.data() + 6, source.data(), 6);
    wr16(frame.data() + 12, etherType);
    if (!payload.empty())
        std::memcpy(frame.data() + kEthernetHeader, payload.data(), payload.size());
    return frame;
}

Bytes buildArp(const ArpPacket& packet)
{
    Bytes p(kArpLength, 0);
    wr16(p.data(), 1);
    wr16(p.data() + 2, kEtherTypeIpv4);
    p[4] = 6;
    p[5] = 4;
    wr16(p.data() + 6, packet.operation);
    std::memcpy(p.data() + 8, packet.senderMac.data(), 6);
    wr32(p.data() + 14, packet.senderIp);
    std::memcpy(p.data() + 18, packet.targetMac.data(), 6);
    wr32(p.data() + 24, packet.targetIp);
    return p;
}

Bytes buildIpv4(uint32_t source, uint32_t destination, uint8_t protocol,
                uint16_t identification, const Bytes& payload)
{
    Bytes p(kIpv4Header + payload.size(), 0);
    p[0] = 0x45;                                            // v4, 20-byte header
    wr16(p.data() + 2, static_cast<uint16_t>(p.size()));
    wr16(p.data() + 4, identification);
    wr16(p.data() + 6, 0x4000);                             // don't fragment
    p[8] = kDefaultTtl;
    p[9] = protocol;
    wr32(p.data() + 12, source);
    wr32(p.data() + 16, destination);
    wr16(p.data() + 10, checksum(p.data(), kIpv4Header));
    if (!payload.empty())
        std::memcpy(p.data() + kIpv4Header, payload.data(), payload.size());
    return p;
}

Bytes buildUdp(uint32_t sourceIp, uint32_t destinationIp, uint16_t sourcePort,
               uint16_t destinationPort, const uint8_t* data, std::size_t size)
{
    Bytes p(kUdpHeader + size, 0);
    wr16(p.data(), sourcePort);
    wr16(p.data() + 2, destinationPort);
    wr16(p.data() + 4, static_cast<uint16_t>(p.size()));
    if (size) std::memcpy(p.data() + kUdpHeader, data, size);
    uint16_t sum = checksum(p.data(), p.size(),
                            sumPseudoHeader(sourceIp, destinationIp, kProtoUdp, p.size()));
    if (sum == 0) sum = 0xFFFF;                             // 0 would mean "none"
    wr16(p.data() + 6, sum);
    return p;
}

Bytes buildTcp(const TcpSegment& s)
{
    const std::size_t headerLength = kTcpHeader + (s.mss != 0 ? 4 : 0);
    Bytes p(headerLength + s.payload.size(), 0);
    wr16(p.data(), s.sourcePort);
    wr16(p.data() + 2, s.destinationPort);
    wr32(p.data() + 4, s.sequence);
    wr32(p.data() + 8, s.acknowledgment);
    p[12] = static_cast<uint8_t>((headerLength / 4) << 4);
    p[13] = s.flags;
    wr16(p.data() + 14, s.window);
    if (s.mss != 0) {
        p[kTcpHeader]     = 2;
        p[kTcpHeader + 1] = 4;
        wr16(p.data() + kTcpHeader + 2, s.mss);
    }
    if (!s.payload.empty())
        std::memcpy(p.data() + headerLength, s.payload.data(), s.payload.size());
    wr16(p.data() + 16,
         checksum(p.data(), p.size(),
                  sumPseudoHeader(s.sourceIp, s.destinationIp, kProtoTcp, p.size())));
    return p;
}

Bytes buildIcmpEchoReply(View request)
{
    Bytes p(request.data, request.data + request.size);
    if (p.size() < kIcmpHeader) return {};
    p[0] = kIcmpEchoReply;
    p[2] = p[3] = 0;
    wr16(p.data() + 2, checksum(p.data(), p.size()));
    return p;
}

// [B] RFC 8200 §§ 3, 4, 8: bounded extension walking and IPv6 pseudo-header.
bool parseIpv6(View p, Ipv6Packet& out)
{
    if (p.size < 40 || p.data[0] >> 4 != 6) return false;
    const std::size_t end = 40 + rd16(p.data + 4);
    if (end > p.size) return false;
    std::copy_n(p.data + 8, 16, out.source.begin());
    std::copy_n(p.data + 24, 16, out.destination.begin());
    out.hopLimit = p.data[7];
    uint8_t next = p.data[6];
    std::size_t at = 40;
    for (int n = 0; next == 0 || next == 60 || next == 43; ++n) {
        if (n >= 8 || at + 8 > end) return false;
        // Routing headers with nonzero Segments Left require routing, which
        // this endpoint must not perform (nor use to bypass the perimeter).
        if (next == 43 && p.data[at + 3]) return false;
        const auto size = (std::size_t(p.data[at + 1]) + 1) * 8;
        if (at + size > end || (next == 0 && at != 40)) return false;
        // Unknown options whose action bits require discard are not ignored.
        if (next != 43) {
            for (std::size_t i = at + 2; i < at + size;) {
                if (!p.data[i]) { ++i; continue; }
                if (i + 2 > at + size || i + 2 + p.data[i + 1] > at + size ||
                    (p.data[i] & 0xc0)) return false;
                i += 2 + p.data[i + 1];
            }
        }
        next = p.data[at]; at += size;
    }
    out.protocol = next;
    out.payload = {p.data + at, end - at};
    return out.hopLimit != 0;
}

uint16_t checksum6(const Ipv6& source, const Ipv6& destination, uint8_t protocol, View p)
{
    uint32_t sum = protocol + static_cast<uint32_t>(p.size);
    for (int i = 0; i < 16; i += 2) sum += rd16(source.data() + i) + rd16(destination.data() + i);
    return checksum(p.data, p.size, sum);
}

Bytes buildIpv6(const Ipv6& source, const Ipv6& destination, uint8_t protocol,
                const Bytes& payload, uint8_t hopLimit)
{
    if (payload.size() > 65535) return {};
    Bytes out(40 + payload.size());
    out[0] = 0x60;
    wr16(out.data() + 4, static_cast<uint16_t>(payload.size()));
    out[6] = protocol; out[7] = hopLimit;
    std::copy(source.begin(), source.end(), out.begin() + 8);
    std::copy(destination.begin(), destination.end(), out.begin() + 24);
    std::copy(payload.begin(), payload.end(), out.begin() + 40);
    return out;
}

std::vector<Bytes> fragmentIpv6(const Ipv6& source, const Ipv6& destination,
                               uint8_t protocol, uint32_t id, const Bytes& payload)
{
    if (payload.size() > 65535) return {};
    if (payload.size() <= 1460) return {buildIpv6(source, destination, protocol, payload)};
    std::vector<Bytes> out;
    constexpr std::size_t chunk = 1448;
    for (std::size_t at = 0; at < payload.size(); at += chunk) {
        const auto size = std::min(chunk, payload.size() - at);
        Bytes fragment(8 + size);
        fragment[0] = protocol;
        wr16(fragment.data() + 2, static_cast<uint16_t>(at | (at + size < payload.size() ? 1 : 0)));
        wr32(fragment.data() + 4, id);
        std::copy_n(payload.data() + at, size, fragment.data() + 8);
        out.push_back(buildIpv6(source, destination, 44, fragment));
    }
    return out;
}

void Ipv6Reassembler::expire(uint64_t now)
{
    for (auto it = pending_.begin(); it != pending_.end();)
        if (now - it->second.started >= 60000) it = pending_.erase(it);
        else ++it;
}

Bytes Ipv6Reassembler::accept(View p, uint64_t now)
{
    expire(now);
    Ipv6Packet ip;
    if (!parseIpv6(p, ip)) return {};
    if (ip.protocol != 44) return Bytes(p.data, p.data + 40 + rd16(p.data + 4));
    if (ip.payload.size < 8) return {};
    const auto* f = ip.payload.data;
    const auto key = std::make_tuple(ip.source, ip.destination, rd32(f + 4));
    const auto bits = rd16(f + 2);
    const std::size_t offset = bits & 0xfff8, size = ip.payload.size - 8;
    const bool more = bits & 1;
    // [B] RFC 8200 § 4.5, RFC 5722: never accept overlapping IPv6 fragments.
    if (f[1] || (bits & 6) || !size || offset + size > 65535 || (more && size % 8) ||
        (f[0] != kProtoTcp && f[0] != kProtoUdp && f[0] != kProtoIcmp6) ||
        (offset == 0 && size < (f[0] == kProtoTcp ? 20u : 8u)) ||
        (offset == 0 && f[0] == kProtoIcmp6 && size && f[8] >= 133 && f[8] <= 137)) {
        pending_.erase(key); return {};
    }
    if (!offset && !more) return buildIpv6(ip.source, ip.destination, f[0], Bytes(f + 8, f + 8 + size), ip.hopLimit);
    if (!pending_.count(key) && pending_.size() >= 32) return {};
    auto insertion = pending_.try_emplace(key);
    auto& a = insertion.first->second;
    if (insertion.second) { a.started = now; a.protocol = f[0]; a.hop = ip.hopLimit; }
    const auto end = offset + size;
    if (a.protocol != f[0] || (a.last && (end > a.end || (!more && end != a.end))) ||
        (!more && a.data.size() > end)) { pending_.erase(key); return {}; }
    a.data.resize(std::max(a.data.size(), end));
    a.present.resize(a.data.size());
    for (std::size_t i = offset; i < end; ++i) {
        if (a.present[i]) { pending_.erase(key); return {}; }
        a.present[i] = 1; ++a.count; a.data[i] = f[8 + i - offset];
    }
    if (!offset) { a.first = true; a.hop = ip.hopLimit; }
    if (!more) { a.last = true; a.end = end; }
    if (!a.first || !a.last || a.count != a.end) return {};
    Bytes result = buildIpv6(ip.source, ip.destination, a.protocol, a.data, a.hop);
    pending_.erase(key);
    return result;
}

}  // namespace detail
}  // namespace nat
