# Community 319

> 21 nodes

## Key Concepts

- **.mmuTranslate()** (23 connections) — `src/core/Bus.hpp`
- **Plan mémoire et MMU** (9 connections) — `docs/MAME_DIVERGENCES.md`
- **ramBanks()** (8 connections) — `src/core/Bus.cpp`
- **.rebuildMmuCache()** (7 connections) — `src/core/Bus.hpp`
- **mmuBank0Size()** (7 connections) — `src/core/Bus.cpp`
- **.write8Slow()** (6 connections) — `src/core/Bus.hpp`
- **7. Bus, mémoire, bus error — `Bus.cpp` ↔ `ioMem*.c`, `cpu/memory.c`, `stMemory.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts voulus — ne pas réaligner** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **.dmaWrite8()** (4 connections) — `src/core/Bus.hpp`
- **mmuConfSize()** (4 connections) — `src/core/Bus.cpp`
- **mmuXlatSTE()** (4 connections) — `src/core/Bus.cpp`
- **mmuXlatSTF()** (4 connections) — `src/core/Bus.cpp`
- **Types de machine & mémoire** (4 connections) — `docs/IMPLEMENTED.md`
- **.ramCycleWrite8()** (3 connections) — `src/core/Bus.hpp`
- **.write8()** (3 connections) — `src/core/Bus.hpp`
- **watchHook** (3 connections) — `src/core/Bus.hpp`
- **Techniques vérifiées** (3 connections) — `DEV.md`
- **Audit doc↔code, compteur vidéo 22 bits, `$FF8261`/res 3, repos GPIP matériel, RAM 2,5 Mo (2026-10-20)** (2 connections) — `CHANGELOG.md`
- **Faux positifs** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Refermés** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts ouverts** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 607](Community_607.md) (9 shared connections)
- [Community 14](Community_14.md) (7 shared connections)
- [Community 288](Community_288.md) (7 shared connections)
- [Community 203](Community_203.md) (7 shared connections)
- [Community 449](Community_449.md) (3 shared connections)
- [Community 429](Community_429.md) (3 shared connections)
- [Community 594](Community_594.md) (2 shared connections)
- [Community 444](Community_444.md) (1 shared connections)
- [Community 205](Community_205.md) (1 shared connections)
- [Community 218](Community_218.md) (1 shared connections)
- [Community 483](Community_483.md) (1 shared connections)
- [Community 39](Community_39.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `docs/MAME_DIVERGENCES.md`
- `src/core/Bus.cpp`
- `src/core/Bus.hpp`

## Audit Trail

- EXTRACTED: 40 (51%)
- INFERRED: 38 (49%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*