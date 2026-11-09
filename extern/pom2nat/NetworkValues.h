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

// NetworkValues — byte-order helpers, as VALUES.
//
// These are arithmetic, nothing more. They live apart from SocketCompat.h
// because a device that has to byte-swap a port number does not thereby need
// the host socket API, and the configure-time layer guard treats reaching for
// that API from a device as a violation — correctly, since it is how a device
// quietly acquires a syscall.

#ifndef POM2_NETWORK_VALUES_H
#define POM2_NETWORK_VALUES_H

#include <cstdint>

namespace pom2 {

inline constexpr uint16_t byteSwap16(uint16_t v)
{
    return static_cast<uint16_t>((v << 8) | (v >> 8));
}

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
inline constexpr uint16_t hostToNet16(uint16_t v) { return v; }
inline constexpr uint16_t netToHost16(uint16_t v) { return v; }
#else
inline constexpr uint16_t hostToNet16(uint16_t v) { return byteSwap16(v); }
inline constexpr uint16_t netToHost16(uint16_t v) { return byteSwap16(v); }
#endif

} // namespace pom2

#endif // POM2_NETWORK_VALUES_H
