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

// Exception barrier for the body of a long-lived thread.
//
// An exception that escapes the callable of a std::thread does not propagate
// anywhere — it calls std::terminate(), which kills the whole process with no
// log line and no message. For an emulator that allocates multi-MB rewind
// blobs, mounts user-named files and talks to sockets on background threads,
// that turns an ordinary std::bad_alloc or a filesystem_error into something
// the user reports as "it just vanished", indistinguishable from a segfault.
//
// The rule was already written down for the CLI's deferred-action thread
// (main.cpp) and applied to AiControlServer::runWorker; this header is that
// same guard factored out so every thread entry point can wear it.
//
// Usage — the two shapes are equivalent, pick whichever reads better at the
// call site:
//     worker_ = pom2::guardedThread("FujiNet", [this] { workerLoop(); });
//     std::thread([this] { pom2::runGuarded("Printer", [this]{ loop(); }); });
//
// `tag` is the logger tag and must have static storage duration (a string
// literal at every call site) — the guard keeps the pointer, not a copy.
//
// What the guard does NOT do: restart the thread, or repair whatever state the
// dying thread was halfway through mutating. It converts an unobservable
// process death into a logged, observable dead thread. A caller that has a
// coherent "this subsystem is now stopped" state to publish should do so right
// after the guard returns — EmulationController's CPU worker parks itself, for
// instance, so waitUntilParked() returns instead of burning its poll budget.

#ifndef POM2_THREAD_GUARD_H
#define POM2_THREAD_GUARD_H

#include <exception>
#include <string>
#include <thread>
#include <utility>

#include "Logger.h"

namespace pom2 {

/// Run `fn` as the body of a thread, turning an escaping exception into a
/// logged error instead of std::terminate(). Never throws.
template <class Fn>
void runGuarded(const char* tag, Fn&& fn) noexcept
{
    try {
        fn();
    } catch (const std::exception& e) {
        log().error(tag, std::string("thread terminated by exception: ") + e.what());
    } catch (...) {
        log().error(tag, "thread terminated by a non-std exception");
    }
}

/// Spawn a std::thread whose body is wrapped in runGuarded(). Same ownership
/// and joinability semantics as the std::thread constructor.
template <class Fn>
std::thread guardedThread(const char* tag, Fn&& fn)
{
    return std::thread([tag, body = std::forward<Fn>(fn)]() mutable {
        runGuarded(tag, body);
    });
}

} // namespace pom2

#endif // POM2_THREAD_GUARD_H
