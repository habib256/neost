# Community 203

> 20 nodes

## Key Concepts

- **.read8Slow()** (11 connections) — `src/core/Bus.hpp`
- **.busFault()** (11 connections) — `src/core/Bus.hpp`
- **.peek8()** (11 connections) — `src/core/Bus.hpp`
- **Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`** (8 connections) — `docs/HATARI_DIVERGENCES.md`
- **.busFaultNSlow()** (7 connections) — `src/core/Bus.hpp`
- **.romWindowSize()** (7 connections) — `src/core/Bus.hpp`
- **.steCartWindow()** (7 connections) — `src/core/Bus.hpp`
- **.megaSteCacheable()** (5 connections) — `src/core/Bus.hpp`
- **.steCartRelocated()** (5 connections) — `src/core/Bus.hpp`
- **.steRomSelectVoid()** (5 connections) — `src/core/Bus.hpp`
- **.busSelfTest()** (4 connections) — `src/core/Bus.hpp`
- **.megaSteCacheRead()** (3 connections) — `src/core/Bus.hpp`
- **.megaSteCacheUpdate()** (3 connections) — `src/core/Bus.hpp`
- **.dmaRead8()** (3 connections) — `src/core/Bus.hpp`
- **.dmaWrite8()** (3 connections) — `src/core/Bus.hpp`
- **.peek16()** (2 connections) — `src/core/Bus.hpp`
- **.read8()** (2 connections) — `src/core/Bus.hpp`
- **Chasse aux bugs sur les changements du jour (2026-10-22)** (2 connections) — `CHANGELOG.md`
- **9ᵉ passe — workflow zones peu couvertes (2026-08-06)** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **8. Pièges connus** (2 connections) — `docs/OPENDST.md`

## Relationships

- [Community 14](Community_14.md) (16 shared connections)
- [Community 30](Community_30.md) (11 shared connections)
- [Community 386](Community_386.md) (5 shared connections)
- [Community 321](Community_321.md) (3 shared connections)
- [Community 202](Community_202.md) (2 shared connections)
- [Community 105](Community_105.md) (2 shared connections)
- [Community 281](Community_281.md) (1 shared connections)
- [Community 27](Community_27.md) (1 shared connections)
- [Community 121](Community_121.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 138](Community_138.md) (1 shared connections)
- [Community 291](Community_291.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/OPENDST.md`
- `src/core/Bus.hpp`

## Audit Trail

- EXTRACTED: 35 (47%)
- INFERRED: 39 (53%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*