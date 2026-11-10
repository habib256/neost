# Community 121

> 19 nodes

## Key Concepts

- **FDC WD1772 + DMA + STX — `Fdc.cpp`, `StxImage.cpp` ↔ `fdc.c`, `floppies/stx.c`** (15 connections) — `docs/HATARI_DIVERGENCES.md`
- **.loadImage()** (13 connections) — `src/io/Fdc.hpp`
- **.writeBack()** (11 connections) — `src/io/Fdc.hpp`
- **.eject()** (8 connections) — `src/io/Fdc.hpp`
- **decodeMsa()** (8 connections) — `src/io/Fdc.cpp`
- **Débogage headless (l'outil n°1)** (8 connections) — `DEV.md`
- **.msaSelfTest()** (7 connections) — `src/io/Fdc.hpp`
- **decodeDim()** (6 connections) — `src/io/Fdc.cpp`
- **encodeMsa()** (6 connections) — `src/io/Fdc.cpp`
- **.present()** (6 connections) — `src/io/Fdc.hpp`
- **.transitionSetState()** (5 connections) — `src/io/Fdc.hpp`
- **.transitionForceWprt()** (4 connections) — `src/io/Fdc.hpp`
- **decodeContainer()** (4 connections) — `src/io/Fdc.cpp`
- **looksLikeMsaHeader()** (4 connections) — `src/io/Fdc.cpp`
- **A30 — les parseurs d'images disquette passent au fuzzer, et la CI ne bâtissait pas ce qu'elle lance (2026-08-28)** (4 connections) — `CHANGELOG.md`
- **Techniques vérifiées** (3 connections) — `DEV.md`
- **.inserted()** (2 connections) — `src/io/Fdc.hpp`
- **msaFindRun()** (2 connections) — `src/io/Fdc.cpp`
- **string** (1 connections)

## Relationships

- [Community 95](Community_95.md) (19 shared connections)
- [Community 12](Community_12.md) (7 shared connections)
- [Community 24](Community_24.md) (7 shared connections)
- [Community 31](Community_31.md) (5 shared connections)
- [Community 102](Community_102.md) (5 shared connections)
- [Community 78](Community_78.md) (2 shared connections)
- [Community 26](Community_26.md) (2 shared connections)
- [Community 18](Community_18.md) (2 shared connections)
- [Community 88](Community_88.md) (2 shared connections)
- [Community 63](Community_63.md) (2 shared connections)
- [Community 273](Community_273.md) (1 shared connections)
- [Community 124](Community_124.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HATARI_DIVERGENCES.md`
- `src/io/Fdc.cpp`
- `src/io/Fdc.hpp`

## Audit Trail

- EXTRACTED: 41 (46%)
- INFERRED: 49 (54%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*