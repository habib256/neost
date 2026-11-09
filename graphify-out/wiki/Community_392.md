# Community 392

> 7 nodes

## Key Concepts

- **Périphériques des ports — un par port : clés joystick / série, DAC Pro Sound, boutons (`--plug`, GUI Dongles)** (7 connections) — `docs/EXTENSIONS.md`
- **YM2149::synthesizeFrame()** (5 connections) — `src/core/YM2149.cpp`
- **.setPortBDac()** (4 connections) — `src/core/YM2149.hpp`
- **.setGpipReadHook()** (4 connections) — `src/io/Mfp.hpp`
- **.dacRaw()** (3 connections) — `src/core/YM2149.hpp`
- **Page « Dongles » et adaptateurs de port : `PortDongle` (2026-08-23)** (3 connections) — `CHANGELOG.md`
- **Inventaire des dongles ST connus (recherche 2026-08-23) et pourquoi le reste n'est pas émulé** (1 connections) — `docs/EXTENSIONS.md`

## Relationships

- [Community 28](Community_28.md) (3 shared connections)
- [Community 6](Community_6.md) (2 shared connections)
- [Community 51](Community_51.md) (2 shared connections)
- [Community 140](Community_140.md) (1 shared connections)
- [Community 352](Community_352.md) (1 shared connections)
- [Community 48](Community_48.md) (1 shared connections)
- [Community 130](Community_130.md) (1 shared connections)
- [Community 268](Community_268.md) (1 shared connections)
- [Community 7](Community_7.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/EXTENSIONS.md`
- `src/core/YM2149.cpp`
- `src/core/YM2149.hpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 10 (50%)
- INFERRED: 10 (50%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*