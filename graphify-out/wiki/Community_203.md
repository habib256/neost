# Community 203

> 28 nodes

## Key Concepts

- **2. Plan mémoire figé** (18 connections) — `docs/HYPOTHESES_ST.md`
- **.read8Slow()** (12 connections) — `src/core/Bus.hpp`
- **.peek8()** (12 connections) — `src/core/Bus.hpp`
- **.busFaultN()** (11 connections) — `src/core/Bus.hpp`
- **.busFault()** (10 connections) — `src/core/Bus.hpp`
- **.romWindowSize()** (9 connections) — `src/core/Bus.hpp`
- **Plan mémoire et MMU** (9 connections) — `docs/MAME_DIVERGENCES.md`
- **.loadTos()** (8 connections) — `src/core/Bus.hpp`
- **.steCartWindow()** (8 connections) — `src/core/Bus.hpp`
- **tosUnsupportedMachine()** (8 connections) — `src/core/MachineType.hpp`
- **size_t** (8 connections)
- **.busFaultNSlow()** (7 connections) — `src/core/Bus.hpp`
- **ramLabel()** (7 connections) — `src/core/MachineType.hpp`
- **.steCartRelocated()** (6 connections) — `src/core/Bus.hpp`
- **.peekMapped8()** (5 connections) — `src/core/Bus.hpp`
- **.steRomSelectVoid()** (5 connections) — `src/core/Bus.hpp`
- **.busSelfTest()** (4 connections) — `src/core/Bus.hpp`
- **.seedResetVectors()** (4 connections) — `src/core/Bus.hpp`
- **mmuXlatSTE()** (4 connections) — `src/core/Bus.cpp`
- **mmuXlatSTF()** (4 connections) — `src/core/Bus.cpp`
- **tosImageBase()** (4 connections) — `src/core/MachineType.hpp`
- **Bug hunt 11ᵉ passe : 24 correctifs sur six sous-systèmes, save-state v51 (2026-10-20)** (4 connections) — `CHANGELOG.md`
- **.loadCart()** (3 connections) — `src/core/Bus.hpp`
- **.peek16()** (2 connections) — `src/core/Bus.hpp`
- **.read8()** (2 connections) — `src/core/Bus.hpp`
- *... and 3 more nodes in this community*

## Relationships

- [Community 14](Community_14.md) (16 shared connections)
- [Community 58](Community_58.md) (12 shared connections)
- [Community 186](Community_186.md) (11 shared connections)
- [Community 325](Community_325.md) (8 shared connections)
- [Community 4](Community_4.md) (3 shared connections)
- [Community 301](Community_301.md) (2 shared connections)
- [Community 441](Community_441.md) (2 shared connections)
- [Community 218](Community_218.md) (2 shared connections)
- [Community 350](Community_350.md) (2 shared connections)
- [Community 449](Community_449.md) (2 shared connections)
- [Community 392](Community_392.md) (2 shared connections)
- [Community 247](Community_247.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HYPOTHESES_ST.md`
- `docs/MAME_DIVERGENCES.md`
- `docs/OPENDST.md`
- `src/core/Bus.cpp`
- `src/core/Bus.hpp`
- `src/core/MachineType.hpp`

## Audit Trail

- EXTRACTED: 52 (40%)
- INFERRED: 79 (60%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*