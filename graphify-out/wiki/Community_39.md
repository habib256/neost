# Community 39

> 54 nodes

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **.liveNow()** (14 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (12 connections) — `src/core/Scheduler.hpp`
- **.runMfpTimersTo()** (9 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (9 connections) — `src/core/Scheduler.hpp`
- **Interruptions (MFP 68901)** (9 connections) — `docs/IMPLEMENTED.md`
- **.schedule()** (7 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (7 connections) — `CHANGELOG.md`
- **3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour** (5 connections) — `docs/PERFORMANCE.md`
- **.iack()** (4 connections) — `src/io/Mfp.hpp`
- **.cancel()** (4 connections) — `src/core/Scheduler.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **.cyclesUntil()** (4 connections) — `src/core/Scheduler.hpp`
- **.nextDue()** (4 connections) — `src/core/Scheduler.hpp`
- **.rawCyclesUntil()** (4 connections) — `src/core/Scheduler.hpp`
- **.serialize()** (4 connections) — `src/core/Scheduler.hpp`
- **.setDispatchHooks()** (4 connections) — `src/core/Scheduler.hpp`
- **function** (4 connections)
- **.firingDue()** (3 connections) — `src/core/Scheduler.hpp`
- **.runBlitterTo()** (3 connections) — `src/core/Scheduler.hpp`
- **.setCallback()** (3 connections) — `src/core/Scheduler.hpp`
- **.syncTo()** (3 connections) — `src/core/Scheduler.hpp`
- **Bug hunt passes 1-3 + CI de release (2026-07-31)** (3 connections) — `CHANGELOG.md`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **.armedInvariant()** (2 connections) — `src/core/Scheduler.hpp`
- *... and 29 more nodes in this community*

## Relationships

- [Community 135](Community_135.md) (8 shared connections)
- [Community 77](Community_77.md) (7 shared connections)
- [Community 37](Community_37.md) (6 shared connections)
- [Community 130](Community_130.md) (4 shared connections)
- [Community 215](Community_215.md) (3 shared connections)
- [Community 102](Community_102.md) (2 shared connections)
- [Community 138](Community_138.md) (2 shared connections)
- [Community 258](Community_258.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 9](Community_9.md) (2 shared connections)
- [Community 386](Community_386.md) (2 shared connections)
- [Community 6](Community_6.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_AUTOMATION.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 86 (64%)
- INFERRED: 48 (36%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*