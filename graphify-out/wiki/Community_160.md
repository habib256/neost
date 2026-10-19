# Community 160

> 17 nodes · cohesion 0.16

## Key Concepts

- **.updateGlueState()** (10 connections) — `src/core/Shifter.hpp`
- **lineLenAttrEnv()** (9 connections) — `src/core/ShifterInternal.hpp`
- **envFlag()** (7 connections) — `src/core/ShifterInternal.hpp`
- **Shifter::liveGlueCatchUp()** (7 connections) — `src/core/VideoGlue.cpp`
- **Shifter::replayGlue()** (7 connections) — `src/core/VideoGlue.cpp`
- **glueLineLenFor()** (6 connections) — `src/core/VideoGlue.cpp`
- **A32 — le Shifter perd 1 586 lignes, le GLUE vidéo retrouve son nom (2026-08-28)** (6 connections) — `CHANGELOG.md`
- **.restartVideoCounter()** (5 connections) — `src/core/Shifter.hpp`
- **Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)** (5 connections) — `CHANGELOG.md`
- **Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`** (5 connections) — `docs/HATARI_DIVERGENCES.md`
- **.lineLenEnv()** (4 connections) — `src/core/Shifter.hpp`
- **.recordSyncWrite()** (4 connections) — `src/core/Shifter.hpp`
- **Shifter::timerBFrameCycleForLine()** (4 connections) — `src/core/VideoGlue.cpp`
- **.timerBPosForLine()** (2 connections) — `src/core/Shifter.hpp`
- **lineGeom_** (2 connections) — `src/core/Shifter.hpp`
- **V3 est CLOS : l'attribution de ligne à la grille réelle devient le défaut — et le verrou ne faisait rien (2026-09-01)** (2 connections) — `CHANGELOG.md`
- **8. 🎯 PLAN D'ATTAQUE COORDONNÉ (refonte beam-sync) — pour la prochaine passe** (2 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`

## Relationships

- [Community 256](Community_256.md) (8 shared connections)
- [Shifter](Shifter.md) (6 shared connections)
- [Community 204](Community_204.md) (6 shared connections)
- [Community 165](Community_165.md) (6 shared connections)
- [Mfp.cpp](Mfp.cpp.md) (3 shared connections)
- [Community 130](Community_130.md) (3 shared connections)
- [.gpipSetLine](gpipSetLine.md) (2 shared connections)
- [Community 93](Community_93.md) (1 shared connections)
- [Community 67](Community_67.md) (1 shared connections)
- [Community 317](Community_317.md) (1 shared connections)
- [Community 208](Community_208.md) (1 shared connections)
- [Scheduler](Scheduler.md) (1 shared connections)

## Source Files

- `CHANGELOG.md`
- `docs/HATARI_DIVERGENCES.md`
- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `src/core/Shifter.hpp`
- `src/core/ShifterInternal.hpp`
- `src/core/VideoGlue.cpp`

## Audit Trail

- EXTRACTED: 22 (34%)
- INFERRED: 43 (66%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*