# Community 186

> 22 nodes

## Key Concepts

- **.mmuTranslate()** (23 connections) — `src/core/Bus.hpp`
- **memConfigForBytes()** (10 connections) — `src/core/MachineType.hpp`
- **ramBanks()** (8 connections) — `src/core/Bus.cpp`
- **.rebuildMmuCache()** (7 connections) — `src/core/Bus.hpp`
- **mmuBank0Size()** (7 connections) — `src/core/Bus.cpp`
- **.write8Slow()** (6 connections) — `src/core/Bus.hpp`
- **7. Bus, mémoire, bus error — `Bus.cpp` ↔ `ioMem*.c`, `cpu/memory.c`, `stMemory.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts voulus — ne pas réaligner** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **.dmaWrite8()** (4 connections) — `src/core/Bus.hpp`
- **mmuFastLimit_** (4 connections) — `src/core/Bus.hpp`
- **mmuConfSize()** (4 connections) — `src/core/Bus.cpp`
- **Types de machine & mémoire** (4 connections) — `docs/IMPLEMENTED.md`
- **.mmuConfNow()** (3 connections) — `src/core/Bus.hpp`
- **.ramCycleWrite8()** (3 connections) — `src/core/Bus.hpp`
- **.write8()** (3 connections) — `src/core/Bus.hpp`
- **watchHook** (3 connections) — `src/core/Bus.hpp`
- **Techniques vérifiées** (3 connections) — `DEV.md`
- **1. Bus & mémoire (ioMem.c, cpu/memory.c, stMemory.c)** (3 connections) — `docs/HATARI_MAPPING.md`
- **Audit doc↔code, compteur vidéo 22 bits, `$FF8261`/res 3, repos GPIP matériel, RAM 2,5 Mo (2026-10-20)** (2 connections) — `CHANGELOG.md`
- **Faux positifs** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Refermés** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Écarts ouverts** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 203](Community_203.md) (11 shared connections)
- [Community 14](Community_14.md) (9 shared connections)
- [Community 58](Community_58.md) (7 shared connections)
- [Community 218](Community_218.md) (2 shared connections)
- [Community 454](Community_454.md) (2 shared connections)
- [Community 350](Community_350.md) (2 shared connections)
- [Community 301](Community_301.md) (2 shared connections)
- [Community 394](Community_394.md) (2 shared connections)
- [Community 288](Community_288.md) (1 shared connections)
- [Community 392](Community_392.md) (1 shared connections)
- [Community 39](Community_39.md) (1 shared connections)
- [Community 4](Community_4.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `src/core/Bus.cpp`
- `src/core/Bus.hpp`
- `src/core/MachineType.hpp`

## Audit Trail

- EXTRACTED: 43 (54%)
- INFERRED: 37 (46%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*