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

// pom2test::timeScale() — the factor an IN-TEST wall-clock bound is
// multiplied by. tests/CMakeLists.txt hands every test
// POM2_TEST_TIME_SCALE = POM2_TEST_TIMEOUT_SCALE, the same integer that
// scales the ctest TIMEOUTs: CI's nightly sanitizer job sets 6, on a machine
// the project measured as 5.9x slower, while ~15 bounds inside the tests
// ("returned within 60 ms", "a 20 ms TTL between two calls") stayed literal
// and failed there for no reason but the build (bug hunt 2026-11-01 round 2,
// test quality #3). 1 when unset, so a plain run asserts exactly what it did.
//
// Scale the bound that says "it came back promptly", never a bound that is
// the property under test in the other direction (a timeout that must FIRE).

#ifndef POM2_TEST_TIME_SCALE_H
#define POM2_TEST_TIME_SCALE_H

#include <cstdlib>

namespace pom2test {

inline long timeScale()
{
    static const long scale = [] {
        const char* e = std::getenv("POM2_TEST_TIME_SCALE");
        const long v = e ? std::strtol(e, nullptr, 10) : 1;
        return v >= 1 ? v : 1L;
    }();
    return scale;
}

}  // namespace pom2test

#endif  // POM2_TEST_TIME_SCALE_H
