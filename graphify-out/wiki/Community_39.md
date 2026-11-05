# Community 39

> 54 nodes

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (19 connections) — `src/core/Scheduler.hpp`
- **.syncTo()** (11 connections) — `src/core/Scheduler.hpp`
- **.liveNow()** (10 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (10 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (10 connections) — `CHANGELOG.md`
- **.schedule()** (6 connections) — `src/core/Scheduler.hpp`
- **.runMfpTimersTo()** (5 connections) — `src/core/Scheduler.hpp`
- **Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)** (5 connections) — `CHANGELOG.md`
- **Cœur & boot** (5 connections) — `docs/IMPLEMENTED.md`
- **3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour** (5 connections) — `docs/PERFORMANCE.md`
- **.addStolenCycles()** (4 connections) — `src/core/Scheduler.hpp`
- **.cancel()** (4 connections) — `src/core/Scheduler.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **.nextDue()** (4 connections) — `src/core/Scheduler.hpp`
- **function** (4 connections)
- **A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)** (4 connections) — `CHANGELOG.md`
- **.armedInvariant()** (3 connections) — `src/core/Scheduler.hpp`
- **.cyclesUntil()** (3 connections) — `src/core/Scheduler.hpp`
- **.rawCyclesUntil()** (3 connections) — `src/core/Scheduler.hpp`
- **.serialize()** (3 connections) — `src/core/Scheduler.hpp`
- **.setCallback()** (3 connections) — `src/core/Scheduler.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **Conclusion** (3 connections) — `docs/HATARI_DIVERGENCES.md`
- **3. MFP 68901 (mfp.c)** (3 connections) — `docs/HATARI_MAPPING.md`
- *... and 29 more nodes in this community*

## Relationships

- [Community 173](Community_173.md) (8 shared connections)
- [Community 77](Community_77.md) (7 shared connections)
- [Community 215](Community_215.md) (6 shared connections)
- [Community 130](Community_130.md) (6 shared connections)
- [Community 386](Community_386.md) (3 shared connections)
- [Community 22](Community_22.md) (2 shared connections)
- [Community 152](Community_152.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 64](Community_64.md) (2 shared connections)
- [Community 88](Community_88.md) (2 shared connections)
- [Community 256](Community_256.md) (1 shared connections)
- [Community 102](Community_102.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 85 (63%)
- INFERRED: 49 (37%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*