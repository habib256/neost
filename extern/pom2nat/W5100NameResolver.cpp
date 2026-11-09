// POM2 Apple II Emulator
// Copyright (C) 2026 VERHILLE Arnaud
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// Moved out of W5100Device, not rewritten. The bounded wait, the in-flight
// cap and the shared mailbox each exist because of a specific failure; the
// comments recording those moved with the code.

#include "W5100NameResolver.h"

#include "Logger.h"
#include "SocketCompat.h"
#include "ThreadGuard.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <future>
#include <thread>

namespace pom2 {
namespace {

#if POM2_HAS_SOCKETS
uint32_t systemLookup(const std::string& name)
{
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(name.c_str(), nullptr, &hints, &result) != 0 || !result)
        return 0;

    uint32_t address = 0;
    for (addrinfo* p = result; p; p = p->ai_next) {
        if (p->ai_family != AF_INET || !p->ai_addr) continue;
        sockaddr_in sa{};
        std::memcpy(&sa, p->ai_addr,
                    std::min(sizeof(sa), static_cast<std::size_t>(p->ai_addrlen)));
        address = sa.sin_addr.s_addr;
        break;
    }
    freeaddrinfo(result);
    return address;
}
#endif

} // namespace

W5100NameResolver::W5100NameResolver(LookupFn lookup)
    : lookup_(std::move(lookup))
{
#if POM2_HAS_SOCKETS
    if (!lookup_) lookup_ = LookupFn(systemLookup);
#endif
}

// Token bucket — see kResolvesPerSecond in the header for why an in-flight
// cap is not enough on its own.
bool W5100NameResolver::takeRateToken()
{
    const auto now = std::chrono::steady_clock::now();
    if (rateStamp_ == std::chrono::steady_clock::time_point{}) {
        rateStamp_ = now;
    } else {
        const double elapsed =
            std::chrono::duration<double>(now - rateStamp_).count();
        rateStamp_ = now;
        rateTokens_ = std::min<double>(kResolveBurst,
                                       rateTokens_ + elapsed * kResolvesPerSecond);
    }
    if (rateTokens_ < 1.0) return false;
    rateTokens_ -= 1.0;
    return true;
}

W5100Resolver::Result W5100NameResolver::peek(const std::string& name)
{
    Result out;
    out.status = Status::Pending;
    const auto cached = cache_.find(name);
    if (cached == cache_.end()) return out;
    const CacheEntry& e = cached->second;
    if (e.address) {
        out.status  = Status::Resolved;
        out.address = e.address;
    } else if (std::chrono::steady_clock::now() - e.stored < failureTtl_) {
        out.status = Status::Failed;
    }
    return out;     // a failure past its lifetime reads as "not known"
}

W5100Resolver::Result W5100NameResolver::resolve(const std::string& name,
                                                     int waitMs)
{
    Result out;

    const auto cached = cache_.find(name);
    if (cached != cache_.end()) {
        const CacheEntry& e = cached->second;
        if (e.address || std::chrono::steady_clock::now() - e.stored < failureTtl_) {
            out.status  = e.address ? Status::Resolved : Status::Failed;
            out.address = e.address;
            return out;
        }
        cache_.erase(cached);   // a failure past its lifetime: ask again
    }

    // A cache MISS is the only thing that puts a query on the wire, so the
    // meter sits here and nowhere else.
    if (!takeRateToken()) {
        log().warn("W5100", "virtual-DNS rate limit reached — lookup not "
                            "attempted (name of " + std::to_string(name.size()) +
                            " bytes)");
        out.status = Status::Refused;
        return out;
    }

    if (!lookup_) {
        // No resolver at all — the WASM build, where there is no usable
        // host name service.
        log().warn("W5100", "virtual DNS is unavailable in this build");
        return out;
    }

    // Check the cap BEFORE std::async is even called: its future's destructor
    // would block, so a guest looping OPEN over random hostnames must be
    // stopped here rather than after the fact.
    {
        std::lock_guard<std::mutex> lk(mailbox_->mutex);
        if (mailbox_->inFlight >= kMaxInFlight) {
            log().warn("W5100", "too many DNS lookups in flight — '" + name +
                                    "' not attempted");
            out.status = Status::Refused;
            return out;
        }
    }

    auto lookup = lookup_;
    // Thread creation can throw (std::system_error) — on the CPU thread,
    // under stateMutex. One OPEN failing is the right outcome; the worker
    // dying through its exception barrier is not.
    std::future<uint32_t> future;
    try {
        future = std::async(std::launch::async,
                            [lookup, name]() { return lookup(name); });
    } catch (const std::exception&) {
        log().warn("W5100", "could not start a DNS lookup thread");
        out.status = Status::Refused;
        return out;
    }

    if (future.wait_for(std::chrono::milliseconds(waitMs)) ==
        std::future_status::ready) {
        const uint32_t address = future.get();
        store(name, address);
        out.status  = address ? Status::Resolved : Status::Failed;
        out.address = address;
        return out;
    }

    // Timed out. The lookup is NOT cancelled — it parks its answer in the
    // mailbox and poll() folds it into the cache on the CPU thread, which is
    // what keeps the cache single-threaded despite the async lookup. The
    // mailbox is held by shared_ptr so a lookup that outlives the card has
    // nowhere unsafe to write.
    auto shared = std::make_shared<std::future<uint32_t>>(std::move(future));
    auto mailbox = mailbox_;
    {
        std::lock_guard<std::mutex> lk(mailbox->mutex);
        ++mailbox->inFlight;
    }
    try {
    std::thread([name, shared, mailbox]() {
        bool counted = false;
        runGuarded("W5100", [&] {
            const uint32_t late = shared->get();
            std::lock_guard<std::mutex> lk(mailbox->mutex);
            mailbox->pending.push_back({ name, late });
            --mailbox->inFlight;
            counted = true;
        });
        // The guard swallowed an exception before the count was released;
        // release it here or the cap leaks a slot permanently.
        if (!counted) {
            std::lock_guard<std::mutex> lk(mailbox->mutex);
            --mailbox->inFlight;
        }
    }).detach();
    } catch (const std::exception&) {
        // The slot was counted before the thread existed: give it back, or
        // eight such failures refuse virtual DNS for good.
        std::lock_guard<std::mutex> lk(mailbox->mutex);
        --mailbox->inFlight;
        out.status = Status::Refused;
        return out;
    }

    log().info("W5100", "DNS lookup for '" + name +
                            "' still in flight — retry the connection");
    out.status = Status::Pending;
    return out;
}

void W5100NameResolver::poll()
{
    std::vector<Pending> ready;
    {
        std::lock_guard<std::mutex> lk(mailbox_->mutex);
        if (mailbox_->pending.empty()) return;
        ready.swap(mailbox_->pending);
    }
    for (const Pending& p : ready) store(p.name, p.address);
}

void W5100NameResolver::store(const std::string& name, uint32_t address)
{
    if (cache_.size() >= kMaxCache) cache_.clear();
    cache_[name] = { address, std::chrono::steady_clock::now() };
}

void W5100NameResolver::clearCache()
{
    cache_.clear();
}

} // namespace pom2
