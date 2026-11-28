# Community 60

> 32 nodes

## Key Concepts

- **.write8()** (13 connections) — `src/core/Shifter.hpp`
- **Convergence Moira ↔ WinUAE — chantier « full timing-engine » (beam-sync)** (10 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.syncCpuBus()** (9 connections) — `src/core/Shifter.hpp`
- **liveFrameClock_** (9 connections) — `src/core/Shifter.hpp`
- **.videoCounter()** (8 connections) — `src/core/Shifter.hpp`
- **.recordColorWrite()** (7 connections) — `src/core/Shifter.hpp`
- **7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)** (7 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **Shifter::writeVideoCounterByte()** (5 connections) — `src/core/VideoCounter.cpp`
- **⭐ ÉTAT COURANT (posé le 2026-08-27 — lire CECI avant tout le reste)** (5 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.realLinePos()** (4 connections) — `src/core/Shifter.hpp`
- **Shifter::fetchWindowFrom()** (4 connections) — `src/core/VideoGlue.cpp`
- **Shifter::recordSyncWrite()** (4 connections) — `src/core/VideoGlue.cpp`
- **Validé** (4 connections) — `docs/IMPLEMENTED.md`
- **.beamLineActive()** (3 connections) — `src/core/Shifter.hpp`
- **.beamPos()** (3 connections) — `src/core/Shifter.hpp`
- **.datedResWriteCycle()** (3 connections) — `src/core/Shifter.hpp`
- **Shifter::outsideHorizontalDe()** (3 connections) — `src/core/VideoGlue.cpp`
- **Shifter::writeShifterRes()** (3 connections) — `src/core/VideoGlue.cpp`
- **.writeShifterRes()** (2 connections) — `src/core/Shifter.hpp`
- **.writeVideoCounterByte()** (2 connections) — `src/core/Shifter.hpp`
- **Shifter::datedResWriteCycle()** (2 connections) — `src/core/VideoGlue.cpp`
- **hblShorten_** (2 connections) — `src/core/Shifter.hpp`
- **A10 — la couverture Spectrum 512 est RENDUE, sans une ligne de ROM Atari (2026-08-28)** (2 connections) — `CHANGELOG.md`
- **1. L'outil n°1 — harnais différentiel de cycles (Moira ↔ WinUAE)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **4. Implémenté (RAM_SLOT+IACK DÉFAUT ON depuis 2026-06-17 ; toggle via `=0`)** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- *... and 7 more nodes in this community*

## Relationships

- [Community 5](Community_5.md) (12 shared connections)
- [Community 186](Community_186.md) (9 shared connections)
- [Community 205](Community_205.md) (9 shared connections)
- [Community 88](Community_88.md) (5 shared connections)
- [Community 27](Community_27.md) (4 shared connections)
- [Community 288](Community_288.md) (3 shared connections)
- [Community 9](Community_9.md) (3 shared connections)
- [Community 449](Community_449.md) (1 shared connections)
- [Community 412](Community_412.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 470](Community_470.md) (1 shared connections)
- [Community 232](Community_232.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/IMPLEMENTED.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `src/core/Shifter.hpp`
- `src/core/VideoCounter.cpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 36 (40%)
- INFERRED: 53 (60%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*