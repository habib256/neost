# Community 58

> 14 nodes

## Key Concepts

- **.syncTo()** (14 connections) — `src/core/Scheduler.hpp`
- **12. CPU 68000 et intégration — `Cpu68k.cpp`, `extern/moira` ↔ `cpu/newcpu.c`, `gencpu.c`, `hatari-glue.c`** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **.addStolenCycles()** (4 connections) — `src/core/Scheduler.hpp`
- **A34 — un seul modèle d'exécution, et les 83 verrous du cœur sont enfin classés (2026-08-28)** (4 connections) — `CHANGELOG.md`
- **Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)** (4 connections) — `CHANGELOG.md`
- **.armedInvariant()** (3 connections) — `src/core/Scheduler.hpp`
- **.syncToRollingFrames()** (3 connections) — `src/core/Scheduler.hpp`
- **A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)** (3 connections) — `CHANGELOG.md`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **rollCb_** (2 connections) — `src/core/Scheduler.hpp`
- **Écarts voulus — ne pas réaligner** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **Faux positifs** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Verrous du cœur : verdicts** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts ouverts** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 39](Community_39.md) (7 shared connections)
- [Community 130](Community_130.md) (4 shared connections)
- [Community 221](Community_221.md) (2 shared connections)
- [Community 449](Community_449.md) (1 shared connections)
- [Community 392](Community_392.md) (1 shared connections)
- [Community 218](Community_218.md) (1 shared connections)
- [Community 468](Community_468.md) (1 shared connections)
- [Community 434](Community_434.md) (1 shared connections)
- [Community 67](Community_67.md) (1 shared connections)
- [Community 160](Community_160.md) (1 shared connections)
- [Community 215](Community_215.md) (1 shared connections)
- [Community 9](Community_9.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 20 (54%)
- INFERRED: 17 (46%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*