# Community 152

> 24 nodes

## Key Concepts

- **.runTo()** (19 connections) — `src/core/Scheduler.hpp`
- **.syncTo()** (11 connections) — `src/core/Scheduler.hpp`
- **.scanNextDue()** (10 connections) — `src/core/Scheduler.hpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (10 connections) — `CHANGELOG.md`
- **.schedule()** (6 connections) — `src/core/Scheduler.hpp`
- **🔁 2ᵉ passe d'audit approfondi (2026-06-15, workflow 7 agents)** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **.runMfpTimersTo()** (5 connections) — `src/core/Scheduler.hpp`
- **Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)** (5 connections) — `CHANGELOG.md`
- **3.1 Ordonnanceur — du balayage linéaire au minimum tenu à jour** (5 connections) — `docs/PERFORMANCE.md`
- **.addStolenCycles()** (4 connections) — `src/core/Scheduler.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **.nextDue()** (4 connections) — `src/core/Scheduler.hpp`
- **A42 soldé : l'élection de l'IRQ MFP voit enfin tout le lot de dispatch (2026-09-03)** (4 connections) — `CHANGELOG.md`
- **.armedInvariant()** (3 connections) — `src/core/Scheduler.hpp`
- **.serialize()** (3 connections) — `src/core/Scheduler.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **Conclusion** (3 connections) — `docs/HATARI_DIVERGENCES.md`
- **3. MFP 68901 (mfp.c)** (3 connections) — `docs/HATARI_MAPPING.md`
- **.isMfpTimer()** (2 connections) — `src/core/Scheduler.hpp`
- **endSlice_** (2 connections) — `src/core/Scheduler.hpp`
- **MIDI IN — l'ACIA tire à 31 250 bauds au lieu d'une rafale par trame (2026-08-29)** (2 connections) — `CHANGELOG.md`
- **🟠 Nouveaux — MOYENNES (fidélité)** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **Vérification des correctifs mergés** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **StateArchive** (1 connections)

## Relationships

- [Community 39](Community_39.md) (14 shared connections)
- [Community 215](Community_215.md) (5 shared connections)
- [Community 130](Community_130.md) (5 shared connections)
- [Community 349](Community_349.md) (3 shared connections)
- [Community 110](Community_110.md) (2 shared connections)
- [Community 288](Community_288.md) (2 shared connections)
- [Community 386](Community_386.md) (2 shared connections)
- [Community 64](Community_64.md) (2 shared connections)
- [Community 203](Community_203.md) (2 shared connections)
- [Community 77](Community_77.md) (1 shared connections)
- [Community 67](Community_67.md) (1 shared connections)
- [Community 200](Community_200.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/PERFORMANCE.md`
- `src/core/Scheduler.hpp`

## Audit Trail

- EXTRACTED: 41 (49%)
- INFERRED: 42 (51%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*