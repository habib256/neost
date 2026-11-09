# Community 386

> 16 nodes

## Key Concepts

- **.mmuTranslate()** (17 connections) — `src/core/Bus.hpp`
- **.rebuildMmuCache()** (9 connections) — `src/core/Bus.hpp`
- **memConfigForBytes()** (9 connections) — `src/core/MachineType.hpp`
- **Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`** (9 connections) — `docs/HATARI_DIVERGENCES.md`
- **ramBanks()** (7 connections) — `src/core/Bus.cpp`
- **mmuBank0Size()** (6 connections) — `src/core/Bus.cpp`
- **Types de machine & mémoire** (5 connections) — `docs/IMPLEMENTED.md`
- **mmuConfSize()** (4 connections) — `src/core/Bus.cpp`
- **1. Bus & mémoire (ioMem.c, cpu/memory.c, stMemory.c)** (4 connections) — `docs/HATARI_MAPPING.md`
- **.wrStePads()** (3 connections) — `src/core/Bus.hpp`
- **.mmuConfNow()** (3 connections) — `src/core/Bus.hpp`
- **mmuFastLimit_** (3 connections) — `src/core/Bus.hpp`
- **3.2 Bus — cache du décodage MMU et chemin rapide en ligne** (3 connections) — `docs/PERFORMANCE.md`
- **mmuXlatSTE()** (2 connections) — `src/core/Bus.cpp`
- **mmuXlatSTF()** (2 connections) — `src/core/Bus.cpp`
- **Audit doc↔code, compteur vidéo 22 bits, `$FF8261`/res 3, repos GPIP matériel, RAM 2,5 Mo (2026-10-20)** (2 connections) — `CHANGELOG.md`

## Relationships

- [Community 30](Community_30.md) (8 shared connections)
- [Community 14](Community_14.md) (5 shared connections)
- [Community 203](Community_203.md) (3 shared connections)
- [Community 202](Community_202.md) (2 shared connections)
- [Community 39](Community_39.md) (2 shared connections)
- [Community 192](Community_192.md) (2 shared connections)
- [Community 136](Community_136.md) (2 shared connections)
- [Community 218](Community_218.md) (1 shared connections)
- [Community 288](Community_288.md) (1 shared connections)
- [Community 175](Community_175.md) (1 shared connections)
- [Community 41](Community_41.md) (1 shared connections)
- [Community 182](Community_182.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/PERFORMANCE.md`
- `src/core/Bus.cpp`
- `src/core/Bus.hpp`
- `src/core/MachineType.hpp`

## Audit Trail

- EXTRACTED: 33 (52%)
- INFERRED: 31 (48%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*