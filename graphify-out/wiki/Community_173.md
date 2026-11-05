# Community 173

> 41 nodes

## Key Concepts

- **.gpipSetLine()** (21 connections) — `src/io/Mfp.hpp`
- **Priorités — divergences HAUTE et MOYENNE** (20 connections) — `docs/HATARI_DIVERGENCES.md`
- **Divergences OUVERTES à jour (par sous-système, sévérité décroissante)** (18 connections) — `docs/HATARI_DIVERGENCES.md`
- **Détail par sous-système** (15 connections) — `docs/HATARI_DIVERGENCES.md`
- **.updateGlueState()** (10 connections) — `src/core/Shifter.hpp`
- **Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`** (10 connections) — `docs/HATARI_DIVERGENCES.md`
- **🔬 5ᵉ passe — rafraîchissement complet des statuts (2026-07-07, 4 agents)** (9 connections) — `docs/HATARI_DIVERGENCES.md`
- **Statuts corrigés par cette passe (le détail ↑ dans les sections historiques est PÉRIMÉ)** (9 connections) — `docs/HATARI_DIVERGENCES.md`
- **.gpipUpdateInterrupt()** (8 connections) — `src/io/Mfp.hpp`
- **.updateGlueRes()** (7 connections) — `src/core/Shifter.hpp`
- **.mfpSelfTest()** (6 connections) — `src/io/Mfp.hpp`
- **MFP 68901 — `Mfp.cpp` ↔ `mfp.c`** (6 connections) — `docs/HATARI_DIVERGENCES.md`
- **.hblankAt()** (5 connections) — `src/io/Mfp.hpp`
- **.restartVideoCounter()** (5 connections) — `src/core/Shifter.hpp`
- **Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)** (5 connections) — `CHANGELOG.md`
- **.recordSyncWrite()** (4 connections) — `src/core/Shifter.hpp`
- **.setXsintLine()** (3 connections) — `src/io/Mfp.hpp`
- **.hblank()** (3 connections) — `src/io/Mfp.hpp`
- **.setStfLowPass()** (3 connections) — `src/core/YM2149.hpp`
- **.setBlitterLine()** (3 connections) — `src/io/Mfp.hpp`
- **.setFdcLine()** (3 connections) — `src/io/Mfp.hpp`
- **.setMonitorButton()** (3 connections) — `src/io/Mfp.hpp`
- **Son (YM2149 + DMA STE) — `YM2149.cpp`, `DmaSound.cpp` ↔ `psg.c`, `dmaSnd.c`, `sound.c`** (3 connections) — `docs/HATARI_DIVERGENCES.md`
- **.firingDue()** (2 connections) — `src/core/Scheduler.hpp`
- **.setAciaLineMidi()** (2 connections) — `src/io/Mfp.hpp`
- *... and 16 more nodes in this community*

## Relationships

- [Community 215](Community_215.md) (17 shared connections)
- [Community 6](Community_6.md) (16 shared connections)
- [Community 39](Community_39.md) (8 shared connections)
- [Community 88](Community_88.md) (7 shared connections)
- [Community 5](Community_5.md) (5 shared connections)
- [Community 288](Community_288.md) (5 shared connections)
- [Community 13](Community_13.md) (5 shared connections)
- [Community 63](Community_63.md) (4 shared connections)
- [Community 105](Community_105.md) (3 shared connections)
- [Community 22](Community_22.md) (2 shared connections)
- [Community 130](Community_130.md) (2 shared connections)
- [Community 10](Community_10.md) (2 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `src/core/Scheduler.hpp`
- `src/core/Shifter.hpp`
- `src/core/YM2149.hpp`
- `src/io/Mfp.hpp`

## Audit Trail

- EXTRACTED: 61 (40%)
- INFERRED: 91 (60%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*