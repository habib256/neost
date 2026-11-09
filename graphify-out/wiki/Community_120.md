# Community 120

> 32 nodes

## Key Concepts

- **Timing** (25 connections) — `src/core/VideoGlue.hpp`
- **Vidéo (Shifter)** (14 connections) — `docs/IMPLEMENTED.md`
- **Machine::onHbl()** (12 connections) — `src/core/Machine.cpp`
- **Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`** (9 connections) — `docs/HATARI_DIVERGENCES.md`
- **.writeShifterRes()** (8 connections) — `src/core/Shifter.hpp`
- **.commitScanline()** (4 connections) — `src/core/Shifter.hpp`
- **.updateGlueRes()** (4 connections) — `src/core/Shifter.hpp`
- **P3 — Comportement / protocole (IRQ, registres, resets)** (4 connections) — `docs/HATARI_MAPPING.md`
- **P1 — Registres vidéo STE : 2 restes** (3 connections) — `docs/CYCLE_ACCURACY.md`
- **.activeHeight()** (2 connections) — `src/core/Shifter.hpp`
- **.glueSelfTest()** (2 connections) — `src/core/Shifter.hpp`
- **.liveLineLength()** (2 connections) — `src/core/Shifter.hpp`
- **Hbl_Pos_Hi** (1 connections) — `src/core/VideoGlue.hpp`
- **Hbl_Pos_Low_50** (1 connections) — `src/core/VideoGlue.hpp`
- **Hbl_Pos_Low_60** (1 connections) — `src/core/VideoGlue.hpp`
- **HBlank_Off_Low_50** (1 connections) — `src/core/VideoGlue.hpp`
- **HBlank_Off_Low_60** (1 connections) — `src/core/VideoGlue.hpp`
- **HDE_Off_Hi** (1 connections) — `src/core/VideoGlue.hpp`
- **HDE_Off_Low_50** (1 connections) — `src/core/VideoGlue.hpp`
- **HDE_Off_Low_60** (1 connections) — `src/core/VideoGlue.hpp`
- **HDE_On_Hi** (1 connections) — `src/core/VideoGlue.hpp`
- **HDE_On_Low_50** (1 connections) — `src/core/VideoGlue.hpp`
- **HDE_On_Low_60** (1 connections) — `src/core/VideoGlue.hpp`
- **HSync_Off_Off_Low** (1 connections) — `src/core/VideoGlue.hpp`
- **HSync_On_Off_Low** (1 connections) — `src/core/VideoGlue.hpp`
- *... and 7 more nodes in this community*

## Relationships

- [Community 5](Community_5.md) (6 shared connections)
- [Community 182](Community_182.md) (5 shared connections)
- [Community 37](Community_37.md) (4 shared connections)
- [Community 135](Community_135.md) (3 shared connections)
- [Community 288](Community_288.md) (3 shared connections)
- [Community 205](Community_205.md) (3 shared connections)
- [Community 176](Community_176.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 258](Community_258.md) (2 shared connections)
- [Community 399](Community_399.md) (1 shared connections)
- [Community 10](Community_10.md) (1 shared connections)
- [Community 281](Community_281.md) (1 shared connections)

## Source Files

- `docs/CYCLE_ACCURACY.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `src/core/Machine.cpp`
- `src/core/Shifter.hpp`
- `src/core/VideoGlue.hpp`

## Audit Trail

- EXTRACTED: 36 (49%)
- INFERRED: 38 (51%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*