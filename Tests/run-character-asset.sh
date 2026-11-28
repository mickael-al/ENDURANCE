#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-character-asset.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
"${CXX:-g++}" -std=c++17 -O2 -ffast-math -pthread \
    -IANTAGONIZ/Header/ProjectUser -IANTAGONIZ/Header/Struct -IThirdParty/glm -IThirdParty/OpenFBX/src \
    Tests/CharacterAsset.cpp ANTAGONIZ/Source/ProjectUser/EnduranceCharacterAsset.cpp \
    ThirdParty/OpenFBX/src/ofbx.cpp ThirdParty/OpenFBX/src/miniz.c \
    -o "$endurance_test_binary"
"$endurance_test_binary" ENDURANCE/Ressources/Model/Character/Endurance.fbx
