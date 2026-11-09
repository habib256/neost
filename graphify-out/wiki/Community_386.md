# Community 386

> 14 nodes

## Key Concepts

- **.mmuTranslate()** (17 connections) — `src/core/Bus.hpp`
- **.rebuildMmuCache()** (9 connections) — `src/core/Bus.hpp`
- **Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`** (9 connections) — `docs/HATARI_DIVERGENCES.md`
- **ramBanks()** (7 connections) — `src/core/Bus.cpp`
- **mmuBank0Size()** (6 connections) — `src/core/Bus.cpp`
- **Types de machine & mémoire** (5 connections) — `docs/IMPLEMENTED.md`
- **mmuConfSize()** (4 connections) — `src/core/Bus.cpp`
- **.wrStePads()** (3 connections) — `src/core/Bus.hpp`
- **.mmuConfNow()** (3 connections) — `src/core/Bus.hpp`
- **mmuFastLimit_** (3 connections) — `src/core/Bus.hpp`
- **3.2 Bus — cache du décodage MMU et chemin rapide en ligne** (3 connections) — `docs/PERFORMANCE.md`
- **mmuXlatSTE()** (2 connections) — `src/core/Bus.cpp`
- **mmuXlatSTF()** (2 connections) — `src/core/Bus.cpp`
- **Audit doc↔code, compteur vidéo 22 bits, `$FF8261`/res 3, repos GPIP matériel, RAM 2,5 Mo (2026-10-20)** (2 connections) — `CHANGELOG.md`

## Relationships

- [Community 30](Community_30.md) (8 shared connections)
- [Community 200](Community_200.md) (6 shared connections)
- [Community 14](Community_14.md) (5 shared connections)
- [Community 203](Community_203.md) (3 shared connections)
- [Community 258](Community_258.md) (2 shared connections)
- [Community 182](Community_182.md) (2 shared connections)
- [Community 202](Community_202.md) (2 shared connections)
- [Community 39](Community_39.md) (2 shared connections)
- [Community 321](Community_321.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 356](Community_356.md) (1 shared connections)
- [Community 37](Community_37.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `src/core/Bus.cpp`
- `src/core/Bus.hpp`

## Audit Trail

- EXTRACTED: 30 (54%)
- INFERRED: 26 (46%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*