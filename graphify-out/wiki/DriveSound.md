# DriveSound

> 40 nodes · cohesion 0.07

## Key Concepts

- **DriveSound** (18 connections) — `src/audio/DriveSound.hpp`
- **FdcSound** (10 connections) — `src/io/Fdc.hpp`
- **DriveSound.cpp** (10 connections) — `src/audio/DriveSound.cpp`
- **tcpConnect()** (8 connections) — `src/net/Socket.cpp`
- **.shutdown()** (6 connections) — `src/audio/DriveSound.hpp`
- **SocketHandle** (6 connections)
- **socketValid()** (5 connections) — `src/net/Socket.hpp`
- **.init()** (4 connections) — `src/audio/DriveSound.hpp`
- **.motorStop()** (4 connections) — `src/audio/DriveSound.hpp`
- **.onEvent()** (4 connections) — `src/audio/DriveSound.hpp`
- **.select()** (4 connections) — `src/io/StePads.hpp`
- **sockClose()** (4 connections) — `src/net/Socket.cpp`
- **sockRecv()** (4 connections) — `src/net/Socket.cpp`
- **sockShutdown()** (4 connections) — `src/net/Socket.cpp`
- **FujiNet retiré : NeoST n'émule plus que du matériel qui a existé (2026-08-22)** (4 connections) — `CHANGELOG.md`
- **.setEnabled()** (3 connections) — `src/audio/DriveSound.hpp`
- **sockHasData()** (3 connections) — `src/net/Socket.cpp`
- **sockSend()** (3 connections) — `src/net/Socket.cpp`
- **.mix()** (2 connections) — `src/audio/DriveSound.hpp`
- **.~DriveSound()** (2 connections) — `src/audio/DriveSound.hpp`
- **netInitOnce()** (2 connections) — `src/net/Socket.cpp`
- **miniaudio** (2 connections)
- **.enabled()** (1 connections) — `src/audio/DriveSound.hpp`
- **.ok()** (1 connections) — `src/audio/DriveSound.hpp`
- **string** (1 connections)
- *... and 15 more nodes in this community*

## Relationships

- [Community 165](Community_165.md) (8 shared connections)
- [cstdint](cstdint.md) (3 shared connections)
- [Community 100](Community_100.md) (3 shared connections)
- [Fdc](Fdc.md) (1 shared connections)
- [AppConfig.cpp](AppConfig.cpp.md) (1 shared connections)
- [Community 115](Community_115.md) (1 shared connections)
- [Scu](Scu.md) (1 shared connections)
- [StePads](StePads.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `src/audio/DriveSound.cpp`
- `src/audio/DriveSound.hpp`
- `src/io/Fdc.hpp`
- `src/io/StePads.hpp`
- `src/net/Socket.cpp`
- `src/net/Socket.hpp`

## Audit Trail

- EXTRACTED: 59 (79%)
- INFERRED: 16 (21%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*