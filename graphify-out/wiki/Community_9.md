# Community 9

> 43 nodes

## Key Concepts

- **StxImage** (30 connections) — `src/io/StxImage.hpp`
- **StxImage.cpp** (22 connections) — `src/io/StxImage.cpp`
- **FDC WD1772 + DMA + STX — `Fdc.cpp`, `StxImage.cpp` ↔ `fdc.c`, `floppies/stx.c`** (15 connections) — `docs/HATARI_DIVERGENCES.md`
- **main()** (11 connections) — `tests/stx_writetrack_test.cpp`
- **stx_writetrack_test.cpp** (11 connections) — `tests/stx_writetrack_test.cpp`
- **.parse()** (10 connections) — `src/io/StxImage.hpp`
- **.loadWd1772()** (9 connections) — `src/io/StxImage.hpp`
- **.reinterpretSaveTrack()** (8 connections) — `src/io/StxImage.hpp`
- **.findTrack()** (7 connections) — `src/io/StxImage.hpp`
- **.findSectorByPosition()** (6 connections) — `src/io/StxImage.hpp`
- **.saveWd1772()** (6 connections) — `src/io/StxImage.hpp`
- **.buildSectorsSimple()** (6 connections) — `src/io/StxImage.hpp`
- **SaveTrack** (5 connections) — `src/io/StxImage.hpp`
- **forgeFlux()** (5 connections) — `tests/stx_writetrack_test.cpp`
- **crc16()** (4 connections) — `src/io/StxImage.cpp`
- **MoiraConfig.h** (4 connections) — `extern/moira/Moira/MoiraConfig.h`
- **cassert** (4 connections)
- **wr16be()** (3 connections) — `src/io/StxImage.cpp`
- **wr32be()** (3 connections) — `src/io/StxImage.cpp`
- **add()** (3 connections) — `tests/stx_writetrack_test.cpp`
- **expect()** (3 connections) — `tests/stx_writetrack_test.cpp`
- **forgeStx()** (3 connections) — `tests/stx_writetrack_test.cpp`
- **.sides()** (2 connections) — `src/io/StxImage.hpp`
- **.tracksPerSide()** (2 connections) — `src/io/StxImage.hpp`
- **rd16be()** (2 connections) — `src/io/StxImage.cpp`
- *... and 18 more nodes in this community*

## Relationships

- [Community 102](Community_102.md) (17 shared connections)
- [Community 110](Community_110.md) (13 shared connections)
- [Community 4](Community_4.md) (3 shared connections)
- [Community 24](Community_24.md) (3 shared connections)
- [Community 201](Community_201.md) (2 shared connections)
- [Community 153](Community_153.md) (2 shared connections)
- [Community 54](Community_54.md) (2 shared connections)
- [Community 63](Community_63.md) (2 shared connections)
- [Community 95](Community_95.md) (2 shared connections)
- [Community 78](Community_78.md) (1 shared connections)
- [Community 23](Community_23.md) (1 shared connections)
- [Community 176](Community_176.md) (1 shared connections)

## Source Files

- `docs/HATARI_DIVERGENCES.md`
- `extern/moira/Moira/MoiraConfig.h`
- `src/io/StxImage.cpp`
- `src/io/StxImage.hpp`
- `tests/stx_writetrack_test.cpp`

## Audit Trail

- EXTRACTED: 97 (73%)
- INFERRED: 35 (27%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*