#!/usr/bin/env bash
set -euo pipefail

engine_input_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
engine_input_tmp="$(mktemp -d "${TMPDIR:-/tmp}/antagoniz-editor-input.XXXXXX")"
trap 'rm -rf -- "$engine_input_tmp"' EXIT
engine_input_imgui="${IMGUI_ROOT:-$engine_input_root/ThirdParty/imgui-cmake}"
cd -- "$engine_input_root"

if [[ ! -f "$engine_input_imgui/Source/imgui.cpp" ]]; then
    printf 'Dear ImGui sources missing: %s\n' "$engine_input_imgui" >&2
    exit 1
fi

"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -IANTAGONIZ/Header/Miscellaneous \
    "-I$engine_input_imgui/Header" "-I$(dirname -- "$engine_input_imgui")" \
    Tests/EditorInput.cpp \
    "$engine_input_imgui/Source/imgui.cpp" \
    "$engine_input_imgui/Source/imgui_draw.cpp" \
    "$engine_input_imgui/Source/imgui_tables.cpp" \
    "$engine_input_imgui/Source/imgui_widgets.cpp" \
    -o "$engine_input_tmp/check"
"$engine_input_tmp/check"
