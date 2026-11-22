#!/usr/bin/env bash
# Build and run the real CPU lunar baker without OpenGL, NumPy, or Pillow.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/endurance-moon-bake.XXXXXXXX")
trap 'rm -rf -- "$temporary"' EXIT
cxx=${CXX:-c++}
for mode in standard fast-math; do
    flags=(-std=c++17 -O2 -Wall -Wextra -pthread)
    if [[ "$mode" == fast-math ]]; then flags+=(-ffast-math); fi
    "$cxx" "${flags[@]}" \
        -I "$root/ANTAGONIZ/Header/ProjectUser" \
        "$root/Tests/Atmosphere/check_moon_bake.cpp" \
        "$root/ANTAGONIZ/Source/ProjectUser/ProceduralMoon.cpp" \
        -o "$temporary/check_moon_bake"
    printf 'Native lunar bake (%s)\n' "$mode"
    "$temporary/check_moon_bake" "$temporary/$mode"
done
