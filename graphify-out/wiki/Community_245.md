# Community 245

> 8 nodes · cohesion 0.25

## Key Concepts

- **Détail par sous-système** (13 connections) — `docs/HATARI_DIVERGENCES.md`
- **ACIA 6850 / IKBD HD6301 / MIDI — `Ikbd.cpp`, `MidiAcia.cpp` ↔ `acia.c`, `ikbd.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **SCC Z85C30 (MegaSTE) — `Scc.cpp` ↔ `scc.c`** (2 connections) — `docs/HATARI_DIVERGENCES.md`
- **Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **CPU (Moira + intégration) — suivi (audit vendor 2026-08-13)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **FDC — phase INDEX des médias (actualisé le 2026-10-18)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **GEMDOS HD — sauvegardes (2026-10-19)** (1 connections) — `docs/HATARI_DIVERGENCES.md`
- **USART MFP — réception hôte** (1 connections) — `docs/HATARI_DIVERGENCES.md`

## Relationships

- [Scheduler](Scheduler.md) (1 shared connections)
- [Community 105](Community_105.md) (1 shared connections)
- [.gpipSetLine](gpipSetLine.md) (1 shared connections)
- [Community 160](Community_160.md) (1 shared connections)
- [Community 31](Community_31.md) (1 shared connections)
- [YM2149.cpp](YM2149.cpp.md) (1 shared connections)
- [Community 249](Community_249.md) (1 shared connections)
- [Community 19](Community_19.md) (1 shared connections)

## Source Files

- `docs/HATARI_DIVERGENCES.md`

## Audit Trail

- EXTRACTED: 13 (87%)
- INFERRED: 2 (13%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*