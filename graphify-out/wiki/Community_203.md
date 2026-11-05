# Community 203

> 22 nodes

## Key Concepts

- **.read8Slow()** (11 connections) — `src/core/Bus.hpp`
- **.busFault()** (11 connections) — `src/core/Bus.hpp`
- **.peek8()** (11 connections) — `src/core/Bus.hpp`
- **.busFaultN()** (10 connections) — `src/core/Bus.hpp`
- **Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`** (8 connections) — `docs/HATARI_DIVERGENCES.md`
- **.busFaultNSlow()** (7 connections) — `src/core/Bus.hpp`
- **.romWindowSize()** (7 connections) — `src/core/Bus.hpp`
- **.steCartWindow()** (7 connections) — `src/core/Bus.hpp`
- **3. Les optimisations, et ce qu'elles paient** (6 connections) — `docs/PERFORMANCE.md`
- **.steCartRelocated()** (5 connections) — `src/core/Bus.hpp`
- **.steRomSelectVoid()** (5 connections) — `src/core/Bus.hpp`
- **.busSelfTest()** (4 connections) — `src/core/Bus.hpp`
- **Le CPU (Moira, cycle-exact)** (4 connections) — `DEV.md`
- **.dmaRead8()** (3 connections) — `src/core/Bus.hpp`
- **.dmaWrite8()** (3 connections) — `src/core/Bus.hpp`
- **3.2 Bus — cache du décodage MMU et chemin rapide en ligne** (3 connections) — `docs/PERFORMANCE.md`
- **.peek16()** (2 connections) — `src/core/Bus.hpp`
- **.read8()** (2 connections) — `src/core/Bus.hpp`
- **Chasse aux bugs sur les changements du jour (2026-10-22)** (2 connections) — `CHANGELOG.md`
- **9ᵉ passe — workflow zones peu couvertes (2026-08-06)** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **8. Pièges connus** (2 connections) — `docs/OPENDST.md`
- **3.3 `busFaultN` — sortie anticipée pour la RAM ordinaire** (2 connections) — `docs/PERFORMANCE.md`

## Relationships

- [Community 14](Community_14.md) (14 shared connections)
- [Community 30](Community_30.md) (9 shared connections)
- [Community 386](Community_386.md) (6 shared connections)
- [Community 202](Community_202.md) (2 shared connections)
- [Community 152](Community_152.md) (2 shared connections)
- [Community 67](Community_67.md) (2 shared connections)
- [Community 64](Community_64.md) (2 shared connections)
- [Community 105](Community_105.md) (2 shared connections)
- [Community 281](Community_281.md) (1 shared connections)
- [Community 27](Community_27.md) (1 shared connections)
- [Community 63](Community_63.md) (1 shared connections)
- [Community 173](Community_173.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `DEV.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/OPENDST.md`
- `docs/PERFORMANCE.md`
- `src/core/Bus.hpp`

## Audit Trail

- EXTRACTED: 37 (45%)
- INFERRED: 46 (55%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*