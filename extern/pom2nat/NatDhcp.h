// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatDhcp — the built-in virtual network's DHCP server: one guest, one fixed
// lease. A DISCOVER gets an OFFER and a REQUEST an ACK, both carrying the
// guest address, the subnet mask, the router, the DNS server and the lease
// time. Anything else gets no reply.
//
// [B] RFC 2131 (the BOOTP fixed fields, the 300-byte minimum reply a BOOTP
//     relay may assume) and RFC 2132 (the options).
// [D] After Casso `Devices/Network/DhcpServer` @ 2d899adf (MIT).

#ifndef POM2_NAT_DHCP_H
#define POM2_NAT_DHCP_H

#include "NatPacket.h"

#include <cstdint>

namespace pom2 {
namespace nat {

inline constexpr uint16_t kDhcpServerPort = 67;
inline constexpr uint16_t kDhcpClientPort = 68;

struct DhcpLease {
    uint32_t clientIp   = 0;
    uint32_t subnetMask = 0;
    uint32_t router     = 0;
    uint32_t dnsServer  = 0;
    uint32_t leaseSeconds = 86400;
};

/// The BOOTP payload of a client datagram in; the reply payload out, or
/// empty when there is none.
Bytes dhcpReply(View request, const DhcpLease& lease);

}  // namespace nat
}  // namespace pom2

#endif  // POM2_NAT_DHCP_H
