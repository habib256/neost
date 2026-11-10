# Community 39

> 66 nodes

## Key Concepts

- **Scheduler** (47 connections) — `src/core/Scheduler.hpp`
- **Priorités — divergences HAUTE et MOYENNE** (20 connections) — `docs/HATARI_DIVERGENCES.md`
- **.runTo()** (19 connections) — `src/core/Scheduler.hpp`
- **Divergences OUVERTES à jour (par sous-système, sévérité décroissante)** (18 connections) — `docs/HATARI_DIVERGENCES.md`
- **.timerPeriodSubCycles()** (11 connections) — `src/io/Mfp.hpp`
- **.syncTo()** (11 connections) — `src/core/Scheduler.hpp`
- **.liveNow()** (10 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (10 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (10 connections) — `CHANGELOG.md`
- **.readTimerData()** (9 connections) — `src/io/Mfp.hpp`
- **.schedule()** (6 connections) — `src/core/Scheduler.hpp`
- **Le poll de timer MFP passe à 0 px de l'oracle — et la divergence qu'on croyait tenir n'existait plus (2026-09-02)** (6 connections) — `CHANGELOG.md`
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
- **.timerPeriodCycles()** (3 connections) — `src/io/Mfp.hpp`
- **.armedInvariant()** (3 connections) — `src/core/Scheduler.hpp`
- *... and 41 more nodes in this community*

## Relationships

- [Community 215](Community_215.md) (16 shared connections)
- [Community 77](Community_77.md) (7 shared connections)
- [Community 130](Community_130.md) (7 shared connections)
- [Community 392](Community_392.md) (6 shared connections)
- [Community 110](Community_110.md) (5 shared connections)
- [Community 203](Community_203.md) (4 shared connections)
- [Community 55](Community_55.md) (4 shared connections)
- [Community 6](Community_6.md) (3 shared connections)
- [Community 176](Community_176.md) (3 shared connections)
- [Community 102](Community_102.md) (2 shared connections)
- [Community 205](Community_205.md) (2 shared connections)
- [Community 294](Community_294.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `docs/archive/HATARI_MAPPING_AUDIT_2026-07.md`
- `src/core/Scheduler.hpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 104 (53%)
- INFERRED: 93 (47%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*