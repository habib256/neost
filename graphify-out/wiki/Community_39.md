# Community 39

> 60 nodes

## Key Concepts

- **Scheduler** (51 connections) — `src/core/Scheduler.hpp`
- **.syncTo()** (14 connections) — `src/core/Scheduler.hpp`
- **.liveNow()** (11 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (10 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (9 connections) — `CHANGELOG.md`
- **.runMfpTimersTo()** (6 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (6 connections) — `src/core/Scheduler.hpp`
- **.schedule()** (6 connections) — `src/core/Scheduler.hpp`
- **function** (5 connections)
- **.addStolenCycles()** (4 connections) — `src/core/Scheduler.hpp`
- **.cancel()** (4 connections) — `src/core/Scheduler.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **.nextDue()** (4 connections) — `src/core/Scheduler.hpp`
- **Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)** (4 connections) — `CHANGELOG.md`
- **Refermés** (4 connections) — `docs/HATARI_DIVERGENCES.md`
- **Cœur & boot** (4 connections) — `docs/IMPLEMENTED.md`
- **3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour** (4 connections) — `docs/PERFORMANCE.md`
- **.armedInvariant()** (3 connections) — `src/core/Scheduler.hpp`
- **.cyclesUntil()** (3 connections) — `src/core/Scheduler.hpp`
- **.firingDue()** (3 connections) — `src/core/Scheduler.hpp`
- **.rawCyclesUntil()** (3 connections) — `src/core/Scheduler.hpp`
- **.serialize()** (3 connections) — `src/core/Scheduler.hpp`
- **.setCallback()** (3 connections) — `src/core/Scheduler.hpp`
- **.syncToRollingFrames()** (3 connections) — `src/core/Scheduler.hpp`
- **A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)** (3 connections) — `CHANGELOG.md`
- *... and 35 more nodes in this community*

## Relationships

- [Community 77](Community_77.md) (7 shared connections)
- [Community 130](Community_130.md) (5 shared connections)
- [Community 203](Community_203.md) (4 shared connections)
- [Community 215](Community_215.md) (4 shared connections)
- [Community 392](Community_392.md) (3 shared connections)
- [Community 9](Community_9.md) (3 shared connections)
- [Community 258](Community_258.md) (2 shared connections)
- [Community 88](Community_88.md) (2 shared connections)
- [Community 102](Community_102.md) (1 shared connections)
- [Community 471](Community_471.md) (1 shared connections)
- [Community 237](Community_237.md) (1 shared connections)
- [Community 317](Community_317.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 92 (69%)
- INFERRED: 42 (31%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*