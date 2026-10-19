# Community 256

> 25 nodes · cohesion 0.14

## Key Concepts

- **Vidéo (Shifter)** (23 connections) — `docs/IMPLEMENTED.md`
- **Shifter::glueSelfTest()** (21 connections) — `src/core/VideoGlue.cpp`
- **.finishFrame()** (11 connections) — `src/core/Shifter.hpp`
- **.liveGlueCatchUp()** (11 connections) — `src/core/Shifter.hpp`
- **.glueLineBytes()** (9 connections) — `src/core/Shifter.hpp`
- **.videoCounter()** (9 connections) — `src/core/Shifter.hpp`
- **.endVideoLine()** (8 connections) — `src/core/Shifter.hpp`
- **7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)** (7 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **.startHBL()** (6 connections) — `src/core/Shifter.hpp`
- **Shifter::endVideoLine()** (6 connections) — `src/core/VideoCounter.cpp`
- **.commitAnchor()** (5 connections) — `src/core/Shifter.hpp`
- **.replayGlue()** (5 connections) — `src/core/Shifter.hpp`
- **.vblankAtLine()** (5 connections) — `src/core/Shifter.hpp`
- **A16b — le segfault du chantier V3 : un invariant rompu par `replayGlue`, pas un défaut du selftest (2026-08-28)** (5 connections) — `CHANGELOG.md`
- **2. Vidéo / Shifter / Glue (video.c, spec512.c, conv_st.c)** (5 connections) — `docs/HATARI_MAPPING.md`
- **.glueSelfTest()** (4 connections) — `src/core/Shifter.hpp`
- **.liveLineDisplayed()** (4 connections) — `src/core/Shifter.hpp`
- **Shifter::commitScanline()** (4 connections) — `src/core/VideoCounter.cpp`
- **P1 — Registres vidéo STE : 2 restes** (4 connections) — `docs/CYCLE_ACCURACY.md`
- **.commitScanline()** (3 connections) — `src/core/Shifter.hpp`
- **Shifter::liveLineDisplayed()** (3 connections) — `src/core/VideoGlue.cpp`
- **Shifter::timerBPosForLine()** (3 connections) — `src/core/VideoGlue.cpp`
- **.timerBLinePos()** (2 connections) — `src/core/Shifter.hpp`
- **Shifter::glueLineBytes()** (2 connections) — `src/core/VideoGlue.cpp`
- **(archive) Sous-problèmes d'entrée d'IRQ — désormais RAFFINEMENTS, plus des blocages** (1 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`

## Relationships

- [Community 67](Community_67.md) (21 shared connections)
- [Shifter](Shifter.md) (13 shared connections)
- [Community 204](Community_204.md) (11 shared connections)
- [Community 160](Community_160.md) (8 shared connections)
- [Community 165](Community_165.md) (6 shared connections)
- [Community 208](Community_208.md) (2 shared connections)
- [Community 211](Community_211.md) (2 shared connections)
- [Community 8](Community_8.md) (2 shared connections)
- [Cpu68k.cpp](Cpu68k.cpp.md) (2 shared connections)
- [Community 192](Community_192.md) (2 shared connections)
- [Community 318](Community_318.md) (1 shared connections)
- [Community 105](Community_105.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/CYCLE_ACCURACY.md`
- `docs/HATARI_MAPPING.md`
- `docs/IMPLEMENTED.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `src/core/Shifter.hpp`
- `src/core/VideoCounter.cpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 28 (23%)
- INFERRED: 96 (77%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*