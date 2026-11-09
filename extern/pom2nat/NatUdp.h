// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatUdp — guest UDP over host UDP sockets, one socket per guest source
// port.
//
// What comes back in: a datagram is delivered to the guest only when it
// comes from an ADDRESS that flow has sent to (any port of it — a TFTP
// server answers from a port it picks, RFC 1350 § 4). Everything else that
// lands on the host socket is dropped. That is an address-restricted NAT:
// tighter than libslirp, which forwards whatever arrives.
//
// The host socket takes an EPHEMERAL port. Casso binds the guest's own port
// number so "a peer that replies to the source port it received from gets
// through"; any NAT does that already — the peer sees the host's port as
// the source and answers it — and binding the guest's number would let the
// guest choose a port on the host, on every interface.
//
// A flow ends after kIdleMs with no traffic. The table is bounded; a guest
// that opens more flows than it holds loses its least recently used one.
//
// [D] After Casso `Devices/Network/UdpNat` @ 2d899adf (MIT). Not taken: the
//     same-port bind (above), the "from this PC" and "LAN access" inbound
//     rules, port forwards, and the DNS relay (NatDns.h).

#ifndef POM2_NAT_UDP_H
#define POM2_NAT_UDP_H

#include "NatGuestSink.h"
#include "NatHost.h"
#include "NatPacket.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace pom2 {
namespace nat {

class UdpRelay
{
public:
    static constexpr uint64_t    kIdleMs      = 120000;
    static constexpr std::size_t kMaxFlows    = 32;
    static constexpr std::size_t kMaxPeers    = 32;     ///< per flow
    /// Largest IPv4 UDP payload (65535 − 20 − 8). GuestSink fragments it
    /// to the virtual Ethernet MTU; host datagrams are never truncated.
    static constexpr std::size_t kMaxPayload  = 65507;
    static constexpr int         kMaxPerPump  = 16;

    UdpRelay(GuestSink& sink, HostNetwork* network) : sink_(sink), network_(network) {}

    /// `host`: where the datagram really goes. `shownAs`: the address the
    /// guest named — replies from `host.ip` are presented as coming from it.
    void sendFromGuest(uint16_t guestPort, const Endpoint& host, uint32_t shownAs,
                       View payload, uint64_t nowMs);
    void pump(uint64_t nowMs);
    void closeAll() { flows_.clear(); }
    std::size_t flowCount() const { return flows_.size(); }

private:
    struct Flow {
        std::unique_ptr<HostSocket>   socket;
        std::map<uint32_t, uint32_t>  peers;        ///< host address -> shown as
        std::map<Ipv6, uint32_t>      peers6;
        uint64_t                      lastMs = 0;
    };

    Flow* flowFor(uint16_t guestPort, uint64_t nowMs, bool ipv6);

    GuestSink&               sink_;
    HostNetwork*             network_;
    std::map<uint32_t, Flow> flows_;
    std::vector<uint8_t>     buffer_;
};

}  // namespace nat
}  // namespace pom2

#endif  // POM2_NAT_UDP_H
