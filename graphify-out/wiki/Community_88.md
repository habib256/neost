# Community 88

> 37 nodes

## Key Concepts

- **Vidéo (Shifter)** (23 connections) — `docs/IMPLEMENTED.md`
- **Shifter::glueSelfTest()** (20 connections) — `src/core/VideoGlue.cpp`
- **.renderGlueFrame()** (17 connections) — `src/core/Shifter.hpp`
- **.renderLine()** (16 connections) — `src/core/Shifter.hpp`
- **.stColorToArgb()** (11 connections) — `src/core/Shifter.hpp`
- **.glueLineBytes()** (11 connections) — `src/core/Shifter.hpp`
- **.decodeLineIndices()** (10 connections) — `src/core/Shifter.hpp`
- **.finishFrame()** (10 connections) — `src/core/Shifter.hpp`
- **.endVideoLine()** (10 connections) — `src/core/Shifter.hpp`
- **.scrollCounterAdvance()** (8 connections) — `src/core/Shifter.hpp`
- **.bordered()** (7 connections) — `src/core/Shifter.hpp`
- **.serialize()** (7 connections) — `src/core/Shifter.hpp`
- **7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)** (7 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.spec512SelfTest()** (6 connections) — `src/core/Shifter.hpp`
- **.activeWidth()** (6 connections) — `src/core/Shifter.hpp`
- **.commitAnchor()** (5 connections) — `src/core/Shifter.hpp`
- **.glueSelfTest()** (5 connections) — `src/core/Shifter.hpp`
- **2. Vidéo (GLUE, MMU, Shifter) — `Shifter.cpp`, `VideoGlue.cpp`, `VideoCounter.cpp` ↔ `video.c`, `spec512.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **.steBorderBytes()** (4 connections) — `src/core/Shifter.hpp`
- **.applyShifterBusAlignment()** (4 connections) — `src/core/Shifter.hpp`
- **.decodeWindowIndices()** (4 connections) — `src/core/Shifter.hpp`
- **3.4 Shifter — deux tables au lieu de deux boucles** (4 connections) — `docs/PERFORMANCE.md`
- **.renderFrame()** (3 connections) — `src/core/Shifter.hpp`
- **.commitScanline()** (3 connections) — `src/core/Shifter.hpp`
- **Shifter::commitScanline()** (3 connections) — `src/core/VideoCounter.cpp`
- *... and 12 more nodes in this community*

## Relationships

- [Community 5](Community_5.md) (22 shared connections)
- [Community 4](Community_4.md) (13 shared connections)
- [Community 596](Community_596.md) (10 shared connections)
- [Community 205](Community_205.md) (10 shared connections)
- [Community 60](Community_60.md) (5 shared connections)
- [Community 24](Community_24.md) (4 shared connections)
- [Community 449](Community_449.md) (4 shared connections)
- [Community 444](Community_444.md) (3 shared connections)
- [Community 470](Community_470.md) (3 shared connections)
- [Community 258](Community_258.md) (3 shared connections)
- [Community 39](Community_39.md) (2 shared connections)
- [Community 203](Community_203.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/CASE_STUDIES.md`
- `docs/CYCLE_ACCURACY.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `docs/PERFORMANCE.md`
- `src/core/Shifter.hpp`
- `src/core/VideoCounter.cpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 58 (35%)
- INFERRED: 108 (65%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*