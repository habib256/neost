// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatHost — the host's network stack as the built-in virtual network uses
// it: non-blocking IPv4/IPv6 TCP and UDP sockets, explicit host listeners,
// and a blocking native DNS exchange used only by bounded resolver workers.
//
// A seam, for the reason `W5100Socket.h` is one: the relays (NatTcp, NatUdp)
// are state machines worth testing exhaustively, and a test that needs a real
// peer on a real socket tests the kernel's timing as much as the relay. Here
// the test IS the remote peer. `makeHostNetwork()` (NatHostSockets.cpp) is
// the production implementation over SocketCompat.h — POSIX and Winsock.
//
// No socket call waits. The backend is pumped from the CPU thread under stateMutex
// (NetworkBackend.h), so an operation that would block reports that and
// returns.
//
// Addresses and ports are HOST byte order here, as everywhere in the NAT.
//
// [D] Shape after Casso `Seams/IHostNetwork.h` @ 2d899adf (MIT), extended
//     with IPv6 and host-configured listeners; guests cannot open listeners.

#ifndef POM2_NAT_HOST_H
#define POM2_NAT_HOST_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include "NatPacket.h"

namespace pom2 {
namespace nat {

struct Endpoint {
    uint32_t ip   = 0;
    uint16_t port = 0;
    Ipv6 ipv6{};
    bool isIpv6 = false;
};

enum class ConnectState : uint8_t { Pending, Connected, Failed };

enum class IoStatus : uint8_t {
    Ok,          ///< bytes moved (possibly fewer than asked)
    WouldBlock,  ///< nothing moved; try again later
    Closed,      ///< receive only: the peer finished sending (orderly EOF)
    Failed,      ///< the connection is gone
};

class HostSocket
{
public:
    virtual ~HostSocket() = default;

    // ── TCP ───────────────────────────────────────────────────────────────
    /// Start a connect. False when it could not even be started.
    virtual bool         connect(const Endpoint& remote) = 0;
    virtual ConnectState pollConnect() = 0;
    virtual IoStatus     send(const uint8_t* data, std::size_t size, std::size_t& sent) = 0;
    virtual IoStatus     receive(uint8_t* buffer, std::size_t capacity, std::size_t& received) = 0;
    /// Half-close: this side has no more to send; receiving goes on.
    virtual void         shutdownSend() = 0;
    virtual std::unique_ptr<HostSocket> accept(Endpoint& /*peer*/) { return nullptr; }

    // ── UDP ───────────────────────────────────────────────────────────────
    /// False when the datagram could not be sent; UDP permits the loss.
    virtual bool sendTo(const Endpoint& remote, const uint8_t* data, std::size_t size) = 0;
    /// False when nothing is pending.
    virtual bool receiveFrom(uint8_t* buffer, std::size_t capacity,
                             std::size_t& received, Endpoint& from) = 0;
};

class HostNetwork
{
public:
    virtual ~HostNetwork() = default;
    /// nullptr when the host refused the socket (descriptor exhaustion, …).
    virtual std::unique_ptr<HostSocket> openTcp() = 0;
    /// Bound to an EPHEMERAL port on every interface. The guest does not get
    /// to choose a port on the host (W5100Device's `localPortAllowed` draws
    /// the same line for the Uthernet II).
    virtual std::unique_ptr<HostSocket> openUdp() = 0;
    virtual std::unique_ptr<HostSocket> openTcp6() { return nullptr; }
    virtual std::unique_ptr<HostSocket> openUdp6() { return nullptr; }
    /// Explicit host-side publication only; no guest packet can bind a port.
    virtual std::unique_ptr<HostSocket> listenTcp(const Endpoint&) { return nullptr; }
    virtual std::unique_ptr<HostSocket> bindUdp(const Endpoint&) { return nullptr; }
};

/// The host's real sockets, or nullptr on a build with none (the browser).
std::unique_ptr<HostNetwork> makeHostNetwork();
/// Blocking native DNS exchange. Only called on DnsProxy's bounded workers.
Bytes queryHostDns(const Bytes& query);

}  // namespace nat
}  // namespace pom2

#endif  // POM2_NAT_HOST_H
