// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NatDns — see the header.

#include "NatDns.h"

#include "NatAllowlist.h"
#include "W5100Resolver.h"
#include "ThreadGuard.h"

#include <cstring>
#include <algorithm>
#include <thread>

namespace nat {
namespace detail {
namespace {

constexpr std::size_t kHeader = 12;
constexpr uint16_t kTypeA = 1, kClassIn = 1;
constexpr uint8_t  kNoError = 0, kFormErr = 1, kServFail = 2, kNxDomain = 3, kNotImp = 4;
// [B] RFC 1035 § 4.1.1: RCODE 5, "Refused - The name server refuses to
// perform the specified operation for policy reasons" (NeoST allowlist).
constexpr uint8_t  kRefused = 5;
constexpr uint32_t kTtlSeconds = 60;

struct Question {
    std::string name;
    uint16_t    type = 0, klass = 0;
    std::size_t end = 0;          ///< offset just past the question
};

/// One question, uncompressed labels (RFC 1035 § 4.1.2 — a query has no
/// earlier name to point back at, so a compression pointer is malformed).
bool parseQuestion(View q, Question& out)
{
    std::size_t i = kHeader;
    std::string name;
    for (;;) {
        if (i >= q.size) return false;
        const uint8_t length = q.data[i++];
        if (length == 0) break;
        if (length > 63 || i + length > q.size) return false;
        if (!name.empty()) name.push_back('.');
        name.append(reinterpret_cast<const char*>(q.data + i), length);
        i += length;
        if (name.size() > 253) return false;
    }
    if (i + 4 > q.size) return false;
    out.name  = name;
    out.type  = rd16(q.data + i);
    out.klass = rd16(q.data + i + 2);
    out.end   = i + 4;
    return true;
}

/// Host names only. Whatever else the guest wrote is not handed to the host
/// resolver or to the log.
bool plainHostName(const std::string& name)
{
    if (name.empty()) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

/// `query` is the guest's message truncated to header + question.
/// `address`: 4 bytes in network order, or nullptr for no answer record.
Bytes reply(const Bytes& query, uint8_t rcode, const uint8_t* address)
{
    Bytes out = query;
    // QR=1, opcode copied, RD copied, RA=1, the response code.
    const uint16_t asked = rd16(out.data() + 2);
    wr16(out.data() + 2, static_cast<uint16_t>(0x8080 | (asked & 0x7900) | rcode));
    wr16(out.data() + 6, address ? 1 : 0);      // ANCOUNT
    wr16(out.data() + 8, 0);                    // NSCOUNT
    wr16(out.data() + 10, 0);                   // ARCOUNT
    if (address) {
        const std::size_t at = out.size();
        out.resize(at + 16);
        uint8_t* a = out.data() + at;
        wr16(a, 0xC00C);                        // the name: a pointer to the question's
        wr16(a + 2, kTypeA);
        wr16(a + 4, kClassIn);
        wr32(a + 6, kTtlSeconds);
        wr16(a + 10, 4);
        std::memcpy(a + 12, address, 4);
    }
    return out;
}

}  // namespace

bool DnsProxy::query(uint16_t guestPort, View payload, uint64_t nowMs, const Emit& emit)
{
    if (payload.size < kHeader) return false;                 // not even a header
    const uint16_t flags = rd16(payload.data + 2);
    if (flags & 0x8000) return false;                         // a response, not a query
    handleQuery(guestPort, payload, flags, nowMs, emit);
    return true;
}

void DnsProxy::handleQuery(uint16_t guestPort, View payload, uint16_t flags, uint64_t nowMs,
                           const Emit& emit)
{
    Bytes header(payload.data, payload.data + kHeader);
    const uint8_t opcode = static_cast<uint8_t>((flags >> 11) & 0x0F);
    Question question;
    if (opcode != 0) {
        wr16(header.data() + 4, 0);
        emit(guestPort, reply(header, kNotImp, nullptr));
        return;
    }
    if (rd16(payload.data + 4) != 1 || !parseQuestion(payload, question)) {
        wr16(header.data() + 4, 0);
        emit(guestPort, reply(header, kFormErr, nullptr));
        return;
    }
    Bytes asked(payload.data, payload.data + question.end);   // header + question
    // The native exchange sends precisely this single question. In particular
    // an EDNS additional record was not copied, so its count must not survive.
    wr16(asked.data() + 6, 0); wr16(asked.data() + 8, 0); wr16(asked.data() + 10, 0);

    if (question.klass != kClassIn) {
        emit(guestPort, reply(asked, kNotImp, nullptr));
        return;
    }
    if (!plainHostName(question.name) && !(question.name.empty() && question.type != kTypeA)) {
        emit(guestPort, reply(asked, kNxDomain, nullptr));
        return;
    }
    // NeoST: the domain allowlist, before any host lookup — a refused name
    // never reaches the host resolver, in any record type.
    if (gate_ && !gate_->admitName(question.name)) {
        emit(guestPort, reply(asked, kRefused, nullptr));
        return;
    }
    if (question.type != kTypeA) {
        queryRaw(guestPort, asked, nowMs, emit);
        return;
    }
    if (!resolver_) {
        emit(guestPort, reply(asked, kServFail, nullptr));
        return;
    }

    // The guest resends a query it has had no answer to. That retry must not
    // start a second host lookup, nor must a second question for a name
    // already being looked up.
    bool nameInFlight = false;
    for (const Pending& p : pending_) {
        if (p.name != question.name) continue;
        nameInFlight = true;
        if (p.guestPort == guestPort && rd16(p.query.data()) == rd16(asked.data()))
            return;                                           // the same query again
    }
    if (pending() >= kMaxPending) {
        emit(guestPort, reply(asked, kServFail, nullptr));
        return;
    }

    if (!nameInFlight) {
        const W5100Resolver::Result r = resolver_->resolve(question.name, 0);
        switch (r.status) {
            case W5100Resolver::Status::Resolved:
                if (gate_) gate_->learn4(rd32(reinterpret_cast<const uint8_t*>(&r.address)));
                emit(guestPort, reply(asked, kNoError,
                                      reinterpret_cast<const uint8_t*>(&r.address)));
                return;
            case W5100Resolver::Status::Failed:
                emit(guestPort, reply(asked, kNxDomain, nullptr));
                return;
            case W5100Resolver::Status::Refused:
                emit(guestPort, reply(asked, kServFail, nullptr));
                return;
            case W5100Resolver::Status::Pending:
                break;
        }
    }
    Pending p;
    p.guestPort  = guestPort;
    p.query      = std::move(asked);
    p.name       = question.name;
    p.deadlineMs = nowMs + kTimeoutMs;
    pending_.push_back(std::move(p));
}

void DnsProxy::pump(uint64_t nowMs, const Emit& emit)
{
    pumpRaw(nowMs, emit);
    if (pending_.empty() || !resolver_) return;
    resolver_->poll();
    for (std::size_t i = 0; i < pending_.size();) {
        const Pending& p = pending_[i];
        // peek, not resolve: an answer that has not arrived must not start
        // another lookup (and spend another rate token) on every pump.
        const W5100Resolver::Result r = resolver_->peek(p.name);
        bool done = true;
        if (r.status == W5100Resolver::Status::Resolved) {
            if (gate_) gate_->learn4(rd32(reinterpret_cast<const uint8_t*>(&r.address)));
            emit(p.guestPort, reply(p.query, kNoError,
                                    reinterpret_cast<const uint8_t*>(&r.address)));
        } else if (r.status == W5100Resolver::Status::Failed)
            emit(p.guestPort, reply(p.query, kNxDomain, nullptr));
        else if (nowMs >= p.deadlineMs)
            emit(p.guestPort, reply(p.query, kServFail, nullptr));
        else
            done = false;
        if (done) pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        else      ++i;
    }
}

void DnsProxy::queryRaw(uint16_t port, const Bytes& query, uint64_t now, const Emit& emit)
{
    if (!lookup_) { emit(port, reply(query, kNotImp, nullptr)); return; }
    std::shared_ptr<Job> job;
    for (const auto& p : raw_) {
        if (p.query.size() != query.size() ||
            !std::equal(query.begin() + 12, query.end(), p.query.begin() + 12)) continue;
        if (p.port == port && rd16(p.query.data()) == rd16(query.data())) return;
        job = p.job;
    }
    if (pending() >= kMaxPending) { emit(port, reply(query, kServFail, nullptr)); return; }
    if (!job) {
        Workers& b = *workers_;                     // CPU thread: no lock for the bucket
        if (b.tokenMs) b.tokens = std::min(16.0, b.tokens + (now - b.tokenMs) * .004);
        b.tokenMs = now;
        if (b.tokens < 1) { emit(port, reply(query, kServFail, nullptr)); return; }
        b.tokens -= 1;
        {
            std::lock_guard<std::mutex> lock(workers_->mutex);
            if (workers_->active >= kMaxPending) { emit(port, reply(query, kServFail, nullptr)); return; }
            ++workers_->active;
        }
        job = std::make_shared<Job>();
        auto workers = workers_;
        auto lookup = lookup_;
        try {
            std::thread([job, workers, lookup, query] {
                Bytes answer;
                runGuarded("NatDns", [&] { answer = lookup(query); });
                { std::lock_guard<std::mutex> lock(job->mutex); job->answer = std::move(answer); job->ready = true; }
                { std::lock_guard<std::mutex> lock(workers->mutex); --workers->active; }
            }).detach();
        } catch (...) {
            std::lock_guard<std::mutex> lock(workers_->mutex);
            --workers_->active;
            emit(port, reply(query, kServFail, nullptr));
            return;
        }
    }
    raw_.push_back({port, query, now + kTimeoutMs, job});
}

void DnsProxy::pumpRaw(uint64_t now, const Emit& emit)
{
    for (auto it = raw_.begin(); it != raw_.end();) {
        Bytes answer;
        bool ready;
        { std::lock_guard<std::mutex> lock(it->job->mutex); ready = it->job->ready; if (ready) answer = it->job->answer; }
        if (!ready && now < it->deadline) { ++it; continue; }
        Question asked, returned;
        parseQuestion({it->query.data(), it->query.size()}, asked);
        const auto equalName = [](const std::string& a, const std::string& b) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                [&](char x, char y) { return lower(x) == lower(y); });
        };
        if (answer.size() < 12 || answer.size() > 65507 || !(answer[2] & 0x80) ||
            rd16(answer.data() + 4) != 1 || !parseQuestion({answer.data(), answer.size()}, returned) ||
            !equalName(returned.name, asked.name) || returned.type != asked.type || returned.klass != kClassIn)
            answer = reply(it->query, kServFail, nullptr);
        else if (gate_)
            gate_->learnWire(answer);
        wr16(answer.data(), rd16(it->query.data()));
        emit(it->port, answer);
        it = raw_.erase(it);
    }
}

}  // namespace detail
}  // namespace nat
