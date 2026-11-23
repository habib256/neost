# Community 121

> 25 nodes

## Key Concepts

- **.readTrackStx()** (14 connections) — `src/io/Fdc.hpp`
- **.readTrackST()** (12 connections) — `src/io/Fdc.hpp`
- **Disquette (FDC WD1772 + DMA)** (11 connections) — `docs/IMPLEMENTED.md`
- **.readAddressST()** (9 connections) — `src/io/Fdc.hpp`
- **.nextSectorIDStx()** (9 connections) — `src/io/Fdc.hpp`
- **.readSectorStx()** (9 connections) — `src/io/Fdc.hpp`
- **.sectorsCountView()** (9 connections) — `src/io/StxImage.hpp`
- **.densityFactor()** (8 connections) — `src/io/Fdc.hpp`
- **.bytesPerTrackStx()** (8 connections) — `src/io/Fdc.hpp`
- **stxTrackWithSectorWrites()** (8 connections) — `src/io/Fdc.cpp`
- **.readAddressStx()** (7 connections) — `src/io/Fdc.hpp`
- **.tracksPerDisk()** (6 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosBytes()** (6 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosCycles()** (6 connections) — `src/io/Fdc.hpp`
- **.rngNext()** (6 connections) — `src/io/Fdc.hpp`
- **Refermés** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **.computeFloppyDensity()** (5 connections) — `src/io/Fdc.hpp`
- **3. FDC WD1772, DMA, STX, IPF — `Fdc.cpp`, `StxImage.cpp`, `WrittenTrack.cpp`, `IpfImage.cpp` ↔ `fdc.c`, `floppies/stx.c`, `msa.c`, `dim.c`, `ipf.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **.busToFdcCycles()** (4 connections) — `src/io/Fdc.hpp`
- **crc16()** (4 connections) — `src/io/Fdc.cpp`
- **Écarts voulus — ne pas réaligner** (4 connections) — `docs/HATARI_DIVERGENCES.md`
- **.bufferAddTiming()** (3 connections) — `src/io/Fdc.hpp`
- **Images IPF (SPS/CAPS) dans les lecteurs A et B, sans libcapsimage (2026-10-22)** (3 connections) — `CHANGELOG.md`
- **Faux positifs** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts ouverts** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 149](Community_149.md) (20 shared connections)
- [Community 95](Community_95.md) (16 shared connections)
- [Community 12](Community_12.md) (15 shared connections)
- [Community 24](Community_24.md) (8 shared connections)
- [Community 170](Community_170.md) (8 shared connections)
- [Community 31](Community_31.md) (6 shared connections)
- [Community 63](Community_63.md) (3 shared connections)
- [Community 78](Community_78.md) (1 shared connections)
- [Community 88](Community_88.md) (1 shared connections)
- [Community 175](Community_175.md) (1 shared connections)
- [Community 23](Community_23.md) (1 shared connections)
- [Community 102](Community_102.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `src/io/Fdc.cpp`
- `src/io/Fdc.hpp`
- `src/io/StxImage.hpp`

## Audit Trail

- EXTRACTED: 47 (37%)
- INFERRED: 79 (63%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*