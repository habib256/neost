// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BuiltinNatBackend — a user-mode virtual network that needs no library: the
// far side of the Ethernet wire for the Uthernet I (always) and for the
// Uthernet II's MACRAW and IPRAW sockets.
//
// Why it exists
// -------------
// The only transport that carried frames was libslirp, an optional build
// dependency that is not there on Windows — so on Windows the Uthernet I
// plugged, probed, and never saw a frame. This backend is a few thousand
// lines over the same portable sockets the Uthernet II already uses
// (SocketCompat.h), so every desktop build has a working network. libslirp
// stays the default where it is compiled in: it is the more complete stack.
//
// What is on the wire (the same numbering as libslirp and QEMU, on purpose:
// one guest configuration works on either backend)
// ------------------------------------------------------------------------
//   10.0.2.0/24   the virtual network
//   10.0.2.2      the gateway — answers ARP for every address but the
//                 guest's own, answers ping, serves DHCP
//   10.0.2.3      the DNS server (answered from the host resolver, NatDns.h)
//   10.0.2.15     what DHCP hands out
//
// UDP and TCP are relayed through host sockets (NatUdp.h, NatTcp.h), with
// bounded IPv4/IPv6 fragment reassembly and explicit TCP/UDP port forwards.
// IPv6 uses fd00:2::/64, router fe80::2, gateway fd00:2::2, DNS fd00:2::3,
// with ND and SLAAC advertisements. Other ICMP is not relayed to the Internet.
//
// THE GUEST IS INSIDE THE LOOPBACK PERIMETER (CLAUDE.md)
// ------------------------------------------------------
// The sockets this backend opens are HOST sockets. `routeDestination` is the
// whole fence and it is the same decision, behind the same setting
// (`uthernet_allow_loopback`), as W5100Device::checkDestination and
// SlirpOptions::allowHostLoopback:
//
//   * the gateway is NOT an alias for the host's loopback. Casso's is
//     (`VirtualSubnet::GuestToHost`: 10.0.65.1 -> 127.0.0.1) and that was
//     deliberately not taken: it is the route by which a guest reaches
//     POM2's own AI control server. With the opt-in, it is.
//   * 127/8, 0/8 and 169.254/16 named directly are refused (with the
//     opt-in, 127/8 is allowed).
//   * 224/4 and 240/4 are refused — DHCP's broadcast is intercepted before
//     the fence, so the guest's address configuration does not need them.
//   * the rest of 10.0.2.0/24 is nobody: refused.
//   * `restricted` (key `uthernet_slirp_restricted`) refuses everything but
//     the virtual services.
//
// A refused TCP connection is reset; a refused datagram is dropped.
//
// [D] Design after Casso `Devices/Network/VirtualNetwork` @ 2d899adf (MIT).
//     The numbering is libslirp's. The fence is POM2's.

#ifndef POM2_BUILTIN_NAT_BACKEND_H
#define POM2_BUILTIN_NAT_BACKEND_H

#include "NatHost.h"
#include "NetworkBackend.h"
#include "W5100Resolver.h"   // complete, for the defaulted unique_ptr argument

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace nat {

namespace detail {

inline constexpr uint32_t kNetwork     = 0x0A000200;   // 10.0.2.0
inline constexpr uint32_t kSubnetMask  = 0xFFFFFF00;   // /24
inline constexpr uint32_t kGatewayIp   = 0x0A000202;   // 10.0.2.2
inline constexpr uint32_t kDnsIp       = 0x0A000203;   // 10.0.2.3
inline constexpr uint32_t kGuestIp     = 0x0A00020F;   // 10.0.2.15
inline constexpr uint32_t kLoopbackIp  = 0x7F000001;   // 127.0.0.1
inline constexpr Ipv6 kGateway6 = {0xfd,0,0,2,0,0,0,0,0,0,0,0,0,0,0,2};
inline constexpr Ipv6 kDns6 = {0xfd,0,0,2,0,0,0,0,0,0,0,0,0,0,0,3};
inline constexpr Ipv6 kRouter6 = {0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,2};

/// What the fence decides for one destination.
enum class Route : uint8_t {
    Refused,    ///< reset (TCP) or drop (UDP)
    Host,       ///< relay to `host`
    Dns,        ///< UDP to the virtual DNS server: answered here
    Gated,      ///< NeoST: passed the fence, refused by the domain allowlist
};

struct RoutePolicy {
    bool allowHostLoopback = false;
    bool restricted        = false;
};

/// THE FENCE. Pure, so it is tested as a table (`builtin_nat`). `tcp` false
/// means UDP. On `Route::Host`, `host` is where the traffic really goes.
Route routeDestination(uint32_t destination, uint16_t port, bool tcp,
                       const RoutePolicy& policy, Endpoint& host);
Route routeDestination6(const Ipv6& destination, uint16_t port, bool tcp,
                        const RoutePolicy& policy, Endpoint& host);

}  // namespace detail

struct BuiltinNatOptions {
    struct PortForward {
        bool tcp = true;
        detail::Endpoint bind{detail::kLoopbackIp, 0};
        uint16_t guestPort = 0;
    };
    std::vector<PortForward> forwards;
    std::function<detail::Bytes(const detail::Bytes&)> dnsLookup;
    /// Let the guest reach the host's loopback interface: through the
    /// gateway address (10.0.2.2 -> 127.0.0.1) and by naming 127/8 directly.
    /// FALSE by default; key `uthernet_allow_loopback`.
    bool allowHostLoopback = false;
    /// The guest may talk to the virtual services (DHCP, DNS, ping) and to
    /// nothing else. Key `uthernet_slirp_restricted` — the name is
    /// historical, the decision is the same one.
    bool restricted = false;
    /// NeoST: the domain allowlist (NatAllowlist.h, parseAllowedDomains).
    /// Empty — the default — leaves the guest free; otherwise the DNS server
    /// answers only listed domains and the guest reaches only the addresses
    /// it handed back for them (or a listed IPv4 address). Key
    /// `net_allow_domains`.
    std::vector<std::string> allowedDomains;
    /// Milliseconds, monotonic. Empty selects the host's steady clock; a
    /// test supplies its own so retransmission and idle timeouts are exact.
    std::function<uint64_t()> nowMs;
};

/// Semicolon-separated tcp|udp:bind-ip:host-port:guest-port rules.
/// An invalid list is rejected atomically. 0.0.0.0 publishes on the LAN.
bool parseNatForwards(const std::string& text, std::vector<BuiltinNatOptions::PortForward>& out);

/// True on a build with host sockets (every desktop; not the browser).
bool builtinNatAvailable();

/// Which transport the `ethernet_backend` setting selects on THIS build:
/// "slirp", "builtin", "loopback" or "none".
///
/// "slirp" — the default, and what every existing settings file says — means
/// "the NAT": libslirp where it is compiled in, the built-in network where it
/// is not. So a Windows build, which has never had libslirp, gets a working
/// network from the same setting, and nobody has to learn a new value.
/// "builtin" asks for the built-in network even where libslirp exists.
inline const char* resolveEthernetBackend(const std::string& choice, bool slirpCompiledIn,
                                          bool builtinCompiledIn)
{
    if (choice == "loopback") return "loopback";
    if (choice == "none")     return "none";
    if (choice == "builtin")  return builtinCompiledIn ? "builtin" : "none";
    if (slirpCompiledIn)      return "slirp";
    return builtinCompiledIn ? "builtin" : "none";
}

/// The built-in network, or nullptr when `network` is null and the build has
/// no sockets. `network` and `resolver` are injectable for tests; null
/// selects the host's real sockets and resolver.
std::unique_ptr<NetworkBackend> makeBuiltinNatBackend(
    const BuiltinNatOptions& options = {},
    std::unique_ptr<detail::HostNetwork> network = nullptr,
    std::unique_ptr<W5100Resolver> resolver = nullptr);

}  // namespace nat

#endif  // POM2_BUILTIN_NAT_BACKEND_H
