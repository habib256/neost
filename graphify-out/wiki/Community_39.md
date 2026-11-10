# Community 39

> 56 nodes

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (19 connections) — `src/core/Scheduler.hpp`
- **.liveNow()** (10 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (10 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (10 connections) — `CHANGELOG.md`
- **.schedule()** (6 connections) — `src/core/Scheduler.hpp`
- **🔁 2ᵉ passe d'audit approfondi (2026-06-15, workflow 7 agents)** (6 connections) — `docs/HATARI_DIVERGENCES.md`
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
- **Conclusion** (3 connections) — `docs/HATARI_DIVERGENCES.md`
- **3. MFP 68901 (mfp.c)** (3 connections) — `docs/HATARI_MAPPING.md`
- **.isMfpTimer()** (2 connections) — `src/core/Scheduler.hpp`
- *... and 31 more nodes in this community*

## Relationships

- [Community 135](Community_135.md) (9 shared connections)
- [Community 77](Community_77.md) (7 shared connections)
- [Community 130](Community_130.md) (5 shared connections)
- [Community 205](Community_205.md) (4 shared connections)
- [Community 386](Community_386.md) (3 shared connections)
- [Community 102](Community_102.md) (2 shared connections)
- [Community 105](Community_105.md) (2 shared connections)
- [Community 215](Community_215.md) (2 shared connections)
- [Community 9](Community_9.md) (2 shared connections)
- [Community 88](Community_88.md) (2 shared connections)
- [Community 392](Community_392.md) (1 shared connections)
- [Community 95](Community_95.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 90 (68%)
- INFERRED: 43 (32%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*