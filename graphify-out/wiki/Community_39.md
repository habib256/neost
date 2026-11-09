# Community 39

> 44 nodes

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (12 connections) — `src/core/Scheduler.hpp`
- **.runMfpTimersTo()** (9 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (9 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (8 connections) — `CHANGELOG.md`
- **.schedule()** (7 connections) — `src/core/Scheduler.hpp`
- **3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour** (5 connections) — `docs/PERFORMANCE.md`
- **.cancel()** (4 connections) — `src/core/Scheduler.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **.nextDue()** (4 connections) — `src/core/Scheduler.hpp`
- **.serialize()** (4 connections) — `src/core/Scheduler.hpp`
- **.setDispatchHooks()** (4 connections) — `src/core/Scheduler.hpp`
- **function** (4 connections)
- **.runBlitterTo()** (3 connections) — `src/core/Scheduler.hpp`
- **.setCallback()** (3 connections) — `src/core/Scheduler.hpp`
- **.syncTo()** (3 connections) — `src/core/Scheduler.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **.armedInvariant()** (2 connections) — `src/core/Scheduler.hpp`
- **.isMfpTimer()** (2 connections) — `src/core/Scheduler.hpp`
- **.setEndSlice()** (2 connections) — `src/core/Scheduler.hpp`
- **.setLiveClock()** (2 connections) — `src/core/Scheduler.hpp`
- **Callback** (2 connections)
- **endSlice_** (2 connections) — `src/core/Scheduler.hpp`
- **SRC_COUNT** (2 connections) — `src/core/Scheduler.hpp`
- **A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)** (2 connections) — `CHANGELOG.md`
- *... and 19 more nodes in this community*

## Relationships

- [Community 37](Community_37.md) (6 shared connections)
- [Community 160](Community_160.md) (6 shared connections)
- [Community 173](Community_173.md) (5 shared connections)
- [Community 77](Community_77.md) (5 shared connections)
- [Community 67](Community_67.md) (3 shared connections)
- [Community 130](Community_130.md) (3 shared connections)
- [Community 102](Community_102.md) (2 shared connections)
- [Community 222](Community_222.md) (2 shared connections)
- [Community 64](Community_64.md) (2 shared connections)
- [Community 386](Community_386.md) (2 shared connections)
- [Community 6](Community_6.md) (1 shared connections)
- [Community 349](Community_349.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 75 (72%)
- INFERRED: 29 (28%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*