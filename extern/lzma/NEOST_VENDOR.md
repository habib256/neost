# SDK LZMA — décodeur 7z vendorisé

Décodeur C des archives `.7z` tiré de **7-Zip 24.09** d'Igor Pavlov, **domaine public**
(`License.txt`, `lzma.txt` ; https://www.7-zip.org/sdk.html). Il sert `src/util/Archive.cpp`
(`neost::archive::Reader`) : le cœur libretro ouvre les `.7z` comme les `.zip`.

## Provenance

- Source : `https://github.com/ip7z/7zip/archive/refs/tags/24.09.tar.gz`
  (SHA-256 `e4757b307925227724a2044651193664ba3d04e9ac13b8e631f1667896014bbf`), dossier `C/`,
  plus `DOC/License.txt` et `DOC/lzma.txt`. Récupéré le 2026-11-26.
- Fichiers repris : ceux de `C/Util/7z/makefile.gcc` (le programme `7zdec`), sans
  `7zMain.c` ni `7zFile.c` (Archive.cpp lit l'archive en mémoire), sans l'assembleur
  optionnel `LzmaDecOpt`.
- **Aucune modification.** Un fichier modifié ici ajoute sa ligne à ce document.

## Compilation

`CMakeLists.txt` (`NEOST_LZMA_SOURCES`) compile ces fichiers dans `neost_core`, en C, avec
`Z7_PPMD_SUPPORT` (méthode PPMd, fréquente dans les `.7z` de texte) et `Z7_EXTRACT_ONLY`. Ils
héritent du code indépendant de la position et de la visibilité cachée du cœur libretro.
Méthodes lues (`7zDec.c`) : stocké, LZMA, LZMA2, PPMd, et les filtres BCJ, BCJ2, ARM, ARMT, ARM64, RISCV, PPC, SPARC, IA64, Delta.
Refusées : chiffrement AES, BZip2, Deflate (un `.7z` « deflate » est rare).

## Mettre à jour

Reprendre la même liste depuis le nouveau tag, recompiler, puis
`./build/neost-selftest` (cas « Archives .zip, .7z et .gz ») et
`python3 tools/run_libretro.py --only sevenzip_disk`.
