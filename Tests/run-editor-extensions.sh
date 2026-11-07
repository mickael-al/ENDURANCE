#!/usr/bin/env bash
set -euo pipefail

engine_extensions_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
engine_extensions_tmp="$(mktemp -d "${TMPDIR:-/tmp}/antagoniz-editor-extensions.XXXXXX")"
trap 'rm -rf -- "$engine_extensions_tmp"' EXIT
engine_extensions_imgui="${IMGUI_ROOT:-$engine_extensions_root/ThirdParty/imgui-cmake}"
cd -- "$engine_extensions_root"

if [[ ! -f "$engine_extensions_imgui/Source/imgui.cpp" ]]; then
    printf 'Dear ImGui sources missing: %s\n' "$engine_extensions_imgui" >&2
    exit 1
fi

engine_extensions_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic
    -DIMGUI_ENABLE_TEST_ENGINE -IANTAGONIZ/Header/Miscellaneous
    "-I$engine_extensions_imgui/Header" "-I$(dirname -- "$engine_extensions_imgui")")
"${CXX:-g++}" "${engine_extensions_flags[@]}" -DEDITOR_EXTENSIONS_SECOND_TU \
    -c Tests/EditorExtensions.cpp -o "$engine_extensions_tmp/second-tu.o"
"${CXX:-g++}" "${engine_extensions_flags[@]}" \
    Tests/EditorExtensions.cpp "$engine_extensions_tmp/second-tu.o" \
    ANTAGONIZ/Source/Miscellaneous/EditorExtension.cpp \
    "$engine_extensions_imgui/Source/imgui.cpp" \
    "$engine_extensions_imgui/Source/imgui_draw.cpp" \
    "$engine_extensions_imgui/Source/imgui_tables.cpp" \
    "$engine_extensions_imgui/Source/imgui_widgets.cpp" \
    -o "$engine_extensions_tmp/check"
"$engine_extensions_tmp/check"
