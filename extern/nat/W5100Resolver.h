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

// W5100Resolver — the name-lookup surface the Uthernet II device calls.
//
// The device asks a question ("what address is this hostname?") and gets an
// answer or a "not yet". It has no business knowing that answering involves a
// thread, a bounded wait or a cache — those are runtime concerns, and the
// configure-time layer guard rejects a device that reaches for them.
//
// W5100NameResolver is the production implementation. A test substitutes its
// own and answers instantly.

#ifndef POM2_W5100_RESOLVER_H
#define POM2_W5100_RESOLVER_H

#include <cstdint>
#include <string>

namespace nat {

class W5100Resolver
{
public:
    enum class Status : std::uint8_t {
        Resolved,   ///< `address` is usable now
        Pending,    ///< timed out; the answer may arrive later, via poll()
        Refused,    ///< too many lookups already in flight
        Failed,     ///< the resolver answered, and the answer was "no"
    };

    struct Result {
        Status   status  = Status::Failed;
        uint32_t address = 0;   ///< network byte order
    };

    virtual ~W5100Resolver() = default;

    /// Resolve within `waitMs`, answering from cache without waiting.
    virtual Result resolve(const std::string& name, int waitMs) = 0;

    /// The answer already in hand, or `Pending` when there is none. Never
    /// starts a lookup and never spends a rate token — it is how a caller
    /// that got `Pending` from resolve() asks again without asking the
    /// network again (NatDns.cpp). A resolver with no cache answers Pending.
    virtual Result peek(const std::string& /*name*/) { return Result{ Status::Pending, 0 }; }

    /// Fold any late answers in. Called from the CPU thread only.
    virtual void poll() = 0;

    virtual void clearCache() = 0;
};

} // namespace nat

#endif // POM2_W5100_RESOLVER_H
