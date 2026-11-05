# Community 333

> 27 nodes

## Key Concepts

- **Moira — copie vendorisée (fork NeoST)** (18 connections) — `extern/moira/NEOST_VENDOR.md`
- **.processException()** (8 connections) — `extern/moira/Moira/Moira.h`
- **.setIPL()** (8 connections) — `extern/moira/Moira/Moira.h`
- **.halt()** (6 connections) — `extern/moira/Moira/Moira.h`
- **.checkForIrq()** (5 connections) — `extern/moira/Moira/Moira.h`
- **2. Carte des divergences (source-grounded, 3 agents — file:line des DEUX côtés)** (5 connections) — `docs/MOIRA_WINUAE_CONVERGENCE.md`
- **Patch local : `NEOST_IPLFETCH` (reconnaissance IPL différée)** (5 connections) — `extern/moira/NEOST_VENDOR.md`
- **.pollIpl()** (4 connections) — `extern/moira/Moira/Moira.h`
- **.willProcessFault()** (3 connections) — `extern/moira/Moira/Moira.h`
- **Patch local : reset gardé (2026-07-08)** (3 connections) — `extern/moira/NEOST_VENDOR.md`
- **cpuDidHalt()** (2 connections) — `extern/moira/Moira/Moira.h`
- **.sync()** (2 connections) — `tools/moira_bare_bench.cpp`
- **Patch local : STOP+8 — retenue d'IRQ de 4 cycles après STOP (2026-10-22)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **Patch local : STOP niveau-sensible (2026-07-03)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **Pin d'origine — retrouvé le 2026-08-28 (chantier A35)** (2 connections) — `extern/moira/NEOST_VENDOR.md`
- **NEOST_VENDOR.md** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **ANDI/ORI/EORI #,SR/#,CCR et MOVE to CCR : double rechargement (2026-10-20)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **Bornes bus et trace d'instruction (2026-10-19)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **Contenu conservé** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **DIVS : valeurs absolues sans comportement indéfini (2026-10-20)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **DIVU/DIVS : cycles internes avant le préfetch (2026-10-20)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **Ordre des accès, d'après le banc fx68k (2026-11-03)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **Patch local : diagnostic d'exception (`NEOST_EXC_DIAG`)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **Patch local : hooks d'IACK (`iackSyncBefore` / `iackSyncAfter`)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- **Patch local : instruction illégale délégable (`illegalOpcodeHook`) (2026-10-01)** (1 connections) — `extern/moira/NEOST_VENDOR.md`
- *... and 2 more nodes in this community*

## Relationships

- [Community 40](Community_40.md) (6 shared connections)
- [Community 16](Community_16.md) (6 shared connections)
- [Community 99](Community_99.md) (4 shared connections)
- [Community 60](Community_60.md) (2 shared connections)
- [Community 64](Community_64.md) (2 shared connections)
- [Community 314](Community_314.md) (2 shared connections)
- [Community 169](Community_169.md) (1 shared connections)
- [Community 288](Community_288.md) (1 shared connections)
- [Community 248](Community_248.md) (1 shared connections)
- [Community 112](Community_112.md) (1 shared connections)
- [Community 197](Community_197.md) (1 shared connections)

## Source Files

- `docs/MOIRA_WINUAE_CONVERGENCE.md`
- `extern/moira/Moira/Moira.h`
- `extern/moira/NEOST_VENDOR.md`
- `tools/moira_bare_bench.cpp`

## Audit Trail

- EXTRACTED: 35 (61%)
- INFERRED: 22 (39%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*