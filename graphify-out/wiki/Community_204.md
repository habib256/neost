# Community 204

> 45 nodes

## Key Concepts

- **Track** (45 connections) — `src/io/StxImage.hpp`
- **Disquette (FDC WD1772 + DMA)** (31 connections) — `docs/IMPLEMENTED.md`
- **.readTrackStx()** (14 connections) — `src/io/Fdc.hpp`
- **.readTrackST()** (12 connections) — `src/io/Fdc.hpp`
- **.nextSectorIDStx()** (9 connections) — `src/io/Fdc.hpp`
- **.readSectorStx()** (9 connections) — `src/io/Fdc.hpp`
- **.sectorsCountView()** (9 connections) — `src/io/StxImage.hpp`
- **.densityFactor()** (8 connections) — `src/io/Fdc.hpp`
- **.bytesPerTrackStx()** (8 connections) — `src/io/Fdc.hpp`
- **stxTrackWithSectorWrites()** (8 connections) — `src/io/Fdc.cpp`
- **.readAddressStx()** (7 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosBytes()** (6 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosCycles()** (6 connections) — `src/io/Fdc.hpp`
- **.rngNext()** (6 connections) — `src/io/Fdc.hpp`
- **.computeFloppyDensity()** (5 connections) — `src/io/Fdc.hpp`
- **.busToFdcCycles()** (4 connections) — `src/io/Fdc.hpp`
- **crc16()** (4 connections) — `src/io/Fdc.cpp`
- **.stxWrites()** (4 connections) — `src/io/Fdc.hpp`
- **Images IPF (SPS/CAPS) dans les lecteurs A et B, sans libcapsimage (2026-10-22)** (4 connections) — `CHANGELOG.md`
- **.bufferAddTiming()** (3 connections) — `src/io/Fdc.hpp`
- **added** (2 connections) — `src/io/StxImage.hpp`
- **blockSize** (1 connections) — `src/io/StxImage.hpp`
- **flags** (1 connections) — `src/io/StxImage.hpp`
- **fuzzySize** (1 connections) — `src/io/StxImage.hpp`
- **mfmSize** (1 connections) — `src/io/StxImage.hpp`
- *... and 20 more nodes in this community*

## Relationships

- [Community 406](Community_406.md) (18 shared connections)
- [Community 138](Community_138.md) (14 shared connections)
- [Community 12](Community_12.md) (14 shared connections)
- [Community 31](Community_31.md) (13 shared connections)
- [Community 95](Community_95.md) (13 shared connections)
- [Community 24](Community_24.md) (7 shared connections)
- [Community 14](Community_14.md) (4 shared connections)
- [Community 103](Community_103.md) (3 shared connections)
- [Community 102](Community_102.md) (2 shared connections)
- [Community 176](Community_176.md) (2 shared connections)
- [Community 203](Community_203.md) (2 shared connections)
- [Community 48](Community_48.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/IMPLEMENTED.md`
- `src/io/Fdc.cpp`
- `src/io/Fdc.hpp`
- `src/io/StxImage.hpp`

## Audit Trail

- EXTRACTED: 72 (43%)
- INFERRED: 96 (57%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*