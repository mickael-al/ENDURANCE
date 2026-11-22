#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/endurance-moon-geometry.XXXXXXXX")
trap 'rm -rf -- "$temporary"' EXIT
cxx=${CXX:-c++}
for mode in standard fast-math; do
    flags=(-std=c++17 -O2 -Wall -Wextra -pthread)
    if [[ "$mode" == fast-math ]]; then flags+=(-ffast-math); fi
    "$cxx" "${flags[@]}" -I "$root/ANTAGONIZ/Header/ProjectUser" \
        "$root/Tests/Atmosphere/check_moon_geometry.cpp" \
        "$root/ANTAGONIZ/Source/ProjectUser/ProceduralMoonGeometry.cpp" \
        -o "$temporary/check_moon_geometry"
    printf 'Native excavated Moon geometry (%s)\n' "$mode"
    "$temporary/check_moon_geometry"
done
