# Community 480

> 18 nodes

## Key Concepts

- **Divergences NeoST ↔ MAME — plan mémoire, bus errors, DMA, SCC** (8 connections) — `docs/MAME_DIVERGENCES.md`
- **A31 — le dispatch MMIO devient une table, et sa cohérence est PROUVÉE (2026-08-28)** (7 connections) — `CHANGELOG.md`
- **.ramSpaceRead8()** (6 connections) — `src/core/Bus.hpp`
- **.videoRead16At()** (5 connections) — `src/core/Bus.hpp`
- **FDC WD1772 et DMA** (5 connections) — `docs/MAME_DIVERGENCES.md`
- **.wrMfp()** (4 connections) — `src/core/Bus.hpp`
- **.rdStePads()** (4 connections) — `src/core/Bus.hpp`
- **.dmaRead8()** (4 connections) — `src/core/Bus.hpp`
- **.ramCycleRead16Slow()** (4 connections) — `src/core/Bus.hpp`
- **.rdMfp()** (3 connections) — `src/core/Bus.hpp`
- **.wrStePads()** (3 connections) — `src/core/Bus.hpp`
- **.ramCycleRead16()** (3 connections) — `src/core/Bus.hpp`
- **.ramCycleRead8()** (3 connections) — `src/core/Bus.hpp`
- **Bus errors et décodage IO** (2 connections) — `docs/MAME_DIVERGENCES.md`
- **MFP 68901, RTC RP5C15, YM2149 — survol** (2 connections) — `docs/MAME_DIVERGENCES.md`
- **Blitter** (1 connections) — `docs/MAME_DIVERGENCES.md`
- **SCC Z85C30 (Mega STE)** (1 connections) — `docs/MAME_DIVERGENCES.md`
- **Écarts qu'une source de rang supérieur tranche** (1 connections) — `docs/MAME_DIVERGENCES.md`

## Relationships

- [Community 14](Community_14.md) (10 shared connections)
- [Community 607](Community_607.md) (8 shared connections)
- [Community 319](Community_319.md) (3 shared connections)
- [Community 203](Community_203.md) (2 shared connections)
- [Community 24](Community_24.md) (2 shared connections)
- [Community 51](Community_51.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 27](Community_27.md) (1 shared connections)
- [Community 232](Community_232.md) (1 shared connections)
- [Community 95](Community_95.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/MAME_DIVERGENCES.md`
- `src/core/Bus.hpp`

## Audit Trail

- EXTRACTED: 29 (60%)
- INFERRED: 19 (40%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*