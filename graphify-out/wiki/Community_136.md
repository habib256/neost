# Community 136

> 27 nodes

## Key Concepts

- **MidiRing** (16 connections) — `src/net/MidiRing.hpp`
- **Les extensions, une par une** (10 connections) — `docs/EXTENSIONS.md`
- **main()** (5 connections) — `tests/midi_ring_logic.cpp`
- **Clé Steinberg — dongle Cubase sur le port cartouche (`--dongle`, GUI Dongles)** (4 connections) — `docs/EXTENSIONS.md`
- **.sendByte()** (3 connections) — `src/net/MidiRing.hpp`
- **.tryPop()** (3 connections) — `src/net/MidiRing.hpp`
- **Périphériques des ports — un par port : clés joystick / série, DAC Pro Sound, boutons (`--plug`, GUI Dongles)** (3 connections) — `docs/EXTENSIONS.md`
- **.close()** (2 connections) — `src/net/MidiRing.hpp`
- **.open()** (2 connections) — `src/net/MidiRing.hpp`
- **.poll()** (2 connections) — `src/net/MidiRing.hpp`
- **Anneau MIDI réseau — MIDIMaze en ligne (`--midi-net H:P[:L]`)** (2 connections) — `docs/EXTENSIONS.md`
- **Appareils MIDI de l'hôte — le câble DIN du ST vers du vrai matériel** (2 connections) — `docs/EXTENSIONS.md`
- **Recette de capture matérielle (à faire quand une clé et un logiciel sont réunis)** (2 connections) — `docs/EXTENSIONS.md`
- **UltraSatan — interface SD/MMC sur le bus ACSI (`--ultrasatan`, GUI Hard disks)** (2 connections) — `docs/EXTENSIONS.md`
- **.ok()** (1 connections) — `src/net/MidiRing.hpp`
- **deque** (1 connections)
- **SocketHandle** (1 connections)
- **fd_** (1 connections) — `src/net/MidiRing.hpp`
- **jitter_** (1 connections) — `src/net/MidiRing.hpp`
- **peerAddr_** (1 connections) — `src/net/MidiRing.hpp`
- **peerAddrLen_** (1 connections) — `src/net/MidiRing.hpp`
- **EtherNEC — NE2000 sur le port cartouche (`--ethernec`, GUI Network)** (1 connections) — `docs/EXTENSIONS.md`
- **Inventaire des dongles ST connus (recherche 2026-08-23) et pourquoi le reste n'est pas émulé** (1 connections) — `docs/EXTENSIONS.md`
- **Modem Hayes sur RS-232 (`--modem`, GUI Network)** (1 connections) — `docs/EXTENSIONS.md`
- **NetUSBee — NE2000 + hôte USB ISP1160 sur le port cartouche (`--netusbee`, GUI Network)** (1 connections) — `docs/EXTENSIONS.md`
- *... and 2 more nodes in this community*

## Relationships

- [Community 51](Community_51.md) (3 shared connections)
- [Community 4](Community_4.md) (2 shared connections)
- [Community 249](Community_249.md) (1 shared connections)
- [Community 67](Community_67.md) (1 shared connections)
- [Community 399](Community_399.md) (1 shared connections)
- [Community 101](Community_101.md) (1 shared connections)
- [Community 9](Community_9.md) (1 shared connections)
- [Community 232](Community_232.md) (1 shared connections)
- [Community 150](Community_150.md) (1 shared connections)
- [Community 103](Community_103.md) (1 shared connections)

## Source Files

- `docs/EXTENSIONS.md`
- `src/net/MidiRing.hpp`
- `tests/midi_ring_logic.cpp`

## Audit Trail

- EXTRACTED: 28 (67%)
- INFERRED: 14 (33%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*