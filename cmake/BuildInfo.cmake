# Identité de build gravée dans les binaires (cf. src/BuildInfo.hpp).
#
# Exécuté en mode script (`cmake -P`) À CHAQUE BUILD par la cible neost_build_info_gen,
# et une fois à la configuration. Il écrit ${OUT} (build_info.cpp) SEULEMENT si l'identité
# des sources a changé — sinon rien n'est recompilé ni relié.
#
# Pourquoi : un client du protocole `--server` (TOS File Cmd, 2026-09-23) a perdu du temps
# sur un `build/neost-headless` périmé — un banc échouait cinq fois de suite, un simple
# rebuild l'a « corrigé », et rien ne permettait de savoir quel NeoST il pilotait.
# `--version` et la réponse à `hello` annoncent donc :
#   commit : hash court de HEAD, suffixé « +xxxxxxxx » (SHA-1 de `git diff HEAD` et de la
#            liste des fichiers non suivis) si l'arbre de travail diffère de HEAD ;
#            « nogit » hors dépôt (archive, paquet).
#   built  : horodatage ISO 8601 SANS espace (les clients tokenisent `hello` sur les
#            espaces), pris au moment où cette identité a changé pour la dernière fois.
#            SOURCE_DATE_EPOCH est honoré (builds reproductibles).
# Un banc peut ainsi comparer `commit` à `git rev-parse --short=12 HEAD` et refuser un
# binaire plus vieux que les sources.
#
# Variables attendues : SRC_DIR (racine du dépôt), OUT (fichier .cpp à produire).

set(commit "nogit")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${SRC_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SRC_DIR}" rev-parse --short=12 HEAD
                    OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE rc ERROR_QUIET)
    if(rc EQUAL 0 AND head)
        set(commit "${head}")
        # Modifications non commitées : le diff par rapport à HEAD (contenu) + les fichiers
        # non suivis (noms). Deux arbres de travail différents donnent deux suffixes.
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SRC_DIR}" diff HEAD --no-color
                        OUTPUT_VARIABLE diff ERROR_QUIET)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SRC_DIR}" ls-files --others --exclude-standard
                        OUTPUT_VARIABLE untracked ERROR_QUIET)
        if(diff OR untracked)
            string(SHA1 dirty "${diff}\n--\n${untracked}")
            string(SUBSTRING "${dirty}" 0 8 dirty)
            set(commit "${commit}+${dirty}")
        endif()
    endif()
endif()

# Horodatage : seulement si l'identité a changé (sinon un rebuild sans modification
# relierait tout pour rien). On relit donc l'ancien fichier.
set(old_commit "")
set(old_date "")
if(EXISTS "${OUT}")
    file(STRINGS "${OUT}" old_lines REGEX "^const char\\* const neost::build::k")
    foreach(l IN LISTS old_lines)
        if(l MATCHES "kCommit *= *\"([^\"]*)\"")
            set(old_commit "${CMAKE_MATCH_1}")
        elseif(l MATCHES "kDate *= *\"([^\"]*)\"")
            set(old_date "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()
if(old_commit STREQUAL commit AND old_date)
    return()
endif()

string(TIMESTAMP date "%Y-%m-%dT%H:%M:%S")
file(WRITE "${OUT}"
"// GÉNÉRÉ par cmake/BuildInfo.cmake à chaque build — ne pas éditer, ne pas versionner.
#include \"BuildInfo.hpp\"
const char* const neost::build::kCommit = \"${commit}\";
const char* const neost::build::kDate   = \"${date}\";
")
message(STATUS "NeoST: build identity ${commit} (${date})")
