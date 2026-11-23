# Community 221

> 22 nodes

## Key Concepts

- **.writeTimerCtrl()** (14 connections) — `src/io/Mfp.hpp`
- **.syncTo()** (14 connections) — `src/core/Scheduler.hpp`
- **.scheduleTimerAt()** (12 connections) — `src/io/Mfp.hpp`
- **.timerCtrl()** (11 connections) — `src/io/Mfp.hpp`
- **.readTimerData()** (10 connections) — `src/io/Mfp.hpp`
- **.timerPulseMode()** (9 connections) — `src/io/Mfp.hpp`
- **.updatePulseGate()** (9 connections) — `src/io/Mfp.hpp`
- **.timerPeriodSubCycles()** (9 connections) — `src/io/Mfp.hpp`
- **.storeStoppedCounter()** (9 connections) — `src/io/Mfp.hpp`
- **.pulseGateActive()** (6 connections) — `src/io/Mfp.hpp`
- **.updateTbiTracking()** (6 connections) — `src/io/Mfp.hpp`
- **.onTimerExpire()** (5 connections) — `src/io/Mfp.hpp`
- **delayCtrl()** (5 connections) — `src/io/Mfp.cpp`
- **Le poll de timer MFP passe à 0 px de l'oracle — et la divergence qu'on croyait tenir n'existait plus (2026-09-02)** (5 connections) — `CHANGELOG.md`
- **.scheduleTimer()** (4 connections) — `src/io/Mfp.hpp`
- **.liveTimerCount()** (4 connections) — `src/io/Mfp.hpp`
- **.firstEdgeSubAtOrAfter()** (3 connections) — `src/io/Mfp.hpp`
- **.timerPeriodCycles()** (3 connections) — `src/io/Mfp.hpp`
- **isPulseCtrl()** (3 connections) — `src/io/Mfp.cpp`
- **tbiPulseSink_** (3 connections) — `src/io/Mfp.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **P1 — Timing systémique (dérives d'horloge, phase CPU↔faisceau)** (3 connections) — `docs/archive/HATARI_MAPPING_AUDIT_2026-07.md`

## Relationships

- [Community 215](Community_215.md) (25 shared connections)
- [Community 6](Community_6.md) (16 shared connections)
- [Community 417](Community_417.md) (8 shared connections)
- [Community 392](Community_392.md) (4 shared connections)
- [Community 258](Community_258.md) (4 shared connections)
- [Community 39](Community_39.md) (2 shared connections)
- [Community 130](Community_130.md) (2 shared connections)
- [Community 160](Community_160.md) (1 shared connections)
- [Community 138](Community_138.md) (1 shared connections)
- [Community 218](Community_218.md) (1 shared connections)
- [Community 468](Community_468.md) (1 shared connections)
- [Community 466](Community_466.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/archive/HATARI_MAPPING_AUDIT_2026-07.md`
- `src/core/Scheduler.hpp`
- `src/io/Mfp.cpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 46 (42%)
- INFERRED: 63 (58%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*