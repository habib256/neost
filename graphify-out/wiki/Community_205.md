# Community 205

> 14 nodes

## Key Concepts

- **Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`** (9 connections) — `docs/HATARI_DIVERGENCES.md`
- **Shifter::liveGlueCatchUp()** (8 connections) — `src/core/VideoGlue.cpp`
- **Shifter::replayGlue()** (8 connections) — `src/core/VideoGlue.cpp`
- **.writeShifterRes()** (8 connections) — `src/core/Shifter.hpp`
- **.updateGlueState()** (5 connections) — `src/core/Shifter.hpp`
- **glueLineLenFor()** (5 connections) — `src/core/VideoGlue.cpp`
- **2. Vidéo / Shifter / Glue (video.c, spec512.c, conv_st.c)** (5 connections) — `docs/HATARI_MAPPING.md`
- **.updateGlueRes()** (4 connections) — `src/core/Shifter.hpp`
- **P3 — Comportement / protocole (IRQ, registres, resets)** (4 connections) — `docs/HATARI_MAPPING.md`
- **.startHBL()** (3 connections) — `src/core/Shifter.hpp`
- **.writeVideoCounterByte()** (3 connections) — `src/core/Shifter.hpp`
- **.liveLineLength()** (2 connections) — `src/core/Shifter.hpp`
- **lineGeom_** (2 connections) — `src/core/Shifter.hpp`
- **NO_SYNC/SYNC_HIGH — gardes comparatives ajoutées le 2026-10-19** (2 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 5](Community_5.md) (9 shared connections)
- [Community 288](Community_288.md) (5 shared connections)
- [Community 88](Community_88.md) (3 shared connections)
- [Community 258](Community_258.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 182](Community_182.md) (2 shared connections)
- [Community 352](Community_352.md) (2 shared connections)
- [Community 211](Community_211.md) (1 shared connections)
- [Community 139](Community_139.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 105](Community_105.md) (1 shared connections)
- [Community 176](Community_176.md) (1 shared connections)

## Source Files

- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `src/core/Shifter.hpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 25 (50%)
- INFERRED: 25 (50%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*