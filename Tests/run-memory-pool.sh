#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-memory-pool.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
"${CXX:-g++}" -std=c++17 -O2 -g -Wall -Wextra -Wpedantic \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -IANTAGONIZ/Header/Miscellaneous Tests/MemoryPool.cpp -o "$endurance_test_binary"
"$endurance_test_binary"
