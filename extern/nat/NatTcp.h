// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatTcp — the guest's TCP connections, each relayed through one host TCP
// connection.
//
// The guest side is a minimal TCP endpoint. It has no congestion control,
// because the virtual wire between it and the guest never drops a frame:
// segments go out within the guest's advertised window and MSS, and the
// oldest unacknowledged one is resent after kRetransmitMs for guest stacks
// that discard segments themselves (uIP keeps no reassembly queue).
//
// A guest SYN for a new peer starts a host connect; the SYN-ACK is held
// until that connect completes, so the guest learns "refused" as a reset —
// what a real network would tell it — rather than as a connection that
// opens and dies. A segment for no known connection gets RFC 793's reset.
//
// WHERE a connection may go is not decided here: the backend passes a
// router, and a destination it refuses is reset exactly like a closed port.
//
// [B] RFC 793 § 3.4 (reset generation), § 3.9 (segment arrival), RFC 1122
//     § 4.2.2.6 (536 is the MSS when the option is absent).
// [D] After Casso `Devices/Network/TcpNat` + `TcpNatConnection` @ 2d899adf
//     (MIT). Added: explicit inbound connections, a bound on the connection
//     table and on how long either handshake may stay pending.
// [B] RFC 9293 § 3.8.6.1: zero-window probes back off without exhausting
//     the retransmission budget while the receiver keeps its window shut.
// [B] RFC 5382 § 5 REQ-5: an established connection's idle timeout MUST NOT
//     be less than 2 hours 4 minutes — kIdleMs is exactly that. Without one,
//     a guest that rebooted or lost its stack left connections with nothing
//     in flight (so nothing ever retransmitted and failed) holding slots of
//     the 32-entry table for the whole session (NeoST audit, 2026-11-11).

#ifndef POM2_NAT_TCP_H
#define POM2_NAT_TCP_H

#include "NatGuestSink.h"
#include "NatHost.h"
#include "NatPacket.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

namespace pom2 {
namespace nat {

class TcpConnection
{
public:
    enum class State : uint8_t {
        Connecting,     ///< guest SYN received, host connect in progress
        SynSent,        ///< accepted host socket, waiting for guest SYN-ACK
        Established,
        Closed,
    };

    struct Key {
        uint16_t guestPort = 0;
        uint32_t peerIp    = 0;     ///< the guest-side address
        uint16_t peerPort  = 0;
        bool operator<(const Key& o) const
        {
            return std::tie(guestPort, peerIp, peerPort) <
                   std::tie(o.guestPort, o.peerIp, o.peerPort);
        }
    };

    static constexpr std::size_t kQueueLimit       = 4096;
    static constexpr uint16_t    kMaxMss           = 1460;
    static constexpr uint16_t    kDefaultMss       = 536;
    static constexpr uint64_t    kRetransmitMs     = 1000;    // the first RTO; doubles
    static constexpr uint64_t    kMaxRtoMs         = 60000;
    static constexpr int         kMaxRetransmits   = 8;
    static constexpr uint64_t    kConnectTimeoutMs = 30000;
    static constexpr uint64_t    kIdleMs           = (2 * 60 + 4) * 60 * 1000;

    TcpConnection(GuestSink& sink, const Key& key, std::unique_ptr<HostSocket> host,
                  const TcpSegment& syn, uint32_t isn, uint64_t nowMs, bool inbound = false);

    void  onGuestSegment(const TcpSegment& segment, uint64_t nowMs);
    void  pump(uint64_t nowMs);
    State state() const { return state_; }
    bool  closed() const { return state_ == State::Closed; }

private:
    void     sendToGuest(uint8_t flags, uint32_t sequence, const uint8_t* data,
                         std::size_t size, uint16_t mss);
    void     sendAck();
    void     resetAndClose();
    void     close();
    uint16_t ourWindow() const;
    uint32_t inFlight() const { return ourNext_ - ourUnacked_; }

    void onAck(const TcpSegment& segment);
    void onData(const TcpSegment& segment);
    void onFin(const TcpSegment& segment);

    void pumpConnect(uint64_t nowMs);
    void pumpHostWrite(uint64_t nowMs);
    void pumpHostRead(uint64_t nowMs);
    void pumpGuestSend(uint64_t nowMs);
    void pumpRetransmit(uint64_t nowMs);
    void pumpPersist(uint64_t nowMs);

    GuestSink&                  sink_;
    Key                         key_;
    std::unique_ptr<HostSocket> host_;
    State                       state_ = State::Connecting;
    uint32_t                    ourIsn_ = 0;
    uint32_t                    ourUnacked_ = 0;
    uint32_t                    ourNext_ = 0;
    uint32_t                    guestNext_ = 0;
    uint16_t                    guestWindow_ = 0;
    uint16_t                    mss_ = kDefaultMss;
    std::deque<uint8_t>         toHost_;
    std::deque<uint8_t>         toGuest_;       ///< front has sequence ourUnacked_
    bool                        guestFin_ = false;
    bool                        hostShutdown_ = false;
    bool                        hostEof_ = false;
    bool                        finSent_ = false;
    bool                        finAcked_ = false;
    uint64_t                    startedMs_ = 0;
    uint64_t                    lastSendMs_ = 0;
    uint64_t                    lastActivityMs_ = 0;   ///< a segment or host bytes, either way
    int                         retransmits_ = 0;
    uint64_t                    persistMs_ = 0;
    unsigned                    persistBackoff_ = 0;
    bool                        persisting_ = false;
    bool                        inbound_ = false;
};

class TcpRelay
{
public:
    /// Where a guest connection to `destination:port` really goes. False =
    /// refused; the guest gets a reset.
    using Router = std::function<bool(uint32_t destination, uint16_t port, Endpoint& host)>;

    /// Connections at once. A SYN past it is reset; an Apple II holds four
    /// (the W5100 has four sockets) or a handful (uIP's default table).
    static constexpr std::size_t kMaxConnections = 32;

    TcpRelay(GuestSink& sink, HostNetwork* network, Router router)
        : sink_(sink), network_(network), router_(std::move(router)) {}

    void onGuestSegment(const TcpSegment& segment, uint64_t nowMs);
    void acceptInbound(uint16_t guestPort, const Endpoint& peer,
                       std::unique_ptr<HostSocket> socket, uint64_t nowMs);
    void pump(uint64_t nowMs);
    void closeAll() { connections_.clear(); }
    std::size_t connectionCount() const { return connections_.size(); }
    /// The guest-side peer address of every connection in the table.
    template <class F> void forEachPeer(F&& f) const
    {
        for (const auto& c : connections_) f(c.first.peerIp);
    }

private:
    void     startOutbound(const TcpSegment& syn, uint64_t nowMs);
    void     sendReset(const TcpSegment& cause);
    uint32_t takeIsn();

    GuestSink&                                                 sink_;
    HostNetwork*                                               network_;
    Router                                                     router_;
    std::map<TcpConnection::Key, std::unique_ptr<TcpConnection>> connections_;
    /// Deterministic initial sequence numbers: only the guest ever receives
    /// them, across a wire nobody else is on, and tests want repeatable runs.
    uint32_t                                                   nextIsn_ = 0x01000000;
};

}  // namespace nat
}  // namespace pom2

#endif  // POM2_NAT_TCP_H
