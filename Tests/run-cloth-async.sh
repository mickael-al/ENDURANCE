#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-cloth-async.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
"${CXX:-g++}" -std=c++17 -O2 -pthread -fexceptions -fno-fast-math -fno-math-errno \
    -Wall -Wextra -Wpedantic \
    -ITests/ClothAsyncFakes -IANTAGONIZ/Header/Rendering/Shape \
    -IANTAGONIZ/Header/Physics -IANTAGONIZ/Header/Struct \
    -IANTAGONIZ/Header -IThirdParty/glm \
    Tests/ClothAsync.cpp ANTAGONIZ/Source/Rendering/Shape/SkinnedCloth.cpp \
    ANTAGONIZ/Source/Physics/ClothSimulation.cpp \
    ANTAGONIZ/Source/Physics/PreparedClothContacts.cpp -o "$endurance_test_binary"
"$endurance_test_binary"
