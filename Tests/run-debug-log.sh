#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-debug-log.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
"${CXX:-g++}" -std=c++17 -O1 -g -Wall -Wextra -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ITests/DebugLogFakes -IANTAGONIZ/Header/Debug -IThirdParty/termcolor/include \
    Tests/DebugLog.cpp ANTAGONIZ/Source/Debug/Debug.cpp \
    -o "$endurance_test_binary"
# Keep termcolor in its normal non-terminal mode even when this script is
# launched from an interactive terminal. pipefail preserves sanitizer failures.
"$endurance_test_binary" 2>&1 | cat
