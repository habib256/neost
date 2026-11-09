// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatDhcp — see the header.

#include "NatDhcp.h"

#include <algorithm>
#include <cstddef>

namespace pom2 {
namespace nat {
namespace {

// BOOTP fixed fields (RFC 2131 § 2), then the magic cookie and the options
// (RFC 2132): each a code, a length and the value; 0 pads, 255 ends.
constexpr std::size_t kOp = 0, kHtype = 1, kHlen = 2, kXid = 4, kFlags = 10,
                      kYiaddr = 16, kSiaddr = 20, kChaddr = 28, kCookie = 236,
                      kOptions = 240, kMinimumReply = 300;
constexpr uint32_t kMagicCookie = 0x63825363;
constexpr uint8_t  kBootRequest = 1, kBootReply = 2, kEthernet = 1, kMacLength = 6;
constexpr uint8_t  kDiscover = 1, kOffer = 2, kRequest = 3, kAck = 5;
constexpr uint8_t  kOptPad = 0, kOptSubnetMask = 1, kOptRouter = 3, kOptDns = 6,
                   kOptLeaseTime = 51, kOptMessageType = 53, kOptServerId = 54,
                   kOptEnd = 255;

/// Option 53, or 0 when the options end without it or run off the packet.
uint8_t messageType(View r)
{
    std::size_t i = kOptions;
    while (i < r.size && r.data[i] != kOptEnd) {
        if (r.data[i] == kOptPad) { ++i; continue; }
        if (i + 1 >= r.size) break;
        if (r.data[i] == kOptMessageType && r.data[i + 1] == 1 && i + 2 < r.size)
            return r.data[i + 2];
        i += 2 + static_cast<std::size_t>(r.data[i + 1]);
    }
    return 0;
}

void option8(Bytes& out, uint8_t code, uint8_t value)
{
    out.push_back(code);
    out.push_back(1);
    out.push_back(value);
}

void option32(Bytes& out, uint8_t code, uint32_t value)
{
    out.push_back(code);
    out.push_back(4);
    const std::size_t at = out.size();
    out.resize(at + 4);
    wr32(out.data() + at, value);
}

}  // namespace

Bytes dhcpReply(View r, const DhcpLease& lease)
{
    if (r.size < kOptions || r.data[kOp] != kBootRequest || r.data[kHtype] != kEthernet ||
        r.data[kHlen] != kMacLength || rd32(r.data + kCookie) != kMagicCookie)
        return {};

    const uint8_t asked = messageType(r);
    uint8_t answer = 0;
    if (asked == kDiscover)     answer = kOffer;
    else if (asked == kRequest) answer = kAck;
    else return {};

    Bytes out(kOptions, 0);
    out[kOp]    = kBootReply;
    out[kHtype] = kEthernet;
    out[kHlen]  = kMacLength;
    std::copy_n(r.data + kXid,    4,  out.begin() + kXid);
    std::copy_n(r.data + kFlags,  2,  out.begin() + kFlags);
    std::copy_n(r.data + kChaddr, 16, out.begin() + kChaddr);
    wr32(out.data() + kYiaddr, lease.clientIp);
    wr32(out.data() + kSiaddr, lease.router);
    wr32(out.data() + kCookie, kMagicCookie);
    option8 (out, kOptMessageType, answer);
    option32(out, kOptServerId,    lease.router);
    option32(out, kOptLeaseTime,   lease.leaseSeconds);
    option32(out, kOptSubnetMask,  lease.subnetMask);
    option32(out, kOptRouter,      lease.router);
    option32(out, kOptDns,         lease.dnsServer);
    out.push_back(kOptEnd);
    if (out.size() < kMinimumReply) out.resize(kMinimumReply, kOptPad);
    return out;
}

}  // namespace nat
}  // namespace pom2
