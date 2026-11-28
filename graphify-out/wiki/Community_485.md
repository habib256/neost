# Community 485

> 6 nodes

## Key Concepts

- **case_midi_hidden()** (5 connections) — `tools/run_libretro.py`
- **case_midi_out()** (5 connections) — `tools/run_libretro.py`
- **bconout_midi()** (4 connections) — `tools/run_libretro.py`
- **Bconout(3, b) : un octet sur le port MIDI par le BIOS.** (1 connections) — `tools/run_libretro.py`
- **MIDI OUT vers l'interface MIDI de RetroArch : note on envoyée par Bconout(3),…** (1 connections) — `tools/run_libretro.py`
- **Trames cachées de l'anticipation (son coupé) : rien ne part vers le MIDI.** (1 connections) — `tools/run_libretro.py`

## Relationships

- [Community 247](Community_247.md) (5 shared connections)
- [Community 124](Community_124.md) (2 shared connections)

## Source Files

- `tools/run_libretro.py`

## Audit Trail

- EXTRACTED: 12 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*