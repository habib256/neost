# Community 203

> 46 nodes

## Key Concepts

- **.mmuTranslate()** (20 connections) — `src/core/Bus.hpp`
- **.read8Slow()** (11 connections) — `src/core/Bus.hpp`
- **.busFault()** (11 connections) — `src/core/Bus.hpp`
- **.peek8()** (11 connections) — `src/core/Bus.hpp`
- **.busFaultN()** (10 connections) — `src/core/Bus.hpp`
- **memConfigForBytes()** (9 connections) — `src/core/MachineType.hpp`
- **Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`** (8 connections) — `docs/HATARI_DIVERGENCES.md`
- **.rebuildMmuCache()** (7 connections) — `src/core/Bus.hpp`
- **.busFaultNSlow()** (7 connections) — `src/core/Bus.hpp`
- **.romWindowSize()** (7 connections) — `src/core/Bus.hpp`
- **.steCartWindow()** (7 connections) — `src/core/Bus.hpp`
- **ramBanks()** (7 connections) — `src/core/Bus.cpp`
- **Performance du cœur NeoST — profil, optimisations, recette de build** (7 connections) — `docs/PERFORMANCE.md`
- **mmuBank0Size()** (6 connections) — `src/core/Bus.cpp`
- **3. Les optimisations, et ce qu'elles paient** (6 connections) — `docs/PERFORMANCE.md`
- **.megaSteCacheable()** (5 connections) — `src/core/Bus.hpp`
- **.steCartRelocated()** (5 connections) — `src/core/Bus.hpp`
- **.steRomSelectVoid()** (5 connections) — `src/core/Bus.hpp`
- **.busSelfTest()** (4 connections) — `src/core/Bus.hpp`
- **mmuConfSize()** (4 connections) — `src/core/Bus.cpp`
- **Le CPU (Moira, cycle-exact)** (4 connections) — `DEV.md`
- **Types de machine & mémoire** (4 connections) — `docs/IMPLEMENTED.md`
- **.megaSteCacheRead()** (3 connections) — `src/core/Bus.hpp`
- **.megaSteCacheUpdate()** (3 connections) — `src/core/Bus.hpp`
- **.dmaRead8()** (3 connections) — `src/core/Bus.hpp`
- *... and 21 more nodes in this community*

## Relationships

- [Community 14](Community_14.md) (43 shared connections)
- [Community 394](Community_394.md) (6 shared connections)
- [Community 39](Community_39.md) (4 shared connections)
- [Community 24](Community_24.md) (3 shared connections)
- [Community 9](Community_9.md) (3 shared connections)
- [Community 182](Community_182.md) (2 shared connections)
- [Community 392](Community_392.md) (2 shared connections)
- [Community 130](Community_130.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 291](Community_291.md) (2 shared connections)
- [Community 218](Community_218.md) (1 shared connections)
- [Community 415](Community_415.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/OPENDST.md`
- `docs/PERFORMANCE.md`
- `src/core/Bus.cpp`
- `src/core/Bus.hpp`
- `src/core/MachineType.hpp`

## Audit Trail

- EXTRACTED: 80 (53%)
- INFERRED: 70 (47%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*