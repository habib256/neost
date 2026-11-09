# Community 160

> 20 nodes

## Key Concepts

- **Divergences OUVERTES à jour (par sous-système, sévérité décroissante)** (18 connections) — `docs/HATARI_DIVERGENCES.md`
- **.read8()** (11 connections) — `src/io/Mfp.hpp`
- **.updateTimers()** (10 connections) — `src/io/Mfp.hpp`
- **.runMfpTimersTo()** (9 connections) — `src/core/Scheduler.hpp`
- **.writeEventTime()** (7 connections) — `src/io/Mfp.hpp`
- **.flushIrqUpdate()** (6 connections) — `src/io/Mfp.hpp`
- **L'élection de l'exception MFP : la règle chronologique existait mais ne pouvait jamais s'appliquer (2026-09-02)** (6 connections) — `CHANGELOG.md`
- **🔁 2ᵉ passe d'audit approfondi (2026-06-15, workflow 7 agents)** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **Conclusion** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **🟠 Nouveaux — MOYENNES (fidélité)** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **3. MFP 68901 (mfp.c)** (6 connections) — `docs/HATARI_MAPPING.md`
- **.setXsintLine()** (4 connections) — `src/io/Mfp.hpp`
- **.ctz32()** (4 connections) — `src/core/Scheduler.hpp`
- **Les trois autres pistes « Super Hang-On » d'Hatari, instruites (2026-09-02)** (4 connections) — `CHANGELOG.md`
- **.mmioWriteEnd()** (3 connections) — `src/io/Mfp.hpp`
- **Super Hang-On : le vrai coupable était `MFP_UpdateTimers` — que j'avais porté puis retiré le matin même (2026-09-02)** (3 connections) — `CHANGELOG.md`
- **.setInDispatch()** (2 connections) — `src/io/Mfp.hpp`
- **gpipHook_** (2 connections) — `src/io/Mfp.hpp`
- **Le poll de timer MFP passe à 0 px de l'oracle — et la divergence qu'on croyait tenir n'existait plus (2026-09-02)** (2 connections) — `CHANGELOG.md`
- **Vérification des correctifs mergés** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 215](Community_215.md) (16 shared connections)
- [Community 39](Community_39.md) (10 shared connections)
- [Community 6](Community_6.md) (9 shared connections)
- [Community 130](Community_130.md) (4 shared connections)
- [Community 27](Community_27.md) (4 shared connections)
- [Community 205](Community_205.md) (3 shared connections)
- [Community 37](Community_37.md) (2 shared connections)
- [Community 349](Community_349.md) (2 shared connections)
- [Community 105](Community_105.md) (2 shared connections)
- [Community 121](Community_121.md) (2 shared connections)
- [Community 28](Community_28.md) (2 shared connections)
- [Community 182](Community_182.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `src/core/Scheduler.hpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 32 (36%)
- INFERRED: 58 (64%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*