#!/usr/bin/env bash
# =============================================================================
#  build_apk.sh — Construit le paquet Android de NeoST (arm64-v8a).
#
#  Enchaîne les trois étapes qu'on oublie une fois sur deux : récupérer SDL2,
#  copier les données redistribuables dans les assets, puis lancer Gradle.
#
#  Prérequis (non installés par ce script) :
#    · un SDK Android avec NDK 27 et CMake 3.22 — `sdkmanager --install
#      "platforms;android-34" "build-tools;34.0.0" "ndk;27.2.12479018" "cmake;3.22.1"` ;
#    · un JDK 17 ou 21 COMPLET. ⚠ Le paquet `openjdk-21-jre-headless` d'Ubuntu
#      n'a PAS `jlink`, dont le plugin Android a besoin pour construire ses
#      « system modules » : le build échoue alors sur un message qui ne dit pas
#      qu'il manque un paquet. Un JDK Temurin déballé dans $HOME suffit.
#
#  Variables : ANDROID_HOME (obligatoire), JAVA_HOME (sinon JDK par défaut).
#  Argument  : « debug » (défaut, signé avec la clé de debug → installable) ou
#              « release » (NON signé — cf. § Signature du README).
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"
VARIANT="${1:-debug}"

if [ -z "${ANDROID_HOME:-}" ]; then
    echo "ERREUR : ANDROID_HOME n'est pas défini (chemin du SDK Android)." >&2
    exit 1
fi

./fetch_sdl.sh
./stage_assets.sh

GRADLE_ARGS=()
[ -n "${JAVA_HOME:-}" ] && GRADLE_ARGS+=("-Dorg.gradle.java.home=$JAVA_HOME")

case "$VARIANT" in
    debug)   ./gradlew "${GRADLE_ARGS[@]}" assembleDebug ;;
    release) ./gradlew "${GRADLE_ARGS[@]}" assembleRelease ;;
    *) echo "usage: $0 [debug|release]" >&2; exit 1 ;;
esac

APK=$(find app/build/outputs/apk -name '*.apk' -newer gradlew | head -1)
[ -z "$APK" ] && APK=$(find app/build/outputs/apk -name '*.apk' | head -1)

# Garde-fou : un APK sans sa bibliothèque native ou sans EmuTOS se lance et
# meurt à l'écran noir, sans rien dire. On le vérifie ici, pas chez l'utilisateur.
# La liste est lue UNE fois : `unzip -l | grep -q` sous `set -o pipefail` échouait
# quand grep trouvait tôt et sortait — unzip, encore en train d'écrire, prenait
# SIGPIPE et le tube rendait faux (vu le 2026-11-12 : « libneost.so absent » d'un APK
# qui le contenait).
LISTING=$(unzip -l "$APK")
for entry in lib/arm64-v8a/libneost.so lib/arm64-v8a/libSDL2.so assets/etos192us.img; do
    if ! grep -q "$entry" <<<"$LISTING"; then
        echo "ERREUR : $entry absent de $APK" >&2
        exit 1
    fi
done

echo "OK : $APK ($(du -h "$APK" | cut -f1))"
