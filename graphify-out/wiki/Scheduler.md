# Scheduler

> 54 nodes · cohesion 0.06

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **.runTo()** (20 connections) — `src/core/Scheduler.hpp`
- **.syncTo()** (11 connections) — `src/core/Scheduler.hpp`
- **.liveNow()** (10 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (10 connections) — `src/core/Scheduler.hpp`
- **.schedule()** (6 connections) — `src/core/Scheduler.hpp`
- **Chantiers structurels (UN à la fois, jamais combinés)** (6 connections) — `TODO.md`
- **🔬 Divergences Hatari & précision cycle — restes** (6 connections) — `TODO.md`
- **.addStolenCycles()** (5 connections) — `src/core/Scheduler.hpp`
- **Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)** (5 connections) — `CHANGELOG.md`
- **3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour** (5 connections) — `docs/PERFORMANCE.md`
- **.cancel()** (4 connections) — `src/core/Scheduler.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **.nextDue()** (4 connections) — `src/core/Scheduler.hpp`
- **function** (4 connections)
- **A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)** (4 connections) — `CHANGELOG.md`
- **.armedInvariant()** (3 connections) — `src/core/Scheduler.hpp`
- **.cyclesUntil()** (3 connections) — `src/core/Scheduler.hpp`
- **.rawCyclesUntil()** (3 connections) — `src/core/Scheduler.hpp`
- **.runMfpTimersTo()** (3 connections) — `src/core/Scheduler.hpp`
- **.serialize()** (3 connections) — `src/core/Scheduler.hpp`
- **.setCallback()** (3 connections) — `src/core/Scheduler.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **Conclusion** (3 connections) — `docs/HATARI_DIVERGENCES.md`
- **.isMfpTimer()** (2 connections) — `src/core/Scheduler.hpp`
- *... and 29 more nodes in this community*

## Relationships

- [Mfp.cpp](Mfp.cpp.md) (13 shared connections)
- [Source](Source.md) (7 shared connections)
- [Community 130](Community_130.md) (5 shared connections)
- [NeostMoira](NeostMoira.md) (3 shared connections)
- [Community 100](Community_100.md) (2 shared connections)
- [Community 211](Community_211.md) (2 shared connections)
- [Community 192](Community_192.md) (2 shared connections)
- [Community 148](Community_148.md) (2 shared connections)
- [Community 237](Community_237.md) (2 shared connections)
- [.gpipSetLine](gpipSetLine.md) (1 shared connections)
- [Community 245](Community_245.md) (1 shared connections)
- [Community 317](Community_317.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `TODO.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 85 (64%)
- INFERRED: 47 (36%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*