# Échoue si le bundle web lie un stub Emscripten « not implemented » : ces
# fonctions se lient sans erreur mais font abort() au premier appel (cf. la
# garde POST_BUILD de neost-web dans CMakeLists.txt).
file(READ "${JS}" content)
string(REGEX MATCHALL "abort\\([\"'][A-Za-z0-9_]+ (is )?not implemented\\.?[\"']\\)" stubs "${content}")
if(stubs)
    list(REMOVE_DUPLICATES stubs)
    string(REPLACE ";" "\n  " stubs "${stubs}")
    message(FATAL_ERROR
        "WASM : stubs Emscripten non implémentés liés dans ${JS} :\n  ${stubs}\n"
        "Chaque appel ferait abort() dans le navigateur — exclure l'appel sous "
        "__EMSCRIPTEN__.")
endif()
