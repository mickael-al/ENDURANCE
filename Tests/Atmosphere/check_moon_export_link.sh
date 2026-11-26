#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cxx=${CXX:-c++}
# Optional alternate ThirdParty root reproduces the bug against an older header.
stb_root=${STB_WRITE_INCLUDE_ROOT:-"$repo/ThirdParty"}
for optimization in 0 2; do
    "$cxx" -std=c++17 "-O$optimization" \
        -I "$stb_root" -I "$repo/ThirdParty" -I "$repo/ANTAGONIZ/Header/ProjectUser" \
        "$repo/Tests/Atmosphere/check_moon_export_link.cpp" \
        "$repo/ANTAGONIZ/Source/ProjectUser/ProceduralMoonExport.cpp" \
        "$repo/ANTAGONIZ/Source/ProjectUser/ProceduralMoon.cpp" \
        "$repo/ANTAGONIZ/Source/ProjectUser/ProceduralMoonGeometry.cpp" \
        -o "$build/check_moon_export_link"
    "$build/check_moon_export_link" "$build/moon.glb"
done
