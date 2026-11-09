# Community 175

> 25 nodes

## Key Concepts

- **Scu** (17 connections) — `src/io/Scu.hpp`
- **Détail par sous-système** (15 connections) — `docs/HATARI_DIVERGENCES.md`
- **.syncState()** (3 connections) — `src/io/Scu.hpp`
- **.setStfLowPass()** (2 connections) — `src/core/YM2149.hpp`
- **ACIA 6850 / IKBD HD6301 / MIDI — `Ikbd.cpp`, `MidiAcia.cpp` ↔ `acia.c`, `ikbd.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **MFP 68901 — `Mfp.cpp` ↔ `mfp.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **SCC Z85C30 (MegaSTE) — `Scc.cpp` ↔ `scc.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **Son (YM2149 + DMA STE) — `YM2149.cpp`, `DmaSound.cpp` ↔ `psg.c`, `dmaSnd.c`, `sound.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **.gatedLevel()** (1 connections) — `src/io/Scu.hpp`
- **.read8()** (1 connections) — `src/io/Scu.hpp`
- **.reset()** (1 connections) — `src/io/Scu.hpp`
- **.write8()** (1 connections) — `src/io/Scu.hpp`
- **gpr1** (1 connections) — `src/io/Scu.hpp`
- **gpr2** (1 connections) — `src/io/Scu.hpp`
- **sysInterrupter** (1 connections) — `src/io/Scu.hpp`
- **sysIntMask** (1 connections) — `src/io/Scu.hpp`
- **sysIntState** (1 connections) — `src/io/Scu.hpp`
- **vmeInterrupter** (1 connections) — `src/io/Scu.hpp`
- **vmeIntMask** (1 connections) — `src/io/Scu.hpp`
- **vmeIntState** (1 connections) — `src/io/Scu.hpp`
- **CPU (Moira + intégration) — suivi (audit vendor 2026-08-13)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **FDC — phase INDEX des médias (actualisé le 2026-10-18)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **GEMDOS HD — autostart `--auto` (2026-10-24)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **GEMDOS HD — sauvegardes (2026-10-19)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **USART MFP — réception hôte** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Community 37](Community_37.md) (2 shared connections)
- [Community 14](Community_14.md) (1 shared connections)
- [Community 4](Community_4.md) (1 shared connections)
- [Community 389](Community_389.md) (1 shared connections)
- [Community 321](Community_321.md) (1 shared connections)
- [Community 28](Community_28.md) (1 shared connections)
- [Community 349](Community_349.md) (1 shared connections)
- [Community 105](Community_105.md) (1 shared connections)
- [Community 205](Community_205.md) (1 shared connections)
- [Community 267](Community_267.md) (1 shared connections)
- [Community 36](Community_36.md) (1 shared connections)
- [Community 386](Community_386.md) (1 shared connections)

## Source Files

- `docs/HATARI_DIVERGENCES.md`
- `src/core/YM2149.hpp`
- `src/io/Scu.hpp`

## Audit Trail

- EXTRACTED: 31 (82%)
- INFERRED: 7 (18%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*