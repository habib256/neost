// BuildInfo.hpp — identité de build gravée dans les binaires.
//
// Définitions dans build_info.cpp, GÉNÉRÉ par cmake/BuildInfo.cmake à chaque build (le
// pourquoi et le format y sont documentés). Exposé par `--version` (neost, neost-headless)
// et par la réponse à `hello` du protocole serveur (docs/OPENDST.md § 5) : un banc externe
// peut refuser un binaire plus vieux que ses sources.
#pragma once

namespace neost::build {
extern const char* const kCommit;   // « 8ee8de0abcd1 », « 8ee8de0abcd1+1f2e3d4c » (arbre modifié), « nogit »
extern const char* const kDate;     // ISO 8601 sans espace : « 2026-09-23T14:05:11 »
}
