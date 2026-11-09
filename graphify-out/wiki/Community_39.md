# Community 39

> 47 nodes

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (12 connections) — `src/core/Scheduler.hpp`
- **.runMfpTimersTo()** (9 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (9 connections) — `src/core/Scheduler.hpp`
- **.schedule()** (7 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (7 connections) — `CHANGELOG.md`
- **🔁 2ᵉ passe d'audit approfondi (2026-06-15, workflow 7 agents)** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **Conclusion** (6 connections) — `docs/HATARI_DIVERGENCES.md`
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
- *... and 22 more nodes in this community*

## Relationships

- [Community 160](Community_160.md) (7 shared connections)
- [Community 139](Community_139.md) (6 shared connections)
- [Community 77](Community_77.md) (5 shared connections)
- [Community 215](Community_215.md) (4 shared connections)
- [Community 54](Community_54.md) (3 shared connections)
- [Community 130](Community_130.md) (3 shared connections)
- [Community 102](Community_102.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 386](Community_386.md) (2 shared connections)
- [Community 135](Community_135.md) (1 shared connections)
- [Community 374](Community_374.md) (1 shared connections)
- [Community 88](Community_88.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 81 (72%)
- INFERRED: 31 (28%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*