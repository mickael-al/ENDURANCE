#!/usr/bin/env bash
set -euo pipefail
endurance_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/endurance-ambience.XXXXXX")"
trap 'rm -rf -- "$endurance_test_dir"' EXIT
cd -- "$endurance_root"
"${CXX:-g++}" -std=c++17 -O2 -ffast-math -Wall -Wextra -Wpedantic -pthread \
    -ITests/AudioManagerFakes -IANTAGONIZ/Header/ProjectUser \
    -IANTAGONIZ/Header/Miscellaneous -IThirdParty/glm \
    ANTAGONIZ/Source/ProjectUser/AudioManager.cpp \
    ANTAGONIZ/Source/ProjectUser/EnduranceAmbience.cpp \
    Tests/Ambience/Playback.cpp -o "$endurance_test_dir/playback"
"$endurance_test_dir/playback"
"${CXX:-g++}" -std=c++17 -O2 -ffast-math -Wall -Wextra -Wpedantic \
    -IANTAGONIZ/Header/Sound ANTAGONIZ/Source/Sound/SoundPcm.cpp \
    Tests/Ambience/LoopSeam.cpp -o "$endurance_test_dir/seam"
"$endurance_test_dir/seam"
