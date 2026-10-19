# Community 31

> 39 nodes · cohesion 0.12

## Key Concepts

- **Fdc.cpp** (106 connections) — `src/io/Fdc.cpp`
- **.loadImage()** (13 connections) — `src/io/Fdc.hpp`
- **FDC WD1772 + DMA + STX — `Fdc.cpp`, `StxImage.cpp` ↔ `fdc.c`, `floppies/stx.c`** (13 connections) — `docs/HATARI_DIVERGENCES.md`
- **.readTrackST()** (11 connections) — `src/io/Fdc.hpp`
- **.writeBack()** (11 connections) — `src/io/Fdc.hpp`
- **.bufferAdd()** (11 connections) — `src/io/Fdc.hpp`
- **.updateFloppyDensity()** (10 connections) — `src/io/Fdc.hpp`
- **.writeTrackBuffer()** (9 connections) — `src/io/Fdc.hpp`
- **.writeSectorST()** (8 connections) — `src/io/Fdc.hpp`
- **.writeSectorStx()** (8 connections) — `src/io/Fdc.hpp`
- **decodeMsa()** (8 connections) — `src/io/Fdc.cpp`
- **.readAddressST()** (7 connections) — `src/io/Fdc.hpp`
- **.readAddressStx()** (7 connections) — `src/io/Fdc.hpp`
- **.msaSelfTest()** (7 connections) — `src/io/Fdc.hpp`
- **.eject()** (7 connections) — `src/io/Fdc.hpp`
- **.refreshImageGeometry()** (7 connections) — `src/io/Fdc.hpp`
- **.readSectorST()** (6 connections) — `src/io/Fdc.hpp`
- **.tracksPerDisk()** (6 connections) — `src/io/Fdc.hpp`
- **decodeDim()** (6 connections) — `src/io/Fdc.cpp`
- **encodeMsa()** (6 connections) — `src/io/Fdc.cpp`
- **.present()** (6 connections) — `src/io/Fdc.hpp`
- **stxTrackWithSectorWrites()** (6 connections) — `src/io/Fdc.cpp`
- **.writeTrackStx()** (5 connections) — `src/io/Fdc.hpp`
- **.stxPersist()** (5 connections) — `src/io/Fdc.hpp`
- **.transitionSetState()** (5 connections) — `src/io/Fdc.hpp`
- *... and 14 more nodes in this community*

## Relationships

- [.updateReadTrack](updateReadTrack.md) (38 shared connections)
- [Community 24](Community_24.md) (36 shared connections)
- [Fdc](Fdc.md) (25 shared connections)
- [Community 110](Community_110.md) (20 shared connections)
- [Community 100](Community_100.md) (15 shared connections)
- [Community 208](Community_208.md) (8 shared connections)
- [FloppyDisk](FloppyDisk.md) (5 shared connections)
- [Community 170](Community_170.md) (2 shared connections)
- [Community 226](Community_226.md) (2 shared connections)
- [Community 153](Community_153.md) (2 shared connections)
- [Community 165](Community_165.md) (2 shared connections)
- [StxImage](StxImage.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HATARI_DIVERGENCES.md`
- `src/io/Fdc.cpp`
- `src/io/Fdc.hpp`

## Audit Trail

- EXTRACTED: 167 (65%)
- INFERRED: 90 (35%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*