# Community 31

> 50 nodes

## Key Concepts

- **Track** (44 connections) — `src/io/StxImage.hpp`
- **.readTrackStx()** (14 connections) — `src/io/Fdc.hpp`
- **.readTrackST()** (12 connections) — `src/io/Fdc.hpp`
- **Disquette (FDC WD1772 + DMA)** (11 connections) — `docs/IMPLEMENTED.md`
- **.readAddressST()** (9 connections) — `src/io/Fdc.hpp`
- **.nextSectorIDStx()** (9 connections) — `src/io/Fdc.hpp`
- **.readSectorStx()** (9 connections) — `src/io/Fdc.hpp`
- **.sectorsCountView()** (9 connections) — `src/io/StxImage.hpp`
- **.densityFactor()** (8 connections) — `src/io/Fdc.hpp`
- **.writeSectorStx()** (8 connections) — `src/io/Fdc.hpp`
- **stxTrackWithSectorWrites()** (8 connections) — `src/io/Fdc.cpp`
- **.readAddressStx()** (7 connections) — `src/io/Fdc.hpp`
- **.tracksPerDisk()** (6 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosBytes()** (6 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosCycles()** (6 connections) — `src/io/Fdc.hpp`
- **.rngNext()** (6 connections) — `src/io/Fdc.hpp`
- **Refermés** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **3. FDC WD1772, DMA, STX, IPF — `Fdc.cpp`, `StxImage.cpp`, `WrittenTrack.cpp`, `IpfImage.cpp` ↔ `fdc.c`, `floppies/stx.c`, `msa.c`, `dim.c`, `ipf.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **.busToFdcCycles()** (4 connections) — `src/io/Fdc.hpp`
- **crc16()** (4 connections) — `src/io/Fdc.cpp`
- **.bufferReadBytePos()** (4 connections) — `src/io/Fdc.hpp`
- **Écarts voulus — ne pas réaligner** (4 connections) — `docs/HATARI_DIVERGENCES.md`
- **.bufferAddTiming()** (3 connections) — `src/io/Fdc.hpp`
- **added** (2 connections) — `src/io/StxImage.hpp`
- **blockSize** (1 connections) — `src/io/StxImage.hpp`
- *... and 25 more nodes in this community*

## Relationships

- [Community 149](Community_149.md) (22 shared connections)
- [Community 95](Community_95.md) (18 shared connections)
- [Community 12](Community_12.md) (15 shared connections)
- [Community 63](Community_63.md) (12 shared connections)
- [Community 24](Community_24.md) (7 shared connections)
- [Community 121](Community_121.md) (6 shared connections)
- [Community 287](Community_287.md) (2 shared connections)
- [Community 238](Community_238.md) (1 shared connections)
- [Community 88](Community_88.md) (1 shared connections)
- [Community 175](Community_175.md) (1 shared connections)
- [Community 23](Community_23.md) (1 shared connections)
- [Community 143](Community_143.md) (1 shared connections)

## Source Files

- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `src/io/Fdc.cpp`
- `src/io/Fdc.hpp`
- `src/io/StxImage.hpp`

## Audit Trail

- EXTRACTED: 77 (48%)
- INFERRED: 84 (52%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*