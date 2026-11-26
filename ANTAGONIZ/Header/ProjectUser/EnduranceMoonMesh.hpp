#pragma once

#include "MoonMeshData.hpp"
#include <string>

namespace EnduranceMoon
{
// Load an opaque, baked Moon Lab GLB into one indexed mesh and its sRGB atlas.
// Preserves radius-one coordinates, detached fragments, linear vertex colors
// and the exporter's image row order. No GL calls or image-global state changes.
// Unsupported glTF features fail explicitly; a failed load preserves output.
bool loadMoonMesh(const std::string& path, Ge::MoonMeshData& output, std::string& error);
}
