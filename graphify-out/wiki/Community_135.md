# Community 135

> 11 nodes

## Key Concepts

- **Divergences OUVERTES à jour (par sous-système, sévérité décroissante)** (18 connections) — `docs/HATARI_DIVERGENCES.md`
- **.scheduleTimerAt()** (12 connections) — `src/io/Mfp.hpp`
- **.timerPeriodSubCycles()** (11 connections) — `src/io/Mfp.hpp`
- **.syncTo()** (11 connections) — `src/core/Scheduler.hpp`
- **.readTimerData()** (9 connections) — `src/io/Mfp.hpp`
- **.storeStoppedCounter()** (8 connections) — `src/io/Mfp.hpp`
- **Le poll de timer MFP passe à 0 px de l'oracle — et la divergence qu'on croyait tenir n'existait plus (2026-09-02)** (6 connections) — `CHANGELOG.md`
- **delayCtrl()** (5 connections) — `src/io/Mfp.cpp`
- **.timerPeriodCycles()** (3 connections) — `src/io/Mfp.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **P1 — Timing systémique (dérives d'horloge, phase CPU↔faisceau)** (3 connections) — `docs/HATARI_MAPPING.md`

## Relationships

- [Community 215](Community_215.md) (19 shared connections)
- [Community 39](Community_39.md) (9 shared connections)
- [Community 6](Community_6.md) (5 shared connections)
- [Community 392](Community_392.md) (3 shared connections)
- [Community 95](Community_95.md) (3 shared connections)
- [Community 48](Community_48.md) (3 shared connections)
- [Community 130](Community_130.md) (2 shared connections)
- [Community 27](Community_27.md) (2 shared connections)
- [Community 67](Community_67.md) (1 shared connections)
- [Community 138](Community_138.md) (1 shared connections)
- [Community 10](Community_10.md) (1 shared connections)
- [Community 140](Community_140.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `src/core/Scheduler.hpp`
- `src/io/Mfp.cpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 22 (31%)
- INFERRED: 49 (69%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*