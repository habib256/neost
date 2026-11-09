// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatGuestSink — where the built-in network's relays put traffic bound for
// the emulated machine. The implementation (BuiltinNatBackend) wraps each
// packet in IPv4 and Ethernet headers and queues the frame. Addresses are
// the GUEST-SIDE ones, host byte order.
//
// [D] After Casso `Devices/Network/IGuestPacketSink.h` @ 2d899adf (MIT).

#ifndef POM2_NAT_GUEST_SINK_H
#define POM2_NAT_GUEST_SINK_H

#include "NatPacket.h"

#include <cstddef>
#include <cstdint>

namespace pom2 {
namespace nat {

class GuestSink
{
public:
    virtual ~GuestSink() = default;
    virtual void udpToGuest(uint32_t sourceIp, uint16_t sourcePort, uint16_t guestPort,
                            const uint8_t* data, std::size_t size) = 0;
    virtual void tcpToGuest(const TcpSegment& segment) = 0;
    virtual uint32_t guestIp() const = 0;
};

}  // namespace nat
}  // namespace pom2

#endif  // POM2_NAT_GUEST_SINK_H
