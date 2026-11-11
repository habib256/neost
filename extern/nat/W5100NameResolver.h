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

// W5100NameResolver — the virtual-DNS half of the Uthernet II.
//
// The real W5100 has no name resolution; POM2 adds it so a guest can write a
// hostname into the socket's DNS registers and get an address back
// (`Uthernet2.cpp:1012-1037`). That is a HOST lookup, and host lookups block:
// the whole design here exists to keep a slow or dead resolver from stalling
// the CPU thread, which calls this under stateMutex.
//
// Three rules, and each one is load-bearing:
//   * the lookup runs off-thread with a BOUNDED wait. A timed-out lookup is
//     not cancelled — it parks its answer in a mailbox that poll() drains on
//     the CPU thread, so the cache stays single-threaded.
//   * in-flight lookups are CAPPED. A guest looping OPEN over random
//     hostnames would otherwise pile up resolver threads without bound.
//   * the mailbox is shared by shared_ptr, so a lookup that outlives the card
//     (the user pulled it out of the slot mid-resolve) has nowhere unsafe to
//     write.
//
// The lookup itself is injectable so a test can resolve deterministically,
// with no network and no waiting.

#ifndef POM2_W5100_NAME_RESOLVER_H
#define POM2_W5100_NAME_RESOLVER_H

#include "W5100Resolver.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nat {

class W5100NameResolver final : public W5100Resolver
{
public:
    /// Blocking host lookup, returning an address in network byte order or 0.
    /// Runs on a resolver thread, never on the caller's.
    using LookupFn = std::function<uint32_t(const std::string&)>;

    /// An empty lookup selects the production getaddrinfo adapter.
    explicit W5100NameResolver(LookupFn lookup = {});

    W5100NameResolver(const W5100NameResolver&) = delete;
    W5100NameResolver& operator=(const W5100NameResolver&) = delete;

    // Status and Result are inherited from W5100Resolver: they are the
    // question's vocabulary, not this implementation's.

    /// Resolve within `waitMs`. Answers from the cache without waiting.
    Result resolve(const std::string& name, int waitMs) override;

    Result peek(const std::string& name) override;

    /// Fold any late answers into the cache. CPU thread only — that is what
    /// keeps the cache free of a lock.
    void poll() override;

    void clearCache() override;

    std::size_t cacheSize() const { return cache_.size(); }

    /// Bound on concurrent resolver threads, and on the cache.
    static constexpr int         kMaxInFlight = 8;
    static constexpr std::size_t kMaxCache    = 512;
    /// How long a FAILED lookup is remembered. Negative caching stops a guest
    /// retrying a bad name from starting a resolver thread per attempt, but
    /// kept for the whole session it also pinned a name that failed while
    /// the host network was still coming up: unresolvable until a cold boot.
    static constexpr std::chrono::seconds kFailureTtl{ 30 };
    void setFailureTtl(std::chrono::steady_clock::duration ttl) { failureTtl_ = ttl; }

    /// New lookups per second, as a token bucket.
    ///
    /// The in-flight cap bounds how many resolver THREADS exist at once; it
    /// does not bound how many queries leave the machine, because a lookup
    /// that answers instantly frees its slot instantly. A guest looping OPEN
    /// over `<payload>.attacker.example` therefore had an unmetered channel
    /// out through the host's resolver. Four per second is far above anything
    /// a period network client does (a name is resolved once per connection
    /// and then cached here) and far below a useful data rate.
    ///
    /// CACHE HITS DO NOT SPEND A TOKEN: a guest reconnecting to the same host
    /// in a loop is ordinary behaviour and never touches the network.
    static constexpr int kResolvesPerSecond = 4;
    /// Bucket capacity, i.e. how many lookups a burst may contain. Above
    /// kMaxInFlight on purpose: the in-flight cap is a bound on resolver
    /// THREADS and has to stay reachable — a burst ceiling below it would hide
    /// it, and the two answer different failures (too many at once vs too many
    /// over time). Sustained rate is still kResolvesPerSecond.
    static constexpr int kResolveBurst = 2 * kMaxInFlight;

private:
    /// Take one token, refilling at kResolvesPerSecond. False = refused.
    bool takeRateToken();

    struct Pending {
        std::string name;
        uint32_t    address = 0;
    };

    struct CacheEntry {
        uint32_t                              address = 0;
        std::chrono::steady_clock::time_point stored;
    };
    void store(const std::string& name, uint32_t address);

    /// One lookup's answer, shared by the caller's bounded wait and the
    /// worker. `abandoned`: the caller stopped waiting, so the worker posts
    /// to the mailbox instead.
    struct Job {
        std::mutex              mutex;
        std::condition_variable ready;
        bool                    done = false, abandoned = false;
        uint32_t                address = 0;
    };

    /// Written by detached resolver threads, drained by poll().
    struct Mailbox {
        std::mutex           mutex;
        std::vector<Pending> pending;
        int                  inFlight = 0;
    };

    LookupFn                        lookup_;
    std::map<std::string, CacheEntry> cache_;
    std::chrono::steady_clock::duration failureTtl_ = kFailureTtl;
    std::shared_ptr<Mailbox>        mailbox_ = std::make_shared<Mailbox>();

    /// Token bucket. CPU thread only, like the cache — resolve() is called
    /// from the device under the emulator's stateMutex.
    double                                 rateTokens_ = kResolveBurst;
    std::chrono::steady_clock::time_point  rateStamp_{};
};

} // namespace nat

#endif // POM2_W5100_NAME_RESOLVER_H
