#!/usr/bin/env bash
set -euo pipefail

endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-audio.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"

"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
    -ITests/AudioManagerFakes -IANTAGONIZ/Header/ProjectUser -IThirdParty/glm \
    ANTAGONIZ/Source/ProjectUser/AudioManager.cpp Tests/AudioManagerLifecycle.cpp \
    -o "$endurance_test_binary"
"$endurance_test_binary"
