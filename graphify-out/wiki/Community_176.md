# Community 176

> 24 nodes

## Key Concepts

- **Bus error & cartouches de diagnostic** (34 connections) — `docs/IMPLEMENTED.md`
- **Détail par sous-système** (15 connections) — `docs/HATARI_DIVERGENCES.md`
- **make_vblank_test.py** (8 connections) — `tools/make_vblank_test.py`
- **w()** (5 connections) — `tools/make_vblank_test.py`
- **bra()** (4 connections) — `tools/make_vblank_test.py`
- **delay()** (4 connections) — `tools/make_vblank_test.py`
- **Machine::beginFrame_()** (3 connections) — `src/core/Machine.cpp`
- **.syncState()** (3 connections) — `src/io/Scu.hpp`
- **relative()** (3 connections) — `tools/make_vblank_test.py`
- **.setStfLowPass()** (2 connections) — `src/core/YM2149.hpp`
- **.serialBaud()** (2 connections) — `src/io/Mfp.hpp`
- **.serialUcr()** (2 connections) — `src/io/Mfp.hpp`
- **.readAnalog()** (2 connections) — `src/io/StePads.hpp`
- **label()** (2 connections) — `tools/make_vblank_test.py`
- **ACIA 6850 / IKBD HD6301 / MIDI — `Ikbd.cpp`, `MidiAcia.cpp` ↔ `acia.c`, `ikbd.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **MFP 68901 — `Mfp.cpp` ↔ `mfp.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **SCC Z85C30 (MegaSTE) — `Scc.cpp` ↔ `scc.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **Son (YM2149 + DMA STE) — `YM2149.cpp`, `DmaSound.cpp` ↔ `psg.c`, `dmaSnd.c`, `sound.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **CPU (Moira + intégration) — suivi (audit vendor 2026-08-13)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **FDC — phase INDEX des médias (actualisé le 2026-10-18)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **GEMDOS HD — autostart `--auto` (2026-10-24)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **GEMDOS HD — sauvegardes (2026-10-19)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **USART MFP — réception hôte** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **Disquette libre : retrait bas PAL et VBlank, ancrés par VBL/STOP. on :…** (1 connections) — `tools/make_vblank_test.py`

## Relationships

- [Community 37](Community_37.md) (4 shared connections)
- [Community 6](Community_6.md) (2 shared connections)
- [Community 106](Community_106.md) (2 shared connections)
- [Community 3](Community_3.md) (2 shared connections)
- [Community 120](Community_120.md) (2 shared connections)
- [Community 27](Community_27.md) (2 shared connections)
- [Community 245](Community_245.md) (2 shared connections)
- [Community 19](Community_19.md) (2 shared connections)
- [Community 182](Community_182.md) (1 shared connections)
- [Community 10](Community_10.md) (1 shared connections)
- [Community 28](Community_28.md) (1 shared connections)
- [Community 175](Community_175.md) (1 shared connections)

## Source Files

- `docs/HATARI_DIVERGENCES.md`
- `docs/IMPLEMENTED.md`
- `src/core/Machine.cpp`
- `src/core/YM2149.hpp`
- `src/io/Mfp.hpp`
- `src/io/Scu.hpp`
- `src/io/StePads.hpp`
- `tools/make_vblank_test.py`

## Audit Trail

- EXTRACTED: 37 (49%)
- INFERRED: 38 (51%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*