# Community 203

> 23 nodes

## Key Concepts

- **2. Plan mémoire figé** (18 connections) — `docs/HYPOTHESES_ST.md`
- **.read8Slow()** (12 connections) — `src/core/Bus.hpp`
- **.peek8()** (12 connections) — `src/core/Bus.hpp`
- **.busFaultN()** (11 connections) — `src/core/Bus.hpp`
- **.busFault()** (10 connections) — `src/core/Bus.hpp`
- **.busFaultNSlow()** (9 connections) — `src/core/Bus.hpp`
- **.romWindowSize()** (9 connections) — `src/core/Bus.hpp`
- **.buildIoFault()** (8 connections) — `src/core/Bus.hpp`
- **.steCartWindow()** (8 connections) — `src/core/Bus.hpp`
- **.steCartRelocated()** (6 connections) — `src/core/Bus.hpp`
- **3. Les optimisations, et ce qu'elles paient** (6 connections) — `docs/PERFORMANCE.md`
- **.peekMapped8()** (5 connections) — `src/core/Bus.hpp`
- **.steRomSelectVoid()** (5 connections) — `src/core/Bus.hpp`
- **Le CPU (Moira, cycle-exact)** (5 connections) — `DEV.md`
- **.busSelfTest()** (4 connections) — `src/core/Bus.hpp`
- **Bug hunt 11ᵉ passe : 24 correctifs sur six sous-systèmes, save-state v51 (2026-10-20)** (4 connections) — `CHANGELOG.md`
- **Bus, bus error & cartouches de diagnostic** (3 connections) — `docs/IMPLEMENTED.md`
- **3.2 Bus — cache du décodage MMU et chemin rapide en ligne** (3 connections) — `docs/PERFORMANCE.md`
- **.peek16()** (2 connections) — `src/core/Bus.hpp`
- **.read8()** (2 connections) — `src/core/Bus.hpp`
- **Chasse aux bugs sur les changements du jour (2026-10-22)** (2 connections) — `CHANGELOG.md`
- **8. Pièges connus** (2 connections) — `docs/OPENDST.md`
- **3.3 `busFaultN` — sortie anticipée pour la RAM ordinaire** (2 connections) — `docs/PERFORMANCE.md`

## Relationships

- [Community 14](Community_14.md) (14 shared connections)
- [Community 319](Community_319.md) (12 shared connections)
- [Community 607](Community_607.md) (9 shared connections)
- [Community 449](Community_449.md) (4 shared connections)
- [Community 138](Community_138.md) (3 shared connections)
- [Community 480](Community_480.md) (2 shared connections)
- [Community 36](Community_36.md) (2 shared connections)
- [Community 292](Community_292.md) (2 shared connections)
- [Community 484](Community_484.md) (2 shared connections)
- [Community 130](Community_130.md) (2 shared connections)
- [Community 596](Community_596.md) (2 shared connections)
- [Community 27](Community_27.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HYPOTHESES_ST.md`
- `docs/IMPLEMENTED.md`
- `docs/OPENDST.md`
- `docs/PERFORMANCE.md`
- `src/core/Bus.hpp`

## Audit Trail

- EXTRACTED: 38 (35%)
- INFERRED: 71 (65%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*