// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatTcp — see the header.

#include "NatTcp.h"

#include <algorithm>
#include <vector>

namespace pom2 {
namespace nat {

// ── One connection ───────────────────────────────────────────────────────

TcpConnection::TcpConnection(GuestSink& sink, const Key& key,
                             std::unique_ptr<HostSocket> host, const TcpSegment& syn,
                             uint32_t isn, uint64_t nowMs, bool inbound)
    : sink_(sink), key_(key), host_(std::move(host)), ourIsn_(isn), ourUnacked_(isn),
      ourNext_(isn), guestNext_(syn.sequence + 1), guestWindow_(syn.window),
      mss_(syn.mss != 0 ? std::min(syn.mss, kMaxMss) : kDefaultMss),
      startedMs_(nowMs), lastSendMs_(nowMs), lastActivityMs_(nowMs)
{
    if (inbound) {
        inbound_ = true;
        state_ = State::SynSent;
        ourNext_ = isn + 1;
        guestNext_ = 0;
        sendToGuest(kTcpSyn, isn, nullptr, 0, kMaxMss);
    }
}

void TcpConnection::onGuestSegment(const TcpSegment& segment, uint64_t nowMs)
{
    lastActivityMs_ = nowMs;
    if (segment.flags & kTcpRst) { close(); return; }
    if (state_ == State::SynSent) {
        if ((segment.flags & (kTcpSyn | kTcpAck)) != (kTcpSyn | kTcpAck) ||
            segment.acknowledgment != ourNext_) return;
        guestNext_ = segment.sequence + 1;
        guestWindow_ = segment.window;
        mss_ = segment.mss ? std::min(segment.mss, kMaxMss) : kDefaultMss;
        ourUnacked_ = ourNext_;
        state_ = State::Established;
        retransmits_ = 0;
        sendAck();
        return;
    }
    // While the host connect is pending the guest can only be resending its
    // SYN; the SYN-ACK goes out when the connect completes.
    if (state_ != State::Established) return;

    // A repeated SYN means the guest missed our SYN-ACK.
    if ((segment.flags & kTcpSyn) && segment.sequence + 1 == guestNext_ &&
        ourUnacked_ == ourIsn_ + 1) {
        if (inbound_) sendAck();
        else sendToGuest(kTcpSyn | kTcpAck, ourIsn_, nullptr, 0, kMaxMss);
        return;
    }
    // Any OTHER SYN on an established connection: the guest rebooted or was
    // rewound (the NIC comes back from a snapshot, this table does not) and
    // reuses the 4-tuple — uIP restarts its ports at 1024. [B] RFC 5961
    // § 4.2: answer with a challenge ACK; the guest, in SYN-SENT, resets it
    // ([B] RFC 793 § 3.9) and the RST arm above closes the stale entry.
    // Swallowed, every retry was lost until the server timed out
    // (bug hunt 2026-11-07).
    if (segment.flags & kTcpSyn) { sendAck(); return; }
    if (segment.flags & kTcpAck) onAck(segment);
    onData(segment);
    onFin(segment);
}

/// An acknowledgment of nothing new, or of more than was sent, changes
/// nothing but the window.
void TcpConnection::onAck(const TcpSegment& segment)
{
    const uint32_t delta = segment.acknowledgment - ourUnacked_;
    if (delta > inFlight()) return;
    guestWindow_ = segment.window;
    if (guestWindow_) { persisting_ = false; persistBackoff_ = 0; }
    if (delta == 0) return;

    const std::size_t dataAcked = std::min<std::size_t>(delta, toGuest_.size());
    toGuest_.erase(toGuest_.begin(), toGuest_.begin() + static_cast<std::ptrdiff_t>(dataAcked));
    ourUnacked_  = segment.acknowledgment;
    retransmits_ = 0;
    if (finSent_ && ourUnacked_ == ourNext_) finAcked_ = true;
}

/// Only the next in-sequence bytes are taken, as many as the queue for the
/// host has room for; anything else produces a duplicate ACK, so the guest
/// resends from this side's next sequence number.
void TcpConnection::onData(const TcpSegment& segment)
{
    if (segment.payload.empty()) return;
    const uint32_t    offset = guestNext_ - segment.sequence;
    const std::size_t room   = kQueueLimit - toHost_.size();
    if (offset < segment.payload.size()) {
        const std::size_t take = std::min(segment.payload.size() - offset, room);
        toHost_.insert(toHost_.end(), segment.payload.begin() + offset,
                       segment.payload.begin() + offset + static_cast<std::ptrdiff_t>(take));
        guestNext_ += static_cast<uint32_t>(take);
    }
    sendAck();
}

/// A FIN counts only once every byte before it has been taken.
void TcpConnection::onFin(const TcpSegment& segment)
{
    if (!(segment.flags & kTcpFin)) return;
    const uint32_t finSequence =
        segment.sequence + static_cast<uint32_t>(segment.payload.size());
    if (!guestFin_ && finSequence == guestNext_) {
        guestFin_ = true;
        ++guestNext_;
    }
    sendAck();
}

void TcpConnection::pump(uint64_t nowMs)
{
    if (state_ == State::Connecting) { pumpConnect(nowMs); return; }
    if (state_ == State::SynSent) {
        if (nowMs - startedMs_ >= kConnectTimeoutMs) { resetAndClose(); return; }
        if (nowMs - lastSendMs_ >= (kRetransmitMs << std::min(retransmits_, 4))) {
            sendToGuest(kTcpSyn, ourIsn_, nullptr, 0, kMaxMss);
            lastSendMs_ = nowMs;
            ++retransmits_;
        }
        return;
    }
    if (state_ != State::Established) return;
    if (nowMs - lastActivityMs_ >= kIdleMs) { resetAndClose(); return; }

    pumpHostWrite(nowMs);
    if (state_ == State::Established) pumpHostRead(nowMs);
    if (state_ == State::Established) pumpGuestSend(nowMs);
    if (state_ == State::Established) pumpPersist(nowMs);
    if (state_ == State::Established) pumpRetransmit(nowMs);
    // Both directions finished and everything the guest sent has reached
    // the host: nothing is left to carry.
    if (state_ == State::Established && guestFin_ && finAcked_ && toHost_.empty()) close();
}

void TcpConnection::pumpConnect(uint64_t nowMs)
{
    const ConnectState c = host_->pollConnect();
    if (c == ConnectState::Failed || (c == ConnectState::Pending &&
                                      nowMs - startedMs_ > kConnectTimeoutMs)) {
        resetAndClose();
        return;
    }
    if (c == ConnectState::Pending) return;

    state_      = State::Established;
    ourUnacked_ = ourIsn_ + 1;
    ourNext_    = ourIsn_ + 1;
    lastSendMs_ = nowMs;
    lastActivityMs_ = nowMs;
    sendToGuest(kTcpSyn | kTcpAck, ourIsn_, nullptr, 0, kMaxMss);
}

void TcpConnection::pumpHostWrite(uint64_t nowMs)
{
    if (!toHost_.empty()) {
        const bool wasFull = toHost_.size() == kQueueLimit;
        const std::vector<uint8_t> pending(toHost_.begin(), toHost_.end());
        std::size_t sent = 0;
        const IoStatus s = host_->send(pending.data(), pending.size(), sent);
        if (s == IoStatus::Failed) { resetAndClose(); return; }
        toHost_.erase(toHost_.begin(), toHost_.begin() + static_cast<std::ptrdiff_t>(sent));
        if (sent) lastActivityMs_ = nowMs;
        // The window this side advertised reopens.
        if (wasFull && sent != 0) sendAck();
    }
    if (guestFin_ && toHost_.empty() && !hostShutdown_) {
        host_->shutdownSend();
        hostShutdown_ = true;
    }
}

void TcpConnection::pumpHostRead(uint64_t nowMs)
{
    const std::size_t room = kQueueLimit - std::min(toGuest_.size(), kQueueLimit);
    if (hostEof_ || room == 0) return;
    std::vector<uint8_t> buffer(room);
    std::size_t received = 0;
    const IoStatus s = host_->receive(buffer.data(), buffer.size(), received);
    if (s == IoStatus::Failed) { resetAndClose(); return; }
    if (s == IoStatus::Closed) { hostEof_ = true; lastActivityMs_ = nowMs; return; }
    if (received) lastActivityMs_ = nowMs;
    toGuest_.insert(toGuest_.end(), buffer.begin(),
                    buffer.begin() + static_cast<std::ptrdiff_t>(received));
}

/// New data within the guest's window, MSS-sized; then FIN once the host has
/// closed and everything before it has gone out.
void TcpConnection::pumpGuestSend(uint64_t nowMs)
{
    std::vector<uint8_t> payload;
    while (!finSent_) {
        const std::size_t sentData = inFlight();
        const std::size_t unsent   = toGuest_.size() - sentData;
        const std::size_t window   = guestWindow_ > sentData ? guestWindow_ - sentData : 0;
        const std::size_t count    = std::min({ static_cast<std::size_t>(mss_), unsent, window });
        if (count == 0) break;
        payload.assign(toGuest_.begin() + static_cast<std::ptrdiff_t>(sentData),
                       toGuest_.begin() + static_cast<std::ptrdiff_t>(sentData + count));
        sendToGuest(kTcpAck | kTcpPsh, ourNext_, payload.data(), payload.size(), 0);
        ourNext_   += static_cast<uint32_t>(count);
        lastSendMs_ = nowMs;
    }
    if (hostEof_ && !finSent_ && guestWindow_ > inFlight() && inFlight() == toGuest_.size()) {
        sendToGuest(kTcpFin | kTcpAck, ourNext_, nullptr, 0, 0);
        ++ourNext_;
        finSent_    = true;
        lastSendMs_ = nowMs;
    }
}

/// The oldest unacknowledged segment — data, or the FIN — with a timeout that
/// doubles on each resend from kRetransmitMs up to kMaxRtoMs ([B] RFC 6298
/// § 5.5), up to kMaxRetransmits times: about three minutes without progress,
/// above RFC 1122 § 4.2.3.5's R2 floor of 100 s. A fixed 1 s timer reset
/// the connection after ~9 s — a guest busy on a 5.25" overlay for that
/// long lost its transfer (bug hunt 2026-11-07).
void TcpConnection::pumpRetransmit(uint64_t nowMs)
{
    if (guestWindow_ == 0) return;
    const uint64_t rto = std::min<uint64_t>(
        kRetransmitMs << std::min(retransmits_, 6), kMaxRtoMs);
    if (inFlight() == 0 || nowMs - lastSendMs_ < rto) return;
    if (++retransmits_ > kMaxRetransmits) { resetAndClose(); return; }
    lastSendMs_ = nowMs;

    // (A lost SYN-ACK is not resent from here: it occupies nothing in
    // flight, and the guest, still without an answer, resends its SYN —
    // which onGuestSegment answers with the SYN-ACK again.)
    const std::size_t dataInFlight = inFlight() - ((finSent_ && !finAcked_) ? 1 : 0);
    if (dataInFlight != 0) {
        const std::size_t count = std::min<std::size_t>(mss_, dataInFlight);
        const std::vector<uint8_t> payload(toGuest_.begin(),
                                           toGuest_.begin() + static_cast<std::ptrdiff_t>(count));
        sendToGuest(kTcpAck | kTcpPsh, ourUnacked_, payload.data(), payload.size(), 0);
    } else if (finSent_ && !finAcked_) {
        sendToGuest(kTcpFin | kTcpAck, ourUnacked_, nullptr, 0, 0);
    }
}

void TcpConnection::pumpPersist(uint64_t nowMs)
{
    if (guestWindow_ || (toGuest_.empty() && !hostEof_)) {
        persisting_ = false;
        persistBackoff_ = 0;
        return;
    }
    if (!persisting_) { persisting_ = true; persistMs_ = nowMs; return; }
    const auto delay = std::min<uint64_t>(kRetransmitMs << persistBackoff_, kMaxRtoMs);
    if (nowMs - persistMs_ < delay) return;
    persistMs_ = nowMs;
    persistBackoff_ = std::min(persistBackoff_ + 1, 6u);
    if (!toGuest_.empty()) {
        const uint8_t byte = toGuest_.front();
        sendToGuest(kTcpAck, ourUnacked_, &byte, 1, 0);
        // A reopening receiver can accept this byte. Count it only once.
        if (inFlight() == 0) ++ourNext_;
    } else if (finSent_ && !finAcked_) {
        sendToGuest(kTcpFin | kTcpAck, ourUnacked_, nullptr, 0, 0);
    } else {
        sendToGuest(kTcpAck, ourNext_ - 1, nullptr, 0, 0);
    }
}

void TcpConnection::sendToGuest(uint8_t flags, uint32_t sequence, const uint8_t* data,
                                std::size_t size, uint16_t mss)
{
    TcpSegment s;
    s.sourceIp        = key_.peerIp;
    s.destinationIp   = sink_.guestIp();
    s.sourcePort      = key_.peerPort;
    s.destinationPort = key_.guestPort;
    s.sequence        = sequence;
    s.acknowledgment  = (flags & kTcpAck) ? guestNext_ : 0;
    s.flags           = flags;
    s.window          = ourWindow();
    s.mss             = mss;
    if (size) s.payload.assign(data, data + size);
    sink_.tcpToGuest(s);
}

void TcpConnection::sendAck() { sendToGuest(kTcpAck, ourNext_, nullptr, 0, 0); }

void TcpConnection::resetAndClose()
{
    sendToGuest(kTcpRst | kTcpAck, ourNext_, nullptr, 0, 0);
    close();
}

void TcpConnection::close()
{
    host_.reset();
    state_ = State::Closed;
}

uint16_t TcpConnection::ourWindow() const
{
    return static_cast<uint16_t>(kQueueLimit - toHost_.size());
}

// ── The table ────────────────────────────────────────────────────────────

void TcpRelay::onGuestSegment(const TcpSegment& segment, uint64_t nowMs)
{
    const TcpConnection::Key key{ segment.sourcePort, segment.destinationIp,
                                  segment.destinationPort };
    auto found = connections_.find(key);
    const bool isSyn = (segment.flags & (kTcpSyn | kTcpAck)) == kTcpSyn;
    // A connection the guest has just reset is gone, not waiting for the
    // next pump(): the SYN that follows a challenge-ACK'd RST opens a new one
    // instead of landing on the closed entry and being dropped.
    if (found != connections_.end() && found->second->closed()) {
        connections_.erase(found);
        found = connections_.end();
    }

    if (found != connections_.end())  found->second->onGuestSegment(segment, nowMs);
    else if (isSyn)                   startOutbound(segment, nowMs);
    else if (!(segment.flags & kTcpRst)) sendReset(segment);
}

void TcpRelay::startOutbound(const TcpSegment& syn, uint64_t nowMs)
{
    Endpoint host;
    std::unique_ptr<HostSocket> socket;
    if (network_ && connections_.size() < kMaxConnections && router_ &&
        router_(syn.destinationIp, syn.destinationPort, host))
        socket = host.isIpv6 ? network_->openTcp6() : network_->openTcp();
    if (!socket || !socket->connect(host)) { sendReset(syn); return; }

    const TcpConnection::Key key{ syn.sourcePort, syn.destinationIp, syn.destinationPort };
    connections_[key] = std::make_unique<TcpConnection>(sink_, key, std::move(socket), syn,
                                                        takeIsn(), nowMs);
}

void TcpRelay::pump(uint64_t nowMs)
{
    for (auto it = connections_.begin(); it != connections_.end();) {
        it->second->pump(nowMs);
        if (it->second->closed()) it = connections_.erase(it);
        else                      ++it;
    }
}

void TcpRelay::acceptInbound(uint16_t guestPort, const Endpoint& peer,
                            std::unique_ptr<HostSocket> socket, uint64_t nowMs)
{
    const TcpConnection::Key key{guestPort, peer.ip, peer.port};
    if (!socket || connections_.size() >= kMaxConnections || connections_.count(key)) return;
    connections_[key] = std::make_unique<TcpConnection>(sink_, key, std::move(socket),
        TcpSegment{}, takeIsn(), nowMs, true);
}

/// RFC 793's reset for a segment with no connection: for a segment with the
/// ACK flag, the reset's sequence number is that ACK value; otherwise the
/// reset acknowledges every sequence number the segment occupied.
void TcpRelay::sendReset(const TcpSegment& cause)
{
    uint32_t length = static_cast<uint32_t>(cause.payload.size());
    if (cause.flags & kTcpSyn) ++length;
    if (cause.flags & kTcpFin) ++length;

    TcpSegment reset;
    reset.sourceIp        = cause.destinationIp;
    reset.destinationIp   = cause.sourceIp;
    reset.sourcePort      = cause.destinationPort;
    reset.destinationPort = cause.sourcePort;
    if (cause.flags & kTcpAck) {
        reset.sequence = cause.acknowledgment;
        reset.flags    = kTcpRst;
    } else {
        reset.acknowledgment = cause.sequence + length;
        reset.flags          = kTcpRst | kTcpAck;
    }
    sink_.tcpToGuest(reset);
}

uint32_t TcpRelay::takeIsn()
{
    const uint32_t isn = nextIsn_;
    nextIsn_ += 0x00100000;
    return isn;
}

}  // namespace nat
}  // namespace pom2
