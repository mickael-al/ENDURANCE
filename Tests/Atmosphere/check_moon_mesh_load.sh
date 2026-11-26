#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cxx=${CXX:-c++}
for mode in standard fast; do
    flags=()
    if [[ "$mode" == fast ]]; then flags+=(-ffast-math); fi
    "$cxx" -std=c++17 -O2 "${flags[@]}" \
        -I "$repo/ThirdParty" -I "$repo/ThirdParty/glm" \
        -I "$repo/ANTAGONIZ/Header/ProjectUser" -I "$repo/ANTAGONIZ/Header/Rendering/Shader" \
        "$repo/Tests/Atmosphere/check_moon_mesh_load.cpp" \
        "$repo/ANTAGONIZ/Source/ProjectUser/EnduranceMoonMesh.cpp" \
        -o "$build/check_moon_mesh_load"
    "$build/check_moon_mesh_load" "$repo/ENDURANCE/Ressources/Model/Moon/Baked/moon.glb" "$build"
done
