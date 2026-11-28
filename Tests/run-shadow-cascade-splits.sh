#!/usr/bin/env bash
set -euo pipefail
endurance_test_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
endurance_test_binary="$(mktemp "${TMPDIR:-/tmp}/endurance-shadow-cascade.XXXXXX")"
trap 'rm -f -- "$endurance_test_binary"' EXIT
cd -- "$endurance_test_root"
for endurance_math_flags in -fno-fast-math -ffast-math; do
    "${CXX:-g++}" -std=c++17 -O2 "$endurance_math_flags" -Wall -Wextra -Wpedantic \
        -IANTAGONIZ/Header/Rendering/Light Tests/ShadowCascadeSplits.cpp -o "$endurance_test_binary"
    "$endurance_test_binary"
done
