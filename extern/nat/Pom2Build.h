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

// Pom2Build.h — compile-time build-tier facts, in one place.
//
// Right now that is exactly one question: which OpenGL tier are we compiling
// against? POM2 targets two, and they differ in only three places — the GL
// headers, the shader `#version` prologue, and how context creation is asked
// for:
//
//   * desktop GL 3.2 core / GLSL 150  — Linux, Windows, macOS
//   * OpenGL ES 3.0 / GLSL ES 300     — WebAssembly (WebGL 2 IS GLES 3.0) and
//                                       Raspberry Pi 4/5 and most ARM SoCs
//
// The API subset POM2 actually uses (VAO/VBO, shaders, FBO, 2D textures) is the
// common ground of both tiers, so one set of sources covers them; there is no
// renderer fork.
//
// WHY THIS HEADER EXISTS: the GL translation units used to ask
// `#if defined(__EMSCRIPTEN__)` when what they meant was "do we speak GLES?".
// Those are not the same question, and conflating them is what made the
// Raspberry Pi unreachable: the Pi needs the GLES tier while being an ordinary
// native Linux build, so every one of those guards took the desktop branch and
// the result requested a GL 3.2 core context — which Mesa's V3D cannot give
// (it caps *desktop* GL at 3.1), so context creation fails outright and the
// emulator never opens a window.
//
// `POM2_GL_ES` says "we speak GLES", nothing more. `__EMSCRIPTEN__` keeps its
// real meaning ("we are in a browser") and is still the right guard for the
// things that genuinely are browser-specific — sockets, threads, the file
// system — which is why those TUs are deliberately left alone.

#ifndef POM2_BUILD_H
#define POM2_BUILD_H

/// 1 when the GL tier is OpenGL ES 3.0 / GLSL ES 300, 0 for desktop GL 3.2.
///
/// Set implicitly by the WASM build (WebGL 2 is GLES 3.0, so Emscripten has no
/// choice), or explicitly by `-DPOM2_GLES=ON`, which defines POM2_BUILD_GLES.
#if defined(__EMSCRIPTEN__) || defined(POM2_BUILD_GLES)
#  define POM2_GL_ES 1
#else
#  define POM2_GL_ES 0
#endif

/// 1 when the platform gives us host sockets, 0 when it does not.
///
/// POM2's three networking translation units — AiControlServer (the HTTP
/// control API), SuperSerialCard (the telnet bridge) and W5100Device (the
/// Uthernet II's hardware TCP/IP stack mapped onto host sockets) — were written
/// against POSIX sockets. Exactly one target has no usable host sockets at all:
///
///   * Emscripten: the browser exposes no BSD-socket API, and nothing can be
///     abstracted over that absence.
///
/// WINDOWS IS NOT ON THAT LIST ANY MORE (2026-08-01). It has sockets, via
/// Winsock2 — the same stack behind a different API (SOCKET vs int,
/// closesocket, ioctlsocket, WSAStartup, WSAGetLastError). That difference now
/// lives in ONE header, `SocketCompat.h`, and the three TUs above are written
/// against it, so the Uthernet II, the telnet bridge and the AI control API
/// work on Windows like anywhere else.
///
/// Under Emscripten the affected features still degrade the documented way: the
/// cards plug, reset and answer their registers but see no traffic, and the SSC
/// opens no listener. Everything else — CPU, video, audio, disks, printer — is
/// unaffected.
///
/// Guard host-socket code with `#if POM2_HAS_SOCKETS`, not with
/// `#ifndef __EMSCRIPTEN__`: the latter silently assumed "not a browser
/// therefore POSIX", which is what broke the Windows build in the first place.
/// And inside such a block, reach for `SocketCompat.h` rather than a bare
/// POSIX call — see the four silent Winsock traps documented there.
#if !defined(NEOST_WITH_NET) || defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#  define POM2_HAS_SOCKETS 0
#else
#  define POM2_HAS_SOCKETS 1
#endif

#endif  // POM2_BUILD_H
