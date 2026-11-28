# Community 121

> 16 nodes

## Key Concepts

- **Disquette (FDC WD1772 + DMA)** (31 connections) — `docs/IMPLEMENTED.md`
- **.nextSectorIDStx()** (9 connections) — `src/io/Fdc.hpp`
- **.readSectorStx()** (9 connections) — `src/io/Fdc.hpp`
- **.sectorsCountView()** (9 connections) — `src/io/StxImage.hpp`
- **.densityFactor()** (8 connections) — `src/io/Fdc.hpp`
- **.readAddressStx()** (7 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosBytes()** (6 connections) — `src/io/Fdc.hpp`
- **.indexCurrentPosCycles()** (6 connections) — `src/io/Fdc.hpp`
- **Refermés** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **3. FDC WD1772, DMA, STX, IPF — `Fdc.cpp`, `StxImage.cpp`, `WrittenTrack.cpp`, `IpfImage.cpp` ↔ `fdc.c`, `floppies/stx.c`, `msa.c`, `dim.c`, `ipf.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **Cœur libretro** (5 connections) — `TODO.md`
- **.busToFdcCycles()** (4 connections) — `src/io/Fdc.hpp`
- **.stxWrites()** (4 connections) — `src/io/Fdc.hpp`
- **.bufferAddTiming()** (3 connections) — `src/io/Fdc.hpp`
- **Faux positifs** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts ouverts** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 149](Community_149.md) (15 shared connections)
- [Community 95](Community_95.md) (13 shared connections)
- [Community 12](Community_12.md) (9 shared connections)
- [Community 31](Community_31.md) (9 shared connections)
- [Community 24](Community_24.md) (6 shared connections)
- [Community 14](Community_14.md) (4 shared connections)
- [Community 103](Community_103.md) (3 shared connections)
- [Community 176](Community_176.md) (2 shared connections)
- [Community 252](Community_252.md) (2 shared connections)
- [Community 397](Community_397.md) (1 shared connections)
- [Community 392](Community_392.md) (1 shared connections)
- [Community 470](Community_470.md) (1 shared connections)

## Source Files

- `TODO.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `src/io/Fdc.hpp`
- `src/io/StxImage.hpp`

## Audit Trail

- EXTRACTED: 24 (26%)
- INFERRED: 70 (74%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*