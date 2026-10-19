# NeostMoira

> 45 nodes · cohesion 0.13

## Key Concepts

- **NeostMoira** (40 connections) — `src/core/Cpu68k.cpp`
- **.getClock()** (18 connections) — `extern/moira/Moira/Moira.h`
- **u32** (15 connections)
- **.sync()** (11 connections) — `src/core/Cpu68k.cpp`
- **u16** (11 connections)
- **.busDiag()** (10 connections) — `src/core/Cpu68k.cpp`
- **.faultOrHalt()** (10 connections) — `src/core/Cpu68k.cpp`
- **.read16()** (10 connections) — `src/core/Cpu68k.cpp`
- **.readMste16Mhz()** (10 connections) — `src/core/Cpu68k.cpp`
- **Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)** (10 connections) — `CHANGELOG.md`
- **.getPC0()** (9 connections) — `extern/moira/Moira/Moira.h`
- **.raiseBusError()** (9 connections) — `src/core/Cpu68k.cpp`
- **.read8()** (9 connections) — `src/core/Cpu68k.cpp`
- **.writeMste16Mhz()** (9 connections) — `src/core/Cpu68k.cpp`
- **.udsDone()** (8 connections) — `src/core/Cpu68k.cpp`
- **u8** (8 connections)
- **.didExecute()** (7 connections) — `src/core/Cpu68k.cpp`
- **.willAccessBus()** (7 connections) — `src/core/Cpu68k.cpp`
- **.willExecute()** (7 connections) — `src/core/Cpu68k.cpp`
- **.write16()** (7 connections) — `src/core/Cpu68k.cpp`
- **.write8()** (7 connections) — `src/core/Cpu68k.cpp`
- **.readIrqUserVector()** (6 connections) — `src/core/Cpu68k.cpp`
- **.getIRD()** (5 connections) — `extern/moira/Moira/Moira.h`
- **.alignRamBus()** (5 connections) — `src/core/Cpu68k.cpp`
- **.latchDb()** (5 connections) — `src/core/Cpu68k.cpp`
- *... and 20 more nodes in this community*

## Relationships

- [Cpu68k.cpp](Cpu68k.cpp.md) (17 shared connections)
- [Moira](Moira.md) (8 shared connections)
- [CpuState](CpuState.md) (3 shared connections)
- [Community 60](Community_60.md) (3 shared connections)
- [Community 203](Community_203.md) (3 shared connections)
- [Scheduler](Scheduler.md) (3 shared connections)
- [Community 36](Community_36.md) (2 shared connections)
- [GemdosHd](GemdosHd.md) (2 shared connections)
- [Community 333](Community_333.md) (2 shared connections)
- [Instr](Instr.md) (2 shared connections)
- [M68kException](M68kException.md) (2 shared connections)
- [Community 130](Community_130.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/PERFORMANCE.md`
- `extern/moira/Moira/Moira.h`
- `src/core/Cpu68k.cpp`
- `src/io/GemdosHd.hpp`

## Audit Trail

- EXTRACTED: 140 (75%)
- INFERRED: 46 (25%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*