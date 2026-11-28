#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-character-pose.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
"${CXX:-g++}" -std=c++17 -O2 -fexceptions -fno-fast-math -fno-math-errno \
    -Wall -Wextra -Wpedantic \
    -IANTAGONIZ/Header -IANTAGONIZ/Header/ProjectUser \
    -IANTAGONIZ/Header/Rendering/Shape -IANTAGONIZ/Header/Struct \
    -IANTAGONIZ/Header/Interface -IANTAGONIZ/Header/Miscellaneous -IThirdParty/glm \
    Tests/CharacterPose.cpp ANTAGONIZ/Source/ProjectUser/EnduranceCharacterPose.cpp \
    ANTAGONIZ/Source/Physics/ClothSimulation.cpp ANTAGONIZ/Source/Physics/PreparedClothContacts.cpp \
    -o "$endurance_test_binary"
"$endurance_test_binary"
