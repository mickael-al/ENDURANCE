#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-character-import.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
"${CXX:-g++}" -std=c++17 -O2 -ffunction-sections -fdata-sections \
    -fno-fast-math -DGLEW_NO_GLU -DGLFW_INCLUDE_NONE -DCHARACTER_IMPORT_STANDALONE \
    -IANTAGONIZ/Header/Rendering/Shape -IANTAGONIZ/Header/Rendering/Shader \
    -IANTAGONIZ/Header/Rendering/Materials -IANTAGONIZ/Header/Interface \
    -IANTAGONIZ/Header/Struct -IANTAGONIZ/Header/Miscellaneous -IANTAGONIZ/Header/Debug \
    -IThirdParty -IThirdParty/glm -IThirdParty/glfw/include \
    Tests/CharacterImport.cpp ANTAGONIZ/Source/Rendering/Shape/ModelManager.cpp \
    ThirdParty/OpenFBX/src/ofbx.cpp ThirdParty/OpenFBX/src/miniz.c \
    -Wl,--gc-sections -o "$endurance_test_binary"
"$endurance_test_binary"
