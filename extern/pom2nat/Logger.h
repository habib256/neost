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

// Minimal levelled logger — thread-safe stderr sink with a tag per message.
// Used everywhere a subsystem wants to log something:
//   pom2::log().info("ROM", "Loaded apple2.rom");

#ifndef POM2_LOGGER_H
#define POM2_LOGGER_H

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>

namespace pom2 {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/// The longest message a single log line may carry, before the tail is
/// dropped. No POM2 message comes near it; a value read out of `state.cfg`
/// or a name read off a filesystem has no bound at all, and a megabyte of it
/// scrolling past is a log the user cannot read.
inline constexpr std::size_t kLogMaxMessageBytes = 8192;

/// Make an ARBITRARY string safe to hand to a terminal.
///
/// Every log message here is built by concatenation, and what is concatenated
/// in is routinely NOT ours: a disk path out of `state.cfg`, a filename off
/// the host filesystem, a path out of a `tnfs://` URL. On POSIX a filename is
/// any byte but '/' and NUL, so all of these are legal names:
///
///   "\x1b[2J\x1b[31mPWNED.dsk"                    — clears the screen, recolours it
///   "\x1b]0;owned\x07b.dsk"                       — rewrites the window title
///   "a.dsk\n[ERROR] ROM: apple2e.rom checksum FAILED"  — forges a whole log line
///
/// The third is the one that costs: it produces a message the user will
/// report as POM2's, about a subsystem that never spoke. So printable ASCII
/// and any byte >= 0x80 (UTF-8 — the messages are full of it) pass through,
/// and every C0 control plus DEL becomes a visible escape. One line in, one
/// line out.
inline std::string sanitizeLogText(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    const std::size_t n = in.size() < kLogMaxMessageBytes
                        ? in.size() : kLogMaxMessageBytes;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c >= 0x20 && c != 0x7F) { out += static_cast<char>(c); continue; }
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: {
                static const char hex[] = "0123456789ABCDEF";
                out += "\\x";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
                break;
            }
        }
    }
    if (in.size() > n) out += "\\...(truncated)";
    return out;
}

class Logger
{
public:
    void log(LogLevel level, const char* tag, const std::string& msg) {
        static const char* names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
        // Sanitised OUTSIDE the lock: it is pure string work, and the mutex
        // exists only to keep two threads' fprintf from interleaving.
        const std::string safeTag = sanitizeLogText(tag ? tag : "");
        const std::string safeMsg = sanitizeLogText(msg);
        std::lock_guard<std::mutex> lk(mtx);
        std::fprintf(stderr, "[%s] %s: %s\n",
                     names[static_cast<int>(level)], safeTag.c_str(),
                     safeMsg.c_str());
    }
    void debug(const char* tag, const std::string& m) { log(LogLevel::Debug, tag, m); }
    void info (const char* tag, const std::string& m) { log(LogLevel::Info,  tag, m); }
    void warn (const char* tag, const std::string& m) { log(LogLevel::Warn,  tag, m); }
    void error(const char* tag, const std::string& m) { log(LogLevel::Error, tag, m); }
private:
    std::mutex mtx;
};

/// IMMORTAL on purpose: `*new Logger` is never destroyed, so `log()` stays
/// usable for the whole life of the process — including after main() returns.
/// A plain function-local static is registered with atexit and destroyed in
/// reverse construction order, and the logger is reached from every thread
/// POM2 owns: a detached reaper, a guarded worker on its way out through the
/// exception barrier, or a static destructor in another translation unit all
/// log AFTER that point, and each would then lock a destroyed std::mutex —
/// undefined behaviour, at exit, where nobody can diagnose it. Leaking one
/// mutex and a FILE* the OS reclaims anyway is the cheaper half of the trade.
inline Logger& log() { static Logger& g = *new Logger(); return g; }

} // namespace pom2

#endif // POM2_LOGGER_H
