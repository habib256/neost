# Community 205

> 28 nodes

## Key Concepts

- **.write8()** (14 connections) — `src/core/Shifter.hpp`
- **.updateGlueState()** (10 connections) — `src/core/Shifter.hpp`
- **Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`** (10 connections) — `docs/HATARI_DIVERGENCES.md`
- **Convergence Moira ↔ WinUAE — chantier « full timing-engine » (beam-sync)** (10 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.syncCpuBus()** (8 connections) — `src/core/Shifter.hpp`
- **.recordColorWrite()** (7 connections) — `src/core/Shifter.hpp`
- **liveFrameClock_** (7 connections) — `src/core/Shifter.hpp`
- **.restartVideoCounter()** (5 connections) — `src/core/Shifter.hpp`
- **Shifter::writeVideoCounterByte()** (5 connections) — `src/core/VideoCounter.cpp`
- **Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)** (5 connections) — `CHANGELOG.md`
- **3. Ce qui est ACQUIS (phases 0-6 + extensions)** (5 connections) — `docs/CYCLE_ACCURACY.md`
- **.realLinePos()** (4 connections) — `src/core/Shifter.hpp`
- **.recordSyncWrite()** (4 connections) — `src/core/Shifter.hpp`
- **⭐ ÉTAT COURANT (posé le 2026-08-27 — lire CECI avant tout le reste)** (4 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.beamLineActive()** (3 connections) — `src/core/Shifter.hpp`
- **.beamPos()** (3 connections) — `src/core/Shifter.hpp`
- **.lineStoppedOver()** (3 connections) — `src/core/Shifter.hpp`
- **Shifter::outsideHorizontalDe()** (3 connections) — `src/core/VideoGlue.cpp`
- **.writeShifterRes()** (2 connections) — `src/core/Shifter.hpp`
- **.writeVideoCounterByte()** (2 connections) — `src/core/Shifter.hpp`
- **Shifter::datedResWriteCycle()** (2 connections) — `src/core/VideoGlue.cpp`
- **hblShorten_** (2 connections) — `src/core/Shifter.hpp`
- **1. L'outil n°1 — harnais différentiel de cycles (Moira ↔ WinUAE)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **5. ✅ CONVERGENCE INSTRUCTION — COMPLÈTE (validée par workflow 6 classes)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **6. ✅ FONDATION CORRIGÉE — dispatch BLOC (Enchanted Land DÉBLOQUÉ)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- *... and 3 more nodes in this community*

## Relationships

- [Community 288](Community_288.md) (16 shared connections)
- [Community 5](Community_5.md) (14 shared connections)
- [Community 88](Community_88.md) (12 shared connections)
- [Community 39](Community_39.md) (4 shared connections)
- [Community 130](Community_130.md) (2 shared connections)
- [Community 392](Community_392.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 27](Community_27.md) (2 shared connections)
- [Community 95](Community_95.md) (1 shared connections)
- [Community 135](Community_135.md) (1 shared connections)
- [Community 317](Community_317.md) (1 shared connections)
- [Community 105](Community_105.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/CYCLE_ACCURACY.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `src/core/Shifter.hpp`
- `src/core/VideoCounter.cpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 33 (35%)
- INFERRED: 61 (65%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*