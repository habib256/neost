# make_hd_image.py

> 20 nodes · cohesion 0.18

## Key Concepts

- **make_hd_image.py** (18 connections) — `tools/make_hd_image.py`
- **main()** (7 connections) — `tools/make_hd_image.py`
- **probe_donor()** (6 connections) — `tools/make_hd_image.py`
- **run()** (6 connections) — `tools/make_hd_image.py`
- **root_sector()** (5 connections) — `tools/make_hd_image.py`
- **find_donor()** (4 connections) — `tools/make_hd_image.py`
- **part_type()** (4 connections) — `tools/make_hd_image.py`
- **run_batched()** (4 connections) — `tools/make_hd_image.py`
- **word_sum()** (4 connections) — `tools/make_hd_image.py`
- **_cmd_str()** (3 connections) — `tools/make_hd_image.py`
- **first_partition_offset()** (3 connections) — `tools/make_hd_image.py`
- **_discard_partial()** (2 connections) — `tools/make_hd_image.py`
- **Ligne de commande lisible : mcopy en reçoit des milliers, l'afficher en entier…** (1 connections) — `tools/make_hd_image.py`
- **Exécute `prefix + lot + suffix` en autant de lots qu'il faut.** (1 connections) — `tools/make_hd_image.py`
- **GEM plafonne à 32 Mo (65536 secteurs) ; au-delà le TOS attend BGM.** (1 connections) — `tools/make_hd_image.py`
- **Secteur racine AHDI. Avec `code`, il devient AMORÇABLE (somme = $1234).** (1 connections) — `tools/make_hd_image.py`
- **Offset en octets de la 1re partition d'un disque Atari, ou None.** (1 connections) — `tools/make_hd_image.py`
- **(code d'amorçage, nom du pilote, offset partition) si `disk` est amorçable.** (1 connections) — `tools/make_hd_image.py`
- **Donneur explicite, sinon premier disque amorçable trouvé dans hd/.** (1 connections) — `tools/make_hd_image.py`
- **glob** (1 connections)

## Relationships

- [subprocess](subprocess.md) (4 shared connections)
- [struct](struct.md) (1 shared connections)
- [Community 267](Community_267.md) (1 shared connections)

## Source Files

- `tools/make_hd_image.py`

## Audit Trail

- EXTRACTED: 40 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*